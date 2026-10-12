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
        runtime = (MISSION / "uav_fsm/plug_uav_fsm_runtime.moos").read_text()
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
        assignments = [
            f"{key}:={value}" if isinstance(value, str) else f"{key}={value}"
            for key, value in values.items()
        ]
        subprocess.run(
            [str(MOOS_BIN / "uPokeDB"), str(self.path), "--quiet", *assignments],
            cwd=self.directory, timeout=10, check=True, capture_output=True,
        )

    def query(self, *conditions):
        condition = conditions[0]
        for term in conditions[1:]:
            condition = f"({condition}) and ({term})"
        result = subprocess.run(
            [str(MOOS_BIN / "uQueryDB"), str(self.path), "--wait=6",
             "--condition=" + condition,
             "--check_var=UAV_FSM_OPERATION_STATE",
             "--check_var=UAV_FSM_LANDING_OPERATIONS_STATE",
             "--check_var=LANDING_AIRBORNE_CHECKS",
             "--check_var=LANDING_OWN_PHASE",
             "--check_var=LANDING_OWN_RENDEZVOUS_PHASE",
             "--check_var=LANDING_PEER_CLEARANCE",
             "--check_var=RETURN",
             "--check_var=UAV_HEALTH_ALL_OK",
             "--check_var=UAV_FSM_RUNTIME_STATE",
             "--check_var=UAV_FSM_OPERATION_TRANSITION"],
            cwd=self.directory, capture_output=True,
            text=True, timeout=12, check=False,
        )
        diagnostics = Path(self.directory) / ".checkvars"
        antler_log = Path(self.directory) / "antler.log"
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr
                         + (diagnostics.read_text() if diagnostics.exists() else "")
                         + (antler_log.read_text() if antler_log.exists() else ""))

    def enter_mission(self):
        self.poke(RETURN="false", LANDING_OWN_PHASE="IDLE",
                  LANDING_OWN_RENDEZVOUS_PHASE="IDLE",
                  LANDING_AIRBORNE_CHECKS="inactive",
                  LANDING_GROUND_CHECKS="inactive", LANDING_YAW_CHECKS="inactive",
                  LANDING_PEER_CLEARANCE="false", LANDING_OWN_TARGET_FRESH="true",
                  SECONDARY_ARRIVED="false", UAV_LANDED_STATE="ON_GROUND",
                  UAV_IS_ARMED=0, UAV_HEALTH_ALL_OK=1, UAV_GPS_AVAILABLE=1,
                  TAKEOFF_PRECHECKS="pending", TAKEOFF_REQUEST_RESULT="UNKNOWN",
                  TAKEOFF_AIRBORNE="inactive",
                  ROUTE_BUFFER_VEHICLE_STATE="DEPLOY_ACCEPTED", ROUTE_READY="true",
                  TASK_EXECUTE="true")
        self.query("UAV_FSM_TAKEOFF_OPERATIONS_STATE=ESTABLISHING_TAKEOFF_READINESS",
                   "SIM_UAV_ON_SHIP=true")
        self.poke(TAKEOFF_PRECHECKS="success")
        self.query("UAV_FSM_TAKEOFF_OPERATIONS_STATE=REQUESTING_TAKEOFF_CLEARANCE")
        self.poke(TAKEOFF_REQUEST_RESULT="APPROVED")
        self.query("UAV_FSM_TAKEOFF_OPERATIONS_STATE=TAKEOFF", "SIM_UAV_ON_SHIP=true")
        self.poke(UAV_LANDED_STATE="IN_AIR", UAV_IS_ARMED=1,
                  TAKEOFF_AIRBORNE="success")
        self.query("UAV_FSM_OPERATION_STATE=MISSION_EXECUTION", "SIM_UAV_ON_SHIP=false")

    def begin_primary_approach(self):
        self.poke(ROUTE_READY="false", LANDING_AIRBORNE_CHECKS="success",
                  LANDING_OWN_PHASE="RENDEZVOUS",
                  LANDING_OWN_RENDEZVOUS_PHASE="COMPLETE",
                  LANDING_PEER_CLEARANCE="true")
        self.query("UAV_FSM_LANDING_OPERATIONS_STATE=APPROACHING",
                   "SIM_UAV_ON_SHIP=true")

    def test_primary_landing_retry_and_secondary_landing(self):
        with self.fixture():
            self.poke(UAV_LAT=42.358436, UAV_LON=-71.087448, UAV_YAW=70,
                      UAV_HEADING=80, UAV_ALTITUDE_MSL=1.6)
            self.query("NAV_LAT=42.358436", "UAV_NAV_LAT=42.358436",
                       "UAV_NAV_LONG=-71.087448", "UAV_NAV_HEADING=70",
                       "NAV_HEADING=80", "UAV_NAV_ALTITUDE_MSL=1.6")
            self.enter_mission()
            self.begin_primary_approach()
            self.poke(LANDING_YAW_CHECKS="success", LANDING_OWN_PHASE="LANDING")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=LANDING", "SIM_UAV_ON_SHIP=true")
            self.poke(LANDING_OWN_TARGET_FRESH="false")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=RETRYING_LANDING_TARGET",
                       "SIM_UAV_ON_SHIP=true")
            self.poke(LANDING_OWN_TARGET_FRESH="true")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=LANDING", "SIM_UAV_ON_SHIP=true")
            # Effects are one-shot: the active landing state must not undo a poke.
            self.poke(SIM_UAV_ON_SHIP="false")
            time.sleep(1.25)
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=LANDING", "SIM_UAV_ON_SHIP=false")
            self.poke(LANDING_OWN_PHASE="COMPLETE", LANDING_GROUND_CHECKS="success",
                      UAV_LANDED_STATE="ON_GROUND", UAV_IS_ARMED=0)
            self.query("UAV_FSM_OPERATION_STATE=IDLE", "SIM_UAV_ON_SHIP=true")
            self.enter_mission()
            self.begin_primary_approach()
            self.poke(LANDING_OWN_PHASE="ABORTED")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=DEVIATING_TO_SECONDARY_LANDING_POSITION",
                       "SIM_UAV_ON_SHIP=false")
            self.poke(SECONDARY_ARRIVED="true")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=SECONDARY_LANDING",
                       "SIM_UAV_ON_SHIP=false")
            self.poke(LANDING_GROUND_CHECKS="success", UAV_LANDED_STATE="ON_GROUND",
                      UAV_IS_ARMED=0)
            self.query("UAV_FSM_OPERATION_STATE=OFFNOMINAL", "SIM_UAV_ON_SHIP=false")

    def test_secondary_loiter_detaches(self):
        with self.fixture():
            self.enter_mission()
            self.poke(SIM_UAV_ON_SHIP="true", UAV_HEALTH_ALL_OK=0,
                      ROUTE_READY="false")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=TRANSITING_TO_SECONDARY_LOITER_LOCATION",
                       "SIM_UAV_ON_SHIP=false")
            self.poke(SECONDARY_ARRIVED="true")
            self.query("UAV_FSM_LANDING_OPERATIONS_STATE=LOITERING_AT_SECONDARY_ALTITUDE",
                       "SIM_UAV_ON_SHIP=false")
            self.poke(LANDING_AIRBORNE_CHECKS="success")
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
