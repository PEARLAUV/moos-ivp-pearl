#--------------------------------------------------------
# The CMakeLists.txt for:                 uSimPEARL
# Author(s):                              Ethan Rolland
#--------------------------------------------------------

# Set System Specific Libraries
if (${WIN32})
  # Windows Libraries
  SET(SYSTEM_LIBS
     wsock32 )
else (${WIN32})
  # Linux and Apple Libraries
  SET(SYSTEM_LIBS
      m
      pthread )
endif (${WIN32})

FILE(GLOB SRC *.cpp)

ADD_EXECUTABLE(uSimPEARL ${SRC})
   
TARGET_LINK_LIBRARIES(uSimPEARL
   ${MOOS_LIBRARIES}
   mbutil
   NMEAParse
   ${SYSTEM_LIBS} )

message("+++++++++++++++++++++++++++++++++++++++++")
message("SYSTEM_LIBS:" ${SYSTEM_LIBS})
message("+++++++++++++++++++++++++++++++++++++++++")