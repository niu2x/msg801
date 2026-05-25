#pragma once

#include <cstdint>
#include <random>
#include <span>
#include <vector>

#include <msg801/tunnel/processor.hpp>

namespace msg801::tunnel {

class PaddingProcessor : public Processor {
public:
    PaddingProcessor(size_t chunk_size, size_t pad_max, uint64_t seed, bool reverse = false);

    void on_local_data(std::span<const char> input,
                       std::vector<DataBuffer>& output) override;

    void on_remote_data(std::span<const char> input,
                        std::vector<DataBuffer>& output) override;

private:
    size_t chunk_size_;
    size_t pad_max_;
    bool reverse_;
    std::mt19937_64 local_rng_;
    std::mt19937_64 remote_rng_;
    std::vector<char> local_decode_buf_;
    std::vector<char> remote_decode_buf_;

    static void write_u32(std::vector<char>& out, uint32_t v);
    static uint32_t read_u32(const char* p);

    void encode(std::span<const char> input,
                std::mt19937_64& rng,
                std::vector<DataBuffer>& output);

    void decode(std::span<const char> input,
                std::vector<char>& acc,
                std::vector<DataBuffer>& output);
};

} // namespace msg801::tunnel
