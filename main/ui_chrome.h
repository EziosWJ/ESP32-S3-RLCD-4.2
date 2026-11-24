#pragma once
#include <stdbool.h>
#include "battery.h"

typedef struct {
    bool time_valid, connected, connecting, signal_valid, battery_valid;
    // Battery display semantics: percent_valid means the shown percent is a
    // trustworthy state-of-charge; charging suppresses the percent, full draws
    // a full bar. Unknown / not detected falls back to "--%".
    bool battery_percent_valid, battery_charging, battery_full;
    battery_power_state_t battery_state;
    char time[6];
    int rssi;
    unsigned battery_percent;
} ui_header_t;

typedef enum { UI_ICON_CLI, UI_ICON_RUN, UI_ICON_CLOCK, UI_ICON_SERVICE,
               UI_ICON_WIFI, UI_ICON_CHIP, UI_ICON_BATTERY, UI_ICON_WALLET } ui_icon_t;

void ui_draw_header(const char *title, const ui_header_t *header, unsigned screen, unsigned count);
void ui_draw_card(int x, int y, int width, int height);
void ui_draw_icon(ui_icon_t icon, int x, int y);
// Narrow digits keep even contract-length decimal amounts complete on screen.
void ui_draw_decimal(int x, int y, const char *decimal);
