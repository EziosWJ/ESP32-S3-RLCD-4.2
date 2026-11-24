#include "ui.h"

#include <string.h>
#include "ui_pages.h"
#include "rlcd.h"
#include "codeck_client.h"
#include "codeck_page.h"
#include "ui_chrome.h"
#include "board_config.h"
#include "esp_timer.h"

static ui_page_t current_page;
static ui_model_t model;
static bool dirty, setup_visible, portal_was_active;
#define NETWORK_SCREEN_COUNT 3U
#define SENSOR_SCREEN_COUNT 2U
static unsigned codeck_frame;
static unsigned network_frame;
static unsigned sensor_frame;
static codeck_snapshot_t codeck, codeck_next;

// Battery display debounce: a new candidate only reaches the submitted state
// after staying stable for BOARD_BATTERY_DISPLAY_DEBOUNCE_MS. Degradations
// (reading lost, pack dropped, percent estimate invalidated, state -> UNKNOWN)
// commit immediately so the display never keeps showing a stale charge.
typedef struct {
    battery_reading_t submitted;   // last value actually drawn
    battery_reading_t pending;    // candidate under observation
    bool have_submitted;
    bool pending_valid;
    int64_t pending_start_us;
} battery_debounce_t;

static battery_debounce_t battery_db;
static bool codeck_visible_sent;

static uint32_t quantize_mv(uint32_t mv)
{
    const uint32_t step = BOARD_BATTERY_DISPLAY_VOLTAGE_STEP_MV;
    return (mv + step / 2U) / step * step;
}

static bool battery_same(const battery_reading_t *a, const battery_reading_t *b)
{
    return a->valid == b->valid && a->detected == b->detected &&
           a->percent_valid == b->percent_valid &&
           quantize_mv(a->voltage_mv) == quantize_mv(b->voltage_mv) &&
           a->percent == b->percent && a->power_state == b->power_state;
}

// True when next is worse than submitted: a good signal dropping out. These
// transitions skip the settle window on purpose.
static bool battery_degraded(const battery_reading_t *old, const battery_reading_t *next)
{
    return (old->valid && !next->valid) ||
           (old->detected && !next->detected) ||
           (old->percent_valid && !next->percent_valid) ||
           (old->power_state != BATTERY_POWER_UNKNOWN &&
            next->power_state == BATTERY_POWER_UNKNOWN);
}

void ui_init(bool wifi_available, const ui_system_info_t *system)
{
    memset(&model, 0, sizeof(model));
    model.wifi_available = wifi_available;
    model.system = *system;
    model.status = "READING SENSOR";
    current_page = PAGE_SENSOR;
    dirty = true;
    setup_visible = portal_was_active = false;
    codeck_frame = network_frame = 0;
    sensor_frame = 0;
    memset(&codeck, 0, sizeof(codeck));
    codeck.status = CODECK_WAIT_NETWORK;
    memset(&battery_db, 0, sizeof(battery_db));
    codeck_visible_sent = false;
}

void ui_set_page(ui_page_t page)
{
    if ((unsigned)page >= PAGE_COUNT) {
        return;
    }
    if (current_page != page || setup_visible) {
        current_page = page;
        setup_visible = false;
        dirty = true;
    }
}

void ui_next_page(void)
{
    // First short press leaves the setup screen at the previously selected page.
    ui_set_page(setup_visible ? current_page : (ui_page_t)((current_page + 1) % PAGE_COUNT));
}

void ui_show_setup(void)
{
    setup_visible = true;
    dirty = true;
}

bool ui_can_setup(void)
{
    return !setup_visible && current_page == PAGE_NETWORK;
}

void ui_scroll(void)
{
    if (setup_visible) return;
    if (current_page == PAGE_CODECK) {
        codeck_frame = (codeck_frame + 1) % codeck_page_count(&codeck);
        dirty = true;
    } else if (current_page == PAGE_NETWORK) {
        network_frame = (network_frame + 1) % NETWORK_SCREEN_COUNT;
        dirty = true;
    } else if (current_page == PAGE_SENSOR) {
        sensor_frame = (sensor_frame + 1) % SENSOR_SCREEN_COUNT;
        dirty = true;
    }
}

