"""Simulate the RFID Client: send one card scan to the server over UDP."""

import argparse
import json
import socket

from packet_auth import seal

UDP_PORT = 3333


def send_rfid_scan(host, card_id, gate="in", temperature_c=28.0):
    request = {
        "type": "rfid_event",
        "card_id": card_id,
        "gate": gate,
        "temperature_c": temperature_c,
    }
    payload = json.dumps(request, separators=(",", ":")).encode("utf-8")

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_socket:
        udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        udp_socket.sendto(seal(payload), (host, UDP_PORT))

    print(f"Sent signed {payload.decode()} to {host}:{UDP_PORT}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("card_id", nargs="?", default="A1B2C3D4")
    parser.add_argument("--gate", choices=["in", "out"], default="in")
    parser.add_argument("--host", default="192.168.4.255",
                        help="server IP or broadcast address (default: hotspot broadcast)")
    parser.add_argument("--temp", type=float, default=28.0, help="temperature in Celsius")
    args = parser.parse_args()
    send_rfid_scan(args.host, args.card_id, args.gate, args.temp)
