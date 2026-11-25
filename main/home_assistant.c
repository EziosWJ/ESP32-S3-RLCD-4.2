#include "home_assistant.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "board_config.h"
#include "display_time.h"
#include "ha_state.h"
#include "wifi_setup.h"

#define HA_BODY_SIZE 4096
#define HA_TOKEN_MAX 2048
// After the batch endpoint reports 404/405/501, keep the per-entity GET
// fallback for this many rounds before probing /api/template again.
#define HA_TEMPLATE_RETRY_ROUNDS 10U

static const char *TAG = "home_assistant";
static QueueHandle_t snapshots;
static char authorization[HA_TOKEN_MAX + 8];

// Response scratch and the snapshot queue are CPU-only: prefer PSRAM and fall
// back to internal RAM when PSRAM is unavailable.
static void *psram_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p != NULL ? p : malloc(size);
}

static QueueHandle_t snapshot_queue(size_t item_size)
{
    QueueHandle_t queue =
        xQueueCreateWithCaps(1, item_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return queue != NULL ? queue : xQueueCreate(1, item_size);
}

// One slot per displayed value. host < 0 marks a device sensor; otherwise it
// is the host metric index in ha_host_entity_t.
typedef struct {
    const char *entity;
    bool humidity;
    int host;
} ha_slot_t;

#define HA_SLOT_COUNT (HA_DEVICE_COUNT * 2 + HA_HOST_ENTITY_COUNT)
static const ha_slot_t slots[HA_SLOT_COUNT] = {
    {"sensor.xiaomi_c24_08dc_temperature", false, -1},
    {"sensor.xiaomi_c24_08dc_relative_humidity", true, -1},
    {"sensor.xiaomi_h39h00_47aa_temperature", false, -1},
    {"sensor.xiaomi_h39h00_47aa_relative_humidity", true, -1},
    {"sensor.miwifi_rd15_srv_cpu_usage", false, HA_HOST_CPU_USAGE},
    {"sensor.miwifi_rd15_srv_memory_usage", false, HA_HOST_MEMORY_USAGE},
    {"sensor.miwifi_rd15_srv_disk_usage", false, HA_HOST_DISK_USAGE},
    {"sensor.miwifi_rd15_srv_cpu_temperature", false, HA_HOST_CPU_TEMPERATURE},
    {"sensor.miwifi_rd15_srv_uptime", false, HA_HOST_UPTIME},
    {"sensor.miwifi_rd15_srv_network_receive_rate", false, HA_HOST_NETWORK_RECEIVE_RATE},
    {"sensor.miwifi_rd15_srv_network_transmit_rate", false, HA_HOST_NETWORK_TRANSMIT_RATE},
};

// Keys inside the rendered batch object, aligned with slots[].
static const char *template_keys[HA_SLOT_COUNT] = {
    "t0", "h0", "t1", "h1", "cpu", "mem", "disk", "cput", "up", "rx", "tx",
};

// Renders one JSON object: every entry is {"s": state text, "u": unit}. Jinja
// uses single quotes only, so the whole template embeds verbatim into the
// JSON POST body without escaping. to_json on the server side handles any
// quoting inside state/unit values.
static const char HA_TEMPLATE_JINJA[] =
    "{{ {'t0': {'s': states('sensor.xiaomi_c24_08dc_temperature'), 'u': ''},"
    " 'h0': {'s': states('sensor.xiaomi_c24_08dc_relative_humidity'), 'u': ''},"
    " 't1': {'s': states('sensor.xiaomi_h39h00_47aa_temperature'), 'u': ''},"
    " 'h1': {'s': states('sensor.xiaomi_h39h00_47aa_relative_humidity'), 'u': ''},"
    " 'cpu': {'s': states('sensor.miwifi_rd15_srv_cpu_usage'), 'u': state_attr('sensor.miwifi_rd15_srv_cpu_usage', 'unit_of_measurement')},"
    " 'mem': {'s': states('sensor.miwifi_rd15_srv_memory_usage'), 'u': state_attr('sensor.miwifi_rd15_srv_memory_usage', 'unit_of_measurement')},"
    " 'disk': {'s': states('sensor.miwifi_rd15_srv_disk_usage'), 'u': state_attr('sensor.miwifi_rd15_srv_disk_usage', 'unit_of_measurement')},"
    " 'cput': {'s': states('sensor.miwifi_rd15_srv_cpu_temperature'), 'u': state_attr('sensor.miwifi_rd15_srv_cpu_temperature', 'unit_of_measurement')},"
    " 'up': {'s': states('sensor.miwifi_rd15_srv_uptime'), 'u': ''},"
    " 'rx': {'s': states('sensor.miwifi_rd15_srv_network_receive_rate'), 'u': state_attr('sensor.miwifi_rd15_srv_network_receive_rate', 'unit_of_measurement')},"
    " 'tx': {'s': states('sensor.miwifi_rd15_srv_network_transmit_rate'), 'u': state_attr('sensor.miwifi_rd15_srv_network_transmit_rate', 'unit_of_measurement')}"
    "} | to_json }}";

typedef enum {
    HA_ROUND_OK,
    HA_ROUND_AUTH,
    HA_ROUND_TEMPLATE_UNAVAILABLE,
    HA_ROUND_READ_ERROR,
} ha_round_result_t;

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
    for (unsigned i = 0; i < HA_HOST_ENTITY_COUNT; ++i) {
        snapshot->host[i].status = status;
    }
}

