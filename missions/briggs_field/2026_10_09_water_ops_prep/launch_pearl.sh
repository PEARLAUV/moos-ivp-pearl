#!/bin/bash
#------------------------------------------------------------
#   Script: launch_pearl.sh
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
PEARL_ROOT=$(cd "$MISSION_DIR/../../.." && pwd)
export PATH="$PEARL_ROOT/bin:$PATH"
cd "$MISSION_DIR" || exit 1
CMD_ARGS=""
TIME_WARP=1
VERBOSE="no"
JUST_MAKE="no"
AUTO_LAUNCHED="no"

MODE="SIM"
IP_ADDR="localhost"
MOOS_PORT="9002"
PSHARE_PORT="9202"
SHORE_IP="localhost"
SHORE_PSHARE="9200"
DIRECT_PEER="false"
UAV_IP="unused"
UAV_PSHARE="9201"
VNAME="pearl"
COLOR="dodger_blue"
START_POS="-4,-22,0"
CRUISE_SPEED="0.5"
MIN_STATE_TIME="0"
SHERLOCK_METRICS_HOST="192.168.88.252"
SHERLOCK_METRICS_PORT="9273"

#------------------------------------------------------------
#  Part 3: Parse command-line arguments.
#------------------------------------------------------------
for ARGI; do
  CMD_ARGS+=" ${ARGI}"
  if [[ "${ARGI}" = "--help" || "${ARGI}" = "-h" ]]; then
    echo "$ME [OPTIONS] [time_warp]"
    echo "  --help, -h             Show this help message"
    echo "  --just_make, -j        Only create targ files"
    echo "  --verbose, -v          Verbose launch summary"
    echo "  --auto, -a             Script launch, no uMAC"
    echo "  --mode=<SIM|REAL>      PEARL operating mode"
    echo "  --ip=<localhost>       Advertised PEARL pShare host"
    echo "  --mport=<9002>         PEARL MOOSDB port"
    echo "  --pshare=<9202>        PEARL pShare port"
    echo "  --shore=<localhost>    Shoreside host"
    echo "  --shore_pshare=<9200>  Shoreside pShare port"
    echo "  --uav_ip=<address>     Direct UAV host (optional)"
    echo "  --uav_pshare=<9201>    Direct UAV pShare port"
    echo "  --start_pos=<X,Y,H>    PEARL SIM start position"
    echo "  --speed=<0.5>          PEARL Helm speed, m/s"
    echo "  --min_state_time=<0>   Minimum FSM state dwell, seconds"
    echo "  --sherlock_metrics_host=<host>  Sherlock Telegraf host"
    echo "  --sherlock_metrics_port=<9273>  Sherlock Telegraf port"
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
  elif [[ "${ARGI}" == --uav_ip=* ]]; then
    UAV_IP="${ARGI#*=}"
    DIRECT_PEER="true"
  elif [[ "${ARGI}" == --uav_pshare=* ]]; then
    UAV_PSHARE="${ARGI#*=}"
  elif [[ "${ARGI}" == --start_pos=* || "${ARGI}" == --startpos=* ]]; then
    START_POS="${ARGI#*=}"
  elif [[ "${ARGI}" == --speed=* ]]; then
    CRUISE_SPEED="${ARGI#*=}"
  elif [[ "${ARGI}" == --min_state_time=* ]]; then
    MIN_STATE_TIME="${ARGI#*=}"
  elif [[ "${ARGI}" == --sherlock_metrics_host=* ]]; then
    SHERLOCK_METRICS_HOST="${ARGI#*=}"
  elif [[ "${ARGI}" == --sherlock_metrics_port=* ]]; then
    SHERLOCK_METRICS_PORT="${ARGI#*=}"
  else
    echo "$ME: Bad Arg:[$ARGI]. Exit Code 1."
    exit 1
  fi
done

MODE=$(echo "$MODE" | tr '[:lower:]' '[:upper:]')
if [[ "$MODE" != "SIM" && "$MODE" != "REAL" ]]; then
  echo "$ME: --mode must be SIM or REAL. Exit Code 1."
  exit 1
