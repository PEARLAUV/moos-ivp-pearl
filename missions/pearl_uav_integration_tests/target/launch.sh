#!/usr/bin/env bash
# UAV-only test of pMavlinkLandingTarget: static target, three N/S passes.
set -euo pipefail
ME=$(basename "$0")
MISSION_DIR=$(cd "$(dirname "$0")" && pwd)
cd "$MISSION_DIR"

TIME_WARP=1
WARP_SET=no
JUST_MAKE=no
XLAUNCHED=no
LAUNCH_GUI=yes
TARGET_X=0
TARGET_Y=-115
TARGET_YAW=30
HALF_LENGTH=25
UAV_SPEED=1.5
ALTITUDE=8
TARGET_RANGE=12
SHORE_MPORT=9000
UAV_MPORT=9001
SHORE_PSHARE=9200
UAV_PSHARE=9201
SITL_PORT=${SITL_PORT:-5760}
SITL_TARGET_PORT=5763
SITL_DIAGNOSTIC_PORT=5762

for ARGI; do
  case "$ARGI" in
    --help|-h)
      echo "$ME [OPTIONS] [time_warp]"
      echo "  -j, --just_make          Generate configs only"
      echo "  -x, --xlaunched          No uMAC; stop with SIGINT/SIGTERM"
      echo "  -ng, --nogui             Headless (no pMarineViewer)"
      echo "  --target_x=0 --target_y=-115 --target_yaw=30"
      echo "  --half_length=25         Pass half-length, north/south of target (m)"
      echo "  --uav_speed=1.5          Pass speed (m/s)"
      echo "  --target_range=12        3-D observation range (m)"
      echo "  --shore_mport=9000 --uav_mport=9001"
      echo "  --shore_pshare=9200 --uav_pshare=9201"
      echo "  --sitl_port=5760 --sitl_target_port=5763 --sitl_diagnostic_port=5762"
      echo "Passes: west 3 m at 8 m; east 5 m at 4 m; center at 15 m."
      exit 0 ;;
    --just_make|-j) JUST_MAKE=yes ;;
    --xlaunched|-x) XLAUNCHED=yes ;;
    --nogui|-ng) LAUNCH_GUI=no ;;
    --target_x=*) TARGET_X=${ARGI#*=} ;;
    --target_y=*) TARGET_Y=${ARGI#*=} ;;
    --target_yaw=*) TARGET_YAW=${ARGI#*=} ;;
    --half_length=*) HALF_LENGTH=${ARGI#*=} ;;
    --uav_speed=*) UAV_SPEED=${ARGI#*=} ;;
    --target_range=*) TARGET_RANGE=${ARGI#*=} ;;
    --shore_mport=*) SHORE_MPORT=${ARGI#*=} ;;
    --uav_mport=*) UAV_MPORT=${ARGI#*=} ;;
    --shore_pshare=*) SHORE_PSHARE=${ARGI#*=} ;;
    --uav_pshare=*) UAV_PSHARE=${ARGI#*=} ;;
    --sitl_port=*) SITL_PORT=${ARGI#*=} ;;
    --sitl_target_port=*) SITL_TARGET_PORT=${ARGI#*=} ;;
    --sitl_diagnostic_port=*) SITL_DIAGNOSTIC_PORT=${ARGI#*=} ;;
    *)
      if [[ "$ARGI" =~ ^[1-9][0-9]*$ && "$WARP_SET" == no ]]; then
        TIME_WARP=$ARGI
        WARP_SET=yes
      else
        echo "$ME: Bad argument: $ARGI" >&2
        exit 1
      fi ;;
  esac
done

NUMBER='^[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)$'
for VALUE in "$TARGET_X" "$TARGET_Y" "$TARGET_YAW" "$HALF_LENGTH" "$UAV_SPEED" "$TARGET_RANGE"; do
  [[ "$VALUE" =~ $NUMBER ]] || { echo "$ME: Invalid number: $VALUE" >&2; exit 1; }
