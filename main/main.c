#include <inttypes.h>
#include <stdbool.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"

#include "board_config.h"

static const char *TAG = "rlcd";

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

    ESP_LOGI(TAG, "Base application ready; peripheral drivers are not enabled yet");

    while (true) {
        ESP_LOGI(TAG, "Alive; free internal heap: %zu bytes, free PSRAM heap: %zu bytes",
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
