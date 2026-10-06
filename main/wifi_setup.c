#include "wifi_setup.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "board_config.h"
#include "portal_codec.h"

#define EVENT_IP BIT0
#define EVENT_DISCONNECTED BIT1
#define EVENT_SCAN_DONE BIT2
#define EVENT_LOST_IP BIT3
#define MAX_NETWORKS 20
#define AP_ADDRESS "192.168.4.1"

static const char *TAG = "wifi_setup";
extern const char portal_start[] asm("_binary_wifi_portal_html_start");
extern const char portal_end[] asm("_binary_wifi_portal_html_end");

typedef struct {
    char ssid[33];
    char password[65];
} credentials_t;

typedef enum { COMMAND_CONNECT, COMMAND_SCAN, COMMAND_ENTER_PORTAL } command_type_t;
typedef struct {
    command_type_t type;
    credentials_t credentials;
} command_t;

typedef struct {
    char ssid[33];
    int rssi;
    bool secure;
} network_t;

static SemaphoreHandle_t lock;
static QueueHandle_t commands;
static EventGroupHandle_t events;
static esp_netif_t *station;
static nvs_handle_t storage;
static httpd_handle_t server;
static wifi_setup_status_t status;
static network_t networks[MAX_NETWORKS];
static unsigned network_count;
static bool scanning;
static bool scan_requested;
static char scan_error[80];
static char setup_token[33];
static unsigned disconnect_reason;

// Only the network worker mutates these connection/lifecycle fields.
static credentials_t saved, target;
static bool have_saved, testing, connecting;
static int64_t connect_deadline, retry_at, close_at;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void set_state(wifi_setup_state_t state, const char *message)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    status.state = state;
    strlcpy(status.message, message, sizeof(status.message));
    if (state != WIFI_SETUP_CONNECTED) {
        status.ip[0] = '\0';
    }
    xSemaphoreGive(lock);
}

void wifi_setup_get_status(wifi_setup_status_t *out)
{
    if (lock == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = status;
    xSemaphoreGive(lock);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(events, EVENT_IP);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        xEventGroupSetBits(events, EVENT_LOST_IP);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        xSemaphoreTake(lock, portMAX_DELAY);
        disconnect_reason = event->reason;
        xSemaphoreGive(lock);
        xEventGroupSetBits(events, EVENT_DISCONNECTED);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        xEventGroupSetBits(events, EVENT_SCAN_DONE);
    }
}

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    if (root == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return err;
}

// Do not expose the portal to clients on the household LAN in AP+STA mode.
static bool portal_request(httpd_req_t *req)
{
    // IDF listens on an IPv6 dual-stack socket when CONFIG_LWIP_IPV6 is set.
    // lwIP then returns ::ffff:192.168.4.1 even for an IPv4 HTTP client.
    struct sockaddr_storage local = {0};
    socklen_t len = sizeof(local);
    wifi_setup_status_t snapshot;
    wifi_setup_get_status(&snapshot);
    if (!snapshot.portal_active ||
        getsockname(httpd_req_to_sockfd(req), (struct sockaddr *)&local, &len) != 0) {
        return false;
    }
    static const uint8_t ap_ip[] = {192, 168, 4, 1};
    if (local.ss_family == AF_INET && len >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)&local;
        return portal_address_matches_ipv4((const uint8_t *)&ipv4->sin_addr.s_addr, 4, ap_ip);
    }
#if CONFIG_LWIP_IPV6
    if (local.ss_family == AF_INET6 && len >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *ipv6 = (const struct sockaddr_in6 *)&local;
        return portal_address_matches_ipv4((const uint8_t *)&ipv6->sin6_addr, 16, ap_ip);
    }
#endif
    return false;
}

static bool authorized_post(httpd_req_t *req)
{
    char token[sizeof(setup_token)];
    char type[48];
    return portal_request(req) &&
           httpd_req_get_hdr_value_str(req, "X-Setup-Token", token, sizeof(token)) == ESP_OK &&
           strcmp(token, setup_token) == 0 &&
           httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) == ESP_OK &&
           strcmp(type, "application/json") == 0;
}

