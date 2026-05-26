#include "msg801/udp_server.hpp"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <spdlog/spdlog.h>

#include <array>
#include <cstdlib>

namespace msg801 {

namespace {

boost::asio::awaitable<void> do_serve(boost::asio::io_context& io_context, uint16_t port)
{
    auto endpoint = boost::asio::ip::udp::endpoint { boost::asio::ip::udp::v4(), port };
    boost::asio::ip::udp::socket socket { io_context, endpoint };

    std::array<char, 65536>        buf;
    boost::asio::ip::udp::endpoint sender;

    while (true) {
        auto len = co_await socket.async_receive_from(boost::asio::buffer(buf),
                                                      sender,
                                                      boost::asio::use_awaitable);

        spdlog::info("[{}:{}] {}",
                     sender.address().to_string(),
                     sender.port(),
                     std::string_view(buf.data(), len));
    }
}

} // namespace

void serve_udp(uint16_t port)
{
    boost::asio::io_context io_context;

    spdlog::info("Listening on 0.0.0.0:{}", port);

    boost::asio::co_spawn(io_context, do_serve(io_context, port), [](std::exception_ptr e) {
        if (e) {
            try {
                std::rethrow_exception(e);
            } catch (const std::exception& ex) {
                spdlog::error("{}", ex.what());
            }
        }
        std::exit(EXIT_FAILURE);
    });

    io_context.run();
}

} // namespace msg801
