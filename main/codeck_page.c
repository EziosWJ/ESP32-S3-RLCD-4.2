#include "codeck_page.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "rlcd.h"
#include "brand_icons.h"
#include "codeck_label.h"
#include "ui_chrome.h"
#include "display_time.h"

static const char *status_label(codeck_status_t s)
{
    switch (s) {
    case CODECK_OK: return "ONLINE";
    case CODECK_WAIT_NETWORK: return "OFFLINE";
    case CODECK_WAIT_TIME: return "WAITING FOR SNTP";
    case CODECK_NO_KEY: return "NO DEVICE KEY";
    case CODECK_AUTH_ERROR: return "401 AUTH ERROR";
    case CODECK_SERVICE_ERROR: return "503 SERVICE UNAVAILABLE";
    case CODECK_ACCESS_DENIED: return "403 ACCESS DENIED";
    case CODECK_TLS_ERROR: return "TLS CONNECTION ERROR";
    case CODECK_DNS_ERROR: return "DNS ERROR";
    case CODECK_CONNECT_TIMEOUT: return "CONNECTION TIMEOUT";
    case CODECK_JSON_ERROR: return "INCOMPLETE OR INVALID JSON";
    case CODECK_UNSUPPORTED_VERSION: return "UNSUPPORTED API VERSION";
    case CODECK_CAPACITY_ERROR: return "SNAPSHOT CAPACITY ERROR";
    default: return "NETWORK ERROR";
    }
}

unsigned codeck_page_count(const codeck_snapshot_t *s)
{
    return 1 + (s->has_snapshot && s->balances_available && s->account_count ? (s->account_count + 1) / 2 : 1);
}

static void quota(int x, const char *name, const codeck_window_t *w)
{
    char text[48], reset[24];
    ui_draw_card(x, 192, 174, 102);
    rlcd_text(x + 10, 202, name, 2);
    if (w->valid) {
        snprintf(text, sizeof(text), "%.0f%%", 100 - w->used_percent);
        rlcd_text(x + 10, 224, text, 3);
        rlcd_text(x + 94, 234, "LEFT", 1);
    } else rlcd_text(x + 10, 224, "--", 3);
    rlcd_rect(x + 10, 252, 154, 7);
    if (w->valid) {
        const int width = (int)((100 - w->used_percent) * 150 / 100);
        for (int y = 254; y < 257; ++y) rlcd_hline(x + 12, y, width);
    }
    display_time_format(w->valid ? w->resets_at : "", reset, sizeof(reset), true);
    rlcd_text(x + 10, 270, "RESET", 1);
    rlcd_text(x + 10, 282, reset, 1);
}

static void status_item(int x, int y, ui_icon_t icon, const char *text)
{
    ui_draw_icon(icon, x, y);
    rlcd_text(x + 22, y + 3, text, 1);
}

static void overview(const codeck_snapshot_t *s)
{
    char text[48], observed[24];
    brand_icon_draw(BRAND_ICON_OPENAI, 20, 96, 32);
    rlcd_text(60, 103, "CODEX CLI", 2);
    display_time_format(s->quota_observed_at, observed, sizeof(observed), false);
    snprintf(text, sizeof(text), "OBS %s", observed);
    rlcd_text(218, 108, text, 1);
    status_item(30, 137, UI_ICON_CLI, !s->has_snapshot ? "CLI --" : s->codex_cli_available ? "CLI OK" : "CLI DOWN");
    if (s->has_snapshot) snprintf(text, sizeof(text), "%s %u", s->status == CODECK_OK ? "RUN" : "LAST RUN", s->running_tasks);
    else strcpy(text, "RUN --");
    status_item(218, 137, UI_ICON_RUN, text);
    status_item(30, 164, UI_ICON_CLOCK, !s->has_snapshot ? "SCHED --" : s->scheduler_enabled ? "SCHED ON" : "SCHED OFF");
    status_item(218, 164, UI_ICON_SERVICE, !s->has_snapshot ? "SERVICE --" : s->service_available ? "SERVICE OK" : "SERVICE DOWN");
    const codeck_window_t empty = {0};
    quota(20, "5H", s->has_snapshot && s->quota_available ? &s->windows[0] : &empty);
    quota(206, "7DAY", s->has_snapshot && s->quota_available ? &s->windows[1] : &empty);
}

