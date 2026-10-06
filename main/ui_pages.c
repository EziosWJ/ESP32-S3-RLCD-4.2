#include "ui_pages.h"

#include <ctype.h>
#include <stdio.h>
#include "esp_system.h"
#include "board_config.h"
#include "rlcd.h"
#include "ui_chrome.h"
#include "codeck_label.h"

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
    ui_draw_card(20, y, 360, 62);
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

static unsigned usage_percent(size_t total, size_t free_bytes)
{
    if (total == 0) return 0;
    if (free_bytes > total) free_bytes = total;
    return (unsigned)(((total - free_bytes) * 100U + total / 2U) / total);
}

static void draw_usage_bar(int x, int y, int width, int height, unsigned percent)
{
    if (percent > 100) percent = 100;
    rlcd_rect(x, y, width, height);
    const int fill_width = (width - 4) * (int)percent / 100;
    for (int row = y + 2; row < y + height - 2; ++row) {
        rlcd_hline(x + 2, row, fill_width);
    }
}

static void usage_card(int y, const char *title, bool valid, unsigned percent,
                       const char *value, const char *detail)
{
    char text[16];
    ui_draw_card(20, y, 360, 72);
    rlcd_text(30, y + 5, title, 2);
    if (valid) snprintf(text, sizeof(text), "%u%%", percent);
    else snprintf(text, sizeof(text), "--%%");
    rlcd_text(316, y + 5, text, 2);
    rlcd_text(30, y + 24, value, 1);
    draw_usage_bar(30, y + 37, 340, 10, valid ? percent : 0);
    rlcd_text(30, y + 53, detail, 1);
}

static void draw_memory_page(const ui_model_t *m)
{
    char value[48], detail[48];
    const ui_memory_info_t *memory = &m->memory;
    const bool flash_valid = memory->app_image_bytes > 0 && memory->app_partition_bytes > 0;
    const size_t app_image = memory->app_image_bytes;
    const size_t app_partition = memory->app_partition_bytes;
    unsigned flash_percent = flash_valid ? (unsigned)(app_image * 100U / app_partition) : 0;
    if (flash_percent > 100) flash_percent = 100;
    if (flash_valid) {
        snprintf(value, sizeof(value), "IMAGE %u KB / SLOT %u KB",
                 (unsigned)(app_image / 1024U), (unsigned)(app_partition / 1024U));
        snprintf(detail, sizeof(detail), "APP SLOT FREE %u KB",
                 (unsigned)((app_partition > app_image ? app_partition - app_image : 0) / 1024U));
    } else {
        snprintf(value, sizeof(value), "APP IMAGE UNAVAILABLE");
        snprintf(detail, sizeof(detail), "PARTITION DATA UNAVAILABLE");
    }
    usage_card(61, "APP FLASH", flash_valid, flash_percent, value, detail);

    const size_t internal_total = memory->internal_heap_total_bytes;
    const size_t internal_free = memory->internal_heap_free_bytes;
    const bool internal_valid = internal_total > 0;
    const unsigned internal_percent = usage_percent(internal_total, internal_free);
    snprintf(value, sizeof(value), "USED %u KB  FREE %u KB",
             (unsigned)((internal_total > internal_free ? internal_total - internal_free : 0) / 1024U),
             (unsigned)(internal_free / 1024U));
    snprintf(detail, sizeof(detail), "LOW %u KB  MAX BLOCK %u KB",
             (unsigned)(memory->internal_heap_min_free_bytes / 1024U),
             (unsigned)(memory->internal_heap_largest_free_block / 1024U));
    usage_card(137, "INTERNAL HEAP", internal_valid, internal_percent,
               internal_valid ? value : "NOT AVAILABLE", internal_valid ? detail : "HEAP UNAVAILABLE");

    const size_t psram_total = memory->psram_heap_total_bytes;
    const size_t psram_free = memory->psram_heap_free_bytes;
    const bool psram_valid = psram_total > 0;
    const unsigned psram_percent = usage_percent(psram_total, psram_free);
    snprintf(value, sizeof(value), "USED %u KB  FREE %u KB",
             (unsigned)((psram_total > psram_free ? psram_total - psram_free : 0) / 1024U),
             (unsigned)(psram_free / 1024U));
    snprintf(detail, sizeof(detail), "LOW %u KB  MAX BLOCK %u KB",
             (unsigned)(memory->psram_heap_min_free_bytes / 1024U),
             (unsigned)(memory->psram_heap_largest_free_block / 1024U));
    usage_card(213, "PSRAM HEAP", psram_valid, psram_percent,
               psram_valid ? value : "NOT AVAILABLE", psram_valid ? detail : "PSRAM UNAVAILABLE");
}

