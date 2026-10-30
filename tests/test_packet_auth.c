/* Signed UDP packets: SipHash-2-4 reference vectors, seal/open, tampering, the replay window. */
#include <string.h>

#include "packet_auth.h"
#include "test.h"

static void test_siphash_vectors(void)
{
    /* Reference vectors from the SipHash paper: key 00..0f, message 00..len-1. */
    uint8_t key[16], msg[64];
    for (int i = 0; i < 16; ++i)
        key[i] = (uint8_t)i;
    for (int i = 0; i < 64; ++i)
        msg[i] = (uint8_t)i;
    CHECK(siphash24(key, msg, 0) == 0x726fdb47dd0e0e31ULL);
    CHECK(siphash24(key, msg, 15) == 0xa129ca6149be45e5ULL);
    CHECK(siphash24(key, msg, 63) == 0x958a324ceb064572ULL);
}

static void test_seal_open(void)
{
    uint8_t key[16];
    CHECK(packet_auth_parse_key("5061726b696e674b65792d44656d6f21", key));
    CHECK(key[0] == 0x50 && key[15] == 0x21);
    CHECK(!packet_auth_parse_key("5061726b696e674b65792d44656d6f2", key));   /* 31 digits */
    CHECK(!packet_auth_parse_key("5061726b696e674b65792d44656d6fzz", key));  /* not hex */

    char packet[128];
    const char *body = "{\"type\":\"climate\",\"temperature_c\":28.0}";
    size_t len = packet_auth_seal(key, 0x1234abcd, body, packet, sizeof(packet));
    CHECK(len == PACKET_AUTH_HEADER_LEN + strlen(body));
    CHECK(memcmp(packet + 16, "1234abcd", 8) == 0);

    uint32_t seq = 0;
    const char *opened = packet_auth_open(packet, len, key, &seq);
    CHECK(opened && strcmp(opened, body) == 0 && seq == 0x1234abcd);

    /* Any changed byte, in the tag, the sequence number or the body, is rejected. */
    bool all_rejected = true;
    for (size_t i = 0; i < len; ++i)
    {
        char tampered[128];
        memcpy(tampered, packet, len + 1);
        tampered[i] = tampered[i] == '0' ? '1' : '0';
        all_rejected &= packet_auth_open(tampered, len, key, NULL) == NULL;
    }
    CHECK(all_rejected);

    uint8_t other[16] = {0};
    CHECK(packet_auth_open(packet, len, other, NULL) == NULL); /* wrong key */
    CHECK(packet_auth_open(body, strlen(body), key, NULL) == NULL); /* unsigned JSON */
    CHECK(packet_auth_open(packet, PACKET_AUTH_HEADER_LEN, key, NULL) == NULL); /* empty body */
    CHECK(packet_auth_seal(key, 1, body, packet, PACKET_AUTH_HEADER_LEN + strlen(body)) == 0); /* no room */
}

static void test_replay_window(void)
{
    packet_window_t w = {0};
    CHECK(packet_window_check(&w, 0) == PACKET_STALE);
    CHECK(packet_window_check(&w, 10) == PACKET_NEW);
    CHECK(packet_window_check(&w, 10) == PACKET_DUPLICATE); /* a client retry */
    CHECK(packet_window_check(&w, 12) == PACKET_NEW);
    CHECK(packet_window_check(&w, 11) == PACKET_NEW);       /* late, but never seen */
    CHECK(packet_window_check(&w, 11) == PACKET_DUPLICATE);
    CHECK(packet_window_check(&w, 100) == PACKET_NEW);
    CHECK(packet_window_check(&w, 69) == PACKET_NEW);       /* 31 behind: still in window */
    CHECK(packet_window_check(&w, 68) == PACKET_STALE);     /* 32 behind: replay */
    CHECK(packet_window_check(&w, 12) == PACKET_STALE);
    CHECK(packet_window_check(&w, 100000) == PACKET_NEW);   /* jump after a client reboot */
    CHECK(packet_window_check(&w, 100) == PACKET_STALE);
}

int main(void)
{
    test_siphash_vectors();
    test_seal_open();
    test_replay_window();
    return TEST_REPORT("packet_auth");
}
