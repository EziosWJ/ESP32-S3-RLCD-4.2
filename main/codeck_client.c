#include "codeck_client.h"
#include <stdlib.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_tls_errors.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "clock_service.h"
#include "wifi_setup.h"
#include "board_config.h"

static QueueHandle_t snapshots;
static char authorization[520];
static const char *TAG = "codeck";

static bool load_key(void)
{
#ifdef CODECK_KEY_EMBEDDED
    extern const unsigned char start[] asm("_binary_codeck_key_start");
    extern const unsigned char end[] asm("_binary_codeck_key_end");
    const unsigned char *p = start, *q = end;
    if (q-p >= 3 && p[0]==0xef && p[1]==0xbb && p[2]==0xbf) p+=3;
    while (q>p && (q[-1]==0 || q[-1]=='\n' || q[-1]=='\r' || q[-1]==' ' || q[-1]=='\t')) --q;
    while (p<q && (*p==' ' || *p=='\t' || *p=='\n' || *p=='\r')) ++p;
    if (q==p || q-p > 512) return false;
    for (const unsigned char *s=p;s<q;++s) {
        if (!((*s>='a'&&*s<='z') || (*s>='A'&&*s<='Z') || (*s>='0'&&*s<='9') ||
              *s=='-' || *s=='_' || *s=='.' || *s=='~' || *s=='+' || *s=='/' || *s=='=')) return false;
    }
    memcpy(authorization,"Bearer ",7); memcpy(authorization+7,p,q-p);
    authorization[7+(q-p)] = 0; return true;
#else
    return false;
#endif
}

static codeck_status_t fetch(char *body, codeck_snapshot_t *scratch, bool *recoverable)
{
    *recoverable = false;
    const esp_http_client_config_t cfg = {
        .url = BOARD_CODECK_SNAPSHOT_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = BOARD_CODECK_TIMEOUT_MS,
        // Resolve the hostname normally, but use the device's IPv4 route.
        // Keep the hostname for SNI and certificate verification.
        .addr_type = HTTP_ADDR_TYPE_INET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = false,
        .disable_auto_redirect = true,
        .user_agent = "RLCD-Codeck/1",
        .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return CODECK_NETWORK_ERROR;
    codeck_status_t result = CODECK_NETWORK_ERROR;
    const int64_t started = esp_timer_get_time();
    const char *stage = "connect";
    esp_err_t err = esp_http_client_set_header(client,"Accept","application/json");
    if (err==ESP_OK) err=esp_http_client_set_header(client,"Authorization",authorization);
    if (err==ESP_OK) err=esp_http_client_open(client,0);
    if (err!=ESP_OK) goto transport_error;
    stage = "headers";
    const int64_t content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) goto transport_error;
    const int http = esp_http_client_get_status_code(client);
    if (http!=200) ESP_LOGW(TAG,"Snapshot HTTP response: %d",http);
    if (http==401) { result=CODECK_AUTH_ERROR; goto done; }
    if (http==403) { result=CODECK_ACCESS_DENIED; goto done; }
    if (http==503) { result=CODECK_SERVICE_ERROR; goto done; }
    if (http!=200) goto done;
    if (content_length > CODECK_BODY_LIMIT) { result=CODECK_CAPACITY_ERROR; goto done; }
    size_t length = 0;
    stage = "body";
    const int64_t deadline = esp_timer_get_time() + 20LL*1000*1000;
    while (!esp_http_client_is_complete_data_received(client)) {
        if (esp_timer_get_time() > deadline) {
            result=CODECK_CONNECT_TIMEOUT; *recoverable=true; goto done;
        }
        char extra;
        const int read = esp_http_client_read(client, length < CODECK_BODY_LIMIT ? body+length : &extra,
                                              length < CODECK_BODY_LIMIT ? CODECK_BODY_LIMIT-length : 1);
        if (read<0) goto transport_error;
        if (read==0) {
            if (!esp_http_client_is_complete_data_received(client)) { result=CODECK_JSON_ERROR; goto done; }
            break;
        }
        if (length==CODECK_BODY_LIMIT) { result=CODECK_CAPACITY_ERROR; goto done; }
        length += (size_t)read;
    }
    body[length]=0;
    result=codeck_state_parse(body,length,scratch);
    goto done;
transport_error: {
    int tls_error = 0, flags = 0;
    const esp_err_t tls_result = esp_http_client_get_and_clear_last_tls_error(client,&tls_error,&flags);
    if (tls_result==ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME) result=CODECK_DNS_ERROR;
    else if (tls_result==ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT) result=CODECK_CONNECT_TIMEOUT;
    else if (flags || tls_error < 0 || tls_result==ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED ||
             tls_result==ESP_ERR_ESP_TLS_SERVER_HANDSHAKE_TIMEOUT) result=CODECK_TLS_ERROR;
    // Certificate errors require a valid trust chain/time, rather than a
    // station reset. SDKs may report the mbedTLS detail with either sign.
    const bool certificate_error = flags || tls_error==0x3000 || tls_error==-0x3000 ||
                                   tls_error==0x2700 || tls_error==-0x2700;
    *recoverable = !certificate_error;
    // Only fixed stage names and numeric SDK diagnostics; never headers/body/key.
    ESP_LOGW(TAG,"Snapshot transport: stage=%s elapsed_ms=%lld sdk=0x%x tls=0x%x detail=%d flags=0x%x",
             stage,(long long)((esp_timer_get_time()-started)/1000),
             (unsigned)err,(unsigned)tls_result,tls_error,(unsigned)flags);
}
done:
    esp_http_client_cleanup(client);
    return result;
}

