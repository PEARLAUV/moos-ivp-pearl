#!/bin/bash
#------------------------------------------------------------
#   Script: launch.sh
#  Mission: pearl_uav_integration_tests/rendezvous
#
#  PEARL (simulated) and the UAV (ArduPilot SITL) start on parallel N/S
#  lanes travelling in opposite directions. DEPLOY independently starts
#  pRendezvous on both vehicles; each vehicle completes at its own point.
#------------------------------------------------------------
ME=$(basename "$0")
MISSION_DIR=$(cd "$(dirname "$0")" && pwd)
cd "$MISSION_DIR" || exit 1

TIME_WARP=1
JUST_MAKE="no"
XLAUNCHED="no"
LAUNCH_GUI="yes"

# Lanes run north/south. PEARL starts at the south end heading north; the UAV
# takes off at the north end of its lane and heads south.
PEARL_LANE_X="-15"
UAV_LANE_X="15"
LANE_NORTH_Y="-40"
LANE_SOUTH_Y="-190"
PEARL_SPEED="0.5"
UAV_SPEED="1.5"
ALTITUDE="8"
SHORE_MPORT="9000"
UAV_MPORT="9001"
PEARL_MPORT="9002"
SHORE_PSHARE="9200"
UAV_PSHARE="9201"
PEARL_PSHARE="9202"
SITL_PORT="${SITL_PORT:-5760}"

for ARGI; do
  if [[ "${ARGI}" = "--help" || "${ARGI}" = "-h" ]]; then
    echo "$ME [OPTIONS] [time_warp]"
    echo
    echo "Options:"
    echo "  --help, -h              Show this help message"
    echo "  --just_make, -j         Only create targ files"
    echo "  --xlaunched, -x         No uMAC; block until SIGINT/SIGTERM"
    echo "  --nogui, -ng            No pMarineViewer"
    echo
    echo "Geometry (local metres):"
    echo "  --pearl_x=<-15>         PEARL lane x"
    echo "  --uav_x=<15>            UAV lane x"
    echo "  --north=<-40>           Lane north end y (UAV start)"
    echo "  --south=<-190>          Lane south end y (PEARL start)"
    echo "  --pearl_speed=<0.5>     PEARL lane speed, m/s (max 0.5)"
    echo "  --uav_speed=<1.5>       UAV lane speed, m/s"
    echo
    echo "Ports:"
    echo "  --shore_mport=<9000>  --uav_mport=<9001>  --pearl_mport=<9002>"
    echo "  --shore_pshare=<9200> --uav_pshare=<9201> --pearl_pshare=<9202>"
    echo "  --sitl_port=<5760>      SITL controller TCP port"
    exit 0
  elif [[ "${ARGI}" =~ ^[0-9]+$ && "$TIME_WARP" = 1 ]]; then
    TIME_WARP=$ARGI
  elif [[ "${ARGI}" = "--just_make" || "${ARGI}" = "-j" ]]; then
    JUST_MAKE="yes"
  elif [[ "${ARGI}" = "--xlaunched" || "${ARGI}" = "-x" ]]; then
    XLAUNCHED="yes"
  elif [[ "${ARGI}" = "--nogui" || "${ARGI}" = "-ng" ]]; then
    LAUNCH_GUI="no"
  elif [[ "${ARGI}" == --pearl_x=* ]]; then
    PEARL_LANE_X="${ARGI#*=}"
  elif [[ "${ARGI}" == --uav_x=* ]]; then
    UAV_LANE_X="${ARGI#*=}"
  elif [[ "${ARGI}" == --north=* ]]; then
    LANE_NORTH_Y="${ARGI#*=}"
  elif [[ "${ARGI}" == --south=* ]]; then
    LANE_SOUTH_Y="${ARGI#*=}"
  elif [[ "${ARGI}" == --pearl_speed=* ]]; then
    PEARL_SPEED="${ARGI#*=}"
  elif [[ "${ARGI}" == --uav_speed=* ]]; then
    UAV_SPEED="${ARGI#*=}"
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
  elif [[ "${ARGI}" == --sitl_port=* ]]; then
    SITL_PORT="${ARGI#*=}"
  else
    echo "$ME: Bad Arg:[$ARGI]. Exit Code 1."
    exit 1
  fi
