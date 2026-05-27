#pragma once

#include <cstddef>
#include <vector>

#include <msg801/tunnel/processor.hpp>

namespace msg801::tunnel {

class CfbProcessor : public Processor {
public:
    explicit CfbProcessor(ByteSpan key, bool reverse = false);

    void on_local_data(ByteSpan input, DataBufferList& output) override;

    void on_remote_data(ByteSpan input, DataBufferList& output) override;

private:
    ByteVector enc_iv_;
    ByteVector dec_iv_;
    bool reverse_;
    size_t enc_offset_ = 0;
    size_t dec_offset_ = 0;

    static void mix(Byte cipher, ByteVector& iv, size_t pos);

    void encrypt(ByteSpan input, DataBufferList& output, ByteVector& iv,
                 size_t& offset);

    void decrypt(ByteSpan input, DataBufferList& output, ByteVector& iv,
                 size_t& offset);
};

class CfbNonceProcessor : public Processor {
public:
    explicit CfbNonceProcessor(ByteSpan iv, ByteSpan hmac_key, bool reverse = false);

    void on_local_data(ByteSpan input, DataBufferList& output) override;

    void on_remote_data(ByteSpan input, DataBufferList& output) override;

private:
    ByteVector base_iv_;
    ByteVector hmac_key_;
    ByteVector enc_iv_;
    ByteVector dec_iv_;
    bool reverse_;
    size_t enc_offset_ = 0;
    size_t dec_offset_ = 0;
    bool local_ready_ = false;
    bool remote_ready_ = false;
    ByteVector remote_handshake_buffer_;

    static void mix(Byte cipher, ByteVector& iv, size_t pos);

    void encrypt(ByteSpan input, DataBufferList& output, ByteVector& iv,
                 size_t& offset);

    void decrypt(ByteSpan input, DataBufferList& output, ByteVector& iv,
                 size_t& offset);

    void init_local(DataBufferList& output);

    void init_remote(ByteSpan packet);

    ByteVector derive_iv(ByteSpan nonce) const;

    ByteVector make_nonce() const;

    static ByteVector hmac_sha256(ByteSpan key, ByteSpan data);
};

} // namespace msg801::tunnel
