#!/usr/bin/env python3
"""Run the mission's actual MOOS ship-coupling policy in an isolated database."""

import os
import re
import signal
import socket
import subprocess
import tempfile
import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
MOOS_BIN = MISSION.parents[2].parent / "moos-ivp" / "bin"


class ShipCouplingPolicyTest(unittest.TestCase):
    def test_start_secondary_recovery_and_repeated_primary_recovery(self):
        source = (MISSION / "meta_uav.moos").read_text()
        aliases = ("uSimShipStart", "uSimShipDetach", "uSimShipAttach")
        echo = re.search(r"ProcessConfig = pEchoVar\s*\{[^}]+\}", source)
        if echo is None:
            raise AssertionError("Missing local UAV telemetry echo")
        # This fixture exercises the SITL branch of the actual mission config.
        echo_config = "\n".join(line for line in echo.group().splitlines()
                                if not line.startswith("#"))
        configs = []
        for alias in aliases:
            match = re.search(rf"ProcessConfig = {alias}\s*\{{[^}}]+\}}", source)
            if match is None:
                raise AssertionError(f"Missing mission policy: {alias}")
            configs.append(match.group())
        with tempfile.TemporaryDirectory(prefix="ship-policy-") as directory:
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            path = Path(directory) / "policy.moos"
            runs = "\n".join(
                f"Run = {MOOS_BIN / 'uTimerScript'} @ NewConsole = false ~ {alias}"
                for alias in aliases
            )
            path.write_text(f"""ServerHost = 127.0.0.1
ServerPort = {port}
Community = ship_policy_test
ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = {MOOS_BIN / 'MOOSDB'} @ NewConsole = false
  Run = {MOOS_BIN / 'pEchoVar'} @ NewConsole = false
  {runs}
}}
""" + "\n".join(configs) + "\n" + echo_config)
            with (Path(directory) / "antler.log").open("w") as log:
                process = subprocess.Popen([str(MOOS_BIN / "pAntler"), str(path)],
                                           cwd=directory, stdout=log, stderr=subprocess.STDOUT,
                                           start_new_session=True)
                try:
                    self.check_fact(path, directory, True)
                    subprocess.run([str(MOOS_BIN / "uPokeDB"), str(path), "--quiet",
                                    "UAV_LAT=42.358436", "UAV_LON=-71.087448",
                                    "UAV_YAW=70", "UAV_HEADING=80", "UAV_ALTITUDE_MSL=1.6"],
                                   cwd=directory, timeout=8, check=True, capture_output=True)
                    conditions = ("NAV_LAT=42.358436", "UAV_NAV_LAT=42.358436",
                                  "UAV_NAV_LONG=-71.087448", "UAV_NAV_HEADING=70",
                                  "NAV_HEADING=80", "UAV_NAV_ALTITUDE_MSL=1.6")
                    condition = conditions[0]
                    for term in conditions[1:]:
                        condition = f"({condition}) and ({term})"
                    result = subprocess.run(
                        [str(MOOS_BIN / "uQueryDB"), str(path), "--wait=6",
                         "--condition=" + condition], cwd=directory,
                        timeout=10, check=False, capture_output=True, text=True,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    for state, expected in (
                        ("DEVIATING_TO_SECONDARY_LANDING_POSITION", False),
                        ("SECONDARY_LANDING", False),
                        ("NONE", False),
                        ("APPROACHING", True),
                        ("LANDING", True),
                        ("NONE", True),
                        ("TRANSITING_TO_SECONDARY_LOITER_LOCATION", False),
                        ("LOITERING_AT_SECONDARY_ALTITUDE", False),
                        ("NONE", False),
                        ("RETRYING_LANDING_TARGET", True),
                        ("DEVIATING_TO_SECONDARY_LANDING_POSITION", False),
                    ):
                        subprocess.run([str(MOOS_BIN / "uPokeDB"), str(path), "--quiet",
                                        f"UAV_FSM_LANDING_OPERATIONS_STATE={state}"],
                                       cwd=directory, timeout=8, check=True,
                                       capture_output=True)
                        self.check_fact(path, directory, expected)
                finally:
                    if process.poll() is None:
                        os.killpg(process.pid, signal.SIGTERM)
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            os.killpg(process.pid, signal.SIGKILL)
                            process.wait(timeout=5)

    def check_fact(self, path, directory, expected):
        result = subprocess.run(
            [str(MOOS_BIN / "uQueryDB"), str(path), "--wait=6",
             f"--condition=SIM_UAV_ON_SHIP={'true' if expected else 'false'}"],
            cwd=directory, capture_output=True, text=True, timeout=10, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
