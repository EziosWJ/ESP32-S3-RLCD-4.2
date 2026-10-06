#include "ha_state.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

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
        const char *start = state->valuestring;
        char *end;
        errno = 0;
        const float number = strtof(start, &end);
        valid = end != start && errno != ERANGE && isfinite(number);
        while (isspace((unsigned char)*end)) {
            ++end;
        }
        valid = valid && *end == '\0' &&
                (humidity ? number >= 0 && number <= 100 : number >= -100 && number <= 150);
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
        const char *start = state->valuestring;
        char *end;
        errno = 0;
        number = strtof(start, &end);
        valid = end != start && errno != ERANGE && isfinite(number);
        while (isspace((unsigned char)*end)) {
            ++end;
        }
        valid = valid && *end == '\0';
    }
    if (valid && unit) {
        unit[0] = '\0';
        const cJSON *attributes = cJSON_GetObjectItemCaseSensitive(root, "attributes");
        const cJSON *unit_item = cJSON_IsObject(attributes) ?
                                 cJSON_GetObjectItemCaseSensitive(attributes, "unit_of_measurement") : NULL;
        if (cJSON_IsString(unit_item)) {
            const unsigned char *source = (const unsigned char *)unit_item->valuestring;
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
