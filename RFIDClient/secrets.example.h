// Demo values. To deploy, copy this file to secrets.h (git-ignored) and change all three;
// the key must match PARKING_UDP_KEY in the server's menuconfig.
#pragma once
static const char *WIFI_SSID = "ESP32-Parking";
static const char *WIFI_PASSWORD = "12345678";
// "ParkingKey-Demo!" = 5061726b696e674b65792d44656d6f21
static const uint8_t UDP_KEY[16] = {0x50, 0x61, 0x72, 0x6b, 0x69, 0x6e, 0x67, 0x4b,
                                    0x65, 0x79, 0x2d, 0x44, 0x65, 0x6d, 0x6f, 0x21};
