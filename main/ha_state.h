#pragma once

#include <stdbool.h>
#include <stddef.h>

// Validate the entity identity, a complete JSON body, and a finite numeric state.
bool ha_state_parse(const char *json, const char *entity_id, bool humidity, float *value);
bool ha_state_parse_number(const char *json, const char *entity_id, float *value,
                           char *unit, size_t unit_size);
bool ha_state_parse_text(const char *json, const char *entity_id,
                         char *value, size_t value_size);

// Batch /api/template payload. The endpoint renders one JSON object whose
// entries are {"s": "<state>", "u": "<unit>"}; the HTTP envelope
// {"result": "<rendered json>"} is unwrapped first. These helpers read one
// key out of that inner object; a missing key, unknown/unavailable state or
// malformed entry makes the call fail so the caller marks the entity OFFLINE.

// Unwrap the template envelope: copy the decoded "result" string into inner.
bool ha_state_parse_template_envelope(const char *json, char *inner, size_t inner_size);
// Device sensor: numeric state with the existing temperature/humidity range checks.
bool ha_state_parse_template_number(const char *json, const char *key, bool humidity,
                                    float *value);
// Host metric: numeric state plus optional unit (same ASCII sanitization as /api/states).
bool ha_state_parse_template_number_unit(const char *json, const char *key, float *value,
                                         char *unit, size_t unit_size);
// Host uptime: raw state text only; timestamp formatting stays in the caller.
bool ha_state_parse_template_text(const char *json, const char *key,
                                  char *value, size_t value_size);
