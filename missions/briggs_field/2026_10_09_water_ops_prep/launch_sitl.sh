#!/usr/bin/env bash
# Mission-specific parameter file, shared ArduPilot SITL launcher.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
PEARL_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd -P)"
# Durable ownership is independent of LOG_* so clean.sh cannot erase it.
OWNERS_DIR="${SITL_OWNERS_DIR:-$SCRIPT_DIR/.sitl-owned}"
stop_owned_project() {
  local project=$1
  if [[ ! "$project" =~ ^moos-sitl-$(id -u)-[0-9]+$ ]]; then
    echo "Refusing invalid/unowned SITL project: $project" >&2
    return 1
  fi
  [[ -f "$OWNERS_DIR/$project" ]] || return 0
  echo "Stopping owned SITL project: $project"
  if COMPOSE_PROJECT_NAME="$project" timeout 15 \
      "$PEARL_ROOT/scripts/launch_ardupilot_sitl.sh" down --timeout 5; then
    rm -f -- "$OWNERS_DIR/$project"
  else
    echo "SITL cleanup failed; ownership retained for retry: $project" >&2
    return 1
  fi
}
# Stop only this mission's MarineViewer wrapper and QGC VNC containers.
# The optional group scope is used by combined-run teardown; make kill uses
# the whole mission scope. Wrapper TERM runs its TigerVNC/noVNC cleanup trap.
if [[ "${1:-}" == --cleanup-ui || "${1:-}" == --cleanup-ui-group=* ]]; then
  group=""
  [[ "$1" != --cleanup-ui-group=* ]] || group="${1#*=}"
  python3 - "$SCRIPT_DIR" "$group" <<'PY'
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
mission, group = Path(sys.argv[1]).resolve(), sys.argv[2]
if group and not group.isdecimal():
    sys.exit("Invalid UI process-group scope")
containers = set()
for proc in Path("/proc").glob("[0-9]*"):
    try:
        if proc.stat().st_uid != os.getuid() or (proc / "cwd").resolve() != mission:
            continue
        pid = int(proc.name)
        if group and os.getpgid(pid) != int(group):
            continue
        args = [part.decode(errors="replace") for part in
                (proc / "cmdline").read_bytes().rstrip(bytes(1)).split(bytes(1))]
        if (any(Path(arg).name == "moos-vnc-session" for arg in args)
                and any(Path(arg).name == "targ_shoreside.moos" for arg in args)):
            os.kill(pid, signal.SIGTERM)
        if ("run" in args and "--name" in args
                and any(arg.startswith("QGC_MAVLINK_ENDPOINT=") for arg in args)):
            containers.add(args[args.index("--name") + 1])
    except (OSError, ValueError, IndexError):
        continue
docker = shutil.which("docker")
if docker:
    # Also recover orphaned QGC containers with this user's mission export
    # mount, but only for whole-mission cleanup, not a single run's group.
    if not group:
        listing = subprocess.run([docker, "ps", "-aq"], capture_output=True,
                                 text=True, timeout=10)
        if listing.returncode:
            sys.exit("Cannot enumerate QGC VNC containers for cleanup")
        for cid in listing.stdout.split():
            inspected = subprocess.run([docker, "inspect", cid], capture_output=True,
                                       text=True, timeout=10)
            try:
                info = json.loads(inspected.stdout)[0]
                config = info.get("Config", {})
                if str(config.get("User", "")).split(":")[0] != str(os.getuid()):
                    continue
                if not any(str(env).startswith("QGC_MAVLINK_ENDPOINT=")
                           for env in config.get("Env", [])):
                    continue
                if any(Path(mount.get("Source", "")).resolve() == mission / "gcf-home"
                       and mount.get("Destination") == "/home/qgc/Documents/QGroundControl"
                       for mount in info.get("Mounts", [])):
                    containers.add(cid)
            except (ValueError, IndexError, TypeError):
                continue
    failed = False
    for container in containers:
        result = subprocess.run([docker, "stop", "--time", "5", container], timeout=15)
        if result.returncode:
            # A wrapper may already have stopped/removed its --rm container.
            exists = subprocess.run([docker, "inspect", container],
                                    capture_output=True, timeout=10)
            failed |= exists.returncode == 0
    if failed:
        sys.exit("Failed to stop an owned QGC VNC container")
PY
  exit $?
fi
# Cleanup must not depend on model profiles, endpoints, source readiness or Home.
if [[ "${1:-}" == --cleanup-owned ]]; then
  status=0
  shopt -s nullglob
  for marker in "$OWNERS_DIR"/moos-sitl-$(id -u)-*; do
    stop_owned_project "${marker##*/}" || status=1
  done
  exit "$status"
elif [[ "${1:-}" == --cleanup-project=* ]]; then
  stop_owned_project "${1#*=}"
  exit $?
fi
# Optional drone -> SITL mode -> mission layers; caller overlays follow them.
source "$PEARL_ROOT/scripts/ardupilot_param_stack.sh"
ardupilot_parameter_stack "$PEARL_ROOT" "$SCRIPT_DIR" SITL
case "${MISSION_SITE:-briggs}" in
  briggs) default_home="42.3569186052,-71.0990291116,0,0" ;;
  charles) default_home="42.358436,-71.087448,0,0" ;;
  *) echo "Invalid MISSION_SITE: ${MISSION_SITE}" >&2; exit 1 ;;
esac
# Explicit CLI flags are required by the wrapper; environment values alone
# do not select its Home/endpoints. These initialize only a NEW owned SITL.
# Caller --home/endpoint flags, if supplied later, still override these defaults.
exec "$PEARL_ROOT/scripts/launch_ardupilot_sitl.sh" \
  --no-rc "${ARDUPILOT_PARAM_ARGS[@]}" --home "${SITL_HOME:-$default_home}" \
  --port "${SITL_PORT:-5760}" --diagnostic-port "${SITL_DIAGNOSTIC_PORT:-5762}" \
  --sim-port "${SITL_TARGET_PORT:-5763}" "$@"
