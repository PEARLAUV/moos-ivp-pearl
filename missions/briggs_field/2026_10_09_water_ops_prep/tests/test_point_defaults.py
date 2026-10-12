"""Owned strict generation only: point defaults, split launchers and invalid CLI."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
ROOT = MISSION.parents[2]


class PointDefaultsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="point-defaults-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.mission = self.root / "missions/briggs_field/prep"
        self.mission.mkdir(parents=True)
        for path in MISSION.iterdir():
            if path.is_file() and path.suffix in {".sh", ".moos", ".bhv", ".parm", ".toml"}:
                shutil.copy2(path, self.mission / path.name)
        for name in ("pearl_fsm", "uav_fsm"):
            shutil.copytree(MISSION / name, self.mission / name)
        (self.root / "missions/uav_solo").mkdir()
        (self.root / "missions/uav_solo/briggs_test").symlink_to(ROOT / "missions/uav_solo/briggs_test")
        for name in ("scripts", "config"):
            (self.root / name).symlink_to(ROOT / name)

    def launch(self, script, *args, success=True):
        result = subprocess.run(["bash", script, "-j", *args], cwd=self.mission,
                                capture_output=True, text=True, timeout=30, check=False)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def read(self, name):
        return (self.mission / name).read_text()

    def test_top_defaults_and_custom_points(self):
        self.launch("launch.sh", "-ng")
        self.assertIn("initial_return_point = x=10.6,y=4.8", self.read("targ_shoreside.moos"))
        self.assertIn("initial_point = x=10,y=15", self.read("targ_shoreside.moos"))
        self.assertIn("points         = 10.6,4.8", self.read("targ_pearl.bhv"))
        for mode in ("SITL",):
            with self.subTest(mode=mode):
                self.launch("launch.sh", "-ng", f"--mode={mode}", "--pearl_return=-23.5,44",
                            "--uav_rally=8,-19")
                shore = self.read("targ_shoreside.moos")
                self.assertIn("initial_return_point = x=-23.5,y=44", shore)
                self.assertIn("initial_point = x=8,y=-19", shore)
                self.assertIn("left_context[pearl-return]", shore)
                self.assertIn("points         = -23.5,44", self.read("targ_pearl.bhv"))
                self.assertIn("return_update_var = PEARL_RETURN_UPDATE", self.read("targ_pearl.moos"))
                self.assertIn("points = 8,-19", self.read("targ_uav.bhv"))

    def test_split_launchers_and_legacy_alias(self):
        self.launch("launch_shoreside.sh", "-ng", "--pearl_return=12,34", "--uav_rally=-7,-9")
        self.assertIn("initial_return_point = x=12,y=34", self.read("targ_shoreside.moos"))
        self.assertIn("initial_point = x=-7,y=-9", self.read("targ_shoreside.moos"))
        self.launch("launch_pearl.sh", "--pearl_return=12,34")
        self.assertIn("points         = 12,34", self.read("targ_pearl.bhv"))
        self.launch("launch_uav.sh", "--uav_rally=-7,-9")
        self.assertIn("points = -7,-9", self.read("targ_uav.bhv"))
        self.launch("launch_pearl.sh", "--mode=REAL", "--pearl_return=12,34")
        self.launch("launch_uav.sh", "--mode=REAL", "--uav_rally=-7,-9")
        self.assertIn("points         = 12,34", self.read("targ_pearl.bhv"))
        self.assertIn("points = -7,-9", self.read("targ_uav.bhv"))
        self.launch("launch_shoreside.sh", "-ng", "--secondary_landing=6,7")
        self.assertIn("initial_point = x=6,y=7", self.read("targ_shoreside.moos"))

    def test_bad_values_fail_before_generation(self):
        for spec in ("nan,1", "1,inf", "1", "1,2,3", "100001,0", "0,-100001"):
            for script, option in (("launch.sh", "pearl_return"),
                                   ("launch.sh", "uav_rally"),
                                   ("launch_shoreside.sh", "pearl_return"),
                                   ("launch_shoreside.sh", "uav_rally"),
                                   ("launch_pearl.sh", "pearl_return"),
                                   ("launch_uav.sh", "uav_rally")):
                with self.subTest(script=script, option=option, spec=spec):
                    self.launch(script, f"--{option}={spec}", success=False)


if __name__ == "__main__":
    unittest.main()
