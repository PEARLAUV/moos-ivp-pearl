#!/bin/bash
#------------------------------------------------------------
#   Script: launch.sh
#  Mission: aug_pearl_uav
#   Author: Charles Benjamin
#------------------------------------------------------------
#  Part 1: Set convenience functions and catch SIGINT.
#------------------------------------------------------------
vecho() { if [ "$VERBOSE" = "yes" ]; then echo "$ME: $1"; fi; }
LAUNCH_GROUPS=()
CLEANUP_ACTIVE="no"
SITL_ACTIVE_PROJECT_FILE=""
cleanup_run() {
  trap - EXIT
  trap '' INT TERM
  if [[ "$CLEANUP_ACTIVE" == yes ]]; then
    echo; echo "$ME: Halting owned mission apps and SITL"
    for group in "${LAUNCH_GROUPS[@]}"; do
      "$MISSION_DIR/launch_sitl.sh" --cleanup-ui-group="$group" || true
      kill -TERM -- "-$group" 2>/dev/null || true
    done
    if [[ -s "$SITL_ACTIVE_PROJECT_FILE" ]]; then
      read -r project < "$SITL_ACTIVE_PROJECT_FILE"
      "$MISSION_DIR/launch_sitl.sh" --cleanup-project="$project" || true
    fi
  fi
  [[ -z "$SITL_ACTIVE_PROJECT_FILE" ]] || rm -f -- "$SITL_ACTIVE_PROJECT_FILE"
}
trap cleanup_run EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
launch_child() {
  if [[ "$JUST_MAKE" == yes ]]; then
    "$@"
  else
    # Give each community its own group, so shutdown does not kill unrelated
    # MOOS processes or the invoking make/terminal process group.
    setsid "$@" <&0 &
    local group=$!
    LAUNCH_GROUPS+=("$group")
    wait "$group"
  fi
}

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
SITE="briggs"
# 42.356424, -71.098393, UTM offsets from the existing mission datum.
PEARL_START="51.033305,-56.215822,0"
PEARL_START_EXPLICIT="no"
SECONDARY_LANDING="10,15"
PEARL_RETURN="10.6,4.8"
MIN_STATE_TIME="0"
DECK_ALTITUDE="1"
TARGET_RANGE="12"
YAW_ALIGNMENT="true"
AP_URL=""
AP_PROTOCOL=""
VIDEO_RECORDING="false"
VIDEO_ARGS=()
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
    echo "  --site=<briggs|charles>       Shared datum/background (default: briggs)"
    echo "  --pearl_start=<X,Y,H>         PEARL simulator start position"
    echo "  --uav_rally=<X,Y>             Initial map-editable UAV rally (default: 10,15)"
    echo "  --pearl_return=<X,Y>          Initial map-editable PEARL return (default: 10.6,4.8)"
    echo "  --secondary_landing=<X,Y>     Alias for --uav_rally"
    echo "  --deck_altitude=<1>     Shared target/deck height, metres MSL"
    echo "  --target_range=<12>     Landing-target 3-D detection range, metres"
    echo "  --min_state_time=<0>          Minimum FSM state dwell, seconds"
    echo "  --yaw-alignment               Require target-relative yaw alignment (default)"
    echo "  --no-yaw-alignment            Explicitly bypass target-relative yaw alignment"
    echo "  --ap_url=<endpoint>           UAV ArduPilot endpoint"
    echo "  --ap_protocol=<proto>         UAV udp, tcp, or serial"
    echo "  --model=<x500>               Drone parameter profile basename (not SITL frame)"
    echo "  --drone-params=<PATH|none>    Override model-selected drone profile"
    echo "  --mode-params=<PATH|none>     Optional UAV mode profile"
    echo "  --mission-params=<PATH|none>  Optional UAV mission profile"
    echo "  --upload-params               REAL: guarded upload before UAV MOOS"
    echo "  --param-system-id=<1> --param-component-id=<1> Native FC identity"
    echo "  --video-recording          Enable physical-UAV recording control in SITL"
    echo "  --no-video-recording       Disable it (default)"
    echo "  --video-api=<HOST:PORT>    Remote MediaMTX API endpoint"
    echo "  --video-record-root=<PATH> Remote MediaMTX recording root"
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
  elif [[ "${ARGI}" == --site=* ]]; then
    SITE="${ARGI#*=}"
  elif [[ "${ARGI}" == --pearl_start=* ]]; then
    PEARL_START="${ARGI#*=}"
    PEARL_START_EXPLICIT="yes"
  elif [[ "${ARGI}" == --secondary_landing=* || "${ARGI}" == --uav_rally=* ]]; then
    SECONDARY_LANDING="${ARGI#*=}"
  elif [[ "${ARGI}" == --pearl_return=* ]]; then
    PEARL_RETURN="${ARGI#*=}"
  elif [[ "${ARGI}" == --deck_altitude=* ]]; then
    DECK_ALTITUDE="${ARGI#*=}"
  elif [[ "${ARGI}" == --target_range=* ]]; then
    TARGET_RANGE="${ARGI#*=}"
  elif [[ "${ARGI}" == --min_state_time=* ]]; then
    MIN_STATE_TIME="${ARGI#*=}"
  elif [[ "${ARGI}" == --yaw-alignment ]]; then
    YAW_ALIGNMENT="true"
  elif [[ "${ARGI}" == --no-yaw-alignment ]]; then
    YAW_ALIGNMENT="false"
  elif [[ "${ARGI}" == --ap_url=* ]]; then
    AP_URL="${ARGI#*=}"
  elif [[ "${ARGI}" == --ap_protocol=* ]]; then
    AP_PROTOCOL="${ARGI#*=}"
  elif [[ "${ARGI}" == --model=* || "${ARGI}" == --drone-params=* || "${ARGI}" == --mode-params=* ||
          "${ARGI}" == --mission-params=* || "${ARGI}" == --upload-params ||
          "${ARGI}" == --param-system-id=* || "${ARGI}" == --param-component-id=* ]]; then
    PARAM_ARGS+=("$ARGI")
  elif [[ "${ARGI}" == --video-recording ]]; then
    VIDEO_RECORDING="true"
    VIDEO_ARGS+=("$ARGI")
  elif [[ "${ARGI}" == --no-video-recording ]]; then
    VIDEO_RECORDING="false"
    VIDEO_ARGS+=("$ARGI")
  elif [[ "${ARGI}" == --video-api=* || "${ARGI}" == --video-record-root=* ]]; then
    VIDEO_ARGS+=("$ARGI")
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
SITE="${SITE,,}"
if [[ "$SITE" != briggs && "$SITE" != charles ]]; then
  echo "$ME: --site must be briggs or charles. Exit Code 1." >&2
  exit 1
