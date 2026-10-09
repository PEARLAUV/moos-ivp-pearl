#!/usr/bin/env python3
"""Opt-in real SITL recovery test; never stops or pokes unrelated missions.

Requires the patched ardupilot-sitl:local image and mission executables on PATH.
Artifacts remain outside the mission's clean.sh scope, including on failure.
"""

import argparse
import json
import math
import os
import shutil
import signal
import socket
import struct
import subprocess
import tempfile
import threading
import time
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
NODES = ("UAV", "PEARL", "SHORESIDE")
TRACKED = {
    "RECOVER", "RECOVER_UAV", "ARDU_COMMAND", "UAV_COMMAND_RESULT",
    "LANDING_OWN_PHASE", "LANDING_OWN_SESSION", "LANDING_OWN_CHECKS_READY",
    "LANDING_OWN_CLEARANCE", "LANDING_DECK_ENABLED", "LANDING_EXCHANGE",
    "RENDEZVOUS_START", "RENDEZVOUS_OWN_PHASE", "RENDEZVOUS_OWN_MOTION",
    "UAV_AUTO_START", "UAV_LANDED_STATE", "UAV_IS_ARMED", "UAV_ALTITUDE_MSL",
    "NAV_X", "NAV_Y", "NAV_HEADING", "IVPHELM_STATE", "IVPHELM_ALLSTOP",
    "UAV_LANDING_TARGET_AVAILABLE", "UAV_LANDING_TARGET_FRAME",
    "UAV_LANDING_TARGET_AGE", "LANDMARK_EMULATOR_ACTIVE",
    "LANDMARK_EMULATOR_DISTANCE", "VIEW_CIRCLE", "VIEW_POINT", "VIEW_POLYGON",
    "LANDING_TARGET_VIEW_ACTIVE", "MAVLINK_SHIP_POSE_ACTIVE", "MAVLINK_SHIP_BRIDGE_CONNECTED",
}


class Logs:
    def __init__(self, directory):
        self.directory = directory
        self.files = {}
        self.starts = {}
        self.last = {node: {} for node in NODES}
        self.rows = {node: [] for node in NODES}

    def update(self):
        for node in NODES:
            if node not in self.files:
                paths = list(self.directory.glob(f"LOG_{node}*/*.alog"))
                if not paths:
                    continue
                self.files[node] = paths[0].open()
            for line in self.files[node]:
                if line.startswith("%% LOGSTART"):
                    self.starts[node] = float(line.split()[-1])
                    continue
                fields = line.split(None, 3)
                if len(fields) != 4 or fields[1] not in TRACKED:
                    continue
                relative, variable, source, value = fields
                try:
                    stamp = self.starts[node] + float(relative)
                except (ValueError, KeyError):
                    continue
                value = value.strip()
                old = self.last[node].get(variable)
                self.last[node][variable] = value
                self.rows[node].append((stamp, variable, source, value))
                if old != value and variable in {
                    "LANDING_OWN_PHASE", "RENDEZVOUS_OWN_PHASE",
                    "UAV_AUTO_START", "UAV_LANDED_STATE",
                }:
                    print(f"{node} {relative}: {variable}={value}", flush=True)

    def matching(self, node, variable, value=None):
        return [r for r in self.rows[node] if r[1] == variable and
                (value is None or r[3] == value)]

    def first(self, node, variable, value):
        matches = self.matching(node, variable, value)
        assert matches, f"Missing {node} {variable}={value}"
        return matches[0][0]

    def close(self):
        for file in self.files.values():
            file.close()


def gcs_heartbeat(sequence):
    """MAVLink v1 GCS heartbeat only: enables diagnostic STATUSTEXT, no commands."""
    payload = struct.pack("<IBBBBB", 0, 6, 8, 0, 4, 3)
    packet = bytes((len(payload), sequence % 256, 254, 190, 0)) + payload
    crc = 0xFFFF
    for byte in packet + bytes((50,)):
        tmp = byte ^ (crc & 0xFF)
        tmp ^= (tmp << 4) & 0xFF
        crc = ((crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)) & 0xFFFF
    return b"\xfe" + packet + struct.pack("<H", crc)


