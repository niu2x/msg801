#include "msg801/tunnel/cfb.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace msg801::tunnel {

namespace {

constexpr size_t NONCE_SIZE            = 512;
constexpr size_t TAG_SIZE              = 32;
constexpr size_t HANDSHAKE_PACKET_SIZE = TAG_SIZE + NONCE_SIZE;

ByteVector digest(ByteSpan input, const EVP_MD* (*md)(), size_t digest_size)
{
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) {
        throw std::runtime_error("failed to create digest context");
    }

    ByteVector   out(digest_size);
    unsigned int out_len = 0;
    int          ok      = EVP_DigestInit_ex(ctx, md(), nullptr)
             && EVP_DigestUpdate(ctx, input.data(), input.size())
             && EVP_DigestFinal_ex(ctx, out.data(), &out_len);
    EVP_MD_CTX_free(ctx);

    if (!ok || out_len != digest_size) {
        throw std::runtime_error("failed to compute digest");
    }
    return out;
}

ByteVector hmac_sha256(ByteSpan key, ByteSpan data)
{
    if (key.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("hmac key is too large");
    }

    ByteVector   out(TAG_SIZE);
    unsigned int out_len = 0;
    auto*        ret     = HMAC(EVP_sha256(),
                     key.data(),
                     static_cast<int>(key.size()),
                     data.data(),
                     data.size(),
                     out.data(),
                     &out_len);
    if (ret == nullptr || out_len != TAG_SIZE) {
        throw std::runtime_error("failed to compute hmac-sha256");
    }
    return out;
}

} // namespace

CfbProcessor::CfbProcessor(ByteSpan key, bool reverse)
: enc_iv_(key.begin(), key.end()),
  dec_iv_(key.begin(), key.end()),
  reverse_(reverse)
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

void CfbProcessor::encrypt(ByteSpan input, DataBufferList& output, ByteVector& iv, size_t& offset)
{
    auto buf = DataBuffer { .data = ByteVector(input.begin(), input.end()) };
    for (size_t i = 0; i < buf.data.size(); ++i) {
        size_t pos    = (offset + i) % iv.size();
        Byte   cipher = buf.data[i] ^ iv[pos];
        buf.data[i]   = cipher;
        mix(cipher, iv, pos);
    }
    offset += buf.data.size();
    output.push_back(std::move(buf));
}

void CfbProcessor::decrypt(ByteSpan input, DataBufferList& output, ByteVector& iv, size_t& offset)
{
    auto buf = DataBuffer { .data = ByteVector(input.begin(), input.end()) };
    for (size_t i = 0; i < buf.data.size(); ++i) {
        size_t pos    = (offset + i) % iv.size();
        Byte   cipher = buf.data[i];
        buf.data[i] ^= iv[pos];
        mix(cipher, iv, pos);
    }
    offset += buf.data.size();
    output.push_back(std::move(buf));
}

CfbNonceProcessor::CfbNonceProcessor(ByteSpan iv, ByteSpan hmac_key, bool reverse)
: base_iv_(iv.begin(), iv.end()),
  hmac_key_(hmac_key.begin(), hmac_key.end()),
  reverse_(reverse)
{
}

void CfbNonceProcessor::on_local_data(ByteSpan input, DataBufferList& output)
{
    if (!local_ready_) {
        init_local(output);
    }

    if (reverse_) {
        decrypt(input, output, dec_iv_, dec_offset_);
    } else {
        encrypt(input, output, enc_iv_, enc_offset_);
    }
}

void CfbNonceProcessor::on_remote_data(ByteSpan input, DataBufferList& output)
{
    if (!remote_ready_) {
        remote_handshake_buffer_.insert(remote_handshake_buffer_.end(), input.begin(), input.end());
        if (remote_handshake_buffer_.size() < HANDSHAKE_PACKET_SIZE) {
            return;
        }

        ByteSpan packet(remote_handshake_buffer_.data(), HANDSHAKE_PACKET_SIZE);
        init_remote(packet);

        ByteSpan remain(remote_handshake_buffer_.data() + HANDSHAKE_PACKET_SIZE,
                        remote_handshake_buffer_.size() - HANDSHAKE_PACKET_SIZE);
        if (reverse_) {
            encrypt(remain, output, enc_iv_, enc_offset_);
        } else {
            decrypt(remain, output, dec_iv_, dec_offset_);
        }
        remote_handshake_buffer_.clear();
        return;
    }

    if (reverse_) {
        encrypt(input, output, enc_iv_, enc_offset_);
    } else {
        decrypt(input, output, dec_iv_, dec_offset_);
    }
}

