#!/usr/bin/env python3
"""Verify that the SIM pre-arm facts satisfy the same typed checks as REAL."""

from __future__ import annotations

import json
import re
import signal
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

from test_checks import CHECK_BINARY, MOOS_BIN, ROOT, free_port

MISSION = ROOT / "missions/charles_river/2026_09_29_chevron_demo"


def config_block(source: Path, name: str) -> str:
    text = source.read_text(encoding="utf-8")
    match = re.search(rf"ProcessConfig = {name}\s*\{{.*?^\}}", text,
                      re.MULTILINE | re.DOTALL)
    if not match:
        raise RuntimeError(f"Missing {name} in {source}")
    return match.group()


class SimFlagsIntegrationTest(unittest.TestCase):
    def test_numeric_sim_telemetry_passes_all_typed_prechecks(self) -> None:
        for binary in (CHECK_BINARY, MOOS_BIN / "MOOSDB", MOOS_BIN / "pAntler",
                       MOOS_BIN / "uTimerScript", MOOS_BIN / "uPokeDB",
                       MOOS_BIN / "uQueryDB"):
            if not binary.is_file():
                raise RuntimeError(f"Missing test dependency: {binary}")
        with tempfile.TemporaryDirectory(prefix="ucheck-sim-flags-") as temp:
            root = Path(temp)
            mission = root / "sim_flags.moos"
            names = ("uSimUavBattery", "uSimUavInitialArmed",
                     "uSimUavGroundTelemetry", "uPretakeoffChecks")
            runs = "\n".join(
                f"  Run = {MOOS_BIN / 'uTimerScript' if name.startswith('uSim') else CHECK_BINARY} "
                f"@ NewConsole=false ~ {name}" for name in names
            )
            blocks = "\n\n".join(
                config_block(MISSION / ("plug_uav_sim.moos" if name.startswith("uSim")
                                        else "meta_uav.moos"), name)
                for name in names
            )
            # The mission's typed rules are identical for SIM and REAL.
            self.assertNotIn("#ifdef", blocks)
            mission.write_text(
                f"ServerHost=127.0.0.1\nServerPort={free_port()}\n"
                f"Community=sim_flags_test\nProcessConfig=ANTLER\n{{\n"
                f"  MSBetweenLaunches=100\n"
                f"  Run = {MOOS_BIN / 'MOOSDB'} @ NewConsole=false\n{runs}\n}}\n"
                f"{blocks}\n", encoding="utf-8",
            )
            with (root / "antler.log").open("w", encoding="utf-8") as log:
                antler = subprocess.Popen(
                    [str(MOOS_BIN / "pAntler"), str(mission)], cwd=root,
                    stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
                )
                try:
                    time.sleep(2)
                    self.assertIsNone(antler.poll(), "pAntler exited before setup")
                    poke = subprocess.run(
                        [str(MOOS_BIN / "uPokeDB"), str(mission), "--quiet",
                         "SIM_ATTACHMENT_ATTACHED=1", "ROUTE_READY:=true",
                         "UAV_FSM_OPERATION_STATE_TAKEOFF_OPERATIONS_EPOCH:=sim-task-1",
                         "UAV_FSM_TAKEOFF_OPERATIONS_STATE:=ESTABLISHING_TAKEOFF_READINESS"],
                        cwd=root, capture_output=True, text=True, timeout=8,
                    )
                    self.assertEqual(poke.returncode, 0, poke.stdout + poke.stderr)
                    query = subprocess.run(
                        [str(MOOS_BIN / "uQueryDB"), str(mission),
                         "--condition=TAKEOFF_PRECHECK_GPS=success",
                         "--condition=TAKEOFF_PRECHECK_BATTERY=success",
                         "--condition=TAKEOFF_PRECHECK_ARMABLE=success",
                         "--condition=TAKEOFF_PRECHECK_FLIGHT_PLAN=success",
                         "--condition=TAKEOFF_PRECHECKS=success",
                         "--wait=8"],
                        cwd=root, capture_output=True, text=True, timeout=12,
                    )
                    if query.returncode:
                        log.flush()
                    self.assertEqual(query.returncode, 0,
                                     query.stdout + query.stderr +
                                     (root / "antler.log").read_text(encoding="utf-8"))
                    snapshot_query = subprocess.run(
                        [str(MOOS_BIN / "uQueryDB"), str(mission),
                         "--condition=TAKEOFF_PRECHECKS=success",
                         "--check_var=TAKEOFF_PRECHECKS_SNAPSHOT", "--vo", "--wait=6"],
                        cwd=root, capture_output=True, text=True, timeout=12,
                    )
                    self.assertEqual(snapshot_query.returncode, 0,
                                     snapshot_query.stdout + snapshot_query.stderr)
                    snapshot = json.loads((root / ".checkvars").read_text(
                        encoding="utf-8").partition("=")[2])
                    self.assertEqual(snapshot["session"]["id"], "sim-task-1")
                    for session in ("session-one", "session-two"):
                        poke = subprocess.run(
                            [str(MOOS_BIN / "uPokeDB"), str(mission), "--quiet",
                             "UAV_FSM_TAKEOFF_OPERATIONS_STATE:=REQUESTING_TAKEOFF_CLEARANCE",
                             f"UAV_FSM_OPERATION_STATE_TAKEOFF_OPERATIONS_EPOCH:={session}",
                             "UAV_TAKEOFF_REQUEST_ID:=clearance-independent"],
                            cwd=root, capture_output=True, text=True, timeout=15,
                        )
                        self.assertEqual(poke.returncode, 0, poke.stdout + poke.stderr)
                        time.sleep(1)  # Let uChecks invalidate the previous session.
                        query = subprocess.run(
                            [str(MOOS_BIN / "uQueryDB"), str(mission),
                             "--condition=TAKEOFF_PRECHECKS=success",
                             "--check_var=TAKEOFF_PRECHECKS_STATE",
                             "--vo", "--wait=12"], cwd=root,
                            capture_output=True, text=True, timeout=20,
                        )
                        self.assertEqual(query.returncode, 0, query.stdout + query.stderr)
                        state = (root / ".checkvars").read_text(encoding="utf-8")
                        self.assertIn(f"session={session}", state)
                finally:
                    if antler.poll() is None:
                        import os
                        os.killpg(antler.pid, signal.SIGTERM)
                        try:
                            antler.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            os.killpg(antler.pid, signal.SIGKILL)
                            antler.wait(timeout=3)


if __name__ == "__main__":
    unittest.main()