done
awk -v h="$HALF_LENGTH" -v s="$UAV_SPEED" -v r="$TARGET_RANGE" \
  'BEGIN { exit !(h>12 && s>0 && s<=5 && r>0) }' || {
  echo "$ME: Require half_length > 12, 0 < uav_speed <= 5, target_range > 0." >&2
  exit 1
}
for PORT in "$SHORE_MPORT" "$UAV_MPORT" "$SHORE_PSHARE" "$UAV_PSHARE" \
            "$SITL_PORT" "$SITL_TARGET_PORT" "$SITL_DIAGNOSTIC_PORT"; do
  [[ "$PORT" =~ ^[0-9]+$ ]] && ((10#$PORT > 0 && 10#$PORT <= 65535)) || {
    echo "$ME: Invalid port: $PORT" >&2; exit 1;
  }
done

LEFT_X=$(awk -v x="$TARGET_X" 'BEGIN { print x-3 }')
RIGHT_X=$(awk -v x="$TARGET_X" 'BEGIN { print x+5 }')
NORTH_Y=$(awk -v y="$TARGET_Y" -v h="$HALF_LENGTH" 'BEGIN { print y+h }')
SOUTH_Y=$(awk -v y="$TARGET_Y" -v h="$HALF_LENGTH" 'BEGIN { print y-h }')
ORIGIN_LAT=$(awk '$1 == "LatOrigin" { print $3 }' plug_origin_warp.moos)
ORIGIN_LON=$(awk '$1 == "LongOrigin" { print $3 }' plug_origin_warp.moos)
read -r TARGET_LAT TARGET_LON < <(local_to_latlon.py "$ORIGIN_LAT" "$ORIGIN_LON" "$TARGET_X" "$TARGET_Y")
read -r HOME_LAT HOME_LON < <(local_to_latlon.py "$ORIGIN_LAT" "$ORIGIN_LON" "$LEFT_X" "$NORTH_Y")

nsplug meta_uav.moos targ_uav.moos --strict --force \
  WARP="$TIME_WARP" MOOS_PORT="$UAV_MPORT" PSHARE_PORT="$UAV_PSHARE" \
  SHORE_PSHARE="$SHORE_PSHARE" SITL_PORT="$SITL_PORT" SITL_TARGET_PORT="$SITL_TARGET_PORT" \
  TARGET_X="$TARGET_X" TARGET_Y="$TARGET_Y" TARGET_YAW="$TARGET_YAW" \
  TARGET_LAT="$TARGET_LAT" TARGET_LON="$TARGET_LON" TARGET_RANGE="$TARGET_RANGE"
nsplug meta_uav.bhv targ_uav.bhv --strict --force UAV_SPEED="$UAV_SPEED" \
  LEFT_X="$LEFT_X" RIGHT_X="$RIGHT_X" CENTER_X="$TARGET_X" NORTH_Y="$NORTH_Y" SOUTH_Y="$SOUTH_Y"
nsplug meta_shoreside.moos targ_shoreside.moos --strict --force WARP="$TIME_WARP" \
  MOOS_PORT="$SHORE_MPORT" PSHARE_PORT="$SHORE_PSHARE" LAUNCH_GUI="$LAUNCH_GUI" \
  TARGET_X="$TARGET_X" TARGET_Y="$TARGET_Y"
if [[ "$JUST_MAKE" == yes ]]; then
  echo "$ME: Targ files made; exiting without launch."
  exit 0
fi

ANTLER_PIDS=()
cleanup() {
  trap - EXIT INT TERM
  echo; echo "$ME: Halting owned apps"
  local PID WAITED=0 ALIVE
  for PID in "${ANTLER_PIDS[@]}"; do
    pkill -INT -P "$PID" 2>/dev/null || true
    kill -INT "$PID" 2>/dev/null || true
  done
  while (( WAITED < 50 )); do
    ALIVE=no
    for PID in "${ANTLER_PIDS[@]}"; do
      kill -0 "$PID" 2>/dev/null && ALIVE=yes
    done
    [[ "$ALIVE" == no ]] && break
    sleep 0.1
    WAITED=$((WAITED + 1))
  done
  for PID in "${ANTLER_PIDS[@]}"; do
    pkill -KILL -P "$PID" 2>/dev/null || true
    kill -KILL "$PID" 2>/dev/null || true
  done
  if [[ -n "${SITL_LOG_FILE:-}" ]]; then
    timeout 5 launch_ardupilot_sitl.sh "${SITL_WRAPPER_ARGS[@]}" logs --no-color >> "$SITL_LOG_FILE" 2>&1 || true
    timeout 15 launch_ardupilot_sitl.sh "${SITL_WRAPPER_ARGS[@]}" down --timeout 5 >> "$SITL_LOG_FILE" 2>&1 || true
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Start with the UAV already airborne at the first-pass altitude.
SITL_HOME="$HOME_LAT,$HOME_LON,$ALTITUDE,180"
SITL_WRAPPER_ARGS=(--airborne --home "$SITL_HOME" --speedup "$TIME_WARP" \
  --port "$SITL_PORT" --sim-port "$SITL_TARGET_PORT" --diagnostic-port "$SITL_DIAGNOSTIC_PORT")
COMPOSE_PROJECT_NAME="moos-sitl-target-$(id -u)-$$"
export COMPOSE_PROJECT_NAME
mkdir -p LOG_sitl LOG_antler
SITL_LOG_FILE="$MISSION_DIR/LOG_sitl/$COMPOSE_PROJECT_NAME.log"
STARTUP_TIMEOUT=${SITL_STARTUP_TIMEOUT:-60}
echo "$ME: Starting SITL airborne at $SITL_HOME; log: $SITL_LOG_FILE"
if ! timeout "$STARTUP_TIMEOUT" launch_ardupilot_sitl.sh \
    "${SITL_WRAPPER_ARGS[@]}" up -d --no-build > "$SITL_LOG_FILE" 2>&1; then
  echo "$ME: SITL airborne startup failed; see $SITL_LOG_FILE" >&2
  exit 1
fi
for COMMUNITY in uav shoreside; do
  pAntler "targ_$COMMUNITY.moos" > "LOG_antler/$COMMUNITY.log" 2>&1 &
  ANTLER_PIDS+=("$!")
done
if [[ "$XLAUNCHED" == yes ]]; then
  wait "${ANTLER_PIDS[@]}"
else
  uMAC targ_shoreside.moos
fi
