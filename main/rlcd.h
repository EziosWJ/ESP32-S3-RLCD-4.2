#pragma once

#include "esp_err.h"

// Single-task drawing API: black glyphs on a white 400 x 300 landscape canvas.
esp_err_t rlcd_init(void);
void rlcd_clear(void);
void rlcd_text(int x, int y, const char *text, unsigned scale);
void rlcd_hline(int x, int y, int width);
void rlcd_rect(int x, int y, int width, int height);
// Synchronous transfer: the framebuffer can be modified after this returns.
esp_err_t rlcd_flush(void);
