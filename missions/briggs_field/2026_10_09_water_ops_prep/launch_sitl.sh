#!/usr/bin/env bash
# Launch the containerized ArduPilot SITL with mission parameter defaults.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
SITL_WRAPPER="${SITL_WRAPPER:-$HOME/workspace/mit/pearl/ardupilot-sitl/bin/ardupilot-sitl}"

if [[ "${1:-}" == --help || "${1:-}" == -h ]]; then
  echo "Usage: $0 [time_warp] [STATE_DIRECTORY] [COMPOSE_COMMAND ...]"
  echo "Use the same time_warp as launch.sh; default is SITL_SPEEDUP or 1."
  exit 0
fi

if [[ ! -x "$SITL_WRAPPER" ]]; then
  echo "launch_sitl.sh: wrapper not found or not executable: $SITL_WRAPPER" >&2
  exit 1
fi

# Use the same factor as ./launch.sh [time_warp]. Remaining arguments retain
# the wrapper's state-directory/Compose semantics.
TIME_WARP="${SITL_SPEEDUP:-1}"
if [[ "${1:-}" =~ ^[0-9]+$ ]]; then
  TIME_WARP="$1"
  shift
fi
if [[ ! "$TIME_WARP" =~ ^[1-9][0-9]*$ ]]; then
  echo "launch_sitl.sh: time warp / SITL_SPEEDUP must be a positive integer." >&2
  exit 1
fi
export SITL_SPEEDUP="$TIME_WARP"
export SITL_PARAM_FILE="$SCRIPT_DIR/sitl.parm"
exec "$SITL_WRAPPER" "$@"
