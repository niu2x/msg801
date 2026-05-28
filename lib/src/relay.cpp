#include "msg801/relay.hpp"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <exception>
#include <memory>
#include <optional>
#include <random>
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
// IDR Protocol constants
// =========================================================================

enum Method : Byte {
    REGISTER        = 0x01,
    READY           = 0x02,
    NEW_SESSION     = 0x03,
    FORWARD_SESSION = 0x04,
    SESSION_OK      = 0x05,
    SESSION_ERR     = 0x06,
    DATA            = 0x10,
    DATA_EOF        = 0x11,
    SESSION_CLOSE   = 0x12,
    EOF_            = 0xFF,
};

constexpr size_t UUID_BYTES = 36;

// =========================================================================
// Frame codec
// =========================================================================

struct Frame {
    Method     method;
    ByteVector body;
};

asio::awaitable<std::optional<Frame>> read_frame(tcp::socket& sock)
{
    std::array<Byte, 4>       hdr;
    boost::system::error_code ec;
    auto n = co_await asio::async_read(sock, asio::buffer(hdr),
                                       redirect_error(use_awaitable, ec));
    if (ec || n == 0)
        co_return std::nullopt;

    uint32_t len = (static_cast<uint32_t>(hdr[0]) << 24)
                   | (static_cast<uint32_t>(hdr[1]) << 16)
                   | (static_cast<uint32_t>(hdr[2]) << 8)
                   | static_cast<uint32_t>(hdr[3]);

    if (len < 1)
        co_return std::nullopt;

    ByteVector payload(len);
    co_await asio::async_read(sock, asio::buffer(payload), use_awaitable);

    co_return Frame { static_cast<Method>(payload[0]),
                      ByteVector(payload.begin() + 1, payload.end()) };
}

asio::awaitable<void> write_frame(tcp::socket& sock, Method method,
                                  const void* data, size_t len)
{
    uint32_t total = static_cast<uint32_t>(1 + len);
    std::array<Byte, 4> hdr;
    hdr[0] = static_cast<Byte>((total >> 24) & 0xFF);
    hdr[1] = static_cast<Byte>((total >> 16) & 0xFF);
    hdr[2] = static_cast<Byte>((total >> 8) & 0xFF);
    hdr[3] = static_cast<Byte>(total & 0xFF);

    Byte method_byte = static_cast<Byte>(method);
    std::array<asio::const_buffer, 3> bufs = {
        asio::buffer(hdr),
        asio::buffer(&method_byte, 1),
        asio::buffer(data, len),
    };
    co_await asio::async_write(sock, bufs, use_awaitable);
}

asio::awaitable<void> write_frame(tcp::socket& sock, Method method)
{
    return write_frame(sock, method, nullptr, 0);
}

asio::awaitable<void> write_frame(tcp::socket& sock, Method method,
                                  const ByteVector& body)
{
    co_await write_frame(sock, method, body.data(), body.size());
}

ByteVector build_frame_bytes(Method method, const void* data, size_t len)
{
    uint32_t total = static_cast<uint32_t>(1 + len);
    ByteVector frame;
    frame.reserve(4 + total);
    frame.push_back(static_cast<Byte>((total >> 24) & 0xFF));
    frame.push_back(static_cast<Byte>((total >> 16) & 0xFF));
    frame.push_back(static_cast<Byte>((total >> 8) & 0xFF));
    frame.push_back(static_cast<Byte>(total & 0xFF));
    frame.push_back(static_cast<Byte>(method));
    if (data && len > 0)
        frame.insert(frame.end(), static_cast<const Byte*>(data),
                     static_cast<const Byte*>(data) + len);
    return frame;
}

ByteVector build_uuid_body(const std::string& uuid)
{
    return ByteVector(uuid.begin(), uuid.end());
}

ByteVector build_uuid_data_body(const std::string& uuid,
                                const void* data, size_t len)
{
    ByteVector body(uuid.begin(), uuid.end());
    if (data && len > 0)
        body.insert(body.end(), static_cast<const Byte*>(data),
                    static_cast<const Byte*>(data) + len);
    return body;
}

