include(GNUInstallDirs)

add_executable(msg801-cli
    src/main.cpp
)

target_link_libraries(msg801-cli PRIVATE
    msg801
    Boost::program_options
)

set_target_properties(msg801-cli PROPERTIES
    OUTPUT_NAME "msg801"
    BUILD_RPATH "${CMAKE_BINARY_DIR}/lib"
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
)

cxx_project_preset(msg801-cli)
warning_as_error_enable(msg801-cli)

install(TARGETS msg801-cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
