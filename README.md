# INT3108 Embedded Smart Parking Project

A three-board smart parking system built for the INT3108 Embedded Programming
course (VNU-UET).

- **Server** — ESP32-C3 SuperMini running ESP-IDF/FreeRTOS. Hosts the Wi-Fi
  access point, receives UDP messages, serves a web dashboard, draws the
  parking/fire screens on an ST7789 TFT, drives two SG90 barriers and a buzzer.
- **Client** (`Client/Client.ino`) — ESP8266 that reads five IR parking
  sensors, a flame sensor and an analog water sensor, and broadcasts a
  `parking_update` once per second.
- **Client2** (`Client2/Client2.ino`) — ESP8266 with two MFRC522 RFID readers
  and a DHT11. Each card scan sends an `rfid_event` with the card UID and the
  latest temperature.

The full technical report (Vietnamese, LaTeX source and PDF) is in
[`report/`](report/).

## Features

- Five parking-space states from debounced digital IR sensors.
- RFID entry/exit: the first scan of a card opens the entry barrier, the next
  scan of the same card opens the exit barrier. RFID-opened barriers close
  automatically after 5 seconds.
- Parking fee of 5,000 VND per started hour (minimum one hour). The TFT shows
  card ID, parking time and fee for 60 seconds after exit.
- Fire alarm: animated "FIRE DETECTED" screen, dashboard banner and an
  active-low buzzer on GPIO9 while the latest update reports fire.
- Water level (mm) from the analog sensor and temperature from the DHT11.
- Browser dashboard and JSON API hosted by the server, with independent and
  emergency (all) barrier controls.
- Barrier open/closed state saved in NVS and restored after reboot.

## Boards, languages, and software

| Part | Hardware / software |
| --- | --- |
| Server | ESP32-C3 SuperMini; ESP-IDF 6.1; C; FreeRTOS (provided by ESP-IDF) |
| Client | ESP8266; Arduino core for ESP8266; C++ (`Client/Client.ino`) |
| Client2 | ESP8266; Arduino; `MFRC522` and `DHT sensor library` (`Client2/Client2.ino`) |
| Display | 1.54-inch 240×240 ST7789 SPI TFT |
| Barriers | Two SG90 servos driven by 50 Hz LEDC PWM |
| Sensors | 5 digital IR, 1 digital flame, 1 analog water, 2 × RC522 RFID, 1 × DHT11 |
| Dashboard | HTML, CSS, and JavaScript embedded in the ESP32-C3 firmware |
| Server storage | ESP-IDF NVS for barrier state |

The server uses `esp_lcd` + SPI (DMA, 40 MHz) for the display, LEDC for servo
PWM, GPIO for the buzzer, Wi-Fi SoftAP, `esp_http_server`, lwIP sockets for
UDP, and FreeRTOS tasks/mutexes/critical sections for concurrency. Both
ESP8266 clients support ArduinoOTA updates.

## Hardware connections

### ESP32-C3 server

| Device signal | ESP32-C3 GPIO / power |
| --- | --- |
| TFT SCL / SCK | GPIO4 |
| TFT SDA / MOSI | GPIO6 |
| TFT DC / A0 | GPIO7 |
| TFT RES / RST | GPIO3 |
| TFT CS | GPIO10 |
| TFT BL / LED | GPIO5 |
| TFT VCC / GND | 3V3 / GND |
| IN barrier servo signal | GPIO0 |
| OUT barrier servo signal | GPIO1 |
| Buzzer signal (active-low) | GPIO9 |

GPIO9 is an ESP32-C3 boot-strapping pin; use a buzzer module that does not pull
it low during reset. The buzzer output starts HIGH (silent).

Power the servos from a 5 V supply, not from the ESP32-C3 3V3 pin, and connect
the supply ground to ESP32-C3 GND. Display GPIOs and panel offsets can be
changed under **Parking TFT display** in `idf.py menuconfig`
(`Server/main/Kconfig.projbuild`). More detail:
[`docs/esp32-c3-tft-and-sg90-wiring.md`](docs/esp32-c3-tft-and-sg90-wiring.md).

### ESP8266 sensor client (`Client/`)

| Sensor | Client pin |
| --- | --- |
| Parking IR sensors 1–5 | GPIO12, GPIO13, GPIO14, GPIO4, GPIO16 |
| Flame sensor digital output | GPIO5 |
| Water sensor analog output | A0 |

