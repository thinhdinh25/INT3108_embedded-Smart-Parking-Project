#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "packet_auth.h"

// Wi-Fi of the RFID client's access point and the UDP signing key.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

/******************SENSOR SECTION BEGIN**********************/

#define IR1    16
#define IR2    4
#define IR3    14
#define IR4    13
#define IR5    12

#define WATER  A0
#define FLAME  5

// The server expects exactly five binary parking-slot values.
const int sensorPins[5] = {IR1, IR2, IR3, IR4, IR5};

bool state[5] = {0};
int pendingState[5] = {0};
unsigned long changeTime[5] = {0};

// Flame sensor state and debounce tracking
bool flameState = false;
bool pendingFlameState = false;
unsigned long flameChangeTime = 0;


unsigned long lastWaterRead = 0;
const unsigned long WATER_READ_INTERVAL = 250;


// Water sensor tracking and calibration
float currentWaterMm = 0.0f;
const int WATER_DRY_ADC = 20;            // Noise floor when sensor is dry
const int WATER_MAX_ADC = 700;           // Typical max reading when fully submerged
const float MAX_SENSOR_DEPTH_MM = 40.0f; // Standard resistive sensor trace length (40mm)

const unsigned long STABLE_TIME = 50;

// Standard IR and Flame modules assert LOW (0) when triggered.
bool read_active_low(int pin) {
    return !digitalRead(pin);  // raw LOW (0) -> active (1); raw HIGH (1) -> inactive (0)
}

float read_water_mm() {
    int raw = analogRead(WATER);
    if (raw <= WATER_DRY_ADC) {
        return 0.0f;
    }
    int constrainedRaw = constrain(raw, WATER_DRY_ADC, WATER_MAX_ADC);
    return (float)(constrainedRaw - WATER_DRY_ADC) * MAX_SENSOR_DEPTH_MM / (float)(WATER_MAX_ADC - WATER_DRY_ADC);
}

void init_sensor() {
    for (int i = 0; i < 5; i++) {
        pinMode(sensorPins[i], INPUT);
        int current = read_active_low(sensorPins[i]);
        state[i] = current;
        pendingState[i] = current;
        changeTime[i] = millis();
    }

    // Initialize Flame Sensor
    pinMode(FLAME, INPUT);
    flameState = read_active_low(FLAME);
    pendingFlameState = flameState;
    flameChangeTime = millis();

    // Initialize Water Sensor (A0 is input by default on ESP8266)
    currentWaterMm = read_water_mm();
}

// Returns true when a debounced slot or flame state changed, so it can be sent at once.
bool update_sensor() {
    unsigned long now = millis();
    bool changed = false;

    // 1. Update IR Parking Sensors
    for (int i = 0; i < 5; i++) {
        int current = read_active_low(sensorPins[i]);

        if (current != pendingState[i]) {
            pendingState[i] = current;
            changeTime[i] = now;
        }

        if (pendingState[i] != state[i] &&
            now - changeTime[i] >= STABLE_TIME) {
            state[i] = pendingState[i];
            changed = true;

            Serial.print("Sensor ");
            Serial.print(i);
            Serial.print(" = ");
            Serial.println(state[i]);
        }
    }

    // 2. Update Flame Sensor (with debounce)
    bool currentFlame = read_active_low(FLAME);
    if (currentFlame != pendingFlameState) {
        pendingFlameState = currentFlame;
        flameChangeTime = now;
    }

    if (pendingFlameState != flameState &&
        now - flameChangeTime >= STABLE_TIME) {

        flameState = pendingFlameState;
        changed = true;
        Serial.print("Flame Alert = ");
        Serial.println(flameState ? "DETECTED (1)" : "CLEAR (0)");
    }

    // 3. Update Water Level Sensor
    if (now - lastWaterRead >= WATER_READ_INTERVAL) {
        lastWaterRead = now;
        currentWaterMm = read_water_mm();
    }
    return changed;
}

/******************SENSOR SECTION END**********************/

/******************UDP SECTION BEGIN*********************/

WiFiUDP udp;
IPAddress serverIP(192, 168, 4, 255);
const uint16_t serverPort = 3333;

const unsigned long SEND_INTERVAL = 1000;
unsigned long lastSend = 0;

void send_UDP() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected; UDP update skipped");
        return;
    }

    // `1` means occupied/detected and `0` means free/clear.
    char payload[112];
    int jsonLength = snprintf(payload, sizeof(payload),
                          "{\"type\":\"parking_update\",\"slots\":[%d,%d,%d,%d,%d],\"water_mm\":%.1f,\"fire\":%s}",
                          state[0], state[1], state[2], state[3], state[4],
                          currentWaterMm, flameState ? "false" : "true");
    char packet[sizeof(payload) + 24];
    int length = jsonLength < 0 || jsonLength >= sizeof(payload)
                     ? 0 : auth_seal(UDP_KEY, seq_take(), payload, packet, sizeof(packet));
    if (length == 0) {
        Serial.println("UDP payload formatting failed");
        return;
    }

    if (udp.beginPacket(serverIP, serverPort) &&
        udp.write(reinterpret_cast<const uint8_t*>(packet), length) == length &&
        udp.endPacket()) {
        Serial.print("Sent: ");
        Serial.println(payload);
    } else {
        Serial.println("UDP send failed");
    }
}

/******************UDP SECTION END**********************/

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    seq_begin();

    Serial.print("Connecting to ");
    Serial.println(WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.print("Connected, client IP: ");
    Serial.println(WiFi.localIP());

    ArduinoOTA.begin();
    init_sensor();
    udp.begin(4210);
}

void loop() {
    ArduinoOTA.handle();
    bool changed = update_sensor();
    unsigned long now = millis();
    // Send on every debounced change for low latency, plus a 1 s heartbeat so a lost
    // datagram is corrected by the next full snapshot.
    if (changed || now - lastSend >= SEND_INTERVAL) {
        lastSend = now;
        send_UDP();
    }
}