def capture_status(port, stop, messages):
    """Observe ArduPilot itself on its separate diagnostics link."""
    link = None
    while not stop.is_set():
        try:
            link = socket.create_connection(("127.0.0.1", port), timeout=1)
            link.settimeout(0.2)
            break
        except OSError:
            stop.wait(0.2)
    if link is None:
        return
    buffer = bytearray()
    sequence, sent_at = 0, 0
    try:
        while not stop.is_set():
            if time.monotonic() - sent_at >= 1:
                link.sendall(gcs_heartbeat(sequence))
                sequence += 1
                sent_at = time.monotonic()
            try:
                data = link.recv(4096)
                if not data:
                    break
                buffer.extend(data)
            except TimeoutError:
                continue
            while len(buffer) >= 10:
                if buffer[0] not in (0xFD, 0xFE):
                    del buffer[0]
                    continue
                v2 = buffer[0] == 0xFD
                size = (12 if v2 else 8) + buffer[1]
                if v2 and buffer[2] & 1:
                    size += 13
                if len(buffer) < size:
                    break
                packet = bytes(buffer[:size])
                del buffer[:size]
                message_id = int.from_bytes(packet[7:10], "little") if v2 else packet[5]
                payload = packet[10:10 + packet[1]] if v2 else packet[6:6 + packet[1]]
                if message_id == 253 and len(payload) > 1:
                    text = payload[1:51].split(b"\0")[0].decode(errors="replace")
                    messages.append({"time": time.time(), "text": text})
                    print("ArduPilot:", text, flush=True)
    except OSError as error:
        messages.append({"time": time.time(), "error": str(error)})
    finally:
        link.close()


def fields(spec, separator=","):
    return dict(part.split("=", 1) for part in spec.split(separator) if "=" in part)


