#!/usr/bin/env python3
"""Opt-in isolated real-SITL test of all three target-observation passes."""

import argparse
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
TRACKED = {"TARGET_TEST_PHASE", "NAV_X", "NAV_Y", "NAV_ALTITUDE", "UAV_ALTITUDE_MSL",
           "LANDMARK_EMULATOR_ACTIVE", "LANDMARK_EMULATOR_DISTANCE",
           "LANDMARK_EMULATOR_CONDITIONS_MET", "UAV_LANDING_TARGET_AVAILABLE",
           "UAV_LANDING_TARGET_FRAME", "UAV_LANDING_TARGET_AGE", "UAV_LANDING_TARGET_Z",
           "VIEW_POINT", "VIEW_CIRCLE", "VIEW_POLYGON", "LANDING_TARGET_VIEW_ACTIVE",
           "IVPHELM_STATE", "UAV_LANDED_STATE", "ARDU_COMMAND", "TARGET_NAV_HEADING"}


class Logs:
    def __init__(self, directory):
        self.directory = directory
        self.files = {}
        self.last = {node: {} for node in ("UAV", "SHORESIDE")}
        self.rows = {node: [] for node in self.last}
        self.samples = []
        self.phases = []

    def update(self):
        for node in self.last:
            if node not in self.files:
                paths = list(self.directory.glob(f"LOG_{node}*/*.alog"))
                if not paths:
                    continue
                self.files[node] = paths[0].open()
            for line in self.files[node]:
                fields = line.split(None, 3)
                if len(fields) != 4 or fields[1] not in TRACKED:
                    continue
                stamp, key, source, value = fields
                value = value.strip()
                self.last[node][key] = value
                self.rows[node].append((stamp, key, source, value))
                if (node == "UAV" and key == "TARGET_TEST_PHASE"
                        and (not self.phases or self.phases[-1] != value)):
                    self.phases.append(value)
                    print(stamp, "phase:", value, flush=True)
                if node == "UAV" and key == "NAV_Y":
                    self.samples.append(dict(self.last[node]))

    def close(self):
        for file in self.files.values():
            file.close()


