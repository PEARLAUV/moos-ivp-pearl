#!/usr/bin/env python3

"""Integration checks for pFSMRuntime state entry and exit effects."""

from __future__ import annotations

import json
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
MOOS_BIN_DIR = Path(
    os.environ.get("MOOS_BIN_DIR", REPO_ROOT.parent / "moos-ivp" / "bin")
)
FSM_BINARY = Path(
    os.environ.get("PFSMRUNTIME_BINARY", REPO_ROOT / "bin" / "pFSMRuntime")
)


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


class FSMRuntimeEffectsIntegrationTest(unittest.TestCase):
    def setUp(self) -> None:
        required = [
            MOOS_BIN_DIR / "MOOSDB",
            MOOS_BIN_DIR / "pAntler",
            MOOS_BIN_DIR / "pLogger",
            MOOS_BIN_DIR / "uPokeDB",
            MOOS_BIN_DIR / "uQueryDB",
            FSM_BINARY,
        ]
        missing = [str(path) for path in required if not path.is_file()]
        if missing:
            raise RuntimeError("Build dependencies are missing: " + ", ".join(missing))

        self.temp_dir = tempfile.TemporaryDirectory(prefix="fsm-runtime-effects-")
        self.temp_path = Path(self.temp_dir.name)
        self.log_path = self.temp_path / "logs"
        self.log_path.mkdir()
        self.mission_path = self.temp_path / "effects.moos"
        self.antler_log_path = self.temp_path / "pAntler.log"
        self.mission_path.write_text(
            f"""ServerHost = 127.0.0.1
ServerPort = {free_port()}
Community  = fsm_effects_test

ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 150
  Run = {MOOS_BIN_DIR / 'MOOSDB'} @ NewConsole = false
  Run = {MOOS_BIN_DIR / 'pLogger'} @ NewConsole = false ~ pLogger
  Run = {FSM_BINARY} @ NewConsole = false ~ pFSMRuntime
}}

ProcessConfig = pLogger
{{
  AppTick = 20
  CommsTick = 20
  File = effects
  PATH = {self.log_path}
  AsyncLog = true
  FileTimeStamp = false
  WildCardLogging = false
  Log = ROOT_STATE @ 0
  Log = CHILD_STATE @ 0
  Log = ROOT_A_ENTRY @ 0
  Log = ROOT_A_EXIT @ 0
  Log = ROOT_B_ENTRY @ 0
  Log = ROOT_B_ATTEMPTS @ 0
  Log = CHILD_C_ENTRY @ 0
  Log = CHILD_C_EXIT @ 0
  Log = AUTO_STATE @ 0
  Log = AUTO_A_ENTRY @ 0
  Log = AUTO_A_EXIT @ 0
  Log = AUTO_B_ENTRY @ 0
  Log = FSM_TRANSITION @ 0
  Log = RESET_R1_EPOCH @ 0
}}

ProcessConfig = pFSMRuntime
{{
  AppTick = 20
  CommsTick = 20
  min_state_time = 0
  runtime = state_var=FX_RUNTIME_STATE, heartbeat_var=FX_RUNTIME_HEARTBEAT, epoch_var=FX_RUNTIME_EPOCH, heartbeat_interval=1
  initialize = var=GO, type=string, value=false
  initialize = var=ENABLED, type=string, value=true
  initialize = var=RESET_ENABLED, type=string, value=true
  initialize = var=RESET_GO, type=string, value=false
  initialize = var=RESET_OTHER, type=string, value=false
  initialize = var=RESET_COUNT, type=double, value=2
  initialize = var=ROOT_A_ENTRY, type=string, value=false
  initialize = var=ROOT_A_EXIT, type=string, value=false
  initialize = var=ROOT_B_ENTRY, type=string, value=false
  initialize = var=ROOT_B_ATTEMPTS, type=double, value=0
  initialize = var=CHILD_C_ENTRY, type=string, value=false
  initialize = var=CHILD_C_EXIT, type=string, value=false
  initialize = var=AUTO_A_ENTRY, type=string, value=false
  initialize = var=AUTO_A_EXIT, type=string, value=false
  initialize = var=AUTO_B_ENTRY, type=string, value=false

  machine = id=root, state_var=ROOT_STATE, initial_state=A, initially_active=true, status_var=ROOT_STATUS, transition_var=ROOT_TRANSITION, active_state_var=ROOT_ACTIVE, activation_condition="ENABLED=true", condition_history=true
  state = machine=root, value=A
  state = machine=root, value=B
  effect = machine=root, state=A, phase=entry, var=ROOT_A_ENTRY, type=string, value=true
  effect = machine=root, state=A, phase=exit, var=ROOT_A_EXIT, type=string, value=true
  effect = machine=root, state=B, phase=entry, var=ROOT_B_ENTRY, type=string, value=true
  effect = machine=root, state=B, phase=entry, var=ROOT_B_ATTEMPTS, type=double, value=2.5
  substate = machine=root, parent=A, var=CHILD_STATE
  transition = machine=root, id=GO, from=A, to=B, guard="GO=true"

  machine = id=child, state_var=CHILD_STATE, initial_state=C, initially_active=false, status_var=CHILD_STATUS, transition_var=CHILD_TRANSITION, parent_state_var=ROOT_STATE, parent_state=A, reset_on_inactive=true
  state = machine=child, value=C
  effect = machine=child, state=C, phase=entry, var=CHILD_C_ENTRY, type=string, value=true
  effect = machine=child, state=C, phase=exit, var=CHILD_C_EXIT, type=string, value=true

  machine = id=reset_root, state_var=RESET_ROOT_STATE, initial_state=R1, initially_active=true, status_var=RESET_ROOT_STATUS, transition_var=RESET_ROOT_TRANSITION, active_state_var=RESET_ROOT_ACTIVE, activation_condition="RESET_ENABLED=true", condition_history=false
  state = machine=reset_root, value=R1, entry_epoch_var=RESET_R1_EPOCH
  state = machine=reset_root, value=R2
  effect = machine=reset_root, state=R1, phase=exit, var=RESET_GO, type=string, value=false
  substate = machine=reset_root, parent=R2, var=RESET_CHILD_STATE
  transition = machine=reset_root, id=RESET_ADVANCE, from=R1, to=R2, guard="RESET_GO=true"
  transition = machine=reset_root, id=RESET_ALTERNATIVE, from=R1, to=R2, guard="RESET_OTHER=true"
  transition = machine=reset_root, id=RESET_NUMERIC, from=R1, to=R2, guard="RESET_COUNT=3"
  transition = machine=reset_root, id=RESET_MISSING, from=R1, to=R2, guard="RESET_UNSEEN=true"

  machine = id=reset_child, state_var=RESET_CHILD_STATE, initial_state=RC, initially_active=false, status_var=RESET_CHILD_STATUS, transition_var=RESET_CHILD_TRANSITION, parent_state_var=RESET_ROOT_STATE, parent_state=R2, reset_on_inactive=false
  state = machine=reset_child, value=RC

  machine = id=auto, state_var=AUTO_STATE, initial_state=A, initially_active=true, status_var=AUTO_STATUS, transition_var=AUTO_TRANSITION
  state = machine=auto, value=A
  state = machine=auto, value=B
  effect = machine=auto, state=A, phase=entry, var=AUTO_A_ENTRY, type=string, value=true
  effect = machine=auto, state=A, phase=exit, var=AUTO_A_EXIT, type=string, value=true
  effect = machine=auto, state=B, phase=entry, var=AUTO_B_ENTRY, type=string, value=true
  transition = machine=auto, id=AUTO_ADVANCE, from=A, to=B, guard="AUTO_STATE=A"

  machine = id=checks, state_var=CHECK_STATE, initial_state=WAITING, initially_active=true, status_var=CHECK_STATUS, transition_var=CHECK_TRANSITION
  state = machine=checks, value=WAITING
  state = machine=checks, value=DONE
  effect = machine=checks, state=WAITING, phase=exit, var=CHECK_SNAPSHOT, type=string, value=cleared
  transition = machine=checks, id=CHECK_DONE, from=WAITING, to=DONE, guard="CHECK_RESULT=success", check_snapshots=CHECK_RESULT:CHECK_SNAPSHOT
  transition = machine=checks, id=CHECK_OTHER, from=WAITING, to=DONE, guard="CHECK_OTHER=success", check_snapshots=CHECK_OTHER:CHECK_MISSING_SNAPSHOT
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
                    ("127.0.0.1", self._mission_port()), timeout=0.2
                ):
                    return
            except OSError:
                time.sleep(0.1)
        self.fail("MOOSDB did not become available within five seconds")

    def _mission_port(self) -> int:
        text = self.mission_path.read_text(encoding="utf-8")
        match = re.search(r"ServerPort\s*=\s*(\d+)", text)
        assert match
        return int(match.group(1))

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

    def assert_query(self, *conditions: str, wait: float = 4) -> None:
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

    def stop_and_read_alog(self) -> list[tuple[str, str]]:
        os.killpg(self.antler.pid, signal.SIGTERM)
        try:
            self.antler.wait(timeout=6)
        except subprocess.TimeoutExpired:
            os.killpg(self.antler.pid, signal.SIGKILL)
            self.antler.wait(timeout=3)
        alog = self.log_path / "effects" / "effects.alog"
        deadline = time.monotonic() + 2
        while not alog.is_file() and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertTrue(alog.is_file(), "pLogger did not produce effects.alog")

        events: list[tuple[str, str]] = []
        pattern = re.compile(r"^\s*\d+(?:\.\d+)?\s+(\S+)\s+pFSMRuntime\s+(.*)$")
        alog_text = alog.read_text(encoding="utf-8", errors="replace")
        for line in alog_text.splitlines():
            match = pattern.match(line)
            if match:
                events.append((match.group(1), match.group(2).strip()))
        antler_text = self.antler_log_path.read_text(
            encoding="utf-8", errors="replace"
        )
        self.assertTrue(
            events,
            f"no pFSMRuntime events found in alog:\n{alog_text}\n"
            f"pAntler:\n{antler_text}",
        )
        return events

    @staticmethod
    def event_index(
        events: list[tuple[str, str]], variable: str, value: str, start: int = 0
    ) -> int:
        for index in range(start, len(events)):
            if events[index] == (variable, value):
                return index
        raise AssertionError(f"missing logged event {variable}={value}: {events}")

    def test_condition_deactivates_hierarchy_and_restores_deep_history(self) -> None:
        self.assert_query("ROOT_STATE==A", "CHILD_STATE==C")
        self.poke("ENABLED=false")
        self.assert_query(
            "ROOT_STATE==none",
            "CHILD_STATE==none",
            "ROOT_ACTIVE==none",
            "ROOT_A_EXIT==false",
            "CHILD_C_EXIT==false",
        )
        self.poke("ENABLED=true")
        self.assert_query(
            "ROOT_STATE==A",
            "CHILD_STATE==C",
            "ROOT_STATE_A_COUNT==1",
            "CHILD_STATE_C_COUNT==1",
        )

        self.poke("GO=true")
        self.assert_query("ROOT_STATE==B", "CHILD_STATE==none")
        self.poke("ENABLED=false")
        self.assert_query("ROOT_STATE==none", "CHILD_STATE==none")
        self.poke("ENABLED=true")
        self.assert_query("ROOT_STATE==B", "CHILD_STATE==none")

    def test_condition_can_reset_complete_hierarchy(self) -> None:
        self.assert_query("RESET_ROOT_STATE==R1", "RESET_CHILD_STATE==none")
        self.poke("RESET_GO=true")
        self.assert_query("RESET_ROOT_STATE==R2", "RESET_CHILD_STATE==RC")
        self.poke("RESET_ENABLED=false", "RESET_GO=false")
        self.assert_query(
            "RESET_ROOT_STATE==none",
            "RESET_CHILD_STATE==none",
            "RESET_ROOT_ACTIVE==none",
        )
        self.poke("RESET_ENABLED=true")
        self.assert_query("RESET_ROOT_STATE==R1", "RESET_CHILD_STATE==none",
                          "RESET_ROOT_STATE_R1_COUNT==2")
        events = self.stop_and_read_alog()
        epochs = [value for var, value in events if var == "RESET_R1_EPOCH"]
        self.assertEqual(len(epochs), 2)
        self.assertEqual(epochs[0].split(":")[0], epochs[1].split(":")[0])
        self.assertEqual([value.rsplit(":", 1)[1] for value in epochs], ["1", "2"])

    def test_json_transition_batches_include_child_and_all_candidate_inputs(self) -> None:
        self.assert_query("RESET_ROOT_STATE==R1", "RESET_CHILD_STATE==none")
        self.poke("RESET_GO=true")
        self.assert_query("RESET_ROOT_STATE==R2", "RESET_CHILD_STATE==RC",
                          "RESET_GO==false")
        events = self.stop_and_read_alog()
        batches = [json.loads(value) for var, value in events
                   if var == "FSM_TRANSITION"]
        initial_group = next(group for group in batches
                             if any(item["machine"] == "root" and item["id"] == "INITIAL"
                                    for item in group["transitions"]))
        self.assertIn("child", [item["machine"] for item in initial_group["transitions"]])
        self.assertEqual(initial_group["context"], {})
        group = next(group for group in batches
                     if any(item["id"] == "RESET_ADVANCE"
                            for item in group["transitions"]))
        self.assertEqual([item["id"] for item in group["transitions"]],
                         ["RESET_ADVANCE", "INITIAL"])
        self.assertLessEqual(initial_group["transition_time"],
                             group["transition_time"])
        self.assertEqual(group["context"],
                         {"RESET_GO": "true", "RESET_OTHER": "false",
                          "RESET_COUNT": 2, "RESET_UNSEEN": None})
        self.assertTrue(all("context" not in item and "transition_time" not in item
                            for item in group["transitions"]))
        identifiers = [item["occurrence_id"] for group in batches
                       for item in group["transitions"]]
        self.assertEqual(identifiers, list(range(1, len(identifiers) + 1)))
        self.assertTrue(all(isinstance(batch["transition_time"], (int, float))
                            and batch["transition_time"] > 0
                            for batch in batches))

    def test_check_snapshots_replace_scalar_context_before_exit_effects(self) -> None:
        self.assert_query("CHECK_STATE==WAITING")
        snapshot = {"aggregate": {"status": "success"},
                    "session": {"id": "demo-1"},
                    "checks": {"GPS": {"status": "success"}}}
        self.poke("CHECK_SNAPSHOT:=" + json.dumps(snapshot, separators=(",", ":")))
        time.sleep(0.2)  # Allow the FSM subscription to cache the snapshot.
        self.poke("CHECK_RESULT:=success")
        self.assert_query("CHECK_STATE==DONE", "CHECK_SNAPSHOT==cleared")
        events = self.stop_and_read_alog()
        batches = [json.loads(value) for var, value in events
                   if var == "FSM_TRANSITION"]
        group = next(group for group in batches
                     if any(item["id"] == "CHECK_DONE"
                            for item in group["transitions"]))
        self.assertEqual(group["context"],
                         {"CHECK_SNAPSHOT": snapshot,
                          "CHECK_MISSING_SNAPSHOT": None})
        self.assertNotIn("CHECK_RESULT", group["context"])
        self.assertNotIn("CHECK_OTHER", group["context"])

    def test_unconditional_transition_runs_entry_and_exit_effects(self) -> None:
        # The generator emits a source-state guard for an omitted `when`.
        self.assert_query(
            "AUTO_STATE==B", "AUTO_A_ENTRY==true",
            "AUTO_A_EXIT==true", "AUTO_B_ENTRY==true",
        )

    def test_effect_values_and_publication_order(self) -> None:
        self.assert_query(
            "ROOT_STATE==A",
            "ROOT_A_ENTRY==true",
            "CHILD_STATE==C",
            "CHILD_C_ENTRY==true",
        )
        self.poke("GO=true")
        self.assert_query(
            "ROOT_STATE==B",
            "CHILD_STATE==none",
            "CHILD_C_EXIT==true",
            "ROOT_A_EXIT==true",
            "ROOT_B_ENTRY==true",
            "ROOT_B_ATTEMPTS==2.5",
        )

        events = self.stop_and_read_alog()
        root_a = self.event_index(events, "ROOT_STATE", "A")
        root_a_entry = self.event_index(events, "ROOT_A_ENTRY", "true", root_a)
        child_c = self.event_index(events, "CHILD_STATE", "C", root_a_entry)
        child_c_entry = self.event_index(events, "CHILD_C_ENTRY", "true", child_c)
        self.assertLess(root_a, root_a_entry)
        self.assertLess(child_c, child_c_entry)

        child_exit = self.event_index(events, "CHILD_C_EXIT", "true")
        child_none = self.event_index(events, "CHILD_STATE", "none", child_exit)
        root_exit = self.event_index(events, "ROOT_A_EXIT", "true", child_none)
        root_b = self.event_index(events, "ROOT_STATE", "B", root_exit)
        root_b_entry = self.event_index(events, "ROOT_B_ENTRY", "true", root_b)
        root_b_attempts = self.event_index(
            events, "ROOT_B_ATTEMPTS", "2.50000", root_b_entry
        )
        self.assertLess(child_exit, child_none)
        self.assertLess(child_none, root_exit)
        self.assertLess(root_exit, root_b)
        self.assertLess(root_b, root_b_entry)
        self.assertLess(root_b_entry, root_b_attempts)


if __name__ == "__main__":
    unittest.main()
