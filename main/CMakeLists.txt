idf_component_register(
    SRCS "main.c" "rlcd.c" "shtc3.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_system esp_hw_support esp_psram spi_flash heap freertos log
                  esp_driver_gpio esp_driver_spi esp_driver_i2c
)
