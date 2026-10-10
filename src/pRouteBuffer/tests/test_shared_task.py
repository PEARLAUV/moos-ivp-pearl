"""Owned MOOS shared-drawing submissions; no mediator or vehicle commands executed."""

import os
import re
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MOOS = ROOT.parent / "moos-ivp/bin"


class SharedTaskTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="shared-route-task-")
        self.root = Path(self.temp.name)
        self.process = None
        self.addCleanup(self.cleanup)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        self.mission = self.root / "test.moos"
        self.mission.write_text(f"""ServerHost=127.0.0.1
ServerPort={port}
Community=shore
ProcessConfig=ANTLER
{{
 MSBetweenLaunches=100
 Run={MOOS / 'MOOSDB'} @ NewConsole=false
 Run={MOOS / 'pLogger'} @ NewConsole=false
 Run={ROOT / 'bin/pRouteBuffer'} @ NewConsole=false
}}
ProcessConfig=pLogger
{{
 AppTick=4
 CommsTick=4
 File=task
 PATH={self.root}
 AsyncLog=true
 FileTimeStamp=false
 WildCardLogging=false
 Log=NODE_MESSAGE_LOCAL @ 0
 Log=VIEW_SEGLIST @ 0
 Log=ROUTE_BUFFER_TASK_DESTINATION @ 0
 Log=ROUTE_BUFFER_TASK_ID @ 0
 Log=RALLY_TASK_COMMIT @ 0
}}
ProcessConfig=pRouteBuffer
{{
 AppTick=4
 CommsTick=4
 role=shoreside
 source_node=shore
 destination_node=uav
 task_destination=pearl
 task_destination=uav
 task_submit_flag=uav:RALLY_TASK_COMMIT=true
 stage_clear_request_var=ROUTE_BUFFER_CLEAR_STAGED
}}
""")
        self.output = (self.root / "antler.log").open("w")
        self.process = subprocess.Popen([str(MOOS / "pAntler"), str(self.mission)],
                                        cwd=self.root, stdout=self.output,
                                        stderr=subprocess.STDOUT, start_new_session=True)
        self.poke("RALLY_TASK_COMMIT:=false")

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
                       cwd=self.root, capture_output=True, check=True, timeout=20)

    def query(self, *conditions):
        result = subprocess.run([str(MOOS / "uQueryDB"), str(self.mission),
                                 *(f"--condition={c}" for c in conditions), "--wait=6"],
                                cwd=self.root, capture_output=True, text=True, check=False, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_destination_snapshot_consumption_and_empty_double_click(self):
        self.poke("ROUTE_BUFFER_TASK:=pearl")
        self.query("ROUTE_BUFFER_STATE=TASK_REJECTED_EMPTY", "ROUTE_BUFFER_COUNT=0")
        self.poke("ROUTE_POINT:=x=0,y=20")
        self.poke("ROUTE_POINT:=x=20,y=20")
        self.poke("ROUTE_BUFFER_TASK:=pearl")
        self.query("ROUTE_BUFFER_STATE=TASK_SUBMITTED", "ROUTE_BUFFER_COUNT=0",
                   "ROUTE_BUFFER_TASK_DESTINATION=pearl", "RALLY_TASK_COMMIT=false")
        self.poke("ROUTE_BUFFER_TASK:=uav")  # Drawing is already consumed.
        self.query("ROUTE_BUFFER_STATE=TASK_REJECTED_EMPTY", "RALLY_TASK_COMMIT=false")
        self.poke("ROUTE_POINT:=x=99,y=99")
        self.poke("ROUTE_BUFFER_TASK:=unknown")
        self.query("ROUTE_BUFFER_STATE=TASK_REJECTED_TARGET", "ROUTE_BUFFER_COUNT=1")
        self.poke("ROUTE_BUFFER_TASK:=uav")
        self.query("ROUTE_BUFFER_STATE=TASK_SUBMITTED", "ROUTE_BUFFER_COUNT=0",
                   "ROUTE_BUFFER_TASK_DESTINATION=uav", "RALLY_TASK_COMMIT=true")
        self.poke("ROUTE_POINT:=x=50,y=50")
        self.poke("ROUTE_BUFFER_CLEAR_STAGED=true")
        self.query("ROUTE_BUFFER_STATE=EMPTY", "ROUTE_BUFFER_COUNT=0")
        time.sleep(0.5)
        assert self.process is not None
        os.killpg(self.process.pid, signal.SIGTERM)
        self.process.wait(timeout=4)
        log = (self.root / "task/task.alog").read_text()
        messages = [line.split("pRouteBuffer", 1)[1] for line in log.splitlines()
                    if re.search(r"\sNODE_MESSAGE_LOCAL\s+pRouteBuffer\s", line)]
        # Legacy startup sync consumes the mediator\'s otherwise rejected ID zero.
        messages = [msg for msg in messages if "action=sync" not in msg]
        self.assertEqual(len(messages), 2, messages)
        self.assertIn("dest_node=pearl", messages[0])
        self.assertIn("points={0,20:20,20}", messages[0])
        self.assertNotIn("99,99", messages[0])
        self.assertIn("dest_node=uav", messages[1])
        self.assertIn("points={99,99}", messages[1])
        self.assertTrue(all("action=deploy" in msg and "task_time=" in msg and
                            "task_id=task_" in msg for msg in messages))
        self.assertTrue(all("action=clear" not in msg for msg in messages))
        self.assertTrue(any("label=route_buffer_path" in line and "active=false" in line
                            for line in log.splitlines()))


if __name__ == "__main__":
    unittest.main()
