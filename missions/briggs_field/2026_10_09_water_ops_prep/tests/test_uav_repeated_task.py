"""Real UAV FSM/Helm/buffer lifecycle; synthetic checks/navigation, no FC or flight executor."""

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


class UavRepeatedTaskTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="uav-repeated-task-")
        self.directory = Path(self.temp.name)
        self.process = None
        self.addCleanup(self.cleanup)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        source = (MISSION / "meta_uav.moos").read_text()
        route = re.search(r"ProcessConfig = pRouteBuffer\s*\{.*?\n\}", source, re.DOTALL)
        flight_plan = re.search(r"  check = name=FLIGHT_PLAN.*?(?=\n  check =)", source, re.DOTALL)
        sequence = re.search(r"ProcessConfig = uRouteSequence\s*\{[^}]+\}",
                             (MISSION / "plug_uav_ardupilot.moos").read_text())
        assert route and flight_plan and sequence
        behavior = self.directory / "test.bhv"
        subprocess.run([str(MOOS / "nsplug"), str(MISSION / "meta_uav.bhv"), str(behavior),
                        "--strict", "--force", "-x", "SECONDARY_LANDING_X=10",
                        "SECONDARY_LANDING_Y=15"], check=True, capture_output=True)
        self.mission = self.directory / "test.moos"
        self.mission.write_text(f"""ServerHost=127.0.0.1
ServerPort={port}
Community=uav
LatOrigin=42.3569186052
LongOrigin=-71.0990291116
ProcessConfig=ANTLER
{{
 MSBetweenLaunches=100
 Run={MOOS / 'MOOSDB'} @ NewConsole=false
 Run={ROOT / 'bin/pFSMRuntime'} @ NewConsole=false
 Run={MOOS / 'pHelmIvP'} @ NewConsole=false
 Run={ROOT / 'bin/pRouteBuffer'} @ NewConsole=false
 Run={ROOT / 'bin/uChecks'} @ NewConsole=false ~ uPretakeoffChecks
 Run={MOOS / 'uTimerScript'} @ NewConsole=false ~ uRouteSequence
}}
ProcessConfig=pFSMRuntime
{{
 AppTick=4
 CommsTick=4
{(MISSION / 'plug_uav_fsm_runtime.moos').read_text()}
}}
ProcessConfig=pHelmIvP
{{
 AppTick=4
 CommsTick=4
 behaviors={behavior}
 start_in_drive=false
 park_on_allstop=false
 Domain=course:0:359:360
 Domain=speed:0:5:51
 Domain=altitude:0:30:61
}}
ProcessConfig=uPretakeoffChecks
{{
 AppTick=4
 CommsTick=4
 condition=(UAV_FSM_TAKEOFF_OPERATIONS_STATE=ESTABLISHING_TAKEOFF_READINESS) or (UAV_FSM_TAKEOFF_OPERATIONS_STATE=REQUESTING_TAKEOFF_CLEARANCE)
 result_var=TAKEOFF_PRECHECKS
 result_prefix=TAKEOFF_PRECHECK_
 session_var=UAV_FSM_OPERATION_STATE_TAKEOFF_OPERATIONS_EPOCH
{flight_plan[0]}
}}
{route[0]}
{sequence[0]}
""")
        self.output = (self.directory / "antler.log").open("w")
        self.process = subprocess.Popen([str(MOOS / "pAntler"), str(self.mission)],
                                        cwd=self.directory, stdout=self.output,
                                        stderr=subprocess.STDOUT, start_new_session=True)
        self.query("UAV_FSM_OPERATION_STATE=IDLE", "IVPHELM_STATE=PARK")
        self.poke("NAV_X=0", "NAV_Y=0", "NAV_SPEED=0", "NAV_HEADING=90", "NAV_ALTITUDE=8",
                  "UAV_IS_ARMED=0", "UAV_LANDED_STATE:=ON_GROUND",
                  "UAV_RALLY_READY=1", "RALLY_TASK_STATE:=READY")

    def cleanup(self):
        if self.process and self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=4)
        if hasattr(self, "output"):
            self.output.close()
        self.temp.cleanup()

    def poke(self, *values):
        subprocess.run([str(MOOS / "uPokeDB"), str(self.mission), "--quiet", *values],
                       cwd=self.directory, capture_output=True, check=True, timeout=25)

    def query(self, *conditions):
        result = subprocess.run([str(MOOS / "uQueryDB"), str(self.mission), "--wait=5",
                                 *(f"--condition={c}" for c in conditions),
                                 "--check_var=ROUTE_BUFFER_VEHICLE_STATE",
                                 "--check_var=ROUTE_UPDATE", "--check_var=ROUTE_READY",
                                 "--check_var=TAKEOFF_PRECHECK_FLIGHT_PLAN", "--check_var=DESIRED_HEADING",
                                 "--check_var=IVPHELM_BHV_ACTIVE", "--check_var=UAV_FSM_OPERATION_STATE",
                                 "--check_var=ROUTE_DEPLOY", "--esv"],
                                cwd=self.directory, capture_output=True, text=True, check=False, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr +
                         ((self.directory / ".checkvars").read_text()
                          if (self.directory / ".checkvars").exists() else "") +
                         (self.directory / "antler.log").read_text())

    def stage(self, task, points="{20,0:40,0}"):
        self.poke(f"ROUTE_BUFFER_COMMAND:=action=deploy # task_id={task} # "
                  f"task_time={time.time():.6f} # points={points}")
        self.query("ROUTE_BUFFER_VEHICLE_STATE=STAGED")
        # Mimic pRallyPoint's admitted-task notification; do not invent route readiness.
        self.poke("TASK_EXECUTE:=true")
        self.query("IVPHELM_STATE=DRIVE", "ROUTE_READY=true",
                   "TAKEOFF_PRECHECK_FLIGHT_PLAN=success")
        self.query("UAV_FSM_TAKEOFF_OPERATIONS_STATE=REQUESTING_TAKEOFF_CLEARANCE")

    def test_nominal_completion_releases_identity_for_second_task(self):
        self.stage("first")
        self.poke("TAKEOFF_REQUEST_RESULT:=APPROVED")
        self.query("UAV_FSM_TAKEOFF_OPERATIONS_STATE=TAKEOFF")
        self.poke("UAV_IS_ARMED=1", "UAV_LANDED_STATE:=IN_AIR", "TAKEOFF_AIRBORNE:=success")
        self.query("UAV_FSM_OPERATION_STATE=MISSION_EXECUTION", "ROUTE_DEPLOY=true")
        self.poke("ROUTE_BUFFER_COMMAND:=action=deploy # task_id=busy # "
                  f"task_time={time.time():.6f} # points={{0,20:0,40}}")
        self.query("ROUTE_BUFFER_VEHICLE_STATE=TASK_REJECTED_CONTEXT", "ROUTE_READY=true")
        self.assertIn("name=first", (self.directory / ".checkvars").read_text())
        # Genuine final-waypoint completion from real Helm, not a route-ready poke.
        self.poke("NAV_X=20")
        self.poke("NAV_X=40")
        self.query("UAV_FSM_OPERATION_STATE=LANDING_OPERATIONS", "ROUTE_READY=false")
        # Coordination/native/check inputs are explicitly synthetic in this no-FC harness.
        self.poke("LANDING_AIRBORNE_CHECKS:=success", "LANDING_OWN_PHASE:=RENDEZVOUS",
                  "LANDING_OWN_RENDEZVOUS_PHASE:=COMPLETE", "LANDING_PEER_CLEARANCE:=true")
        self.query("UAV_FSM_LANDING_OPERATIONS_STATE=APPROACHING")
        self.poke("UAV_IS_ARMED=0", "UAV_LANDED_STATE:=ON_GROUND",
                  "LANDING_YAW_CHECKS:=success", "LANDING_GROUND_CHECKS:=success",
                  "LANDING_OWN_PHASE:=COMPLETE")
        self.query("UAV_FSM_OPERATION_STATE=IDLE", "IVPHELM_STATE=PARK")
        self.poke("NAV_X=0", "TAKEOFF_REQUEST_RESULT:=UNKNOWN", "TAKEOFF_AIRBORNE:=inactive",
                  "LANDING_OWN_PHASE:=IDLE")
        # Cleanup releases ownership but preserves duplicate suppression.
        self.poke("ROUTE_BUFFER_COMMAND:=action=deploy # task_id=first # "
                  f"task_time={time.time():.6f} # points={{0,20:0,40}}")
        self.query("ROUTE_BUFFER_VEHICLE_STATE=CLEAR_ACCEPTED", "ROUTE_READY=false")
        self.stage("second")
        checkvars = (self.directory / ".checkvars").read_text()
        self.assertIn("name=second", checkvars)
        self.assertNotIn("name=first", checkvars)

    def test_clearance_denial_releases_staged_identity_without_restart(self):
        self.stage("denied")
        self.poke("TAKEOFF_REQUEST_RESULT:=REJECTED")
        self.query("UAV_FSM_OPERATION_STATE=IDLE", "IVPHELM_STATE=PARK",
                   "ROUTE_BUFFER_VEHICLE_STATE=CLEAR_ACCEPTED", "ROUTE_EXECUTE=false")
        self.poke("TAKEOFF_REQUEST_RESULT:=UNKNOWN")
        self.stage("replacement", "{0,20:0,40}")
        self.poke("TAKEOFF_REQUEST_RESULT:=APPROVED")
        self.query("UAV_FSM_TAKEOFF_OPERATIONS_STATE=TAKEOFF")
        self.poke("UAV_IS_ARMED=1", "UAV_LANDED_STATE:=IN_AIR", "TAKEOFF_AIRBORNE:=success")
        self.query("UAV_FSM_OPERATION_STATE=MISSION_EXECUTION", "ROUTE_DEPLOY=true",
                   "DESIRED_HEADING<1")
        active = (self.directory / ".checkvars").read_text()
        self.assertNotIn("operator_route_denied", active)
        self.assertIn("operator_route_replacement", active)


if __name__ == "__main__":
    unittest.main()
