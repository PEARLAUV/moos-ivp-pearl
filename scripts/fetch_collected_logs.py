#!/usr/bin/env python3
"""Fetch all pre-collected drone and PEARL bundles, without remote collection.

Each run inventories finalized bundles, reuses verified local copies, and retains
independent transfer outcomes. It does not infer cross-node flight membership.
"""

from __future__ import annotations

import argparse
import datetime as dt
import fnmatch
import json
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

import collect_mission_logs as local_collector
from log_progress import Progress, hash_file, run_streaming, status

SAFE_NAME = re.compile(r"[A-Za-z0-9_.-]+\Z")
DEFAULT_REMOTE_ROOT = (
    "/home/pearl/moos-ivp-pearl/missions/briggs_field/"
    "2026_10_09_water_ops_prep/field_logs")


class FetchError(RuntimeError):
    """A remote artifact could not be discovered, transferred, or verified."""


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
    # OpenSSH joins arguments into a remote shell command. Quote every argument.
    return ["ssh", *options, host, shlex.join(command)]


def remote_inventory(host: str, remote_root: str, node_pattern: str,
                     ssh_options: list[str]) -> dict[str, Any]:
    # Read only. Do not invoke make, contact a controller, stop services, or create
    # remote files. Finalized partial bundles are deliberately included as well.
    script = '''import fnmatch, hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1]).expanduser().resolve(strict=True)
if not root.is_dir():
    raise RuntimeError("artifact root is not a directory: " + str(root))
rows = []
for path in sorted(root.iterdir()):
    if not fnmatch.fnmatchcase(path.name, sys.argv[2]):
        continue
    row = {"name": path.name}
    try:
        if path.is_symlink() or not path.is_dir():
            raise RuntimeError("bundle is not a regular directory")
        sums = path / "SHA256SUMS"
        manifest = path / "MANIFEST.json"
        if sums.is_symlink() or manifest.is_symlink() or not manifest.is_file():
            raise RuntimeError("missing or symlinked bundle metadata")
        row["sha256sums_sha256"] = hashlib.sha256(sums.read_bytes()).hexdigest()
    except (OSError, RuntimeError) as exc:
        row["error"] = str(exc)
    rows.append(row)
print(json.dumps({"root": str(root), "artifacts": rows}))
'''
    status(f"SSH inventory: {host}:{remote_root} (timeout 60s)")
    command = ssh_command(host, ssh_options,
                          ["python3", "-c", script, remote_root, node_pattern])
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=False,
                                timeout=60)
        if result.returncode != 0:
            raise FetchError(result.stderr.strip() or f"exit {result.returncode}")
        inventory = json.loads(result.stdout)
        if not isinstance(inventory, dict) or not isinstance(inventory.get("root"), str):
            raise ValueError("invalid inventory root")
        if not Path(inventory["root"]).is_absolute():
            raise ValueError("inventory root must be absolute")
        rows = inventory["artifacts"]
        if not isinstance(rows, list) or not rows:
            raise ValueError(f"no collected bundles for {node_pattern}")
        names = set()
        for row in rows:
            name = row["name"]
            if (not isinstance(name, str) or not SAFE_NAME.fullmatch(name)
                    or not fnmatch.fnmatchcase(name, node_pattern) or name in names):
                raise ValueError("unsafe or duplicate artifact name")
            if not row.get("error") and not re.fullmatch(
                    r"[0-9a-f]{64}", row.get("sha256sums_sha256", "")):
                raise ValueError("invalid bundle fingerprint")
            names.add(name)
        return inventory
    except (OSError, subprocess.SubprocessError, ValueError, KeyError, TypeError,
            FetchError) as exc:
        raise FetchError(f"SSH inventory failed for {host}:{remote_root}: {exc}") from exc


