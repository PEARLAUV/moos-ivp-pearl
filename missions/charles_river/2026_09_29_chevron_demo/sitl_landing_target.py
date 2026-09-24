#!/usr/bin/env python3
"""Publish a centered MAVLink landing target for aug_pearl_uav SITL."""

import argparse
import os
import time

# LANDING_TARGET position fields are MAVLink 2 extensions. This must be set
# before importing pymavlink.
os.environ.setdefault("MAVLINK20", "1")

from pymavlink import mavutil


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Publish the centered target that stands in for PEARL's vision "
            "source during ArduCopter SITL runs."
        )
    )
    parser.add_argument("--connection", default="tcp:127.0.0.1:5763")
    parser.add_argument("--source-system", type=int, default=1)
    parser.add_argument("--source-component", type=int, default=191)
    parser.add_argument("--target-num", type=int, default=0)
    parser.add_argument("--rate", type=float, default=10.0, help="publish rate in Hz")
    parser.add_argument("--distance", type=float, default=8.0)
    parser.add_argument("--x", type=float, default=0.0)
    parser.add_argument("--y", type=float, default=0.0)
    parser.add_argument("--z", type=float, default=8.0)
    args = parser.parse_args()
    if args.rate <= 0:
        parser.error("--rate must be greater than zero")
    return args


def publish_until_disconnected(args: argparse.Namespace) -> None:
    connection = mavutil.mavlink_connection(
        args.connection,
        source_system=args.source_system,
        source_component=args.source_component,
    )
    try:
        if connection.wait_heartbeat(timeout=5) is None:
            raise ConnectionError("timed out waiting for ArduPilot heartbeat")

        print(
            "sitl_landing_target.py: publishing target "
            f"{args.target_num} on {args.connection}",
            flush=True,
        )
        interval = 1.0 / args.rate
        while True:
            connection.mav.landing_target_send(
                time.time_ns() // 1000,
                args.target_num,
                mavutil.mavlink.MAV_FRAME_BODY_FRD,
                0.0,
                0.0,
                args.distance,
                0.0,
                0.0,
                args.x,
                args.y,
                args.z,
                (1.0, 0.0, 0.0, 0.0),
                mavutil.mavlink.LANDING_TARGET_TYPE_VISION_FIDUCIAL,
                1,
            )
            time.sleep(interval)
    finally:
        connection.close()


def main() -> None:
    args = parse_args()
    while True:
        try:
            publish_until_disconnected(args)
        except KeyboardInterrupt:
            return
        except (ConnectionError, OSError):
            # ArduCopter may not have opened SERIAL2 yet, or it may be
            # restarting. Keep the SITL helper alive and reconnect quietly.
            time.sleep(1.0)


if __name__ == "__main__":
    main()
