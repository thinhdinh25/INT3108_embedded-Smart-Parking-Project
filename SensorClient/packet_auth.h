// Signed UDP packets, same format as Server/main/parking/packet_auth.c (identical copy in
// RFIDClient/; the host tests check that the server accepts what both sketches send):
//   "<tag:16 hex><seq:8 hex><json>", tag = SipHash-2-4(key, seq hex + json)
#pragma once
#include <EEPROM.h>

static uint64_t sip_load64(const uint8_t *p) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
  return v;
}

#define SIP_ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))
#define SIP_ROUND                                                     \
  do {                                                                \
    v0 += v1; v1 = SIP_ROTL(v1, 13); v1 ^= v0; v0 = SIP_ROTL(v0, 32); \
    v2 += v3; v3 = SIP_ROTL(v3, 16); v3 ^= v2;                        \
    v0 += v3; v3 = SIP_ROTL(v3, 21); v3 ^= v0;                        \
    v2 += v1; v1 = SIP_ROTL(v1, 17); v1 ^= v2; v2 = SIP_ROTL(v2, 32); \
  } while (0)

static uint64_t siphash24(const uint8_t key[16], const uint8_t *m, size_t len) {
  const uint64_t k0 = sip_load64(key), k1 = sip_load64(key + 8);
  uint64_t v0 = k0 ^ 0x736f6d6570736575ULL, v1 = k1 ^ 0x646f72616e646f6dULL;
  uint64_t v2 = k0 ^ 0x6c7967656e657261ULL, v3 = k1 ^ 0x7465646279746573ULL;
  uint64_t b = (uint64_t)len << 56;
  for (; len >= 8; len -= 8, m += 8) {
    uint64_t mi = sip_load64(m);
    v3 ^= mi; SIP_ROUND; SIP_ROUND; v0 ^= mi;
  }
  for (size_t i = 0; i < len; i++) b |= (uint64_t)m[i] << (8 * i);
  v3 ^= b; SIP_ROUND; SIP_ROUND; v0 ^= b;
  v2 ^= 0xff; SIP_ROUND; SIP_ROUND; SIP_ROUND; SIP_ROUND;
  return v0 ^ v1 ^ v2 ^ v3;
}

// Returns the packet length, or 0 if out is too small.
static int auth_seal(const uint8_t key[16], uint32_t seq, const char *json, char *out, size_t outSize) {
  size_t jsonLen = strlen(json);
  if (outSize < 24 + jsonLen + 1) return 0;
  snprintf(out + 16, outSize - 16, "%08lx%s", (unsigned long)seq, json);
  uint64_t tag = siphash24(key, (const uint8_t *)out + 16, 8 + jsonLen);
  char tagHex[17];
  snprintf(tagHex, sizeof(tagHex), "%08lx%08lx", (unsigned long)(tag >> 32), (unsigned long)(tag & 0xffffffffUL));
  memcpy(out, tagHex, 16);
  return 24 + jsonLen;
}

// The server accepts each sequence number once and only if it is recent, so numbers must
// keep growing across reboots: blocks are reserved in flash, one write per SEQ_BLOCK packets.
static const uint32_t SEQ_BLOCK = 65536;
static uint32_t seqNext = 0, seqReserved = 0;

static void seq_begin() {
  EEPROM.begin(sizeof(uint32_t));
  EEPROM.get(0, seqReserved);
  if (seqReserved == 0xFFFFFFFFUL) seqReserved = 0;  // erased flash
  seqNext = seqReserved + 1;  // above everything the last boot could have used
}

static uint32_t seq_take() {
  if (seqNext >= seqReserved) {
    seqReserved = seqNext + SEQ_BLOCK;
    EEPROM.put(0, seqReserved);
    EEPROM.commit();
  }
  return seqNext++;
}
