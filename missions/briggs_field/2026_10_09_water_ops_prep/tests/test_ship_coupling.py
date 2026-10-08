#!/usr/bin/env python3
"""Exercise generated ship-coupling FSM effects in an isolated MOOS database."""

import os
import re
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from contextlib import contextmanager
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
MOOS_BIN = MISSION.parents[2].parent / "moos-ivp" / "bin"
FSM_BIN = MISSION.parents[2] / "bin" / "pFSMRuntime"


class ShipCouplingPolicyTest(unittest.TestCase):
    @contextmanager
    def fixture(self):
        source = (MISSION / "meta_uav.moos").read_text()
        for alias in ("uSimShipStart", "uSimShipDetach", "uSimShipAttach"):
            self.assertNotIn(alias, source)
        echo = re.search(r"ProcessConfig = pEchoVar\s*\{[^}]+\}", source)
        if echo is None:
            raise AssertionError("Missing local UAV telemetry echo")
        # Exercise the SITL branch of the actual mission's echo configuration.
        echo_config = "\n".join(line for line in echo.group().splitlines()
                                if not line.startswith("#"))
        runtime = (MISSION / "plug_uav_fsm_runtime.moos").read_text()
        with tempfile.TemporaryDirectory(prefix="ship-policy-") as directory:
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            self.directory = directory
            self.path = Path(directory) / "policy.moos"
            self.path.write_text(f"""ServerHost = 127.0.0.1
ServerPort = {port}
Community = ship_policy_test
ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = {MOOS_BIN / 'MOOSDB'} @ NewConsole = false
  Run = {MOOS_BIN / 'pEchoVar'} @ NewConsole = false
  Run = {FSM_BIN} @ NewConsole = false
}}
ProcessConfig = pFSMRuntime
{{
  AppTick = 20
  CommsTick = 20
{runtime}
}}
{echo_config}
""")
            with (Path(directory) / "antler.log").open("w") as log:
                process = subprocess.Popen(
                    [str(MOOS_BIN / "pAntler"), str(self.path)], cwd=directory,
                    stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
                )
                try:
                    self.query("UAV_FSM_OPERATION_STATE=IDLE", "SIM_UAV_ON_SHIP=true")
                    yield
                finally:
                    if process.poll() is None:
                        os.killpg(process.pid, signal.SIGTERM)
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            os.killpg(process.pid, signal.SIGKILL)
                            process.wait(timeout=5)

    def poke(self, **values):
        subprocess.run(
            [str(MOOS_BIN / "uPokeDB"), str(self.path), "--quiet",
             *(f"{key}={value}" for key, value in values.items())],
            cwd=self.directory, timeout=10, check=True, capture_output=True,
        )

    def query(self, *conditions):
        condition = conditions[0]
        for term in conditions[1:]:
            condition = f"({condition}) and ({term})"
        result = subprocess.run(
            [str(MOOS_BIN / "uQueryDB"), str(self.path), "--wait=6",
             "--condition=" + condition], cwd=self.directory, capture_output=True,
            text=True, timeout=12, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def enter_mission(self):
        self.poke(RETURN="false", UAV_RENDEZVOUS_PHASE="IDLE",
                  UAV_LANDED_STATE="ON_GROUND", UAV_IS_ARMED=0,
                  UAV_HEALTH_ALL_OK=1, UAV_GPS_AVAILABLE=1,
                  TAKEOFF_PRECHECKS="pending", TAKEOFF_REQUEST_RESULT="UNKNOWN",
                  UAV_LANDING_TARGET_STATE="TRACKED", UAV_RENDEZVOUS_ARRIVED=0,
                  ROUTE_BUFFER_VEHICLE_STATE="DEPLOY_ACCEPTED", UAV_LANDING_GATE_READY=0,
                  UAV_PREC_LAND_RESULT="UNKNOWN", UAV_SECONDARY_RECOVERY_STATE="UNKNOWN",
                  TASK_EXECUTE="true")
        self.query("UAV_FSM_TAKEOFF_OPERATIONS_STATE=ESTABLISHING_TAKEOFF_READINESS",
                   "SIM_UAV_ON_SHIP=true")
        self.poke(TAKEOFF_PRECHECKS="success")
        self.query("UAV_FSM_TAKEOFF_OPERATIONS_STATE=REQUESTING_TAKEOFF_CLEARANCE")
        self.poke(TAKEOFF_REQUEST_RESULT="APPROVED")
        self.query("UAV_FSM_TAKEOFF_OPERATIONS_STATE=TAKEOFF", "SIM_UAV_ON_SHIP=true")
        self.poke(UAV_LANDED_STATE="IN_AIR", UAV_IS_ARMED=1)
        self.query("UAV_FSM_OPERATION_STATE=MISSION_EXECUTION", "SIM_UAV_ON_SHIP=false")

    def test_primary_landing_retry_and_secondary_landing(self):
        with self.fixture():
            self.poke(UAV_LAT=42.358436, UAV_LON=-71.087448, UAV_YAW=70,
                      UAV_HEADING=80, UAV_ALTITUDE_MSL=1.6)
            self.query("NAV_LAT=42.358436", "UAV_NAV_LAT=42.358436",
                       "UAV_NAV_LONG=-71.087448", "UAV_NAV_HEADING=70",
                       "NAV_HEADING=80", "UAV_NAV_ALTITUDE_MSL=1.6")
            self.enter_mission()
            self.poke(RETURN="true", UAV_RENDEZVOUS_PHASE="ACQUIRING_TARGET")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=APPROACHING", "SIM_UAV_ON_SHIP=true")
            self.poke(UAV_LANDING_GATE_READY=1, UAV_PREC_LAND_RESULT="ACCEPTED",
                      UAV_RENDEZVOUS_PHASE="LANDING")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=LANDING", "SIM_UAV_ON_SHIP=true")
            self.poke(UAV_LANDING_TARGET_STATE="LOST")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=RETRYING_LANDING_TARGET",
                       "SIM_UAV_ON_SHIP=true")
            self.poke(UAV_LANDING_TARGET_STATE="TRACKED")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=LANDING", "SIM_UAV_ON_SHIP=true")
            # Effects are one-shot: the active landing state must not undo a poke.
            self.poke(SIM_UAV_ON_SHIP="false")
            time.sleep(1.25)
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=LANDING", "SIM_UAV_ON_SHIP=false")
            self.poke(UAV_RENDEZVOUS_PHASE="COMPLETE", UAV_LANDED_STATE="ON_GROUND",
                      UAV_IS_ARMED=0)
            self.query("UAV_FSM_OPERATION_STATE=IDLE", "SIM_UAV_ON_SHIP=true")
            self.enter_mission()
            self.poke(RETURN="true", UAV_RENDEZVOUS_PHASE="ACQUIRING_TARGET")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=APPROACHING", "SIM_UAV_ON_SHIP=true")
            self.poke(UAV_RENDEZVOUS_PHASE="ABORT")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=DEVIATING_TO_SECONDARY_LANDING_POSITION",
                       "SIM_UAV_ON_SHIP=false")
            self.poke(SIM_UAV_ON_SHIP="true", UAV_SECONDARY_RECOVERY_STATE="ARRIVED")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=SECONDARY_LANDING",
                       "SIM_UAV_ON_SHIP=false")
            self.poke(UAV_SECONDARY_RECOVERY_STATE="LANDED", UAV_LANDED_STATE="ON_GROUND",
                      UAV_IS_ARMED=0)
            self.query("UAV_FSM_OPERATION_STATE=OFFNOMINAL", "SIM_UAV_ON_SHIP=false")

    def test_secondary_loiter_detaches(self):
        with self.fixture():
            self.enter_mission()
            self.poke(SIM_UAV_ON_SHIP="true", UAV_HEALTH_ALL_OK=0, RETURN="true")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=TRANSITING_TO_SECONDARY_LOITER_LOCATION",
                       "SIM_UAV_ON_SHIP=false")
            self.poke(SIM_UAV_ON_SHIP="true", UAV_SECONDARY_RECOVERY_STATE="ARRIVED")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=LOITERING_AT_SECONDARY_ALTITUDE",
                       "SIM_UAV_ON_SHIP=false")
            self.poke(UAV_SECONDARY_RECOVERY_STATE="LOITERING")
            self.query("UAV_FSM_OPERATION_STATE=LOITERING", "SIM_UAV_ON_SHIP=false")

    def test_failed_takeoff_returns_attached_to_idle(self):
        with self.fixture():
            self.poke(UAV_LANDED_STATE="ON_GROUND", TAKEOFF_PRECHECKS="success",
                      TAKEOFF_REQUEST_RESULT="APPROVED", TASK_EXECUTE="true")
            self.query("UAV_FSM_TAKEOFF_OPERATIONS_STATE=TAKEOFF", "SIM_UAV_ON_SHIP=true")
            self.poke(TAKEOFF_REQUEST_RESULT="REJECTED")
            # Child exit detaches first; the destination IDLE entry reattaches.
            self.query("UAV_FSM_OPERATION_STATE=IDLE", "SIM_UAV_ON_SHIP=true")


if __name__ == "__main__":
    unittest.main()
