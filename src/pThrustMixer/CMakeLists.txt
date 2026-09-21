#--------------------------------------------------------
# The CMakeLists.txt for: pThrustMixer
#--------------------------------------------------------

SET(SRC
  ThrustMixer.cpp
  ThrustMixer_Info.cpp
  main.cpp
)

ADD_EXECUTABLE(pThrustMixer ${SRC})

TARGET_LINK_LIBRARIES(pThrustMixer
  ${MOOS_LIBRARIES}
  apputil
  mbutil
  m
  pthread)
