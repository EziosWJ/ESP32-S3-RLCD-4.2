#pragma once

#include <stdint.h>
#include "esp_err.h"

#define HA_DEVICE_COUNT 2

typedef enum {
    HA_READING,
    HA_LIVE,
    HA_OFFLINE,
    HA_AUTH_ERROR,
    HA_NOT_FOUND,
    HA_READ_ERROR,
    HA_NO_TOKEN,
    HA_STALE,
} ha_value_status_t;

typedef struct {
    float value;
    ha_value_status_t status;
    int64_t updated_ms;
} ha_value_t;

typedef struct {
    ha_value_t temperature;
    ha_value_t humidity;
} ha_device_t;

typedef struct {
    ha_device_t devices[HA_DEVICE_COUNT];
} ha_snapshot_t;

esp_err_t home_assistant_init(void);
// Copies the worker snapshot and invalidates old readings; never blocks on HTTP.
void home_assistant_get_snapshot(ha_snapshot_t *snapshot);
