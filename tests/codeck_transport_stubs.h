#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_ERR_NO_MEM 257
#define ESP_LOG_WARN 2
#define ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED 0x801a
#define ESP_ERR_ESP_TLS_SERVER_HANDSHAKE_TIMEOUT 0x8009
#define ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME 0x8001
#define ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT 0x8006
#define HTTP_ADDR_TYPE_INET 2
#define pdPASS 1
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
static inline void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define HTTP_METHOD_GET 0
typedef int esp_err_t;
typedef void *QueueHandle_t;
typedef void *esp_http_client_handle_t;
typedef struct {
    const char *url;
    int method,timeout_ms,addr_type;
    esp_err_t (*crt_bundle_attach)(void *);
    bool skip_cert_common_name_check,disable_auto_redirect;
    const char *user_agent;
    int buffer_size;
} esp_http_client_config_t;
esp_err_t esp_crt_bundle_attach(void *);
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *);
esp_err_t esp_http_client_open(esp_http_client_handle_t,int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t,char *,int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
esp_err_t esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t,int *,int *);
void esp_http_client_cleanup(esp_http_client_handle_t);
int64_t esp_timer_get_time(void);
uint32_t esp_random(void);
void esp_log_level_set(const char *,int);
QueueHandle_t xQueueCreate(unsigned,size_t);
int xQueueOverwrite(QueueHandle_t,const void *);
int xQueuePeek(QueueHandle_t,void *,unsigned);
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,void *);
void vTaskDelete(void *);
void vTaskDelay(unsigned);
