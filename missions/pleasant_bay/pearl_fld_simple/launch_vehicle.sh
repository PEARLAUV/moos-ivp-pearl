#!/bin/bash -e
#-------------------------------------------------------------- 
#   Script: launch_vehicle.sh                                    
#   Author: Michael Benjamin  
#     Date: April 2020     
#--------------------------------------------------------------
#  Part 1: Declare global var defaults
#--------------------------------------------------------------
TIME_WARP=1
JUST_MAKE="no"
AUTO=""
VNAME="pearl"
REGION="pleasant_bay"
START_POS="0,0"  

PEARL_IP="192.168.20.137"  #IP address of the RPi on PEARL
SHORE_IP="192.168.20.158"  #IP address of the shoreside laptop
VEHICLE_PORT="9001"
VEHICLE_LISTEN="9301"
SHORE_LISTEN="9300"

CRUISESPEED="0.5"  #speed to traverse waypoints in m/s

#--------------------------------------------------------------
#  Part 2: Check for and handle command-line arguments
#--------------------------------------------------------------
for ARGI; do
    if [ "${ARGI}" = "--help" -o "${ARGI}" = "-h" ]; then
	echo "launch_vehicle.sh [SWITCHES] [time_warp]               "
	echo "  --just_make, -j                                      "
	echo "  --auto, -a        Auto-launched. uMAC not used.      "
	echo "  --vname=VNAME     Vehicle name (Default is 'pearl')  "
	echo "  --pleasant_bay    Set region to be Pleasant Bay (Default) "
	echo "  --startpos=X,Y    (Default is 0,0)                   "
	echo "  --ip=<addr>       (Default is 192.168.20.137)        "
	echo "  --shore=<addr>    (Default is 192.168.20.158)        "
	echo "  --vport=<port>    (Default is 9001)                  "
	echo "  --vlisten=<port>  (Default is 9301)                  "
	echo "  --slisten=<port>  (Default is 9300)                  "
	echo "  --help, -h                                           " 
	exit 0;
    elif [ "${ARGI//[^0-9]/}" = "$ARGI" -a "$TIME_WARP" = 1 ]; then 
        TIME_WARP=$ARGI
    elif [ "${ARGI}" = "--just_make" -o "${ARGI}" = "-j" ]; then
	JUST_MAKE="yes"
    elif [ "${ARGI}" = "--auto" -o "${ARGI}" = "-a" ]; then
        AUTO="yes"
    elif [ "${ARGI:0:8}" = "--vname=" ]; then
        VNAME="${ARGI#--vname=*}"
    elif [ "${ARGI}" = "--pleasant_bay" ]; then
        REGION="pleasant_bay"
    elif [ "${ARGI:0:11}" = "--startpos=" ]; then
        START_POS="${ARGI#--startpos=*}"
    elif [ "${ARGI:0:5}" = "--ip=" ]; then
        PEARL_IP="${ARGI#--ip=*}"
    elif [ "${ARGI:0:8}" = "--shore=" ]; then
        SHORE_IP="${ARGI#--shore=*}"
    elif [ "${ARGI:0:8}" = "--vport=" ]; then
	VEHICLE_PORT="${ARGI#--vport=*}"
    elif [ "${ARGI:0:10}" = "--vlisten=" ]; then
	VEHICLE_LISTEN="${ARGI#--vlisten=*}"
    elif [ "${ARGI:0:10}" = "--slisten=" ]; then
        SHORE_LISTEN="${ARGI#--slisten=*}"
    else 
	echo "launch_vehicle.sh: Bad Arg: " $ARGI
	exit 1
    fi
done


#--------------------------------------------------------------
#  Part 3: Create the .moos and .bhv files. 
#--------------------------------------------------------------
# What is nsplug? Type "nsplug --help" or "nsplug --manual"

NSFLAGS="-s -f"
if [ "${AUTO}" = "" ]; then
    NSFLAGS="-i -f"
fi

nsplug meta_vehicle.moos targ_$VNAME.moos $NSFLAGS  \
       WARP=$TIME_WARP               \
       VNAME=$VNAME                  \
       REGION=$REGION                \
       START_POS=$START_POS          \
       PEARL_IP=$PEARL_IP            \
       SHORE_IP=$SHORE_IP            \
       VPORT=$VEHICLE_PORT           \
       SHARE_LISTEN=$VEHICLE_LISTEN  \
       SHORE_LISTEN=$SHORE_LISTEN    


nsplug meta_vehicle.bhv targ_$VNAME.bhv $NSFLAGS  \
       VNAME=$VNAME                  \
       START_POS=$START_POS          \
       REGION=$REGION                \
       SPEED=$CRUISESPEED            \
       ORDER="normal"

if [ ${JUST_MAKE} = "yes" ] ; then
    exit 0
fi

#--------------------------------------------------------------
#  Part 4: Launch the processes
#--------------------------------------------------------------
echo "Launching $VNAME MOOS Community, WARP:" $TIME_WARP
pAntler targ_$VNAME.moos >& /dev/null &
echo "Done Launching the vehicle mission."

#-------------------------------------------------------------- 
#  Part 5: Unless auto-launched, launch uMAC until mission quit          
#-------------------------------------------------------------- 
if [ "${AUTO}" = "" ]; then
    uMAC targ_$VNAME.moos
    kill -- -$$
fi
