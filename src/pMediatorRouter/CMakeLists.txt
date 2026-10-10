add_executable(pMediatorRouter MediatorRouter.cpp main.cpp)
target_link_libraries(pMediatorRouter ${MOOS_LIBRARIES} apputil mbutil m pthread)
