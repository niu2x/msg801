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

    void on_local_data(ByteSpan input, DataBufferList& output);

    void on_remote_data(ByteSpan input, DataBufferList& output);

    void flush_local(DataBufferList& output);

    void flush_remote(DataBufferList& output);

private:
    std::vector<std::unique_ptr<Processor>> processors_;
};

} // namespace msg801::tunnel
