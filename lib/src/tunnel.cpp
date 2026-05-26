#include "msg801/tunnel.hpp"

#include "msg801/tunnel/cfb.hpp"
#include "msg801/tunnel/identity.hpp"
#include "msg801/tunnel/padding.hpp"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <spdlog/spdlog.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <random>

namespace msg801 {

namespace {

using namespace std::chrono_literals;
using tunnel::DataBufferList;
namespace asio = boost::asio;
using asio::ip::tcp;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
using asio::redirect_error;

std::vector<std::string> split(const std::string& s, char delim)
{
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        out.push_back(item);
    }
    return out;
}

std::unordered_map<std::string, std::string> parse_kv(const std::string& s)
{
    std::unordered_map<std::string, std::string> out;
    for (const auto& item : split(s, ',')) {
        auto eq = item.find('=');
        if (eq == std::string::npos) continue;
        out[item.substr(0, eq)] = item.substr(eq + 1);
    }
    return out;
}

bool parse_bool(const std::string& v)
{
    return v == "1" || v == "true" || v == "yes";
}

std::optional<tunnel::ProcessorChain> build_processor_chain(const std::vector<std::string>& specs)
{
    tunnel::ProcessorChain chain;
    if (specs.empty()) {
        chain.add(std::make_unique<tunnel::IdentityProcessor>());
        return chain;
    }

    for (const auto& spec : specs) {
        auto colon = spec.find(':');
        std::string name = colon == std::string::npos ? spec : spec.substr(0, colon);
        std::string args = colon == std::string::npos ? std::string{} : spec.substr(colon + 1);
        auto kv = parse_kv(args);

        if (name == "identity") {
            chain.add(std::make_unique<tunnel::IdentityProcessor>());
        } else if (name == "cfb") {
            auto it = kv.find("key");
            if (it == kv.end()) {
                spdlog::error("processor cfb requires key=<...>");
                return std::nullopt;
            }
            bool reverse = kv.count("reverse") ? parse_bool(kv["reverse"]) : false;
            ByteVector key_bytes(it->second.begin(), it->second.end());
            chain.add(std::make_unique<tunnel::CfbProcessor>(
                ByteSpan(key_bytes), reverse));
        } else if (name == "padding") {
            if (!kv.count("chunk") || !kv.count("max")) {
                spdlog::error("processor padding requires chunk=<N>,max=<M>");
                return std::nullopt;
            }
            size_t chunk = static_cast<size_t>(std::stoull(kv["chunk"]));
            size_t max = static_cast<size_t>(std::stoull(kv["max"]));
            constexpr size_t U32_MAX_AS_SIZE_T = static_cast<size_t>(std::numeric_limits<uint32_t>::max());
            if (chunk > U32_MAX_AS_SIZE_T || max > U32_MAX_AS_SIZE_T) {
                spdlog::error("processor padding requires chunk/max <= {}", U32_MAX_AS_SIZE_T);
                return std::nullopt;
            }
            uint64_t seed = 0;
            if (kv.count("seed")) {
                seed = static_cast<uint64_t>(std::stoull(kv["seed"]));
            } else {
                uint64_t t = static_cast<uint64_t>(
                    std::chrono::high_resolution_clock::now().time_since_epoch().count());
                uint64_t r = (static_cast<uint64_t>(std::random_device{}()) << 32)
                           ^ static_cast<uint64_t>(std::random_device{}());
                seed = t ^ r;
            }
            bool reverse = kv.count("reverse") ? parse_bool(kv["reverse"]) : false;
            chain.add(std::make_unique<tunnel::PaddingProcessor>(chunk, max, seed, reverse));
        } else {
            spdlog::error("unknown processor: {}", name);
            return std::nullopt;
        }
    }

    return chain;
}

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

void log_stat(uint64_t id, double rx_rate, double tx_rate)
{
    spdlog::info(R"({{"t":{},"ev":"stat","id":{},"rx_rate":{:.0},"tx_rate":{:.0}}})",
                 now_ms(), id, rx_rate, tx_rate);
}

// ---- Session state ----

struct SessionState : std::enable_shared_from_this<SessionState> {
    uint64_t            id;
    tcp::socket         local_sock;
    tcp::socket         remote_sock;
    uint64_t            rx_bytes = 0;
    uint64_t            tx_bytes = 0;
    uint64_t            last_stat_rx = 0;
    uint64_t            last_stat_tx = 0;
    std::chrono::steady_clock::time_point last_stat_time;
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
        , last_stat_time(std::chrono::steady_clock::now())
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
    std::array<Byte, 65536> buf;
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
                    DataBufferList flushed;
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

        DataBufferList output;
        try {
            s->chain.on_local_data(ByteSpan(buf.data(), n), output);
        } catch (const std::exception& e) {
            spdlog::warn("close session id={} due to invalid local stream data: {}",
                         s->id, e.what());
            if (!s->closed) s->close_all();
            co_return;
        }

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
    std::array<Byte, 65536> buf;
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
                    DataBufferList flushed;
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

        DataBufferList output;
        try {
            s->chain.on_remote_data(ByteSpan(buf.data(), n), output);
        } catch (const std::exception& e) {
            spdlog::warn("close session id={} due to invalid remote stream data: {}",
                         s->id, e.what());
            if (!s->closed) s->close_all();
            co_return;
        }

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

// ---- Stat loop ----

asio::awaitable<void> stat_loop(SessionPtr s)
{
    asio::steady_timer timer{co_await asio::this_coro::executor};
    while (true) {
        timer.expires_after(5s);
        boost::system::error_code ec;
        co_await timer.async_wait(redirect_error(use_awaitable, ec));
        if (ec || s->closed) co_return;

        auto now = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration<double>(now - s->last_stat_time).count();
        if (dt > 0) {
            double rx_rate = (s->rx_bytes - s->last_stat_rx) / dt;
            double tx_rate = (s->tx_bytes - s->last_stat_tx) / dt;
            s->last_stat_rx = s->rx_bytes;
            s->last_stat_tx = s->tx_bytes;
            s->last_stat_time = now;
            log_stat(s->id, rx_rate, tx_rate);
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
    co_spawn(s->local_sock.get_executor(), stat_loop(s), detached);
}

asio::awaitable<void> do_accept(asio::io_context& ctx,
                                tcp::acceptor acceptor,
                                tcp::endpoint remote_ep,
                                std::vector<std::string> processor_specs)
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

        auto chain_opt = build_processor_chain(processor_specs);
        if (!chain_opt.has_value()) {
            co_return;
        }
        auto chain = std::move(chain_opt.value());

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

void run_tunnel(std::string_view listen_addr, std::string_view remote_addr,
                const std::vector<std::string>& processor_specs)
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

    co_spawn(ctx, do_accept(ctx, std::move(acceptor), remote_ep,
                           processor_specs), detached);

    ctx.run();
}

} // namespace msg801
