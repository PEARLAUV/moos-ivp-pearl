#!/bin/bash

PWD=`pwd`
BUILD_DIR=./build
HELP="no"
CLEAN="no"
PEARLOPTIONS="all"

#-------------------------------------------------------------------
# Define some terminal colors for convenience
#-------------------------------------------------------------------

txtrst=$(tput setaf 0)  # Reset
txtred=$(tput setaf 1)  # Red
txtgrn=$(tput setaf 2)  # Green
txtylw=$(tput setaf 3)  # Yellow
txtblu=$(tput setaf 4)  # Blue
txtpur=$(tput setaf 5)  # Purple
txtcyn=$(tput setaf 6)  # Cyan
txtwht=$(tput setaf 7)  # White


SCRIPT_ABS_DIR=`pwd`
PROJ4_INCLUDE_DIR="${SCRIPT_ABS_DIR}/MOOS/proj-4.8.0/include"

#-------------------------------------------------------------------
#  Part 1: Check for and handle command-line arguments
#-------------------------------------------------------------------
for ARGI; do
    if [ "${ARGI}" = "--help" ] ; then
	HELP="yes"
    fi
    if [ "${ARGI}" = "-h" ] ; then
	HELP="yes"
    fi
    if [ "${ARGI}" = "--clean" ] ; then
	CLEAN="yes"
    fi
    if [ "${ARGI}" = "clean" ] ; then
	CLEAN="yes"
    fi 
done

export PEARLOPTIONS=${PEARLOPTIONS}

#-------------------------------------------------------------------
#  Part 2: Handle the request for help
#-------------------------------------------------------------------

if [ "${HELP}" = "yes" ]; then
    printf "${txtblu}%s [SWITCHES]    \n" $0
    printf "Switches:                           \n" 
    printf "  --clean, clean  no build, remove files created during build \n"
    printf "  --help, -h      display help      \n" 
    printf "Note:                              \n"
    printf "  There is no provision to pass arguments to \"make\".\n"
    exit 0;
fi


#-------------------------------------------------------------------
#  Part 3: Handle a request to for make clean. Most cleaning handled
#          by the invocation of make clean, but some extras are done.
#-------------------------------------------------------------------

if [ "${CLEAN}" = "yes" ]; then
    cd ${BUILD_DIR}
    make clean
    rm -rf CMakeFiles/ CMakeCache.txt Makefile src/ cmake_install.cmake
    cd ${PWD}
    exit 0;
fi

#-------------------------------------------------------------------
#  Part 4: Invoke the call to make in the build directory
#-------------------------------------------------------------------

cd ${BUILD_DIR}
cmake -DPROJ4_INCLUDE_DIRS=${PROJ4_INCLUDE_DIR}  .. 
make 
cd ${PWD}


############################################################
#                   END of build.sh
############################################################

