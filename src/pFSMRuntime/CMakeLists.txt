#--------------------------------------------------------
# The CMakeLists.txt for:                       pFSMRuntime
#--------------------------------------------------------

SET(SRC
  FSMRuntime.cpp
  FSMRuntime_Info.cpp
  main.cpp
)

ADD_EXECUTABLE(pFSMRuntime ${SRC})

TARGET_LINK_LIBRARIES(pFSMRuntime
  ${MOOS_LIBRARIES}
  apputil
  logic
  mbutil
  m
  pthread)
