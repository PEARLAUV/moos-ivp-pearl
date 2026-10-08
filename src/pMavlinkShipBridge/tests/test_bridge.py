#!/usr/bin/env python3
"""Exercise the real MOOS app against a TCP MAVLink capture endpoint."""

import math
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
CRC_EXTRA = {0: 50, 33: 104, 149: 200}


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
  event = var=UAV_NAV_LAT, val=42.358436, time=0
  event = var=UAV_NAV_LONG, val=-71.087448, time=0
  event = var=UAV_NAV_HEADING, val=0, time=0
  event = var=UAV_NAV_ALTITUDE_MSL, val=13, time=0
  event = var=SHIP_POSE_ENABLED, val=true, time=0
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
  target_number = 7
  detection_range = 12
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
                self.assertEqual(packet[5:7], bytes((1 if mid == 149 else 17, 191)))
                self.assertEqual(
                    checksum(packet[1:-2] + bytes((CRC_EXTRA[mid],))),
                    struct.unpack("<H", packet[-2:])[0],
                )
                messages.append((mid, packet[10:-2]))
        return messages

    def navigation(self, **overrides):
        latitude = 42.358436
        uav_latitude = latitude - math.degrees(4 / 6378137)
        uav_longitude = -71.087448 - math.degrees(
            3 / (6378137 * math.cos(math.radians((latitude + uav_latitude) / 2)))
        )
        values = {"NAV_LAT": latitude, "NAV_LONG": -71.087448,
                  "NAV_HEADING": 90, "NAV_SPEED": 0.5,
                  "UAV_NAV_LAT": uav_latitude, "UAV_NAV_LONG": uav_longitude,
                  "UAV_NAV_HEADING": 90, "UAV_NAV_ALTITUDE_MSL": 13}
        values.update(overrides)
        self.poke(**values)

    def test_pose_target_range_staleness_and_reconnection(self):
        self.assertFalse(any(mid == 149 for mid, _ in self.collect()))
        self.navigation()
        messages = self.collect()
        self.assertFalse(any(mid == 33 for mid, _ in messages))
        self.assertTrue(any(mid == 149 for mid, _ in messages))
        self.poke(SHIP_POSE_ENABLED="true")
        messages = self.collect()
        pose = next(payload for mid, payload in messages if mid == 33)
        _, lat, lon, alt, _, vn, ve, _, heading = struct.unpack("<IiiiihhhH", pose)
        self.assertEqual((lat, lon, alt), (423584360, -710874480, 5000))
        self.assertEqual((vn, ve, heading), (0, 50, 9000))
        target = next(payload for mid, payload in messages if mid == 149)
        fields = struct.unpack("<QfffffBBfff4fBB", target)
        self.assertAlmostEqual(fields[3], (3**2 + 4**2 + 8**2)**0.5, places=5)
        self.assertEqual(fields[6:8], (7, 12))
        self.assertAlmostEqual(fields[8], 3, places=5)
        self.assertAlmostEqual(fields[9], -4, places=5)
        self.assertAlmostEqual(fields[10], 8, places=5)
        self.assertEqual(fields[-2:], (2, 1))

        # Only poses are condition-gated; visual target detection is independent.
        for disabled in ("false", "invalid"):
            self.navigation(SHIP_POSE_ENABLED=disabled)
            self.collect()  # Drain packets generated before the mail arrived.
            messages = self.collect()
            self.assertFalse(any(mid == 33 for mid, _ in messages))
            self.assertTrue(any(mid == 149 for mid, _ in messages))
        self.navigation(SHIP_POSE_ENABLED="true")
        self.assertTrue(any(mid == 33 for mid, _ in self.collect()))

        self.navigation(UAV_NAV_LAT=42.359436)
        self.collect()  # Drain packets generated before the mail arrived.
        self.assertFalse(any(mid == 149 for mid, _ in self.collect()))

        self.navigation()
        self.assertTrue(any(mid == 149 for mid, _ in self.collect()))
        time.sleep(4.5)
        self.collect()
        self.assertFalse(any(mid in (33, 149) for mid, _ in self.collect()))

        self.connection.close()
        self.connection, _ = self.listener.accept()
        self.connection.settimeout(0.05)
        self.buffer.clear()
        self.navigation()
        self.assertTrue(any(mid == 149 for mid, _ in self.collect(1)))

    def test_warped_prefix_stream(self):
        self.collect(0.5)  # Let the periodic source and subscriber settle.
        messages = self.collect(1)
        poses = [payload for mid, payload in messages if mid == 33]
        targets = [payload for mid, payload in messages if mid == 149]
        # 10 Hz simulated publication should exceed the unwarped wall-time rate.
        self.assertGreaterEqual(len(poses), 25)
        self.assertGreaterEqual(len(targets), 25)
        self.assertEqual(struct.unpack("<IiiiihhhH", poses[-1])[1:4],
                         (423584360, -710874480, 5000))

    def test_numeric_pose_condition(self):
        # MOOSDB fixes a variable\'s type at its first publication, so numeric
        # and string boolean inputs must be tested in separate fresh databases.
        for enabled in (1, 0, 2, 1):
            self.navigation(SHIP_POSE_ENABLED=enabled)
            self.collect()
            messages = self.collect()
            self.assertEqual(any(mid == 33 for mid, _ in messages), enabled == 1)
            self.assertTrue(any(mid == 149 for mid, _ in messages))

    def test_dynamic_height_and_height_staleness(self):
        self.navigation()
        self.poke(SHIP_POSE_ENABLED="true")
        self.collect()
        self.assertFalse(any(mid in (33, 149) for mid, _ in self.collect()))
        for altitude in (7, 9, 6):
            self.navigation(TEST_DECK_ALTITUDE=altitude)
            messages = self.collect()
            pose = next(payload for mid, payload in reversed(messages) if mid == 33)
            self.assertEqual(struct.unpack("<IiiiihhhH", pose)[3], altitude * 1000)
            target = next(payload for mid, payload in reversed(messages) if mid == 149)
            self.assertAlmostEqual(struct.unpack("<QfffffBBfff4fBB", target)[10],
                                   13 - altitude, places=5)
        time.sleep(4.5)
        self.navigation()  # Fresh GPS does not conceal stale deck-height input.
        self.collect()
        self.assertFalse(any(mid in (33, 149) for mid, _ in self.collect()))


if __name__ == "__main__":
    unittest.main()
