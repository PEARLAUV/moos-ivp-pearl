#!/bin/bash
#------------------------------------------------------------
#   Script: launch_uav.sh
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
UAV_BASE_DIR=$(cd "$MISSION_DIR/../../uav_solo/briggs_test" && pwd)
PEARL_ROOT=$(cd "$MISSION_DIR/../../.." && pwd)
export PATH="$PEARL_ROOT/bin:$PATH"
cd "$MISSION_DIR" || exit 1
CMD_ARGS=""
TIME_WARP=1
VERBOSE="no"
JUST_MAKE="no"
AUTO_LAUNCHED="no"

MODE="SIM"
IS_SIM="true"
IP_ADDR="localhost"
MOOS_PORT="9001"
PSHARE_PORT="9201"
SHORE_IP="localhost"
SHORE_PSHARE="9200"
DIRECT_PEER="false"
PEARL_IP="unused"
PEARL_PSHARE="9202"
VNAME="uav"
COLOR="yellow"
START_POS="-4,-22,0"
AP_URL=""
AP_PROTOCOL=""
TAKEOFF_ALTITUDE="8"

#------------------------------------------------------------
#  Part 3: Parse command-line arguments.
#------------------------------------------------------------
for ARGI; do
  CMD_ARGS+=" ${ARGI}"
  if [[ "${ARGI}" = "--help" || "${ARGI}" = "-h" ]]; then
    echo "$ME [OPTIONS] [time_warp]"
    echo "  --help, -h              Show this help message"
    echo "  --just_make, -j         Only create targ files"
    echo "  --verbose, -v           Verbose launch summary"
    echo "  --auto, -a              Script launch, no uMAC"
    echo "  --mode=<SIM|SITL|REAL>  UAV operating mode"
    echo "  --ip=<localhost>        Advertised UAV pShare host"
    echo "  --mport=<9001>          UAV MOOSDB port"
    echo "  --pshare=<9201>         UAV pShare port"
    echo "  --shore=<localhost>     Shoreside host"
    echo "  --shore_pshare=<9200>   Shoreside pShare port"
    echo "  --pearl_ip=<address>    Direct PEARL host (optional)"
    echo "  --pearl_pshare=<9202>   Direct PEARL pShare port"
    echo "  --start_pos=<X,Y,H>     UAV SIM start position"
    echo "  --ap_url=<endpoint>     ArduPilot endpoint"
    echo "  --ap_protocol=<proto>   udp, tcp, or serial"
    echo "  --takeoff_altitude=<8>  Copter altitude, m AGL"
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
  elif [[ "${ARGI}" == --pearl_ip=* ]]; then
    PEARL_IP="${ARGI#*=}"
    DIRECT_PEER="true"
  elif [[ "${ARGI}" == --pearl_pshare=* ]]; then
    PEARL_PSHARE="${ARGI#*=}"
  elif [[ "${ARGI}" == --start_pos=* || "${ARGI}" == --startpos=* ]]; then
    START_POS="${ARGI#*=}"
  elif [[ "${ARGI}" == --ap_url=* ]]; then
    AP_URL="${ARGI#*=}"
  elif [[ "${ARGI}" == --ap_protocol=* ]]; then
    AP_PROTOCOL="${ARGI#*=}"
  elif [[ "${ARGI}" == --takeoff_altitude=* ]]; then
    TAKEOFF_ALTITUDE="${ARGI#*=}"
  else
    echo "$ME: Bad Arg:[$ARGI]. Exit Code 1."
    exit 1
  fi
done

MODE=$(echo "$MODE" | tr '[:lower:]' '[:upper:]')
if [ "$MODE" = "SIM" ]; then
  IS_SIM="true"
elif [ "$MODE" = "SITL" ]; then
  IS_SIM="true"
elif [ "$MODE" = "REAL" ]; then
  IS_SIM="false"
else
  echo "$ME: --mode must be SIM, SITL, or REAL. Exit Code 1."
  exit 1
fi

if [ -z "$AP_URL" ]; then
  if [ "$MODE" = "SITL" ]; then
    AP_URL="127.0.0.1:5760"
  elif [ "$MODE" = "REAL" ]; then
    AP_URL="ttyACM0:115200"
  else
    AP_URL="unused"
  fi
fi
if [ -z "$AP_PROTOCOL" ]; then
  if [ "$MODE" = "SITL" ]; then
    AP_PROTOCOL="tcp"
  elif [ "$MODE" = "REAL" ]; then
    AP_PROTOCOL="serial"
  else
    AP_PROTOCOL="unused"
  fi
fi

#------------------------------------------------------------
#  Part 4: Show launch summary.
#------------------------------------------------------------
if [ "$VERBOSE" = "yes" ]; then
  echo "============================================"
  echo "  $ME SUMMARY"
  echo "============================================"
  echo "CMD_ARGS =          [${CMD_ARGS}]"
  echo "MODE =              [${MODE}]"
  echo "TIME_WARP =         [${TIME_WARP}]"
  echo "IP_ADDR =           [${IP_ADDR}]"
  echo "MOOS_PORT =         [${MOOS_PORT}]"
  echo "PSHARE_PORT =       [${PSHARE_PORT}]"
  echo "SHORE_IP =          [${SHORE_IP}]"
  echo "SHORE_PSHARE =      [${SHORE_PSHARE}]"
  echo "DIRECT_PEER =       [${DIRECT_PEER}]"
  echo "PEARL_IP =          [${PEARL_IP}]"
  echo "PEARL_PSHARE =      [${PEARL_PSHARE}]"
  echo "START_POS =         [${START_POS}]"
  echo "AP_URL =            [${AP_URL}]"
  echo "AP_PROTOCOL =       [${AP_PROTOCOL}]"
  echo "TAKEOFF_ALTITUDE =  [${TAKEOFF_ALTITUDE}]"
fi

#------------------------------------------------------------
#  Part 5: Generate UAV targets from the qualified field stack.
#------------------------------------------------------------
NSFLAGS=(--strict --force -x "--path=$MISSION_DIR:$UAV_BASE_DIR")

nsplug meta_uav.moos "targ_${VNAME}.moos" "${NSFLAGS[@]}" \
  WARP="$TIME_WARP" IP_ADDR="$IP_ADDR" MOOS_PORT="$MOOS_PORT" \
  PSHARE_PORT="$PSHARE_PORT" SHORE_IP="$SHORE_IP" \
  SHORE_PSHARE="$SHORE_PSHARE" DIRECT_PEER="$DIRECT_PEER" \
  PEARL_IP="$PEARL_IP" PEARL_PSHARE="$PEARL_PSHARE" \
  VNAME="$VNAME" COLOR="$COLOR" \
  PLATFORM_LENGTH="1.5" \
  START_POS="$START_POS" AP_URL="$AP_URL" AP_PROTOCOL="$AP_PROTOCOL" \
  TAKEOFF_ALTITUDE="$TAKEOFF_ALTITUDE" IS_SIM="$IS_SIM" XMODE="$MODE"

nsplug meta_uav.bhv "targ_${VNAME}.bhv" "${NSFLAGS[@]}" \
  VNAME="$VNAME" TAKEOFF_ALTITUDE="$TAKEOFF_ALTITUDE" XMODE="$MODE"

if [ "$JUST_MAKE" = "yes" ]; then
  echo "$ME: Targ files made; exiting without launch."
  exit 0
fi

#------------------------------------------------------------
#  Part 6: Launch the UAV community.
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
