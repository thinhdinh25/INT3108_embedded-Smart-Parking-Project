# INT3108 Smart Parking Project

ESP-IDF firmware for an ESP32-C3 smart parking controller. It creates a Wi-Fi
access point, hosts a browser dashboard, listens for UDP sensor updates, and
shows the latest parking state on a 1.54-inch ST7789 TFT.

## Network

- Wi-Fi SSID: `ESP32-Parking`
- Wi-Fi password: `12345678`
- Device address: `192.168.4.1`
- Dashboard: `http://192.168.4.1`
- Sensor update endpoint: UDP `192.168.4.255:3333` (JSON datagrams)
- Dashboard data endpoint: `GET /api/parking`
- Barrier controls: separate IN (GPIO0) and OUT (GPIO1) Open/Close controls, plus emergency commands for both servos

### SG90 barrier servos

The server firmware drives two SG90 servos independently at 50 Hz. The entry
gate on GPIO0 uses 90° for open and 0° for closed. The exit gate on GPIO1 is
mounted in the opposite orientation and uses 0° for open and 90° for closed.
The ESP32 saves each commanded state separately in NVS and restores them at
startup. A first RFID scan opens the entry barrier; a matching scan to exit
opens the exit barrier. RFID-triggered openings close automatically after three
seconds. When a parking update reports fire, both barriers open and remain open
until a later update reports fire clear; then both barriers close. This fire
mode takes priority over RFID auto-close and manual close commands.
Adjust the pulse limits in
`main/barrier/servo.c` if your servo or barrier linkage needs calibration.
With the default TFT wiring above, connect the signal wires to ESP32-C3
SuperMini GPIO0 and GPIO1, one servo per pin. Keep the existing 5 V connections
on the servos; connect both servo grounds to GND and make sure that the 5 V
supply ground is also connected to ESP32 GND. Do not connect either servo's 5 V
lead to an ESP32 GPIO. Verify both GPIO labels on your board before wiring.

Connect both senders to the ESP32 access point and send JSON datagrams to
`192.168.4.255:3333`. Slot value `1` means occupied; `0` means free. The parking
sensor client sends water level in millimeters and `fire` reports whether the
fire sensor is active:

```json
{"type":"parking_update","slots":[1,0,0,1,0],"water_mm":0.2,"fire":false}
```

Temperature is included in the RFID scan message. The first scan of a card ID
records its entry; scanning the same card again records its exit and displays
the latest receipt on the TFT:

```json
{"type":"rfid_event","card_id":"A1B2C3D4","temperature_c":28.0}
```

From the repository root, run:

```sh
python examples/udp_sender.py
python examples/udp_rfid_sender.py
```

Run `udp_rfid_sender.py` once to simulate entry and run it again with the same
card ID to simulate exit. The fee is 5,000 VND per started hour (minimum one
hour). The TFT shows the most recent completed parking duration in hours and
minutes, the card ID, and a larger fee in the footer for 60 seconds after exit;
the receipt then clears. The fire alarm screen has a moving flame icon. Active
sessions and the receipt are held in RAM (up to five active card IDs), so a
server reboot clears them.
`Client/Client.ino`,
`../examples/udp_sender.py` (Python), and `../examples/udp_sender_espidf.c`
(ESP-IDF) send parking updates; `../examples/udp_rfid_sender.py` sends RFID
scans.

## Project layout

```text
Server/
├── CMakeLists.txt
└── main/
    ├── main.c                  Application startup
    ├── Kconfig.projbuild       TFT pin and panel offset settings
    ├── display/                ST7789 parking and fire alarm screens
    ├── parking/                Shared parking and sensor state
    └── network/
        ├── http/               Browser dashboard and JSON API
        └── udp/                UDP update receiver and parser
```

## ST7789 display

The firmware uses ESP-IDF's built-in `esp_lcd` ST7789 panel driver and SPI
master driver. It does not need an Arduino display library or an extra managed
component. The UI is configured for a 240×240 panel and shows free slots,
individual slot states, temperature, and water level. An active `fire` field
replaces the normal TFT view with a fire alarm screen and shows a prominent
dashboard alert. The server also drives an active-low buzzer on GPIO9 while
the latest accepted UDP update reports `fire: true`; it turns the buzzer off
when an update reports `fire: false`. The buzzer output is initialized off.

Default ESP32-C3 SuperMini wiring:

| Display pin | ESP32-C3 GPIO |
| --- | ---: |
| SCL / SCK | 4 |
| SDA / MOSI | 6 |
| DC / A0 | 7 |
| RES / RST | 3 |
| CS | 10 |
| BL / LED | 5 |
| VCC | 3V3 |
| GND | GND |

MISO is not used. If your wiring differs, open the Command Palette in VS Code
and run **ESP-IDF: SDK Configuration Editor**. Edit the **Parking
TFT display** values to match the wiring, and adjust the panel X/Y offsets if
the image is shifted. `Client/Client.ino` currently uses sample temperature
and water values until you set its sensor-specific reading and calibration
code. The fire sensor is read by the client sender and the server receives that
state in the UDP `fire` field. GPIO9 is also an ESP32-C3 strapping pin; use a
buzzer module input that does not force GPIO9 low during reset, and verify the
module is compatible with 3.3 V logic. This firmware assumes LOW activates
the buzzer.

## Build and flash in VS Code

The repository root contains both the client and server; the ESP-IDF project
root is `Server/`. In VS Code, choose `Server/` with **ESP-IDF: Pick a Workspace
Folder**, then use the ESP-IDF Build, Flash, and Monitor commands. This project
uses ESP-IDF 6.1 and targets the ESP32-C3 SuperMini.

From an ESP-IDF configured terminal at the repository root, run:

```sh
idf.py -C Server set-target esp32c3
idf.py -C Server build
idf.py -C Server flash monitor
```


| Display pin | ESP32-C3 GPIO |
|---|---:|
| SCL / SCK | 4 |
| SDA / MOSI | 6 |
| DC | 7 |
| RES / RST | 3 |
| CS | 10 |
| BL / LED | 5 |
| VCC / GND | 3V3 / GND |
