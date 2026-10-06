#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    bool valid;
    char date[11];
    char time[9];
} clock_display_t;

// Call after Wi-Fi initialization. All calls run in the main/UI task.
esp_err_t clock_service_init(void);
void clock_service_update(bool connected, clock_display_t *display);
// Thread-safe gate for TLS clients: true only after successful SNTP sync.
bool clock_service_is_synchronized(void);
