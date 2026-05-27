#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <msg801/export.hpp>

namespace msg801::relay {

MSG801_API void run_server(uint16_t port);

MSG801_API void run_node(std::string_view server_addr,
                         uint16_t         server_port,
                         std::string_view id,
                         char             role,
                         std::string_view extra);

} // namespace msg801::relay
