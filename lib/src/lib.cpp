#include "msg801/lib.hpp"

#include <boost/version.hpp>

namespace msg801 {

std::string greeting()
{
    return "Hello from libmsg801!";
}

const char* version()
{
    return "0.1.0";
}

} // namespace msg801
