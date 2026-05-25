#pragma once

#include <string>

#include <msg801/export.hpp>

namespace msg801 {

[[nodiscard]] MSG801_API std::string greeting();
[[nodiscard]] MSG801_API const char* version();

} // namespace msg801
