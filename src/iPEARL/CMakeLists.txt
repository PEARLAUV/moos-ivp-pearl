#-----------------------------------------
# The CMakeLists.txt for:           iPEARL
#-----------------------------------------

FILE(GLOB SRC *.cpp)

ADD_EXECUTABLE(iPEARL ${SRC})

TARGET_LINK_LIBRARIES(iPEARL
   ${MOOS_LIBRARIES}
   SimpleSerial
   NMEAParse
   mbutil
   geometry
   ${SYSTEM_LIBS} )