# INT3108 Smart Parking Project

ESP-IDF firmware for an ESP32-C3 smart parking controller. It creates a Wi-Fi
access point, hosts a browser dashboard, listens for UDP sensor updates, and
shows the latest parking state on a 1.54-inch ST7789 TFT.

## Network

- Wi-Fi SSID: `ESP32-Parking`
- Wi-Fi password: `12345678`
- Device address: `192.168.4.1`
- Dashboard: `http://192.168.4.1`
- UDP listener: `192.168.4.1:3333`
- Dashboard data endpoint: `GET /api/parking`

Connect the sender to the ESP32 access point and broadcast a JSON datagram to
`192.168.4.255:3333`. Slot value `1` means occupied; `0` means free.
Temperature is sent in Celsius and water depth in millimeters:

```json
{"type":"parking_update","slots":[1,0,0,1,0],"temperature_c":28.0,"water_mm":0.2}
```

The example represents 3 free spaces, a temperature of 28.0 °C, and 0.2 mm of
water. Runnable sender examples are in `../examples/udp_sender.py` (Python)
and `../examples/udp_sender_espidf.c` (ESP-IDF). The latest accepted state is
kept in RAM and resets when the ESP32 reboots.

## Project layout

```text
Server/
├── CMakeLists.txt
└── main/
    ├── main.c                  Application startup
    ├── Kconfig.projbuild       TFT pin and panel offset settings
    ├── display/                ST7789 driver setup and screen rendering
    ├── parking/                Shared parking and sensor state
    └── network/
        ├── http/               Browser dashboard and JSON API
        └── udp/                UDP update receiver and parser
```

## ST7789 display

The firmware uses ESP-IDF's built-in `esp_lcd` ST7789 panel driver and SPI
master driver. It does not need an Arduino display library or an extra managed
component. The UI is configured for a 240×240 panel and shows free slots,
individual slot states, temperature, and water depth.

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
the image is shifted. Sample temperature and water readings are sent by both
example clients; replace them with real sensor readings in your sender.

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
