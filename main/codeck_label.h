#pragma once
#include <stdint.h>
void codeck_label_text(int x, int y, const char *utf8, int width);
void codeck_label_lines(int x, int y, const char *utf8, int width, unsigned lines);
#ifdef CODECK_FONT_HOST
void codeck_font_use(const uint8_t *font);
#endif
