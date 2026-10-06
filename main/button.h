#pragma once

#include "esp_err.h"

typedef enum {
    BUTTON_NONE,
    BUTTON_SHORT_PRESS,
    BUTTON_LONG_PRESS,
    BUTTON_BOOT_SHORT_PRESS,
} button_event_t;

esp_err_t button_init(void);
// Poll from the UI task every 20 ms; no drawing or network work here.
button_event_t button_poll(void);
