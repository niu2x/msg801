#include "msg801/tunnel/cfb.hpp"

namespace msg801::tunnel {

CfbProcessor::CfbProcessor(ByteSpan key, bool reverse)
    : enc_iv_(key.begin(), key.end())
    , dec_iv_(key.begin(), key.end())
    , reverse_(reverse)
{
    while (enc_iv_.size() < 8) {
        enc_iv_.push_back(static_cast<Byte>(enc_iv_.size()));
        dec_iv_.push_back(static_cast<Byte>(dec_iv_.size()));
    }
}

void CfbProcessor::on_local_data(ByteSpan input, DataBufferList& output)
{
    if (reverse_)
        decrypt(input, output, dec_iv_, dec_offset_);
    else
        encrypt(input, output, enc_iv_, enc_offset_);
}

void CfbProcessor::on_remote_data(ByteSpan input, DataBufferList& output)
{
    if (reverse_)
        encrypt(input, output, enc_iv_, enc_offset_);
    else
        decrypt(input, output, dec_iv_, dec_offset_);
}

void CfbProcessor::mix(Byte cipher, ByteVector& iv, size_t pos)
{
    iv[pos] += cipher;
    iv[(pos + 1) % iv.size()] ^= cipher;
}

void CfbProcessor::encrypt(ByteSpan input,
                           DataBufferList& output,
                           ByteVector& iv,
                           size_t& offset)
{
    auto buf = DataBuffer{
        .data = ByteVector(input.begin(), input.end())
    };
    for (size_t i = 0; i < buf.data.size(); ++i) {
        size_t pos = (offset + i) % iv.size();
        Byte cipher = buf.data[i] ^ iv[pos];
        buf.data[i] = cipher;
        mix(cipher, iv, pos);
    }
    offset += buf.data.size();
    output.push_back(std::move(buf));
}

void CfbProcessor::decrypt(ByteSpan input,
                           DataBufferList& output,
                           ByteVector& iv,
                           size_t& offset)
{
    auto buf = DataBuffer{
        .data = ByteVector(input.begin(), input.end())
    };
    for (size_t i = 0; i < buf.data.size(); ++i) {
        size_t pos = (offset + i) % iv.size();
        Byte cipher = buf.data[i];
        buf.data[i] ^= iv[pos];
        mix(cipher, iv, pos);
    }
    offset += buf.data.size();
    output.push_back(std::move(buf));
}

} // namespace msg801::tunnel
