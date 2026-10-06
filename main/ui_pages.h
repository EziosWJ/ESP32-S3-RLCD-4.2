#pragma once

#include "ui.h"
#include "wifi_setup.h"
#include "home_assistant.h"
#include "clock_service.h"

typedef struct {
    bool valid;
    float temperature;
    float humidity;
    const char *status;
    bool wifi_available;
    wifi_setup_status_t wifi;
    ha_snapshot_t ha;
    ui_system_info_t system;
    ui_memory_info_t memory;
    clock_display_t clock;
    battery_reading_t battery;
} ui_model_t;

void draw_sensor_page(const ui_model_t *model);
void draw_network_page(const ui_model_t *model, unsigned screen);
void draw_setup_page(const ui_model_t *model);
