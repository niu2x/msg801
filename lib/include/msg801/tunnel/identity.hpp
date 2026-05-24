#pragma once

#include "msg801/tunnel/processor.hpp"

namespace msg801::tunnel {

class IdentityProcessor : public Processor {
public:
    void on_local_data(std::span<const char> input,
                       std::vector<DataBuffer>& output) override
    {
        output.push_back(DataBuffer{
            .data = std::vector<char>(input.begin(), input.end())
        });
    }

    void on_remote_data(std::span<const char> input,
                        std::vector<DataBuffer>& output) override
    {
        output.push_back(DataBuffer{
            .data = std::vector<char>(input.begin(), input.end())
        });
    }
};

} // namespace msg801::tunnel
