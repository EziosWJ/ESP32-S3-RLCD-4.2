#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool portal_credentials_valid(const char *ssid, const char *password);
// Address bytes are in network order. Accept IPv4 and IPv4-mapped IPv6 only.
bool portal_address_matches_ipv4(const uint8_t *address, size_t length, const uint8_t expected[4]);
// In-place DNS response: one uncompressed question, A/IN -> 192.168.4.1.
// Unsupported types receive an empty answer. Malformed packets are dropped.
size_t portal_dns_reply(uint8_t *packet, size_t length, size_t capacity);
