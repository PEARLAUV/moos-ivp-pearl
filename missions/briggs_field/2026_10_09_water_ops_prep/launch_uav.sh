#!/bin/bash
#------------------------------------------------------------
#   Script: launch_uav.sh
#  Mission: aug_pearl_uav
#   Author: Charles Benjamin
#------------------------------------------------------------
#  Part 1: Set convenience functions and catch SIGINT.
#------------------------------------------------------------
vecho() { if [ "$VERBOSE" = "yes" ]; then echo "$ME: $1"; fi; }
# shellcheck disable=SC2329
on_exit() { echo; echo "$ME: Halting all apps"; kill -- -$$; }
trap on_exit SIGINT

#------------------------------------------------------------
#  Part 2: Set defaults.
#------------------------------------------------------------
ME=$(basename "$0")
MISSION_DIR=$(cd "$(dirname "$0")" && pwd)
UAV_BASE_DIR=$(cd "$MISSION_DIR/../../uav_solo/briggs_test" && pwd)
PEARL_ROOT=$(cd "$MISSION_DIR/../../.." && pwd)
export PATH="$PEARL_ROOT/bin:$PATH"
cd "$MISSION_DIR" || exit 1
CMD_ARGS=""
TIME_WARP=1
VERBOSE="no"
JUST_MAKE="no"
AUTO_LAUNCHED="no"

MODE="SITL"
IS_SIM="true"
IP_ADDR="localhost"
MOOS_PORT="9001"
PSHARE_PORT="9201"
SHORE_IP="localhost"
SHORE_PSHARE="9200"
DIRECT_PEER="false"
PEARL_IP="localhost"
PEARL_PSHARE="9202"
VNAME="uav"
COLOR="yellow"
AP_URL=""
AP_PROTOCOL=""
SECONDARY_LANDING="10,15"
MIN_STATE_TIME="0"
DECK_ALTITUDE="1"
TARGET_RANGE="12"
UPLOAD_PARAMS="no"
PARAM_SYSTEM_ID=1
PARAM_COMPONENT_ID=1

