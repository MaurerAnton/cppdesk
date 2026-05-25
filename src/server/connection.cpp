// ============================================================================
// connection.cpp — Comprehensive Connection Management Implementation
// ============================================================================
// Implements:
//   - Full connection lifecycle: accept → handshake → authenticate →
//     service negotiation → data exchange → close
//   - Secure handshake with crypto key exchange (ECDH + AES-GCM)
//   - Message routing to services (video, audio, clipboard, input, terminal)
//   - Service subscription/unsubscription handling
//   - Permission enforcement (view-only, clipboard, file transfer, audio)
//   - Idle timeout detection and connection health monitoring
//   - Login failure rate limiting with exponential backoff
//   - Connection pooling and periodic cleanup
//   - Control message handling (switch display, privacy mode, etc.)
//   - Comprehensive error handling and graceful disconnect
//   - Per-connection statistics collection and telemetry
// ============================================================================

#include "cppdesk/server/server.hpp"
#include "common/protocol.hpp"
#include "common/config.hpp"
#include "common/crypto.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ============================================================================
// Platform detection
// ============================================================================
#if defined(_WIN32) || defined(_WIN64)
    #define CPPDESK_PLATFORM_WINDOWS 1
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#elif defined(__linux__) || defined(__unix__)
    #define CPPDESK_PLATFORM_LINUX 1
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <signal.h>
    #include <errno.h>
#elif defined(__APPLE__) || defined(__MACH__)
    #define CPPDESK_PLATFORM_MACOS 1
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <signal.h>
    #include <errno.h>
#endif

// ============================================================================
// namespace cppdesk::server
// ============================================================================
namespace cppdesk::server {

using namespace common;
using common::crypto::*;

// ============================================================================
// Forward declarations
// ============================================================================
class ConnectionManager;
class HandshakeManager;
class Authenticator;
class ConnectionPool;
class MessageRouter;
class ServiceNegotiator;
class PermissionEnforcer;
class IdleDetector;
class ControlHandler;
class ErrorHandler;
class ConnectionStatistics;
class MessageFramer;
class DataExchangeManager;
class KeyExchangeHandler;
class RateLimiter;
class ConnectionHealthMonitor;
struct ConnectionConfig;
struct ConnectionStateMachine;
struct ConnectionStats;
struct HandshakeState;
struct AuthState;
struct ServiceSubscription;
struct PendingMessage;
struct RateLimitEntry;
struct HealthMetric;

// ============================================================================
// Constants
// ============================================================================
namespace conn_constants {

// Connection states
inline constexpr int32_t CONN_STATE_INIT           = 0;
inline constexpr int32_t CONN_STATE_ACCEPTING      = 1;
inline constexpr int32_t CONN_STATE_HANDSHAKING    = 2;
inline constexpr int32_t CONN_STATE_AUTHENTICATING = 3;
inline constexpr int32_t CONN_STATE_NEGOTIATING    = 4;
inline constexpr int32_t CONN_STATE_READY          = 5;
inline constexpr int32_t CONN_STATE_ACTIVE         = 6;
inline constexpr int32_t CONN_STATE_CLOSING        = 7;
inline constexpr int32_t CONN_STATE_CLOSED         = 8;
inline constexpr int32_t CONN_STATE_ERROR          = 9;

// Timeouts (milliseconds)
inline constexpr int64_t HANDSHAKE_TIMEOUT_MS        = 10'000;   // 10 seconds
inline constexpr int64_t AUTH_TIMEOUT_MS              = 15'000;   // 15 seconds
inline constexpr int64_t NEGOTIATION_TIMEOUT_MS       = 5'000;    // 5 seconds
inline constexpr int64_t IDLE_TIMEOUT_MS              = 300'000;  // 5 minutes
inline constexpr int64_t HEARTBEAT_INTERVAL_MS        = 30'000;   // 30 seconds
inline constexpr int64_t HEARTBEAT_TIMEOUT_MS         = 90'000;   // 90 seconds
inline constexpr int64_t CONNECTION_POOL_CLEANUP_MS   = 60'000;   // 1 minute
inline constexpr int64_t STATS_REPORT_INTERVAL_MS     = 30'000;   // 30 seconds
inline constexpr int64_t RECONNECT_BACKOFF_MS         = 1'000;    // 1 second
inline constexpr int64_t MAX_RECONNECT_BACKOFF_MS     = 60'000;   // 1 minute

// Rate limiting
inline constexpr int32_t MAX_LOGIN_ATTEMPTS           = 5;
inline constexpr int64_t LOGIN_BLOCK_DURATION_MS      = 300'000;  // 5 minutes
inline constexpr int64_t RATE_LIMIT_WINDOW_MS         = 1'000;    // 1 second
inline constexpr int32_t MAX_REQUESTS_PER_WINDOW      = 100;

// Connection pool
inline constexpr int32_t MAX_CONNECTIONS              = 256;
inline constexpr int32_t MAX_CONNECTIONS_PER_IP       = 8;
inline constexpr int32_t MIN_CONNECTION_ID            = 1000;
inline constexpr size_t  MESSAGE_QUEUE_MAX_SIZE       = 1024;
inline constexpr size_t  SEND_BUFFER_SIZE             = 65536;
inline constexpr size_t  RECV_BUFFER_SIZE             = 65536;

// Handshake
inline constexpr size_t  HANDSHAKE_CHALLENGE_SIZE     = 32;
inline constexpr size_t  HANDSHAKE_SESSION_KEY_SIZE   = 32;
inline constexpr size_t  HANDSHAKE_NONCE_SIZE         = 12;
inline constexpr size_t  HANDSHAKE_MAX_RETRIES        = 3;
inline constexpr int32_t HANDSHAKE_PROTOCOL_VERSION   = 2;

// Message framing
inline constexpr uint32_t FRAME_MAGIC                 = 0x43505044; // "CPPD"
inline constexpr size_t   FRAME_HEADER_SIZE           = 16;
inline constexpr uint32_t MAX_MESSAGE_SIZE            = 16 * 1024 * 1024; // 16 MB

// Authentication
inline constexpr int32_t AUTH_SUCCESS                 = 0;
inline constexpr int32_t AUTH_INVALID_PASSWORD        = 1;
inline constexpr int32_t AUTH_BLOCKED                 = 2;
inline constexpr int32_t AUTH_SESSION_LIMIT           = 3;
inline constexpr int32_t AUTH_VERSION_MISMATCH        = 4;
inline constexpr int32_t AUTH_SERVER_BUSY             = 5;
inline constexpr int32_t AUTH_INTERNAL_ERROR          = 99;

// Statistics
inline constexpr int64_t STATS_BANDWIDTH_WINDOW_MS    = 5'000;  // 5 seconds

} // namespace conn_constants

// ============================================================================
// Enums
// ============================================================================

/// Connection state enumeration.
enum class ConnectionState : uint8_t {
    INIT          = 0,
    ACCEPTING     = 1,
    HANDSHAKING   = 2,
    AUTHENTICATING = 3,
    NEGOTIATING   = 4,
    READY         = 5,
    ACTIVE        = 6,
    CLOSING       = 7,
    CLOSED        = 8,
    ERROR         = 9,
};

/// Handshake phase.
enum class HandshakePhase : uint8_t {
    HELLO          = 0,
    KEY_EXCHANGE   = 1,
    VERIFY         = 2,
    COMPLETE       = 3,
    FAILED         = 4,
};

/// Disconnect reason.
enum class DisconnectReason : uint8_t {
    NORMAL            = 0,
    TIMEOUT           = 1,
    AUTH_FAILED       = 2,
    PROTOCOL_ERROR    = 3,
    NETWORK_ERROR     = 4,
    SERVER_SHUTDOWN   = 5,
    CLIENT_REQUEST    = 6,
    KICKED            = 7,
    BANNED            = 8,
    RATE_LIMITED      = 9,
    INTERNAL_ERROR    = 10,
    IDLE_TIMEOUT      = 11,
    HEARTBEAT_LOST    = 12,
    PERMISSION_DENIED = 13,
};

/// Service category for routing.
enum class ServiceCategory : uint8_t {
    VIDEO      = 0,
    AUDIO      = 1,
    INPUT      = 2,
    CLIPBOARD  = 3,
    FILE       = 4,
    TERMINAL   = 5,
    CONTROL    = 6,
    UNKNOWN    = 7,
};

/// Control action types.
enum class ControlAction : uint8_t {
    NONE                = 0,
    SWITCH_DISPLAY      = 1,
    SWITCH_PERMISSION   = 2,
    PRIVACY_MODE        = 3,
    RESTART_SERVICE     = 4,
    SHUTDOWN            = 5,
    LOGOUT              = 6,
    LOCK_SCREEN         = 7,
    REFRESH_VIDEO       = 8,
    CHANGE_QUALITY      = 9,
    REQUEST_KEYFRAME    = 10,
    MUTE_AUDIO          = 11,
    RECONNECT           = 12,
    PORT_FORWARD        = 13,
    CHAT_MESSAGE        = 14,
    WHITEBOARD_ACTION   = 15,
};

// ============================================================================
// Core Data Structures
// ============================================================================

/// Connection configuration.
struct ConnectionConfig {
    std::string    server_id;
    std::string    server_version;
    std::string    password_hash;
    std::string    password_salt;
    bool           require_encryption      = true;
    bool           require_authentication   = true;
    bool           allow_anonymous          = false;
    bool           view_only_default        = false;
    bool           enable_clipboard         = true;
    bool           enable_file_transfer     = true;
    bool           enable_audio             = true;
    bool           enable_terminal          = false;
    bool           enable_privacy_mode      = true;
    bool           enable_port_forwarding   = false;
    bool           enable_whiteboard        = false;
    bool           enable_chat              = true;
    int64_t        idle_timeout_ms          = conn_constants::IDLE_TIMEOUT_MS;
    int64_t        heartbeat_interval_ms    = conn_constants::HEARTBEAT_INTERVAL_MS;
    int64_t        heartbeat_timeout_ms     = conn_constants::HEARTBEAT_TIMEOUT_MS;
    int32_t        max_connections          = conn_constants::MAX_CONNECTIONS;
    int32_t        max_connections_per_ip   = conn_constants::MAX_CONNECTIONS_PER_IP;
    int32_t        max_login_attempts       = conn_constants::MAX_LOGIN_ATTEMPTS;
    int64_t        login_block_duration_ms  = conn_constants::LOGIN_BLOCK_DURATION_MS;
    Resolution     default_resolution;
    std::vector<std::string> allowed_services;
    std::vector<std::string> blocked_services;
    std::map<std::string, std::string> extra_options;

    [[nodiscard]] bool is_service_allowed(const std::string& name) const {
        if (!allowed_services.empty()) {
            return std::find(allowed_services.begin(),
                             allowed_services.end(), name)
                   != allowed_services.end();
        }
        if (!blocked_services.empty()) {
            return std::find(blocked_services.begin(),
                             blocked_services.end(), name)
                   == blocked_services.end();
        }
        return true;
    }

    [[nodiscard]] bool is_valid() const {
        return idle_timeout_ms > 0
            && heartbeat_interval_ms > 0
            && heartbeat_timeout_ms > heartbeat_interval_ms
            && max_connections > 0
            && max_connections_per_ip > 0
            && max_login_attempts > 0
            && login_block_duration_ms > 0;
    }
};

/// Per-connection runtime statistics.
struct ConnectionStats {
    // Connection info
    int32_t     connection_id           = 0;
    std::string remote_addr;
    std::string client_version;
    std::string client_platform;
    std::string username;

    // Timing
    int64_t     connected_at_ms         = 0;
    int64_t     authenticated_at_ms     = 0;
    int64_t     last_activity_ms        = 0;
    int64_t     last_heartbeat_ms       = 0;
    int64_t     last_data_sent_ms       = 0;
    int64_t     last_data_recv_ms       = 0;
    int64_t     idle_duration_ms        = 0;

    // Traffic
    uint64_t    bytes_sent              = 0;
    uint64_t    bytes_received          = 0;
    uint64_t    messages_sent           = 0;
    uint64_t    messages_received       = 0;
    uint64_t    frames_sent             = 0;
    uint64_t    frames_received         = 0;
    uint64_t    packets_sent            = 0;
    uint64_t    packets_received        = 0;

    // Bandwidth tracking
    std::deque<std::pair<int64_t, uint64_t>> sent_timeline;
    std::deque<std::pair<int64_t, uint64_t>> recv_timeline;
    uint64_t    current_send_bps        = 0;
    uint64_t    current_recv_bps        = 0;
    uint64_t    peak_send_bps           = 0;
    uint64_t    peak_recv_bps           = 0;

    // Errors
    uint64_t    send_errors             = 0;
    uint64_t    recv_errors             = 0;
    uint64_t    protocol_errors         = 0;
    uint64_t    auth_failures           = 0;
    uint64_t    timeout_events          = 0;

    // Service stats
    std::map<std::string, uint64_t> service_messages_sent;
    std::map<std::string, uint64_t> service_messages_recv;
    uint32_t    active_subscriptions    = 0;

    // Quality metrics
    double      avg_latency_ms          = 0.0;
    double      max_latency_ms          = 0.0;
    double      min_latency_ms          = std::numeric_limits<double>::max();
    double      packet_loss_pct         = 0.0;

    // State
    ConnectionState state             = ConnectionState::INIT;
    DisconnectReason disconnect_reason = DisconnectReason::NORMAL;

    void update_bandwidth(int64_t now_ms) {
        // Purge old entries
        auto cutoff = now_ms - conn_constants::STATS_BANDWIDTH_WINDOW_MS;
        while (!sent_timeline.empty() && sent_timeline.front().first < cutoff) {
            sent_timeline.pop_front();
        }
        while (!recv_timeline.empty() && recv_timeline.front().first < cutoff) {
            recv_timeline.pop_front();
        }

        // Calculate current bandwidth
        current_send_bps = 0;
        for (auto& [ts, bytes] : sent_timeline) current_send_bps += bytes;
        current_send_bps = current_send_bps * 1000
                         / conn_constants::STATS_BANDWIDTH_WINDOW_MS;

        current_recv_bps = 0;
        for (auto& [ts, bytes] : recv_timeline) current_recv_bps += bytes;
        current_recv_bps = current_recv_bps * 1000
                         / conn_constants::STATS_BANDWIDTH_WINDOW_MS;

        peak_send_bps = std::max(peak_send_bps, current_send_bps);
        peak_recv_bps = std::max(peak_recv_bps, current_recv_bps);
    }

    void record_send(uint64_t bytes, int64_t now_ms) {
        bytes_sent += bytes;
        messages_sent++;
        packets_sent++;
        last_data_sent_ms = now_ms;
        last_activity_ms = now_ms;
        sent_timeline.emplace_back(now_ms, bytes);
        update_bandwidth(now_ms);
    }

    void record_recv(uint64_t bytes, int64_t now_ms) {
        bytes_received += bytes;
        messages_received++;
        packets_received++;
        last_data_recv_ms = now_ms;
        last_activity_ms = now_ms;
        recv_timeline.emplace_back(now_ms, bytes);
        update_bandwidth(now_ms);
    }

    [[nodiscard]] std::string summary() const {
        std::ostringstream oss;
        oss << "Conn#" << connection_id
            << " [" << remote_addr << "]"
            << " state=" << static_cast<int>(state)
            << " sent=" << bytes_sent << "B"
            << " recv=" << bytes_received << "B"
            << " s_rate=" << current_send_bps << "bps"
            << " r_rate=" << current_recv_bps << "bps"
            << " subs=" << active_subscriptions
            << " err=" << (send_errors + recv_errors + protocol_errors)
            << " idle=" << idle_duration_ms << "ms";
        return oss.str();
    }
};

/// Handshake state tracking.
struct HandshakeState {
    HandshakePhase  phase           = HandshakePhase::HELLO;
    int32_t         protocol_version = conn_constants::HANDSHAKE_PROTOCOL_VERSION;
    int32_t         retry_count     = 0;
    int64_t         started_at_ms   = 0;
    int64_t         last_activity_ms = 0;

