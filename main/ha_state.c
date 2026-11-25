#include "ha_state.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

static bool parse_state_number(const char *start, float *number)
{
    char *end;
    errno = 0;
    const float parsed = strtof(start, &end);
    bool valid = end != start && errno != ERANGE && isfinite(parsed);
    while (isspace((unsigned char)*end)) {
        ++end;
    }
    valid = valid && *end == '\0';
    if (valid) {
        *number = parsed;
    }
    return valid;
}

static bool sensor_range(float number, bool humidity)
{
    return humidity ? number >= 0 && number <= 100 : number >= -100 && number <= 150;
}

static void sanitize_unit(const unsigned char *source, char *unit, size_t unit_size)
{
    unit[0] = '\0';
    size_t written = 0;
    while (*source && written + 1 < unit_size) {
        // The built-in RLCD font is ASCII-only. Keep readable ASCII units and
        // remove the UTF-8 degree sign so "°C" is displayed as "C".
        if (source[0] == 0xc2 && source[1] == 0xb0) {
            source += 2;
        } else if (source[0] == 0xe2 && source[1] == 0x84 && source[2] == 0x83) {
            unit[written++] = 'C';
            source += 3;
        } else if (*source < 0x80) {
            if (*source >= 32 && *source <= 126) unit[written++] = (char)*source;
            ++source;
        } else {
            ++source;
            while ((*source & 0xc0) == 0x80) ++source;
        }
    }
    unit[written] = '\0';
}

bool ha_state_parse(const char *json, const char *entity_id, bool humidity, float *value)
{
    if (!json || !entity_id || !value) {
        return false;
    }
    cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
    if (!root) {
        return false;
    }
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "entity_id");
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    bool valid = cJSON_IsObject(root) && cJSON_IsString(id) &&
                 strcmp(id->valuestring, entity_id) == 0 && cJSON_IsString(state);
    if (valid) {
        float number = 0;
        valid = parse_state_number(state->valuestring, &number) &&
                sensor_range(number, humidity);
        if (valid) {
            *value = number;
        }
    }
    cJSON_Delete(root);
    return valid;
}

bool ha_state_parse_number(const char *json, const char *entity_id, float *value,
                           char *unit, size_t unit_size)
{
    if (!json || !entity_id || !value || (unit && unit_size == 0)) {
        return false;
    }
    cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
    if (!root) {
        return false;
    }
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "entity_id");
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    bool valid = cJSON_IsObject(root) && cJSON_IsString(id) &&
                 strcmp(id->valuestring, entity_id) == 0 && cJSON_IsString(state);
    float number = 0;
    if (valid) {
        valid = parse_state_number(state->valuestring, &number);
    }
    if (valid && unit) {
        unit[0] = '\0';
        const cJSON *attributes = cJSON_GetObjectItemCaseSensitive(root, "attributes");
        const cJSON *unit_item = cJSON_IsObject(attributes) ?
                                 cJSON_GetObjectItemCaseSensitive(attributes, "unit_of_measurement") : NULL;
        if (cJSON_IsString(unit_item)) {
            sanitize_unit((const unsigned char *)unit_item->valuestring, unit, unit_size);
        }
    }
    if (valid) {
        *value = number;
    }
    cJSON_Delete(root);
    return valid;
}

bool ha_state_parse_text(const char *json, const char *entity_id,
                         char *value, size_t value_size)
{
    if (!json || !entity_id || !value || value_size == 0) {
        return false;
    }
    value[0] = '\0';
    cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
    if (!root) {
        return false;
    }
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "entity_id");
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    bool valid = cJSON_IsObject(root) && cJSON_IsString(id) &&
                 strcmp(id->valuestring, entity_id) == 0 && cJSON_IsString(state);
    if (valid) {
        const size_t length = strlen(state->valuestring);
        valid = length > 0 && length < value_size;
        if (valid) memcpy(value, state->valuestring, length + 1);
    }
    cJSON_Delete(root);
    return valid;
}

