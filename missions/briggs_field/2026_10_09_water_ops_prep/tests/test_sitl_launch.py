#!/usr/bin/env python3
"""Exercise launch_uav.sh's SITL startup ordering, gating, speedup and cleanup."""

import contextlib
import json
import os
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
MOOS_BIN = MISSION.parents[2].parent / "moos-ivp/bin"
UAV_BASE = MISSION / "../../uav_solo/briggs_test"

FAKE_SIMULATOR = '''#!/usr/bin/env python3
import json, os, pathlib, signal, socket, subprocess, sys, time
root = pathlib.Path(os.environ["TEST_ROOT"])
command = sys.argv[1]
if command == "serve":
    (root / "running").touch()
    time.sleep(0.4)
    if os.environ.get("TEST_NO_TCP"):
        while True: time.sleep(0.1)
    with socket.socket() as server:
        server.bind(("127.0.0.1", int(os.environ["SITL_PORT"])))
        server.listen()
        (root / "ready").touch()
        while True:
            connection, _ = server.accept()
            connection.close()
if command == "ps":
    if (root / "running").exists(): print("owned-container")
    sys.exit(0)
if command == "logs": sys.exit(0)
record = {"args": sys.argv[1:], "speedup": os.environ["SITL_SPEEDUP"],
          "params": os.environ["SITL_PARAM_FILE"],
          "project": os.environ["COMPOSE_PROJECT_NAME"]}
if command == "up" and not os.environ.get("TEST_START_FAIL"):
    server = subprocess.Popen([sys.executable, __file__, "serve"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                              start_new_session=True)
    record["pid"] = server.pid
if command == "down" and (root / "up.json").exists():
    pid = json.loads((root / "up.json").read_text()).get("pid")
    if pid:
        try: os.kill(pid, signal.SIGTERM)
        except ProcessLookupError: pass
(root / (command + ".json")).write_text(json.dumps(record))
if command == "up" and os.environ.get("TEST_START_FAIL"): sys.exit(7)
'''

FAKE_ANTLER = '''#!/usr/bin/env python3
import json, os, pathlib, sys, time
root = pathlib.Path(os.environ["TEST_ROOT"])
if os.environ.get("TEST_REQUIRE_READY"):
    assert (root / "ready").exists(), "ANTLER launched before controller readiness"
(root / "antler.json").write_text(json.dumps({"args": sys.argv[1:], "pid": os.getpid(),
    "config": pathlib.Path(sys.argv[1]).read_text()}))
while True: time.sleep(0.1)
'''


def generate_target(directory, mode, warp):
    target = Path(directory) / "targ_probe.moos"
    values = {
        "WARP": str(warp), "IP_ADDR": "localhost", "MOOS_PORT": "9001",
        "PSHARE_PORT": "9201", "SHORE_IP": "localhost", "SHORE_PSHARE": "9200",
        "DIRECT_PEER": "false", "PEARL_IP": "unused", "PEARL_PSHARE": "9202",
        "VNAME": "uav", "COLOR": "yellow", "PLATFORM_LENGTH": "1.5",
        "AP_URL": "127.0.0.1:5760", "AP_PROTOCOL": "tcp",
        "SECONDARY_LANDING_X": "10", "SECONDARY_LANDING_Y": "15",
        "MIN_STATE_TIME": "0", "XMODE": mode, "IS_SIM": str(mode == "SITL").lower(),
        "SHIP_SYSTEM_ID": "17" if mode == "SITL" else "0",
        "PEARL_LIB_DIR": str(MISSION.parents[2] / "lib"),
    }
    subprocess.run(
        [str(MOOS_BIN / "nsplug"), str(MISSION / "meta_uav.moos"), str(target),
         "--strict", "--force", "-x", f"--path={MISSION}:{UAV_BASE}",
         *(f"{name}={value}" for name, value in values.items())],
        check=True, capture_output=True, text=True,
    )
    return target


