#include "ui.h"

#include <string.h>
#include "ui_pages.h"
#include "rlcd.h"

static ui_page_t current_page;
static ui_model_t model;
static bool dirty, setup_visible, portal_was_active;

void ui_init(bool wifi_available, const ui_system_info_t *system)
{
    memset(&model, 0, sizeof(model));
    model.wifi_available = wifi_available;
    model.system = *system;
    model.status = "READING SENSOR";
    current_page = PAGE_SENSOR;
    dirty = true;
    setup_visible = portal_was_active = false;
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
            strcmp(wifi.ip, model.wifi.ip) != 0 ||
            strcmp(wifi.ap_ssid, model.wifi.ap_ssid) != 0 ||
            strcmp(wifi.ap_password, model.wifi.ap_password) != 0) {
            model.wifi = wifi;
            dirty = true;
        }
    }
    if (!dirty) {
        return ESP_OK;
    }
    rlcd_clear();
    if (setup_visible) {
        draw_setup_page(&model);
    } else {
        switch (current_page) {
        case PAGE_SENSOR: draw_sensor_page(&model); break;
        case PAGE_NETWORK: draw_network_page(&model); break;
        case PAGE_SYSTEM: draw_system_page(&model); break;
        default: break;
        }
    }
    draw_page_footer(current_page, setup_visible);
    const esp_err_t err = rlcd_flush();
    if (err == ESP_OK) {
        dirty = false;
    }
    return err;
}
