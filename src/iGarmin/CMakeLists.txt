#--------------------------------
# The CMakeLists.txt for: iGarmin
#--------------------------------

FILE(GLOB SRC *.cpp)

ADD_EXECUTABLE(iGarmin ${SRC})

TARGET_LINK_LIBRARIES(iGarmin
${MOOS_LIBRARIES}
mbutil
pthread
${SYSTEM_LIBS} )