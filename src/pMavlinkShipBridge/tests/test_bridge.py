#!/usr/bin/env python3
"""Exercise the real MOOS app against a TCP MAVLink capture endpoint."""

import os
import signal
import socket
import struct
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MOOS_BIN = ROOT.parent / "moos-ivp" / "bin"
APP = ROOT / "bin" / "pMavlinkShipBridge"
CRC_EXTRA = {0: 50, 33: 104}


def checksum(data):
    crc = 0xFFFF
    for byte in data:
        tmp = byte ^ (crc & 0xFF)
        tmp = (tmp ^ (tmp << 4)) & 0xFF
        crc = (crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)
    return crc


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class BridgeTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mavlink-ship-")
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen()
        self.listener.settimeout(10)
        self.mission = Path(self.temp.name) / "test.moos"
        altitude_config = ("ship_altitude_var = TEST_DECK_ALTITUDE"
                           if "dynamic_height" in self._testMethodName else "")
        warped = "warped_prefix" in self._testMethodName
        pulse_run = (f"Run = {MOOS_BIN / 'uTimerScript'} @ NewConsole = false ~ pulse"
                     if warped else "")
        pulse_config = """ProcessConfig = pulse
{
  AppTick = 20
  CommsTick = 20
  paused = false
  reset_max = unlimited
  reset_time = 0.2
  event = var=REMOTE_NAV_LAT, val=42.358436, time=0
  event = var=REMOTE_NAV_LONG, val=-71.087448, time=0
  event = var=REMOTE_NAV_HEADING, val=0, time=0
  event = var=REMOTE_NAV_SPEED, val=0, time=0
  event = var=SHIP_POSE_ENABLED, val=true, time=0
  event = var=NAV_LAT, val=0, time=0
  event = var=NAV_LONG, val=0, time=0
  event = var=NAV_HEADING, val=180, time=0
  event = var=NAV_SPEED, val=5, time=0
}
""" if warped else ""
        self.mission.write_text(f"""MOOSTimeWarp = {5 if warped else 1}
ServerHost = 127.0.0.1
ServerPort = {free_port()}
Community = bridge_test
ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = {MOOS_BIN / 'MOOSDB'} @ NewConsole = false
  Run = {APP} @ NewConsole = false
  {pulse_run}
}}
ProcessConfig = pMavlinkShipBridge
{{
  AppTick = 20
  CommsTick = 20
  mavlink_port = {self.listener.getsockname()[1]}
  ship_system_id = 17
  source_component_id = 191
  input_timeout = 4
  ship_altitude_msl = 5
  ship_nav_prefix = {'REMOTE_NAV' if warped else 'NAV'}
  {altitude_config}
}}
{pulse_config}
""")
        with open(Path(self.temp.name) / "antler.log", "w") as log:
            self.process = subprocess.Popen(
                [str(MOOS_BIN / "pAntler"), str(self.mission)],
                cwd=self.temp.name, stdout=log, stderr=subprocess.STDOUT,
                start_new_session=True,
            )
        self.connection, _ = self.listener.accept()
        self.connection.settimeout(0.05)
        self.buffer = bytearray()
        time.sleep(0.5)

    def tearDown(self):
        self.connection.close()
        self.listener.close()
        if self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=5)
        self.temp.cleanup()

    def poke(self, **values):
        subprocess.run(
            [str(MOOS_BIN / "uPokeDB"), str(self.mission), "--quiet",
             *(f"{key}={value}" for key, value in values.items())],
            capture_output=True, timeout=8, check=True,
        )

    def collect(self, seconds=0.5):
        messages = []
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                self.buffer.extend(self.connection.recv(4096))
            except TimeoutError:
                continue
            while len(self.buffer) >= 10:
                self.assertEqual(self.buffer[0], 0xFD)
                size = 12 + self.buffer[1]
                if len(self.buffer) < size:
                    break
                packet = bytes(self.buffer[:size])
                del self.buffer[:size]
                mid = int.from_bytes(packet[7:10], "little")
                self.assertEqual(packet[5:7], bytes((17, 191)))
                self.assertEqual(
                    checksum(packet[1:-2] + bytes((CRC_EXTRA[mid],))),
                    struct.unpack("<H", packet[-2:])[0],
                )
                messages.append((mid, packet[10:-2]))
        return messages

    def navigation(self, **overrides):
        values = {"NAV_LAT": 42.358436, "NAV_LONG": -71.087448,
                  "NAV_HEADING": 90, "NAV_SPEED": 0.5}
        values.update(overrides)
        self.poke(**values)

    def test_pose_gating_staleness_and_reconnection(self):
        self.navigation()
        self.assertFalse(any(mid == 33 for mid, _ in self.collect()))
        self.poke(SHIP_POSE_ENABLED="true")
        messages = self.collect()
        pose = next(payload for mid, payload in messages if mid == 33)
        _, lat, lon, alt, _, vn, ve, _, heading = struct.unpack("<IiiiihhhH", pose)
        self.assertEqual((lat, lon, alt), (423584360, -710874480, 5000))
        self.assertEqual((vn, ve, heading), (0, 50, 9000))

        for disabled in ("false", "invalid"):
            self.navigation(SHIP_POSE_ENABLED=disabled)
            self.collect()  # Drain packets generated before the mail arrived.
            self.assertFalse(any(mid == 33 for mid, _ in self.collect()))
        self.navigation(SHIP_POSE_ENABLED="true")
        self.assertTrue(any(mid == 33 for mid, _ in self.collect()))

        time.sleep(4.5)
        self.collect()
        self.assertFalse(any(mid == 33 for mid, _ in self.collect()))

        self.connection.close()
        self.connection, _ = self.listener.accept()
        self.connection.settimeout(0.05)
        self.buffer.clear()
        self.navigation(SHIP_POSE_ENABLED="true")
        self.assertTrue(any(mid == 33 for mid, _ in self.collect(1)))

    def test_warped_prefix_stream(self):
        self.collect(0.5)  # Let the periodic source and subscriber settle.
        messages = self.collect(1)
        poses = [payload for mid, payload in messages if mid == 33]
        # 10 Hz simulated publication should exceed the unwarped wall-time rate.
        self.assertGreaterEqual(len(poses), 25)
        self.assertEqual(struct.unpack("<IiiiihhhH", poses[-1])[1:4],
                         (423584360, -710874480, 5000))
        # Native UAV NAV_* must not remain subscribed as a default ship prefix.
        self.poke(APPCAST_REQ="node=all,app=pMavlinkShipBridge,duration=10,key=test")
        subprocess.run(
            [str(MOOS_BIN / "uQueryDB"), str(self.mission), "--wait=5",
             "--condition=APPCAST!=missing", "--check_var=APPCAST"],
            cwd=self.temp.name, capture_output=True, timeout=10, check=True,
        )
        appcast = (Path(self.temp.name) / ".checkvars").read_text()
        self.assertIn("pMavlinkShipBridge", appcast)
        self.assertNotIn("Unhandled Mail: NAV_", appcast)

    def test_numeric_pose_condition(self):
        # MOOSDB fixes a variable\'s type at its first publication, so numeric
        # and string boolean inputs must be tested in separate fresh databases.
        for enabled in (1, 0, 2, 1):
            self.navigation(SHIP_POSE_ENABLED=enabled)
            self.collect()
            messages = self.collect()
            self.assertEqual(any(mid == 33 for mid, _ in messages), enabled == 1)

    def test_dynamic_height_and_height_staleness(self):
        self.navigation()
        self.poke(SHIP_POSE_ENABLED="true")
        self.collect()
        self.assertFalse(any(mid == 33 for mid, _ in self.collect()))
        for altitude in (7, 9, 6):
            self.navigation(TEST_DECK_ALTITUDE=altitude)
            messages = self.collect()
            pose = next(payload for mid, payload in reversed(messages) if mid == 33)
            self.assertEqual(struct.unpack("<IiiiihhhH", pose)[3], altitude * 1000)
        time.sleep(4.5)
        self.navigation()  # Fresh GPS does not conceal stale deck-height input.
        self.collect()
        self.assertFalse(any(mid == 33 for mid, _ in self.collect()))


if __name__ == "__main__":
    unittest.main()
