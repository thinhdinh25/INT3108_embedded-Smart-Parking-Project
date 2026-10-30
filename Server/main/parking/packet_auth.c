#include <stdio.h>
#include <string.h>

#include "packet_auth.h"

#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))
#define SIPROUND                                                         \
    do {                                                                 \
        v0 += v1; v1 = ROTL(v1, 13); v1 ^= v0; v0 = ROTL(v0, 32);        \
        v2 += v3; v3 = ROTL(v3, 16); v3 ^= v2;                           \
        v0 += v3; v3 = ROTL(v3, 21); v3 ^= v0;                           \
        v2 += v1; v1 = ROTL(v1, 17); v1 ^= v2; v2 = ROTL(v2, 32);        \
    } while (0)

static uint64_t load_le64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = v << 8 | p[i];
    return v;
}

/* Reference SipHash-2-4 (Aumasson & Bernstein): a keyed PRF built for short messages. */
uint64_t siphash24(const uint8_t key[16], const void *data, size_t len)
{
    const uint8_t *m = data;
    const uint64_t k0 = load_le64(key), k1 = load_le64(key + 8);
    uint64_t v0 = k0 ^ 0x736f6d6570736575ULL, v1 = k1 ^ 0x646f72616e646f6dULL;
    uint64_t v2 = k0 ^ 0x6c7967656e657261ULL, v3 = k1 ^ 0x7465646279746573ULL;
    uint64_t b = (uint64_t)len << 56;

    for (; len >= 8; len -= 8, m += 8)
    {
        uint64_t mi = load_le64(m);
        v3 ^= mi;
        SIPROUND;
        SIPROUND;
        v0 ^= mi;
    }
    for (size_t i = 0; i < len; ++i)
        b |= (uint64_t)m[i] << (8 * i);
    v3 ^= b;
    SIPROUND;
    SIPROUND;
    v0 ^= b;
    v2 ^= 0xff;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1; /* tags are always lower case */
}

static bool parse_hex(const char *s, size_t digits, uint64_t *out)
{
    uint64_t v = 0;
    for (size_t i = 0; i < digits; ++i)
    {
        int d = hex_value(s[i]);
        if (d < 0)
            return false;
        v = v << 4 | (uint64_t)d;
    }
    *out = v;
    return true;
}

bool packet_auth_parse_key(const char *hex32, uint8_t key[16])
{
    if (!hex32 || strlen(hex32) != 32)
        return false;
    for (int i = 0; i < 16; ++i)
    {
        uint64_t byte;
        if (!parse_hex(hex32 + 2 * i, 2, &byte))
            return false;
        key[i] = (uint8_t)byte;
    }
    return true;
}

const char *packet_auth_open(const char *packet, size_t len, const uint8_t key[16], uint32_t *seq)
{
    uint64_t tag, seq64;
    if (!packet || len <= PACKET_AUTH_HEADER_LEN || !parse_hex(packet, 16, &tag) ||
        !parse_hex(packet + 16, 8, &seq64))
        return NULL;
    if (siphash24(key, packet + 16, len - 16) != tag)
        return NULL;
    if (seq)
        *seq = (uint32_t)seq64;
    return packet + PACKET_AUTH_HEADER_LEN;
}

size_t packet_auth_seal(const uint8_t key[16], uint32_t seq, const char *body, char *out, size_t out_size)
{
    size_t body_len = strlen(body);
    if (out_size < PACKET_AUTH_HEADER_LEN + body_len + 1)
        return 0;
    snprintf(out + 16, out_size - 16, "%08lx%s", (unsigned long)seq, body);
    uint64_t tag = siphash24(key, out + 16, 8 + body_len);
    char tag_hex[17];
    snprintf(tag_hex, sizeof(tag_hex), "%08lx%08lx", (unsigned long)(tag >> 32), (unsigned long)(tag & 0xffffffffUL));
    memcpy(out, tag_hex, 16);
    return PACKET_AUTH_HEADER_LEN + body_len;
}

packet_freshness_t packet_window_check(packet_window_t *window, uint32_t seq)
{
    if (seq == 0)
        return PACKET_STALE;
    if (seq > window->newest)
    {
        uint32_t shift = seq - window->newest;
        window->seen = shift >= 32 ? 1u : (window->seen << shift) | 1u;
        window->newest = seq;
        return PACKET_NEW;
    }
    uint32_t age = window->newest - seq;
    if (age >= 32)
        return PACKET_STALE;
    if (window->seen & (1u << age))
        return PACKET_DUPLICATE;
    window->seen |= 1u << age;
    return PACKET_NEW;
}
