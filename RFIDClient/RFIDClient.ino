#include <SPI.h>
#include <MFRC522.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <DHT.h>

// --- CẤU HÌNH WIFI & UDP --- (mật khẩu Wi-Fi nằm trong secrets.h)
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

WiFiUDP udp;
// Broadcast của mạng con lấy lúc khởi động (VD: 192.168.100.44/24 -> 192.168.100.255)
IPAddress udpAddress(255, 255, 255, 255);
const unsigned int udpPort = 3333;              // Cổng UDP đích

// --- CẤU HÌNH PHẦN CỨNG RC522 ---
#define RST_PIN   16  // GPIO 16 (D0) dùng chung cả 2 module
#define SS_1_PIN  5   // GPIO 4 (D2) cho Đầu đọc 1 (Cổng VÀO - IN)
#define SS_2_PIN  4   // GPIO 5 (D1) cho Đầu đọc 2 (Cổng RA - OUT)
#define NR_OF_READERS 2

#define READER_IN  0
#define READER_OUT 1

byte ssPins[] = {SS_1_PIN, SS_2_PIN};
MFRC522 mfrc522[NR_OF_READERS];

// --- CẤU HÌNH PHẦN CỨNG DHT11 ---
#define DHTPIN    2      // GPIO 2 (Chân D4 trên NodeMCU/Wemos)
#define DHTTYPE   DHT11  // Cảm biến DHT11
DHT dht(DHTPIN, DHTTYPE);

unsigned long lastDHTReadTime = 0;
const unsigned long DHT_INTERVAL = 5000; // Read DHT11 every 5 seconds
float currentTemperatureC = 0.0f;

// --- BIẾN CHỐNG ĐỌC LẶP THẺ (DEBOUNCE) ---
// Fixed buffers instead of String: no heap allocation per scan, so no fragmentation
// over long uptime. A 10-byte UID is 20 hex characters.
#define UID_HEX_MAX 21
char lastUID[NR_OF_READERS][UID_HEX_MAX] = {"", ""};
unsigned long lastReadTime[NR_OF_READERS] = {0, 0};
const unsigned long COOLDOWN_TIME = 2000; // Thời gian chờ đọc lại cùng UID (2 giây)

// --- GỬI LẠI SỰ KIỆN QUẸT THẺ ĐẾN KHI MÁY CHỦ XÁC NHẬN (ACK) ---
// A lost datagram no longer loses a scan: each one is resent every ACK_TIMEOUT until the
// server answers with an ACK carrying the same sequence number (it acts on each scan once).
// Numbers start at a random value each boot, so they rarely repeat what the server just saw.
#define PACKET_MAX 176
#define MAX_PENDING 4
const unsigned long ACK_TIMEOUT = 300;
const uint8_t MAX_TRIES = 6;
struct PendingScan {
  uint32_t seq;          // 0 = free slot
  uint8_t tries;
  unsigned long sentAt;
  int length;
  char packet[PACKET_MAX];
};
PendingScan pending[MAX_PENDING];
uint32_t nextSeq = 1;

// Khai báo hàm nguyên mẫu
void formatCardUID(const byte *buffer, byte bufferSize, char *out);
bool canReadUID(const char *uid, unsigned long now);
void sendUDPRfid(const char *uid, const char *gate);
void handleDHT11();
void sendJson(const char *json, PendingScan *retry, uint32_t seq);
void serviceAcks();
void setupOTA();

