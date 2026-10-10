#!/usr/bin/env python3
"""Opt-in physical stop comparison on an owned copy of the landing SITL mission.

Runs the lane at the prep route's 3 m/s, 4 m capture, 8 m slowdown. No recovery
request is sent: final braking/hold is isolated from new rendezvous guidance.
All ports/processes/Compose resources are owned; artifacts remain in /tmp.
Use --legacy to measure the old HOLD_POSITION baseline (no new-path bounds).
"""

import argparse
import bisect
import json
import os
import shutil
import signal
import socket
import subprocess
import tempfile
import time
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
LANDING = MISSION.parents[1] / "pearl_uav_integration_tests/landing"


class Logs:
    def __init__(self, directory):
        self.directory = directory
        self.file = None
        self.rows = []
        self.last = {}

    def update(self):
        if self.file is None:
            paths = list(self.directory.glob("LOG_UAV*/*.alog"))
            if not paths:
                return
            self.file = paths[0].open()
        for line in self.file:
            parts = line.split(None, 3)
            if len(parts) != 4:
                continue
            try:
                stamp = float(parts[0])
            except ValueError:
                continue
            key, source, value = parts[1], parts[2], parts[3].strip()
            self.rows.append((stamp, key, source, value))
            self.last[key] = value

    def close(self):
        if self.file is not None:
            self.file.close()


def ports():
    reservations = []
    result = []
    try:
        for _ in range(9):
            tcp = socket.socket()
            tcp.bind(("127.0.0.1", 0))
            reservations.append(tcp)
            port = tcp.getsockname()[1]
            udp = socket.socket(type=socket.SOCK_DGRAM)
            udp.bind(("127.0.0.1", port))
            reservations.append(udp)
            result.append(port)
    finally:
        for sock in reservations:
            sock.close()
    return result


def wait_for(logs, process, predicate, timeout):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        logs.update()
        if process.poll() is not None:
            raise RuntimeError(f"Owned launcher exited: {process.returncode}")
        if logs.last.get("UAV_BRAKE_HOLD_PHASE") in ("FAILED", "CANCELED"):
            raise RuntimeError(f"Brake failed/preempted: {logs.last.get('UAV_BRAKE_HOLD_REASON')}")
        if predicate():
            return
        time.sleep(0.2)
    raise TimeoutError("Owned stop test timed out; inspect retained artifacts")


