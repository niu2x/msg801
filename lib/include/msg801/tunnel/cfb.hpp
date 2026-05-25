#pragma once

#include "msg801/tunnel/processor.hpp"

#include <span>
#include <vector>

namespace msg801::tunnel {

/// Stream cipher using CFB (Cipher Feedback) mode.
///
/// 内部维护两套独立 IV：encrypt（local→remote）和 decrypt（remote→local）。
/// 方向隔离，即使一个方向的密文被篡改，也不影响另一方向的密钥流。
///
/// reverse=true 时交换 on_local / on_remote 的加解密角色，
/// 用于 A→B→C 双跳场景（A 为入口加密，B 为出口解密）。
class CfbProcessor : public Processor {
public:
    explicit CfbProcessor(std::span<const char> key, bool reverse = false)
        : enc_iv_(key.begin(), key.end())
        , dec_iv_(key.begin(), key.end())
        , reverse_(reverse)
    {
        while (enc_iv_.size() < 8) {
            enc_iv_.push_back(static_cast<char>(enc_iv_.size()));
            dec_iv_.push_back(static_cast<char>(dec_iv_.size()));
        }
    }

    void on_local_data(std::span<const char> input,
                       std::vector<DataBuffer>& output) override
    {
        if (reverse_)
            decrypt(input, output, dec_iv_, dec_offset_);
        else
            encrypt(input, output, enc_iv_, enc_offset_);
    }

    void on_remote_data(std::span<const char> input,
                        std::vector<DataBuffer>& output) override
    {
        if (reverse_)
            encrypt(input, output, enc_iv_, enc_offset_);
        else
            decrypt(input, output, dec_iv_, dec_offset_);
    }

private:
    std::vector<char> enc_iv_;
    std::vector<char> dec_iv_;
    bool reverse_;
    size_t enc_offset_ = 0;
    size_t dec_offset_ = 0;

    void mix(char cipher, std::vector<char>& iv, size_t pos)
    {
        iv[pos] += cipher;
        iv[(pos + 1) % iv.size()] ^= cipher;
    }

    /// 加密: input=明文, output=密文, iv 用密文字节更新
    void encrypt(std::span<const char> input,
                 std::vector<DataBuffer>& output,
                 std::vector<char>& iv,
                 size_t& offset)
    {
        auto buf = DataBuffer{
            .data = std::vector<char>(input.begin(), input.end())
        };
        for (size_t i = 0; i < buf.data.size(); ++i) {
            size_t pos = (offset + i) % iv.size();
            char cipher = buf.data[i] ^ iv[pos];
            buf.data[i] = cipher;
            mix(cipher, iv, pos);
        }
        offset += buf.data.size();
        output.push_back(std::move(buf));
    }

    /// 解密: input=密文, output=明文, iv 用原始输入字节（密文）更新
    void decrypt(std::span<const char> input,
                 std::vector<DataBuffer>& output,
                 std::vector<char>& iv,
                 size_t& offset)
    {
        auto buf = DataBuffer{
            .data = std::vector<char>(input.begin(), input.end())
        };
        for (size_t i = 0; i < buf.data.size(); ++i) {
            size_t pos = (offset + i) % iv.size();
            char cipher = buf.data[i];      // 输入就是密文
            buf.data[i] ^= iv[pos];          // 还原明文
            mix(cipher, iv, pos);            // 用密文字节更新 IV
        }
        offset += buf.data.size();
        output.push_back(std::move(buf));
    }
};

} // namespace msg801::tunnel
