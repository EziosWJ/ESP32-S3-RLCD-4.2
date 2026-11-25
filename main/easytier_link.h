#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Mirrors et::State; keep this C header free of C++ types.
typedef enum {
    EASYTIER_STATE_UNINITIALIZED = 0,
    EASYTIER_STATE_STOPPED,
    EASYTIER_STATE_WAITING_UNDERLAY,
    EASYTIER_STATE_CONNECTING,
    EASYTIER_STATE_HANDSHAKING,
    EASYTIER_STATE_SYNCING,
    EASYTIER_STATE_READY,
    EASYTIER_STATE_STOPPING,
    EASYTIER_STATE_ERROR,
} easytier_state_t;

typedef struct {
    easytier_state_t state;
    bool configured;      // easytier.toml parsed and the instance exists
    bool started;         // component tasks are running
    uint32_t virtual_ipv4;  // network byte order, 0 when not assigned
    uint8_t prefix_len;
    uint32_t peer_id;
    uint32_t direct_peers;
    uint32_t reachable_routes;
    esp_err_t last_error;
    uint64_t dropped_packets;
} easytier_status_t;

// Parses the embedded easytier.toml, creates the instance and starts its
// tasks. Must be called from a task with at least 8 KiB stack (app_main).
esp_err_t easytier_start(void);

// Allows the component to dial its entry peers. Call only after Wi-Fi has an
// IP; call again after every reconnect. Non-blocking and idempotent.
esp_err_t easytier_connect(void);

// Closes mesh connections and revokes routes. Call when Wi-Fi loses its IP.
esp_err_t easytier_disconnect(void);

// Copies a status snapshot. Safe to call from app_main only.
esp_err_t easytier_status(easytier_status_t *out);

#ifdef __cplusplus
}
#endif
