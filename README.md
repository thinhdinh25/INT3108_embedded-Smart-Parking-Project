# INT3108 Embedded Smart Parking Project

A three-board smart parking system built for the INT3108 Embedded Programming
course (VNU-UET).

- **Server** — ESP32-C3 SuperMini running ESP-IDF/FreeRTOS. Joins the shared
  Wi-Fi, receives UDP messages, takes VietQR payments at the exit (checked with
  SePay), serves a web dashboard at
  `http://parking.local`, draws the parking/fire screens on an ST7789 TFT, and
  drives two SG90 barriers and a buzzer.
- **Sensor Client** (`SensorClient/SensorClient.ino`) — ESP8266 that reads five
  IR parking sensors, a flame sensor and an analog water sensor, and broadcasts
  a `parking_update` once per second.
- **RFID Client** (`RFIDClient/RFIDClient.ino`) — ESP8266 that reads two MFRC522 RFID readers (entry and exit) and a DHT11. Each
  card scan sends an `rfid_event` with the card UID, gate and latest
  temperature.

The full technical report (Vietnamese, LaTeX source and PDF) is in
[`report/`](report/).

## Features

- Five parking-space states from debounced digital IR sensors.
- RFID entry/exit: a card scanned on the entry reader opens the entry barrier;
  the same card scanned on the exit reader opens the exit barrier.
  RFID-opened barriers close automatically after 3 seconds (one-shot timer).
- Rejected scans (wrong gate, lot full) show a red notice on the TFT for 5 s,
  e.g. `WRONG GATE – ALREADY INSIDE - USE EXIT GATE`.
- Parking fee of 5,000 VND per started hour (minimum one hour). The TFT shows
  card ID, parking time and fee for 60 seconds after exit.
- Fire alarm: animated "FIRE DETECTED" screen, dashboard banner, an
  active-low buzzer on GPIO9, and both barriers held open while the latest
  update reports fire.
- Water level (mm) from the analog sensor and temperature from the DHT11.
- Browser dashboard and JSON API hosted by the server, with independent and
  emergency (all) barrier controls.
- Dashboard centred on a large parking-lot view (cars drawn in occupied bays),
  plus an in/out log: who is parked now with the fee so far, and the last 10
  card scans with exit duration and fee.
- Sessions, the log and the parking clock are saved in NVS after every scan
  and every 5 minutes, so a power cut loses nothing. The operator's browser
  sets the real time (the lot has no internet), and downtime is added back.
- Temperature is sent every 5 s on its own, not only with card scans.
- Sensor watchdog: if the Sensor Client is silent for 3 s, the TFT shows
  `SENSOR OFFLINE` with grey bays and the dashboard marks the data as last
  known state. Stale data never blocks entry or raises the lot warning.
- Flood alarm: at 20 mm water (off again at 15 mm) the buzzer sounds, the TFT
  and dashboard warn, and entry scans are refused (`FLOOD ALARM`); cars inside
  can still leave.
- Revenue statistics: today's and the last 7 days' revenue and in/out counts
  (UTC+7 days), saved in NVS with the sessions.
- Cross-check with the IR sensors: when all 5 bays are occupied a new card is
  refused, and the dashboard warns when cards inside and occupied bays differ
  for more than 15 s. More cars than cards for 15 s also pulses the buzzer and
  shows `CAR W/O CARD` on the TFT until the counts match or the sensors go silent.
- Both barriers start closed after every reboot.

## Boards, languages, and software

| Part | Hardware / software |
| --- | --- |
| Server | ESP32-C3 SuperMini; ESP-IDF 6.1; C; FreeRTOS (provided by ESP-IDF) |
| Sensor Client | ESP8266; Arduino core for ESP8266; C++ (`SensorClient/SensorClient.ino`) |
| RFID Client | ESP8266; Arduino; `MFRC522` and `DHT sensor library` (`RFIDClient/RFIDClient.ino`) |
| Display | 1.54-inch 240×240 ST7789 SPI TFT |
| Barriers | Two SG90 servos driven by 50 Hz LEDC PWM |
| Sensors | 5 digital IR, 1 digital flame, 1 analog water, 2 × RC522 RFID, 1 × DHT11 |
| Dashboard | HTML, CSS, and JavaScript embedded in the ESP32-C3 firmware |

The server uses `esp_lcd` + SPI (DMA, 40 MHz) for the display, LEDC for servo
PWM, `esp_timer` for barrier auto-close, GPIO for the buzzer, Wi-Fi station
mode, mDNS (`espressif/mdns`), `esp_http_server`, lwIP sockets for UDP, and
FreeRTOS tasks/mutexes/critical sections for concurrency. Both ESP8266 clients
support ArduinoOTA updates.

## Hardware connections

### ESP32-C3 server