fi
if [[ "$SITE" == charles && "$PEARL_START_EXPLICIT" == no ]]; then
  PEARL_START="-4,-22,0"
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

if ! python3 - "$SECONDARY_LANDING" "$PEARL_RETURN" "$PEARL_START" <<'PY'
import math
import sys
for name, spec in zip(("uav_rally", "pearl_return", "pearl_start"), sys.argv[1:]):
    try:
        values = [float(v) for v in spec.split(",")]
        lengths = (2, 3) if name == "pearl_start" else (2,)
        assert len(values) in lengths
        assert all(math.isfinite(v) and abs(v) <= 100000 for v in values)
    except (ValueError, AssertionError):
        sys.exit(f"{name}: require finite local coordinates within 100 km")
PY
then
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
  echo "SITE =              [${SITE}]"
  echo "UAV_MODE =          [${UAV_MODE}]"
  echo "PEARL_MODE =        [${PEARL_MODE}]"
  echo "PEARL_START =       [${PEARL_START}]"
  echo "UAV_RALLY =         [${SECONDARY_LANDING}]"
  echo "PEARL_RETURN =      [${PEARL_RETURN}]"
  echo "SHORE_MPORT =       [${SHORE_MPORT}]"
  echo "UAV_MPORT =         [${UAV_MPORT}]"
  echo "PEARL_MPORT =       [${PEARL_MPORT}]"
  echo "SHORE_PSHARE =      [${SHORE_PSHARE}]"
  echo "UAV_PSHARE =        [${UAV_PSHARE}]"
  echo "PEARL_PSHARE =      [${PEARL_PSHARE}]"
  echo "MIN_STATE_TIME =    [${MIN_STATE_TIME}]"
  echo "YAW_ALIGNMENT =     [${YAW_ALIGNMENT}]"
  echo "VIDEO_RECORDING =   [${VIDEO_RECORDING}]"
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
if [[ "$JUST_MAKE" != yes ]]; then
  command -v setsid >/dev/null || { echo "$ME: setsid is required." >&2; exit 1; }
  SITL_ACTIVE_PROJECT_FILE="$(mktemp "${TMPDIR:-/tmp}/prep-sitl-project.XXXXXX")" || exit 1
  export SITL_ACTIVE_PROJECT_FILE
  CLEANUP_ACTIVE="yes"
