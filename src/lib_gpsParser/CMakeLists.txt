#--------------------------------------------------------
# The CMakeLists.txt for:                   lib_gpsParser
# Author(s):                                   Alon Yaari
#--------------------------------------------------------

FILE(GLOB SRC *.cpp)

FILE(GLOB HEADERS *.h)

# Build Library
ADD_LIBRARY(gpsParser ${SRC})

# Install Headers
#INSTALL( FILES ${HEADERS} DESTINATION ${CMAKE_INSTALL_PREFIX}/include/lib_gpsParser)

# Install Libararies
INSTALL( TARGETS gpsParser
   RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
   LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
   ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
)

TARGET_LINK_LIBRARIES(gpsParser
  NMEAParse
   ${SYSTEM_LIBS} )