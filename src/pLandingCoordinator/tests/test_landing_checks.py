#!/usr/bin/env python3
"""Exercise the MOOS-side precision-landing gate against real MOOS apps."""

import os
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MOOS_BIN = Path(os.environ.get("MOOS_BIN_DIR", ROOT.parent / "moos-ivp/bin"))


class LandingChecksTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="landing-checks-")
        self.root = Path(self.temp.name)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        self.mission = self.root / "test.moos"
        self.mission.write_text(f"""ServerHost = 127.0.0.1
ServerPort = {port}
Community = uav
ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = {MOOS_BIN / 'MOOSDB'} @ NewConsole = false
  Run = {ROOT / 'bin/pLandingCoordinator'} @ NewConsole = false
}}
ProcessConfig = pLandingCoordinator
{{
  AppTick = 20
  CommsTick = 20
  ownship = uav
  peer_node = pearl
  role = aircraft
  input_timeout = 30
  landing_timeout = 120
}}
""")
        self.log = (self.root / "antler.log").open("w")
        self.process = subprocess.Popen(
            [str(MOOS_BIN / "pAntler"), str(self.mission)], cwd=self.root,
            stdout=self.log, stderr=subprocess.STDOUT, start_new_session=True,
        )
        time.sleep(1.5)

    def tearDown(self):
        if self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=3)
        self.log.close()
        self.temp.cleanup()

    def poke(self, *values):
        result = subprocess.run(
            [str(MOOS_BIN / "uPokeDB"), str(self.mission), "--quiet", *values],
            capture_output=True, text=True, timeout=12,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def query(self, *conditions, variable=None):
        args = [str(MOOS_BIN / "uQueryDB"), str(self.mission), "--wait=4",
                *(f"--condition={condition}" for condition in conditions)]
        if variable:
            args.extend([f"--check_var={variable}", "--vo"])
        result = subprocess.run(args, cwd=self.root, capture_output=True, text=True, timeout=12)
        self.assertEqual(result.returncode, 0,
                         result.stdout + result.stderr + (self.root / "antler.log").read_text())
        if variable:
            return (self.root / ".checkvars").read_text().strip().partition("=")[2]

    def peer(self, session, started, phase, rv="COMPLETE"):
        self.poke(f"LANDING_EXCHANGE:=session={session}#node=pearl#phase={phase}"
                  f"#started={started:.6f}#time={time.time():.6f}#rv_phase={rv}")

    def test_clearance_requires_target_health_and_airborne_before_autoland(self):
        # An emulator claiming activity must not authorize landing.
        self.poke("ARDU_COMMAND:=NONE", "RECOVER:=true", "LANDMARK_EMULATOR_ACTIVE=1")
        session = self.query("LANDING_OWN_PHASE=REQUESTED", variable="LANDING_OWN_SESSION")
        started = time.time() - 1
        self.peer(session, started, "AGREED", "IDLE")
        self.query("LANDING_OWN_PHASE=RENDEZVOUS", "RENDEZVOUS_START=true")
        # pRendezvous owns a new local session; old completion is not sufficient.
        self.poke("RENDEZVOUS_OWN_SESSION:=rv-new")
        self.poke("RENDEZVOUS_OWN_PHASE:=COMPLETE")
        self.peer(session, started, "CLEARED")
        self.poke("UAV_HEALTH_ALL_OK=1", "UAV_HEALTH_AGE=0.1", "UAV_IS_ARMED=1",
                  "UAV_LANDED_STATE_AVAILABLE=1", "UAV_LANDED_STATE:=IN_AIR",
                  "UAV_LANDED_STATE_AGE=0.1", "UAV_LANDING_TARGET_POSITION_VALID=1",
                  "UAV_LANDING_TARGET_FRAME=12", "UAV_LANDING_TARGET_AGE=0.1")
        self.query("LANDING_OWN_CHECKS_READY=false", "ARDU_COMMAND=NONE")
        self.poke("UAV_LANDING_TARGET_AVAILABLE=1", "UAV_LANDING_TARGET_FRAME=0")
        self.query("LANDING_OWN_CHECKS_READY=false", "ARDU_COMMAND=NONE")
        self.poke("UAV_LANDING_TARGET_FRAME=12", "UAV_LANDING_TARGET_AGE=1")
        self.query("LANDING_OWN_CHECKS_READY=false", "ARDU_COMMAND=NONE")
        self.poke("UAV_LANDING_TARGET_AGE=0.1", "UAV_IS_ARMED=0")
        self.query("LANDING_OWN_CHECKS_READY=false", "ARDU_COMMAND=NONE")
        self.poke("UAV_IS_ARMED=1", "UAV_HEALTH_ALL_OK=0")
        self.query("LANDING_OWN_CHECKS_READY=false", "ARDU_COMMAND=NONE")
        self.poke("UAV_HEALTH_ALL_OK=1")
        self.query("LANDING_OWN_PHASE=COMMANDING", "ARDU_COMMAND=AUTOLAND")
        # The bridge reports AUTOLAND as LAND, not PRECISION_LAND.
        self.poke("UAV_COMMAND_RESULT:=id=7,command=LAND,status=ACCEPTED,detail=SUCCESS")
        self.query("LANDING_OWN_PHASE=LANDING")
        self.poke("UAV_LANDED_STATE:=ON_GROUND", "UAV_IS_ARMED=0")
        self.query("LANDING_OWN_PHASE=COMPLETE")


if __name__ == "__main__":
    unittest.main()
