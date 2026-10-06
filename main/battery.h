#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool valid;
    // Voltage-based inference, not a physical battery insertion sensor.
    bool detected;
    uint32_t voltage_mv;
    unsigned percent;
} battery_reading_t;

esp_err_t battery_init(void);
esp_err_t battery_read(battery_reading_t *reading);
