#include "ui.h"

#include <string.h>
#include "ui_pages.h"
#include "rlcd.h"
#include "codeck_client.h"
#include "codeck_page.h"
#include "ui_chrome.h"

static ui_page_t current_page;
static ui_model_t model;
static bool dirty, setup_visible, portal_was_active;
static unsigned codeck_frame;
static unsigned network_frame;
static codeck_snapshot_t codeck, codeck_next;

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
    memset(&codeck, 0, sizeof(codeck));
    codeck.status = CODECK_WAIT_NETWORK;
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
        network_frame = (network_frame + 1) % 2;
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
    if (current_page == PAGE_SENSOR || setup_visible) {
        dirty = true;
    }
}

void ui_update_battery(const battery_reading_t *reading)
{
    const bool changed = model.battery.valid != reading->valid ||
                         model.battery.detected != reading->detected ||
                         model.battery.voltage_mv != reading->voltage_mv ||
                         model.battery.percent != reading->percent;
    model.battery = *reading;
    if (changed) dirty = true;
}

esp_err_t ui_render(void)
{
    ha_snapshot_t ha;
    home_assistant_get_snapshot(&ha);
    for (unsigned i = 0; i < HA_DEVICE_COUNT; ++i) {
        const ha_device_t *old = &model.ha.devices[i];
        const ha_device_t *next = &ha.devices[i];
        if (old->temperature.status != next->temperature.status ||
            old->humidity.status != next->humidity.status ||
            old->temperature.value != next->temperature.value ||
            old->humidity.value != next->humidity.value) {
            if (current_page == PAGE_SENSOR && !setup_visible) dirty = true;
        }
    }
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
        .battery_percent = model.battery.percent,
    };
    ui_header_t timed_header = header;
    memcpy(timed_header.time, model.clock.time, 5);
    timed_header.time[5] = 0;
    const char *title = setup_visible ? "WIFI SETUP" : current_page == PAGE_SENSOR ? "SENSOR" :
                        current_page == PAGE_CODECK ? "CODECK" : "DEVICE";
    const unsigned frame = setup_visible ? 0 : current_page == PAGE_CODECK ? codeck_frame :
                           current_page == PAGE_NETWORK ? network_frame : 0;
    const unsigned count = setup_visible ? 1 : current_page == PAGE_CODECK ? codeck_page_count(&codeck) :
                           current_page == PAGE_NETWORK ? 2 : 1;
    ui_draw_header(title, &timed_header, frame, count);
    if (setup_visible) {
        draw_setup_page(&model);
    } else {
        switch (current_page) {
        case PAGE_SENSOR: draw_sensor_page(&model); break;
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
