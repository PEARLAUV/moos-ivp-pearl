#!/usr/bin/env python3
"""Check real AppCast output on isolated MOOS ports; no SITL or flight commands."""

import os
import re
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
MOOS_BIN = Path(os.environ.get("MOOS_BIN_DIR", ROOT.parent / "moos-ivp/bin"))


class LandingAppCastTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="landing-appcast-")
        self.directory = Path(self.temp.name)
        self.process = None
        self.output = None
        self.server = socket.socket()
        self.server.bind(("127.0.0.1", 0))
        self.server.listen()
        self.endpoint = self.server.getsockname()[1]

    def tearDown(self):
        if self.process and self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=5)
        if self.output:
            self.output.close()
        self.server.close()
        self.temp.cleanup()

    def launch(self, app, config):
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        self.app = app
        self.mission = self.directory / "test.moos"
        self.mission.write_text(f"""ServerHost = 127.0.0.1
ServerPort = {port}
Community = uav
ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = {MOOS_BIN / 'MOOSDB'} @ NewConsole = false
  Run = {ROOT / 'bin' / app} @ NewConsole = false
  Run = {MOOS_BIN / 'pLogger'} @ NewConsole = false
}}
ProcessConfig = pLogger
{{
  File = LOG_probe
  AsyncLog = true
  Log = APPCAST
}}
ProcessConfig = {app}
{{
  AppTick = 20
  CommsTick = 20
  {config}
}}
""")
        self.output = (self.directory / "antler.log").open("w")
        self.process = subprocess.Popen([str(MOOS_BIN / "pAntler"), str(self.mission)],
                                        cwd=self.directory, stdout=self.output,
                                        stderr=subprocess.STDOUT, start_new_session=True)
        time.sleep(1.5)

    def poke(self, *values):
        subprocess.run([str(MOOS_BIN / "uPokeDB"), str(self.mission), "--quiet", *values],
                       capture_output=True, text=True, check=True, timeout=12)

    def report(self):
        self.poke(f"APPCAST_REQ:=node=uav,app={self.app},duration=5,key=readability")
        time.sleep(1)
        reports = []
        for path in self.directory.glob("LOG_probe*/*.alog"):
            for line in path.read_text().splitlines():
                fields = line.split(None, 3)
                if len(fields) == 4 and fields[1] == "APPCAST" and fields[2] == self.app:
                    reports.append(fields[3])
        self.assertTrue(reports, "No AppCast from " + self.app)
        report = reports[-1].replace("!@", "\n")
        # Every labeled report row uses the same 22-character right-aligned field.
        for line in report.splitlines():
            if re.match(r"^\s+[A-Za-z][A-Za-z /-]*:", line):
                self.assertEqual(line.index(":"), 22, line)
        return report

    def test_aircraft_readiness_missing_then_ready_then_blocked(self):
        self.launch("pLandingCoordinator", "ownship=uav\npeer_node=pearl\nrole=aircraft\ninput_timeout=30")
        report = self.report()
        self.assertIn("Landing session\n----------------------", report)
        self.assertIn("Session: none", report)
        self.assertIn("Peer updates: not received", report)
        self.assertIn("Target available: missing", report)
        self.assertIn("Overall checks: blocked", report)
        self.poke("UAV_LANDING_TARGET_AVAILABLE=1", "UAV_LANDING_TARGET_POSITION_VALID=1",
                  "UAV_LANDING_TARGET_FRAME=12", "UAV_LANDING_TARGET_AGE=0.04",
                  "UAV_HEALTH_ALL_OK=1", "UAV_HEALTH_AGE=0.10", "UAV_IS_ARMED=1",
                  "UAV_LANDED_STATE_AVAILABLE=1", "UAV_LANDED_STATE:=IN_AIR",
                  "UAV_LANDED_STATE_AGE=0.20")
        report = self.report()
        self.assertIn("Overall checks: ready", report)
        self.assertIn("Target frame: BODY_FRD (12)", report)
        self.assertIn("Target age: 0.04 s (OK; max 0.50 s)", report)
        self.assertIn("Health age: 0.10 s (OK; max 30.00 s)", report)
        self.poke("UAV_LANDING_TARGET_AGE=1.25")
        report = self.report()
        self.assertIn("Overall checks: blocked", report)
        self.assertIn("Target age: 1.25 s (too old; max 0.50 s)", report)

    def test_platform_report_does_not_imply_missing_aircraft_checks(self):
        self.launch("pLandingCoordinator", "ownship=pearl\npeer_node=uav\nrole=platform")
        report = self.report()
        self.assertIn("Platform readiness\n----------------------", report)
        self.assertIn("Aircraft checks: evaluated by aircraft coordinator", report)
        self.assertNotIn("Overall checks: blocked", report)

    def test_target_geometry_precision_and_missing_inputs(self):
        self.launch("pMavlinkLandingTarget", f"mavlink_port={self.endpoint}\n"
                    "input_timeout=30\ntarget_altitude_msl=1")
        report = self.report()
        self.assertIn("Streaming\n----------------------", report)
        self.assertIn("Target latitude: missing", report)
        self.assertIn("Distance: -- (no fresh geometry)", report)
        self.poke("TARGET_NAV_LAT=42", "TARGET_NAV_LONG=-71", "UAV_NAV_LAT=42",
                  "UAV_NAV_LONG=-71", "UAV_NAV_HEADING=90.126", "UAV_NAV_ALTITUDE_MSL=8")
        report = self.report()
        self.assertIn("Status: streaming", report)
        self.assertIn("Broadcasting: yes", report)
        self.assertIn("Detection range: 12.00 m (3-D)", report)
        self.assertIn("Distance: 7.00 m", report)
        self.assertIn("Down: 7.00 m", report)
        self.assertIn("Target latitude: 42.0000000 deg (fresh)", report)
        self.assertIn("Vehicle yaw: 90.13 deg (fresh)", report)
        self.assertIn("Target altitude: 1.00 m MSL (fixed)", report)


if __name__ == "__main__":
    unittest.main()
