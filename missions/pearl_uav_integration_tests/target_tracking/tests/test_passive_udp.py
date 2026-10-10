#!/usr/bin/env python3
"""End-to-end GPS-free passive MAVLink telemetry test using synthetic UDP packets."""

import math
import os
import shutil
import signal
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
PEARL = Path(__file__).resolve().parents[4]
MOOS_BIN = PEARL.parent / "moos-ivp/bin"
UAV_BIN = PEARL.parent / "moos-ivp-uav-base/bin"
CRC_EXTRA = {0: 50, 30: 39, 132: 85, 149: 200}


def checksum(data):
    crc = 0xFFFF
    for byte in data:
        tmp = byte ^ (crc & 0xFF)
        tmp = (tmp ^ (tmp << 4)) & 0xFF
        crc = (crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)
    return crc


def encode(message_id, payload, sequence):
    header = bytes((len(payload), 0, 0, sequence % 256, 1, 1))
    header += message_id.to_bytes(3, "little")
    crc = checksum(header + payload + bytes((CRC_EXTRA[message_id],)))
    return b"\xfd" + header + payload + struct.pack("<H", crc)


def unused_port(kind):
    with socket.socket(socket.AF_INET, kind) as reservation:
        reservation.bind(("127.0.0.1", 0))
        return reservation.getsockname()[1]


class PassiveUdpTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="target-tracking-udp-")
        self.directory = Path(self.temp.name)
        for name in ("launch.sh", "clean.sh", "meta_target_tracking.moos"):
            shutil.copy2(MISSION / name, self.directory / name)
        self.moos_port = unused_port(socket.SOCK_STREAM)
        self.udp_port = unused_port(socket.SOCK_DGRAM)
        self.stop_sender = threading.Event()
        self.process = None

    def tearDown(self):
        self.stop_sender.set()
        if self.process and self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=5)
        self.temp.cleanup()

    def send_telemetry(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(("127.0.0.1", 0))
        sequence = 0
        boot_ms = 1000
        while not self.stop_sender.is_set():
            time_usec = boot_ms * 1000
            messages = (
                (0, struct.pack("<IBBBBB", 0, 2, 3, 0, 3, 3)),
                (30, struct.pack("<Iffffff", boot_ms, math.radians(10),
                                 math.radians(-5), math.radians(30), 0, 0, 0)),
                (132, struct.pack("<IHHHBBBB", boot_ms, 20, 700, 123,
                                  0, 1, 25, 0)),
                (149, struct.pack("<QfffffBBfff4fBB", time_usec, 0, 0, 10,
                                  0, 0, 0, 12, 10, 0, 2,
                                  1, 0, 0, 0, 0, 1)),
            )
            for message_id, payload in messages:
                sock.sendto(encode(message_id, payload, sequence),
                            ("127.0.0.1", self.udp_port))
                sequence = (sequence + 1) % 256
            boot_ms += 100
            self.stop_sender.wait(0.1)
        sock.close()

    def query(self, condition, wait=8):
        result = subprocess.run(
            [str(MOOS_BIN / "uQueryDB"), "--host=localhost",
             f"--port={self.moos_port}", f"--condition={condition}",
             f"--wait={wait}"],
            capture_output=True,
            text=True,
            timeout=wait + 8,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_attitude_rangefinder_and_relative_target_without_gps(self):
        env = os.environ.copy()
        env["PATH"] = os.pathsep.join(
            (str(PEARL / "bin"), str(UAV_BIN), str(MOOS_BIN), env.get("PATH", ""))
        )
        output = (self.directory / "launch.log").open("w")
        self.addCleanup(output.close)
        self.process = subprocess.Popen(
            [str(self.directory / "launch.sh"), "--nogui", "--xlaunched",
             f"--mport={self.moos_port}", "--udp_bind=0.0.0.0",
             f"--udp_port={self.udp_port}"],
            cwd=self.directory,
            env=env,
            stdout=output,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        sender = threading.Thread(target=self.send_telemetry, daemon=True)
        sender.start()

        self.query("PARDUBRIDGE_PASSIVE_CONNECTED=1", wait=12)
        self.query("UAV_ATTITUDE_AVAILABLE=1")
        self.query("UAV_ROLL>9.9")
        self.query("UAV_PITCH<0")
        self.query("UAV_RANGEFINDER_AVAILABLE=1")
        self.query("UAV_RANGEFINDER_DISTANCE>1.22")
        self.query("LANDING_TARGET_VIEW_ACTIVE=1")


if __name__ == "__main__":
    unittest.main()
