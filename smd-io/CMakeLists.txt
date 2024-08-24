cmake_minimum_required (VERSION 3.8)

project ("ym-smd-io")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ym-smd-io src/binary_data_reader.cpp "src/binary_file_reader.cpp")

target_include_directories(ym-smd-io PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(ym-smd-io INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)