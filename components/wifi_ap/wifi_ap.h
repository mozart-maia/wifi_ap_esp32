#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Inicializa o sistema de provisionamento Wi-Fi
 * 
 * Cria um Access Point (AP) e inicia um servidor web para configuração
 * 
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wifi_prov_init(void);

/**
 * @brief Inicia o modo AP para provisionamento
 * 
 * @param ap_ssid Nome do AP (ex: "ESP32-Setup")
 * @param ap_password Senha do AP (pode ser NULL para AP aberto)
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wifi_prov_start_ap(const char *ap_ssid, const char *ap_password);

/**
 * @brief Conecta ao Wi-Fi usando as credenciais fornecidas
 * 
 * @param ssid SSID do Wi-Fi alvo
 * @param password Senha do Wi-Fi alvo
 * @return ESP_OK em caso de sucesso
 */
esp_err_t wifi_prov_connect(const char *ssid, const char *password);

/**
 * @brief Verifica se está conectado ao Wi-Fi
 * 
 * @return true se conectado, false caso contrário
 */
bool wifi_prov_is_connected(void);

#endif // WIFI_PROVISIONING_H
