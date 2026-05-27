#pragma once

#include <msg801/byte_types.hpp>

namespace msg801::tunnel {

struct DataBuffer {
    ByteVector data;
};

using DataBufferList = std::vector<DataBuffer>;

class Processor {
public:
    explicit Processor(bool reverse = false)
    : reverse_(reverse)
    {
    }

    virtual ~Processor() = default;

    void on_local_data(ByteSpan input, DataBufferList& output)
    {
        if (reverse_) {
            unpack(input, output);
        } else {
            pack(input, output);
        }
    }

    void on_remote_data(ByteSpan input, DataBufferList& output)
    {
        if (reverse_) {
            pack(input, output);
        } else {
            unpack(input, output);
        }
    }

    virtual void flush_local(DataBufferList& /*output*/)
    {
    }
    virtual void flush_remote(DataBufferList& /*output*/)
    {
    }

protected:
    virtual void pack(ByteSpan input, DataBufferList& output)   = 0;
    virtual void unpack(ByteSpan input, DataBufferList& output) = 0;

private:
    bool reverse_;
};

} // namespace msg801::tunnel
