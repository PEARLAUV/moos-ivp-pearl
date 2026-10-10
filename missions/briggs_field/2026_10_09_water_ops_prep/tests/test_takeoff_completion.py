#!/usr/bin/env python3
"""Real isolated uChecks + FSM regression for the taking-off completion guard."""

import os
import re
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
ROOT = MISSION.parents[2]


class TakeoffCompletion(unittest.TestCase):
    def test_every_check_collection_declares_required_prefix(self):
        for file in ("meta_uav.moos", "meta_pearl.moos"):
            text = (MISSION / file).read_text()
            aliases = re.findall(r"Run = uChecks\s+@[^\n]+~ (\w+)", text)
            for alias in aliases:
                config = re.search(rf"ProcessConfig = {alias}\s*\{{([^}}]+)\}}", text)
                self.assertIsNotNone(config, alias)
                if config is not None:
                    self.assertIn("result_prefix =", config[1], alias)

    def test_fresh_armed_in_air_evidence_exits_takeoff(self):
        source = (MISSION / "meta_uav.moos").read_text()
        config = re.search(r"ProcessConfig = uAirborneChecks\s*\{[^}]+\}", source)
        self.assertIsNotNone(config)
        assert config is not None
        extra = "\n".join(
            re.findall(
                r"ProcessConfig = uLanding(?:Airborne|Ground)Checks\s*\{[^}]+\}", source
            )
        )
        secondary = re.findall(
            r"ProcessConfig = uSecondary(?:Land|Sequence)\s*\{[^}]+\}",
            (MISSION / "plug_uav_ardupilot.moos").read_text(),
        )
        generated = (MISSION / "plug_uav_fsm_runtime.moos").read_text()
        with tempfile.TemporaryDirectory(prefix="takeoff-completion-") as temp:
            directory = Path(temp)
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            mission = directory / "probe.moos"
            mission.write_text(f"""ServerHost = localhost
ServerPort = {port}
Community = takeoff_probe
ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = MOOSDB @ NewConsole = false
  Run = {ROOT / "bin/pFSMRuntime"} @ NewConsole = false
  Run = {ROOT / "bin/uChecks"} @ NewConsole = false ~ uAirborneChecks
  Run = {ROOT / "bin/uChecks"} @ NewConsole = false ~ uLandingAirborneChecks
  Run = {ROOT / "bin/uChecks"} @ NewConsole = false ~ uLandingGroundChecks
  Run = {ROOT / "bin/pLandingCoordinator"} @ NewConsole = false
  Run = uTimerScript @ NewConsole = false ~ uSecondarySequence
  Run = uTimerScript @ NewConsole = false ~ uSecondaryLand
}}
ProcessConfig = pFSMRuntime
{{
  AppTick = 20
  CommsTick = 20
{generated}
}}
{config[0]}
{extra}
{chr(10).join(secondary)}
ProcessConfig = pLandingCoordinator
{{
  AppTick = 20
  CommsTick = 20
  ownship = uav
  peer_node = pearl
  role = aircraft
  input_timeout = 30
  rendezvous_condition = UAV_FSM_LANDING_OPERATIONS_STATE=CALCULATING_RENDEZVOUS_POINT
  command_condition = UAV_FSM_LANDING_OPERATIONS_STATE=APPROACHING
}}
""")
            with (directory / "antler.log").open("w") as output:
                process = subprocess.Popen(
                    ["pAntler", str(mission)],
                    cwd=directory,
                    stdout=output,
                    stderr=subprocess.STDOUT,
                    start_new_session=True,
                )
            try:

                def poke(*values):
                    subprocess.run(
                        ["uPokeDB", str(mission), "--quiet", *values],
                        capture_output=True,
                        check=True,
                        timeout=12,
                    )

                def query(*conditions, variable=None):
                    args = [
                        "uQueryDB",
                        str(mission),
                        "--wait=3",
                        *(f"--condition={c}" for c in conditions),
                    ]
                    if variable:
                        args.extend([f"--check_var={variable}", "--vo"])
                    result = subprocess.run(
                        args,
                        cwd=directory,
                        capture_output=True,
                        text=True,
                        check=False,
                        timeout=12,
                    )
                    self.assertEqual(
                        result.returncode,
                        0,
                        result.stdout
                        + result.stderr
                        + (directory / "antler.log").read_text(),
                    )
                    if variable:
                        return (
                            (directory / ".checkvars")
                            .read_text()
                            .strip()
                            .partition("=")[2]
                        )

                query("UAV_FSM_OPERATION_STATE=IDLE")
                poke("TASK_EXECUTE:=true", "ROUTE_READY:=true", "ROUTE_DEPLOY:=false")
                query("UAV_FSM_TAKEOFF_OPERATIONS_STATE=ESTABLISHING_TAKEOFF_READINESS")
                poke("TAKEOFF_PRECHECKS:=success", "TAKEOFF_REQUEST_RESULT:=APPROVED")
                poke(
                    "UAV_IS_ARMED=1",
                    "UAV_LANDED_STATE_AVAILABLE=1",
                    "UAV_LANDED_STATE:=TAKING_OFF",
                    "UAV_LANDED_STATE_AGE=0.1",
                )
                query(
                    "UAV_FSM_TAKEOFF_OPERATIONS_STATE=TAKEOFF",
                    "TAKEOFF_AIRBORNE=pending",
                )
                poke("UAV_LANDED_STATE:=IN_AIR", "UAV_LANDED_STATE_AGE=5")
                query(
                    "UAV_FSM_OPERATION_STATE=TAKEOFF_OPERATIONS",
                    "TAKEOFF_AIRBORNE=pending",
                )
                poke("UAV_IS_ARMED=0", "UAV_LANDED_STATE_AGE=0.1")
                query(
                    "UAV_FSM_OPERATION_STATE=TAKEOFF_OPERATIONS",
                    "TAKEOFF_AIRBORNE=pending",
                )
                poke(
                    "UAV_IS_ARMED=1",
                    "UAV_LANDED_STATE_AVAILABLE=1",
                    "UAV_LANDED_STATE:=IN_AIR",
                    "UAV_LANDED_STATE_AGE=0.1",
                )
                query(
                    "TAKEOFF_AIRBORNE=success",
                    "UAV_FSM_OPERATION_STATE=MISSION_EXECUTION",
                    "SIM_UAV_ON_SHIP=false",
                )
                # Full generated FSM + real check collections/coordinator:
                # an uncommanded touchdown must exit off-nominal without flight.
                poke("RETURN:=true")
                query("UAV_FSM_OPERATION_STATE=LANDING_OPERATIONS")
                poke(
                    "UAV_HEALTH_ALL_OK=1",
                    "UAV_HEALTH_AGE=0.1",
                    "UAV_GPS_AVAILABLE=1",
                    "UAV_GPS_AGE=0.1",
                    "UAV_IS_ARMED=1",
                    "UAV_LANDED_STATE_AVAILABLE=1",
                    "UAV_LANDED_STATE:=IN_AIR",
                    "UAV_LANDED_STATE_AGE=0.1",
                    "ARDU_COMMAND:=NONE",
                )
                session = query(
                    "LANDING_OWN_PHASE=REQUESTED", variable="LANDING_OWN_SESSION"
                )
                # A matched peer packet, isolated from all live communities.
                poke(
                    f"LANDING_EXCHANGE:=session={session}#node=pearl#phase=AGREED"
                    f"#started={time.time() - 1:.6f}#time={time.time():.6f}#rv_phase=IDLE"
                )
                query("LANDING_OWN_PHASE=RENDEZVOUS")
                poke(
                    "RENDEZVOUS_OWN_SESSION:=rv-test", "RENDEZVOUS_OWN_PHASE:=COMPLETE"
                )
                poke(
                    f"LANDING_EXCHANGE:=session={session}#node=pearl#phase=CLEARED"
                    f"#started={time.time() - 1:.6f}#time={time.time():.6f}#rv_phase=COMPLETE"
                )
                poke(
                    "UAV_HEALTH_ALL_OK=1",
                    "UAV_HEALTH_AGE=0.1",
                    "UAV_GPS_AVAILABLE=1",
                    "UAV_GPS_AGE=0.1",
                    "UAV_IS_ARMED=1",
                    "UAV_LANDED_STATE_AVAILABLE=1",
                    "UAV_LANDED_STATE:=IN_AIR",
                    "UAV_LANDED_STATE_AGE=0.1",
                )
                query("UAV_FSM_LANDING_OPERATIONS_STATE=APPROACHING")
                poke(
                    "UAV_LANDED_STATE_AVAILABLE=1",
                    "UAV_LANDED_STATE:=ON_GROUND",
                    "UAV_LANDED_STATE_AGE=0.1",
                    "UAV_IS_ARMED=0",
                )
                query(
                    "LANDING_OWN_PHASE=ABORTED",
                    "UAV_FSM_OPERATION_STATE=OFFNOMINAL",
                    "SECONDARY_ARRIVED=false",
                    "SIM_UAV_ON_SHIP=false",
                    "ARDU_COMMAND=NONE",
                )
            finally:
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGINT)
                    try:
                        process.wait(timeout=6)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
