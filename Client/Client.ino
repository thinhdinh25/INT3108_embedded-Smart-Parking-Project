#include <ESP8266WiFi.h>
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
#define FIRE   8

const int sensorPins[7] = {IR1, IR2, IR3, IR4, IR5, WATER, FIRE};

int state[7] = {0};
int pendingState[7] = {0};

unsigned long changeTime[7] = {0};

const unsigned long STABLE_TIME = 10000;

void init_sensor() {
    for (int i = 0; i < 7; i++) {
        pinMode(sensorPins[i], INPUT);
        int current = digitalRead(sensorPins[i]);
        state[i] = current;
        pendingState[i] = current;
        changeTime[i] = millis();
    }
}

void update_sensor() {
    unsigned long now = millis();

    for (int i = 0; i < 7; i++) {
        int current = digitalRead(sensorPins[i]);

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

    // The server protocol requires five binary slots and numeric temperature
    // and water fields. Those two measurements are not implemented by this client.
    char payload[128];
    int length = snprintf(payload, sizeof(payload),
                          "{\"type\":\"parking_update\",\"slots\":[%d,%d,%d,%d,%d],\"temperature_c\":0,\"water_mm\":0}",
                          state[0], state[1], state[2], state[3], state[4]);
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
