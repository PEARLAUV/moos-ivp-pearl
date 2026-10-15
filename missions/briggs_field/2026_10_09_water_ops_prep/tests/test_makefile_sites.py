"""Node/site and local SIM recipes; isolated configs, no hardware or live apps."""

import json
import os
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
ROOT = MISSION.parents[2]
NODES = ("shoreside", "pearl", "drone")
SITES = ("briggs", "charles")
PIXHAWK_IF00 = (
    "serial/by-id/usb-ArduPilot_Pixhawk6X_200023000E51333531343838-if00:115200"
)


def run_make(directory, *args):
    return subprocess.run(["make", "--no-print-directory", "-C", str(directory), *args],
                          capture_output=True, text=True, check=True, timeout=45)


class MakefileSiteTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Copy only mission inputs into an owned repo layout. Never regenerate,
        # replace or clean targets/FSM outputs belonging to a running user mission.
        cls.temp = tempfile.TemporaryDirectory(prefix="prep-make-sites-")
        cls.addClassCleanup(cls.temp.cleanup)
        repo = Path(cls.temp.name) / "repo"
        cls.mission = repo / "missions/briggs_field/prep"
        cls.mission.mkdir(parents=True)
        for name in ("scripts", "bin", "lib", "config"):
            (repo / name).symlink_to(ROOT / name, target_is_directory=True)
        base = repo / "missions/uav_solo/briggs_test"
        base.parent.mkdir(parents=True)
        base.symlink_to(MISSION / "../../uav_solo/briggs_test", target_is_directory=True)
        (repo / "missions/images").symlink_to(ROOT / "missions/images",
                                            target_is_directory=True)
        for path in MISSION.iterdir():
            if path.is_file() and (path.name == "Makefile" or
                                   path.suffix in (".sh", ".moos", ".bhv", ".toml", ".parm")):
                shutil.copy2(path, cls.mission / path.name)
        # Shoreside consumes the checked-in generated scope fragments before a
        # vehicle launcher has an opportunity to regenerate either FSM.
        for name in ("pearl_fsm", "uav_fsm"):
            shutil.copytree(MISSION / name, cls.mission / name)
        env = {key: value for key, value in os.environ.items()
               if key not in ("DRONE_MODEL", "DRONE_PARAM_FILE",
                              "MODE_PARAM_FILE", "MISSION_PARAM_FILE", "MODEL")}
        cls.env = env
        cls.configs = {}
        cls.behaviors = {}
        cls.sim_configs = {}
        cls.sim_behaviors = {}
        for site in SITES:
            for node in NODES:
                subprocess.run(
                    ["make", "--no-print-directory", "-C", str(cls.mission),
                     f"{node}-{site}", "LAUNCH_ARGS=--just_make",
                     *(["MODEL=x500"] if node == "drone" else [])],
                    env=env, capture_output=True, text=True, check=True, timeout=45)
                community = "uav" if node == "drone" else node
                cls.configs[node, site] = (
                    cls.mission / f"targ_{community}.moos").read_text()
                if node == "pearl" and site == "charles":
                    cls.behaviors[site] = (cls.mission / "targ_pearl.bhv").read_text()
        for site in SITES:
            subprocess.run(
                ["make", "--no-print-directory", "-C", str(cls.mission),
                 f"sim-{site}", "LAUNCH_ARGS=--just_make"], env=env,
                capture_output=True, text=True, check=True, timeout=45)
            for node in NODES:
                community = "uav" if node == "drone" else node
                cls.sim_configs[node, site] = (
                    cls.mission / f"targ_{community}.moos").read_text()
            cls.sim_behaviors[site] = (cls.mission / "targ_pearl.bhv").read_text()

    def recipe(self, node, site):
        output = run_make(MISSION, "-n", f"{node}-{site}",
                          *(["MODEL=x500"] if node == "drone" else [])).stdout.replace("\\\n", " ")
        words = shlex.split(output)
        return words[words.index("&&") + 1:]

    def test_physical_drone_requires_explicit_command_line_model(self):
        for site in SITES:
            for extra, env in (([], self.env),
                               ([], dict(self.env, MODEL="x500", DRONE_MODEL="x500")),
                               (["MODEL="], self.env),
                               (["MODEL=   "], self.env),
                               (["DRONE_ARGS=--model=x500"], self.env)):
                with self.subTest(site=site, extra=extra, inherited=env.get("MODEL")):
                    result = subprocess.run(
                        ["make", "--no-print-directory", "-C", str(self.mission),
                         f"drone-{site}", "LAUNCH_ARGS=--just_make", *extra],
                        env=env, capture_output=True, text=True, timeout=10, check=False)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("MODEL", result.stderr)
                    self.assertNotIn("./launch_uav.sh", result.stdout)
            result = run_make(self.mission, "-n", f"drone-{site}", "MODEL=chosen-drone")
            self.assertIn('--model="chosen-drone"', result.stdout)

    def test_local_sim_recipes_and_forwarded_geometry(self):
        for site in SITES:
            sim = self.recipe("sim", site)
            self.assertEqual(sim[0], "./launch.sh")
            options = dict(arg[2:].split("=", 1) for arg in sim[1:] if arg.startswith("--"))
            self.assertEqual(options["site"], site)
            self.assertEqual(options["mode"], "SITL")
            self.assertEqual(options["model"], "x500")
            self.assertNotIn("--upload-params", sim)
            self.assertFalse(any("100." in arg for arg in sim))
            pearl = dict(arg[2:].split("=", 1) for arg in self.recipe("pearl", site)[1:])
            shore = dict(arg[2:].split("=", 1) for arg in self.recipe("shoreside", site)[1:])
            self.assertEqual(options["pearl_start"], pearl["start_pos"])
            self.assertEqual(options["pearl_return"], shore["pearl_return"])
            self.assertEqual(options["uav_rally"], shore["uav_rally"])
            for node in NODES:
                source = self.sim_configs[node, site]
                self.assertNotIn("100.88.214.50", source)
                self.assertNotIn("100.112.119.114", source)
                self.assertNotIn("100.113.99.58", source)
                self.assertNotIn("100.71.137.47", source)
                for name in ("LatOrigin", "LongOrigin"):
                    pattern = rf"^{name}\s*=\s*(\S+)"
                    self.assertEqual(re.findall(pattern, source, re.MULTILINE),
                                     re.findall(pattern, self.configs[node, site], re.MULTILINE))
            sim_shore = self.sim_configs["shoreside", site]
            for key in ("tiff_file", "set_pan_x", "set_pan_y", "zoom",
                        "initial_return_point", "initial_point"):
                pattern = rf"(?m)^\s*{key}\s*=\s*(.+)$"
                self.assertEqual(re.findall(pattern, sim_shore),
                                 re.findall(pattern, self.configs["shoreside", site]))
            self.assertIn("Run = uSimMarineV22", self.sim_configs["pearl", site])
            self.assertIn("start_pos = " + options["pearl_start"],
                          self.sim_configs["pearl", site])
            self.assertIn("points         = " + options["pearl_return"],
                          self.sim_behaviors[site])

    def test_sitl_site_home_and_explicit_endpoint_flags(self):
        wrapper = self.mission / "capture-wrapper"
        wrapper.write_text("#!/usr/bin/env python3\nimport json, sys\n"
                           "print(json.dumps(sys.argv[1:]))\n")
        wrapper.chmod(0o755)
        for site, home in (("briggs", "42.3569186052,-71.0990291116,0,0"),
                           ("charles", "42.358436,-71.087448,0,0")):
            env = dict(self.env, MISSION_SITE=site, SITL_WRAPPER=str(wrapper),
                       SITL_PORT="15760", SITL_DIAGNOSTIC_PORT="15762",
                       SITL_TARGET_PORT="15763")
            env.pop("SITL_HOME", None)
            result = subprocess.run(
                [str(self.mission / "launch_sitl.sh"), "3", "config"],
                env=env, capture_output=True, text=True, check=True, timeout=10)
            args = json.loads(result.stdout)
            for flag, expected in (("--home", home), ("--port", "15760"),
                                   ("--diagnostic-port", "15762"), ("--sim-port", "15763"),
                                   ("--speedup", "3")):
                self.assertEqual(args[args.index(flag) + 1], expected)
            env["SITL_HOME"] = "42,-71,2,90"
            result = subprocess.run([str(self.mission / "launch_sitl.sh"), "config"],
                                    env=env, capture_output=True, text=True, check=True, timeout=10)
            args = json.loads(result.stdout)
            self.assertEqual(args[args.index("--home") + 1], env["SITL_HOME"])

    def test_no_default_loiters(self):
        for site in SITES:
            for node in (*NODES, "sim"):
                self.assertFalse(any(arg.startswith("--loiter")
                                     for arg in self.recipe(node, site)))
            self.assertNotIn("BHV_Loiter", self.sim_behaviors[site])
        for launcher in ("launch.sh", "launch_pearl.sh"):
            self.assertNotIn("--loiter", (MISSION / launcher).read_text())

    def test_housekeeping_recipes_are_explicit_and_dry_run_only(self):
        for target, command in (("clean", "./clean.sh"), ("kill", "ktm")):
            output = run_make(MISSION, "-n", target).stdout
            self.assertIn(command, output)
        logs = run_make(MISSION, "-n", "logs").stdout
        self.assertIn("scripts/collect_mission_logs.py", logs)
        self.assertIn('--node "auto"', logs)
        self.assertIn('--ap-protocol "serial"', logs)
        self.assertIn(PIXHAWK_IF00, logs)
        self.assertNotIn("ssh ", logs)
        for site in SITES:
            collect = run_make(MISSION, "-n", f"collect-logs-{site}").stdout
            self.assertIn("scripts/fetch_collected_logs.py", collect)
            self.assertIn(f"--site {site}", collect)
            self.assertIn('--drone-host "pearl@100.112.119.114"', collect)
            self.assertIn('--pearl-real-host "pearl@100.113.99.58"', collect)
            self.assertIn('--pearl-grass-host "pearl@100.71.137.47"', collect)
            self.assertIn("field_logs/fetched", collect)
            remote_root = ("/home/pearl/moos-ivp-pearl/missions/briggs_field/"
                           "2026_10_09_water_ops_prep/field_logs")
            self.assertIn(f'--drone-remote-root "{remote_root}"', collect)
            self.assertIn(f'--pearl-remote-root "{remote_root}"', collect)
            self.assertNotIn("scripts/collect_mission_logs.py", collect)
            overridden = run_make(MISSION, "-n", f"collect-logs-{site}",
                                  "COLLECT_DRONE_HOST=archive@drone.test",
                                  "COLLECT_DRONE_REMOTE_ROOT=/drone",
                                  "COLLECT_PEARL_REMOTE_ROOT=/pearl").stdout
            self.assertIn('--drone-host "archive@drone.test"', overridden)
            self.assertIn('--drone-remote-root "/drone"', overridden)
            self.assertIn('--pearl-remote-root "/pearl"', overridden)
        collect_sim = run_make(MISSION, "-n", "collect-logs-sim").stdout
        self.assertIn("scripts/collect_sim_logs.py", collect_sim)
        self.assertIn("field_logs/sim", collect_sim)
        self.assertNotIn("--drone-host", collect_sim)
        for site in SITES:
            for node in (*NODES, "sim"):
                args = self.recipe(node, site)
                self.assertNotIn("ktm", args)
                self.assertNotIn("./clean.sh", args)
                self.assertNotIn("--log_clean", args)

    def test_invalid_site_rejected_before_target_generation(self):
        targets = {path: path.read_bytes() for path in self.mission.glob("targ_*")}
        for node in NODES:
            launcher = "uav" if node == "drone" else node
            result = subprocess.run(
                [str(self.mission / f"launch_{launcher}.sh"), "--mode=REAL",
                 "--site=unknown", "--just_make"],
                capture_output=True, text=True, check=False, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("--site must be briggs or charles", result.stderr)
        for path, before in targets.items():
            self.assertEqual(path.read_bytes(), before)

    def test_default_is_help_not_live_launch(self):
        output = run_make(MISSION).stdout
        self.assertIn("Run locally", output)
        self.assertIn("generate only", output)
        self.assertIn("logs: after shutdown", output)
        self.assertIn("collect-logs-briggs/charles", output)
        self.assertIn("collect-logs-sim", output)

    def test_modes_models_and_all_tailnet_links(self):
        for site in SITES:
            pearl_ip = "100.71.137.47" if site == "briggs" else "100.113.99.58"
            for node in NODES:
                args = self.recipe(node, site)
                options = dict(arg[2:].split("=", 1) for arg in args[1:] if "=" in arg)
                self.assertEqual(options["site"], site)
                self.assertEqual(options["mode"],
                                 "GRASS" if (node, site) == ("pearl", "briggs") else "REAL")
                if node == "drone":
                    self.assertIn("--upload-params", args)
                else:
                    self.assertNotIn("--upload-params", args)
                if node == "shoreside":
                    self.assertEqual(options["ip"], "100.88.214.50")
                    self.assertEqual(options["uav_ip"], "100.112.119.114")
                    self.assertEqual(options["pearl_ip"], pearl_ip)
                elif node == "pearl":
                    self.assertEqual(options["ip"], pearl_ip)
                    self.assertEqual(options["shore"], "100.88.214.50")
                    self.assertEqual(options["uav_ip"], "100.112.119.114")
                else:
                    self.assertEqual(options["model"], "x500")
                    self.assertEqual(options["ip"], "100.112.119.114")
                    self.assertEqual(options["shore"], "100.88.214.50")
                    self.assertEqual(options["pearl_ip"], pearl_ip)
                    self.assertRegex(
                        self.configs[node, site],
                        rf"(?m)^\s*url\s*=\s*{re.escape(PIXHAWK_IF00)}\s*$",
                    )
                    self.assertRegex(
                        self.configs[node, site],
                        r"(?m)^\s*url_protocol\s*=\s*serial\s*$",
                    )
                    self.assertNotIn("url          = ttyACM0:115200",
                                     self.configs[node, site])

    def test_wifi_observability_is_uav_local_and_not_a_sherlock_dependency(self):
        variables = ("INTERFACE", "STATUS", "STATUS_VALID", "LINK_UP", "SIGNAL_DBM",
                     "SIGNAL_VALID", "SAMPLE_TIME")
        for site in SITES:
            drone = self.configs["drone", site]
            self.assertIn("wifi_interface = wlan1", drone)
            self.assertIn("wifi_prefix = UAV_WIFI", drone)
            self.assertIn("wifi_poll_interval = 2", drone)
            self.assertIn("wifi_syslog = true", drone)
            self.assertNotIn("Run = iSherlockTelemetry", drone)
            self.assertNotIn("wifi_interface =", self.sim_configs["drone", site])
            self.assertIn("WildCardLogging     = true", drone)
            for suffix in variables:
                self.assertIn("bridge = src=UAV_WIFI_" + suffix, drone)
                self.assertIn("scope = UAV_WIFI_" + suffix, self.configs["shoreside", site])

    def test_same_site_datum_on_every_node(self):
        for site, latitude, longitude in (("briggs", 42.3569186052, -71.0990291116),
                                          ("charles", 42.358436, -71.087448)):
            for node in NODES:
                source = self.configs[node, site]
                for name, expected in (("LatOrigin", latitude), ("LongOrigin", longitude)):
                    match = re.search(rf"^{name}\s*=\s*(\S+)", source, re.MULTILINE)
                    assert match is not None, f"Missing {name} in {node}-{site}"
                    self.assertAlmostEqual(float(match[1]), expected)
                self.assertNotIn("$(SITE", source)

    def test_site_imagery_and_framing(self):
        for site, image, pan_x, pan_y, zoom in (
                ("briggs", "../../images/briggs_test/briggs_test.tif", 50, 200, 2),
                ("charles", "../../images/MIT_SP.tif", 0, -400, 1)):
            source = self.configs["shoreside", site]
            self.assertRegex(source, rf"tiff_file\s*=\s*{re.escape(image)}")
            for name, wanted in (("set_pan_x", pan_x), ("set_pan_y", pan_y), ("zoom", zoom)):
                self.assertRegex(source, rf"(?m)^\s*{name}\s*=\s*{wanted}\s*$")

    def test_selected_return_and_rally_points(self):
        for site in SITES:
            pearl = dict(arg[2:].split("=", 1) for arg in self.recipe("pearl", site)[1:])
            shore = dict(arg[2:].split("=", 1) for arg in self.recipe("shoreside", site)[1:])
            drone = dict(arg[2:].split("=", 1) for arg in self.recipe("drone", site)[1:]
                         if "=" in arg)
            _, _, heading = map(float, pearl["start_pos"].split(","))
            self.assertEqual(heading, 0)
            if site == "briggs":
                origin = ("42.3569186052", "-71.0990291116")
                expected_points = ((42.356575, -71.098486), (42.356454, -71.098831))
            else:
                origin = ("42.358436", "-71.087448")
                expected_points = ((42.358465, -71.087338), (42.358542, -71.087357))
            for key, expected in zip(("pearl_return", "uav_rally"), expected_points,
                                     strict=True):
                result = subprocess.check_output(
                    [str(ROOT / "scripts/local_to_latlon.py"), *origin,
                     *shore[key].split(",")], text=True)
                lat, lon = map(float, result.split())
                self.assertAlmostEqual(lat, expected[0], places=8)
                self.assertAlmostEqual(lon, expected[1], places=8)
            self.assertEqual(pearl["pearl_return"], shore["pearl_return"])
            self.assertEqual(drone["secondary_landing"], shore["uav_rally"])
            source = self.configs["shoreside", site]
            return_x, return_y = shore["pearl_return"].split(",")
            rally_x, rally_y = shore["uav_rally"].split(",")
            self.assertIn(f"initial_return_point = x={return_x},y={return_y}", source)
            self.assertIn(f"initial_point = x={rally_x},y={rally_y}", source)
        self.assertIn("points         = 9.138278,2.997768", self.behaviors["charles"])

    def test_real_and_grass_do_not_inject_simulated_start_or_launch_sitl(self):
        for source in self.configs.values():
            self.assertNotIn("Run = uSimMarine", source)
            self.assertNotIn("start_pos =", source)
        self.assertNotIn("Run = pHelmIvP", self.configs["pearl", "briggs"])
        self.assertIn("Run = pHelmIvP", self.configs["pearl", "charles"])
        self.assertFalse((self.mission / "LOG_sitl").exists())
        self.assertFalse((self.mission / "LOG_params").exists())
        self.assertFalse((self.mission / "LOG_antler").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
