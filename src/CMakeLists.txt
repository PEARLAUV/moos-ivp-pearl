##############################################################################
# FILE:        moos-ivp-pearl/src/CMakeLists.txt
# DATE:        01/14/2021
# DESCRIPTION: CMakeLists.txt file for the moos-ivp-pearl source directory
##############################################################################


#============================================================================
# Add the libraries in the current directory to the include path
#============================================================================
FILE(GLOB LOCAL_LIBRARY_DIRS ./lib_* ../lib)
INCLUDE_DIRECTORIES(${LOCAL_LIBRARY_DIRS})
LINK_DIRECTORIES(${LOCAL_LIBRARY_DIRS})

find_path(MODBUS_INCLUDE_DIR NAMES modbus/modbus.h)
find_library(MODBUS_LIBRARY NAMES modbus)
find_path(PINGCPP_INCLUDE_DIR NAMES ping-device-ping1d.h)
find_library(PINGCPP_DEVICE_LIBRARY NAMES DEVICE)
find_library(PINGCPP_HAL_LIBRARY NAMES HAL)

SET (PEARL-OPTIONS $ENV{PEARLOPTIONS})

#============================================================================
# List the subdirectories to build...
#============================================================================
IF ( PEARL-OPTIONS STREQUAL all )
     ADD_SUBDIRECTORY(lib_NMEAParse)
     ADD_SUBDIRECTORY(lib_SimpleSerial)
     ADD_SUBDIRECTORY(lib_gpsParser)
#    ADD_SUBDIRECTORY(lib_NMEA2000)
     ADD_SUBDIRECTORY(iGPS)
     ADD_SUBDIRECTORY(iDualGPS)
     ADD_SUBDIRECTORY(iPEARL)
     ADD_SUBDIRECTORY(pAUVdock)
     ADD_SUBDIRECTORY(pGenPath)
     ADD_SUBDIRECTORY(pRouteBuffer)
     ADD_SUBDIRECTORY(pThrustMixer)
     ADD_SUBDIRECTORY(pTakeoffGate)
     ADD_SUBDIRECTORY(pRendezvous)
     ADD_SUBDIRECTORY(pPearlSunPlan)
     ADD_SUBDIRECTORY(pSimSOC)
     ADD_SUBDIRECTORY(uSimAttachment)
#     ADD_SUBDIRECTORY(iChargeController)
#     ADD_SUBDIRECTORY(iGarmin)
     ADD_SUBDIRECTORY(uSunTracking)
     ADD_SUBDIRECTORY(pPearlPID)
     ADD_SUBDIRECTORY(iRPISerial)
     ADD_SUBDIRECTORY(iSherlockTelemetry)
     ADD_SUBDIRECTORY(pOdometry)
     ADD_SUBDIRECTORY(pTargetCPA)
#     ADD_SUBDIRECTORY(iM300)
     ADD_SUBDIRECTORY(iM300Health)
#     ADD_SUBDIRECTORY(lib_sock_utils)
     # Old behavior skipped iBlueRoboticsPing in the all build.
     # ADD_SUBDIRECTORY(iBlueRoboticsPing)
     ADD_SUBDIRECTORY(iBlueRoboticsPing)

ELSEIF ( PEARL-OPTIONS STREQUAL mac )
     ADD_SUBDIRECTORY(lib_NMEAParse)
     ADD_SUBDIRECTORY(lib_SimpleSerial)
     ADD_SUBDIRECTORY(lib_gpsParser)
     ADD_SUBDIRECTORY(iGPS)
     ADD_SUBDIRECTORY(iDualGPS)
     ADD_SUBDIRECTORY(iPEARL)
     ADD_SUBDIRECTORY(pAUVdock)
     ADD_SUBDIRECTORY(pRouteBuffer)
     ADD_SUBDIRECTORY(pThrustMixer)
     ADD_SUBDIRECTORY(pTakeoffGate)
     ADD_SUBDIRECTORY(pRendezvous)
     ADD_SUBDIRECTORY(pPearlSunPlan)
     ADD_SUBDIRECTORY(pSimSOC)
     ADD_SUBDIRECTORY(uSimAttachment)
     IF (MODBUS_INCLUDE_DIR AND MODBUS_LIBRARY)
         INCLUDE_DIRECTORIES(${MODBUS_INCLUDE_DIR})
         ADD_SUBDIRECTORY(iChargeController)
     ELSE ()
         MESSAGE(WARNING "Skipping iChargeController because libmodbus was not found.")
     ENDIF ()
     ADD_SUBDIRECTORY(uSunTracking)
     ADD_SUBDIRECTORY(pPearlPID)
     ADD_SUBDIRECTORY(iRPISerial)
     ADD_SUBDIRECTORY(iSherlockTelemetry)
     # Old behavior required ping-cpp headers. The app now reads the local
     # Ping1D daemon state endpoint instead of opening sonar serial directly.
     # IF (PINGCPP_INCLUDE_DIR)
     #     INCLUDE_DIRECTORIES(${PINGCPP_INCLUDE_DIR})
     #     ADD_SUBDIRECTORY(iBlueRoboticsPing)
     # ELSE ()
     #     MESSAGE(WARNING "Skipping iBlueRoboticsPing because ping-cpp headers were not found.")
     # ENDIF ()
     ADD_SUBDIRECTORY(iBlueRoboticsPing)
ENDIF ()

##############################################################################
#                           END of CMakeLists.txt
##############################################################################
