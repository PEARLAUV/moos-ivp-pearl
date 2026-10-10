#!/usr/bin/env python3
"""Opt-in, owned ground-only native FC validation. No arm/mode/flight commands.

RUN_PARAM_STACK_SITL=1 python3 scripts/tests/test_param_stack_sitl.py
Artifacts retained under /tmp/param-stack-sitl-*. Only fixture stacks are stopped.
"""

import importlib.util
import json
import os
import shutil
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path
from typing import Any, cast

ROOT = Path(__file__).resolve().parents[2]
WRAPPER = ROOT.parent / "ardupilot-sitl/bin/ardupilot-sitl"
MISSION = ROOT / "missions/briggs_field/2026_10_09_water_ops_prep"
SCRIPT = ROOT / "scripts/ardupilot_params.py"
spec = importlib.util.spec_from_file_location("params", SCRIPT)
assert spec is not None and spec.loader is not None
params = cast(Any, importlib.util.module_from_spec(spec))
spec.loader.exec_module(params)


@unittest.skipUnless(os.environ.get("RUN_PARAM_STACK_SITL") == "1", "Explicit opt-in required")
class NativeParameterStackTest(unittest.TestCase):
    def start(self, layers, artifact, label):
        reservations = []
        for _ in range(3):
            reservation = socket.socket()
            reservation.bind(("127.0.0.1", 0))
            reservations.append(reservation)
        ports = [value.getsockname()[1] for value in reservations]
        project = f"param-stack-{os.getuid()}-{os.getpid()}-{label}"
        env = dict(os.environ, COMPOSE_PROJECT_NAME=project)
        command = [str(WRAPPER), "--no-rc", "--home", "42.3569186052,-71.0990291116,0,0",
                   "--port", str(ports[0]), "--diagnostic-port", str(ports[1]),
                   "--sim-port", str(ports[2])]
        for layer in layers:
            command += ["--param-file", str(layer)]
        for reservation in reservations:
            reservation.close()
        log = (artifact / (label + "-launcher.log")).open("w")
        # Register cleanup before startup so partial Compose failures are owned.
        self.addCleanup(log.close)
        self.addCleanup(subprocess.run, command + ["down", "--timeout", "5"],
                        env=env, stdout=log, stderr=subprocess.STDOUT, timeout=30, check=False)
        subprocess.run(command + ["up", "-d", "--no-build"], env=env,
                       stdout=log, stderr=subprocess.STDOUT, timeout=45, check=True)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            link = None
            try:
                link = params.Link("tcp", f"127.0.0.1:{ports[1]}")
                client = params.Client(link, timeout=10)
                client.admit()
                self.addCleanup(link.close)
                return link, client, ports, env, command, log
            except (OSError, params.UploadError):
                if link is not None:
                    link.close()
                time.sleep(0.2)
        self.fail("Owned SITL did not provide native grounded/disarmed admission")

    def test_ordered_defaults_and_actual_verified_parameter_upload(self):
        artifact = Path(tempfile.mkdtemp(prefix="param-stack-sitl-"))
        print("Artifacts:", artifact, flush=True)
        # Exercise a literal dollar/quote in a real mount, not just rendered YAML.
        drone = artifact / "drone's $profile.parm"
        shutil.copyfile(ROOT / "config/ardupilot/drones/x500.parm", drone)
        layers = [drone, MISSION / "sitl.parm", MISSION / "mission.parm"]
        link, client, ports, env, command, log = self.start(layers, artifact, "sitl")
        try:
            expected = {"RNGFND1_TYPE": 100, "RNGFND1_MIN": 0, "RNGFND1_MAX": 12,
                        "RNGFND1_GNDCLR": 0, "PLND_ALT_MIN": 0, "PLND_ALT_MAX": 4,
                        "LAND_SPD_HIGH_MS": 1, "LAND_SPD_MS": 0.4, "LAND_ALT_LOW_M": 2,
                        "RTL_CONE_SLOPE": 0, "RTL_OPTIONS": 0, "WP_YAW_BEHAVIOR": 2,
                        "LAND_REPOSITION": 1, "PLND_OPTIONS": 1}
            readings = {name: client.read(name)[0] for name in expected}
            for name, wanted in expected.items():
                self.assertTrue(params.same(readings[name], float(wanted)),
                                f"{name}: {readings[name]} != float32({wanted})")
            (artifact / "sitl-readback.json").write_text(json.dumps(readings, indent=2))
        finally:
            link.close()
        subprocess.run(command + ["down", "--timeout", "5"], env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=30)

        # A genuine native analog backend allows exercising REAL upload guards on
        # isolated SITL. This is not proof of real sensor/hardware behavior.
        analog = artifact / "analog-mode.parm"
        analog.write_text(
            '# Owned analog sensor fixture; no flight commands or navigation changes.\n'
            '# https://ardupilot.org/dev/docs/adding_simulated_devices.html#adding-a-rangefinder\n'
            'RNGFND1_TYPE 1 # Enum 1=analog; REAL uploader rejects native SITL type 100.\n'
            'RNGFND1_PIN 0 # Virtual analog input, not a hardware connection.\n'
            'RNGFND1_SCALING 10 # Metres/volt, matches simulated analog scaling.\n'
            'SIM_SONAR_SCALE 10 # Metres/volt; virtual 0..5 V produces 0..50 m.\n'
            'SIM_SONAR_ROT 25 # Enum 25=downward beam.\n'
            'RNGFND1_MIN 0.2 # Metres; fixture initial value, changed to user 0.15 by upload.\n'
            'RNGFND1_GNDCLR 0.2 # Metres; initial value, changed to user 0.17 by upload.\n')
        link, client, ports, _, _, _ = self.start(
            [ROOT / 'config/ardupilot/drones/x500.parm', analog, MISSION / 'mission.parm'],
            artifact, 'upload')
        self.assertAlmostEqual(client.read("RNGFND1_MIN")[0], 0.2, places=6)
        link.close()
        report = artifact / "native-upload.json"
        args = [str(SCRIPT), "--apply", "--protocol", "tcp", "--endpoint",
                f"127.0.0.1:{ports[1]}", "--report", str(report), "--timeout", "10"]
        for path in (ROOT / "config/ardupilot/drones/x500.parm",
                     MISSION / "real.parm", MISSION / "mission.parm"):
            args += ["--param-file", str(path)]
        result = subprocess.run(args, capture_output=True, text=True, timeout=80, check=False)
        (artifact / "upload-output.txt").write_text(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        evidence = json.loads(report.read_text())
        self.assertEqual(evidence["status"], "VERIFIED_CONFIGURATION")
        self.assertEqual(evidence["written"], ["RNGFND1_MIN", "RNGFND1_GNDCLR"])
        self.assertAlmostEqual(evidence["readback"]["RNGFND1_MIN"], 0.15, places=6)
        self.assertAlmostEqual(evidence["readback"]["RNGFND1_GNDCLR"], 0.17, places=6)
        for name, wanted in (("LAND_SPD_HIGH_MS", 1), ("LAND_SPD_MS", 0.4),
                             ("LAND_ALT_LOW_M", 2), ("RTL_CONE_SLOPE", 0),
                             ("RTL_OPTIONS", 0), ("WP_YAW_BEHAVIOR", 2),
                             ("LAND_REPOSITION", 1), ("PLND_OPTIONS", 1)):
            self.assertTrue(params.same(evidence["readback"][name], float(wanted)))
        (artifact / "result.json").write_text(json.dumps({
            "status": "PASS", "ground_only": True, "real_hardware_test": False,
            "flight_test": False, "native_upload_report": str(report)}, indent=2))


if __name__ == "__main__":
    unittest.main(verbosity=2)
