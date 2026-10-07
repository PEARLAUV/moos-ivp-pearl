#!/usr/bin/env python3
"""Integration regression for a parent exit guarded by a child state's dwell."""

from __future__ import annotations

import os
import re
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
MOOS_BIN_DIR = Path(os.environ.get("MOOS_BIN_DIR", REPO_ROOT.parent / "moos-ivp" / "bin"))
FSM_BINARY = Path(os.environ.get("PFSMRUNTIME_BINARY", REPO_ROOT / "bin" / "pFSMRuntime"))


class FSMMinimumStateTimeTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory(prefix="fsm-min-state-time-")
        self.addCleanup(self.temp_dir.cleanup)
        self.path = Path(self.temp_dir.name)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        self.mission = self.path / "dwell.moos"
        self.log_dir = self.path / "logs"
        self.log_dir.mkdir()
        self.mission.write_text(
            f"""ServerHost = 127.0.0.1
ServerPort = {port}
Community = dwell_test

ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = {MOOS_BIN_DIR / 'MOOSDB'} @ NewConsole = false
  Run = {MOOS_BIN_DIR / 'pLogger'} @ NewConsole = false ~ pLogger
  Run = {FSM_BINARY} @ NewConsole = false ~ pFSMRuntime
}}

ProcessConfig = pLogger
{{
  AppTick = 20
  CommsTick = 20
  File = dwell
  PATH = {self.log_dir}
  AsyncLog = true
  FileTimeStamp = false
  WildCardLogging = false
  Log = ROOT_STATE @ 0
  Log = CHILD_STATE @ 0
}}

ProcessConfig = pFSMRuntime
{{
  AppTick = 20
  CommsTick = 20
  min_state_time = 1.5
  runtime = state_var=DWELL_RUNTIME, heartbeat_var=DWELL_HEARTBEAT, epoch_var=DWELL_EPOCH
  initialize = var=CHILD_GO, type=string, value=false
  initialize = var=EXIT_GO, type=string, value=false
  machine = id=root, state_var=ROOT_STATE, initial_state=A, initially_active=true, status_var=ROOT_STATUS, transition_var=ROOT_TRANSITION, active_state_var=ROOT_ACTIVE
  state = machine=root, value=A
  state = machine=root, value=B
  substate = machine=root, parent=A, var=CHILD_STATE
  transition = machine=root, id=EXIT, from=A, to=B, guard="(CHILD_STATE=D) and (EXIT_GO=true)"
  machine = id=child, state_var=CHILD_STATE, initial_state=C, parent_state_var=ROOT_STATE, parent_state=A, status_var=CHILD_STATUS, transition_var=CHILD_TRANSITION
  state = machine=child, value=C
  state = machine=child, value=D
  transition = machine=child, id=NEXT, from=C, to=D, guard="CHILD_GO=true"
}}
""",
            encoding="utf-8",
        )
        self.log = (self.path / "antler.log").open("w", encoding="utf-8")
        self.addCleanup(self.log.close)
        self.antler = subprocess.Popen(
            [str(MOOS_BIN_DIR / "pAntler"), str(self.mission)],
            cwd=self.path,
            stdout=self.log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        self.addCleanup(self.stop_antler)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if self.antler.poll() is not None:
                self.fail("pAntler exited before MOOSDB started")
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                    break
            except OSError:
                time.sleep(0.05)
        else:
            self.fail("MOOSDB did not start:\n" + (self.path / "antler.log").read_text(encoding="utf-8"))

    def stop_antler(self) -> None:
        if self.antler.poll() is None:
            os.killpg(self.antler.pid, signal.SIGTERM)
            try:
                self.antler.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(self.antler.pid, signal.SIGKILL)
                self.antler.wait(timeout=3)

    def poke(self, *pairs: str) -> None:
        result = subprocess.run(
            [str(MOOS_BIN_DIR / "uPokeDB"), str(self.mission), "--quiet", *pairs],
            capture_output=True, text=True, timeout=6,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def query(self, condition: str, wait: float = 4) -> bool:
        result = subprocess.run(
            [str(MOOS_BIN_DIR / "uQueryDB"), str(self.mission),
             f"--condition={condition}", f"--wait={wait}"],
            capture_output=True, text=True, timeout=wait + 3,
        )
        return result.returncode == 0

    def test_child_terminal_state_must_dwell_before_parent_exit(self) -> None:
        self.assertTrue(
            self.query("ROOT_STATE==A"),
            (self.path / "antler.log").read_text(encoding="utf-8"),
        )
        self.assertTrue(self.query("CHILD_STATE==C"))
        # The parent has already satisfied its dwell before the child moves.
        time.sleep(1.7)
        self.poke("CHILD_GO=true", "EXIT_GO=true")
        self.assertTrue(self.query("ROOT_STATE==B", wait=5))
        self.assertTrue(self.query("CHILD_STATE==none"))
        self.stop_antler()

        alog = self.log_dir / "dwell" / "dwell.alog"
        self.assertTrue(alog.is_file())
        entries: dict[tuple[str, str], float] = {}
        for line in alog.read_text(encoding="utf-8").splitlines():
            match = re.match(r"^\s*(\d+(?:\.\d+)?)\s+(\S+)\s+pFSMRuntime\s+(.*)$", line)
            if match:
                entries[(match[2], match[3].strip())] = float(match[1])
        root_a = entries[("ROOT_STATE", "A")]
        child_d = entries[("CHILD_STATE", "D")]
        root_b = entries[("ROOT_STATE", "B")]
        self.assertGreater(root_b - root_a, 1.5)
        self.assertGreaterEqual(root_b - child_d, 1.4)


if __name__ == "__main__":
    unittest.main()
