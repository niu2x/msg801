#pragma once

#include <msg801/tunnel/processor.hpp>

namespace msg801::tunnel {

class IdentityProcessor : public Processor {
public:
    void on_local_data(std::span<const char> input,
                       std::vector<DataBuffer>& output) override;

    void on_remote_data(std::span<const char> input,
                        std::vector<DataBuffer>& output) override;
};

} // namespace msg801::tunnel
