#include "codeck_demo.h"

#include <stdio.h>
#include <stdbool.h>
#include "rlcd.h"
#include "brand_icons.h"

static void card(int x, int y, int width, int height)
{
    rlcd_hline(x + 4, y, width - 8);
    rlcd_hline(x + 4, y + height - 1, width - 8);
    for (int row = 1; row < height - 1; ++row) {
        const int inset = row == 1 || row == height - 2 ? 2 :
                          row == 2 || row == height - 3 ? 1 : 0;
        rlcd_hline(x + inset, y + row, 1);
        rlcd_hline(x + width - 1 - inset, y + row, 1);
    }
}

static const char *state_label(codeck_demo_state_t state)
{
    switch (state) {
    case CODECK_DEMO_STALE: return "OLD BALANCE";
    case CODECK_DEMO_NO_QUOTA: return "QUOTA UNAVAILABLE";
    case CODECK_DEMO_OFFLINE: return "503 OFFLINE - CACHED";
    case CODECK_DEMO_AUTH_ERROR: return "401 AUTH ERROR";
    case CODECK_DEMO_EMPTY: return "NO DATA";
    case CODECK_DEMO_NO_BALANCES: return "BALANCES UNAVAILABLE";
    case CODECK_DEMO_SERVICE_DOWN: return "SERVICE UNAVAILABLE";
    default: return "ONLINE";
    }
}

static void quota_card(int x, const char *name, unsigned used, const char *reset,
                       codeck_demo_state_t state)
{
    char text[32];
    card(x, 144, 174, 111);
    rlcd_text(x + 10, 154, name, 2);
    if (state == CODECK_DEMO_NO_QUOTA || state == CODECK_DEMO_EMPTY) {
        rlcd_text(x + 10, 180, "--", 4);
        rlcd_text(x + 10, 226, state == CODECK_DEMO_EMPTY ? "NO WINDOWS" : "UNAVAILABLE", 1);
        return;
    }
    snprintf(text, sizeof(text), "%u%% LEFT", 100U - used);
    rlcd_text(x + 10, 179, text, 3);
    snprintf(text, sizeof(text), "%u%% USED", used);
    rlcd_text(x + 10, 207, text, 1);
    rlcd_rect(x + 10, 222, 154, 7);
    const int remaining_width = (int)((100U - used) * 150U / 100U);
    for (int row = 0; row < 3; ++row) rlcd_hline(x + 12, 224 + row, remaining_width);
    rlcd_text(x + 10, 241, reset, 1);
}

static void overview(codeck_demo_state_t state)
{
    const bool cached = state == CODECK_DEMO_OFFLINE || state == CODECK_DEMO_AUTH_ERROR;
    const bool down = state == CODECK_DEMO_SERVICE_DOWN;
    rlcd_text(20, 72, down ? "SERVICE DOWN" : cached ? "SERVICE LAST OK" : "SERVICE OK", 2);
    rlcd_text(224, 72, cached ? "CLI LAST OK" : "CLI OK", 2);
    rlcd_text(20, 94, down ? "SCHEDULER OFF" : cached ? "SCHED LAST ON" : "SCHEDULER ON", 2);
    rlcd_text(224, 94, cached ? "LAST TASKS 1" : "RUNNING 1", 2);
    brand_icon_draw(BRAND_ICON_CODEX_TERMINAL, 20, 114, 24);
    rlcd_text(52, 120, "CODEX QUOTA", 2);
    rlcd_text(218, 123, state == CODECK_DEMO_NO_QUOTA || state == CODECK_DEMO_EMPTY ?
              "OBS --" : "OBS 08:29:54 UTC", 1);
    quota_card(20, "PRIMARY", 42, "RESET 2026-10-06 10:00 UTC", state);
    quota_card(206, "SECONDARY", 18, "RESET 2026-10-10 00:00 UTC", state);
}

static void balances(codeck_demo_state_t state)
{
    const bool cached = state == CODECK_DEMO_OFFLINE || state == CODECK_DEMO_AUTH_ERROR;
    if (state == CODECK_DEMO_EMPTY || state == CODECK_DEMO_NO_BALANCES) {
        rlcd_rect(20, 75, 360, 155);
        rlcd_text(34, 99, "BALANCES", 2);
        rlcd_text(34, 133, "--", 4);
        rlcd_text(34, 179, state == CODECK_DEMO_EMPTY ? "NO ACCOUNTS OR AMOUNTS" : "BALANCE DATA UNAVAILABLE", 2);
        rlcd_text(20, 247, "UNKNOWN AMOUNTS STAY UNKNOWN", 1);
        return;
    }
    // Display fixtures for amount strings 9.97458636, 20.00, 12.50000000.
    // PRIMARY ACCOUNT is an ASCII alias for the sample label 主账户.
    card(20, 72, 360, 100);
    brand_icon_draw(BRAND_ICON_DEEPSEEK, 30, 82, 24);
    rlcd_text(64, 82, "DEEPSEEK", 2);
    rlcd_text(272, 85, cached ? "CACHED" : "FRESH", 1);
    rlcd_text(64, 103, "PRIMARY ACCOUNT", 1);
    rlcd_hline(30, 116, 340);
    rlcd_text(30, 126, "USD 9.97", 3);
    rlcd_text(218, 130, "CNY 20.00", 2);
    rlcd_text(30, 155, "UPDATED 10-06 08:25:00 UTC", 1);
    card(20, 179, 360, 77);
    brand_icon_draw(BRAND_ICON_OPENROUTER, 30, 183, 24);
    rlcd_text(64, 188, "OPENROUTER", 2);
    rlcd_rect(268, 185, 100, 17);
    rlcd_text(276, 190, cached ? "CACHED" : state == CODECK_DEMO_STALE ? "OLD DATA" : "FRESH", 1);
    rlcd_text(30, 209, "USD 12.50", 3);
    // Provider and config label are both OpenRouter in this fixture.
    rlcd_text(30, 238, "UPDATED 10-06 08:20:00 UTC", 1);
}

void draw_codeck_demo(codeck_demo_state_t state, unsigned sheet)
{
    rlcd_text(20, 10, "CODECK", 3);
    rlcd_rect(284, 10, 96, 19);
    rlcd_text(292, 16, "DEMO DATA", 1);
    rlcd_text(20, 39, state_label(state), 2);
    rlcd_hline(20, 61, 360);
    if (sheet == 0) overview(state);
    else balances(state);
    rlcd_hline(20, 265, 360);
    rlcd_text(20, 273, "SNAPSHOT 2026-10-06 08:30 UTC", 1);
    rlcd_text(20, 288, sheet == 0 ? "2 OF 4  CODECK 1 OF 2" : "2 OF 4  CODECK 2 OF 2", 1);
    rlcd_text(224, 288, "12S AUTO  KEY NEXT", 1);
}