bool ui_handle_button(button_event_t event)
{
    if (event == BUTTON_SHORT_PRESS) ui_next_page();
    else if (event == BUTTON_BOOT_SHORT_PRESS) ui_scroll();
    else if (event == BUTTON_LONG_PRESS) return ui_can_setup();
    return false;
}

void ui_update_measurement(bool valid, float temperature, float humidity, const char *status)
{
    model.valid = valid;
    model.temperature = temperature;
    model.humidity = humidity;
    model.status = status;
    if ((current_page == PAGE_SENSOR && sensor_frame == 0) || setup_visible) {
        dirty = true;
    }
}

void ui_update_battery(const battery_reading_t *reading)
{
    if (reading == NULL) return;
    // First reading lands immediately; the header must not start blank.
    if (!battery_db.have_submitted) {
        battery_db.submitted = *reading;
        battery_db.have_submitted = true;
        battery_db.pending_valid = false;
        dirty = true;
        return;
    }
    if (battery_same(&battery_db.submitted, reading)) {
        // Candidate drifted back to the drawn value: drop any pending change.
        battery_db.pending_valid = false;
        return;
    }
    if (battery_degraded(&battery_db.submitted, reading)) {
        battery_db.submitted = *reading;
        battery_db.pending_valid = false;
        dirty = true;
        return;
    }
    // Improvement or fluctuation: hold as pending until it settles. A new
    // candidate that differs from the current pending restarts the window;
    // one equal to the pending keeps the original start time.
    if (!battery_db.pending_valid || !battery_same(&battery_db.pending, reading)) {
        battery_db.pending = *reading;
        battery_db.pending_start_us = esp_timer_get_time();
        battery_db.pending_valid = true;
    }
}

// Commits a pending candidate once it has stayed stable through the debounce
// window. Called from ui_render, which runs every BOARD_UI_POLL_INTERVAL_MS, so
// a settled value lands on the next frame instead of waiting for a sample.
static void battery_debounce_land(void)
{
    if (!battery_db.pending_valid) return;
    const int64_t elapsed_ms =
        (esp_timer_get_time() - battery_db.pending_start_us) / 1000;
    if (elapsed_ms < (int64_t)BOARD_BATTERY_DISPLAY_DEBOUNCE_MS) return;
    battery_db.submitted = battery_db.pending;
    battery_db.pending_valid = false;
    dirty = true;
}

void ui_update_memory_info(const ui_memory_info_t *memory)
{
    if (memory == NULL) return;
    model.memory = *memory;
    if (current_page == PAGE_NETWORK && network_frame == 2 && !setup_visible) {
        dirty = true;
    }
}

