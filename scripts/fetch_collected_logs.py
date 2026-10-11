#!/usr/bin/env python3
"""Fetch the newest pre-collected drone and PEARL artifacts onto shoreside."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

import collect_mission_logs as local_collector

SAFE_NAME = re.compile(r"[A-Za-z0-9_.-]+\Z")


class FetchError(RuntimeError):
    """A remote artifact could not be selected, transferred, or verified."""


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace(
        "+00:00", "Z")


def assert_shoreside(shore_ip: str) -> None:
    addresses = local_collector.local_addresses()
    if shore_ip not in addresses:
        raise FetchError(
            f"collect-logs must run on shoreside ({shore_ip}); local addresses are "
            f"{sorted(addresses)}")


def ssh_command(host: str, options: list[str], command: list[str]) -> list[str]:
    # OpenSSH joins arguments into a remote shell command. Quote every fixed or
    # operator-supplied argument before handing it to that shell.
    return ["ssh", *options, host, shlex.join(command)]


def remote_latest(host: str, remote_root: str, node_pattern: str,
                  ssh_options: list[str]) -> str:
    script = (
        'root=$1; pattern=$2; test -d "$root" || { '
        'echo "artifact root not found: $root" >&2; exit 20; }; '
        'find "$root" -mindepth 1 -maxdepth 1 -type d -name "$pattern" '
        "-printf '%T@ %f\\n' | sort -nr | head -n 1 | cut -d' ' -f2-"
    )
    command = ssh_command(host, ssh_options,
                          ["sh", "-c", script, "fetch-collected-logs",
                           remote_root, node_pattern])
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=False,
                                timeout=30)
    except (OSError, subprocess.SubprocessError) as exc:
        raise FetchError(f"SSH discovery failed for {host}: {exc}") from exc
    if result.returncode != 0:
        detail = result.stderr.strip() or f"exit {result.returncode}"
        raise FetchError(f"SSH discovery failed for {host}: {detail}")
    name = result.stdout.strip()
    if (not name or "\n" in name or not SAFE_NAME.fullmatch(name)
            or node_pattern.strip("*") not in name):
        raise FetchError(f"No valid collected artifact found on {host} for {node_pattern}")
    return name


def rsync_artifact(host: str, remote_root: str, name: str, destination: Path,
                   ssh_options: list[str]) -> None:
    destination.mkdir(parents=True, exist_ok=False)
    ssh_transport = shlex.join(["ssh", *ssh_options])
    source = f"{host}:{remote_root.rstrip('/')}/{name}/"
    command = ["rsync", "-a", "--partial", "--protect-args", "-e", ssh_transport,
               source, str(destination) + "/"]
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=False)
    except OSError as exc:
        raise FetchError(f"rsync failed for {host}:{name}: {exc}") from exc
    if result.returncode != 0:
        detail = result.stderr.strip() or f"exit {result.returncode}"
        raise FetchError(f"rsync failed for {host}:{name}: {detail}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_artifact(artifact: Path, expected_nodes: set[str]) -> dict[str, Any]:
    manifest_path = artifact / "MANIFEST.json"
    sums_path = artifact / "SHA256SUMS"
    if not manifest_path.is_file() or not sums_path.is_file():
        raise FetchError(f"Artifact lacks MANIFEST.json or SHA256SUMS: {artifact}")
    try:
        manifest = json.loads(manifest_path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise FetchError(f"Invalid artifact manifest: {artifact}") from exc
    node = manifest.get("node")
    if node not in expected_nodes:
        raise FetchError(
            f"Artifact node mismatch at {artifact}: expected {sorted(expected_nodes)}, got {node}")
    checked = 0
    for number, line in enumerate(sums_path.read_text().splitlines(), 1):
        if not line.strip():
            continue
        if "  " not in line:
            raise FetchError(f"Malformed checksum at {sums_path}:{number}")
        wanted, relative = line.split("  ", 1)
        if not re.fullmatch(r"[0-9a-f]{64}", wanted):
            raise FetchError(f"Malformed SHA-256 at {sums_path}:{number}")
        candidate = (artifact / relative).resolve()
        try:
            candidate.relative_to(artifact.resolve())
        except ValueError as exc:
            raise FetchError(f"Checksum path escapes artifact at {sums_path}:{number}") from exc
        if not candidate.is_file() or sha256(candidate) != wanted:
            raise FetchError(f"Checksum verification failed: {candidate}")
        checked += 1
    if checked == 0:
        raise FetchError(f"Artifact has no checksummed files: {artifact}")
    return {"artifact": artifact.name, "node": node,
            "source_complete": bool(manifest.get("complete")),
            "verified_files": checked}


def fetch_source(label: str, host: str, remote_root: str, pattern: str,
                 expected_nodes: set[str], stage: Path,
                 ssh_options: list[str]) -> dict[str, Any]:
    name = remote_latest(host, remote_root, pattern, ssh_options)
    if not SAFE_NAME.fullmatch(name):
        raise FetchError(f"Unsafe remote artifact name from {host}")
    destination = stage / label / name
    rsync_artifact(host, remote_root, name, destination, ssh_options)
    result = verify_artifact(destination, expected_nodes)
    result.update({"host": host, "remote_root": remote_root})
    return result


def finalize(stage: Path, output_root: Path, manifest: dict[str, Any],
             error: str | None = None) -> Path:
    manifest["transfer_complete"] = error is None
    if error:
        manifest["error"] = error
        (stage / "FETCH_ERROR.txt").write_text(error + "\n")
    (stage / "FETCH_MANIFEST.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    base = f"{dt.datetime.now(dt.timezone.utc):%Y%m%dT%H%M%SZ}_remote-artifacts"
    final = output_root / base
    suffix = 2
    while final.exists():
        final = output_root / f"{base}_{suffix}"
        suffix += 1
    stage.rename(final)
    return final


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site", choices=("briggs", "charles"), default="briggs")
    parser.add_argument("--shore-ip", default="100.88.214.50")
    parser.add_argument("--drone-host", default="pi@100.112.119.114")
    parser.add_argument("--pearl-real-host", default="pearl@100.113.99.58")
    parser.add_argument("--pearl-grass-host", default="pearl@100.71.137.47")
    parser.add_argument("--remote-root", default=(
        "workspace/mit/pearl/moos-ivp-pearl/missions/briggs_field/"
        "2026_10_09_water_ops_prep/field_logs"))
    parser.add_argument("--drone-remote-root")
    parser.add_argument("--pearl-remote-root")
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--ssh-option", action="append",
                        default=["-o", "ConnectTimeout=15"])
    parser.add_argument("--allow-non-shoreside", action="store_true",
                        help=argparse.SUPPRESS)
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    output_root = args.output_root.resolve()
    stage: Path | None = None
    manifest: dict[str, Any] = {
        "collector": "fetch_collected_logs.py", "created_utc": utc_now(),
        "site": args.site, "sources": {},
    }
    try:
        if not args.allow_non_shoreside:
            assert_shoreside(args.shore_ip)
        drone_root = args.drone_remote_root or args.remote_root
        pearl_root = args.pearl_remote_root or args.remote_root
        pearl_host = (args.pearl_grass_host if args.site == "briggs"
                      else args.pearl_real_host)
        pearl_nodes = {"pearl-grass"} if args.site == "briggs" else {"pearl-real"}
        pearl_pattern = ("*_pearl-grass_LOG_pearl_*" if args.site == "briggs"
                         else "*_pearl-real_LOG_pearl_*")
        output_root.mkdir(parents=True, exist_ok=True)
        stage = Path(tempfile.mkdtemp(prefix=".fetching-", dir=output_root))
        manifest["sources"]["drone"] = fetch_source(
            "drone", args.drone_host, drone_root, "*_drone_LOG_uav_*", {"drone"},
            stage, args.ssh_option)
        manifest["sources"]["pearl"] = fetch_source(
            "pearl", pearl_host, pearl_root, pearl_pattern, pearl_nodes,
            stage, args.ssh_option)
        final = finalize(stage, output_root, manifest)
        stage = None
        incomplete = [label for label, row in manifest["sources"].items()
                      if not row["source_complete"]]
        if incomplete:
            print("Warning: transferred source artifact(s) are marked incomplete: "
                  + ", ".join(incomplete), file=sys.stderr)
        print(final)
        return 0
    except (FetchError, OSError, ValueError, KeyboardInterrupt) as exc:
        message = str(exc) or exc.__class__.__name__
        if stage is not None and stage.exists():
            try:
                final = finalize(stage, output_root, manifest, message)
                stage = None
                print(f"INCOMPLETE fetch directory: {final}", file=sys.stderr)
            except OSError:
                if stage is not None:
                    shutil.rmtree(stage, ignore_errors=True)
        print(f"Collected-log fetch failed: {message}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
