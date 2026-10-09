#!/bin/bash
# Remove generated targets and logs from this mission directory.
cd "$(dirname "$0")" || exit 1

rm -rf -- MOOSLog_* XLOG_* LOG_*
rm -f  -- ./*~ targ_* .LastOpenedMOOSLogDirectory .mem_info* .checkvars
