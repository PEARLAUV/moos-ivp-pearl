#!/usr/bin/env python3
"""Real MOOS transaction tests, isolated from every vehicle/flight controller."""

import os
import signal
import socket
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class RallyTaskTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="rally-task-")
        self.directory = Path(self.temp.name)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        self.mission = self.directory / "test.moos"
        self.mission.write_text(f"""ServerHost = localhost
ServerPort = {port}
Community = shoreside
ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = MOOSDB @ NewConsole = false
  Run = {ROOT / "bin/pRallyPoint"} @ NewConsole = false ~ rallyShore
  Run = {ROOT / "bin/pRallyPoint"} @ NewConsole = false ~ rallyVehicle
}}
ProcessConfig = rallyShore
{{
  AppTick = 20
  CommsTick = 20
  role = shoreside
  initial_point = x=10,y=15
}}
ProcessConfig = rallyVehicle
{{
  AppTick = 20
  CommsTick = 20
  role = vehicle
}}
""")
        self.output = (self.directory / "antler.log").open("w")
        self.process = subprocess.Popen(
            ["pAntler", str(self.mission)],
            cwd=self.directory,
            stdout=self.output,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        self.query("RALLY_STAGED_STATE=STAGED")

    def tearDown(self):
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
            ["uPokeDB", str(self.mission), "--quiet", *values],
            check=True,
            capture_output=True,
            timeout=12,
        )

    def query(self, *conditions, variable=None):
        args = [
            "uQueryDB",
            str(self.mission),
            "--wait=3",
            *(f"--condition={c}" for c in conditions),
        ]
        if variable:
            args.extend([f"--check_var={variable}", "--vo"])
        result = subprocess.run(
            args,
            cwd=self.directory,
            capture_output=True,
            text=True,
            check=False,
            timeout=12,
        )
        self.assertEqual(
            result.returncode,
            0,
            result.stdout + result.stderr + (self.directory / "antler.log").read_text(),
        )
        if variable:
            return (self.directory / ".checkvars").read_text().strip().partition("=")[2]
        return None

    def grounded(self, *extra):
        self.poke(
            "UAV_FSM_OPERATION_STATE:=IDLE",
            "UAV_IS_ARMED=0",
            "UAV_LANDED_STATE_AVAILABLE=1",
            "UAV_LANDED_STATE:=ON_GROUND",
            "UAV_LANDED_STATE_AGE=0.1",
            *extra,
        )

    def staged_task(self):
        self.poke("RALLY_POINT:=x=51,y=-56", "RALLY_ALTITUDE=20")
        self.poke("RALLY_TASK_COMMIT:=true")
        message = self.query("RALLY_STAGED_STATE=SENT", variable="NODE_MESSAGE_LOCAL")
        assert message is not None
        return message.partition("string_val=")[2].strip('"')

    def test_snapshot_matching_readback_and_no_duplicate_task(self):
        self.grounded()
        self.poke("TASK_EXECUTE:=false")
        spec = self.staged_task()
        task_id = spec.split("#", 1)[0].partition("=")[2]
        self.assertIn("#altitude=20", spec)
        self.grounded(f"RALLY_TASK:={spec}")
        upload = self.query(
            "RALLY_TASK_STATE=VERIFYING", "TASK_EXECUTE=true", variable="RALLY_UPLOAD"
        )
        self.assertEqual(upload, spec)
        self.poke("TASK_EXECUTE:=false")  # FSM consumes admission, not verification.
        # Subsequent GUI edits cannot mutate this transaction.
        self.poke("RALLY_POINT:=x=-90,y=40", "RALLY_ALTITUDE=30")
        self.poke("UAV_RALLY_RESULT:=id=wrong#status=READY")
        self.query("RALLY_TASK_STATE=VERIFYING", "TASK_EXECUTE=false")
        self.grounded("UAV_FSM_OPERATION_STATE:=TAKEOFF_OPERATIONS",
                      "UAV_FSM_TAKEOFF_OPERATIONS_STATE:=ESTABLISHING_TAKEOFF_READINESS",
                      f"UAV_RALLY_RESULT:=id={task_id}#status=READY")
        self.query("RALLY_TASK_STATE=READY", "TASK_EXECUTE=false")
        self.assertEqual(
            self.query("RALLY_TASK_STATE=READY", variable="RALLY_WAYPOINT_UPDATE"),
            "points=51,-56",
        )
        self.assertEqual(
            self.query("RALLY_TASK_STATE=READY", variable="RALLY_ALTITUDE_UPDATE"),
            "altitude=20",
        )
        self.poke("TASK_EXECUTE:=false", f"RALLY_TASK:={spec}")
        self.query("RALLY_TASK_STATE=READY", "TASK_EXECUTE=false")
        # An older ID cannot replace the next transaction after it has begun.
        second = "id=next-task#x=5#y=6#altitude=15"
        self.grounded(f"RALLY_TASK:={second}")
        self.query("RALLY_TASK_STATE=VERIFYING")
        self.grounded("UAV_RALLY_RESULT:=id=next-task#status=READY")
        self.query("RALLY_TASK_STATE=READY", "TASK_EXECUTE=true")
        self.grounded("TASK_EXECUTE:=false", f"RALLY_TASK:={spec}")
        self.query(
            "RALLY_TASK_STATE=READY", "TASK_EXECUTE=false", "RALLY_TASK_ID=next-task"
        )

    def test_failure_does_not_retrigger_admitted_task(self):
        spec = self.staged_task()
        self.grounded("TASK_EXECUTE:=false", f"RALLY_TASK:={spec}")
        self.query("RALLY_TASK_STATE=VERIFYING", "TASK_EXECUTE=true")
        self.poke("TASK_EXECUTE:=false")
        task_id = spec.split("#", 1)[0].partition("=")[2]
        self.poke(
            f"UAV_RALLY_RESULT:=id={task_id}#status=FAILED#detail=readback_mismatch"
        )
        self.query("RALLY_TASK_STATE=FAILED", "TASK_EXECUTE=false")

    def test_invalid_altitude_blocks_snapshot(self):
        self.poke("RALLY_ALTITUDE=15.0000001", "RALLY_TASK_COMMIT:=true")
        self.query("RALLY_STAGED_STATE=REJECTED")


if __name__ == "__main__":
    unittest.main()