typedef struct {
    int64_t next_poll_ms, reconnect_allowed_ms;
    unsigned failures, transport_failures;
    bool was_ready;
    codeck_status_t last_result;
} recovery_t;

static bool poll_ready(recovery_t *r, codeck_status_t gate, int64_t now)
{
    if (gate!=CODECK_OK) { r->was_ready=false; return false; }
    if (!r->was_ready) {
        // Keep the authentication cooldown even when Wi-Fi is toggled.
        if (r->last_result!=CODECK_AUTH_ERROR) {
            r->next_poll_ms=now; r->failures=0; r->transport_failures=0;
            ESP_LOGI(TAG,"Network/time ready; requesting snapshot");
        }
        r->was_ready=true;
    }
    return now>=r->next_poll_ms;
}

static void schedule_result(recovery_t *r, codeck_status_t result, bool recoverable,
                            int64_t now, uint32_t random)
{
    r->last_result=result;
    if (result==CODECK_OK) { r->failures=0; r->transport_failures=0; }
    else {
        if (r->failures<32) ++r->failures;
        if (recoverable) { if (r->transport_failures<32) ++r->transport_failures; }
        else r->transport_failures=0;
    }
    r->next_poll_ms=now + codeck_retry_delay(result,r->failures,random);
    if (r->transport_failures>=BOARD_CODECK_RECONNECT_FAILURES && now>=r->reconnect_allowed_ms) {
        wifi_setup_status_t wifi; wifi_setup_get_status(&wifi);
        if (wifi.state==WIFI_SETUP_CONNECTED && !wifi.portal_active &&
            wifi_setup_request_reconnect()==ESP_OK) {
            r->transport_failures=0;
            r->reconnect_allowed_ms=now+BOARD_CODECK_RECONNECT_COOLDOWN_MS;
            ESP_LOGW(TAG,"Queued Wi-Fi recovery; cooldown_ms=%u",BOARD_CODECK_RECONNECT_COOLDOWN_MS);
        }
    }
}

