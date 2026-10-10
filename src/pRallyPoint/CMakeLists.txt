ADD_EXECUTABLE(pRallyPoint RallyPoint.cpp main.cpp)
TARGET_COMPILE_FEATURES(pRallyPoint PRIVATE cxx_std_17)
TARGET_LINK_LIBRARIES(pRallyPoint ${MOOS_LIBRARIES} contacts apputil geometry mbutil m pthread)
