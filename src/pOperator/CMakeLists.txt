find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

set(SRC
  Operator.cpp
  main.cpp
)

add_executable(pOperator ${SRC})
target_include_directories(pOperator PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_libraries(pOperator
  ${MOOS_LIBRARIES}
  apputil
  mbutil
  m
  pthread
  ${JSONCPP_LIBRARIES}
)
set_property(TARGET pOperator PROPERTY CXX_STANDARD 11)
