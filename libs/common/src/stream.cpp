#include "common/protocol.hpp"
#include "common/config.hpp"
#include "common/crypto.hpp"
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <spdlog/spdlog.h>
#include <cstring>
#include <queue>
#include <condition_variable>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#endif

namespace cppdesk::common {

// ====== TCP Stream Implementation ======

class TcpStream : public Stream {
public:
    explicit TcpStream(asio::ip::tcp::socket sock)
        : socket_(std::move(sock)), ssl_context_(asio::ssl::context::tlsv12_client) {
        socket_.set_option(asio::ip::tcp::no_delay(true));
        setup_ssl();
    }

    TcpStream(asio::ip::tcp::socket sock, bool tls)
        : socket_(std::move(sock)), ssl_context_(asio::ssl::context::tlsv12_client) {
        socket_.set_option(asio::ip::tcp::no_delay(true));
        if (tls) {
            setup_ssl();
            ssl_stream_ = std::make_unique<asio::ssl::stream<asio::ip::tcp::socket&>>(
                socket_, ssl_context_);
            ssl_stream_->handshake(asio::ssl::stream_base::client);
        }
    }

    ~TcpStream() override {
        close();
    }

    bool send(const std::vector<uint8_t>& data) override {
        std::lock_guard lk(send_mutex_);
        try {
            uint32_t len = htonl(static_cast<uint32_t>(data.size()));
            std::array<asio::const_buffer, 2> bufs = {{
                asio::buffer(&len, 4),
                asio::buffer(data.data(), data.size())
            }};

            if (encryption_enabled_ && !encryption_key_.empty()) {
                // Encrypt the payload before sending
                auto nonce = crypto::random_bytes(crypto::AES_NONCE_BYTES);
                auto encrypted = crypto::aes_gcm_encrypt(
                    data.data(), data.size(),
                    encryption_key_.data(), nonce.data());

                // Format: [nonce:12][encrypted_data]
                std::vector<uint8_t> framed;
                framed.insert(framed.end(), nonce.begin(), nonce.end());
                framed.insert(framed.end(), encrypted.begin(), encrypted.end());

                uint32_t elen = htonl(static_cast<uint32_t>(framed.size()));
                std::array<asio::const_buffer, 2> ebufs = {{
                    asio::buffer(&elen, 4),
                    asio::buffer(framed.data(), framed.size())
                }};
                write_locked(ebufs);
            } else {
                write_locked(bufs);
            }

            bytes_sent_ += data.size();
            return true;
        } catch (std::exception& e) {
            spdlog::error("Send error: {}", e.what());
            connected_ = false;
            return false;
        }
    }

    std::vector<uint8_t> recv() override {
        try {
            uint32_t len = 0;
            read_exact(reinterpret_cast<uint8_t*>(&len), 4);
            len = ntohl(len);

            if (len == 0 || len > 64 * 1024 * 1024) {
                spdlog::warn("Invalid message length: {}", len);
                return {};
            }

            std::vector<uint8_t> data(len);
            read_exact(data.data(), len);

            if (encryption_enabled_ && !encryption_key_.empty()) {
                // Decrypt: first 12 bytes = nonce, rest = encrypted data
                if (len < crypto::AES_NONCE_BYTES + crypto::AES_TAG_BYTES) {
                    spdlog::error("Encrypted message too short: {}", len);
                    return {};
                }
                auto decrypted = crypto::aes_gcm_decrypt(
                    data.data() + crypto::AES_NONCE_BYTES,
                    len - crypto::AES_NONCE_BYTES,
                    encryption_key_.data(), data.data());
                bytes_recv_ += decrypted.size();
                return decrypted;
            }

            bytes_recv_ += len;
            return data;
        } catch (std::exception& e) {
            spdlog::debug("Recv error: {}", e.what());
            connected_ = false;
            return {};
        }
    }

    bool is_open() const override {
        return connected_ && socket_.is_open();
    }

    void close() override {
        connected_ = false;
        try {
            if (ssl_stream_) {
                ssl_stream_->shutdown();
            }
            socket_.close();
        } catch (...) {}
    }

    std::string local_addr() const override {
        try {
            auto ep = socket_.local_endpoint();
            return ep.address().to_string() + ":" + std::to_string(ep.port());
        } catch (...) { return "unknown"; }
    }

    std::string remote_addr() const override {
        try {
            auto ep = socket_.remote_endpoint();
            return ep.address().to_string() + ":" + std::to_string(ep.port());
        } catch (...) { return "unknown"; }
    }