void setup() {
  Serial.begin(115200);
  while (!Serial);
  delay(200);
  Serial.println(F("\n================================================"));
  Serial.println(F(" 2 RC522 + DHT11 - OTA & UDP BROADCAST (3333)   "));
  Serial.println(F("================================================"));

  // 1. Khởi tạo DHT11
  dht.begin();

  // 2. Kết nối Wi-Fi chung (máy chủ và Sensor Client cũng vào mạng này)
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print(F("Đang kết nối Wi-Fi "));
  Serial.println(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.print(F("\nĐã kết nối, IP: "));
  Serial.println(WiFi.localIP());
  udpAddress = WiFi.broadcastIP();  // IP | ~mask: right for any subnet size, not just /24
  Serial.print(F("UDP broadcast IP: "));
  Serial.println(udpAddress);

  // 3. Khởi tạo cổng UDP & OTA
  nextSeq = ESP.random() | 1;  // hardware RNG; odd, so never 0
  udp.begin(udpPort);
  setupOTA();

  // 4. Khởi tạo SPI và RC522
  SPI.begin();

  for (uint8_t reader = 0; reader < NR_OF_READERS; reader++) {
    pinMode(ssPins[reader], OUTPUT);
    digitalWrite(ssPins[reader], HIGH);
  }

  for (uint8_t reader = 0; reader < NR_OF_READERS; reader++) {
    mfrc522[reader].PCD_Init(ssPins[reader], RST_PIN);
    delay(50);

    // Đẩy gain lên cực đại 48dB để tăng độ nhạy
    mfrc522[reader].PCD_SetAntennaGain(mfrc522[reader].RxGain_max);
    // Tắt ăng-ten để tránh nhiễu chéo
    mfrc522[reader].PCD_AntennaOff();

    Serial.print(F("Đầu đọc "));
    Serial.print(reader + 1);
    Serial.print(F(" (GPIO "));
    Serial.print(ssPins[reader]);
    Serial.print(F(") - Firmware: "));
    mfrc522[reader].PCD_DumpVersionToSerial();
  }

  Serial.println(F("Hệ thống đã sẵn sàng!\n"));
}

void loop() {
  // 1. Duy trì lắng nghe nạp code qua mạng (ArduinoOTA)
  ArduinoOTA.handle();

  // 2. Send the temperature every DHT_INTERVAL, independent of card scans.
  handleDHT11();
  serviceAcks();

  // 3. Quét lần lượt 2 đầu đọc RFID
  for (uint8_t reader = 0; reader < NR_OF_READERS; reader++) {
    
    // Bật sóng RF cho đầu đọc hiện tại
    mfrc522[reader].PCD_AntennaOn();
    delay(4); // Chờ 4ms cho từ trường nạp điện thẻ

    // Kiểm tra có thẻ đặt vào không
    if (mfrc522[reader].PICC_IsNewCardPresent() && mfrc522[reader].PICC_ReadCardSerial()) {
      
      char currentUID[UID_HEX_MAX];
      formatCardUID(mfrc522[reader].uid.uidByte, mfrc522[reader].uid.size, currentUID);
      unsigned long now = millis();

      // KIỂM TRA CHỐNG LẶP (chỉ đọc lại cùng UID sau 2 giây):
      if (canReadUID(currentUID, now)) {
        
        strcpy(lastUID[reader], currentUID);
        lastReadTime[reader] = now;

        Serial.print(F("=> [ĐẦU ĐỌC "));
        Serial.print(reader + 1);
        Serial.print(F("] Nhận thẻ UID: "));
        Serial.println(currentUID);

        // --- KHU VỰC XỬ LÝ LỆNH CHO TỪNG ĐẦU ĐỌC (IN / OUT) ---
        if (reader == READER_IN) {
          // Đầu đọc 1: Cổng VÀO (IN)
          Serial.println(F("-> Quét tại cổng VÀO (IN)"));
          sendUDPRfid(currentUID, "in");
        } else if (reader == READER_OUT) {
          // Đầu đọc 2: Cổng RA (OUT)
          Serial.println(F("-> Quét tại cổng RA (OUT)"));
          sendUDPRfid(currentUID, "out");
        }
        Serial.println(F("----------------------------------------"));
      }

      mfrc522[reader].PICC_HaltA();
      mfrc522[reader].PCD_StopCrypto1();
    }

    // Tắt sóng RF để nhường cho đầu đọc tiếp theo
    mfrc522[reader].PCD_AntennaOff();
    
    yield();
  }
}

// Chỉ cho phép đọc lại cùng một UID khi đã qua COOLDOWN_TIME (2 giây)
bool canReadUID(const char *uid, unsigned long now) {
  for (uint8_t r = 0; r < NR_OF_READERS; r++) {
    if (lastUID[r][0] != '\0' && strcmp(uid, lastUID[r]) == 0 && (now - lastReadTime[r] < COOLDOWN_TIME)) {
      return false;
    }
  }
  return true;
}

// Read the temperature periodically for the next RFID request.
void handleDHT11() {
  unsigned long now = millis();
  if (now - lastDHTReadTime >= DHT_INTERVAL) {
    lastDHTReadTime = now;

    float h = dht.readHumidity();
    float t = dht.readTemperature(); // Đọc độ C

    // Kiểm tra xem cảm biến có trả về dữ liệu hợp lệ không
    if (isnan(h) || isnan(t)) {
      Serial.println(F("-> [Lỗi DHT11] Không đọc được dữ liệu từ cảm biến trên GPIO 2!"));
      return;
    }

    currentTemperatureC = t;
    char json[48];
    snprintf(json, sizeof(json), "{\"type\":\"climate\",\"temperature_c\":%.1f}", t);
    sendJson(json, nullptr, 0);
  }
}

// Broadcasts json; with retry set, keeps it there until the server ACKs it.
void sendJson(const char *json, PendingScan *retry, uint32_t seq) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("-> [Lỗi UDP] Chưa kết nối Wi-Fi!"));
    return;
  }
  int length = strlen(json);
  udp.beginPacket(udpAddress, udpPort);
  udp.write(reinterpret_cast<const uint8_t*>(json), length);
  udp.endPacket();
  if (retry) {
    retry->seq = seq;
    retry->tries = 1;
    retry->sentAt = millis();
    retry->length = length;
    memcpy(retry->packet, json, length);
  }
}

