#!/bin/bash
#------------------------------------------------------------
#   Script: launch_sitl.sh
#  Mission: aug_pearl_uav
#------------------------------------------------------------
set -euo pipefail

ME=$(basename "$0")
MISSION_DIR=$(cd "$(dirname "$0")" && pwd)
ARDUPILOT_SITL_ROOT="${ARDUPILOT_SITL_ROOT:-$HOME/workspace/mit/pearl/ardupilot-sitl}"
SITL_LAUNCHER="$ARDUPILOT_SITL_ROOT/bin/ardupilot-sitl"
STATE_DIR=""
LANDING_TARGET="yes"
COMPOSE_ARGS=()

for ARGI; do
  if [[ "$ARGI" = "--help" || "$ARGI" = "-h" ]]; then
    echo "$ME [OPTIONS] [COMPOSE_COMMAND ...]"
    echo "  --help, -h                    Show this help message"
    echo "  --sitl_root=<path>            ardupilot-sitl checkout"
    echo "  --state_dir=<path>            Persist SITL state in this directory"
    echo "  --no_landing_target           Do not run the vision-target producer"
    echo
    echo "With no Compose command, starts 'up --no-build' in the foreground."
    echo "Other commands (down, logs, ps, etc.) pass through to ardupilot-sitl."
    exit 0
  elif [[ "$ARGI" == --sitl_root=* ]]; then
    ARDUPILOT_SITL_ROOT="${ARGI#*=}"
    SITL_LAUNCHER="$ARDUPILOT_SITL_ROOT/bin/ardupilot-sitl"
  elif [[ "$ARGI" == --state_dir=* ]]; then
    STATE_DIR="${ARGI#*=}"
  elif [[ "$ARGI" = "--no_landing_target" ]]; then
    LANDING_TARGET="no"
  else
    COMPOSE_ARGS+=("$ARGI")
  fi
done

if [[ ! -x "$SITL_LAUNCHER" ]]; then
  echo "$ME: ardupilot-sitl launcher not found at: $SITL_LAUNCHER" >&2
  exit 1
fi

COMMAND="${COMPOSE_ARGS[0]:-up}"
STARTING="no"
case "$COMMAND" in
  up) STARTING="yes" ;;
esac

LANDING_TARGET_PID=""
cleanup() {
  if [[ -n "$LANDING_TARGET_PID" ]]; then
    kill "$LANDING_TARGET_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

if [[ "$STARTING" = "yes" && "$LANDING_TARGET" = "yes" ]]; then
  PYTHONPATH="$ARDUPILOT_SITL_ROOT/ardupilot/modules/mavlink${PYTHONPATH:+:$PYTHONPATH}" \
    python3 "$MISSION_DIR/sitl_landing_target.py" &
  LANDING_TARGET_PID=$!
fi

LAUNCH_ARGS=()
if [[ -n "$STATE_DIR" ]]; then
  LAUNCH_ARGS+=("$STATE_DIR")
fi
LAUNCH_ARGS+=("${COMPOSE_ARGS[@]}")

"$SITL_LAUNCHER" "${LAUNCH_ARGS[@]}"
