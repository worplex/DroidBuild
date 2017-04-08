add_executable(DroidBuild build.cpp)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
include_directories(${EXTRA_HEADERS})
target_link_libraries(DroidBuild pthread dl)
