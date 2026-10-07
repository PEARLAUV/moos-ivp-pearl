#!/bin/bash
#------------------------------------------------------------
#   Script: launch_sitl.sh
#  Mission: aug_pearl_uav
#------------------------------------------------------------
ME=$(basename "$0")
MISSION_DIR=$(cd "$(dirname "$0")" && pwd)
ARDUPILOT_ROOT="${ARDUPILOT_ROOT:-$HOME/ardupilot}"
SPEEDUP="1"
WIPE="yes"
NO_REBUILD="no"
DIRECT_RUN="no"
LANDING_TARGET="yes"

HOME_LAT="42.358436"
HOME_LON="-71.087448"

for ARGI; do
  if [[ "${ARGI}" = "--help" || "${ARGI}" = "-h" ]]; then
    echo "$ME [OPTIONS]"
    echo "  --help, -h                    Show this help message"
    echo "  --ardupilot_root=<path>       ArduPilot checkout"
    echo "  --speedup=<1>                 SITL speedup"
    echo "  --keep_eeprom                 Reuse prior SITL state"
    echo "  --no_rebuild                  Reuse the existing SITL binary"
    echo "  --direct                      Run ArduCopter in this terminal"
    echo "  --no_landing_target           Do not publish the SITL vision target"
    exit 0
  elif [[ "${ARGI}" == --ardupilot_root=* ]]; then
    ARDUPILOT_ROOT="${ARGI#*=}"
  elif [[ "${ARGI}" == --speedup=* ]]; then
    SPEEDUP="${ARGI#*=}"
  elif [ "${ARGI}" = "--keep_eeprom" ]; then
    WIPE="no"
  elif [ "${ARGI}" = "--no_rebuild" ]; then
    NO_REBUILD="yes"
  elif [ "${ARGI}" = "--direct" ]; then
    DIRECT_RUN="yes"
  elif [ "${ARGI}" = "--no_landing_target" ]; then
    LANDING_TARGET="no"
  else
    echo "$ME: Bad Arg:[$ARGI]. Exit Code 1."
    exit 1
  fi
done

SIM_VEHICLE="$ARDUPILOT_ROOT/Tools/autotest/sim_vehicle.py"
ARDUCOPTER_BIN="$ARDUPILOT_ROOT/build/sitl/bin/arducopter"
DEFAULT_PARAMS="$ARDUPILOT_ROOT/Tools/autotest/default_params/copter.parm"
if [ "$DIRECT_RUN" = "no" ] && [ ! -x "$SIM_VEHICLE" ]; then
  echo "$ME: ArduPilot sim_vehicle.py not found at: $SIM_VEHICLE"
  exit 1
fi
if [ "$DIRECT_RUN" = "yes" ] && [ ! -x "$ARDUCOPTER_BIN" ]; then
  echo "$ME: Built ArduCopter SITL binary not found at: $ARDUCOPTER_BIN"
  echo "$ME: Build ArduCopter first or omit --direct."
  exit 1
fi
if [ "$DIRECT_RUN" = "yes" ] && [ ! -f "$DEFAULT_PARAMS" ]; then
  echo "$ME: ArduCopter default parameters not found at: $DEFAULT_PARAMS"
  exit 1
fi
SITL_DIR="$MISSION_DIR/SITL_aug_pearl_uav"
mkdir -p "$SITL_DIR"

ARGS=(-v ArduCopter -f quad)
ARGS+=(--custom-location="$HOME_LAT,$HOME_LON,5,0")
ARGS+=(--speedup="$SPEEDUP")
ARGS+=(--use-dir="$SITL_DIR")
ARGS+=(--add-param-file="$MISSION_DIR/sitl.parm")
ARGS+=(--no-mavproxy)
ARGS+=(--sitl-instance-args="--serial1=udpclient:127.0.0.1:14551")
[ "$WIPE" = "yes" ] && ARGS+=(--wipe-eeprom)
[ "$NO_REBUILD" = "yes" ] && ARGS+=(--no-rebuild)

echo "$ME: Starting ArduCopter SITL at $HOME_LAT,$HOME_LON"
echo "$ME: Direct MAVLink endpoint is TCP 127.0.0.1:5760"
PYTHONPATH="$ARDUPILOT_ROOT/modules/mavlink${PYTHONPATH:+:$PYTHONPATH}" \
  python3 "$MISSION_DIR/sitl_rc_center.py" &
RC_CENTER_PID=$!

LANDING_TARGET_PID=""
if [ "$LANDING_TARGET" = "yes" ]; then
  PYTHONPATH="$ARDUPILOT_ROOT/modules/mavlink${PYTHONPATH:+:$PYTHONPATH}" \
    python3 "$MISSION_DIR/sitl_landing_target.py" &
  LANDING_TARGET_PID=$!
fi

cleanup_helpers() {
  kill "$RC_CENTER_PID" 2>/dev/null || true
  if [ -n "$LANDING_TARGET_PID" ]; then
    kill "$LANDING_TARGET_PID" 2>/dev/null || true
  fi
}
trap cleanup_helpers EXIT INT TERM

if [ "$DIRECT_RUN" = "yes" ]; then
  DIRECT_ARGS=(--model + --speedup "$SPEEDUP" --slave 0)
  DIRECT_ARGS+=(--serial1=udpclient:127.0.0.1:14551)
  DIRECT_ARGS+=(--defaults "$DEFAULT_PARAMS,$MISSION_DIR/sitl.parm")
  DIRECT_ARGS+=(--sim-address=127.0.0.1 -I0)
  DIRECT_ARGS+=(--home "$HOME_LAT,$HOME_LON,5,0")
  [ "$WIPE" = "yes" ] && DIRECT_ARGS=(-w "${DIRECT_ARGS[@]}")
  cd "$SITL_DIR" || exit 1
  "$ARDUCOPTER_BIN" "${DIRECT_ARGS[@]}"
  exit $?
fi

cd "$ARDUPILOT_ROOT" || exit 1
"$SIM_VEHICLE" "${ARGS[@]}"
