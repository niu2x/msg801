#include "msg801/tunnel/identity.hpp"

namespace msg801::tunnel {

void IdentityProcessor::on_local_data(std::span<const char> input,
                                      std::vector<DataBuffer>& output)
{
    output.push_back(DataBuffer{
        .data = std::vector<char>(input.begin(), input.end())
    });
}

void IdentityProcessor::on_remote_data(std::span<const char> input,
                                       std::vector<DataBuffer>& output)
{
    output.push_back(DataBuffer{
        .data = std::vector<char>(input.begin(), input.end())
    });
}

} // namespace msg801::tunnel