fi
if [[ ! "$MIN_STATE_TIME" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
  echo "$ME: --min_state_time must be a non-negative number. Exit Code 1."
  exit 1
fi

if [[ "$START_POS" != *,* ]]; then
  echo "$ME: Bad --start_pos format: $START_POS. Exit Code 1."
  exit 1
elif [[ "$START_POS" != *,*,* ]]; then
  START_POS="${START_POS},0"
fi

#------------------------------------------------------------
#  Part 4: Show launch summary.
#------------------------------------------------------------
if [ "$VERBOSE" = "yes" ]; then
  echo "============================================"
  echo "  $ME SUMMARY"
  echo "============================================"
  echo "CMD_ARGS =       [${CMD_ARGS}]"
  echo "MODE =           [${MODE}]"
  echo "TIME_WARP =      [${TIME_WARP}]"
  echo "IP_ADDR =        [${IP_ADDR}]"
  echo "MOOS_PORT =      [${MOOS_PORT}]"
  echo "PSHARE_PORT =    [${PSHARE_PORT}]"
  echo "SHORE_IP =       [${SHORE_IP}]"
  echo "SHORE_PSHARE =   [${SHORE_PSHARE}]"
  echo "DIRECT_PEER =    [${DIRECT_PEER}]"
  echo "UAV_IP =         [${UAV_IP}]"
  echo "UAV_PSHARE =     [${UAV_PSHARE}]"
  echo "START_POS =      [${START_POS}]"
  echo "CRUISE_SPEED =   [${CRUISE_SPEED}]"
  echo "MIN_STATE_TIME = [${MIN_STATE_TIME}]"
  echo "SHERLOCK_METRICS_HOST = [${SHERLOCK_METRICS_HOST}]"
  echo "SHERLOCK_METRICS_PORT = [${SHERLOCK_METRICS_PORT}]"
fi

#------------------------------------------------------------
#  Part 5: Regenerate the PEARL FSM, then build mission targets.
#------------------------------------------------------------
if ! "$PEARL_ROOT/scripts/generate_fsm.py" "$MISSION_DIR/pearl_fsm.toml"; then
  echo "$ME: Failed to generate PEARL FSM artifacts; not launching." >&2
  exit 1
fi

NSFLAGS=(--strict --force -x)

if ! nsplug meta_pearl.moos "targ_${VNAME}.moos" "${NSFLAGS[@]}" \
  WARP="$TIME_WARP" IP_ADDR="$IP_ADDR" MOOS_PORT="$MOOS_PORT" \
  PSHARE_PORT="$PSHARE_PORT" SHORE_IP="$SHORE_IP" \
  SHORE_PSHARE="$SHORE_PSHARE" DIRECT_PEER="$DIRECT_PEER" \
  UAV_IP="$UAV_IP" UAV_PSHARE="$UAV_PSHARE" \
  VNAME="$VNAME" COLOR="$COLOR" \
  START_POS="$START_POS" SPEED="$CRUISE_SPEED" XMODE="$MODE" \
  MIN_STATE_TIME="$MIN_STATE_TIME" PEARL_LIB_DIR="$PEARL_ROOT/lib" \
  SHERLOCK_METRICS_HOST="$SHERLOCK_METRICS_HOST" \
  SHERLOCK_METRICS_PORT="$SHERLOCK_METRICS_PORT"; then
  echo "$ME: Failed to generate targ_${VNAME}.moos; not launching." >&2
  exit 1
fi

if ! nsplug meta_pearl.bhv "targ_${VNAME}.bhv" "${NSFLAGS[@]}" \
  VNAME="$VNAME" SPEED="$CRUISE_SPEED" XMODE="$MODE"; then
  echo "$ME: Failed to generate targ_${VNAME}.bhv; not launching." >&2
  exit 1
fi

if [ "$JUST_MAKE" = "yes" ]; then
  echo "$ME: Targ files made; exiting without launch."
  exit 0
fi

#------------------------------------------------------------
#  Part 6: Launch the PEARL community.
#------------------------------------------------------------
echo "Launching $VNAME MOOS Community. MODE=$MODE WARP=$TIME_WARP"
pAntler "targ_${VNAME}.moos" >& /dev/null &
echo "Done Launching $VNAME MOOS Community"

#------------------------------------------------------------
#  Part 7: Return to a calling launcher or open uMAC.
#------------------------------------------------------------
if [ "$AUTO_LAUNCHED" = "yes" ]; then
  exit 0
fi

uMAC "targ_${VNAME}.moos"
trap "" SIGINT
kill -- -$$
