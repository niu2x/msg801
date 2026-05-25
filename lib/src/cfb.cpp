#include "msg801/tunnel/cfb.hpp"

namespace msg801::tunnel {

CfbProcessor::CfbProcessor(std::span<const char> key, bool reverse)
    : enc_iv_(key.begin(), key.end())
    , dec_iv_(key.begin(), key.end())
    , reverse_(reverse)
{
    while (enc_iv_.size() < 8) {
        enc_iv_.push_back(static_cast<char>(enc_iv_.size()));
        dec_iv_.push_back(static_cast<char>(dec_iv_.size()));
    }
}

void CfbProcessor::on_local_data(std::span<const char> input,
                                 std::vector<DataBuffer>& output)
{
    if (reverse_)
        decrypt(input, output, dec_iv_, dec_offset_);
    else
        encrypt(input, output, enc_iv_, enc_offset_);
}

void CfbProcessor::on_remote_data(std::span<const char> input,
                                  std::vector<DataBuffer>& output)
{
    if (reverse_)
        encrypt(input, output, enc_iv_, enc_offset_);
    else
        decrypt(input, output, dec_iv_, dec_offset_);
}

void CfbProcessor::mix(char cipher, std::vector<char>& iv, size_t pos)
{
    iv[pos] += cipher;
    iv[(pos + 1) % iv.size()] ^= cipher;
}

void CfbProcessor::encrypt(std::span<const char> input,
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

void CfbProcessor::decrypt(std::span<const char> input,
                           std::vector<DataBuffer>& output,
                           std::vector<char>& iv,
                           size_t& offset)
{
    auto buf = DataBuffer{
        .data = std::vector<char>(input.begin(), input.end())
    };
    for (size_t i = 0; i < buf.data.size(); ++i) {
        size_t pos = (offset + i) % iv.size();
        char cipher = buf.data[i];
        buf.data[i] ^= iv[pos];
        mix(cipher, iv, pos);
    }
    offset += buf.data.size();
    output.push_back(std::move(buf));
}

} // namespace msg801::tunnel
