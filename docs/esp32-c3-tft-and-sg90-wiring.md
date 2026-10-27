# ESP32-C3 SuperMini, ST7789, and SG90 barrier servos

This note documents the display wiring and the SG90 barrier servo controlled by the server dashboard.

## ST7789 display

The firmware defaults in `Server/sdkconfig` use these GPIOs:

| ST7789 pin | ESP32-C3 GPIO |
| --- | ---: |
| SCL / SCK | 4 |
| SDA / MOSI | 6 |
| DC / A0 | 7 |
| RES / RST | 3 |
| CS | 10 |
| BL / LED | 5 |
| VCC | 3V3 |
| GND | GND |

The display uses SPI without MISO. GPIO21 is not part of this display pin map. If it is disabled or unavailable on the particular board, these connections do not depend on it.

## Two SG90 barrier servo connections

The server firmware controls both barrier servos together. The default display pin map does not use GPIO0 or GPIO1:

| Servo | Signal (usually orange/yellow) | +5 V (usually red) | Ground (usually brown/black) |
| --- | --- | --- | --- |
| SG90 #1 | ESP32-C3 GPIO0 | Existing 5 V supply | ESP32-C3 GND and 5 V supply ground |
| SG90 #2 | ESP32-C3 GPIO1 | Existing 5 V supply | ESP32-C3 GND and 5 V supply ground |

Check the wire colors against the specific servo's documentation. Do not connect its +5 V lead to an ESP32 GPIO.

### Power and signal notes

- Keep both servos on the existing 5 V supply, sized for their combined stall current; do not power them from the ESP32-C3 3V3 output.
- A shared ground between the 5 V supply and ESP32-C3 is required for the signal to work.
- Verify the GPIO0 and GPIO1 labels on the exact SuperMini board before connecting signal wires.
- The firmware sends 50 Hz PWM. Dashboard **Open** targets 90° and **Close** targets 0°; the servo's pulse range may need calibration for the mechanism.

## Pin availability summary

The documented TFT wiring occupies GPIO3, GPIO4, GPIO5, GPIO6, GPIO7, and GPIO10. The two servo signals use GPIO0 and GPIO1. GPIO21 being unavailable does not affect this wiring.

## Firmware configuration

The firmware initializes servo PWM on GPIO0 and GPIO1 during startup; both are fixed in `Server/main/barrier/servo.c`.


The display GPIO defaults are in `Server/sdkconfig` and can be changed through the ESP-IDF SDK Configuration Editor under **Parking TFT display**.
