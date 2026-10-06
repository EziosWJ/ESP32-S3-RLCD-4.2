#pragma once
#include <stdbool.h>

typedef struct {
    bool time_valid, connected, connecting, signal_valid, battery_valid;
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
