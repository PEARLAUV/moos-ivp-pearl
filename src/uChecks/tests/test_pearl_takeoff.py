#!/usr/bin/env python3
"""Exercise the PEARL SIM inputs, session-scoped checks, and takeoff processor."""
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

ROOT = Path(__file__).resolve().parents[3]
MISSION = ROOT / "missions/charles_river/2026_09_29_chevron_demo/meta_pearl.moos"
MOOS = Path(os.environ.get("MOOS_BIN_DIR", ROOT.parent / "moos-ivp" / "bin"))


def block(text, name):
    match = re.search(r"ProcessConfig = " + name + r"\s*\{.*?\n\}", text, re.S)
    if not match:
        raise AssertionError(f"Missing {name} configuration")
    return match.group().replace("$(VNAME)", "pearl")


class PearlTakeoffChecksTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="pearl-takeoff-checks-")
        self.root = Path(self.tmp.name)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        config = MISSION.read_text()
        runtime = block(config, "pFSMRuntime").replace(
            "#include plug_pearl_fsm_runtime.moos",
            (MISSION.parent / "plug_pearl_fsm_runtime.moos").read_text()
        ).replace("$(MIN_STATE_TIME)", "0")
        self.mission = self.root / "pearl.moos"
        self.mission.write_text(f"""ServerHost=127.0.0.1
ServerPort={port}
Community=pearl
ProcessConfig=ANTLER
{{
 MSBetweenLaunches=100
 Run={MOOS / 'MOOSDB'} @ NewConsole=false
 Run={MOOS / 'uTimerScript'} @ NewConsole=false ~ uSimTakeoffInputs
 Run={MOOS / 'uTimerScript'} @ NewConsole=false ~ uSimDroneportBayCover
 Run={ROOT / 'bin/uChecks'} @ NewConsole=false ~ uChecksTakeoff
 Run={ROOT / 'bin/uChecks'} @ NewConsole=false ~ uChecksPostInterruption
 Run={ROOT / 'bin/uChecks'} @ NewConsole=false ~ uChecksRecovery
 Run={ROOT / 'bin/pTakeoffProcessor'} @ NewConsole=false
 Run={ROOT / 'bin/pFSMRuntime'} @ NewConsole=false
}}
{runtime}
{block(config, 'uSimTakeoffInputs')}
{block(config, 'uSimDroneportBayCover')}
{block(config, 'uChecksTakeoff')}
{block(config, 'uChecksPostInterruption')}
{block(config, 'uChecksRecovery')}
{block(config, 'pTakeoffProcessor')}
""")
        self.log = (self.root / "antler.log").open("w")
        self.antler = subprocess.Popen([str(MOOS / "pAntler"), str(self.mission)],
                                       cwd=self.root, stdout=self.log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
        time.sleep(2)
        self.assertIsNone(self.antler.poll())

    def tearDown(self):
        if self.antler.poll() is None:
            os.killpg(self.antler.pid, signal.SIGTERM)
            try:
                self.antler.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(self.antler.pid, signal.SIGKILL)
                self.antler.wait(timeout=3)
        self.log.close()
        self.tmp.cleanup()

    def poke(self, *pairs):
        result = subprocess.run([str(MOOS / "uPokeDB"), str(self.mission),
                                 "--quiet", *pairs], capture_output=True, text=True,
                                timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def query(self, condition, wait=8):
        result = subprocess.run([str(MOOS / "uQueryDB"), str(self.mission),
                                 f"--condition={condition}", f"--wait={wait}"],
                                capture_output=True, text=True, timeout=wait + 12)
        if result.returncode:
            self.log.flush()
            self.fail(result.stdout + result.stderr +
                      (self.root / "antler.log").read_text())

    def value(self, var, condition):
        result = subprocess.run([str(MOOS / "uQueryDB"), str(self.mission),
                                 f"--condition={condition}", f"--check_var={var}",
                                 "--vo", "--wait=6"], cwd=self.root,
                                capture_output=True, text=True, timeout=18)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return (self.root / ".checkvars").read_text().strip()

    def test_bad_gps_fix_denies_and_restores_no_active_request(self):
        self.poke("MOOS_MANUAL_OVERRIDE:=false", "PEARL_ALLSTOP:=false")
        self.query("PEARL_FSM_SUPPORT_ACTIVITY_STATE==NO_ACTIVE_REQUEST")
        self.poke("UTS_PAUSE:=true")
        time.sleep(0.5)
        self.poke("GPS_QUALITY:=0")
        self.poke("TAKEOFF_REQUEST:=id=badfix#status=requested")
        time.sleep(2)
        self.query("PEARL_FSM_SUPPORT_ACTIVITY_STATE==NO_ACTIVE_REQUEST")
        self.query("TAKEOFF_REQUEST_ID==none")
        message = self.value("NODE_MESSAGE_LOCAL", "PEARL_FSM_OPERATION_STATE==NORMAL_OPERATION")
        self.assertIn("id=badfix#status=denied", message)

    def test_recovery_requires_already_open_droneport_without_opening_it(self):
        self.poke("MOOS_MANUAL_OVERRIDE:=false", "PEARL_ALLSTOP:=false")
        self.query("PEARL_FSM_SUPPORT_ACTIVITY_STATE==NO_ACTIVE_REQUEST")
        self.poke("PEARL_DRONEPORT_BAY_COVER_STATE:=closed",
                  "PEARL_RENDEZVOUS_SESSION:=recovery-closed",
                  "PEARL_RENDEZVOUS_PHASE:=REQUESTING")
        self.query("PEARL_FSM_SUPPORT_ACTIVITY_STATE==RECOVERY_PRE_INTERRUPTION_CHECKS")
        self.query("RECOVERY_CHECK_DRONEPORT_OPEN==fail")
        self.query("RECOVERY_PRECHECKS_STATE==fail")
        time.sleep(1)
        self.query("PEARL_DRONEPORT_BAY_COVER_STATE==closed")
        self.query("PEARL_FSM_SUPPORT_ACTIVITY_STATE==RECOVERY_PRE_INTERRUPTION_CHECKS")
        self.assertIn(
            "session=recovery-closed",
            self.value("RECOVERY_PRECHECKS_STATE_STATE",
                       "RECOVERY_PRECHECKS_STATE==fail"),
        )

        # Opening is an external fact, not an effect of entering recovery.
        # A new rendezvous session resets the failed check collection.
        self.poke("PEARL_DRONEPORT_BAY_COVER_STATE:=open",
                  "PEARL_RENDEZVOUS_SESSION:=recovery-open")
        self.query("RECOVERY_PRECHECKS_STATE==success")
        self.query("PEARL_FSM_SUPPORT_ACTIVITY_STATE==AGREEING_RENDEZVOUS")
        snapshot = json.loads(
            self.value("RECOVERY_PRECHECKS_STATE_SNAPSHOT",
                       "RECOVERY_PRECHECKS_STATE==success").partition("=")[2]
        )
        self.assertEqual(snapshot["session"]["id"], "recovery-open")
        self.assertEqual(snapshot["checks"]["DRONEPORT_OPEN"]["status"], "success")

    def test_sim_facts_authorize_only_after_fsm_entry(self):
        self.poke("MOOS_MANUAL_OVERRIDE:=false", "PEARL_ALLSTOP:=false")
        self.query("PEARL_FSM_SUPPORT_ACTIVITY_STATE==NO_ACTIVE_REQUEST")
        self.poke("TAKEOFF_REQUEST:=id=demo1#status=requested")
        self.query("TAKEOFF_REQUEST_ID==demo1")
        self.query("PEARL_FSM_TAKEOFF_SUPPORT_STATE==ESTABLISHING_TAKEOFF_CONDITIONS")
        self.query("PEARL_DRONEPORT_BAY_COVER_STATE==opening")
        self.assertIn("session=demo1", self.value("TAKEOFF_PRECHECKS_STATE_STATE",
                                                    "TAKEOFF_REQUEST_ID==demo1"))
        self.assertIn("ESTABLISHING_TAKEOFF_CONDITIONS",
                      self.value("PEARL_FSM_TAKEOFF_SUPPORT_STATE",
                                 "TAKEOFF_REQUEST_ID==demo1"))
        time.sleep(3.3)  # Beyond the former three-second inactive expiry.
        self.assertIn("=success", self.value("TAKEOFF_PRECHECKS_STATE",
                                             "PEARL_FSM_TAKEOFF_SUPPORT_STATE==ESTABLISHING_TAKEOFF_CONDITIONS"))
        raw = self.value("TAKEOFF_PRECHECKS_STATE_SNAPSHOT", "TAKEOFF_PRECHECKS_STATE==success")
        snapshot = json.loads(raw.partition("=")[2])
        self.assertEqual(snapshot["session"]["id"], "demo1")
        self.assertTrue(snapshot["active"])
        self.assertEqual(snapshot["aggregate"]["status"], "success")
        self.assertIn("WIND", snapshot["checks"])
        self.query("PEARL_DRONEPORT_BAY_COVER_STATE==open", wait=14)
        self.query("TAKEOFF_POSTCHECKS_STATE==success", wait=6)
        self.query("TAKEOFF_REQUEST_STATE==granted", wait=6)
        self.query("PEARL_FSM_TAKEOFF_SUPPORT_STATE==AWAITING_DRONE_DEPARTURE", wait=6)
        self.assertIn("session=demo1", self.value("TAKEOFF_POSTCHECKS_STATE_STATE",
                                                    "TAKEOFF_REQUEST_ID==demo1"))
        self.assertIn("status=granted", self.value("NODE_MESSAGE_LOCAL",
                                                   "TAKEOFF_REQUEST_ID==demo1"))
        self.poke("PEARL_DRONEPORT_BAY_COVER_STATE:=closed")
        self.query("TAKEOFF_POSTCHECKS_STATE==fail")  # The bay check stays live.
        self.poke("TAKEOFF_REQUEST:=id=demo2#status=requested")
        time.sleep(0.8)
        self.assertIn("status=busy", self.value("NODE_MESSAGE_LOCAL",
                                                "TAKEOFF_REQUEST_ID==demo1"))
        self.poke("UAV_FSM_OPERATION_STATE:=MISSION_EXECUTION",
                  "TAKEOFF_CANCEL_STATE:=none", "TAKEOFF_SUPPORT_ACTIVE:=false",
                  "BASE_RESTORE_STATE:=restoring")
        self.query("PEARL_FSM_TAKEOFF_SUPPORT_STATE==TAKEOFF_IN_PROGRESS")
        time.sleep(0.8)
        self.query("PEARL_FSM_OPERATION_STATE==TAKEOFF_SUPPORT")
        self.poke("TAKEOFF_REQUEST:=id=demo1#status=takeoff_complete")
        self.query("TAKEOFF_REQUEST_STATE==complete")
        self.query("PEARL_FSM_OPERATION_STATE==NORMAL_OPERATION")
        self.query("TAKEOFF_REQUEST_ID==none")
        self.assertIn("id=demo1,status=takeoff_complete",
                      self.value("UAV_TAKEOFF_PROGRESS",
                                 "PEARL_FSM_OPERATION_STATE==NORMAL_OPERATION"))


class TakeoffProgressRoutingTest(unittest.TestCase):
    def test_close_port_button_spoofs_pearl_cover_telemetry(self):
        shore = (MISSION.parent / "meta_shoreside.moos").read_text(encoding="utf-8")
        self.assertRegex(
            shore,
            r'button_7\s*=\s*CLOSE PORT\s*#\s*NODE_MESSAGE_LOCAL="'
            r'src_node=shoreside,dest_node=pearl,'
            r'var_name=PEARL_DRONEPORT_BAY_COVER_STATE,string_val=closed"',
        )
        self.assertNotRegex(
            shore,
            r'var_name=PEARL_DRONEPORT_BAY_COVER_STATE,string_val=open',
        )
        self.assertNotRegex(shore, r'button_\d+\s*=\s*RENDEZVOUS\b')
        self.assertNotRegex(shore, r'button_\d+\s*=\s*RNDV ABORT\b')

    def test_pearl_exports_takeoff_status_and_progress_to_shoreside(self):
        pearl = MISSION.read_text(encoding="utf-8")
        shore = (MISSION.parent / "meta_shoreside.moos").read_text(encoding="utf-8")
        for var in ("TAKEOFF_REQUEST_STATE", "UAV_TAKEOFF_PROGRESS"):
            self.assertRegex(pearl, rf"output = src_name={var},\s+"
                                    r"route=\$\(SHORE_IP\):\$\(SHORE_PSHARE\)")
            self.assertIn(f"scope = {var}", shore)


if __name__ == "__main__":
    unittest.main()
