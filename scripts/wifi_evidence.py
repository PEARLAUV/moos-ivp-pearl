"""Read-only UAV-local Wi-Fi evidence collection; no scans or network changes.

Journal exports are historical observations (when retained). iw/proc/sysfs reads
are collection-time snapshots, never substitutes for flight-period RSSI history.
This module is used by collect_mission_logs; it is not a daemon or MOOS app.
"""
from __future__ import annotations

import datetime as dt
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
from typing import Any


RSSI = re.compile(r"\b(?:rssi|signal)\s*[:=]\s*(-\d+(?:\.\d+)?)", re.I)
LOG_PATTERNS = ("syslog*", "kern.log*", "messages*", "daemon.log*",
                "wpa_supplicant*", "NetworkManager*")


def mission_window(log_directory: Path, start: float, padding: float = 120) -> dict:
    """Use the MOOS host clock, not filename wall time or flight-controller GPS."""
    if not math.isfinite(start) or start <= 0:
        raise ValueError("Invalid MOOS LOGSTART")
    alogs = list(log_directory.glob("*.alog"))
    if len(alogs) != 1:
        raise ValueError("Expected exactly one alog for journal window")
    elapsed = 0.0
    with alogs[0].open(errors="replace") as stream:
        for line in stream:
            if line.startswith("%"):
                continue
            fields = line.split(None, 3)
            if len(fields) != 4:
                continue
            try:
                value = float(fields[0])
            except ValueError:
                continue
            if math.isfinite(value) and value >= 0:
                elapsed = max(elapsed, value)
    if not math.isfinite(start + elapsed + padding):
        raise ValueError("Non-finite journal window")
    return {"clock": "MOOS LOGSTART plus alog elapsed seconds; host clock, not GPS",
            "start_unix": start, "end_unix": start + elapsed,
            "padding_seconds": padding, "since_unix": max(0, start - padding),
            "until_unix": start + elapsed + padding}


