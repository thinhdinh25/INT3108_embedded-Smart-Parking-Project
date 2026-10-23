# INT3108 Embedded Smart Parking Project

This project is a two-board smart parking system. The **Server** is an ESP32-C3
SuperMini that hosts a Wi-Fi access point, receives sensor readings, runs a web
dashboard, displays parking and fire status on an ST7789 TFT, and controls two
barrier servos. The **Client** is a separate ESP8266 board that reads the
parking, flame, and water sensors and sends their readings to the server over
UDP.

## Features

- Five parking-space states from digital IR sensors.
- Flame sensor state sent from the client to the server. The server shows a
  fire alarm on the display and dashboard and sounds its GPIO9 buzzer while
  the latest accepted update reports fire.
- Water level estimate from the client's analog sensor. Temperature is
  currently a sample value in the client firmware.
- Browser dashboard and JSON API hosted by the server.
- Two independently controlled SG90 barriers, with commanded open/closed
  state saved in server NVS.

## Boards, languages, and software

| Part | Hardware / software |
| --- | --- |
| Server | ESP32-C3 SuperMini; ESP-IDF 6.1; C; FreeRTOS (provided by ESP-IDF) |
| Client | ESP8266 board; Arduino core for ESP8266; C++ in `Client/Client.ino` |
| Display | 1.54-inch 240×240 ST7789 SPI TFT |
| Barriers | Two SG90 servos driven by 50 Hz LEDC PWM |
| Sensors | Five digital IR sensors, one digital flame sensor, one analog water sensor |
| Dashboard | HTML, CSS, and JavaScript served from the ESP32-C3 |
| Server storage | ESP-IDF NVS for barrier state |

The server uses the ESP-IDF `esp_lcd` and SPI drivers for the display, LEDC for
servo PWM, GPIO for the active-low buzzer, ESP Wi-Fi SoftAP for networking,
FreeRTOS tasks for services, and lwIP sockets for UDP.

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
| Buzzer signal | GPIO9 |

The buzzer is configured as **active-low**: GPIO9 LOW sounds it when fire is
reported; GPIO9 HIGH leaves it silent. GPIO9 is an ESP32-C3 boot-strapping pin.
Use a buzzer module whose input does not pull GPIO9 low during reset, and whose
input is compatible with 3.3 V logic. The buzzer output starts HIGH (off).

Power the servos from a suitable 5 V supply, not from the ESP32-C3 3V3 pin.
Connect the external supply ground to ESP32-C3 GND. Do not connect a servo's 5
V lead to a GPIO. Check the labels on the exact board and modules before wiring.

### ESP8266 client

The current `Client/Client.ino` uses these numeric ESP8266 GPIO values:

| Sensor | Client pin |
| --- | --- |
| Parking IR sensors 1–5 | GPIO12, GPIO13, GPIO14, GPIO4, GPIO16 |
| Flame sensor digital output | GPIO5 |
| Water sensor analog output | A0 |

IR and flame modules are read as active-low in the current firmware. The water
level conversion uses the calibration constants in `Client/Client.ino`; adjust
them for the actual sensor. The temperature is currently set to a sample value
of 28 °C. Use the voltage limits for your particular ESP8266 board when wiring
the analog water sensor.

## Network and data flow

The ESP32-C3 creates the access point and the ESP8266 client joins it:

- SSID: `ESP32-Parking`
- Password: `12345678`
- Server address: `192.168.4.1`
- Dashboard: [http://192.168.4.1](http://192.168.4.1)
- UDP sensor receiver: port `3333` (client sends to `192.168.4.255`)
- Dashboard JSON endpoint: `GET /api/parking`

The client sends an update once per second. Example:

```json
{"type":"parking_update","slots":[1,0,0,1,0],"temperature_c":28.0,"water_mm":0.2,"fire":false}
```

Slot `1` means occupied and `0` means free. The `fire` field must be a JSON
boolean. When it is `true`, the server updates its display and dashboard and
drives the buzzer on GPIO9. When it is `false`, the server silences the buzzer.
The server applies the buzzer output after accepting a valid update.

The default access point password is intended for a local project setup. Change
the credentials in `Server/main/network/wifi_ap.c` and `Client/Client.ino`
together before using the system on a shared or public network.

## Setup and run

### 1. Build and flash the ESP32-C3 server

Install ESP-IDF 6.1 and its ESP32-C3 toolchain, then open an ESP-IDF configured
terminal in the repository. The ESP-IDF project root is `Server/`:

```sh
idf.py -C Server set-target esp32c3
idf.py -C Server build
idf.py -C Server -p PORT flash monitor
```

Replace `PORT` with the serial port for the ESP32-C3. In VS Code, select
`Server/` with **ESP-IDF: Pick a Workspace Folder**, then use the ESP-IDF
Build, Flash, and Monitor commands. Default TFT GPIOs and panel offsets are
configured in `Server/sdkconfig` and `Server/main/Kconfig.projbuild`.

### 2. Build and flash the ESP8266 client

Install Arduino IDE (or Arduino CLI) and the ESP8266 Arduino core. Open
`Client/Client.ino`, select the matching ESP8266 board and serial port, and
upload it. The Wi-Fi credentials and UDP destination are defined near the top
of the sketch. The ESP8266 joins the server access point and starts sending
sensor updates.

### 3. Start and use the system

Power the server and connect a phone or computer to `ESP32-Parking`. Open
`http://192.168.4.1` to view the dashboard. Then power the client; the server
display and dashboard should show incoming sensor state. Test fire behavior by
activating the client flame sensor: the server should show the fire alarm and
the GPIO9 buzzer should sound. Clear the sensor and wait for its debounce and
next UDP update; the buzzer should stop.

## Repository layout

```text
Server/                 ESP32-C3 ESP-IDF server firmware and server README
  main/buzzer/          GPIO9 active-low buzzer output
  main/barrier/         Servo PWM and NVS state
  main/display/         ST7789 UI and fire alarm screen
  main/network/         Wi-Fi AP, HTTP dashboard/API, UDP receiver
  main/parking/         Parking state and update parser
Client/Client.ino       ESP8266 Arduino sensor sender
examples/               Python and ESP-IDF UDP example senders
docs/                   Additional wiring notes
```

See [`Server/README.md`](Server/README.md) for server-specific wiring and
firmware details.
