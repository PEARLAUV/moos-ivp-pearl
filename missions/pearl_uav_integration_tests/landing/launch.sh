#!/bin/bash
#------------------------------------------------------------
#   Script: launch.sh
#  Mission: pearl_uav_integration_tests/landing
#
#  PEARL (simulated) loiters west of the UAV's N/S transit lane.
#  RECOVER agrees a landing session,
#  runs rendezvous, then hands cleared precision landing to ArduPilot.
#------------------------------------------------------------
ME=$(basename "$0")
MISSION_DIR=$(cd "$(dirname "$0")" && pwd)
cd "$MISSION_DIR" || exit 1

TIME_WARP=1
JUST_MAKE="no"
XLAUNCHED="no"
LAUNCH_GUI="yes"

# UAV transits south. PEARL circles west of its lane, centered midway along it.
PEARL_CENTER_X="-15"
PEARL_RADIUS="15"
UAV_LANE_X="15"
LANE_NORTH_Y="-40"
LANE_SOUTH_Y="-190"
PEARL_SPEED="0.5"
UAV_SPEED="1.5"
ALTITUDE="8"
DECK_ALTITUDE="1"
TARGET_RANGE="12"
SITL_TARGET_PORT="5763"
SITL_DIAGNOSTIC_PORT="5762"
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
    echo "  --pearl_x=<-15>         PEARL loiter center x (west of UAV lane)"
    echo "  --pearl_radius=<15>     Circular loiter radius, metres (min 5)"
    echo "  --uav_x=<15>            UAV lane x"
    echo "  --north=<-40>           Lane north end y (UAV start)"
    echo "  --south=<-190>          UAV lane south end y"
    echo "  --pearl_speed=<0.5>     PEARL loiter/transit speed, m/s (max 0.5)"
    echo "  --uav_speed=<1.5>       UAV lane speed, m/s"
    echo "  --deck_altitude=<1>     Deck surface altitude in metres MSL"
    echo "  --target_range=<12>     Target detection range in 3D metres"
    echo
    echo "Ports:"
    echo "  --shore_mport=<9000>  --uav_mport=<9001>  --pearl_mport=<9002>"
    echo "  --shore_pshare=<9200> --uav_pshare=<9201> --pearl_pshare=<9202>"
    echo "  --sitl_port=<5760>      SITL controller TCP port"
    echo "  --sitl_target_port=<5763> Simulation router TCP port"
    echo "  --sitl_diagnostic_port=<5762> Diagnostics TCP port"
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
    PEARL_CENTER_X="${ARGI#*=}"
  elif [[ "${ARGI}" == --pearl_radius=* ]]; then
    PEARL_RADIUS="${ARGI#*=}"
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
  elif [[ "${ARGI}" == --sitl_target_port=* ]]; then
    SITL_TARGET_PORT="${ARGI#*=}"
  elif [[ "${ARGI}" == --sitl_diagnostic_port=* ]]; then
    SITL_DIAGNOSTIC_PORT="${ARGI#*=}"
  elif [[ "${ARGI}" == --deck_altitude=* ]]; then
    DECK_ALTITUDE="${ARGI#*=}"
  elif [[ "${ARGI}" == --target_range=* ]]; then
    TARGET_RANGE="${ARGI#*=}"
  else
    echo "$ME: Bad Arg:[$ARGI]. Exit Code 1."
    exit 1
  fi
done

NUMBER='^[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)$'
for VALUE in "$PEARL_CENTER_X" "$PEARL_RADIUS" "$UAV_LANE_X" "$LANE_NORTH_Y" "$LANE_SOUTH_Y" \
             "$PEARL_SPEED" "$UAV_SPEED" "$DECK_ALTITUDE" "$TARGET_RANGE"; do
  if [[ ! "$VALUE" =~ $NUMBER ]]; then
    echo "$ME: geometry and speeds must be numeric, got [$VALUE]. Exit Code 1."
    exit 1
  fi
done
if ! awk -v d="$DECK_ALTITUDE" -v r="$TARGET_RANGE" 'BEGIN { exit !(d>=0 && d<8 && r>0) }'; then
  echo "$ME: require 0 <= deck_altitude < 8 and target_range > 0." >&2
  exit 1
