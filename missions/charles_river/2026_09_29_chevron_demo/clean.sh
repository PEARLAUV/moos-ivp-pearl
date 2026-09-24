#!/bin/bash
#--------------------------------------------------------------
#   Script: clean.sh
#  Mission: aug_pearl_uav
#   Author: Charles Benjamin
#--------------------------------------------------------------
RM_FLAGS=()
MISSION_DIR=$(cd "$(dirname "$0")" && pwd)
cd "$MISSION_DIR" || exit 1

for ARGI; do
  if [[ "${ARGI}" = "--help" || "${ARGI}" = "-h" ]]; then
    echo "clean.sh [OPTIONS]"
    echo "  --verbose, -v"
    echo "  --help, -h"
    exit 0
  elif [[ "${ARGI}" = "--verbose" || "${ARGI}" = "-v" ]]; then
    RM_FLAGS=(-v)
  else
    echo "clean.sh: Bad Arg:[$ARGI]. Exit Code 1."
    exit 1
  fi
done

if [ "${#RM_FLAGS[@]}" -gt 0 ]; then
  echo "Cleaning: $PWD"
fi

rm -rf "${RM_FLAGS[@]}" -- MOOSLog_* XLOG_* LOG_*
rm -f  "${RM_FLAGS[@]}" -- ./*~ targ_* tmp_* *.moosx *.bhvx
rm -f  "${RM_FLAGS[@]}" -- .LastOpenedMOOSLogDirectory .mem_info* .checkvars