esp_err_t ui_render(void)
{
    ha_snapshot_t ha;
    home_assistant_get_snapshot(&ha);
    bool ha_changed = false;
    for (unsigned i = 0; i < HA_DEVICE_COUNT; ++i) {
        const ha_device_t *old = &model.ha.devices[i];
        const ha_device_t *next = &ha.devices[i];
        if (old->temperature.status != next->temperature.status ||
            old->humidity.status != next->humidity.status ||
            old->temperature.value != next->temperature.value ||
            old->humidity.value != next->humidity.value) {
            ha_changed = true;
        }
    }
    for (unsigned i = 0; i < HA_HOST_ENTITY_COUNT; ++i) {
        const ha_value_t *old = &model.ha.host[i];
        const ha_value_t *next = &ha.host[i];
        if (old->status != next->status || old->value != next->value ||
            strcmp(old->unit, next->unit) != 0 || strcmp(old->text, next->text) != 0) {
            ha_changed = true;
        }
    }
    if (ha_changed && current_page == PAGE_SENSOR && !setup_visible) dirty = true;
    model.ha = ha;
    if (model.wifi_available) {
        wifi_setup_status_t wifi;
        wifi_setup_get_status(&wifi);
        const bool active = wifi.portal_active && wifi.state != WIFI_SETUP_CONNECTED;
        if (active && !portal_was_active) {
            ui_show_setup();
        }
        if (setup_visible && wifi.state == WIFI_SETUP_CONNECTED &&
            model.wifi.state != WIFI_SETUP_CONNECTED) {
            setup_visible = false;
            dirty = true;
        }
        portal_was_active = active;
        if (wifi.state != model.wifi.state || wifi.portal_active != model.wifi.portal_active ||
            wifi.signal_valid != model.wifi.signal_valid || wifi.rssi != model.wifi.rssi ||
            strcmp(wifi.station_ssid, model.wifi.station_ssid) != 0 ||
            strcmp(wifi.ip, model.wifi.ip) != 0 ||
            strcmp(wifi.ap_ssid, model.wifi.ap_ssid) != 0 ||
            strcmp(wifi.ap_password, model.wifi.ap_password) != 0) {
            model.wifi = wifi;
            dirty = true;
        }
    }
    clock_display_t clock;
    clock_service_update(model.wifi_available && model.wifi.state == WIFI_SETUP_CONNECTED, &clock);
    if (clock.valid != model.clock.valid || strcmp(clock.date, model.clock.date) != 0 ||
        strncmp(clock.time, model.clock.time, 5) != 0) {
        model.clock = clock;
        dirty = true;
    }
    battery_debounce_land();
    model.battery = battery_db.submitted;
    // Codeck worker fetches only while its page is actually visible.
    const bool codeck_visible = current_page == PAGE_CODECK && !setup_visible;
    if (codeck_visible != codeck_visible_sent) {
        codeck_visible_sent = codeck_visible;
        codeck_client_set_visible(codeck_visible);
    }
    if (current_page == PAGE_CODECK && !setup_visible) {
        codeck_client_get_snapshot(&codeck_next);
        if(codeck_next.revision!=codeck.revision || codeck_next.status!=codeck.status ||
           codeck_next.has_snapshot!=codeck.has_snapshot ||
           codeck_next.retry_seconds!=codeck.retry_seconds || codeck_next.fetching!=codeck.fetching) {
            codeck=codeck_next; dirty=true;
        }
        const unsigned count = codeck_page_count(&codeck);
        if (codeck_frame >= count) {
            codeck_frame = count - 1;
            dirty = true;
        }
    }
    if (!dirty) {
        return ESP_OK;
    }
    rlcd_clear();
    const ui_header_t header = {
        .time_valid = model.clock.valid,
        .connected = model.wifi_available && model.wifi.state == WIFI_SETUP_CONNECTED,
        .connecting = model.wifi_available && (model.wifi.state == WIFI_SETUP_CONNECTING || model.wifi.state == WIFI_SETUP_STARTING),
        .signal_valid = model.wifi.signal_valid, .rssi = model.wifi.rssi,
        .battery_valid = model.battery.valid && model.battery.detected,
        .battery_percent_valid = model.battery.percent_valid,
        .battery_charging = model.battery.power_state == BATTERY_POWER_CHARGING,
        .battery_full = model.battery.power_state == BATTERY_POWER_FULL,
        .battery_state = model.battery.power_state,
        .battery_percent = model.battery.percent,
    };
    ui_header_t timed_header = header;
    memcpy(timed_header.time, model.clock.time, 5);
    timed_header.time[5] = 0;
    const char *title = setup_visible ? "WIFI SETUP" : current_page == PAGE_SENSOR ? "SENSOR" :
                        current_page == PAGE_CODECK ? "CODECK" : "DEVICE";
    const unsigned frame = setup_visible ? 0 : current_page == PAGE_CODECK ? codeck_frame :
                           current_page == PAGE_NETWORK ? network_frame :
                           current_page == PAGE_SENSOR ? sensor_frame : 0;
    const unsigned count = setup_visible ? 1 : current_page == PAGE_CODECK ? codeck_page_count(&codeck) :
                           current_page == PAGE_NETWORK ? NETWORK_SCREEN_COUNT :
                           current_page == PAGE_SENSOR ? SENSOR_SCREEN_COUNT : 1;
    ui_draw_header(title, &timed_header, frame, count);
    if (setup_visible) {
        draw_setup_page(&model);
    } else {
        switch (current_page) {
        case PAGE_SENSOR: draw_sensor_page(&model, sensor_frame); break;
        case PAGE_CODECK: draw_codeck_page(&codeck,codeck_frame); break;
        case PAGE_NETWORK: draw_network_page(&model, network_frame); break;
        default: break;
        }
    }
    const esp_err_t err = rlcd_flush();
    if (err == ESP_OK) {
        dirty = false;
    }
    return err;
}
