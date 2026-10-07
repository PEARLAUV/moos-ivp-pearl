#!/usr/bin/env python3

"""Integration checks for authoritative PEARL takeoff-state gating."""

from __future__ import annotations

import os
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
MOOS_BIN_DIR = Path(
    os.environ.get("MOOS_BIN_DIR", REPO_ROOT.parent / "moos-ivp" / "bin")
)
FSM_BINARY = Path(
    os.environ.get("PFSMRUNTIME_BINARY", REPO_ROOT / "bin" / "pFSMRuntime")
)
GATE_BINARY = Path(
    os.environ.get("PTAKEOFFGATE_BINARY", REPO_ROOT / "bin" / "pTakeoffGate")
)
FSM_PLUG = (
    REPO_ROOT
    / "missions"
    / "charles_river"
    / "2026_09_29_chevron_demo"
    / "plug_pearl_fsm_runtime.moos"
)


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


class TakeoffStateGatingIntegrationTest(unittest.TestCase):
    def setUp(self) -> None:
        required = [
            MOOS_BIN_DIR / "MOOSDB",
            MOOS_BIN_DIR / "pAntler",
            MOOS_BIN_DIR / "uPokeDB",
            MOOS_BIN_DIR / "uQueryDB",
            FSM_BINARY,
            GATE_BINARY,
            FSM_PLUG,
        ]
        missing = [str(path) for path in required if not path.is_file()]
        if missing:
            raise RuntimeError("Build dependencies are missing: " + ", ".join(missing))

        self.moos_port = free_port()
        self.temp_dir = tempfile.TemporaryDirectory(prefix="takeoff-state-gate-")
        self.temp_path = Path(self.temp_dir.name)
        self.mission_path = self.temp_path / "takeoff_gate.moos"
        self.antler_log_path = self.temp_path / "pAntler.log"
        min_state_time = 5 if self._testMethodName == "test_no_early_support" else 0
        step_delay = (
            5
            if self._testMethodName == "test_postcheck_failure_cancels_and_restores"
            else 0.1
        )
        plug = FSM_PLUG.read_text(encoding="utf-8")
        self.mission_path.write_text(
            f"""ServerHost = 127.0.0.1
ServerPort = {self.moos_port}
Community  = takeoff_state_gate_test

ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = {MOOS_BIN_DIR / 'MOOSDB'} @ NewConsole = false
  Run = {FSM_BINARY} @ NewConsole = false
  Run = {GATE_BINARY} @ NewConsole = false
}}

ProcessConfig = pFSMRuntime
{{
  AppTick = 20
  CommsTick = 20
  min_state_time = {min_state_time}
{plug}
}}

ProcessConfig = pTakeoffGate
{{
  AppTick = 20
  CommsTick = 20
  role = pearl
  ownship = pearl
  peer_node = uav
  request_timeout = 10
  coordination_step_delay = {step_delay}
  min_pearl_soc = 15
  max_wind_speed = 4
  input_max_age = 30
  fsm_state_max_age = 2.5
}}
""",
            encoding="utf-8",
        )
        self.antler_log = self.antler_log_path.open("w", encoding="utf-8")
        self.antler = subprocess.Popen(
            [str(MOOS_BIN_DIR / "pAntler"), str(self.mission_path)],
            cwd=self.temp_path,
            stdout=self.antler_log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        self.wait_for_moosdb()
        time.sleep(0.5)
        self.poke(
            "PEARL_BATTERY_SOC=80",
            "PEARL_BATTERY_DATA_VALID=true",
            "PEARL_WIND_SPEED=1",
            "PEARL_WIND_DATA_VALID=true",
            "DEPLOY=true",
            "RETURN=false",
            "STATION_KEEP=false",
            "MOOS_MANUAL_OVERRIDE=false",
            "UAV_FSM_OPERATION_STATE:=IDLE",
        )

    def tearDown(self) -> None:
        if self.antler.poll() is None:
            os.killpg(self.antler.pid, signal.SIGTERM)
            try:
                self.antler.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(self.antler.pid, signal.SIGKILL)
                self.antler.wait(timeout=3)
        self.antler_log.close()
        self.temp_dir.cleanup()

    def wait_for_moosdb(self) -> None:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if self.antler.poll() is not None:
                self.fail("pAntler exited before MOOSDB became available")
            try:
                with socket.create_connection(
                    ("127.0.0.1", self.moos_port), timeout=0.2
                ):
                    return
            except OSError:
                time.sleep(0.1)
        self.fail("MOOSDB did not become available within five seconds")

    def poke(self, *pairs: str) -> None:
        result = subprocess.run(
            [str(MOOS_BIN_DIR / "uPokeDB"), str(self.mission_path), "--quiet", *pairs],
            cwd=self.temp_path,
            capture_output=True,
            text=True,
            timeout=6,
            check=False,
        )
        if result.returncode != 0:
            self.fail(f"uPokeDB failed: {pairs}\n{result.stdout}\n{result.stderr}")

    def assert_query(self, *conditions: str, wait: float = 5) -> None:
        result = subprocess.run(
            [
                str(MOOS_BIN_DIR / "uQueryDB"),
                str(self.mission_path),
                *(f"--condition={condition}" for condition in conditions),
                f"--wait={wait}",
            ],
            cwd=self.temp_path,
            capture_output=True,
            text=True,
            timeout=wait + 3,
            check=False,
        )
        if result.returncode != 0:
            output = self.antler_log_path.read_text(encoding="utf-8", errors="replace")
            self.fail(
                f"MOOS conditions did not become true: {conditions}\n"
                f"{result.stdout}\n{result.stderr}\npAntler:\n{output}"
            )

    def request_takeoff(self, session: str) -> None:
        self.poke(f"TAKEOFF_REQUEST:=id={session}#status=requested")

    def test_no_early_support(self) -> None:
        self.request_takeoff("held_parent")
        self.assert_query(
            "TAKEOFF_REQUEST_STATE==open",
            "PEARL_FSM_OPERATION_STATE==NORMAL_OPERATION",
            "TAKEOFF_SUPPORT_ACTIVE==false",
            wait=2,
        )

    def test_postcheck_failure_cancels_and_restores(self) -> None:
        self.request_takeoff("failed_postcheck")
        self.assert_query(
            "PEARL_FSM_OPERATION_STATE==TAKEOFF_SUPPORT",
            "TAKEOFF_SUPPORT_ACTIVE==true",
            "PEARL_FSM_TAKEOFF_SUPPORT_STATE==POST_INTERRUPTION_CHECKS",
            wait=8,
        )
        self.poke("PEARL_WIND_SPEED=10", "PEARL_WIND_DATA_VALID=true")
        self.assert_query(
            "PEARL_FSM_OPERATION_STATE==NORMAL_OPERATION",
            "PEARL_TAKEOFF_CONTROL_RESTORED==true",
            "STATION_KEEP==false",
            "TAKEOFF_REQUEST_STATE==inactive",
            wait=8,
        )

    def test_happy_path_is_state_gated_and_restores_controls(self) -> None:
        self.request_takeoff("happy_path")
        self.assert_query(
            "PEARL_FSM_OPERATION_STATE==TAKEOFF_SUPPORT",
            "TAKEOFF_SUPPORT_ACTIVE==true",
            "PEARL_FSM_TAKEOFF_SUPPORT_STATE==POST_INTERRUPTION_CHECKS",
            "TAKEOFF_CLEARANCE_STATE==granted",
        )

        self.poke("UAV_FSM_OPERATION_STATE:=TAKEOFF_OPERATIONS")
        self.assert_query(
            "PEARL_FSM_TAKEOFF_SUPPORT_STATE==AWAITING_DRONE_DEPARTURE"
        )
        self.poke("UAV_FSM_OPERATION_STATE:=MISSION_EXECUTION")
        self.assert_query(
            "PEARL_FSM_OPERATION_STATE==NORMAL_OPERATION",
            "PEARL_TAKEOFF_CONTROL_RESTORED==true",
            "DEPLOY==true",
            "RETURN==false",
            "STATION_KEEP==false",
            "MOOS_MANUAL_OVERRIDE==false",
            "TAKEOFF_REQUEST_STATE==inactive",
        )


if __name__ == "__main__":
    unittest.main()
