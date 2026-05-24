#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace msg801::tunnel {

struct DataBuffer {
    std::vector<char> data;
};

class Processor {
public:
    virtual ~Processor() = default;

    virtual void on_local_data(std::span<const char> input,
                               std::vector<DataBuffer>& output) = 0;
    virtual void on_remote_data(std::span<const char> input,
                                std::vector<DataBuffer>& output) = 0;

    virtual void flush_local(std::vector<DataBuffer>& /*output*/) {}
    virtual void flush_remote(std::vector<DataBuffer>& /*output*/) {}
};

} // namespace msg801::tunnel
