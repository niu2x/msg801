#pragma once

#include <cstddef>
#include <vector>

#include <msg801/tunnel/processor.hpp>

namespace msg801::tunnel {

class CfbProcessor : public Processor {
public:
    explicit CfbProcessor(ByteSpan key, bool reverse = false);

private:
    ByteVector pack_iv_;
    ByteVector unpack_iv_;
    size_t     pack_offset_   = 0;
    size_t     unpack_offset_ = 0;

    static void mix(Byte cipher, ByteVector& iv, size_t pos);

    void pack(ByteSpan input, DataBufferList& output) override;

    void unpack(ByteSpan input, DataBufferList& output) override;

    void encrypt(ByteSpan input, DataBufferList& output, ByteVector& iv, size_t& offset);

    void decrypt(ByteSpan input, DataBufferList& output, ByteVector& iv, size_t& offset);
};

class CfbNonceProcessor : public Processor {
public:
    explicit CfbNonceProcessor(ByteSpan iv, ByteSpan hmac_key, bool reverse = false);

private:
    ByteVector base_iv_;
    ByteVector hmac_key_;
    ByteVector pack_iv_;
    ByteVector unpack_iv_;
    size_t     pack_offset_   = 0;
    size_t     unpack_offset_ = 0;
    bool       pack_ready_    = false;
    bool       unpack_ready_  = false;
    ByteVector unpack_handshake_buffer_;

    static void mix(Byte cipher, ByteVector& iv, size_t pos);

    void pack(ByteSpan input, DataBufferList& output) override;

    void unpack(ByteSpan input, DataBufferList& output) override;

    void encrypt(ByteSpan input, DataBufferList& output, ByteVector& iv, size_t& offset);

    void decrypt(ByteSpan input, DataBufferList& output, ByteVector& iv, size_t& offset);

    void init_pack(DataBufferList& output);

    void init_unpack(ByteSpan packet);

    ByteVector derive_iv(ByteSpan nonce) const;

    ByteVector make_nonce() const;

    static ByteVector hmac_sha256(ByteSpan key, ByteSpan data);
};

} // namespace msg801::tunnel
