"""Owned real Helm/FSM route-task regression. Synthetic NAV, no flight controller."""

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


class PearlRouteTaskTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="pearl-route-task-")
        self.directory = Path(self.temp.name)
        self.process = None
        self.addCleanup(self.cleanup)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        bhv = self.directory / "pearl.bhv"
        subprocess.run(["nsplug", str(MISSION / "meta_pearl.bhv"), str(bhv),
                        "--strict", "--force", "SPEED=0.5"], check=True, capture_output=True)
        config = re.search(r"ProcessConfig = pRouteBuffer\s*\{.*?\n\}",
                           (MISSION / "meta_pearl.moos").read_text(), re.DOTALL)
        assert config
        self.mission = self.directory / "test.moos"
        self.mission.write_text(f"""ServerHost=127.0.0.1
ServerPort={port}
Community=pearl
LatOrigin=42.3569186052
LongOrigin=-71.0990291116
ProcessConfig=ANTLER
{{
 MSBetweenLaunches=100
 Run={MOOS / 'MOOSDB'} @ NewConsole=false
 Run={MOOS / 'pHelmIvP'} @ NewConsole=false
 Run={ROOT / 'bin/pFSMRuntime'} @ NewConsole=false
 Run={ROOT / 'bin/pRouteBuffer'} @ NewConsole=false
}}
ProcessConfig=pHelmIvP
{{
 AppTick=4
 CommsTick=4
 start_in_drive=true
 park_on_allstop=false
 behaviors={bhv}
 Domain=course:0:359:360
 Domain=speed:0:0.5:11
}}
ProcessConfig=pFSMRuntime
{{
 AppTick=4
 CommsTick=4
{(MISSION / 'plug_pearl_fsm_runtime.moos').read_text()}
}}
{config.group()}
""")
        self.output = (self.directory / "antler.log").open("w")
        self.process = subprocess.Popen([str(MOOS / "pAntler"), str(self.mission)],
                                        cwd=self.directory, stdout=self.output,
                                        stderr=subprocess.STDOUT, start_new_session=True)
        self.poke("NAV_X=0", "NAV_Y=0", "NAV_SPEED=0", "NAV_HEADING=0")
        self.query("PEARL_FSM_MAIN_ACTIVITY_STATE=IDLE", "IVPHELM_STATE=DRIVE",
                   "PEARL_FSM_SUPPORT_ACTIVITY_STATE=NO_ACTIVE_REQUEST")

    def cleanup(self):
        if self.process and self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGINT)
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=4)
        if hasattr(self, "output"):
            self.output.close()
        self.temp.cleanup()

    def poke(self, *pairs):
        subprocess.run([str(MOOS / "uPokeDB"), str(self.mission), "--quiet", *pairs],
                       cwd=self.directory, capture_output=True, check=True, timeout=20)

    def query(self, *conditions, expected=0, wait=5):
        result = subprocess.run([str(MOOS / "uQueryDB"), str(self.mission),
                                 *(f"--condition={c}" for c in conditions),
                                 "--check_var=ROUTE_READY_NAME", "--check_var=ROUTE_BUFFER_VEHICLE_STATE",
                                 "--check_var=TASK_EXECUTE", "--check_var=ROUTE_UPDATE",
                                 "--check_var=PEARL_FSM_MAIN_ACTIVITY_STATE",
                                 "--check_var=PEARL_FSM_OPERATION_STATE",
                                 "--check_var=PEARL_FSM_SUPPORT_ACTIVITY_STATE",
                                 "--check_var=PEARL_FSM_TAKEOFF_SUPPORT_STATE",
                                 "--check_var=PEARL_FSM_TAKEOFF_SUPPORT_STATUS",
                                 "--check_var=PEARL_FSM_RUNTIME_STATE", "--check_var=IVPHELM_STATE",
                                 "--check_var=MOOS_MANUAL_OVERRIDE", "--check_var=PEARL_ALLSTOP", "--esv",
                                 f"--wait={wait}"], cwd=self.directory, capture_output=True,
                                text=True, check=False, timeout=wait + 15)
        checkvars = self.directory / ".checkvars"
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr +
                         (checkvars.read_text() if checkvars.exists() else "") +
                         (self.directory / "antler.log").read_text())

    def task(self, identity="first", points="{0,20:20,20}"):
        self.poke(f"ROUTE_BUFFER_COMMAND:=action=deploy # task_id={identity} # "
                  f"task_time={time.time():.6f} # points={points}")
        self.query("PEARL_FSM_MAIN_ACTIVITY_STATE=EXECUTING_MISSION", "ROUTE_READY=true",
                   "ROUTE_DEPLOY=true", "ROUTE_BUFFER_VEHICLE_STATE=TASK_STARTED")

    def finish(self):
        self.poke("NAV_X=0", "NAV_Y=20")
        self.query("PEARL_FSM_MAIN_ACTIVITY_STATE=EXECUTING_MISSION", "DESIRED_HEADING>89",
                   "DESIRED_HEADING<91")
        self.poke("NAV_X=20")
        self.query("PEARL_FSM_MAIN_ACTIVITY_STATE=RETURNING_TO_BASE", "ROUTE_READY=false")
        self.poke("NAV_X=10.6", "NAV_Y=4.8")
        self.query("PEARL_FSM_MAIN_ACTIVITY_STATE=IDLE", "MAIN_TASK_STATUS=returned")

    def test_route_completion_return_and_repeated_tasks(self):
        self.task()
        self.finish()
        self.task("second")
        self.finish()

    def test_busy_rejection_and_support_history_resume(self):
        self.task()
        self.poke(f"ROUTE_BUFFER_COMMAND:=action=deploy # task_id=busy # "
                  f"task_time={time.time():.6f} # points={{99,99}}")
        self.query("ROUTE_BUFFER_VEHICLE_STATE=TASK_REJECTED_CONTEXT",
                   "PEARL_FSM_MAIN_ACTIVITY_STATE=EXECUTING_MISSION", "ROUTE_READY=true")
        self.poke("NAV_X=0", "NAV_Y=20")
        self.query("WPT_INDEX=1")
        self.poke("NAV_X=10", "NAV_Y=20")
        self.query("DESIRED_HEADING>=89", "DESIRED_HEADING<=91")
        self.poke("TAKEOFF_REQUEST_ID:=probe", "TAKEOFF_PRECHECKS_STATE:=success")
        self.query("PEARL_FSM_OPERATION_STATE=TAKEOFF_SUPPORT")
        self.poke("TAKEOFF_REQUEST_ID:=none", "TAKEOFF_CANCEL_STATE:=cancel",
                  "TAKEOFF_REQUEST_STATE:=canceled")
        self.query("PEARL_FSM_OPERATION_STATE=NORMAL_OPERATION",
                   "PEARL_FSM_MAIN_ACTIVITY_STATE=EXECUTING_MISSION", "ROUTE_READY=true")
        self.query("DESIRED_HEADING>=89", "DESIRED_HEADING<=91", "WPT_INDEX=1")
        self.finish()

    def test_stale_command_and_missing_task_id_fail_closed(self):
        self.poke(f"ROUTE_BUFFER_COMMAND:=action=deploy # task_id=old # "
                  f"task_time={time.time() - 40:.6f} # points={{0,20}}")
        self.query("ROUTE_BUFFER_VEHICLE_STATE=TASK_REJECTED_STALE",
                   "PEARL_FSM_MAIN_ACTIVITY_STATE=IDLE", "TASK_EXECUTE=false")
        self.poke("ROUTE_BUFFER_COMMAND:=action=deploy # points={0,20}")
        self.query("ROUTE_BUFFER_VEHICLE_STATE=TASK_REJECTED_STALE",
                   "PEARL_FSM_MAIN_ACTIVITY_STATE=IDLE", "TASK_EXECUTE=false")

    def test_duplicate_task_does_not_reexecute_after_return(self):
        stamp = time.time()
        self.task("same")
        self.finish()
        self.poke(f"ROUTE_BUFFER_COMMAND:=action=deploy # task_id=same # "
                  f"task_time={stamp:.6f} # points={{0,20:20,20}}")
        self.query("PEARL_FSM_MAIN_ACTIVITY_STATE=IDLE", "ROUTE_READY=false")


if __name__ == "__main__":
    unittest.main()
