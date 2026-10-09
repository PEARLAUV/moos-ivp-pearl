ADD_EXECUTABLE(pMavlinkLandingTarget MavlinkLandingTarget.cpp main.cpp)
TARGET_LINK_LIBRARIES(pMavlinkLandingTarget ${MOOS_LIBRARIES} MOOSGeodesy mavlink_transport apputil logic mbutil m pthread)
