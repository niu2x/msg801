#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <msg801/tunnel/processor.hpp>

namespace msg801::tunnel {

class PaddingProcessor : public Processor {
public:
    PaddingProcessor(size_t chunk_size, size_t pad_max, uint64_t seed, bool reverse = false);

private:
    size_t          chunk_size_;
    size_t          pad_max_;
    std::mt19937_64 pack_rng_;
    ByteVector      unpack_buffer_;

    static void     write_u32(ByteVector& out, uint32_t v);
    static uint32_t read_u32(const Byte* p);

    void pack(ByteSpan input, DataBufferList& output) override;

    void unpack(ByteSpan input, DataBufferList& output) override;

    void apply_pack(ByteSpan input, std::mt19937_64& rng, DataBufferList& output);

    void apply_unpack(ByteSpan input, ByteVector& acc, DataBufferList& output);
};

} // namespace msg801::tunnel
