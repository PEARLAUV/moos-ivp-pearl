#!/usr/bin/env python3
"""Assemble the newest closed three-community local simulation logs."""

from __future__ import annotations

import argparse
import re
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Any

import collect_mission_logs as local_collector

LOGSTART = re.compile(r"^%% LOGSTART\s+([0-9]+(?:\.[0-9]+)?)\s*$")
PREFIXES = {
    "shoreside": "LOG_SHORESIDE_",
    "pearl": "LOG_pearl_",
    "drone": "LOG_uav_",
}


class SimCollectionError(RuntimeError):
    """The latest coherent, closed simulation run could not be assembled."""


def log_start(directory: Path) -> float:
    configs = list(directory.glob("*._moos"))
    if len(configs) != 1:
        raise SimCollectionError(f"Expected exactly one ._moos file in {directory}")
    with configs[0].open(errors="replace") as source:
        for line in source:
            match = LOGSTART.match(line.rstrip())
            if match:
                try:
                    return float(match.group(1))
                except ValueError as exc:
                    raise SimCollectionError(
                        f"Invalid LOGSTART in {configs[0]}") from exc
    raise SimCollectionError(f"Missing LOGSTART in {configs[0]}")


def is_sitl_uav_log(directory: Path) -> bool:
    configs = list(directory.glob("*._moos"))
    if len(configs) != 1:
        return False
    source = configs[0].read_text(errors="replace")
    return ("Run = pMavlinkLandingTarget" in source
            and re.search(r"(?m)^\s*url_protocol\s*=\s*tcp\s*$", source) is not None)


def candidates(mission: Path, prefix: str, *, sitl_uav_only: bool = False
               ) -> list[tuple[float, Path]]:
    found = []
    for directory in mission.glob(prefix + "*"):
        if (directory.is_dir() and list(directory.glob("*.alog"))
                and (not sitl_uav_only or is_sitl_uav_log(directory))):
            found.append((log_start(directory), directory.resolve()))
    if not found:
        raise SimCollectionError(f"No local {prefix}* simulation log found")
    return found


def select_run(mission: Path, maximum_skew: float = 10.0) -> dict[str, Path]:
    drone_rows = candidates(mission, PREFIXES["drone"], sitl_uav_only=True)
    drone_start, drone = max(drone_rows, key=lambda row: row[0])
    selected = {"drone": drone}
    for node in ("pearl", "shoreside"):
        rows = candidates(mission, PREFIXES[node])
        start, directory = min(rows, key=lambda row: abs(row[0] - drone_start))
        skew = abs(start - drone_start)
        if skew > maximum_skew:
            raise SimCollectionError(
                f"Newest UAV log has no matching {node} log within {maximum_skew:g}s "
                f"(closest skew {skew:.3f}s)")
        selected[node] = directory
    return selected


def latest_sitl_log(mission: Path) -> Path:
    rows = [path for path in (mission / "LOG_sitl").glob("moos-sitl-*.log")
            if path.is_file()]
    if not rows:
        raise SimCollectionError("No LOG_sitl/moos-sitl-*.log launcher log found")
    return max(rows, key=lambda path: path.stat().st_mtime).resolve()


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mission-dir", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--maximum-skew", type=float, default=10.0)
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    mission = args.mission_dir.resolve()
    output_root = args.output_root.resolve()
    stage: Path | None = None
    selected: dict[str, Path] = {}
    manifest: dict[str, Any] = {
        "collector": "collect_sim_logs.py",
        "created_utc": local_collector.utc_now(),
        "node": "sim",
        "sources": {},
    }
    try:
        if not mission.is_dir():
            raise SimCollectionError(f"Mission directory does not exist: {mission}")
        if not 0 <= args.maximum_skew <= 60:
            raise SimCollectionError("--maximum-skew must be in [0,60] seconds")
        selected = select_run(mission, args.maximum_skew)
        sitl_log = latest_sitl_log(mission)
        for directory in selected.values():
            opened = local_collector.open_files_below(directory)
            if opened:
                raise SimCollectionError(
                    "Simulation MOOS logs are still open; stop the simulation first: "
                    + "; ".join(opened[:5]))
        sitl_opened = local_collector.open_files_below(sitl_log)
        if sitl_opened:
            raise SimCollectionError(
                "SITL launcher log is still open; stop the simulation first: "
                + "; ".join(sitl_opened[:5]))
        output_root.mkdir(parents=True, exist_ok=True)
        stage = Path(tempfile.mkdtemp(prefix=".collecting-sim-", dir=output_root))
        for node, source in selected.items():
            destination = stage / "moos" / node / source.name
            shutil.copytree(source, destination, copy_function=shutil.copy2)
            manifest["sources"][node] = {
                "path": str(source), "log_start": log_start(source)
            }
        (stage / "sitl").mkdir()
        shutil.copy2(sitl_log, stage / "sitl" / sitl_log.name)
        manifest["sources"]["sitl"] = {"path": str(sitl_log)}
        params = mission / "LOG_params"
        if params.is_dir() and any(path.is_file() for path in params.rglob("*")):
            shutil.copytree(params, stage / "parameters", copy_function=shutil.copy2)
            manifest["sources"]["parameters"] = {"path": str(params)}
        final = local_collector.finalize(
            stage, output_root, "sim", selected["drone"].name,
            manifest, True)
        stage = None
        print(final)
        return 0
    except (SimCollectionError, OSError, ValueError, KeyboardInterrupt) as exc:
        message = str(exc) or exc.__class__.__name__
        if stage is not None and stage.exists():
            try:
                final = local_collector.finalize(
                    stage, output_root, "sim", selected.get("drone", Path("unknown")).name,
                    manifest, False, message)
                stage = None
                print(f"INCOMPLETE simulation artifact: {final}", file=sys.stderr)
            except OSError:
                if stage is not None:
                    shutil.rmtree(stage, ignore_errors=True)
        print(f"Simulation log collection failed: {message}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
