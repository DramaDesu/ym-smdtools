cmake_minimum_required (VERSION 3.8)

project ("ym-smd-tools")

option(BUILD_TESTS "Build tests" ON)

add_subdirectory(smd-io)
add_subdirectory(smd-compression)
add_subdirectory(smd-gems)

if (BUILD_TESTS)
	enable_testing()
	add_subdirectory(test)
endif()