#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "codeck_state.h"
esp_err_t codeck_client_init(void);
void codeck_client_get_snapshot(codeck_snapshot_t *snapshot);
// Page visibility gate: the worker only fetches while the CODECK page is
// visible. Safe to call from app_main (the UI owner) at any time.
void codeck_client_set_visible(bool visible);
