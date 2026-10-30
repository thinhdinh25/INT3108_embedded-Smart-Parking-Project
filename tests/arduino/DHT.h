#pragma once
#include "Arduino.h"
#define DHT11 11
namespace fake { inline float dht_temperature = 28.0f; }
struct DHT {
    DHT(int, int) {}
    void begin() {}
    float readHumidity() { return 60.0f; }
    float readTemperature() { return fake::dht_temperature; }
};
