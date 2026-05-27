#include "msg801/relay.hpp"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>
#include <optional>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace msg801::relay {

namespace {

using namespace std::string_view_literals;
namespace asio = boost::asio;
using asio::co_spawn;
using asio::detached;
using asio::redirect_error;
using asio::use_awaitable;
using asio::ip::tcp;

using Byte       = uint8_t;
using ByteVector = std::vector<Byte>;

// =========================================================================
// Frame protocol helpers
// =========================================================================

asio::awaitable<std::optional<ByteVector>> read_frame(tcp::socket& sock)
{
    std::array<Byte, 4>       hdr;
    boost::system::error_code ec;
    auto n = co_await asio::async_read(sock, asio::buffer(hdr), redirect_error(use_awaitable, ec));
    if (ec || n == 0) {
        co_return std::nullopt;
    }

    uint32_t len = (static_cast<uint32_t>(hdr[0]) << 24) | (static_cast<uint32_t>(hdr[1]) << 16)
                   | (static_cast<uint32_t>(hdr[2]) << 8) | static_cast<uint32_t>(hdr[3]);

    if (len == 0) {
        co_return ByteVector {};
    }

    ByteVector payload(len);
    co_await asio::async_read(sock, asio::buffer(payload), use_awaitable);
    co_return payload;
}

asio::awaitable<void> write_frame(tcp::socket& sock, const Byte* data, size_t len)
{
    std::array<Byte, 4> hdr;
    hdr[0] = static_cast<Byte>((len >> 24) & 0xFF);
    hdr[1] = static_cast<Byte>((len >> 16) & 0xFF);
    hdr[2] = static_cast<Byte>((len >> 8) & 0xFF);
    hdr[3] = static_cast<Byte>(len & 0xFF);

    std::array<asio::const_buffer, 2> bufs = { asio::buffer(hdr), asio::buffer(data, len) };
    co_await asio::async_write(sock, bufs, use_awaitable);
}

asio::awaitable<void> write_end_frame(tcp::socket& sock)
{
    static constexpr std::array<Byte, 4> ZERO = { 0, 0, 0, 0 };
    co_await asio::async_write(sock, asio::buffer(ZERO), use_awaitable);
}

// =========================================================================
// Text line reader / writer
// =========================================================================

asio::awaitable<std::optional<std::string>> read_line(tcp::socket& sock)
{
    asio::streambuf           buf;
    boost::system::error_code ec;
    auto n = co_await asio::async_read_until(sock, buf, '\n', redirect_error(use_awaitable, ec));
    if (ec || n == 0) {
        co_return std::nullopt;
    }
    auto        data = buf.data();
    std::string line(asio::buffers_begin(data), asio::buffers_end(data));
    buf.consume(buf.size());
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    co_return line;
}

asio::awaitable<void> write_line(tcp::socket& sock, std::string_view line)
{
    std::string msg(line);
    msg += '\n';
    co_await asio::async_write(sock, asio::buffer(msg), use_awaitable);
}

// =========================================================================
// SERVER (Role A)
// =========================================================================

struct ProviderSession : std::enable_shared_from_this<ProviderSession> {
    tcp::socket sock;
    bool        alive = true;

    explicit ProviderSession(tcp::socket s)
    : sock(std::move(s))
    {
    }
};

// Per-session relay between one B data connection and a provider (C)
struct SessionRelay : std::enable_shared_from_this<SessionRelay> {
    tcp::socket                      b_sock;
    std::shared_ptr<ProviderSession> provider;
    bool                             stop           = false;
    bool                             provider_error = false;

