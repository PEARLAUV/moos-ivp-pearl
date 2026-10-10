"""Owned fake process/container shutdown tests. No real Docker, VNC or ktm."""

import os
import shutil
import signal
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
ROOT = MISSION.parents[2]


class ShutdownTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="prep-shutdown-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        repo = self.root / "repo"
        self.mission = repo / "missions/briggs_field/prep"
        self.mission.mkdir(parents=True)
        (repo / "scripts").symlink_to(ROOT / "scripts", target_is_directory=True)
        for name in ("launch.sh", "launch_sitl.sh", "Makefile"):
            shutil.copy2(MISSION / name, self.mission / name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.owners = self.mission / ".sitl-owned"
        self.owners.mkdir()
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ["PATH"],
                        TEST_ROOT=str(self.root), TEST_MISSION=str(self.mission),
                        SITL_OWNERS_DIR=str(self.owners), SITL_WRAPPER=str(self.bin / "wrapper"))
        self.script("wrapper", """
import os, pathlib, sys
root = pathlib.Path(os.environ["TEST_ROOT"])
with (root / "downs").open("a") as output:
    output.write(os.environ["COMPOSE_PROJECT_NAME"] + "\\n")
sys.exit(7 if os.environ.get("FAIL_DOWN") else 0)
""")
        self.script("ktm", """
import os, pathlib, sys
(pathlib.Path(os.environ["TEST_ROOT"]) / "ktm-called").touch()
sys.exit(int(os.environ.get("FAIL_KTM", "0")))
""")
        self.script("docker", """
import json, os, pathlib, signal, sys
root = pathlib.Path(os.environ["TEST_ROOT"])
args = sys.argv[1:]
if args[0] == "ps":
    print("owned other wronguser")
elif args[0] == "inspect":
    name = args[-1]
    source = os.environ["TEST_MISSION"] + "/gcf-home"
    if name == "other": source = str(root / "other-mission/gcf-home")
    uid = os.getuid() + (1 if name == "wronguser" else 0)
    print(json.dumps([{"Config": {"User": str(uid) + ":1000",
         "Env": ["QGC_MAVLINK_ENDPOINT=udp://localhost:14550"]},
         "Mounts": [{"Source": source,
         "Destination": "/home/qgc/Documents/QGroundControl"}]}]))
elif args[0] == "stop":
    with (root / "qgc-stopped").open("a") as output: output.write(args[-1] + "\\n")
elif args[0] == "run":
    with (root / "pids").open("a") as output: output.write(str(os.getpid()) + "\\n")
    while True: signal.pause()
else:
    sys.exit(2)
""")
        self.script("moos-vnc-session", """
import os, pathlib, signal, sys
root = pathlib.Path(os.environ["TEST_ROOT"])
with (root / "pids").open("a") as output: output.write(str(os.getpid()) + "\\n")
def stop(*_args):
    (root / "vnc-stopped").touch()
    sys.exit(0)
signal.signal(signal.SIGTERM, stop)
(root / "vnc-ready").touch()
while True: signal.pause()
""")
        self.processes = []
        self.addCleanup(self.stop_fixtures)

    def script(self, name, body, directory=None):
        path = (directory or self.bin) / name
        path.write_text("#!/usr/bin/env python3\n" + body)
        path.chmod(0o755)
        return path

    def stop_fixtures(self):
        for process in self.processes:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        path = self.root / "pids"
        if path.exists():
            for value in path.read_text().splitlines():
                try:
                    os.kill(int(value), signal.SIGTERM)
                except ProcessLookupError:
                    pass

    def wait_for(self, path):
        deadline = time.monotonic() + 10
        while not path.exists():
            if time.monotonic() > deadline:
                self.fail(f"Timeout waiting for {path}")
            time.sleep(0.02)

    def run_cleanup(self, *args, **extra_env):
        return subprocess.run([str(self.mission / "launch_sitl.sh"), *args],
                              env=dict(self.env, **extra_env), cwd=self.mission,
                              capture_output=True, text=True, timeout=30, check=False)

    def test_owned_projects_only_and_failed_teardown_keeps_marker(self):
        project = f"moos-sitl-{os.getuid()}-12345"
        marker = self.owners / project
        marker.touch()
        other = self.owners / "other-mission"
        other.touch()
        result = self.run_cleanup("--cleanup-owned", FAIL_DOWN="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(marker.exists())
        result = self.run_cleanup("--cleanup-owned")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(marker.exists())
        self.assertTrue(other.exists())
        self.assertEqual((self.root / "downs").read_text().splitlines(), [project, project])
        result = self.run_cleanup("--cleanup-project=unregistered")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.root / "downs").read_text().splitlines(), [project, project])

    def test_ui_cleanup_stops_owned_vnc_and_qgc_not_unrelated(self):
        owned = subprocess.Popen([str(self.bin / "moos-vnc-session"), "targ_shoreside.moos"],
                                 env=self.env, cwd=self.mission)
        self.processes.append(owned)
        self.wait_for(self.root / "vnc-ready")
        other_dir = self.root / "unrelated"
        other_dir.mkdir()
        other = subprocess.Popen([str(self.bin / "moos-vnc-session"), "targ_shoreside.moos"],
                                 env=self.env, cwd=other_dir)
        self.processes.append(other)
        result = self.run_cleanup("--cleanup-ui")
        self.assertEqual(result.returncode, 0, result.stderr)
        owned.wait(timeout=5)
        self.assertIsNone(other.poll())
        self.assertEqual((self.root / "qgc-stopped").read_text().splitlines(), ["owned"])

    def test_make_kill_still_cleans_containers_when_ktm_fails(self):
        project = f"moos-sitl-{os.getuid()}-12346"
        (self.owners / project).touch()
        result = subprocess.run(["make", "--no-print-directory", "-C", str(self.mission), "kill"],
                                env=dict(self.env, FAIL_KTM="7"), capture_output=True,
                                text=True, check=False, timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue((self.root / "ktm-called").exists())
        self.assertEqual((self.root / "downs").read_text().splitlines(), [project])
        self.assertEqual((self.root / "qgc-stopped").read_text().splitlines(), ["owned"])

    def test_combined_ctrl_c_stops_exact_run_groups_ui_and_sitl(self):
        child = """
import os, pathlib, subprocess
root = pathlib.Path(os.environ["TEST_ROOT"])
if pathlib.Path(__file__).name == "launch_uav.sh":
    project = f"moos-sitl-{os.getuid()}-{os.getpid()}"
    (pathlib.Path(os.environ["SITL_OWNERS_DIR"]) / project).touch()
    pathlib.Path(os.environ["SITL_ACTIVE_PROJECT_FILE"]).write_text(project + "\\n")
    (root / "project").write_text(project)
if pathlib.Path(__file__).name == "launch_shoreside.sh":
    subprocess.Popen(["moos-vnc-session", "targ_shoreside.moos"])
    subprocess.Popen(["docker", "run", "--name", "run-qgc", "--env",
                      "QGC_MAVLINK_ENDPOINT=udp://localhost:14550"])
"""
        for name in ("launch_uav.sh", "launch_pearl.sh", "launch_shoreside.sh"):
            self.script(name, child, self.mission)
        self.script("uMAC", """
import os, pathlib, signal
(pathlib.Path(os.environ["TEST_ROOT"]) / "console-ready").touch()
while True: signal.pause()
""")
        process = subprocess.Popen(["make", "--no-print-directory", "-C",
                                    str(self.mission), "sim-charles"],
                                   env=self.env, cwd=self.mission, start_new_session=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.processes.append(process)
        self.wait_for(self.root / "console-ready")
        self.wait_for(self.root / "vnc-ready")
        os.killpg(process.pid, signal.SIGINT)
        stdout, stderr = process.communicate(timeout=30)
        self.assertNotEqual(process.returncode, 0, stdout + stderr)
        self.assertIn("Halting owned mission apps and SITL", stdout + stderr)
        self.assertTrue((self.root / "vnc-stopped").exists())
        self.assertIn("run-qgc", (self.root / "qgc-stopped").read_text().splitlines())
        project = (self.root / "project").read_text()
        self.assertEqual((self.root / "downs").read_text().splitlines(), [project])
        self.assertFalse((self.owners / project).exists())
        self.assertFalse((self.root / "ktm-called").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