static esp_err_t page_handler(httpd_req_t *req)
{
    if (!portal_request(req)) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Connect to the device hotspot");
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                       "connect-src 'self'; frame-ancestors 'none'; form-action 'self'");
    return httpd_resp_send(req, portal_start, portal_end - portal_start - 1);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    if (!portal_request(req)) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Connect to the device hotspot");
    }
    static const char *names[] = {"starting", "ready", "connecting", "connected", "retrying", "failed"};
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return send_json(req, root);
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    cJSON_AddStringToObject(root, "state", names[status.state]);
    cJSON_AddStringToObject(root, "message", status.message);
    cJSON_AddStringToObject(root, "ip", status.ip);
    cJSON_AddStringToObject(root, "token", setup_token);
    cJSON_AddBoolToObject(root, "scanning", scanning || scan_requested);
    cJSON_AddStringToObject(root, "scan_error", scan_error);
    cJSON *list = cJSON_AddArrayToObject(root, "networks");
    for (unsigned i = 0; list != NULL && i < network_count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            break;
        }
        cJSON_AddStringToObject(item, "ssid", networks[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", networks[i].rssi);
        cJSON_AddBoolToObject(item, "secure", networks[i].secure);
        cJSON_AddItemToArray(list, item);
    }
    xSemaphoreGive(lock);
    return send_json(req, root);
}

static esp_err_t scan_handler(httpd_req_t *req)
{
    if (!authorized_post(req)) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid setup request");
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    const bool busy = scanning || scan_requested || status.state == WIFI_SETUP_CONNECTING ||
                      status.state == WIFI_SETUP_CONNECTED;
    if (!busy) {
        scan_requested = true;
    }
    xSemaphoreGive(lock);
    const command_t cmd = {.type = COMMAND_SCAN};
    if (busy) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "Wait for the current operation");
    }
    if (xQueueSend(commands, &cmd, 0) != pdTRUE) {
        xSemaphoreTake(lock, portMAX_DELAY);
        scan_requested = false;
        xSemaphoreGive(lock);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Busy; retry");
    }
    return httpd_resp_sendstr(req, "OK");
}

static esp_err_t connect_handler(httpd_req_t *req)
{
    if (!authorized_post(req)) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid setup request");
    }
    if (req->content_len == 0 || req->content_len > 768) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request length");
    }
    char body[769];
    size_t received = 0;
    while (received < req->content_len) {
        const int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) {
            return ESP_FAIL; // Close an incomplete request, including receive timeout.
        }
        received += count;
    }
    body[received] = '\0';
    if (memchr(body, 0, received) != NULL || strstr(body, "\\u0000") != NULL) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "NUL is not supported");
    }
    cJSON *root = cJSON_ParseWithLengthOpts(body, received + 1, NULL, true);
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(root, "password");
    command_t cmd = {.type = COMMAND_CONNECT};
    const bool valid = cJSON_IsObject(root) && cJSON_IsString(ssid) && cJSON_IsString(password) &&
                       portal_credentials_valid(ssid->valuestring, password->valuestring);
    if (valid) {
        strlcpy(cmd.credentials.ssid, ssid->valuestring, sizeof(cmd.credentials.ssid));
        strlcpy(cmd.credentials.password, password->valuestring, sizeof(cmd.credentials.password));
    }
    cJSON_Delete(root);
    memset(body, 0, sizeof(body));
    if (!valid) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID or password");
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    const bool busy = status.state == WIFI_SETUP_CONNECTING || status.state == WIFI_SETUP_CONNECTED;
    const wifi_setup_state_t previous = status.state;
    if (!busy) {
        status.state = WIFI_SETUP_CONNECTING; // Reserve before HTTP acknowledgement.
        strlcpy(status.message, "Connecting to Wi-Fi...", sizeof(status.message));
    }
    xSemaphoreGive(lock);
    if (busy || xQueueSend(commands, &cmd, 0) != pdTRUE) {
        memset(&cmd, 0, sizeof(cmd));
        if (!busy) {
            set_state(previous, "Device busy. Please retry.");
        }
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "Device busy. Please retry.");
    }
    memset(&cmd, 0, sizeof(cmd));
    httpd_resp_set_status(req, "202 Accepted");
    return httpd_resp_sendstr(req, "OK");
}

static esp_err_t redirect_handler(httpd_req_t *req, httpd_err_code_t error)
{
    if (!portal_request(req)) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Connect to the device hotspot");
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" AP_ADDRESS "/");
    return httpd_resp_sendstr(req, "Open the Wi-Fi setup page");
}