def rsync_artifact(host: str, remote_root: str, name: str, destination: Path,
                   ssh_options: list[str]) -> None:
    destination.mkdir(parents=True, exist_ok=False)
    ssh_transport = shlex.join(["ssh", *ssh_options])
    source = f"{host}:{remote_root.rstrip('/')}/{name}/"
    command = ["rsync", "-a", "--partial", "--protect-args",
               "--info=progress2", "--outbuf=L", "--no-inc-recursive", "--human-readable",
               "-e", ssh_transport, source, str(destination) + "/"]
    status(f"Transfer: {host}:{name} (rsync listing/transferring; progress is per bundle)")
    try:
        returncode, output_tail = run_streaming(command, f"rsync {host}:{name}")
    except OSError as exc:
        raise FetchError(f"rsync failed for {host}:{name}: {exc}") from exc
    if returncode != 0:
        detail = output_tail.strip() or "no subprocess output"
        raise FetchError(f"rsync failed for {host}:{name}: exit {returncode}: {detail}")


def sha256(path: Path, progress: Progress | None = None) -> str:
    return hash_file(path, progress)


def verify_artifact(artifact: Path, expected_nodes: set[str]) -> dict[str, Any]:
    manifest_path = artifact / "MANIFEST.json"
    sums_path = artifact / "SHA256SUMS"
    if artifact.is_symlink() or not manifest_path.is_file() or not sums_path.is_file():
        raise FetchError(f"Artifact lacks regular MANIFEST.json or SHA256SUMS: {artifact}")
    # A transfer is only verified when ALL regular files, including the manifest,
    # are covered. Reject symlinks instead of following paths outside the bundle.
    files = set()
    for path in artifact.rglob("*"):
        if path.is_symlink():
            raise FetchError(f"Symlink in artifact: {path}")
        if path.is_file() and path != sums_path:
            files.add(path.relative_to(artifact).as_posix())
        elif not path.is_file() and not path.is_dir():
            raise FetchError(f"Special file in artifact: {path}")
    checked = set()
    total = sum((artifact / relative).stat().st_size for relative in files)
    with Progress(f"SHA256 {artifact.name}", total) as progress:
        for number, line in enumerate(sums_path.read_text().splitlines(), 1):
            if not line.strip():
                continue
            if "  " not in line:
                raise FetchError(f"Malformed checksum at {sums_path}:{number}")
            wanted, relative = line.split("  ", 1)
            if not re.fullmatch(r"[0-9a-f]{64}", wanted):
                raise FetchError(f"Malformed SHA-256 at {sums_path}:{number}")
            if (relative not in files or relative in checked
                    or Path(relative).is_absolute() or ".." in Path(relative).parts):
                raise FetchError(f"Unsafe, duplicate, or missing checksum path: {relative}")
            if sha256(artifact / relative, progress) != wanted:
                raise FetchError(f"Checksum verification failed: {artifact / relative}")
            checked.add(relative)
        if checked != files or "MANIFEST.json" not in checked:
            raise FetchError(f"Incomplete checksum coverage at {artifact}: {sorted(files - checked)}")
    try:
        manifest = json.loads(manifest_path.read_text())
        if not isinstance(manifest, dict):
            raise ValueError("manifest is not an object")
    except (OSError, ValueError) as exc:
        raise FetchError(f"Invalid artifact manifest: {artifact}") from exc
    node = manifest.get("node")
    if not isinstance(node, str) or node not in expected_nodes:
        raise FetchError(
            f"Artifact node mismatch at {artifact}: expected {sorted(expected_nodes)}, got {node}")
    return {"artifact": artifact.name, "node": node,
            "source_complete": manifest.get("complete") is True,
            "moos_log_source": manifest.get("moos_log_source"),
            "verified_files": len(checked)}


