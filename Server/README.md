# INT3108 Smart Parking Project

ESP-IDF firmware for an ESP32 smart parking controller. The device creates a
Wi-Fi access point, serves a small web page over HTTP, and listens for UDP
datagrams from other microcontrollers on the local network.

## Network interfaces

- Wi-Fi SSID: `ESP32-Parking`
- Wi-Fi password: `12345678`
- Device address: `192.168.4.1`
- Web interface: `http://192.168.4.1`
- UDP listener: `192.168.4.1:3333`

The UDP service currently logs each received datagram's sender and text
payload. It does not send a reply or interpret a message protocol yet.

## Source layout

```text
main/
├── main.c                         Application entry and NVS initialization
└── network/
    ├── network_service.c/.h       Starts the network services
    ├── wifi_ap.c/.h                SoftAP setup and connection events
    ├── http/
    │   ├── http_server.c/.h        HTTP routes and handlers
    │   ├── index.html               Web page embedded in firmware
    │   └── style.css                Page stylesheet embedded in firmware
    └── udp/
        └── udp_server.c/.h          UDP datagram listener
```

`main/CMakeLists.txt` lists the firmware sources, embedded web assets, and
ESP-IDF component dependencies.

## Build and flash

From an ESP-IDF configured terminal, select the target board and build with:

```sh
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```
