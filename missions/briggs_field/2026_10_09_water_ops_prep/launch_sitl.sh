#!/usr/bin/env bash
# Mission-specific parameter file, shared ArduPilot SITL launcher.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
PEARL_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd -P)"
# Optional drone -> SITL mode -> mission layers; caller overlays follow them.
source "$PEARL_ROOT/scripts/ardupilot_param_stack.sh"
ardupilot_parameter_stack "$PEARL_ROOT" "$SCRIPT_DIR" SITL
exec "$PEARL_ROOT/scripts/launch_ardupilot_sitl.sh" \
  --no-rc "${ARDUPILOT_PARAM_ARGS[@]}" "$@"
