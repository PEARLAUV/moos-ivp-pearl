#!/bin/bash -e
#--------------------------------------------------------------
#   Script: launch_shoreside.sh                                    
#   Author: Michael Benjamin  
#     Date: April 2020     
#--------------------------------------------------------------  
#  Part 1: Declare global var defaults
#--------------------------------------------------------------
TIME_WARP=1
JUST_MAKE="no"
AUTO=""
REGION="pleasant_bay"
SHORE_IP="192.168.20.158"  #IP address of the shoreside laptop
SHORESIDE_PORT="9000"
SHORE_LISTEN="9300"

#--------------------------------------------------------------
#  Part 2: Check for and handle command-line arguments
#--------------------------------------------------------------
for ARGI; do
    echo "Arg:["$ARGI"]"
    if [ "${ARGI}" = "--help" -o "${ARGI}" = "-h" ] ; then
	echo "launch_shoreside.sh [SWITCHES] [time_warp]        "
	echo "  --just_make, -j                                 "
	echo "  --auto, -a        Auto-launched. uMAC not used. "
	echo "  --pleasant_bay    Set region to be Pleasant Bay (Default) "
	echo "  --ip=<addr>       (Default is 192.168.20.158)   "
	echo "  --sport=<port>    (Default is 9000)             "
	echo "  --slisten=<port>  (Default is 9300)             " 
        echo "  --help, -h                                      "
	exit 0;
    elif [ "${ARGI//[^0-9]/}" = "$ARGI" -a "$TIME_WARP" = 1 ]; then 
        TIME_WARP=$ARGI
    elif [ "${ARGI}" = "--just_make" -o "${ARGI}" = "-j" ] ; then
	JUST_MAKE="yes"
    elif [ "${ARGI}" = "--auto" -o "${ARGI}" = "-a" ]; then
        AUTO="yes"
    elif [ "${ARGI}" = "--pleasant_bay" ]; then
        REGION="pleasant_bay"
    elif [ "${ARGI:0:5}" = "--ip=" ]; then
        SHORE_IP="${ARGI#--ip=*}"
    elif [ "${ARGI:0:8}" = "--sport=" ]; then
	SHORESIDE_PORT="${ARGI#--sport=*}"
    elif [ "${ARGI:0:10}" = "--slisten=" ]; then
        SHORE_LISTEN="${ARGI#--slisten=*}"
    else 
	echo "launch_shoreside.sh: Bad Arg: " $ARGI
	exit 1
    fi
done

#--------------------------------------------------------------
#  Part 3: Create the .moos and .bhv files using nsplug
#--------------------------------------------------------------
# What is nsplug? Type "nsplug --help" or "nsplug --manual"

NSFLAGS="-s -f"
if [ "${AUTO}" = "" ]; then
    NSFLAGS="-i -f"
fi
nsplug meta_shoreside.moos targ_shoreside.moos $NSFLAGS   \
       WARP=$TIME_WARP             \
       REGION=$REGION              \
       SHORE_IP=$SHORE_IP          \
       SHARE_LISTEN=$SHORE_LISTEN  \
       SPORT=$SHORESIDE_PORT       

if [ ${JUST_MAKE} = "yes" ] ; then
    exit 0
fi

#--------------------------------------------------------------
#  Part 4: Launch the processes
#--------------------------------------------------------------
echo "Launching Shoreside MOOS Community WARP:" $TIME_WARP
pAntler targ_shoreside.moos >& /dev/null &
echo "Done launching shoreside"

#-------------------------------------------------------------- 
#  Part 5: Unless auto-launched, launch uMAC until mission quit          
#-------------------------------------------------------------- 
if [ "${AUTO}" = "" ]; then
    uMAC targ_shoreside.moos
    kill -- -$$
fi
