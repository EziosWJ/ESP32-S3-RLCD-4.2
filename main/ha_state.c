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
