#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <msg801/export.hpp>

namespace msg801 {

struct SendResult {
    bool        success;
    std::string error;
};

[[nodiscard]] MSG801_API SendResult send_udp(std::string_view ip,
                                             uint16_t         port,
                                             std::string_view message);

} // namespace msg801