def validate(logs):
    assert logs.phases == ["takeoff", "pass_1", "shift_right", "prepare_2", "pass_2",
                           "shift_center", "prepare_3", "pass_3", "COMPLETE"], logs.phases
    result = []
    for phase, x, altitude, visible in (("pass_1", -3, 8, True),
                                      ("pass_2", 5, 4, True), ("pass_3", 0, 15, False)):
        samples = [s for s in logs.samples if s.get("TARGET_TEST_PHASE") == phase
                   and "NAV_X" in s and "UAV_ALTITUDE_MSL" in s]
        assert samples, phase
        closest = min(samples, key=lambda s: abs(float(s["NAV_Y"]) + 115))
        assert abs(float(closest["NAV_Y"]) + 115) < 0.5, closest
        assert abs(float(closest["NAV_X"]) - x) < 0.4, closest
        assert abs(float(closest["UAV_ALTITUDE_MSL"]) - altitude) < 0.3, closest
        assert (float(closest["LANDMARK_EMULATOR_ACTIVE"]) == 1) == visible, closest
        assert (float(closest["UAV_LANDING_TARGET_AVAILABLE"]) == 1) == visible, closest
        assert (float(closest["LANDING_TARGET_VIEW_ACTIVE"]) == 1) == visible, closest
        if visible:
            assert float(closest["UAV_LANDING_TARGET_FRAME"]) == 12
            assert float(closest["UAV_LANDING_TARGET_AGE"]) < 0.5
            assert abs(float(closest["UAV_LANDING_TARGET_Z"]) -
                       (float(closest["UAV_ALTITUDE_MSL"]) - 1)) < 0.15, closest
        else:
            assert all(float(s.get("LANDMARK_EMULATOR_ACTIVE", "0")) == 0 for s in samples)
        result.append({"phase": phase, "x": float(closest["NAV_X"]),
                       "y": float(closest["NAV_Y"]), "altitude_msl": float(closest["UAV_ALTITUDE_MSL"]),
                       "broadcasting": visible})
    assert float(logs.last["UAV"]["TARGET_NAV_HEADING"]) == 30
    assert not any(row[3] == "MALCONFIG" for row in logs.rows["UAV"] if row[1] == "IVPHELM_STATE")
    assert not any(row[3] == "AUTOLAND" for row in logs.rows["UAV"] if row[1] == "ARDU_COMMAND")
    # The target remains visible during every pass, including the out-of-range third pass.
    for label in ("landing_target_point", "landing_target_range", "landing_target_slice"):
        specs = [dict(p.split("=", 1) for p in row[3].split(",") if "=" in p)
                 for row in logs.rows["SHORESIDE"] if row[1] in ("VIEW_POINT", "VIEW_CIRCLE")]
        specs = [s for s in specs if s.get("label") == label]
        assert specs, label
        first_active = next(i for i, s in enumerate(specs) if s["active"] == "true")
        assert all(s["active"] == "true" for s in specs[first_active:]), label
        assert all(float(s["x"]) == 0 and float(s["y"]) == -115 for s in specs[first_active:])
    polygons = [row[3] for row in logs.rows["SHORESIDE"] if row[1] == "VIEW_POLYGON"
                and "label=received_landing_target" in row[3]]
    active_polygons = [spec for spec in polygons if "active=true" in spec]
    assert active_polygons
    for spec in active_polygons:
        points = [tuple(map(float, pair.split(","))) for pair in
                  spec.split("pts={", 1)[1].split("}", 1)[0].split(":")]
        heading = math.degrees(math.atan2(points[1][0]-points[0][0], points[1][1]-points[0][1]))
        error = (heading - 30 + 180) % 360 - 180
        assert abs(error) < 3, (heading, spec)  # Heading remains fixed on both N/S passes.
    assert "active=false" in polygons[-1]  # No received-data footprint on the 15 m pass.
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--warp", type=int, default=2)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.warp < 1:
        parser.error("--warp must be positive")
    directory = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix="target-passes-"))
    if args.output:
        directory.mkdir(parents=True, exist_ok=False)
    for path in MISSION.iterdir():
        if path.name.startswith("meta_") or path.name in ("launch.sh", "plug_origin_warp.moos"):
            shutil.copy2(path, directory / path.name)
    ports, sockets = [], []
    try:
        for _ in range(7):
            tcp = socket.socket()
            tcp.bind(("127.0.0.1", 0))
            sockets.append(tcp)
            port = tcp.getsockname()[1]
            udp = socket.socket(type=socket.SOCK_DGRAM)
            udp.bind(("127.0.0.1", port))
            sockets.append(udp)
            ports.append(port)
    finally:
        for sock in sockets:
            sock.close()
    names = ("shore_mport", "uav_mport", "shore_pshare", "uav_pshare", "sitl_port",
             "sitl_target_port", "sitl_diagnostic_port")
    command = [str(directory / "launch.sh"), "-ng", "-x", str(args.warp)]
    command.extend(f"--{key}={port}" for key, port in zip(names, ports))
    (directory / "run.json").write_text(json.dumps({"command": command}, indent=2))
    logs = Logs(directory)
    result: dict[str, object] = {"result": "FAIL"}
    print("ARTIFACTS:", directory, flush=True)
    with (directory / "launcher.log").open("w") as output:
        process = subprocess.Popen(command, cwd=directory, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
    try:
        deadline = time.monotonic() + 240
        while time.monotonic() < deadline:
            logs.update()
            if process.poll() is not None:
                raise RuntimeError(f"Launcher exited: {process.returncode}")
            if logs.last["UAV"].get("TARGET_TEST_PHASE") == "COMPLETE":
                break
            if logs.last["UAV"].get("IVPHELM_STATE") == "MALCONFIG":
                raise RuntimeError("Helm MALCONFIG")
            time.sleep(0.3)
        else:
            raise TimeoutError("Pass sequence timed out")
        time.sleep(2)
        logs.update()
        result["passes"] = validate(logs)
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
