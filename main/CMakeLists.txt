idf_component_register(
    SRCS "main.c" "rlcd.c" "shtc3.c" "battery.c" "wifi_setup.c" "portal_codec.c"
         "button.c" "ui.c" "ui_pages.c" "ui_chrome.c" "display_time.c" "codeck_client.c" "codeck_state.c" "codeck_page.c" "codeck_label.c" "brand_icons.c" "home_assistant.c" "ha_state.c" "clock_service.c" "power.c"
         "easytier_link.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_system esp_hw_support esp_psram spi_flash heap freertos log app_update bootloader_support
                  esp_driver_gpio esp_driver_spi esp_driver_i2c esp_adc
                  esp_pm esp_driver_usb_serial_jtag
                  esp_wifi esp_netif esp_event esp_http_server esp_http_client nvs_flash espressif__cjson lwip esp_timer mbedtls esp-tls
                  myqfeng__easytier-esp
    EMBED_TXTFILES "wifi_portal.html"
)
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/../assets/fonts/codeck_labels.bin" BINARY RENAME_TO codeck_labels)

# Real network secret lives in easytier.toml (gitignored). A fresh clone falls
# back to the placeholder example, which easytier_start() rejects until filled.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../easytier.toml")
    set(EASYTIER_TOML "${CMAKE_CURRENT_SOURCE_DIR}/../easytier.toml")
else()
    set(EASYTIER_TOML "${CMAKE_CURRENT_SOURCE_DIR}/../easytier.toml.example")
endif()
target_add_binary_data(${COMPONENT_LIB} "${EASYTIER_TOML}" TEXT RENAME_TO easytier_toml)

# Optional local secret: never put the token in source, compiler flags or logs.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../rc.key")
    target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/../rc.key" TEXT RENAME_TO ha_token)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HA_TOKEN_EMBEDDED=1)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../codeck.key")
    target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/../codeck.key" TEXT RENAME_TO codeck_key)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CODECK_KEY_EMBEDDED=1)
endif()
