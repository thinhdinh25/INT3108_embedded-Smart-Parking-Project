#pragma once
#include <deque>
#include "Arduino.h"
/* Every packet passed to endPacket() is recorded in fake::udp_sent; tests queue incoming
 * datagrams in fake::udp_inbox. */
namespace fake { inline std::deque<std::string> udp_inbox; }
struct WiFiUDP {
    std::string pending, current;
    uint16_t port = 0;
    void begin(uint16_t) {}
    int beginPacket(IPAddress, uint16_t p) { pending.clear(); port = p; return 1; }
    size_t write(const uint8_t *data, size_t len) { pending.append((const char *)data, len); return len; }
    int endPacket() { fake::udp_sent.push_back(pending); return 1; }
    int parsePacket() {
        if (fake::udp_inbox.empty()) return 0;
        current = fake::udp_inbox.front();
        fake::udp_inbox.pop_front();
        return (int)current.size();
    }
    int read(uint8_t *buf, size_t len) {
        size_t n = current.size() < len ? current.size() : len;
        memcpy(buf, current.data(), n);
        return (int)n;
    }
};
