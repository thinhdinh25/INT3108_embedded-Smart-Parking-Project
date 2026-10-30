#pragma once
#include "Arduino.h"
/* Every packet passed to endPacket() is recorded in fake::udp_sent. */
struct WiFiUDP {
    std::string pending;
    uint16_t port = 0;
    void begin(uint16_t) {}
    int beginPacket(IPAddress, uint16_t p) { pending.clear(); port = p; return 1; }
    size_t write(const uint8_t *data, size_t len) { pending.append((const char *)data, len); return len; }
    int endPacket() { fake::udp_sent.push_back(pending); return 1; }
};