    SessionRelay(tcp::socket s, std::shared_ptr<ProviderSession> p)
    : b_sock(std::move(s)),
      provider(std::move(p))
    {
    }
};

asio::awaitable<void> pump_one_way(tcp::socket&                  from,
                                   tcp::socket&                  to,
                                   std::shared_ptr<SessionRelay> r,
                                   bool                          is_from_provider)
{
    try {
        while (!r->stop) {
            auto frame = co_await read_frame(from);
            if (!frame) {
                if (is_from_provider)
                    r->provider_error = true;
                r->stop = true;
                break;
            }
            if (frame->empty()) {
                r->stop = true;
                co_await write_end_frame(to);
                break;
            }
            co_await write_frame(to, frame->data(), frame->size());
        }
    } catch (...) {
        if (is_from_provider)
            r->provider_error = true;
        r->stop = true;
    }
}

asio::awaitable<void> run_session(std::shared_ptr<SessionRelay> r)
{
    auto ex = r->b_sock.get_executor();

    co_spawn(ex, pump_one_way(r->b_sock, r->provider->sock, r, false), detached);
    co_spawn(ex, pump_one_way(r->provider->sock, r->b_sock, r, true), detached);

    // Wait for both directions to finish
    asio::steady_timer timer(ex);
    while (!r->stop) {
        timer.expires_after(std::chrono::milliseconds(50));
        boost::system::error_code ec;
        co_await timer.async_wait(redirect_error(use_awaitable, ec));
    }
}

// =========================================================================
// Server accept loop
// =========================================================================

asio::awaitable<void> do_server_accept(tcp::acceptor acceptor)
{
    auto providers
        = std::make_shared<std::unordered_map<std::string, std::shared_ptr<ProviderSession>>>();

    while (true) {
        tcp::socket sock { acceptor.get_executor() };
        try {
            co_await acceptor.async_accept(sock, use_awaitable);
        } catch (...) {
            break;
        }

        auto provs = providers; // keep map alive for the spawned handler
        co_spawn(
            sock.get_executor(),
            [](tcp::socket sock, decltype(provs) provs) -> asio::awaitable<void> {
                auto line_opt = co_await read_line(sock);
                if (!line_opt)
                    co_return;

                auto& line = *line_opt;
                auto  sp1  = line.find(' ');
                if (sp1 == std::string::npos)
                    co_return;

                auto cmd  = line.substr(0, sp1);
                auto rest = line.substr(sp1 + 1);

                if (cmd == "REGISTER") {
                    auto sp2 = rest.find(' ');
                    if (sp2 == std::string::npos)
                        co_return;
                    auto id        = rest.substr(0, sp2);
                    auto role_part = rest.substr(sp2 + 1);
                    auto sp3       = role_part.find(' ');
                    char role      = 0;
                    if (sp3 != std::string::npos) {
                        role = role_part.substr(0, sp3)[0];
                    } else {
                        role = role_part[0];
                    }

                    if (role == 'C') {
                        spdlog::info("relay: provider registered id={}", id);
                        auto ps      = std::make_shared<ProviderSession>(std::move(sock));
                        (*provs)[id] = ps;
                        co_await write_line(ps->sock, "MATCHED"sv);
                    } else if (role == 'B') {
                        spdlog::info("relay: visitor B registered id={}", id);
                        auto it = provs->find(id);
                        if (it != provs->end() && it->second->alive) {
                            co_await write_line(sock, "MATCHED"sv);
                            auto r = std::make_shared<SessionRelay>(std::move(sock), it->second);
                            co_await run_session(r);
                            if (r->provider_error) {
                                spdlog::warn("relay: removing dead provider id={}", id);
                                it->second->alive = false;
                                provs->erase(it);
                            }
                            spdlog::info("relay: session ended id={}", id);
                        } else {
                            co_await write_line(sock, "ERROR no provider for this id"sv);
                        }
                    }

                } else if (cmd == "SESSION") {
                    auto id = rest;
                    auto it = provs->find(id);
                    if (it != provs->end() && it->second->alive) {
                        co_await write_line(sock, "MATCHED"sv);
                        auto r = std::make_shared<SessionRelay>(std::move(sock), it->second);
                        co_await run_session(r);
                        if (r->provider_error) {
                            spdlog::warn("relay: removing dead provider id={}", id);
                            it->second->alive = false;
                            provs->erase(it);
                        }
                        spdlog::info("relay: session ended id={}", id);
                    } else {
                        co_await write_line(sock, "ERROR no provider for this id"sv);
                    }
                }
            }(std::move(sock), provs),
            detached);
    }
}

} // namespace

