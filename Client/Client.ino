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
#define IR4    15
#define IR5    16

#define WATER  4
#define TEMP   5

// The server expects exactly five binary parking-slot values.
const int sensorPins[5] = {IR1, IR2, IR3, IR4, IR5};

bool state[5] = {0};
int pendingState[5] = {0};

unsigned long changeTime[5] = {0};

// Sample values used until the water and temperature sensors are integrated.
const float SAMPLE_TEMPERATURE_C = 28.0f;
const float SAMPLE_WATER_MM = 0.2f;

const unsigned long STABLE_TIME = 250;

// These IR modules assert LOW when a vehicle is detected.
bool read_slot_state(int pin) {
    return digitalRead(pin) == LOW;
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



/******************UDP SECTION BEGIN**********************/
WiFiUDP udp;

// The server listens on UDP 3333 and accepts broadcast packets on its AP.
IPAddress serverIP(192, 168, 4, 255);
const uint16_t serverPort = 3333;

const unsigned long SEND_INTERVAL = 1000;
unsigned long lastSend = 0;

void send_UDP() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected; UDP update skipped");
        return;
    }

    // `1` means occupied and `0` means free. Temperature and water are sample
    // values until those sensors are integrated.
    char payload[128];
    int length = snprintf(payload, sizeof(payload),
                          "{\"type\":\"parking_update\",\"slots\":[%d,%d,%d,%d,%d],\"temperature_c\":%.1f,\"water_mm\":%.1f}",
                          state[0], state[1], state[2], state[3], state[4],
                          SAMPLE_TEMPERATURE_C, SAMPLE_WATER_MM);
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
