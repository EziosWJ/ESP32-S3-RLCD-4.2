#include "home_assistant.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "board_config.h"
#include "ha_state.h"
#include "wifi_setup.h"

#define HA_BODY_SIZE 4096
#define HA_TOKEN_MAX 2048

static const char *TAG = "home_assistant";
static QueueHandle_t snapshots;
static char authorization[HA_TOKEN_MAX + 8];
static const char *entities[HA_DEVICE_COUNT][2] = {
    {"sensor.xiaomi_c24_08dc_temperature", "sensor.xiaomi_c24_08dc_relative_humidity"},
    {"sensor.xiaomi_h39h00_47aa_temperature", "sensor.xiaomi_h39h00_47aa_relative_humidity"},
};

static bool load_token(void)
{
#ifdef HA_TOKEN_EMBEDDED
    extern const char token_start[] asm("_binary_ha_token_start");
    extern const char token_end[] asm("_binary_ha_token_end");
    const char *start = token_start;
    const char *end = token_end;
    while (end > start && (end[-1] == '\0' || isspace((unsigned char)end[-1]))) --end;
    while (start < end && isspace((unsigned char)*start)) ++start;
    // Accept a UTF-8 BOM from Windows editors.
    if (end - start >= 3 && (unsigned char)start[0] == 0xef &&
        (unsigned char)start[1] == 0xbb && (unsigned char)start[2] == 0xbf) start += 3;
    const size_t length = end - start;
    if (length == 0 || length > HA_TOKEN_MAX) return false;
    for (const char *p = start; p < end; ++p) {
        if (!(isalnum((unsigned char)*p) || *p == '.' || *p == '-' || *p == '_')) return false;
    }
    memcpy(authorization, "Bearer ", 7);
    memcpy(authorization + 7, start, length);
    authorization[7 + length] = '\0';
    return true;
#else
    return false;
#endif
}

static void set_status(ha_snapshot_t *snapshot, ha_value_status_t status)
{
    for (unsigned i = 0; i < HA_DEVICE_COUNT; ++i) {
        snapshot->devices[i].temperature.status = status;
        snapshot->devices[i].humidity.status = status;
    }
}

static void read_entity(const char *entity, bool humidity, ha_value_t *value, char *body)
{
    value->status = HA_READ_ERROR;
    char url[192];
    snprintf(url, sizeof(url), "%s/api/states/%s", BOARD_HA_URL, entity);
    const esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = BOARD_HA_TIMEOUT_MS,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return;
    esp_err_t err = esp_http_client_set_header(client, "Authorization", authorization);
    if (err == ESP_OK) err = esp_http_client_open(client, 0);
    if (err == ESP_OK && esp_http_client_fetch_headers(client) >= 0) {
        const int status = esp_http_client_get_status_code(client);
        if (status == 401 || status == 403) {
            value->status = HA_AUTH_ERROR;
        } else if (status == 404) {
            value->status = HA_NOT_FOUND;
        } else if (status == 200) {
            const int length = esp_http_client_read_response(client, body, HA_BODY_SIZE - 1);
            if (length > 0 && length < HA_BODY_SIZE - 1 &&
                esp_http_client_is_complete_data_received(client)) {
                body[length] = '\0';
                float number;
                if (ha_state_parse(body, entity, humidity, &number)) {
                    value->value = number;
                    value->updated_ms = esp_timer_get_time() / 1000;
                    value->status = HA_LIVE;
                } else {
                    value->status = HA_OFFLINE;
                }
            }
        }
    }
    esp_http_client_cleanup(client);
}

static void worker(void *arg)
{
    ha_snapshot_t snapshot = {0};
    char *body = malloc(HA_BODY_SIZE);
    if (!body) {
        set_status(&snapshot, HA_READ_ERROR);
        xQueueOverwrite(snapshots, &snapshot);
        ESP_LOGE(TAG, "No memory for HTTP response");
        vTaskDelete(NULL);
        return;
    }
    int64_t next_poll = 0;
    while (true) {
        wifi_setup_status_t wifi;
        wifi_setup_get_status(&wifi);
        const int64_t now = esp_timer_get_time() / 1000;
        if (wifi.state != WIFI_SETUP_CONNECTED) {
            set_status(&snapshot, HA_OFFLINE);
            xQueueOverwrite(snapshots, &snapshot);
            next_poll = 0;
        } else if (now >= next_poll) {
            for (unsigned i = 0; i < HA_DEVICE_COUNT; ++i) {
                for (unsigned j = 0; j < 2; ++j) {
                    ha_value_t *value = j ? &snapshot.devices[i].humidity : &snapshot.devices[i].temperature;
                    read_entity(entities[i][j], j == 1, value, body);
                    if (value->status == HA_AUTH_ERROR) {
                        set_status(&snapshot, HA_AUTH_ERROR);
                        ESP_LOGW(TAG, "Authentication failed; check rc.key");
                        goto poll_done;
                    }
                    xQueueOverwrite(snapshots, &snapshot);
                }
            }
poll_done:
            xQueueOverwrite(snapshots, &snapshot);
            next_poll = esp_timer_get_time() / 1000 + BOARD_HA_POLL_INTERVAL_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t home_assistant_init(void)
{
    snapshots = xQueueCreate(1, sizeof(ha_snapshot_t));
    if (!snapshots) return ESP_ERR_NO_MEM;
    ha_snapshot_t snapshot = {0};
    if (!load_token()) {
        set_status(&snapshot, HA_NO_TOKEN);
        xQueueOverwrite(snapshots, &snapshot);
        ESP_LOGW(TAG, "Missing or invalid rc.key; HA disabled");
        return ESP_OK;
    }
    xQueueOverwrite(snapshots, &snapshot);
    if (xTaskCreate(worker, "ha_reader", 6144, NULL, 3, NULL) != pdPASS) {
        set_status(&snapshot, HA_READ_ERROR);
        xQueueOverwrite(snapshots, &snapshot);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void home_assistant_get_snapshot(ha_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    if (!snapshots || xQueuePeek(snapshots, snapshot, 0) != pdTRUE) {
        set_status(snapshot, HA_READ_ERROR);
        return;
    }
    wifi_setup_status_t wifi;
    wifi_setup_get_status(&wifi);
    const int64_t now = esp_timer_get_time() / 1000;
    for (unsigned i = 0; i < HA_DEVICE_COUNT; ++i) {
        ha_value_t *values[] = {&snapshot->devices[i].temperature, &snapshot->devices[i].humidity};
        for (unsigned j = 0; j < 2; ++j) {
            if (values[j]->status == HA_LIVE) {
                if (wifi.state != WIFI_SETUP_CONNECTED) values[j]->status = HA_OFFLINE;
                else if (now - values[j]->updated_ms > BOARD_HA_STALE_MS) values[j]->status = HA_STALE;
            }
        }
    }
}
