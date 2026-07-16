add_executable(ym-smd-unit-tests unit_tests.cpp)
target_link_libraries(ym-smd-unit-tests PRIVATE ym-smd)
# The round-trip test drives the internal LZSS compressor directly.
target_include_directories(ym-smd-unit-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../smd/src)

add_test(NAME unit COMMAND ym-smd-unit-tests)

add_executable(ym-smd-rom-tests rom_tests.cpp)
target_link_libraries(ym-smd-rom-tests PRIVATE ym-smd)

add_test(NAME rom COMMAND ym-smd-rom-tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(rom PROPERTIES SKIP_RETURN_CODE 77)
