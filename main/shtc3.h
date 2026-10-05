#pragma once

#include "esp_err.h"

// Initialize the I2C controller. Physical sensor errors are returned by read().
esp_err_t shtc3_init(void);
// Outputs are updated only after both measurements pass their CRC checks.
esp_err_t shtc3_read(float *temperature_c, float *humidity_percent);
