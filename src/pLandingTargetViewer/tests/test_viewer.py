#!/usr/bin/env python3
"""Exercise received-target rendering in real isolated MOOS instances."""

import math
import os
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MOOS_BIN = Path(os.environ.get("MOOS_BIN_DIR", ROOT.parent / "moos-ivp/bin"))


class ViewerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="landing-target-viewer-")
        self.directory = Path(self.temp.name)
        self.process = None

    def tearDown(self):
        if self.process and self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=5)
        self.temp.cleanup()

    def launch(self, config="", warp=1, timeout=30.0, origin=True):
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        self.mission = self.directory / "test.moos"
        origin_config = "LatOrigin = 0\nLongOrigin = 3\n" if origin else ""
        self.mission.write_text(f"""ServerHost = 127.0.0.1
ServerPort = {port}
Community = viewer_test
MOOSTimeWarp = {warp}
{origin_config}ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = {MOOS_BIN / 'MOOSDB'} @ NewConsole = false
  Run = {ROOT / 'bin/pLandingTargetViewer'} @ NewConsole = false
  Run = {MOOS_BIN / 'pLogger'} @ NewConsole = false
}}
ProcessConfig = pLandingTargetViewer
{{
  AppTick = 20
  CommsTick = 20
  observation_timeout = {timeout}
  input_timeout = 30
  target_size = 2
  {config}
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
        time.sleep(1.5)

    def poke(self, *values):
        subprocess.run([str(MOOS_BIN / "uPokeDB"), str(self.mission), "--quiet", *values],
                       check=True, capture_output=True, text=True, timeout=12)
        time.sleep(0.3)

    def packet(self, include_position=True):
        pose = ("UAV_LAT=0", "UAV_LON=3") if include_position else ()
        self.poke(*pose, "UAV_YAW=30", "UAV_ROLL=0", "UAV_PITCH=0",
                  "UAV_ATTITUDE_AVAILABLE=1", "UAV_ATTITUDE_AGE=0",
                  "UAV_LANDING_TARGET_AVAILABLE=1", "UAV_LANDING_TARGET_AGE=0",
                  "UAV_LANDING_TARGET_POSITION_VALID=1", "UAV_LANDING_TARGET_FRAME=12",
                  "UAV_LANDING_TARGET_X=10", "UAV_LANDING_TARGET_Y=0", "UAV_LANDING_TARGET_Z=7",
                  "UAV_LANDING_TARGET_DISTANCE=10", "UAV_LANDING_TARGET_SIZE_X=0",
                  "UAV_LANDING_TARGET_SIZE_Y=0", f"UAV_LANDING_TARGET_Q_W={math.cos(math.pi/6)}",
                  "UAV_LANDING_TARGET_Q_X=0", "UAV_LANDING_TARGET_Q_Y=0", "UAV_LANDING_TARGET_Q_Z=0.5",
                  # These simulator values must never affect received-data reconstruction.
                  "TARGET_NAV_X=999", "TARGET_NAV_Y=999", "TARGET_NAV_HEADING=0")

    def test_direct_exposure_position_not_latest_aircraft_position(self):
        self.launch("observation_pose_samples_var=TEST_POSES")
        self.packet()
        self.poke(f"TEST_POSES:=10000000,0,0,0,0,3,8,{time.time()},10000000",
                  "UAV_LANDING_TARGET_TIME_USEC=10000000")
        points = self.points()
        rectangle = points[:2] + points[3:]
        self.assertAlmostEqual(sum(p[0] for p in rectangle)/4, 0, delta=0.01)
        # Spherical displacement followed by ellipsoidal UTM projection at equator.
        self.assertAlmostEqual(sum(p[1] for p in rectangle)/4, 9.929, delta=0.01)
        self.poke("UAV_LAT=1", "UAV_LON=4", "UAV_YAW=180")
        self.assertEqual(points, self.points())  # Newer unrelated navigation cannot move it.
        self.poke("TEST_POSES:=invalid")
        self.assertIn("active=false", self.specs()[-1])

    def specs(self):
        specs = []
        for path in self.directory.glob("LOG_probe*/*.alog"):
            for line in path.read_text().splitlines():
                fields = line.split(None, 3)
                if len(fields) == 4 and fields[1] == "VIEW_POLYGON" and fields[2] == "pLandingTargetViewer":
                    specs.append(fields[3].strip())
        return specs

    def node_reports(self):
        reports = []
        for path in self.directory.glob("LOG_probe*/*.alog"):
            for line in path.read_text().splitlines():
                fields = line.split(None, 3)
                if len(fields) == 4 and fields[1] == "NODE_REPORT" and fields[2] == "pLandingTargetViewer":
                    reports.append(dict(part.split("=", 1) for part in fields[3].strip().split(",")))
        return reports

    def points(self):
        spec = self.specs()[-1]
        self.assertIn("active=true", spec)
        self.assertIn("label_color=invisible", spec)
        return [tuple(map(float, pair.split(",")))
                for pair in spec.split("pts={", 1)[1].split("}", 1)[0].split(":")]

    def assert_house(self, points, length, width):
        self.assertEqual(len(points), 5)
        # Three original sides are unchanged; the forward side is absent.
        for a, b, size in ((0, 1, length), (3, 4, length), (4, 0, width)):
            self.assertAlmostEqual(math.dist(points[a], points[b]), size, delta=0.04)
        removed = tuple(b-a for a, b in zip(points[1], points[3]))
        midpoint = tuple((a+b)/2 for a, b in zip(points[1], points[3]))
        roof = tuple(b-a for a, b in zip(midpoint, points[2]))
        forward = tuple(b-a for a, b in zip(points[0], points[1]))
        self.assertGreater(sum(a*b for a, b in zip(roof, forward)), 0)
        self.assertAlmostEqual(math.dist(midpoint, points[2]), width/2, delta=0.03)
        for a, b in ((1, 2), (2, 3)):
            edge = tuple(y-x for x, y in zip(points[a], points[b]))
            cosine = abs(sum(x*y for x, y in zip(edge, removed))) / (
                math.hypot(*edge) * math.hypot(*removed))
            self.assertAlmostEqual(math.degrees(math.acos(cosine)), 45, delta=0.15)

    def test_relative_projection_without_origin_or_vehicle_position(self):
        self.launch("projection_mode=relative\n  relative_anchor_x=100\n  relative_anchor_y=200",
                    origin=False)
        self.packet(include_position=False)
        points = self.points()
        self.assert_house(points, length=2, width=2)
        base = [points[i] for i in (0, 1, 3, 4)]
        self.assertAlmostEqual(sum(p[0] for p in base) / 4, 105, delta=0.01)
        self.assertAlmostEqual(sum(p[1] for p in base) / 4, 208.66, delta=0.01)
        # Vehicle yaw 30 + target-relative quaternion yaw 60 = east.
        self.assertAlmostEqual(points[1][0] - points[0][0], 2, delta=0.01)
        self.assertAlmostEqual(points[1][1] - points[0][1], 0, delta=0.01)
        vehicle = self.node_reports()[-1]
        self.assertEqual(vehicle["NAME"], "uav")
        self.assertEqual(vehicle["TYPE"], "quadcopter")
        self.assertAlmostEqual(float(vehicle["X"]), 100, delta=0.001)
        self.assertAlmostEqual(float(vehicle["Y"]), 200, delta=0.001)
        self.assertAlmostEqual(float(vehicle["HDG"]), 30, delta=0.01)

    def test_position_quaternion_and_configured_size(self):
        self.launch()
        self.assertIn("active=false", self.specs()[-1])
        self.packet()
        points = self.points()
        self.assert_house(points, length=2, width=2)
        base = [points[i] for i in (0, 1, 3, 4)]
        self.assertAlmostEqual(sum(p[0] for p in base)/4, 5, delta=0.1)
        self.assertAlmostEqual(sum(p[1] for p in base)/4, 8.60, delta=0.1)
        self.assertAlmostEqual(points[2][0], 7, delta=0.1)
        self.assertAlmostEqual(points[2][1], 8.60, delta=0.1)
        # Vehicle yaw 30 + target-relative quaternion yaw 60 = true east.
        self.assertGreater(points[1][0] - points[0][0], 1.95)
        self.assertAlmostEqual(points[1][1] - points[0][1], 0, delta=0.01)

    def test_tilt_rotates_vertical_vector_and_full_quaternion(self):
        self.launch()
        self.packet()
        # Roll 20, pitch -10, yaw 30: use independently evaluated R^T*(N,E,D).
        roll, pitch, yaw = map(math.radians, (20, -10, 30))
        cr, sr, cp, sp, cy, sy = (math.cos(roll), math.sin(roll), math.cos(pitch),
                                math.sin(pitch), math.cos(yaw), math.sin(yaw))
        matrix = ((cp*cy, sr*sp*cy-cr*sy, cr*sp*cy+sr*sy),
                  (cp*sy, sr*sp*sy+cr*cy, cr*sp*sy-sr*cy), (-sp, sr*cp, cr*cp))
        body = tuple(sum(matrix[j][i]*v for j, v in enumerate((10, 3, 7))) for i in range(3))
        # Quaternion inverse aircraft attitude times level target yaw 90.
        cr, sr, cp, sp, cy, sy = (math.cos(roll/2), math.sin(roll/2), math.cos(pitch/2),
                                math.sin(pitch/2), math.cos(yaw/2), math.sin(yaw/2))
        w, x, y, z = (cr*cp*cy+sr*sp*sy, -(sr*cp*cy-cr*sp*sy),
                      -(cr*sp*cy+sr*cp*sy), -(cr*cp*sy-sr*sp*cy))
        c, s = math.cos(math.pi/4), math.sin(math.pi/4)
        q = (w*c-z*s, x*c+y*s, y*c-x*s, w*s+z*c)
        self.poke("UAV_ROLL=20", "UAV_PITCH=-10", "UAV_ATTITUDE_AGE=0",
                  *(f"UAV_LANDING_TARGET_{k}={v}" for k, v in zip(("X", "Y", "Z"), body)),
                  *(f"UAV_LANDING_TARGET_Q_{k}={v}" for k, v in zip(("W", "X", "Y", "Z"), q)))
        points = self.points()
        self.assert_house(points, length=2, width=2)
        base = [points[i] for i in (0, 1, 3, 4)]
        self.assertAlmostEqual(sum(p[0] for p in base)/4, 3, delta=0.02)
        self.assertAlmostEqual(sum(p[1] for p in base)/4, 9.93, delta=0.03)
        self.assertAlmostEqual(points[1][0]-points[0][0], 2, delta=0.03)
        self.assertAlmostEqual(points[1][1]-points[0][1], 0, delta=0.03)
        self.poke("UAV_ATTITUDE_AGE=99")
        self.assertIn("active=false", self.specs()[-1])
        self.poke("UAV_ATTITUDE_AGE=0", "UAV_ATTITUDE_AVAILABLE=0")
        self.assertIn("active=false", self.specs()[-1])

    def test_received_angular_dimensions_and_missing_quaternion_fallback(self):
        self.launch("fallback_heading=45")
        self.packet()
        self.poke(f"UAV_LANDING_TARGET_SIZE_X={2*math.atan(3/20)}",
                  f"UAV_LANDING_TARGET_SIZE_Y={2*math.atan(2/20)}")
        points = self.points()
        self.assert_house(points, length=2, width=3)
        self.poke("UAV_LANDING_TARGET_Q_W=0", "UAV_LANDING_TARGET_Q_Z=0")
        points = self.points()
        self.assert_house(points, length=2, width=3)
        dx, dy = points[1][0]-points[0][0], points[1][1]-points[0][1]
        self.assertAlmostEqual(math.degrees(math.atan2(dx, dy)), 45, delta=0.1)

    def test_roof_map_angles_with_target_tilt_and_mirrored_projection(self):
        self.launch()
        self.packet()
        # Relative yaw 60, pitch 60: true target yaw east, projected length 1.
        self.poke("UAV_LANDING_TARGET_Q_W=0.75", "UAV_LANDING_TARGET_Q_X=-0.25",
                  f"UAV_LANDING_TARGET_Q_Y={math.sqrt(3)/4}",
                  f"UAV_LANDING_TARGET_Q_Z={math.sqrt(3)/4}")
        self.assert_house(self.points(), length=1, width=2)
        # Relative yaw 60, roll 180 mirrors winding, but the roof stays forward.
        self.poke("UAV_LANDING_TARGET_Q_W=0",
                  f"UAV_LANDING_TARGET_Q_X={math.sqrt(3)/2}",
                  "UAV_LANDING_TARGET_Q_Y=0.5", "UAV_LANDING_TARGET_Q_Z=0")
        points = self.points()
        self.assert_house(points, length=2, width=2)
        self.assertAlmostEqual(points[2][0], 7, delta=0.1)
        self.assertAlmostEqual(points[2][1], 8.60, delta=0.1)

    def test_actual_bridge_age_validity_and_frame_gate_the_display(self):
        self.launch()
        self.packet()
        self.points()
        self.poke("UAV_LANDING_TARGET_AGE=31")  # All other relay fields still present.
        self.assertIn("active=false", self.specs()[-1])
        self.poke("UAV_LANDING_TARGET_AGE=0", "UAV_LANDING_TARGET_POSITION_VALID=0")
        self.assertIn("active=false", self.specs()[-1])
        self.poke("UAV_LANDING_TARGET_POSITION_VALID=1", "UAV_LANDING_TARGET_FRAME=8")
        self.assertIn("active=false", self.specs()[-1])
        self.poke("UAV_LANDING_TARGET_FRAME=12", "UAV_LANDING_TARGET_AVAILABLE=0")
        self.assertIn("active=false", self.specs()[-1])

    def test_frozen_age_expires_in_real_seconds_with_time_warp(self):
        self.launch(warp=5, timeout=0.5)
        self.packet()
        time.sleep(0.7)
        specs = self.specs()
        self.assertTrue(any("active=true" in spec for spec in specs))
        self.assertIn("active=false", specs[-1])


if __name__ == "__main__":
    unittest.main()