done

NUMBER='^[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)$'
for VALUE in "$PEARL_LANE_X" "$UAV_LANE_X" "$LANE_NORTH_Y" "$LANE_SOUTH_Y" \
             "$PEARL_SPEED" "$UAV_SPEED"; do
  if [[ ! "$VALUE" =~ $NUMBER ]]; then
    echo "$ME: geometry and speeds must be numeric, got [$VALUE]. Exit Code 1."
    exit 1
  fi
done
if [ "$TIME_WARP" -lt 1 ]; then
  echo "$ME: time_warp must be at least 1. Exit Code 1."
  exit 1
fi

#------------------------------------------------------------
#  Generate targets.
#------------------------------------------------------------
NSFLAGS=(--strict --force)

nsplug meta_uav.moos targ_uav.moos "${NSFLAGS[@]}" \
  WARP="$TIME_WARP" MOOS_PORT="$UAV_MPORT" PSHARE_PORT="$UAV_PSHARE" \
  SHORE_PSHARE="$SHORE_PSHARE" PEARL_PSHARE="$PEARL_PSHARE" \
  SITL_PORT="$SITL_PORT" ALTITUDE="$ALTITUDE" \
  LANE_START_ALTITUDE="$(awk -v a="$ALTITUDE" 'BEGIN { print a - 1 }')" || exit 1
nsplug meta_uav.bhv targ_uav.bhv "${NSFLAGS[@]}" \
  ALTITUDE="$ALTITUDE" UAV_SPEED="$UAV_SPEED" \
  UAV_LANE_X="$UAV_LANE_X" LANE_END_Y="$LANE_SOUTH_Y" || exit 1

nsplug meta_pearl.moos targ_pearl.moos "${NSFLAGS[@]}" \
  WARP="$TIME_WARP" MOOS_PORT="$PEARL_MPORT" PSHARE_PORT="$PEARL_PSHARE" \
  SHORE_PSHARE="$SHORE_PSHARE" UAV_PSHARE="$UAV_PSHARE" \
  PEARL_START="$PEARL_LANE_X,$LANE_SOUTH_Y,0" || exit 1
nsplug meta_pearl.bhv targ_pearl.bhv "${NSFLAGS[@]}" \
  PEARL_SPEED="$PEARL_SPEED" \
  PEARL_LANE_X="$PEARL_LANE_X" LANE_END_Y="$LANE_NORTH_Y" || exit 1

nsplug meta_shoreside.moos targ_shoreside.moos "${NSFLAGS[@]}" \
  WARP="$TIME_WARP" MOOS_PORT="$SHORE_MPORT" PSHARE_PORT="$SHORE_PSHARE" \
  LAUNCH_GUI="$LAUNCH_GUI" || exit 1

if [ "$JUST_MAKE" = "yes" ]; then
  echo "$ME: Targ files made; exiting without launch."
  exit 0
fi

