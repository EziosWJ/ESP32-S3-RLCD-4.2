#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CODECK_MAX_ACCOUNTS 8
#define CODECK_MAX_CURRENCIES 4
#define CODECK_BODY_LIMIT 4096

typedef enum {
    CODECK_OK, CODECK_WAIT_NETWORK, CODECK_WAIT_TIME, CODECK_NO_KEY,
    CODECK_AUTH_ERROR, CODECK_SERVICE_ERROR, CODECK_ACCESS_DENIED,
    CODECK_NETWORK_ERROR, CODECK_TLS_ERROR, CODECK_JSON_ERROR,
    CODECK_UNSUPPORTED_VERSION, CODECK_CAPACITY_ERROR,
    CODECK_DNS_ERROR, CODECK_CONNECT_TIMEOUT,
} codeck_status_t;
typedef struct { bool valid; double used_percent; char resets_at[40]; } codeck_window_t;
typedef struct { char currency[8]; char amount[64]; } codeck_amount_t;
typedef struct {
    char provider[32], name[128], observed_at[40];
    bool observed, stale;
    unsigned amount_count;
    codeck_amount_t amounts[CODECK_MAX_CURRENCIES];
} codeck_account_t;
typedef struct {
    bool has_snapshot, service_available, codex_cli_available, scheduler_enabled;
    unsigned running_tasks;
    char generated_at[40], quota_observed_at[40];
    bool quota_available, balances_available;
    codeck_window_t windows[2];
    unsigned account_count;
    codeck_account_t accounts[CODECK_MAX_ACCOUNTS];
    codeck_status_t status;
    int64_t received_ms;
    uint32_t revision;
    unsigned retry_seconds;
    bool fetching;
} codeck_snapshot_t;

// Parse into scratch; caller commits only CODECK_OK. No string truncation.
codeck_status_t codeck_state_parse(const char *body, size_t length, codeck_snapshot_t *out);
bool codeck_amount_format(const char *decimal, char *out, size_t capacity);
void codeck_state_failure(codeck_snapshot_t *last, codeck_status_t error);
unsigned codeck_retry_delay(codeck_status_t status, unsigned failures, uint32_t random);
