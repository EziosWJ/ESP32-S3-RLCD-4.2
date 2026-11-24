#include "power.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_config.h"

static const char *TAG = "power";

/*
 * Wi-Fi maximum transmit power, in 0.25 dBm units (IDF range 8..84).
 * Mapping from esp_wifi_set_max_tx_power() docs: {56->14 dBm, 60->15 dBm,
 * 66->16 dBm, 72->18 dBm, 80->20 dBm}. 60 = 15 dBm is a conservative cap for
 * this short-range indoor board (NTP / Home Assistant / Codeck requests to a
 * nearby AP). Raise toward 80 (20 dBm, PHY maximum) only if field measurements
 * show weak RSSI or dropped requests; lower it to save transmit current.
 */
#define POWER_WIFI_MAX_TX_POWER_Q6 60

#define PROBE_TASK_STACK_BYTES 2048
#define PROBE_TASK_PRIORITY    1 /* below the network and UI workers */

static bool s_usb_connected;
static bool s_pm_active;

/* Apply the DFS window and the light-sleep policy for the current USB state. */
static esp_err_t apply_pm_config(bool usb_connected)
{
    const esp_pm_config_t pm = {
        .max_freq_mhz = BOARD_PM_MAX_FREQ_MHZ,
        .min_freq_mhz = BOARD_PM_MIN_FREQ_MHZ,
        .light_sleep_enable = BOARD_PM_LIGHT_SLEEP_ENABLE && !usb_connected,
    };
    return esp_pm_configure(&pm);
}

/* Modem power save and tx-power cap. Skipped once if Wi-Fi is not up. */
static void apply_wifi_power_save(void)
{
    esp_err_t err = esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    if (err == ESP_ERR_WIFI_NOT_INIT || err == ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "Wi-Fi unavailable; modem power save skipped: %s", esp_err_to_name(err));
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi modem power save set failed: %s", esp_err_to_name(err));
    }

    err = esp_wifi_set_max_tx_power(POWER_WIFI_MAX_TX_POWER_Q6);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi tx power cap failed: %s", esp_err_to_name(err));
    }
}

/* Re-probe the USB Serial/JTAG attachment and reconfigure PM only on change. */
static void probe_task(void *arg)
{
    (void)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(BOARD_PM_USB_PROBE_INTERVAL_MS));
        const bool connected = usb_serial_jtag_is_connected();
        if (connected == s_usb_connected) {
            continue;
        }
        const esp_err_t err = apply_pm_config(connected);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "PM reconfigure after USB change failed: %s", esp_err_to_name(err));
            continue;
        }
        s_usb_connected = connected;
        const bool ls_enabled = BOARD_PM_LIGHT_SLEEP_ENABLE && !connected;
        ESP_LOGW(TAG, "USB Serial/JTAG %s; light sleep %s",
                 connected ? "attached" : "detached", ls_enabled ? "enabled" : "disabled");
    }
}

esp_err_t power_management_init(void)
{
    s_usb_connected = usb_serial_jtag_is_connected();

    const esp_err_t err = apply_pm_config(s_usb_connected);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PM configure failed (non-fatal): %s", esp_err_to_name(err));
    } else {
        s_pm_active = true;
    }

    apply_wifi_power_save();

    /* Evidence line: read back the configuration actually accepted by PM. */
    esp_pm_config_t actual = {0};
    int max_mhz = BOARD_PM_MAX_FREQ_MHZ;
    int min_mhz = BOARD_PM_MIN_FREQ_MHZ;
    bool ls_enabled = BOARD_PM_LIGHT_SLEEP_ENABLE && !s_usb_connected;
    if (s_pm_active && esp_pm_get_configuration(&actual) == ESP_OK) {
        max_mhz = actual.max_freq_mhz;
        min_mhz = actual.min_freq_mhz;
        ls_enabled = actual.light_sleep_enable;
    }
    ESP_LOGW(TAG, "PM active: CPU %d/%d MHz, light sleep %s, USB probe: %s",
             max_mhz, min_mhz, ls_enabled ? "ON" : "OFF",
             s_usb_connected ? "connected" : "not connected");

    if (s_pm_active &&
        xTaskCreate(probe_task, "pm_usb_probe", PROBE_TASK_STACK_BYTES, NULL,
                    PROBE_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGW(TAG, "USB probe task creation failed; PM keeps current setting");
    }

    return err;
}
