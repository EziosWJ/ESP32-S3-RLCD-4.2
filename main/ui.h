#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "battery.h"
#include "button.h"

typedef enum {
    PAGE_SENSOR,
    PAGE_CODECK,
    PAGE_NETWORK,
    PAGE_COUNT,
} ui_page_t;

typedef struct {
    uint32_t flash_bytes;
    size_t psram_bytes;
    unsigned cores;
} ui_system_info_t;

typedef struct {
    size_t app_image_bytes;
    size_t app_partition_bytes;
    size_t internal_heap_total_bytes;
    size_t internal_heap_free_bytes;
    size_t internal_heap_min_free_bytes;
    size_t internal_heap_largest_free_block;
    size_t psram_heap_total_bytes;
    size_t psram_heap_free_bytes;
    size_t psram_heap_min_free_bytes;
    size_t psram_heap_largest_free_block;
} ui_memory_info_t;

// All UI calls belong to app_main, the sole framebuffer owner.
void ui_init(bool wifi_available, const ui_system_info_t *system);
void ui_set_page(ui_page_t page);
void ui_next_page(void);
void ui_scroll(void);
bool ui_can_setup(void);
// Applies navigation; true asks app_main to queue a Wi-Fi setup request.
bool ui_handle_button(button_event_t event);
void ui_show_setup(void);
void ui_update_measurement(bool valid, float temperature, float humidity, const char *status);
void ui_update_battery(const battery_reading_t *reading);
void ui_update_memory_info(const ui_memory_info_t *memory);
// Checks network changes and flushes only when a redraw is needed.
esp_err_t ui_render(void);
