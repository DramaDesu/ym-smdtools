cmake_minimum_required(VERSION 3.16)

project("ym-smd-tools" VERSION 0.2.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(YM_SMD_IS_TOP_LEVEL ON)
else()
	set(YM_SMD_IS_TOP_LEVEL OFF)
endif()

option(BUILD_TESTS "Build tests" ${YM_SMD_IS_TOP_LEVEL})

add_subdirectory(smd)

if(BUILD_TESTS)
	enable_testing()
	add_subdirectory(test)
endif()
