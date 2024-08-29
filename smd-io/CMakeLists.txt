cmake_minimum_required (VERSION 3.8)

project ("ym-smd-io")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ym-smd-io "src/binary_data_reader.cpp" "src/binary_file_reader.cpp")

target_include_directories(ym-smd-io PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(ym-smd-io INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

include(GNUInstallDirs)

install(TARGETS ym-smd-io
    EXPORT ym-smd-io-targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(EXPORT ym-smd-io-targets
    FILE ym-smd-io-targets.cmake
    NAMESPACE ym-smd-io::
    DESTINATION share/ym-smd-io
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

include(CMakePackageConfigHelpers)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ym-smd-io-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ym-smd-io-config.cmake"
    @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ym-smd-io-config.cmake"
    DESTINATION share/ym-smd-io
)