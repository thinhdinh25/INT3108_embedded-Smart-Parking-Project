"""Send an RFID card scan to the ESP32 parking server over UDP."""

import argparse
import json
import socket

ESP32_BROADCAST = "192.168.4.255"
UDP_PORT = 3333


def send_rfid_scan(card_id, gate="in", temperature_c=28.0):
    request = {
        "type": "rfid_event",
        "card_id": card_id,
        "gate": gate,
        "temperature_c": temperature_c,
    }
    payload = json.dumps(request).encode("utf-8")

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_socket:
        udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        udp_socket.sendto(payload, (ESP32_BROADCAST, UDP_PORT))

    print(f"Sent {payload.decode()} to {ESP32_BROADCAST}:{UDP_PORT}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("card_id", nargs="?", default="A1B2C3D4")
    parser.add_argument("--gate", choices=["in", "out"], default="in")
    args = parser.parse_args()
    send_rfid_scan(args.card_id, args.gate)
