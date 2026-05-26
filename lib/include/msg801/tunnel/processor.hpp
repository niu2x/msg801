#pragma once

#include <msg801/byte_types.hpp>

namespace msg801::tunnel {

struct DataBuffer {
    ByteVector data;
};

using DataBufferList = std::vector<DataBuffer>;

class Processor {
public:
    virtual ~Processor() = default;

    virtual void on_local_data(ByteSpan input, DataBufferList& output) = 0;
    virtual void on_remote_data(ByteSpan input, DataBufferList& output) = 0;

    virtual void flush_local(DataBufferList& /*output*/) {}
    virtual void flush_remote(DataBufferList& /*output*/) {}
};

} // namespace msg801::tunnel
