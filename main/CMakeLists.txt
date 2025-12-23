idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES spi_flash esp_wifi esp_netif esp_http_server nvs_flash
                       REQUIRES wifi_ap
                       INCLUDE_DIRS ".")