class SITLLaunchTest(unittest.TestCase):
    def test_normal_bridge_in_both_modes(self):
        with tempfile.TemporaryDirectory(prefix="sitl-config-") as directory:
            for mode in ("SITL", "REAL"):
                source = generate_target(directory, mode, 5).read_text()
                self.assertRegex(source, r"Run = pArduBridge\s+@")
                self.assertNotIn("launch_sitl_moos.sh", source)
                self.assertNotIn("SITL_ARGS", source)

    def exercise_launcher(self, *, mode="SITL", warp=5, just_make=False,
                          overrides=None, expected_exit=0):
        targets = [MISSION / "targ_uav.moos", MISSION / "targ_uav.bhv"]
        originals = {path: path.read_bytes() if path.exists() else None for path in targets}
        with tempfile.TemporaryDirectory(prefix="sitl-launch-") as directory:
            root = Path(directory)
            for name, body in (("simulator", FAKE_SIMULATOR), ("pAntler", FAKE_ANTLER)):
                path = root / name
                path.write_text(body)
                path.chmod(0o755)
            with socket.socket() as reservation:
                reservation.bind(("127.0.0.1", 0))
                port = reservation.getsockname()[1]
            env = dict(os.environ, TEST_ROOT=directory, SITL_WRAPPER=str(root / "simulator"),
                       PATH=str(root) + os.pathsep + os.environ["PATH"],
                       SITL_LOG_DIR=str(root / "logs"), SITL_PORT=str(port),
                       SITL_STARTUP_TIMEOUT="4", SITL_SPEEDUP="99")
            if mode == "SITL":
                env["TEST_REQUIRE_READY"] = "1"
            env.update(overrides or {})
            command = [str(MISSION / "launch_uav.sh"), "--auto", f"--mode={mode}", str(warp)]
            if just_make:
                command.append("--just_make")
            antler = None
            try:
                result = subprocess.run(command, env=env, capture_output=True, text=True,
                                        timeout=25, check=False)
                self.assertEqual(result.returncode, expected_exit, result.stdout + result.stderr)
                if just_make or expected_exit != 0:
                    self.assertFalse((root / "antler.json").exists())
                else:
                    self.wait_for(root / "antler.json")
                    antler = json.loads((root / "antler.json").read_text())
                    self.assertRegex(antler["config"], r"Run = pArduBridge\s+@")
                    self.assertEqual(antler["args"], ["targ_uav.moos"])
                    os.kill(antler["pid"], signal.SIGTERM)
                if mode == "REAL" or just_make:
                    self.assertFalse((root / "up.json").exists())
                else:
                    self.wait_for(root / "down.json")
                    up = json.loads((root / "up.json").read_text())
                    down = json.loads((root / "down.json").read_text())
                    self.assertEqual(up["speedup"], str(warp))
                    self.assertEqual(up["params"], str(MISSION / "sitl.parm"))
                    self.assertTrue(up["project"].startswith("moos-sitl-"))
                    self.assertEqual(down["project"], up["project"])
                    self.assertEqual(up["args"], ["up", "-d", "--no-build"])
                    self.assertEqual(down["args"], ["down", "--timeout", "5"])
            finally:
                if antler:
                    with contextlib.suppress(ProcessLookupError):
                        os.kill(antler["pid"], signal.SIGTERM)
                if (root / "up.json").exists():
                    pid = json.loads((root / "up.json").read_text()).get("pid")
                    if pid:
                        with contextlib.suppress(ProcessLookupError):
                            os.kill(pid, signal.SIGTERM)
                for path, original in originals.items():
                    if original is None:
                        path.unlink(missing_ok=True)
                    else:
                        path.write_bytes(original)

    def wait_for(self, path):
        deadline = time.monotonic() + 8
        while not path.exists():
            if time.monotonic() >= deadline:
                self.fail(f"Timed out waiting for {path}")
            time.sleep(0.05)

    def test_startup_order_speedup_and_auto_cleanup(self):
        for warp in (1, 5):
            with self.subTest(warp=warp):
                self.exercise_launcher(warp=warp)

    def test_real_does_not_start_simulator(self):
        self.exercise_launcher(mode="REAL")

    def test_just_make_does_not_start_simulator(self):
        self.exercise_launcher(just_make=True)

    def test_startup_failure_prevents_antler(self):
        self.exercise_launcher(overrides={"TEST_START_FAIL": "1"}, expected_exit=1)

    def test_readiness_timeout_prevents_antler(self):
        self.exercise_launcher(overrides={"TEST_NO_TCP": "1", "SITL_STARTUP_TIMEOUT": "1"},
                               expected_exit=1)


if __name__ == "__main__":
    unittest.main()
