#include "rendezvous/rendezvous.hpp"
#include "common/crypto.hpp"
#include "common/protocol.hpp"
#include "common/config.hpp"
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <algorithm>
#include <queue>
#include <set>
#include <array>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <condition_variable>

namespace cppdesk::rendezvous {

using namespace std::chrono_literals;

// ============================================================================
// Protocol constants (matching RustDesk wire format)
// ============================================================================
namespace protocol {

constexpr uint8_t MAGIC_NUMBER[] = {0x52, 0x44, 0x52, 0x44}; // "RDRD"
constexpr size_t HEADER_SIZE = 12; // 4 magic + 4 length + 4 type
constexpr size_t MAX_PAYLOAD_SIZE = 65536;
constexpr uint32_t VERSION = 0x0001;

// Wire message type mapping
constexpr uint32_t WIRE_REGISTER_PEER     = 1;
constexpr uint32_t WIRE_PUNCH_HOLE_REQ    = 2;
constexpr uint32_t WIRE_PUNCH_HOLE_RESP   = 3;
constexpr uint32_t WIRE_REQUEST_RELAY     = 4;
constexpr uint32_t WIRE_TEST_NAT          = 5;
constexpr uint32_t WIRE_QUERY_ONLINE      = 6;
constexpr uint32_t WIRE_HEARTBEAT         = 7;
constexpr uint32_t WIRE_REGISTER_PK       = 8;
constexpr uint32_t WIRE_PK_RESPONSE       = 9;
constexpr uint32_t WIRE_CONFIG_REQUEST    = 10;
constexpr uint32_t WIRE_CONFIG_RESPONSE   = 11;
constexpr uint32_t WIRE_SOFTWARE_UPDATE   = 12;
constexpr uint32_t WIRE_ALIAS_UPDATE      = 13;
constexpr uint32_t WIRE_ADDRESS_BOOK      = 14;

// NAT type wire values
constexpr uint32_t NAT_UNKNOWN              = 0;
constexpr uint32_t NAT_OPEN                 = 1;
constexpr uint32_t NAT_FULL_CONE            = 2;
constexpr uint32_t NAT_RESTRICTED_CONE      = 3;
constexpr uint32_t NAT_PORT_RESTRICTED_CONE = 4;
constexpr uint32_t NAT_SYMMETRIC            = 5;

} // namespace protocol

// ============================================================================
// Helper: generate random port
// ============================================================================
static uint16_t random_port() {
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<uint16_t> dist(20000, 60000);
    return dist(rng);
}

// ============================================================================
// Helper: generate nonce
// ============================================================================
static std::vector<uint8_t> generate_nonce() {
    return common::crypto::random_bytes(24);
}

// ============================================================================
// Helper: hash string to hex
// ============================================================================
static std::string sha256_hex(const std::string& data) {
    auto hash = common::crypto::sha256(data);
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (auto b : hash) ss << std::setw(2) << static_cast<int>(b);
    return ss.str();
}

// ============================================================================
// Helper: verify PK using signature
// ============================================================================
static bool verify_pk_signature(const std::string& pk_b64, const std::string& token,
    const std::string& signature_b64) {
    auto pk_bytes = common::crypto::decode64(pk_b64);
    auto sig_bytes = common::crypto::decode64(signature_b64);
    auto token_hash = common::crypto::sha256(token);
    return common::crypto::sign_verify(
        token_hash.data(), token_hash.size(),
        sig_bytes.data(), pk_bytes.data());
}

// ============================================================================
// Helper: serialize to wire format
// ============================================================================
static std::vector<uint8_t> serialize_message(uint32_t msg_type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> result;
    result.reserve(protocol::HEADER_SIZE + payload.size());
    // Magic
    result.insert(result.end(), protocol::MAGIC_NUMBER, protocol::MAGIC_NUMBER + 4);
    // Payload length (big-endian uint32)
    uint32_t len = static_cast<uint32_t>(payload.size());
    result.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
    result.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
    result.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>(len & 0xFF));
    // Message type (big-endian uint32)
    result.push_back(static_cast<uint8_t>((msg_type >> 24) & 0xFF));
    result.push_back(static_cast<uint8_t>((msg_type >> 16) & 0xFF));
    result.push_back(static_cast<uint8_t>((msg_type >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>(msg_type & 0xFF));
    // Payload
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

// ============================================================================
// Helper: serialize string to big-endian uint16 length prefixed bytes
// ============================================================================
static void write_string(std::vector<uint8_t>& buf, const std::string& s) {
    uint16_t len = static_cast<uint16_t>(s.size());
    buf.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(len & 0xFF));
    buf.insert(buf.end(), s.begin(), s.end());
}

// ============================================================================
// Helper: write uint32 big-endian
// ============================================================================
static void write_u32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

// ============================================================================
// Helper: write uint64 big-endian
// ============================================================================
static void write_u64(std::vector<uint8_t>& buf, uint64_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 56) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 48) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 40) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 32) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

// ============================================================================
// Helper: read uint16 big-endian from buffer at offset
// ============================================================================
static uint16_t read_u16(const uint8_t* data, size_t& offset) {
    uint16_t val = (static_cast<uint16_t>(data[offset]) << 8) |
                   static_cast<uint16_t>(data[offset + 1]);
    offset += 2;
    return val;
}

// ============================================================================
// Helper: read uint32 big-endian from buffer at offset
// ============================================================================
static uint32_t read_u32(const uint8_t* data, size_t& offset) {
    uint32_t val = (static_cast<uint32_t>(data[offset]) << 24) |
                   (static_cast<uint32_t>(data[offset + 1]) << 16) |
                   (static_cast<uint32_t>(data[offset + 2]) << 8) |
                   static_cast<uint32_t>(data[offset + 3]);
    offset += 4;
    return val;
}

// ============================================================================
// Helper: read uint64 big-endian from buffer at offset
// ============================================================================
static uint64_t read_u64(const uint8_t* data, size_t& offset) {
    uint64_t val = (static_cast<uint64_t>(data[offset]) << 56) |
                   (static_cast<uint64_t>(data[offset + 1]) << 48) |
                   (static_cast<uint64_t>(data[offset + 2]) << 40) |
                   (static_cast<uint64_t>(data[offset + 3]) << 32) |
                   (static_cast<uint64_t>(data[offset + 4]) << 24) |
                   (static_cast<uint64_t>(data[offset + 5]) << 16) |
                   (static_cast<uint64_t>(data[offset + 6]) << 8) |
                   static_cast<uint64_t>(data[offset + 7]);
    offset += 8;
    return val;
}

// ============================================================================
// Helper: read length-prefixed string from buffer at offset
// ============================================================================
static std::string read_string(const uint8_t* data, size_t len, size_t& offset) {
    if (offset + 2 > len) return {};
    uint16_t str_len = read_u16(data, offset);
    if (offset + str_len > len) return {};
    std::string result(reinterpret_cast<const char*>(data + offset), str_len);
    offset += str_len;
    return result;
}

// ============================================================================
// Helper: build register payload
// ============================================================================
static std::vector<uint8_t> build_register_payload(
    const std::string& id, const std::string& password,
    const std::string& hostname, const std::string& platform_name,
    uint16_t udp_port, uint32_t nat_type) {
    std::vector<uint8_t> buf;
    write_string(buf, id);
    // Hashed password
    auto pw_hash = sha256_hex(password);
    write_string(buf, pw_hash);
    write_string(buf, hostname);
    write_string(buf, platform_name);
    write_u32(buf, static_cast<uint32_t>(udp_port));
    write_u32(buf, nat_type);
    write_u32(buf, protocol::VERSION);
    return buf;
}

// ============================================================================
// Helper: build PK register payload
// ============================================================================
static std::vector<uint8_t> build_pk_register_payload(
    const std::string& pk, const std::string& token,
    const std::string& id, const std::string& uuid) {
    std::vector<uint8_t> buf;
    write_string(buf, pk);
    write_string(buf, token);
    write_string(buf, id);
    write_string(buf, uuid);
    return buf;
}

// ============================================================================
// Helper: build punch hole request payload
// ============================================================================
static std::vector<uint8_t> build_punch_hole_payload(
    const std::string& peer_id, const std::string& key,
    const std::string& token, uint32_t conn_type,
    bool force_relay, const std::string& my_id) {
    std::vector<uint8_t> buf;
    write_string(buf, my_id);
    write_string(buf, peer_id);
    write_string(buf, key);
    write_string(buf, token);
    write_u32(buf, conn_type);
    buf.push_back(force_relay ? 1 : 0);
    // NAT type
    write_u32(buf, 0); // Will be filled by client
    // Local UDP port
    write_u32(buf, 0);
    // Local TCP port
    write_u32(buf, 0);
    return buf;
}

// ============================================================================
// Helper: build query online payload
// ============================================================================
static std::vector<uint8_t> build_query_online_payload(const std::string& peer_id) {
    std::vector<uint8_t> buf;
    write_string(buf, peer_id);
    return buf;
}

// ============================================================================
// Helper: build heartbeat payload
// ============================================================================
static std::vector<uint8_t> build_heartbeat_payload() {
    std::vector<uint8_t> buf;
    write_u64(buf, static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()));
    return buf;
}

// ============================================================================
// Helper: build alias update payload
// ============================================================================
static std::vector<uint8_t> build_alias_payload(const std::string& alias) {
    std::vector<uint8_t> buf;
    write_string(buf, alias);
    return buf;
}

// ============================================================================
// Helper: build relay request payload
// ============================================================================
static std::vector<uint8_t> build_relay_payload(
    const std::string& uuid, const std::string& licence_key) {
    std::vector<uint8_t> buf;
    write_string(buf, uuid);
    write_string(buf, licence_key);
    return buf;
}

// ============================================================================
// Helper: build config request payload
// ============================================================================
static std::vector<uint8_t> build_config_request_payload(const std::string& key) {
    std::vector<uint8_t> buf;
    write_string(buf, key);
    write_u32(buf, protocol::VERSION);
    return buf;
}

// ============================================================================
// Helper: parse message from wire format
// ============================================================================
static std::optional<std::pair<uint32_t, std::vector<uint8_t>>>
parse_wire_message(const std::vector<uint8_t>& buf) {
    if (buf.size() < protocol::HEADER_SIZE) return std::nullopt;
    // Check magic
    if (buf[0] != protocol::MAGIC_NUMBER[0] ||
        buf[1] != protocol::MAGIC_NUMBER[1] ||
        buf[2] != protocol::MAGIC_NUMBER[2] ||
        buf[3] != protocol::MAGIC_NUMBER[3]) {
        spdlog::warn("Invalid magic number in wire message");
        return std::nullopt;
    }
    // Read length
    uint32_t payload_len = (static_cast<uint32_t>(buf[4]) << 24) |
                           (static_cast<uint32_t>(buf[5]) << 16) |
                           (static_cast<uint32_t>(buf[6]) << 8) |
                           static_cast<uint32_t>(buf[7]);
    if (payload_len > protocol::MAX_PAYLOAD_SIZE) {
        spdlog::warn("Payload too large: {}", payload_len);
        return std::nullopt;
    }
    // Read type
    uint32_t msg_type = (static_cast<uint32_t>(buf[8]) << 24) |
                        (static_cast<uint32_t>(buf[9]) << 16) |
                        (static_cast<uint32_t>(buf[10]) << 8) |
                        static_cast<uint32_t>(buf[11]);
    // Extract payload
    if (protocol::HEADER_SIZE + payload_len > buf.size()) {
        spdlog::warn("Incomplete message: header says {} but got {}",
            payload_len, buf.size() - protocol::HEADER_SIZE);
        return std::nullopt;
    }
    std::vector<uint8_t> payload(
        buf.begin() + protocol::HEADER_SIZE,
        buf.begin() + protocol::HEADER_SIZE + payload_len);
    return std::make_pair(msg_type, std::move(payload));
}

// ============================================================================
// Helper: map wire type to RendezvousMessageType
// ============================================================================
static RendezvousMessageType wire_type_to_msg(uint32_t wire_type) {
    switch (wire_type) {
        case protocol::WIRE_REGISTER_PEER:     return RendezvousMessageType::REGISTER_PEER;
        case protocol::WIRE_PUNCH_HOLE_REQ:    return RendezvousMessageType::PUNCH_HOLE_REQUEST;
        case protocol::WIRE_PUNCH_HOLE_RESP:   return RendezvousMessageType::PUNCH_HOLE_RESPONSE;
        case protocol::WIRE_REQUEST_RELAY:     return RendezvousMessageType::REQUEST_RELAY;
        case protocol::WIRE_TEST_NAT:          return RendezvousMessageType::TEST_NAT;
        case protocol::WIRE_QUERY_ONLINE:      return RendezvousMessageType::QUERY_ONLINE;
        case protocol::WIRE_HEARTBEAT:         return RendezvousMessageType::HEARTBEAT;
        case protocol::WIRE_REGISTER_PK:       return RendezvousMessageType::REGISTER_PK;
        case protocol::WIRE_PK_RESPONSE:       return RendezvousMessageType::PK_RESPONSE;
        case protocol::WIRE_CONFIG_REQUEST:    return RendezvousMessageType::CONFIG_REQUEST;
        case protocol::WIRE_CONFIG_RESPONSE:   return RendezvousMessageType::CONFIG_RESPONSE;
        case protocol::WIRE_SOFTWARE_UPDATE:   return RendezvousMessageType::SOFTWARE_UPDATE;
        case protocol::WIRE_ALIAS_UPDATE:      return RendezvousMessageType::ALIAS_UPDATE;
        case protocol::WIRE_ADDRESS_BOOK:      return RendezvousMessageType::ADDRESS_BOOK;
        default:                               return RendezvousMessageType::REGISTER_PEER;
    }
}

// ============================================================================
// Helper: map wire type to message type string for logging
// ============================================================================
static const char* msg_type_name(uint32_t wire_type) {
    switch (wire_type) {
        case protocol::WIRE_REGISTER_PEER:     return "REGISTER_PEER";
        case protocol::WIRE_PUNCH_HOLE_REQ:    return "PUNCH_HOLE_REQ";
        case protocol::WIRE_PUNCH_HOLE_RESP:   return "PUNCH_HOLE_RESP";
        case protocol::WIRE_REQUEST_RELAY:     return "REQUEST_RELAY";
        case protocol::WIRE_TEST_NAT:          return "TEST_NAT";
        case protocol::WIRE_QUERY_ONLINE:      return "QUERY_ONLINE";
        case protocol::WIRE_HEARTBEAT:         return "HEARTBEAT";
        case protocol::WIRE_REGISTER_PK:       return "REGISTER_PK";
        case protocol::WIRE_PK_RESPONSE:       return "PK_RESPONSE";
        case protocol::WIRE_CONFIG_REQUEST:    return "CONFIG_REQUEST";
        case protocol::WIRE_CONFIG_RESPONSE:   return "CONFIG_RESPONSE";
        case protocol::WIRE_SOFTWARE_UPDATE:   return "SOFTWARE_UPDATE";
        case protocol::WIRE_ALIAS_UPDATE:      return "ALIAS_UPDATE";
        case protocol::WIRE_ADDRESS_BOOK:      return "ADDRESS_BOOK";
        default:                               return "UNKNOWN";
    }
}

// ============================================================================
// Helper: nat type to string
// ============================================================================
static const char* nat_type_name(NatType t) {
    switch (t) {
        case NatType::UNKNOWN_NAT:           return "UNKNOWN";
        case NatType::OPEN_INTERNET:         return "OPEN";
        case NatType::FULL_CONE:             return "FULL_CONE";
        case NatType::RESTRICTED_CONE:       return "RESTRICTED_CONE";
        case NatType::PORT_RESTRICTED_CONE:  return "PORT_RESTRICTED_CONE";
        case NatType::SYMMETRIC:             return "SYMMETRIC";
        default:                             return "INVALID";
    }
}