def validate(logs, messages, deck_altitude, target_range):
    uav, pearl = logs.last["UAV"], logs.last["PEARL"]
    for node in ("UAV", "PEARL"):
        assert not logs.matching(node, "LANDING_OWN_PHASE", "ABORTED")
        assert logs.last[node]["LANDING_OWN_PHASE"] == "COMPLETE"
    assert uav["LANDING_OWN_SESSION"] == pearl["LANDING_OWN_SESSION"]
    assert logs.matching("UAV", "RECOVER", "true")
    assert not logs.matching("PEARL", "RECOVER")
    agreed = logs.first("PEARL", "LANDING_OWN_PHASE", "AGREED")
    for node in ("UAV", "PEARL"):
        assert logs.first(node, "RENDEZVOUS_START", "true") >= agreed
    cleared = logs.first("PEARL", "LANDING_OWN_CLEARANCE", "true")
    for node in ("UAV", "PEARL"):
        assert logs.first(node, "RENDEZVOUS_OWN_PHASE", "COMPLETE") <= cleared
    assert logs.first("PEARL", "LANDING_DECK_ENABLED", "true") >= cleared - 0.01
    assert logs.first("UAV", "LANDMARK_EMULATOR_ACTIVE", "1.00000") < cleared
    assert logs.first("UAV", "UAV_LANDING_TARGET_AVAILABLE", "1.00000") < cleared
    land_commands = logs.matching("UAV", "ARDU_COMMAND", "AUTOLAND")
    assert len(land_commands) == 1, f"Expected exactly one AUTOLAND: {land_commands}"
    commanded = land_commands[0][0]
    assert commanded >= cleared
    assert logs.first("UAV", "LANDING_OWN_CHECKS_READY", "true") <= commanded
    # Later bridge results (e.g. disarmed GUIDED preparation) can replace the
    # latest value; verify the LAND acceptance event, not an unrelated command.
    assert any("command=LAND,status=ACCEPTED" in r[3]
               for r in logs.matching("UAV", "UAV_COMMAND_RESULT") if r[0] >= commanded)
    parked = logs.first("UAV", "IVPHELM_STATE", "PARK")
    assert commanded <= parked < logs.first("UAV", "UAV_LANDED_STATE", "LANDING")
    # No guidance-hole warnings at the handoff; controller LAND owns descent.
    assert not any("MALCONFIG" in r[3] for r in logs.matching("UAV", "IVPHELM_STATE"))
    assert not any("MissingDecVars" in r[3] for r in logs.matching("UAV", "IVPHELM_ALLSTOP")
                   if commanded - 1 <= r[0] <= parked + 1)
    texts = [m.get("text", "") for m in messages]
    assert any("PrecLand: Target Found" in t for t in texts), texts
    assert any("PrecLand: Init Complete" in t for t in texts), texts
    assert not any("PrecLand: Init Failed" in t or "PrecLand: Target Lost" in t for t in texts)
    assert float(uav["UAV_LANDING_TARGET_FRAME"]) == 12
    assert float(uav["UAV_LANDING_TARGET_AGE"]) < 0.5
    assert uav["UAV_LANDED_STATE"] == "ON_GROUND"
    assert float(uav["UAV_IS_ARMED"]) == 0
    assert abs(float(uav["UAV_ALTITUDE_MSL"]) - deck_altitude) < 0.25
    offset = math.hypot(float(uav["NAV_X"]) - float(pearl["NAV_X"]),
                        float(uav["NAV_Y"]) - float(pearl["NAV_Y"]))
    assert offset < 0.25, offset
    # Both independent rendezvous instances must relay the same winning freeze.
    motions = [fields(logs.last[n]["RENDEZVOUS_OWN_MOTION"], "#") for n in ("UAV", "PEARL")]
    for key in ("point_x", "point_y", "freeze_time", "freeze_node"):
        assert motions[0][key] == motions[1][key], (key, motions)
    assert float(motions[0]["freeze_time"]) > 0
    active, inside, outside = False, False, False
    for row in logs.rows["UAV"]:
        if row[1] == "LANDMARK_EMULATOR_ACTIVE":
            active = float(row[3]) == 1
        elif row[1] == "LANDMARK_EMULATOR_DISTANCE":
            distance = float(row[3])
            if active:
                assert 0 < distance <= target_range + 0.01, row
                inside = True
            if distance > target_range:
                assert not active, row
                outside = True
    assert inside and outside
    # Terminal rendezvous markers are removed at both producers and shoreside;
    # selected/frozen point metadata above remains available for coordination.
    for node in ("UAV", "PEARL"):
        label = "rendezvous_" + node.lower()
        own_circles = [r for r in logs.matching(node, "VIEW_CIRCLE") if f"label={label}," in r[3]]
        assert any("active=true" in r[3] for r in own_circles), label
        assert "active=false" in own_circles[-1][3], label
        removed = [r for r in own_circles if "active=false" in r[3]]
        assert removed[0][0] >= logs.first(node, "RENDEZVOUS_OWN_PHASE", "COMPLETE"), label
        forwarded = [r for r in logs.matching("SHORESIDE", "VIEW_CIRCLE") if f"label={label}," in r[3]]
        assert forwarded and "active=false" in forwarded[-1][3], label
    polygons = [r for r in logs.matching("SHORESIDE", "VIEW_POLYGON")
                if "label=received_landing_target" in r[3]]
    assert any("active=true" in r[3] for r in polygons)
    assert float(logs.last["SHORESIDE"]["LANDING_TARGET_VIEW_ACTIVE"]) == 1
    assert float(pearl["MAVLINK_SHIP_POSE_ACTIVE"]) == 1
    assert float(pearl["MAVLINK_SHIP_BRIDGE_CONNECTED"]) == 1
    # Verify simulator visualizations reached shoreside, not just the producer.
    circles = [fields(r[3]) for r in logs.matching("SHORESIDE", "VIEW_CIRCLE")]
    for label in ("landing_target_range", "landing_target_slice"):
        active = [c for c in circles if c.get("label") == label and c.get("active") == "true"]
        assert active, label
        assert all(c["label_color"] == "invisible" for c in active)
        radii = [float(c["radius"]) for c in active]
        if label.endswith("range"):
            assert all(abs(radius - target_range) < 0.01 for radius in radii)
        else:
            assert 0 < min(radii) < target_range - 1
            assert max(radii) > target_range - 0.05
    points = [fields(r[3]) for r in logs.matching("SHORESIDE", "VIEW_POINT")
              if "label=landing_target_point,active=true" in r[3]]
    assert points
    assert all(p["label_color"] == "invisible" for p in points)
    assert abs(float(points[-1]["x"]) - float(pearl["NAV_X"])) < 0.05
    assert abs(float(points[-1]["y"]) - float(pearl["NAV_Y"])) < 0.05
    last_slice = next(c for c in reversed(circles) if c.get("label") == "landing_target_slice")
    down = float(uav["UAV_ALTITUDE_MSL"]) - deck_altitude
    assert abs(float(last_slice["radius"]) - math.sqrt(max(0, target_range**2 - down**2))) < 0.01
    return {"session": uav["LANDING_OWN_SESSION"],
            "touchdown_msl": float(uav["UAV_ALTITUDE_MSL"]),
            "deck_msl": deck_altitude, "horizontal_error_m": offset,
            "freeze": motions[0], "precision_status": [t for t in texts if "PrecLand:" in t]}