    std::array<uint8_t, 32>  server_public_key{};
    std::array<uint8_t, 32>  server_secret_key{};
    std::array<uint8_t, 32>  client_public_key{};
    std::array<uint8_t, 32>  shared_secret{};
    std::array<uint8_t, 12>  encryption_nonce{};
    std::array<uint8_t, 32>  session_key{};

    std::vector<uint8_t>    challenge;
    std::vector<uint8_t>    challenge_response;
    bool                    encryption_established = false;
    bool                    verified               = false;

    void reset() {
        phase = HandshakePhase::HELLO;
        retry_count = 0;
        started_at_ms = 0;
        last_activity_ms = 0;
        server_public_key.fill(0);
        server_secret_key.fill(0);
        client_public_key.fill(0);
        shared_secret.fill(0);
        encryption_nonce.fill(0);
        session_key.fill(0);
        challenge.clear();
        challenge_response.clear();
        encryption_established = false;
        verified = false;
    }

    [[nodiscard]] bool is_expired(int64_t now_ms) const {
        if (started_at_ms == 0) return false;
        return (now_ms - started_at_ms) > conn_constants::HANDSHAKE_TIMEOUT_MS;
    }

    [[nodiscard]] bool can_retry() const {
        return retry_count < conn_constants::HANDSHAKE_MAX_RETRIES;
    }
};

/// Authentication state tracking.
struct AuthState {
    bool        authenticated       = false;
    int32_t     auth_result         = conn_constants::AUTH_SUCCESS;
    std::string auth_message;
    std::string username;
    std::string client_version;
    std::string client_platform;
    int64_t     started_at_ms       = 0;
    int32_t     attempt_count       = 0;
    ControlPermissions permissions;
    bool        view_only           = false;

    void reset() {
        authenticated = false;
        auth_result = conn_constants::AUTH_SUCCESS;
        auth_message.clear();
        username.clear();
        client_version.clear();
        client_platform.clear();
        started_at_ms = 0;
        attempt_count = 0;
        permissions = ControlPermissions{};
        view_only = false;
    }

    [[nodiscard]] bool is_expired(int64_t now_ms) const {
        if (started_at_ms == 0) return false;
        return (now_ms - started_at_ms) > conn_constants::AUTH_TIMEOUT_MS;
    }
};

/// Service subscription record.
struct ServiceSubscription {
    std::string service_name;
    int64_t     subscribed_at_ms = 0;
    bool        active           = true;
    uint64_t    messages_sent    = 0;
    uint64_t    messages_recv    = 0;
    std::map<std::string, std::string> options;
};

/// Pending outgoing message.
struct PendingMessage {
    MessageType type;
    std::vector<uint8_t> data;
    int64_t     queued_at_ms       = 0;
    int64_t     expires_at_ms      = 0;
    int32_t     priority           = 0;  // 0=highest, 255=lowest
    bool        requires_ack       = false;
    uint32_t    sequence           = 0;
    int32_t     retry_count        = 0;

    [[nodiscard]] bool is_expired(int64_t now_ms) const {
        return expires_at_ms > 0 && now_ms > expires_at_ms;
    }
};

/// Rate limit entry for an IP address.
struct RateLimitEntry {
    int32_t     request_count       = 0;
    int64_t     window_start_ms     = 0;
    int32_t     login_failures      = 0;
    int64_t     blocked_until_ms    = 0;
    bool        permanently_blocked = false;
};

/// Health monitoring metric.
struct HealthMetric {
    double      avg_latency_ms      = 0.0;
    double      packet_loss         = 0.0;
    int32_t     missed_heartbeats   = 0;
    bool        connection_stalled  = false;
    int64_t     last_health_check_ms = 0;
};

// ============================================================================
// MessageFramer — binary message framing for the stream protocol
// ============================================================================
// Frame format (little-endian):
//   [0..3]   Magic: 0x43505044 ("CPPD")
//   [4..7]   Message type (uint32_t)
//   [8..11]  Payload size (uint32_t)
//   [12..15] Sequence number (uint16_t) | Flags (uint16_t)
//   [16..N]  Payload (variable)
// ============================================================================
class MessageFramer {
public:
    MessageFramer() = default;

    /// Encode a message into a framed binary packet.
    [[nodiscard]] std::vector<uint8_t> encode(
        MessageType type,
        const std::vector<uint8_t>& payload,
        uint16_t sequence = 0,
        uint16_t flags = 0) const
    {
        std::vector<uint8_t> frame;
        frame.reserve(conn_constants::FRAME_HEADER_SIZE + payload.size());

        // Magic
        uint32_t magic = conn_constants::FRAME_MAGIC;
        auto* magic_bytes = reinterpret_cast<const uint8_t*>(&magic);
        frame.insert(frame.end(), magic_bytes, magic_bytes + 4);

        // Message type
        uint32_t type_val = static_cast<uint32_t>(type);
        auto* type_bytes = reinterpret_cast<const uint8_t*>(&type_val);
        frame.insert(frame.end(), type_bytes, type_bytes + 4);

        // Payload size
        uint32_t size = static_cast<uint32_t>(payload.size());
        auto* size_bytes = reinterpret_cast<const uint8_t*>(&size);
        frame.insert(frame.end(), size_bytes, size_bytes + 4);

        // Sequence + Flags
        uint32_t seq_flags = (static_cast<uint32_t>(flags) << 16) | sequence;
        auto* sf_bytes = reinterpret_cast<const uint8_t*>(&seq_flags);
        frame.insert(frame.end(), sf_bytes, sf_bytes + 4);

        // Payload
        frame.insert(frame.end(), payload.begin(), payload.end());

        return frame;
    }

    /// Decode a framed binary packet into message type and payload.
    /// Returns true if a complete frame was decoded.
    [[nodiscard]] bool decode(
        const std::vector<uint8_t>& buffer,
        size_t& consumed,
        MessageType& type,
        std::vector<uint8_t>& payload,
        uint16_t& sequence,
        uint16_t& flags) const
    {
        consumed = 0;
        if (buffer.size() < conn_constants::FRAME_HEADER_SIZE) {
            return false; // Incomplete header
        }

        // Verify magic
        uint32_t magic;
        std::memcpy(&magic, buffer.data(), 4);
        if (magic != conn_constants::FRAME_MAGIC) {
            spdlog::warn("MessageFramer: invalid magic 0x{:08X}, expected 0x{:08X}",
                magic, conn_constants::FRAME_MAGIC);
            return false;
        }

        // Message type
        uint32_t type_val;
        std::memcpy(&type_val, buffer.data() + 4, 4);
        type = static_cast<MessageType>(type_val);

        // Payload size
        uint32_t payload_size;
        std::memcpy(&payload_size, buffer.data() + 8, 4);

        // Sequence + Flags
        uint32_t seq_flags;
        std::memcpy(&seq_flags, buffer.data() + 12, 4);
        sequence = static_cast<uint16_t>(seq_flags & 0xFFFF);
        flags = static_cast<uint16_t>((seq_flags >> 16) & 0xFFFF);

        // Validate payload size
        if (payload_size > conn_constants::MAX_MESSAGE_SIZE) {
            spdlog::error("MessageFramer: payload size {} exceeds max {}",
                payload_size, conn_constants::MAX_MESSAGE_SIZE);
            consumed = conn_constants::FRAME_HEADER_SIZE;
            return false;
        }

        // Check if we have the full payload
        size_t total_frame_size = conn_constants::FRAME_HEADER_SIZE + payload_size;
        if (buffer.size() < total_frame_size) {
            return false; // Incomplete frame
        }

        // Extract payload
        payload.assign(
            buffer.begin() + conn_constants::FRAME_HEADER_SIZE,
            buffer.begin() + total_frame_size);

        consumed = total_frame_size;
        return true;
    }

    /// Create a simple message with JSON payload.
    [[nodiscard]] std::vector<uint8_t> encode_json(
        MessageType type,
        const nlohmann::json& j,
        uint16_t sequence = 0) const
    {
        std::string json_str = j.dump();
        std::vector<uint8_t> payload(json_str.begin(), json_str.end());
        return encode(type, payload, sequence);
    }

    /// Decode a message whose payload is JSON.
    [[nodiscard]] static nlohmann::json decode_json_payload(
        const std::vector<uint8_t>& payload)
    {
        if (payload.empty()) return nlohmann::json{};
        try {
            return nlohmann::json::parse(
                std::string_view(
                    reinterpret_cast<const char*>(payload.data()),
                    payload.size()));
        } catch (const nlohmann::json::exception& e) {
            spdlog::warn("MessageFramer: JSON parse error: {}", e.what());
            return nlohmann::json{};
        }
    }
};

// ============================================================================
// HandshakeManager — secure connection handshake with key exchange
// ============================================================================
class HandshakeManager {
public:
    HandshakeManager() {
        generate_server_keys();
    }

    /// Generate a fresh set of ephemeral server keys for ECDH.
    void generate_server_keys() {
        auto kp = crypto::generate_box_keypair();
        std::memcpy(state_.server_public_key.data(), kp.pk.data(), 32);
        std::memcpy(state_.server_secret_key.data(), kp.sk.data(), 32);
        state_.challenge = crypto::random_bytes(
            conn_constants::HANDSHAKE_CHALLENGE_SIZE);
        spdlog::debug("HandshakeManager: generated new ephemeral key pair");
    }

    /// Start the handshake — generate hello message to send to client.
    [[nodiscard]] std::vector<uint8_t> create_hello_message() {
        state_.reset();
        generate_server_keys();
        state_.phase = HandshakePhase::HELLO;
        state_.started_at_ms = now_ms();
        state_.last_activity_ms = state_.started_at_ms;

        nlohmann::json hello;
        hello["protocol_version"] = conn_constants::HANDSHAKE_PROTOCOL_VERSION;
        hello["server_public_key"] = crypto::encode64(
            state_.server_public_key.data(), 32);
        hello["challenge"] = crypto::encode64(
            state_.challenge.data(), state_.challenge.size());
        hello["supported_codecs"] = {"H264", "H265", "VP9", "VP8"};
        hello["server_version"] = common::get_version_number();

        std::string json_str = hello.dump();
        std::vector<uint8_t> payload(json_str.begin(), json_str.end());
        return framer_.encode(MessageType::MISC, payload);
    }

    /// Process client's key exchange response.
    [[nodiscard]] bool process_key_exchange(
        const std::vector<uint8_t>& payload,
        std::vector<uint8_t>& response)
    {
        if (state_.phase != HandshakePhase::HELLO) {
            spdlog::warn("HandshakeManager: unexpected key exchange in phase {}",
                static_cast<int>(state_.phase));
            return false;
        }

        state_.phase = HandshakePhase::KEY_EXCHANGE;
        state_.last_activity_ms = now_ms();

        auto j = MessageFramer::decode_json_payload(payload);
        if (j.empty()) return false;

        // Decode client's public key
        std::string client_pk_b64 = j.value("client_public_key", "");
        auto client_pk = crypto::decode64(client_pk_b64);
        if (client_pk.size() != 32) {
            spdlog::error("HandshakeManager: invalid client public key size {}",
                client_pk.size());
            return false;
        }
        std::memcpy(state_.client_public_key.data(), client_pk.data(), 32);

        // Decode challenge response
        std::string challenge_resp_b64 = j.value("challenge_response", "");
        state_.challenge_response = crypto::decode64(challenge_resp_b64);

        // Verify challenge response (client signs our challenge)
        bool challenge_ok = crypto::sign_verify(
            state_.challenge.data(), state_.challenge.size(),
            state_.challenge_response.data(),
            state_.client_public_key.data());

        if (!challenge_ok) {
            spdlog::warn("HandshakeManager: challenge verification failed");
            state_.phase = HandshakePhase::FAILED;
            return false;
        }

        // Compute shared secret using ECDH (box_keypair + client PK → shared)
        auto nonce_bytes = crypto::random_bytes(crypto::BOX_NONCE_BYTES);
        std::array<uint8_t, crypto::BOX_NONCE_BYTES> nonce{};
        std::memcpy(nonce.data(), nonce_bytes.data(), crypto::BOX_NONCE_BYTES);

        // Derive shared secret by encrypting zero bytes with box
        std::vector<uint8_t> zeros(32, 0);
        auto encrypted = crypto::box_encrypt(
            zeros.data(), zeros.size(),
            nonce.data(),
            state_.client_public_key.data(),
            state_.server_secret_key.data());

        if (encrypted.size() < 32) {
            spdlog::error("HandshakeManager: key agreement failed");
            state_.phase = HandshakePhase::FAILED;
            return false;
        }

        // Use the first 32 bytes of the encrypted output as shared secret
        std::memcpy(state_.shared_secret.data(), encrypted.data(), 32);

        // Derive session key using SHA-256(shared_secret || challenge)
        std::vector<uint8_t> session_material;
        session_material.insert(session_material.end(),
            state_.shared_secret.begin(), state_.shared_secret.end());
        session_material.insert(session_material.end(),
            state_.challenge.begin(), state_.challenge.end());

        auto session_hash = crypto::sha256(
            session_material.data(), session_material.size());
        std::memcpy(state_.session_key.data(), session_hash.data(), 32);

        // Generate encryption nonce
        auto enc_nonce = crypto::random_bytes(crypto::AES_NONCE_BYTES);
        std::memcpy(state_.encryption_nonce.data(), enc_nonce.data(),
            crypto::AES_NONCE_BYTES);

        state_.encryption_established = true;
        state_.phase = HandshakePhase::VERIFY;

        // Build response
        nlohmann::json resp;
        resp["status"] = "ok";
        resp["session_nonce"] = crypto::encode64(
            state_.encryption_nonce.data(), crypto::AES_NONCE_BYTES);
        resp["encryption"] = "aes-256-gcm";
        resp["compression"] = "none";

        std::string json_str = resp.dump();
        std::vector<uint8_t> resp_payload(json_str.begin(), json_str.end());
        response = framer_.encode(MessageType::MISC, resp_payload);

        spdlog::info("HandshakeManager: key exchange complete, encryption established");
        return true;
    }

    /// Complete the handshake verification phase.
    [[nodiscard]] bool complete_handshake() {
        if (state_.phase != HandshakePhase::VERIFY) {
            return false;
        }
        state_.phase = HandshakePhase::COMPLETE;
        state_.verified = true;
        spdlog::info("HandshakeManager: handshake completed successfully");
        return true;
    }

    /// Encrypt data with the session key.
    [[nodiscard]] std::vector<uint8_t> encrypt(
        const std::vector<uint8_t>& plaintext) const
    {
        if (!state_.encryption_established) return plaintext;
        return crypto::aes_gcm_encrypt(
            plaintext.data(), plaintext.size(),
            state_.session_key.data(),
            state_.encryption_nonce.data());
    }

    /// Decrypt data with the session key.
    [[nodiscard]] std::vector<uint8_t> decrypt(
        const std::vector<uint8_t>& ciphertext) const
    {
        if (!state_.encryption_established) return ciphertext;
        return crypto::aes_gcm_decrypt(
            ciphertext.data(), ciphertext.size(),
            state_.session_key.data(),
            state_.encryption_nonce.data());
    }

    /// Get the session key for the underlying stream.
    [[nodiscard]] std::vector<uint8_t> get_session_key() const {
        return std::vector<uint8_t>(
            state_.session_key.begin(), state_.session_key.end());
    }

