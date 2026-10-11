add_executable(pVideoRecorder
  VideoRecorder.cpp
  main.cpp
)

target_link_libraries(pVideoRecorder
  ${MOOS_LIBRARIES}
  apputil
  mbutil
  m
  pthread
)