// =========================================================================
// UUID generation
// =========================================================================

std::string generate_uuid()
{
    static std::mt19937_64 rng(std::random_device {}());
    uint64_t hi = rng();
    uint64_t lo = rng();
    hi &= ~0xF000ULL;
    hi |= 0x4000ULL;
    lo &= ~0xC000000000000000ULL;
    lo |= 0x8000000000000000ULL;

    std::array<char, 37> buf;
    std::snprintf(buf.data(), buf.size(),
                  "%08x-%04x-%04x-%04x-%012llx",
                  static_cast<unsigned>(hi >> 32),
                  static_cast<unsigned>((hi >> 16) & 0xFFFF),
                  static_cast<unsigned>(hi & 0xFFFF),
                  static_cast<unsigned>((lo >> 48) & 0xFFFF),
                  static_cast<unsigned long long>(lo & 0x0000FFFFFFFFFFFFULL));
    return std::string(buf.data(), UUID_BYTES);
}

// =========================================================================
// Write serialization helper (strand-based)
// =========================================================================

struct AConn : std::enable_shared_from_this<AConn> {
    tcp::socket                                  sock;
    asio::strand<asio::io_context::executor_type> strand;

    AConn(asio::io_context& ctx)
    : sock(ctx),
      strand(asio::make_strand(ctx.get_executor()))
    {
    }
};

asio::awaitable<void> aconn_write(std::shared_ptr<AConn> conn,
                                  Method method, ByteVector body)
{
    co_await asio::post(asio::bind_executor(conn->strand, use_awaitable));
    auto frame = build_frame_bytes(method, body.data(), body.size());
    co_await asio::async_write(conn->sock, asio::buffer(frame),
                               asio::bind_executor(conn->strand, use_awaitable));
}

} // namespace

// =========================================================================
// SERVER (Role A)
// =========================================================================
namespace {

struct Peer : std::enable_shared_from_this<Peer> {
    tcp::socket sock;
    std::string id;
    char        role; // 'B' or 'C'

    Peer(tcp::socket s, std::string id_, char role_)
    : sock(std::move(s)), id(std::move(id_)), role(role_)
    {
    }
};

struct Match {
    std::shared_ptr<Peer> b;
    std::shared_ptr<Peer> c;
    bool                  dead = false;
    std::mutex            dead_mtx;
};

asio::awaitable<void> pump_one_way(std::shared_ptr<Peer>  from,
                                   std::shared_ptr<Peer>  to,
                                   std::shared_ptr<Match> match)
{
    try {
        while (true) {
            auto frame_opt = co_await read_frame(from->sock);
            if (!frame_opt)
                break;

            auto& frame = *frame_opt;

            if (frame.method == EOF_) {
                boost::system::error_code ec;
                co_await write_frame(to->sock, EOF_);
                to->sock.close(ec);
                break;
            }

            // Translate: B sends NEW_SESSION, C receives FORWARD_SESSION
            if (frame.method == NEW_SESSION && from->role == 'B')
                co_await write_frame(to->sock, FORWARD_SESSION, frame.body);
            else
                co_await write_frame(to->sock, frame.method, frame.body);
        }
    } catch (...) {
    }

    bool was_dead;
    { std::lock_guard<std::mutex> lock(match->dead_mtx); was_dead = match->dead; match->dead = true; }
    if (!was_dead) {
        boost::system::error_code ec;
        co_await write_frame(to->sock, EOF_);
        to->sock.close(ec);
        from->sock.close(ec);
        spdlog::info("relay A: pair {} closed", from->id);
    }
}

} // namespace

