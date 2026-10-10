#!/usr/bin/env python3
"""Test real MOOS condition/range gates, visuals and captured MAVLink packets."""

import importlib.util
import math
import os
import signal
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MOOS_BIN = Path(os.environ.get("MOOS_BIN_DIR", ROOT.parent / "moos-ivp/bin"))


def checksum(data):
    crc = 0xFFFF
    for byte in data:
        tmp = byte ^ (crc & 0xFF)
        tmp = (tmp ^ (tmp << 4)) & 0xFF
        crc = ((crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)) & 0xFFFF
    return crc


class LandingTargetTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="landing-target-")
        self.directory = Path(self.temp.name)
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen()
        self.listener.settimeout(10)
        self.connection = None
        self.process = None
        self.buffer = bytearray()
        self.stop_native = threading.Event()
        self.native_thread = None

    def tearDown(self):
        self.stop_native.set()
        if self.native_thread:
            self.native_thread.join(timeout=2)
        if self.connection:
            self.connection.close()
        self.listener.close()
        if self.process and self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=5)
        self.temp.cleanup()

    def launch(self, conditions="", timeout=30, show_visuals=True, pose_source="moos"):
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        self.mission = self.directory / "test.moos"
        self.mission.write_text(f"""ServerHost = 127.0.0.1
ServerPort = {port}
Community = target_test
LatOrigin = 42
LongOrigin = -71
ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = {MOOS_BIN / 'MOOSDB'} @ NewConsole = false
  Run = {ROOT / 'bin/pMavlinkLandingTarget'} @ NewConsole = false
  Run = {MOOS_BIN / 'pLogger'} @ NewConsole = false
}}
ProcessConfig = pMavlinkLandingTarget
{{
  AppTick = 20
  CommsTick = 20
  mavlink_port = {self.listener.getsockname()[1]}
  vehicle_pose_source = {pose_source}
  input_timeout = {timeout}
  detection_range = 12
  show_visuals = {str(show_visuals).lower()}
  {conditions}
}}
ProcessConfig = pLogger
{{
  File = LOG_probe
  AsyncLog = true
  WildCardLogging = true
}}
""")
        with (self.directory / "antler.log").open("w") as output:
            self.process = subprocess.Popen([str(MOOS_BIN / "pAntler"), str(self.mission)],
                                            cwd=self.directory, stdout=output,
                                            stderr=subprocess.STDOUT, start_new_session=True)
        self.connection, _ = self.listener.accept()
        self.connection.settimeout(0.05)
        time.sleep(0.8)

    def poke(self, *values):
        subprocess.run([str(MOOS_BIN / "uPokeDB"), str(self.mission), "--quiet", *values],
                       capture_output=True, text=True, timeout=12, check=True)

    def geometry(self, altitude=8):
        self.poke("TARGET_NAV_X=0", "TARGET_NAV_Y=-115", "TARGET_NAV_LAT=42",
                  "TARGET_NAV_LONG=-71", "UAV_NAV_LAT=42", "UAV_NAV_LONG=-71",
                  "UAV_NAV_HEADING=30", "UAV_NAV_ROLL=0", "UAV_NAV_PITCH=0",
                  "UAV_NAV_ATTITUDE_AVAILABLE=1", "UAV_NAV_ATTITUDE_AGE=0",
                  "UAV_NAV_ATTITUDE_TIME_USEC=123456",
                  f"UAV_NAV_ALTITUDE_MSL={altitude}")

    def collect(self, seconds=0.5):
        assert self.connection is not None
        packets = []
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                data = self.connection.recv(4096)
                if not data:
                    break
                self.buffer.extend(data)
            except TimeoutError:
                continue
            while len(self.buffer) >= 12:
                self.assertEqual(self.buffer[0], 0xFD)
                size = 12 + self.buffer[1]
                if len(self.buffer) < size:
                    break
                packet = bytes(self.buffer[:size])
                del self.buffer[:size]
                message_id = int.from_bytes(packet[7:10], "little")
                extra = {0: 50, 76: 152, 149: 200}[message_id]
                self.assertEqual(struct.unpack("<H", packet[-2:])[0],
                                 checksum(packet[1:-2] + bytes((extra,))))
                if message_id == 76:
                    self.assertEqual(struct.unpack_from("<H", packet, 38)[0], 511)
                    self.assertIn(struct.unpack_from("<f", packet, 10)[0], (31, 33))
                    self.assertEqual(packet[40:42], bytes((1, 1)))
                if message_id == 149:
                    self.assertEqual(packet[5:7], bytes((1, 191)))
                    packets.append(packet[10:-2])
        return packets

    def start_native(self, pitch=10, skew=0, system=1):
        assert self.connection is not None
        def send():
            started = time.monotonic()
            while not self.stop_native.is_set():
                stamp = 10000 + int((time.monotonic() - started) * 1000)
                q = (math.cos(math.radians(pitch)/2), 0,
                     math.sin(math.radians(pitch)/2), 0)
                payloads = ((31, struct.pack("<Ifffffff", stamp, *q, 0, 0, 0), 246),
                            (33, struct.pack("<IiiiihhhH", stamp+skew, 420000000,
                                             -710000000, 8000, 8000, 0, 0, 0, 0), 104))
                for mid, payload, extra in payloads:
                    header = bytes((len(payload), 0, 0, 0, system, 1, mid, 0, 0))
                    packet = b"\xfd" + header + payload
                    packet += struct.pack("<H", checksum(header + payload + bytes((extra,))))
                    try:
                        self.connection.sendall(packet)
                    except OSError:
                        return
                self.stop_native.wait(0.05)
        self.native_thread = threading.Thread(target=send, daemon=True)
        self.native_thread.start()

    def test_direct_native_pose_ignores_relayed_aircraft_geometry_and_expires(self):
        self.launch(pose_source="mavlink")
        self.geometry()  # Fresh MOOS-only aircraft data cannot unlock direct mode.
        self.assertFalse(self.collect())
        self.start_native(pitch=10)
        self.collect()
        packet = self.collect()[-1]
        self.assertAlmostEqual(struct.unpack_from("<fff", packet, 30)[0],
                               -8*math.sin(math.radians(10)), places=5)
        # Deliberately contradictory relayed pose must not change native geometry.
        self.poke("UAV_NAV_PITCH=-45", "UAV_NAV_ALTITUDE_MSL=999",
                  "UAV_NAV_LAT=0", "UAV_NAV_LONG=0")
        packet = self.collect()[-1]
        body = struct.unpack_from("<fff", packet, 30)
        self.assertAlmostEqual(body[0], -8*math.sin(math.radians(10)), places=5)
        self.assertAlmostEqual(body[2], 8*math.cos(math.radians(10)), places=5)
        self.stop_native.set()
        self.native_thread.join(timeout=2)
        self.collect(seconds=0.6)
        self.geometry()  # No stale-to-MOOS fallback after native input is lost.
        self.collect()
        self.assertFalse(self.collect())

    def test_direct_native_wrong_system_and_skew_fail_closed(self):
        for system, skew in ((2, 0), (1, 100)):
            with self.subTest(system=system, skew=skew):
                self.launch(pose_source="mavlink")
                self.start_native(system=system, skew=skew)
                self.geometry()
                self.assertFalse(self.collect())
                self.stop_native.set()
                self.native_thread.join(timeout=2)
                self.connection.close()
                os.killpg(self.process.pid, signal.SIGTERM)
                self.process.wait(timeout=5)
                self.buffer.clear()
                self.stop_native.clear()

    def latest_visuals(self):
        latest = {}
        for path in self.directory.glob("LOG_probe*/*.alog"):
            for line in path.read_text().splitlines():
                fields = line.split(None, 3)
                if len(fields) == 4 and fields[1] in ("VIEW_POINT", "VIEW_CIRCLE", "VIEW_POLYGON"):
                    spec = dict(item.split("=", 1) for item in fields[3].split(",") if "=" in item)
                    latest[spec.get("label")] = (fields[1], spec)
        return latest

    def assert_visuals(self, active):
        visuals = self.latest_visuals()
        for label in ("landing_target_point", "landing_target_range", "landing_target_slice"):
            self.assertIn(label, visuals)
            variable, spec = visuals[label]
            self.assertEqual(spec["active"].strip(), "true" if active else "false", spec)
            self.assertEqual(spec["label_color"].strip(), "invisible")
            self.assertEqual(float(spec["x"]), 0)
            self.assertEqual(float(spec["y"]), -115)
            if label.endswith("point"):
                self.assertEqual(variable, "VIEW_POINT")
        self.assertNotIn("landing_target_deck", visuals)
        return visuals

    def test_default_true_and_distance_only_gates_broadcast(self):
        self.launch()
        self.poke("TARGET_NAV_X=0", "TARGET_NAV_Y=-115")
        self.assertFalse(self.collect())
        self.assert_visuals(True)  # Display needs no UAV observations or target yaw.
        self.geometry()
        self.collect()  # Drain packets queued during the poke.
        packets = self.collect()
        self.assertTrue(packets)
        self.assertTrue(all(p[29] == 12 and p[59] == 1 for p in packets))
        self.assertEqual(struct.unpack_from("<Q", packets[-1])[0], 123456)
        self.assertAlmostEqual(struct.unpack_from("<f", packets[-1], 16)[0], 8, places=4)
        self.assertEqual(struct.unpack_from("<fff", packets[-1], 30), (0, 0, 8))
        self.assertEqual(struct.unpack_from("<ffff", packets[-1], 42), (0, 0, 0, 0))
        visuals = self.assert_visuals(True)
        self.assertAlmostEqual(float(visuals["landing_target_slice"][1]["radius"]),
                               math.sqrt(12**2 - 8**2), places=3)
        self.geometry(altitude=15)
        self.collect()
        self.assertFalse(self.collect())
        visuals = self.assert_visuals(True)
        self.assertEqual(float(visuals["landing_target_slice"][1]["radius"]), 0.001)

    def test_quaternion_encodes_grid_heading_independently_of_vehicle_yaw(self):
        self.launch(show_visuals=False)  # Heading is needed for packets, not just visuals.
        self.geometry()
        self.poke("TARGET_NAV_HEADING=30", "UAV_NAV_HEADING=90")
        self.collect()
        packet = self.collect()[-1]
        q = struct.unpack_from("<ffff", packet, 42)
        spec = importlib.util.spec_from_file_location("utm_helper", ROOT / "scripts/local_to_latlon.py")
        assert spec is not None and spec.loader is not None
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        east0, north0, _ = helper.utm(42, -71)
        east1, north1, _ = helper.utm(42.0001, -71)
        grid_north = math.degrees(math.atan2(east1-east0, north1-north0))
        for vehicle_yaw in (90, 180):
            if vehicle_yaw == 180:
                self.poke("UAV_NAV_HEADING=180")
                self.collect()
                q = struct.unpack_from("<ffff", self.collect()[-1], 42)
            relative_yaw = math.degrees(2 * math.atan2(q[3], q[0]))
            expected = 30 - grid_north - vehicle_yaw
            error = (relative_yaw - expected + 180) % 360 - 180
            self.assertAlmostEqual(error, 0, delta=0.0001)
            self.assertAlmostEqual(sum(value**2 for value in q), 1, places=6)
            self.assertEqual(q[1:3], (0, 0))

    def test_full_attitude_body_vector_quaternion_and_angular_sign(self):
        self.launch(show_visuals=False)
        self.geometry()
        # N/E/Down displacement (3,-2,8) metres, independent of aircraft attitude.
        self.poke(f"TARGET_NAV_LAT={42 + math.degrees(3 / 6378137)}",
                  f"TARGET_NAV_LONG={-71 + math.degrees(-2 / (6378137 * math.cos(math.radians(42))))}",
                  "TARGET_NAV_HEADING=30")
        for roll, pitch, yaw in ((12, -10, 30), (-17, 15, 90), (20, 9, 359)):
            self.poke(f"UAV_NAV_ROLL={roll}", f"UAV_NAV_PITCH={pitch}", f"UAV_NAV_HEADING={yaw}")
            self.collect()
            packet = self.collect()[-1]
            body = struct.unpack_from("<fff", packet, 30)
            distance = struct.unpack_from("<f", packet, 16)[0]
            cr, sr = math.cos(math.radians(roll)), math.sin(math.radians(roll))
            cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
            cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
            matrix = ((cp*cy, sr*sp*cy-cr*sy, cr*sp*cy+sr*sy),
                      (cp*sy, sr*sp*sy+cr*cy, cr*sp*sy-sr*cy),
                      (-sp, sr*cp, cr*cp))
            world = tuple(sum(row[j]*body[j] for j in range(3)) for row in matrix)
            for actual, expected in zip(world, (3, -2, 8)):
                self.assertAlmostEqual(actual, expected, delta=0.0001)
            self.assertAlmostEqual(distance, math.sqrt(77), places=4)
            ax, ay = struct.unpack_from("<ff", packet, 8)
            # Actual ArduPilot angular-only convention must agree with emitted XYZ.
            los = (-math.tan(ay), math.tan(ax), 1)
            norm = math.sqrt(sum(x*x for x in los))
            for a, b in zip(los, body):
                self.assertAlmostEqual(a/norm, b/distance, delta=1e-6)
            w, x, y, z = struct.unpack_from("<ffff", packet, 42)
            axis = (1-2*(y*y+z*z), 2*(x*y+w*z), 2*(x*z-w*y))
            target_world = tuple(sum(row[j]*axis[j] for j in range(3)) for row in matrix)
            self.assertAlmostEqual(target_world[2], 0, delta=1e-6)
            target_yaw = math.degrees(math.atan2(target_world[1], target_world[0]))
            # UTM zone 19 convergence at 42N, -71 is about +1.34 degrees.
            self.assertAlmostEqual(target_yaw, 30-1.33857, delta=0.01)
        # Reposted numeric Euler angles do not turn invalid native evidence into readiness.
        self.poke("UAV_NAV_ATTITUDE_AVAILABLE=0")
        self.collect()
        self.assertFalse(self.collect())
        self.poke("UAV_NAV_ATTITUDE_AVAILABLE=1", "UAV_NAV_ATTITUDE_AGE=99")
        self.collect()
        self.assertFalse(self.collect())

    def test_multiple_conditions_and_shared_geometry_variable(self):
        self.launch("condition = ENABLE=true\ncondition = QUALITY>=2\n"
                    "condition = UAV_NAV_ALTITUDE_MSL<10")
        self.geometry()
        self.assertFalse(self.collect())
        self.assert_visuals(False)  # Unknown condition inputs fail closed.
        self.poke("ENABLE:=true", "QUALITY=1")
        self.collect()
        self.assertFalse(self.collect())
        self.assert_visuals(False)
        self.poke("QUALITY=2")
        self.collect()
        self.assertTrue(self.collect())
        self.assert_visuals(True)
        self.poke("ENABLE:=false")
        self.collect()
        self.assertFalse(self.collect())
        self.assert_visuals(False)
        self.poke("ENABLE:=true")
        self.geometry(altitude=11)  # In range, but overlapping altitude condition is false.
        self.collect()
        self.assertFalse(self.collect())
        self.assert_visuals(False)

    def test_invalid_condition_never_defaults_to_true(self):
        self.launch("condition = (ENABLE=true")
        self.geometry()
        self.assertFalse(self.collect())
        self.assert_visuals(False)

    def test_stale_geometry_stops_packets_not_visuals(self):
        self.launch(timeout=4)
        self.geometry()
        self.assertTrue(self.collect())
        time.sleep(4.5)
        self.collect()
        self.assertFalse(self.collect())
        self.assert_visuals(True)

    def test_visuals_do_not_require_a_connected_mavlink_endpoint(self):
        self.launch()
        self.geometry()
        self.collect()
        assert self.connection is not None
        self.connection.close()
        self.connection = None
        self.listener.close()
        time.sleep(1.5)
        self.assert_visuals(True)


if __name__ == "__main__":
    unittest.main()