static ha_value_t *slot_value(ha_snapshot_t *snapshot, unsigned index)
{
    if (index < HA_DEVICE_COUNT * 2) {
        const unsigned device = index / 2;
        return index % 2 ? &snapshot->devices[device].humidity :
                            &snapshot->devices[device].temperature;
    }
    return &snapshot->host[index - HA_DEVICE_COUNT * 2];
}

static void fill_from_template(const char *inner, ha_snapshot_t *scratch)
{
    const int64_t now = esp_timer_get_time() / 1000;
    for (unsigned i = 0; i < HA_DEVICE_COUNT * 2; ++i) {
        ha_value_t *value = slot_value(scratch, i);
        float number = 0;
        if (ha_state_parse_template_number(inner, template_keys[i], slots[i].humidity, &number)) {
            value->value = number;
            value->unit[0] = '\0';
            value->updated_ms = now;
            value->status = HA_LIVE;
        } else {
            value->status = HA_OFFLINE;
        }
    }
    for (unsigned i = 0; i < HA_HOST_ENTITY_COUNT; ++i) {
        const unsigned index = HA_DEVICE_COUNT * 2 + i;
        ha_value_t *value = &scratch->host[i];
        if (i == HA_HOST_UPTIME) {
            char state[64];
            char uptime[sizeof(value->text)];
            bool parsed = ha_state_parse_template_text(inner, template_keys[index],
                                                       state, sizeof(state)) &&
                          display_time_format(state, uptime, sizeof(uptime), true);
            if (parsed) {
                parsed = strlen(uptime) + sizeof(" UTC+8") <= sizeof(value->text);
            }
            if (parsed) {
                const size_t length = strlen(uptime);
                memcpy(value->text, uptime, length);
                memcpy(value->text + length, " UTC+8", sizeof(" UTC+8"));
                value->unit[0] = '\0';
                value->updated_ms = now;
                value->status = HA_LIVE;
            } else {
                value->status = HA_OFFLINE;
            }
        } else {
            float number = 0;
            char unit[HA_UNIT_MAX] = {0};
            if (ha_state_parse_template_number_unit(inner, template_keys[index], &number,
                                                   unit, sizeof(unit))) {
                value->value = number;
                memcpy(value->unit, unit, sizeof(value->unit));
                value->updated_ms = now;
                value->status = HA_LIVE;
            } else {
                value->status = HA_OFFLINE;
            }
        }
    }
}

