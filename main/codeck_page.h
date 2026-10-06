#pragma once
#include "codeck_state.h"
unsigned codeck_page_count(const codeck_snapshot_t *snapshot);
void draw_codeck_page(const codeck_snapshot_t *snapshot, unsigned sheet);
