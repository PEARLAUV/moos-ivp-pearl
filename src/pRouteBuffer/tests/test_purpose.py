#!/usr/bin/env python3
"""Vehicle-side route purpose and FSM staging authorization."""

import os
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MOOS = Path(os.environ.get("MOOS_BIN_DIR", ROOT.parent / "moos-ivp" / "bin"))
ROUTE_BUFFER = ROOT / "bin/pRouteBuffer"


class RoutePurposeTest(unittest.TestCase):
    def setUp(self):
        required = [MOOS / binary for binary in ("MOOSDB", "pAntler", "pLogger", "uPokeDB", "uQueryDB")]
        required.append(ROUTE_BUFFER)
        if any(not binary.is_file() for binary in required):
            self.skipTest(f"Missing test binaries: {required}")
        self.temp = tempfile.TemporaryDirectory(prefix="route-purpose-")
        self.root = Path(self.temp.name)
        self.logs = self.root / "logs"
        self.logs.mkdir()
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        self.mission = self.root / "purpose.moos"
        self.mission.write_text(f"""ServerHost=127.0.0.1
ServerPort={port}
Community=uav
ProcessConfig=ANTLER
{{
 MSBetweenLaunches=100
 Run={MOOS / 'MOOSDB'} @ NewConsole=false
 Run={MOOS / 'pLogger'} @ NewConsole=false ~ pLogger
 Run={ROUTE_BUFFER} @ NewConsole=false ~ pRouteBuffer
}}
ProcessConfig=pLogger
{{
 AppTick=20
 CommsTick=20
 File=purpose
 PATH={self.logs}
 AsyncLog=true
 FileTimeStamp=false
 WildCardLogging=false
 Log=ROUTE_UPDATE @ 0
}}
ProcessConfig=pRouteBuffer
{{
 AppTick=20
 CommsTick=20
 role=vehicle
 deploy_delay=0.3
 route_deploy_var=ROUTE_EXECUTE
 route_purpose_var=ROUTE_EXECUTE_PURPOSE
 operation_state_var=UAV_FSM_OPERATION_STATE
 recovery_source=uPokeDB
 operator_stage_state=IDLE
 operator_stage_state=TAKEOFF_OPERATIONS
 operator_stage_state=MISSION_EXECUTION
 recovery_stage_state=LANDING_OPERATIONS
}}
""", encoding="utf-8")
        self.log = (self.root / "antler.log").open("w", encoding="utf-8")
        self.antler = subprocess.Popen([str(MOOS / "pAntler"), str(self.mission)],
                                      cwd=self.root, stdout=self.log,
                                      stderr=subprocess.STDOUT, start_new_session=True)
        time.sleep(1.5)
        self.assertIsNone(self.antler.poll())

    def tearDown(self):
        if self.antler.poll() is None:
            os.killpg(self.antler.pid, signal.SIGTERM)
            try:
                self.antler.wait(timeout=4)
            except subprocess.TimeoutExpired:
                os.killpg(self.antler.pid, signal.SIGKILL)
                self.antler.wait(timeout=4)
        self.log.close()
        self.temp.cleanup()

    def poke(self, *pairs):
        result = subprocess.run([str(MOOS / "uPokeDB"), str(self.mission),
                                 "--quiet", *pairs], cwd=self.root,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def query(self, *conditions):
        result = subprocess.run([str(MOOS / "uQueryDB"), str(self.mission),
                                 *(f"--condition={c}" for c in conditions),
                                 "--wait=5"], cwd=self.root, capture_output=True,
                                text=True, timeout=9)
        if result.returncode:
            self.log.flush()
            self.fail(result.stdout + result.stderr +
                      (self.root / "antler.log").read_text(encoding="utf-8"))

    def value(self, var, state):
        result = subprocess.run([str(MOOS / "uQueryDB"), str(self.mission),
                                 f"--condition=ROUTE_BUFFER_VEHICLE_STATE=={state}",
                                 f"--check_var={var}", "--vo", "--wait=5"],
                                cwd=self.root, capture_output=True, text=True,
                                timeout=9)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return (self.root / ".checkvars").read_text(encoding="utf-8").strip().partition("=")[2]

    def test_repeated_clear_does_not_resurrect_recovery_route(self):
        self.poke("UAV_FSM_OPERATION_STATE:=LANDING_OPERATIONS")
        self.poke("ROUTE_BUFFER_COMMAND:=action=deploy # purpose=recovery # "
                  "route_name=recovery_first # points={1,1:2,2}")
        self.query("ROUTE_BUFFER_VEHICLE_STATE==DEPLOY_ACCEPTED")
        self.poke("ROUTE_BUFFER_COMMAND:=action=clear")
        self.query("ROUTE_BUFFER_VEHICLE_STATE==CLEAR_ACCEPTED")
        self.poke("UAV_FSM_OPERATION_STATE:=IDLE", "UAV_LANDED_STATE:=ON_GROUND")
        self.poke("ROUTE_BUFFER_COMMAND:=action=clear")
        self.query("ROUTE_BUFFER_VEHICLE_STATE==CLEAR_ACCEPTED")
        self.poke("ROUTE_BUFFER_COMMAND:=action=deploy # points={10,10:20,20}")
        self.query("ROUTE_BUFFER_VEHICLE_STATE==DEPLOY_ACCEPTED")
        os.killpg(self.antler.pid, signal.SIGTERM)
        self.antler.wait(timeout=5)

        alog = self.logs / "purpose" / "purpose.alog"
        updates = [line.split("pRouteBuffer", 1)[1].strip()
                   for line in alog.read_text(encoding="utf-8").splitlines()
                   if "ROUTE_UPDATE" in line and "pRouteBuffer" in line]
        self.assertEqual(len(updates), 3, updates)
        self.assertIn("name=recovery_first # order=normal", updates[0])
        self.assertEqual(updates[1], "name=recovery_first#duration=0")
        self.assertIn("name=active # order=normal", updates[2])

    def test_operator_stages_before_takeoff_but_cannot_replace_recovery(self):
        self.poke("UAV_FSM_OPERATION_STATE:=TAKEOFF_OPERATIONS")
        self.poke("ROUTE_BUFFER_COMMAND:=action=deploy # points={0,0:10,0}")
        self.query("ROUTE_BUFFER_VEHICLE_STATE==DEPLOY_ACCEPTED",
                   "ROUTE_EXECUTE_PURPOSE==operator")
        self.query("ROUTE_EXECUTE==true")
        staged_route = self.value("ROUTE_UPDATE", "DEPLOY_ACCEPTED")
        self.poke("UAV_FSM_OPERATION_STATE:=LANDING_OPERATIONS")
        self.poke("ROUTE_BUFFER_COMMAND:=action=deploy # points={0,0:99,0}")
        self.query("ROUTE_BUFFER_VEHICLE_STATE==DEPLOY_REJECTED_AUTHORITY",
                   "ROUTE_EXECUTE_PURPOSE==operator")
        # A rejected operator route must not replace the staged Helm snapshot.
        self.assertEqual(self.value("ROUTE_UPDATE", "DEPLOY_REJECTED_AUTHORITY"),
                         staged_route)
        self.poke("ROUTE_BUFFER_COMMAND:=action=deploy # purpose=recovery # points={1,1:2,2}")
        self.query("ROUTE_BUFFER_VEHICLE_STATE==DEPLOY_ACCEPTED",
                   "ROUTE_EXECUTE_PURPOSE==recovery")
        self.query("ROUTE_EXECUTE==true")
        recovery_route = self.value("ROUTE_UPDATE", "DEPLOY_ACCEPTED")
        self.poke("ROUTE_BUFFER_COMMAND:=action=append # point=9,9")
        self.query("ROUTE_BUFFER_VEHICLE_STATE==APPEND_REJECTED_AUTHORITY")
        self.assertEqual(self.value("ROUTE_UPDATE", "APPEND_REJECTED_AUTHORITY"),
                         recovery_route)


if __name__ == "__main__":
    unittest.main()