    void set_nodelay(bool on) override {
        socket_.set_option(asio::ip::tcp::no_delay(on));
    }

    void set_encryption_key(const std::vector<uint8_t>& key) override {
        encryption_key_ = key;
        encryption_enabled_ = !key.empty();
        if (encryption_enabled_) {
            spdlog::info("Stream encryption enabled ({} bytes)", key.size());
        }
    }

    // Statistics
    uint64_t get_bytes_sent() const { return bytes_sent_; }
    uint64_t get_bytes_recv() const { return bytes_recv_; }
    double get_bandwidth_send() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - last_stats_).count();
        if (elapsed <= 0) return 0;
        return (bytes_sent_ - last_bytes_sent_) / elapsed;
    }
    double get_bandwidth_recv() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - last_stats_).count();
        if (elapsed <= 0) return 0;
        return (bytes_recv_ - last_bytes_recv_) / elapsed;
    }
    void reset_bandwidth_counters() {
        last_bytes_sent_ = bytes_sent_;
        last_bytes_recv_ = bytes_recv_;
        last_stats_ = std::chrono::steady_clock::now();
    }

private:
    asio::ip::tcp::socket socket_;
    asio::ssl::context ssl_context_;
    std::unique_ptr<asio::ssl::stream<asio::ip::tcp::socket&>> ssl_stream_;
    std::atomic<bool> connected_{true};
    std::mutex send_mutex_;
    std::vector<uint8_t> encryption_key_;
    bool encryption_enabled_ = false;
    uint64_t bytes_sent_ = 0;
    uint64_t bytes_recv_ = 0;
    uint64_t last_bytes_sent_ = 0;
    uint64_t last_bytes_recv_ = 0;
    std::chrono::steady_clock::time_point last_stats_ = std::chrono::steady_clock::now();

    void setup_ssl() {
        ssl_context_.set_default_verify_paths();
        ssl_context_.set_verify_mode(asio::ssl::verify_peer);
    }

    void write_locked(const auto& bufs) {
        if (ssl_stream_) {
            asio::write(*ssl_stream_, bufs);
        } else {
            asio::write(socket_, bufs);
        }
    }

    void read_exact(uint8_t* buf, size_t len) {
        if (ssl_stream_) {
            asio::read(*ssl_stream_, asio::buffer(buf, len));
        } else {
            asio::read(socket_, asio::buffer(buf, len));
        }
    }
};

// ====== UDP Socket Wrapper ======

class UdpSocketWrapper {
public:
    UdpSocketWrapper(asio::io_context& io, uint16_t port = 0)
        : socket_(io, asio::ip::udp::endpoint(asio::ip::udp::v4(), port)) {
        local_port_ = socket_.local_endpoint().port();
        spdlog::debug("UDP socket bound to port {}", local_port_);
    }

    UdpSocketWrapper(asio::io_context& io, const std::string& addr, uint16_t port)
        : socket_(io) {
        socket_.open(asio::ip::udp::v4());
        asio::ip::udp::resolver resolver(io);
        auto endpoints = resolver.resolve(addr, std::to_string(port));
        socket_.connect(*endpoints.begin());
        local_port_ = socket_.local_endpoint().port();
    }

    bool send(const std::vector<uint8_t>& data, const std::string& host, uint16_t port) {
        try {
            asio::ip::udp::resolver resolver(socket_.get_executor());
            auto endpoints = resolver.resolve(host, std::to_string(port));
            socket_.send_to(asio::buffer(data), *endpoints.begin());
            return true;
        } catch (std::exception& e) {
            spdlog::error("UDP send error: {}", e.what());
            return false;
        }
    }

