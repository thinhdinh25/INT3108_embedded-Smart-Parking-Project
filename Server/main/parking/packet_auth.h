#ifndef PACKET_AUTH_H
#define PACKET_AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Signed UDP packet: "<tag:16 hex><seq:8 hex><body>", tag = SipHash-2-4(key, seq hex + body).
 * The clients carry a copy of this format in packet_auth.h next to each sketch. */
#define PACKET_AUTH_HEADER_LEN 24

uint64_t siphash24(const uint8_t key[16], const void *data, size_t len);
bool packet_auth_parse_key(const char *hex32, uint8_t key[16]);
/* Returns the body, or NULL if the packet is malformed or its tag is wrong. */
const char *packet_auth_open(const char *packet, size_t len, const uint8_t key[16], uint32_t *seq);
/* Returns the packet length, or 0 if out is too small. */
size_t packet_auth_seal(const uint8_t key[16], uint32_t seq, const char *body, char *out, size_t out_size);

/* Anti-replay window (as in IPsec): each sequence number is accepted once, in any order,
 * as long as it is within 32 of the newest one seen. */
typedef struct
{
    uint32_t newest;
    uint32_t seen; /* bit i = newest - i already accepted */
} packet_window_t;

typedef enum
{
    PACKET_NEW,
    PACKET_DUPLICATE, /* already accepted: a retry, answer it again but do not act twice */
    PACKET_STALE,     /* too old or 0: drop */
} packet_freshness_t;

packet_freshness_t packet_window_check(packet_window_t *window, uint32_t seq);

#endif
