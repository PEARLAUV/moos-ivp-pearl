#!/usr/bin/env python3
"""Exercise the real route handoff timer with native numeric arming telemetry."""

import os
import re
import signal
import socket
import subprocess
import tempfile
import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
MOOS_BIN = MISSION.parents[2].parent / "moos-ivp/bin"


class RouteHandoffTest(unittest.TestCase):
    def test_numeric_arming_and_airborne_mission_gate(self):
        source = (MISSION / "plug_uav_ardupilot.moos").read_text()
        config = re.search(r"ProcessConfig = uRouteSequence\s*\{[^}]+\}", source)
        if config is None:
            self.fail("Missing route handoff timer")
        with tempfile.TemporaryDirectory(prefix="route-handoff-") as directory:
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            mission = Path(directory) / "handoff.moos"
            mission.write_text(f"""ServerHost = localhost
ServerPort = {port}
Community = handoff_test
ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = {MOOS_BIN / 'MOOSDB'} @ NewConsole = false
  Run = {MOOS_BIN / 'uTimerScript'} @ NewConsole = false ~ uRouteSequence
}}
""" + config.group())
            with (Path(directory) / "antler.log").open("w") as log:
                process = subprocess.Popen([str(MOOS_BIN / "pAntler"), str(mission)],
                                           stdout=log, stderr=subprocess.STDOUT,
                                           start_new_session=True)
                try:
                    def poke(**values):
                        subprocess.run(
                            [str(MOOS_BIN / "uPokeDB"), str(mission), "--quiet",
                             *(f"{key}={value}" for key, value in values.items())],
                            capture_output=True, timeout=8, check=True,
                        )

                    def query(condition, wait=2, expected=0):
                        result = subprocess.run(
                            [str(MOOS_BIN / "uQueryDB"), str(mission), f"--wait={wait}",
                             f"--condition={condition}"],
                            capture_output=True, timeout=wait + 10, check=False,
                        )
                        self.assertEqual(result.returncode, expected,
                                         result.stdout.decode() + result.stderr.decode())

                    poke(ROUTE_EXECUTE="true", ROUTE_EXECUTE_PURPOSE="operator",
                         ROUTE_READY="true", UAV_IS_ARMED=0,
                         UAV_LANDED_STATE="IN_AIR", UAV_FSM_OPERATION_STATE="MISSION_EXECUTION",
                         HELM_STATUS="false", ROUTE_DEPLOY="false", ARDU_COMMAND="NONE")
                    query("HELM_STATUS=true", wait=1, expected=1)
                    poke(UAV_IS_ARMED=1, UAV_LANDED_STATE="TAKING_OFF")
                    query("HELM_STATUS=true", wait=1, expected=1)
                    poke(UAV_LANDED_STATE="IN_AIR", UAV_FSM_OPERATION_STATE="TAKEOFF_OPERATIONS")
                    query("HELM_STATUS=true", wait=1, expected=1)
                    poke(UAV_FSM_OPERATION_STATE="MISSION_EXECUTION")
                    query("((HELM_STATUS=true) and (ROUTE_DEPLOY=true)) and (ARDU_COMMAND=FLY_WAYPOINT)",
                          wait=5)
                    query("ROUTE_EXECUTE=false")
                finally:
                    if process.poll() is None:
                        os.killpg(process.pid, signal.SIGTERM)
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            os.killpg(process.pid, signal.SIGKILL)
                            process.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
