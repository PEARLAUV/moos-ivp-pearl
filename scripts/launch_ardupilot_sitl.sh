#!/usr/bin/env bash
# Shared launcher for the containerized ArduPilot SITL wrapper.
set -euo pipefail

usage() {
  cat <<EOF
Usage: $0 [--param-file PATH] [--no-rc] [--home LOCATION] [--speedup FACTOR]
          [--port ENDPOINT] [--diagnostic-port ENDPOINT] [--sim-port ENDPOINT]
          [time_warp] [STATE_DIRECTORY] [COMPOSE_COMMAND ...]
Passes mission and runtime options to the ardupilot-sitl wrapper by CLI flags.
Use SITL_WRAPPER to select a non-default wrapper executable.
EOF
}

param_file=""
wrapper_options=()
speedup_set=false
while (($#)); do
  case "$1" in
    --param-file)
      if (($# < 2)); then
        echo "$0: --param-file requires a path." >&2
        usage >&2
        exit 2
      fi
      param_file=$2
      shift 2
      ;;
    --param-file=*)
      param_file=${1#*=}
      shift
      ;;
    --no-rc)
      wrapper_options+=(--no-rc)
      shift
      ;;
    --target-port)
      # Compatibility with missions using the former simulation-port name.
      if (($# < 2)); then
        echo "$0: --target-port requires a value." >&2
        exit 2
      fi
      wrapper_options+=(--sim-port "$2")
      shift 2
      ;;
    --home|--speedup|--port|--diagnostic-port|--sim-port)
      if (($# < 2)); then
        echo "$0: $1 requires a value." >&2
        usage >&2
        exit 2
      fi
      [[ "$1" == "--speedup" ]] && speedup_set=true
      wrapper_options+=("$1" "$2")
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      break
      ;;
  esac
done

if [[ -n "$param_file" ]]; then
  if [[ ! -f "$param_file" ]]; then
    echo "$0: parameter file not found: $param_file" >&2
    exit 1
  fi
  param_file="$(cd -- "$(dirname -- "$param_file")" && pwd -P)/$(basename -- "$param_file")"
fi

SITL_WRAPPER="${SITL_WRAPPER:-$HOME/workspace/mit/pearl/ardupilot-sitl/bin/ardupilot-sitl}"
if [[ ! -x "$SITL_WRAPPER" ]]; then
  echo "$0: wrapper not found or not executable: $SITL_WRAPPER" >&2
  exit 1
fi

# Keep the existing positional time-warp form as a compatibility shorthand,
# but translate it to the wrapper's explicit --speedup option.
if [[ "${1:-}" =~ ^[0-9]+$ ]]; then
  if [[ "$speedup_set" == true || ! "$1" =~ ^[1-9][0-9]*$ ]]; then
    echo "$0: positional time_warp must be positive and cannot accompany --speedup." >&2
    exit 2
  fi
  wrapper_options+=(--speedup "$1")
  shift
fi

wrapper_args=()
if [[ -n "$param_file" ]]; then
  wrapper_args+=(--param-file "$param_file")
fi
exec "$SITL_WRAPPER" "${wrapper_args[@]}" "${wrapper_options[@]}" "$@"
