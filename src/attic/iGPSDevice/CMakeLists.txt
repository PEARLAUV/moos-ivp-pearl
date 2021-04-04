#--------------------------------------------------------
# The CMakeLists.txt for:                      iGPSDevice
#--------------------------------------------------------

FILE(GLOB SRC *.cpp)

ADD_EXECUTABLE(iGPSDevice ${SRC})
   
TARGET_LINK_LIBRARIES(iGPSDevice
   ${MOOS_LIBRARIES}
   ${MOOSGeodesy_LIBRARIES}
   gpsParser
   SimpleSerial
   mbutil
   m
   pthread)
