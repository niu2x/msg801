#include "msg801/tunnel.hpp"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <spdlog/spdlog.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace msg801 {

namespace {

using namespace std::chrono_literals;
namespace asio = boost::asio;
using asio::ip::tcp;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
using asio::redirect_error;

// ---- JSON logging helpers ----

[[nodiscard]] int64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void log_conn_new(uint64_t id, const tcp::endpoint& src, const tcp::endpoint& dst)
{
    spdlog::info(R"({{"t":{},"ev":"conn_new","id":{},"src":"{}","dst":"{}"}})",
                 now_ms(), id, src.address().to_string() + ":" + std::to_string(src.port()),
                 dst.address().to_string() + ":" + std::to_string(dst.port()));
}

void log_conn_close(uint64_t id, uint64_t rx, uint64_t tx, double dur_sec)
{
    spdlog::info(R"({{"t":{},"ev":"conn_close","id":{},"rx":{},"tx":{},"dur":{:.3}}})",
                 now_ms(), id, rx, tx, dur_sec);
}

// ---- Session state ----

struct SessionState : std::enable_shared_from_this<SessionState> {
    uint64_t            id;
    tcp::socket         local_sock;
    tcp::socket         remote_sock;
    uint64_t            rx_bytes = 0;
    uint64_t            tx_bytes = 0;
    bool                local_eof = false;
    bool                remote_eof = false;
    bool                closed = false;
    int                 done_count = 0;
    std::chrono::steady_clock::time_point start;
    tunnel::ProcessorChain chain;

    SessionState(asio::io_context& ctx,
                 tcp::socket local,
                 tunnel::ProcessorChain proc_chain)
        : id(0)
        , local_sock(std::move(local))
        , remote_sock(ctx)
        , start(std::chrono::steady_clock::now())
        , chain(std::move(proc_chain))
    {}

