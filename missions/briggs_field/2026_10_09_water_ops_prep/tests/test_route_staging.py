#!/usr/bin/env python3
"""Real Helm staging regression: no flight controller, isolated DB and processes."""

import os
import re
import signal
import socket
import subprocess
import tempfile
import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
ROOT = MISSION.parents[2]
MOOS = ROOT.parent / "moos-ivp/bin"


class RouteStagingTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="prep-route-staging-")
        self.directory = Path(self.temp.name)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        behaviors = (MISSION / "meta_uav.bhv").read_text()
        behaviors = behaviors.replace("$(SECONDARY_LANDING_X)", "10").replace(
            "$(SECONDARY_LANDING_Y)", "15"
        )
        bhv = self.directory / "test.bhv"
        bhv.write_text(behaviors)
        route_config = re.search(
            r"ProcessConfig = pRouteBuffer\s*\{.*?\n\}",
            (MISSION / "meta_uav.moos").read_text(),
            re.DOTALL,
        )
        assert route_config is not None
        self.mission = self.directory / "test.moos"
        self.mission.write_text(
            f"""ServerHost=127.0.0.1
ServerPort={port}
Community=uav
LatOrigin=42.3569186052
LongOrigin=-71.0990291116
ProcessConfig=ANTLER
{{
  MSBetweenLaunches=100
  Run={MOOS / "MOOSDB"} @ NewConsole=false
  Run={MOOS / "pHelmIvP"} @ NewConsole=false
  Run={ROOT / "bin/pRouteBuffer"} @ NewConsole=false
  Run={ROOT / "bin/uChecks"} @ NewConsole=false
}}
ProcessConfig=pHelmIvP
{{
  AppTick=20
  CommsTick=20
  behaviors={bhv}
  start_in_drive=false
  park_on_allstop=false
  Domain=course:0:359:360
  Domain=speed:0:5:51
  Domain=altitude:0:30:61
}}
ProcessConfig=uChecks
{{
  AppTick=20
  CommsTick=20
  result_var=TEST_FLIGHT_PLAN
  result_prefix=TEST_PLAN_
  condition=TEST_CHECK=true
  check=name=FLIGHT_PLAN,timeout=10
  require=check=FLIGHT_PLAN,var=ROUTE_READY,type=string,equals=true,on_mismatch=wait
  require=check=FLIGHT_PLAN,var=UAV_RALLY_READY,type=double,equals=1,on_mismatch=wait
}}
"""
            + route_config.group()
        )
        self.output = (self.directory / "antler.log").open("w")
        self.process = subprocess.Popen(
            [str(MOOS / "pAntler"), str(self.mission)],
            cwd=self.directory,
            stdout=self.output,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        self.addCleanup(self.cleanup)
        self.query("IVPHELM_STATE=PARK", wait=8)
        self.poke(
            "UAV_FSM_OPERATION_STATE:=IDLE",
            "NAV_X=0",
            "NAV_Y=0",
            "NAV_SPEED=0",
            "NAV_HEADING=0",
            "NAV_ALTITUDE=8",
            "TEST_CHECK:=false",
        )

    def cleanup(self):
        if self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=4)
        self.output.close()
        self.temp.cleanup()

    def poke(self, *values):
        subprocess.run(
            [str(MOOS / "uPokeDB"), str(self.mission), "--quiet", *values],
            cwd=self.directory,
            capture_output=True,
            check=True,
            timeout=12,
        )

    def query(self, *conditions, expected=0, wait=3):
        result = subprocess.run(
            [
                str(MOOS / "uQueryDB"),
                str(self.mission),
                f"--wait={wait}",
                *(f"--condition={v}" for v in conditions),
                "--check_var=DESIRED_SPEED", "--check_var=DESIRED_HEADING",
                "--check_var=ROUTE_DEPLOY", "--check_var=ROUTE_CLEAR",
                "--check_var=IVPHELM_ALLSTOP", "--esv",
            ],
            cwd=self.directory,
            capture_output=True,
            text=True,
            check=False,
            timeout=wait + 10,
        )
        checkvars = self.directory / ".checkvars"
        self.assertEqual(
            result.returncode,
            expected,
            result.stdout + result.stderr
            + (checkvars.read_text() if checkvars.exists() else "")
            + (self.directory / "antler.log").read_text(),
        )

    def test_parked_snapshot_spawns_only_after_real_drive_and_checks_both_facts(self):
        self.poke("ROUTE_BUFFER_COMMAND:=action=deploy # points={20,0:40,0}")
        self.query("ROUTE_BUFFER_VEHICLE_STATE=DEPLOY_ACCEPTED", "IVPHELM_STATE=PARK")
        self.query("ROUTE_READY=true", expected=1, wait=1)
        self.poke(
            "MOOS_MANUAL_OVERRIDE:=false",
            "UAV_FSM_OPERATION_STATE:=TAKEOFF_OPERATIONS",
            "UAV_RALLY_READY=0",
            "TEST_CHECK:=true",
        )
        self.query("IVPHELM_STATE=DRIVE", "ROUTE_READY=true")
        self.query("TEST_FLIGHT_PLAN=pending")
        self.poke("UAV_RALLY_READY=1")
        self.query("TEST_FLIGHT_PLAN=success")

    def test_current_leg_lead_and_final_only_slowdown(self):
        # Real Helm with controlled navigation; no bridge or flight controller.
        self.poke(
            "UAV_FSM_OPERATION_STATE:=MISSION_EXECUTION",
            "ROUTE_BUFFER_COMMAND:=action=deploy # points={0,20:20,20}",
        )
        self.query("ROUTE_BUFFER_VEHICLE_STATE=DEPLOY_ACCEPTED")
        self.poke("MOOS_MANUAL_OVERRIDE:=false")
        self.query("IVPHELM_STATE=DRIVE", "ROUTE_READY=true")
        self.poke("ROUTE_DEPLOY:=true")
        self.query("DESIRED_SPEED>2.9", "DESIRED_HEADING<1")

        # First-leg lead points to (0,10), not straight to the waypoint (0,20).
        self.poke("NAV_X=2", "NAV_Y=5", "NAV_SPEED=3")
        self.query("DESIRED_HEADING>337", "DESIRED_HEADING<340", "DESIRED_SPEED>2.9")

        # Near an intermediate point: 5 m to it + 20 m next leg, so no slowdown.
        self.poke("NAV_X=0", "NAV_Y=15")
        self.query("DESIRED_HEADING<1", "DESIRED_SPEED>2.9")
        # At 2 m, the current 4 m fly-by capture advances onto the next leg.
        self.poke("NAV_Y=18")
        self.query("DESIRED_HEADING>67", "DESIRED_HEADING<70", "DESIRED_SPEED>2.9")

        # Six metres remaining: 3 m/s * (6/8) = 2.25 m/s, outside 4 m capture.
        self.poke("NAV_X=14", "NAV_Y=20")
        self.query("DESIRED_HEADING>89", "DESIRED_HEADING<91",
                   "DESIRED_SPEED>2.1", "DESIRED_SPEED<2.4")

        # Final capture retains route cleanup but requests measured brake/hold.
        self.poke("NAV_X=19")
        self.query("ROUTE_DEPLOY=false", "ROUTE_READY=false",
                   "ARDU_COMMAND=BRAKE_AND_HOLD")

    def test_clear_while_parked_cannot_resurrect_deferred_route(self):
        self.poke("ROUTE_BUFFER_COMMAND:=action=deploy # points={20,0:40,0}")
        self.query("ROUTE_BUFFER_VEHICLE_STATE=DEPLOY_ACCEPTED")
        self.poke("ROUTE_BUFFER_COMMAND:=action=clear")
        self.query("ROUTE_BUFFER_VEHICLE_STATE=CLEAR_ACCEPTED")
        self.poke("MOOS_MANUAL_OVERRIDE:=false")
        self.query("IVPHELM_STATE=DRIVE")
        self.query("ROUTE_READY=true", expected=1, wait=1)


if __name__ == "__main__":
    unittest.main()
