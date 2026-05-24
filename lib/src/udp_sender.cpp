#include "msg801/udp_sender.hpp"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <exception>

namespace msg801 {

namespace {

boost::asio::awaitable<void> do_send(
    boost::asio::io_context& io_context,
    std::string ip,
    uint16_t port,
    std::string message)
{
    auto endpoint = boost::asio::ip::udp::endpoint{
        boost::asio::ip::make_address(ip), port};

    boost::asio::ip::udp::socket socket{io_context};
    socket.open(boost::asio::ip::udp::v4());

    co_await socket.async_send_to(
        boost::asio::buffer(message), endpoint,
        boost::asio::use_awaitable);
}

} // namespace

SendResult send_udp(std::string_view ip, uint16_t port, std::string_view message)
{
  boost::asio::io_context io_context;
  std::exception_ptr ex;

  boost::asio::co_spawn(
      io_context,
      do_send(io_context, std::string{ip}, port, std::string{message}),
      [&](std::exception_ptr e) { ex = std::move(e); });

  io_context.run();

  if (ex) {
    try {
      std::rethrow_exception(ex);
    } catch (const std::exception &e) {
      return SendResult{false, e.what()};
    }
  }

  return SendResult{true, {}};
}

} // namespace msg801
