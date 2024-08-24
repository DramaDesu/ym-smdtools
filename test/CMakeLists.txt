cmake_minimum_required (VERSION 3.8)

project ("ym-uncompression-test")

set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ym-uncompression-test "uncompression_test.cpp")

target_link_libraries(ym-uncompression-test PRIVATE ym-smd-io)
target_link_libraries(ym-uncompression-test PRIVATE ym-smd-compression)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dune_path.txt ${CMAKE_CURRENT_BINARY_DIR}/dune_path.txt COPYONLY)