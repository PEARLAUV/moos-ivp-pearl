#------------------------------------------
# The CMakeLists.txt for: uSunTracking
#------------------------------------------

FILE(GLOB SRC *.cpp)

ADD_EXECUTABLE(uSunTracking ${SRC})

TARGET_LINK_LIBRARIES(uSunTracking
   ${MOOS_LIBRARIES}
   SimpleSerial
   mbutil
   ${SYSTEM_LIBS} )