def validate(logs, legacy):
    command = "HOLD_POSITION" if legacy else "BRAKE_AND_HOLD"
    request = next(t for t, k, _, v in logs.rows if k == "ARDU_COMMAND" and v == command)
    data = {key: [(t, float(v)) for t, k, _, v in logs.rows if k == key]
            for key in ("NAV_X", "NAV_Y", "NAV_SPEED", "UAV_BRAKE_HOLD_SPEED",
                        "UAV_ALTITUDE", "UAV_YAW")}
    stamps = {k: [t for t, _ in v] for k, v in data.items()}

    def value(key, stamp):
        index = bisect.bisect_right(stamps[key], stamp) - 1
        if index < 0:
            raise AssertionError(f"Missing {key} at {stamp}")
        return data[key][index][1]

    # Incoming lane is south: forward progress = -NAV_Y. Measure peak to
    # subsequent backwards position, excluding startup and descent entirely.
    samples = [(t, -y) for t, y in data["NAV_Y"] if request <= t <= request + 8]
    peak = max(samples, key=lambda p: p[1])
    after = [p for p in samples if p[0] >= peak[0]]
    low = min(after, key=lambda p: p[1])
    backtrack = peak[1] - low[1]
    altitude = value("UAV_ALTITUDE", request)
    yaw = value("UAV_YAW", request)
    altitude_errors = [abs(v - altitude) for t, v in data["UAV_ALTITUDE"]
                       if request <= t <= request + 8]
    yaw_errors = [abs((v - yaw + 180) % 360 - 180) for t, v in data["UAV_YAW"]
                  if request <= t <= request + 8]
    assert max(altitude_errors) < 0.25, altitude_errors
    assert max(yaw_errors) < 2, yaw_errors
    result = {"max_altitude_error_m": max(altitude_errors),
              "max_yaw_error_deg": max(yaw_errors),
              "request_time": request, "request_speed_m_s": value("NAV_SPEED", request),
              "forward_peak": peak, "backward_minimum": low,
              "backtrack_m": backtrack, "command": command}
    assert result["request_speed_m_s"] > 1.5, result
    assert logs.last.get("UAV_LANDED_STATE") == "IN_AIR", logs.last
    assert float(logs.last["UAV_IS_ARMED"]) == 1
    assert not any(k == "ARDU_COMMAND" and v in ("AUTOLAND", "RETURN_TO_LAUNCH", "RETURN")
                   for _, k, _, v in logs.rows)
    if not legacy:
        hold = next(t for t, k, _, v in logs.rows
                    if k == "UAV_BRAKE_HOLD_PHASE" and v == "HOLD_TARGET_SENT")
        native_speed = value("UAV_BRAKE_HOLD_SPEED", hold)
        result.update(hold_target_time=hold, native_hold_speed_m_s=native_speed)
        assert hold > request and native_speed <= 0.15, result
        assert backtrack < 0.25, result
        assert not any(k == "UAV_BRAKE_HOLD_PHASE" and v in ("FAILED", "CANCELED")
                       for _, k, _, v in logs.rows)
        assert any(k == "UAV_COMMAND_RESULT" and "command=BRAKE_AND_HOLD,status=COMPLETED" in v
                   and "NATIVE_STOPPED_HOLD_TARGET_QUEUED" in v for _, k, _, v in logs.rows)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--legacy", action="store_true")
    parser.add_argument("--warp", type=int, default=1)
    args = parser.parse_args()
    if args.warp < 1:
        parser.error("--warp must be positive")
    directory = Path(tempfile.mkdtemp(prefix="route-brake-legacy-" if args.legacy else "route-brake-native-"))
    for path in LANDING.iterdir():
        if path.name.startswith("meta_") or path.name in ("launch.sh", "plug_origin_warp.moos", "sitl.parm"):
            shutil.copy2(path, directory / path.name)
    behavior = directory / "meta_uav.bhv"
    source = behavior.read_text()
    lane, rest = source.split("// Live-updated rendezvous guidance", 1)
    lane = lane.replace("capture_radius = 0.5", "capture_radius = 4")
    lane = lane.replace("slip_radius    = 1", "slip_radius    = 5")
    lane = lane.replace("end_spd        = slow_dist=5, stop_dist=0", "end_spd        = slow_dist=8, stop_dist=0")
    if not args.legacy:
        lane = lane.replace("ARDU_COMMAND=HOLD_POSITION", "ARDU_COMMAND=BRAKE_AND_HOLD")
    behavior.write_text(lane + "// Live-updated rendezvous guidance" + rest)
    command = [str(directory / "launch.sh"), "-ng", "-x", str(args.warp), "--uav_speed=3"]
    names = ("shore_mport", "uav_mport", "pearl_mport", "shore_pshare", "uav_pshare",
             "pearl_pshare", "sitl_port", "sitl_target_port", "sitl_diagnostic_port")
    command.extend(f"--{name}={port}" for name, port in zip(names, ports(), strict=True))
    (directory / "run.json").write_text(json.dumps({"command": command, "bridge": shutil.which("pArduBridge")}, indent=2))
    logs = Logs(directory)
    process = None
    result = {"result": "FAIL"}
    print("ARTIFACTS:", directory, flush=True)
    try:
        with (directory / "launcher.log").open("w") as output:
            process = subprocess.Popen(command, cwd=directory, stdout=output,
                                       stderr=subprocess.STDOUT, start_new_session=True)
        if args.legacy:
            predicate = lambda: logs.last.get("AUTOPILOT_MODE") == "HELM_INACTIVE_LOITERING"
        else:
            predicate = lambda: logs.last.get("UAV_BRAKE_HOLD_PHASE") == "HOLD_TARGET_SENT"
        wait_for(logs, process, predicate, 180)
        # Observation window only, never a readiness/arrival condition.
        time.sleep(9 / args.warp)
        logs.update()
        result.update(validate(logs, args.legacy), result="PASS")
        print(json.dumps(result, indent=2), flush=True)
    finally:
        if process is not None and process.poll() is None:
            os.kill(process.pid, signal.SIGINT)  # launcher cleans only its owned resources
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                # Do not kill the launcher during its Compose teardown.
                process.wait(timeout=60)
        logs.update()
        logs.close()
        (directory / "result.json").write_text(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
