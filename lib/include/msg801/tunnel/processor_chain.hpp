#pragma once

#include <memory>
#include <vector>

#include <msg801/tunnel/processor.hpp>
#include <msg801/tunnel/identity.hpp>

namespace msg801::tunnel {

class ProcessorChain {
public:
    void add(std::unique_ptr<Processor> p)
    {
        processors_.push_back(std::move(p));
    }

    void on_local_data(std::span<const char> input,
                       std::vector<DataBuffer>& output);

    void on_remote_data(std::span<const char> input,
                        std::vector<DataBuffer>& output);

    void flush_local(std::vector<DataBuffer>& output);

    void flush_remote(std::vector<DataBuffer>& output);

private:
    std::vector<std::unique_ptr<Processor>> processors_;
};

} // namespace msg801::tunnel
