cmake_minimum_required (VERSION 3.8)

include(${CMAKE_SOURCE_DIR}/cmake/library_utils.cmake)

create_ym_library("ym-smd-io")

target_sources(ym-smd-io PRIVATE "src/binary_data_reader.cpp" "src/binary_file_reader.cpp")