| Device signal | ESP32-C3 GPIO / power |
| --- | --- |
| TFT SCL / SCK | GPIO4 |
| TFT SDA / MOSI | GPIO6 |
| TFT DC / A0 | GPIO7 |
| TFT RES / RST | GPIO3 |
| TFT CS | GPIO10 |
| TFT BL / LED | 3V3 (always on; GPIO5 now has the entry gate sensor) |
| TFT VCC / GND | 3V3 / GND |
| IN barrier servo signal | GPIO0 |
| OUT barrier servo signal | GPIO1 |
| Buzzer signal (active-low) | GPIO9 |
| Entry gate IR sensor (active-low) | GPIO5 |
| Exit gate IR sensor (active-low) | GPIO2 |

GPIO9 is an ESP32-C3 boot-strapping pin; use a buzzer module that does not pull
it low during reset. The buzzer output starts HIGH (silent).

Power the servos from a 5 V supply, not from the ESP32-C3 3V3 pin, and connect
the supply ground to ESP32-C3 GND. Display GPIOs and panel offsets can be
changed under **Parking TFT display** in `idf.py menuconfig`
(`Server/main/Kconfig.projbuild`). More detail:
[`docs/esp32-c3-tft-and-sg90-wiring.md`](docs/esp32-c3-tft-and-sg90-wiring.md).

### Sensor Client (ESP8266, `SensorClient/`)

| Sensor | Sensor Client pin |
| --- | --- |
| Parking slots S1–S5 | GPIO16, GPIO4, GPIO14, GPIO13, GPIO12 |
| Flame sensor digital output | GPIO5 |
| Water sensor analog output | A0 |

IR modules are active-low (LOW = car present). For the flame module used in the
project, the firmware sends `fire: true` when the DO pin reads HIGH. Each
digital input is debounced for 50 ms. The water conversion uses
`WATER_DRY_ADC = 20`, `WATER_MAX_ADC = 700` and a 40 mm sensor length; calibrate
these for your sensor.

### RFID Client (ESP8266, `RFIDClient/`)

| Device | RFID Client pin |
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

All three boards join one shared Wi-Fi (your router or phone hotspot, with
Internet for SePay); no board runs a hotspot:

- SSID `Parking`, password `12345678` by default (see Secrets below)
- It must be a 2.4 GHz network: the ESP32-C3 and ESP8266 have no 5 GHz radio. On a
  router with separate names, pick the 2.4 GHz one; a phone or laptop on the 5 GHz
  name still reaches the boards if the router bridges the two.
- Every board gets its address by DHCP. The router must not isolate its
  clients (no "AP isolation"), or the broadcasts never reach the server.
- Dashboard: [http://parking.local](http://parking.local) (mDNS, re-announced
  whenever the server gets a new IP). If a device
  cannot resolve `.local` names, use the IP the server prints on its serial log
  (`Web interface: http://…`).
- UDP receiver on port `3333`; clients send to broadcast `255.255.255.255:3333`,
  so they need neither the subnet nor the server's IP

Sensor update (Sensor Client, every 1 s). Slot `1` means occupied, `0` free:

```json
{"type":"parking_update","slots":[1,0,0,1,0],"water_mm":0.2,"fire":false}
```

RFID scan (RFID Client, per card read; `gate` is `in` or `out`):

```json
{"type":"rfid_event","card_id":"A1B2C3D4","gate":"in","temperature_c":28.0,"seq":305441741}
```

Temperature (RFID Client, every 5 s):

```json
{"type":"climate","temperature_c":28.0}
```

Datagrams are plain JSON (not signed; the network is a private WPA2 hotspot).
The server ACKs every `rfid_event` with `{"type":"rfid_ack","seq":N}`; the RFID
Client resends an unacknowledged scan every 300 ms (up to 6 times). The server
remembers the last 16 `seq` values and acts on each scan once. `seq` starts
from the ESP8266 hardware RNG at boot and counts up.

The server parsers are strict: unknown keys, duplicate keys, wrong types or
trailing data reject the datagram. `fire` must be a JSON boolean and may be
omitted by older clients.

HTTP API:

| Method | Path | Result |
| --- | --- | --- |
| GET | `/` , `/style.css` | Dashboard |
| GET | `/api/parking` | Slots, free count, temperature, water, fire, flood, sensor `online` |
| GET | `/api/barrier` | `{"in_open":…,"out_open":…}` |
| GET | `/api/rfid` | Price per hour, today / 7-day revenue and visits, cars inside (entry time, fee so far), last 10 RFID events (in/out/rejected, duration, fee) |
| POST | `/api/barrier/{in,out,all}/{open,close}` | Move barriers manually |
| POST | `/api/time` | Body: Unix seconds. Sets the parking clock |
| POST | `/api/payment/confirm` | Body: card ID. Operator confirms a transfer or cash; opens the exit |
| POST | `/api/rfid/remove` | Body: card ID. Deletes a car from the inside list: no fee, no barrier |

Every POST needs the admin PIN in the `X-Parking-Key` header (the dashboard
asks for it once and remembers it); a wrong or missing PIN returns 401.

### Secrets

