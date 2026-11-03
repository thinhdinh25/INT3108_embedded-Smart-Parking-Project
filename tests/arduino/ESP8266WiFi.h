#pragma once
#include "Arduino.h"
#define WL_CONNECTED 3
#define WIFI_STA 1
#define WIFI_AP 2
struct FakeWiFi {
    int mode_ = 0;
    bool connected = true;
    void mode(int m) { mode_ = m; }
    int getMode() const { return mode_; }
    void setAutoReconnect(bool) {}
    void begin(const char *, const char *) {}
    int status() const { return connected ? WL_CONNECTED : 0; }
    IPAddress local_ip{192, 168, 100, 44};
    IPAddress localIP() const { return local_ip; }
    bool softAPConfig(IPAddress, IPAddress, IPAddress) { return true; }
    bool softAP(const char *, const char *, int = 1, bool = false, int = 4) { return true; }
    IPAddress softAPIP() const { return IPAddress(192, 168, 4, 1); }
};
inline FakeWiFi WiFi;
