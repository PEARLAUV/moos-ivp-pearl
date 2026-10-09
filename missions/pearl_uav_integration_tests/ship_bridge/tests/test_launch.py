#!/usr/bin/env python3
"""Config-only checks; do not launch MOOS/SITL or touch other missions."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]


class LaunchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ship-bridge-config-")
        self.directory = Path(self.temp.name)
        for path in MISSION.iterdir():
            if path.is_file() and path.name not in ("README.md", ".gitignore"):
                shutil.copy2(path, self.directory / path.name)

    def tearDown(self):
        self.temp.cleanup()

    def generate(self, *flags):
        return subprocess.run([str(self.directory / "launch.sh"), "-j", *flags],
                              cwd=self.directory, capture_output=True, text=True, timeout=15, check=False)

    def read(self, name):
        return (self.directory / name).read_text()

    def test_defaults_and_idle_aircraft(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        shore = self.read("targ_shoreside.moos")
        self.assertIn("button_1 = ATTACH # SHIP_POSE_ENABLED_PEARL=true", shore)
        self.assertIn("button_2 = DETACH # SHIP_POSE_ENABLED_PEARL=false", shore)
        self.assertEqual(shore.count("button_"), 2)
        self.assertIn("qbridge = SHIP_POSE_ENABLED", shore)
        pearl = self.read("targ_pearl.moos")
        self.assertIn("bridge = src=SHIP_ATTACHMENT_REQUESTED", pearl)
        self.assertNotIn("bridge = src=SHIP_POSE_ENABLED", pearl)  # pShare would reject inbound command.
        self.assertIn("ship_altitude_msl = 1", pearl)
        self.assertIn("Behavior = BHV_Loiter", self.read("targ_pearl.bhv"))
        self.assertIn("initialize SHIP_POSE_ENABLED = false", self.read("targ_pearl.bhv"))
        self.assertNotIn("Behavior =", self.read("targ_uav.bhv"))
        self.assertIn("UAV_TEST_PHASE = IDLE", self.read("targ_uav.bhv"))
        self.assertIn("start_in_drive = false", self.read("targ_uav.moos"))
        for forbidden in ("pRendezvous", "pLandingCoordinator", "pMavlinkLandingTarget",
                          "ARM_UAV", "DO_TAKEOFF", "AUTOLAND", "FLY_WAYPOINT"):
            for name in ("targ_uav.moos", "targ_pearl.moos", "targ_shoreside.moos"):
                self.assertNotIn(forbidden, self.read(name))

    def test_geometry_ports_warp_and_headless(self):
        result = self.generate("-ng", "3", "--center_x=-10", "--center_y=-100",
                               "--loiter_radius=20", "--pearl_speed=0.3", "--deck_altitude=1.5",
                               "--shore_mport=9100", "--pearl_mport=9102", "--uav_mport=9101",
                               "--sitl_target_port=5863", "--sitl_port=5860")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("MOOSTimeWarp = 3", self.read("targ_uav.moos"))
        self.assertIn("start_pos = -10,-120,270", self.read("targ_pearl.moos"))
        self.assertIn("ship_altitude_msl = 1.5", self.read("targ_pearl.moos"))
        self.assertIn("mavlink_port = 5863", self.read("targ_pearl.moos"))
        self.assertIn("url = 127.0.0.1:5860", self.read("targ_uav.moos"))
        self.assertIn("speed = 0.3", self.read("targ_pearl.bhv"))
        self.assertNotIn("Run = moos-vnc-session", self.read("targ_shoreside.moos"))
        self.assertFalse((self.directory / "LOG_sitl").exists())
        self.assertNotIn("PEARL_ROOT", self.read("launch.sh"))
        self.assertNotIn("PATH=", self.read("launch.sh"))
        self.assertEqual(os.access(self.directory / "launch.sh", os.X_OK), True)

    def test_rejects_invalid_geometry_ports_and_flags(self):
        for flags in (("--pearl_speed=1",), ("--deck_altitude=-1",), ("--loiter_radius=1",),
                      ("--uav_x=nan",), ("--uav_mport=9000",), ("--sitl_port=70000",),
                      ("--typo",), ("0",)):
            with self.subTest(flags=flags):
                self.assertNotEqual(self.generate(*flags).returncode, 0)


if __name__ == "__main__":
    unittest.main()
