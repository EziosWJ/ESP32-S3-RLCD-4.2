#include "portal_codec.h"

#include <string.h>

bool portal_address_matches_ipv4(const uint8_t *address, size_t length, const uint8_t expected[4])
{
    if (address == NULL || expected == NULL || (length != 4 && length != 16)) {
        return false;
    }
    const size_t offset = length - 4;
    for (size_t i = 0; i < offset; ++i) {
        if (address[i] != (i >= 10 ? 0xff : 0)) {
            return false;
        }
    }
    for (size_t i = 0; i < 4; ++i) {
        if (address[offset + i] != expected[i]) {
            return false;
        }
    }
    return true;
}

bool portal_credentials_valid(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL) {
        return false;
    }
    const size_t ssid_len = strlen(ssid);
    const size_t pass_len = strlen(password);
    if (ssid_len == 0 || ssid_len > 32) {
        return false;
    }
    if (pass_len == 64) {
        return strspn(password, "0123456789abcdefABCDEF") == 64;
    }
    if (pass_len != 0 && (pass_len < 8 || pass_len > 63)) {
        return false;
    }
    for (size_t i = 0; i < pass_len; ++i) {
        if ((unsigned char)password[i] < 32 || (unsigned char)password[i] > 126) {
            return false;
        }
    }
    return true;
}

size_t portal_dns_reply(uint8_t *packet, size_t length, size_t capacity)
{
    if (length < 17 || length > capacity || (packet[2] & 0xf8) != 0 ||
        packet[4] != 0 || packet[5] != 1 || packet[6] || packet[7] ||
        packet[8] || packet[9]) {
        return 0;
    }
    size_t pos = 12;
    size_t name_length = 0;
    while (pos < length && packet[pos] != 0) {
        const size_t label = packet[pos++];
        if (label > 63 || label > length - pos || (name_length += label + 1) > 254) {
            return 0;
        }
        pos += label;
    }
    if (pos >= length || length - pos < 5) {
        return 0;
    }
    ++pos;
    const bool ipv4 = packet[pos] == 0 && packet[pos + 1] == 1 &&
                      packet[pos + 2] == 0 && packet[pos + 3] == 1;
    const size_t question_end = pos + 4;
    if (ipv4 && capacity - question_end < 16) {
        return 0;
    }
    packet[2] = 0x80 | (packet[2] & 1); // QR and original RD; no recursion offered.
    packet[3] = 0;
    packet[6] = 0;
    packet[7] = ipv4 ? 1 : 0;
    packet[10] = packet[11] = 0; // Drop additional/EDNS records.
    if (!ipv4) {
        return question_end;
    }
    const uint8_t answer[] = {
        0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4, 192, 168, 4, 1,
    };
    memcpy(packet + question_end, answer, sizeof(answer));
    return question_end + sizeof(answer);
}
