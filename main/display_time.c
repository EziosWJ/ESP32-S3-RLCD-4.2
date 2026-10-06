#include "display_time.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int number(const char *p, unsigned length)
{
    int value = 0;
    for (unsigned i = 0; i < length; ++i) {
        if (p[i] < '0' || p[i] > '9') return -1;
        value = value * 10 + p[i] - '0';
    }
    return value;
}

static int month_days(int year, int month)
{
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

bool display_time_format(const char *s, char *out, size_t capacity, bool full)
{
    if (!out || !capacity) return false;
    snprintf(out, capacity, "--");
    if (!s || strlen(s) < 20 || s[4] != '-' || s[7] != '-' ||
        (s[10] != 'T' && s[10] != 't') || s[13] != ':' || s[16] != ':') return false;
    int year = number(s, 4), month = number(s + 5, 2), day = number(s + 8, 2);
    const int hour = number(s + 11, 2), minute = number(s + 14, 2), second = number(s + 17, 2);
    if (year < 1 || month < 1 || month > 12 || day < 1 || day > month_days(year, month) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return false;
    const char *zone = s + 19;
    if (*zone == '.') {
        ++zone;
        if (!isdigit((unsigned char)*zone)) return false;
        while (isdigit((unsigned char)*zone)) ++zone;
    }
    int offset = 0;
    if (*zone == 'Z' || *zone == 'z') { if (zone[1]) return false; }
    else if ((*zone == '+' || *zone == '-') && strlen(zone) == 6 && zone[3] == ':') {
        const int zh = number(zone + 1, 2), zm = number(zone + 4, 2);
        if (zh < 0 || zh > 23 || zm < 0 || zm > 59) return false;
        offset = (zh * 60 + zm) * (*zone == '+' ? 1 : -1);
    } else return false;
    int local = hour * 60 + minute - offset + 8 * 60;
    while (local < 0) {
        local += 1440;
        if (--day == 0) {
            if (--month == 0) { month = 12; --year; }
            day = month_days(year, month);
        }
    }
    while (local >= 1440) {
        local -= 1440;
        if (++day > month_days(year, month)) {
            day = 1;
            if (++month == 13) { month = 1; ++year; }
        }
    }
    if (year < 1 || year > 9999) return false;
    const int written = full ? snprintf(out, capacity, "%04d-%02d-%02d %02d:%02d", year, month, day, local / 60, local % 60) :
                               snprintf(out, capacity, "%02d-%02d %02d:%02d", month, day, local / 60, local % 60);
    if (written < 0 || (size_t)written >= capacity) { snprintf(out, capacity, "--"); return false; }
    return true;
}
