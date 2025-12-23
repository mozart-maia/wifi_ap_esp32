#include "wifi_ap.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include <stdbool.h>
#include <ctype.h>


static const char *TAG = "WiFi_Prov";

/* FreeRTOS event group para sinalizar quando estamos conectados */
static EventGroupHandle_t s_wifi_event_group;

/* Bit para indicar conexão bem-sucedida */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
#define MAX_RETRY_ATTEMPTS 5

static httpd_handle_t server = NULL;
static bool wifi_connected = false;

/* Página HTML para configuração do Wi-Fi */
static const char *html_page = 
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>Configuração Wi-Fi ESP32</title>"
    "<style>"
    "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; display: flex; justify-content: center; align-items: center; }"
    ".container { background: white; padding: 30px; border-radius: 10px; box-shadow: 0 10px 25px rgba(0,0,0,0.2); max-width: 400px; width: 100%; }"
    "h1 { color: #333; text-align: center; margin-bottom: 30px; }"
    ".form-group { margin-bottom: 20px; }"
    "label { display: block; margin-bottom: 5px; color: #555; font-weight: bold; }"
    "input[type='text'], input[type='password'] { width: 100%; padding: 10px; border: 2px solid #ddd; border-radius: 5px; box-sizing: border-box; font-size: 16px; }"
    "input[type='text']:focus, input[type='password']:focus { border-color: #667eea; outline: none; }"
    "button { width: 100%; padding: 12px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; border: none; border-radius: 5px; font-size: 16px; font-weight: bold; cursor: pointer; transition: transform 0.2s; }"
    "button:hover { transform: translateY(-2px); }"
    "button:active { transform: translateY(0); }"
    ".info { text-align: center; margin-top: 20px; color: #666; font-size: 14px; }"
    "</style>"
    "</head>"
    "<body>"
    "<div class='container'>"
    "<h1>🔧 Configuração Wi-Fi</h1>"
    "<form action='/save' method='POST'>"
    "<div class='form-group'>"
    "<label for='ssid'>Nome da Rede (SSID):</label>"
    "<input type='text' id='ssid' name='ssid' required placeholder='Digite o nome da rede'>"
    "</div>"
    "<div class='form-group'>"
    "<label for='password'>Senha:</label>"
    "<input type='password' id='password' name='password' required placeholder='Digite a senha'>"
    "</div>"
    "<button type='submit'>Conectar</button>"
    "</form>"
    "<div class='info'>Configure sua ESP32 para conectar à sua rede Wi-Fi</div>"
    "</div>"
    "</body>"
    "</html>";

/* Página de sucesso */
static const char *success_page = 
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>Conectado!</title>"
    "<style>"
    "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #11998e 0%, #38ef7d 100%); min-height: 100vh; display: flex; justify-content: center; align-items: center; }"
    ".container { background: white; padding: 40px; border-radius: 10px; box-shadow: 0 10px 25px rgba(0,0,0,0.2); text-align: center; max-width: 400px; }"
    "h1 { color: #11998e; margin-bottom: 20px; }"
    ".icon { font-size: 64px; margin-bottom: 20px; }"
    "p { color: #555; font-size: 16px; line-height: 1.6; }"
    "</style>"
    "</head>"
    "<body>"
    "<div class='container'>"
    "<div class='icon'>✅</div>"
    "<h1>Conectado com Sucesso!</h1>"
    "<p>Sua ESP32 está se conectando ao Wi-Fi.<br>Você pode fechar esta página.</p>"
    "</div>"
    "</body>"
    "</html>";

/* Handler de eventos Wi-Fi */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY_ATTEMPTS) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Tentando reconectar ao Wi-Fi...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "Falha ao conectar");
        wifi_connected = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP obtido:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        wifi_connected = true;
    }
}

/* Handler para servir a página principal */
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, strlen(html_page));
    return ESP_OK;
}

