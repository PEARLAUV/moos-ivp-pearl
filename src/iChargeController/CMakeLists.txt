#------------------------------------------
# The CMakeLists.txt for: iChargeController
#------------------------------------------

FILE(GLOB SRC *.cpp)

ADD_EXECUTABLE(iChargeController ${SRC})

TARGET_LINK_LIBRARIES(iChargeController
   ${MOOS_LIBRARIES}
   mbutil
   pthread
   modbus
   ${SYSTEM_LIBS} )