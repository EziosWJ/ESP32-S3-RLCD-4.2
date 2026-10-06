#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codeck_transport_stubs.h"
// Include the actual client so the harness can invoke its private fetch path.
#include "../main/codeck_client.c"

static const char *response;
static size_t response_length, consumed;
static int mode;
static bool accept_set, auth_set, config_checked;
static codeck_snapshot_t *queued;
static wifi_setup_state_t wifi_state;
esp_err_t esp_crt_bundle_attach(void *p) { return 0; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg)
{
    assert(strcmp(cfg->url,"https://codeck.wangj.de/api/device/v1/snapshot")==0);
    assert(cfg->method==HTTP_METHOD_GET && cfg->disable_auto_redirect);
    assert(cfg->addr_type==HTTP_ADDR_TYPE_INET && cfg->timeout_ms==20000);
    assert(cfg->crt_bundle_attach==esp_crt_bundle_attach && !cfg->skip_cert_common_name_check);
    config_checked=true; return (void *)1;
}
esp_err_t esp_http_client_set_header(void *c,const char *name,const char *value)
{
    if(strcmp(name,"Accept")==0) { assert(strcmp(value,"application/json")==0); accept_set=true; }
    if(strcmp(name,"Authorization")==0) { assert(strcmp(value,"Bearer fixture-only")==0); auth_set=true; }
    return 0;
}
esp_err_t esp_http_client_open(void *c,int length) { return mode==3 || mode==4 || mode>=9 ? -1 : 0; }
int64_t esp_http_client_fetch_headers(void *c) { return mode==6 ? 5000 : 0; } // chunked / no content length
int esp_http_client_get_status_code(void *c) { return mode==1 ? 401 : mode==2 ? 503 : mode==8 ? 403 : 200; }
int esp_http_client_read(void *c,char *out,int limit)
{
    size_t end=mode==5 ? response_length-1 : response_length;
    if(consumed>=end) return 0;
    size_t count=end-consumed; if(count>73) count=73; if(count>(size_t)limit) count=(size_t)limit;
    memcpy(out,response+consumed,count); consumed+=count; return (int)count;
}
bool esp_http_client_is_complete_data_received(void *c) { return mode!=5 && consumed==response_length; }
esp_err_t esp_http_client_get_and_clear_last_tls_error(void *c,int *error,int *flags)
{
    *error=mode==3 || mode==11 ? -0x2700 : 0; *flags=mode==3 ? 4 : 0;
    return mode==3 || mode==11 ? ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED :
           mode==9 ? ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT :
           mode==10 ? ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME :
           mode==12 ? ESP_ERR_ESP_TLS_SERVER_HANDSHAKE_TIMEOUT : 0;
}
void esp_http_client_cleanup(void *c) {}
int64_t esp_timer_get_time(void) { return 0; }
uint32_t esp_random(void) { return 0; }
void esp_log_level_set(const char *s,int l) {}
QueueHandle_t xQueueCreate(unsigned n,size_t size) { return NULL; }
int xQueueOverwrite(void *q,const void *s) { return 0; }
int xQueuePeek(void *q,void *s,unsigned t) { if(!queued) return 0; memcpy(s,queued,sizeof(*queued)); return pdTRUE; }
int xTaskCreate(void (*f)(void *),const char *n,unsigned size,void *a,unsigned p,void *h) { return 0; }
void vTaskDelete(void *p) {}
void vTaskDelay(unsigned n) {}
bool clock_service_is_synchronized(void) { return true; }
void wifi_setup_get_status(wifi_setup_status_t *status) { memset(status,0,sizeof(*status)); status->state=wifi_state; }

int main(int argc,char **argv)
{
    assert(argc==2);
    FILE *file=fopen(argv[1],"rb"); assert(file);
    char fixture[4097]; size_t length=fread(fixture,1,4096,file); fclose(file); fixture[length]=0;
    strcpy(authorization,"Bearer fixture-only");
    codeck_snapshot_t *last=calloc(1,sizeof(*last)),*scratch=calloc(1,sizeof(*scratch));
    char body[4097]; assert(last && scratch);
    response=fixture; response_length=length;
    assert(fetch(body,last)==CODECK_OK && last->has_snapshot);
    const codeck_status_t errors[]={CODECK_OK,CODECK_AUTH_ERROR,CODECK_SERVICE_ERROR,CODECK_TLS_ERROR,
                                  CODECK_NETWORK_ERROR,CODECK_JSON_ERROR,CODECK_CAPACITY_ERROR,
                                  CODECK_CAPACITY_ERROR,CODECK_ACCESS_DENIED,CODECK_CONNECT_TIMEOUT,
                                  CODECK_DNS_ERROR,CODECK_TLS_ERROR,CODECK_TLS_ERROR};
    for(mode=1;mode<=12;++mode) {
        char oversized[4097]; memset(oversized,'x',sizeof(oversized));
        response=mode==7 ? oversized : fixture; response_length=mode==7 ? sizeof(oversized) : length;
        consumed=0;
        codeck_status_t result=fetch(body,scratch); assert(result==errors[mode]);
        codeck_snapshot_t before=*last;
        codeck_state_failure(last,result);
        before.status=last->status; before.revision=last->revision;
        assert(memcmp(&before,last,sizeof(before))==0 && last->has_snapshot);
    }
    assert(accept_set && auth_set && config_checked);
    char exact_budget[4096]; memcpy(exact_budget,fixture,length);
    memset(exact_budget+length,' ',sizeof(exact_budget)-length);
    response=exact_budget; response_length=sizeof(exact_budget); consumed=0; mode=0;
    assert(fetch(body,scratch)==CODECK_OK);
    snapshots=(void *)1; queued=scratch;
    wifi_state=WIFI_SETUP_RETRYING;
    codeck_client_get_snapshot(last);
    assert(last->status==CODECK_WAIT_NETWORK && last->has_snapshot &&
           memcmp(last->accounts,scratch->accounts,sizeof(last->accounts))==0);
    wifi_state=WIFI_SETUP_CONNECTED;
    codeck_client_get_snapshot(last); assert(last->status==CODECK_OK);
    free(last); free(scratch);
    puts("Production HTTP client: success, HTTP errors, TLS, DNS, connection timeout, network, truncation and oversize passed; cache retained");
    return 0;
}
