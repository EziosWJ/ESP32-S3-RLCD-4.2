#pragma once
#include "esp_err.h"
#include "codeck_state.h"
esp_err_t codeck_client_init(void);
void codeck_client_get_snapshot(codeck_snapshot_t *snapshot);
