#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

// Join the access point created by the ESP32 parking server.
const char* ssid = "ESP32-Parking";
const char* password = "12345678";

/******************SENSOR SECTION BEGIN**********************/

#define IR1    12
#define IR2    13
#define IR3    14
#define IR4    4
#define IR5    16

<<<<<<< HEAD
#define WATER  4
#define TEMP   5
#define FIRE   2
#define FIRE_ACTIVE_LEVEL LOW
=======
#define WATER  A0
#define FLAME  5
>>>>>>> 15f5fd4d151b7ef1b5830050f4d57b246b4096f0

// The server expects exactly five binary parking-slot values.
const int sensorPins[5] = {IR1, IR2, IR3, IR4, IR5};

bool state[5] = {0};
int pendingState[5] = {0};
unsigned long changeTime[5] = {0};

<<<<<<< HEAD
// Water and temperature readings remain sample values until calibrated for the installed sensors.
const float SAMPLE_TEMPERATURE_C = 28.0f;
const float SAMPLE_WATER_MM = 0.2f;

const unsigned long STABLE_TIME = 250;

// Reverse the IR signal: raw LOW (0) -> occupied (1),
// raw HIGH (1) -> empty (0).
bool read_slot_state(int pin) {
    return !digitalRead(pin);
=======
// Flame sensor state and debounce tracking
bool flameState = false;
bool pendingFlameState = false;
unsigned long flameChangeTime = 0;

// Water sensor tracking and calibration
float currentWaterMm = 0.0f;
const int WATER_DRY_ADC = 20;            // Noise floor when sensor is dry
const int WATER_MAX_ADC = 700;           // Typical max reading when fully submerged
const float MAX_SENSOR_DEPTH_MM = 40.0f; // Standard resistive sensor trace length (40mm)

const unsigned long STABLE_TIME = 250;

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
>>>>>>> 15f5fd4d151b7ef1b5830050f4d57b246b4096f0
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

void update_sensor() {
    unsigned long now = millis();

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
        Serial.print("Flame Alert = ");
        Serial.println(flameState ? "DETECTED (1)" : "CLEAR (0)");
    }

    // 3. Update Water Level Sensor
    currentWaterMm = read_water_mm();
}

/******************SENSOR SECTION END**********************/

/******************HTTP SECTION BEGIN*********************/

const char* serverURL = "http://192.168.4.1/api/parking";

const unsigned long SEND_INTERVAL = 1000;
unsigned long lastSend = 0;

<<<<<<< HEAD
bool read_fire_state() {
    return digitalRead(FIRE) == FIRE_ACTIVE_LEVEL;
=======
void send_UDP() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected; UDP update skipped");
        return;
    }

    // `1` means occupied/detected and `0` means free/clear.
    char payload[128];
    int length = snprintf(payload, sizeof(payload),
                          "{\"type\":\"parking_update\",\"slots\":[%d,%d,%d,%d,%d],\"water_mm\":%.1f,\"flame\":%d}",
                          state[0], state[1], state[2], state[3], state[4],
                          currentWaterMm, flameState ? 1 : 0);
    if (length < 0 || length >= sizeof(payload)) {
        Serial.println("UDP payload formatting failed");
        return;
    }

    if (udp.beginPacket(serverIP, serverPort) &&
        udp.write(reinterpret_cast<const uint8_t*>(payload), length) == length &&
        udp.endPacket()) {
        Serial.print("Sent: ");
        Serial.println(payload);
    } else {
        Serial.println("UDP send failed");
    }
>>>>>>> 15f5fd4d151b7ef1b5830050f4d57b246b4096f0
}

void send_HTTP() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected; HTTP update skipped");
        return;
    }

    char payload[160];
    int length = snprintf(
        payload,
        sizeof(payload),
        "{\"type\":\"parking_update\",\"slots\":[%d,%d,%d,%d,%d],"
        "\"temperature_c\":%.1f,\"water_mm\":%.1f,\"fire\":%s}",
        state[0], state[1], state[2], state[3], state[4],
        SAMPLE_TEMPERATURE_C, SAMPLE_WATER_MM,
        read_fire_state() ? "true" : "false");

    if (length < 0 || length >= sizeof(payload)) {
        Serial.println("HTTP payload formatting failed");
        return;
    }

    WiFiClient client;
    HTTPClient http;
    if (!http.begin(client, serverURL)) {
        Serial.println("HTTP connection setup failed");
        return;
    }

    http.addHeader("Content-Type", "application/json");
    int status = http.POST(reinterpret_cast<uint8_t*>(payload), length);
    if (status >= 200 && status < 300) {
        Serial.print("Server accepted: ");
        Serial.println(payload);
    } else {
        Serial.printf("HTTP update failed: %d\n", status);
    }
    http.end();
}

/******************HTTP SECTION END**********************/

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);
<<<<<<< HEAD

    Serial.print("Connecting to ");
    Serial.println(ssid);

=======
    Serial.print("Connecting to ");
    Serial.println(ssid);
>>>>>>> 15f5fd4d151b7ef1b5830050f4d57b246b4096f0
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
<<<<<<< HEAD

    Serial.println();
    Serial.print("Connected, client IP: ");
    Serial.println(WiFi.localIP());

    ArduinoOTA.begin();
    init_sensor();
    pinMode(FIRE, INPUT_PULLUP);
=======
    Serial.println();
    Serial.print("Connected, client IP: ");
    Serial.println(WiFi.localIP());
    ArduinoOTA.begin();
    init_sensor();
    udp.begin(4210);
>>>>>>> 15f5fd4d151b7ef1b5830050f4d57b246b4096f0
}

void loop() {
    ArduinoOTA.handle();
    update_sensor();
<<<<<<< HEAD

    unsigned long now = millis();
    if (now - lastSend >= SEND_INTERVAL) {
        lastSend = now;
        send_HTTP();
    }
}
=======
    unsigned long now = millis();
    if (now - lastSend >= SEND_INTERVAL) {
        lastSend = now;
        send_UDP();
    }
}
>>>>>>> 15f5fd4d151b7ef1b5830050f4d57b246b4096f0
