"""Sign UDP packets like the real clients: "<tag:16 hex><seq:8 hex><json>",
tag = SipHash-2-4(key, seq hex + json). Same format as Server/main/parking/packet_auth.c."""

import time

# Must match PARKING_UDP_KEY on the server ("ParkingKey-Demo!" by default).
DEMO_KEY = bytes.fromhex("5061726b696e674b65792d44656d6f21")
MASK = (1 << 64) - 1


def _rotl(x, b):
    return ((x << b) | (x >> (64 - b))) & MASK


def siphash24(key, data):
    k0 = int.from_bytes(key[:8], "little")
    k1 = int.from_bytes(key[8:], "little")
    v = [k0 ^ 0x736F6D6570736575, k1 ^ 0x646F72616E646F6D,
         k0 ^ 0x6C7967656E657261, k1 ^ 0x7465646279746573]

    def rounds(n):
        for _ in range(n):
            v[0] = (v[0] + v[1]) & MASK; v[1] = _rotl(v[1], 13) ^ v[0]; v[0] = _rotl(v[0], 32)
            v[2] = (v[2] + v[3]) & MASK; v[3] = _rotl(v[3], 16) ^ v[2]
            v[0] = (v[0] + v[3]) & MASK; v[3] = _rotl(v[3], 21) ^ v[0]
            v[2] = (v[2] + v[1]) & MASK; v[1] = _rotl(v[1], 17) ^ v[2]; v[2] = _rotl(v[2], 32)

    tail = len(data) % 8
    for i in range(0, len(data) - tail, 8):
        m = int.from_bytes(data[i:i + 8], "little")
        v[3] ^= m
        rounds(2)
        v[0] ^= m
    b = (len(data) & 0xFF) << 56 | int.from_bytes(data[len(data) - tail:], "little")
    v[3] ^= b
    rounds(2)
    v[0] ^= b
    v[2] ^= 0xFF
    rounds(4)
    return v[0] ^ v[1] ^ v[2] ^ v[3]


def default_seq():
    # Tenths of a second since 2024: always newer than the last simulator run.
    # shortcut: shares the server's replay window with the real client of the same type, so
    # the real client is rejected after a simulator run until NVS is erased; use one or the other.
    return int((time.time() - 1704067200) * 10) & 0xFFFFFFFF


def seal(body, seq=None, key=DEMO_KEY):
    seq = default_seq() if seq is None else seq
    signed = f"{seq:08x}".encode() + body
    return f"{siphash24(key, signed):016x}".encode() + signed


if __name__ == "__main__":
    # Reference vector from the SipHash paper.
    assert siphash24(bytes(range(16)), bytes(range(15))) == 0xA129CA6149BE45E5
    print("ok")
