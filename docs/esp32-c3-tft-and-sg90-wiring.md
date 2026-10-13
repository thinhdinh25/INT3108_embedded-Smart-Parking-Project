# ESP32-C3 SuperMini, ST7789, and two SG90 servos

This note documents the display wiring used by the server firmware and a practical way to add two SG90 servos.

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

## Two SG90 signal connections

Each SG90 needs one PWM control signal. Subject to the board's actual pin labels and any other attached hardware, use two otherwise-unused exposed GPIOs, for example GPIO0 and GPIO1:

| Servo | Signal wire | Power wire | Ground wire |
| --- | --- | --- | --- |
| SG90 #1 | GPIO0 | External regulated +5 V | Common GND |
| SG90 #2 | GPIO1 | External regulated +5 V | Common GND |

SG90 wire colors commonly use orange/yellow for signal, red for +5 V, and brown/black for ground; check the specific servo's documentation.

### Power and signal notes

- Power the servos from a regulated 5 V supply sized for both servos' stall current. Do not power both from the ESP32-C3 3V3 output. The 5 V supply's current requirement depends on the servo/load; allow substantial headroom over their idle current.
- Connect the external supply ground, both servo grounds, and ESP32-C3 GND together so the control signals have a shared reference.
- Connect each servo's signal wire to its assigned GPIO. Servo signal is a logic control input; do not connect the servo's +5 V wire to a GPIO.
- Check the exact SuperMini board revision and pin labels before wiring. GPIO0/GPIO1 are candidate signal pins, not a claim that every clone exposes them or that they are free in every build.
- Servo control generally uses roughly 50 Hz pulses. Select a PWM-capable peripheral in firmware and confirm the signal levels are accepted by the specific servos.

## Pin availability summary

The documented TFT wiring occupies GPIO3, GPIO4, GPIO5, GPIO6, GPIO7, and GPIO10. Two more GPIOs are needed for the servo signals. GPIO21 being unavailable does not by itself prevent adding the servos; use two other exposed, unused GPIOs and verify board-specific restrictions before connecting them.

## Firmware configuration

The current server code initializes the TFT but does not implement servo PWM. Adding the physical wiring alone will not move the servos; firmware must configure two PWM outputs and provide the desired angle/position commands.

The display GPIO defaults are in `Server/sdkconfig` and can be changed through the ESP-IDF SDK Configuration Editor under **Parking TFT display**.
