SET(SRC
  LandingCoordinator.cpp
  main.cpp
)

ADD_EXECUTABLE(pLandingCoordinator ${SRC})
TARGET_LINK_LIBRARIES(pLandingCoordinator
  ${MOOS_LIBRARIES}
  apputil
  logic
  mbutil
  m
  pthread)
