include(GNUInstallDirs)

if(APPLE)
    set(MSG801_CLI_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
else()
    set(MSG801_CLI_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

add_executable(msg801-cli
    src/main.cpp
)

target_link_libraries(msg801-cli PRIVATE
    msg801
    Boost::program_options
)

set_target_properties(msg801-cli PROPERTIES
    OUTPUT_NAME "msg801"
    BUILD_WITH_INSTALL_RPATH ON
    INSTALL_RPATH "${MSG801_CLI_RPATH}"
)

cxx_project_preset(msg801-cli)
warning_as_error_enable(msg801-cli)

install(TARGETS msg801-cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
