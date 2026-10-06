#pragma once

#include <stdbool.h>
#include <stddef.h>

// Validate the entity identity, a complete JSON body, and a finite numeric state.
bool ha_state_parse(const char *json, const char *entity_id, bool humidity, float *value);
bool ha_state_parse_number(const char *json, const char *entity_id, float *value,
                           char *unit, size_t unit_size);
bool ha_state_parse_text(const char *json, const char *entity_id,
                         char *value, size_t value_size);
