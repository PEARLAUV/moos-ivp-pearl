#--------------------------------------------------------
# The CMakeLists.txt for:                         iGPSSim
#--------------------------------------------------------

FILE(GLOB SRC *.cpp)

ADD_EXECUTABLE(iGPSSim ${SRC})
   
TARGET_LINK_LIBRARIES(iGPSSim
   ${MOOS_LIBRARIES}
   ${MOOSGeodesy_LIBRARIES}
   NMEAParse
   SimpleSerial
   mbutil
   m
   pthread)
