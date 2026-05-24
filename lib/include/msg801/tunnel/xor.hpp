#pragma once

#include "msg801/tunnel/processor.hpp"

#include <span>
#include <vector>

namespace msg801::tunnel {

/// Stream cipher using CFB (Cipher Feedback) mode.
///
/// - key 作为初始 IV
/// - 每字节: output = input ^ iv[pos]; 然后用 **密文字节** 更新 IV
/// - 两端（encrypt/decrypt）用同样的密文字节更新 IV，保证状态同步
/// - 无周期性模式，每个密文字节影响后续所有 IV 状态
///
/// reverse=true 时交换 on_local / on_remote 的加解密角色，
/// 用于 A→B→C 双跳场景（A 为入口加密，B 为出口解密）。
class XorProcessor : public Processor {
public:
    explicit XorProcessor(std::span<const char> key, bool reverse = false)
        : iv_(key.begin(), key.end()), reverse_(reverse)
    {
        while (iv_.size() < 8)
            iv_.push_back(static_cast<char>(iv_.size()));
    }

    void on_local_data(std::span<const char> input,
                       std::vector<DataBuffer>& output) override
    {
        if (reverse_)
            decrypt(input, output);
        else
            encrypt(input, output);
    }

    void on_remote_data(std::span<const char> input,
                        std::vector<DataBuffer>& output) override
    {
        if (reverse_)
            encrypt(input, output);
        else
            decrypt(input, output);
    }

private:
    std::vector<char> iv_;
    bool reverse_;

    void mix(char cipher, size_t pos)
    {
        iv_[pos] += cipher;
        iv_[(pos + 1) % iv_.size()] ^= cipher;
    }

    void encrypt(std::span<const char> input,
                 std::vector<DataBuffer>& output)
    {
        auto buf = DataBuffer{
            .data = std::vector<char>(input.begin(), input.end())
        };
        for (size_t i = 0; i < buf.data.size(); ++i) {
            size_t pos = i % iv_.size();
            char cipher = buf.data[i] ^ iv_[pos];
            buf.data[i] = cipher;
            mix(cipher, pos);
        }
        output.push_back(std::move(buf));
    }

    void decrypt(std::span<const char> input,
                 std::vector<DataBuffer>& output)
    {
        auto buf = DataBuffer{
            .data = std::vector<char>(input.begin(), input.end())
        };
        for (size_t i = 0; i < buf.data.size(); ++i) {
            size_t pos = i % iv_.size();
            char cipher = buf.data[i];           // input IS ciphertext
            buf.data[i] ^= iv_[pos];             // = plaintext
            mix(cipher, pos);                    // feed ciphertext back
        }
        output.push_back(std::move(buf));
    }
};

} // namespace msg801::tunnel
