idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_system esp_hw_support esp_psram spi_flash heap freertos log
)