static ha_round_result_t fetch_template(esp_http_client_handle_t client, char *request_body,
                                         char *response_body, ha_snapshot_t *scratch)
{
    const int request_len = snprintf(request_body, HA_BODY_SIZE, "{\"template\":\"%s\"}",
                                     HA_TEMPLATE_JINJA);
    if (request_len <= 0 || request_len >= HA_BODY_SIZE) {
        return HA_ROUND_READ_ERROR;
    }
    esp_err_t err = esp_http_client_set_method(client, HTTP_METHOD_POST);
    if (err == ESP_OK) {
        err = esp_http_client_set_url(client, BOARD_HA_URL "/api/template");
    }
    if (err == ESP_OK) {
        err = esp_http_client_set_header(client, "Content-Type", "application/json");
    }
    if (err == ESP_OK) {
        err = esp_http_client_open(client, request_len);
    }
    if (err == ESP_OK) {
        const int written = esp_http_client_write(client, request_body, request_len);
        err = written == request_len ? ESP_OK : ESP_FAIL;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "template transport failed: %s (errno=%d)", esp_err_to_name(err),
                 esp_http_client_get_errno(client));
        return HA_ROUND_READ_ERROR;
    }
    if (esp_http_client_fetch_headers(client) < 0) {
        ESP_LOGW(TAG, "template headers failed (errno=%d)", esp_http_client_get_errno(client));
        return HA_ROUND_READ_ERROR;
    }
    const int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "template status=%d", status);
    // /api/template requires administrator rights. A valid token without
    // admin permission still has working /api/states, so do not convict the
    // whole round on a template 401/403: degrade to the per-entity GET path
    // and let those responses decide the final auth state.
    if (status == 401 || status == 403 || status == 404 || status == 405 || status == 501) {
        return HA_ROUND_TEMPLATE_UNAVAILABLE;
    }
    if (status != 200) {
        return HA_ROUND_READ_ERROR;
    }
    const int length = esp_http_client_read_response(client, response_body, HA_BODY_SIZE - 1);
    ESP_LOGI(TAG, "template body length=%d complete=%d errno=%d", length,
             esp_http_client_is_complete_data_received(client) ? 1 : 0,
             esp_http_client_get_errno(client));
    if (length <= 0 || length >= HA_BODY_SIZE - 1 ||
        !esp_http_client_is_complete_data_received(client)) {
        return HA_ROUND_READ_ERROR;
    }
    response_body[length] = '\0';
    // The request buffer is free now; reuse it for the rendered inner object.
    if (!ha_state_parse_template_envelope(response_body, request_body, HA_BODY_SIZE)) {
        return HA_ROUND_READ_ERROR;
    }
    fill_from_template(request_body, scratch);
    return HA_ROUND_OK;
}

static ha_round_result_t fetch_fallback(esp_http_client_handle_t client, char *body,
                                         ha_snapshot_t *scratch)
{
    const int64_t now = esp_timer_get_time() / 1000;
    for (unsigned i = 0; i < HA_SLOT_COUNT; ++i) {
        const ha_slot_t *slot = &slots[i];
        ha_value_t *value = slot_value(scratch, i);
        char url[192];
        snprintf(url, sizeof(url), "%s/api/states/%s", BOARD_HA_URL, slot->entity);
        esp_err_t err = esp_http_client_set_method(client, HTTP_METHOD_GET);
        if (err == ESP_OK) {
            err = esp_http_client_set_url(client, url);
        }
        if (err != ESP_OK || esp_http_client_open(client, 0) != ESP_OK ||
            esp_http_client_fetch_headers(client) < 0) {
            // Transport failure aborts the round; the caller keeps the old snapshot.
            ESP_LOGW(TAG, "fallback %s transport failed: %s errno=%d", slot->entity,
                     esp_err_to_name(err), esp_http_client_get_errno(client));
            return HA_ROUND_READ_ERROR;
        }
        const int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "fallback %s status=%d", slot->entity, status);
        if (status == 401 || status == 403) {
            return HA_ROUND_AUTH;
        }
        bool parsed = false;
        float number = 0;
        char unit[HA_UNIT_MAX] = {0};
        char state[64];
        char uptime[sizeof(value->text)];
        if (status == 200) {
            const int length = esp_http_client_read_response(client, body, HA_BODY_SIZE - 1);
            if (length > 0 && length < HA_BODY_SIZE - 1 &&
                esp_http_client_is_complete_data_received(client)) {
                body[length] = '\0';
                if (slot->host == HA_HOST_UPTIME) {
                    parsed = ha_state_parse_text(body, slot->entity, state, sizeof(state)) &&
                             display_time_format(state, uptime, sizeof(uptime), true);
                    if (parsed) {
                        parsed = strlen(uptime) + sizeof(" UTC+8") <= sizeof(value->text);
                    }
                } else if (slot->host >= 0) {
                    parsed = ha_state_parse_number(body, slot->entity, &number, unit, sizeof(unit));
                } else {
                    parsed = ha_state_parse(body, slot->entity, slot->humidity, &number);
                }
            }
            if (parsed) {
                if (slot->host == HA_HOST_UPTIME) {
                    const size_t length = strlen(uptime);
                    memcpy(value->text, uptime, length);
                    memcpy(value->text + length, " UTC+8", sizeof(" UTC+8"));
                    value->unit[0] = '\0';
                } else {
                    value->value = number;
                    if (slot->host >= 0) memcpy(value->unit, unit, sizeof(value->unit));
                }
                value->updated_ms = now;
                value->status = HA_LIVE;
                continue;
            }
            ESP_LOGI(TAG, "fallback %s parse failed", slot->entity);
            value->status = HA_OFFLINE;
        } else {
            // Drain a non-200 body so the keep-alive connection stays in sync.
            char drain;
            while (!esp_http_client_is_complete_data_received(client)) {
                if (esp_http_client_read(client, &drain, 1) <= 0) break;
            }
            if (status == 404) {
                value->status = HA_NOT_FOUND;
            } else {
                value->status = HA_READ_ERROR;
            }
        }
    }
    return HA_ROUND_OK;
}

