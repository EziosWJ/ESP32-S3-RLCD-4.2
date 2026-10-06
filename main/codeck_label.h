#pragma once
#include <stdint.h>
void codeck_label_text(int x, int y, const char *utf8, int width);
#ifdef CODECK_FONT_HOST
void codeck_font_use(const uint8_t *font);
#endif
