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

SET (PEARL-OPTIONS $ENV{PEARLOPTIONS})

#============================================================================
# List the subdirectories to build...
#============================================================================
IF ( PEARL-OPTIONS STREQUAL all )
     ADD_SUBDIRECTORY(lib_NMEAParse)
     ADD_SUBDIRECTORY(lib_SimpleSerial)
     ADD_SUBDIRECTORY(lib_gpsParser)
     ADD_SUBDIRECTORY(lib_NMEA2000)
     ADD_SUBDIRECTORY(iGPS)
     ADD_SUBDIRECTORY(iDualGPS)
     ADD_SUBDIRECTORY(iPEARL)
     ADD_SUBDIRECTORY(iChargeController)
     ADD_SUBDIRECTORY(iGarmin)
     ADD_SUBDIRECTORY(uSunTracking)
     ADD_SUBDIRECTORY(pPearlPID)
     ADD_SUBDIRECTORY(iRPISerial)
ELSE IF ( PEARL-OPTIONS STREQUAL mac )
     ADD_SUBDIRECTORY(lib_NMEAParse)
     ADD_SUBDIRECTORY(lib_SimpleSerial)
     ADD_SUBDIRECTORY(lib_gpsParser)
     ADD_SUBDIRECTORY(iGPS)
     ADD_SUBDIRECTORY(iDualGPS)
     ADD_SUBDIRECTORY(iPEARL)
     ADD_SUBDIRECTORY(iChargeController)
     ADD_SUBDIRECTORY(uSunTracking)
     ADD_SUBDIRECTORY(pPearlPID)
     ADD_SUBDIRECTORY(iRPISerial)
ENDIF ()

##############################################################################
#                           END of CMakeLists.txt
##############################################################################