/* Função auxiliar para decodificar URL */
static void url_decode(char *dst, const char *src)
{
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a')
                a -= 'a'-'A';
            if (a >= 'A')
                a -= ('A' - 10);
            else
                a -= '0';
            if (b >= 'a')
                b -= 'a'-'A';
            if (b >= 'A')
                b -= ('B' - 10);
            else
                b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

/* Handler para processar o formulário */
static esp_err_t save_handler(httpd_req_t *req)
{
    char buf[200];
    int ret, remaining = req->content_len;

    /* Recebe os dados POST */
    if (remaining > sizeof(buf) - 1) {
        httpd_resp_send_500(req);
        ESP_LOGI(TAG, "Erro ao receber dados de POST...");
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            ESP_LOGI(TAG, "Timeout ao receber dados de POST...");
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    /* Parse dos parâmetros ssid=xxx&password=yyy */
    char ssid[33] = {0};
    char password[65] = {0};
    
    char *ssid_start = strstr(buf, "ssid=");
    char *pass_start = strstr(buf, "password=");
    
    if (ssid_start && pass_start) {
        ssid_start += 5; // pula "ssid="
        char *ssid_end = strchr(ssid_start, '&');
        if (ssid_end) {
            int ssid_len = ssid_end - ssid_start;
            if (ssid_len < sizeof(ssid)) {
                strncpy(ssid, ssid_start, ssid_len);
                ssid[ssid_len] = '\0';
                url_decode(ssid, ssid);
            }
        }
        
        pass_start += 9; // pula "password="
        strncpy(password, pass_start, sizeof(password) - 1);
        url_decode(password, password);
        
        ESP_LOGI(TAG, "SSID recebido: %s", ssid);
        ESP_LOGI(TAG, "Senha recebida: %s", password);  // ← ADICIONE
        ESP_LOGI(TAG, "Iniciando conexao ao Wi-Fi...");  // ← ADICIONE
        
        /* Envia página de sucesso */
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, success_page, strlen(success_page));
        
        /* Aguarda um pouco para garantir que a página foi enviada */
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        /* Conecta ao Wi-Fi com as credenciais fornecidas */
        wifi_prov_connect(ssid, password);
        ESP_LOGI(TAG, "Conectado em %s senha: %s...", ssid, password);
        
        return ESP_OK;
    }
    
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

/* Inicia o servidor HTTP */
static esp_err_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Iniciando servidor HTTP na porta %d", config.server_port);
    
    if (httpd_start(&server, &config) == ESP_OK) {
        /* Registra os handlers */
        httpd_uri_t root_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t save_uri = {
            .uri       = "/save",
            .method    = HTTP_POST,
            .handler   = save_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &save_uri);
        
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Erro ao iniciar servidor HTTP");
    return ESP_FAIL;
}

/* Inicializa o sistema de provisionamento */
esp_err_t wifi_prov_init(void)
{
    /* Inicializa NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Inicializa TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Cria event group */
    s_wifi_event_group = xEventGroupCreate();

    return ESP_OK;
}

/* Inicia o modo AP */
esp_err_t wifi_prov_start_ap(const char *ap_ssid, const char *ap_password)
{
    /* Cria interface AP */
    esp_netif_create_default_wifi_ap();

    /* Inicializa Wi-Fi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Registra handlers de eventos */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    /* Configura o AP */
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(ap_ssid),
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };
    
    strcpy((char *)wifi_config.ap.ssid, ap_ssid);
    
    if (ap_password != NULL && strlen(ap_password) >= 8) {
        strcpy((char *)wifi_config.ap.password, ap_password);
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP iniciado. SSID: %s", ap_ssid);
    
    /* Inicia o servidor web */
    start_webserver();
    
    return ESP_OK;
}

/* Conecta ao Wi-Fi */
/* Conecta ao Wi-Fi - VERSÃO CORRIGIDA */
esp_err_t wifi_prov_connect(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "=== INICIANDO CONEXÃO WI-FI ===");
    ESP_LOGI(TAG, "SSID: %s", ssid);
    
    /* Aguarda um pouco para garantir que a resposta HTTP foi enviada */
    vTaskDelay(pdMS_TO_TICKS(500));
    
    /* Para o servidor web ANTES de mexer no Wi-Fi */
    if (server) {
        ESP_LOGI(TAG, "Parando servidor HTTP...");
        // TODO: essa funcao aparentemente estava  travando o fluxo. eu comentei e aparentemente passou a funcionar. talvez rever? é necessario manter? 
        // httpd_stop(server);
        server = NULL;
        vTaskDelay(pdMS_TO_TICKS(200));  // Aguarda finalizar
    }
    
    /* Limpa event bits anteriores */
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_retry_num = 0;
    
    /* Para o Wi-Fi atual */
    ESP_LOGI(TAG, "Reconfigurando Wi-Fi...");
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    /* Cria interface STA se não existir */
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == NULL) {
        ESP_LOGI(TAG, "Criando interface STA...");
        sta_netif = esp_netif_create_default_wifi_sta();
    }
    
    /* Muda para modo APSTA (AP + Station) */
    ESP_LOGI(TAG, "Mudando para modo APSTA...");
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    
    /* Configura Station */
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "Configurando credenciais...");
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    
    /* Reinicia Wi-Fi */
    ESP_LOGI(TAG, "Iniciando Wi-Fi...");
    esp_wifi_start();

    ESP_LOGI(TAG, "Aguardando conexão (timeout: 30s)...");

    /* Aguarda conexão ou falha (timeout de 30 segundos) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(30000)); // 30 segundos

    /* Limpa os bits */
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "✅ CONECTADO AO WI-FI COM SUCESSO!");
        
        /* Desabilita o modo AP agora que conectou */
        ESP_LOGI(TAG, "Mudando para modo Station puro...");
        esp_wifi_set_mode(WIFI_MODE_STA);
        
        return ESP_OK;
        
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "❌ FALHA AO CONECTAR - Senha incorreta ou rede não encontrada");
        
        /* Volta para modo AP puro */
        ESP_LOGI(TAG, "Voltando para modo AP...");
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_wifi_set_mode(WIFI_MODE_AP);
        esp_wifi_start();
        
        /* Reinicia servidor web após estabilizar */
        vTaskDelay(pdMS_TO_TICKS(500));
        start_webserver();
        
        return ESP_FAIL;
        
    } else {
        ESP_LOGE(TAG, "❌ TIMEOUT - Não foi possível conectar em 30 segundos");
        
        /* Volta para modo AP puro */
        ESP_LOGI(TAG, "Voltando para modo AP...");
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_wifi_set_mode(WIFI_MODE_AP);
        esp_wifi_start();
        
        vTaskDelay(pdMS_TO_TICKS(500));
        start_webserver();
        
        return ESP_FAIL;
    }
}

/* Verifica se está conectado */
bool wifi_prov_is_connected(void)
{
    return wifi_connected;
}
