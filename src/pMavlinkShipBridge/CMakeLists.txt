#--------------------------------------------------------
# The CMakeLists.txt for: pMavlinkShipBridge
#--------------------------------------------------------

SET(SRC
  MavlinkShipBridge.cpp
  main.cpp
)

ADD_EXECUTABLE(pMavlinkShipBridge ${SRC})

TARGET_LINK_LIBRARIES(pMavlinkShipBridge
  ${MOOS_LIBRARIES}
  apputil
  mbutil
  m
  pthread)
