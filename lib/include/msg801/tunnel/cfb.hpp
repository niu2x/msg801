#pragma once

#include <span>
#include <vector>

#include <msg801/tunnel/processor.hpp>

namespace msg801::tunnel {

class CfbProcessor : public Processor {
public:
    explicit CfbProcessor(std::span<const char> key, bool reverse = false);

    void on_local_data(std::span<const char> input,
                       std::vector<DataBuffer>& output) override;

    void on_remote_data(std::span<const char> input,
                        std::vector<DataBuffer>& output) override;

private:
    std::vector<char> enc_iv_;
    std::vector<char> dec_iv_;
    bool reverse_;
    size_t enc_offset_ = 0;
    size_t dec_offset_ = 0;

    static void mix(char cipher, std::vector<char>& iv, size_t pos);

    void encrypt(std::span<const char> input,
                 std::vector<DataBuffer>& output,
                 std::vector<char>& iv,
                 size_t& offset);

    void decrypt(std::span<const char> input,
                 std::vector<DataBuffer>& output,
                 std::vector<char>& iv,
                 size_t& offset);
};

} // namespace msg801::tunnel
