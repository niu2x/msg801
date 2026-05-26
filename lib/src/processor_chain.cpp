#include "msg801/tunnel/processor_chain.hpp"

namespace msg801::tunnel {

void ProcessorChain::on_local_data(ByteSpan input, DataBufferList& output)
{
    if (processors_.empty()) {
        output.push_back(DataBuffer{
            .data = ByteVector(input.begin(), input.end())
        });
        return;
    }

    DataBufferList cur;
    cur.push_back(DataBuffer{
        .data = ByteVector(input.begin(), input.end())
    });

    for (auto& p : processors_) {
        DataBufferList next;
        for (auto& buf : cur) {
            p->on_local_data(ByteSpan(buf.data.data(), buf.data.size()), next);
        }
        cur = std::move(next);
    }
    output = std::move(cur);
}

void ProcessorChain::on_remote_data(ByteSpan input, DataBufferList& output)
{
    if (processors_.empty()) {
        output.push_back(DataBuffer{
            .data = ByteVector(input.begin(), input.end())
        });
        return;
    }

    DataBufferList cur;
    cur.push_back(DataBuffer{
        .data = ByteVector(input.begin(), input.end())
    });

    for (auto it = processors_.rbegin(); it != processors_.rend(); ++it) {
        DataBufferList next;
        for (auto& buf : cur) {
            (*it)->on_remote_data(ByteSpan(buf.data.data(), buf.data.size()), next);
        }
        cur = std::move(next);
    }
    output = std::move(cur);
}

void ProcessorChain::flush_local(DataBufferList& output)
{
    DataBufferList cur;
    for (auto& p : processors_) {
        DataBufferList next;
        for (auto& buf : cur) {
            p->on_local_data(ByteSpan(buf.data.data(), buf.data.size()), next);
        }
        p->flush_local(next);
        cur = std::move(next);
    }
    output = std::move(cur);
}

void ProcessorChain::flush_remote(DataBufferList& output)
{
    DataBufferList cur;
    for (auto it = processors_.rbegin(); it != processors_.rend(); ++it) {
        DataBufferList next;
        for (auto& buf : cur) {
            (*it)->on_remote_data(ByteSpan(buf.data.data(), buf.data.size()), next);
        }
        (*it)->flush_remote(next);
        cur = std::move(next);
    }
    output = std::move(cur);
}

} // namespace msg801::tunnel