void run_server(uint16_t port)
{
    asio::io_context ctx;
    tcp::acceptor    acceptor { ctx, tcp::endpoint(tcp::v4(), port) };
    spdlog::info("IDR server listening on port {}", port);

    auto pairs = std::make_shared<
        std::unordered_map<std::string, std::shared_ptr<Match>>>();

    co_spawn(
        ctx,
        [](tcp::acceptor acceptor, auto pairs) -> asio::awaitable<void> {
            while (true) {
                tcp::socket sock { acceptor.get_executor() };
                try {
                    co_await acceptor.async_accept(sock, use_awaitable);
                } catch (...) {
                    break;
                }

                co_spawn(
                    sock.get_executor(),
                    [](tcp::socket sock, auto pairs) -> asio::awaitable<void> {
                        auto frame_opt = co_await read_frame(sock);
                        if (!frame_opt || frame_opt->method != REGISTER)
                            co_return;

                        auto& body = frame_opt->body;
                        if (body.empty())
                            co_return;

                        // Parse "id B" or "id C"
                        auto view  = std::string_view(
                            reinterpret_cast<const char*>(body.data()), body.size());
                        auto sp    = view.find(' ');
                        if (sp == std::string_view::npos || sp + 2 > view.size())
                            co_return;

                        auto id   = std::string(view.substr(0, sp));
                        char role = view[sp + 1];
                        if (role != 'B' && role != 'C')
                            co_return;

                        auto peer = std::make_shared<Peer>(
                            std::move(sock), id, role);

                        auto& m = (*pairs)[id];
                        if (!m)
                            m = std::make_shared<Match>();

                        // Reject duplicate registration
                        if ((role == 'B' && m->b) || (role == 'C' && m->c)) {
                            spdlog::warn("relay A: duplicate {} for id {}",
                                         (role == 'B' ? "B" : "C"), id);
                            co_await write_frame(peer->sock, EOF_);
                            co_return;
                        }

                        if (role == 'B')
                            m->b = peer;
                        else
                            m->c = peer;

                        spdlog::info("relay A: {} registered id={}",
                                     (role == 'B' ? "B" : "C"), id);

                        if (m->b && m->c) {
                            co_await write_frame(m->b->sock, READY);
                            co_await write_frame(m->c->sock, READY);
                            spdlog::info("relay A: pair {} ready", id);

                            co_spawn(peer->sock.get_executor(),
                                     pump_one_way(m->b, m->c, m), detached);
                            co_spawn(peer->sock.get_executor(),
                                     pump_one_way(m->c, m->b, m), detached);
                        }
                    }(std::move(sock), pairs),
                    detached);
            }
        }(std::move(acceptor), pairs),
        detached);

    ctx.run();
}

// =========================================================================
// NODE (B / C)
// =========================================================================
namespace {

// -----------------------------------------------------------------------
// Visitor (B)
// -----------------------------------------------------------------------

struct ClientSession {
    std::string  uuid;
    tcp::socket  client;
    bool         half_close_recv = false;
    bool         half_close_send = false;
    bool         closed          = false;

