#!/usr/bin/env python3
"""Stage successive operator routes through the real Helm after landing parks it."""

import os
import re
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
ROOT = MISSION.parents[2]
MOOS = ROOT.parent / "moos-ivp/bin"


class RouteRestageTest(unittest.TestCase):
    def test_second_route_ready_without_flight_control_handoff(self):
        source = (MISSION / "meta_uav.moos").read_text()
        route = re.search(r"ProcessConfig = pRouteBuffer\s*\{[^}]+\}", source)
        route_clear = re.search(r"ProcessConfig = uRouteClear\s*\{[^}]+\}", source)
        if route is None or route_clear is None:
            self.fail("Missing route buffer or route-clear configuration")
        self.assertNotIn("uRouteStageHelm", source)
        runtime = (MISSION / "uav_fsm/plug_uav_fsm_runtime.moos").read_text()
        with tempfile.TemporaryDirectory(prefix="route-restage-") as directory:
            root = Path(directory)
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            behavior = root / "route.bhv"
            subprocess.run(
                [str(MOOS / "nsplug"), str(MISSION / "meta_uav.bhv"), str(behavior),
                 "--strict", "--force", "-x", "SECONDARY_LANDING_X=10",
                 "SECONDARY_LANDING_Y=15"],
                check=True, capture_output=True, text=True,
            )
            mission = root / "route.moos"
            mission.write_text(f"""ServerHost=localhost
ServerPort={port}
Community=uav
LatOrigin=42.358436
LongOrigin=-71.087448
ProcessConfig=ANTLER
{{
  MSBetweenLaunches=100
  Run = {MOOS / 'MOOSDB'} @ NewConsole=false
  Run = {ROOT / 'bin/pRouteBuffer'} @ NewConsole=false ~ pRouteBuffer
  Run = {MOOS / 'pHelmIvP'} @ NewConsole=false
  Run = {ROOT / 'bin/pFSMRuntime'} @ NewConsole=false
  Run = {MOOS / 'uTimerScript'} @ NewConsole=false ~ uRouteClear
}}
ProcessConfig=pHelmIvP
{{
  AppTick=10
  CommsTick=10
  behaviors={behavior}
  ivp_behavior_dir={ROOT / 'lib'}
  start_in_drive=true
  park_on_allstop=false
  Domain=course:0:359:360
  Domain=speed:0:5:51
  Domain=altitude:0:30:61
}}
""" + route.group() + "\n" + route_clear.group()
                                     + "\nProcessConfig=pFSMRuntime\n{\nAppTick=10\nCommsTick=10\n"
                                     + runtime + "\n}\n")
            with (root / "antler.log").open("w") as log:
                antler = subprocess.Popen([str(MOOS / "pAntler"), str(mission)],
                                          cwd=root, stdout=log, stderr=subprocess.STDOUT,
                                          start_new_session=True)
                try:
                    def poke(**values):
                        assignments = [
                            f"{key}:={value}" if isinstance(value, str) else f"{key}={value}"
                            for key, value in values.items()
                        ]
                        subprocess.run([str(MOOS / "uPokeDB"), str(mission), "--quiet",
                                        *assignments], cwd=root, capture_output=True,
                                       timeout=10, check=True)

                    def query(condition):
                        result = subprocess.run([str(MOOS / "uQueryDB"), str(mission), "--wait=5",
                                                 f"--condition={condition}",
                                                 "--check_var=IVPHELM_STATE",
                                                 "--check_var=ROUTE_READY",
                                                 "--check_var=ROUTE_BUFFER_VEHICLE_STATE",
                                                 "--check_var=ROUTE_DEPLOY",
                                                 "--check_var=ROUTE_CLEAR"],
                                                cwd=root, capture_output=True, text=True,
                                                timeout=15, check=False)
                        self.assertEqual(result.returncode, 0,
                                         result.stdout + result.stderr
                                         + ((root / ".checkvars").read_text() if (root / ".checkvars").exists() else "")
                                         + (root / "antler.log").read_text())

                    poke(UAV_LANDED_STATE="ON_GROUND", MOOS_MANUAL_OVERRIDE="true",
                         AUTOPILOT_MODE="HELM_PARKED", HELM_STATUS="false", ARDU_COMMAND="NONE",
                         NAV_X=0, NAV_Y=0, NAV_HEADING=0, NAV_SPEED=0, NAV_ALTITUDE=0)
                    query("(UAV_FSM_OPERATION_STATE=IDLE) and (IVPHELM_STATE=PARK)")
                    for route_number, points in enumerate(
                            ("{10,10:20,20}", "{30,30:40,40}", "{50,50:60,60}"), 1):
                        # Exercise the generated entry effect, not a simulated state publication.
                        poke(TAKEOFF_PRECHECKS="pending", TASK_EXECUTE="true")
                        query("(UAV_FSM_OPERATION_STATE=TAKEOFF_OPERATIONS) and (MOOS_MANUAL_OVERRIDE=false)")
                        # ENTER_GUIDED is a legitimate takeoff-readiness effect. Reset
                        # the sentinel so this assertion covers route staging only.
                        poke(ARDU_COMMAND="NONE")
                        poke(ROUTE_BUFFER_COMMAND=(
                            f"action=deploy # task_id=restage-{route_number} # "
                            f"task_time={time.time():.6f} # points={points}"
                        ))
                        query("ROUTE_BUFFER_VEHICLE_STATE=DEPLOY_ACCEPTED")
                        query("ROUTE_READY=true")
                        query("((AUTOPILOT_MODE=HELM_PARKED) and (HELM_STATUS=false)) and (ARDU_COMMAND=NONE)")
                        query("ROUTE_DEPLOY=false")
                        poke(MOOS_MANUAL_OVERRIDE="true")
                        # Entry is one-shot: an override issued during takeoff is not cleared again.
                        query("(IVPHELM_STATE=PARK) and (MOOS_MANUAL_OVERRIDE=true)")
                        poke(TAKEOFF_PRECHECKS="fail")
                        query("UAV_FSM_OPERATION_STATE=IDLE")
                        # IDLE's generated entry effect requests the authoritative clear.
                        query("(ROUTE_BUFFER_VEHICLE_STATE=CLEAR_ACCEPTED) and "
                              "((ROUTE_READY=false) and (ROUTE_CLEAR=false))")
                        # CLEAR legitimately posts HOLD_POSITION; reset the fixture
                        # sentinel before checking that restaging sends no new command.
                        poke(ARDU_COMMAND="NONE")
                    # A rejected operator snapshot must not wake the Helm during recovery.
                    poke(TAKEOFF_PRECHECKS="pending", TASK_EXECUTE="true",
                         ROUTE_READY="true")
                    query("UAV_FSM_TAKEOFF_OPERATIONS_STATE=ESTABLISHING_TAKEOFF_READINESS")
                    poke(TAKEOFF_PRECHECKS="success")
                    query("UAV_FSM_TAKEOFF_OPERATIONS_STATE=REQUESTING_TAKEOFF_CLEARANCE")
                    poke(TAKEOFF_REQUEST_RESULT="APPROVED", UAV_LANDED_STATE="IN_AIR",
                         UAV_IS_ARMED=1, TAKEOFF_AIRBORNE="success")
                    query("UAV_FSM_OPERATION_STATE=MISSION_EXECUTION")
                    poke(RETURN="true", MOOS_MANUAL_OVERRIDE="true")
                    query("(UAV_FSM_OPERATION_STATE=LANDING_OPERATIONS) and (IVPHELM_STATE=PARK)")
                    poke(ROUTE_BUFFER_COMMAND="action=deploy # points={70,70:80,80}")
                    query("ROUTE_BUFFER_VEHICLE_STATE=DEPLOY_REJECTED_AUTHORITY")
                    query("(IVPHELM_STATE=PARK) and (ROUTE_READY=false)")
                finally:
                    if antler.poll() is None:
                        os.killpg(antler.pid, signal.SIGTERM)
                        try:
                            antler.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            os.killpg(antler.pid, signal.SIGKILL)
                            antler.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
