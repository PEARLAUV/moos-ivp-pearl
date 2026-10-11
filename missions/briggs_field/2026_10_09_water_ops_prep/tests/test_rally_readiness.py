"""Owned real FSM/rally/uChecks regression; synthetic telemetry, no flight controller."""

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


class RallyReadinessTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="rally-readiness-")
        self.directory = Path(self.temp.name)
        self.process = None
        self.addCleanup(self.cleanup)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        source = (MISSION / "meta_uav.moos").read_text()
        # Isolate the actual FLIGHT_PLAN requirements, not a substitute predicate.
        flight_plan = re.search(r"  check = name=FLIGHT_PLAN.*?(?=\n  check =)", source, re.DOTALL)
        assert flight_plan
        self.mission = self.directory / "test.moos"
        self.mission.write_text(f"""ServerHost=127.0.0.1
ServerPort={port}
Community=uav
ProcessConfig=ANTLER
{{
 MSBetweenLaunches=100
 Run={MOOS / 'MOOSDB'} @ NewConsole=false
 Run={ROOT / 'bin/pFSMRuntime'} @ NewConsole=false
 Run={ROOT / 'bin/pRallyPoint'} @ NewConsole=false
 Run={ROOT / 'bin/uChecks'} @ NewConsole=false ~ uPretakeoffChecks
 Run={MOOS / 'uTimerScript'} @ NewConsole=false ~ nativeGround
}}
ProcessConfig=pFSMRuntime
{{
 AppTick=4
 CommsTick=4
{(MISSION / 'uav_fsm/plug_uav_fsm_runtime.moos').read_text()}
}}
ProcessConfig=pRallyPoint
{{
 AppTick=4
 CommsTick=4
 role=vehicle
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
ProcessConfig=nativeGround
{{
 AppTick=10
 CommsTick=10
 paused=false
 reset_max=unlimited
 event=var=UAV_IS_ARMED,val=0,time=0
 event=var=UAV_LANDED_STATE_AVAILABLE,val=1,time=0
 event=var=UAV_LANDED_STATE,val=ON_GROUND,time=0
 event=var=UAV_LANDED_STATE_AGE,val=0.1,time=0
 event=var=NATIVE_TICK,val=1,time=0.5
}}
""")
        self.output = (self.directory / "antler.log").open("w")
        self.process = subprocess.Popen([str(MOOS / "pAntler"), str(self.mission)],
                                        cwd=self.directory, stdout=self.output,
                                        stderr=subprocess.STDOUT, start_new_session=True)
        self.query("UAV_FSM_OPERATION_STATE=IDLE", "UAV_IS_ARMED=0")

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

    def poke(self, *pairs):
        subprocess.run([str(MOOS / "uPokeDB"), str(self.mission), "--quiet", *pairs],
                       cwd=self.directory, capture_output=True, check=True, timeout=20)

    def query(self, *conditions):
        result = subprocess.run([str(MOOS / "uQueryDB"), str(self.mission),
                                 *(f"--condition={c}" for c in conditions),
                                 "--check_var=UAV_FSM_OPERATION_STATE",
                                 "--check_var=UAV_FSM_TAKEOFF_OPERATIONS_STATE",
                                 "--check_var=RALLY_TASK_STATE", "--check_var=RALLY_TASK_REASON",
                                 "--check_var=TASK_EXECUTE",
                                 "--check_var=TAKEOFF_PRECHECK_FLIGHT_PLAN_STATE",
                                 "--esv", "--wait=5"],
                                cwd=self.directory, capture_output=True, text=True,
                                check=False, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr +
                         ((self.directory / ".checkvars").read_text()
                      if (self.directory / ".checkvars").exists() else "") +
                     (self.directory / "antler.log").read_text())

    def admit(self):
        # Deliberately retained old FC readiness must not qualify this new task.
        self.poke("UAV_IS_ARMED=0", "UAV_LANDED_STATE_AVAILABLE=1",
                  "UAV_LANDED_STATE:=ON_GROUND", "UAV_LANDED_STATE_AGE=0.1",
                  "ROUTE_READY:=true", "UAV_RALLY_READY=1",
                  "RALLY_TASK:=id=new-task#x=10#y=15#altitude=15")
        self.query("UAV_FSM_OPERATION_STATE=TAKEOFF_OPERATIONS",
                   "UAV_FSM_TAKEOFF_OPERATIONS_STATE=ESTABLISHING_TAKEOFF_READINESS",
                   "RALLY_TASK_STATE=VERIFYING", "TAKEOFF_PRECHECK_FLIGHT_PLAN=pending")

    def test_matching_readback_releases_checks_not_task_transition(self):
        self.admit()
        self.poke("UAV_RALLY_RESULT:=id=old-task#status=READY")
        self.query("RALLY_TASK_STATE=VERIFYING",
                   "UAV_FSM_TAKEOFF_OPERATIONS_STATE=ESTABLISHING_TAKEOFF_READINESS")
        self.poke("UAV_IS_ARMED=0", "UAV_LANDED_STATE_AVAILABLE=1",
                  "UAV_LANDED_STATE:=ON_GROUND", "UAV_LANDED_STATE_AGE=0.1",
                  "UAV_RALLY_READY=1", "UAV_RALLY_RESULT:=id=new-task#status=READY")
        self.query("RALLY_TASK_STATE=READY", "TASK_EXECUTE=false",
                   "UAV_FSM_OPERATION_STATE=TAKEOFF_OPERATIONS",
                   "UAV_FSM_TAKEOFF_OPERATIONS_STATE=REQUESTING_TAKEOFF_CLEARANCE")

    def test_failed_verification_blocks_checks_after_state_entry(self):
        self.admit()
        self.poke("UAV_RALLY_READY=0",
                  "UAV_RALLY_RESULT:=id=new-task#status=FAILED#detail=parameter_write_failed")
        self.query("RALLY_TASK_STATE=FAILED", "TASK_EXECUTE=false",
                   "UAV_FSM_OPERATION_STATE=TAKEOFF_OPERATIONS",
                   "UAV_FSM_TAKEOFF_OPERATIONS_STATE=ESTABLISHING_TAKEOFF_READINESS",
                   "TAKEOFF_PRECHECK_FLIGHT_PLAN=pending", "UAV_IS_ARMED=0")


if __name__ == "__main__":
    unittest.main()
