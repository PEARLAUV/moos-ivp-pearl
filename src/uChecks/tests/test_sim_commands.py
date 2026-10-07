#!/usr/bin/env python3
"""SIM command responders follow coordinator flags and ARDU_COMMAND."""
from __future__ import annotations

import os
import re
import signal
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

from test_checks import MOOS_BIN, ROOT, free_port
from test_sim_flags import config_block

MISSION = ROOT / "missions/charles_river/2026_09_29_chevron_demo"


class SimCommandTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="uav-sim-commands-")
        self.root = Path(self.temp.name)
        (self.root / "logs").mkdir()
        self.mission = self.root / "commands.moos"
        names = ("uOperatorPrecLand", "uSimArmCommand",
                 "uSimTakeoffCommand", "uRouteSequence", "uSimPrecLandCommand",
                 "uSimAttachmentCommand")
        runs = "\n".join(f" Run = {MOOS_BIN / 'uTimerScript'} @ NewConsole=false ~ {name}"
                         for name in names)
        blocks = "\n\n".join(config_block(MISSION / ("plug_uav_sim.moos" if name.startswith("uSim") or name == "uRouteSequence"
                                                      else "meta_uav.moos"), name)
                               for name in names).replace("$(TAKEOFF_ALTITUDE)", "20")
        self.assertNotIn("#ifdef", blocks)
        self.mission.write_text(f"""ServerHost=127.0.0.1
ServerPort={free_port()}
Community=sim_commands
ProcessConfig=ANTLER
{{
 MSBetweenLaunches=100
 Run = {MOOS_BIN / 'MOOSDB'} @ NewConsole=false
 Run = {MOOS_BIN / 'pLogger'} @ NewConsole=false ~ pLogger
{runs}
}}
ProcessConfig = pLogger
{{
 AppTick = 20
 CommsTick = 20
 File = climb
 PATH = {self.root / 'logs'}
 AsyncLog = true
 FileTimeStamp = false
 WildCardLogging = false
 Log = NAV_ALTITUDE @ 0
}}
{blocks}
""", encoding="utf-8")
        self.log = (self.root / "antler.log").open("w", encoding="utf-8")
        self.antler = subprocess.Popen([str(MOOS_BIN / "pAntler"), str(self.mission)],
                                       cwd=self.root, stdout=self.log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
        time.sleep(1.5)
        self.assertIsNone(self.antler.poll())

    def tearDown(self) -> None:
        if self.antler.poll() is None:
            os.killpg(self.antler.pid, signal.SIGTERM)
            try:
                self.antler.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(self.antler.pid, signal.SIGKILL)
                self.antler.wait(timeout=3)
        self.log.close()
        self.temp.cleanup()

    def poke(self, *pairs: str) -> None:
        proc = subprocess.run([str(MOOS_BIN / "uPokeDB"), str(self.mission),
                               "--quiet", *pairs], capture_output=True,
                              text=True, timeout=30)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)

    def query(self, *conditions: str, wait: int = 9) -> None:
        proc = subprocess.run([str(MOOS_BIN / "uQueryDB"), str(self.mission),
                               *(f"--condition={c}" for c in conditions),
                               f"--wait={wait}"], capture_output=True,
                              text=True, timeout=wait + 12)
        if proc.returncode:
            self.log.flush()
            self.fail(proc.stdout + proc.stderr +
                      (self.root / "antler.log").read_text(encoding="utf-8"))

    def command_result(self, mode: str) -> str:
        proc = subprocess.run([str(MOOS_BIN / "uQueryDB"), str(self.mission),
                               f"--condition=AUTOPILOT_MODE=={mode}",
                               "--check_var=UAV_COMMAND_RESULT", "--vo", "--wait=6"],
                              cwd=self.root, capture_output=True, text=True, timeout=18)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        return (self.root / ".checkvars").read_text(encoding="utf-8")

    def test_coordinator_arm_and_takeoff_then_operator_precision_land(self) -> None:
        self.poke("ARM_UAV:=true")
        self.query("UAV_IS_ARMED==1", "AUTOPILOT_MODE==SIM_ARMED")
        self.poke("ARDU_COMMAND:=DO_TAKEOFF")
        self.query("SIM_ATTACHMENT_REQUEST==false", "UAV_LANDED_STATE==IN_AIR",
                   "NAV_ALTITUDE==20", "AUTOPILOT_MODE==SIM_HOLD")
        self.assertIn("command=TAKEOFF", self.command_result("SIM_HOLD"))
        self.poke("UAV_FSM_OPERATION_STATE:=LANDING_OPERATIONS",
                  "UAV_PREC_LAND_COMMIT:=true")
        self.query("ARDU_COMMAND==AUTOLAND")
        self.query("SIM_ATTACHMENT_REQUEST==false", "UAV_LANDED_STATE==ON_GROUND",
                   "UAV_IS_ARMED==0", "NAV_ALTITUDE==0",
                   "AUTOPILOT_MODE==SIM_LANDED")
        self.poke("UAV_FSM_OPERATION_STATE:=IDLE")
        self.query("SIM_ATTACHMENT_REQUEST==true")
        self.assertIn("command=LAND", self.command_result("SIM_LANDED"))
        os.killpg(self.antler.pid, signal.SIGTERM)
        self.antler.wait(timeout=6)
        log = (self.root / "logs/climb/climb.alog").read_text(encoding="utf-8")
        altitudes = [float(match.group(1)) for line in log.splitlines()
                     if (match := re.match(
                         r"^\s*\d+(?:\.\d+)?\s+NAV_ALTITUDE\s+\S+\s+"
                         r"([+-]?\d+(?:\.\d+)?)", line))]
        climb = [altitude for altitude in altitudes if altitude > 0]
        self.assertEqual(len(climb), 25, climb)
        for step, altitude in enumerate(climb, start=1):
            self.assertAlmostEqual(altitude, 20 * step / 25, places=4)

    def test_sim_route_sequence_waits_for_airborne_route_and_consumes_request(self) -> None:
        self.poke("ROUTE_DEPLOY:=false", "ROUTE_READY:=true",
                  "ROUTE_EXECUTE_PURPOSE:=operator",
                  "UAV_FSM_OPERATION_STATE:=TAKEOFF_OPERATIONS",
                  "ROUTE_EXECUTE:=true")
        time.sleep(0.6)
        self.query("ROUTE_DEPLOY==false", "ROUTE_EXECUTE==true")
        self.poke("ARM_UAV:=true")
        self.query("UAV_IS_ARMED==1")
        self.poke("ARDU_COMMAND:=DO_TAKEOFF")
        self.query("UAV_LANDED_STATE==IN_AIR")
        time.sleep(0.5)
        self.query("ROUTE_DEPLOY==false", "ROUTE_EXECUTE==true")
        self.poke("UAV_FSM_OPERATION_STATE:=MISSION_EXECUTION")
        self.query("ROUTE_DEPLOY==true", "ROUTE_EXECUTE==false")
        self.poke("ROUTE_DEPLOY:=false", "ROUTE_EXECUTE_PURPOSE:=recovery",
                  "UAV_FSM_OPERATION_STATE:=LANDING_OPERATIONS",
                  "ROUTE_EXECUTE:=true")
        self.query("ROUTE_DEPLOY==true", "ROUTE_EXECUTE==false")

    def test_mission_has_no_remote_arm_or_takeoff_publisher(self) -> None:
        uav = (MISSION / "meta_uav.moos").read_text(encoding="utf-8")
        shore = (MISSION / "meta_shoreside.moos").read_text(encoding="utf-8")
        self.assertNotIn("ProcessConfig = uOperatorArm", uav)
        self.assertNotIn("ProcessConfig = uOperatorTakeoff", uav)
        self.assertNotIn("var=UAV_ARM_REQUEST", uav)
        self.assertNotIn("val=DO_TAKEOFF", uav)
        self.assertNotIn("button_1 = UAV ARM", shore)
        self.assertNotIn("button_4 = UAV DEPLOY", shore)
        self.assertNotIn("button_5 = UAV CLEAR", shore)
        self.assertIn("~ uSimAttachmentCommand", uav)
        self.assertIn("right_context[uav-clear] = ROUTE_BUFFER_CLEAR=true", shore)
        self.assertIn("right_context[uav-clear] = NODE_MESSAGE_LOCAL=", shore)

    def test_takeoff_requires_armed_telemetry_and_rearms_next_cycle(self) -> None:
        self.poke("ARDU_COMMAND:=DO_TAKEOFF")
        time.sleep(0.4)
        self.poke("ARM_UAV:=true")
        self.query("UAV_LANDED_STATE==IN_AIR", "NAV_ALTITUDE==20")
        self.poke("UAV_FSM_OPERATION_STATE:=LANDING_OPERATIONS",
                  "ARDU_COMMAND:=AUTOLAND")
        self.query("UAV_LANDED_STATE==ON_GROUND", "UAV_IS_ARMED==0",
                   "ARM_UAV==false", "SIM_ATTACHMENT_REQUEST==false")
        self.poke("UAV_FSM_OPERATION_STATE:=OFFNOMINAL")
        time.sleep(0.5)
        self.query("SIM_ATTACHMENT_REQUEST==false")
        self.poke("ARM_UAV:=true")
        self.query("UAV_IS_ARMED==1")


if __name__ == "__main__":
    unittest.main()
