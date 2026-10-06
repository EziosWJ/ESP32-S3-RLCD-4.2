#pragma once

#include <stdint.h>
#include "esp_err.h"

#define HA_DEVICE_COUNT 2
#define HA_HOST_ENTITY_COUNT 7
#define HA_UNIT_MAX 16

typedef enum {
    HA_HOST_CPU_USAGE,
    HA_HOST_MEMORY_USAGE,
    HA_HOST_DISK_USAGE,
    HA_HOST_CPU_TEMPERATURE,
    HA_HOST_UPTIME,
    HA_HOST_NETWORK_RECEIVE_RATE,
    HA_HOST_NETWORK_TRANSMIT_RATE,
} ha_host_entity_t;

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
    char unit[HA_UNIT_MAX];
    char text[24];
} ha_value_t;

typedef struct {
    ha_value_t temperature;
    ha_value_t humidity;
} ha_device_t;

typedef struct {
    ha_device_t devices[HA_DEVICE_COUNT];
    ha_value_t host[HA_HOST_ENTITY_COUNT];
} ha_snapshot_t;

esp_err_t home_assistant_init(void);
// Copies the worker snapshot and invalidates old readings; never blocks on HTTP.
void home_assistant_get_snapshot(ha_snapshot_t *snapshot);
