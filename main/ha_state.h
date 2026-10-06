#pragma once

#include <stdbool.h>

// Validate the entity identity, a complete JSON body, and a finite numeric state.
bool ha_state_parse(const char *json, const char *entity_id, bool humidity, float *value);
