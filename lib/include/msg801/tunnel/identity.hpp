#pragma once

#include <msg801/tunnel/processor.hpp>

namespace msg801::tunnel {

class IdentityProcessor : public Processor {
public:
    void on_local_data(ByteSpan input, DataBufferList& output) override;

    void on_remote_data(ByteSpan input, DataBufferList& output) override;
};

} // namespace msg801::tunnel
