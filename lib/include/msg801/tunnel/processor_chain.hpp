#pragma once

#include "msg801/tunnel/processor.hpp"
#include "msg801/tunnel/identity.hpp"

#include <memory>
#include <vector>

namespace msg801::tunnel {

class ProcessorChain {
public:
    void add(std::unique_ptr<Processor> p)
    {
        processors_.push_back(std::move(p));
    }

    void on_local_data(std::span<const char> input,
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

    void on_remote_data(std::span<const char> input,
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

    void flush_local(std::vector<DataBuffer>& output)
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

    void flush_remote(std::vector<DataBuffer>& output)
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

private:
    std::vector<std::unique_ptr<Processor>> processors_;
};

} // namespace msg801::tunnel
