#!/usr/bin/env python3
"""Verify shared SITL CLI forwarding without Docker or live mission changes."""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

HELPER = Path(__file__).resolve().parents[1] / "launch_ardupilot_sitl.sh"


class SITLHelperTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sitl-helper-")
        self.directory = Path(self.temp.name)
        self.wrapper = self.directory / "wrapper"
        self.wrapper.write_text(
            "#!/usr/bin/env python3\nimport json, sys\nprint(json.dumps(sys.argv[1:]))\n"
        )
        self.wrapper.chmod(0o755)
        self.env = dict(os.environ, SITL_WRAPPER=str(self.wrapper),
                        SITL_HOME="ignored", SITL_SPEEDUP="99")

    def tearDown(self):
        self.temp.cleanup()

    def call(self, *args, success=True):
        result = subprocess.run([str(HELPER), *args], env=self.env, cwd=self.directory,
                                capture_output=True, text=True, timeout=5, check=False)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return json.loads(result.stdout)
        self.assertNotEqual(result.returncode, 0)
        return result

    def test_explicit_runtime_flags_and_endpoints(self):
        args = ["--no-rc", "--home", "42,-71,0,180", "--speedup", "2",
                "--port", "127.0.0.1:15760", "--diagnostic-port", "15762",
                "--sim-port", "15763", "up", "-d", "--no-build"]
        self.assertEqual(self.call(*args), args)

    def test_former_target_port_is_translated(self):
        self.assertEqual(self.call("--target-port", "15763", "config"),
                         ["--sim-port", "15763", "config"])

    def test_legacy_warp_becomes_cli_speedup(self):
        self.assertEqual(self.call("--no-rc", "5", "up", "-d"),
                         ["--no-rc", "--speedup", "5", "up", "-d"])
        self.call("--speedup", "2", "5", success=False)
        self.call("0", success=False)

    def test_optional_overlay_resolves_to_absolute_path(self):
        (self.directory / "test.parm").write_text("# Empty test overlay.\n")
        self.assertEqual(self.call("--param-file=test.parm", "down", "--timeout", "5"),
                         ["--param-file", str(self.directory / "test.parm"),
                          "down", "--timeout", "5"])
        self.assertEqual(self.call("ps", "-q"), ["ps", "-q"])
        self.call("--param-file", "missing.parm", success=False)

    def test_runtime_flags_require_values(self):
        for flag in ("--home", "--speedup", "--port", "--diagnostic-port",
                     "--sim-port", "--target-port", "--param-file"):
            with self.subTest(flag=flag):
                self.call(flag, success=False)


if __name__ == "__main__":
    unittest.main()
