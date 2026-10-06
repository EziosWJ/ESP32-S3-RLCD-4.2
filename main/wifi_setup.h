#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    WIFI_SETUP_STARTING,
    WIFI_SETUP_READY,
    WIFI_SETUP_CONNECTING,
    WIFI_SETUP_CONNECTED,
    WIFI_SETUP_RETRYING,
    WIFI_SETUP_FAILED,
} wifi_setup_state_t;

typedef struct {
    wifi_setup_state_t state;
    bool portal_active;
    char ap_ssid[20];
    char ap_password[9];
    char ip[16];
    char station_ssid[33];
    bool signal_valid;
    int rssi;
    char message[128];
} wifi_setup_status_t;

// Starts the network worker; drawing remains exclusively in app_main.
esp_err_t wifi_setup_init(void);
void wifi_setup_get_status(wifi_setup_status_t *status);
// Queues a request; the network worker owns all portal lifecycle operations.
esp_err_t wifi_setup_start_portal(void);
// Recover a stalled station connection through the network worker.
// Ignored while the setup portal or another connection is active.
esp_err_t wifi_setup_request_reconnect(void);
