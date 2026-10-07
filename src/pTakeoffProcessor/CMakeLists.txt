add_executable(pTakeoffProcessor TakeoffProcessor.cpp main.cpp)
target_link_libraries(pTakeoffProcessor ${MOOS_LIBRARIES} geometry apputil logic mbutil m pthread)
