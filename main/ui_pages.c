#include "ui_pages.h"

#include <ctype.h>
#include <stdio.h>
#include "esp_system.h"
#include "board_config.h"
#include "rlcd.h"

static void header(const char *title)
{
    rlcd_text(20, 18, title, 3);
    rlcd_hline(20, 52, 360);
}

static const char *network_state(const ui_model_t *m)
{
    if (!m->wifi_available) {
        return "INIT ERROR";
    }
    switch (m->wifi.state) {
    case WIFI_SETUP_READY: return "SETUP READY";
    case WIFI_SETUP_CONNECTING: return "CONNECTING";
    case WIFI_SETUP_CONNECTED: return "CONNECTED";
    case WIFI_SETUP_RETRYING: return "OFFLINE - RETRYING";
    case WIFI_SETUP_FAILED: return "SETUP ERROR";
    default: return "STARTING";
    }
}

static const char *ha_status(ha_value_status_t status)
{
    switch (status) {
    case HA_LIVE: return "LIVE";
    case HA_OFFLINE: return "OFFLINE";
    case HA_AUTH_ERROR: return "AUTH ERROR";
    case HA_NOT_FOUND: return "NOT FOUND";
    case HA_READ_ERROR: return "READ ERROR";
    case HA_NO_TOKEN: return "NO TOKEN";
    case HA_STALE: return "STALE";
    default: return "READING";
    }
}

static void sensor_card(int y, const char *name, const char *status,
                        bool temp_valid, float temp, bool rh_valid, float rh)
{
    char text[24];
    rlcd_rect(20, y, 360, 62);
    rlcd_text(30, y + 7, name, 2);
    rlcd_text(276, y + 10, status, 1);
    if (temp_valid) snprintf(text, sizeof(text), "%.1f C", (double)temp);
    else snprintf(text, sizeof(text), "--.- C");
    rlcd_text(30, y + 30, text, 3);
    if (rh_valid) snprintf(text, sizeof(text), "%.1f %%", (double)rh);
    else snprintf(text, sizeof(text), "--.- %%");
    rlcd_text(222, y + 30, text, 3);
}

void draw_sensor_page(const ui_model_t *m)
{
    static const char *names[HA_DEVICE_COUNT] = {"LIVING ROOM", "STUDY"};
    header("HOME ENVIRONMENT");
    sensor_card(60, "LOCAL SHTC3", m->valid ? "LIVE" : "READ ERROR",
                m->valid, m->temperature, m->valid, m->humidity);
    for (unsigned i = 0; i < HA_DEVICE_COUNT; ++i) {
        const ha_device_t *d = &m->ha.devices[i];
        const ha_value_status_t status = d->temperature.status != HA_LIVE ?
                                          d->temperature.status : d->humidity.status;
        sensor_card(128 + (int)i * 68, names[i], ha_status(status),
                    d->temperature.status == HA_LIVE, d->temperature.value,
                    d->humidity.status == HA_LIVE, d->humidity.value);
    }
}

void draw_network_page(const ui_model_t *m)
{
    header("NETWORK STATUS");
    rlcd_text(20, 74, "WIFI", 2);
    rlcd_text(20, 100, network_state(m), 3);
    rlcd_text(20, 142, "DEVICE IP", 2);
    // The font has no slash/colon; dots and hyphens are supported.
    rlcd_text(20, 168, m->wifi.ip[0] ? m->wifi.ip : "--.--.--.--", 3);
    rlcd_text(20, 212, m->wifi_available && m->wifi.portal_active ?
              "SETUP HOTSPOT ACTIVE" : "SETUP HOTSPOT OFF", 2);
    rlcd_text(20, 242, "HOLD KEY TO OPEN WIFI SETUP", 1);
}

void draw_system_page(const ui_model_t *m)
{
    char text[64];
    header("SYSTEM INFO");
    rlcd_text(20, 74, "ESP32-S3 RLCD 4.2", 2);
    snprintf(text, sizeof(text), "CPU CORES %u", m->system.cores);
    rlcd_text(20, 110, text, 2);
    snprintf(text, sizeof(text), "FLASH %u MB", (unsigned)(m->system.flash_bytes / (1024U * 1024U)));
    rlcd_text(20, 140, text, 2);
    snprintf(text, sizeof(text), "PSRAM %u MB", (unsigned)(m->system.psram_bytes / (1024U * 1024U)));
    rlcd_text(20, 170, text, 2);
    snprintf(text, sizeof(text), "DISPLAY %u X %u", BOARD_RLCD_WIDTH, BOARD_RLCD_HEIGHT);
    rlcd_text(20, 200, text, 2);
    snprintf(text, sizeof(text), "ESP-IDF %.44s", esp_get_idf_version());
    for (char *p = text; *p != '\0'; ++p) {
        *p = (char)toupper((unsigned char)*p);
    }
    rlcd_text(20, 238, text, 1);
}

void draw_setup_page(const ui_model_t *m)
{
    char text[40];
    header("WIFI SETUP");
    rlcd_text(20, 68, "CONNECT PHONE TO", 2);
    rlcd_text(20, 92, m->wifi.ap_ssid, 3);
    rlcd_text(20, 124, "HOTSPOT PASSWORD", 2);
    rlcd_text(20, 148, m->wifi.ap_password, 3);
    rlcd_text(20, 182, "OPEN 192.168.4.1", 2);
    rlcd_text(20, 210, m->wifi.state == WIFI_SETUP_CONNECTING ? "CONNECTING - PLEASE WAIT" :
              m->wifi.state == WIFI_SETUP_FAILED ? "FAILED - RETRY IN BROWSER" :
              "SELECT WIFI IN BROWSER", 2);
    if (m->valid) {
        snprintf(text, sizeof(text), "T %.1f C  RH %.1f %%", (double)m->temperature, (double)m->humidity);
        rlcd_text(20, 242, text, 2);
    } else {
        rlcd_text(20, 242, m->status, 1);
    }
}

void draw_page_footer(ui_page_t page, bool setup)
{
    static const char *labels[] = {"1 OF 3  SENSOR", "2 OF 3  NETWORK", "3 OF 3  SYSTEM"};
    rlcd_hline(20, 268, 360);
    rlcd_text(20, 277, setup ? "SETUP  KEY TO BROWSE" : labels[page], 1);
    rlcd_text(20, 290, "KEY NEXT  HOLD 3 SECONDS WIFI SETUP", 1);
}