    ClientSession(tcp::socket s, std::string u)
    : uuid(std::move(u)), client(std::move(s))
    {
    }
};

asio::awaitable<void> run_visitor(asio::io_context&    ctx,
                                  std::shared_ptr<AConn> aconn,
                                  std::string_view     listen_addr)
{
    auto colon = listen_addr.rfind(':');
    if (colon == std::string_view::npos) {
        spdlog::error("relay B: invalid listen address {}", listen_addr);
        co_return;
    }
    auto listen_ip   = listen_addr.substr(0, colon);
    auto listen_port = static_cast<uint16_t>(
        std::stoul(std::string(listen_addr.substr(colon + 1))));

    tcp::acceptor acceptor { ctx,
                             { asio::ip::make_address(listen_ip), listen_port } };
    spdlog::info("relay B: listening on {}:{}", listen_ip, listen_port);

    auto sessions = std::make_shared<
        std::unordered_map<std::string, std::shared_ptr<ClientSession>>>();

    // Reader coroutine: read frames from A, dispatch by uuid
    co_spawn(
        ctx.get_executor(),
        [aconn, sessions]() -> asio::awaitable<void> {
            try {
                while (true) {
                    auto frame_opt = co_await read_frame(aconn->sock);
                    if (!frame_opt)
                        break;

                    auto& frame = *frame_opt;

                    if (frame.method == EOF_)
                        break;

                    if (frame.body.size() < UUID_BYTES)
                        continue;

                    auto uuid = std::string(frame.body.begin(),
                                            frame.body.begin() + UUID_BYTES);
                    auto it = sessions->find(uuid);
                    if (it == sessions->end())
                        continue;
                    auto& s = it->second;

                    switch (frame.method) {
                    case SESSION_OK:
                        spdlog::info("relay B: session {} ready", uuid);
                        break;

                    case SESSION_ERR: {
                        spdlog::warn("relay B: session {} rejected", uuid);
                        s->closed = true;
                        boost::system::error_code ec;
                        s->client.close(ec);
                        sessions->erase(it);
                        break;
                    }

                    case DATA: {
                        auto data_len = frame.body.size() - UUID_BYTES;
                        if (data_len > 0) {
                            boost::system::error_code ec;
                            co_await asio::async_write(
                                s->client,
                                asio::buffer(frame.body.data() + UUID_BYTES, data_len),
                                redirect_error(use_awaitable, ec));
                            if (ec) {
                                co_await aconn_write(aconn, SESSION_CLOSE,
                                                     build_uuid_body(uuid));
                                s->closed = true;
                                s->client.close(ec);
                                sessions->erase(it);
                            }
                        }
                        break;
                    }

                    case DATA_EOF: {
                        s->half_close_recv = true;
                        boost::system::error_code ec;
                        s->client.shutdown(tcp::socket::shutdown_send, ec);
                        if (s->half_close_send) {
                            s->closed = true;
                            sessions->erase(it);
                        }
                        break;
                    }

                    case SESSION_CLOSE: {
                        s->closed = true;
                        boost::system::error_code ec;
                        s->client.close(ec);
                        sessions->erase(it);
                        break;
                    }

                    default:
                        break;
                    }
                }
            } catch (...) {
            }

            // A disconnected — close all clients
            for (auto& [_, s] : *sessions) {
                boost::system::error_code ec;
                s->client.close(ec);
            }
            sessions->clear();

            co_return;
        }(),
        detached);

    // Accept loop
    while (true) {
        tcp::socket client(ctx);
        try {
            co_await acceptor.async_accept(client, use_awaitable);
        } catch (...) {
            break;
        }
        spdlog::info("relay B: accepted client");

        auto uuid = generate_uuid();
        auto sess = std::make_shared<ClientSession>(std::move(client), uuid);
        (*sessions)[uuid] = sess;

        co_await aconn_write(aconn, NEW_SESSION, build_uuid_body(uuid));

        // Pump: read raw from client → send DATA to A
        co_spawn(
            ctx.get_executor(),
            [aconn, sess, sessions]() -> asio::awaitable<void> {
                std::array<Byte, 65536> buf;
                try {
                    while (!sess->closed) {
                        boost::system::error_code ec;
                        auto n = co_await sess->client.async_read_some(
                            asio::buffer(buf),
                            redirect_error(use_awaitable, ec));
                        if (ec == asio::error::eof || n == 0) {
                            sess->half_close_send = true;
                            co_await aconn_write(aconn, DATA_EOF,
                                                 build_uuid_body(sess->uuid));
                            if (sess->half_close_recv)
                                sess->closed = true;
                            break;
                        }
                        if (ec) {
                            co_await aconn_write(aconn, SESSION_CLOSE,
                                                 build_uuid_body(sess->uuid));
                            sess->closed = true;
                            break;
                        }
                        co_await aconn_write(
                            aconn, DATA,
                            build_uuid_data_body(sess->uuid, buf.data(), n));
                    }
                } catch (...) {
                    co_await aconn_write(aconn, SESSION_CLOSE,
                                         build_uuid_body(sess->uuid));
                    sess->closed = true;
                }

                sessions->erase(sess->uuid);
            },
            detached);
    }

    // Acceptor stopped, wait a moment then close all
    for (auto& [_, s] : *sessions) {
        boost::system::error_code ec;
        s->client.close(ec);
    }
    sessions->clear();
}

// -----------------------------------------------------------------------
// Provider (C)
// -----------------------------------------------------------------------

struct SessionState {
    std::string              uuid;
    std::shared_ptr<tcp::socket> remote;
    std::deque<ByteVector>   arrival_buffer;
    bool                     remote_connected = false;
    bool                     connecting       = false;
    bool                     half_close_recv  = false;
    bool                     half_close_send  = false;
    bool                     closed           = false;

