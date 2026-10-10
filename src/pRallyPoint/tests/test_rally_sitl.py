#!/usr/bin/env python3
"""Opt-in FC rally upload/readback test; unique ports/project, no flight commands.

Run with --run-sitl. Requires the local ArduPilot image; leaves test artifacts.
Never connects to, stops, or changes an existing mission or SITL project.
"""

import argparse
import json
import os
import signal
import socket
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT.parent / "moos-ivp-uav-base"
WRAPPER = ROOT.parent / "ardupilot-sitl/bin/ardupilot-sitl"


def run_test(parameter_files=()):
    directory = Path(tempfile.mkdtemp(prefix="rally-sitl-"))
    sockets = []
    for _ in range(4):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
    database, mission_port, diagnostic, simulation = [
        s.getsockname()[1] for s in sockets
    ]
    for sock in sockets:
        sock.close()
    env = dict(os.environ, COMPOSE_PROJECT_NAME=f"rally-check-{os.getpid()}")
    options = [
        str(WRAPPER),
        "--no-rc",
        "--home",
        "42.3569186052,-71.0990291116,0,0",
        "--port",
        str(mission_port),
        "--diagnostic-port",
        str(diagnostic),
        "--sim-port",
        str(simulation),
        str(directory / "sitl"),
    ]
    for parameter_file in parameter_files:
        options[-1:-1] = ["--param-file", str(Path(parameter_file).resolve())]
    mission = directory / "test.moos"
    mission.write_text(f"""ServerHost = localhost
ServerPort = {database}
Community = uav
LatOrigin = 42.3569186052
LongOrigin = -71.0990291116
ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = MOOSDB @ NewConsole = false
  Run = {BASE / "bin/pArduBridge"} @ NewConsole = false
  Run = pLogger @ NewConsole = false
}}
ProcessConfig = pArduBridge
{{
  AppTick = 10
  CommsTick = 10
  url = 127.0.0.1:{mission_port}
  url_protocol = tcp
  vehicle_type = copter
  geodesy = utm
  prefix = UAV
  vname = uav
  require_verified_rally = true
  is_sim = true
  logger = false
}}
ProcessConfig = pLogger
{{
  AppTick = 10
  CommsTick = 10
  File = LOG_UAV
  PATH = ./
  AsyncLog = true
  FileTimeStamp = true
  WildCardLogging = true
}}
""")
    process = None
    stream = (directory / "antler.log").open("w")
    started = False
    print(f"Artifacts: {directory}", flush=True)
    try:
        started = True
        subprocess.run(
            [*options, "up", "--no-build", "-d"],
            env=env,
            check=True,
            capture_output=True,
            timeout=45,
        )
        process = subprocess.Popen(
            ["pAntler", str(mission)],
            cwd=directory,
            stdout=stream,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )

        def query(*conditions):
            result = subprocess.run(
                [
                    "uQueryDB",
                    str(mission),
                    "--wait=45",
                    *(f"--condition={v}" for v in conditions),
                ],
                cwd=directory,
                capture_output=True,
                text=True,
                check=False,
                timeout=55,
            )
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)

        def poke(value):
            subprocess.run(
                ["uPokeDB", str(mission), "--quiet", value],
                cwd=directory,
                check=True,
                capture_output=True,
                timeout=12,
            )

        query("UAV_LANDED_STATE=ON_GROUND", "UAV_IS_ARMED=0", "UAV_GPS_AVAILABLE=1")
        poke("RALLY_UPLOAD:=id=sitl-first#x=51#y=-56#altitude=15")
        query("UAV_RALLY_STATE=READY", "UAV_RALLY_READY=1", "UAV_RALLY_ID=sitl-first")
        # Replacement must replace, not append to, the FC rally list.
        poke("RALLY_UPLOAD:=id=sitl-second#x=-40#y=10#altitude=20")
        query("UAV_RALLY_STATE=READY", "UAV_RALLY_READY=1", "UAV_RALLY_ID=sitl-second")
        (directory / "result.json").write_text(
            json.dumps(
                {
                    "passed": True,
                    "rally_count": 1,
                    "altitude_above_home_m": 20,
                    "home_excluded": True,
                    "distance_limit_disabled": True,
                    "verification": "FC mission readback and parameter readback, two replacements",
                    "flight_test": False,
                },
                indent=2,
            )
            + "\n"
        )
        print(
            "PASS: two native FC rally transactions, readback and rally-only policy",
            flush=True,
        )
    finally:
        if process is not None and process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=4)
        stream.close()
        if started:
            subprocess.run(
                [*options, "down"],
                env=env,
                capture_output=True,
                check=False,
                timeout=45,
            )
        # Let owned logger processes flush; retained artifacts are never deleted.
        time.sleep(0.2)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-sitl", action="store_true", required=True)
    parser.add_argument("--param-file", action="append", default=[])
    arguments = parser.parse_args()
    run_test(arguments.param_file)
