#!/bin/bash
#------------------------------------------------------------
#   Script: launch.sh
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
#  Part 2: Set defaults. The top-level launcher is local-only;
#          split-host operation uses the three sublaunchers.
#------------------------------------------------------------
ME=$(basename "$0")
MISSION_DIR=$(cd "$(dirname "$0")" && pwd)
cd "$MISSION_DIR" || exit 1
CMD_ARGS=""
TIME_WARP=1
VERBOSE="no"
JUST_MAKE="no"
LOG_CLEAN="no"
XLAUNCHED="no"
NOGUI="no"

MODE="SITL"
# 42.356424, -71.098393, UTM offsets from the existing mission datum.
PEARL_START="51.033305,-56.215822,0"
SECONDARY_LANDING="10,15"
MIN_STATE_TIME="0"
DECK_ALTITUDE="1"
TARGET_RANGE="12"
AP_URL=""
AP_PROTOCOL=""
PARAM_ARGS=()

SHORE_MPORT="9000"
UAV_MPORT="9001"
PEARL_MPORT="9002"
SHORE_PSHARE="9200"
UAV_PSHARE="9201"
PEARL_PSHARE="9202"

#------------------------------------------------------------
#  Part 3: Parse command-line arguments.
#------------------------------------------------------------
for ARGI; do
  CMD_ARGS+=" ${ARGI}"
  if [[ "${ARGI}" = "--help" || "${ARGI}" = "-h" ]]; then
    echo "$ME [OPTIONS] [time_warp]"
    echo
    echo "Modes:"
    echo "  --mode=<SITL>                 Local all-community launcher (default: SITL)"
    echo
    echo "Options:"
    echo "  --help, -h                    Show this help message"
    echo "  --verbose, -v                 Verbose launch summary"
    echo "  --just_make, -j               Only create targ files"
    echo "  --log_clean, -lc              Run clean.sh before launch"
    echo "  --xlaunched, -x               Launched by automation"
    echo "  --nogui, -ng                  No pMarineViewer"
    echo "  --pearl_start=<X,Y,H>         PEARL simulator start position"
    echo "  --secondary_landing=<X,Y>     Contingency landing point (default: 10,15)"
    echo "  --deck_altitude=<1>     Shared target/deck height, metres MSL"
    echo "  --target_range=<12>     Landing-target 3-D detection range, metres"
    echo "  --min_state_time=<0>          Minimum FSM state dwell, seconds"
    echo "  --ap_url=<endpoint>           UAV ArduPilot endpoint"
    echo "  --ap_protocol=<proto>         UAV udp, tcp, or serial"
    echo "  --model=<x500>               Drone parameter profile basename (not SITL frame)"
    echo "  --drone-params=<PATH|none>    Override model-selected drone profile"
    echo "  --mode-params=<PATH|none>     Optional UAV mode profile"
    echo "  --mission-params=<PATH|none>  Optional UAV mission profile"
    echo "  --upload-params               REAL: guarded upload before UAV MOOS"
    echo "  --param-system-id=<1> --param-component-id=<1> Native FC identity"
    echo
    echo "Ports:"
    echo "  --shore_mport=<9000>          Shoreside MOOSDB port"
    echo "  --uav_mport=<9001>            UAV MOOSDB port"
    echo "  --pearl_mport=<9002>          PEARL MOOSDB port"
    echo "  --shore_pshare=<9200>         Shoreside pShare port"
    echo "  --uav_pshare=<9201>           UAV pShare port"
    echo "  --pearl_pshare=<9202>         PEARL pShare port"
    exit 0
  elif [[ "${ARGI}" =~ ^[0-9]+$ && "$TIME_WARP" = 1 ]]; then
    if [ "$ARGI" -lt 1 ]; then
      echo "$ME: time_warp must be at least 1. Exit Code 1."
      exit 1
    fi
    TIME_WARP=$ARGI
  elif [[ "${ARGI}" = "--verbose" || "${ARGI}" = "-v" ]]; then
    VERBOSE="yes"
  elif [[ "${ARGI}" = "--just_make" || "${ARGI}" = "-j" ]]; then
    JUST_MAKE="yes"
  elif [[ "${ARGI}" = "--log_clean" || "${ARGI}" = "-lc" ]]; then
    LOG_CLEAN="yes"
  elif [[ "${ARGI}" = "--xlaunched" || "${ARGI}" = "-x" ]]; then
    XLAUNCHED="yes"
  elif [[ "${ARGI}" = "--nogui" || "${ARGI}" = "-ng" ]]; then
    NOGUI="yes"
  elif [[ "${ARGI}" == --mode=* ]]; then
    MODE="${ARGI#*=}"
  elif [[ "${ARGI}" == --pearl_start=* ]]; then
    PEARL_START="${ARGI#*=}"
  elif [[ "${ARGI}" == --secondary_landing=* ]]; then
    SECONDARY_LANDING="${ARGI#*=}"
  elif [[ "${ARGI}" == --deck_altitude=* ]]; then
    DECK_ALTITUDE="${ARGI#*=}"
  elif [[ "${ARGI}" == --target_range=* ]]; then
    TARGET_RANGE="${ARGI#*=}"
  elif [[ "${ARGI}" == --min_state_time=* ]]; then
    MIN_STATE_TIME="${ARGI#*=}"
  elif [[ "${ARGI}" == --ap_url=* ]]; then
    AP_URL="${ARGI#*=}"
  elif [[ "${ARGI}" == --ap_protocol=* ]]; then
    AP_PROTOCOL="${ARGI#*=}"
  elif [[ "${ARGI}" == --model=* || "${ARGI}" == --drone-params=* || "${ARGI}" == --mode-params=* ||
          "${ARGI}" == --mission-params=* || "${ARGI}" == --upload-params ||
          "${ARGI}" == --param-system-id=* || "${ARGI}" == --param-component-id=* ]]; then
    PARAM_ARGS+=("$ARGI")
  elif [[ "${ARGI}" == --shore_mport=* ]]; then
    SHORE_MPORT="${ARGI#*=}"
  elif [[ "${ARGI}" == --uav_mport=* ]]; then
    UAV_MPORT="${ARGI#*=}"
  elif [[ "${ARGI}" == --pearl_mport=* ]]; then
    PEARL_MPORT="${ARGI#*=}"
  elif [[ "${ARGI}" == --shore_pshare=* ]]; then
    SHORE_PSHARE="${ARGI#*=}"
  elif [[ "${ARGI}" == --uav_pshare=* ]]; then
    UAV_PSHARE="${ARGI#*=}"
  elif [[ "${ARGI}" == --pearl_pshare=* ]]; then
    PEARL_PSHARE="${ARGI#*=}"
  else
    echo "$ME: Bad Arg:[$ARGI]. Exit Code 1."
    exit 1
  fi
