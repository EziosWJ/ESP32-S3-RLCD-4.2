#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "ui_pages.h"
#include "codeck_page.h"
#include "codeck_client.h"
#include "codeck_label.h"
#include "display_time.h"
#include "driver/gpio.h"

static wifi_setup_status_t wifi;
static clock_display_t clock_value;
static codeck_snapshot_t snapshot;
static ha_snapshot_t ha;
static int64_t tick_ms;
static int levels[2] = {1, 1}, flushes, fail_flush;
static bool checking;
static char visible[8192];

int64_t esp_timer_get_time(void) { return tick_ms * 1000; }
esp_err_t gpio_config(const gpio_config_t *config)
{
    assert(config->pin_bit_mask == ((1ULL << 18) | 1));
    return ESP_OK;
}
int gpio_get_level(int pin) { assert(pin == 0 || pin == 18); return levels[pin == 0]; }
const char *esp_get_idf_version(void) { return "v6.1.0"; }
void wifi_setup_get_status(wifi_setup_status_t *out) { *out = wifi; }
void home_assistant_get_snapshot(ha_snapshot_t *out) { *out = ha; }
void clock_service_update(bool connected, clock_display_t *out) { (void)connected; *out = clock_value; }
void codeck_client_get_snapshot(codeck_snapshot_t *out) { *out = snapshot; }
void rlcd_clear(void) { visible[0] = 0; }
esp_err_t rlcd_flush(void) { ++flushes; return fail_flush ? -1 : ESP_OK; }
void rlcd_text(int x, int y, const char *text, unsigned scale)
{
    assert(x >= 0 && y >= 0 && y + 7 * (int)scale <= 300);
    assert(!text[0] || x + ((int)strlen(text) * 6 - 1) * (int)scale <= 400);
    assert(strlen(visible) + strlen(text) + 2 < sizeof(visible));
    strcat(visible, text); strcat(visible, "\n");
    if (!checking) printf("T %d %d %u %s\n", x, y, scale, text);
}
void rlcd_hline(int x, int y, int width)
{
    assert(x >= 0 && x + width <= 400 && y >= 0 && y < 300 && width >= 0);
    if (!checking) printf("H %d %d %d\n", x, y, width);
}
void rlcd_rect(int x, int y, int width, int height)
{
    assert(x >= 0 && y >= 0 && x + width <= 400 && y + height <= 300);
    if (!checking) printf("R %d %d %d %d\n", x, y, width, height);
}

static void expect(const char *text) { assert(ui_render() == ESP_OK); assert(strstr(visible, text)); }
static void event(button_event_t e) { assert(!ui_handle_button(e)); }
static button_event_t poll(int64_t ms, int key, int boot)
{
    tick_ms = ms; levels[0] = key; levels[1] = boot; return button_poll();
}
static void test_time(void)
{
    const char *inputs[] = {"2030-01-01T12:00:00Z", "2026-12-31T23:59:59Z", "2024-02-28T23:00:00Z",
                           "2024-02-29T23:00:00.123Z", "2026-10-06T14:20:00+08:00", "2026-01-01T00:01:00+12:00",
                           "2026-10-06T23:00:00-07:00", "2000-02-29T23:00:00Z"};
    const char *outputs[] = {"2030-01-01 20:00", "2027-01-01 07:59", "2024-02-29 07:00", "2024-03-01 07:00",
                            "2026-10-06 14:20", "2025-12-31 20:01", "2026-10-07 14:00", "2000-03-01 07:00"};
    char out[24];
    for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        assert(display_time_format(inputs[i], out, sizeof(out), true)); assert(!strcmp(out, outputs[i]));
    }
    const char *invalid[] = {"", "2026-02-29T00:00:00Z", "1900-02-29T00:00:00Z", "2026-13-01T00:00:00Z",
                            "2026-01-32T00:00:00Z", "2026-01-01T24:00:00Z", "2026-01-01T00:00:00",
                            "2026-01-01T00:00:00.Z", "2026-01-01T00:00:00+08:60", "2026-01-01T00:00:00Ztail",
                            "9999-12-31T23:00:00Z", "0001-01-01T00:00:00+23:59"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!display_time_format(invalid[i], out, sizeof(out), true)); assert(!strcmp(out, "--"));
    }
    assert(display_time_format(inputs[0], out, sizeof(out), false) && !strcmp(out, "01-01 20:00"));
    assert(!display_time_format(inputs[0], out, 3, true) && !strcmp(out, "--"));
}