// Clears scans the server has ACKed ({"type":"rfid_ack","seq":N}) and resends the rest.
// Other traffic on the port (the Sensor Client's broadcasts) does not match and is ignored.
void serviceAcks() {
  char in[PACKET_MAX];
  while (udp.parsePacket() > 0) {
    int n = udp.read(reinterpret_cast<uint8_t*>(in), sizeof(in) - 1);
    in[n > 0 ? n : 0] = '\0';
    for (PendingScan &p : pending) {
      char expected[48];
      snprintf(expected, sizeof(expected), "{\"type\":\"rfid_ack\",\"seq\":%lu}", (unsigned long)p.seq);
      if (p.seq != 0 && strcmp(in, expected) == 0) p.seq = 0;
    }
  }

  unsigned long now = millis();
  for (PendingScan &p : pending) {
    if (p.seq == 0 || now - p.sentAt < ACK_TIMEOUT) continue;
    if (p.tries >= MAX_TRIES) {
      Serial.println(F("-> [UDP RFID] Máy chủ không phản hồi, bỏ sự kiện"));
      p.seq = 0;
      continue;
    }
    udp.beginPacket(udpAddress, udpPort);
    udp.write(reinterpret_cast<const uint8_t*>(p.packet), p.length);
    udp.endPacket();
    p.tries++;
    p.sentAt = now;
  }
}

// Send an RFID scan using the server's request format; it is retried until ACKed.
void sendUDPRfid(const char *uid, const char *gate) {
  // Server format: {"type":"rfid_event","card_id":"A1B2C3D4","gate":"in","temperature_c":28.5,"seq":7}
  uint32_t seq = nextSeq++;
  if (nextSeq == 0) nextSeq = 1;  // 0 means "free slot" and is rejected by the server
  char payload[PACKET_MAX];
  int length = snprintf(payload, sizeof(payload),
                        "{\"type\":\"rfid_event\",\"card_id\":\"%s\",\"gate\":\"%s\",\"temperature_c\":%.1f,\"seq\":%lu}",
                        uid, gate, currentTemperatureC, (unsigned long)seq);
  if (length < 0 || length >= sizeof(payload)) {
    Serial.println(F("-> [UDP RFID] Payload formatting failed"));
    return;
  }

  // Reuse a free slot, or the one sent longest ago if four scans are still unanswered.
  PendingScan *slot = &pending[0];
  for (PendingScan &p : pending) {
    if (p.seq == 0) { slot = &p; break; }
    if (p.sentAt < slot->sentAt) slot = &p;
  }
  sendJson(payload, slot, seq);
  Serial.print(F("-> [UDP RFID] Đã gửi: "));
  Serial.println(payload);
}

// Hàm cấu hình ArduinoOTA
void setupOTA() {
  ArduinoOTA.setHostname("ESP8266-RC522-DHT11");

  ArduinoOTA.onStart([]() {
    for (uint8_t r = 0; r < NR_OF_READERS; r++) {
      mfrc522[r].PCD_AntennaOff();
    }
    Serial.println(F("\n[OTA] Bắt đầu cập nhật Firmware..."));
  });

  ArduinoOTA.onEnd([]() {
    Serial.println(F("\n[OTA] Cập nhật thành công! Đang khởi động lại..."));
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Tiến trình: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("\n[OTA] Lỗi [%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println(F("Sai mật khẩu OTA"));
    else if (error == OTA_BEGIN_ERROR) Serial.println(F("Lỗi khởi tạo OTA"));
    else if (error == OTA_CONNECT_ERROR) Serial.println(F("Lỗi kết nối"));
    else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Lỗi nhận dữ liệu"));
    else if (error == OTA_END_ERROR) Serial.println(F("Lỗi kết thúc"));
  });

  ArduinoOTA.begin();
  Serial.println(F("ArduinoOTA đã sẵn sàng (Hostname: ESP8266-RC522-DHT11)"));
}

// Chuyển UID sang chuỗi HEX viết hoa, mỗi byte đúng 2 ký tự (out cần UID_HEX_MAX byte)
void formatCardUID(const byte *buffer, byte bufferSize, char *out) {
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  if (bufferSize > (UID_HEX_MAX - 1) / 2) {
    bufferSize = (UID_HEX_MAX - 1) / 2;
  }
  for (byte i = 0; i < bufferSize; i++) {
    out[2 * i] = HEX_DIGITS[buffer[i] >> 4];
    out[2 * i + 1] = HEX_DIGITS[buffer[i] & 0x0F];
  }
  out[2 * bufferSize] = '\0';
}
