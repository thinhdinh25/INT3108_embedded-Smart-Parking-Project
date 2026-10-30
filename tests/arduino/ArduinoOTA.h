#pragma once
typedef int ota_error_t;
enum { OTA_AUTH_ERROR, OTA_BEGIN_ERROR, OTA_CONNECT_ERROR, OTA_RECEIVE_ERROR, OTA_END_ERROR };
struct FakeOTA {
    void begin() {}
    void handle() {}
    void setHostname(const char *) {}
    template <typename F> void onStart(F) {}
    template <typename F> void onEnd(F) {}
    template <typename F> void onProgress(F) {}
    template <typename F> void onError(F) {}
};
inline FakeOTA ArduinoOTA;
