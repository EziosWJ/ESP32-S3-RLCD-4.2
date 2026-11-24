#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    BATTERY_POWER_UNKNOWN = 0,
    BATTERY_POWER_DISCHARGING,
    BATTERY_POWER_CHARGING,
    BATTERY_POWER_FULL,
} battery_power_state_t;

typedef struct {
    // The ADC sample itself succeeded.
    bool valid;
    // Pack voltage is inside the plausible battery window; no dedicated
    // insertion signal exists, so this stays a voltage-based inference.
    bool detected;
    // percent is a trustworthy state-of-charge estimate. False while charging
    // (the charger raises the terminal voltage above the open-circuit curve).
    bool percent_valid;
    // Calibrated pack voltage in millivolts.
    uint32_t voltage_mv;
    // 0..100 state of charge; only meaningful when percent_valid is true.
    unsigned percent;
    battery_power_state_t power_state;
} battery_reading_t;

esp_err_t battery_init(void);
// Samples the pack, updates the filtered state of charge and fills *reading.
// Call it every BOARD_SAMPLE_INTERVAL_MS; the filter state lives in this module.
esp_err_t battery_read(battery_reading_t *reading);