    [[nodiscard]] bool is_encrypted() const {
        return state_.encryption_established;
    }

    [[nodiscard]] bool is_complete() const {
        return state_.phase == HandshakePhase::COMPLETE;
    }

    [[nodiscard]] bool is_expired(int64_t now) const {
        return state_.is_expired(now);
    }

    [[nodiscard]] bool can_retry() const {
        return state_.can_retry();
    }

    void record_retry() { state_.retry_count++; }
    [[nodiscard]] HandshakePhase phase() const { return state_.phase; }

    void reset() { state_.reset(); }

    [[nodiscard]] const HandshakeState& state() const { return state_; }

private:
    HandshakeState  state_;
    MessageFramer   framer_;

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// ============================================================================
// RateLimiter — generic request rate limiter
// ============================================================================
class RateLimiter {
public:
    RateLimiter(int32_t max_requests, int64_t window_ms)
        : max_requests_(max_requests), window_ms_(window_ms) {}

    /// Check if a request from the given key is allowed.
    /// Returns true if allowed, false if rate limited.
    [[nodiscard]] bool allow(const std::string& key) {
        int64_t now = now_ms();
        std::lock_guard lk(mutex_);

        auto& entry = entries_[key];

        // Reset window if expired
        if (now - entry.window_start_ms > window_ms_) {
            entry.request_count = 0;
            entry.window_start_ms = now;
        }

        entry.request_count++;

        // Clean old entries periodically
        if (entries_.size() > 10000) {
            cleanup_expired(now);
        }

        return entry.request_count <= max_requests_;
    }

    /// Check login attempt rate limiting.
    [[nodiscard]] bool allow_login(const std::string& addr) {
        std::lock_guard lk(mutex_);
        auto& entry = entries_[addr];
        int64_t now = now_ms();

        // Check if blocked
        if (entry.blocked_until_ms > now) {
            spdlog::warn("RateLimiter: login from {} blocked until {}",
                addr, entry.blocked_until_ms);
            return false;
        }
        if (entry.permanently_blocked) {
            return false;
        }

        return true;
    }

    /// Record a failed login attempt.
    void record_login_failure(const std::string& addr,
        int64_t block_duration_ms) {
        std::lock_guard lk(mutex_);
        int64_t now = now_ms();
        auto& entry = entries_[addr];
        entry.login_failures++;
        entry.window_start_ms = now;

        if (entry.login_failures >= 5) {
            entry.blocked_until_ms = now + block_duration_ms;
            spdlog::warn("RateLimiter: {} blocked for {}ms after {} failures",
                addr, block_duration_ms, entry.login_failures);
        }
    }

    /// Reset rate limit for an address (on successful login).
    void reset(const std::string& addr) {
        std::lock_guard lk(mutex_);
        entries_.erase(addr);
    }

    /// Permanently block an address.
    void ban(const std::string& addr) {
        std::lock_guard lk(mutex_);
        auto& entry = entries_[addr];
        entry.permanently_blocked = true;
        spdlog::warn("RateLimiter: {} permanently banned", addr);
    }

    /// Unban an address.
    void unban(const std::string& addr) {
        std::lock_guard lk(mutex_);
        entries_.erase(addr);
    }

    [[nodiscard]] bool is_banned(const std::string& addr) const {
        std::lock_guard lk(mutex_);
        auto it = entries_.find(addr);
        return it != entries_.end() && it->second.permanently_blocked;
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard lk(mutex_);
        return entries_.size();
    }

private:
    int32_t max_requests_;
    int64_t window_ms_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, RateLimitEntry> entries_;

