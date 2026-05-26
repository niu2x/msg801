#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace msg801 {

using Byte = uint8_t;
using ByteSpan = std::span<const Byte>;
using MutableByteSpan = std::span<Byte>;
using ByteVector = std::vector<Byte>;

} // namespace msg801
