#pragma once

typedef enum {
    BRAND_ICON_OPENAI,
    BRAND_ICON_DEEPSEEK,
    BRAND_ICON_OPENROUTER,
    BRAND_ICON_COUNT,
} brand_icon_t;
#define BRAND_ICON_CODEX_TERMINAL BRAND_ICON_OPENAI

// Official source artwork rasterized at 24, 32 and 48 pixels; black on white.
// The Codex quota uses the official OpenAI Blossom.
void brand_icon_draw(brand_icon_t icon, int x, int y, unsigned size);