def previous_copies(output_root: Path) -> dict[tuple[str, str, str, str], Path]:
    """Index provenance, not mtimes; callers rehash each candidate before reuse."""
    copies = {}
    for manifest_path in sorted(output_root.glob("*_remote-artifacts*/FETCH_MANIFEST.json")):
        try:
            manifest = json.loads(manifest_path.read_text())
            for source in manifest["sources"].values():
                for row in source.get("artifacts", []):
                    if row.get("status") not in ("fetched", "reused"):
                        continue
                    candidate = (output_root / row["local_path"]).resolve()
                    candidate.relative_to(output_root)
                    key = (source["host"], source["remote_root"], row["name"],
                           row["sha256sums_sha256"])
                    copies[key] = candidate
        except (OSError, ValueError, KeyError, TypeError, AttributeError):
            # Old-format manifests and interrupted snapshots are not cache proof.
            continue
    return copies


def fetch_source(label: str, host: str, remote_root: str, pattern: str,
                 expected_nodes: set[str], stage: Path, output_root: Path,
                 copies: dict, ssh_options: list[str], result: dict[str, Any]) -> list[str]:
    result.update(host=host, requested_remote_root=remote_root, artifacts=[])
    before = remote_inventory(host, remote_root, pattern, ssh_options)
    remote_root = before["root"]
    result["remote_root"] = remote_root
    result["inventory_before"] = before
    errors = []
    status(f"{label}: {len(before['artifacts'])} saved bundles found")
    for index, record in enumerate(before["artifacts"], 1):
        row = dict(record, status="failed")
        result["artifacts"].append(row)
        name = row["name"]
        status(f"{label}: bundle {index}/{len(before['artifacts'])}: {name}")
        try:
            if row.get("error"):
                raise FetchError(row["error"])
            fingerprint = row["sha256sums_sha256"]
            key = (host, remote_root, name, fingerprint)
            candidate = copies.get(key)
            if candidate is not None:
                status(f"Rechecking cached copy before reuse: {candidate}")
                try:
                    if sha256(candidate / "SHA256SUMS") != fingerprint:
                        raise FetchError("local checksum inventory changed")
                    row.update(verify_artifact(candidate, expected_nodes))
                    row.update(status="reused", local_path=candidate.relative_to(output_root).as_posix())
                    status(f"Reused verified bundle: {label}/{name}")
                    continue
                except (FetchError, OSError, ValueError) as exc:
                    # Preserve the damaged copy; download a fresh one, never edit it.
                    status(f"Cached copy unusable ({exc}); retaining it and fetching a fresh copy")
            destination = stage / label / name
            row["staged_path"] = f"{label}/{name}"
            rsync_artifact(host, remote_root, name, destination, ssh_options)
            if sha256(destination / "SHA256SUMS") != fingerprint:
                raise FetchError("source checksum inventory changed during transfer")
            status(f"Verifying downloaded bundle: {label}/{name}")
            row.update(verify_artifact(destination, expected_nodes))
            row["status"] = "fetched"
            status(f"Verified downloaded bundle: {label}/{name}")
        except (FetchError, OSError, ValueError) as exc:
            row["error"] = str(exc)
            errors.append(f"{label}/{name}: {exc}")
            status(f"FAILED {label}/{name}: {exc}")
    status(f"{label}: checking remote inventory again for source changes")
    after = remote_inventory(host, remote_root, pattern, ssh_options)
    result["inventory_after"] = after
    after_rows = {row["name"]: row for row in after["artifacts"]}
    for row in result["artifacts"]:
        original = next(item for item in before["artifacts"] if item["name"] == row["name"])
        if after_rows.get(row["name"]) != original:
            row.update(status="failed", error="source changed or disappeared during fetch")
            errors.append(f"{label}/{row['name']}: {row['error']}")
    added = sorted(set(after_rows) - {row["name"] for row in before["artifacts"]})
    result["pending_new_bundles"] = added
    if added:
        errors.append(f"{label}: new bundles appeared during fetch; rerun: {', '.join(added)}")
    if after["root"] != before["root"]:
        errors.append(f"{label}: remote root changed during fetch")
    result["transfer_complete"] = not errors
    return errors


