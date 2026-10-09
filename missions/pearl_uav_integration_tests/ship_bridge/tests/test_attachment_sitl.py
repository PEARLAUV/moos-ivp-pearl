#!/usr/bin/env python3
"""Isolated, opt-in SITL attach/detach/reattach test; UAV never flies."""

import argparse
import importlib.util
import json
import math
import os
import shutil
import signal
import socket
import subprocess
import tempfile
import time
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
# Reuse this collection's streaming alog reader; no recovery logic is executed.
SPEC = importlib.util.spec_from_file_location(
    "recovery_logs", MISSION.parent / "landing/tests/test_recovery_sitl.py")
assert SPEC is not None and SPEC.loader is not None
READER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(READER)
setattr(READER, "TRACKED", {
    "NAV_X", "NAV_Y", "NAV_HEADING", "NAV_SPEED", "UAV_ALTITUDE_MSL", "UAV_IS_ARMED",
    "UAV_LANDED_STATE", "IVPHELM_STATE", "IVPHELM_ALLSTOP", "UAV_TEST_PHASE",
    "SHIP_POSE_ENABLED", "SHIP_POSE_ENABLED_PEARL", "SHIP_ATTACHMENT_REQUESTED", "MAVLINK_SHIP_POSE_ACTIVE",
    "MAVLINK_SHIP_BRIDGE_CONNECTED", "ARDU_COMMAND", "ARM_UAV", "NODE_REPORT",
    "PEARL_HELM_STATE", "UAV_HELM_STATE",
})