def wait_for(logs, process, predicate, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        logs.update()
        if process.poll() is not None:
            raise RuntimeError(f"Launcher exited: {process.returncode}")
        if any(logs.last[n].get("LANDING_OWN_PHASE") == "ABORTED" for n in ("UAV", "PEARL")):
            raise RuntimeError("Recovery aborted; inspect artifacts")
        if predicate():
            return
        time.sleep(0.25)
    raise TimeoutError("Mission transition timed out; inspect artifacts")


def stop_geometry_link(directory):
    """Stop only this test's PEARL pShare after disarm to test stale observations."""
    matches = []
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        try:
            argv = (proc / "cmdline").read_bytes().split(b"\0")
            if (Path(os.fsdecode(argv[0])).name == "pShare" and
                    b"targ_pearl.moos" in argv and (proc / "cwd").resolve() == directory):
                matches.append(int(proc.name))
        except (OSError, IndexError):
            continue
    assert len(matches) == 1, f"Cannot uniquely identify isolated geometry link: {matches}"
    os.kill(matches[0], signal.SIGTERM)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--warp", type=int, default=2)
    parser.add_argument("--deck-altitude", type=float, default=1)
    parser.add_argument("--target-range", type=float, default=12)
    parser.add_argument("--output", type=Path, help="New artifact directory (default: /tmp)")
    args = parser.parse_args()
    if args.warp < 1:
        parser.error("--warp must be positive")
    directory = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix="landing-recovery-"))
    if args.output:
        directory.mkdir(parents=True, exist_ok=False)
    for path in MISSION.iterdir():
        if path.name.startswith("meta_") or path.name in ("launch.sh", "plug_origin_warp.moos", "sitl.parm"):
            shutil.copy2(path, directory / path.name)
    # Hold sockets together while allocating to avoid duplicate ports. Bind TCP
    # and UDP for pShare; no services are launched on the mission's normal ports.
    reservations, ports = [], []
    try:
        for _ in range(9):
            tcp = socket.socket()
            tcp.bind(("127.0.0.1", 0))
            reservations.append(tcp)
            port = tcp.getsockname()[1]
            udp = socket.socket(type=socket.SOCK_DGRAM)
            udp.bind(("127.0.0.1", port))
            reservations.append(udp)
            ports.append(port)
    finally:
        for sock in reservations:
            sock.close()
    command = [str(directory / "launch.sh"), "-ng", "-x", str(args.warp),
               f"--deck_altitude={args.deck_altitude}", f"--target_range={args.target_range}"]
    names = ("shore_mport", "uav_mport", "pearl_mport", "shore_pshare", "uav_pshare",
             "pearl_pshare", "sitl_port", "sitl_target_port", "sitl_diagnostic_port")
    command.extend(f"--{name}={port}" for name, port in zip(names, ports))
    (directory / "run.json").write_text(json.dumps({"command": command, "ports": dict(zip(names, ports))}, indent=2))
    logs = Logs(directory)
    stop, messages = threading.Event(), []
    capture = threading.Thread(target=capture_status, args=(ports[-1], stop, messages), daemon=True)
    result: dict[str, object] = {"result": "FAIL"}
    process = None
    print("ARTIFACTS:", directory, flush=True)
    try:
        with (directory / "launcher.log").open("w") as output:
            process = subprocess.Popen(command, cwd=directory, stdout=output,
                                       stderr=subprocess.STDOUT, start_new_session=True)
        capture.start()
        wait_for(logs, process, lambda: logs.last["UAV"].get("UAV_AUTO_START") == "lane", 150)
        subprocess.run(["uPokeDB", str(directory / "targ_shoreside.moos"), "--quiet", "RECOVER_UAV:=true"],
                       check=True, stdout=subprocess.DEVNULL, timeout=12)
        wait_for(logs, process, lambda: all(logs.last[n].get("LANDING_OWN_PHASE") == "COMPLETE"
                                          for n in ("UAV", "PEARL")), 240)
        time.sleep(3)
        logs.update()
        result.update(validate(logs, messages, args.deck_altitude, args.target_range))
        result["touchdown_state"] = dict(logs.last["UAV"])
        lost_at = logs.rows["UAV"][-1][0]
        stop_geometry_link(directory)
        wait_for(logs, process, lambda: logs.last["UAV"].get("LANDMARK_EMULATOR_ACTIVE") == "0.00000"
                 and logs.last["UAV"].get("UAV_LANDING_TARGET_AVAILABLE") == "0.00000", 15)
        time.sleep(1)
        logs.update()
        for variable, label in (("VIEW_CIRCLE", "landing_target_range"),
                                ("VIEW_CIRCLE", "landing_target_slice"),
                                ("VIEW_POINT", "landing_target_point")):
            assert any(r[0] > lost_at and f"label={label},active=true" in r[3]
                       for r in logs.matching("SHORESIDE", variable)), label
        polygons = [r for r in logs.matching("SHORESIDE", "VIEW_POLYGON")
                    if r[0] > lost_at and "label=received_landing_target" in r[3]]
        assert polygons and "active=false" in polygons[-1][3]
        assert float(logs.last["SHORESIDE"]["LANDING_TARGET_VIEW_ACTIVE"]) == 0
        assert logs.last["UAV"]["UAV_LANDED_STATE"] == "ON_GROUND"
        assert float(logs.last["UAV"]["UAV_IS_ARMED"]) == 0
        result["target_loss_after_disarm"] = (
            "stream stopped, bridge unavailable, received footprint hidden; simulator visuals retained")
        result["result"] = "PASS"
        print(json.dumps({key: value for key, value in result.items() if key != "touchdown_state"}, indent=2), flush=True)
    except Exception as error:
        result["error"] = repr(error)
        raise
    finally:
        stop.set()
        if capture.is_alive():
            capture.join(timeout=3)
        (directory / "mavlink_status.json").write_text(json.dumps(messages, indent=2))
        result["last"] = logs.last
        (directory / "result.json").write_text(json.dumps(result, indent=2))
        if process and process.poll() is None:
            process.send_signal(signal.SIGINT)  # launcher's scoped trap stops only its children/project
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
        logs.close()
        print(f"{result['result']}; artifacts: {directory}", flush=True)


if __name__ == "__main__":
    main()
