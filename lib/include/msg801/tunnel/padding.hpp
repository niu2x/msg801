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

    void on_local_data(ByteSpan input, DataBufferList& output) override;

    void on_remote_data(ByteSpan input, DataBufferList& output) override;

private:
    size_t chunk_size_;
    size_t pad_max_;
    bool reverse_;
    std::mt19937_64 local_rng_;
    std::mt19937_64 remote_rng_;
    ByteVector local_decode_buf_;
    ByteVector remote_decode_buf_;

    static void write_u32(ByteVector& out, uint32_t v);
    static uint32_t read_u32(const Byte* p);

    void encode(ByteSpan input, std::mt19937_64& rng, DataBufferList& output);

    void decode(ByteSpan input, ByteVector& acc, DataBufferList& output);
};

} // namespace msg801::tunnel
