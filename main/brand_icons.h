#pragma once

typedef enum {
    BRAND_ICON_CODEX_TERMINAL,
    BRAND_ICON_DEEPSEEK,
    BRAND_ICON_OPENROUTER,
    BRAND_ICON_COUNT,
} brand_icon_t;

// Official source artwork rasterized at 24, 32 and 48 pixels; black on white.
// Codex uses its official terminal UI icon, not a substitute brand logo.
void brand_icon_draw(brand_icon_t icon, int x, int y, unsigned size);
