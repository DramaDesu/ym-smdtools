add_library(ym-smd
    src/rom_image.cpp
    src/virgin_lz.cpp
    src/ancient_lzss.cpp
    src/lzss/LZSS.c
)
add_library(ym-smd::ym-smd ALIAS ym-smd)

target_compile_features(ym-smd PUBLIC cxx_std_20)

target_include_directories(ym-smd
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

include(GNUInstallDirs)

install(TARGETS ym-smd
    EXPORT ym-smd-targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(EXPORT ym-smd-targets
    FILE ym-smd-targets.cmake
    NAMESPACE ym-smd::
    DESTINATION share/ym-smd)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

include(CMakePackageConfigHelpers)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ym-smd-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ym-smd-config.cmake"
    @ONLY)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ym-smd-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ym-smd-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ym-smd-config-version.cmake"
    DESTINATION share/ym-smd)
