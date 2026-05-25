#include "msg801/tunnel/processor_chain.hpp"

namespace msg801::tunnel {

void ProcessorChain::on_local_data(std::span<const char> input,
                                   std::vector<DataBuffer>& output)
{
    if (processors_.empty()) {
        output.push_back(DataBuffer{
            .data = std::vector<char>(input.begin(), input.end())
        });
        return;
    }

    std::vector<DataBuffer> cur;
    cur.push_back(DataBuffer{
        .data = std::vector<char>(input.begin(), input.end())
    });

    for (auto& p : processors_) {
        std::vector<DataBuffer> next;
        for (auto& buf : cur) {
            p->on_local_data(std::span<const char>(buf.data.data(), buf.data.size()), next);
        }
        cur = std::move(next);
    }
    output = std::move(cur);
}

void ProcessorChain::on_remote_data(std::span<const char> input,
                                    std::vector<DataBuffer>& output)
{
    if (processors_.empty()) {
        output.push_back(DataBuffer{
            .data = std::vector<char>(input.begin(), input.end())
        });
        return;
    }

    std::vector<DataBuffer> cur;
    cur.push_back(DataBuffer{
        .data = std::vector<char>(input.begin(), input.end())
    });

    for (auto it = processors_.rbegin(); it != processors_.rend(); ++it) {
        std::vector<DataBuffer> next;
        for (auto& buf : cur) {
            (*it)->on_remote_data(std::span<const char>(buf.data.data(), buf.data.size()), next);
        }
        cur = std::move(next);
    }
    output = std::move(cur);
}

void ProcessorChain::flush_local(std::vector<DataBuffer>& output)
{
    std::vector<DataBuffer> cur;
    for (auto& p : processors_) {
        std::vector<DataBuffer> next;
        for (auto& buf : cur) {
            p->on_local_data(std::span<const char>(buf.data.data(), buf.data.size()), next);
        }
        p->flush_local(next);
        cur = std::move(next);
    }
    output = std::move(cur);
}

void ProcessorChain::flush_remote(std::vector<DataBuffer>& output)
{
    std::vector<DataBuffer> cur;
    for (auto it = processors_.rbegin(); it != processors_.rend(); ++it) {
        std::vector<DataBuffer> next;
        for (auto& buf : cur) {
            (*it)->on_remote_data(std::span<const char>(buf.data.data(), buf.data.size()), next);
        }
        (*it)->flush_remote(next);
        cur = std::move(next);
    }
    output = std::move(cur);
}

} // namespace msg801::tunnel
