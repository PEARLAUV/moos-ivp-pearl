#!/usr/bin/env python3
"""Static ordered-profile contract; not real sensor or flight validation."""

import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
ROOT = MISSION.parents[2]


def parameters(mode="SITL"):
    values = {}
    for path in (ROOT / "config/ardupilot/drones/x500.parm",
                 MISSION / (mode.lower() + ".parm"), MISSION / "mission.parm"):
        if not path.exists():
            continue
        for line in path.read_text().splitlines():
            fields = line.split("#", 1)[0].split()
            if fields:
                if len(fields) != 2:
                    raise AssertionError(f"Malformed parameter entry: {line}")
                values[fields[0]] = float(fields[1])
    return values


class RangefinderConfigTest(unittest.TestCase):
    def test_native_sitl_backend_and_downward_beam(self):
        values = parameters()
        self.assertEqual(values["RNGFND1_TYPE"], 100)
        self.assertEqual(values["RNGFND1_ORIENT"], 25)
        self.assertEqual(values["SIM_SONAR_ROT"], 25)

    def test_user_approved_sitl_zero_overrides(self):
        values = parameters()
        self.assertEqual(values["RNGFND1_MIN"], 0)
        self.assertEqual(values["RNGFND1_GNDCLR"], 0)
        self.assertEqual(values["RNGFND1_MAX"], 12)

    def test_real_keeps_hardware_limits_and_has_no_simulation_parameters(self):
        values = parameters("REAL")
        self.assertEqual(values["RNGFND1_MIN"], 0.15)
        self.assertEqual(values["RNGFND1_GNDCLR"], 0.17)
        self.assertEqual(values["RNGFND1_MAX"], 12)
        self.assertNotIn("RNGFND1_TYPE", values)  # Keep installed hardware driver.
        self.assertFalse(any(name.startswith("SIM_") for name in values))
        self.assertNotIn("RNGFND1_GNCLR", values)

    def test_camera_offsets_only_active_in_sitl(self):
        sitl = parameters("SITL")
        real = parameters("REAL")
        real_lines = (MISSION / "real.parm").read_text().splitlines()
        for axis, placeholder in (("X", "FORWARD"), ("Y", "RIGHT"), ("Z", "DOWN")):
            name = "PLND_CAM_POS_" + axis
            self.assertEqual(sitl[name], 0)
            self.assertNotIn(name, real)
            self.assertIn(f"# {name} <MEASURED_{placeholder}_M>", real_lines)

    def test_landing_offsets_only_active_in_sitl(self):
        sitl = parameters("SITL")
        real = parameters("REAL")
        real_lines = (MISSION / "real.parm").read_text().splitlines()
        for axis, placeholder in (("X", "FORWARD"), ("Y", "RIGHT")):
            name = "PLND_LAND_OFS_" + axis
            self.assertEqual(sitl[name], 0)
            self.assertNotIn(name, real)
            self.assertIn(f"# {name} <DESIRED_{placeholder}_CM>", real_lines)

    def test_shared_mission_precision_policy(self):
        for mode in ("REAL", "SITL"):
            values = parameters(mode)
            self.assertEqual(values["PLND_ALT_MIN"], 0)
            self.assertEqual(values["PLND_ALT_MAX"], 4)
            self.assertEqual(values["PLND_STRICT"], 2)
            self.assertEqual(values["PLND_ENABLED"], 1)
            self.assertEqual(values["PLND_TYPE"], 1)
            self.assertEqual(values["PLND_XY_DIST_MAX"], 0.3)
            self.assertEqual(values["PLND_RET_MAX"], 3)
            self.assertEqual(values["PLND_TIMEOUT"], 1)
            self.assertEqual(values["PLND_RET_BEHAVE"], 1)
            self.assertEqual(values["PLND_EST_TYPE"], 1)

    def test_telemetry_loss_timeout(self):
        for mode in ("REAL", "SITL"):
            self.assertEqual(parameters(mode)["FS_GCS_TIMEOUT"], 8)

    def test_approved_rtl_baseline_cone_and_landing(self):
        for mode in ("REAL", "SITL"):
            values = parameters(mode)
            self.assertEqual(values["RTL_ALT_M"], 15)
            self.assertEqual(values["RTL_ALT_TYPE"], 0)
            self.assertEqual(values["RTL_CLIMB_MIN_M"], 0)
            self.assertEqual(values["RTL_LOIT_TIME"], 10000)
            self.assertEqual(values["RTL_SPEED_MS"], 2)
            self.assertEqual(values["RTL_CONE_SLOPE"], 0)
            self.assertEqual(values["RTL_ALT_FINAL_M"], 0)
            self.assertEqual(values["RTL_OPTIONS"], 0)
            self.assertEqual(int(values["RTL_OPTIONS"]) & (1 << 2), 0)
            self.assertEqual(values["WP_YAW_BEHAVIOR"], 2)
            self.assertEqual(values["PLND_STRICT"], 2)
            self.assertEqual(values["RALLY_INCL_HOME"], 0)
            self.assertEqual(values["RALLY_LIMIT_KM"], 0)
            self.assertNotIn("RALLY_TOTAL", values)

    def test_user_selected_landing_speed_profile(self):
        for mode in ("REAL", "SITL"):
            values = parameters(mode)
            self.assertEqual(values["LAND_SPD_HIGH_MS"], 1)
            self.assertEqual(values["LAND_SPD_MS"], 0.4)
            self.assertGreaterEqual(values["LAND_SPD_MS"], 0.3)
            self.assertLessEqual(values["LAND_SPD_MS"], 2)
            self.assertEqual(values["LAND_ALT_LOW_M"], 2)

    def test_reposition_enabled_without_enabling_precision_resume(self):
        for mode in ("REAL", "SITL"):
            values = parameters(mode)
            self.assertEqual(values["LAND_REPOSITION"], 1)
            self.assertEqual(values["PLND_OPTIONS"], 1)
            self.assertEqual(int(values["PLND_OPTIONS"]) & (1 << 1), 0)
            self.assertEqual(values["PLND_STRICT"], 2)

    def test_no_competing_native_beacon(self):
        self.assertEqual(parameters()["SIM_PLD_ENABLE"], 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
