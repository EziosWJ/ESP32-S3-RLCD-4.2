#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "board_config.h"
#include "button.h"
#include "battery.h"
#include "ui.h"
#include "rlcd.h"
#include "shtc3.h"
#include "wifi_setup.h"
#include "home_assistant.h"
#include "clock_service.h"
#include "codeck_client.h"
#include "power.h"
#include "easytier_link.h"

static const char *TAG = "rlcd";
static const char *MESH_TAG = "easytier";

static const char *easytier_state_name(easytier_state_t state)
{
    switch (state) {
    case EASYTIER_STATE_UNINITIALIZED: return "UNINITIALIZED";
    case EASYTIER_STATE_STOPPED: return "STOPPED";
    case EASYTIER_STATE_WAITING_UNDERLAY: return "WAITING_UNDERLAY";
    case EASYTIER_STATE_CONNECTING: return "CONNECTING";
    case EASYTIER_STATE_HANDSHAKING: return "HANDSHAKING";
    case EASYTIER_STATE_SYNCING: return "SYNCING";
    case EASYTIER_STATE_READY: return "READY";
    case EASYTIER_STATE_STOPPING: return "STOPPING";
    case EASYTIER_STATE_ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

static const char *ha_status_name(ha_value_status_t status)
{
    switch (status) {
    case HA_READING: return "READING";
    case HA_LIVE: return "LIVE";
    case HA_OFFLINE: return "OFFLINE";
    case HA_AUTH_ERROR: return "AUTH_ERROR";
    case HA_NOT_FOUND: return "NOT_FOUND";
    case HA_READ_ERROR: return "READ_ERROR";
    case HA_NO_TOKEN: return "NO_TOKEN";
    case HA_STALE: return "STALE";
    }
    return "UNKNOWN";
}

static void print_resource_status(void)
{
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t psram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const size_t internal_free = heap_caps_get_free_size(internal);
    const size_t internal_low = heap_caps_get_minimum_free_size(internal);
    const size_t internal_max = heap_caps_get_largest_free_block(internal);
    const size_t psram_total = heap_caps_get_total_size(psram);
    const size_t psram_free = heap_caps_get_free_size(psram);
    ESP_LOGI(MESH_TAG,
             "heap: int_free=%u int_low=%u int_max=%u psram_total=%u psram_free=%u psram_used=%u",
             (unsigned)internal_free, (unsigned)internal_low, (unsigned)internal_max,
             (unsigned)psram_total, (unsigned)psram_free,
             (unsigned)(psram_total - psram_free));
}

static void format_virtual_ipv4(uint32_t address, char out[16])
{
    if (address == 0) {
        snprintf(out, 16, "-");
        return;
    }
    snprintf(out, 16, "%u.%u.%u.%u",
             (unsigned)((address >> 24) & 0xffU),
             (unsigned)((address >> 16) & 0xffU),
             (unsigned)((address >> 8) & 0xffU),
             (unsigned)(address & 0xffU));
}

static void print_mesh_status(bool easytier_ready)
{
    print_resource_status();
    ha_snapshot_t ha = {0};
    home_assistant_get_snapshot(&ha);
    ESP_LOGI(MESH_TAG, "ha: device1 temp=%s humidity=%s",
             ha_status_name(ha.devices[0].temperature.status),
             ha_status_name(ha.devices[0].humidity.status));
    easytier_status_t mesh = {0};
    if (!easytier_ready || easytier_status(&mesh) != ESP_OK) {
        ESP_LOGI(MESH_TAG, "mesh: DISABLED (easytier.toml placeholders or start failure)");
        return;
    }
    char ip[16];
    format_virtual_ipv4(mesh.virtual_ipv4, ip);
    const char *error = mesh.last_error == ESP_OK ? "OK" : esp_err_to_name(mesh.last_error);
    ESP_LOGI(MESH_TAG,
             "mesh: state=%s ip=%s peers=%u routes=%u err=%s dropped=%" PRIu64,
             easytier_state_name(mesh.state), ip, (unsigned)mesh.direct_peers,
             (unsigned)mesh.reachable_routes, error, mesh.dropped_packets);
}

static void read_app_image_size(ui_memory_info_t *memory)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) return;

    const esp_partition_pos_t position = {
        .offset = running->address,
        .size = running->size,
    };
    esp_image_metadata_t metadata = {0};
    if (esp_image_get_metadata(&position, &metadata) == ESP_OK) {
        memory->app_image_bytes = metadata.image_len;
        memory->app_partition_bytes = running->size;
    }
}

static void update_heap_sizes(ui_memory_info_t *memory)
{
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t psram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    memory->internal_heap_total_bytes = heap_caps_get_total_size(internal);
    memory->internal_heap_free_bytes = heap_caps_get_free_size(internal);
    memory->internal_heap_min_free_bytes = heap_caps_get_minimum_free_size(internal);
    memory->internal_heap_largest_free_block = heap_caps_get_largest_free_block(internal);
    memory->psram_heap_total_bytes = heap_caps_get_total_size(psram);
    memory->psram_heap_free_bytes = heap_caps_get_free_size(psram);
    memory->psram_heap_min_free_bytes = heap_caps_get_minimum_free_size(psram);
    memory->psram_heap_largest_free_block = heap_caps_get_largest_free_block(psram);
}