    void cleanup_expired(int64_t now) {
        for (auto it = entries_.begin(); it != entries_.end(); ) {
            if (now - it->second.window_start_ms > window_ms_ * 10 &&
                it->second.blocked_until_ms < now &&
                !it->second.permanently_blocked) {
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// ============================================================================
// Authenticator — connection authentication
// ============================================================================
class Authenticator {
public:
    explicit Authenticator(const ConnectionConfig& config)
        : config_(config),
          login_rate_limiter_(
              config.max_login_attempts,
              config.login_block_duration_ms) {}

    /// Authenticate a login attempt. Returns auth result code.
    [[nodiscard]] int32_t authenticate(
        const std::string& remote_addr,
        const std::string& password,
        const std::string& client_version,
        AuthState& auth_state)
    {
        auth_state.started_at_ms = now_ms();
        auth_state.attempt_count++;
        auth_state.client_version = client_version;

        // Check rate limiting
        if (!login_rate_limiter_.allow_login(remote_addr)) {
            auth_state.auth_result = conn_constants::AUTH_BLOCKED;
            auth_state.auth_message = "Too many login attempts. Please wait.";
            spdlog::warn("Authenticator: login blocked for {}", remote_addr);
            return conn_constants::AUTH_BLOCKED;
        }

        // Check banned
        if (login_rate_limiter_.is_banned(remote_addr)) {
            auth_state.auth_result = conn_constants::AUTH_BLOCKED;
            auth_state.auth_message = "This address has been banned.";
            spdlog::warn("Authenticator: banned address {} attempted login", remote_addr);
            return conn_constants::AUTH_BLOCKED;
        }

        // Verify password
        if (config_.require_authentication && !config_.password_hash.empty()) {
            bool password_ok = crypto::verify_password(
                password, config_.password_hash, config_.password_salt);

            if (!password_ok) {
                login_rate_limiter_.record_login_failure(
                    remote_addr, config_.login_block_duration_ms);
                auth_state.auth_result = conn_constants::AUTH_INVALID_PASSWORD;
                auth_state.auth_message = "Invalid password.";
                auth_state.auth_failures++;
                spdlog::warn("Authenticator: invalid password from {} (attempt {})",
                    remote_addr, auth_state.attempt_count);
                return conn_constants::AUTH_INVALID_PASSWORD;
            }
        } else if (config_.allow_anonymous) {
            spdlog::info("Authenticator: anonymous login allowed from {}", remote_addr);
        } else {
            auth_state.auth_result = conn_constants::AUTH_INTERNAL_ERROR;
            auth_state.auth_message = "Authentication required but not configured.";
            spdlog::error("Authenticator: no authentication configured");
            return conn_constants::AUTH_INTERNAL_ERROR;
        }

        // Check version compatibility (warn but don't block)
        if (!client_version.empty() && !config_.server_version.empty()) {
            if (client_version != config_.server_version) {
                spdlog::info("Authenticator: version mismatch server={} client={}",
                    config_.server_version, client_version);
            }
        }

        // Authentication successful
        login_rate_limiter_.reset(remote_addr);
        auth_state.authenticated = true;
        auth_state.auth_result = conn_constants::AUTH_SUCCESS;
        auth_state.auth_message = "Authentication successful.";
        auth_state.view_only = config_.view_only_default;

        // Set default permissions
        auth_state.permissions.keyboard = !config_.view_only_default;
        auth_state.permissions.clipboard = config_.enable_clipboard;
        auth_state.permissions.file_transfer = config_.enable_file_transfer;
        auth_state.permissions.audio = config_.enable_audio;
        auth_state.permissions.privacy_mode = config_.enable_privacy_mode;

        spdlog::info("Authenticator: successful login from {} (version {})",
            remote_addr, client_version);
        return conn_constants::AUTH_SUCCESS;
    }

    /// Create a login response message.
    [[nodiscard]] std::vector<uint8_t> create_login_response(
        int32_t result_code,
        const std::string& message,
        const ControlPermissions& perms,
        const Resolution& resolution) const
    {
        LoginResponse resp;
        resp.success = (result_code == conn_constants::AUTH_SUCCESS);
        resp.message = message;
        resp.code = result_code;
        resp.view_only = !perms.keyboard;
        resp.resolution = resolution;

        nlohmann::json j;
        j["success"] = resp.success;
        j["message"] = resp.message;
        j["code"] = resp.code;
        j["view_only"] = resp.view_only;
        j["resolution"] = {
            {"width", resp.resolution.width},
            {"height", resp.resolution.height}
        };
        j["permissions"] = {
            {"keyboard", perms.keyboard},
            {"clipboard", perms.clipboard},
            {"file_transfer", perms.file_transfer},
            {"audio", perms.audio},
            {"privacy_mode", perms.privacy_mode},
            {"restart", perms.restart},
            {"shutdown", perms.shutdown}
        };
        j["server_version"] = config_.server_version;

        std::string json_str = j.dump();
        return std::vector<uint8_t>(json_str.begin(), json_str.end());
    }

    /// Update permissions for a connection.
    void update_permissions(
        AuthState& auth_state,
        const ControlPermissions& new_perms)
    {
        auth_state.permissions = new_perms;
        auth_state.view_only = !new_perms.keyboard;
        spdlog::info("Authenticator: permissions updated "
            "(keyboard={}, clipboard={}, file_transfer={}, audio={})",
            new_perms.keyboard, new_perms.clipboard,
            new_perms.file_transfer, new_perms.audio);
    }

    void ban_address(const std::string& addr) {
        login_rate_limiter_.ban(addr);
    }

    void unban_address(const std::string& addr) {
        login_rate_limiter_.unban(addr);
    }

    [[nodiscard]] const ConnectionConfig& config() const { return config_; }

private:
    ConnectionConfig config_;
    RateLimiter      login_rate_limiter_;

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// ============================================================================
// PermissionEnforcer — validates operation permissions
// ============================================================================
class PermissionEnforcer {
public:
    PermissionEnforcer() = default;

    /// Check if a message type is allowed given current permissions.
    [[nodiscard]] bool is_allowed(
        MessageType type,
        const ControlPermissions& perms,
        bool view_only) const
    {
        switch (type) {
        // Input events — require keyboard permission
        case MessageType::MOUSE_EVENT:
        case MessageType::KEY_EVENT:
            if (view_only) {
                spdlog::debug("PermissionEnforcer: input blocked (view-only mode)");
                return false;
            }
            if (!perms.keyboard) {
                spdlog::debug("PermissionEnforcer: input blocked (keyboard permission)");
                return false;
            }
            return true;

        // Cursor data — always allowed (read-only)
        case MessageType::CURSOR_DATA:
        case MessageType::CURSOR_POSITION:
        case MessageType::CURSOR_SHAPE:
            return true;

        // Clipboard — require clipboard permission
        case MessageType::CLIPBOARD_TEXT:
        case MessageType::CLIPBOARD_IMAGE:
            if (!perms.clipboard) {
                spdlog::debug("PermissionEnforcer: clipboard blocked");
                return false;
            }
            return true;

        // Clipboard file — separate permission
        case MessageType::CLIPBOARD_FILE:
            if (!perms.clipboard) {
                spdlog::debug("PermissionEnforcer: clipboard file blocked");
                return false;
            }
            return true;

        // File transfer — require file_transfer permission
        case MessageType::FILE_TRANSFER_REQUEST:
        case MessageType::FILE_TRANSFER_RESPONSE:
        case MessageType::FILE_CHUNK:
        case MessageType::FILE_DONE:
        case MessageType::FILE_DIR:
            if (!perms.file_transfer) {
                spdlog::debug("PermissionEnforcer: file transfer blocked");
                return false;
            }
            return true;

        // Audio — require audio permission
        case MessageType::AUDIO_FRAME:
        case MessageType::AUDIO_CONFIG:
            if (!perms.audio) {
                spdlog::debug("PermissionEnforcer: audio blocked");
                return false;
            }
            return true;

        // Video — always allowed
        case MessageType::VIDEO_FRAME:
        case MessageType::VIDEO_CODEC_CHANGE:
        case MessageType::VIDEO_QUALITY_CHANGE:
            return true;

        // Control messages
        case MessageType::SWITCH_DISPLAY:
        case MessageType::SWITCH_PERMISSION:
            return true;

        // Privacy mode — check permission
        case MessageType::PRIVACY_MODE:
            if (!perms.privacy_mode) {
                spdlog::debug("PermissionEnforcer: privacy mode blocked");
                return false;
            }
            return true;

        // Port forwarding — separate permission
        case MessageType::PORT_FORWARD:
            return perms.restart; // use restart as proxy for admin

        // Service subscription — always allowed
        case MessageType::SUBSCRIBE_SERVICE:
        case MessageType::UNSUBSCRIBE_SERVICE:
            return true;

        // Chat and whiteboard
        case MessageType::CHAT_MESSAGE:
        case MessageType::WHITEBOARD:
            return true;

        // Misc
        case MessageType::MISC:
            return true;

        default:
            spdlog::debug("PermissionEnforcer: unknown message type {}",
                static_cast<uint32_t>(type));
            return true;
        }
    }

    /// Check if a service is allowed for a connection.
    [[nodiscard]] bool is_service_allowed(
        const std::string& service_name,
        const ConnectionConfig& config) const
    {
        return config.is_service_allowed(service_name);
    }
};

// ============================================================================
// ServiceNegotiator — manages service subscriptions
// ============================================================================
class ServiceNegotiator {
public:
    explicit ServiceNegotiator(ServerWeakPtr server)
        : server_(std::move(server)) {}

    /// Subscribe to a service.
    [[nodiscard]] bool subscribe(
        int32_t conn_id,
        const std::string& service_name,
        const std::map<std::string, std::string>& options = {})
    {
        std::lock_guard lk(mutex_);

        // Check if already subscribed
        auto it = subscriptions_.find(service_name);
        if (it != subscriptions_.end() && it->second.active) {
            spdlog::debug("ServiceNegotiator: already subscribed to {} (conn {})",
                service_name, conn_id);
            return true;
        }

        // Create subscription
        ServiceSubscription sub;
        sub.service_name = service_name;
        sub.subscribed_at_ms = now_ms();
        sub.active = true;
        sub.options = options;
        subscriptions_[service_name] = sub;

        // Notify server
        if (auto srv = server_.lock()) {
            srv->subscribe(service_name, srv->get_connection(conn_id), true);
        } else {
            spdlog::warn("ServiceNegotiator: server not available for subscribe");
            return false;
        }

        spdlog::info("ServiceNegotiator: conn {} subscribed to {}",
            conn_id, service_name);
        return true;
    }

    /// Unsubscribe from a service.
    [[nodiscard]] bool unsubscribe(
        int32_t conn_id,
        const std::string& service_name)
    {
        std::lock_guard lk(mutex_);

        auto it = subscriptions_.find(service_name);
        if (it == subscriptions_.end()) {
            spdlog::debug("ServiceNegotiator: not subscribed to {} (conn {})",
                service_name, conn_id);
            return false;
        }

        it->second.active = false;

        // Notify server
        if (auto srv = server_.lock()) {
            srv->subscribe(service_name, srv->get_connection(conn_id), false);
        }

        subscriptions_.erase(it);
        spdlog::info("ServiceNegotiator: conn {} unsubscribed from {}",
            conn_id, service_name);
        return true;
    }

    /// Unsubscribe from all services.
    void unsubscribe_all(int32_t conn_id) {
        std::lock_guard lk(mutex_);
        auto srv = server_.lock();
        for (auto& [name, sub] : subscriptions_) {
            sub.active = false;
            if (srv) {
                srv->subscribe(name, srv->get_connection(conn_id), false);
            }
        }
        subscriptions_.clear();
        spdlog::info("ServiceNegotiator: conn {} unsubscribed from all services",
            conn_id);
    }

    /// Check if subscribed to a service.
    [[nodiscard]] bool is_subscribed(const std::string& service_name) const {
        std::lock_guard lk(mutex_);
        auto it = subscriptions_.find(service_name);
        return it != subscriptions_.end() && it->second.active;
    }

    /// Get all current subscriptions.
    [[nodiscard]] std::vector<std::string> active_subscriptions() const {
        std::lock_guard lk(mutex_);
        std::vector<std::string> result;
        for (auto& [name, sub] : subscriptions_) {
            if (sub.active) result.push_back(name);
        }
        return result;
    }

    /// Record a message sent on a service.
    void record_message_sent(const std::string& service_name) {
        std::lock_guard lk(mutex_);
        auto it = subscriptions_.find(service_name);
        if (it != subscriptions_.end()) {
            it->second.messages_sent++;
        }
    }

    /// Record a message received on a service.
    void record_message_recv(const std::string& service_name) {
        std::lock_guard lk(mutex_);
        auto it = subscriptions_.find(service_name);
        if (it != subscriptions_.end()) {
            it->second.messages_recv++;
        }
    }

    [[nodiscard]] size_t subscription_count() const {
        std::lock_guard lk(mutex_);
        return subscriptions_.size();
    }

    [[nodiscard]] std::map<std::string, ServiceSubscription>
    get_subscriptions() const {
        std::lock_guard lk(mutex_);
        return subscriptions_;
    }

private:
    ServerWeakPtr server_;
    mutable std::mutex mutex_;
    std::map<std::string, ServiceSubscription> subscriptions_;

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// ============================================================================
// MessageRouter — routes messages to appropriate service handlers
// ============================================================================
class MessageRouter {
public:
    using ServiceHandler = std::function<void(
        int32_t conn_id, MessageType type,
        const std::vector<uint8_t>& data)>;

    using ControlHandlerFn = std::function<void(
        int32_t conn_id, ControlAction action,
        const nlohmann::json& params)>;

    MessageRouter() {
        register_default_routes();
    }

    /// Register a handler for a specific service category.
    void register_service_handler(
        ServiceCategory category,
        ServiceHandler handler)
    {
        std::lock_guard lk(mutex_);
        service_handlers_[category] = std::move(handler);
    }

    /// Register a handler for a specific message type.
    void register_message_handler(
        MessageType type,
        ServiceHandler handler)
    {
        std::lock_guard lk(mutex_);
        message_handlers_[type] = std::move(handler);
    }

    /// Register a control action handler.
    void register_control_handler(ControlHandlerFn handler) {
        std::lock_guard lk(mutex_);
        control_handler_ = std::move(handler);
    }

    /// Route a message to the appropriate handler.
    [[nodiscard]] bool route(
        int32_t conn_id,
        MessageType type,
        const std::vector<uint8_t>& data,
        const ControlPermissions& perms,
        bool view_only)
    {
        // Check permissions first
        if (!permission_enforcer_.is_allowed(type, perms, view_only)) {
            spdlog::warn("MessageRouter: conn {} blocked from sending {} "
                "(view_only={}, perms.keyboard={})",
                conn_id, static_cast<uint32_t>(type), view_only, perms.keyboard);
            return false;
        }

        // Try exact message handler first
        {
            std::lock_guard lk(mutex_);
            auto msg_it = message_handlers_.find(type);
            if (msg_it != message_handlers_.end() && msg_it->second) {
                msg_it->second(conn_id, type, data);
                return true;
            }
        }

        // Fall back to service category routing
        ServiceCategory category = classify_message(type);

        std::lock_guard lk(mutex_);
        auto svc_it = service_handlers_.find(category);
        if (svc_it != service_handlers_.end() && svc_it->second) {
            svc_it->second(conn_id, type, data);
            return true;
        }

        // Check if it's a control message
        if (category == ServiceCategory::CONTROL) {
            auto action = classify_control_action(type, data);
            if (action != ControlAction::NONE && control_handler_) {
                auto j = MessageFramer::decode_json_payload(data);
                control_handler_(conn_id, action, j);
                return true;
            }
        }

        spdlog::debug("MessageRouter: no handler for message type {} (conn {})",
            static_cast<uint32_t>(type), conn_id);
        return false;
    }

    /// Classify a message type into a service category.
    [[nodiscard]] static ServiceCategory classify_message(MessageType type) {
        switch (type) {
        case MessageType::VIDEO_FRAME:
        case MessageType::VIDEO_CODEC_CHANGE:
        case MessageType::VIDEO_QUALITY_CHANGE:
            return ServiceCategory::VIDEO;

        case MessageType::AUDIO_FRAME:
        case MessageType::AUDIO_CONFIG:
            return ServiceCategory::AUDIO;

        case MessageType::MOUSE_EVENT:
        case MessageType::KEY_EVENT:
        case MessageType::CURSOR_DATA:
        case MessageType::CURSOR_POSITION:
        case MessageType::CURSOR_SHAPE:
            return ServiceCategory::INPUT;

        case MessageType::CLIPBOARD_TEXT:
        case MessageType::CLIPBOARD_FILE:
        case MessageType::CLIPBOARD_IMAGE:
            return ServiceCategory::CLIPBOARD;

        case MessageType::FILE_TRANSFER_REQUEST:
        case MessageType::FILE_TRANSFER_RESPONSE:
        case MessageType::FILE_CHUNK:
        case MessageType::FILE_DONE:
        case MessageType::FILE_DIR:
            return ServiceCategory::FILE;

        case MessageType::SWITCH_DISPLAY:
        case MessageType::SWITCH_PERMISSION:
        case MessageType::CLOSE_CONNECTION:
        case MessageType::PRIVACY_MODE:
        case MessageType::PORT_FORWARD:
        case MessageType::CHAT_MESSAGE:
        case MessageType::WHITEBOARD:
        case MessageType::MISC:
            return ServiceCategory::CONTROL;

        case MessageType::SUBSCRIBE_SERVICE:
        case MessageType::UNSUBSCRIBE_SERVICE:
        case MessageType::SERVICE_DATA:
            return ServiceCategory::CONTROL;

        default:
            return ServiceCategory::UNKNOWN;
        }
    }

    /// Classify a control action from message type and data.
    [[nodiscard]] static ControlAction classify_control_action(
        MessageType type,
        const std::vector<uint8_t>& data)
    {
        switch (type) {
        case MessageType::SWITCH_DISPLAY:   return ControlAction::SWITCH_DISPLAY;
        case MessageType::SWITCH_PERMISSION: return ControlAction::SWITCH_PERMISSION;
        case MessageType::CLOSE_CONNECTION:  return ControlAction::NONE;
        case MessageType::PRIVACY_MODE:      return ControlAction::PRIVACY_MODE;
        case MessageType::PORT_FORWARD:      return ControlAction::PORT_FORWARD;
        case MessageType::CHAT_MESSAGE:      return ControlAction::CHAT_MESSAGE;
        case MessageType::WHITEBOARD:        return ControlAction::WHITEBOARD_ACTION;
        default:                             return ControlAction::NONE;
        }
    }

private:
    mutable std::mutex mutex_;
    PermissionEnforcer permission_enforcer_;
    std::map<ServiceCategory, ServiceHandler> service_handlers_;
    std::map<MessageType, ServiceHandler> message_handlers_;
    ControlHandlerFn control_handler_;

    void register_default_routes() {
        // Default service handlers are registered with no-op lambdas
        // They will be overridden when actual services are connected
        for (int i = 0; i <= 7; ++i) {
            service_handlers_[static_cast<ServiceCategory>(i)] =
                [](int32_t, MessageType, const std::vector<uint8_t>&) {};
        }
    }
};

// ============================================================================
// IdleDetector — monitors connection activity for idle timeout
// ============================================================================
class IdleDetector {
public:
    explicit IdleDetector(int64_t idle_timeout_ms)
        : idle_timeout_ms_(idle_timeout_ms) {}

    /// Record activity on the connection.
    void record_activity() {
        last_activity_ms_.store(now_ms(), std::memory_order_release);
        idle_notified_.store(false, std::memory_order_release);
    }

    /// Check if the connection has been idle beyond the timeout.
    [[nodiscard]] bool is_idle() const {
        int64_t last = last_activity_ms_.load(std::memory_order_acquire);
        if (last == 0) return false;
        return (now_ms() - last) > idle_timeout_ms_;
    }

    /// Get idle duration in milliseconds.
    [[nodiscard]] int64_t idle_duration_ms() const {
        int64_t last = last_activity_ms_.load(std::memory_order_acquire);
        if (last == 0) return 0;
        return now_ms() - last;
    }

    /// Check if we should send an idle warning (at 80% of timeout).
    [[nodiscard]] bool should_warn() const {
        int64_t last = last_activity_ms_.load(std::memory_order_acquire);
        if (last == 0) return false;
        int64_t idle = now_ms() - last;
        return idle > (idle_timeout_ms_ * 80 / 100) && !idle_notified_.load();
    }

    /// Mark that an idle warning has been sent.
    void mark_warned() {
        idle_notified_.store(true, std::memory_order_release);
    }

    /// Set a custom idle timeout.
    void set_timeout(int64_t timeout_ms) {
        idle_timeout_ms_ = timeout_ms;
    }

    [[nodiscard]] int64_t timeout_ms() const { return idle_timeout_ms_; }

    void reset() {
        last_activity_ms_.store(now_ms(), std::memory_order_release);
        idle_notified_.store(false, std::memory_order_release);
    }

private:
    int64_t idle_timeout_ms_;
    std::atomic<int64_t> last_activity_ms_{0};
    std::atomic<bool> idle_notified_{false};

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// ============================================================================
// ConnectionHealthMonitor — monitors connection health metrics
// ============================================================================
class ConnectionHealthMonitor {
public:
    explicit ConnectionHealthMonitor(int64_t heartbeat_timeout_ms)
        : heartbeat_timeout_ms_(heartbeat_timeout_ms) {}

    /// Record a heartbeat received.
    void record_heartbeat() {
        last_heartbeat_ms_.store(now_ms(), std::memory_order_release);
        missed_heartbeats_.store(0, std::memory_order_release);
        stalled_.store(false, std::memory_order_release);
    }

    /// Check if heartbeat has been lost.
    [[nodiscard]] bool is_heartbeat_lost() const {
        int64_t last = last_heartbeat_ms_.load(std::memory_order_acquire);
        if (last == 0) return false; // No heartbeats received yet
        return (now_ms() - last) > heartbeat_timeout_ms_;
    }

    /// Record a missed heartbeat check.
    void record_missed_heartbeat() {
        int32_t missed = missed_heartbeats_.fetch_add(1) + 1;
        if (missed >= 3) {
            stalled_.store(true, std::memory_order_release);
            spdlog::warn("ConnectionHealthMonitor: connection stalled "
                "({} missed heartbeats)", missed);
        }
    }

    /// Record latency measurement.
    void record_latency(double latency_ms) {
        auto& metric = metrics_;
        metric.avg_latency_ms = metric.avg_latency_ms * 0.9 + latency_ms * 0.1;
        metric.last_health_check_ms = now_ms();
    }

    /// Record packet loss measurement.
    void record_packet_loss(double loss_pct) {
        metrics_.packet_loss = loss_pct;
    }

    [[nodiscard]] bool is_healthy() const {
        return !is_heartbeat_lost() && !stalled_.load();
    }

    [[nodiscard]] const HealthMetric& metrics() const { return metrics_; }

    void reset() {
        last_heartbeat_ms_.store(now_ms(), std::memory_order_release);
        missed_heartbeats_.store(0, std::memory_order_release);
        stalled_.store(false, std::memory_order_release);
        metrics_ = HealthMetric{};
    }

private:
    int64_t heartbeat_timeout_ms_;
    std::atomic<int64_t> last_heartbeat_ms_{0};
    std::atomic<int32_t> missed_heartbeats_{0};
    std::atomic<bool> stalled_{false};
    HealthMetric metrics_;

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// ============================================================================
// ControlHandler — processes control messages and actions
// ============================================================================
class ControlHandler {
public:
    ControlHandler() = default;

    /// Handle a control action.
    [[nodiscard]] ControlResult handle_action(
        ControlAction action,
        const nlohmann::json& params,
        AuthState& auth_state,
        ConnectionStats& stats,
        ServiceNegotiator& negotiator,
        int32_t conn_id)
    {
        ControlResult result;
        result.success = true;
        result.action = action;

        switch (action) {
        case ControlAction::SWITCH_DISPLAY:
            result = handle_switch_display(params, auth_state, conn_id);
            break;

        case ControlAction::SWITCH_PERMISSION:
            result = handle_switch_permission(params, auth_state, conn_id);
            break;

        case ControlAction::PRIVACY_MODE:
            result = handle_privacy_mode(params, auth_state, conn_id);
            break;

        case ControlAction::REFRESH_VIDEO:
            result = handle_refresh_video(params, conn_id);
            break;

        case ControlAction::CHANGE_QUALITY:
            result = handle_change_quality(params, conn_id);
            break;

        case ControlAction::REQUEST_KEYFRAME:
            result = handle_request_keyframe(params, conn_id);
            break;

        case ControlAction::MUTE_AUDIO:
            result = handle_mute_audio(params, auth_state, conn_id);
            break;

        case ControlAction::CHAT_MESSAGE:
            result = handle_chat_message(params, conn_id);
            break;

        case ControlAction::WHITEBOARD_ACTION:
            result = handle_whiteboard_action(params, conn_id);
            break;

        case ControlAction::PORT_FORWARD:
            result = handle_port_forward(params, auth_state, conn_id);
            break;

        case ControlAction::RESTART_SERVICE:
            result = handle_restart_service(params, auth_state, conn_id);
            break;

        case ControlAction::SHUTDOWN:
            result = handle_shutdown(params, auth_state, conn_id);
            break;

        case ControlAction::LOCK_SCREEN:
            result = handle_lock_screen(params, conn_id);
            break;

        case ControlAction::LOGOUT:
            result = handle_logout(params, conn_id);
            break;

        case ControlAction::RECONNECT:
            result = handle_reconnect(params, conn_id);
            break;

        default:
            result.success = false;
            result.message = "Unknown control action: "
                + std::to_string(static_cast<int>(action));
            spdlog::warn("ControlHandler: unknown action {} from conn {}",
                static_cast<int>(action), conn_id);
            break;
        }

        return result;
    }

    /// Build a control response message.
    [[nodiscard]] std::vector<uint8_t> build_response(
        const ControlResult& result) const
    {
        nlohmann::json j;
        j["success"] = result.success;
        j["action"] = static_cast<int>(result.action);
        j["message"] = result.message;
        if (!result.data.is_null()) {
            j["data"] = result.data;
        }

        std::string json_str = j.dump();
        return std::vector<uint8_t>(json_str.begin(), json_str.end());
    }

    struct ControlResult {
        bool success = false;
        ControlAction action = ControlAction::NONE;
        std::string message;
        nlohmann::json data;
    };

private:
    ControlResult handle_switch_display(
        const nlohmann::json& params,
        AuthState& auth_state,
        int32_t conn_id)
    {
        ControlResult result;
        result.action = ControlAction::SWITCH_DISPLAY;

        int32_t display_idx = params.value("display_index", 0);
        spdlog::info("ControlHandler: conn {} switching to display {}",
            conn_id, display_idx);

        result.success = true;
        result.data["display_index"] = display_idx;
        result.message = "Display switched to " + std::to_string(display_idx);
        return result;
    }

    ControlResult handle_switch_permission(
        const nlohmann::json& params,
        AuthState& auth_state,
        int32_t conn_id)
    {
        ControlResult result;
        result.action = ControlAction::SWITCH_PERMISSION;

        ControlPermissions new_perms = auth_state.permissions;
        if (params.contains("keyboard")) {
            new_perms.keyboard = params["keyboard"].get<bool>();
        }
        if (params.contains("clipboard")) {
            new_perms.clipboard = params["clipboard"].get<bool>();
        }
        if (params.contains("file_transfer")) {
            new_perms.file_transfer = params["file_transfer"].get<bool>();
        }
        if (params.contains("audio")) {
            new_perms.audio = params["audio"].get<bool>();
        }
        if (params.contains("privacy_mode")) {
            new_perms.privacy_mode = params["privacy_mode"].get<bool>();
        }
        if (params.contains("restart")) {
            new_perms.restart = params["restart"].get<bool>();
        }
        if (params.contains("shutdown")) {
            new_perms.shutdown = params["shutdown"].get<bool>();
        }

        auth_state.permissions = new_perms;
        auth_state.view_only = !new_perms.keyboard;

        result.success = true;
        result.data["permissions"] = params;
        result.message = "Permissions updated";
        spdlog::info("ControlHandler: conn {} permissions updated", conn_id);
        return result;
    }

    ControlResult handle_privacy_mode(
        const nlohmann::json& params,
        AuthState& auth_state,
        int32_t conn_id)
    {
        ControlResult result;
        result.action = ControlAction::PRIVACY_MODE;

        bool enabled = params.value("enabled", false);
        if (!auth_state.permissions.privacy_mode) {
            result.success = false;
            result.message = "Privacy mode not permitted";
            return result;
        }

        spdlog::info("ControlHandler: conn {} privacy mode {}",
            conn_id, enabled ? "ON" : "OFF");
        result.success = true;
        result.data["privacy_mode"] = enabled;
        result.message = enabled ? "Privacy mode enabled" : "Privacy mode disabled";
        return result;
    }

    ControlResult handle_refresh_video(
        const nlohmann::json& params, int32_t conn_id)
    {
        ControlResult result;
        result.action = ControlAction::REFRESH_VIDEO;
        result.success = true;
        result.message = "Video refresh requested";
        spdlog::debug("ControlHandler: conn {} video refresh", conn_id);
        return result;
    }

    ControlResult handle_change_quality(
        const nlohmann::json& params, int32_t conn_id)
    {
        ControlResult result;
        result.action = ControlAction::CHANGE_QUALITY;

        int32_t quality = params.value("quality", 75);
        quality = std::clamp(quality, 1, 100);

        result.success = true;
        result.data["quality"] = quality;
        result.message = "Quality set to " + std::to_string(quality);
        spdlog::info("ControlHandler: conn {} quality changed to {}", conn_id, quality);
        return result;
    }

    ControlResult handle_request_keyframe(
        const nlohmann::json& params, int32_t conn_id)
    {
        ControlResult result;
        result.action = ControlAction::REQUEST_KEYFRAME;
        result.success = true;
        result.message = "Keyframe requested";
        spdlog::debug("ControlHandler: conn {} keyframe requested", conn_id);
        return result;
    }

    ControlResult handle_mute_audio(
        const nlohmann::json& params,
        AuthState& auth_state,
        int32_t conn_id)
    {
        ControlResult result;
        result.action = ControlAction::MUTE_AUDIO;

        bool muted = params.value("muted", false);
        result.success = true;
        result.data["audio_muted"] = muted;
        result.message = muted ? "Audio muted" : "Audio unmuted";
        spdlog::info("ControlHandler: conn {} audio {}", conn_id,
            muted ? "muted" : "unmuted");
        return result;
    }

    ControlResult handle_chat_message(
        const nlohmann::json& params, int32_t conn_id)
    {
        ControlResult result;
        result.action = ControlAction::CHAT_MESSAGE;

        std::string text = params.value("text", "");
        result.success = true;
        result.data["text"] = text;
        result.data["sender"] = conn_id;
        result.data["timestamp"] = now_ms();
        spdlog::debug("ControlHandler: conn {} chat: {}", conn_id,
            text.substr(0, 100));
        return result;
    }

    ControlResult handle_whiteboard_action(
        const nlohmann::json& params, int32_t conn_id)
    {
        ControlResult result;
        result.action = ControlAction::WHITEBOARD_ACTION;
        result.success = true;
        result.data = params;
        spdlog::debug("ControlHandler: conn {} whiteboard action", conn_id);
        return result;
    }

    ControlResult handle_port_forward(
        const nlohmann::json& params,
        AuthState& auth_state,
        int32_t conn_id)
    {
        ControlResult result;
        result.action = ControlAction::PORT_FORWARD;

        if (!auth_state.permissions.restart) {
            result.success = false;
            result.message = "Port forwarding not permitted";
            return result;
        }

        result.success = true;
        result.data = params;
        result.message = "Port forwarding configured";
        spdlog::info("ControlHandler: conn {} port forwarding", conn_id);
        return result;
    }

    ControlResult handle_restart_service(
        const nlohmann::json& params,
        AuthState& auth_state,
        int32_t conn_id)
    {
        ControlResult result;
        result.action = ControlAction::RESTART_SERVICE;

        if (!auth_state.permissions.restart) {
            result.success = false;
            result.message = "Service restart not permitted";
            return result;
        }

        result.success = true;
        result.message = "Service restart requested";
        spdlog::info("ControlHandler: conn {} requested service restart", conn_id);
        return result;
    }

    ControlResult handle_shutdown(
        const nlohmann::json& params,
        AuthState& auth_state,
        int32_t conn_id)
    {
        ControlResult result;
        result.action = ControlAction::SHUTDOWN;

        if (!auth_state.permissions.shutdown) {
            result.success = false;
            result.message = "Shutdown not permitted";
            return result;
        }

        result.success = true;
        result.message = "Shutdown requested";
        spdlog::warn("ControlHandler: conn {} requested shutdown", conn_id);
        return result;
    }

    ControlResult handle_lock_screen(
        const nlohmann::json& params, int32_t conn_id)
    {
        ControlResult result;
        result.action = ControlAction::LOCK_SCREEN;
        result.success = true;
        result.message = "Screen lock requested";
        spdlog::info("ControlHandler: conn {} lock screen", conn_id);
        return result;
    }

    ControlResult handle_logout(
        const nlohmann::json& params, int32_t conn_id)
    {
        ControlResult result;
        result.action = ControlAction::LOGOUT;
        result.success = true;
        result.message = "Logout requested";
        spdlog::info("ControlHandler: conn {} logout", conn_id);
        return result;
    }

    ControlResult handle_reconnect(
        const nlohmann::json& params, int32_t conn_id)
    {
        ControlResult result;
        result.action = ControlAction::RECONNECT;
        result.success = true;
        result.message = "Reconnect requested";
        spdlog::info("ControlHandler: conn {} reconnect", conn_id);
        return result;
    }

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// ============================================================================
// ErrorHandler — classifies and handles connection errors
// ============================================================================
class ErrorHandler {
public:
    /// Classify an error and determine appropriate disconnect reason.
    [[nodiscard]] static DisconnectReason classify_error(
        int error_code,
        const std::string& context)
    {
        if (error_code == 0) return DisconnectReason::NORMAL;

        switch (error_code) {
#if defined(CPPDESK_PLATFORM_WINDOWS)
        case WSAECONNRESET:
        case WSAECONNABORTED:
        case WSAENETRESET:
        case WSAENOTCONN:
            return DisconnectReason::NETWORK_ERROR;
        case WSAETIMEDOUT:
            return DisconnectReason::TIMEOUT;
        case WSAECONNREFUSED:
            return DisconnectReason::NETWORK_ERROR;
        case WSAEHOSTUNREACH:
            return DisconnectReason::NETWORK_ERROR;
#else
        case ECONNRESET:
        case ECONNABORTED:
        case ENOTCONN:
        case EPIPE:
            return DisconnectReason::NETWORK_ERROR;
        case ETIMEDOUT:
            return DisconnectReason::TIMEOUT;
        case ECONNREFUSED:
            return DisconnectReason::NETWORK_ERROR;
        case EHOSTUNREACH:
            return DisconnectReason::NETWORK_ERROR;
#endif
        default:
            break;
        }

        spdlog::debug("ErrorHandler: unclassified error {} in {}",
            error_code, context);
        return DisconnectReason::INTERNAL_ERROR;
    }

    /// Get a human-readable message for a disconnect reason.
    [[nodiscard]] static std::string reason_message(DisconnectReason reason) {
        switch (reason) {
        case DisconnectReason::NORMAL:            return "Normal disconnect";
        case DisconnectReason::TIMEOUT:           return "Connection timed out";
        case DisconnectReason::AUTH_FAILED:       return "Authentication failed";
        case DisconnectReason::PROTOCOL_ERROR:    return "Protocol error";
        case DisconnectReason::NETWORK_ERROR:     return "Network error";
        case DisconnectReason::SERVER_SHUTDOWN:   return "Server shutting down";
        case DisconnectReason::CLIENT_REQUEST:    return "Client requested disconnect";
        case DisconnectReason::KICKED:            return "Kicked by server";
        case DisconnectReason::BANNED:            return "Address banned";
        case DisconnectReason::RATE_LIMITED:      return "Rate limited";
        case DisconnectReason::INTERNAL_ERROR:    return "Internal server error";
        case DisconnectReason::IDLE_TIMEOUT:      return "Idle timeout";
        case DisconnectReason::HEARTBEAT_LOST:    return "Heartbeat lost";
        case DisconnectReason::PERMISSION_DENIED:  return "Permission denied";
        default:                                  return "Unknown reason";
        }
    }

    /// Create a disconnect notification message.
    [[nodiscard]] static std::vector<uint8_t> create_disconnect_message(
        DisconnectReason reason,
        const std::string& extra = "")
    {
        nlohmann::json j;
        j["type"] = "disconnect";
        j["reason"] = static_cast<int>(reason);
        j["message"] = reason_message(reason);
        if (!extra.empty()) {
            j["detail"] = extra;
        }

        std::string json_str = j.dump();
        return std::vector<uint8_t>(json_str.begin(), json_str.end());
    }
};

// ============================================================================
// ConnectionStatistics — manages per-connection statistics collection
// ============================================================================
class ConnectionStatisticsCollector {
public:
    ConnectionStatisticsCollector() = default;

    /// Start statistics collection for a connection.
    void start(int32_t conn_id, const std::string& remote_addr) {
        std::lock_guard lk(mutex_);
        ConnectionStats stats;
        stats.connection_id = conn_id;
        stats.remote_addr = remote_addr;
        stats.connected_at_ms = now_ms();
        stats.state = ConnectionState::INIT;
        stats_ = stats;
    }

    /// Record a state transition.
    void record_state_change(ConnectionState new_state) {
        std::lock_guard lk(mutex_);
        stats_.state = new_state;
        if (new_state == ConnectionState::AUTHENTICATING) {
            stats_.authenticated_at_ms = now_ms();
        }
    }

    /// Record data sent.
    void record_send(uint64_t bytes) {
        std::lock_guard lk(mutex_);
        stats_.record_send(bytes, now_ms());
    }

    /// Record data received.
    void record_recv(uint64_t bytes) {
        std::lock_guard lk(mutex_);
        stats_.record_recv(bytes, now_ms());
    }

    /// Record an error.
    void record_error(const std::string& type) {
        std::lock_guard lk(mutex_);
        if (type == "send") stats_.send_errors++;
        else if (type == "recv") stats_.recv_errors++;
        else if (type == "protocol") stats_.protocol_errors++;
    }

    /// Record a timeout event.
    void record_timeout() {
        std::lock_guard lk(mutex_);
        stats_.timeout_events++;
    }

    /// Record a service message.
    void record_service_message(const std::string& service, bool sent) {
        std::lock_guard lk(mutex_);
        if (sent) {
            stats_.service_messages_sent[service]++;
        } else {
            stats_.service_messages_recv[service]++;
        }
    }

    /// Record latency measurement.
    void record_latency(double latency_ms) {
        std::lock_guard lk(mutex_);
        stats_.avg_latency_ms = stats_.avg_latency_ms * 0.9 + latency_ms * 0.1;
        stats_.max_latency_ms = std::max(stats_.max_latency_ms, latency_ms);
        stats_.min_latency_ms = std::min(stats_.min_latency_ms, latency_ms);
    }

    /// Record packet loss.
    void record_packet_loss(double loss_pct) {
        std::lock_guard lk(mutex_);
        stats_.packet_loss_pct = loss_pct;
    }

    /// Update subscription count.
    void update_subscription_count(uint32_t count) {
        std::lock_guard lk(mutex_);
        stats_.active_subscriptions = count;
    }

    /// Update idle duration.
    void update_idle_duration(int64_t idle_ms) {
        std::lock_guard lk(mutex_);
        stats_.idle_duration_ms = idle_ms;
    }

    /// Set disconnect reason.
    void set_disconnect_reason(DisconnectReason reason) {
        std::lock_guard lk(mutex_);
        stats_.disconnect_reason = reason;
    }

    /// Get current stats snapshot.
    [[nodiscard]] ConnectionStats snapshot() const {
        std::lock_guard lk(mutex_);
        return stats_;
    }

    /// Get stats as JSON for reporting.
    [[nodiscard]] nlohmann::json to_json() const {
        std::lock_guard lk(mutex_);
        nlohmann::json j;
        j["connection_id"]     = stats_.connection_id;
        j["remote_addr"]       = stats_.remote_addr;
        j["state"]             = static_cast<int>(stats_.state);
        j["connected_at_ms"]   = stats_.connected_at_ms;
        j["idle_duration_ms"]  = stats_.idle_duration_ms;
        j["bytes_sent"]        = stats_.bytes_sent;
        j["bytes_received"]    = stats_.bytes_received;
        j["messages_sent"]     = stats_.messages_sent;
        j["messages_received"] = stats_.messages_received;
        j["send_bps"]          = stats_.current_send_bps;
        j["recv_bps"]          = stats_.current_recv_bps;
        j["peak_send_bps"]     = stats_.peak_send_bps;
        j["peak_recv_bps"]     = stats_.peak_recv_bps;
        j["send_errors"]       = stats_.send_errors;
        j["recv_errors"]       = stats_.recv_errors;
        j["protocol_errors"]   = stats_.protocol_errors;
        j["timeout_events"]    = stats_.timeout_events;
        j["active_subscriptions"] = stats_.active_subscriptions;
        j["avg_latency_ms"]    = stats_.avg_latency_ms;
        j["packet_loss_pct"]   = stats_.packet_loss_pct;
        j["disconnect_reason"] = static_cast<int>(stats_.disconnect_reason);
        return j;
    }

    void reset() {
        std::lock_guard lk(mutex_);
        stats_ = ConnectionStats{};
    }

private:
    mutable std::mutex mutex_;
    ConnectionStats stats_;

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// ============================================================================
// ConnectionPool — manages concurrent connection limits and lifecycle
// ============================================================================
class ConnectionPool {
public:
    explicit ConnectionPool(const ConnectionConfig& config)
        : config_(config) {}

    /// Try to allocate a slot for a new connection. Returns false if limit reached.
    [[nodiscard]] bool try_allocate(int32_t conn_id, const std::string& remote_addr) {
        std::lock_guard lk(mutex_);

        // Global connection limit
        if (static_cast<int32_t>(connections_.size()) >= config_.max_connections) {
            spdlog::warn("ConnectionPool: max connections reached ({})",
                config_.max_connections);
            return false;
        }

        // Per-IP limit
        int32_t ip_count = 0;
        for (auto& [id, addr] : connections_) {
            if (addr == remote_addr) ip_count++;
        }
        if (ip_count >= config_.max_connections_per_ip) {
            spdlog::warn("ConnectionPool: max connections per IP reached "
                "({} for {})", config_.max_connections_per_ip, remote_addr);
            return false;
        }

        connections_[conn_id] = remote_addr;
        spdlog::info("ConnectionPool: allocated slot for conn {} ({}) "
            "(total: {})", conn_id, remote_addr, connections_.size());
        return true;
    }

    /// Release a connection slot.
    void release(int32_t conn_id) {
        std::lock_guard lk(mutex_);
        auto it = connections_.find(conn_id);
        if (it != connections_.end()) {
            spdlog::info("ConnectionPool: released slot for conn {} ({}) "
                "(total: {})", conn_id, it->second, connections_.size() - 1);
            connections_.erase(it);
        }
    }

    /// Get the address for a connection.
    [[nodiscard]] std::string get_address(int32_t conn_id) const {
        std::lock_guard lk(mutex_);
        auto it = connections_.find(conn_id);
        return it != connections_.end() ? it->second : "";
    }

    /// Get current connection count.
    [[nodiscard]] size_t size() const {
        std::lock_guard lk(mutex_);
        return connections_.size();
    }

    /// Get all active connection IDs.
    [[nodiscard]] std::vector<int32_t> active_connections() const {
        std::lock_guard lk(mutex_);
        std::vector<int32_t> ids;
        ids.reserve(connections_.size());
        for (auto& [id, _] : connections_) ids.push_back(id);
        return ids;
    }

    /// Check if a connection exists in the pool.
    [[nodiscard]] bool contains(int32_t conn_id) const {
        std::lock_guard lk(mutex_);
        return connections_.count(conn_id) > 0;
    }

    /// Get connections per IP.
    [[nodiscard]] std::map<std::string, int32_t> per_ip_counts() const {
        std::lock_guard lk(mutex_);
        std::map<std::string, int32_t> counts;
        for (auto& [id, addr] : connections_) {
            counts[addr]++;
        }
        return counts;
    }

    /// Clear all connections (server shutdown).
    void clear() {
        std::lock_guard lk(mutex_);
        connections_.clear();
        spdlog::info("ConnectionPool: all connections cleared");
    }

private:
    ConnectionConfig config_;
    mutable std::mutex mutex_;
    std::map<int32_t, std::string> connections_;
};

// ============================================================================
// QueuedMessageBuffer — thread-safe message queue for outgoing data
// ============================================================================
class QueuedMessageBuffer {
public:
    explicit QueuedMessageBuffer(size_t max_size = conn_constants::MESSAGE_QUEUE_MAX_SIZE)
        : max_size_(max_size) {}

    /// Enqueue a message. Returns false if queue is full.
    [[nodiscard]] bool enqueue(PendingMessage msg) {
        std::lock_guard lk(mutex_);
        if (queue_.size() >= max_size_) {
            spdlog::warn("QueuedMessageBuffer: queue full, dropping message type={}",
                static_cast<uint32_t>(msg.type));
            dropped_count_++;
            return false;
        }
        msg.queued_at_ms = now_ms();
        queue_.push_back(std::move(msg));
        return true;
    }

    /// Enqueue with priority (higher priority = inserted earlier).
    [[nodiscard]] bool enqueue_priority(PendingMessage msg) {
        std::lock_guard lk(mutex_);
        if (queue_.size() >= max_size_) {
            dropped_count_++;
            return false;
        }
        msg.queued_at_ms = now_ms();

        // Insert sorted by priority
        auto it = std::lower_bound(queue_.begin(), queue_.end(), msg,
            [](const PendingMessage& a, const PendingMessage& b) {
                return a.priority < b.priority;
            });
        queue_.insert(it, std::move(msg));
        return true;
    }

    /// Dequeue the next message. Returns std::nullopt if queue is empty.
    [[nodiscard]] std::optional<PendingMessage> dequeue() {
        std::lock_guard lk(mutex_);
        if (queue_.empty()) return std::nullopt;
        auto msg = std::move(queue_.front());
        queue_.pop_front();
        return msg;
    }

    /// Peek at the next message without removing.
    [[nodiscard]] std::optional<const PendingMessage*> peek() const {
        std::lock_guard lk(mutex_);
        if (queue_.empty()) return std::nullopt;
        return &queue_.front();
    }

    /// Remove expired messages.
    void purge_expired(int64_t now) {
        std::lock_guard lk(mutex_);
        queue_.erase(
            std::remove_if(queue_.begin(), queue_.end(),
                [now](const PendingMessage& m) { return m.is_expired(now); }),
            queue_.end());
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard lk(mutex_);
        return queue_.size();
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard lk(mutex_);
        return queue_.empty();
    }

    [[nodiscard]] uint64_t dropped_count() const {
        return dropped_count_.load();
    }

    void clear() {
        std::lock_guard lk(mutex_);
        queue_.clear();
    }

private:
    size_t max_size_;
    mutable std::mutex mutex_;
    std::deque<PendingMessage> queue_;
    std::atomic<uint64_t> dropped_count_{0};

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// ============================================================================
// DataExchangeManager — manages bidirectional data flow
// ============================================================================
class DataExchangeManager {
public:
    DataExchangeManager(
        StreamPtr stream,
        ConnectionStatisticsCollector& stats,
        QueuedMessageBuffer& out_queue,
        IdleDetector& idle_detector)
        : stream_(std::move(stream))
        , stats_(stats)
        , out_queue_(out_queue)
        , idle_detector_(idle_detector)
    {
        recv_buffer_.reserve(conn_constants::RECV_BUFFER_SIZE);
    }

    /// Send data to the client.
    [[nodiscard]] bool send_data(
        const std::vector<uint8_t>& data,
        const HandshakeManager* handshake = nullptr)
    {
        if (!stream_ || !stream_->is_open()) {
            stats_.record_error("send");
            return false;
        }

        std::vector<uint8_t> to_send;
        if (handshake && handshake->is_encrypted()) {
            to_send = handshake->encrypt(data);
        } else {
            to_send = data;
        }

        try {
            bool ok = stream_->send(to_send);
            if (ok) {
                stats_.record_send(to_send.size());
                idle_detector_.record_activity();
            } else {
                stats_.record_error("send");
            }
            return ok;
        } catch (const std::exception& e) {
            spdlog::error("DataExchange: send exception: {}", e.what());
            stats_.record_error("send");
            return false;
        }
    }

    /// Send a framed message.
    [[nodiscard]] bool send_message(
        MessageType type,
        const std::vector<uint8_t>& payload,
        const HandshakeManager* handshake = nullptr)
    {
        auto frame = framer_.encode(type, payload, next_sequence_++);
        return send_data(frame, handshake);
    }

    /// Send a JSON message.
    [[nodiscard]] bool send_json(
        MessageType type,
        const nlohmann::json& j,
        const HandshakeManager* handshake = nullptr)
    {
        auto frame = framer_.encode_json(type, j, next_sequence_++);
        return send_data(frame, handshake);
    }

    /// Receive data from the client. Returns raw bytes.
    [[nodiscard]] bool recv_data(
        std::vector<uint8_t>& out,
        const HandshakeManager* handshake = nullptr)
    {
        if (!stream_ || !stream_->is_open()) {
            return false;
        }

        try {
            auto raw = stream_->recv();
            if (raw.empty()) {
                return false;
            }

            if (handshake && handshake->is_encrypted()) {
                out = handshake->decrypt(raw);
                if (out.empty()) {
                    spdlog::warn("DataExchange: decryption produced empty output");
                    stats_.record_error("recv");
                    return false;
                }
            } else {
                out = std::move(raw);
            }

            stats_.record_recv(out.size());
            idle_detector_.record_activity();
            return true;
        } catch (const std::exception& e) {
            spdlog::error("DataExchange: recv exception: {}", e.what());
            stats_.record_error("recv");
            return false;
        }
    }

    /// Receive and decode a framed message.
    [[nodiscard]] bool recv_message(
        MessageType& type,
        std::vector<uint8_t>& payload,
        const HandshakeManager* handshake = nullptr)
    {
        // Accumulate data until we have a full frame
        recv_buffer_.clear();
        size_t consumed = 0;

        while (true) {
            std::vector<uint8_t> chunk;
            if (!recv_data(chunk, handshake)) {
                return false;
            }

            recv_buffer_.insert(recv_buffer_.end(), chunk.begin(), chunk.end());

            uint16_t seq = 0, flags = 0;
            if (framer_.decode(recv_buffer_, consumed, type, payload, seq, flags)) {
                // If there's leftover data, keep it for next read
                if (consumed < recv_buffer_.size()) {
                    recv_buffer_.erase(
                        recv_buffer_.begin(),
                        recv_buffer_.begin() + consumed);
                } else {
                    recv_buffer_.clear();
                }
                return true;
            }

            // Check for oversized buffer
            if (recv_buffer_.size() > conn_constants::MAX_MESSAGE_SIZE + 1024) {
                spdlog::error("DataExchange: receive buffer overflow");
                stats_.record_error("protocol");
                return false;
            }
        }
    }

    /// Process the outgoing queue (send pending messages).
    [[nodiscard]] size_t process_outgoing_queue(
        const HandshakeManager* handshake = nullptr)
    {
        size_t sent = 0;
        int64_t now = now_ms();
        static constexpr size_t MAX_BATCH = 16;

        out_queue_.purge_expired(now);

        while (sent < MAX_BATCH) {
            auto msg = out_queue_.dequeue();
            if (!msg.has_value()) break;

            if (msg->is_expired(now)) continue;

            if (send_message(msg->type, msg->data, handshake)) {
                sent++;
                if (msg->requires_ack) {
                    // Track pending acknowledgements
                    pending_acks_[msg->sequence] = *msg;
                }
            } else {
                // Re-queue on failure (up to retry limit)
                if (msg->retry_count < 3) {
                    msg->retry_count++;
                    out_queue_.enqueue_priority(*msg);
                }
                break;
            }
        }

        return sent;
    }

    /// Flush all pending messages.
    void flush(const HandshakeManager* handshake = nullptr) {
        size_t flushed = 0;
        while (out_queue_.size() > 0) {
            flushed += process_outgoing_queue(handshake);
            if (flushed == 0) break;
        }
        spdlog::debug("DataExchange: flushed {} pending messages", flushed);
    }

    [[nodiscard]] bool is_open() const {
        return stream_ && stream_->is_open();
    }

    [[nodiscard]] std::string remote_addr() const {
        return stream_ ? stream_->remote_addr() : "";
    }

    void close() {
        if (stream_) {
            stream_->close();
        }
    }

private:
    StreamPtr stream_;
    ConnectionStatisticsCollector& stats_;
    QueuedMessageBuffer& out_queue_;
    IdleDetector& idle_detector_;
    MessageFramer framer_;
    std::vector<uint8_t> recv_buffer_;
    uint16_t next_sequence_{0};
    std::map<uint32_t, PendingMessage> pending_acks_;
    uint64_t next_ack_id_{0};

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// ============================================================================
// Connection class — FULL connection lifecycle implementation
// ============================================================================
// This is the core connection object. It manages the complete lifecycle:
//   accept → handshake → authenticate → negotiate services →
//   data exchange → close
// ============================================================================
class ConnectionImpl {
public:
    ConnectionImpl(
        int32_t id,
        StreamPtr stream,
        const std::string& addr,
        ServerWeakPtr server,
        ConnectionConfig config,
        std::optional<ControlPermissions> perms)
        : conn_id_(id)
        , stream_(std::move(stream))
        , remote_addr_(addr)
        , server_(std::move(server))
        , config_(std::move(config))
        , handshake_manager_()
        , authenticator_(config_)
        , service_negotiator_(server_)
        , idle_detector_(config_.idle_timeout_ms)
        , health_monitor_(config_.heartbeat_timeout_ms)
        , stats_collector_()
        , out_queue_()
        , data_exchange_(stream_, stats_collector_, out_queue_, idle_detector_)
    {
        stats_collector_.start(conn_id_, remote_addr_);

        if (perms.has_value()) {
            auth_state_.permissions = *perms;
            auth_state_.view_only = !perms->keyboard;
        }

        spdlog::info("ConnectionImpl: created conn#{} from {}", conn_id_, remote_addr_);
    }

    ~ConnectionImpl() {
        stop();
        spdlog::info("ConnectionImpl: destroyed conn#{}", conn_id_);
    }

    // ========================================================================
    // Start the connection lifecycle
    // ========================================================================
    void start() {
        if (running_.exchange(true)) {
            spdlog::warn("ConnectionImpl: conn#{} already running", conn_id_);
            return;
        }

        set_state(ConnectionState::ACCEPTING);
        spdlog::info("ConnectionImpl: starting conn#{} from {}", conn_id_, remote_addr_);

        // Launch the main connection thread
        worker_thread_ = std::thread(&ConnectionImpl::run, this);
    }

    // ========================================================================
    // Stop the connection
    // ========================================================================
    void stop(DisconnectReason reason = DisconnectReason::NORMAL) {
        if (!running_.exchange(false)) return;

        set_state(ConnectionState::CLOSING);
        stats_collector_.set_disconnect_reason(reason);
        spdlog::info("ConnectionImpl: stopping conn#{} reason={}",
            conn_id_, ErrorHandler::reason_message(reason));

        // Notify client of disconnect
        if (data_exchange_.is_open()) {
            auto msg = ErrorHandler::create_disconnect_message(reason);
            data_exchange_.send_message(MessageType::CLOSE_CONNECTION, msg);
        }

        // Unsubscribe from all services
        service_negotiator_.unsubscribe_all(conn_id_);

        // Flush pending messages
        data_exchange_.flush(&handshake_manager_);

        // Close stream
        data_exchange_.close();

        // Remove from server
        if (auto srv = server_.lock()) {
            srv->remove_connection(conn_id_);
        }

        set_state(ConnectionState::CLOSED);

        // Join worker thread
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }

        // Log final statistics
        auto stats = stats_collector_.snapshot();
        spdlog::info("ConnectionImpl: conn#{} closed — {} "
            "sent={}B recv={}B msgs={}/{} err={} duration={}ms",
            conn_id_, ErrorHandler::reason_message(reason),
            stats.bytes_sent, stats.bytes_received,
            stats.messages_sent, stats.messages_received,
            stats.send_errors + stats.recv_errors + stats.protocol_errors,
            now_ms() - stats.connected_at_ms);
    }

    [[nodiscard]] bool is_running() const { return running_.load(); }
    [[nodiscard]] int32_t id() const { return conn_id_; }
    [[nodiscard]] std::string remote_addr() const { return remote_addr_; }
    [[nodiscard]] ConnectionState state() const { return state_.load(); }

    // ========================================================================
    // Send data to the client (public API)
    // ========================================================================
    [[nodiscard]] bool send(MessageType type, const std::vector<uint8_t>& data) {
        if (!running_.load()) return false;
        return data_exchange_.send_message(type, data, &handshake_manager_);
    }

    [[nodiscard]] bool send_json(MessageType type, const nlohmann::json& j) {
        if (!running_.load()) return false;
        return data_exchange_.send_json(type, j, &handshake_manager_);
    }

    // ========================================================================
    // Update permissions
    // ========================================================================
    void update_permissions(const ControlPermissions& perms) {
        authenticator_.update_permissions(auth_state_, perms);
        data_exchange_.send_json(MessageType::SWITCH_PERMISSION,
            nlohmann::json{{"permissions", {
                {"keyboard", perms.keyboard},
                {"clipboard", perms.clipboard},
                {"file_transfer", perms.file_transfer},
                {"audio", perms.audio}
            }}}, &handshake_manager_);
    }

    [[nodiscard]] const ControlPermissions& permissions() const {
        return auth_state_.permissions;
    }

    [[nodiscard]] bool is_view_only() const {
        return auth_state_.view_only;
    }

    // ========================================================================
    // Statistics access
    // ========================================================================
    [[nodiscard]] ConnectionStats stats() const {
        return stats_collector_.snapshot();
    }

    [[nodiscard]] nlohmann::json stats_json() const {
        return stats_collector_.to_json();
    }

private:
    // ========================================================================
    // Main connection loop
    // ========================================================================
    void run() {
        spdlog::info("ConnectionImpl: worker thread started for conn#{}", conn_id_);
        int64_t last_heartbeat_sent = 0;
        int64_t last_stats_logged = 0;

        // Step 1: Handshake
        if (!perform_handshake()) {
            stop(DisconnectReason::PROTOCOL_ERROR);
            return;
        }

        // Step 2: Authenticate
        if (!perform_authentication()) {
            stop(DisconnectReason::AUTH_FAILED);
            return;
        }

        // Step 3: Service negotiation
        if (!perform_service_negotiation()) {
            stop(DisconnectReason::PROTOCOL_ERROR);
            return;
        }

        set_state(ConnectionState::ACTIVE);
        spdlog::info("ConnectionImpl: conn#{} is now ACTIVE", conn_id_);

        // Step 4: Main data exchange loop
        while (running_.load() && data_exchange_.is_open()) {
            int64_t now = now_ms();

            // --- Check health ---
            // Idle timeout
            if (idle_detector_.is_idle()) {
                spdlog::warn("ConnectionImpl: conn#{} idle timeout ({}ms)",
                    conn_id_, idle_detector_.idle_duration_ms());
                stop(DisconnectReason::IDLE_TIMEOUT);
                return;
            }

            // Idle warning
            if (idle_detector_.should_warn()) {
                spdlog::info("ConnectionImpl: conn#{} idle warning", conn_id_);
                idle_detector_.mark_warned();
                // Send idle warning
                send_idle_warning();
            }

            // Heartbeat check
            if (health_monitor_.is_heartbeat_lost()) {
                spdlog::warn("ConnectionImpl: conn#{} heartbeat lost", conn_id_);
                stop(DisconnectReason::HEARTBEAT_LOST);
                return;
            }

            // --- Send heartbeat ---
            if (now - last_heartbeat_sent > config_.heartbeat_interval_ms) {
                send_heartbeat();
                last_heartbeat_sent = now;
                health_monitor_.record_missed_heartbeat();
            }

            // --- Process outgoing queue ---
            data_exchange_.process_outgoing_queue(&handshake_manager_);

            // --- Receive and process messages ---
            MessageType msg_type;
            std::vector<uint8_t> msg_payload;
            if (data_exchange_.recv_message(msg_type, msg_payload, &handshake_manager_)) {
                process_message(msg_type, msg_payload);
            }

            // --- Log periodic stats ---
            if (now - last_stats_logged > conn_constants::STATS_REPORT_INTERVAL_MS) {
                log_stats();
                last_stats_logged = now;
            }

            // --- Update idle duration ---
            stats_collector_.update_idle_duration(idle_detector_.idle_duration_ms());

            // --- Small sleep to prevent busy-looping ---
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        spdlog::info("ConnectionImpl: main loop exited for conn#{}", conn_id_);
    }

    // ========================================================================
    // Step 1: Perform secure handshake
    // ========================================================================
    [[nodiscard]] bool perform_handshake() {
        set_state(ConnectionState::HANDSHAKING);
        spdlog::info("ConnectionImpl: conn#{} starting handshake", conn_id_);

        // Send hello
        auto hello = handshake_manager_.create_hello_message();
        if (!data_exchange_.send_data(hello)) {
            spdlog::error("ConnectionImpl: conn#{} failed to send hello", conn_id_);
            return false;
        }

        // Wait for key exchange response
        int64_t start = now_ms();
        bool key_exchanged = false;

        while (!key_exchanged && running_.load()) {
            if (now_ms() - start > conn_constants::HANDSHAKE_TIMEOUT_MS) {
                spdlog::error("ConnectionImpl: conn#{} handshake timeout", conn_id_);
                return false;
            }

            MessageType type;
            std::vector<uint8_t> payload;
            if (!data_exchange_.recv_message(type, payload)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            if (type != MessageType::MISC) {
                spdlog::warn("ConnectionImpl: conn#{} unexpected msg type {} "
                    "during handshake", conn_id_, static_cast<uint32_t>(type));
                continue;
            }

            std::vector<uint8_t> key_exchange_resp;
            if (handshake_manager_.process_key_exchange(payload, key_exchange_resp)) {
                // Send the key exchange response back
                data_exchange_.send_data(key_exchange_resp);
                key_exchanged = true;

                // Apply encryption to the stream
                auto key = handshake_manager_.get_session_key();
                if (!key.empty() && stream_) {
                    stream_->set_encryption_key(key);
                }
            } else {
                if (!handshake_manager_.can_retry()) {
                    spdlog::error("ConnectionImpl: conn#{} handshake max retries", conn_id_);
                    return false;
                }
                handshake_manager_.record_retry();
            }
        }

        // Complete handshake
        if (!handshake_manager_.complete_handshake()) {
            spdlog::error("ConnectionImpl: conn#{} handshake completion failed", conn_id_);
            return false;
        }

        spdlog::info("ConnectionImpl: conn#{} handshake complete (encrypted={})",
            conn_id_, handshake_manager_.is_encrypted());
        return true;
    }

    // ========================================================================
    // Step 2: Perform authentication
    // ========================================================================
    [[nodiscard]] bool perform_authentication() {
        set_state(ConnectionState::AUTHENTICATING);
        spdlog::info("ConnectionImpl: conn#{} waiting for authentication", conn_id_);

        int64_t start = now_ms();

        while (running_.load() && !auth_state_.authenticated) {
            if (now_ms() - start > conn_constants::AUTH_TIMEOUT_MS) {
                spdlog::error("ConnectionImpl: conn#{} auth timeout", conn_id_);
                stats_collector_.record_timeout();
                return false;
            }

            MessageType type;
            std::vector<uint8_t> payload;
            if (!data_exchange_.recv_message(type, payload)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            // Expect LOGIN message
            if (type == MessageType::LOGIN) {
                auto j = MessageFramer::decode_json_payload(payload);
                if (j.empty()) continue;

                std::string password = j.value("password", "");
                std::string client_ver = j.value("version", "");
                auth_state_.username = j.value("username", "");
                auth_state_.client_platform = j.value("platform", "");

                int32_t result = authenticator_.authenticate(
                    remote_addr_, password, client_ver, auth_state_);

                // Send login response
                auto resp = authenticator_.create_login_response(
                    result, auth_state_.auth_message,
                    auth_state_.permissions, config_.default_resolution);
                data_exchange_.send_message(MessageType::LOGIN_RESPONSE, resp,
                    &handshake_manager_);

                if (result == conn_constants::AUTH_SUCCESS) {
                    spdlog::info("ConnectionImpl: conn#{} authenticated "
                        "(user={}, platform={}, version={})",
                        conn_id_, auth_state_.username,
                        auth_state_.client_platform, auth_state_.client_version);
                    return true;
                } else {
                    spdlog::warn("ConnectionImpl: conn#{} auth failed: {}",
                        conn_id_, auth_state_.auth_message);
                    if (result == conn_constants::AUTH_BLOCKED) {
                        return false; // Don't retry if blocked
                    }
                    // Allow retry on other failures
                    start = now_ms();
                }
            } else if (type == MessageType::CLOSE_CONNECTION) {
                spdlog::info("ConnectionImpl: conn#{} client closed during auth",
                    conn_id_);
                return false;
            } else {
                spdlog::debug("ConnectionImpl: conn#{} ignoring msg type {} "
                    "during auth", conn_id_, static_cast<uint32_t>(type));
            }
        }

        return auth_state_.authenticated;
    }

    // ========================================================================
    // Step 3: Service negotiation — auto-subscribe to default services
    // ========================================================================
    [[nodiscard]] bool perform_service_negotiation() {
        set_state(ConnectionState::NEGOTIATING);
        spdlog::info("ConnectionImpl: conn#{} negotiating services", conn_id_);

        // Build list of default services
        std::vector<std::string> default_services = {
            "video_monitor_0",
        };

        if (config_.enable_audio) {
            default_services.push_back("audio");
        }

        if (config_.enable_clipboard) {
            default_services.push_back("clipboard");
        }

        if (config_.enable_terminal) {
            default_services.push_back("terminal");
        }

        // Always subscribe to input services (read-only)
        default_services.push_back("cursor");
        default_services.push_back("cursor_pos");

        // Subscribe to services that are allowed
        for (auto& svc_name : default_services) {
            if (!config_.is_service_allowed(svc_name)) {
                spdlog::debug("ConnectionImpl: service {} not allowed", svc_name);
                continue;
            }
            service_negotiator_.subscribe(conn_id_, svc_name);
        }

        stats_collector_.update_subscription_count(
            static_cast<uint32_t>(service_negotiator_.subscription_count()));

        set_state(ConnectionState::READY);
        spdlog::info("ConnectionImpl: conn#{} negotiation complete "
            "({} services subscribed)",
            conn_id_, service_negotiator_.subscription_count());
        return true;
    }

    // ========================================================================
    // Process an incoming message
    // ========================================================================
    void process_message(MessageType type, const std::vector<uint8_t>& payload) {
        stats_collector_.record_recv(payload.size());

        switch (type) {
        // --- Authentication (already handled, but re-authentication possible) ---
        case MessageType::LOGIN: {
            spdlog::info("ConnectionImpl: conn#{} re-authentication attempt", conn_id_);
            auto j = MessageFramer::decode_json_payload(payload);
            std::string password = j.value("password", "");
            int32_t result = authenticator_.authenticate(
                remote_addr_, password, auth_state_.client_version, auth_state_);
            auto resp = authenticator_.create_login_response(
                result, auth_state_.auth_message,
                auth_state_.permissions, config_.default_resolution);
            data_exchange_.send_message(MessageType::LOGIN_RESPONSE, resp,
                &handshake_manager_);
            break;
        }

        // --- Close ---
        case MessageType::CLOSE_CONNECTION: {
            spdlog::info("ConnectionImpl: conn#{} client initiated close", conn_id_);
            stop(DisconnectReason::CLIENT_REQUEST);
            break;
        }

        // --- Heartbeat ---
        case MessageType::HEARTBEAT: {
            health_monitor_.record_heartbeat();
            stats_collector_.record_latency(
                static_cast<double>(now_ms() - last_heartbeat_sent_ms_));
            break;
        }

        // --- Service subscription ---
        case MessageType::SUBSCRIBE_SERVICE: {
            auto j = MessageFramer::decode_json_payload(payload);
            std::string svc_name = j.value("service", "");
            std::map<std::string, std::string> options;
            if (j.contains("options") && j["options"].is_object()) {
                for (auto& [k, v] : j["options"].items()) {
                    options[k] = v.get<std::string>();
                }
            }

            if (config_.is_service_allowed(svc_name)) {
                service_negotiator_.subscribe(conn_id_, svc_name, options);
                stats_collector_.update_subscription_count(
                    static_cast<uint32_t>(service_negotiator_.subscription_count()));
                send_json(MessageType::SERVICE_DATA, {
                    {"type", "subscribe_ack"},
                    {"service", svc_name},
                    {"status", "ok"}
                });
            } else {
                send_json(MessageType::SERVICE_DATA, {
                    {"type", "subscribe_ack"},
                    {"service", svc_name},
                    {"status", "denied"}
                });
            }
            break;
        }

        case MessageType::UNSUBSCRIBE_SERVICE: {
            auto j = MessageFramer::decode_json_payload(payload);
            std::string svc_name = j.value("service", "");
            service_negotiator_.unsubscribe(conn_id_, svc_name);
            stats_collector_.update_subscription_count(
                static_cast<uint32_t>(service_negotiator_.subscription_count()));
            break;
        }

        // --- Control messages ---
        case MessageType::SWITCH_DISPLAY:
        case MessageType::SWITCH_PERMISSION:
        case MessageType::PRIVACY_MODE:
        case MessageType::PORT_FORWARD:
        case MessageType::CHAT_MESSAGE:
        case MessageType::WHITEBOARD: {
            auto j = MessageFramer::decode_json_payload(payload);
            auto action = MessageRouter::classify_control_action(type, payload);
            auto result = control_handler_.handle_action(
                action, j, auth_state_, stats_collector_.snapshot(),
                service_negotiator_, conn_id_);

            if (result.success) {
                // Update permissions if applicable
                if (action == ControlAction::SWITCH_PERMISSION) {
                    update_permissions(auth_state_.permissions);
                }
            }

            auto resp = control_handler_.build_response(result);
            data_exchange_.send_message(MessageType::MISC, resp,
                &handshake_manager_);
            break;
        }

        // --- Video ---
        case MessageType::VIDEO_QUALITY_CHANGE: {
            auto j = MessageFramer::decode_json_payload(payload);
            spdlog::info("ConnectionImpl: conn#{} video quality change: {}",
                conn_id_, j.dump());
            // Forward to video service via message routing
            // The actual routing is done by the server-level message dispatch
            break;
        }

        case MessageType::VIDEO_CODEC_CHANGE: {
            auto j = MessageFramer::decode_json_payload(payload);
            spdlog::info("ConnectionImpl: conn#{} video codec change: {}",
                conn_id_, j.dump());
            break;
        }

        // --- Audio ---
        case MessageType::AUDIO_CONFIG: {
            auto j = MessageFramer::decode_json_payload(payload);
            spdlog::info("ConnectionImpl: conn#{} audio config: {}",
                conn_id_, j.dump());
            break;
        }

        // --- Input events ---
        case MessageType::MOUSE_EVENT:
        case MessageType::KEY_EVENT: {
            if (auth_state_.view_only || !auth_state_.permissions.keyboard) {
                spdlog::debug("ConnectionImpl: conn#{} input blocked (view-only)",
                    conn_id_);
                break;
            }
            // Input events are forwarded to input service via routing
            route_to_service(type, payload);
            break;
        }

        case MessageType::CURSOR_DATA:
        case MessageType::CURSOR_POSITION:
        case MessageType::CURSOR_SHAPE: {
            route_to_service(type, payload);
            break;
        }

        // --- Clipboard ---
        case MessageType::CLIPBOARD_TEXT:
        case MessageType::CLIPBOARD_IMAGE:
        case MessageType::CLIPBOARD_FILE: {
            if (!auth_state_.permissions.clipboard) {
                spdlog::debug("ConnectionImpl: conn#{} clipboard blocked", conn_id_);
                break;
            }
            route_to_service(type, payload);
            break;
        }

        // --- File transfer ---
        case MessageType::FILE_TRANSFER_REQUEST:
        case MessageType::FILE_TRANSFER_RESPONSE:
        case MessageType::FILE_CHUNK:
        case MessageType::FILE_DONE:
        case MessageType::FILE_DIR: {
            if (!auth_state_.permissions.file_transfer) {
                spdlog::debug("ConnectionImpl: conn#{} file transfer blocked",
                    conn_id_);
                break;
            }
            route_to_service(type, payload);
            break;
        }

        // --- Misc ---
        case MessageType::MISC: {
            auto j = MessageFramer::decode_json_payload(payload);
            spdlog::debug("ConnectionImpl: conn#{} misc message: {}",
                conn_id_, j.dump());
            break;
        }

        default: {
            spdlog::debug("ConnectionImpl: conn#{} unhandled message type {}",
                conn_id_, static_cast<uint32_t>(type));
            break;
        }
        }
    }

    // ========================================================================
    // Route a message to the appropriate service handler via the server
    // ========================================================================
    void route_to_service(MessageType type, const std::vector<uint8_t>& payload) {
        // The actual routing to specific service implementations is handled
        // at the server level. Here we just record stats and forward.
        auto category = message_router_.classify_message(type);

        // Record stats
        std::string svc_category;
        switch (category) {
        case ServiceCategory::VIDEO:     svc_category = "video"; break;
        case ServiceCategory::AUDIO:     svc_category = "audio"; break;
        case ServiceCategory::INPUT:     svc_category = "input"; break;
        case ServiceCategory::CLIPBOARD: svc_category = "clipboard"; break;
        case ServiceCategory::FILE:      svc_category = "file"; break;
        default:                         svc_category = "other"; break;
        }
        stats_collector_.record_service_message(svc_category, false);

        // Forward to server for actual dispatch
        if (auto srv = server_.lock()) {
            message_router_.route(
                conn_id_, type, payload,
                auth_state_.permissions,
                auth_state_.view_only);
        }
    }

    // ========================================================================
    // Heartbeat
    // ========================================================================
    void send_heartbeat() {
        nlohmann::json hb;
        hb["timestamp"] = now_ms();
        hb["sequence"] = heartbeat_seq_++;
        data_exchange_.send_json(MessageType::HEARTBEAT, hb,
            &handshake_manager_);
        last_heartbeat_sent_ms_ = now_ms();
    }

    // ========================================================================
    // Idle warning
    // ========================================================================
    void send_idle_warning() {
        nlohmann::json warn;
        warn["type"] = "idle_warning";
        warn["idle_ms"] = idle_detector_.idle_duration_ms();
        warn["timeout_ms"] = config_.idle_timeout_ms;
        data_exchange_.send_json(MessageType::MISC, warn,
            &handshake_manager_);
    }

    // ========================================================================
    // Periodic statistics logging
    // ========================================================================
    void log_stats() {
        auto stats = stats_collector_.snapshot();
        spdlog::debug("ConnectionImpl: stats for conn#{} — {}",
            conn_id_, stats.summary());
    }

    // ========================================================================
    // State management
    // ========================================================================
    void set_state(ConnectionState new_state) {
        state_.store(new_state, std::memory_order_release);
        stats_collector_.record_state_change(new_state);
        spdlog::debug("ConnectionImpl: conn#{} state → {}",
            conn_id_, static_cast<int>(new_state));
    }

    // ========================================================================
    // Utility
    // ========================================================================
    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // --- Member variables ---

    // Identity
    int32_t         conn_id_;
    std::string     remote_addr_;
    ServerWeakPtr   server_;
    ConnectionConfig config_;

    // I/O
    StreamPtr   stream_;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    std::atomic<ConnectionState> state_{ConnectionState::INIT};

    // Managers
    HandshakeManager                handshake_manager_;
    Authenticator                   authenticator_;
    ServiceNegotiator               service_negotiator_;
    IdleDetector                    idle_detector_;
    ConnectionHealthMonitor         health_monitor_;
    ConnectionStatisticsCollector   stats_collector_;
    QueuedMessageBuffer             out_queue_;
    DataExchangeManager             data_exchange_;
    MessageRouter                   message_router_;
    ControlHandler                  control_handler_;

    // State
    AuthState       auth_state_;
    uint64_t        heartbeat_seq_{0};
    int64_t         last_heartbeat_sent_ms_{0};
};

// ============================================================================
// ConnectionManager — global connection lifecycle manager
// ============================================================================
// Handles connection creation, pooling, cleanup, and global statistics.
// This is the top-level manager that the server uses.
// ============================================================================
class ConnectionManagerImpl {
public:
    explicit ConnectionManagerImpl(ConnectionConfig config)
        : config_(std::move(config))
        , pool_(config_)
        , rate_limiter_(config_.max_login_attempts, config_.login_block_duration_ms)
    {
        spdlog::info("ConnectionManager: initialized "
            "(max_conns={}, max_per_ip={}, idle_timeout={}ms)",
            config_.max_connections, config_.max_connections_per_ip,
            config_.idle_timeout_ms);
    }

    ~ConnectionManagerImpl() {
        shutdown();
    }

    // ========================================================================
    // Accept and create a new connection
    // ========================================================================
    [[nodiscard]] std::shared_ptr<ConnectionImpl> accept_connection(
        StreamPtr stream,
        ServerWeakPtr server,
        std::optional<ControlPermissions> perms = std::nullopt)
    {
        std::unique_lock lk(mutex_);

        // Generate connection ID
        int32_t conn_id = next_id();
        std::string addr = stream->remote_addr();

        // Check rate limiting
        if (!rate_limiter_.allow_login(addr)) {
            spdlog::warn("ConnectionManager: rejecting connection from {} (rate limited)",
                addr);
            stream->close();
            return nullptr;
        }

        // Allocate pool slot
        if (!pool_.try_allocate(conn_id, addr)) {
            spdlog::warn("ConnectionManager: rejecting connection from {} (pool full)",
                addr);
            stream->close();
            return nullptr;
        }

        // Create connection
        auto conn = std::make_shared<ConnectionImpl>(
            conn_id, std::move(stream), addr,
            std::move(server), config_, perms);

        connections_[conn_id] = conn;
        total_connections_created_++;

        spdlog::info("ConnectionManager: accepted conn#{} from {} (total active: {})",
            conn_id, addr, connections_.size());

        lk.unlock();

        // Start the connection
        conn->start();

        return conn;
    }

    // ========================================================================
    // Remove a connection
    // ========================================================================
    void remove_connection(int32_t conn_id) {
        std::unique_lock lk(mutex_);

        auto it = connections_.find(conn_id);
        if (it == connections_.end()) {
            spdlog::debug("ConnectionManager: conn#{} not found for removal", conn_id);
            return;
        }

        auto conn = it->second;
        connections_.erase(it);
        pool_.release(conn_id);

        spdlog::info("ConnectionManager: removed conn#{} (total active: {})",
            conn_id, connections_.size());

        lk.unlock();

        // Ensure connection is stopped
        if (conn->is_running()) {
            conn->stop(DisconnectReason::NORMAL);
        }

        total_connections_closed_++;
    }

    // ========================================================================
    // Get a connection by ID
    // ========================================================================
    [[nodiscard]] std::shared_ptr<ConnectionImpl> get_connection(
        int32_t conn_id) const
    {
        std::lock_guard lk(mutex_);
        auto it = connections_.find(conn_id);
        return it != connections_.end() ? it->second : nullptr;
    }

    // ========================================================================
    // Get count of active connections
    // ========================================================================
    [[nodiscard]] size_t active_count() const {
        std::lock_guard lk(mutex_);
        return connections_.size();
    }

    // ========================================================================
    // Periodic cleanup — remove stale/dead connections
    // ========================================================================
    void cleanup() {
        std::unique_lock lk(mutex_);
        std::vector<int32_t> to_remove;

        int64_t now = now_ms();
        for (auto& [id, conn] : connections_) {
            if (!conn || !conn->is_running()) {
                to_remove.push_back(id);
                continue;
            }

            // Check if connection state is error or closed
            auto state = conn->state();
            if (state == ConnectionState::CLOSED ||
                state == ConnectionState::ERROR) {
                to_remove.push_back(id);
            }
        }

        lk.unlock();

        for (int32_t id : to_remove) {
            spdlog::info("ConnectionManager: cleanup removing stale conn#{}", id);
            remove_connection(id);
        }

        if (!to_remove.empty()) {
            spdlog::info("ConnectionManager: cleaned up {} stale connections",
                to_remove.size());
        }
    }

    // ========================================================================
    // Close all connections
    // ========================================================================
    void close_all() {
        std::unique_lock lk(mutex_);
        auto conns_copy = connections_;
        connections_.clear();
        pool_.clear();
        lk.unlock();

        spdlog::info("ConnectionManager: closing all {} connections", conns_copy.size());

        for (auto& [id, conn] : conns_copy) {
            if (conn) {
                conn->stop(DisconnectReason::SERVER_SHUTDOWN);
            }
        }

        spdlog::info("ConnectionManager: all connections closed");
    }

    // ========================================================================
    // Broadcast a message to all connections
    // ========================================================================
    void broadcast(MessageType type, const std::vector<uint8_t>& data,
        int32_t exclude_conn = -1)
    {
        std::lock_guard lk(mutex_);
        for (auto& [id, conn] : connections_) {
            if (id == exclude_conn) continue;
            if (conn && conn->is_running()) {
                conn->send(type, data);
            }
        }
    }

    // ========================================================================
    // Broadcast JSON to all connections
    // ========================================================================
    void broadcast_json(MessageType type, const nlohmann::json& j,
        int32_t exclude_conn = -1)
    {
        std::lock_guard lk(mutex_);
        for (auto& [id, conn] : connections_) {
            if (id == exclude_conn) continue;
            if (conn && conn->is_running()) {
                conn->send_json(type, j);
            }
        }
    }

    // ========================================================================
    // Kick a connection
    // ========================================================================
    void kick_connection(int32_t conn_id, const std::string& reason = "") {
        auto conn = get_connection(conn_id);
        if (conn) {
            spdlog::warn("ConnectionManager: kicking conn#{} ({})",
                conn_id, reason);
            conn->stop(DisconnectReason::KICKED);
            remove_connection(conn_id);
        }
    }

    // ========================================================================
    // Ban an IP address
    // ========================================================================
    void ban_address(const std::string& addr) {
        rate_limiter_.ban(addr);
        spdlog::warn("ConnectionManager: banned {}", addr);

        // Kick all connections from this address
        std::unique_lock lk(mutex_);
        std::vector<int32_t> to_kick;
        for (auto& [id, conn] : connections_) {
            if (conn && conn->remote_addr() == addr) {
                to_kick.push_back(id);
            }
        }
        lk.unlock();

        for (int32_t id : to_kick) {
            kick_connection(id, "banned");
        }
    }

    // ========================================================================
    // Unban an IP address
    // ========================================================================
    void unban_address(const std::string& addr) {
        rate_limiter_.unban(addr);
        spdlog::info("ConnectionManager: unbanned {}", addr);
    }

    // ========================================================================
    // Set view-only mode for a connection
    // ========================================================================
    void set_view_only(int32_t conn_id, bool view_only) {
        auto conn = get_connection(conn_id);
        if (!conn) return;

        ControlPermissions perms = conn->permissions();
        perms.keyboard = !view_only;
        conn->update_permissions(perms);
        spdlog::info("ConnectionManager: conn#{} view_only={}", conn_id, view_only);
    }

    // ========================================================================
    // Get all active connection IDs
    // ========================================================================
    [[nodiscard]] std::vector<int32_t> active_connection_ids() const {
        std::lock_guard lk(mutex_);
        std::vector<int32_t> ids;
        ids.reserve(connections_.size());
        for (auto& [id, _] : connections_) {
            ids.push_back(id);
        }
        return ids;
    }

    // ========================================================================
    // Get global statistics
    // ========================================================================
    [[nodiscard]] nlohmann::json global_stats() const {
        std::lock_guard lk(mutex_);

        nlohmann::json j;
        j["active_connections"] = connections_.size();
        j["total_created"] = total_connections_created_;
        j["total_closed"] = total_connections_closed_;
        j["max_connections"] = config_.max_connections;
        j["pool_size"] = pool_.size();

        nlohmann::json per_conn = nlohmann::json::array();
        for (auto& [id, conn] : connections_) {
            if (conn) {
                per_conn.push_back(conn->stats_json());
            }
        }
        j["connections"] = per_conn;

        // Per-IP counts
        auto ip_counts = pool_.per_ip_counts();
        nlohmann::json ip_json;
        for (auto& [addr, count] : ip_counts) {
            ip_json[addr] = count;
        }
        j["per_ip"] = ip_json;

        return j;
    }

    // ========================================================================
    // Shutdown the manager
    // ========================================================================
    void shutdown() {
        if (shutdown_.exchange(true)) return;
        spdlog::info("ConnectionManager: shutting down");
        close_all();
    }

    [[nodiscard]] bool is_shutdown() const {
        return shutdown_.load();
    }

private:
    ConnectionConfig config_;
    ConnectionPool pool_;
    RateLimiter rate_limiter_;
    mutable std::mutex mutex_;

    std::map<int32_t, std::shared_ptr<ConnectionImpl>> connections_;
    std::atomic<int32_t> id_counter_{conn_constants::MIN_CONNECTION_ID};
    std::atomic<uint64_t> total_connections_created_{0};
    std::atomic<uint64_t> total_connections_closed_{0};
    std::atomic<bool> shutdown_{false};

    int32_t next_id() {
        return id_counter_.fetch_add(1);
    }

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// ============================================================================
// ConnectionManager — public singleton wrapper
// ============================================================================
// This is the public API for connection management.
// ============================================================================
class ConnectionManagerSingleton {
public:
    static ConnectionManagerSingleton& instance() {
        static ConnectionManagerSingleton inst;
        return inst;
    }

    /// Initialize the connection manager with configuration.
    void initialize(const ConnectionConfig& config) {
        std::lock_guard lk(mutex_);
        if (impl_) {
            spdlog::warn("ConnectionManager: already initialized");
            return;
        }
        impl_ = std::make_unique<ConnectionManagerImpl>(config);
        spdlog::info("ConnectionManager: initialized with config");
    }

    /// Accept a new connection from an incoming stream.
    [[nodiscard]] std::shared_ptr<ConnectionImpl> accept(
        StreamPtr stream,
        ServerWeakPtr server,
        std::optional<ControlPermissions> perms = std::nullopt)
    {
        std::lock_guard lk(mutex_);
        if (!impl_) {
            spdlog::error("ConnectionManager: not initialized");
            return nullptr;
        }
        return impl_->accept_connection(std::move(stream), std::move(server), perms);
    }

    /// Remove a connection.
    void remove(int32_t conn_id) {
        std::lock_guard lk(mutex_);
        if (impl_) impl_->remove_connection(conn_id);
    }

    /// Get a connection by ID.
    [[nodiscard]] std::shared_ptr<ConnectionImpl> get(int32_t conn_id) const {
        std::lock_guard lk(mutex_);
        return impl_ ? impl_->get_connection(conn_id) : nullptr;
    }

    /// Periodic cleanup.
    void cleanup() {
        std::lock_guard lk(mutex_);
        if (impl_) impl_->cleanup();
    }

    /// Close all connections.
    void close_all() {
        std::lock_guard lk(mutex_);
        if (impl_) impl_->close_all();
    }

    /// Broadcast to all connections.
    void broadcast(MessageType type, const std::vector<uint8_t>& data,
        int32_t exclude = -1) {
        std::lock_guard lk(mutex_);
        if (impl_) impl_->broadcast(type, data, exclude);
    }

    /// Kick a connection.
    void kick(int32_t conn_id, const std::string& reason = "") {
        std::lock_guard lk(mutex_);
        if (impl_) impl_->kick_connection(conn_id, reason);
    }

    /// Ban an address.
    void ban(const std::string& addr) {
        std::lock_guard lk(mutex_);
        if (impl_) impl_->ban_address(addr);
    }

    /// Unban an address.
    void unban(const std::string& addr) {
        std::lock_guard lk(mutex_);
        if (impl_) impl_->unban_address(addr);
    }

    /// Set view-only for a connection.
    void set_view_only(int32_t conn_id, bool vo) {
        std::lock_guard lk(mutex_);
        if (impl_) impl_->set_view_only(conn_id, vo);
    }

    /// Get active count.
    [[nodiscard]] size_t active_count() const {
        std::lock_guard lk(mutex_);
        return impl_ ? impl_->active_count() : 0;
    }

    /// Get global stats.
    [[nodiscard]] nlohmann::json stats() const {
        std::lock_guard lk(mutex_);
        return impl_ ? impl_->global_stats() : nlohmann::json{};
    }

    /// Shutdown.
    void shutdown() {
        std::lock_guard lk(mutex_);
        if (impl_) {
            impl_->shutdown();
            impl_.reset();
        }
    }

    [[nodiscard]] bool is_initialized() const {
        std::lock_guard lk(mutex_);
        return impl_ != nullptr;
    }

private:
    ConnectionManagerSingleton() = default;
    mutable std::mutex mutex_;
    std::unique_ptr<ConnectionManagerImpl> impl_;
};

} // namespace cppdesk::server
