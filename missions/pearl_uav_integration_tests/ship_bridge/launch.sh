#!/usr/bin/env bash
# Disarmed UAV attachment/detachment with a loitering PEARL.
set -euo pipefail
ME=$(basename "$0")
MISSION_DIR=$(cd "$(dirname "$0")" && pwd)
cd "$MISSION_DIR"
TIME_WARP=1
WARP_SET=no
JUST_MAKE=no
XLAUNCHED=no
LAUNCH_GUI=yes
CENTER_X=0
CENTER_Y=-115
LOITER_RADIUS=15
PEARL_SPEED=0.5
UAV_X=25
UAV_Y=-115
DECK_ALTITUDE=1
SHORE_MPORT=9000
UAV_MPORT=9001
PEARL_MPORT=9002
SHORE_PSHARE=9200
UAV_PSHARE=9201
PEARL_PSHARE=9202
SITL_PORT=5760
SITL_TARGET_PORT=5763
SITL_DIAGNOSTIC_PORT=5762

for ARGI; do
  case "$ARGI" in
    --help|-h)
      echo "$ME [OPTIONS] [time_warp]"
      echo "  -j, --just_make          Generate configs only"
      echo "  -x, --xlaunched          No uMAC; stop with SIGINT/SIGTERM"
      echo "  -ng, --nogui             Headless (no pMarineViewer)"
      echo "  --center_x=0 --center_y=-115 --loiter_radius=15"
      echo "  --pearl_speed=0.5        Loiter speed (0 < m/s <= 0.5)"
      echo "  --uav_x=25 --uav_y=-115  Initially detached UAV (local metres)"
      echo "  --deck_altitude=1       Deck surface (metres MSL; home is 0)"
      echo "  --shore_mport=9000 --uav_mport=9001 --pearl_mport=9002"
      echo "  --shore_pshare=9200 --uav_pshare=9201 --pearl_pshare=9202"
      echo "  --sitl_port=5760 --sitl_target_port=5763 --sitl_diagnostic_port=5762"
      exit 0 ;;
    --just_make|-j) JUST_MAKE=yes ;;
    --xlaunched|-x) XLAUNCHED=yes ;;
    --nogui|-ng) LAUNCH_GUI=no ;;
    --center_x=*) CENTER_X=${ARGI#*=} ;;
    --center_y=*) CENTER_Y=${ARGI#*=} ;;
    --loiter_radius=*) LOITER_RADIUS=${ARGI#*=} ;;
    --pearl_speed=*) PEARL_SPEED=${ARGI#*=} ;;
    --uav_x=*) UAV_X=${ARGI#*=} ;;
    --uav_y=*) UAV_Y=${ARGI#*=} ;;
    --deck_altitude=*) DECK_ALTITUDE=${ARGI#*=} ;;
    --shore_mport=*) SHORE_MPORT=${ARGI#*=} ;;
    --uav_mport=*) UAV_MPORT=${ARGI#*=} ;;
    --pearl_mport=*) PEARL_MPORT=${ARGI#*=} ;;
    --shore_pshare=*) SHORE_PSHARE=${ARGI#*=} ;;
    --uav_pshare=*) UAV_PSHARE=${ARGI#*=} ;;
    --pearl_pshare=*) PEARL_PSHARE=${ARGI#*=} ;;
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
for VALUE in "$CENTER_X" "$CENTER_Y" "$LOITER_RADIUS" "$PEARL_SPEED" "$UAV_X" "$UAV_Y" "$DECK_ALTITUDE"; do
  [[ "$VALUE" =~ $NUMBER ]] || { echo "$ME: Invalid number: $VALUE" >&2; exit 1; }
done
awk -v r="$LOITER_RADIUS" -v s="$PEARL_SPEED" -v d="$DECK_ALTITUDE" \
  'BEGIN { exit !(r>=5 && s>0 && s<=0.5 && d>=0 && d<=5) }' || {
  echo "$ME: Require loiter_radius >= 5, 0 < pearl_speed <= 0.5, 0 <= deck_altitude <= 5." >&2
  exit 1
}
PORTS=("$SHORE_MPORT" "$UAV_MPORT" "$PEARL_MPORT" "$SHORE_PSHARE" "$UAV_PSHARE" "$PEARL_PSHARE"
       "$SITL_PORT" "$SITL_TARGET_PORT" "$SITL_DIAGNOSTIC_PORT")
declare -A SEEN_PORTS=()
for PORT in "${PORTS[@]}"; do
  [[ "$PORT" =~ ^[0-9]+$ ]] && ((10#$PORT > 0 && 10#$PORT <= 65535)) || {
    echo "$ME: Invalid port: $PORT" >&2; exit 1;
  }
  PORT=$((10#$PORT))
  [[ -z "${SEEN_PORTS[$PORT]:-}" ]] || { echo "$ME: Ports must be distinct." >&2; exit 1; }
  SEEN_PORTS[$PORT]=yes
done
PEARL_Y=$(awk -v y="$CENTER_Y" -v r="$LOITER_RADIUS" 'BEGIN { print y-r }')
ORIGIN_LAT=$(awk '$1 == "LatOrigin" { print $3 }' plug_origin_warp.moos)
ORIGIN_LON=$(awk '$1 == "LongOrigin" { print $3 }' plug_origin_warp.moos)
read -r HOME_LAT HOME_LON < <(local_to_latlon.py "$ORIGIN_LAT" "$ORIGIN_LON" "$UAV_X" "$UAV_Y")

nsplug meta_uav.moos targ_uav.moos --strict --force \
  WARP="$TIME_WARP" MOOS_PORT="$UAV_MPORT" PSHARE_PORT="$UAV_PSHARE" \
  SHORE_PSHARE="$SHORE_PSHARE" SITL_PORT="$SITL_PORT"
nsplug meta_uav.bhv targ_uav.bhv --strict --force
nsplug meta_pearl.moos targ_pearl.moos --strict --force \
  WARP="$TIME_WARP" MOOS_PORT="$PEARL_MPORT" PSHARE_PORT="$PEARL_PSHARE" \
  SHORE_PSHARE="$SHORE_PSHARE" SITL_TARGET_PORT="$SITL_TARGET_PORT" \
  PEARL_START="$CENTER_X,$PEARL_Y,270" DECK_ALTITUDE="$DECK_ALTITUDE"
nsplug meta_pearl.bhv targ_pearl.bhv --strict --force \
  CENTER_X="$CENTER_X" CENTER_Y="$CENTER_Y" LOITER_RADIUS="$LOITER_RADIUS" PEARL_SPEED="$PEARL_SPEED"
nsplug meta_shoreside.moos targ_shoreside.moos --strict --force WARP="$TIME_WARP" \
  MOOS_PORT="$SHORE_MPORT" PSHARE_PORT="$SHORE_PSHARE" LAUNCH_GUI="$LAUNCH_GUI" \
  CENTER_X="$CENTER_X" CENTER_Y="$CENTER_Y"
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

# Only external-ship parameters plus the shared no-RC profile; no flight automation.
SITL_HOME="$HOME_LAT,$HOME_LON,0,0"
SITL_WRAPPER_ARGS=(--no-rc --home "$SITL_HOME" --param-file "$MISSION_DIR/sitl.parm" --speedup "$TIME_WARP" \
  --port "$SITL_PORT" --sim-port "$SITL_TARGET_PORT" --diagnostic-port "$SITL_DIAGNOSTIC_PORT")
COMPOSE_PROJECT_NAME="moos-sitl-ship-bridge-$(id -u)-$$"
export COMPOSE_PROJECT_NAME
mkdir -p LOG_sitl LOG_antler
SITL_LOG_FILE="$MISSION_DIR/LOG_sitl/$COMPOSE_PROJECT_NAME.log"
STARTUP_TIMEOUT=${SITL_STARTUP_TIMEOUT:-60}
DEADLINE=$((SECONDS + STARTUP_TIMEOUT))
echo "$ME: Starting SITL at $SITL_HOME; log: $SITL_LOG_FILE"
timeout "$STARTUP_TIMEOUT" launch_ardupilot_sitl.sh "${SITL_WRAPPER_ARGS[@]}" up -d --no-build > "$SITL_LOG_FILE" 2>&1
until timeout 1 bash -c 'exec 3<>/dev/tcp/127.0.0.1/"$1"' _ "$SITL_PORT" 2>/dev/null; do
  (( SECONDS < DEADLINE )) || { echo "$ME: SITL controller startup timed out." >&2; exit 1; }
  sleep 0.2
done
for COMMUNITY in uav pearl shoreside; do
  pAntler "targ_$COMMUNITY.moos" > "LOG_antler/$COMMUNITY.log" 2>&1 &
  ANTLER_PIDS+=("$!")
done
if [[ "$XLAUNCHED" == yes ]]; then
  wait "${ANTLER_PIDS[@]}"
else
  uMAC targ_shoreside.moos
fi
