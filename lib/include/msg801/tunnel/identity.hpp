#pragma once

#include <msg801/tunnel/processor.hpp>

namespace msg801::tunnel {

class IdentityProcessor : public Processor {
public:
    explicit IdentityProcessor(bool reverse = false)
    : Processor(reverse)
    {
    }

protected:
    void pack(ByteSpan input, DataBufferList& output) override;

    void unpack(ByteSpan input, DataBufferList& output) override;
};

} // namespace msg801::tunnel