void app_main(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0;

    esp_chip_info(&chip_info);
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_size));
    ui_memory_info_t memory = {0};
    read_app_image_size(&memory);

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
    const bool wifi_available = wifi_err == ESP_OK;
    if (!wifi_available) {
        ESP_LOGE(TAG, "Wi-Fi setup initialization failed: %s", esp_err_to_name(wifi_err));
    }
    if (wifi_available) {
        const esp_err_t clock_err = clock_service_init();
        if (clock_err != ESP_OK) {
            ESP_LOGW(TAG, "Clock initialization failed: %s", esp_err_to_name(clock_err));
        }
    }
    // The release default is WARN; keep the 10-second mesh summary visible
    // without turning on INFO for the rest of the firmware.
    esp_log_level_set(MESH_TAG, ESP_LOG_INFO);
    bool easytier_ready = false;
    if (wifi_available) {
        const esp_err_t easytier_err = easytier_start();
        if (easytier_err == ESP_OK) {
            easytier_ready = true;
        } else {
            ESP_LOGW(TAG, "EasyTier disabled: %s", esp_err_to_name(easytier_err));
        }
    }
    ESP_ERROR_CHECK(button_init());
    const ui_system_info_t system = {
        .flash_bytes = flash_size,
        .psram_bytes = psram_size,
        .cores = chip_info.cores,
    };
    ui_init(wifi_available, &system);
    const esp_err_t battery_err = battery_init();
    const bool battery_available = battery_err == ESP_OK;
    if (!battery_available) {
        ESP_LOGW(TAG, "Battery ADC initialization failed: %s", esp_err_to_name(battery_err));
    }
    if (wifi_available) {
        const esp_err_t ha_err = home_assistant_init();
        if (ha_err != ESP_OK) {
            ESP_LOGW(TAG, "HA initialization failed: %s", esp_err_to_name(ha_err));
        }
        const esp_err_t codeck_err=codeck_client_init();
        if(codeck_err!=ESP_OK) ESP_LOGW(TAG,"Codeck initialization failed: %s",esp_err_to_name(codeck_err));
    }
    ESP_ERROR_CHECK(ui_render());
    ESP_ERROR_CHECK(shtc3_init());
    ESP_LOGI(TAG, "SHTC3 address: 0x%02x, SDA: %d, SCL: %d",
             BOARD_SHTC3_ADDRESS, BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO);
    ESP_LOGI(TAG, "Display: %u x %u landscape, sample interval: %u ms",
             BOARD_RLCD_WIDTH, BOARD_RLCD_HEIGHT, BOARD_SAMPLE_INTERVAL_MS);

    update_heap_sizes(&memory);
    ui_update_memory_info(&memory);

    const esp_err_t power_err = power_management_init();
    if (power_err != ESP_OK) {
        ESP_LOGW(TAG, "Power management initialization failed (non-fatal): %s",
                 esp_err_to_name(power_err));
    }

    int64_t next_sample = esp_timer_get_time() / 1000;
    int64_t next_memory_sample = next_sample + 30000;
    int64_t next_mesh_sample = next_sample + 10000;
    bool wifi_was_connected = false;
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        const button_event_t event = button_poll();
        if (ui_handle_button(event) && wifi_available) {
            const esp_err_t err = wifi_setup_start_portal();
            if (err == ESP_OK) {
                ui_show_setup();
                ESP_LOGI(TAG, "KEY long press: Wi-Fi setup");
            } else {
                ESP_LOGW(TAG, "Wi-Fi setup request failed: %s", esp_err_to_name(err));
            }
        }
        const int64_t now = esp_timer_get_time() / 1000;
        // Wi-Fi CONNECTED edges drive mesh connect/disconnect. The component
        // must see connect() again after every reconnect.
        if (wifi_available) {
            wifi_setup_status_t wifi = {0};
            wifi_setup_get_status(&wifi);
            const bool wifi_connected = wifi.state == WIFI_SETUP_CONNECTED;
            if (wifi_connected != wifi_was_connected) {
                wifi_was_connected = wifi_connected;
                if (easytier_ready) {
                    const esp_err_t link_err =
                        wifi_connected ? easytier_connect() : easytier_disconnect();
                    if (link_err != ESP_OK) {
                        ESP_LOGW(TAG, "EasyTier %s failed: %s",
                                 wifi_connected ? "connect" : "disconnect",
                                 esp_err_to_name(link_err));
                    }
                }
            }
        }
        if (now >= next_mesh_sample) {
            print_mesh_status(easytier_ready);
            next_mesh_sample = now + 10000;
        }
        if (now >= next_memory_sample) {
            update_heap_sizes(&memory);
            ui_update_memory_info(&memory);
            next_memory_sample = now + 30000;
        }
        if (now >= next_sample) {
            battery_reading_t battery = {0};
            if (battery_available) {
                const esp_err_t battery_read_err = battery_read(&battery);
                if (battery_read_err != ESP_OK) {
                    ESP_LOGW(TAG, "Battery read failed: %s", esp_err_to_name(battery_read_err));
                }
            }
            ui_update_battery(&battery);
            float temperature = 0;
            float humidity = 0;
            const esp_err_t err = shtc3_read(&temperature, &humidity);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Temperature: %.1f C, humidity: %.1f %%RH",
                         (double)temperature, (double)humidity);
                ui_update_measurement(true, temperature, humidity, "SHTC3 LIVE");
            } else {
                ESP_LOGW(TAG, "SHTC3 read failed: %s; retrying", esp_err_to_name(err));
                ui_update_measurement(false, 0, 0, "SENSOR ERROR - RETRYING");
            }
            next_sample += BOARD_SAMPLE_INTERVAL_MS;
            if (next_sample <= now) {
                next_sample = now + BOARD_SAMPLE_INTERVAL_MS;
            }
        }
        const esp_err_t err = ui_render();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Display update failed: %s", esp_err_to_name(err));
        }
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BOARD_UI_POLL_INTERVAL_MS));
    }
}