static void worker(void *arg)
{
    codeck_snapshot_t *last = calloc(1,sizeof(*last)), *scratch = calloc(1,sizeof(*scratch));
    char *body = malloc(CODECK_BODY_LIMIT+1);
    if (!last || !scratch || !body) {
        if(last) { codeck_state_failure(last,CODECK_CAPACITY_ERROR); xQueueOverwrite(snapshots,last); }
        free(last); free(scratch); free(body);
        ESP_LOGE(TAG,"No memory for snapshot reader"); vTaskDelete(NULL); return;
    }
    recovery_t recovery={0};
    while (true) {
        wifi_setup_status_t wifi; wifi_setup_get_status(&wifi);
        codeck_status_t gate = wifi.state!=WIFI_SETUP_CONNECTED ? CODECK_WAIT_NETWORK :
                               !clock_service_is_synchronized() ? CODECK_WAIT_TIME : CODECK_OK;
        const bool due=poll_ready(&recovery,gate,esp_timer_get_time()/1000);
        if (gate != CODECK_OK) {
            last->retry_seconds=0;
            if (last->status != gate) { codeck_state_failure(last,gate); xQueueOverwrite(snapshots,last); }
        } else if (due) {
            last->fetching=true; last->retry_seconds=0; xQueueOverwrite(snapshots,last);
            bool recoverable;
            const codeck_status_t result=fetch(body,scratch,&recoverable);
            const int64_t now=esp_timer_get_time()/1000;
            const bool recovering=recovery.last_result!=CODECK_OK;
            schedule_result(&recovery,result,recoverable,now,esp_random());
            if (result==CODECK_OK) {
                scratch->received_ms=now;
                scratch->revision=last->revision+1;
                *last=*scratch;
                if (recovering) ESP_LOGI(TAG,"Snapshot recovered");
            } else {
                codeck_state_failure(last,result);
                // Fixed status only: no response, key, headers or raw error text.
                last->retry_seconds=(unsigned)((recovery.next_poll_ms-now+999)/1000);
                ESP_LOGW(TAG,"Snapshot unavailable (status %u); failures=%u retry_in_s=%u",
                         (unsigned)result,recovery.failures,last->retry_seconds);
            }
            last->fetching=false;
            xQueueOverwrite(snapshots,last);
        } else if (last->status!=CODECK_OK) {
            const bool restore_auth = recovery.last_result==CODECK_AUTH_ERROR &&
                                      last->status!=CODECK_AUTH_ERROR;
            if (restore_auth) codeck_state_failure(last,CODECK_AUTH_ERROR);
            const int64_t remaining=recovery.next_poll_ms-esp_timer_get_time()/1000;
            const unsigned seconds=remaining>0 ? (unsigned)((remaining+999)/1000) : 0;
            if (restore_auth || seconds!=last->retry_seconds) {
                last->retry_seconds=seconds; xQueueOverwrite(snapshots,last);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t codeck_client_init(void)
{
    // The SDK's debug HTTP headers include Authorization; suppress that tag.
    esp_log_level_set("HTTP_CLIENT",ESP_LOG_WARN);
    snapshots=xQueueCreate(1,sizeof(codeck_snapshot_t));
    if(!snapshots) return ESP_ERR_NO_MEM;
    codeck_snapshot_t *initial=calloc(1,sizeof(*initial));
    if(!initial) return ESP_ERR_NO_MEM;
    const bool key_ok=load_key();
    initial->status=key_ok ? CODECK_NETWORK_ERROR : CODECK_NO_KEY;
    xQueueOverwrite(snapshots,initial); free(initial);
    if(!key_ok) { ESP_LOGW(TAG,"Missing or invalid codeck.key"); return ESP_OK; }
    return xTaskCreate(worker,"codeck_reader",8192,NULL,3,NULL)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
void codeck_client_get_snapshot(codeck_snapshot_t *snapshot)
{
    if(!snapshots || xQueuePeek(snapshots,snapshot,0)!=pdTRUE) {
        memset(snapshot,0,sizeof(*snapshot)); snapshot->status=CODECK_NETWORK_ERROR;
    }
    if(snapshot->status==CODECK_OK) {
        wifi_setup_status_t wifi; wifi_setup_get_status(&wifi);
        // Show a lost connection immediately, even while the worker's HTTP
        // operation is still waiting for its socket timeout.
        if(wifi.state!=WIFI_SETUP_CONNECTED) snapshot->status=CODECK_WAIT_NETWORK;
    }
}
