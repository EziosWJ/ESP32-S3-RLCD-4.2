#include "brand_icons.h"

#include <stdint.h>
#include "brand_icons_data.h"
#include "rlcd.h"

void brand_icon_draw(brand_icon_t icon, int x, int y, unsigned size)
{
    if ((unsigned)icon >= BRAND_ICON_COUNT) return;
    const uint8_t *bits;
    switch (size) {
    case 24: bits = brand_bitmaps_24[icon]; break;
    case 32: bits = brand_bitmaps_32[icon]; break;
    case 48: bits = brand_bitmaps_48[icon]; break;
    default: return;
    }
    const unsigned stride = size / 8;
    for (unsigned row = 0; row < size; ++row) {
        int start = -1;
        for (unsigned col = 0; col <= size; ++col) {
            const int ink = col < size && (bits[row * stride + col / 8] & (0x80U >> (col % 8)));
            if (ink && start < 0) start = (int)col;
            if (!ink && start >= 0) {
                rlcd_hline(x + start, y + (int)row, (int)col - start);
                start = -1;
            }
        }
    }
}
