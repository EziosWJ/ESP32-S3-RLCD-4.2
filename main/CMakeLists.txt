idf_component_register(
    SRCS "main.c" "rlcd.c" "shtc3.c" "wifi_setup.c" "portal_codec.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_system esp_hw_support esp_psram spi_flash heap freertos log
                  esp_driver_gpio esp_driver_spi esp_driver_i2c
                  esp_wifi esp_netif esp_event esp_http_server nvs_flash espressif__cjson lwip esp_timer
    EMBED_TXTFILES "wifi_portal.html"
)