done

if [[ ! "$DECK_ALTITUDE" =~ ^[0-9]+([.][0-9]+)?$ || ! "$TARGET_RANGE" =~ ^[0-9]+([.][0-9]+)?$ ]] ||
   ! awk -v d="$DECK_ALTITUDE" -v r="$TARGET_RANGE" 'BEGIN {exit !(d>=0 && d<=5 && r>0)}'; then
  echo "$ME: require deck_altitude in [0,5] m MSL and target_range > 0." >&2
  exit 1
fi
MODE=$(echo "$MODE" | tr '[:lower:]' '[:upper:]')
if [[ "$MODE" != "SITL" ]]; then
  echo "$ME: launch.sh is SITL-only; use the split launchers for REAL or GRASS. Exit Code 1."
  exit 1
fi
if [[ ! "$MIN_STATE_TIME" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
  echo "$ME: --min_state_time must be a non-negative number. Exit Code 1."
  exit 1
fi

if [[ ! "$SECONDARY_LANDING" =~ ^[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+),[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)$ ]]; then
  echo "$ME: --secondary_landing must be numeric X,Y. Exit Code 1."
  exit 1
fi

UAV_MODE="SITL"
PEARL_MODE="SIM"

#------------------------------------------------------------
#  Part 4: Show launch summary.
#------------------------------------------------------------
if [ "$VERBOSE" = "yes" ]; then
  echo "============================================"
  echo "  $ME SUMMARY"
  echo "============================================"
  echo "CMD_ARGS =          [${CMD_ARGS}]"
  echo "TIME_WARP =         [${TIME_WARP}]"
  echo "JUST_MAKE =         [${JUST_MAKE}]"
  echo "MODE =              [${MODE}]"
  echo "UAV_MODE =          [${UAV_MODE}]"
  echo "PEARL_MODE =        [${PEARL_MODE}]"
  echo "PEARL_START =       [${PEARL_START}]"
  echo "SECONDARY_LANDING = [${SECONDARY_LANDING}]"
  echo "SHORE_MPORT =       [${SHORE_MPORT}]"
  echo "UAV_MPORT =         [${UAV_MPORT}]"
  echo "PEARL_MPORT =       [${PEARL_MPORT}]"
  echo "SHORE_PSHARE =      [${SHORE_PSHARE}]"
  echo "UAV_PSHARE =        [${UAV_PSHARE}]"
  echo "PEARL_PSHARE =      [${PEARL_PSHARE}]"
  echo "MIN_STATE_TIME =    [${MIN_STATE_TIME}]"
  echo -n "Hit any key to continue launch "
  read -r _
fi

#------------------------------------------------------------
#  Part 5: Optionally clean generated files.
#------------------------------------------------------------
if [[ "$LOG_CLEAN" = "yes" && -f clean.sh ]]; then
  ./clean.sh
fi

#------------------------------------------------------------
#  Part 6: Launch the two vehicle communities.
#------------------------------------------------------------
UARGS=(--auto "$TIME_WARP" --mode="$UAV_MODE" "${PARAM_ARGS[@]}")
UARGS+=(--mport="$UAV_MPORT" --pshare="$UAV_PSHARE")
UARGS+=(--shore_pshare="$SHORE_PSHARE")
UARGS+=(--pearl_ip=localhost --pearl_pshare="$PEARL_PSHARE")
UARGS+=(--secondary_landing="$SECONDARY_LANDING" --deck_altitude="$DECK_ALTITUDE" --target_range="$TARGET_RANGE")
UARGS+=(--min_state_time="$MIN_STATE_TIME")
[ -n "$AP_URL" ] && UARGS+=(--ap_url="$AP_URL")
[ -n "$AP_PROTOCOL" ] && UARGS+=(--ap_protocol="$AP_PROTOCOL")
[ "$JUST_MAKE" = "yes" ] && UARGS+=(--just_make)
[ "$VERBOSE" = "yes" ] && UARGS+=(--verbose)

PARGS=(--auto "$TIME_WARP" --mode="$PEARL_MODE")
PARGS+=(--mport="$PEARL_MPORT" --pshare="$PEARL_PSHARE")
PARGS+=(--shore_pshare="$SHORE_PSHARE")
PARGS+=(--start_pos="$PEARL_START")
PARGS+=(--uav_ip=localhost --uav_pshare="$UAV_PSHARE")
PARGS+=(--min_state_time="$MIN_STATE_TIME")
[ "$JUST_MAKE" = "yes" ] && PARGS+=(--just_make)
[ "$VERBOSE" = "yes" ] && PARGS+=(--verbose)

vecho "Launching UAV: ${UARGS[*]}"
./launch_uav.sh "${UARGS[@]}" || exit 1
vecho "Launching PEARL: ${PARGS[*]}"
./launch_pearl.sh "${PARGS[@]}" || exit 1

#------------------------------------------------------------
#  Part 7: Launch shoreside.
#------------------------------------------------------------
SARGS=(--auto "$TIME_WARP" --mode="$MODE")
SARGS+=(--mport="$SHORE_MPORT" --pshare="$SHORE_PSHARE")
SARGS+=(--uav_ip=localhost --uav_pshare="$UAV_PSHARE")
SARGS+=(--pearl_ip=localhost --pearl_pshare="$PEARL_PSHARE")
SARGS+=(--secondary_landing="$SECONDARY_LANDING")
[ "$JUST_MAKE" = "yes" ] && SARGS+=(--just_make)
[ "$VERBOSE" = "yes" ] && SARGS+=(--verbose)
[ "$NOGUI" = "yes" ] && SARGS+=(--nogui)

vecho "Launching shoreside: ${SARGS[*]}"
./launch_shoreside.sh "${SARGS[@]}" || exit 1

if [ "$JUST_MAKE" = "yes" ]; then
  echo "$ME: Targ files made; exiting without launch."
  exit 0
fi

#------------------------------------------------------------
#  Part 8: Open the single operator uMAC session.
#------------------------------------------------------------
if [ "$XLAUNCHED" != "yes" ]; then
  uMAC --paused targ_shoreside.moos
  trap "" SIGINT
  echo; echo "$ME: Halting all apps"
  kill -- -$$
fi

exit 0