def xy(logs, node):
    return tuple(float(logs.last[node][key]) for key in ("NAV_X", "NAV_Y"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--warp", type=int, default=2)
    args = parser.parse_args()
    if args.warp < 1:
        parser.error("--warp must be positive")
    directory = Path(tempfile.mkdtemp(prefix="ship-attachment-"))
    for path in MISSION.iterdir():
        if path.name.startswith("meta_") or path.name in ("launch.sh", "plug_origin_warp.moos", "sitl.parm"):
            shutil.copy2(path, directory / path.name)
    ports, sockets = [], []
    try:
        for _ in range(9):
            tcp = socket.socket()
            tcp.bind(("127.0.0.1", 0))
            port = tcp.getsockname()[1]
            udp = socket.socket(type=socket.SOCK_DGRAM)
            udp.bind(("127.0.0.1", port))
            sockets.extend((tcp, udp))
            ports.append(port)
    finally:
        for sock in sockets:
            sock.close()
    names = ("shore_mport", "uav_mport", "pearl_mport", "shore_pshare", "uav_pshare",
             "pearl_pshare", "sitl_port", "sitl_target_port", "sitl_diagnostic_port")
    command = [str(directory / "launch.sh"), "-ng", "-x", str(args.warp)]
    command.extend(f"--{name}={port}" for name, port in zip(names, ports))
    (directory / "run.json").write_text(json.dumps({"command": command}, indent=2))
    logs = READER.Logs(directory)
    result: dict[str, object] = {"result": "FAIL"}
    print("ARTIFACTS:", directory, flush=True)
    with (directory / "launcher.log").open("w") as output:
        process = subprocess.Popen(command, cwd=directory, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)

    def update():
        logs.update()
        if process.poll() is not None:
            raise RuntimeError(f"Launcher exited: {process.returncode}")
        for node in ("UAV", "PEARL"):
            if logs.last[node].get("IVPHELM_STATE") == "MALCONFIG":
                raise RuntimeError(f"{node} Helm MALCONFIG")

    def wait_for(predicate, timeout=60):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            update()
            if predicate():
                return
            time.sleep(0.2)
        raise TimeoutError(f"State not reached: {logs.last}")

    def pause(seconds):
        deadline = time.monotonic() + seconds / args.warp
        while time.monotonic() < deadline:
            update()
            time.sleep(0.2)

    def poke(enabled):
        # Exactly the button payload, through shoreside's addressed qbridge.
        subprocess.run(["uPokeDB", str(directory / "targ_shoreside.moos"), "--quiet",
                        f"SHIP_POSE_ENABLED_PEARL:={'true' if enabled else 'false'}"],
                       check=True, capture_output=True, timeout=12)
        wait_for(lambda: float(logs.last["PEARL"].get("MAVLINK_SHIP_POSE_ACTIVE", "-1")) == int(enabled)
                 and logs.last["SHORESIDE"].get("SHIP_ATTACHMENT_REQUESTED") == str(enabled).lower())

    def attached():
        wait_for(lambda: math.dist(xy(logs, "UAV"), xy(logs, "PEARL")) < 0.8, timeout=30)
        pause(6)
        error = math.dist(xy(logs, "UAV"), xy(logs, "PEARL"))
        assert error < 0.8, (error, logs.last)
        assert abs(float(logs.last["UAV"]["UAV_ALTITUDE_MSL"]) - 1) < 0.3, logs.last
        return error

    try:
        wait_for(lambda: all("NAV_X" in logs.last[n] and "NAV_Y" in logs.last[n]
                             for n in ("UAV", "PEARL")) and
                 logs.last["UAV"].get("UAV_LANDED_STATE") == "ON_GROUND" and
                 float(logs.last["UAV"].get("UAV_IS_ARMED", "-1")) == 0)
        pause(12)  # Let GPS/EKF settle without arming.
        assert logs.last["UAV"].get("UAV_TEST_PHASE") == "IDLE", logs.last
        assert logs.last["UAV"].get("IVPHELM_STATE") == "PARK", logs.last
        assert float(logs.last["PEARL"]["MAVLINK_SHIP_POSE_ACTIVE"]) == 0, logs.last
        assert math.dist(xy(logs, "UAV"), (25, -115)) < 0.5, logs.last
        assert math.dist(xy(logs, "UAV"), xy(logs, "PEARL")) > 10, logs.last
        poke(True)
        result["attach_error_m"] = attached()
        before = xy(logs, "PEARL")
        pause(6)
        assert math.dist(before, xy(logs, "PEARL")) > 1, logs.last
        assert math.dist(xy(logs, "UAV"), xy(logs, "PEARL")) < 0.8, logs.last
        poke(False)
        pause(4)  # Patch timeout is one simulated second; allow telemetry settling too.
        frozen = xy(logs, "UAV")
        pearl = xy(logs, "PEARL")
        pause(10)
        drift = math.dist(frozen, xy(logs, "UAV"))
        assert drift < 0.4, (drift, logs.last)
        assert math.dist(pearl, xy(logs, "PEARL")) > 2, logs.last
        assert math.dist(xy(logs, "UAV"), xy(logs, "PEARL")) > 2, logs.last
        result["detached_drift_m"] = drift
        poke(True)
        result["reattach_error_m"] = attached()
        poke(False)
        pause(3)
        logs.update()
        assert all(float(row[3]) == 0 for row in logs.matching("UAV", "UAV_IS_ARMED"))
        assert not logs.matching("UAV", "UAV_LANDED_STATE", "IN_AIR")
        assert not logs.matching("UAV", "ARDU_COMMAND")
        assert not logs.matching("UAV", "ARM_UAV")
        assert all(row[3] == "PARK" for row in logs.matching("UAV", "IVPHELM_STATE"))
        reports = logs.matching("SHORESIDE", "NODE_REPORT")
        assert all(any(f"NAME={name}," in row[3] for row in reports) for name in ("pearl", "uav"))
        result["result"] = "PASS"
        print(json.dumps(result, indent=2), flush=True)
    except Exception as error:
        result["error"] = repr(error)
        raise
    finally:
        result["last"] = logs.last
        (directory / "result.json").write_text(json.dumps(result, indent=2))
        if process.poll() is None:
            process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
        logs.close()
        print(f"{result['result']}; artifacts: {directory}", flush=True)


if __name__ == "__main__":
    main()