fi
if ! awk -v x="$PEARL_CENTER_X" -v u="$UAV_LANE_X" -v r="$PEARL_RADIUS" \
         -v s="$PEARL_SPEED" -v n="$LANE_NORTH_Y" -v b="$LANE_SOUTH_Y" \
         'BEGIN { exit !(r>=5 && x+r<u && s>0 && s<=0.5 && n>b) }'; then
  echo "$ME: require radius >= 5, pearl_x + radius < uav_x, 0 < pearl_speed <= 0.5, north > south." >&2
  exit 1
fi
PEARL_CENTER_Y=$(awk -v n="$LANE_NORTH_Y" -v s="$LANE_SOUTH_Y" 'BEGIN { print (n+s)/2 }')
PEARL_START_Y=$(awk -v c="$PEARL_CENTER_Y" -v r="$PEARL_RADIUS" 'BEGIN { print c-r }')
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
  SITL_PORT="$SITL_PORT" SITL_TARGET_PORT="$SITL_TARGET_PORT" ALTITUDE="$ALTITUDE" \
  DECK_ALTITUDE="$DECK_ALTITUDE" TARGET_RANGE="$TARGET_RANGE" \
  LANE_START_ALTITUDE="$(awk -v a="$ALTITUDE" 'BEGIN { print a - 1 }')" || exit 1
nsplug meta_uav.bhv targ_uav.bhv "${NSFLAGS[@]}" \
  ALTITUDE="$ALTITUDE" UAV_SPEED="$UAV_SPEED" \
  UAV_LANE_X="$UAV_LANE_X" LANE_END_Y="$LANE_SOUTH_Y" || exit 1

nsplug meta_pearl.moos targ_pearl.moos "${NSFLAGS[@]}" \
  WARP="$TIME_WARP" MOOS_PORT="$PEARL_MPORT" PSHARE_PORT="$PEARL_PSHARE" \
  SHORE_PSHARE="$SHORE_PSHARE" UAV_PSHARE="$UAV_PSHARE" \
  SITL_TARGET_PORT="$SITL_TARGET_PORT" DECK_ALTITUDE="$DECK_ALTITUDE" \
  PEARL_START="$PEARL_CENTER_X,$PEARL_START_Y,270" || exit 1
nsplug meta_pearl.bhv targ_pearl.bhv "${NSFLAGS[@]}" \
  PEARL_SPEED="$PEARL_SPEED" \
  PEARL_CENTER_X="$PEARL_CENTER_X" PEARL_CENTER_Y="$PEARL_CENTER_Y" \
  PEARL_RADIUS="$PEARL_RADIUS" || exit 1

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
SITL_HOME="$HOME_LAT,$HOME_LON,$ALTITUDE,180"
SITL_WRAPPER_ARGS=(--airborne --home "$SITL_HOME" --param-file "$MISSION_DIR/sitl.parm" \
  --speedup "$TIME_WARP" --port "$SITL_PORT" --sim-port "$SITL_TARGET_PORT" \
  --diagnostic-port "$SITL_DIAGNOSTIC_PORT")
# Never adopt or stop a manually launched simulator on the same ports.
COMPOSE_PROJECT_NAME="moos-sitl-landing-$(id -u)-$$"
export COMPOSE_PROJECT_NAME
mkdir -p LOG_sitl || exit 1
SITL_LOG_FILE="$MISSION_DIR/LOG_sitl/$COMPOSE_PROJECT_NAME.log"

STARTUP_TIMEOUT="${SITL_STARTUP_TIMEOUT:-60}"
echo "$ME: Starting SITL airborne at $SITL_HOME; log: $SITL_LOG_FILE"
if ! timeout "$STARTUP_TIMEOUT" launch_ardupilot_sitl.sh \
    "${SITL_WRAPPER_ARGS[@]}" up -d --no-build > "$SITL_LOG_FILE" 2>&1; then
  echo "$ME: SITL airborne startup failed; see $SITL_LOG_FILE" >&2
  exit 1
fi
echo "$ME: SITL airborne and ready."

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