static void test_buttons(void)
{
    tick_ms = 0; assert(button_init() == ESP_OK);
    assert(poll(10,0,1) == BUTTON_NONE); assert(poll(20,1,1) == BUTTON_NONE);
    assert(poll(100,0,1) == BUTTON_NONE); assert(poll(140,0,1) == BUTTON_NONE);
    assert(poll(220,1,1) == BUTTON_NONE); assert(poll(260,1,1) == BUTTON_SHORT_PRESS);
    assert(poll(400,1,0) == BUTTON_NONE); assert(poll(440,1,0) == BUTTON_NONE);
    assert(poll(520,1,1) == BUTTON_NONE); assert(poll(560,1,1) == BUTTON_BOOT_SHORT_PRESS);
    assert(poll(600,0,1) == BUTTON_NONE); assert(poll(640,0,1) == BUTTON_NONE);
    assert(poll(3639,0,1) == BUTTON_NONE); assert(poll(3640,0,1) == BUTTON_LONG_PRESS);
    assert(poll(3700,0,1) == BUTTON_NONE); assert(poll(3720,1,1) == BUTTON_NONE);
    assert(poll(3760,1,1) == BUTTON_NONE);
    assert(poll(4000,1,0) == BUTTON_NONE); assert(poll(4040,1,0) == BUTTON_NONE);
    assert(poll(7040,1,0) == BUTTON_NONE); assert(poll(7100,1,1) == BUTTON_NONE);
    assert(poll(7140,1,1) == BUTTON_NONE);
    // Holds released between polls still suppress accidental short events.
    assert(poll(8000,0,1) == BUTTON_NONE); assert(poll(8040,0,1) == BUTTON_NONE);
    assert(poll(11200,1,1) == BUTTON_NONE); assert(poll(11240,1,1) == BUTTON_LONG_PRESS);
}

