#!/usr/bin/env python3
"""Real isolated MOOS regression for rendezvous visual lifecycle."""

import os
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class VisualTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="rendezvous-visuals-")
        self.directory = Path(self.temp.name)
        self.process = None

    def tearDown(self):
        if self.process and self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGINT)
            try:
                self.process.wait(timeout=6)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=5)
        self.temp.cleanup()

    def launch(self, shape):
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        self.mission = self.directory / "test.moos"
        self.mission.write_text(f"""ServerHost = localhost
ServerPort = {port}
Community = uav
ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = MOOSDB @ NewConsole = false
  Run = {ROOT / 'bin/pRendezvous'} @ NewConsole = false
  Run = pLogger @ NewConsole = false
}}
ProcessConfig = pRendezvous
{{
  AppTick = 20
  CommsTick = 20
  ownship = uav
  peer_node = pearl
  nav_stale_thresh = 30
  state_post_interval = 0.1
  point_freeze_radius = 0
  point_shape = {shape}
}}
ProcessConfig = pLogger
{{
  File = LOG_probe
  AsyncLog = true
  WildCardLogging = true
}}
""")
        with (self.directory / "antler.log").open("w") as output:
            self.process = subprocess.Popen(["pAntler", str(self.mission)], cwd=self.directory,
                                            stdout=output, stderr=subprocess.STDOUT,
                                            start_new_session=True)
        time.sleep(1.5)
        self.variable = "VIEW_POINT" if shape == "point" else "VIEW_CIRCLE"

    def poke(self, *values):
        subprocess.run(["uPokeDB", str(self.mission), "--quiet", *values],
                       check=True, capture_output=True, timeout=12)
        time.sleep(0.3)

    def values(self, variable):
        values = []
        for path in self.directory.glob("LOG_probe*/*.alog"):
            for line in path.read_text().splitlines():
                fields = line.split(None, 3)
                if len(fields) == 4 and fields[1] == variable and fields[2] == "pRendezvous":
                    values.append(fields[3].strip())
        return values

    def start(self):
        self.poke("NAV_X=0", "NAV_Y=0", "NODE_REPORT:=NAME=pearl,X=20,Y=0",
                  "RENDEZVOUS_START:=true")
        self.assertEqual(self.values("RENDEZVOUS_OWN_PHASE")[-1], "RENDEZVOUS")
        self.assertIn("active=true", self.values(self.variable)[-1])

    def test_disc_completion_hides_repeatedly_and_late_freeze_redraws(self):
        self.launch("disc")
        self.start()
        self.poke("NAV_X=10")
        self.assertEqual(self.values("RENDEZVOUS_OWN_PHASE")[-1], "COMPLETE")
        self.assertIn("active=false", self.values(self.variable)[-1])
        before = len(self.values(self.variable))
        time.sleep(0.5)
        self.assertGreater(len(self.values(self.variable)), before)
        self.assertIn("active=false", self.values(self.variable)[-1])
        self.assertEqual(self.values("RENDEZVOUS_OWN_POINT")[-1], "{10.00,0.00}")
        now = time.time()
        self.poke(f"RENDEZVOUS_MOTION:=session=peer#node=pearl#phase=COMPLETE#time={now}"
                  f"#position_time={now}#x=20#y=0#freeze_time={now-10}#freeze_node=pearl#point_x=12#point_y=0")
        self.assertEqual(self.values("RENDEZVOUS_OWN_PHASE")[-1], "RENDEZVOUS")
        self.assertIn("active=true", self.values(self.variable)[-1])
        self.assertIn("x=12.00", self.values(self.variable)[-1])

    def test_point_abort_hides_and_new_session_redraws(self):
        self.launch("point")
        self.start()
        self.poke("RENDEZVOUS_ABORT:=true")
        self.assertEqual(self.values("RENDEZVOUS_OWN_PHASE")[-1], "ABORT")
        self.assertIn("active=false", self.values(self.variable)[-1])
        self.poke("RENDEZVOUS_START:=true")
        self.assertEqual(self.values("RENDEZVOUS_OWN_PHASE")[-1], "RENDEZVOUS")
        self.assertIn("active=true", self.values(self.variable)[-1])


if __name__ == "__main__":
    unittest.main()
