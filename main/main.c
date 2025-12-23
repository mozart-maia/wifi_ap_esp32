#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "wifi_ap.h"

static const char *TAG = "Main";

void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando sistema de provisionamento Wi-Fi");

    /* Inicializa o sistema de provisionamento */
    ESP_ERROR_CHECK(wifi_prov_init());

    /* Inicia o Access Point para configuração */
    /* O usuário deve conectar ao AP "ESP32-Config" e acessar http://192.168.4.1 */
    ESP_ERROR_CHECK(wifi_prov_start_ap("ESP32-Config", NULL));
    
    ESP_LOGI(TAG, "Access Point iniciado!");
    ESP_LOGI(TAG, "Conecte-se ao Wi-Fi: ESP32-Config");
    ESP_LOGI(TAG, "Acesse: http://192.168.4.1");
    
    /* Loop principal - aguarda conexão */
    while (1) {
        if (wifi_prov_is_connected()) {
            ESP_LOGI(TAG, "ESP32 conectada ao Wi-Fi!");
            /* Aqui você pode continuar com o resto da sua aplicação */
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