fi
UARGS=(--auto "$TIME_WARP" --mode="$UAV_MODE" --site="$SITE" "${PARAM_ARGS[@]}" "${VIDEO_ARGS[@]}")
UARGS+=(--mport="$UAV_MPORT" --pshare="$UAV_PSHARE")
UARGS+=(--shore_pshare="$SHORE_PSHARE")
UARGS+=(--pearl_ip=localhost --pearl_pshare="$PEARL_PSHARE")
UARGS+=(--secondary_landing="$SECONDARY_LANDING" --deck_altitude="$DECK_ALTITUDE" --target_range="$TARGET_RANGE")
UARGS+=(--min_state_time="$MIN_STATE_TIME")
if [[ "$YAW_ALIGNMENT" == true ]]; then
  UARGS+=(--yaw-alignment)
else
  UARGS+=(--no-yaw-alignment)
fi
[ -n "$AP_URL" ] && UARGS+=(--ap_url="$AP_URL")
[ -n "$AP_PROTOCOL" ] && UARGS+=(--ap_protocol="$AP_PROTOCOL")
[ "$JUST_MAKE" = "yes" ] && UARGS+=(--just_make)
[ "$VERBOSE" = "yes" ] && UARGS+=(--verbose)

PARGS=(--auto "$TIME_WARP" --mode="$PEARL_MODE" --site="$SITE")
PARGS+=(--mport="$PEARL_MPORT" --pshare="$PEARL_PSHARE")
PARGS+=(--shore_pshare="$SHORE_PSHARE")
PARGS+=(--start_pos="$PEARL_START" --pearl_return="$PEARL_RETURN")
PARGS+=(--uav_ip=localhost --uav_pshare="$UAV_PSHARE")
PARGS+=(--min_state_time="$MIN_STATE_TIME")
[ "$JUST_MAKE" = "yes" ] && PARGS+=(--just_make)
[ "$VERBOSE" = "yes" ] && PARGS+=(--verbose)

vecho "Launching UAV: ${UARGS[*]}"
launch_child ./launch_uav.sh "${UARGS[@]}" || exit 1
vecho "Launching PEARL: ${PARGS[*]}"
launch_child ./launch_pearl.sh "${PARGS[@]}" || exit 1

#------------------------------------------------------------
#  Part 7: Launch shoreside.
#------------------------------------------------------------
SARGS=(--auto "$TIME_WARP" --mode="$MODE" --site="$SITE")
SARGS+=(--mport="$SHORE_MPORT" --pshare="$SHORE_PSHARE")
SARGS+=(--uav_ip=localhost --uav_pshare="$UAV_PSHARE")
SARGS+=(--pearl_ip=localhost --pearl_pshare="$PEARL_PSHARE")
SARGS+=(--uav_rally="$SECONDARY_LANDING" --pearl_return="$PEARL_RETURN")
[ "$JUST_MAKE" = "yes" ] && SARGS+=(--just_make)
[ "$VERBOSE" = "yes" ] && SARGS+=(--verbose)
[ "$NOGUI" = "yes" ] && SARGS+=(--nogui)

vecho "Launching shoreside: ${SARGS[*]}"
launch_child ./launch_shoreside.sh "${SARGS[@]}" || exit 1

if [ "$JUST_MAKE" = "yes" ]; then
  echo "$ME: Targ files made; exiting without launch."
  exit 0
fi

#------------------------------------------------------------
#  Part 8: Open the single operator uMAC session.
#------------------------------------------------------------
if [ "$XLAUNCHED" != "yes" ]; then
  uMAC --paused targ_shoreside.moos
else
  # Automation explicitly owns the returned background mission lifecycle.
  # Persistent registry markers still let make kill stop its owned SITL.
  CLEANUP_ACTIVE="no"
fi

exit 0
