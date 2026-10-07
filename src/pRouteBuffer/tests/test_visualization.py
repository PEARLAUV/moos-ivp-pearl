#!/usr/bin/env python3
"""Native shoreside route preview: orange while staged, yellow when sent."""

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
MOOS = Path(os.environ.get("MOOS_BIN_DIR", ROOT.parent / "moos-ivp" / "bin"))
ROUTE_BUFFER = ROOT / "bin/pRouteBuffer"


class RoutePreviewTest(unittest.TestCase):
    def setUp(self):
        missing = [p for p in (MOOS / "MOOSDB", MOOS / "pAntler",
                                MOOS / "pLogger", MOOS / "uPokeDB",
                                MOOS / "uQueryDB", ROUTE_BUFFER) if not p.is_file()]
        if missing:
            self.skipTest(f"missing binaries: {missing}")
        self.temp = tempfile.TemporaryDirectory(prefix="route-preview-")
        self.root = Path(self.temp.name)
        self.logs = self.root / "logs"
        self.logs.mkdir()
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        self.mission = self.root / "preview.moos"
        self.mission.write_text(f"""ServerHost=127.0.0.1
ServerPort={port}
Community=shore
ProcessConfig=ANTLER
{{
 MSBetweenLaunches=150
 Run={MOOS / 'MOOSDB'} @ NewConsole=false
 Run={MOOS / 'pLogger'} @ NewConsole=false ~ pLogger
 Run={ROUTE_BUFFER} @ NewConsole=false ~ pRouteBuffer
}}
ProcessConfig=pLogger
{{
 AppTick=20
 CommsTick=20
 File=preview
 PATH={self.logs}
 AsyncLog=true
 FileTimeStamp=false
 WildCardLogging=false
 Log=VIEW_SEGLIST @ 0
 Log=ROUTE_BUFFER_STATE @ 0
}}
ProcessConfig=pRouteBuffer
{{
 AppTick=20
 CommsTick=20
 role=shoreside
 source_node=shore
 destination_node=uav
}}
""", encoding="utf-8")
        self.output = (self.root / "antler.log").open("w", encoding="utf-8")
        self.antler = subprocess.Popen([str(MOOS / "pAntler"), str(self.mission)],
                                      cwd=self.root, stdout=self.output,
                                      stderr=subprocess.STDOUT, start_new_session=True)
        time.sleep(1.5)
        self.assertIsNone(self.antler.poll())

    def tearDown(self):
        if self.antler.poll() is None:
            os.killpg(self.antler.pid, signal.SIGTERM)
            try:
                self.antler.wait(timeout=4)
            except subprocess.TimeoutExpired:
                os.killpg(self.antler.pid, signal.SIGKILL)
                self.antler.wait(timeout=4)
        self.output.close()
        self.temp.cleanup()

    def poke(self, pair):
        subprocess.run([str(MOOS / "uPokeDB"), str(self.mission), "--quiet", pair],
                       cwd=self.root, check=True, capture_output=True, timeout=8)

    def query(self, state):
        result = subprocess.run([str(MOOS / "uQueryDB"), str(self.mission),
                                 f"--condition=ROUTE_BUFFER_STATE=={state}",
                                 "--wait=5"], cwd=self.root, capture_output=True,
                                text=True, timeout=9)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_staged_preview_switches_color_and_clears(self):
        self.poke("ROUTE_POINT:=x=0,y=0")
        self.poke("ROUTE_POINT:=x=20,y=0")
        self.query("EDITING")
        self.poke("ROUTE_BUFFER_DEPLOY=true")
        self.query("SUBMITTED")
        self.poke("ROUTE_POINT:=x=30,y=0")
        self.query("EDITING")
        self.poke("ROUTE_BUFFER_CLEAR=true")
        self.query("EMPTY")
        time.sleep(0.5)
        os.killpg(self.antler.pid, signal.SIGTERM)
        self.antler.wait(timeout=5)

        alog = self.logs / "preview" / "preview.alog"
        text = alog.read_text(encoding="utf-8", errors="replace")
        specs = [line.split("pRouteBuffer", 1)[1] for line in text.splitlines()
                 if re.search(r"\sVIEW_SEGLIST\s+pRouteBuffer\s", line)]
        self.assertTrue(specs, text)
        self.assertFalse(any("route_buffer_dash_" in spec for spec in specs), specs)
        self.assertTrue(all("label=route_buffer_path" in spec for spec in specs),
                        specs)
        staged = [i for i, spec in enumerate(specs)
                  if "edge_color=orange" in spec and "active=false" not in spec]
        submitted = [i for i, spec in enumerate(specs)
                     if "edge_color=yellow" in spec and "active=false" not in spec]
        self.assertTrue(staged and submitted, specs)
        self.assertLess(staged[0], submitted[0])
        self.assertTrue(any(i > submitted[0] for i in staged), specs)
        self.assertTrue(all("msg=route (uncommitted)" in specs[i] and
                            "label_color=orange" in specs[i] for i in staged), specs)
        self.assertTrue(all("msg=route (uncommitted)" not in specs[i] and
                            "label_color=invisible" in specs[i] for i in submitted),
                        specs)
        self.assertIn("active=false", specs[-1])


if __name__ == "__main__":
    unittest.main()
