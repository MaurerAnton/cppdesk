// connection_state.cpp - State machine and metrics for connections
#include "server/server.hpp"
#include "common/config.hpp"
#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>
#include <mutex>

namespace cppdesk::server {

enum class ConnectionPhase {
    INIT,
    ACCEPTING,
    HANDSHAKING,
    EXCHANGING_KEYS,
    AUTHENTICATING,
    NEGOTIATING,
    READY,
    ACTIVE,
    CLOSING,
    CLOSED,
    ERROR,
    RECONNECTING,
};

class ConnectionStateMachine {
    ConnectionPhase phase_ = ConnectionPhase::INIT;
    int32_t id_;
    std::chrono::steady_clock::time_point entered_;
    mutable std::mutex m_;
public:
    explicit ConnectionStateMachine(int32_t id) : id_(id) { entered_ = std::chrono::steady_clock::now(); }
    bool transition(ConnectionPhase p) { auto old = phase_; phase_ = p; entered_ = std::chrono::steady_clock::now(); spdlog::debug("[{}] {}->{}", id_, (int)old, (int)p); return true; }
    ConnectionPhase phase() const { return phase_; }
    bool is_active() const { return phase_ == ConnectionPhase::ACTIVE; }
    bool is_closed() const { return phase_ >= ConnectionPhase::CLOSING; }
    auto elapsed() const { return std::chrono::steady_clock::now() - entered_; }
    static const char* phase_name(ConnectionPhase p) {
        switch(p) {
            case ConnectionPhase::INIT: return "INIT";
            case ConnectionPhase::ACCEPTING: return "ACCEPTING";
            case ConnectionPhase::HANDSHAKING: return "HANDSHAKING";
            case ConnectionPhase::EXCHANGING_KEYS: return "EXCHANGING_KEYS";
            case ConnectionPhase::AUTHENTICATING: return "AUTHENTICATING";
            case ConnectionPhase::NEGOTIATING: return "NEGOTIATING";
            case ConnectionPhase::READY: return "READY";
            case ConnectionPhase::ACTIVE: return "ACTIVE";
            case ConnectionPhase::CLOSING: return "CLOSING";
            case ConnectionPhase::CLOSED: return "CLOSED";
            case ConnectionPhase::ERROR: return "ERROR";
            case ConnectionPhase::RECONNECTING: return "RECONNECTING";
            default: return "?";
        }
    }
};

class ConnectionTimeoutTracker {
    std::chrono::seconds idle_{300}, handshake_{30};
    std::chrono::steady_clock::time_point last_;
public:
    void set_idle(std::chrono::seconds s) { idle_ = s; }
    void set_handshake(std::chrono::seconds s) { handshake_ = s; }
    void touch() { last_ = std::chrono::steady_clock::now(); }
    bool is_idle() const { return (std::chrono::steady_clock::now() - last_) > idle_; }
    bool handshake_timed_out() const { return (std::chrono::steady_clock::now() - last_) > handshake_; }
};

class ConnectionMetrics {
    std::atomic<uint64_t> sent_{0}, recv_{0}, msgs_sent_{0}, msgs_recv_{0}, errs_{0};
public:
    void record_send(size_t n) { sent_ += n; msgs_sent_++; }
    void record_recv(size_t n) { recv_ += n; msgs_recv_++; }
    void record_error() { errs_++; }
    auto snapshot() const { return std::make_tuple(sent_.load(), recv_.load(), msgs_sent_.load(), msgs_recv_.load(), errs_.load()); }
};

} // namespace