static void amount_row(int y, const codeck_amount_t *amount, unsigned count)
{
    char decimal[72], text[88];
    if (!codeck_amount_format(amount->amount, decimal, sizeof(decimal))) strcpy(decimal, "--");
    snprintf(text, sizeof(text), "%s %s", amount->currency, decimal);
    const size_t length = strlen(text);
    const unsigned scale = count == 1 && length <= 19 ? 3 : count <= 2 && length <= 28 ? 2 : 1;
    if (length <= 56) rlcd_text(30, y, text, scale);
    else {
        rlcd_text(30, y, amount->currency, 1);
        ui_draw_decimal(84, y + 1, decimal);
    }
}

static void account_card(const codeck_snapshot_t *s, unsigned index, int y)
{
    const codeck_account_t *a = &s->accounts[index];
    ui_draw_card(20, y, 360, 98);
    const brand_icon_t icon = strcmp(a->provider, "deepseek") == 0 ? BRAND_ICON_DEEPSEEK :
                              strcmp(a->provider, "openrouter") == 0 ? BRAND_ICON_OPENROUTER : BRAND_ICON_COUNT;
    if (icon != BRAND_ICON_COUNT) brand_icon_draw(icon, 30, y + 10, 24);
    else ui_draw_icon(UI_ICON_WALLET, 34, y + 13);
    codeck_label_lines(64, y + 9, a->name, 304, 2);
    const bool known = a->observed && a->amount_count;
    if (known) {
        const int step = a->amount_count > 2 ? 9 : 18;
        for (unsigned i = 0; i < a->amount_count; ++i)
            amount_row(y + 44 + (int)i * step, &a->amounts[i], a->amount_count);
    } else rlcd_text(30, y + 47, "-- / NO OBSERVATION", 2);
    char observed[24], provider[21], text[80];
    for (unsigned i = 0; i < sizeof(provider) - 1; ++i) {
        provider[i] = (char)toupper((unsigned char)a->provider[i]);
        if (!provider[i]) break;
    }
    provider[sizeof(provider) - 1] = 0;
    display_time_format(a->observed_at, observed, sizeof(observed), false);
    snprintf(text, sizeof(text), "%s %s %s%s", provider, observed,
             !known ? "NO DATA" : a->stale ? "OLD" : "LIVE",
             s->status != CODECK_OK ? " CACHED" : "");
    rlcd_text(30, y + 85, text, 1);
}

static void balances(const codeck_snapshot_t *s, unsigned sheet)
{
    if (!s->has_snapshot || !s->balances_available || !s->account_count) {
        ui_draw_card(20, 94, 360, 200);
        ui_draw_icon(UI_ICON_WALLET, 34, 110);
        rlcd_text(58, 110, "BALANCES", 2);
        rlcd_text(34, 148, "--", 4);
        rlcd_text(34, 208, !s->has_snapshot ? "NO SUCCESSFUL SNAPSHOT" :
                  !s->balances_available ? "BALANCES UNAVAILABLE" : "NO BALANCE CONFIGS", 2);
        return;
    }
    const unsigned first = (sheet - 1) * 2;
    account_card(s, first, 94);
    if (first + 1 < s->account_count) account_card(s, first + 1, 196);
}

// The UI owns the common HEADER; this renderer draws only the content area.
void draw_codeck_page(const codeck_snapshot_t *s, unsigned sheet)
{
    char text[80], time[24];
    sheet %= codeck_page_count(s);
    rlcd_text(20, 62, status_label(s->status), 1);
    rlcd_text(278, 62, !s->has_snapshot ? "NO SNAPSHOT" : s->status == CODECK_OK ? "LIVE DATA" : "LAST KNOWN", 1);
    display_time_format(s->generated_at, time, sizeof(time), true);
    snprintf(text, sizeof(text), "%s %s", s->has_snapshot && s->status != CODECK_OK ? "CACHED" : "SNAPSHOT", time);
    rlcd_text(20, 78, text, 1);
    if (s->fetching) strcpy(text, "CONNECTING");
    else if (s->retry_seconds) snprintf(text, sizeof(text), "RETRY %uS", s->retry_seconds);
    else text[0] = 0;
    if (text[0]) rlcd_text(284, 78, text, 1);
    if (!sheet) overview(s); else balances(s, sheet);
}
