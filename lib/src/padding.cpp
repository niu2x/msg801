#include <limits>
#include <stdexcept>

#include <spdlog/spdlog.h>

#include "msg801/tunnel/padding.hpp"

namespace msg801::tunnel {

PaddingProcessor::PaddingProcessor(size_t chunk_size, size_t pad_max, uint64_t seed, bool reverse)
: Processor(reverse),
  chunk_size_(chunk_size),
  pad_max_(pad_max),
  pack_rng_(reverse ? (seed ^ 0x9e3779b97f4a7c15ULL) : seed)
{
}

void PaddingProcessor::pack(ByteSpan input, DataBufferList& output)
{
    apply_pack(input, pack_rng_, output);
}

void PaddingProcessor::unpack(ByteSpan input, DataBufferList& output)
{
    apply_unpack(input, unpack_buffer_, output);
}

void PaddingProcessor::write_u32(ByteVector& out, uint32_t v)
{
    out.push_back(static_cast<Byte>((v >> 24) & 0xff));
    out.push_back(static_cast<Byte>((v >> 16) & 0xff));
    out.push_back(static_cast<Byte>((v >> 8) & 0xff));
    out.push_back(static_cast<Byte>(v & 0xff));
}

uint32_t PaddingProcessor::read_u32(const Byte* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
           | (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

void PaddingProcessor::apply_pack(ByteSpan input, std::mt19937_64& rng, DataBufferList& output)
{
    size_t                                  pos = 0;
    std::uniform_int_distribution<uint32_t> pad_dist(0, static_cast<uint32_t>(pad_max_));
    std::uniform_int_distribution<uint32_t> byte_dist(0, 255);

    while (pos < input.size()) {
        size_t   payload_len = std::min(chunk_size_, input.size() - pos);
        uint32_t pad_len     = pad_dist(rng);

        DataBuffer frame;
        frame.data.reserve(8 + payload_len + pad_len);
        write_u32(frame.data, static_cast<uint32_t>(payload_len));
        write_u32(frame.data, pad_len);

        frame.data.insert(frame.data.end(),
                          input.begin() + static_cast<std::ptrdiff_t>(pos),
                          input.begin() + static_cast<std::ptrdiff_t>(pos + payload_len));

        for (uint32_t i = 0; i < pad_len; ++i) {
            frame.data.push_back(static_cast<Byte>(byte_dist(rng)));
        }

        output.push_back(std::move(frame));
        pos += payload_len;
    }
}

void PaddingProcessor::apply_unpack(ByteSpan input, ByteVector& acc, DataBufferList& output)
{
    constexpr uint64_t HEADER_LEN = 8;

    acc.insert(acc.end(), input.begin(), input.end());

    size_t pos = 0;
    while (true) {
        if (acc.size() - pos < 8) {
            break;
        }

        uint32_t payload_len = read_u32(acc.data() + static_cast<std::ptrdiff_t>(pos));
        uint32_t pad_len     = read_u32(acc.data() + static_cast<std::ptrdiff_t>(pos + 4));
        if (payload_len > chunk_size_ || pad_len > pad_max_) {
            spdlog::warn("invalid padding frame header: payload_len={}, pad_len={}, chunk_size={}, "
                         "pad_max={}",
                         payload_len,
                         pad_len,
                         chunk_size_,
                         pad_max_);
            throw std::runtime_error("invalid padding frame header");
        }

        uint64_t frame_len_u64 = HEADER_LEN + static_cast<uint64_t>(payload_len)
                                 + static_cast<uint64_t>(pad_len);
        if (frame_len_u64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            spdlog::warn("invalid padding frame length overflow: payload_len={}, pad_len={}",
                         payload_len,
                         pad_len);
            throw std::runtime_error("invalid padding frame length overflow");
        }

        size_t frame_len = static_cast<size_t>(frame_len_u64);

        if (acc.size() - pos < frame_len) {
            break;
        }

        DataBuffer out;
        out.data.insert(out.data.end(),
                        acc.begin() + static_cast<std::ptrdiff_t>(pos + 8),
                        acc.begin() + static_cast<std::ptrdiff_t>(pos + 8 + payload_len));
        output.push_back(std::move(out));

        pos += frame_len;
    }

    if (pos > 0) {
        acc.erase(acc.begin(), acc.begin() + static_cast<std::ptrdiff_t>(pos));
    }
}

} // namespace msg801::tunnel
