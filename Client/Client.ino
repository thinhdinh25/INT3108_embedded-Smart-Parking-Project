#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
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
    int length = snprintf(payload, sizeof(payload),
                          "{\"type\":\"parking_update\",\"slots\":[%d,%d,%d,%d,%d],\"water_mm\":%.1f,\"fire\":%s}",
                          state[0], state[1], state[2], state[3], state[4],
                          currentWaterMm, flameState ? "true" : "false");
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
}

/******************UDP SECTION END**********************/

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);

    Serial.print("Connecting to ");
    Serial.println(ssid);
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
    update_sensor();
    unsigned long now = millis();
    if (now - lastSend >= SEND_INTERVAL) {
        lastSend = now;
        send_UDP();
    }
}