def capture_command(destination: Path, name: str, argv: list[str],
                    timeout: float = 20) -> dict[str, Any]:
    row: dict[str, Any] = {"argv": argv, "started_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
                           "stdout": f"{name}.stdout", "stderr": f"{name}.stderr"}
    with (destination / row["stdout"]).open("wb") as out, \
            (destination / row["stderr"]).open("wb") as err:
        try:
            result = subprocess.run(argv, stdout=out, stderr=err, timeout=timeout,
                                    check=False, env={**os.environ, "LC_ALL": "C"})
            row["exit_code"] = result.returncode
        except (OSError, subprocess.TimeoutExpired) as exc:
            row["exit_code"] = None
            row["error"] = str(exc)
    row["finished_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
    row["stdout_bytes"] = (destination / row["stdout"]).stat().st_size
    row["stderr_bytes"] = (destination / row["stderr"]).stat().st_size
    return row


def journal_candidates(path: Path) -> tuple[int, list[dict]]:
    """Keep source text and clock provenance; ambiguous units remain unverified."""
    count = 0
    candidates = []
    with path.open(errors="replace") as stream:
        for line_number, line in enumerate(stream, 1):
            try:
                row = json.loads(line)
            except ValueError:
                continue
            if not isinstance(row, dict) or "__REALTIME_TIMESTAMP" not in row:
                continue
            count += 1
            message = row.get("MESSAGE", "")
            if not isinstance(message, str):
                continue
            for match in RSSI.finditer(message):
                iface = re.search(r"\b(wlan[0-9]+|wl[a-z0-9]+)\b", message)
                candidates.append({
                    "source": path.name, "line": line_number, "message": message,
                    "realtime_microseconds": row.get("__REALTIME_TIMESTAMP"),
                    "monotonic_microseconds": row.get("__MONOTONIC_TIMESTAMP"),
                    "boot_id": row.get("_BOOT_ID"),
                    "interface": iface.group(1) if iface else None,
                    "value_text": match.group(1),
                    "units": "dBm" if "dbm" in message.lower() or
                    "CTRL-EVENT-SIGNAL-CHANGE" in message else "unverified",
                })
    return count, candidates


def collect_wifi_evidence(log_directory: Path, destination: Path, start: float,
                          *, sys_net: Path = Path("/sys/class/net"),
                          proc_root: Path = Path("/proc"),
                          log_root: Path = Path("/var/log")) -> dict[str, Any]:
    destination.mkdir(parents=True, exist_ok=False)
    window = mission_window(log_directory, start)
    metadata: dict[str, Any] = {
        "schema_version": 1, "source": "UAV companion board only",
        "window": window, "commands": {}, "snapshots": [], "rotated_logs": [],
        "warnings": [], "snapshot_scope": "collection-time only, not flight history",
        "history_scope": "retained host logs; absence is not proof of a healthy link",
    }
    bounds = ["--since", f"@{window['since_unix']:.6f}",
              "--until", f"@{window['until_unix']:.6f}"]
    commands = {
        "journal-boots": ["journalctl", "--list-boots", "--utc", "--no-pager"],
        "journal-network": ["journalctl", "--quiet", "--no-pager", "--output=json", *bounds,
                            "-u", "NetworkManager.service", "-u", "wpa_supplicant.service"],
        # A field match, rather than -k, avoids implicitly restricting to this boot.
        "journal-kernel": ["journalctl", "--quiet", "--no-pager", "--output=json", *bounds,
                           "_TRANSPORT=kernel"],
        "journal-wifi-samples": ["journalctl", "--quiet", "--no-pager", "--output=json", *bounds,
                                 "SYSLOG_IDENTIFIER=pHostInfo-wifi"],
    }
    iw = shutil.which("iw") or "/usr/sbin/iw"
    commands["current-iw-dev"] = [iw, "dev"]
    for interface in sorted(sys_net.glob("*")):
        if (interface / "wireless").is_dir() and re.fullmatch(r"[A-Za-z0-9_.-]{1,15}", interface.name):
            for query in ("link", "info", "station"):
                argv = [iw, "dev", interface.name, query]
                if query == "station":
                    argv.append("dump")
                commands[f"current-{interface.name}-{query}"] = argv
    for name, argv in commands.items():
        row = capture_command(destination, name, argv)
        metadata["commands"][name] = row
        if row["exit_code"] != 0 or row["stderr_bytes"]:
            metadata["warnings"].append(f"{name}: inspect recorded status/stderr; coverage may be limited")
    for relative in ("net/wireless", "uptime", "sys/kernel/random/boot_id"):
        source = proc_root / relative
        row = {"source": str(source), "destination": "current-proc-" + relative.replace("/", "-")}
        try:
            (destination / row["destination"]).write_bytes(source.read_bytes())
        except OSError as exc:
            row["error"] = str(exc)
            metadata["warnings"].append(f"Unavailable current snapshot: {source}")
        metadata["snapshots"].append(row)
    # Keep rotated text/gzip sources verbatim, not a guessed journal substitute.
    # Copy each file's observed length, recording changes and short reads. Never
    # sudo or alter journald retention, NetworkManager logging, or radio settings.
    rotated = destination / "rotated"
    rotated.mkdir()
    files = sorted({path for pattern in LOG_PATTERNS for path in log_root.glob(pattern)})
    for source in files:
        row = {"source": str(source), "destination": f"rotated/{source.name}"}
        try:
            if source.is_symlink() or not source.is_file():
                raise OSError("not a regular non-symlink file")
            with source.open("rb") as stream:
                before = os.fstat(stream.fileno())
                limit = before.st_size
                with (rotated / source.name).open("xb") as target:
                    copied = 0
                    while copied < limit:
                        data = stream.read(min(1024 * 1024, limit - copied))
                        if not data:
                            break
                        target.write(data)
                        copied += len(data)
                after = os.fstat(stream.fileno())
            row.update(source_size=before.st_size, copied_bytes=copied,
                       source_mtime_ns=before.st_mtime_ns,
                       source_inode=before.st_ino,
                       truncated=copied != before.st_size,
                       source_changed=before.st_mtime_ns != after.st_mtime_ns or before.st_size != after.st_size)
            if row["truncated"] or row["source_changed"]:
                metadata["warnings"].append(f"Partial/changing rotated source: {source}")
        except OSError as exc:
            row["error"] = str(exc)
            metadata["warnings"].append(f"Unavailable rotated source: {source}")
        metadata["rotated_logs"].append(row)
    records = 0
    candidates = []
    for name in ("journal-network", "journal-kernel", "journal-wifi-samples"):
        count, found = journal_candidates(destination / f"{name}.stdout")
        records += count
        candidates.extend(found)
    metadata["journal_records"] = records
    metadata["journal_rssi_candidates"] = len(candidates)
    metadata["rssi_history_status"] = (
        "journal_candidates_require_review" if candidates else "not_found_in_exported_journals")
    if not candidates:
        metadata["warnings"].append(
            "No historical RSSI candidates in journal exports. Rotated files need separate review; "
            "current snapshots do not recover missing flight history.")
    (destination / "journal-rssi-candidates.json").write_text(json.dumps(candidates, indent=2) + "\n")
    (destination / "WIFI_EVIDENCE.json").write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")
    return metadata
