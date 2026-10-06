#pragma once

// UI fixtures only. No credential, JSON parsing or network activity.
typedef enum {
    CODECK_DEMO_NORMAL,
    CODECK_DEMO_STALE,
    CODECK_DEMO_NO_QUOTA,
    CODECK_DEMO_OFFLINE,
    CODECK_DEMO_AUTH_ERROR,
    CODECK_DEMO_EMPTY,
    CODECK_DEMO_NO_BALANCES,
    CODECK_DEMO_SERVICE_DOWN,
    CODECK_DEMO_COUNT,
} codeck_demo_state_t;

void draw_codeck_demo(codeck_demo_state_t state, unsigned sheet);