static void worker(void *arg)
{
    ha_snapshot_t snapshot = {0};
    char *body = psram_malloc(HA_BODY_SIZE);
    char *inner = psram_malloc(HA_BODY_SIZE);
    if (!body || !inner) {
        set_status(&snapshot, HA_READ_ERROR);
        xQueueOverwrite(snapshots, &snapshot);
        free(body);
        free(inner);
        ESP_LOGE(TAG, "No memory for HTTP response");
        vTaskDelete(NULL);
        return;
    }
    bool template_down = false;
    unsigned fallback_rounds = 0;
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
            ha_snapshot_t scratch = snapshot;
            const bool try_template = !template_down ||
                                      fallback_rounds >= HA_TEMPLATE_RETRY_ROUNDS;
            ha_round_result_t round = HA_ROUND_READ_ERROR;
            bool template_succeeded = false;

            const esp_http_client_config_t config = {
                .url = BOARD_HA_URL "/api/template",
                .method = HTTP_METHOD_POST,
                .timeout_ms = BOARD_HA_TIMEOUT_MS,
                .disable_auto_redirect = true,
                .keep_alive_enable = true,
            };
            esp_http_client_handle_t client = esp_http_client_init(&config);
            if (client) {
                if (esp_http_client_set_header(client, "Authorization", authorization) == ESP_OK) {
                    if (try_template) {
                        round = fetch_template(client, body, inner, &scratch);
                        if (round == HA_ROUND_TEMPLATE_UNAVAILABLE) {
                            template_down = true;
                            fallback_rounds = 0;
                            round = fetch_fallback(client, body, &scratch);
                        } else {
                            template_succeeded = round == HA_ROUND_OK;
                        }
                    } else {
                        round = fetch_fallback(client, body, &scratch);
                    }
                }
                esp_http_client_cleanup(client);
            }
            ESP_LOGI(TAG, "round=%d template_ok=%d", (int)round, template_succeeded ? 1 : 0);

            if (round == HA_ROUND_AUTH) {
                set_status(&scratch, HA_AUTH_ERROR);
                snapshot = scratch;
                xQueueOverwrite(snapshots, &snapshot);
                ESP_LOGW(TAG, "Authentication failed; check rc.key");
            } else if (round == HA_ROUND_OK) {
                if (template_succeeded) {
                    template_down = false;
                    fallback_rounds = 0;
                } else if (template_down) {
                    ++fallback_rounds;
                }
                snapshot = scratch;
                // One queue commit per completed round; no per-entity overwrite.
                xQueueOverwrite(snapshots, &snapshot);
            } else {
                // Whole-round failure: retain the previous values, flag READ_ERROR
                // once instead of discarding half a round.
                ha_snapshot_t kept = snapshot;
                set_status(&kept, HA_READ_ERROR);
                snapshot = kept;
                xQueueOverwrite(snapshots, &snapshot);
                if (template_down) {
                    ++fallback_rounds;
                }
            }
            next_poll = esp_timer_get_time() / 1000 + BOARD_HA_POLL_INTERVAL_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t home_assistant_init(void)
{
    snapshots = snapshot_queue(sizeof(ha_snapshot_t));
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
    for (unsigned i = 0; i < HA_HOST_ENTITY_COUNT; ++i) {
        if (snapshot->host[i].status == HA_LIVE) {
            if (wifi.state != WIFI_SETUP_CONNECTED) snapshot->host[i].status = HA_OFFLINE;
            else if (now - snapshot->host[i].updated_ms > BOARD_HA_STALE_MS) snapshot->host[i].status = HA_STALE;
        }
    }
}