// --- Batch /api/template payload -----------------------------------------

static cJSON *template_entry(const char *json, const char *key, cJSON **root_out)
{
    *root_out = cJSON_ParseWithOpts(json, NULL, true);
    if (!*root_out) {
        return NULL;
    }
    if (!cJSON_IsObject(*root_out)) {
        cJSON_Delete(*root_out);
        *root_out = NULL;
        return NULL;
    }
    const cJSON *entry = cJSON_GetObjectItemCaseSensitive(*root_out, key);
    if (!cJSON_IsObject(entry)) {
        cJSON_Delete(*root_out);
        *root_out = NULL;
        return NULL;
    }
    return (cJSON *)entry;
}

static const cJSON *entry_state(const cJSON *entry)
{
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(entry, "s");
    return cJSON_IsString(state) ? state : NULL;
}

bool ha_state_parse_template_envelope(const char *json, char *inner, size_t inner_size)
{
    if (!json || !inner || inner_size == 0) {
        return false;
    }
    inner[0] = '\0';
    cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
    if (!root) {
        return false;
    }
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    bool valid = cJSON_IsObject(root) && cJSON_IsString(result);
    if (valid) {
        const size_t length = strlen(result->valuestring);
        valid = length > 0 && length < inner_size;
        if (valid) memcpy(inner, result->valuestring, length + 1);
    } else if (cJSON_IsObject(root)) {
        // HA's /api/template returns the rendered text directly (Content-Type
        // text/plain), not the legacy {"result":"..."} envelope. Accept the
        // plain JSON object as the inner object when no result key is present.
        const size_t length = strlen(json);
        if (length > 0 && length < inner_size) {
            memcpy(inner, json, length + 1);
            valid = true;
        }
    }
    cJSON_Delete(root);
    return valid;
}

bool ha_state_parse_template_number(const char *json, const char *key, bool humidity,
                                    float *value)
{
    if (!json || !key || !value) {
        return false;
    }
    cJSON *root = NULL;
    const cJSON *entry = template_entry(json, key, &root);
    if (!entry) {
        return false;
    }
    const cJSON *state = entry_state(entry);
    float number = 0;
    const bool valid = state && parse_state_number(state->valuestring, &number) &&
                       sensor_range(number, humidity);
    if (valid) {
        *value = number;
    }
    cJSON_Delete(root);
    return valid;
}

bool ha_state_parse_template_number_unit(const char *json, const char *key, float *value,
                                         char *unit, size_t unit_size)
{
    if (!json || !key || !value || (unit && unit_size == 0)) {
        return false;
    }
    cJSON *root = NULL;
    const cJSON *entry = template_entry(json, key, &root);
    if (!entry) {
        return false;
    }
    const cJSON *state = entry_state(entry);
    float number = 0;
    const bool valid = state && parse_state_number(state->valuestring, &number);
    if (valid) {
        *value = number;
        if (unit) {
            const cJSON *unit_item = cJSON_GetObjectItemCaseSensitive(entry, "u");
            if (cJSON_IsString(unit_item)) {
                sanitize_unit((const unsigned char *)unit_item->valuestring, unit, unit_size);
            } else {
                unit[0] = '\0';
            }
        }
    }
    cJSON_Delete(root);
    return valid;
}

bool ha_state_parse_template_text(const char *json, const char *key,
                                  char *value, size_t value_size)
{
    if (!json || !key || !value || value_size == 0) {
        return false;
    }
    value[0] = '\0';
    cJSON *root = NULL;
    const cJSON *entry = template_entry(json, key, &root);
    if (!entry) {
        return false;
    }
    const cJSON *state = entry_state(entry);
    bool valid = state && strlen(state->valuestring) > 0 &&
                 strlen(state->valuestring) < value_size;
    if (valid) {
        memcpy(value, state->valuestring, strlen(state->valuestring) + 1);
    }
    cJSON_Delete(root);
    return valid;
}
