cmake_minimum_required (VERSION 3.8)

project ("ym-smd-compression")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ym-smd-compression src/supported_games.cpp src/virgin-games/dune_impl.cpp)

target_include_directories(ym-smd-compression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(ym-smd-compression INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(ym-smd-compression PRIVATE ym-smd-io)

include(GNUInstallDirs)

install(TARGETS ym-smd-compression
    EXPORT ym-smd-compression-targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(EXPORT ym-smd-compression-targets
    FILE ym-smd-compression-targets.cmake
    NAMESPACE ym-smd-compression::
    DESTINATION share/ym-smd-compression
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

include(CMakePackageConfigHelpers)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ym-smd-compression-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ym-smd-compression-config.cmake"
    @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ym-smd-compression-config.cmake"
    DESTINATION share/ym-smd-compression
)