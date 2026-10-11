#!/usr/bin/env python3
"""Local three-community simulation artifact tests."""

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any, cast

SCRIPT = Path(__file__).resolve().parents[1] / "collect_sim_logs.py"
sys.path.insert(0, str(SCRIPT.parent))
spec = importlib.util.spec_from_file_location("collect_sim_logs", SCRIPT)
assert spec is not None and spec.loader is not None
collector = cast(Any, importlib.util.module_from_spec(spec))
sys.modules[spec.name] = collector
spec.loader.exec_module(collector)


class CollectSimLogsTest(unittest.TestCase):
    def make_log(self, mission: Path, prefix: str, stamp: str, start: float,
                 sitl_uav: bool | None = None) -> Path:
        directory = mission / f"{prefix}{stamp}"
        directory.mkdir()
        (directory / f"{directory.name}.alog").write_text("0 TEST source value\n")
        if sitl_uav is None:
            sitl_uav = prefix == collector.PREFIXES["drone"]
        mode = ("  Run = pMavlinkLandingTarget @ NewConsole = false\n"
                "  url_protocol = tcp\n" if sitl_uav else "  url_protocol = serial\n")
        (directory / f"{directory.name}._moos").write_text(
            f"%% LOGSTART           {start}\n{mode}")
        return directory

    def test_selects_matching_latest_run_and_sitl_log(self):
        with tempfile.TemporaryDirectory() as directory:
            mission = Path(directory) / "mission"
            mission.mkdir()
            # An older coherent run must not be selected.
            for node, prefix in collector.PREFIXES.items():
                self.make_log(mission, prefix, "01_01_2026_____00_00_00",
                              100.0 + (0.1 if node == "pearl" else 0))
            latest = {
                "drone": self.make_log(mission, collector.PREFIXES["drone"],
                                       "02_01_2026_____00_00_00", 200.0),
                "pearl": self.make_log(mission, collector.PREFIXES["pearl"],
                                       "02_01_2026_____00_00_01", 200.2),
                "shoreside": self.make_log(mission, collector.PREFIXES["shoreside"],
                                           "02_01_2026_____00_00_01", 199.9),
            }
            # A newer REAL UAV log must not displace the latest SITL run.
            self.make_log(mission, collector.PREFIXES["drone"],
                          "03_01_2026_____00_00_00", 300.0, sitl_uav=False)
            sitl = mission / "LOG_sitl"
            sitl.mkdir()
            (sitl / "moos-sitl-1000-123.log").write_text("SITL evidence\n")
            output = mission / "field_logs" / "sim"
            result = collector.main([
                "--mission-dir", str(mission), "--output-root", str(output)
            ])
            self.assertEqual(result, 0)
            artifact = next(output.iterdir())
            manifest = json.loads((artifact / "MANIFEST.json").read_text())
            self.assertTrue(manifest["complete"])
            self.assertEqual(manifest["node"], "sim")
            for node, source in latest.items():
                self.assertTrue((artifact / "moos" / node / source.name).is_dir())
            self.assertTrue((artifact / "sitl" / "moos-sitl-1000-123.log").is_file())
            for line in (artifact / "SHA256SUMS").read_text().splitlines():
                _, relative = line.split("  ", 1)
                self.assertTrue((artifact / relative).is_file())

    def test_rejects_unmatched_communities(self):
        with tempfile.TemporaryDirectory() as directory:
            mission = Path(directory)
            self.make_log(mission, collector.PREFIXES["drone"], "latest", 500)
            self.make_log(mission, collector.PREFIXES["pearl"], "old", 100)
            self.make_log(mission, collector.PREFIXES["shoreside"], "old", 100)
            with self.assertRaises(collector.SimCollectionError):
                collector.select_run(mission, maximum_skew=10)


if __name__ == "__main__":
    unittest.main(verbosity=2)
