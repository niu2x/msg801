#include "msg801/tunnel/identity.hpp"

namespace msg801::tunnel {

void IdentityProcessor::pack(ByteSpan input, DataBufferList& output)
{
    output.push_back(DataBuffer { .data = ByteVector(input.begin(), input.end()) });
}

void IdentityProcessor::unpack(ByteSpan input, DataBufferList& output)
{
    output.push_back(DataBuffer { .data = ByteVector(input.begin(), input.end()) });
}

} // namespace msg801::tunnel
