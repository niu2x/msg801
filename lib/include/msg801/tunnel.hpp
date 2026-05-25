#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <msg801/tunnel/processor_chain.hpp>
#include <msg801/export.hpp>

namespace msg801 {

MSG801_API void run_tunnel(std::string_view listen_addr, std::string_view remote_addr,
                           const std::vector<std::string>& processor_specs = {});

} // namespace msg801
