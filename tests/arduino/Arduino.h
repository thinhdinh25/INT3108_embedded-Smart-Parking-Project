/* Host shim for testing the ESP8266 sketches: a fake clock, pins, serial and UDP capture. */
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

typedef uint8_t byte;
#define INPUT 0
#define OUTPUT 1
#define LOW 0
#define HIGH 1
#define A0 17
#define F(s) (s)
using std::isnan;

namespace fake {
inline unsigned long now_ms = 0;
inline int pin_level[32];      /* digital input levels the sketch reads */
inline int adc_value = 0;      /* A0 reading */
inline std::vector<std::string> udp_sent;
}

inline unsigned long millis() { return fake::now_ms; }
inline void delay(unsigned long ms) { fake::now_ms += ms; }
inline void yield() {}
inline void pinMode(int, int) {}
inline int digitalRead(int pin) { return fake::pin_level[pin]; }
inline void digitalWrite(int, int) {}
inline int analogRead(int) { return fake::adc_value; }
template <typename T> T constrain(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct IPAddress {
    uint8_t b[4];
    IPAddress(int a = 0, int c = 0, int d = 0, int e = 0) : b{(uint8_t)a, (uint8_t)c, (uint8_t)d, (uint8_t)e} {}
};

struct FakeSerial {
    void begin(unsigned long) {}
    explicit operator bool() const { return true; }
    template <typename T> void print(const T &) {}
    template <typename T> void println(const T &) {}
    void println() {}
    template <typename... A> void printf(const char *, A...) {}
};
inline FakeSerial Serial;
