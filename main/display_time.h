#pragma once
#include <stdbool.h>
#include <stddef.h>

// RFC3339 timestamp to UTC+8, independent of host/process timezone.
bool display_time_format(const char *timestamp, char *out, size_t capacity, bool full_date);
