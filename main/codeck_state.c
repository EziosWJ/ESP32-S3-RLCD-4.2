#include "codeck_state.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

static const cJSON *field(const cJSON *o, const char *name) { return cJSON_GetObjectItemCaseSensitive(o, name); }
static bool boolean(const cJSON *o, const char *name, bool *out)
{
    const cJSON *v = field(o, name);
    if (!cJSON_IsBool(v)) return false;
    *out = cJSON_IsTrue(v); return true;
}
static bool string(const cJSON *v, char *out, size_t size)
{
    if (!cJSON_IsString(v) || strlen(v->valuestring) >= size) return false;
    strcpy(out, v->valuestring); return true;
}
static bool timestamp(const cJSON *v, char *out)
{
    if (!string(v, out, 40)) return false;
    // Contract permits fractions and UTC offsets, not only a Z suffix.
    unsigned y, m, d, h, n, s; int end = 0;
    if(strlen(out)<20) return false;
    for(int i=0;i<19;++i) {
        if(i==4 || i==7) { if(out[i]!='-') return false; }
        else if(i==10) { if(out[i]!='T') return false; }
        else if(i==13 || i==16) { if(out[i]!=':') return false; }
        else if(out[i]<'0' || out[i]>'9') return false;
    }
    if (sscanf(out, "%4u-%2u-%2uT%2u:%2u:%2u%n", &y,&m,&d,&h,&n,&s,&end) != 6 || end != 19 ||
        y < 1970 || m < 1 || m > 12 || d < 1 || d > 31 || h > 23 || n > 59 || s > 60) return false;
    const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap=y%4==0 && (y%100!=0 || y%400==0);
    if(d>days[m-1]+(m==2 && leap ? 1U : 0U)) return false;
    const char *p = out + end;
    if (*p == '.') { ++p; if (*p < '0' || *p > '9') return false; while (*p >= '0' && *p <= '9') ++p; }
    // These endpoints promise UTC; accept Z and explicit +00:00 / -00:00.
    return strcmp(p, "Z") == 0 || strcmp(p, "+00:00") == 0 || strcmp(p, "-00:00") == 0;
}

bool codeck_amount_format(const char *decimal, char *out, size_t capacity)
{
    if (!decimal || !out) return false;
    const char *p = decimal; bool negative = *p == '-';
    if (negative) ++p;
    const char *digits = p;
    while (*p >= '0' && *p <= '9') ++p;
    if (p == digits) return false;
    const char *end_integer = p;
    char fraction[2] = {'0','0'}; bool round = false;
    if (*p == '.') {
        ++p; unsigned index = 0;
        if (*p < '0' || *p > '9') return false;
        while (*p >= '0' && *p <= '9') {
            if (index < 2) fraction[index] = *p;
            if (index == 2) round = *p >= '5';
            ++p; ++index;
        }
    }
    if (*p) return false;
    while (digits + 1 < end_integer && *digits == '0') ++digits;
    size_t count = (size_t)(end_integer - digits);
    if (count > 63) return false;
    char result[68];
    result[0] = '0'; memcpy(result + 1, digits, count);
    size_t length = count + 1;
    result[length++] = fraction[0]; result[length++] = fraction[1];
    if (round) {
        size_t i = length;
        while (i && result[i-1] == '9') result[--i] = '0';
        if (i) ++result[i-1];
    }
    size_t start = result[0] == '0' ? 1 : 0;
    bool nonzero = false; for (size_t i = start; i < length; ++i) nonzero |= result[i] != '0';
    size_t need = length - start + 1 + (negative && nonzero ? 1 : 0);
    if (capacity <= need) return false;
    size_t o = 0;
    if (negative && nonzero) out[o++] = '-';
    for (size_t i = start; i < length; ++i) {
        if (i == length - 2) out[o++] = '.';
        out[o++] = result[i];
    }
    out[o] = 0; return true;
}