    void close_all()
    {
        boost::system::error_code ec;
        local_sock.close(ec);
        remote_sock.close(ec);
        closed = true;
    }
};

using SessionPtr = std::shared_ptr<SessionState>;

// ---- Read loops ----

asio::awaitable<void> read_local(SessionPtr s)
{
    std::array<char, 65536> buf;
    while (!s->closed) {
        std::size_t n;
        try {
            n = co_await s->local_sock.async_read_some(asio::buffer(buf), use_awaitable);
        } catch (const boost::system::system_error& e) {
            auto ec = e.code();
            if (ec == asio::error::eof) {
                s->local_eof = true;
                boost::system::error_code ignore;
                s->remote_sock.shutdown(tcp::socket::shutdown_send, ignore);

                {
                    std::vector<tunnel::DataBuffer> flushed;
                    s->chain.flush_local(flushed);
                    for (auto& out : flushed) {
                        try {
                            co_await asio::async_write(s->remote_sock, asio::buffer(out.data), use_awaitable);
                            s->tx_bytes += out.data.size();
                        } catch (...) {
                            s->close_all();
                            co_return;
                        }
                    }
                }

                if (s->remote_eof) {
                    s->close_all();
                }
            } else if (ec == asio::error::operation_aborted) {
                // cancelled by remote-side error, nothing to do
            } else {
                if (!s->closed) s->close_all();
            }
            co_return;
        }

        std::vector<tunnel::DataBuffer> output;
        s->chain.on_local_data(std::span<const char>(buf.data(), n), output);

        for (auto& out : output) {
            try {
                co_await asio::async_write(s->remote_sock, asio::buffer(out.data), use_awaitable);
                s->tx_bytes += out.data.size();
            } catch (...) {
                if (!s->closed) s->close_all();
                co_return;
            }
        }
    }
}

asio::awaitable<void> read_remote(SessionPtr s)
{
    std::array<char, 65536> buf;
    while (!s->closed) {
        std::size_t n;
        try {
            n = co_await s->remote_sock.async_read_some(asio::buffer(buf), use_awaitable);
        } catch (const boost::system::system_error& e) {
            auto ec = e.code();
            if (ec == asio::error::eof) {
                s->remote_eof = true;
                boost::system::error_code ignore;
                s->local_sock.shutdown(tcp::socket::shutdown_send, ignore);

                {
                    std::vector<tunnel::DataBuffer> flushed;
                    s->chain.flush_remote(flushed);
                    for (auto& out : flushed) {
                        try {
                            co_await asio::async_write(s->local_sock, asio::buffer(out.data), use_awaitable);
                            s->rx_bytes += out.data.size();
                        } catch (...) {
                            s->close_all();
                            co_return;
                        }
                    }
                }

                if (s->local_eof) {
                    s->close_all();
                }
            } else if (ec == asio::error::operation_aborted) {
                // cancelled by local-side error, nothing to do
            } else {
                if (!s->closed) s->close_all();
            }
            co_return;
        }

        std::vector<tunnel::DataBuffer> output;
        s->chain.on_remote_data(std::span<const char>(buf.data(), n), output);

        for (auto& out : output) {
            try {
                co_await asio::async_write(s->local_sock, asio::buffer(out.data), use_awaitable);
                s->rx_bytes += out.data.size();
            } catch (...) {
                if (!s->closed) s->close_all();
                co_return;
            }
        }
    }
}

// ---- Session launcher ----

void start_session(SessionPtr s)
{
    auto on_done = [s](std::exception_ptr) {
        if (++s->done_count == 2) {
            auto dur = std::chrono::duration<double>(
                           std::chrono::steady_clock::now() - s->start)
                           .count();
            log_conn_close(s->id, s->rx_bytes, s->tx_bytes, dur);
        }
    };

    co_spawn(s->local_sock.get_executor(), read_local(s), on_done);
    co_spawn(s->remote_sock.get_executor(), read_remote(s), on_done);
}

asio::awaitable<void> do_accept(asio::io_context& ctx,
                                tcp::acceptor acceptor,
                                tcp::endpoint remote_ep)
{
    uint64_t next_id = 0;
    while (true) {
        tcp::socket local{ctx};
        try {
            co_await acceptor.async_accept(local, use_awaitable);
        } catch (...) {
            break;
        }

        auto id = next_id++;
        log_conn_new(id, local.remote_endpoint(), remote_ep);

        auto chain = tunnel::ProcessorChain{};
        chain.add(std::make_unique<tunnel::IdentityProcessor>());

        auto s = std::make_shared<SessionState>(ctx, std::move(local), std::move(chain));
        s->id = id;

        try {
            co_await s->remote_sock.async_connect(remote_ep, use_awaitable);
        } catch (const boost::system::system_error& e) {
            spdlog::error("connect failed id={}: {}", id, e.what());
            s->close_all();
            continue;
        }

        start_session(std::move(s));
    }
}

} // namespace

// ---- Public API ----

void run_tunnel(std::string_view listen_addr, std::string_view remote_addr)
{
    auto colon1 = listen_addr.rfind(':');
    auto colon2 = remote_addr.rfind(':');
    if (colon1 == std::string_view::npos || colon2 == std::string_view::npos) {
        spdlog::error("Invalid address format, expected ip:port");
        return;
    }

    std::string listen_ip(listen_addr.substr(0, colon1));
    uint16_t    listen_port = 0;
    std::string remote_ip(remote_addr.substr(0, colon2));
    uint16_t    remote_port = 0;

    try {
        listen_port = static_cast<uint16_t>(std::stoul(std::string(listen_addr.substr(colon1 + 1))));
        remote_port = static_cast<uint16_t>(std::stoul(std::string(remote_addr.substr(colon2 + 1))));
    } catch (...) {
        spdlog::error("Invalid port number");
        return;
    }

    asio::io_context ctx;

    tcp::endpoint local_ep{asio::ip::make_address(listen_ip), listen_port};
    tcp::endpoint remote_ep{asio::ip::make_address(remote_ip), remote_port};

    tcp::acceptor acceptor{ctx, local_ep};
    spdlog::info("Tunnel listening on {}:{} -> {}:{}",
                 listen_ip, listen_port, remote_ip, remote_port);

    co_spawn(ctx, do_accept(ctx, std::move(acceptor), remote_ep), detached);

    ctx.run();
}

} // namespace msg801
