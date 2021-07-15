#----------------------------------
# The CMakeLists.txt for:  iDualGPS
#----------------------------------

FILE(GLOB SRC *.cpp)

ADD_EXECUTABLE(iDualGPS ${SRC})

TARGET_LINK_LIBRARIES(iDualGPS
	${MOOS_LIBRARIES}
	${MOOSGeodesy_LIBRARIES}
	gpsParser
	SimpleSerial
	mbutil
	m
	pthread)