    explicit SessionState(std::string u)
    : uuid(std::move(u))
    {
    }
};

asio::awaitable<void> run_provider(asio::io_context&      ctx,
                                   std::shared_ptr<AConn>   aconn,
                                   std::string_view         target_addr)
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

    spdlog::info("relay C: target {}:{}, ready", target_ip, target_port);

    auto sessions = std::make_shared<
        std::unordered_map<std::string, std::shared_ptr<SessionState>>>();

    try {
        while (true) {
            auto frame_opt = co_await read_frame(aconn->sock);
            if (!frame_opt)
                break;

            auto& frame = *frame_opt;

            if (frame.method == EOF_)
                break;

            switch (frame.method) {

            case FORWARD_SESSION: {
                auto uuid = std::string(frame.body.begin(), frame.body.end());
                spdlog::info("relay C: new session {}", uuid);

                auto state        = std::make_shared<SessionState>(uuid);
                state->connecting = true;
                (*sessions)[uuid] = state;

                // Connect to target asynchronously
                co_spawn(
                    ctx.get_executor(),
                    [aconn, state, target_ep, sessions]() -> asio::awaitable<void> {
                        auto remote = std::make_shared<tcp::socket>(
                            aconn->sock.get_executor());
                        boost::system::error_code ec;
                        co_await remote->async_connect(
                            target_ep, redirect_error(use_awaitable, ec));

                        if (ec) {
                            spdlog::warn("relay C: connect target failed {}: {}",
                                         state->uuid, ec.message());
                            co_await aconn_write(aconn, SESSION_ERR,
                                                 build_uuid_body(state->uuid));
                            state->closed = true;
                            sessions->erase(state->uuid);
                            co_return;
                        }

                        state->remote           = remote;
                        state->remote_connected = true;
                        state->connecting       = false;

                        co_await aconn_write(aconn, SESSION_OK,
                                             build_uuid_body(state->uuid));

                        // Drain arrival buffer
                        for (auto& buf : state->arrival_buffer) {
                            co_await asio::async_write(*remote, asio::buffer(buf),
                                                       use_awaitable);
                        }
                        state->arrival_buffer.clear();

                        // Pump: read raw from remote → send DATA to A
                        std::array<Byte, 65536> buf;
                        try {
                            while (!state->closed) {
                                boost::system::error_code ec2;
                                auto n = co_await remote->async_read_some(
                                    asio::buffer(buf),
                                    redirect_error(use_awaitable, ec2));
                                if (ec2 == asio::error::eof || n == 0) {
                                    state->half_close_send = true;
                                    co_await aconn_write(aconn, DATA_EOF,
                                        build_uuid_body(state->uuid));
                                    if (state->half_close_recv)
                                        state->closed = true;
                                    break;
                                }
                                if (ec2) {
                                    co_await aconn_write(aconn, SESSION_CLOSE,
                                        build_uuid_body(state->uuid));
                                    state->closed = true;
                                    break;
                                }
                                co_await aconn_write(
                                    aconn, DATA,
                                    build_uuid_data_body(state->uuid, buf.data(), n));
                            }
                        } catch (...) {
                            co_await aconn_write(aconn, SESSION_CLOSE,
                                                 build_uuid_body(state->uuid));
                            state->closed = true;
                        }

                        sessions->erase(state->uuid);
                    },
                    detached);
                break;
            }

            case DATA: {
                if (frame.body.size() < UUID_BYTES)
                    break;
                auto uuid = std::string(frame.body.begin(),
                                        frame.body.begin() + UUID_BYTES);
                auto data_len = frame.body.size() - UUID_BYTES;
                auto it = sessions->find(uuid);
                if (it == sessions->end())
                    break;
                auto& s = it->second;
                if (s->closed)
                    break;

                if (s->remote_connected) {
                    co_await asio::async_write(
                        *s->remote,
                        asio::buffer(frame.body.data() + UUID_BYTES, data_len),
                        use_awaitable);
                } else if (s->connecting) {
                    s->arrival_buffer.emplace_back(
                        frame.body.begin() + UUID_BYTES, frame.body.end());
                }
                break;
            }

            case DATA_EOF: {
                auto uuid = std::string(frame.body.begin(), frame.body.end());
                auto it = sessions->find(uuid);
                if (it == sessions->end())
                    break;
                auto& s = it->second;
                s->half_close_recv = true;
                if (s->remote_connected) {
                    boost::system::error_code ec;
                    s->remote->shutdown(tcp::socket::shutdown_send, ec);
                }
                if (s->half_close_send) {
                    s->closed = true;
                    sessions->erase(it);
                }
                break;
            }

            case SESSION_CLOSE: {
                auto uuid = std::string(frame.body.begin(), frame.body.end());
                auto it = sessions->find(uuid);
                if (it == sessions->end())
                    break;
                auto& s = it->second;
                s->closed = true;
                if (s->remote) {
                    boost::system::error_code ec;
                    s->remote->close(ec);
                }
                sessions->erase(it);
                break;
            }

            default:
                break;
            }
        }
    } catch (...) {
    }

