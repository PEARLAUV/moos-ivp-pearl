set(SRC uChecks.cpp main.cpp)
add_executable(uChecks ${SRC})
target_link_libraries(uChecks ${MOOS_LIBRARIES} apputil logic mbutil m pthread)
