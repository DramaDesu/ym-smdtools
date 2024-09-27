cmake_minimum_required (VERSION 3.8)

include(${CMAKE_SOURCE_DIR}/cmake/library_utils.cmake)

create_ym_library(ym-smd-gems)

target_sources(ym-smd-gems PRIVATE src/loader.cpp "src/midi.cpp")

target_link_libraries(ym-smd-gems PRIVATE ym-smd-io)