static esp_err_t start_http(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 3;
    cfg.send_wait_timeout = 3;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &cfg), TAG, "HTTP start");
    const httpd_uri_t handlers[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page_handler},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/scan", .method = HTTP_POST, .handler = scan_handler},
        {.uri = "/api/connect", .method = HTTP_POST, .handler = connect_handler},
    };
    for (unsigned i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        const esp_err_t err = httpd_register_uri_handler(server, &handlers[i]);
        if (err != ESP_OK) {
            httpd_stop(server);
            server = NULL;
            return err;
        }
    }
    return httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, redirect_handler);
}

static void dns_task(void *arg)
{
    // Long-lived task: a single socket is opened only while the portal is active.
    while (true) {
        wifi_setup_status_t snapshot;
        wifi_setup_get_status(&snapshot);
        if (!snapshot.portal_active) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        const struct sockaddr_in local = {
            .sin_family = AF_INET, .sin_port = htons(53),
            .sin_addr.s_addr = inet_addr(AP_ADDRESS),
        };
        const struct timeval timeout = {.tv_sec = 0, .tv_usec = 250000};
        if (sock < 0) {
            ESP_LOGW(TAG, "DNS socket unavailable");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
            bind(sock, (const struct sockaddr *)&local, sizeof(local)) != 0) {
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        do {
            uint8_t packet[512];
            struct sockaddr_in peer;
            socklen_t peer_size = sizeof(peer);
            const int len = recvfrom(sock, packet, sizeof(packet), 0,
                                     (struct sockaddr *)&peer, &peer_size);
            wifi_setup_get_status(&snapshot);
            if (len > 0 && snapshot.portal_active) {
                const size_t reply_len = portal_dns_reply(packet, len, sizeof(packet));
                if (reply_len != 0) {
                    sendto(sock, packet, reply_len, 0, (struct sockaddr *)&peer, peer_size);
                }
            } else if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }
        } while (snapshot.portal_active);
        close(sock);
    }
}

static void stop_scan(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    const bool was_scanning = scanning;
    scanning = scan_requested = false;
    xSemaphoreGive(lock);
    if (was_scanning) {
        esp_wifi_scan_stop();
    }
    esp_wifi_clear_ap_list();
    xEventGroupClearBits(events, EVENT_SCAN_DONE);
}