codeck_status_t codeck_state_parse(const char *body, size_t length, codeck_snapshot_t *out)
{
    if (!body || !out || !length) return CODECK_JSON_ERROR;
    if (length > CODECK_BODY_LIMIT) return CODECK_CAPACITY_ERROR;
    // Caller supplies a terminating byte; reject embedded NUL / trailing JSON.
    if (memchr(body, 0, length)) return CODECK_JSON_ERROR;
    // cJSON stores decoded strings as C strings. An escaped NUL would hide a
    // decimal suffix or account-name suffix, so reject it before decoding.
    for(size_t i=0;i<length;++i) if(body[i]=='\\') {
        if(i+5<length && memcmp(body+i+1,"u0000",5)==0) return CODECK_JSON_ERROR;
        ++i;
    }
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(body, length + 1, &end, true);
    if (!root) return CODECK_JSON_ERROR;
    codeck_status_t result = CODECK_JSON_ERROR;
    memset(out, 0, sizeof(*out));
    const cJSON *version = field(root, "schema_version");
    if (!cJSON_IsObject(root) || !cJSON_IsNumber(version) || !isfinite(version->valuedouble) ||
        floor(version->valuedouble) != version->valuedouble) goto done;
    if (version->valuedouble != 1) { result = CODECK_UNSUPPORTED_VERSION; goto done; }
    if (!timestamp(field(root,"generated_at"), out->generated_at)) goto done;
    const cJSON *service = field(root,"service"), *quota = field(root,"quota"), *balances = field(root,"balances");
    if (!cJSON_IsObject(service) || !boolean(service,"available",&out->service_available) ||
        !boolean(service,"codex_cli_available",&out->codex_cli_available) ||
        !boolean(service,"scheduler_enabled",&out->scheduler_enabled)) goto done;
    const cJSON *tasks = field(service,"running_tasks");
    if (!cJSON_IsNumber(tasks) || !isfinite(tasks->valuedouble) || tasks->valuedouble < 0 ||
        tasks->valuedouble > UINT_MAX || floor(tasks->valuedouble) != tasks->valuedouble) goto done;
    out->running_tasks = (unsigned)tasks->valuedouble;
    if (!cJSON_IsObject(quota) || !boolean(quota,"available",&out->quota_available)) goto done;
    if (out->quota_available) {
        const cJSON *obs = field(quota,"observed_at"), *windows = field(quota,"windows");
        if (!cJSON_IsArray(windows)) goto done;
        if (!cJSON_IsNull(obs) && !timestamp(obs,out->quota_observed_at)) goto done;
        const cJSON *w;
        cJSON_ArrayForEach(w, windows) {
            const cJSON *kind = field(w,"kind"), *used = field(w,"used_percent");
            if (!cJSON_IsObject(w) || !cJSON_IsString(kind)) goto done;
            int index = strcmp(kind->valuestring,"primary") == 0 ? 0 :
                        strcmp(kind->valuestring,"secondary") == 0 ? 1 : -1;
            if (index < 0 || out->windows[index].valid || !cJSON_IsNumber(used) ||
                !isfinite(used->valuedouble) || used->valuedouble < 0 || used->valuedouble > 100 ||
                !timestamp(field(w,"resets_at"),out->windows[index].resets_at)) goto done;
            out->windows[index].used_percent = used->valuedouble;
            out->windows[index].valid = true;
        }
        if (!out->quota_observed_at[0]) memset(out->windows,0,sizeof(out->windows));
    }
    if (!cJSON_IsObject(balances) || !boolean(balances,"available",&out->balances_available)) goto done;
    if (out->balances_available) {
        const cJSON *items = field(balances,"items"), *item;
        if (!cJSON_IsArray(items)) goto done;
        cJSON_ArrayForEach(item, items) {
            if (out->account_count == CODECK_MAX_ACCOUNTS) { result=CODECK_CAPACITY_ERROR; goto done; }
            codeck_account_t *a = &out->accounts[out->account_count++];
            if (!cJSON_IsObject(item) || !string(field(item,"provider"),a->provider,sizeof(a->provider)) ||
                !string(field(item,"name"),a->name,sizeof(a->name)) || !boolean(item,"stale",&a->stale)) goto done;
            const cJSON *obs = field(item,"observed_at"), *amounts = field(item,"amounts"), *amount;
            if (!cJSON_IsNull(obs) && !timestamp(obs,a->observed_at)) goto done;
            a->observed = a->observed_at[0] != 0;
            if (!cJSON_IsArray(amounts)) goto done;
            cJSON_ArrayForEach(amount, amounts) {
                if (a->amount_count == CODECK_MAX_CURRENCIES) { result=CODECK_CAPACITY_ERROR; goto done; }
                codeck_amount_t *v = &a->amounts[a->amount_count++]; char formatted[72];
                if (!cJSON_IsObject(amount) || !string(field(amount,"currency"),v->currency,sizeof(v->currency)) ||
                    !string(field(amount,"amount"),v->amount,sizeof(v->amount)) ||
                    !codeck_amount_format(v->amount,formatted,sizeof(formatted))) goto done;
                if (!v->currency[0]) goto done;
                for (unsigned i=0;v->currency[i];++i)
                    if(!((v->currency[i]>='A' && v->currency[i]<='Z') ||
                         (v->currency[i]>='0' && v->currency[i]<='9'))) goto done;
            }
        }
    }
    out->has_snapshot = true; out->status = CODECK_OK; result = CODECK_OK;
done:
    cJSON_Delete(root); return result;
}

void codeck_state_failure(codeck_snapshot_t *last, codeck_status_t error)
{
    last->status=error; ++last->revision;
}
unsigned codeck_retry_delay(codeck_status_t status, unsigned failures, uint32_t random)
{
    if (status == CODECK_OK) return 45000;
    if (status == CODECK_AUTH_ERROR || status == CODECK_NO_KEY) return 15U*60U*1000U;
    unsigned shift = failures ? failures-1 : 0;
    if (shift > 5) shift = 5;
    unsigned base = 10000U << shift;
    if (base > 300000) base = 300000;
    return base + random % (base/10 + 1);
}