void CfbNonceProcessor::mix(Byte cipher, ByteVector& iv, size_t pos)
{
    iv[pos] += cipher;
    iv[(pos + 1) % iv.size()] ^= cipher;
}

void CfbNonceProcessor::encrypt(ByteSpan        input,
                                DataBufferList& output,
                                ByteVector&     iv,
                                size_t&         offset)
{
    auto buf = DataBuffer { .data = ByteVector(input.begin(), input.end()) };
    for (size_t i = 0; i < buf.data.size(); ++i) {
        size_t pos    = (offset + i) % iv.size();
        Byte   cipher = buf.data[i] ^ iv[pos];
        buf.data[i]   = cipher;
        mix(cipher, iv, pos);
    }
    offset += buf.data.size();
    output.push_back(std::move(buf));
}

void CfbNonceProcessor::decrypt(ByteSpan        input,
                                DataBufferList& output,
                                ByteVector&     iv,
                                size_t&         offset)
{
    auto buf = DataBuffer { .data = ByteVector(input.begin(), input.end()) };
    for (size_t i = 0; i < buf.data.size(); ++i) {
        size_t pos    = (offset + i) % iv.size();
        Byte   cipher = buf.data[i];
        buf.data[i] ^= iv[pos];
        mix(cipher, iv, pos);
    }
    offset += buf.data.size();
    output.push_back(std::move(buf));
}

void CfbNonceProcessor::init_local(DataBufferList& output)
{
    ByteVector nonce = make_nonce();
    ByteVector tag   = hmac_sha256(ByteSpan(hmac_key_.data(), hmac_key_.size()),
                                 ByteSpan(nonce.data(), nonce.size()));

    auto handshake = DataBuffer { .data = ByteVector {} };
    handshake.data.reserve(HANDSHAKE_PACKET_SIZE);
    handshake.data.insert(handshake.data.end(), tag.begin(), tag.end());
    handshake.data.insert(handshake.data.end(), nonce.begin(), nonce.end());
    output.push_back(std::move(handshake));

    ByteVector iv = derive_iv(ByteSpan(nonce.data(), nonce.size()));
    if (reverse_) {
        dec_iv_ = std::move(iv);
    } else {
        enc_iv_ = std::move(iv);
    }
    local_ready_ = true;
}

void CfbNonceProcessor::init_remote(ByteSpan packet)
{
    ByteSpan   expected_tag(packet.data(), TAG_SIZE);
    ByteSpan   nonce(packet.data() + TAG_SIZE, NONCE_SIZE);
    ByteVector actual_tag = hmac_sha256(ByteSpan(hmac_key_.data(), hmac_key_.size()), nonce);
    if (!std::equal(expected_tag.begin(),
                    expected_tag.end(),
                    actual_tag.begin(),
                    actual_tag.end())) {
        throw std::runtime_error("cfb_nonce handshake hmac mismatch");
    }

    ByteVector iv = derive_iv(nonce);
    if (reverse_) {
        enc_iv_ = std::move(iv);
    } else {
        dec_iv_ = std::move(iv);
    }
    remote_ready_ = true;
}

ByteVector CfbNonceProcessor::derive_iv(ByteSpan nonce) const
{
    ByteVector merged;
    merged.reserve(nonce.size() + base_iv_.size());
    merged.insert(merged.end(), nonce.begin(), nonce.end());
    merged.insert(merged.end(), base_iv_.begin(), base_iv_.end());
    return digest(ByteSpan(merged.data(), merged.size()), EVP_sha512, 64);
}

ByteVector CfbNonceProcessor::make_nonce() const
{
    ByteVector nonce(NONCE_SIZE);
    if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1) {
        throw std::runtime_error("failed to generate cfb_nonce nonce");
    }
    return nonce;
}

ByteVector CfbNonceProcessor::hmac_sha256(ByteSpan key, ByteSpan data)
{
    return ::msg801::tunnel::hmac_sha256(key, data);
}

} // namespace msg801::tunnel
