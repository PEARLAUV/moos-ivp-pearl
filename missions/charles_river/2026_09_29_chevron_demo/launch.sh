#!/bin/bash
#------------------------------------------------------------
#   Script: launch.sh
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

MODE="SIM"
UAV_START="-4,-22,0"
PEARL_START="-4,-22,0"
TAKEOFF_ALTITUDE="8"
AP_URL=""
AP_PROTOCOL=""

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
    echo "  --sim                         Same as --mode=SIM (default)"
    echo "  --mode=<SIM|SITL|REAL>        Whole-mission operating mode"
    echo
    echo "Options:"
    echo "  --help, -h                    Show this help message"
    echo "  --verbose, -v                 Verbose launch summary"
    echo "  --just_make, -j               Only create targ files"
    echo "  --log_clean, -lc              Run clean.sh before launch"
    echo "  --xlaunched, -x               Launched by automation"
    echo "  --nogui, -ng                  No pMarineViewer"
    echo "  --uav_start=<X,Y,H>           UAV SIM start position"
    echo "  --pearl_start=<X,Y,H>         PEARL SIM start position"
    echo "  --takeoff_altitude=<8>        UAV altitude, m AGL"
    echo "  --ap_url=<endpoint>           UAV ArduPilot endpoint"
    echo "  --ap_protocol=<proto>         UAV udp, tcp, or serial"
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
  elif [ "${ARGI}" = "--sim" ]; then
    MODE="SIM"
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
  elif [[ "${ARGI}" == --uav_start=* ]]; then
    UAV_START="${ARGI#*=}"
  elif [[ "${ARGI}" == --pearl_start=* ]]; then
    PEARL_START="${ARGI#*=}"
  elif [[ "${ARGI}" == --takeoff_altitude=* ]]; then
    TAKEOFF_ALTITUDE="${ARGI#*=}"
  elif [[ "${ARGI}" == --ap_url=* ]]; then
    AP_URL="${ARGI#*=}"
  elif [[ "${ARGI}" == --ap_protocol=* ]]; then
    AP_PROTOCOL="${ARGI#*=}"
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

MODE=$(echo "$MODE" | tr '[:lower:]' '[:upper:]')
if [[ "$MODE" != "SIM" && "$MODE" != "SITL" && "$MODE" != "REAL" ]]; then
  echo "$ME: --mode must be SIM, SITL, or REAL. Exit Code 1."
  exit 1
fi

UAV_MODE="$MODE"
PEARL_MODE="$MODE"
SIM_ATTACHMENT="false"
if [ "$MODE" = "SITL" ]; then
  PEARL_MODE="SIM"
elif [ "$MODE" = "SIM" ]; then
  SIM_ATTACHMENT="true"
fi

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
  echo "UAV_MODE =          [${UAV_MODE}]"
  echo "PEARL_MODE =        [${PEARL_MODE}]"
  echo "SIM_ATTACHMENT =    [${SIM_ATTACHMENT}]"
  echo "UAV_START =         [${UAV_START}]"
  echo "PEARL_START =       [${PEARL_START}]"
  echo "SHORE_MPORT =       [${SHORE_MPORT}]"
  echo "UAV_MPORT =         [${UAV_MPORT}]"
  echo "PEARL_MPORT =       [${PEARL_MPORT}]"
  echo "SHORE_PSHARE =      [${SHORE_PSHARE}]"
  echo "UAV_PSHARE =        [${UAV_PSHARE}]"
  echo "PEARL_PSHARE =      [${PEARL_PSHARE}]"
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
UARGS=(--auto "$TIME_WARP" --mode="$UAV_MODE")
UARGS+=(--mport="$UAV_MPORT" --pshare="$UAV_PSHARE")
UARGS+=(--shore_pshare="$SHORE_PSHARE" --start_pos="$UAV_START")
UARGS+=(--pearl_ip=localhost --pearl_pshare="$PEARL_PSHARE")
UARGS+=(--takeoff_altitude="$TAKEOFF_ALTITUDE")
[ -n "$AP_URL" ] && UARGS+=(--ap_url="$AP_URL")
[ -n "$AP_PROTOCOL" ] && UARGS+=(--ap_protocol="$AP_PROTOCOL")
[ "$JUST_MAKE" = "yes" ] && UARGS+=(--just_make)
[ "$VERBOSE" = "yes" ] && UARGS+=(--verbose)

PARGS=(--auto "$TIME_WARP" --mode="$PEARL_MODE")
PARGS+=(--mport="$PEARL_MPORT" --pshare="$PEARL_PSHARE")
PARGS+=(--shore_pshare="$SHORE_PSHARE" --start_pos="$PEARL_START")
PARGS+=(--uav_ip=localhost --uav_pshare="$UAV_PSHARE")
PARGS+=(--sim_attachment="$SIM_ATTACHMENT")
[ "$JUST_MAKE" = "yes" ] && PARGS+=(--just_make)
[ "$VERBOSE" = "yes" ] && PARGS+=(--verbose)

vecho "Launching UAV: ${UARGS[*]}"
./launch_uav.sh "${UARGS[@]}" || exit 1
vecho "Launching PEARL: ${PARGS[*]}"
./launch_pearl.sh "${PARGS[@]}" || exit 1

#------------------------------------------------------------
#  Part 7: Launch shoreside.
#------------------------------------------------------------
SARGS=(--auto "$TIME_WARP" --mode="$MODE")
SARGS+=(--mport="$SHORE_MPORT" --pshare="$SHORE_PSHARE")
SARGS+=(--uav_ip=localhost --uav_pshare="$UAV_PSHARE")
[ "$JUST_MAKE" = "yes" ] && SARGS+=(--just_make)
[ "$VERBOSE" = "yes" ] && SARGS+=(--verbose)
[ "$NOGUI" = "yes" ] && SARGS+=(--nogui)

vecho "Launching shoreside: ${SARGS[*]}"
./launch_shoreside.sh "${SARGS[@]}" || exit 1

if [ "$JUST_MAKE" = "yes" ]; then
  echo "$ME: Targ files made; exiting without launch."
  exit 0
fi

#------------------------------------------------------------
#  Part 8: Open the single operator uMAC session.
#------------------------------------------------------------
if [ "$XLAUNCHED" != "yes" ]; then
  uMAC --paused targ_shoreside.moos
  trap "" SIGINT
  echo; echo "$ME: Halting all apps"
  kill -- -$$
fi

exit 0
