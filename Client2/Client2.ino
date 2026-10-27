#include <SPI.h>
#include <MFRC522.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <DHT.h>

// --- CẤU HÌNH WIFI & UDP ---
const char* ssid = "ESP32-Parking";
const char* password = "12345678";

WiFiUDP udp;
const IPAddress udpAddress(192, 168, 4, 255);   // Địa chỉ Broadcast
const unsigned int udpPort = 3333;              // Cổng UDP đích

// --- CẤU HÌNH PHẦN CỨNG RC522 ---
#define RST_PIN   16  // GPIO 16 (D0) dùng chung cả 2 module
#define SS_1_PIN  4   // GPIO 4 (D2) cho Đầu đọc 1
#define SS_2_PIN  5   // GPIO 5 (D1) cho Đầu đọc 2
#define NR_OF_READERS 2

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
String lastUID[NR_OF_READERS] = {"", ""};
unsigned long lastReadTime[NR_OF_READERS] = {0, 0};
const unsigned long COOLDOWN_TIME = 1500; // Thời gian chờ (1.5 giây)

// Khai báo hàm nguyên mẫu
String getCardUID(byte *buffer, byte bufferSize);
void sendUDPRfid(String uid);
void handleDHT11();
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

  // 2. Kết nối WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print(F("Đang kết nối WiFi"));
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.println(F("\nĐã kết nối WiFi!"));
  Serial.print(F("Địa chỉ IP của mạch: "));
  Serial.println(WiFi.localIP());

  // 3. Khởi tạo cổng UDP & OTA
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

  // 2. Refresh the temperature included in RFID requests.
  handleDHT11();

  // 3. Quét lần lượt 2 đầu đọc RFID
  for (uint8_t reader = 0; reader < NR_OF_READERS; reader++) {
    
    // Bật sóng RF cho đầu đọc hiện tại
    mfrc522[reader].PCD_AntennaOn();
    delay(4); // Chờ 4ms cho từ trường nạp điện thẻ

    // Kiểm tra có thẻ đặt vào không
    if (mfrc522[reader].PICC_IsNewCardPresent() && mfrc522[reader].PICC_ReadCardSerial()) {
      
      String currentUID = getCardUID(mfrc522[reader].uid.uidByte, mfrc522[reader].uid.size);
      unsigned long now = millis();

      // KIỂM TRA CHỐNG LẶP:
      if (currentUID != lastUID[reader] || (now - lastReadTime[reader] >= COOLDOWN_TIME)) {
        
        lastUID[reader] = currentUID;
        lastReadTime[reader] = now;

        Serial.print(F("=> [ĐẦU ĐỌC "));
        Serial.print(reader + 1);
        Serial.print(F("] Nhận thẻ UID: "));
        Serial.println(currentUID);

        // Gửi gói tin UDP RFID tới 192.168.4.255:3333
        sendUDPRfid(currentUID);

        // --- KHU VỰC XỬ LÝ LỆNH CHO TỪNG ĐẦU ĐỌC ---
        if (reader == 0) {
          // Xử lý cổng 1
        } else if (reader == 1) {
          // Xử lý cổng 2
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

    // Temperature is included in the server's rfid_event request.
    currentTemperatureC = t;
  }
}

// Send an RFID scan using the server's current request format.
void sendUDPRfid(String uid) {
  if (WiFi.status() == WL_CONNECTED) {
    // Server format: {"type":"rfid_event","card_id":"A1B2C3D4","temperature_c":28.5}
    char payload[128];
    int length = snprintf(payload, sizeof(payload),
                          "{\"type\":\"rfid_event\",\"card_id\":\"%s\",\"temperature_c\":%.1f}",
                          uid.c_str(), currentTemperatureC);
    if (length < 0 || length >= sizeof(payload)) {
      Serial.println(F("-> [UDP RFID] Payload formatting failed"));
      return;
    }
    
    udp.beginPacket(udpAddress, udpPort);
    udp.write(reinterpret_cast<const uint8_t*>(payload), length);
    udp.endPacket();

    Serial.print(F("-> [UDP RFID] Đã gửi: "));
    Serial.println(payload);
  } else {
    Serial.println(F("-> [Lỗi UDP] Mất kết nối WiFi!"));
  }
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

// Chuyển UID sang chuỗi HEX viết hoa
String getCardUID(byte *buffer, byte bufferSize) {
  String uid = "";
  for (byte i = 0; i < bufferSize; i++) {
    if (buffer[i] < 0x10) {
      uid += "0";
    }
    uid += String(buffer[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}
