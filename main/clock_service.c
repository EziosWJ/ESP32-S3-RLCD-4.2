#include "clock_service.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "board_config.h"

static const char *TAG = "clock";
static bool initialized, was_connected;
static atomic_bool synchronized;

bool clock_service_is_synchronized(void) { return atomic_load(&synchronized); }

esp_err_t clock_service_init(void)
{
    if (setenv("TZ", BOARD_TIMEZONE, 1) != 0) {
        return ESP_ERR_NO_MEM;
    }
    tzset();
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(BOARD_NTP_SERVER);
    config.start = false;
    const esp_err_t err = esp_netif_sntp_init(&config);
    initialized = err == ESP_OK;
    return err;
}

void clock_service_update(bool connected, clock_display_t *display)
{
    if (initialized) {
        // Restart on reconnection so a previous DNS failure needn't wait for
        // the SNTP retry interval. Network requests run in the lwIP task.
        if (connected && !was_connected) {
            const esp_err_t err = esp_netif_sntp_start();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "SNTP start failed: %s", esp_err_to_name(err));
            }
        }
        was_connected = connected;
        if (!synchronized && esp_netif_sntp_sync_wait(0) == ESP_OK) {
            synchronized = true;
            ESP_LOGI(TAG, "Network time synchronized");
        }
    }

    display->valid = false;
    strcpy(display->date, "---- -- --");
    strcpy(display->time, "--:--:--");
    if (!synchronized) {
        return;
    }
    const time_t now = time(NULL);
    struct tm local;
    if (localtime_r(&now, &local) != NULL &&
        strftime(display->date, sizeof(display->date), "%Y-%m-%d", &local) != 0 &&
        strftime(display->time, sizeof(display->time), "%H:%M:%S", &local) != 0) {
        display->valid = true;
    }
}
