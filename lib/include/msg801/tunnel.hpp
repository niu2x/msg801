#pragma once

#include "msg801/tunnel/processor_chain.hpp"
#include "msg801/export.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace msg801 {

MSG801_API void run_tunnel(std::string_view listen_addr, std::string_view remote_addr);

} // namespace msg801
