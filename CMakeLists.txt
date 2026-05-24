cmake_minimum_required(VERSION 3.16)
project(msg801 VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(cxx_project)
include(warning_as_error)

find_package(Boost 1.89 REQUIRED COMPONENTS program_options)
find_package(Threads REQUIRED)

add_subdirectory(lib)
add_subdirectory(cli)
