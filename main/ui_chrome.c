#include "ui_chrome.h"
#include <stdio.h>
#include "rlcd.h"

void ui_draw_card(int x, int y, int width, int height)
{
    rlcd_hline(x + 4, y, width - 8);
    rlcd_hline(x + 4, y + height - 1, width - 8);
    for (int row = 1; row < height - 1; ++row) {
        const int inset = row == 1 || row == height - 2 ? 2 : row == 2 || row == height - 3 ? 1 : 0;
        rlcd_hline(x + inset, y + row, 1);
        rlcd_hline(x + width - 1 - inset, y + row, 1);
    }
}

void ui_draw_icon(ui_icon_t icon, int x, int y)
{
    static const unsigned short shapes[][14] = {
        {0x3fff,0x2001,0x2001,0x2201,0x2101,0x2081,0x2101,0x2201,0x201d,0x2001,0x2001,0x2001,0x2001,0x3fff},
        {0x0800,0x0c00,0x0e00,0x0f00,0x0f80,0x0fc0,0x0fe0,0x0fc0,0x0f80,0x0f00,0x0e00,0x0c00,0x0800,0},
        {0x03f0,0x0c0c,0x1002,0x2001,0x2081,0x2081,0x2081,0x20f1,0x2001,0x2001,0x1002,0x0c0c,0x03f0,0},
        {0x3fff,0x2001,0x2001,0x2015,0x2001,0x3fff,0,0x3fff,0x2001,0x2001,0x2015,0x2001,0x2001,0x3fff},
        {0x03f0,0x0c0c,0x1002,0,0x03f0,0x0408,0,0x00c0,0x0120,0,0x0080,0x01c0,0x0080,0},
        {0x0924,0x0924,0x3fff,0x2001,0x2001,0x27f9,0x2409,0x2409,0x27f9,0x2001,0x2001,0x3fff,0x0924,0x0924},
        {0,0,0x1ffc,0x1004,0x1007,0x1005,0x1005,0x1007,0x1004,0x1ffc,0,0,0,0},
        {0,0x1ffe,0x1002,0x1002,0x3fff,0x2001,0x2001,0x203d,0x2025,0x203d,0x2001,0x2001,0x3fff,0}
    };
    if ((unsigned)icon >= sizeof(shapes) / sizeof(shapes[0])) return;
    for (int row = 0; row < 14; ++row)
        for (int col = 0; col < 14; ++col)
            if (shapes[icon][row] & (1U << (13 - col))) rlcd_hline(x + col, y + row, 1);
}

void ui_draw_header(const char *title, const ui_header_t *h, unsigned screen, unsigned count)
{
    char text[24];
    rlcd_text(20, 16, title, 2);
    rlcd_text(158, 16, h->time_valid ? h->time : "--:--", 2);
    ui_draw_icon(UI_ICON_WIFI, 232, 15);
    if (h->connecting) ui_draw_icon(UI_ICON_CLOCK, 251, 15);
    else if (!h->connected) {
        for (int i = 0; i < 12; ++i) {
            rlcd_hline(233 + i, 16 + i, 1);
            rlcd_hline(244 - i, 16 + i, 1);
        }
    } else if (!h->signal_valid) rlcd_text(250, 20, "?", 1);
    else {
        const unsigned bars = h->rssi >= -55 ? 4 : h->rssi >= -67 ? 3 : h->rssi >= -75 ? 2 : 1;
        for (unsigned i = 0; i < 4; ++i) {
            const int height = 3 + (int)i * 3;
            if (i < bars) for (int r = 0; r < height; ++r) rlcd_hline(251 + (int)i * 5, 29 - r, 3);
            else rlcd_hline(251 + (int)i * 5, 29, 3);
        }
    }
    rlcd_rect(285, 15, 23, 14);
    rlcd_rect(308, 19, 3, 6);
    if (h->battery_valid) {
        const int width = (int)(h->battery_percent * 19 / 100);
        for (int r = 18; r < 26; ++r) rlcd_hline(287, r, width);
        snprintf(text, sizeof(text), "%u%%", h->battery_percent);
    } else snprintf(text, sizeof(text), "--%%");
    rlcd_text(321, 16, text, 2);
    rlcd_text(20, 39, "UTC+8", 1);
    snprintf(text, sizeof(text), "%u/%u", screen + 1, count);
    rlcd_text(350, 39, text, 1);
    rlcd_hline(20, 52, 360);
}

void ui_draw_decimal(int x, int y, const char *text)
{
    static const unsigned char digits[10][5] = {
        {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
        {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}
    };
    for (; *text; ++text, x += 4) {
        for (int row = 0; row < 5; ++row) {
            const unsigned bits = *text >= '0' && *text <= '9' ? digits[*text - '0'][row] :
                                  *text == '.' ? (row == 4 ? 2 : 0) : *text == '-' ? (row == 2 ? 7 : 0) : 0;
            for (int col = 0; col < 3; ++col) if (bits & (4U >> col)) rlcd_hline(x + col, y + row, 1);
        }
    }
}