void draw_network_page(const ui_model_t *m, unsigned screen)
{
    char text[64];
    if (screen == 0) {
        ui_draw_card(20, 64, 360, 140);
        ui_draw_icon(UI_ICON_WIFI, 30, 76);
        rlcd_text(52, 76, network_state(m), 2);
        const bool connected = m->wifi_available && m->wifi.state == WIFI_SETUP_CONNECTED;
        codeck_label_text(30, 102, connected && m->wifi.station_ssid[0] ? m->wifi.station_ssid : "NO CONNECTED NETWORK", 340);
        rlcd_text(30, 128, "DEVICE IP", 1);
        if (connected && m->wifi.signal_valid)
            snprintf(text, sizeof(text), "SIGNAL %d DBM", m->wifi.rssi);
        else snprintf(text, sizeof(text), "SIGNAL --");
        rlcd_text(230, 128, text, 1);
        rlcd_text(30, 151, connected && m->wifi.ip[0] ? m->wifi.ip : "--.--.--.--", 3);
        rlcd_text(30, 186, "2.4 GHZ WIFI", 1);
        ui_draw_card(20, 216, 360, 78);
        ui_draw_icon(UI_ICON_SERVICE, 30, 229);
        rlcd_text(52, 229, m->wifi_available && m->wifi.portal_active ? "HOTSPOT ACTIVE" : "HOTSPOT OFF", 2);
        rlcd_text(30, 254, "HOLD KEY 3S TO SET UP WIFI", 2);
        rlcd_text(30, 280, "BOOT NEXT CARD SCREEN", 1);
        return;
    }
    if (screen >= 2) {
        draw_memory_page(m);
        return;
    }
    ui_draw_card(20, 64, 360, 102);
    ui_draw_icon(UI_ICON_CHIP, 30, 76);
    rlcd_text(52, 76, "ESP32-S3 RLCD 4.2", 2);
    snprintf(text, sizeof(text), "CORES %u", m->system.cores);
    rlcd_text(30, 101, text, 2);
    snprintf(text, sizeof(text), "FLASH %u MB", (unsigned)(m->system.flash_bytes / (1024U * 1024U)));
    rlcd_text(190, 101, text, 2);
    snprintf(text, sizeof(text), "PSRAM %u MB", (unsigned)(m->system.psram_bytes / (1024U * 1024U)));
    rlcd_text(30, 125, text, 2);
    snprintf(text, sizeof(text), "LCD %u X %u", BOARD_RLCD_WIDTH, BOARD_RLCD_HEIGHT);
    rlcd_text(190, 125, text, 2);
    snprintf(text, sizeof(text), "ESP-IDF %.44s", esp_get_idf_version());
    for (char *p = text; *p; ++p) *p = (char)toupper((unsigned char)*p);
    rlcd_text(30, 150, text, 1);
    ui_draw_card(20, 176, 360, 72);
    ui_draw_icon(UI_ICON_BATTERY, 30, 186);
    rlcd_text(52, 186, "BATTERY / ESTIMATED", 2);
    if (m->battery.valid) {
        snprintf(text, sizeof(text), "%u.%03u V",
                 (unsigned)(m->battery.voltage_mv / 1000U),
                 (unsigned)(m->battery.voltage_mv % 1000U));
    } else {
        snprintf(text, sizeof(text), "--.--- V");
    }
    rlcd_text(30, 213, text, 2);
    if (m->battery.valid && m->battery.detected) {
        snprintf(text, sizeof(text), "%u%% EST", m->battery.percent);
    } else {
        snprintf(text, sizeof(text), "--%% EST");
    }
    rlcd_text(238, 213, text, 2);
    rlcd_text(30, 237, !m->battery.valid ? "READING UNKNOWN" :
              m->battery.detected ? "DETECTED BY VOLTAGE" : "NOT DETECTED BY VOLTAGE", 1);
    ui_draw_card(20, 258, 360, 36);
    ui_draw_icon(UI_ICON_CLOCK, 30, 268);
    snprintf(text, sizeof(text), "%s  UTC+8", m->clock.valid ? m->clock.date : "---- -- --");
    rlcd_text(52, 269, text, 2);
}

void draw_setup_page(const ui_model_t *m)
{
    char text[40];
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
    rlcd_text(20, 280, "KEY TO BROWSE / HOTSPOT STAYS ON", 1);
}
