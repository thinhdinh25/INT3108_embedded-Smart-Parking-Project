"""Send one example parking update to the ESP32 SoftAP over UDP."""

import json
import socket

ESP32_BROADCAST = "192.168.4.255"
UDP_PORT = 3333

# 1 = occupied, 0 = free. Here slots 1 and 4 are occupied; 3 spaces are free.
request = {
    "type": "parking_update",
    "slots": [1, 0, 0, 1, 0],
    "temperature_c": 28.0,
    "water_mm": 0.2,
}

payload = json.dumps(request).encode("utf-8")

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_socket:
    udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    udp_socket.sendto(payload, (ESP32_BROADCAST, UDP_PORT))

print(f"Sent {payload.decode()} to {ESP32_BROADCAST}:{UDP_PORT}")
