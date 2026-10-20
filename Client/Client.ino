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
#define IR4    15
#define IR5    16

#define WATER  4
#define TEMP   5
#define FIRE   2
#define FIRE_ACTIVE_LEVEL LOW

// The server expects exactly five binary parking-slot values.
const int sensorPins[5] = {IR1, IR2, IR3, IR4, IR5};

bool state[5] = {0};
int pendingState[5] = {0};

unsigned long changeTime[5] = {0};

// Water and temperature readings remain sample values until calibrated for the installed sensors.
const float SAMPLE_TEMPERATURE_C = 28.0f;
const float SAMPLE_WATER_MM = 0.2f;

const unsigned long STABLE_TIME = 250;

// Reverse the IR signal: raw LOW (0) -> occupied (1),
// raw HIGH (1) -> empty (0).
bool read_slot_state(int pin) {
    return !digitalRead(pin);
}

void init_sensor() {
    for (int i = 0; i < 5; i++) {
        pinMode(sensorPins[i], INPUT);
        int current = read_slot_state(sensorPins[i]);
        state[i] = current;
        pendingState[i] = current;
        changeTime[i] = millis();
    }
}

void update_sensor() {
    unsigned long now = millis();

    for (int i = 0; i < 5; i++) {
        int current = read_slot_state(sensorPins[i]);

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
}

/******************SENSOR SECTION END**********************/

/******************HTTP SECTION BEGIN*********************/

const char* serverURL = "http://192.168.4.1/api/parking";

const unsigned long SEND_INTERVAL = 1000;
unsigned long lastSend = 0;

bool read_fire_state() {
    return digitalRead(FIRE) == FIRE_ACTIVE_LEVEL;
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
    pinMode(FIRE, INPUT_PULLUP);
}

void loop() {
    ArduinoOTA.handle();
    update_sensor();

    unsigned long now = millis();
    if (now - lastSend >= SEND_INTERVAL) {
        lastSend = now;
        send_HTTP();
    }
}