IR modules are active-low (LOW = car present). For the flame module used in the
project, the firmware sends `fire: true` when the DO pin reads HIGH. Each
digital input is debounced for 250 ms. The water conversion uses
`WATER_DRY_ADC = 20`, `WATER_MAX_ADC = 700` and a 40 mm sensor length; calibrate
these for your sensor.

### ESP8266 RFID client (`Client2/`)

| Device | Client2 pin |
| --- | --- |
| RC522 #1 (entry) SDA/SS | GPIO4 (D2) |
| RC522 #2 (exit) SDA/SS | GPIO5 (D1) |
| RC522 RST (shared) | GPIO16 (D0) |
| RC522 SCK / MOSI / MISO | GPIO14 / GPIO13 / GPIO12 (hardware SPI) |
| DHT11 data | GPIO2 (D4) |

The two readers share the SPI bus and take turns: only one antenna is on at a
time. The same card is ignored on the same reader for 1.5 s. The DHT11 is read
every 5 s.

## Network and protocol

The ESP32-C3 creates the access point and both ESP8266 clients join it:

- SSID `ESP32-Parking`, password `12345678`, WPA2-PSK, channel 1, max 4 stations
- Server address `192.168.4.1`, dashboard [http://192.168.4.1](http://192.168.4.1)
- UDP receiver on port `3333`; clients send to broadcast `192.168.4.255:3333`

Sensor update (Client, every 1 s). Slot `1` means occupied, `0` free:

```json
{"type":"parking_update","slots":[1,0,0,1,0],"water_mm":0.2,"fire":false}
```

RFID scan (Client2, per card read):

```json
{"type":"rfid_event","card_id":"A1B2C3D4","temperature_c":28.0}
```

The server parsers are strict: unknown keys, duplicate keys, wrong types or
trailing data reject the datagram. `fire` must be a JSON boolean and may be
omitted by older clients.

HTTP API:

| Method | Path | Result |
| --- | --- | --- |
| GET | `/` , `/style.css` | Dashboard |
| GET | `/api/parking` | Slots, free count, temperature, water, fire |
| GET | `/api/barrier` | `{"in_open":…,"out_open":…}` |
| POST | `/api/barrier/{in,out,all}/{open,close}` | Move barriers manually |

The default password is for a local demo. Change it in
`Server/main/network/wifi_ap.c`, `Client/Client.ino` and `Client2/Client2.ino`
together before using a shared network.

## Setup and run

### 1. Server (ESP32-C3)

Install ESP-IDF 6.1 with the ESP32-C3 toolchain. From the repository root (the
root `CMakeLists.txt` points at `Server/main`):

```sh
idf.py set-target esp32c3
idf.py build
idf.py -p PORT flash monitor
```

### 2. Clients (ESP8266)

In Arduino IDE install the ESP8266 core, then the `MFRC522` and
`DHT sensor library` libraries for Client2. Open `Client/Client.ino` and
`Client2/Client2.ino`, select the ESP8266 board and port, and upload. After the
first upload, both sketches can be updated over the network with ArduinoOTA.

### 3. Use

Power the server, join `ESP32-Parking` from a phone or laptop and open
`http://192.168.4.1`. Power the clients; the TFT and dashboard show live data.
Scan a card to enter, scan it again to exit and see the fee.

Without hardware, simulate the clients from a computer on the AP:

```sh
python examples/udp_sender.py
python examples/udp_rfid_sender.py A1B2C3D4
```

Run the RFID sender twice with the same ID to simulate entry and exit.

## Repository layout

```text
Server/main/
  main.c                App startup (NVS, display, buzzer, servo, network)
  barrier/              SG90 LEDC PWM, NVS state, auto-close task
  buzzer/               GPIO9 active-low buzzer
  display/              ST7789 UI, fire screen, LZSS-compressed GIF frames
  network/              Wi-Fi AP, HTTP dashboard/API, UDP receiver
  parking/              Shared state, JSON parsers, RFID sessions and fees
Client/Client.ino       ESP8266 IR / flame / water sender
Client2/Client2.ino     ESP8266 dual RC522 + DHT11 sender
examples/               Python and ESP-IDF UDP test senders
docs/                   Wiring notes
report/                 Technical report (LaTeX + PDF, Vietnamese)
```

Active RFID sessions and the last receipt are kept in RAM (up to five cars), so
a server reboot clears them.
