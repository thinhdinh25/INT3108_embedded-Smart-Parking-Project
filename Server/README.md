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
- Barrier controls: dashboard Open/Close buttons command SG90 servos on GPIO0 and GPIO1

### SG90 barrier servos

The server firmware drives two SG90 servos together at 50 Hz. **Open** commands 90° and
**Close** commands 0°. The ESP32 saves the last commanded state in NVS and
restores it at startup; sending Open more than once keeps the same 90° target.
Adjust the pulse limits in
`main/barrier/servo.c` if your servo or barrier linkage needs calibration.
With the default TFT wiring above, connect the signal wires to ESP32-C3
SuperMini GPIO0 and GPIO1, one servo per pin. Keep the existing 5 V connections
on the servos; connect both servo grounds to GND and make sure that the 5 V
supply ground is also connected to ESP32 GND. Do not connect either servo's 5 V
lead to an ESP32 GPIO. Verify both GPIO labels on your board before wiring.

Connect the sender to the ESP32 access point and send JSON datagrams to
`192.168.4.255:3333`. Slot value `1` means occupied; `0` means free.
Temperature is sent in Celsius, water level in millimeters, and `fire` reports
whether the fire sensor is active:

```json
{"type":"parking_update","slots":[1,0,0,1,0],"temperature_c":28.0,"water_mm":0.2,"fire":false}
```

The example represents 3 free spaces, a temperature of 28.0 °C, a 0.2 mm water
level, and no fire detected. `Client/Client.ino`, `../examples/udp_sender.py`
(Python), and `../examples/udp_sender_espidf.c` (ESP-IDF) all send this format
over UDP. The latest accepted state is
kept in RAM and resets when the ESP32 reboots.

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
dashboard alert.

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
code. It reads a fire sensor from client GPIO2, active-low by default; change
`FIRE` or `FIRE_ACTIVE_LEVEL` to match your module.

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
