#--------------------------------------------------------
# The CMakeLists.txt for:                      pPearlPID
# Author(s):                                Mike Benjamin
#--------------------------------------------------------

FILE(GLOB SRC *.cpp)

ADD_EXECUTABLE(pPearlPID ${SRC})
   
TARGET_LINK_LIBRARIES(pPearlPID 
  ${MOOS_LIBRARIES}
  mbutil
  ${SYSTEM_LIBS})

