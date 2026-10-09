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

    def launch(self, config="", warp=1, timeout=30.0):
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        self.mission = self.directory / "test.moos"
        self.mission.write_text(f"""ServerHost = 127.0.0.1
ServerPort = {port}
Community = viewer_test
MOOSTimeWarp = {warp}
LatOrigin = 0
LongOrigin = 3
ProcessConfig = ANTLER
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

    def packet(self):
        self.poke("UAV_LAT=0", "UAV_LON=3", "UAV_YAW=30",
                  "UAV_LANDING_TARGET_AVAILABLE=1", "UAV_LANDING_TARGET_AGE=0",
                  "UAV_LANDING_TARGET_POSITION_VALID=1", "UAV_LANDING_TARGET_FRAME=12",
                  "UAV_LANDING_TARGET_X=10", "UAV_LANDING_TARGET_Y=0", "UAV_LANDING_TARGET_Z=7",
                  "UAV_LANDING_TARGET_DISTANCE=10", "UAV_LANDING_TARGET_SIZE_X=0",
                  "UAV_LANDING_TARGET_SIZE_Y=0", f"UAV_LANDING_TARGET_Q_W={math.cos(math.pi/6)}",
                  "UAV_LANDING_TARGET_Q_X=0", "UAV_LANDING_TARGET_Q_Y=0", "UAV_LANDING_TARGET_Q_Z=0.5",
                  # These simulator values must never affect received-data reconstruction.
                  "TARGET_NAV_X=999", "TARGET_NAV_Y=999", "TARGET_NAV_HEADING=0")

    def specs(self):
        specs = []
        for path in self.directory.glob("LOG_probe*/*.alog"):
            for line in path.read_text().splitlines():
                fields = line.split(None, 3)
                if len(fields) == 4 and fields[1] == "VIEW_POLYGON" and fields[2] == "pLandingTargetViewer":
                    specs.append(fields[3].strip())
        return specs

    def points(self):
        spec = self.specs()[-1]
        self.assertIn("active=true", spec)
        self.assertIn("label_color=invisible", spec)
        return [tuple(map(float, pair.split(",")))
                for pair in spec.split("pts={", 1)[1].split("}", 1)[0].split(":")]

    def test_position_quaternion_and_configured_size(self):
        self.launch()
        self.assertIn("active=false", self.specs()[-1])
        self.packet()
        points = self.points()
        self.assertEqual(len(points), 4)
        self.assertAlmostEqual(sum(p[0] for p in points)/4, 5, delta=0.1)
        self.assertAlmostEqual(sum(p[1] for p in points)/4, 8.60, delta=0.1)
        # Vehicle yaw 30 + target-relative quaternion yaw 60 = true east.
        self.assertGreater(points[1][0] - points[0][0], 1.95)
        self.assertAlmostEqual(points[1][1] - points[0][1], 0, delta=0.01)
        for a, b in zip(points, points[1:] + points[:1]):
            self.assertAlmostEqual(math.dist(a, b), 2, delta=0.03)

    def test_received_angular_dimensions_and_missing_quaternion_fallback(self):
        self.launch("fallback_heading=45")
        self.packet()
        self.poke(f"UAV_LANDING_TARGET_SIZE_X={2*math.atan(3/20)}",
                  f"UAV_LANDING_TARGET_SIZE_Y={2*math.atan(2/20)}")
        points = self.points()
        self.assertAlmostEqual(math.dist(points[0], points[1]), 2, delta=0.03)
        self.assertAlmostEqual(math.dist(points[1], points[2]), 3, delta=0.04)
        self.poke("UAV_LANDING_TARGET_Q_W=0", "UAV_LANDING_TARGET_Q_Z=0")
        points = self.points()
        dx, dy = points[1][0]-points[0][0], points[1][1]-points[0][1]
        self.assertAlmostEqual(math.degrees(math.atan2(dx, dy)), 45, delta=0.1)

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
