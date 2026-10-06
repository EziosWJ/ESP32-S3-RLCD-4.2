#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

typedef enum {
    PAGE_SENSOR,
    PAGE_NETWORK,
    PAGE_SYSTEM,
    PAGE_COUNT,
} ui_page_t;

typedef struct {
    uint32_t flash_bytes;
    size_t psram_bytes;
    unsigned cores;
} ui_system_info_t;

// All UI calls belong to app_main, the sole framebuffer owner.
void ui_init(bool wifi_available, const ui_system_info_t *system);
void ui_set_page(ui_page_t page);
void ui_next_page(void);
void ui_show_setup(void);
void ui_update_measurement(bool valid, float temperature, float humidity, const char *status);
// Checks network changes and flushes only when a redraw is needed.
esp_err_t ui_render(void);