The repository only holds demo values. Real ones stay out of Git:

- Server: `idf.py menuconfig` → *Parking network and security*: shared Wi-Fi
  SSID and password, admin PIN (default `2468`); *Parking QR payment (SePay)*:
  bank BIN (BIDV `970418`), receiving account, holder name, SePay API token.
  They are stored in the git-ignored `Server/sdkconfig`. The SePay token has
  full access to the SePay account, so never commit it.
  For a personal BIDV account, SePay only sees transfers sent to a virtual
  account (VA): create one on my.sepay.vn and enter the VA number as the
  receiving account, not the main account number.
- Clients: copy `secrets.example.h` to `secrets.h` next to each sketch and
  edit it (`secrets.h` is git-ignored).

## QR payment at the exit

With a receiving account set, an exit scan no longer opens the barrier at once:

1. The TFT shows a VietQR code (NAPAS/EMVCo, built on the ESP32, no network
   needed) for the fee, with a note `PK` + 6 characters.
2. The driver scans it with any Vietnamese banking app and pays.
3. With a SePay token, the server polls
   `my.sepay.vn/userapi/transactions/list` every 3 s (only transactions newer
   than the QR) for that note and exact amount, then opens the exit barrier, prints
   the receipt and counts the revenue. MoMo and some wallets replace the note,
   so a transfer of exactly the fee to the receiving account/VA also counts.
4. No transfer after 180 s: the TFT shows `PAY AT BOOTH`; a late transfer still
   counts. The operator can always press *Confirm paid* on the dashboard (PIN),
   which also covers cash and works without Internet or a token.

A second car scanning out while one is paying sees `PLEASE WAIT`. The exit
barrier never opens on a scan alone: with no account set the TFT shows
`PAY AT BOOTH` and only *Confirm paid* (or a manual barrier command) opens it.

## Setup and run

### 1. Server (ESP32-C3)

Install ESP-IDF 6.1 with the ESP32-C3 toolchain. From the repository root (the
root `CMakeLists.txt` points at `Server/main`). The first build downloads the
`espressif/mdns` and `espressif/qrcode` components. Fill in the Wi-Fi and
payment settings with `idf.py menuconfig` first:

```sh
idf.py set-target esp32c3
idf.py build
idf.py -p PORT flash monitor
```

### 2. Clients (ESP8266)

In Arduino IDE install the ESP8266 core, then the `MFRC522` and
`DHT sensor library` libraries for the RFID Client. Open
`SensorClient/SensorClient.ino` and `RFIDClient/RFIDClient.ino`, select the
ESP8266 board and port, and upload. After the
first upload, both sketches can be updated over the network with ArduinoOTA.

### 3. Use

Power the boards in any order; they join the shared Wi-Fi and reconnect on
their own. Join the same Wi-Fi from a phone or laptop and open
`http://parking.local`. Scan a card on the entry reader to enter and on the
exit reader to leave and see the fee.

Without the clients, simulate them from a computer on the same Wi-Fi (they
broadcast by default; add `--host <server IP>` if your network drops broadcasts):

```sh
python examples/udp_sender.py --slots 1,0,0,1,0
python examples/udp_rfid_sender.py A1B2C3D4 --gate in
python examples/udp_rfid_sender.py A1B2C3D4 --gate out
python examples/udp_sender.py --slots 1,0,0,1,0 --fire
```

The RFID simulator uses a random `seq` per run, so it can run next to the real
RFID Client.

### 4. Tests (no hardware needed)

```sh
make -C tests
```

Runs 397 checks on the host: VietQR payload (checked against a published
sample) and SePay response matching, the server JSON parsers, RFID sessions/fees/log/
notices/persistence/clock/flood/offline/revenue stats/retry dedupe/QR payment, the animation frame data, and both ESP8266 sketches
compiled against a fake Arduino layer (`tests/arduino/`), with every client
packet parsed by the real server code.

## Repository layout

```text
Server/main/
  main.c                App startup (NVS, display, buzzer, servo, network)
  barrier/              SG90 LEDC PWM, timer-based auto-close, fire mode
  buzzer/               GPIO9 active-low buzzer
  display/              ST7789 UI, fire screen, LZSS-compressed GIF frames
  network/              Wi-Fi station, mDNS, HTTP dashboard/API, UDP receiver,
                        SePay polling
  parking/              Shared state, JSON parsers, RFID sessions and fees,
                        NVS persistence, VietQR payload
SensorClient/           Sensor Client: ESP8266 IR / flame / water sender
RFIDClient/             RFID Client: ESP8266 dual RC522 + DHT11 sender
  (both)                secrets.example.h (copy to secrets.h)
examples/               Python and ESP-IDF UDP test senders
tests/                  Host test suite (make -C tests)
docs/                   Wiring notes
report/                 Technical report (LaTeX + PDF, Vietnamese);
                        diagrams/*.mmd are the Mermaid sources of the figures,
                        photos/ the physical model
```
