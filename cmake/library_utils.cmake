function(create_ym_library project_name)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    add_library(${project_name})

    target_include_directories(${project_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(${project_name} INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                                $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

    include(GNUInstallDirs)

    install(TARGETS ${project_name}
        EXPORT ${project_name}-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

    install(EXPORT ${project_name}-targets
        FILE ${project_name}-targets.cmake
        NAMESPACE ${project_name}::
        DESTINATION share/${project_name}
    )

    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    include(CMakePackageConfigHelpers)

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${project_name}-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${project_name}-config.cmake"
        @ONLY
    )

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${project_name}-config.cmake"
        DESTINATION share/${project_name}
    )
endfunction()