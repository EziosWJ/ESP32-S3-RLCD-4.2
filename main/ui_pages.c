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

void draw_sensor_page(const ui_model_t *m)
{
    char text[32];
    header("TEMPERATURE AND RH");
    rlcd_text(20, 72, "TEMPERATURE", 2);
    if (m->valid) {
        snprintf(text, sizeof(text), "%5.1f C", (double)m->temperature);
    } else {
        snprintf(text, sizeof(text), " --.- C");
    }
    rlcd_text(20, 96, text, 6);
    rlcd_text(20, 156, "HUMIDITY", 2);
    if (m->valid) {
        snprintf(text, sizeof(text), "%5.1f %%", (double)m->humidity);
    } else {
        snprintf(text, sizeof(text), " --.- %%");
    }
    rlcd_text(20, 180, text, 6);
    rlcd_text(20, 234, m->status, 1);
    snprintf(text, sizeof(text), "WIFI %s", network_state(m));
    rlcd_text(20, 250, text, 1);
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
