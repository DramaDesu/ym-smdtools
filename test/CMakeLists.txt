cmake_minimum_required (VERSION 3.8)

project ("ym-uncompression-test")

set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(create_test_executable target_name)
    cmake_parse_arguments(ARG "" "TARGET" "SOURCES;LIBS" ${ARGN})

    add_executable(${target_name} ${ARG_SOURCES})

    target_link_libraries(${target_name} PRIVATE ${ARG_LIBS})

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dune_path.txt 
                   ${CMAKE_CURRENT_BINARY_DIR}/dune_path.txt 
                   COPYONLY)
endfunction()

create_test_executable(
    ym-uncompression-test
    SOURCES uncompression_test.cpp
    LIBS ym-smd-io ym-smd-compression
)

create_test_executable(
    ym-gems-test
    SOURCES convert_gems_to_midi.cpp
    LIBS ym-smd-io ym-smd-gems
)