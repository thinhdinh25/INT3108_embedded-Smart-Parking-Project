#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

const char* ssid = "Bep Vu Son";
const char* password = "88888888";



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

IPAddress serverIP(192, 168, 1, 85);
const uint16_t serverPort = 10000;

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 10000;

void send_UDP() {
    udp.beginPacket(serverIP, serverPort);
    for (int i = 0; i < 7; i++) {
        udp.print(state[i]);
        if (i < 6)
            udp.print(",");
    }
    udp.endPacket();
    Serial.print("Sent: ");
    for (int i = 0; i < 7; i++) {
        Serial.print(state[i]);
        if (i < 6)
            Serial.print(",");
    }
    Serial.println();
}

/******************UDP SECTION END**********************/


void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  ArduinoOTA.begin();
  init_sensor();
  udp.begin(4210);
}

void loop() {
  ArduinoOTA.handle();
  update_sensor();
  send_UDP();
  delay(1000);
}
