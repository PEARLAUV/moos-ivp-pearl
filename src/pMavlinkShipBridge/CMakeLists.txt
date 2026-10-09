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
  mavlink_transport
  apputil
  mbutil
  m
  pthread)
