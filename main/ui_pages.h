#pragma once

#include "ui.h"
#include "wifi_setup.h"

typedef struct {
    bool valid;
    float temperature;
    float humidity;
    const char *status;
    bool wifi_available;
    wifi_setup_status_t wifi;
    ui_system_info_t system;
} ui_model_t;

void draw_sensor_page(const ui_model_t *model);
void draw_network_page(const ui_model_t *model);
void draw_system_page(const ui_model_t *model);
void draw_setup_page(const ui_model_t *model);
void draw_page_footer(ui_page_t page, bool setup);
