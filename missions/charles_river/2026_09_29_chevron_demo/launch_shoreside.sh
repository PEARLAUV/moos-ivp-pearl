#!/bin/bash
#------------------------------------------------------------
#   Script: launch_shoreside.sh
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
LAUNCH_GUI="yes"
MODE="SIM"

IP_ADDR="localhost"
MOOS_PORT="9000"
PSHARE_PORT="9200"
UAV_DIRECT_ENABLE="no"
UAV_IP=""
UAV_PSHARE="9201"

#------------------------------------------------------------
#  Part 3: Parse command-line arguments.
#------------------------------------------------------------
for ARGI; do
  CMD_ARGS+=" ${ARGI}"
  if [[ "${ARGI}" = "--help" || "${ARGI}" = "-h" ]]; then
    echo "$ME [OPTIONS] [time_warp]"
    echo "  --help, -h                    Show this help message"
    echo "  --just_make, -j               Only create targ files"
    echo "  --verbose, -v                 Verbose launch summary"
    echo "  --auto, -a                    Script launch, no uMAC"
    echo "  --nogui, -ng                  No pMarineViewer"
    echo "  --mode=<SIM|SITL|REAL>        Whole-mission operating mode"
    echo "  --ip=<localhost>              Advertised shoreside pShare host"
    echo "  --mport=<9000>                Shoreside MOOSDB port"
    echo "  --pshare=<9200>               Shoreside pShare port"
    echo "  --uav_ip=<host>               Direct UAV Tailnet address"
    echo "  --uav_pshare=<9201>           Direct UAV pShare port"
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
  elif [[ "${ARGI}" = "--nogui" || "${ARGI}" = "-ng" ]]; then
    LAUNCH_GUI="no"
  elif [[ "${ARGI}" == --mode=* ]]; then
    MODE="${ARGI#*=}"
  elif [[ "${ARGI}" == --ip=* ]]; then
    IP_ADDR="${ARGI#*=}"
  elif [[ "${ARGI}" == --mport=* ]]; then
    MOOS_PORT="${ARGI#*=}"
  elif [[ "${ARGI}" == --pshare=* ]]; then
    PSHARE_PORT="${ARGI#*=}"
  elif [[ "${ARGI}" == --uav_ip=* ]]; then
    UAV_IP="${ARGI#*=}"
    UAV_DIRECT_ENABLE="yes"
  elif [[ "${ARGI}" == --uav_pshare=* ]]; then
    UAV_PSHARE="${ARGI#*=}"
  else
    echo "$ME: Bad Arg:[$ARGI]. Exit Code 1."
    exit 1
  fi
done

MODE=$(echo "$MODE" | tr '[:lower:]' '[:upper:]')
if [[ "$MODE" != "SIM" && "$MODE" != "SITL" && "$MODE" != "REAL" ]]; then
  echo "$ME: --mode must be SIM, SITL, or REAL. Exit Code 1."
  exit 1
fi

#------------------------------------------------------------
#  Part 4: Show launch summary.
#------------------------------------------------------------
if [ "$VERBOSE" = "yes" ]; then
  echo "============================================"
  echo "  $ME SUMMARY"
  echo "============================================"
  echo "CMD_ARGS =      [${CMD_ARGS}]"
  echo "TIME_WARP =     [${TIME_WARP}]"
  echo "JUST_MAKE =     [${JUST_MAKE}]"
  echo "LAUNCH_GUI =    [${LAUNCH_GUI}]"
  echo "MODE =          [${MODE}]"
  echo "IP_ADDR =       [${IP_ADDR}]"
  echo "MOOS_PORT =     [${MOOS_PORT}]"
  echo "PSHARE_PORT =   [${PSHARE_PORT}]"
  echo "UAV_DIRECT =    [${UAV_DIRECT_ENABLE}]"
  echo "UAV_IP =        [${UAV_IP}]"
  echo "UAV_PSHARE =    [${UAV_PSHARE}]"
fi

#------------------------------------------------------------
#  Part 5: Generate the shoreside target.
#------------------------------------------------------------
NSFLAGS=(--strict --force -x)

nsplug meta_shoreside.moos targ_shoreside.moos "${NSFLAGS[@]}" \
  WARP="$TIME_WARP" IP_ADDR="$IP_ADDR" MOOS_PORT="$MOOS_PORT" \
  PSHARE_PORT="$PSHARE_PORT" LAUNCH_GUI="$LAUNCH_GUI" \
  UAV_DIRECT_ENABLE="$UAV_DIRECT_ENABLE" UAV_IP="$UAV_IP" \
  UAV_PSHARE="$UAV_PSHARE" \
  XMODE="$MODE"

if [ "$JUST_MAKE" = "yes" ]; then
  echo "$ME: Targ files made; exiting without launch."
  exit 0
fi

#------------------------------------------------------------
#  Part 6: Launch shoreside.
#------------------------------------------------------------
echo "Launching Shoreside MOOS Community. WARP=$TIME_WARP"
pAntler targ_shoreside.moos >& /dev/null &
echo "Done Launching Shoreside Community"

#------------------------------------------------------------
#  Part 7: Return to a calling launcher or open uMAC.
#------------------------------------------------------------
if [ "$AUTO_LAUNCHED" = "yes" ]; then
  exit 0
fi

uMAC targ_shoreside.moos
trap "" SIGINT
kill -- -$$
