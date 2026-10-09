#!/usr/bin/env python3
"""Isolated config-generation checks for PEARL's pre-recovery circular loiter."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]


class LaunchTest(unittest.TestCase):
    def generate(self, *args):
        with tempfile.TemporaryDirectory(prefix="landing-launch-") as temp:
            directory = Path(temp)
            for path in MISSION.iterdir():
                if path.name.startswith("meta_") or path.name in ("launch.sh", "plug_origin_warp.moos"):
                    shutil.copy2(path, directory / path.name)
            result = subprocess.run(["bash", "launch.sh", "-j", *args], cwd=directory,
                                    capture_output=True, text=True, check=False)
            generated = {p.name: p.read_text() for p in directory.glob("targ_*")}
            return result, generated

    def test_default_circle_is_west_and_stops_for_recovery(self):
        result, files = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        behavior = files["targ_pearl.bhv"]
        self.assertIn("Behavior = BHV_Loiter", behavior)
        self.assertIn("x=-15, y=-115, radius=15, pts=12", behavior)
        self.assertIn("clockwise = true", behavior)
        loiter = behavior.split("Behavior = BHV_Loiter", 1)[1].split("}", 1)[0]
        self.assertIn("condition = RENDEZVOUS_OWN_ACTIVE=false", loiter)
        self.assertIn("(RENDEZVOUS_OWN_PHASE=IDLE) or (RENDEZVOUS_OWN_PHASE=STARTING)", loiter)
        self.assertNotIn("RENDEZVOUS_OWN_PHASE=COMPLETE", loiter)
        self.assertIn("-15,-130,270", files["targ_pearl.moos"])
        self.assertIn("Run = pLandingTargetViewer", files["targ_uav.moos"])
        self.assertIn("bridge = src=VIEW_POLYGON", files["targ_uav.moos"])

    def test_custom_geometry(self):
        result, files = self.generate("--pearl_x=-25", "--pearl_radius=20", "--north=-20", "--south=-160")
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertIn("x=-25, y=-90, radius=20", files["targ_pearl.bhv"])
        self.assertIn("-25,-110,270", files["targ_pearl.moos"])

    def test_invalid_geometry_is_rejected(self):
        for args in (("--pearl_radius=4",), ("--pearl_radius=30",),
                     ("--pearl_speed=0.6",), ("--north=-200",)):
            with self.subTest(args=args):
                result, files = self.generate(*args)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(files)


if __name__ == "__main__":
    unittest.main()
