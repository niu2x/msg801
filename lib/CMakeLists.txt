include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(msg801 SHARED
    src/lib.cpp
)

target_include_directories(msg801
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(msg801
    PROPERTIES
        EXPORT_NAME msg801
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
)

cxx_project_preset(msg801)
warning_as_error_enable(msg801)

configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/msg801-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/msg801-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/msg801
)

write_basic_config_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/msg801-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(TARGETS msg801
    EXPORT msg801Targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
        PATTERN "*.hpp"
)

install(EXPORT msg801Targets
    FILE msg801-targets.cmake
    NAMESPACE msg801::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/msg801
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/msg801-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/msg801-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/msg801
)