void run_server(uint16_t port)
{
    asio::io_context ctx;
    tcp::acceptor    acceptor { ctx, tcp::endpoint(tcp::v4(), port) };
    spdlog::info("Relay server listening on port {}", port);
    co_spawn(ctx, do_server_accept(std::move(acceptor)), detached);
    ctx.run();
}

// =========================================================================
// NODE
// =========================================================================
namespace {

struct NodeRelay : std::enable_shared_from_this<NodeRelay> {
    std::shared_ptr<tcp::socket> a_sock;
    tcp::socket                  other;
    int                          done = 0;

    NodeRelay(tcp::socket o)
    : other(std::move(o))
    {
    }
};

asio::awaitable<void> node_pump_other_to_a(std::shared_ptr<NodeRelay> r)
{
    std::array<Byte, 65536> buf;
    try {
        while (true) {
            boost::system::error_code ec;
            auto                      n = co_await r->other.async_read_some(asio::buffer(buf),
                                                       redirect_error(use_awaitable, ec));
            if (ec) {
                co_await write_end_frame(*r->a_sock);
                break;
            }
            co_await write_frame(*r->a_sock, buf.data(), n);
        }
    } catch (...) {
    }
    if (++r->done == 2) {
        boost::system::error_code ec;
        r->other.close(ec);
    }
}

asio::awaitable<void> node_pump_a_to_other(std::shared_ptr<NodeRelay> r)
{
    try {
        while (true) {
            auto frame = co_await read_frame(*r->a_sock);
            if (!frame) {
                break;
            }
            if (frame->empty()) {
                boost::system::error_code ec;
                r->other.shutdown(tcp::socket::shutdown_send, ec);
                break;
            }
            co_await asio::async_write(r->other, asio::buffer(*frame), use_awaitable);
        }
    } catch (...) {
    }
    if (++r->done == 2) {
        boost::system::error_code ec;
        r->other.close(ec);
    }
}

asio::awaitable<void> run_node_relay(std::shared_ptr<NodeRelay> r)
{
    auto ex = r->other.get_executor();
    co_spawn(ex, node_pump_other_to_a(r), detached);
    co_spawn(ex, node_pump_a_to_other(r), detached);

    asio::steady_timer timer(ex);
    while (r->done < 2) {
        timer.expires_after(std::chrono::milliseconds(50));
        boost::system::error_code ec;
        co_await timer.async_wait(redirect_error(use_awaitable, ec));
    }
}

// -----------------------------------------------------------------------
// Node B
// -----------------------------------------------------------------------
asio::awaitable<void> run_node_b(asio::io_context& ctx,
                                 std::string_view  server_addr,
                                 uint16_t          server_port,
                                 std::string_view  id,
                                 std::string_view  listen_addr)
{
    auto colon = listen_addr.rfind(':');
    if (colon == std::string_view::npos) {
        spdlog::error("relay B: invalid listen address {}", listen_addr);
        co_return;
    }
    auto ip   = listen_addr.substr(0, colon);
    auto port = static_cast<uint16_t>(std::stoul(std::string(listen_addr.substr(colon + 1))));

    tcp::acceptor acceptor { ctx, { asio::ip::make_address(ip), port } };
    tcp::endpoint server_ep { asio::ip::make_address(server_addr), server_port };

    spdlog::info("relay B: listening on {}:{}  ->  {}:{}  id={}",
                 ip,
                 port,
                 server_addr,
                 server_port,
                 id);

    while (true) {
        tcp::socket local { ctx };
        try {
            co_await acceptor.async_accept(local, use_awaitable);
        } catch (...) {
            break;
        }
        spdlog::info("relay B: accepted local client");

        auto data_sock = std::make_shared<tcp::socket>(ctx);
        try {
            co_await data_sock->async_connect(server_ep, use_awaitable);
        } catch (...) {
            spdlog::error("relay B: connect to server failed");
            continue;
        }

        co_await write_line(*data_sock, std::string("SESSION ") + std::string(id));

        auto resp = co_await read_line(*data_sock);
        if (!resp || resp->rfind("MATCHED", 0) != 0) {
            spdlog::error("relay B: session rejected: {}", resp ? *resp : "disconnected");
            continue;
        }

        auto r    = std::make_shared<NodeRelay>(std::move(local));
        r->a_sock = data_sock;
        co_await run_node_relay(r);
        spdlog::info("relay B: session ended");
    }
}

// -----------------------------------------------------------------------
// Node C
// -----------------------------------------------------------------------
asio::awaitable<void> run_node_c(asio::io_context&            ctx,
                                 std::shared_ptr<tcp::socket> a_sock,
                                 std::string_view             id,
                                 std::string_view             target_addr)
{
    auto colon = target_addr.rfind(':');
    if (colon == std::string_view::npos) {
        spdlog::error("relay C: invalid target address {}", target_addr);
        co_return;
    }
    auto target_ip   = target_addr.substr(0, colon);
    auto target_port = static_cast<uint16_t>(
        std::stoul(std::string(target_addr.substr(colon + 1))));
    tcp::endpoint target_ep { asio::ip::make_address(target_ip), target_port };

    spdlog::info("relay C: target {}:{}, id={}", target_ip, target_port, id);

    while (true) {
        auto first = co_await read_frame(*a_sock);
        if (!first) {
            spdlog::warn("relay C: server disconnected");
            break;
        }
        if (first->empty()) {
            continue;
        }

        spdlog::info("relay C: new session, connecting to target");

        tcp::socket target_sock(ctx);
        try {
            co_await target_sock.async_connect(target_ep, use_awaitable);
        } catch (...) {
            spdlog::error("relay C: connect to target failed");
            co_await write_end_frame(*a_sock);
            continue;
        }

        co_await asio::async_write(target_sock, asio::buffer(*first), use_awaitable);

        auto r    = std::make_shared<NodeRelay>(std::move(target_sock));
        r->a_sock = a_sock;
        co_await run_node_relay(r);
        spdlog::info("relay C: session ended");
    }
}

} // namespace

