include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(MSG801_LIBRARY_TYPE STATIC)
if(MSG801_BUILD_SHARED)
    set(MSG801_LIBRARY_TYPE SHARED)
endif()

add_library(msg801 ${MSG801_LIBRARY_TYPE}
    src/lib.cpp
    src/udp_sender.cpp
    src/udp_server.cpp
    src/tunnel.cpp
    src/cfb.cpp
    src/padding.cpp
    src/processor_chain.cpp
    src/identity.cpp
)

target_include_directories(msg801
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/spdlog
)

include(GenerateExportHeader)
generate_export_header(msg801
    BASE_NAME msg801
    EXPORT_MACRO_NAME MSG801_API
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/msg801/export.hpp
)

set_target_properties(msg801
    PROPERTIES
        EXPORT_NAME msg801
        VERSION ${PROJECT_VERSION}
)

if(MSG801_BUILD_SHARED)
    set_target_properties(msg801 PROPERTIES
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
endif()

target_link_libraries(msg801 PUBLIC Boost::boost Threads::Threads)
target_link_libraries(msg801 PRIVATE OpenSSL::SSL OpenSSL::Crypto)

cxx_project_preset(msg801)
warning_as_error_enable(msg801)

if(MSG801_BUILD_SHARED)
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
        ${CMAKE_CURRENT_BINARY_DIR}/include/msg801/export.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/msg801
    )
endif()
