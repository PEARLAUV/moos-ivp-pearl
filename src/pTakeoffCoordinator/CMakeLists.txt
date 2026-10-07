add_executable(pTakeoffCoordinator TakeoffCoordinator.cpp main.cpp)
target_link_libraries(pTakeoffCoordinator ${MOOS_LIBRARIES} apputil logic mbutil m pthread)