#------------------------------------------------------------
#  Part 3: Parse command-line arguments.
#------------------------------------------------------------
for ARGI; do
  CMD_ARGS+=" ${ARGI}"
  if [[ "${ARGI}" = "--help" || "${ARGI}" = "-h" ]]; then
    echo "$ME [OPTIONS] [time_warp]"
    echo "  --help, -h              Show this help message"
    echo "  --just_make, -j         Only create targ files"
    echo "  --verbose, -v           Verbose launch summary"
    echo "  --auto, -a              Script launch, no uMAC"
    echo "  --mode=<SITL|REAL>      UAV operating mode"
    echo "  --ip=<localhost>        Advertised UAV pShare host"
    echo "  --mport=<9001>          UAV MOOSDB port"
    echo "  --pshare=<9201>         UAV pShare port"
    echo "  --shore=<localhost>     Shoreside host"
    echo "  --shore_pshare=<9200>   Shoreside pShare port"
    echo "  --pearl_ip=<localhost>  Direct PEARL host; also enables peer telemetry"
    echo "  --pearl_pshare=<9202>   Direct PEARL pShare port"
    echo "  --ap_url=<endpoint>     ArduPilot endpoint"
    echo "  --ap_protocol=<proto>   udp, tcp, or serial"
    echo "  --model=<x500>               Drone parameter profile basename (not SITL frame)"
    echo "  --drone-params=<PATH|none>    Override model-selected drone profile"
    echo "  --mode-params=<PATH|none>     Optional mode parameter profile"
    echo "  --mission-params=<PATH|none>  Optional mission parameter profile"
    echo "  --upload-params         REAL only: guarded upload/readback before MOOS"
    echo "  --param-system-id=<1>   Native FC system ID for verified upload"
    echo "  --param-component-id=<1> Native FC component ID for verified upload"
    echo "  --secondary_landing=<X,Y>  Contingency landing point (default: 10,15)"
    echo "  --deck_altitude=<1>     Shared target/deck height, metres MSL"
    echo "  --target_range=<12>     Landing-target 3-D detection range, metres"
    echo "  --min_state_time=<0>       Minimum FSM state dwell, seconds"
    exit 0
  elif [[ "${ARGI}" =~ ^[0-9]+$ && "$TIME_WARP" = 1 ]]; then
    if [ "$ARGI" -lt 1 ]; then
      echo "$ME: time_warp must be at least 1. Exit Code 1."
      exit 1
    fi
    TIME_WARP=$ARGI
  elif [[ "${ARGI}" = "--just_make" || "${ARGI}" = "-j" ]]; then
    JUST_MAKE="yes"
  elif [[ "${ARGI}" = "--verbose" || "${ARGI}" = "-v" ]]; then
    VERBOSE="yes"
  elif [[ "${ARGI}" = "--auto" || "${ARGI}" = "-a" ]]; then
    AUTO_LAUNCHED="yes"
  elif [[ "${ARGI}" == --mode=* ]]; then
    MODE="${ARGI#*=}"
  elif [[ "${ARGI}" == --ip=* ]]; then
    IP_ADDR="${ARGI#*=}"
  elif [[ "${ARGI}" == --mport=* ]]; then
    MOOS_PORT="${ARGI#*=}"
  elif [[ "${ARGI}" == --pshare=* ]]; then
    PSHARE_PORT="${ARGI#*=}"
  elif [[ "${ARGI}" == --shore=* ]]; then
    SHORE_IP="${ARGI#*=}"
  elif [[ "${ARGI}" == --shore_pshare=* ]]; then
    SHORE_PSHARE="${ARGI#*=}"
  elif [[ "${ARGI}" == --pearl_ip=* ]]; then
    PEARL_IP="${ARGI#*=}"
    DIRECT_PEER="true"
  elif [[ "${ARGI}" == --pearl_pshare=* ]]; then
    PEARL_PSHARE="${ARGI#*=}"
  elif [[ "${ARGI}" == --ap_url=* ]]; then
    AP_URL="${ARGI#*=}"
  elif [[ "${ARGI}" == --ap_protocol=* ]]; then
    AP_PROTOCOL="${ARGI#*=}"
  elif [[ "${ARGI}" == --model=* ]]; then
    export DRONE_MODEL="${ARGI#*=}"
  elif [[ "${ARGI}" == --drone-params=* ]]; then
    DRONE_PARAM_FILE="${ARGI#*=}"
  elif [[ "${ARGI}" == --mode-params=* ]]; then
    MODE_PARAM_FILE="${ARGI#*=}"
  elif [[ "${ARGI}" == --mission-params=* ]]; then
    MISSION_PARAM_FILE="${ARGI#*=}"
  elif [[ "${ARGI}" == --upload-params ]]; then
    UPLOAD_PARAMS="yes"
  elif [[ "${ARGI}" == --param-system-id=* ]]; then
    PARAM_SYSTEM_ID="${ARGI#*=}"
  elif [[ "${ARGI}" == --param-component-id=* ]]; then
    PARAM_COMPONENT_ID="${ARGI#*=}"
  elif [[ "${ARGI}" == --secondary_landing=* ]]; then
    SECONDARY_LANDING="${ARGI#*=}"
  elif [[ "${ARGI}" == --deck_altitude=* ]]; then
    DECK_ALTITUDE="${ARGI#*=}"
  elif [[ "${ARGI}" == --target_range=* ]]; then
    TARGET_RANGE="${ARGI#*=}"
  elif [[ "${ARGI}" == --min_state_time=* ]]; then
    MIN_STATE_TIME="${ARGI#*=}"
  else
    echo "$ME: Bad Arg:[$ARGI]. Exit Code 1."
    exit 1
  fi
done

