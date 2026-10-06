#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"

#include "board_config.h"
#include "rlcd.h"
#include "shtc3.h"
#include "wifi_setup.h"

static const char *TAG = "rlcd";
static bool wifi_available;

static void show_measurement(bool valid, float temperature, float humidity, const char *status)
{
    char text[24];
    wifi_setup_status_t wifi;
    wifi_setup_get_status(&wifi);
    rlcd_clear();
    if (wifi_available && wifi.portal_active && wifi.state != WIFI_SETUP_CONNECTED) {
        rlcd_text(20, 18, "WIFI SETUP", 3);
        rlcd_hline(20, 52, 360);
        rlcd_text(20, 70, "CONNECT PHONE TO", 2);
        rlcd_text(20, 96, wifi.ap_ssid, 3);
        rlcd_text(20, 128, "HOTSPOT PASSWORD", 2);
        rlcd_text(20, 152, wifi.ap_password, 3);
        rlcd_text(20, 188, "OPEN 192.168.4.1", 2);
        rlcd_text(20, 214, wifi.state == WIFI_SETUP_CONNECTING ? "CONNECTING - PLEASE WAIT" :
                  wifi.state == WIFI_SETUP_FAILED ? "FAILED - RETRY IN BROWSER" : "SELECT WIFI IN BROWSER", 2);
        rlcd_hline(20, 244, 360);
        if (valid) {
            snprintf(text, sizeof(text), "T %.1f C  RH %.1f %%", (double)temperature, (double)humidity);
            rlcd_text(20, 266, text, 2);
        } else {
            rlcd_text(20, 266, status, 2);
        }
        goto flush;
    }
    rlcd_text(20, 18, "TEMPERATURE AND RH", 3);
    rlcd_hline(20, 56, 360);
    rlcd_text(20, 78, "TEMPERATURE", 2);
    if (valid) {
        snprintf(text, sizeof(text), "%5.1f C", (double)temperature);
    } else {
        snprintf(text, sizeof(text), " --.- C");
    }
    rlcd_text(20, 102, text, 6);
    rlcd_text(20, 170, "HUMIDITY", 2);
    if (valid) {
        snprintf(text, sizeof(text), "%5.1f %%", (double)humidity);
    } else {
        snprintf(text, sizeof(text), " --.- %%");
    }
    rlcd_text(20, 194, text, 6);
    rlcd_hline(20, 254, 360);
    rlcd_text(20, 242, status, 1);
    if (!wifi_available) {
        rlcd_text(20, 268, "WIFI INIT ERROR", 2);
    } else if (wifi.state == WIFI_SETUP_CONNECTED) {
        char network_text[32];
        snprintf(network_text, sizeof(network_text), "WIFI %s", wifi.ip);
        rlcd_text(20, 268, network_text, 2);
    } else {
        rlcd_text(20, 268, wifi.state == WIFI_SETUP_CONNECTING ? "WIFI CONNECTING" :
                  wifi.state == WIFI_SETUP_RETRYING ? "WIFI OFFLINE - RETRYING" :
                  wifi.state == WIFI_SETUP_FAILED ? "WIFI SETUP ERROR" : "WIFI STARTING", 2);
    }
    rlcd_text(20, 290, "HOLD KEY 3 SECONDS FOR WIFI SETUP", 1);
flush:
    ;
    const esp_err_t err = rlcd_flush();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display update failed: %s", esp_err_to_name(err));
    }
}

void app_main(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0;

    esp_chip_info(&chip_info);
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_size));

    ESP_LOGI(TAG, "Board: %s", BOARD_NAME);
    ESP_LOGI(TAG, "ESP-IDF: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Target: %s, cores: %u, revision: v%u.%u",
             CONFIG_IDF_TARGET, (unsigned)chip_info.cores,
             (unsigned)(chip_info.revision / 100),
             (unsigned)(chip_info.revision % 100));
    ESP_LOGI(TAG, "Flash: %" PRIu32 " bytes", flash_size);
    if (flash_size != BOARD_FLASH_SIZE_BYTES) {
        ESP_LOGW(TAG, "Expected %u bytes of flash", BOARD_FLASH_SIZE_BYTES);
    }

    const size_t psram_size = esp_psram_get_size();
    ESP_LOGI(TAG, "PSRAM: %s, %zu bytes",
             esp_psram_is_initialized() ? "initialized" : "unavailable",
             psram_size);
    if (psram_size != BOARD_PSRAM_SIZE_BYTES) {
        ESP_LOGW(TAG, "Expected %u bytes of PSRAM", BOARD_PSRAM_SIZE_BYTES);
    }

    ESP_ERROR_CHECK(rlcd_init());
    const esp_err_t wifi_err = wifi_setup_init();
    wifi_available = wifi_err == ESP_OK;
    if (!wifi_available) {
        ESP_LOGE(TAG, "Wi-Fi setup initialization failed: %s", esp_err_to_name(wifi_err));
    }
    show_measurement(false, 0, 0, "READING SENSOR");
    ESP_ERROR_CHECK(shtc3_init());
    ESP_LOGI(TAG, "SHTC3 address: 0x%02x, SDA: %d, SCL: %d",
             BOARD_SHTC3_ADDRESS, BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO);
    ESP_LOGI(TAG, "Display: %u x %u landscape, sample interval: %u ms",
             BOARD_RLCD_WIDTH, BOARD_RLCD_HEIGHT, BOARD_SAMPLE_INTERVAL_MS);

    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        float temperature = 0;
        float humidity = 0;
        const esp_err_t err = shtc3_read(&temperature, &humidity);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Temperature: %.1f C, humidity: %.1f %%RH",
                     (double)temperature, (double)humidity);
            show_measurement(true, temperature, humidity, "SHTC3  LIVE");
        } else {
            ESP_LOGW(TAG, "SHTC3 read failed: %s; retrying", esp_err_to_name(err));
            show_measurement(false, 0, 0, "SENSOR ERROR - RETRYING");
        }
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BOARD_SAMPLE_INTERVAL_MS));
    }
}