static void disconnect_station(void)
{
    connecting = testing = false;
    retry_at = close_at = 0;
    stop_scan();
    xEventGroupClearBits(events, EVENT_IP | EVENT_DISCONNECTED | EVENT_LOST_IP);
    // Drain the old disconnect event before accepting new credentials.
    if (esp_wifi_disconnect() == ESP_OK) {
        xEventGroupWaitBits(events, EVENT_DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
    }
    xEventGroupClearBits(events, EVENT_IP | EVENT_DISCONNECTED | EVENT_LOST_IP);
}

static void start_scan(void)
{
    wifi_setup_status_t snapshot;
    wifi_setup_get_status(&snapshot);
    if (!snapshot.portal_active || connecting || close_at != 0) {
        xSemaphoreTake(lock, portMAX_DELAY);
        scan_requested = false;
        xSemaphoreGive(lock);
        return;
    }
    const wifi_scan_config_t config = {.show_hidden = false};
    const esp_err_t err = esp_wifi_scan_start(&config, false);
    xSemaphoreTake(lock, portMAX_DELAY);
    scanning = err == ESP_OK;
    scan_requested = false;
    strlcpy(scan_error, err == ESP_OK ? "" : "Scan failed. Retry or enter the SSID manually.", sizeof(scan_error));
    xSemaphoreGive(lock);
}

static void finish_scan(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    const bool active = scanning;
    xSemaphoreGive(lock);
    if (!active) {
        esp_wifi_clear_ap_list();
        return;
    }
    wifi_ap_record_t records[MAX_NETWORKS];
    uint16_t count = MAX_NETWORKS;
    const esp_err_t err = esp_wifi_scan_get_ap_records(&count, records);
    xSemaphoreTake(lock, portMAX_DELAY);
    scanning = false;
    network_count = 0;
    if (err == ESP_OK) {
        for (unsigned i = 0; i < count; ++i) {
            char ssid[33];
            memcpy(ssid, records[i].ssid, 32);
            ssid[32] = '\0';
            if (ssid[0] == '\0' || strcmp(ssid, status.ap_ssid) == 0) {
                continue;
            }
            bool duplicate = false;
            for (unsigned j = 0; j < network_count; ++j) {
                duplicate |= strcmp(networks[j].ssid, ssid) == 0;
            }
            if (!duplicate) {
                network_t *item = &networks[network_count++];
                strlcpy(item->ssid, ssid, sizeof(item->ssid));
                item->rssi = records[i].rssi;
                item->secure = records[i].authmode != WIFI_AUTH_OPEN;
            }
        }
    } else {
        strlcpy(scan_error, "Scan failed. Retry or enter the SSID manually.", sizeof(scan_error));
    }
    xSemaphoreGive(lock);
    esp_wifi_clear_ap_list();
}

static void begin_connection(const credentials_t *credentials, bool new_credentials)
{
    disconnect_station();
    target = *credentials;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, target.ssid, strlen(target.ssid));
    memcpy(config.sta.password, target.password, strlen(target.password));
    config.sta.threshold.authmode = target.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    memset(&config, 0, sizeof(config));
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    testing = new_credentials;
    connecting = err == ESP_OK;
    connect_deadline = now_ms() + BOARD_WIFI_CONNECT_TIMEOUT_MS;
    if (connecting) {
        set_state(WIFI_SETUP_CONNECTING, "Connecting to Wi-Fi...");
    } else {
        set_state(new_credentials ? WIFI_SETUP_FAILED : WIFI_SETUP_RETRYING,
                  "Unable to start connection. Retry or check the Wi-Fi settings.");
        if (!new_credentials) {
            retry_at = now_ms() + BOARD_WIFI_RETRY_INTERVAL_MS;
        }
    }
}

