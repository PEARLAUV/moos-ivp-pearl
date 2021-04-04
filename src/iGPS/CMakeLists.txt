#-----------------------------------------
# The CMakeLists.txt for:             iGPS
#-----------------------------------------

FILE(GLOB SRC *.cpp)

ADD_EXECUTABLE(iGPS ${SRC})

TARGET_LINK_LIBRARIES(iGPS
   ${MOOS_LIBRARIES}
   ${MOOSGeodesy_LIBRARIES}
   gpsParser
   SimpleSerial
   mbutil
   m
   pthread)