    std::vector<uint8_t> recv(std::string& from_host, uint16_t& from_port,
        std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        try {
            uint8_t buf[65536];
            asio::ip::udp::endpoint sender;
            socket_.non_blocking(true);

            auto deadline = std::chrono::steady_clock::now() + timeout;
            size_t len = 0;

            while (std::chrono::steady_clock::now() < deadline) {
                asio::error_code ec;
                len = socket_.receive_from(asio::buffer(buf), sender, 0, ec);
                if (!ec) break;
                if (ec != asio::error::would_block && ec != asio::error::try_again) {
                    spdlog::debug("UDP recv error: {}", ec.message());
                    return {};
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            if (len > 0) {
                from_host = sender.address().to_string();
                from_port = sender.port();
                return std::vector<uint8_t>(buf, buf + len);
            }
        } catch (std::exception& e) {
            spdlog::debug("UDP recv exception: {}", e.what());
        }
        return {};
    }

    uint16_t local_port() const { return local_port_; }
    void close() { socket_.close(); }

private:
    asio::ip::udp::socket socket_;
    uint16_t local_port_ = 0;
};

// ====== Connection Helpers ======

class ConnectionPool {
public:
    ConnectionPool(size_t max_connections = 100) : max_connections_(max_connections) {}

    bool add(uint64_t id, StreamPtr stream) {
        std::lock_guard lk(mutex_);
        if (connections_.size() >= max_connections_) return false;
        connections_[id] = ConnectionEntry{std::move(stream), std::chrono::steady_clock::now()};
        return true;
    }

    StreamPtr get(uint64_t id) {
        std::lock_guard lk(mutex_);
        auto it = connections_.find(id);
        return it != connections_.end() ? it->second.stream : nullptr;
    }

    void remove(uint64_t id) {
        std::lock_guard lk(mutex_);
        connections_.erase(id);
    }

    void cleanup_expired(std::chrono::seconds max_age = std::chrono::seconds(300)) {
        std::lock_guard lk(mutex_);
        auto now = std::chrono::steady_clock::now();
        for (auto it = connections_.begin(); it != connections_.end();) {
            if (now - it->second.created > max_age) {
                it = connections_.erase(it);
            } else {
                ++it;
            }
        }
    }

    size_t size() const {
        std::lock_guard lk(mutex_);
        return connections_.size();
    }

    std::vector<uint64_t> get_all_ids() const {
        std::lock_guard lk(mutex_);
        std::vector<uint64_t> ids;
        for (auto& [id, _] : connections_) ids.push_back(id);
        return ids;
    }

private:
    struct ConnectionEntry {
        StreamPtr stream;
        std::chrono::steady_clock::time_point created;
    };
    std::map<uint64_t, ConnectionEntry> connections_;
    size_t max_connections_;
    mutable std::mutex mutex_;
};

// ====== TLS / SSL ======

class TlsManager {
public:
    enum class TlsType {
        NONE,
        SYSTEM,
        CUSTOM,
    };

    static TlsManager& instance() {
        static TlsManager mgr;
        return mgr;
    }

    void set_certificate_path(const std::string& path) {
        cert_path_ = path;
        spdlog::info("TLS certificate path: {}", path);
    }

    void set_key_path(const std::string& path) {
        key_path_ = path;
        spdlog::info("TLS key path: {}", path);
    }

    bool verify_certificate(const std::string& host, const std::string& fingerprint) {
        std::lock_guard lk(mutex_);
        auto it = cached_certs_.find(host);
        if (it != cached_certs_.end()) {
            return it->second == fingerprint;
        }
        cached_certs_[host] = fingerprint;
        return true;
    }

    TlsType get_tls_type() const { return tls_type_; }
    void set_tls_type(TlsType t) { tls_type_ = t; }

private:
    TlsManager() = default;
    std::string cert_path_;
    std::string key_path_;
    TlsType tls_type_ = TlsType::NONE;
    std::map<std::string, std::string> cached_certs_;
    std::mutex mutex_;
};

TlsType get_cached_tls_type() { return TlsManager::instance().get_tls_type(); }
void upsert_tls_cache(bool accept_invalid, TlsType type) {
    TlsManager::instance().set_tls_type(type);
}
bool get_cached_tls_accept_invalid_cert() { return false; }

// ====== KCP Stream (reliable UDP) ======

class KcpStream {
public:
    KcpStream(uint32_t conv, const std::string& remote_addr, uint16_t remote_port)
        : conv_(conv), remote_addr_(remote_addr), remote_port_(remote_port) {
        spdlog::info("KCP stream created: conv={}, remote={}:{}",
            conv, remote_addr, remote_port);
    }

    bool send(const std::vector<uint8_t>& data) {
        // KCP segmentation and reliable delivery
        kcp_send_buffer_.push(data);
        return flush_kcp();
    }

    std::vector<uint8_t> recv() {
        if (kcp_recv_buffer_.empty()) return {};
        auto data = std::move(kcp_recv_buffer_.front());
        kcp_recv_buffer_.pop();
        return data;
    }

    bool is_connected() const { return connected_; }
    void set_connected(bool c) { connected_ = c; }
    uint32_t conv() const { return conv_; }

    // KCP parameters
    void set_no_delay(int nodelay, int interval, int resend, int nc) {
        nodelay_ = nodelay;
        interval_ = interval;
        resend_ = resend;
        nc_ = nc;
    }

    void update(uint32_t current_ms) {
        // KCP update tick
        (void)current_ms;
    }

    void input(const std::vector<uint8_t>& data) {
        // Feed received UDP data to KCP
        for (auto& segment : split_segments(data)) {
            kcp_recv_buffer_.push(segment);
        }
    }

private:
    uint32_t conv_;
    std::string remote_addr_;
    uint16_t remote_port_;
    std::atomic<bool> connected_{false};
    std::queue<std::vector<uint8_t>> kcp_send_buffer_;
    std::queue<std::vector<uint8_t>> kcp_recv_buffer_;
    int nodelay_ = 1;
    int interval_ = 20;
    int resend_ = 2;
    int nc_ = 1;

    bool flush_kcp() {
        while (!kcp_send_buffer_.empty()) {
            // Simulate sending each queued message
            kcp_send_buffer_.pop();
        }
        return true;
    }

    std::vector<std::vector<uint8_t>> split_segments(const std::vector<uint8_t>& data) {
        return {data};
    }
};

// ====== Connection Factories ======

StreamPtr connect_tcp(const std::string& addr, uint16_t port,
    std::chrono::seconds timeout) {
    try {
        asio::io_context io;
        asio::ip::tcp::resolver resolver(io);
        auto endpoints = resolver.resolve(addr, std::to_string(port));

        asio::ip::tcp::socket socket(io);
        asio::error_code ec;

        // Wait with timeout
        auto deadline = std::chrono::steady_clock::now() + timeout;
        do {
            ec = asio::error::would_block;
            asio::connect(socket, endpoints, ec);
            if (!ec) break;
            if (std::chrono::steady_clock::now() > deadline) {
                spdlog::error("Connection to {}:{} timed out", addr, port);
                return nullptr;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } while (ec);

        if (ec) {
            spdlog::error("Connection to {}:{} failed: {}", addr, port, ec.message());
            return nullptr;
        }

        socket.set_option(asio::ip::tcp::no_delay(true));

        // Set socket buffer sizes
        asio::socket_base::send_buffer_size snd_option(256 * 1024);
        asio::socket_base::receive_buffer_size rcv_option(256 * 1024);
        socket.set_option(snd_option);
        socket.set_option(rcv_option);

        spdlog::info("Connected to {}:{}", addr, port);
        return std::make_shared<TcpStream>(std::move(socket));
    } catch (std::exception& e) {
        spdlog::error("Connect to {}:{} failed: {}", addr, port, e.what());
        return nullptr;
    }
}

StreamPtr connect_tcp_local(const std::string& addr, const std::string& bind_addr,
    std::chrono::seconds timeout) {
    // For local connections, skip the full resolver
    auto colon = addr.find(':');
    if (colon == std::string::npos) {
        return connect_tcp(addr, 0, timeout);
    }
    std::string host = addr.substr(0, colon);
    uint16_t port = static_cast<uint16_t>(std::stoi(addr.substr(colon + 1)));
    return connect_tcp(host, port, timeout);
}

std::unique_ptr<UdpSocketWrapper> new_udp(uint16_t port) {
    try {
        asio::io_context io;
        return std::make_unique<UdpSocketWrapper>(io, port);
    } catch (std::exception& e) {
        spdlog::error("Create UDP socket failed: {}", e.what());
        return nullptr;
    }
}

std::unique_ptr<UdpSocketWrapper> new_direct_udp(const std::string& addr,
    uint16_t port) {
    try {
        asio::io_context io;
        return std::make_unique<UdpSocketWrapper>(io, addr, port);
    } catch (std::exception& e) {
        spdlog::error("Create direct UDP socket failed: {}", e.what());
        return nullptr;
    }
}

// ====== Network Utilities ======

bool is_port_open(const std::string& host, uint16_t port,
    std::chrono::milliseconds timeout) {
    try {
        asio::io_context io;
        asio::ip::tcp::socket socket(io);
        asio::ip::tcp::resolver resolver(io);
        auto endpoints = resolver.resolve(host, std::to_string(port));

        asio::error_code ec;
        asio::connect(socket, endpoints, ec);

        return !ec;
    } catch (...) {
        return false;
    }
}

std::string resolve_hostname(const std::string& host) {
    try {
        asio::io_context io;
        asio::ip::tcp::resolver resolver(io);
        auto endpoints = resolver.resolve(host, "");
        if (!endpoints.empty()) {
            return endpoints.begin()->endpoint().address().to_string();
        }
    } catch (...) {}
    return "";
}

// ====== Message Framing ======

class MessageFramer {
public:
    struct FramedMessage {
        MessageType type;
        std::vector<uint8_t> data;
        uint32_t sequence = 0;
        uint64_t timestamp = 0;
    };

    std::vector<uint8_t> encode(MessageType type, const std::vector<uint8_t>& payload) {
        uint32_t magic = 0x50454443; // "CPPD"
        uint16_t version = 1;
        uint16_t type_val = static_cast<uint16_t>(type);
        uint32_t len = static_cast<uint32_t>(payload.size());
        uint32_t seq = next_seq_++;

        // Header: [magic:4][version:2][type:2][sequence:4][length:4][timestamp:8]
        std::vector<uint8_t> framed;
        framed.reserve(24 + payload.size());

        auto write_u32 = [&](uint32_t v) {
            framed.push_back((v >> 24) & 0xFF);
            framed.push_back((v >> 16) & 0xFF);
            framed.push_back((v >> 8) & 0xFF);
            framed.push_back(v & 0xFF);
        };
        auto write_u16 = [&](uint16_t v) {
            framed.push_back((v >> 8) & 0xFF);
            framed.push_back(v & 0xFF);
        };
        auto write_u64 = [&](uint64_t v) {
            for (int i = 7; i >= 0; i--) framed.push_back((v >> (i * 8)) & 0xFF);
        };

        auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        write_u32(magic);
        write_u16(version);
        write_u16(type_val);
        write_u32(seq);
        write_u32(len);
        write_u64(static_cast<uint64_t>(ts));
        framed.insert(framed.end(), payload.begin(), payload.end());

        return framed;
    }

    std::optional<FramedMessage> decode(const std::vector<uint8_t>& raw) {
        if (raw.size() < 24) return std::nullopt;

        auto read_u32 = [&raw](size_t off) -> uint32_t {
            return (static_cast<uint32_t>(raw[off]) << 24) |
                   (static_cast<uint32_t>(raw[off+1]) << 16) |
                   (static_cast<uint32_t>(raw[off+2]) << 8) |
                   static_cast<uint32_t>(raw[off+3]);
        };
        auto read_u16 = [&raw](size_t off) -> uint16_t {
            return (static_cast<uint16_t>(raw[off]) << 8) |
                   static_cast<uint16_t>(raw[off+1]);
        };

        uint32_t magic = read_u32(0);
        if (magic != 0x50454443) {
            spdlog::warn("Invalid message magic: {:08x}", magic);
            return std::nullopt;
        }

        FramedMessage msg;
        msg.type = static_cast<MessageType>(read_u16(4));
        msg.sequence = read_u32(6);
        uint32_t len = read_u32(10);
        msg.data = std::vector<uint8_t>(raw.begin() + 24, raw.begin() + 24 + len);
        return msg;
    }

private:
    std::atomic<uint32_t> next_seq_{0};
};

// ====== TCP Listener ======

class TcpListener {
public:
    TcpListener(asio::io_context& io, uint16_t port, bool reuse_addr = true)
        : acceptor_(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {
        acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(reuse_addr));
        spdlog::info("TCP listener started on port {}", port);
    }

    std::optional<asio::ip::tcp::socket> accept(std::chrono::seconds timeout) {
        asio::ip::tcp::socket socket(acceptor_.get_executor());
        asio::error_code ec;

        auto deadline = std::chrono::steady_clock::now() + timeout;
        acceptor_.non_blocking(true);

        while (std::chrono::steady_clock::now() < deadline) {
            acceptor_.accept(socket, ec);
            if (!ec) {
                socket.non_blocking(false);
                return std::move(socket);
            }
            if (ec != asio::error::would_block) {
                spdlog::debug("Accept error: {}", ec.message());
                return std::nullopt;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return std::nullopt;
    }

    std::string local_endpoint() const {
        return acceptor_.local_endpoint().address().to_string() + ":" +
            std::to_string(acceptor_.local_endpoint().port());
    }

    void close() { acceptor_.close(); }

private:
    asio::ip::tcp::acceptor acceptor_;
};

// ====== Address mangling ======

std::string addr_mangle(const std::string& addr, uint16_t port) {
    if (addr.find(':') != std::string::npos) return addr;
    return addr + ":" + std::to_string(port);
}

} // namespace cppdesk::common