    // A disconnected — close all remotes
    for (auto& [_, s] : *sessions) {
        if (s->remote) {
            boost::system::error_code ec;
            s->remote->close(ec);
        }
    }
    sessions->clear();
}

} // namespace

void run_node(std::string_view server_addr,
              uint16_t         server_port,
              std::string_view id,
              char             role,
              std::string_view extra)
{
    if (role != 'B' && role != 'C') {
        spdlog::error("IDR: role must be B or C, got {}", role);
        return;
    }

    asio::io_context ctx;

    // Connect to A synchronously
    tcp::endpoint server_ep { asio::ip::make_address(server_addr), server_port };
    tcp::socket   temp_sock(ctx);
    try {
        temp_sock.connect(server_ep);
    } catch (const std::exception& e) {
        spdlog::error("IDR: connect to {}:{} failed: {}",
                      server_addr, server_port, e.what());
        return;
    }

    // Build and send REGISTER frame
    std::string reg_str = std::string(id) + " " + std::string(1, role);
    asio::write(temp_sock, asio::buffer(
        build_frame_bytes(REGISTER, reg_str.data(), reg_str.size())));

    // Wait for READY
    {
        std::array<Byte, 4> hdr;
        asio::read(temp_sock, asio::buffer(hdr));
        uint32_t len = (static_cast<uint32_t>(hdr[0]) << 24)
                       | (static_cast<uint32_t>(hdr[1]) << 16)
                       | (static_cast<uint32_t>(hdr[2]) << 8)
                       | static_cast<uint32_t>(hdr[3]);
        ByteVector payload(len);
        asio::read(temp_sock, asio::buffer(payload));
        if (payload.empty() || payload[0] != READY) {
            spdlog::error("IDR {}: registration rejected", (role == 'B' ? "B" : "C"));
            return;
        }
    }

    spdlog::info("IDR {}: registered id={}, ready", (role == 'B' ? "B" : "C"), id);

    // Create shared AConn with the connected socket
    auto aconn = std::make_shared<AConn>(ctx);
    aconn->sock = std::move(temp_sock);

    if (role == 'B') {
        co_spawn(ctx, run_visitor(ctx, aconn, extra), detached);
    } else {
        co_spawn(ctx, run_provider(ctx, aconn, extra), detached);
    }

    ctx.run();
}

} // namespace msg801::relay
