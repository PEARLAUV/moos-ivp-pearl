#!/usr/bin/env python3
"""Configuration contract for PEARL's physical, non-propelled GRASS mode."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]


def generate(mode: str) -> str:
    nsplug = shutil.which("nsplug")
    if not nsplug:
        raise unittest.SkipTest("nsplug is not on PATH")
    with tempfile.TemporaryDirectory(prefix=f"pearl-{mode.lower()}-") as directory:
        target = Path(directory) / "targ_pearl.moos"
        values = {
            "WARP": "1",
            "IP_ADDR": "localhost",
            "MOOS_PORT": "9002",
            "PSHARE_PORT": "9202",
            "SHORE_IP": "127.0.0.1",
            "SHORE_PSHARE": "9200",
            "DIRECT_PEER": "true",
            "UAV_IP": "127.0.0.1",
            "UAV_PSHARE": "9201",
            "VNAME": "pearl",
            "COLOR": "red",
            "START_POS": "51.033305,-56.215822,0",
            "SPEED": "0.5",
            "XMODE": mode,
            "MIN_STATE_TIME": "0",
            "PEARL_LIB_DIR": str(MISSION.parents[2] / "lib"),
            "SHERLOCK_METRICS_HOST": "192.0.2.1",
            "SHERLOCK_METRICS_PORT": "9273",
        }
        subprocess.run(
            [
                nsplug,
                str(MISSION / "meta_pearl.moos"),
                str(target),
                "--strict",
                "--force",
                "-x",
                f"--path={MISSION}",
                *(f"{name}={value}" for name, value in values.items()),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        return target.read_text()


class GrassModeContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.grass = generate("GRASS")
        cls.sim = generate("SIM")

    def test_keeps_fsm_and_coordination_without_motion_stack(self):
        for app in (
            "pFSMRuntime",
            "pTakeoffProcessor",
            "pRendezvous",
            "pLandingCoordinator",
        ):
            self.assertIn(f"Run = {app}", self.grass)
        for alias in ("uChecksTakeoff", "uChecksRecovery", "uChecksRecoveryPost"):
            self.assertIn(f"~ {alias}", self.grass)

        for app in (
            "pHelmIvP",
            "pRouteBuffer",
            "uSimMarineV22",
            "pMarinePIDV22",
            "pThrustMixer",
            "pPearlPID",
            "iPEARL",
        ):
            self.assertNotIn(f"Run = {app}", self.grass)
            self.assertNotIn(f"ProcessConfig = {app}", self.grass)
        self.assertNotIn("bridge = src=IVPHELM_STATE", self.grass)
        self.assertNotIn("bridge = src=ROUTE_BUFFER_VEHICLE_STATE", self.grass)

    def test_uses_one_physical_gps_and_no_synthetic_gps(self):
        self.assertIn("Run = iDualGPS           @ NewConsole = false ~ iSingleGPS", self.grass)
        self.assertIn("ProcessConfig = iSingleGPS", self.grass)
        self.assertIn("DUAL_GPS = false", self.grass)
        self.assertIn("PORT_1   = /dev/ttyUSB0", self.grass)
        self.assertIn("GPS_LATITUDE      -> NAV_LAT", self.grass)
        self.assertIn("GPS_HEADING_GPRMC -> NAV_COG", self.grass)
        self.assertIn("uGrassRuntimeInputs", self.grass)
        self.assertIn("event = var=MOOS_MANUAL_OVERRIDE, val=false", self.grass)
        self.assertIn("event = var=PEARL_ALLSTOP,        val=false", self.grass)
        self.assertNotIn("uSimGpsInputs", self.grass)
        self.assertNotIn("event = var=GPS_QUALITY", self.grass)

    def test_platform_fixture_is_shared_but_sim_gps_is_sitl_only(self):
        for generated in (self.grass, self.sim):
            self.assertIn("uSyntheticPlatformInputs", generated)
            self.assertIn("event = var=PEARL_BATTERY_SOC", generated)
            self.assertIn("event = var=PEARL_WIND_SPEED", generated)
        self.assertIn("uSimGpsInputs", self.sim)
        self.assertIn("event = var=GPS_QUALITY", self.sim)

    def test_preserves_rendezvous_and_platform_data_without_sitl_target_feed(self):
        self.assertIn(
            "src_name=NODE_REPORT_LOCAL, dest_name=NODE_REPORT, route=127.0.0.1:9201",
            self.grass,
        )
        self.assertIn("src_name=PEARL_WIND_SPEED, route=127.0.0.1:9201", self.grass)
        self.assertIn("src_name=MEDIATED_TX_UAV", self.grass)
        self.assertNotIn("dest_name=PEARL_NAV_", self.grass)
        self.assertIn("dest_name=PEARL_NAV_LAT", self.sim)

    def test_launch_contracts_are_mode_specific(self):
        launcher = (MISSION / "launch.sh").read_text()
        self.assertIn("launch.sh is SITL-only", launcher)
        self.assertNotIn('--mode=<SITL|REAL>', launcher)

        pearl_launcher = (MISSION / "launch_pearl.sh").read_text()
        self.assertIn('"$MODE" != "GRASS"', pearl_launcher)
        self.assertIn('if [ "$MODE" = "GRASS" ]', pearl_launcher)
        self.assertIn('rm -f "targ_${VNAME}.bhv"', pearl_launcher)


if __name__ == "__main__":
    unittest.main()