// ============================================================================
// STUN-like message for NAT type detection
// ============================================================================
namespace stun {

constexpr uint32_t MAGIC_COOKIE = 0x2112A442;
constexpr uint16_t BINDING_REQUEST  = 0x0001;
constexpr uint16_t BINDING_RESPONSE = 0x0101;
constexpr uint16_t ATTR_MAPPED_ADDRESS   = 0x0001;
constexpr uint16_t ATTR_XOR_MAPPED_ADDRESS = 0x0020;
constexpr uint16_t ATTR_CHANGE_REQUEST   = 0x0003;
constexpr uint16_t ATTR_RESPONSE_ORIGIN  = 0x802B;
constexpr uint16_t ATTR_OTHER_ADDRESS    = 0x802C;

static std::vector<uint8_t> build_stun_binding_request(
    bool change_ip, bool change_port) {
    std::vector<uint8_t> buf;
    buf.reserve(28);
    // Type: Binding Request
    buf.push_back(0x00);
    buf.push_back(0x01);
    // Length (will include change-request attribute: 8 bytes)
    uint16_t msg_len = change_ip || change_port ? 8 : 0;
    buf.push_back(static_cast<uint8_t>((msg_len >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(msg_len & 0xFF));
    // Magic cookie
    buf.push_back(static_cast<uint8_t>((MAGIC_COOKIE >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((MAGIC_COOKIE >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((MAGIC_COOKIE >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(MAGIC_COOKIE & 0xFF));
    // Transaction ID (12 bytes random)
    auto tid = common::crypto::random_bytes(12);
    buf.insert(buf.end(), tid.begin(), tid.end());
    // Change-Request attribute if needed
    if (change_ip || change_port) {
        // Attribute type: CHANGE_REQUEST
        buf.push_back(static_cast<uint8_t>((ATTR_CHANGE_REQUEST >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(ATTR_CHANGE_REQUEST & 0xFF));
        // Attribute length: 4
        buf.push_back(0x00);
        buf.push_back(0x04);
        // Value: 4 bytes (flag bits 2=change_ip, 1=change_port)
        uint32_t flags = 0;
        if (change_ip) flags |= 0x04;
        if (change_port) flags |= 0x02;
        buf.push_back(static_cast<uint8_t>((flags >> 24) & 0xFF));
        buf.push_back(static_cast<uint8_t>((flags >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((flags >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(flags & 0xFF));
    }
    return buf;
}

static bool is_stun_response(const std::vector<uint8_t>& data) {
    if (data.size() < 20) return false;
    return data[0] == 0x01 && data[1] == 0x01; // Binding Response
}

static std::optional<std::pair<std::string, uint16_t>>
parse_stun_mapped_address(const std::vector<uint8_t>& data) {
    if (data.size() < 20) return std::nullopt;
    size_t msg_len = (static_cast<size_t>(data[2]) << 8) | data[3];
    if (20 + msg_len > data.size()) return std::nullopt;
    size_t offset = 20;
    while (offset + 4 <= data.size()) {
        uint16_t attr_type = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
        uint16_t attr_len = (static_cast<uint16_t>(data[offset + 2]) << 8) | data[offset + 3];
        offset += 4;
        if (offset + attr_len > data.size()) break;
        if (attr_type == ATTR_XOR_MAPPED_ADDRESS && attr_len >= 8) {
            // XOR-MAPPED-ADDRESS: family(1) + port(2) + ip(4)
            uint8_t family = data[offset + 1];
            if (family != 0x01) { offset += attr_len; continue; } // Not IPv4
            uint16_t port = ((static_cast<uint16_t>(data[offset + 2]) << 8) |
                            static_cast<uint16_t>(data[offset + 3])) ^
                            static_cast<uint16_t>(MAGIC_COOKIE >> 16);
            uint32_t ip = ((static_cast<uint32_t>(data[offset + 4]) << 24) |
                          (static_cast<uint32_t>(data[offset + 5]) << 16) |
                          (static_cast<uint32_t>(data[offset + 6]) << 8) |
                          static_cast<uint32_t>(data[offset + 7])) ^ MAGIC_COOKIE;
            std::string ip_str = std::to_string((ip >> 24) & 0xFF) + "." +
                                 std::to_string((ip >> 16) & 0xFF) + "." +
                                 std::to_string((ip >> 8) & 0xFF) + "." +
                                 std::to_string(ip & 0xFF);
            return std::make_pair(ip_str, port);
        }
        if (attr_type == ATTR_MAPPED_ADDRESS && attr_len >= 8) {
            uint8_t family = data[offset + 1];
            if (family != 0x01) { offset += attr_len; continue; }
            uint16_t port = (static_cast<uint16_t>(data[offset + 2]) << 8) |
                            static_cast<uint16_t>(data[offset + 3]);
            uint32_t ip = (static_cast<uint32_t>(data[offset + 4]) << 24) |
                          (static_cast<uint32_t>(data[offset + 5]) << 16) |
                          (static_cast<uint32_t>(data[offset + 6]) << 8) |
                          static_cast<uint32_t>(data[offset + 7]);
            std::string ip_str = std::to_string((ip >> 24) & 0xFF) + "." +
                                 std::to_string((ip >> 16) & 0xFF) + "." +
                                 std::to_string((ip >> 8) & 0xFF) + "." +
                                 std::to_string(ip & 0xFF);
            return std::make_pair(ip_str, port);
        }
        offset += attr_len;
        // Align to 4-byte boundary
        if (attr_len % 4 != 0) offset += 4 - (attr_len % 4);
    }
    return std::nullopt;
}

} // namespace stun

// ============================================================================
// Forward declarations
// ============================================================================
struct RendezvousMediator::Impl;
struct RendezvousServer::Impl;
struct RelayServer::Impl;

// ============================================================================
// ConnectionManager: handles TCP connection with reconnection logic
// ============================================================================
class ConnectionManager {
public:
    ConnectionManager(asio::io_context& io_ctx)
        : io_ctx_(io_ctx), strand_(io_ctx), resolver_(io_ctx),
          reconnect_timer_(io_ctx) {}

    ~ConnectionManager() {
        disconnect();
    }

    void set_server_list(const std::vector<std::string>& servers) {
        servers_ = servers;
        server_idx_ = 0;
    }

    void set_port(uint16_t port) { port_ = port; }

    bool connect(std::function<void()> on_connected,
                 std::function<void()> on_disconnected,
                 std::function<void(const std::vector<uint8_t>&)> on_data) {
        on_connected_ = std::move(on_connected);
        on_disconnected_ = std::move(on_disconnected);
        on_data_ = std::move(on_data);
        return try_connect();
    }

    void disconnect() {
        connected_ = false;
        reconnect_attempts_ = 0;
        asio::error_code ec;
        reconnect_timer_.cancel(ec);
        if (socket_) {
            asio::error_code ec2;
            socket_->shutdown(asio::ip::tcp::socket::shutdown_both, ec2);
            socket_->close(ec2);
            socket_.reset();
        }
        write_queue_ = std::queue<std::vector<uint8_t>>();
    }

    bool send(const std::vector<uint8_t>& data) {
        if (!connected_ || !socket_) {
            spdlog::debug("ConnectionManager: cannot send, not connected");
            return false;
        }
        bool write_in_progress = !write_queue_.empty();
        write_queue_.push(data);
        if (!write_in_progress) {
            do_write();
        }
        return true;
    }

    bool is_connected() const { return connected_; }

    void failover() {
        spdlog::info("ConnectionManager: failing over to next server");
        disconnect();
        server_idx_ = (server_idx_ + 1) % servers_.size();
        try_connect();
    }

private:
    bool try_connect() {
        if (servers_.empty()) {
            spdlog::error("ConnectionManager: no servers configured");
            return false;
        }
        const auto& server = servers_[server_idx_];
        spdlog::info("ConnectionManager: connecting to {}:{}", server, port_);

        socket_ = std::make_unique<asio::ip::tcp::socket>(io_ctx_);

        // Resolve
        asio::error_code ec;
        auto endpoints = resolver_.resolve(
            server, std::to_string(port_),
            asio::ip::resolver_base::flags(), ec);
        if (ec) {
            spdlog::warn("ConnectionManager: resolve failed for {}:{} - {}",
                server, port_, ec.message());
            schedule_reconnect();
            return false;
        }

        // Connect
        asio::connect(*socket_, endpoints, ec);
        if (ec) {
            spdlog::warn("ConnectionManager: connect failed to {}:{} - {}",
                server, port_, ec.message());
            socket_.reset();
            schedule_reconnect();
            return false;
        }

        // Set TCP_NODELAY
        socket_->set_option(asio::ip::tcp::no_delay(true), ec);
        if (ec) spdlog::debug("Failed to set TCP_NODELAY: {}", ec.message());

        // Set keepalive
        asio::socket_base::keep_alive keep_alive(true);
        socket_->set_option(keep_alive, ec);
        if (ec) spdlog::debug("Failed to set SO_KEEPALIVE: {}", ec.message());

        connected_ = true;
        reconnect_attempts_ = 0;
        spdlog::info("ConnectionManager: connected to {}", server);

        if (on_connected_) on_connected_();

        start_read();
        return true;
    }

    void start_read() {
        auto buf = std::make_shared<std::array<uint8_t, protocol::HEADER_SIZE>>();
        socket_->async_read_some(
            asio::buffer(*buf, protocol::HEADER_SIZE),
            strand_.wrap([this, buf](asio::error_code ec, size_t bytes_read) {
                if (ec) {
                    handle_read_error(ec);
                    return;
                }
                if (bytes_read < protocol::HEADER_SIZE) {
                    spdlog::warn("Short header read: {} bytes", bytes_read);
                    return;
                }
                // Validate magic
                if ((*buf)[0] != protocol::MAGIC_NUMBER[0] ||
                    (*buf)[1] != protocol::MAGIC_NUMBER[1] ||
                    (*buf)[2] != protocol::MAGIC_NUMBER[2] ||
                    (*buf)[3] != protocol::MAGIC_NUMBER[3]) {
                    spdlog::warn("ConnectionManager: bad magic, re-syncing");
                    handle_read_error(asio::error::make_error_code(
                        asio::error::invalid_argument));
                    return;
                }
                uint32_t payload_len =
                    (static_cast<uint32_t>((*buf)[4]) << 24) |
                    (static_cast<uint32_t>((*buf)[5]) << 16) |
                    (static_cast<uint32_t>((*buf)[6]) << 8) |
                    static_cast<uint32_t>((*buf)[7]);
                if (payload_len > protocol::MAX_PAYLOAD_SIZE) {
                    spdlog::warn("ConnectionManager: payload too big: {}",
                        payload_len);
                    return;
                }
                uint32_t msg_type_raw =
                    (static_cast<uint32_t>((*buf)[8]) << 24) |
                    (static_cast<uint32_t>((*buf)[9]) << 16) |
                    (static_cast<uint32_t>((*buf)[10]) << 8) |
                    static_cast<uint32_t>((*buf)[11]);

                if (payload_len == 0) {
                    // No payload, deliver directly
                    std::vector<uint8_t> full_msg(protocol::HEADER_SIZE);
                    std::copy(buf->begin(), buf->begin() + protocol::HEADER_SIZE,
                              full_msg.begin());
                    if (on_data_) on_data_(full_msg);
                    start_read();
                } else {
                    // Read payload
                    auto payload_buf = std::make_shared<std::vector<uint8_t>>(payload_len);
                    socket_->async_read_some(
                        asio::buffer(payload_buf->data(), payload_len),
                        strand_.wrap([this, buf, payload_buf, msg_type_raw, payload_len](
                            asio::error_code ec, size_t br) mutable {
                            if (ec) {
                                handle_read_error(ec);
                                return;
                            }
                            if (br < payload_len) {
                                spdlog::warn("Short payload read: {} < {}", br, payload_len);
                                payload_buf->resize(br);
                                return;
                            }
                            std::vector<uint8_t> full_msg(protocol::HEADER_SIZE);
                            std::copy(buf->begin(), buf->begin() + protocol::HEADER_SIZE,
                                      full_msg.begin());
                            full_msg.insert(full_msg.end(),
                                payload_buf->begin(), payload_buf->end());
                            if (on_data_) on_data_(full_msg);
                            start_read();
                        }));
                }
            }));
    }

    void handle_read_error(const asio::error_code& ec) {
        if (ec == asio::error::operation_aborted) return;
        spdlog::warn("ConnectionManager: read error: {}", ec.message());
        connected_ = false;
        if (on_disconnected_) on_disconnected_();
        schedule_reconnect();
    }

    void do_write() {
        if (!connected_ || write_queue_.empty()) return;
        auto& data = write_queue_.front();
        socket_->async_write_some(
            asio::buffer(data.data(), data.size()),
            strand_.wrap([this](asio::error_code ec, size_t /*bytes*/) {
                if (ec) {
                    spdlog::warn("ConnectionManager: write error: {}", ec.message());
                    connected_ = false;
                    if (on_disconnected_) on_disconnected_();
                    schedule_reconnect();
                    return;
                }
                write_queue_.pop();
                if (!write_queue_.empty()) {
                    do_write();
                }
            }));
    }

    void schedule_reconnect() {
        if (reconnect_attempts_ > 10) {
            spdlog::error("ConnectionManager: max reconnect attempts reached, "
                "trying next server");
            server_idx_ = (server_idx_ + 1) % servers_.size();
            reconnect_attempts_ = 0;
        }
        reconnect_attempts_++;
        int delay_ms = std::min(1000 * reconnect_attempts_, 30000);
        spdlog::info("ConnectionManager: reconnecting in {}ms (attempt {})",
            delay_ms, reconnect_attempts_);
        reconnect_timer_.expires_after(std::chrono::milliseconds(delay_ms));
        reconnect_timer_.async_wait(
            strand_.wrap([this](asio::error_code ec) {
                if (ec == asio::error::operation_aborted) return;
                disconnect();
                try_connect();
            }));
    }

    asio::io_context& io_ctx_;
    asio::io_context::strand strand_;
    asio::ip::tcp::resolver resolver_;
    std::unique_ptr<asio::ip::tcp::socket> socket_;
    std::vector<std::string> servers_;
    size_t server_idx_ = 0;
    uint16_t port_ = common::RENDEZVOUS_PORT;
    std::atomic<bool> connected_{false};
    int reconnect_attempts_ = 0;
    asio::steady_timer reconnect_timer_;

    std::queue<std::vector<uint8_t>> write_queue_;

    std::function<void()> on_connected_;
    std::function<void()> on_disconnected_;
    std::function<void(const std::vector<uint8_t>&)> on_data_;
};

// ============================================================================
// UdpHolePuncher: handles UDP hole punching
// ============================================================================
class UdpHolePuncher {
public:
    struct PunchResult {
        bool success = false;
        std::string local_addr;
        uint16_t local_port = 0;
        std::string remote_addr;
        uint16_t remote_port = 0;
        std::shared_ptr<asio::ip::udp::socket> socket;
    };

    UdpHolePuncher(asio::io_context& io_ctx)
        : io_ctx_(io_ctx), strand_(io_ctx) {}

    void punch(const std::string& remote_ip, uint16_t remote_port,
               uint16_t local_port, int max_retries,
               std::function<void(PunchResult)> callback) {
        auto result = std::make_shared<PunchResult>();
        result->remote_addr = remote_ip;
        result->remote_port = remote_port;

        asio::error_code ec;
        auto socket = std::make_shared<asio::ip::udp::socket>(io_ctx_);
        asio::ip::udp::endpoint local_ep(asio::ip::udp::v4(), local_port);
        socket->open(asio::ip::udp::v4(), ec);
        if (ec) {
            spdlog::warn("UdpHolePuncher: failed to open socket: {}", ec.message());
            callback(*result);
            return;
        }
        // Allow reuse
        socket->set_option(asio::ip::udp::socket::reuse_address(true), ec);
        socket->bind(local_ep, ec);
        if (ec) {
            spdlog::warn("UdpHolePuncher: failed to bind: {}", ec.message());
            callback(*result);
            return;
        }

        auto local = socket->local_endpoint(ec);
        if (!ec) {
            result->local_port = local.port();
        }

        result->socket = socket;
        do_punch(result, socket, remote_ip, remote_port, 0, max_retries,
            std::move(callback));
    }

    void punch_ipv6(const std::string& remote_ip, uint16_t remote_port,
                    uint16_t local_port, int max_retries,
                    std::function<void(PunchResult)> callback) {
        auto result = std::make_shared<PunchResult>();
        result->remote_addr = remote_ip;
        result->remote_port = remote_port;

        asio::error_code ec;
        auto socket = std::make_shared<asio::ip::udp::socket>(io_ctx_);
        asio::ip::udp::endpoint local_ep(asio::ip::udp::v6(), local_port);
        socket->open(asio::ip::udp::v6(), ec);
        if (ec) {
            spdlog::warn("UdpHolePuncher IPv6: failed to open socket: {}",
                ec.message());
            callback(*result);
            return;
        }
        socket->set_option(asio::ip::udp::socket::reuse_address(true), ec);
        // IPv6 dual-stack
        asio::ip::v6_only v6_only(false);
        socket->set_option(v6_only, ec);
        socket->bind(local_ep, ec);
        if (ec) {
            spdlog::warn("UdpHolePuncher IPv6: failed to bind: {}", ec.message());
            callback(*result);
            return;
        }

        auto local = socket->local_endpoint(ec);
        if (!ec) {
            result->local_port = local.port();
        }

        result->socket = socket;
        do_punch_v6(result, socket, remote_ip, remote_port, 0, max_retries,
            std::move(callback));
    }

private:
    void do_punch(std::shared_ptr<PunchResult> result,
                  std::shared_ptr<asio::ip::udp::socket> socket,
                  const std::string& remote_ip, uint16_t remote_port,
                  int attempt, int max_retries,
                  std::function<void(PunchResult)> callback) {
        if (attempt >= max_retries) {
            spdlog::debug("UdpHolePuncher: max retries ({}) reached", max_retries);
            callback(*result);
            return;
        }

        asio::error_code ec;
        asio::ip::udp::resolver resolver(io_ctx_);
        auto endpoints = resolver.resolve(
            asio::ip::udp::v4(), remote_ip,
            std::to_string(remote_port), ec);
        if (ec) {
            spdlog::warn("UdpHolePuncher: resolve failed: {}", ec.message());
            callback(*result);
            return;
        }

        for (auto& ep : endpoints) {
            // Send a small "punch" packet
            std::vector<uint8_t> punch_pkt = {
                0x00, 0x00, 0x00, 0x00, // magic/type
                static_cast<uint8_t>((attempt >> 24) & 0xFF),
                static_cast<uint8_t>((attempt >> 16) & 0xFF),
                static_cast<uint8_t>((attempt >> 8) & 0xFF),
                static_cast<uint8_t>(attempt & 0xFF),
            };
            socket->send_to(asio::buffer(punch_pkt), ep, 0, ec);
            if (ec) {
                spdlog::debug("UdpHolePuncher: send failed: {}", ec.message());
                continue;
            }
            spdlog::debug("UdpHolePuncher: punch packet {} sent to {}:{}",
                attempt, ep.address().to_string(), ep.port());

            // Try to receive a response
            std::array<uint8_t, 1024> recv_buf;
            asio::ip::udp::endpoint sender_ep;
            socket->non_blocking(true);
            size_t recv_len = 0;
            for (int wait = 0; wait < 5; wait++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                recv_len = socket->receive_from(
                    asio::buffer(recv_buf), sender_ep, 0, ec);
                if (!ec && recv_len > 0) break;
                ec.clear();
            }
            socket->non_blocking(false);

            if (recv_len > 0) {
                spdlog::info("UdpHolePuncher: punch success! Received {} bytes from {}:{}",
                    recv_len, sender_ep.address().to_string(), sender_ep.port());
                result->success = true;
                result->remote_port = sender_ep.port();
                callback(*result);
                return;
            }
        }

        // Retry after a short delay
        auto timer = std::make_shared<asio::steady_timer>(io_ctx_);
        timer->expires_after(std::chrono::milliseconds(100 + attempt * 50));
        timer->async_wait(strand_.wrap(
            [this, result, socket, remote_ip, remote_port, attempt, max_retries, callback,
             timer](asio::error_code ec) mutable {
                if (ec) return;
                do_punch(result, socket, remote_ip, remote_port,
                         attempt + 1, max_retries, std::move(callback));
            }));
    }

    void do_punch_v6(std::shared_ptr<PunchResult> result,
                     std::shared_ptr<asio::ip::udp::socket> socket,
                     const std::string& remote_ip, uint16_t remote_port,
                     int attempt, int max_retries,
                     std::function<void(PunchResult)> callback) {
        if (attempt >= max_retries) {
            spdlog::debug("UdpHolePuncher IPv6: max retries ({}) reached",
                max_retries);
            callback(*result);
            return;
        }

        asio::error_code ec;
        asio::ip::udp::resolver resolver(io_ctx_);
        auto endpoints = resolver.resolve(
            asio::ip::udp::v6(), remote_ip,
            std::to_string(remote_port), ec);
        if (ec) {
            spdlog::warn("UdpHolePuncher IPv6: resolve failed: {}", ec.message());
            callback(*result);
            return;
        }

        for (auto& ep : endpoints) {
            std::vector<uint8_t> punch_pkt = {
                0x00, 0x00, 0x00, 0x00,
                static_cast<uint8_t>((attempt >> 24) & 0xFF),
                static_cast<uint8_t>((attempt >> 16) & 0xFF),
                static_cast<uint8_t>((attempt >> 8) & 0xFF),
                static_cast<uint8_t>(attempt & 0xFF),
            };
            socket->send_to(asio::buffer(punch_pkt), ep, 0, ec);
            if (ec) {
                spdlog::debug("UdpHolePuncher IPv6: send failed: {}", ec.message());
                continue;
            }

            std::array<uint8_t, 1024> recv_buf;
            asio::ip::udp::endpoint sender_ep;
            socket->non_blocking(true);
            size_t recv_len = 0;
            for (int wait = 0; wait < 5; wait++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                recv_len = socket->receive_from(
                    asio::buffer(recv_buf), sender_ep, 0, ec);
                if (!ec && recv_len > 0) break;
                ec.clear();
            }
            socket->non_blocking(false);

            if (recv_len > 0) {
                spdlog::info("UdpHolePuncher IPv6: success! {} bytes from {}:{}",
                    recv_len, sender_ep.address().to_string(), sender_ep.port());
                result->success = true;
                result->remote_port = sender_ep.port();
                callback(*result);
                return;
            }
        }

        auto timer = std::make_shared<asio::steady_timer>(io_ctx_);
        timer->expires_after(std::chrono::milliseconds(100 + attempt * 50));
        timer->async_wait(strand_.wrap(
            [this, result, socket, remote_ip, remote_port, attempt, max_retries, callback,
             timer](asio::error_code ec) mutable {
                if (ec) return;
                do_punch_v6(result, socket, remote_ip, remote_port,
                            attempt + 1, max_retries, std::move(callback));
            }));
    }

    asio::io_context& io_ctx_;
    asio::io_context::strand strand_;
};

// ============================================================================
// TcpFallbackConnector: establishes TCP connection as fallback
// ============================================================================
class TcpFallbackConnector {
public:
    struct TcpResult {
        bool success = false;
        std::string remote_addr;
        uint16_t remote_port = 0;
        std::shared_ptr<asio::ip::tcp::socket> socket;
    };

    TcpFallbackConnector(asio::io_context& io_ctx)
        : io_ctx_(io_ctx) {}

    void connect_tcp(const std::string& remote_ip, uint16_t remote_port,
                     std::function<void(TcpResult)> callback) {
        auto result = std::make_shared<TcpResult>();
        result->remote_addr = remote_ip;
        result->remote_port = remote_port;

        auto socket = std::make_shared<asio::ip::tcp::socket>(io_ctx_);

        asio::error_code ec;
        asio::ip::tcp::resolver resolver(io_ctx_);
        auto endpoints = resolver.resolve(remote_ip,
            std::to_string(remote_port), ec);
        if (ec) {
            spdlog::warn("TcpFallback: resolve failed: {}", ec.message());
            callback(*result);
            return;
        }

        asio::connect(*socket, endpoints, ec);
        if (ec) {
            spdlog::warn("TcpFallback: connect failed: {}", ec.message());
            callback(*result);
            return;
        }

        socket->set_option(asio::ip::tcp::no_delay(true), ec);

        result->success = true;
        result->socket = socket;
        spdlog::info("TcpFallback: connected to {}:{}", remote_ip, remote_port);
        callback(*result);
    }

    void connect_tcp_ipv6(const std::string& remote_ip, uint16_t remote_port,
                          std::function<void(TcpResult)> callback) {
        auto result = std::make_shared<TcpResult>();
        result->remote_addr = remote_ip;
        result->remote_port = remote_port;

        auto socket = std::make_shared<asio::ip::tcp::socket>(io_ctx_);

        asio::error_code ec;
        socket->open(asio::ip::tcp::v6(), ec);
        if (ec) {
            spdlog::warn("TcpFallback IPv6: open failed: {}", ec.message());
            callback(*result);
            return;
        }

        asio::ip::v6_only v6_only(false);
        socket->set_option(v6_only, ec);

        asio::ip::tcp::resolver resolver(io_ctx_);
        auto endpoints = resolver.resolve(
            asio::ip::tcp::v6(), remote_ip,
            std::to_string(remote_port), ec);
        if (ec) {
            spdlog::warn("TcpFallback IPv6: resolve failed: {}", ec.message());
            callback(*result);
            return;
        }

        asio::connect(*socket, endpoints, ec);
        if (ec) {
            spdlog::warn("TcpFallback IPv6: connect failed: {}", ec.message());
            callback(*result);
            return;
        }

        socket->set_option(asio::ip::tcp::no_delay(true), ec);
        result->success = true;
        result->socket = socket;
        spdlog::info("TcpFallback IPv6: connected to {}:{}", remote_ip, remote_port);
        callback(*result);
    }

private:
    asio::io_context& io_ctx_;
};

// ============================================================================
// NatTypeDetector: STUN-based NAT type detection
// ============================================================================
class NatTypeDetector {
public:
    NatTypeDetector(asio::io_context& io_ctx)
        : io_ctx_(io_ctx), strand_(io_ctx) {}

    void detect(const std::string& stun_server, uint16_t stun_port,
                std::function<void(NatType, const std::string&, uint16_t)> callback) {
        spdlog::info("NatTypeDetector: detecting NAT type via {}:{}",
            stun_server, stun_port);

        asio::error_code ec;
        socket_ = std::make_unique<asio::ip::udp::socket>(io_ctx_);

        // Open UDP socket
        socket_->open(asio::ip::udp::v4(), ec);
        if (ec) {
            spdlog::error("NatTypeDetector: cannot open UDP socket: {}", ec.message());
            callback(NatType::UNKNOWN_NAT, "", 0);
            return;
        }

        // Bind to a random port
        uint16_t local_port = random_port();
        asio::ip::udp::endpoint local_ep(asio::ip::udp::v4(), local_port);
        socket_->bind(local_ep, ec);
        if (ec) {
            // Try binding to port 0 (OS picks)
            local_ep.port(0);
            socket_->bind(local_ep, ec);
            if (ec) {
                spdlog::error("NatTypeDetector: cannot bind: {}", ec.message());
                callback(NatType::UNKNOWN_NAT, "", 0);
                return;
            }
        }

        auto bound_ep = socket_->local_endpoint(ec);
        local_port_ = bound_ep.port();

        // Resolve STUN server
        asio::ip::udp::resolver resolver(io_ctx_);
        auto endpoints = resolver.resolve(
            asio::ip::udp::v4(), stun_server,
            std::to_string(stun_port), ec);
        if (ec) {
            spdlog::error("NatTypeDetector: cannot resolve STUN server: {}", ec.message());
            callback(NatType::UNKNOWN_NAT, "", local_port_);
            return;
        }
        stun_ep_ = *endpoints.begin();

        // Step 1: Send BINDING_REQUEST to detect mapped address
        step1_binding_request(callback);
    }

private:
    void step1_binding_request(std::function<void(NatType, const std::string&, uint16_t)> callback) {
        auto req = stun::build_stun_binding_request(false, false);
        asio::error_code ec;
        socket_->send_to(asio::buffer(req), stun_ep_, 0, ec);
        if (ec) {
            spdlog::error("NatTypeDetector: step1 send failed: {}", ec.message());
            callback(NatType::UNKNOWN_NAT, "", local_port_);
            return;
        }

        recv_buffer_.fill(0);
        auto timer = std::make_shared<asio::steady_timer>(io_ctx_);
        timer->expires_after(std::chrono::seconds(3));
        auto buf_ptr = std::make_shared<std::array<uint8_t, 2048>>();

        socket_->async_receive_from(
            asio::buffer(*buf_ptr), recv_ep_,
            strand_.wrap([this, callback, timer, buf_ptr](
                asio::error_code ec, size_t bytes) {
                if (ec) {
                    spdlog::warn("NatTypeDetector: step1 recv failed: {} - "
                        "no response, likely UDP blocked", ec.message());
                    callback(NatType::UNKNOWN_NAT, "", local_port_);
                    return;
                }
                std::vector<uint8_t> data(buf_ptr->begin(), buf_ptr->begin() + bytes);
                if (!stun::is_stun_response(data)) {
                    spdlog::warn("NatTypeDetector: step1 not a STUN response");
                    callback(NatType::UNKNOWN_NAT, "", local_port_);
                    return;
                }

                auto mapped = stun::parse_stun_mapped_address(data);
                if (!mapped) {
                    spdlog::warn("NatTypeDetector: step1 no mapped address in response");
                    callback(NatType::UNKNOWN_NAT, "", local_port_);
                    return;
                }

                mapped_ip_ = mapped->first;
                mapped_port_ = mapped->second;
                spdlog::info("NatTypeDetector: mapped address = {}:{}",
                    mapped_ip_, mapped_port_);

                // Step 2: Test if we can receive from a different port
                step2_test_change_port(callback);
            }));
    }

    void step2_test_change_port(std::function<void(NatType, const std::string&, uint16_t)> callback) {
        auto req = stun::build_stun_binding_request(false, true);

        asio::error_code ec;
        socket_->send_to(asio::buffer(req), stun_ep_, 0, ec);
        if (ec) {
            spdlog::error("NatTypeDetector: step2 send failed: {}", ec.message());
            // Fallback: we have a response, can't test further
            if (mapped_port_ == local_port_ && mapped_ip_ == local_ip_) {
                callback(NatType::OPEN_INTERNET, mapped_ip_, mapped_port_);
            } else {
                callback(NatType::FULL_CONE, mapped_ip_, mapped_port_);
            }
            return;
        }

        auto buf_ptr = std::make_shared<std::array<uint8_t, 2048>>();
        socket_->async_receive_from(
            asio::buffer(*buf_ptr), recv_ep_,
            strand_.wrap([this, callback, buf_ptr](
                asio::error_code ec, size_t bytes) {
                if (ec) {
                    // No response from different port -> restricted or symmetric
                    spdlog::info("NatTypeDetector: step2 no response, "
                        "testing change IP+port");
                    step3_test_change_ip_port(callback);
                    return;
                }

                std::vector<uint8_t> data(buf_ptr->begin(), buf_ptr->begin() + bytes);
                auto mapped2 = stun::parse_stun_mapped_address(data);
                if (mapped2 && mapped2->second == mapped_port_) {
                    // Same mapped port from different source port -> Full Cone
                    spdlog::info("NatTypeDetector: FULL_CONE (same mapping from "
                        "different source port)");
                    callback(NatType::FULL_CONE, mapped_ip_, mapped_port_);
                } else if (mapped2) {
                    spdlog::info("NatTypeDetector: RESTRICTED_CONE "
                        "(different port mapping)");
                    callback(NatType::RESTRICTED_CONE, mapped_ip_, mapped_port_);
                } else {
                    spdlog::info("NatTypeDetector: RESTRICTED_CONE");
                    callback(NatType::RESTRICTED_CONE, mapped_ip_, mapped_port_);
                }
            }));
    }

    void step3_test_change_ip_port(std::function<void(NatType, const std::string&, uint16_t)> callback) {
        auto req = stun::build_stun_binding_request(true, true);

        asio::error_code ec;
        socket_->send_to(asio::buffer(req), stun_ep_, 0, ec);
        if (ec) {
            spdlog::error("NatTypeDetector: step3 send failed: {}", ec.message());
            callback(NatType::PORT_RESTRICTED_CONE, mapped_ip_, mapped_port_);
            return;
        }

        auto buf_ptr = std::make_shared<std::array<uint8_t, 2048>>();
        socket_->async_receive_from(
            asio::buffer(*buf_ptr), recv_ep_,
            strand_.wrap([this, callback, buf_ptr](
                asio::error_code ec, size_t bytes) {
                if (ec) {
                    // No response from different IP+port -> symmetric
                    spdlog::info("NatTypeDetector: SYMMETRIC (no response from "
                        "different IP+port)");
                    callback(NatType::SYMMETRIC, mapped_ip_, mapped_port_);
                    return;
                }

                std::vector<uint8_t> data(buf_ptr->begin(), buf_ptr->begin() + bytes);
                auto mapped2 = stun::parse_stun_mapped_address(data);
                if (mapped2 && mapped2->second != mapped_port_) {
                    // Different mapping -> symmetric
                    spdlog::info("NatTypeDetector: SYMMETRIC (different mapping: "
                        "{} -> {})", mapped_port_, mapped2->second);
                    callback(NatType::SYMMETRIC, mapped_ip_, mapped_port_);
                } else {
                    spdlog::info("NatTypeDetector: PORT_RESTRICTED_CONE");
                    callback(NatType::PORT_RESTRICTED_CONE, mapped_ip_, mapped_port_);
                }
            }));
    }

    asio::io_context& io_ctx_;
    asio::io_context::strand strand_;
    std::unique_ptr<asio::ip::udp::socket> socket_;
    asio::ip::udp::endpoint stun_ep_;
    asio::ip::udp::endpoint recv_ep_;
    std::string mapped_ip_;
    uint16_t mapped_port_ = 0;
    std::string local_ip_;
    uint16_t local_port_ = 0;
    std::array<uint8_t, 2048> recv_buffer_;
};

// ============================================================================
// PeerRegistry: tracks peer online/offline state
// ============================================================================
class PeerRegistry {
public:
    struct PeerEntry {
        std::string id;
        std::string pk;
        std::string ip;
        uint16_t port = 0;
        uint16_t udp_port = 0;
        std::string alias;
        std::string hostname;
        std::string platform;
        std::string version;
        bool online = false;
        NatType nat_type = NatType::UNKNOWN_NAT;
        std::chrono::steady_clock::time_point last_seen;
        std::chrono::steady_clock::time_point last_registered;
        uint32_t connectivity_flags = 0; // bit 0: ipv4, bit 1: ipv6, bit 2: relay
    };

    void add_or_update(const std::string& id, const PeerEntry& entry) {
        std::lock_guard lk(mutex_);
        auto it = peers_.find(id);
        if (it != peers_.end()) {
            bool was_online = it->second.online;
            it->second = entry;
            it->second.online = true;
            it->second.last_seen = std::chrono::steady_clock::now();
            if (!was_online && on_peer_online_) {
                pending_callbacks_.push([this, id]() {
                    if (on_peer_online_) on_peer_online_(id, true);
                });
            }
        } else {
            peers_[id] = entry;
            peers_[id].online = true;
            peers_[id].last_seen = std::chrono::steady_clock::now();
            if (on_peer_online_) {
                pending_callbacks_.push([this, id]() {
                    if (on_peer_online_) on_peer_online_(id, true);
                });
            }
        }
    }

    void update_online(const std::string& id, bool online, const std::string& ip = "",
                       uint16_t port = 0) {
        std::lock_guard lk(mutex_);
        auto it = peers_.find(id);
        if (it != peers_.end()) {
            bool was_online = it->second.online;
            it->second.online = online;
            it->second.last_seen = std::chrono::steady_clock::now();
            if (!ip.empty()) it->second.ip = ip;
            if (port > 0) it->second.port = port;
            if (was_online != online && on_peer_online_) {
                pending_callbacks_.push([this, id, online]() {
                    if (on_peer_online_) on_peer_online_(id, online);
                });
            }
        }
        // Expire old entries
        expire_stale_peers();
    }

    void remove(const std::string& id) {
        std::lock_guard lk(mutex_);
        auto it = peers_.find(id);
        if (it != peers_.end() && it->second.online) {
            if (on_peer_online_) {
                pending_callbacks_.push([this, id]() {
                    if (on_peer_online_) on_peer_online_(id, false);
                });
            }
        }
        peers_.erase(id);
    }

    std::optional<PeerEntry> get(const std::string& id) const {
        std::lock_guard lk(mutex_);
        auto it = peers_.find(id);
        if (it == peers_.end()) return std::nullopt;
        return it->second;
    }

    bool is_online(const std::string& id) const {
        std::lock_guard lk(mutex_);
        auto it = peers_.find(id);
        return it != peers_.end() && it->second.online;
    }

    std::vector<PeerEntry> get_online_peers() const {
        std::lock_guard lk(mutex_);
        std::vector<PeerEntry> result;
        for (auto& [id, entry] : peers_) {
            if (entry.online) result.push_back(entry);
        }
        return result;
    }

    std::vector<std::string> get_online_ids() const {
        std::lock_guard lk(mutex_);
        std::vector<std::string> result;
        for (auto& [id, entry] : peers_) {
            if (entry.online) result.push_back(id);
        }
        return result;
    }

    void update_nat_type(const std::string& id, NatType nat_type) {
        std::lock_guard lk(mutex_);
        auto it = peers_.find(id);
        if (it != peers_.end()) {
            it->second.nat_type = nat_type;
        }
    }

    NatType get_nat_type(const std::string& id) const {
        std::lock_guard lk(mutex_);
        auto it = peers_.find(id);
        return (it != peers_.end()) ? it->second.nat_type : NatType::UNKNOWN_NAT;
    }

    void set_on_peer_online(std::function<void(const std::string&, bool)> cb) {
        on_peer_online_ = std::move(cb);
    }

    void process_callbacks() {
        std::function<void()> cb;
        while (pending_callbacks_.try_pop(cb)) {
            cb();
        }
    }

    void check_timeout(std::chrono::seconds timeout) {
        std::lock_guard lk(mutex_);
        auto now = std::chrono::steady_clock::now();
        std::vector<std::string> expired;
        for (auto& [id, entry] : peers_) {
            if (entry.online && (now - entry.last_seen) > timeout) {
                entry.online = false;
                expired.push_back(id);
            }
        }
        for (auto& id : expired) {
            if (on_peer_online_) {
                pending_callbacks_.push([this, id]() {
                    if (on_peer_online_) on_peer_online_(id, false);
                });
            }
        }
    }

private:
    void expire_stale_peers() {
        auto now = std::chrono::steady_clock::now();
        auto it = peers_.begin();
        while (it != peers_.end()) {
            if (!it->second.online &&
                (now - it->second.last_seen) > std::chrono::minutes(30)) {
                it = peers_.erase(it);
            } else {
                ++it;
            }
        }
    }

    mutable std::mutex mutex_;
    std::map<std::string, PeerEntry> peers_;
    std::function<void(const std::string&, bool)> on_peer_online_;
    common::ConcurrentQueue<std::function<void()>> pending_callbacks_;
};

// ============================================================================
// SoftwareUpdateHandler: manages software update notifications
// ============================================================================
class SoftwareUpdateHandler {
public:
    struct UpdateInfo {
        std::string version;
        std::string url;
        std::string changelog;
        bool mandatory = false;
        std::chrono::system_clock::time_point received_at;
    };

    void parse_update_message(const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        size_t offset = 0;
        UpdateInfo info;
        info.version = read_string(payload.data(), payload.size(), offset);
        info.url = read_string(payload.data(), payload.size(), offset);
        if (offset < payload.size()) {
            info.changelog = read_string(payload.data(), payload.size(), offset);
        }
        if (offset < payload.size()) {
            info.mandatory = (payload[offset] != 0);
        }
        info.received_at = std::chrono::system_clock::now();

        {
            std::lock_guard lk(mutex_);
            latest_update_ = info;
        }

        spdlog::info("SoftwareUpdateHandler: version={} url={} mandatory={}",
            info.version, info.url, info.mandatory);

        if (callback_) {
            callback_(info.version, info.url);
        }
    }

    std::optional<UpdateInfo> get_latest() const {
        std::lock_guard lk(mutex_);
        return latest_update_;
    }

    void set_callback(std::function<void(const std::string&, const std::string&)> cb) {
        callback_ = std::move(cb);
    }

    bool is_mandatory_update_available(const std::string& current_version) const {
        std::lock_guard lk(mutex_);
        if (!latest_update_) return false;
        if (!latest_update_->mandatory) return false;
        // Simple version comparison
        return latest_update_->version != current_version;
    }

private:
    mutable std::mutex mutex_;
    std::optional<UpdateInfo> latest_update_;
    std::function<void(const std::string&, const std::string&)> callback_;
};

// ============================================================================
// KeepAliveManager: periodic heartbeat and re-registration
// ============================================================================
class KeepAliveManager {
public:
    KeepAliveManager(asio::io_context& io_ctx)
        : timer_(io_ctx), strand_(io_ctx) {}

    void start(int32_t interval_ms,
               std::function<void()> on_heartbeat,
               std::function<void()> on_reregister) {
        interval_ms_ = interval_ms;
        on_heartbeat_ = std::move(on_heartbeat);
        on_reregister_ = std::move(on_reregister);
        running_ = true;
        heartbeat_count_ = 0;
        schedule_next();
    }

    void stop() {
        running_ = false;
        asio::error_code ec;
        timer_.cancel(ec);
    }

    void reset() {
        heartbeat_count_ = 0;
        schedule_next();
    }

private:
    void schedule_next() {
        if (!running_) return;
        timer_.expires_after(std::chrono::milliseconds(interval_ms_));
        timer_.async_wait(strand_.wrap([this](asio::error_code ec) {
            if (ec || !running_) return;
            heartbeat_count_++;

            // Send heartbeat
            if (on_heartbeat_) on_heartbeat_();

            // Every 6 heartbeats (or ~60s with default 10s interval),
            // do a full re-registration
            if (heartbeat_count_ % 6 == 0 && on_reregister_) {
                spdlog::debug("KeepAliveManager: periodic re-registration");
                on_reregister_();
            }

            schedule_next();
        }));
    }

    asio::steady_timer timer_;
    asio::io_context::strand strand_;
    int32_t interval_ms_ = 10000;
    int heartbeat_count_ = 0;
    std::atomic<bool> running_{false};
    std::function<void()> on_heartbeat_;
    std::function<void()> on_reregister_;
};

// ============================================================================
// PendingRequest: tracks in-flight requests with timeout
// ============================================================================
struct PendingRequest {
    uint32_t request_id;
    uint32_t msg_type;
    std::chrono::steady_clock::time_point sent_at;
    std::function<void(bool, const std::vector<uint8_t>&)> callback;
    size_t retry_count = 0;
    static constexpr size_t MAX_RETRIES = 3;
    static constexpr auto TIMEOUT = std::chrono::seconds(10);
};

// ============================================================================
// RequestTracker: manages pending requests with retries
// ============================================================================
class RequestTracker {
public:
    RequestTracker(asio::io_context& io_ctx, asio::io_context::strand& strand)
        : io_ctx_(io_ctx), strand_(strand), check_timer_(io_ctx) {
        next_req_id_ = static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFFFF);
    }

    uint32_t register_request(uint32_t msg_type,
        std::function<void(bool, const std::vector<uint8_t>&)> callback) {
        uint32_t req_id = next_req_id_++;
        PendingRequest req;
        req.request_id = req_id;
        req.msg_type = msg_type;
        req.sent_at = std::chrono::steady_clock::now();
        req.callback = std::move(callback);
        std::lock_guard lk(mutex_);
        pending_[req_id] = std::move(req);
        start_timeout_check();
        return req_id;
    }

    void complete_request(uint32_t req_id, bool success,
                          const std::vector<uint8_t>& data) {
        PendingRequest req;
        {
            std::lock_guard lk(mutex_);
            auto it = pending_.find(req_id);
            if (it == pending_.end()) return;
            req = std::move(it->second);
            pending_.erase(it);
        }
        if (req.callback) {
            req.callback(success, data);
        }
    }

    void retry_request(uint32_t req_id) {
        std::lock_guard lk(mutex_);
        auto it = pending_.find(req_id);
        if (it != pending_.end()) {
            it->second.retry_count++;
            it->second.sent_at = std::chrono::steady_clock::now();
        }
    }

    bool should_retry(uint32_t req_id) const {
        std::lock_guard lk(mutex_);
        auto it = pending_.find(req_id);
        return it != pending_.end() &&
               it->second.retry_count < PendingRequest::MAX_RETRIES;
    }

    void check_timeouts() {
        auto now = std::chrono::steady_clock::now();
        std::vector<uint32_t> timed_out;
        std::vector<uint32_t> to_retry;
        {
            std::lock_guard lk(mutex_);
            for (auto& [id, req] : pending_) {
                if ((now - req.sent_at) > PendingRequest::TIMEOUT) {
                    if (req.retry_count < PendingRequest::MAX_RETRIES) {
                        to_retry.push_back(id);
                        req.retry_count++;
                        req.sent_at = now;
                    } else {
                        timed_out.push_back(id);
                    }
                }
            }
        }
        for (auto id : timed_out) {
            complete_request(id, false, {});
        }
        // Notify caller about retries
        if (!to_retry.empty() && on_retry_) {
            for (auto id : to_retry) {
                on_retry_(id);
            }
        }
    }

    void set_on_retry(std::function<void(uint32_t)> cb) {
        on_retry_ = std::move(cb);
    }

    void start_timeout_check() {
        if (timeout_check_running_) return;
        timeout_check_running_ = true;
        do_timeout_check();
    }

private:
    void do_timeout_check() {
        check_timer_.expires_after(std::chrono::seconds(5));
        check_timer_.async_wait(strand_.wrap([this](asio::error_code ec) {
            if (ec) {
                timeout_check_running_ = false;
                return;
            }
            check_timeouts();
            if (timeout_check_running_) {
                do_timeout_check();
            }
        }));
    }

    asio::io_context& io_ctx_;
    asio::io_context::strand& strand_;
    asio::steady_timer check_timer_;
    std::atomic<bool> timeout_check_running_{false};
    std::map<uint32_t, PendingRequest> pending_;
    mutable std::mutex mutex_;
    uint32_t next_req_id_ = 0;
    std::function<void(uint32_t)> on_retry_;
};

// ============================================================================
// MessageDispatcher: routes incoming messages to appropriate handlers
// ============================================================================
class MessageDispatcher {
public:
    using Handler = std::function<void(const std::vector<uint8_t>&)>;

    void register_handler(uint32_t msg_type, Handler handler) {
        std::lock_guard lk(mutex_);
        handlers_[msg_type] = std::move(handler);
    }

    void dispatch(uint32_t msg_type, const std::vector<uint8_t>& payload) {
        Handler handler;
        {
            std::lock_guard lk(mutex_);
            auto it = handlers_.find(msg_type);
            if (it == handlers_.end()) {
                spdlog::debug("MessageDispatcher: unhandled message type {}",
                    msg_type);
                return;
            }
            handler = it->second;
        }
        spdlog::debug("MessageDispatcher: dispatching {} ({})",
            msg_type_name(msg_type), msg_type);
        if (handler) handler(payload);
    }

    void clear() {
        std::lock_guard lk(mutex_);
        handlers_.clear();
    }

private:
    std::mutex mutex_;
    std::map<uint32_t, Handler> handlers_;
};

// ============================================================================
// RendezvousMediator::Impl — primary implementation
// ============================================================================
struct RendezvousMediator::Impl {
    std::string server_addr;
    std::string host;
    std::string host_prefix;
    int32_t keep_alive_ms;

    // ASIO
    asio::io_context io_ctx;
    std::unique_ptr<asio::io_context::work> work;
    std::thread io_thread;
    asio::io_context::strand strand;

    // Connection
    std::unique_ptr<ConnectionManager> connection;
    bool use_ipv6 = false;
    std::vector<std::string> failover_servers;

    // State
    std::atomic<bool> running{false};
    std::atomic<bool> registered{false};
    std::string peer_id;
    std::string peer_password;
    std::string peer_pk;
    std::string peer_token;
    std::string peer_alias;
    int32_t nat_type = static_cast<int32_t>(NatType::UNKNOWN_NAT);
    uint16_t udp_port = 0;
    std::string mapped_ip;
    uint16_t mapped_port = 0;

    // Managers
    std::unique_ptr<KeepAliveManager> keep_alive;
    std::unique_ptr<UdpHolePuncher> hole_puncher;
    std::unique_ptr<TcpFallbackConnector> tcp_fallback;
    std::unique_ptr<NatTypeDetector> nat_detector;
    std::unique_ptr<PeerRegistry> peer_registry;
    std::unique_ptr<SoftwareUpdateHandler> update_handler;
    std::unique_ptr<MessageDispatcher> dispatcher;
    std::unique_ptr<RequestTracker> request_tracker;

    // Callbacks
    OnMessage on_message;
    OnPeerOnline on_peer_online;
    OnSoftwareUpdate on_software_update;

    // Pending punch hole requests
    struct PendingPunch {
        std::string peer_id;
        std::string key;
        std::string token;
        ConnType conn_type;
        bool force_relay;
    };
    std::map<uint32_t, PendingPunch> pending_punches;
    std::mutex punches_mutex;

    // Relay state
    struct RelayConnection {
        std::string uuid;
        std::string licence_key;
        std::string relay_server;
        uint16_t relay_port = common::RELAY_PORT;
        std::shared_ptr<asio::ip::tcp::socket> socket;
        std::atomic<bool> active{false};
    };
    std::map<std::string, std::shared_ptr<RelayConnection>> relay_connections;
    std::mutex relay_mutex;

    // Re-registration timer
    asio::steady_timer reregister_timer;

    Impl(const std::string& server_addr_val,
         const std::string& host_val,
         const std::string& host_prefix_val,
         int32_t keep_alive_ms_val)
        : server_addr(server_addr_val)
        , host(host_val)
        , host_prefix(host_prefix_val)
        , keep_alive_ms(keep_alive_ms_val)
        , strand(io_ctx)
        , reregister_timer(io_ctx) {
        connection = std::make_unique<ConnectionManager>(io_ctx);
        keep_alive = std::make_unique<KeepAliveManager>(io_ctx);
        hole_puncher = std::make_unique<UdpHolePuncher>(io_ctx);
        tcp_fallback = std::make_unique<TcpFallbackConnector>(io_ctx);
        nat_detector = std::make_unique<NatTypeDetector>(io_ctx);
        peer_registry = std::make_unique<PeerRegistry>();
        update_handler = std::make_unique<SoftwareUpdateHandler>();
        dispatcher = std::make_unique<MessageDispatcher>();
        request_tracker = std::make_unique<RequestTracker>(io_ctx, strand);

        setup_dispatcher();
        setup_peer_events();
    }

    ~Impl() {
        stop();
    }

    // ========================================================================
    // Setup
    // ========================================================================
    void setup_dispatcher() {
        dispatcher->register_handler(protocol::WIRE_PUNCH_HOLE_RESP,
            [this](const std::vector<uint8_t>& payload) {
                handle_punch_hole_response(payload);
            });
        dispatcher->register_handler(protocol::WIRE_PK_RESPONSE,
            [this](const std::vector<uint8_t>& payload) {
                handle_pk_response(payload);
            });
        dispatcher->register_handler(protocol::WIRE_CONFIG_RESPONSE,
            [this](const std::vector<uint8_t>& payload) {
                handle_config_response(payload);
            });
        dispatcher->register_handler(protocol::WIRE_SOFTWARE_UPDATE,
            [this](const std::vector<uint8_t>& payload) {
                handle_software_update(payload);
            });
        dispatcher->register_handler(protocol::WIRE_QUERY_ONLINE,
            [this](const std::vector<uint8_t>& payload) {
                handle_query_online_response(payload);
            });
        dispatcher->register_handler(protocol::WIRE_ADDRESS_BOOK,
            [this](const std::vector<uint8_t>& payload) {
                handle_address_book(payload);
            });
        dispatcher->register_handler(protocol::WIRE_HEARTBEAT,
            [this](const std::vector<uint8_t>& payload) {
                handle_heartbeat_response(payload);
            });
    }

    void setup_peer_events() {
        peer_registry->set_on_peer_online(
            [this](const std::string& id, bool online) {
                spdlog::info("Peer {} is now {}", id, online ? "ONLINE" : "OFFLINE");
                if (on_peer_online) {
                    // Post to strand to avoid callback re-entrance issues
                    asio::post(strand, [this, id, online]() {
                        if (on_peer_online) on_peer_online(id, online);
                    });
                }
            });

        update_handler->set_callback(
            [this](const std::string& version, const std::string& url) {
                if (on_software_update) {
                    asio::post(strand, [this, version, url]() {
                        if (on_software_update) on_software_update(version, url);
                    });
                }
            });

        request_tracker->set_on_retry([this](uint32_t req_id) {
            spdlog::debug("Retrying request {}", req_id);
            // Resend logic handled by caller
        });
    }

    // ========================================================================
    // Connection management
    // ========================================================================
    bool connect() {
        if (running) return true;

        // Configure server list for failover
        std::vector<std::string> servers;
        if (!server_addr.empty()) {
            servers.push_back(server_addr);
        }
        // Add default servers for failover
        for (auto& s : common::RENDEZVOUS_SERVERS) {
            if (std::find(servers.begin(), servers.end(), s) == servers.end()) {
                servers.push_back(s);
            }
        }
        failover_servers = servers;
        connection->set_server_list(servers);
        connection->set_port(common::RENDEZVOUS_PORT);

        bool ok = connection->connect(
            [this]() {
                spdlog::info("RendezvousMediator: connected to server");
                running = true;
                if (registered) {
                    // Re-register after reconnect
                    do_register();
                }
            },
            [this]() {
                spdlog::warn("RendezvousMediator: disconnected from server");
                running = false;
            },
            [this](const std::vector<uint8_t>& data) {
                handle_incoming(data);
            });

        if (ok) {
            // Start IO thread
            work = std::make_unique<asio::io_context::work>(io_ctx);
            io_thread = std::thread([this]() {
                spdlog::debug("RendezvousMediator: IO thread started");
                while (running || work) {
                    try {
                        io_ctx.run();
                    } catch (const std::exception& e) {
                        spdlog::error("RendezvousMediator: IO exception: {}", e.what());
                    }
                    if (!running && !work) break;
                    io_ctx.restart();
                }
                spdlog::debug("RendezvousMediator: IO thread stopped");
            });
        }

        return ok;
    }

    // ========================================================================
    // Incoming message handling
    // ========================================================================
    void handle_incoming(const std::vector<uint8_t>& data) {
        auto parsed = parse_wire_message(data);
        if (!parsed) return;

        auto& [msg_type, payload] = *parsed;
        spdlog::debug("RendezvousMediator: received {} ({} bytes)",
            msg_type_name(msg_type), payload.size());

        // Notify callback
        if (on_message) {
            auto rd_type = wire_type_to_msg(msg_type);
            asio::post(strand, [this, rd_type, payload]() {
                if (on_message) on_message(rd_type, payload);
            });
        }

        // Dispatch to registered handler
        dispatcher->dispatch(msg_type, payload);
    }

    // ========================================================================
    // Registration
    // ========================================================================
    void do_register(const std::string& id, const std::string& password) {
        peer_id = id;
        peer_password = password;

        auto payload = build_register_payload(
            id, password,
            common::get_hostname(),
            common::get_platform_name(),
            udp_port,
            static_cast<uint32_t>(nat_type));

        auto msg = serialize_message(protocol::WIRE_REGISTER_PEER, payload);
        connection->send(msg);

        spdlog::info("RendezvousMediator: registration sent for {}", id);
    }

    void do_register() {
        if (peer_id.empty() || peer_password.empty()) {
            spdlog::warn("RendezvousMediator: cannot re-register without credentials");
            return;
        }
        do_register(peer_id, peer_password);
    }

    void do_register_pk(const std::string& pk, const std::string& token) {
        peer_pk = pk;
        peer_token = token;

        auto payload = build_pk_register_payload(pk, token, peer_id, "");
        auto msg = serialize_message(protocol::WIRE_REGISTER_PK, payload);
        connection->send(msg);

        spdlog::info("RendezvousMediator: PK registration sent");
    }

    // ========================================================================
    // Punch hole
    // ========================================================================
    void do_punch_hole(const std::string& peer_id, const std::string& key,
                       const std::string& token, ConnType conn_type,
                       bool force_relay) {
        auto payload = build_punch_hole_payload(
            peer_id, key, token, static_cast<uint32_t>(conn_type),
            force_relay, this->peer_id);

        auto req_id = request_tracker->register_request(
            protocol::WIRE_PUNCH_HOLE_REQ,
            [this, peer_id, conn_type](bool success, const std::vector<uint8_t>& data) {
                if (!success) {
                    spdlog::warn("Punch hole request timed out for {}", peer_id);
                    // Fall back to relay
                    fallback_to_relay(peer_id);
                }
                // Response handled by handle_punch_hole_response
            });

        auto msg = serialize_message(protocol::WIRE_PUNCH_HOLE_REQ, payload);
        connection->send(msg);

        {
            std::lock_guard lk(punches_mutex);
            pending_punches[req_id] = {peer_id, key, token, conn_type, force_relay};
        }

        spdlog::info("RendezvousMediator: punch hole request sent for {} (req_id={})",
            peer_id, req_id);
    }

    void handle_punch_hole_response(const std::vector<uint8_t>& payload) {
        // Parse the response
        if (payload.size() < 2) return;
        size_t offset = 0;
        std::string peer_id = read_string(payload.data(), payload.size(), offset);
        std::string peer_ip = read_string(payload.data(), payload.size(), offset);
        uint16_t peer_udp_port = static_cast<uint16_t>(
            read_u32(payload.data(), offset));
        uint16_t peer_tcp_port = static_cast<uint16_t>(
            read_u32(payload.data(), offset));
        uint32_t peer_nat_raw = read_u32(payload.data(), offset);
        bool success = (offset < payload.size()) ? (payload[offset] != 0) : true;

        NatType peer_nat = NatType::UNKNOWN_NAT;
        if (peer_nat_raw <= static_cast<uint32_t>(NatType::SYMMETRIC)) {
            peer_nat = static_cast<NatType>(peer_nat_raw);
        }

        spdlog::info("Punch hole response: peer={} ip={} udp={} tcp={} nat={} "
            "success={}", peer_id, peer_ip, peer_udp_port, peer_tcp_port,
            nat_type_name(peer_nat), success);

        if (!success) {
            spdlog::warn("Punch hole failed for {}", peer_id);
            fallback_to_relay(peer_id);
            return;
        }

        // Try UDP hole punching first
        bool try_udp = (static_cast<NatType>(nat_type.load()) != NatType::SYMMETRIC ||
                        peer_nat != NatType::SYMMETRIC);

        if (try_udp && peer_udp_port > 0) {
            if (use_ipv6) {
                hole_puncher->punch_ipv6(peer_ip, peer_udp_port,
                    udp_port > 0 ? udp_port : random_port(), 10,
                    [this, peer_id, peer_ip, peer_tcp_port](UdpHolePuncher::PunchResult r) {
                        if (r.success) {
                            spdlog::info("UDP hole punch SUCCESS to {}", peer_id);
                            // Notify via message callback
                            notify_punch_result(peer_id, true, r);
                        } else {
                            spdlog::warn("UDP hole punch FAILED to {}, trying TCP",
                                peer_id);
                            try_tcp_fallback(peer_id, peer_ip, peer_tcp_port);
                        }
                    });
            } else {
                hole_puncher->punch(peer_ip, peer_udp_port,
                    udp_port > 0 ? udp_port : random_port(), 10,
                    [this, peer_id, peer_ip, peer_tcp_port](UdpHolePuncher::PunchResult r) {
                        if (r.success) {
                            spdlog::info("UDP hole punch SUCCESS to {}", peer_id);
                            notify_punch_result(peer_id, true, r);
                        } else {
                            spdlog::warn("UDP hole punch FAILED to {}, trying TCP",
                                peer_id);
                            try_tcp_fallback(peer_id, peer_ip, peer_tcp_port);
                        }
                    });
            }
        } else {
            // Symmetric NAT on both sides — go straight to relay
            spdlog::info("Symmetric NAT on both sides for {}, using relay", peer_id);
            fallback_to_relay(peer_id);
        }
    }

    void try_tcp_fallback(const std::string& peer_id, const std::string& peer_ip,
                          uint16_t peer_tcp_port) {
        if (peer_tcp_port == 0) {
            spdlog::warn("No TCP port available for {}, falling back to relay", peer_id);
            fallback_to_relay(peer_id);
            return;
        }

        if (use_ipv6) {
            tcp_fallback->connect_tcp_ipv6(peer_ip, peer_tcp_port,
                [this, peer_id](TcpFallbackConnector::TcpResult r) {
                    if (r.success) {
                        spdlog::info("TCP fallback SUCCESS to {}", peer_id);
                        notify_tcp_result(peer_id, true, r);
                    } else {
                        spdlog::warn("TCP fallback FAILED to {}, using relay", peer_id);
                        fallback_to_relay(peer_id);
                    }
                });
        } else {
            tcp_fallback->connect_tcp(peer_ip, peer_tcp_port,
                [this, peer_id](TcpFallbackConnector::TcpResult r) {
                    if (r.success) {
                        spdlog::info("TCP fallback SUCCESS to {}", peer_id);
                        notify_tcp_result(peer_id, true, r);
                    } else {
                        spdlog::warn("TCP fallback FAILED to {}, using relay", peer_id);
                        fallback_to_relay(peer_id);
                    }
                });
        }
    }

    void fallback_to_relay(const std::string& peer_id) {
        spdlog::info("RendezvousMediator: falling back to relay for {}", peer_id);
        // Generate a relay UUID
        auto rand_bytes = common::crypto::random_bytes(16);
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < 16; i++) {
            ss << std::setw(2) << static_cast<int>(rand_bytes[i]);
        }
        std::string relay_uuid = ss.str();

        // Send relay request
        auto payload = build_relay_payload(relay_uuid, "");
        auto msg = serialize_message(protocol::WIRE_REQUEST_RELAY, payload);
        connection->send(msg);

        spdlog::info("Relay request sent: uuid={}", relay_uuid);
    }

    void notify_punch_result(const std::string& peer_id, bool success,
                             const UdpHolePuncher::PunchResult& r) {
        // Build a notification payload and post via callback
        if (on_message) {
            std::vector<uint8_t> notify;
            notify.push_back(success ? 1 : 0);
            write_string(notify, peer_id);
            write_string(notify, r.remote_addr);
            write_u32(notify, r.remote_port);
            write_u32(notify, r.local_port);
            asio::post(strand, [this, notify = std::move(notify)]() {
                if (on_message) on_message(RendezvousMessageType::PUNCH_HOLE_RESPONSE,
                    notify);
            });
        }
    }

    void notify_tcp_result(const std::string& peer_id, bool success,
                           const TcpFallbackConnector::TcpResult& r) {
        if (on_message) {
            std::vector<uint8_t> notify;
            notify.push_back(success ? 1 : 0);
            write_string(notify, peer_id);
            write_string(notify, r.remote_addr);
            write_u32(notify, r.remote_port);
            asio::post(strand, [this, notify = std::move(notify)]() {
                if (on_message) on_message(RendezvousMessageType::PUNCH_HOLE_RESPONSE,
                    notify);
            });
        }
    }

    // ========================================================================
    // Query online
    // ========================================================================
    void do_query_online(const std::string& peer_id) {
        auto payload = build_query_online_payload(peer_id);
        auto msg = serialize_message(protocol::WIRE_QUERY_ONLINE, payload);
        connection->send(msg);
        spdlog::debug("Query online sent for {}", peer_id);
    }

    void handle_query_online_response(const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        size_t offset = 0;
        std::string peer_id = read_string(payload.data(), payload.size(), offset);
        bool online = (offset < payload.size()) ? (payload[offset] != 0) : false;
        std::string ip = (offset + 2 <= payload.size()) ?
            read_string(payload.data(), payload.size(), offset) : "";

        peer_registry->update_online(peer_id, online, ip, 0);
    }

    // ========================================================================
    // PK response
    // ========================================================================
    void handle_pk_response(const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        size_t offset = 0;
        std::string pk = read_string(payload.data(), payload.size(), offset);
        std::string signature = read_string(payload.data(), payload.size(), offset);

        bool valid = verify_pk_signature(pk, peer_token, signature);
        registered = valid;
        spdlog::info("PK verification: {}", valid ? "SUCCESS" : "FAILED");
    }

    // ========================================================================
    // Config response
    // ========================================================================
    void handle_config_response(const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        size_t offset = 0;
        std::string key = read_string(payload.data(), payload.size(), offset);
        std::string value = read_string(payload.data(), payload.size(), offset);
        spdlog::info("Config response: {} = {}", key, value);

        if (on_message) {
            std::vector<uint8_t> data;
            write_string(data, key);
            write_string(data, value);
            asio::post(strand, [this, data = std::move(data)]() {
                if (on_message) on_message(RendezvousMessageType::CONFIG_RESPONSE,
                    data);
            });
        }
    }

    // ========================================================================
    // Software update
    // ========================================================================
    void handle_software_update(const std::vector<uint8_t>& payload) {
        update_handler->parse_update_message(payload);
    }

    // ========================================================================
    // Address book
    // ========================================================================
    void handle_address_book(const std::vector<uint8_t>& payload) {
        size_t offset = 0;
        while (offset < payload.size()) {
            std::string peer_id = read_string(payload.data(), payload.size(), offset);
            std::string alias = read_string(payload.data(), payload.size(), offset);
            bool online = (offset < payload.size()) ? (payload[offset] != 0) : false;
            offset++;

            PeerRegistry::PeerEntry entry;
            entry.id = peer_id;
            entry.alias = alias;
            entry.online = online;
            peer_registry->add_or_update(peer_id, entry);
        }
        spdlog::info("Address book updated");
    }

    // ========================================================================
    // Heartbeat response
    // ========================================================================
    void handle_heartbeat_response(const std::vector<uint8_t>& /*payload*/) {
        registered = true;
        spdlog::debug("Heartbeat acknowledged");
    }

    // ========================================================================
    // Start
    // ========================================================================
    void start() {
        if (running) return;

        running = true;
        connect();

        // Start keep-alive
        int32_t ka_ms = keep_alive_ms > 0 ? keep_alive_ms
            : RendezvousMediator::DEFAULT_KEEP_ALIVE;
        keep_alive->start(ka_ms / 6, // heartbeat at 1/6 of keep-alive interval
            [this]() {
                // Send heartbeat
                auto payload = build_heartbeat_payload();
                auto msg = serialize_message(protocol::WIRE_HEARTBEAT, payload);
                if (connection->is_connected()) {
                    connection->send(msg);
                }
            },
            [this]() {
                // Periodic re-registration
                do_register();
            });
    }

    // ========================================================================
    // Stop
    // ========================================================================
    void stop() {
        spdlog::info("RendezvousMediator: stopping");
        running = false;
        registered = false;

        keep_alive->stop();

        asio::error_code ec;
        reregister_timer.cancel(ec);

        if (connection) {
            connection->disconnect();
        }

        work.reset();

        if (io_thread.joinable()) {
            try {
                io_ctx.stop();
                io_thread.join();
            } catch (const std::exception& e) {
                spdlog::warn("Exception stopping IO thread: {}", e.what());
            }
        }

        spdlog::info("RendezvousMediator: stopped");
    }

    // ========================================================================
    // NAT detection
    // ========================================================================
    void do_test_nat() {
        // Use Google's public STUN server
        nat_detector->detect("stun.l.google.com", 19302,
            [this](NatType type, const std::string& ip, uint16_t port) {
                nat_type = static_cast<int32_t>(type);
                mapped_ip = ip;
                mapped_port = port;
                spdlog::info("NAT detection complete: {} ({}:{})",
                    nat_type_name(type), ip, port);
            });
    }

    // ========================================================================
    // Restart
    // ========================================================================
    void restart() {
        spdlog::info("RendezvousMediator: restarting");
        stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Reset state
        registered = false;
        nat_type = static_cast<int32_t>(NatType::UNKNOWN_NAT);

        start();
        spdlog::info("RendezvousMediator: restarted");
    }

    // ========================================================================
    // Periodics
    // ========================================================================
    void process_callbacks() {
        peer_registry->process_callbacks();
    }

    void check_peer_timeouts() {
        peer_registry->check_timeout(std::chrono::seconds(120));
    }
};

// ============================================================================
// RendezvousMediator public API
// ============================================================================

RendezvousMediator::RendezvousMediator(const std::string& server_addr,
    const std::string& host, const std::string& host_prefix, int32_t keep_alive_ms)
    : impl_(std::make_unique<Impl>(server_addr, host, host_prefix, keep_alive_ms)) {
    spdlog::info("RendezvousMediator created: server={}", server_addr);
    impl_->start();
}

RendezvousMediator::~RendezvousMediator() {
    stop();
}

void RendezvousMediator::register_peer(const std::string& id,
    const std::string& password) {
    spdlog::info("Registering peer: id={}", id);
    impl_->registered = true;
    impl_->do_register(id, password);
}

void RendezvousMediator::register_pk(const std::string& pk,
    const std::string& token) {
    spdlog::info("Registering PK with token");
    impl_->do_register_pk(pk, token);
}

void RendezvousMediator::unregister_peer() {
    impl_->registered = false;
    spdlog::info("Unregistering peer");
    // Send unregister to server (empty register with "offline" flag)
    std::vector<uint8_t> payload;
    write_string(payload, "offline");
    auto msg = serialize_message(protocol::WIRE_REGISTER_PEER, payload);
    if (impl_->connection->is_connected()) {
        impl_->connection->send(msg);
    }
}

void RendezvousMediator::update_alias(const std::string& alias) {
    impl_->peer_alias = alias;
    spdlog::debug("Updating alias to: {}", alias);
    auto payload = build_alias_payload(alias);
    auto msg = serialize_message(protocol::WIRE_ALIAS_UPDATE, payload);
    if (impl_->connection->is_connected()) {
        impl_->connection->send(msg);
    }
}

void RendezvousMediator::punch_hole(const std::string& peer_id,
    const std::string& key, const std::string& token, ConnType conn_type,
    bool force_relay) {
    spdlog::info("Punching hole to peer={} conn_type={}{}",
        peer_id, static_cast<int>(conn_type), force_relay ? " [FORCE_RELAY]" : "");
    impl_->do_punch_hole(peer_id, key, token, conn_type, force_relay);
}

void RendezvousMediator::request_relay(const std::string& uuid,
    const std::string& licence_key) {
    spdlog::info("Requesting relay: uuid={}", uuid);
    auto payload = build_relay_payload(uuid, licence_key);
    auto msg = serialize_message(protocol::WIRE_REQUEST_RELAY, payload);
    if (impl_->connection->is_connected()) {
        impl_->connection->send(msg);
    }
}

void RendezvousMediator::query_online(const std::string& peer_id) {
    spdlog::debug("Querying online status: {}", peer_id);
    impl_->do_query_online(peer_id);
}

int32_t RendezvousMediator::get_nat_type() const {
    return impl_->nat_type;
}

void RendezvousMediator::test_nat() {
    spdlog::info("Starting NAT type detection...");
    impl_->do_test_nat();
}

uint16_t RendezvousMediator::get_udp_port() const {
    return impl_->udp_port;
}

bool RendezvousMediator::is_registered() const {
    return impl_->registered;
}

bool RendezvousMediator::is_connected() const {
    return impl_->running && impl_->connection->is_connected();
}

void RendezvousMediator::restart() {
    impl_->restart();
}

void RendezvousMediator::stop() {
    impl_->stop();
}

void RendezvousMediator::set_on_message(OnMessage cb) {
    impl_->on_message = std::move(cb);
}

void RendezvousMediator::set_on_peer_online(OnPeerOnline cb) {
    impl_->on_peer_online = std::move(cb);
}

void RendezvousMediator::set_on_software_update(OnSoftwareUpdate cb) {
    impl_->on_software_update = std::move(cb);
}

// ============================================================================
// RendezvousServer::Impl — server-side implementation
// ============================================================================
struct RendezvousServer::Impl {
    uint16_t port;
    asio::io_context io_ctx;
    asio::ip::tcp::acceptor acceptor;
    asio::io_context::strand strand;
    std::unique_ptr<asio::io_context::work> work;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<uint32_t> session_counter{0};

    struct PeerInfo {
        std::string id;
        std::string pk;
        std::string password_hash;
        std::string ip;
        uint16_t port = 0;
        uint16_t udp_port = 0;
        bool online = false;
        NatType nat_type = NatType::UNKNOWN_NAT;
        std::chrono::steady_clock::time_point last_seen;
        std::chrono::steady_clock::time_point registered_at;
        std::string alias;
        std::string hostname;
        std::string platform;
        std::string version;
    };

    struct ClientSession : std::enable_shared_from_this<ClientSession> {
        uint32_t session_id;
        std::shared_ptr<asio::ip::tcp::socket> socket;
        std::string peer_id;
        std::array<uint8_t, 65536> read_buffer;
        bool authenticated = false;
        std::chrono::steady_clock::time_point connected_at;

        ClientSession(uint32_t sid, std::shared_ptr<asio::ip::tcp::socket> sock)
            : session_id(sid), socket(std::move(sock)),
              connected_at(std::chrono::steady_clock::now()) {}
    };

    std::map<std::string, PeerInfo> peers;
    std::mutex peers_mutex;
    std::map<uint32_t, std::shared_ptr<ClientSession>> sessions;
    std::mutex sessions_mutex;

    OnRegister on_register;
    std::function<void(const std::string&, const std::string&, uint16_t)> on_punch_request;

    // Anti-flood tracking
    struct RateLimit {
        uint32_t count = 0;
        std::chrono::steady_clock::time_point window_start;
    };
    std::map<std::string, RateLimit> rate_limits;
    std::mutex rate_mutex;
    static constexpr uint32_t MAX_REQUESTS_PER_WINDOW = 60;
    static constexpr auto RATE_WINDOW = std::chrono::seconds(60);

    Impl(uint16_t p) : port(p), acceptor(io_ctx), strand(io_ctx) {}

    bool check_rate_limit(const std::string& ip) {
        std::lock_guard lk(rate_mutex);
        auto now = std::chrono::steady_clock::now();
        auto& rl = rate_limits[ip];
        if ((now - rl.window_start) > RATE_WINDOW) {
            rl.count = 0;
            rl.window_start = now;
        }
        rl.count++;
        return rl.count <= MAX_REQUESTS_PER_WINDOW;
    }

    void start_accept() {
        auto session = std::make_shared<ClientSession>(
            session_counter.fetch_add(1),
            std::make_shared<asio::ip::tcp::socket>(io_ctx));

        acceptor.async_accept(*session->socket,
            strand.wrap([this, session](asio::error_code ec) {
                if (!ec) {
                    spdlog::debug("RendezvousServer: new connection, session={}",
                        session->session_id);
                    {
                        std::lock_guard lk(sessions_mutex);
                        sessions[session->session_id] = session;
                    }
                    start_read(session);
                } else if (ec != asio::error::operation_aborted) {
                    spdlog::warn("RendezvousServer: accept error: {}", ec.message());
                }
                if (running) start_accept();
            }));
    }

    void start_read(std::shared_ptr<ClientSession> session) {
        auto buf = std::make_shared<std::array<uint8_t, protocol::HEADER_SIZE>>();
        session->socket->async_read_some(
            asio::buffer(*buf, protocol::HEADER_SIZE),
            strand.wrap([this, session, buf](asio::error_code ec, size_t bytes) {
                if (ec) {
                    handle_session_error(session, ec);
                    return;
                }
                if (bytes < protocol::HEADER_SIZE) return;

                // Validate magic
                if ((*buf)[0] != protocol::MAGIC_NUMBER[0] ||
                    (*buf)[1] != protocol::MAGIC_NUMBER[1] ||
                    (*buf)[2] != protocol::MAGIC_NUMBER[2] ||
                    (*buf)[3] != protocol::MAGIC_NUMBER[3]) {
                    spdlog::warn("RendezvousServer: bad magic from session {}",
                        session->session_id);
                    remove_session(session);
                    return;
                }

                uint32_t payload_len =
                    (static_cast<uint32_t>((*buf)[4]) << 24) |
                    (static_cast<uint32_t>((*buf)[5]) << 16) |
                    (static_cast<uint32_t>((*buf)[6]) << 8) |
                    static_cast<uint32_t>((*buf)[7]);
                uint32_t msg_type =
                    (static_cast<uint32_t>((*buf)[8]) << 24) |
                    (static_cast<uint32_t>((*buf)[9]) << 16) |
                    (static_cast<uint32_t>((*buf)[10]) << 8) |
                    static_cast<uint32_t>((*buf)[11]);

                if (payload_len == 0) {
                    std::vector<uint8_t> full(protocol::HEADER_SIZE);
                    std::copy(buf->begin(), buf->begin() + protocol::HEADER_SIZE,
                              full.begin());
                    handle_session_message(session, msg_type, {});
                    start_read(session);
                } else if (payload_len <= protocol::MAX_PAYLOAD_SIZE) {
                    auto payload_buf = std::make_shared<std::vector<uint8_t>>(payload_len);
                    session->socket->async_read_some(
                        asio::buffer(payload_buf->data(), payload_len),
                        strand.wrap([this, session, payload_buf, msg_type, payload_len](
                            asio::error_code ec, size_t br) {
                            if (ec) {
                                handle_session_error(session, ec);
                                return;
                            }
                            payload_buf->resize(br);
                            handle_session_message(session, msg_type, *payload_buf);
                            start_read(session);
                        }));
                } else {
                    spdlog::warn("RendezvousServer: payload too large: {}", payload_len);
                    remove_session(session);
                }
            }));
    }

    void handle_session_message(std::shared_ptr<ClientSession> session,
                                uint32_t msg_type,
                                const std::vector<uint8_t>& payload) {
        // Rate limiting
        std::string client_ip;
        {
            asio::error_code ec;
            client_ip = session->socket->remote_endpoint(ec).address().to_string();
        }
        if (!check_rate_limit(client_ip)) {
            spdlog::warn("RendezvousServer: rate limit exceeded for {}", client_ip);
            return;
        }

        spdlog::debug("RendezvousServer: msg {} from session {}",
            msg_type_name(msg_type), session->session_id);

        switch (msg_type) {
            case protocol::WIRE_REGISTER_PEER:
                handle_register(session, payload);
                break;
            case protocol::WIRE_PUNCH_HOLE_REQ:
                handle_punch_hole_request(session, payload);
                break;
            case protocol::WIRE_HEARTBEAT:
                handle_heartbeat(session, payload);
                break;
            case protocol::WIRE_QUERY_ONLINE:
                handle_query_online(session, payload);
                break;
            case protocol::WIRE_REGISTER_PK:
                handle_pk_register(session, payload);
                break;
            case protocol::WIRE_ALIAS_UPDATE:
                handle_alias_update(session, payload);
                break;
            case protocol::WIRE_CONFIG_REQUEST:
                handle_config_request(session, payload);
                break;
            case protocol::WIRE_REQUEST_RELAY:
                handle_relay_request(session, payload);
                break;
            default:
                spdlog::debug("RendezvousServer: unhandled msg type {} from session {}",
                    msg_type, session->session_id);
                break;
        }
    }

    void handle_register(std::shared_ptr<ClientSession> session,
                         const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        size_t offset = 0;
        std::string peer_id = read_string(payload.data(), payload.size(), offset);
        std::string password_hash = read_string(payload.data(), payload.size(), offset);
        std::string hostname = read_string(payload.data(), payload.size(), offset);
        std::string platform = read_string(payload.data(), payload.size(), offset);
        uint16_t udp_port = (offset + 4 <= payload.size()) ?
            static_cast<uint16_t>(read_u32(payload.data(), offset)) : 0;
        uint32_t nat_raw = (offset + 4 <= payload.size()) ?
            read_u32(payload.data(), offset) : 0;
        uint32_t version = (offset + 4 <= payload.size()) ?
            read_u32(payload.data(), offset) : 0;

        session->peer_id = peer_id;

        asio::error_code ec;
        std::string ip = session->socket->remote_endpoint(ec).address().to_string();
        uint16_t port = session->socket->remote_endpoint(ec).port();

        NatType nat = NatType::UNKNOWN_NAT;
        if (nat_raw <= static_cast<uint32_t>(NatType::SYMMETRIC)) {
            nat = static_cast<NatType>(nat_raw);
        }

        {
            std::lock_guard lk(peers_mutex);
            auto& peer = peers[peer_id];
            peer.id = peer_id;
            peer.password_hash = password_hash;
            peer.ip = ip;
            peer.port = port;
            peer.udp_port = udp_port;
            peer.online = true;
            peer.nat_type = nat;
            peer.last_seen = std::chrono::steady_clock::now();
            peer.registered_at = std::chrono::steady_clock::now();
            peer.hostname = hostname;
            peer.platform = platform;
            peer.version = std::to_string(version);
        }

        spdlog::info("RendezvousServer: registered peer {} ({}:{}) nat={}",
            peer_id, ip, port, nat_type_name(nat));

        if (on_register) {
            asio::post(strand, [this, peer_id]() {
                if (on_register) on_register(peer_id, true);
            });
        }
    }

    void handle_punch_hole_request(std::shared_ptr<ClientSession> session,
                                   const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        size_t offset = 0;
        std::string my_id = read_string(payload.data(), payload.size(), offset);
        std::string peer_id = read_string(payload.data(), payload.size(), offset);
        std::string key = read_string(payload.data(), payload.size(), offset);
        std::string token = read_string(payload.data(), payload.size(), offset);
        uint32_t conn_type = (offset + 4 <= payload.size()) ?
            read_u32(payload.data(), offset) : 0;
        bool force_relay = (offset < payload.size()) ? (payload[offset] != 0) : false;

        // Look up peer
        PeerInfo peer_info;
        PeerInfo my_info;
        {
            std::lock_guard lk(peers_mutex);
            auto it = peers.find(peer_id);
            if (it == peers.end() || !it->second.online) {
                spdlog::warn("Punch hole: peer {} not found or offline", peer_id);
                send_punch_response(session, peer_id, "", 0, 0, 0, false);
                return;
            }
            peer_info = it->second;

            auto my_it = peers.find(my_id);
            if (my_it != peers.end()) {
                my_info = my_it->second;
            }
        }

        // Determine if relay should be forced
        bool should_relay = force_relay;
        if (!should_relay) {
            // Force relay for symmetric NAT on both sides
            if (my_info.nat_type == NatType::SYMMETRIC &&
                peer_info.nat_type == NatType::SYMMETRIC) {
                should_relay = true;
                spdlog::info("Symmetric NAT on both sides, forcing relay");
            }
        }

        if (should_relay) {
            // Relay path: send relay server info
            spdlog::info("Punch hole: relay requested for {} <-> {}",
                my_id, peer_id);
            send_punch_response(session, peer_id, "", 0, 0,
                static_cast<uint32_t>(peer_info.nat_type), true);
        } else {
            // Direct connection: send peer's address info
            spdlog::info("Punch hole: sending {} address ({}) to {}",
                peer_id, peer_info.ip, my_id);
            send_punch_response(session, peer_id, peer_info.ip,
                peer_info.udp_port, peer_info.port,
                static_cast<uint32_t>(peer_info.nat_type), true);

            // Also notify the target peer to expect a connection
            auto target_it = sessions.find(
                [this, &peer_id](auto& kv) { return kv.second->peer_id == peer_id; });
            if (target_it != sessions.end()) {
                send_punch_notification(target_it->second, my_id, my_info.ip,
                    my_info.udp_port, my_info.port);
            }
        }

        if (on_punch_request) {
            asio::post(strand, [this, peer_id, my_id, peer_info, conn_type]() {
                if (on_punch_request)
                    on_punch_request(my_id, peer_info.ip, peer_info.udp_port);
            });
        }
    }

    void send_punch_response(std::shared_ptr<ClientSession> session,
                             const std::string& peer_id,
                             const std::string& peer_ip,
                             uint16_t peer_udp_port,
                             uint16_t peer_tcp_port,
                             uint32_t peer_nat_type,
                             bool success) {
        std::vector<uint8_t> payload;
        write_string(payload, peer_id);
        write_string(payload, peer_ip);
        write_u32(payload, peer_udp_port);
        write_u32(payload, peer_tcp_port);
        write_u32(payload, peer_nat_type);
        payload.push_back(success ? 1 : 0);

        auto msg = serialize_message(protocol::WIRE_PUNCH_HOLE_RESP, payload);
        asio::error_code ec;
        session->socket->write_some(asio::buffer(msg), ec);
        if (ec) {
            spdlog::warn("Failed to send punch response: {}", ec.message());
        }
    }

    void send_punch_notification(std::shared_ptr<ClientSession> session,
                                 const std::string& remote_id,
                                 const std::string& remote_ip,
                                 uint16_t remote_udp_port,
                                 uint16_t remote_tcp_port) {
        std::vector<uint8_t> payload;
        write_string(payload, remote_id);
        write_string(payload, remote_ip);
        write_u32(payload, remote_udp_port);
        write_u32(payload, remote_tcp_port);
        payload.push_back(1); // notification flag

        auto msg = serialize_message(protocol::WIRE_PUNCH_HOLE_RESP, payload);
        asio::error_code ec;
        session->socket->write_some(asio::buffer(msg), ec);
        if (ec) {
            spdlog::debug("Failed to send punch notification: {}", ec.message());
        }
    }

    void handle_heartbeat(std::shared_ptr<ClientSession> session,
                          const std::vector<uint8_t>& payload) {
        if (session->peer_id.empty()) return;
        std::lock_guard lk(peers_mutex);
        auto it = peers.find(session->peer_id);
        if (it != peers.end()) {
            it->second.last_seen = std::chrono::steady_clock::now();
            it->second.online = true;
        }
        // Send acknowledgment
        std::vector<uint8_t> ack;
        write_u64(ack, static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()));
        auto msg = serialize_message(protocol::WIRE_HEARTBEAT, ack);
        asio::error_code ec;
        session->socket->write_some(asio::buffer(msg), ec);
    }

    void handle_query_online(std::shared_ptr<ClientSession> session,
                             const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        size_t offset = 0;
        std::string peer_id = read_string(payload.data(), payload.size(), offset);

        bool online = false;
        std::string ip;
        {
            std::lock_guard lk(peers_mutex);
            auto it = peers.find(peer_id);
            if (it != peers.end()) {
                online = it->second.online;
                ip = it->second.ip;
                // Check timeout
                auto now = std::chrono::steady_clock::now();
                if ((now - it->second.last_seen) > common::HEARTBEAT_INTERVAL * 3) {
                    it->second.online = false;
                    online = false;
                }
            }
        }

        std::vector<uint8_t> resp;
        write_string(resp, peer_id);
        resp.push_back(online ? 1 : 0);
        write_string(resp, ip);

        auto msg = serialize_message(protocol::WIRE_QUERY_ONLINE, resp);
        asio::error_code ec;
        session->socket->write_some(asio::buffer(msg), ec);
    }

    void handle_pk_register(std::shared_ptr<ClientSession> session,
                            const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        size_t offset = 0;
        std::string pk = read_string(payload.data(), payload.size(), offset);
        std::string token = read_string(payload.data(), payload.size(), offset);
        std::string id = read_string(payload.data(), payload.size(), offset);
        std::string uuid = read_string(payload.data(), payload.size(), offset);

        // Store the PK
        {
            std::lock_guard lk(peers_mutex);
            auto it = peers.find(id);
            if (it != peers.end()) {
                it->second.pk = pk;
            }
        }

        // Send PK response (sign token with server key for verification)
        std::vector<uint8_t> resp;
        write_string(resp, pk);
        // In production, this would be an actual signature
        auto sig = common::crypto::random_bytes(common::crypto::SIGN_BYTES);
        write_string(resp, common::crypto::encode64(sig.data(), sig.size()));

        auto msg = serialize_message(protocol::WIRE_PK_RESPONSE, resp);
        asio::error_code ec;
        session->socket->write_some(asio::buffer(msg), ec);

        spdlog::info("PK registered for {}", id);
    }

    void handle_alias_update(std::shared_ptr<ClientSession> session,
                             const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        if (session->peer_id.empty()) return;
        size_t offset = 0;
        std::string alias = read_string(payload.data(), payload.size(), offset);

        std::lock_guard lk(peers_mutex);
        auto it = peers.find(session->peer_id);
        if (it != peers.end()) {
            it->second.alias = alias;
            spdlog::debug("Alias updated for {}: {}", session->peer_id, alias);
        }
    }

    void handle_config_request(std::shared_ptr<ClientSession> session,
                               const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        size_t offset = 0;
        std::string key = read_string(payload.data(), payload.size(), offset);

        // For now, return a basic config
        std::vector<uint8_t> resp;
        write_string(resp, key);
        if (key == "rendezvous_servers") {
            write_string(resp, "rs-ny.rustdesk.com,rs-sg.rustdesk.com,rs-cn.rustdesk.com");
        } else if (key == "version") {
            write_string(resp, common::get_version_number());
        } else if (key == "software_update_url") {
            write_string(resp, "");
        } else {
            write_string(resp, "");
        }

        auto msg = serialize_message(protocol::WIRE_CONFIG_RESPONSE, resp);
        asio::error_code ec;
        session->socket->write_some(asio::buffer(msg), ec);
    }

    void handle_relay_request(std::shared_ptr<ClientSession> session,
                              const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        size_t offset = 0;
        std::string uuid = read_string(payload.data(), payload.size(), offset);
        std::string licence_key = read_string(payload.data(), payload.size(), offset);
        spdlog::info("Relay requested: uuid={}", uuid);
    }

    void handle_session_error(std::shared_ptr<ClientSession> session,
                              const asio::error_code& ec) {
        if (ec == asio::error::operation_aborted) return;
        spdlog::debug("RendezvousServer: session {} error: {}",
            session->session_id, ec.message());
        remove_session(session);
    }

    void remove_session(std::shared_ptr<ClientSession> session) {
        if (!session->peer_id.empty()) {
            std::lock_guard lk(peers_mutex);
            auto it = peers.find(session->peer_id);
            if (it != peers.end()) {
                it->second.online = false;
                spdlog::info("RendezvousServer: peer {} went offline", session->peer_id);
                if (on_register) {
                    asio::post(strand, [this, id = session->peer_id]() {
                        if (on_register) on_register(id, false);
                    });
                }
            }
        }
        {
            std::lock_guard lk(sessions_mutex);
            sessions.erase(session->session_id);
        }
        spdlog::debug("RendezvousServer: session {} removed", session->session_id);
    }

    // Helper: find session by peer_id
    std::map<uint32_t, std::shared_ptr<ClientSession>>::iterator
    find_session_by_peer(const std::string& peer_id) {
        return std::find_if(sessions.begin(), sessions.end(),
            [&](auto& kv) { return kv.second->peer_id == peer_id; });
    }
};

// ============================================================================
// RendezvousServer public API
// ============================================================================

RendezvousServer::RendezvousServer(uint16_t port)
    : impl_(std::make_unique<Impl>(port)) {
    spdlog::info("RendezvousServer created: port={}", port);
}

RendezvousServer::~RendezvousServer() {
    stop();
}

void RendezvousServer::start() {
    if (impl_->running) return;

    impl_->running = true;
    spdlog::info("RendezvousServer: starting on port {}", impl_->port);

    // Open acceptor
    asio::error_code ec;
    asio::ip::tcp::endpoint ep(asio::ip::tcp::v4(), impl_->port);
    impl_->acceptor.open(ep.protocol(), ec);
    if (ec) {
        spdlog::error("RendezvousServer: cannot open acceptor: {}", ec.message());
        impl_->running = false;
        return;
    }
    impl_->acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
    impl_->acceptor.bind(ep, ec);
    if (ec) {
        spdlog::error("RendezvousServer: cannot bind to {}: {}", impl_->port, ec.message());
        impl_->running = false;
        return;
    }
    impl_->acceptor.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        spdlog::error("RendezvousServer: cannot listen: {}", ec.message());
        impl_->running = false;
        return;
    }

    impl_->start_accept();

    // Start IO worker
    impl_->work = std::make_unique<asio::io_context::work>(impl_->io_ctx);
    impl_->worker = std::thread([this]() {
        spdlog::debug("RendezvousServer: IO worker started");
        while (impl_->running) {
            try {
                impl_->io_ctx.run();
            } catch (const std::exception& e) {
                spdlog::error("RendezvousServer: IO exception: {}", e.what());
            }
            if (!impl_->running) break;
            impl_->io_ctx.restart();
        }
        spdlog::debug("RendezvousServer: IO worker stopped");
    });

    spdlog::info("RendezvousServer: started");
}

void RendezvousServer::stop() {
    if (!impl_->running) return;

    spdlog::info("RendezvousServer: stopping");
    impl_->running = false;

    asio::error_code ec;
    impl_->acceptor.close(ec);
    impl_->work.reset();
    impl_->io_ctx.stop();

    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }

    spdlog::info("RendezvousServer: stopped");
}

bool RendezvousServer::is_running() const {
    return impl_->running;
}

void RendezvousServer::register_peer(const std::string& id,
    const std::string& pk, const std::string& ip, uint16_t port) {
    std::lock_guard lk(impl_->peers_mutex);
    impl_->peers[id] = {id, pk, "", ip, port, 0, true,
        NatType::UNKNOWN_NAT,
        std::chrono::steady_clock::now(),
        std::chrono::steady_clock::now()};
    if (impl_->on_register) {
        asio::post(impl_->strand, [this, id]() {
            if (impl_->on_register) impl_->on_register(id, true);
        });
    }
}

void RendezvousServer::unregister_peer(const std::string& id) {
    std::lock_guard lk(impl_->peers_mutex);
    impl_->peers.erase(id);
    if (impl_->on_register) {
        asio::post(impl_->strand, [this, id]() {
            if (impl_->on_register) impl_->on_register(id, false);
        });
    }
}

bool RendezvousServer::is_peer_online(const std::string& id) const {
    std::lock_guard lk(impl_->peers_mutex);
    auto it = impl_->peers.find(id);
    if (it == impl_->peers.end()) return false;
    // Check if timed out
    auto now = std::chrono::steady_clock::now();
    if ((now - it->second.last_seen) > common::HEARTBEAT_INTERVAL * 3) {
        return false;
    }
    return it->second.online;
}

std::vector<std::string> RendezvousServer::get_online_peers() const {
    std::lock_guard lk(impl_->peers_mutex);
    std::vector<std::string> result;
    auto now = std::chrono::steady_clock::now();
    for (auto& [id, peer] : impl_->peers) {
        if (peer.online &&
            (now - peer.last_seen) <= common::HEARTBEAT_INTERVAL * 3) {
            result.push_back(id);
        }
    }
    return result;
}

void RendezvousServer::update_nat_type(const std::string& id, NatType nat_type) {
    std::lock_guard lk(impl_->peers_mutex);
    auto it = impl_->peers.find(id);
    if (it != impl_->peers.end()) {
        it->second.nat_type = nat_type;
    }
}

NatType RendezvousServer::get_nat_type(const std::string& id) const {
    std::lock_guard lk(impl_->peers_mutex);
    auto it = impl_->peers.find(id);
    return it != impl_->peers.end() ? it->second.nat_type : NatType::UNKNOWN_NAT;
}

void RendezvousServer::set_on_register(OnRegister cb) {
    impl_->on_register = std::move(cb);
}

// ============================================================================
// RelayServer::Impl — relay server (hbbr-equivalent) implementation
// ============================================================================
struct RelayServer::Impl {
    uint16_t port;
    asio::io_context io_ctx;
    asio::ip::tcp::acceptor acceptor;
    asio::io_context::strand strand;
    std::unique_ptr<asio::io_context::work> work;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<uint32_t> session_counter{0};

    struct RelaySession {
        uint32_t id;
        std::string uuid;
        std::string licence_key;
        std::shared_ptr<asio::ip::tcp::socket> socket_a;
        std::shared_ptr<asio::ip::tcp::socket> socket_b;
        std::atomic<bool> active{false};
        std::atomic<bool> side_a_ready{false};
        std::atomic<bool> side_b_ready{false};
        std::chrono::steady_clock::time_point created_at;
        uint64_t bytes_relayed = 0;
        std::thread relay_thread;

        struct RelayBuffer {
            std::vector<uint8_t> data;
            std::chrono::steady_clock::time_point timestamp;
        };
        std::mutex buf_a_mutex;
        std::mutex buf_b_mutex;
        std::deque<RelayBuffer> buffer_a_to_b;
        std::deque<RelayBuffer> buffer_b_to_a;
        static constexpr size_t MAX_BUFFER_SIZE = 1024 * 1024; // 1MB
        static constexpr auto BUFFER_TTL = std::chrono::seconds(30);
    };

    struct PendingRelay {
        std::string uuid;
        std::shared_ptr<asio::ip::tcp::socket> socket;
        std::chrono::steady_clock::time_point created_at;
    };

    // Sessions indexed by UUID
    std::map<std::string, std::shared_ptr<RelaySession>> sessions;
    std::mutex sessions_mutex;

    // Pending connections waiting for a partner
    std::vector<PendingRelay> pending;
    std::mutex pending_mutex;

    // Callback for relay events
    std::function<void(const std::string&, bool)> on_relay_event;

    // Bandwidth statistics
    struct BandwidthStats {
        uint64_t total_bytes_relayed = 0;
        uint64_t total_sessions = 0;
        uint64_t active_sessions = 0;
        std::chrono::steady_clock::time_point last_reset;
    };
    BandwidthStats stats;
    std::mutex stats_mutex;

    Impl(uint16_t p) : port(p), acceptor(io_ctx), strand(io_ctx) {
        stats.last_reset = std::chrono::steady_clock::now();
    }

    void start_accept() {
        auto socket = std::make_shared<asio::ip::tcp::socket>(io_ctx);
        acceptor.async_accept(*socket,
            strand.wrap([this, socket](asio::error_code ec) mutable {
                if (!ec) {
                    spdlog::debug("RelayServer: new connection");
                    handle_new_connection(std::move(socket));
                } else if (ec != asio::error::operation_aborted) {
                    spdlog::warn("RelayServer: accept error: {}", ec.message());
                }
                if (running) start_accept();
            }));
    }

    void handle_new_connection(std::shared_ptr<asio::ip::tcp::socket> socket) {
        // Read initial handshake (UUID + licence key)
        auto buf = std::make_shared<std::array<uint8_t, 4096>>();
        socket->async_read_some(
            asio::buffer(*buf),
            strand.wrap([this, socket, buf](asio::error_code ec, size_t bytes) {
                if (ec) {
                    spdlog::warn("RelayServer: handshake read error: {}", ec.message());
                    return;
                }
                std::vector<uint8_t> data(buf->begin(), buf->begin() + bytes);
                size_t offset = 0;

                std::string uuid = read_string(data.data(), data.size(), offset);
                std::string licence_key = read_string(data.data(), data.size(), offset);

                spdlog::info("RelayServer: handshake uuid={}", uuid);

                // Check if there's an existing session waiting
                auto existing = find_or_create_session(uuid, licence_key);
                if (existing) {
                    existing->socket_b = socket;
                    existing->side_b_ready = true;
                    spdlog::info("RelayServer: paired session {}", uuid);
                    start_relay(existing);
                } else {
                    // Store as pending
                    auto session = std::make_shared<RelaySession>();
                    session->id = session_counter.fetch_add(1);
                    session->uuid = uuid;
                    session->licence_key = licence_key;
                    session->socket_a = socket;
                    session->side_a_ready = true;
                    session->created_at = std::chrono::steady_clock::now();
                    {
                        std::lock_guard lk(sessions_mutex);
                        sessions[uuid] = session;
                    }
                    spdlog::info("RelayServer: waiting for partner for {}", uuid);
                }
            }));
    }

    std::shared_ptr<RelaySession> find_or_create_session(
        const std::string& uuid, const std::string& licence_key) {
        std::lock_guard lk(sessions_mutex);
        auto it = sessions.find(uuid);
        if (it != sessions.end() && !it->second->side_b_ready) {
            return it->second;
        }
        return nullptr;
    }

    void start_relay(std::shared_ptr<RelaySession> session) {
        session->active = true;

        // Update stats
        {
            std::lock_guard lk(stats_mutex);
            stats.total_sessions++;
            stats.active_sessions++;
        }

        if (on_relay_event) {
            asio::post(strand, [this, uuid = session->uuid]() {
                if (on_relay_event) on_relay_event(uuid, true);
            });
        }

        // Launch relay thread
        session->relay_thread = std::thread([this, session]() {
            relay_loop(session);
        });

        spdlog::info("RelayServer: relay started for {} (session {})",
            session->uuid, session->id);
    }

    void relay_loop(std::shared_ptr<RelaySession> session) {
        std::array<uint8_t, 65536> buf;

        auto read_from = [&](std::shared_ptr<asio::ip::tcp::socket> src,
                             std::shared_ptr<asio::ip::tcp::socket> dst,
                             std::mutex& buf_mutex,
                             std::deque<RelaySession::RelayBuffer>& buffer)
                             -> bool {
            asio::error_code ec;
            size_t bytes = src->read_some(asio::buffer(buf), ec);
            if (ec || bytes == 0) {
                return false;
            }
            // Buffer and forward
            RelaySession::RelayBuffer relay_buf;
            relay_buf.data.assign(buf.data(), buf.data() + bytes);
            relay_buf.timestamp = std::chrono::steady_clock::now();
            {
                std::lock_guard lk(buf_mutex);
                if (buffer.size() >= RelaySession::MAX_BUFFER_SIZE) {
                    buffer.pop_front();
                }
                buffer.push_back(std::move(relay_buf));
            }
            // Write to destination
            asio::error_code w_ec;
            asio::write(*dst, asio::buffer(buf, bytes), w_ec);
            session->bytes_relayed += bytes;
            return !w_ec;
        };

        while (session->active) {
            bool ok = true;

            // A -> B
            if (session->side_a_ready && session->side_b_ready) {
                ok = read_from(session->socket_a, session->socket_b,
                               session->buf_a_mutex, session->buffer_a_to_b);
                if (!ok) {
                    spdlog::debug("RelayServer: A->B relay ended");
                    break;
                }
            }

            // B -> A
            if (session->side_b_ready && session->side_a_ready) {
                ok = read_from(session->socket_b, session->socket_a,
                               session->buf_b_mutex, session->buffer_b_to_a);
                if (!ok) {
                    spdlog::debug("RelayServer: B->A relay ended");
                    break;
                }
            }

            // Yield to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        cleanup_session(session);
    }

    void cleanup_session(std::shared_ptr<RelaySession> session) {
        session->active = false;
        spdlog::info("RelayServer: session {} ended ({} bytes relayed)",
            session->uuid, session->bytes_relayed.load());

        // Update stats
        {
            std::lock_guard lk(stats_mutex);
            stats.total_bytes_relayed += session->bytes_relayed;
            if (stats.active_sessions > 0) stats.active_sessions--;
        }

        if (on_relay_event) {
            asio::post(strand, [this, uuid = session->uuid]() {
                if (on_relay_event) on_relay_event(uuid, false);
            });
        }

        // Close sockets
        asio::error_code ec;
        if (session->socket_a) session->socket_a->close(ec);
        if (session->socket_b) session->socket_b->close(ec);

        // Remove from sessions
        {
            std::lock_guard lk(sessions_mutex);
            sessions.erase(session->uuid);
        }
    }

    BandwidthStats get_stats() const {
        std::lock_guard lk(stats_mutex);
        return stats;
    }

    size_t get_active_session_count() const {
        std::lock_guard lk(stats_mutex);
        return stats.active_sessions;
    }

    // Purge stale pending connections
    void purge_stale_pending() {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard lk(sessions_mutex);
        auto it = sessions.begin();
        while (it != sessions.end()) {
            if (!it->second->active &&
                !it->second->side_b_ready &&
                (now - it->second->created_at) > std::chrono::seconds(60)) {
                spdlog::debug("RelayServer: purging stale session {}", it->first);
                if (it->second->socket_a) {
                    asio::error_code ec;
                    it->second->socket_a->close(ec);
                }
                it = sessions.erase(it);
            } else {
                ++it;
            }
        }
    }
};

// ============================================================================
// RelayServer public API
// ============================================================================

RelayServer::RelayServer(uint16_t port) : impl_(std::make_unique<Impl>(port)) {
    spdlog::info("RelayServer created: port={}", port);
}

RelayServer::~RelayServer() {
    stop();
}

void RelayServer::start() {
    if (impl_->running) return;

    impl_->running = true;
    spdlog::info("RelayServer: starting on port {}", impl_->port);

    asio::error_code ec;
    asio::ip::tcp::endpoint ep(asio::ip::tcp::v4(), impl_->port);
    impl_->acceptor.open(ep.protocol(), ec);
    if (ec) {
        spdlog::error("RelayServer: cannot open acceptor: {}", ec.message());
        impl_->running = false;
        return;
    }
    impl_->acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
    impl_->acceptor.bind(ep, ec);
    if (ec) {
        spdlog::error("RelayServer: cannot bind to {}: {}", impl_->port, ec.message());
        impl_->running = false;
        return;
    }
    impl_->acceptor.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        spdlog::error("RelayServer: cannot listen: {}", ec.message());
        impl_->running = false;
        return;
    }

    impl_->start_accept();

    impl_->work = std::make_unique<asio::io_context::work>(impl_->io_ctx);
    impl_->worker = std::thread([this]() {
        spdlog::debug("RelayServer: IO worker started");
        while (impl_->running) {
            try {
                impl_->io_ctx.run();
            } catch (const std::exception& e) {
                spdlog::error("RelayServer: IO exception: {}", e.what());
            }
            if (!impl_->running) break;
            impl_->io_ctx.restart();
        }
        spdlog::debug("RelayServer: IO worker stopped");
    });

    spdlog::info("RelayServer: started on port {}", impl_->port);
}

void RelayServer::stop() {
    if (!impl_->running) return;

    spdlog::info("RelayServer: stopping");
    impl_->running = false;

    // Close all active relay sessions
    {
        std::lock_guard lk(impl_->sessions_mutex);
        for (auto& [uuid, session] : impl_->sessions) {
            session->active = false;
            if (session->relay_thread.joinable()) {
                session->relay_thread.join();
            }
            asio::error_code ec;
            if (session->socket_a) session->socket_a->close(ec);
            if (session->socket_b) session->socket_b->close(ec);
        }
        impl_->sessions.clear();
    }

    asio::error_code ec;
    impl_->acceptor.close(ec);
    impl_->work.reset();
    impl_->io_ctx.stop();

    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }

    spdlog::info("RelayServer: stopped");
}

bool RelayServer::is_running() const {
    return impl_->running;
}

void RelayServer::relay_connection(const std::string& uuid,
    StreamPtr stream_a, StreamPtr stream_b) {
    spdlog::info("RelayServer: relaying connection for uuid={}", uuid);

    auto session = std::make_shared<Impl::RelaySession>();
    session->uuid = uuid;
    session->active = true;
    session->side_a_ready = true;
    session->side_b_ready = true;
    session->created_at = std::chrono::steady_clock::now();

    // Create socket wrappers using raw file descriptors from Stream objects
    // In practice, this would use the underlying socket handles
    // For this implementation, we pass through to the stream relay

    session->relay_thread = std::thread([session, a = std::move(stream_a),
                                          b = std::move(stream_b)]() mutable {
        std::vector<uint8_t> buf(65536);
        while (session->active) {
            // Relay A -> B
            auto data = a->recv();
            if (!data.empty()) {
                b->send(data);
            }
            // Relay B -> A
            data = b->recv();
            if (!data.empty()) {
                a->send(data);
            }
            // Check if both streams are still open
            if (!a->is_open() || !b->is_open()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        session->active = false;
    });

    {
        std::lock_guard lk(impl_->sessions_mutex);
        impl_->sessions[uuid] = session;
    }

    // Update stats
    {
        std::lock_guard lk(impl_->stats_mutex);
        impl_->stats.total_sessions++;
        impl_->stats.active_sessions++;
    }
}

} // namespace cppdesk::rendezvous
