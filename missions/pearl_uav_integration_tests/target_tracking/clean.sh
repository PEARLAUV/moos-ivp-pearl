#!/usr/bin/env bash
# Remove generated configuration and logs from this mission directory.
set -euo pipefail
cd "$(dirname "$0")"

rm -rf -- MOOSLog_* XLOG_* LOG_*
rm -f -- ./*~ targ_* .LastOpenedMOOSLogDirectory .mem_info* .checkvars
