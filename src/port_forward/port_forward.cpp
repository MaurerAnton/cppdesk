// Port forwarding / TCP tunneling
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>
#include <asio.hpp>
#include <spdlog/spdlog.h>

namespace cppdesk::port_forward {

using tcp = asio::ip::tcp;

struct TunnelConfig {
    std::string local_host = "127.0.0.1";
    uint16_t local_port = 0;
    std::string remote_host;
    uint16_t remote_port = 0;
    bool enabled = false;
    std::string name;
    int32_t conn_id = 0;
};

class Tunnel {
    TunnelConfig config_;
    asio::io_context io_;
    tcp::acceptor acceptor_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> bytes_fwd_{0}, bytes_rev_{0};

public:
    explicit Tunnel(const TunnelConfig& cfg)
        : config_(cfg), acceptor_(io_, tcp::endpoint(
            asio::ip::make_address(cfg.local_host), cfg.local_port)) {}

    bool start() {
        try {
            running_ = true;
            worker_ = std::thread([this]() {
                while (running_) {
                    try {
                        tcp::socket client(io_);
                        acceptor_.accept(client);
                        std::thread(&Tunnel::handle, this, std::move(client)).detach();
                    } catch (std::exception& e) {
                        if (running_) spdlog::debug("Tunnel accept: {}", e.what());
                    }
                }
            });
            spdlog::info("Tunnel {}:{} -> {}:{} started",
                config_.local_host, config_.local_port,
                config_.remote_host, config_.remote_port);
            return true;
        } catch (std::exception& e) {
            spdlog::error("Tunnel start failed: {}", e.what());
            return false;
        }
    }

    void stop() {
        running_ = false;
        acceptor_.close();
        if (worker_.joinable()) worker_.join();
    }

    uint64_t bytes_forwarded() const { return bytes_fwd_; }
    uint64_t bytes_reverse() const { return bytes_rev_; }

private:
    void handle(tcp::socket local) {
        try {
            tcp::socket remote(io_);
            tcp::resolver resolver(io_);
            asio::connect(remote, resolver.resolve(
                config_.remote_host, std::to_string(config_.remote_port)));

            local.set_option(tcp::no_delay(true));
            remote.set_option(tcp::no_delay(true));

            // Bidirectional relay
            auto relay = [](auto& from, auto& to, std::atomic<uint64_t>& counter) {
                uint8_t buf[8192];
                while (from.is_open() && to.is_open()) {
                    asio::error_code ec;
                    size_t n = from.read_some(asio::buffer(buf), ec);
                    if (ec || n == 0) break;
                    asio::write(to, asio::buffer(buf, n));
                    counter += n;
                }
            };

            std::thread fwd([&]() { relay(local, remote, bytes_fwd_); });
            relay(remote, local, bytes_rev_);
            fwd.join();
        } catch (std::exception& e) {
            spdlog::debug("Tunnel handler: {}", e.what());
        }
    }
};

class TunnelManager {
    std::map<std::string, std::unique_ptr<Tunnel>> tunnels_;
    std::mutex mutex_;

public:
    bool add_tunnel(const std::string& name, const TunnelConfig& cfg) {
        std::lock_guard lk(mutex_);
        auto tun = std::make_unique<Tunnel>(cfg);
        if (tun->start()) {
            tunnels_[name] = std::move(tun);
            return true;
        }
        return false;
    }

    void remove_tunnel(const std::string& name) {
        std::lock_guard lk(mutex_);
        auto it = tunnels_.find(name);
        if (it != tunnels_.end()) {
            it->second->stop();
            tunnels_.erase(it);
        }
    }

    void stop_all() {
        std::lock_guard lk(mutex_);
        for (auto& [_, t] : tunnels_) t->stop();
        tunnels_.clear();
    }

    size_t count() const { std::lock_guard lk(mutex_); return tunnels_.size(); }
};

} // namespace cppdesk::port_forward
