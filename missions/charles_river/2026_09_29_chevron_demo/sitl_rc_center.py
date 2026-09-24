#!/usr/bin/env python3
"""Supply neutral yaw input to ArduPilot's SITL RC UDP port."""

import argparse
import socket
import struct
import time


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5501)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # Zero leaves a channel unchanged. Override only yaw with a centered stick.
    rc_packet = struct.pack("<8H", 0, 0, 0, 1500, 0, 0, 0, 0)

    while True:
        sock.sendto(rc_packet, (args.host, args.port))
        time.sleep(0.02)


if __name__ == "__main__":
    main()