static void enter_portal(void)
{
    disconnect_station();
    wifi_setup_status_t snapshot;
    wifi_setup_get_status(&snapshot);
    esp_err_t err = ESP_OK;
    if (!snapshot.portal_active) {
        wifi_config_t ap = {.ap = {
            .channel = 1, .authmode = WIFI_AUTH_WPA2_PSK, .max_connection = 2,
        }};
        strlcpy((char *)ap.ap.ssid, snapshot.ap_ssid, sizeof(ap.ap.ssid));
        ap.ap.ssid_len = strlen(snapshot.ap_ssid);
        strlcpy((char *)ap.ap.password, snapshot.ap_password, sizeof(ap.ap.password));
        // Configure WPA2 before starting the AP, avoiding a transient default AP.
        err = esp_wifi_stop();
        const bool stopped = err == ESP_OK;
        if (err == ESP_OK) {
            err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        }
        if (err == ESP_OK) {
            err = esp_wifi_set_config(WIFI_IF_AP, &ap);
        }
        memset(&ap, 0, sizeof(ap));
        if (err == ESP_OK) {
            err = esp_wifi_start();
        }
        if (err == ESP_OK) {
            err = start_http();
        }
        if (err != ESP_OK) {
            if (server != NULL) {
                httpd_stop(server);
                server = NULL;
            }
            esp_wifi_set_mode(WIFI_MODE_STA);
            if (stopped) {
                esp_wifi_start();
            }
            set_state(WIFI_SETUP_FAILED, "Setup could not start. Hold KEY to retry.");
            ESP_LOGE(TAG, "Portal start: %s", esp_err_to_name(err));
            return;
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        status.portal_active = true;
        xSemaphoreGive(lock);
    }
    set_state(WIFI_SETUP_READY, "Choose a 2.4 GHz Wi-Fi network and enter its password.");
    ESP_LOGI(TAG, "Setup hotspot: %s; http://" AP_ADDRESS, snapshot.ap_ssid);
    start_scan();
}

static void connection_failed(bool timeout)
{
    const bool was_testing = testing;
    xSemaphoreTake(lock, portMAX_DELAY);
    const unsigned reason = disconnect_reason;
    xSemaphoreGive(lock);
    disconnect_station();
    if (was_testing) {
        const bool auth_error = reason == WIFI_REASON_AUTH_FAIL ||
                                reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                                reason == WIFI_REASON_HANDSHAKE_TIMEOUT;
        set_state(WIFI_SETUP_FAILED, timeout ? "Connection timed out. Check signal and password, then retry." :
                  auth_error ? "Authentication failed. Check the password and router settings." :
                  "Connection failed. Check Wi-Fi name, signal and password, then retry.");
    } else {
        set_state(WIFI_SETUP_RETRYING, "Wi-Fi offline. Retrying automatically; hold KEY to reconfigure.");
        retry_at = now_ms() + BOARD_WIFI_RETRY_INTERVAL_MS;
    }
    ESP_LOGW(TAG, "Connection failed: reason=%u, timeout=%d", reason, timeout);
}

static void got_ip(void)
{
    // Confirm that a delayed old event cannot persist a replacement credential.
    wifi_ap_record_t ap;
    esp_netif_ip_info_t info;
    if (!connecting || esp_wifi_sta_get_ap_info(&ap) != ESP_OK ||
        memcmp(ap.ssid, target.ssid, strlen(target.ssid)) != 0 ||
        strnlen((const char *)ap.ssid, sizeof(ap.ssid)) != strlen(target.ssid) ||
        esp_netif_get_ip_info(station, &info) != ESP_OK || info.ip.addr == 0) {
        return;
    }
    if (testing) {
        esp_err_t err = nvs_set_blob(storage, "credentials", &target, sizeof(target));
        if (err == ESP_OK) {
            err = nvs_commit(storage);
        }
        if (err != ESP_OK) {
            disconnect_station();
            set_state(WIFI_SETUP_FAILED, "Could not save Wi-Fi settings. Please retry.");
            ESP_LOGE(TAG, "Credential save: %s", esp_err_to_name(err));
            return;
        }
        saved = target;
        have_saved = true;
    }
    connecting = testing = false;
    retry_at = 0;
    set_state(WIFI_SETUP_CONNECTED, "Connected! Settings saved. The hotspot will close in 10 seconds.");
    xSemaphoreTake(lock, portMAX_DELAY);
    snprintf(status.ip, sizeof(status.ip), IPSTR, IP2STR(&info.ip));
    if (status.portal_active) {
        close_at = now_ms() + BOARD_WIFI_PORTAL_GRACE_MS;
    }
    xSemaphoreGive(lock);
    ESP_LOGI(TAG, "Wi-Fi connected, IP: " IPSTR, IP2STR(&info.ip));
}

esp_err_t wifi_setup_start_portal(void)
{
    if (commands == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const command_t cmd = {.type = COMMAND_ENTER_PORTAL};
    return xQueueSend(commands, &cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void network_task(void *arg)
{
    if (have_saved) {
        begin_connection(&saved, false);
    } else {
        enter_portal();
    }
    while (true) {
        command_t cmd;
        if (xQueueReceive(commands, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (cmd.type == COMMAND_CONNECT) {
                begin_connection(&cmd.credentials, true);
            } else if (cmd.type == COMMAND_SCAN) {
                start_scan();
            } else if (cmd.type == COMMAND_ENTER_PORTAL) {
                enter_portal();
            }
            memset(&cmd, 0, sizeof(cmd));
        }
        const EventBits_t bits = xEventGroupWaitBits(events,
            EVENT_IP | EVENT_DISCONNECTED | EVENT_SCAN_DONE | EVENT_LOST_IP, pdTRUE, pdFALSE, 0);
        if (bits & EVENT_SCAN_DONE) {
            finish_scan();
        }
        if (bits & EVENT_IP) {
            got_ip();
        }
        if (bits & EVENT_DISCONNECTED) {
            wifi_ap_record_t ap;
            wifi_setup_status_t snapshot;
            wifi_setup_get_status(&snapshot);
            if ((connecting || snapshot.state == WIFI_SETUP_CONNECTED) &&
                esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
                connection_failed(false);
            }
        }
        if ((bits & EVENT_LOST_IP) && !connecting) {
            esp_netif_ip_info_t info;
            wifi_setup_status_t snapshot;
            wifi_setup_get_status(&snapshot);
            if (snapshot.state == WIFI_SETUP_CONNECTED &&
                esp_netif_get_ip_info(station, &info) == ESP_OK && info.ip.addr == 0) {
                connection_failed(false);
            }
        }
        const int64_t now = now_ms();
        if (connecting && now >= connect_deadline) {
            connection_failed(true);
        }
        if (retry_at != 0 && now >= retry_at && have_saved) {
            begin_connection(&saved, false);
        }
        if (close_at != 0 && now >= close_at) {
            // Mark unavailable before shutting down HTTP, preventing new submissions.
            xSemaphoreTake(lock, portMAX_DELAY);
            status.portal_active = false;
            xSemaphoreGive(lock);
            if (server != NULL) {
                httpd_stop(server);
                server = NULL;
            }
            const esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
            ESP_LOGI(TAG, "Setup hotspot stopped: %s", esp_err_to_name(err));
            close_at = 0;
        }
    }
}

esp_err_t wifi_setup_init(void)
{
    lock = xSemaphoreCreateMutex();
    events = xEventGroupCreate();
    commands = xQueueCreate(4, sizeof(command_t));
    ESP_RETURN_ON_FALSE(lock && events && commands, ESP_ERR_NO_MEM, TAG, "Sync allocation");
    ESP_RETURN_ON_ERROR(nvs_flash_init(), TAG, "NVS init (existing data preserved)");
    ESP_RETURN_ON_ERROR(nvs_open("wifi_setup", NVS_READWRITE, &storage), TAG, "NVS open");
    size_t size = sizeof(saved);
    esp_err_t err = nvs_get_blob(storage, "credentials", &saved, &size);
    have_saved = err == ESP_OK && size == sizeof(saved) &&
                 memchr(saved.ssid, 0, sizeof(saved.ssid)) != NULL &&
                 memchr(saved.password, 0, sizeof(saved.password)) != NULL &&
                 portal_credentials_valid(saved.ssid, saved.password);
    if (!have_saved) {
        memset(&saved, 0, sizeof(saved));
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Stored credentials unavailable/invalid: %s", esp_err_to_name(err));
        }
    }
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Event loop");
    station = esp_netif_create_default_wifi_sta();
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(station && ap, ESP_ERR_NO_MEM, TAG, "Netif allocation");
    const esp_netif_ip_info_t ap_ip = {
        .ip.addr = ESP_IP4TOADDR(192, 168, 4, 1),
        .gw.addr = ESP_IP4TOADDR(192, 168, 4, 1),
        .netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0),
    };
    ESP_RETURN_ON_ERROR(esp_netif_dhcps_stop(ap), TAG, "DHCP stop");
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(ap, &ap_ip), TAG, "AP address");
    ESP_RETURN_ON_ERROR(esp_netif_dhcps_start(ap), TAG, "DHCP start");
    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Wi-Fi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Wi-Fi RAM storage");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL), TAG, "Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL), TAG, "IP events");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "STA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start");
    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), TAG, "AP MAC");
    snprintf(status.ap_ssid, sizeof(status.ap_ssid), "RLCD-%02X%02X%02X", mac[3], mac[4], mac[5]);
    size = sizeof(status.ap_password);
    err = nvs_get_str(storage, "ap_password", status.ap_password, &size);
    if (err != ESP_OK || strlen(status.ap_password) != 8 ||
        strspn(status.ap_password, "0123456789") != 8) {
        // Also replaces the former 12-character hexadecimal password in NVS.
        uint8_t random[8];
        esp_fill_random(random, sizeof(random));
        for (unsigned i = 0; i < sizeof(random); ++i) {
            status.ap_password[i] = '0' + random[i] % 10;
        }
        status.ap_password[sizeof(random)] = '\0';
        ESP_RETURN_ON_ERROR(nvs_set_str(storage, "ap_password", status.ap_password), TAG, "AP password save");
        ESP_RETURN_ON_ERROR(nvs_commit(storage), TAG, "AP password commit");
    }
    uint8_t token[16];
    esp_fill_random(token, sizeof(token));
    for (unsigned i = 0; i < sizeof(token); ++i) {
        snprintf(setup_token + i * 2, 3, "%02X", token[i]);
    }
    ESP_RETURN_ON_FALSE(xTaskCreate(dns_task, "portal_dns", 3072, NULL, 3, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "DNS task");
    ESP_RETURN_ON_FALSE(xTaskCreate(network_task, "wifi_setup", 6144, NULL, 4, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "Network task");
    return ESP_OK;
}
