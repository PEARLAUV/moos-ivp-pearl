#----------------------------------------------------
# The CMakeLists.txt for:                lib_NMEA2000
#----------------------------------------------------

FILE(GLOB SRC *.cpp)

FILE(GLOB HEADERS *.h)

# Build Library
ADD_LIBRARY(NMEA2000 ${SRC})

# Install Headers
#INSTALL( FILES ${HEADERS} DESTINATION ${CMAKE_INSTALL_PREFIX}/include/lib_NMEA2000)

# Install Libararies
INSTALL( TARGETS NMEA2000
   RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
   LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
   ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
)