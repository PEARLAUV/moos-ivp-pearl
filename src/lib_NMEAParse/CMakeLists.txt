#--------------------------------------------------------
# The CMakeLists.txt for:                   lib_NMEAParse
# Author(s):                                   Alon Yaari
#--------------------------------------------------------

FILE(GLOB SRC *.cpp)

FILE(GLOB HEADERS *.h)

# Build Library
ADD_LIBRARY(NMEAParse ${SRC})

# Install Headers
#INSTALL( FILES ${HEADERS} DESTINATION ${CMAKE_INSTALL_PREFIX}/include/lib_NMEAParse)

# Install Libararies
INSTALL( TARGETS NMEAParse
   RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
   LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
   ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
)
