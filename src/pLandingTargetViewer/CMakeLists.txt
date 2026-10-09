ADD_EXECUTABLE(pLandingTargetViewer LandingTargetViewer.cpp main.cpp)
TARGET_LINK_LIBRARIES(pLandingTargetViewer ${MOOS_LIBRARIES} MOOSGeodesy apputil mbutil m pthread)
