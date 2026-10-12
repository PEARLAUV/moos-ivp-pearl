#!/usr/bin/env python3
"""Assemble the latest local mission artifacts into a checksummed directory.

The collector is deliberately local-node only.  On the drone it obtains one
latest DataFlash log and a complete parameter snapshot directly from a stopped,
disarmed, grounded Pixhawk using read-only MAVLink requests.  It never arms,
changes mode, sets parameters, deletes logs, or sends a flight command.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import ardupilot_params as mav

# MAVLink common.xml CRC extras used only for read-only artifact retrieval.
mav.EXTRAS.update({21: 159, 117: 128, 118: 56, 119: 116, 120: 134, 122: 203})

NODE_LOG_PREFIX = {
    "shoreside": "LOG_SHORESIDE_",
    "pearl-real": "LOG_pearl_",
    "pearl-grass": "LOG_pearl_",
    "drone": "LOG_uav_",
}
INTEGER_TYPES = frozenset(mav.INTEGER_LIMITS)


class CollectionError(RuntimeError):
    """A required artifact or safety condition was not satisfied."""


@dataclass(frozen=True)
class LogEntry:
    id: int
    size: int
    time_utc: int
    num_logs: int
    last_log_num: int


class ReadOnlyPixhawk:
    """Minimal read-only MAVLink client for parameters and DataFlash logs."""

    def __init__(self, link, system: int = 1, component: int = 1,
                 response_timeout: float = 5.0, state_freshness: float = 2.0):
        self.link = link
        self.system = system
        self.component = component
        self.response_timeout = response_timeout
        self.state_freshness = state_freshness
        self.reader = mav.Reader()
        self.sequence = 0
        self.heartbeat = None
        self.ground = None
        self.unsafe_seen = False
        self.admitted = False
        self.last_ground_request = 0.0
        self.parameters: dict[int, tuple[str, float, int, int]] = {}
        self.parameter_count = None
        self.log_entries: dict[int, LogEntry] = {}
        self.log_blocks: list[tuple[int, int, bytes]] = []

    def send(self, message_id: int, payload: bytes) -> None:
        self.link.send(mav.encode(message_id, payload, self.sequence))
        self.sequence += 1

    def pump(self, timeout: float = 0.05) -> None:
        data = self.link.recv(timeout)
        for message_id, system, component, payload in self.reader.feed(data):
            if (system, component) != (self.system, self.component):
                continue
            now = time.monotonic()
            if message_id == 0 and len(payload) >= 9 and payload[5] == 3:
                self.heartbeat = (bool(payload[6] & 128), now)
                if self.link.protocol == "udp" and self.link.peer is None:
                    self.link.peer = self.link.last_sender
                if self.admitted and self.heartbeat[0]:
                    self.unsafe_seen = True
            elif message_id == 245 and len(payload) >= 2:
                self.ground = (payload[1] == 1, now)
                if self.admitted and not self.ground[0]:
                    self.unsafe_seen = True
            elif message_id == 22 and len(payload) >= 24:
                value, count, index, name, parameter_type = struct.unpack(
                    "<fHH16sB", payload.ljust(25, bytes(1))[:25])
                parameter_name = name.split(bytes(1), 1)[0].decode("ascii", errors="strict")
                if self.parameter_count is None:
                    self.parameter_count = count
                elif self.parameter_count != count:
                    raise CollectionError("Pixhawk parameter count changed during download")
                if index < count:
                    previous = self.parameters.get(index)
                    current = (parameter_name, value, parameter_type, count)
                    if previous is not None and previous != current:
                        raise CollectionError(
                            f"Pixhawk parameter index {index} changed during download")
                    self.parameters[index] = current
            elif message_id == 118:
                padded = payload.ljust(14, bytes(1))
                time_utc, size, log_id, count, last = struct.unpack("<IIHHH", padded[:14])
                self.log_entries[log_id] = LogEntry(log_id, size, time_utc, count, last)
            elif message_id == 120 and len(payload) >= 7:
                # MAVLink 2 may trim trailing zero bytes from the fixed data[90]
                # payload. Generated decoders restore them; do the same here.
                padded = payload.ljust(97, bytes(1))
                offset, log_id, count = struct.unpack("<IHB", padded[:7])
                if count > 90:
                    raise CollectionError("Malformed MAVLink LOG_DATA response")
                self.log_blocks.append((log_id, offset, padded[7:7 + count]))
        self._request_ground_state()

    def _request_ground_state(self) -> None:
        now = time.monotonic()
        if (self.heartbeat is not None and now - self.last_ground_request >= 0.4
                and (self.link.protocol != "udp" or self.link.peer is not None)):
            # MAV_CMD_REQUEST_MESSAGE(EXTENDED_SYS_STATE). This requests telemetry;
            # it does not change vehicle state.
            self.send(76, struct.pack("<7fHBBB", 245, 0, 0, 0, 0, 0, 0,
                                      512, self.system, self.component, 0))
            self.last_ground_request = now

    def guard(self) -> None:
        now = time.monotonic()
        if self.unsafe_seen:
            raise CollectionError("Pixhawk armed or became airborne during artifact retrieval")
        if self.heartbeat is None or now - self.heartbeat[1] > self.state_freshness:
            raise CollectionError("Missing/stale native ArduPilot heartbeat")
        if self.heartbeat[0]:
            raise CollectionError("Pixhawk is armed; artifact retrieval refused")
        if self.ground is None or now - self.ground[1] > self.state_freshness or not self.ground[0]:
            raise CollectionError("Missing/stale native Pixhawk ON_GROUND evidence")

    def admit(self) -> None:
        deadline = time.monotonic() + self.response_timeout
        while time.monotonic() < deadline:
            self.pump()
            try:
                self.guard()
            except CollectionError:
                continue
            self.admitted = True
            return
        self.guard()
        raise CollectionError("Timed out admitting grounded, disarmed Pixhawk")

    def download_parameters(self, destination: Path, timeout: float = 180.0) -> int:
        self.guard()
        self.parameters.clear()
        self.parameter_count = None
        self.send(21, struct.pack("<BB", self.system, self.component))
        started = last_request = time.monotonic()
        while time.monotonic() - started < timeout:
            self.pump(0.1)
            self.guard()
            count = self.parameter_count
            if count is not None and len(self.parameters) == count:
                break
            now = time.monotonic()
            if now - last_request >= 5:
                if count is None:
                    self.send(21, struct.pack("<BB", self.system, self.component))
                else:
                    missing = [index for index in range(count) if index not in self.parameters]
                    for index in missing[:20]:
                        self.send(20, struct.pack("<hBB16s", index, self.system,
                                                  self.component, bytes(16)))
                last_request = now
        count = self.parameter_count
        if count is None or len(self.parameters) != count:
            missing_count = "unknown" if count is None else str(count - len(self.parameters))
            raise CollectionError(f"Incomplete Pixhawk parameter download; missing {missing_count}")
        names = [row[0] for row in self.parameters.values()]
        if len(set(names)) != len(names):
            raise CollectionError("Duplicate names in Pixhawk parameter download")
        lines = [
            "# Onboard parameters collected by make logs",
            "# Stack: ArduPilot",
            f"# Collected-UTC: {utc_now()}",
            "# Vehicle-Id Component-Id Name Value Type",
        ]
        for index in range(count):
            name, value, parameter_type, _ = self.parameters[index]
            if not math.isfinite(value):
                raise CollectionError(f"Nonfinite Pixhawk parameter {name}")
            rendered = format(value, ".0f") if parameter_type in INTEGER_TYPES else format(value, ".9g")
            lines.append(f"{self.system}\t{self.component}\t{name}\t{rendered}\t{parameter_type}")
        destination.write_text("\n".join(lines) + "\n")
        return count

    def get_log_entries(self) -> list[LogEntry]:
        self.guard()
        self.log_entries.clear()
        deadline = time.monotonic() + max(10.0, self.response_timeout)
        next_request = 0.0
        expected = None
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_request:
                self.send(117, struct.pack("<HHBB", 0, 0xFFFF,
                                           self.system, self.component))
                next_request = now + 2
            self.pump(0.1)
            self.guard()
            if self.log_entries:
                counts = {entry.num_logs for entry in self.log_entries.values()}
                if len(counts) != 1:
                    raise CollectionError("Inconsistent Pixhawk DataFlash log count")
                expected = counts.pop()
                if len(self.log_entries) >= expected:
                    break
        if not self.log_entries:
            raise CollectionError("Pixhawk reported no DataFlash logs")
        if expected is not None and len(self.log_entries) < expected:
            raise CollectionError(
                f"Incomplete Pixhawk log list: {len(self.log_entries)}/{expected}")
        return sorted(self.log_entries.values(), key=lambda entry: entry.id)

    def download_log(self, entry: LogEntry, destination: Path) -> None:
        self.guard()
        if entry.size <= 0:
            raise CollectionError(f"Pixhawk DataFlash log {entry.id} is empty")
        block_size = 90
        blocks = (entry.size + block_size - 1) // block_size
        received = bytearray(blocks)
        complete = 0
        attempts_without_progress = 0
        last_report = -1
        with destination.open("xb+") as output:
            output.truncate(entry.size)
            while complete < blocks:
                try:
                    first = received.index(0)
                except ValueError:
                    break
                offset = first * block_size
                request_bytes = min(block_size * 100, entry.size - offset)
                self.log_blocks.clear()
                self.send(119, struct.pack("<IIHBB", offset, request_bytes, entry.id,
                                           self.system, self.component))
                deadline = time.monotonic() + self.response_timeout
                before = complete
                while time.monotonic() < deadline:
                    self.pump(0.1)
                    self.guard()
                    queued, self.log_blocks = self.log_blocks, []
                    for log_id, packet_offset, data in queued:
                        if log_id != entry.id or not data:
                            continue
                        if packet_offset >= entry.size or packet_offset + len(data) > entry.size:
                            raise CollectionError("Pixhawk LOG_DATA offset outside advertised log")
                        if packet_offset % block_size:
                            raise CollectionError("Pixhawk LOG_DATA offset is not 90-byte aligned")
                        index = packet_offset // block_size
                        expected_size = min(block_size, entry.size - packet_offset)
                        if len(data) != expected_size:
                            continue
                        output.seek(packet_offset)
                        output.write(data)
                        if not received[index]:
                            received[index] = 1
                            complete += 1
                    if complete >= blocks:
                        break
                    # A full requested window arrived; start the next request immediately.
                    window_last = min(blocks, first + 100)
                    if all(received[first:window_last]):
                        break
                if complete == before:
                    attempts_without_progress += 1
                    if attempts_without_progress >= 10:
                        raise CollectionError(
                            f"Pixhawk DataFlash log {entry.id} download stalled at offset {offset}")
                else:
                    attempts_without_progress = 0
                percent = complete * 100 // blocks
                if percent // 10 != last_report // 10:
                    print(f"DataFlash log {entry.id}: {percent}%", file=sys.stderr)
                    last_report = percent
            output.flush()
            os.fsync(output.fileno())
        self.send(122, struct.pack("<BB", self.system, self.component))


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def local_addresses() -> set[str]:
    addresses: set[str] = set()
    try:
        result = subprocess.run(["ip", "-j", "address", "show"], capture_output=True,
                                text=True, check=True, timeout=5)
        for interface in json.loads(result.stdout):
            for row in interface.get("addr_info", []):
                if row.get("family") == "inet" and row.get("local"):
                    addresses.add(row["local"])
    except (OSError, subprocess.SubprocessError, json.JSONDecodeError):
        try:
            addresses.update(socket.gethostbyname_ex(socket.gethostname())[2])
        except OSError:
            pass
    return addresses


def determine_node(requested: str, shore_ip: str, drone_ip: str,
                   pearl_real_ip: str, pearl_grass_ip: str) -> str:
    if requested != "auto":
        return requested
    addresses = local_addresses()
    matches = []
    for node, address in (("shoreside", shore_ip), ("drone", drone_ip),
                          ("pearl-real", pearl_real_ip), ("pearl-grass", pearl_grass_ip)):
        if address in addresses:
            matches.append(node)
    if len(matches) != 1:
        raise CollectionError(
            "Cannot uniquely identify local node from configured IPs; "
            f"matches={matches or 'none'}, addresses={sorted(addresses)}. Set LOG_NODE explicitly.")
    return matches[0]


def latest_log_directory(mission: Path, node: str, explicit: str | None) -> Path:
    if explicit:
        candidate = Path(explicit)
        if not candidate.is_absolute():
            candidate = mission / candidate
        candidates = [candidate]
    else:
        prefix = NODE_LOG_PREFIX[node]
        candidates = [path for path in mission.glob(prefix + "*") if path.is_dir()]
    if not candidates:
        raise CollectionError(f"No local {NODE_LOG_PREFIX[node]}* MOOS log directory found")
    selected = max(candidates, key=lambda path: path.stat().st_mtime).resolve()
    try:
        selected.relative_to(mission.resolve())
    except ValueError as exc:
        raise CollectionError("MOOS log directory must be inside the mission directory") from exc
    if not list(selected.glob("*.alog")):
        raise CollectionError(f"Selected MOOS log has no .alog file: {selected}")
    return selected


def open_files_below(directory: Path) -> list[str]:
    opened = []
    proc = Path("/proc")
    if not proc.is_dir():
        return opened
    for process in proc.iterdir():
        if not process.name.isdigit():
            continue
        fd_dir = process / "fd"
        try:
            handles = list(fd_dir.iterdir())
        except OSError:
            continue
        for handle in handles:
            try:
                target = handle.resolve(strict=True)
                target.relative_to(directory)
            except (OSError, ValueError):
                continue
            opened.append(f"pid={process.name}:{target}")
    return opened


def assert_drone_apps_stopped() -> None:
    proc = Path("/proc")
    if not proc.is_dir():
        return
    found = []
    for process in proc.iterdir():
        if not process.name.isdigit():
            continue
        try:
            command = (process / "cmdline").read_bytes().replace(bytes(1), b" ").decode(
                errors="replace")
        except OSError:
            continue
        if "pArduBridge" in command:
            found.append(f"pid={process.name} {command.strip()}")
    if found:
        raise CollectionError(
            "Drone mission must be stopped before Pixhawk retrieval; pArduBridge is running: "
            + "; ".join(found))


def alog_last_values(log_directory: Path, variables: set[str]) -> dict[str, str]:
    values: dict[str, str] = {}
    alogs = list(log_directory.glob("*.alog"))
    if len(alogs) != 1:
        raise CollectionError(f"Expected exactly one .alog in {log_directory}")
    with alogs[0].open(errors="replace") as source:
        for line in source:
            if not line or line.startswith("%"):
                continue
            fields = line.split(None, 3)
            if len(fields) == 4 and fields[1] in variables:
                values[fields[1]] = fields[3].strip()
    return values


def mediamtx_path_names(api_url: str) -> set[str]:
    try:
        with urllib.request.urlopen(f"{api_url.rstrip('/')}/list", timeout=5) as response:
            payload = json.load(response)
    except (OSError, ValueError, urllib.error.URLError) as exc:
        raise CollectionError(f"Unable to query MediaMTX recording paths: {exc}") from exc
    return {str(item.get("name", "")) for item in payload.get("items", [])}


def stop_recording_after_mission(mission: Path, api_url: str, evidence_path: Path) -> Path:
    running = []
    for name in ("MOOSDB", "pArduBridge", "pVideoRecorder"):
        result = subprocess.run(["pgrep", "-x", name], capture_output=True, text=True,
                                check=False)
        if result.returncode == 0:
            running.append(name)
    if running:
        raise CollectionError(
            "Refusing to stop recording while a local UAV mission is running: "
            + ", ".join(running))

    log_directory = latest_log_directory(mission, "drone", None)
    values = alog_last_values(
        log_directory, {"VIDEO_RECORD_STATE", "VIDEO_RECORD_DIRECTORY", "VIDEO_RECORD_ID"})
    recorded = values.get("VIDEO_RECORD_DIRECTORY")
    if not recorded:
        raise CollectionError("Latest drone MOOS log has no VIDEO_RECORD_DIRECTORY")

    stopped = []
    names = mediamtx_path_names(api_url)
    for name in ("pearl-recorder-fpv", "pearl-recorder-nadir"):
        if name not in names:
            continue
        request = urllib.request.Request(
            f"{api_url.rstrip('/')}/delete/{name}", method="DELETE")
        try:
            with urllib.request.urlopen(request, timeout=10) as response:
                if response.status != 200:
                    raise CollectionError(
                        f"MediaMTX rejected deletion of {name}: HTTP {response.status}")
        except (OSError, urllib.error.URLError) as exc:
            raise CollectionError(f"Unable to stop {name}: {exc}") from exc
        stopped.append(name)

    remaining = sorted(name for name in mediamtx_path_names(api_url)
                       if name.startswith("pearl-recorder-"))
    if remaining:
        raise CollectionError("Recording paths remain active: " + ", ".join(remaining))
    evidence = {
        "schema_version": 1,
        "created_utc": utc_now(),
        "host": socket.gethostname(),
        "log_directory": log_directory.name,
        "recording_directory": recorded,
        "recording_id": values.get("VIDEO_RECORD_ID"),
        "last_logged_state": values.get("VIDEO_RECORD_STATE"),
        "api_url": api_url,
        "stopped_paths": stopped,
        "remaining_recorder_paths": remaining,
    }
    evidence_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = evidence_path.with_suffix(evidence_path.suffix + ".tmp")
    temporary.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n")
    temporary.replace(evidence_path)
    return evidence_path


def copy_drone_video(
        log_directory: Path, video_root: Path, destination: Path,
        evidence_path: Path | None = None,
        api_url: str = "http://127.0.0.1:9997/v3/config/paths") -> dict:
    variables = {"VIDEO_RECORD_STATE", "VIDEO_RECORD_DIRECTORY", "VIDEO_RECORD_ID"}
    values = alog_last_values(log_directory, variables)
    recorded = values.get("VIDEO_RECORD_DIRECTORY")
    if not recorded:
        raise CollectionError("Latest drone MOOS log has no VIDEO_RECORD_DIRECTORY")
    external_evidence = None
    if values.get("VIDEO_RECORD_STATE") != "STOPPED":
        if evidence_path is None:
            raise CollectionError(
                "Latest drone recording is not confirmed STOPPED; run make stop-recording "
                "after the mission exits, then run make logs")
        try:
            external_evidence = json.loads(evidence_path.read_text())
        except (OSError, ValueError) as exc:
            raise CollectionError(
                "Latest drone recording is not confirmed STOPPED; run make stop-recording "
                "after the mission exits, then run make logs") from exc
        if (external_evidence.get("schema_version") != 1 or
                external_evidence.get("log_directory") != log_directory.name or
                external_evidence.get("recording_directory") != recorded or
                external_evidence.get("remaining_recorder_paths") != []):
            raise CollectionError("External video-stop evidence does not match the latest log")
        remaining = sorted(name for name in mediamtx_path_names(api_url)
                           if name.startswith("pearl-recorder-"))
        if remaining:
            raise CollectionError("Recording paths remain active: " + ", ".join(remaining))
        shutil.copy2(evidence_path, destination / "EXTERNAL_STOP_EVIDENCE.json")
    source = Path(recorded).resolve()
    root = video_root.resolve()
    try:
        source.relative_to(root)
    except ValueError as exc:
        raise CollectionError(f"Recorded video directory is outside configured root {root}") from exc
    for camera in ("fpv", "nadir"):
        camera_source = source / camera
        if not camera_source.is_dir() or not any(path.is_file() for path in camera_source.rglob("*")):
            raise CollectionError(f"Required drone video stream is missing or empty: {camera_source}")
        shutil.copytree(camera_source, destination / camera, copy_function=shutil.copy2)
    result = {"recording_id": values.get("VIDEO_RECORD_ID"), "source": str(source),
              "streams": ["fpv", "nadir"]}
    if external_evidence is not None:
        result["stop_evidence"] = "video/EXTERNAL_STOP_EVIDENCE.json"
    return result


def collect_ardupilot(destination: Path, protocol: str, endpoint: str,
                      system: int, component: int, timeout: float) -> dict:
    assert_drone_apps_stopped()
    link = None
    try:
        link = mav.Link(protocol, endpoint)
        client = ReadOnlyPixhawk(link, system, component, timeout)
        client.admit()
        parameters = destination / "parameters"
        dataflash = destination / "dataflash"
        parameters.mkdir(parents=True)
        dataflash.mkdir(parents=True)
        parameter_path = parameters / f"{dt.datetime.now(dt.timezone.utc):%Y%m%dT%H%M%SZ}-postflight.params"
        parameter_count = client.download_parameters(parameter_path)
        entries = client.get_log_entries()
        latest_ids = {entry.last_log_num for entry in entries}
        if len(latest_ids) != 1:
            raise CollectionError("Inconsistent Pixhawk latest DataFlash log ID")
        latest_id = latest_ids.pop()
        try:
            latest = next(entry for entry in entries if entry.id == latest_id)
        except StopIteration as exc:
            raise CollectionError(
                f"Pixhawk latest DataFlash log {latest_id} was absent from its log list") from exc
        stamp = (dt.datetime.fromtimestamp(latest.time_utc, dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
                 if latest.time_utc else "unknown-time")
        log_path = dataflash / f"log_{latest.id}_{stamp}.bin"
        client.download_log(latest, log_path)
        client.guard()
        return {"parameter_count": parameter_count, "parameter_file": str(parameter_path.name),
                "dataflash_id": latest.id, "dataflash_size": latest.size,
                "dataflash_time_utc": latest.time_utc, "dataflash_file": str(log_path.name),
                "available_log_ids": [entry.id for entry in entries]}
    except (OSError, UnicodeError, mav.UploadError) as exc:
        raise CollectionError(str(exc)) from exc
    finally:
        if link is not None:
            link.close()


def file_checksums(root: Path) -> list[tuple[str, str]]:
    rows = []
    for path in sorted(item for item in root.rglob("*") if item.is_file()
                       and item.name != "SHA256SUMS"):
        digest = hashlib.sha256()
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
        rows.append((digest.hexdigest(), path.relative_to(root).as_posix()))
    return rows


def finalize(stage: Path, output_root: Path, node: str, log_name: str,
             manifest: dict[str, Any], complete: bool, error: str | None = None) -> Path:
    manifest["complete"] = complete
    if error:
        manifest["error"] = error
        (stage / "COLLECTION_ERROR.txt").write_text(error + "\n")
    (stage / "MANIFEST.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    sums = file_checksums(stage)
    (stage / "SHA256SUMS").write_text("".join(f"{digest}  {name}\n" for digest, name in sums))
    base = f"{dt.datetime.now(dt.timezone.utc):%Y%m%dT%H%M%SZ}_{node}_{log_name}"
    final = output_root / base
    suffix = 2
    while final.exists():
        final = output_root / f"{base}_{suffix}"
        suffix += 1
    stage.rename(final)
    return final


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mission-dir", type=Path, required=True)
    parser.add_argument("--node", choices=("auto", *NODE_LOG_PREFIX), default="auto")
    parser.add_argument("--log-directory")
    parser.add_argument("--output-root", type=Path)
    parser.add_argument("--shore-ip", default="100.88.214.50")
    parser.add_argument("--drone-ip", default="100.112.119.114")
    parser.add_argument("--pearl-real-ip", default="100.113.99.58")
    parser.add_argument("--pearl-grass-ip", default="100.71.137.47")
    parser.add_argument("--video-root", type=Path,
                        default=Path("/var/lib/landmark-recorder/mission-video"))
    parser.add_argument("--ap-protocol", choices=("serial", "tcp", "udp"), default="serial")
    parser.add_argument("--ap-endpoint", default=(
        "serial/by-id/usb-ArduPilot_Pixhawk6X_200023000E51333531343838-if00:115200"))
    parser.add_argument("--ap-system-id", type=int, default=1)
    parser.add_argument("--ap-component-id", type=int, default=1)
    parser.add_argument("--ap-timeout", type=float, default=5.0)
    parser.add_argument("--video-api-url",
                        default="http://127.0.0.1:9997/v3/config/paths")
    parser.add_argument("--video-stop-evidence", type=Path)
    parser.add_argument("--stop-recording-only", action="store_true")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    mission = args.mission_dir.resolve()
    output_root: Path = (args.output_root or mission / "field_logs").resolve()
    stage: Path | None = None
    final: Path | None = None
    manifest: dict[str, Any] = {
        "collector": "collect_mission_logs.py", "created_utc": utc_now()
    }
    try:
        if not mission.is_dir():
            raise CollectionError(f"Mission directory does not exist: {mission}")
        evidence_path = (args.video_stop_evidence or
                         mission / ".video-stop-evidence.json").resolve()
        if args.stop_recording_only:
            evidence = stop_recording_after_mission(
                mission, args.video_api_url, evidence_path)
            print(evidence)
            return 0
        if not (1 <= args.ap_system_id <= 255 and 1 <= args.ap_component_id <= 255):
            raise CollectionError("Pixhawk system/component IDs must be 1..255")
        if not math.isfinite(args.ap_timeout) or not 0 < args.ap_timeout <= 30:
            raise CollectionError("Pixhawk response timeout must be finite and in (0,30] seconds")
        node = determine_node(args.node, args.shore_ip, args.drone_ip,
                              args.pearl_real_ip, args.pearl_grass_ip)
        log_directory = latest_log_directory(mission, node, args.log_directory)
        opened = open_files_below(log_directory)
        if opened:
            raise CollectionError(
                "Latest MOOS log is still open; stop the local mission before make logs: "
                + "; ".join(opened[:5]))
        output_root.mkdir(parents=True, exist_ok=True)
        stage = Path(tempfile.mkdtemp(prefix=".collecting-", dir=output_root))
        manifest.update({"node": node, "host": socket.gethostname(),
                         "moos_log_source": str(log_directory)})
        shutil.copytree(log_directory, stage / "moos" / log_directory.name,
                        copy_function=shutil.copy2)
        if node == "drone":
            assert_drone_apps_stopped()
            (stage / "video").mkdir()
            manifest["video"] = copy_drone_video(
                log_directory, args.video_root, stage / "video", evidence_path,
                args.video_api_url)
            manifest["ardupilot"] = collect_ardupilot(
                stage / "ardupilot", args.ap_protocol, args.ap_endpoint,
                args.ap_system_id, args.ap_component_id, args.ap_timeout)
        elif node == "pearl-real":
            gap = (
                "PEARL Jetson video was not collected. The deployed pearl-d455 MediaMTX "
                "configuration streams continuously but explicitly has recording disabled, and "
                "the mission publishes no task-scoped Jetson recording path. Add recording before "
                "this artifact requirement can be satisfied. MOOS logs in this directory are valid."
            )
            (stage / "jetson-video").mkdir()
            (stage / "jetson-video" / "UNAVAILABLE.txt").write_text(gap + "\n")
            final = finalize(stage, output_root, node, log_directory.name, manifest, False, gap)
            stage = None
            print(f"INCOMPLETE artifact directory: {final}", file=sys.stderr)
            return 2
        final = finalize(stage, output_root, node, log_directory.name, manifest, True)
        stage = None
        print(final)
        return 0
    except (CollectionError, OSError, ValueError, KeyboardInterrupt) as exc:
        message = str(exc) or exc.__class__.__name__
        if stage is not None and stage.exists():
            try:
                log_name = Path(str(manifest.get("moos_log_source", "unknown-log"))).name
                failed_node = str(manifest.get("node", args.node))
                failed_stage = stage
                final = finalize(failed_stage, output_root, failed_node, log_name,
                                 manifest, False, message)
                stage = None
                print(f"INCOMPLETE artifact directory: {final}", file=sys.stderr)
            except OSError:
                if stage is not None:
                    shutil.rmtree(stage, ignore_errors=True)
        print(f"Log collection failed: {message}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
