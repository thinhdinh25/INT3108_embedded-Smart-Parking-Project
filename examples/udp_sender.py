"""Simulate the Sensor Client: send one parking update to the server over UDP."""

import argparse
import json
import socket

from packet_auth import seal

UDP_PORT = 3333


def send_parking_update(host, slots, water_mm, fire):
    # 1 = occupied, 0 = free, for spaces S1..S5.
    request = {"type": "parking_update", "slots": slots, "water_mm": water_mm, "fire": fire}
    payload = json.dumps(request, separators=(",", ":")).encode("utf-8")

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_socket:
        udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        udp_socket.sendto(seal(payload), (host, UDP_PORT))

    print(f"Sent signed {payload.decode()} to {host}:{UDP_PORT}")


def parse_slots(text):
    slots = [int(s) for s in text.split(",")]
    if len(slots) != 5 or any(s not in (0, 1) for s in slots):
        raise argparse.ArgumentTypeError("expected five 0/1 values, e.g. 1,0,0,1,0")
    return slots


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.4.255",
                        help="server IP or broadcast address (default: hotspot broadcast)")
    parser.add_argument("--slots", type=parse_slots, default=[1, 0, 0, 1, 0],
                        help="S1..S5 as 0/1, e.g. 1,0,0,1,0")
    parser.add_argument("--water", type=float, default=0.2, help="water level in mm")
    parser.add_argument("--fire", action="store_true", help="report fire")
    args = parser.parse_args()
    send_parking_update(args.host, args.slots, args.water, args.fire)