#------------------------------------------------------------
#  Own the simulator and communities; tear everything down on exit.
#------------------------------------------------------------
ANTLER_PIDS=()
cleanup() {
  trap - EXIT INT TERM
  echo; echo "$ME: Halting all apps"
  # MOOS apps (notably pLogger) exit on SIGINT, not SIGTERM; pAntler exits
  # once its children have.
  local PID WAITED=0
  for PID in "${ANTLER_PIDS[@]}"; do
    pkill -INT -P "$PID" 2>/dev/null
    kill -INT "$PID" 2>/dev/null
  done
  while (( WAITED < 50 )); do
    local ALIVE=no
    for PID in "${ANTLER_PIDS[@]}"; do
      kill -0 "$PID" 2>/dev/null && ALIVE=yes
    done
    [ "$ALIVE" = "no" ] && break
    sleep 0.1
    WAITED=$((WAITED + 1))
  done
  for PID in "${ANTLER_PIDS[@]}"; do
    pkill -KILL -P "$PID" 2>/dev/null
    kill -KILL "$PID" 2>/dev/null
  done
  if [ -n "${SITL_LOG_FILE:-}" ]; then
    timeout 5 launch_ardupilot_sitl.sh "${SITL_WRAPPER_ARGS[@]}" logs --no-color \
      >> "$SITL_LOG_FILE" 2>&1
    timeout 15 launch_ardupilot_sitl.sh "${SITL_WRAPPER_ARGS[@]}" down --timeout 5 \
      >> "$SITL_LOG_FILE" 2>&1
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

#------------------------------------------------------------
#  Start SITL with home at the UAV lane's north end, facing south.
#------------------------------------------------------------
# Local x/y are UTM offsets (MOOS-IvP USE_UTM); invert them exactly.
ORIGIN_LAT=$(awk '$1 == "LatOrigin" { print $3 }' plug_origin_warp.moos)
ORIGIN_LON=$(awk '$1 == "LongOrigin" { print $3 }' plug_origin_warp.moos)
if ! read -r HOME_LAT HOME_LON < <(local_to_latlon.py \
    "$ORIGIN_LAT" "$ORIGIN_LON" "$UAV_LANE_X" "$LANE_NORTH_Y"); then
  echo "$ME: Could not compute the SITL home position. Exit Code 1." >&2
  exit 1
fi
SITL_HOME="$HOME_LAT,$HOME_LON,0,180"
SITL_WRAPPER_ARGS=(--no-rc --home "$SITL_HOME" \
  --speedup "$TIME_WARP" --port "$SITL_PORT")
# Never adopt or stop a manually launched simulator on the same ports.
COMPOSE_PROJECT_NAME="moos-sitl-rendezvous-$(id -u)-$$"
export COMPOSE_PROJECT_NAME
mkdir -p LOG_sitl || exit 1
SITL_LOG_FILE="$MISSION_DIR/LOG_sitl/$COMPOSE_PROJECT_NAME.log"

STARTUP_TIMEOUT="${SITL_STARTUP_TIMEOUT:-60}"
DEADLINE=$((SECONDS + STARTUP_TIMEOUT))
echo "$ME: Starting SITL at $SITL_HOME; log: $SITL_LOG_FILE"
if ! timeout "$STARTUP_TIMEOUT" launch_ardupilot_sitl.sh \
    "${SITL_WRAPPER_ARGS[@]}" up -d --no-build > "$SITL_LOG_FILE" 2>&1; then
  echo "$ME: SITL startup failed; see $SITL_LOG_FILE" >&2
  exit 1
fi
while true; do
  RUNNING_ID="$(timeout 2 launch_ardupilot_sitl.sh \
    "${SITL_WRAPPER_ARGS[@]}" ps --status running --quiet 2>> "$SITL_LOG_FILE")"
  if [[ -n "$RUNNING_ID" ]] &&
     timeout 1 bash -c 'exec 3<>/dev/tcp/127.0.0.1/"$1"' _ "$SITL_PORT" 2>/dev/null; then
    break
  fi
  if (( SECONDS >= DEADLINE )); then
    echo "$ME: SITL controller startup timed out; see $SITL_LOG_FILE" >&2
    exit 1
  fi
  sleep 0.2
done
echo "$ME: SITL controller ready."

#------------------------------------------------------------
#  Launch the communities.
#------------------------------------------------------------
mkdir -p LOG_antler
for COMMUNITY in uav pearl shoreside; do
  echo "$ME: Launching $COMMUNITY. WARP=$TIME_WARP"
  pAntler "targ_$COMMUNITY.moos" > "LOG_antler/$COMMUNITY.log" 2>&1 &
  ANTLER_PIDS+=($!)
done

if [ "$XLAUNCHED" = "yes" ]; then
  wait "${ANTLER_PIDS[@]}"
else
  uMAC targ_shoreside.moos
fi
exit 0
