#!/usr/bin/env bash
# Passive single-community observer for a real UAV landing-target stream.
set -euo pipefail

ME=$(basename "$0")
MISSION_DIR=$(cd "$(dirname "$0")" && pwd)
cd "$MISSION_DIR"

JUST_MAKE=no
XLAUNCHED=no
LAUNCH_GUI=yes
MOOS_PORT=9000
UDP_BIND=100.112.119.114
UDP_PORT=14550
LAT_ORIGIN=42.358436
LON_ORIGIN=-71.087448
TARGET_SIZE=1
OBSERVATION_TIMEOUT=0.5

usage() {
  cat <<EOF
$ME [OPTIONS]
  -j, --just_make               Generate targ config only
  -x, --xlaunched               No uMAC; wait for pAntler
  -ng, --nogui                  Do not launch pMarineViewer
  --mport=9000                  Local MOOSDB port
  --udp_bind=100.112.119.114    Drone mavp2p IPv4 endpoint; 0.0.0.0 listens
  --udp_port=14550              MAVLink UDP endpoint port
  --lat_origin=42.358436        Viewer local-grid latitude origin
  --lon_origin=-71.087448       Viewer local-grid longitude origin
  --target_size=1               Fallback target side length in metres
  --observation_timeout=0.5     LANDING_TARGET freshness in real seconds
EOF
}

for ARGI; do
  case "$ARGI" in
    --help|-h) usage; exit 0 ;;
    --just_make|-j) JUST_MAKE=yes ;;
    --xlaunched|-x) XLAUNCHED=yes ;;
    --nogui|-ng) LAUNCH_GUI=no ;;
    --mport=*) MOOS_PORT=${ARGI#*=} ;;
    --udp_bind=*) UDP_BIND=${ARGI#*=} ;;
    --udp_port=*) UDP_PORT=${ARGI#*=} ;;
    --lat_origin=*) LAT_ORIGIN=${ARGI#*=} ;;
    --lon_origin=*) LON_ORIGIN=${ARGI#*=} ;;
    --target_size=*) TARGET_SIZE=${ARGI#*=} ;;
    --observation_timeout=*) OBSERVATION_TIMEOUT=${ARGI#*=} ;;
    *) echo "$ME: Bad argument: $ARGI" >&2; exit 1 ;;
  esac
done

valid_port() {
  [[ "$1" =~ ^[0-9]+$ ]] && ((10#$1 > 0 && 10#$1 <= 65535))
}

valid_ipv4() {
  local addr=$1 octet
  local -a octets
  IFS=. read -r -a octets <<< "$addr"
  [[ ${#octets[@]} -eq 4 ]] || return 1
  for octet in "${octets[@]}"; do
    [[ "$octet" =~ ^[0-9]+$ ]] || return 1
    ((10#$octet >= 0 && 10#$octet <= 255)) || return 1
  done
}

valid_port "$MOOS_PORT" || { echo "$ME: Invalid MOOS port: $MOOS_PORT" >&2; exit 1; }
valid_port "$UDP_PORT" || { echo "$ME: Invalid UDP port: $UDP_PORT" >&2; exit 1; }
valid_ipv4 "$UDP_BIND" || { echo "$ME: --udp_bind requires an IPv4 address" >&2; exit 1; }

NUMBER='^[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)$'
for VALUE in "$LAT_ORIGIN" "$LON_ORIGIN" "$TARGET_SIZE" "$OBSERVATION_TIMEOUT"; do
  [[ "$VALUE" =~ $NUMBER ]] || { echo "$ME: Invalid number: $VALUE" >&2; exit 1; }
done
awk -v lat="$LAT_ORIGIN" -v lon="$LON_ORIGIN" -v size="$TARGET_SIZE" \
    -v timeout="$OBSERVATION_TIMEOUT" \
  'BEGIN { exit !(lat>=-90 && lat<=90 && lon>=-180 && lon<=180 && size>0 && timeout>0) }' || {
  echo "$ME: Invalid origin, target size, or observation timeout." >&2
  exit 1
}

nsplug meta_target_tracking.moos targ_target_tracking.moos --strict --force \
  MOOS_PORT="$MOOS_PORT" UDP_BIND="$UDP_BIND" UDP_PORT="$UDP_PORT" \
  LAT_ORIGIN="$LAT_ORIGIN" LON_ORIGIN="$LON_ORIGIN" \
  TARGET_SIZE="$TARGET_SIZE" OBSERVATION_TIMEOUT="$OBSERVATION_TIMEOUT" \
  LAUNCH_GUI="$LAUNCH_GUI"

if [[ "$JUST_MAKE" == yes ]]; then
  echo "$ME: Targ file made; exiting without launch."
  exit 0
fi

mkdir -p LOG_antler
ANTLER_PID=""
cleanup() {
  trap - EXIT INT TERM
  if [[ -n "$ANTLER_PID" ]]; then
    kill -INT "$ANTLER_PID" 2>/dev/null || true
    for _ in $(seq 1 50); do
      kill -0 "$ANTLER_PID" 2>/dev/null || break
      sleep 0.1
    done
    kill -KILL "$ANTLER_PID" 2>/dev/null || true
    wait "$ANTLER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

printf '%s: Listening for passive MAVLink telemetry on udp://%s:%s\n' \
  "$ME" "$UDP_BIND" "$UDP_PORT"
pAntler targ_target_tracking.moos > LOG_antler/target_tracking.log 2>&1 &
ANTLER_PID=$!

if [[ "$XLAUNCHED" == yes ]]; then
  wait "$ANTLER_PID"
else
  uMAC targ_target_tracking.moos
fi