void run_node(std::string_view server_addr,
              uint16_t         server_port,
              std::string_view id,
              char             role,
              std::string_view extra)
{
    if (role != 'B' && role != 'C') {
        spdlog::error("relay: role must be B or C, got {}", role);
        return;
    }

    asio::io_context ctx;
    tcp::endpoint    server_ep { asio::ip::make_address(server_addr), server_port };

    tcp::socket a_sock(ctx);
    try {
        a_sock.connect(server_ep);
    } catch (const std::exception& e) {
        spdlog::error("relay: connect to server {}:{} failed: {}",
                      server_addr,
                      server_port,
                      e.what());
        return;
    }

    {
        std::string reg = "REGISTER " + std::string(id) + " " + std::string(1, role);
        if (!extra.empty()) {
            reg += " " + std::string(extra);
        }
        reg += '\n';
        asio::write(a_sock, asio::buffer(reg));
    }

    {
        asio::streambuf buf;
        asio::read_until(a_sock, buf, '\n');
        auto        data = buf.data();
        std::string line(asio::buffers_begin(data), asio::buffers_end(data));
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (line.rfind("MATCHED", 0) != 0) {
            spdlog::error("relay: registration rejected: {}", line);
            return;
        }
        spdlog::info("relay: registered as role={} id={}", role, id);
    }

    if (role == 'B') {
        // B doesn't need a_sock for the loop; it opens data connections per session
        a_sock.close();
        co_spawn(ctx, run_node_b(ctx, server_addr, server_port, id, extra), detached);
    } else {
        auto persistent = std::make_shared<tcp::socket>(std::move(a_sock));
        co_spawn(ctx, run_node_c(ctx, persistent, id, extra), detached);
    }

    ctx.run();
}

} // namespace msg801::relay