def finalize(stage: Path, output_root: Path, manifest: dict[str, Any],
             error: str | None = None) -> Path:
    manifest["transfer_complete"] = error is None
    if error:
        manifest["error"] = error
        (stage / "FETCH_ERROR.txt").write_text(error + "\n")
    base = f"{dt.datetime.now(dt.timezone.utc):%Y%m%dT%H%M%SZ}_remote-artifacts"
    final = output_root / base
    suffix = 2
    while final.exists():
        final = output_root / f"{base}_{suffix}"
        suffix += 1
    for source in manifest["sources"].values():
        for row in source.get("artifacts", []):
            if "staged_path" in row:
                row["local_path"] = f"{final.name}/{row['staged_path']}"
    (stage / "FETCH_MANIFEST.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    stage.rename(final)
    return final


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site", choices=("briggs", "charles"), default="briggs")
    parser.add_argument("--shore-ip", default="100.88.214.50")
    parser.add_argument("--drone-host", default="pearl@100.112.119.114")
    parser.add_argument("--pearl-real-host", default="pearl@100.113.99.58")
    parser.add_argument("--pearl-grass-host", default="pearl@100.71.137.47")
    parser.add_argument("--remote-root", default=DEFAULT_REMOTE_ROOT)
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
        "schema_version": 2, "collector": "fetch_collected_logs.py", "created_utc": utc_now(),
        "site": args.site, "selection": "all finalized bundles; no cross-node flight pairing",
        "sources": {},
    }
    try:
        if not args.allow_non_shoreside:
            assert_shoreside(args.shore_ip)
        pearl_host = args.pearl_grass_host if args.site == "briggs" else args.pearl_real_host
        pearl_node = "pearl-grass" if args.site == "briggs" else "pearl-real"
        output_root.mkdir(parents=True, exist_ok=True)
        status(f"Indexing previous fetch manifests: {output_root}")
        copies = previous_copies(output_root)
        status(f"Indexed {len(copies)} prior bundle references; copies are rehashed before reuse")
        stage = Path(tempfile.mkdtemp(prefix=".fetching-", dir=output_root))
        errors = []
        for label, host, root, node, pattern in (
                ("drone", args.drone_host, args.drone_remote_root or args.remote_root,
                 "drone", "*_drone_LOG_uav_*"),
                ("pearl", pearl_host, args.pearl_remote_root or args.remote_root,
                 pearl_node, f"*_{pearl_node}_LOG_pearl_*")):
            result: dict[str, Any] = {"transfer_complete": False}
            manifest["sources"][label] = result
            try:
                errors.extend(fetch_source(label, host, root, pattern, {node}, stage,
                                           output_root, copies, args.ssh_option, result))
            except (FetchError, OSError, ValueError) as exc:
                result["error"] = str(exc)
                errors.append(f"{label}: {exc}")
                status(f"FAILED {label}: {exc}")
        status("Finalizing fetch provenance manifest")
        final = finalize(stage, output_root, manifest, "\n".join(errors) if errors else None)
        stage = None
        incomplete = [f"{label}/{row['name']}" for label, source in manifest["sources"].items()
                      for row in source.get("artifacts", [])
                      if row.get("status") in ("fetched", "reused") and not row["source_complete"]]
        if incomplete:
            print("Warning: source bundles remain incomplete: " + ", ".join(incomplete), file=sys.stderr)
        print(final)
        if errors:
            print("INCOMPLETE fetch; rerun to retrieve outstanding bundles:\n" + "\n".join(errors),
                  file=sys.stderr)
            return 1
        return 0
    except (FetchError, OSError, ValueError, KeyboardInterrupt) as exc:
        message = str(exc) or exc.__class__.__name__
        if stage is not None and stage.exists():
            try:
                final = finalize(stage, output_root, manifest, message)
                print(f"INCOMPLETE fetch directory: {final}", file=sys.stderr)
            except OSError:
                # Keep the staging directory for recovery instead of deleting evidence.
                print(f"Retained fetch staging directory: {stage}", file=sys.stderr)
        print(f"Collected-log fetch failed: {message}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
