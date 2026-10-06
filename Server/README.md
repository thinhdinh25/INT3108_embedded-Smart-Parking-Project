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

The UDP service listens on port `3333` and accepts JSON parking updates. The
dashboard reads the latest state from `GET /api/parking` once per second.

Connect the sender computer or controller to the `ESP32-Parking` Wi-Fi network,
then send a broadcast datagram to `192.168.4.255:3333`. The payload must contain
five slot values: `1` means occupied and `0` means free.

```json
{"type":"parking_update","slots":[1,0,0,1,0]}
```

That example represents 3 free spaces out of 5. A runnable Python example is
in `examples/udp_sender.py`. Run it from a device connected to the ESP32's
access point. The dashboard displays the latest accepted update in memory; the
state is reset when the ESP32 reboots.

If the sender is another ESP-IDF board, use the C example in
`examples/udp_sender_espidf.c`. Call `send_parking_update()` after that board
has connected to the `ESP32-Parking` access point. Its component needs the
`lwip` and `log` dependencies. Both examples send the same JSON protocol.

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

## RTOS usage

This firmware uses ESP-IDF's FreeRTOS scheduler (configured for the ESP32-C3's
single core in `sdkconfig`). `app_main()` runs as an ESP-IDF task. The UDP
listener runs in its own task, created by `udp_server_start()`, and waits for
datagrams without blocking application startup. The ESP-IDF HTTP server runs
its request handling in its own task as well. The `freertos` component is
declared directly in `main/CMakeLists.txt` because the UDP module uses its task
API.

## Build and flash

From an ESP-IDF configured terminal, select the target board and build with:

```sh
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```