static void test_ui(void)
{
    checking = true;
    const ui_system_info_t system = {16U*1024*1024,8U*1024*1024,2};
    ui_init(true, &system);
    ui_update_measurement(true, 25, 60, "LIVE");
    const battery_reading_t battery = {.valid=true,.detected=true,.voltage_mv=3800,.percent=71};
    ui_update_battery(&battery);
    expect("SENSOR"); expect("71%");
    int n = flushes; assert(ui_render() == ESP_OK && flushes == n);
    event(BUTTON_BOOT_SHORT_PRESS); expect("SENSOR"); assert(!ui_handle_button(BUTTON_LONG_PRESS));
    event(BUTTON_SHORT_PRESS); expect("CODECK"); expect("5H"); expect("1/3");
    event(BUTTON_BOOT_SHORT_PRESS); expect("2/3"); expect("DEEPSEEK");
    tick_ms += 13000; expect("2/3");
    event(BUTTON_SHORT_PRESS); expect("DEVICE"); assert(ui_handle_button(BUTTON_LONG_PRESS));
    event(BUTTON_BOOT_SHORT_PRESS); expect("ESP32-S3 RLCD 4.2"); expect("2/2");
    assert(ui_handle_button(BUTTON_LONG_PRESS));
    event(BUTTON_SHORT_PRESS); expect("SENSOR");
    event(BUTTON_SHORT_PRESS); expect("2/3");
    event(BUTTON_BOOT_SHORT_PRESS); expect("3/3");
    event(BUTTON_BOOT_SHORT_PRESS); expect("5H"); expect("1/3");
    event(BUTTON_BOOT_SHORT_PRESS); event(BUTTON_BOOT_SHORT_PRESS); expect("3/3");
    snapshot.account_count = 1; ++snapshot.revision; expect("2/2");
    strcpy(clock_value.time, "15:21:00"); expect("15:21"); expect("2/2");
    wifi.state = WIFI_SETUP_RETRYING; wifi.signal_valid = false; expect("CODECK");
    event(BUTTON_SHORT_PRESS); expect("2/2");
    ui_show_setup(); expect("WIFI SETUP"); assert(!ui_handle_button(BUTTON_LONG_PRESS));
    event(BUTTON_BOOT_SHORT_PRESS); expect("WIFI SETUP");
    wifi.state = WIFI_SETUP_CONNECTED; expect("DEVICE"); expect("2/2");
    event(BUTTON_BOOT_SHORT_PRESS); expect("1/2");
    fail_flush = 1; event(BUTTON_BOOT_SHORT_PRESS); assert(ui_render() != ESP_OK);
    fail_flush = 0; expect("2/2");
    ui_init(true, &system); expect("SENSOR");
    snapshot.has_snapshot = false; snapshot.status = CODECK_WAIT_TIME; ++snapshot.revision;
    event(BUTTON_SHORT_PRESS); expect("WAITING FOR SNTP"); expect("--");
    wifi.portal_active = true; wifi.state = WIFI_SETUP_READY; expect("WIFI SETUP");
    event(BUTTON_SHORT_PRESS); expect("CODECK");
    puts("UI integration: navigation, retained positions, update clamp, header refresh, provisioning, retry and dual-button debounce passed.");
}

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 7) return 1;
    FILE *file = fopen(argv[1], "rb"); if (!file) return 2;
    char body[4097]; const size_t length = fread(body,1,4096,file); fclose(file); body[length] = 0;
    assert(codeck_state_parse(body,length,&snapshot) == CODECK_OK);
    file = fopen(argv[2], "rb"); if (!file) return 3;
    unsigned char *font = malloc(921600); assert(font);
    assert(fread(font,1,921600,file) == 921600); fclose(file); codeck_font_use(font);
    wifi.state = WIFI_SETUP_CONNECTED; wifi.signal_valid = true; wifi.rssi = -60;
    strcpy(wifi.ip, "192.168.31.123"); strcpy(wifi.station_ssid, "HOME WIFI");
    strcpy(wifi.ap_ssid, "RLCD-ABCDEF"); strcpy(wifi.ap_password, "12345678");
    clock_value.valid = true; strcpy(clock_value.time, "14:20:00"); strcpy(clock_value.date,"2026-10-06");
    if (argc == 4) { test_time(); test_buttons(); test_ui(); }
    else {
        const int page = atoi(argv[3]), screen = atoi(argv[4]), scenario = atoi(argv[5]);
        snapshot.status = (codeck_status_t)atoi(argv[6]);
        const ui_system_info_t system = {16U*1024*1024,8U*1024*1024,2};
        ui_init(true, &system); ui_set_page((ui_page_t)page);
        ui_update_measurement(true,-49,100,"LIVE");
        battery_reading_t battery = {.valid=true,.detected=true,.percent=85,.voltage_mv=3800};
        if (scenario == 1) { wifi.state = WIFI_SETUP_RETRYING; wifi.signal_valid = false; clock_value.valid = false; battery.valid = false; }
        if (scenario == 2) wifi.rssi = -85;
        if (scenario == 3) wifi.signal_valid = false;
        if (scenario == 4) { snapshot.has_snapshot = false; ++snapshot.revision; }
        if (scenario == 5) { wifi.state = WIFI_SETUP_READY; wifi.portal_active = true; }
        if (scenario == 6) { snapshot.fetching = true; snapshot.retry_seconds = 4294967295U; }
        if (scenario == 7) wifi.state = WIFI_SETUP_CONNECTING;
        ui_update_battery(&battery);
        // Fetch data before calculating BOOT navigation, exactly as the device does.
        checking = true; assert(ui_render() == ESP_OK);
        for (int i = 0; i < screen; ++i) event(BUTTON_BOOT_SHORT_PRESS);
        checking = false;
        // Force a production redraw without replacing the chosen navigation state.
        battery.voltage_mv++; ui_update_battery(&battery); assert(ui_render() == ESP_OK);
    }
    free(font); return 0;
}