if [[ ! "$MIN_STATE_TIME" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
  echo "$ME: --min_state_time must be a non-negative number. Exit Code 1."
  exit 1
fi

if [[ ! "$SECONDARY_LANDING" =~ ^[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+),[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)$ ]]; then
  echo "$ME: --secondary_landing must be numeric X,Y. Exit Code 1."
  exit 1
fi
SECONDARY_LANDING_X="${SECONDARY_LANDING%%,*}"
SECONDARY_LANDING_Y="${SECONDARY_LANDING#*,}"

if [[ ! "$DECK_ALTITUDE" =~ ^[0-9]+([.][0-9]+)?$ || ! "$TARGET_RANGE" =~ ^[0-9]+([.][0-9]+)?$ ]] ||
   ! awk -v d="$DECK_ALTITUDE" -v r="$TARGET_RANGE" 'BEGIN {exit !(d>=0 && d<=5 && r>0)}'; then
  echo "$ME: require deck_altitude in [0,5] m MSL and target_range > 0." >&2
  exit 1
fi
MODE=$(echo "$MODE" | tr '[:lower:]' '[:upper:]')
SITL_ROUTER_PORT="${SITL_TARGET_PORT:-5763}"
if [ "$MODE" = "SITL" ]; then
  if [[ ! "$SITL_ROUTER_PORT" =~ ^[0-9]{1,5}$ ]] ||
     (( 10#$SITL_ROUTER_PORT < 1 || 10#$SITL_ROUTER_PORT > 65535 )); then
    echo "$ME: invalid SITL_TARGET_PORT" >&2
    exit 1
  fi
  SITL_ROUTER_PORT="$((10#$SITL_ROUTER_PORT))"
  IS_SIM="true"
elif [ "$MODE" = "REAL" ]; then
  IS_SIM="false"
else
  echo "$ME: --mode must be SITL or REAL. Exit Code 1."
  exit 1
fi

if [ -z "$AP_URL" ]; then
  if [ "$MODE" = "SITL" ]; then
    AP_URL="127.0.0.1:${SITL_PORT:-5760}"
  elif [ "$MODE" = "REAL" ]; then
    AP_URL="ttyACM0:115200"
  fi
fi
if [ -z "$AP_PROTOCOL" ]; then
  if [ "$MODE" = "SITL" ]; then
    AP_PROTOCOL="tcp"
  elif [ "$MODE" = "REAL" ]; then
    AP_PROTOCOL="serial"
  fi
fi

# Profiles are optional; explicit missing paths/invalid files are errors.
source "$PEARL_ROOT/scripts/ardupilot_param_stack.sh"
ardupilot_parameter_stack "$PEARL_ROOT" "$MISSION_DIR" "$MODE" || exit 1
python3 "$PEARL_ROOT/scripts/ardupilot_params.py" "${ARDUPILOT_PARAM_ARGS[@]}" > /dev/null || exit 1
if [[ "$UPLOAD_PARAMS" = yes && "$MODE" != REAL ]]; then
  echo "$ME: --upload-params is REAL-only; SITL uses startup defaults." >&2
  exit 1
fi

#------------------------------------------------------------
#  Part 4: Show launch summary.
#------------------------------------------------------------
if [ "$VERBOSE" = "yes" ]; then
  echo "============================================"
  echo "  $ME SUMMARY"
  echo "============================================"
  echo "CMD_ARGS =          [${CMD_ARGS}]"
  echo "MODE =              [${MODE}]"
  echo "TIME_WARP =         [${TIME_WARP}]"
  echo "IP_ADDR =           [${IP_ADDR}]"
  echo "MOOS_PORT =         [${MOOS_PORT}]"
  echo "PSHARE_PORT =       [${PSHARE_PORT}]"
  echo "SHORE_IP =          [${SHORE_IP}]"
  echo "SHORE_PSHARE =      [${SHORE_PSHARE}]"
  echo "DIRECT_PEER =       [${DIRECT_PEER}]"
  echo "PEARL_IP =          [${PEARL_IP}]"
  echo "PEARL_PSHARE =      [${PEARL_PSHARE}]"
  echo "AP_URL =            [${AP_URL}]"
  echo "AP_PROTOCOL =       [${AP_PROTOCOL}]"
  echo "SECONDARY_LANDING = [${SECONDARY_LANDING}]"
  echo "MIN_STATE_TIME =    [${MIN_STATE_TIME}]"
  echo "DRONE_MODEL =       [${DRONE_MODEL}]"
  echo "PARAMETER_STACK =   [${ARDUPILOT_PARAM_ARGS[*]}]"
  echo "UPLOAD_PARAMS =     [${UPLOAD_PARAMS}]"
fi

#------------------------------------------------------------
#  Part 5: Regenerate the UAV FSM, then build targets from the field stack.
#------------------------------------------------------------
if ! "$PEARL_ROOT/scripts/generate_fsm.py" "$MISSION_DIR/uav_fsm.toml"; then
  echo "$ME: Failed to generate UAV FSM artifacts; not launching." >&2
  exit 1
fi

NSFLAGS=(--strict --force -x "--path=$MISSION_DIR:$UAV_BASE_DIR")

nsplug meta_uav.moos "targ_${VNAME}.moos" "${NSFLAGS[@]}" \
  WARP="$TIME_WARP" IP_ADDR="$IP_ADDR" MOOS_PORT="$MOOS_PORT" \
  PSHARE_PORT="$PSHARE_PORT" SHORE_IP="$SHORE_IP" \
  SHORE_PSHARE="$SHORE_PSHARE" DIRECT_PEER="$DIRECT_PEER" \
  PEARL_IP="$PEARL_IP" PEARL_PSHARE="$PEARL_PSHARE" \
  VNAME="$VNAME" COLOR="$COLOR" \
  PLATFORM_LENGTH="1.5" \
  AP_URL="$AP_URL" AP_PROTOCOL="$AP_PROTOCOL" \
  SECONDARY_LANDING_X="$SECONDARY_LANDING_X" \
  SECONDARY_LANDING_Y="$SECONDARY_LANDING_Y" \
  MIN_STATE_TIME="$MIN_STATE_TIME" \
  XMODE="$MODE" IS_SIM="$IS_SIM" \
  SITL_ROUTER_PORT="$SITL_ROUTER_PORT" DECK_ALTITUDE="$DECK_ALTITUDE" TARGET_RANGE="$TARGET_RANGE" \
  PEARL_LIB_DIR="$PEARL_ROOT/lib" || exit 1

nsplug meta_uav.bhv "targ_${VNAME}.bhv" "${NSFLAGS[@]}" \
  VNAME="$VNAME" SECONDARY_LANDING_X="$SECONDARY_LANDING_X" SECONDARY_LANDING_Y="$SECONDARY_LANDING_Y" || exit 1

if [ "$JUST_MAKE" = "yes" ]; then
  echo "$ME: Targ files made; exiting without launch."
  exit 0
fi

# REAL upload is opt-in, before the bridge/Helm can own this endpoint.
# No flight commands, simulated facts, automatic reboot or rollback.
if [[ "$MODE" = REAL && "$UPLOAD_PARAMS" = yes ]]; then
  PARAM_LOG_DIR="${PARAM_LOG_DIR:-$MISSION_DIR/LOG_params}"
  mkdir -p -- "$PARAM_LOG_DIR" || exit 1
  PARAM_REPORT="$PARAM_LOG_DIR/upload-$(date -u +%Y%m%dT%H%M%SZ)-$$.json"
  echo "$ME: Guarded REAL parameter upload; audit: $PARAM_REPORT"
  if ! python3 "$PEARL_ROOT/scripts/ardupilot_params.py" \
      "${ARDUPILOT_PARAM_ARGS[@]}" --apply --protocol "$AP_PROTOCOL" \
      --endpoint "$AP_URL" --system-id "$PARAM_SYSTEM_ID" \
      --component-id "$PARAM_COMPONENT_ID" --report "$PARAM_REPORT"; then
    echo "$ME: Parameters failed verification or require operator reboot; not launching MOOS." >&2
    exit 1
  fi
elif [[ "$MODE" = REAL ]]; then
  echo "$ME: REAL parameter upload not requested; configuration is NOT verified by this launch." >&2
fi

#------------------------------------------------------------
#  Part 6: Start SITL and wait before launching any UAV MOOS processes.
#------------------------------------------------------------
cleanup_sitl() {
  trap - EXIT INT TERM
  if [ -n "$ANTLER_PID" ]; then
    kill -TERM "$ANTLER_PID" 2>/dev/null || true
  fi
  timeout 5 "$MISSION_DIR/launch_sitl.sh" "$TIME_WARP" logs --no-color \
    >> "$SITL_LOG_FILE" 2>&1 || true
  timeout 15 "$MISSION_DIR/launch_sitl.sh" "$TIME_WARP" down --timeout 5 \
    >> "$SITL_LOG_FILE" 2>&1 || true
}

if [ "$MODE" = "SITL" ]; then
  STARTUP_TIMEOUT="${SITL_STARTUP_TIMEOUT:-60}"
  CONTROLLER_PORT="${SITL_PORT:-5760}"
  if [[ ! "$STARTUP_TIMEOUT" =~ ^[1-9][0-9]*$ || ! "$CONTROLLER_PORT" =~ ^[0-9]{1,5}$ ]] ||
     (( 10#$CONTROLLER_PORT < 1 || 10#$CONTROLLER_PORT > 65535 )); then
    echo "$ME: invalid SITL_STARTUP_TIMEOUT or SITL_PORT" >&2
    exit 1
  fi
  CONTROLLER_PORT="$((10#$CONTROLLER_PORT))"
  # Never adopt or stop a manually launched simulator on the same ports.
  COMPOSE_PROJECT_NAME="moos-sitl-$(id -u)-$$"
  export COMPOSE_PROJECT_NAME
  SITL_LOG_DIR="${SITL_LOG_DIR:-$MISSION_DIR/LOG_sitl}"
  mkdir -p -- "$SITL_LOG_DIR" || exit 1
  SITL_LOG_FILE="$SITL_LOG_DIR/$COMPOSE_PROJECT_NAME.log"
  ANTLER_PID=""
  trap cleanup_sitl EXIT
  trap 'exit 143' TERM
  DEADLINE=$((SECONDS + STARTUP_TIMEOUT))
  echo "$ME: Starting SITL with speedup $TIME_WARP; log: $SITL_LOG_FILE"
  if ! timeout "$STARTUP_TIMEOUT" "$MISSION_DIR/launch_sitl.sh" "$TIME_WARP" \
      up -d --no-build > "$SITL_LOG_FILE" 2>&1; then
    echo "$ME: SITL startup failed; see $SITL_LOG_FILE" >&2
    exit 1
  fi
  while true; do
    # Ownership matters: an unrelated listener must not release this launcher.
    RUNNING_ID="$(timeout 2 "$MISSION_DIR/launch_sitl.sh" "$TIME_WARP" \
      ps --status running --quiet sitl 2>> "$SITL_LOG_FILE" || true)"
    ROUTER_ID="$(timeout 2 "$MISSION_DIR/launch_sitl.sh" "$TIME_WARP" \
      ps --status running --quiet sim_router 2>> "$SITL_LOG_FILE" || true)"
    if [[ -n "$RUNNING_ID" && -n "$ROUTER_ID" ]] &&
       timeout 1 bash -c 'exec 3<>/dev/tcp/127.0.0.1/"$1"' _ "$CONTROLLER_PORT" 2>/dev/null &&
       timeout 1 bash -c 'exec 3<>/dev/tcp/127.0.0.1/"$1"' _ "$SITL_ROUTER_PORT" 2>/dev/null; then
      break
    fi
    if (( SECONDS >= DEADLINE )); then
      echo "$ME: SITL controller/router startup timed out; see $SITL_LOG_FILE" >&2
      exit 1
    fi
    sleep 0.2
  done
  echo "$ME: SITL controller and simulation router ready."
fi

#------------------------------------------------------------
#  Part 7: Launch the UAV community normally.
#------------------------------------------------------------
echo "Launching $VNAME MOOS Community. MODE=$MODE WARP=$TIME_WARP"
pAntler "targ_${VNAME}.moos" >& /dev/null &
ANTLER_PID=$!
if [ "$MODE" = "SITL" ]; then
  # --auto returns to launch.sh. Keep a cleanup watcher in its process group
  # so normal mission shutdown (or ANTLER exit) also removes the owned simulator.
  (
    trap cleanup_sitl EXIT
    trap 'exit 0' INT TERM
    while kill -0 "$ANTLER_PID" 2>/dev/null; do sleep 0.5; done
  ) </dev/null >/dev/null 2>&1 &
  trap - EXIT TERM
fi
echo "Done Launching $VNAME MOOS Community"

#------------------------------------------------------------
#  Part 8: Return to a calling launcher or open uMAC.
#------------------------------------------------------------
if [ "$AUTO_LAUNCHED" = "yes" ]; then
  exit 0
fi

uMAC "targ_${VNAME}.moos"
trap "" SIGINT
kill -- -$$
