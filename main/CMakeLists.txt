idf_component_register(
    SRCS "main.c" "rlcd.c" "shtc3.c" "wifi_setup.c" "portal_codec.c"
         "button.c" "ui.c" "ui_pages.c" "home_assistant.c" "ha_state.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_system esp_hw_support esp_psram spi_flash heap freertos log
                  esp_driver_gpio esp_driver_spi esp_driver_i2c
                  esp_wifi esp_netif esp_event esp_http_server esp_http_client nvs_flash espressif__cjson lwip esp_timer
    EMBED_TXTFILES "wifi_portal.html"
)

# Optional local secret: never put the token in source, compiler flags or logs.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../rc.key")
    target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/../rc.key" TEXT RENAME_TO ha_token)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HA_TOKEN_EMBEDDED=1)
endif()
