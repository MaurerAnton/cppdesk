#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <vector>
#include <optional>
#include <map>
#include <mutex>
#include <chrono>
#include <nlohmann/json.hpp>

namespace cppdesk::common {

using json = nlohmann::json;

// Time constants
inline constexpr auto CONNECT_TIMEOUT = std::chrono::seconds(5);
inline constexpr auto READ_TIMEOUT = std::chrono::seconds(30);
inline constexpr auto REG_INTERVAL = std::chrono::seconds(60);
inline constexpr auto CLIPBOARD_INTERVAL = std::chrono::milliseconds(500);
inline constexpr auto SERVICE_INTERVAL = std::chrono::seconds(300);
inline constexpr auto HEARTBEAT_INTERVAL = std::chrono::seconds(10);
inline constexpr auto UDP_PUNCH_TIMEOUT = std::chrono::seconds(15);

// Port defaults
inline constexpr uint16_t RENDEZVOUS_PORT = 21116;
inline constexpr uint16_t RELAY_PORT = 21117;
inline constexpr uint16_t WEBSOCKET_PORT = 21118;
inline constexpr uint16_t LOCAL_PORT = 21119;

// Rendezvous servers
inline const std::vector<std::string> RENDEZVOUS_SERVERS = {
    "rs-ny.rustdesk.com",
    "rs-sg.rustdesk.com",
    "rs-cn.rustdesk.com",
};

// Configuration keys
namespace keys {
    inline constexpr std::string_view ID = "id";
    inline constexpr std::string_view PASSWORD = "password";
    inline constexpr std::string_view KEY_PAIR = "key_pair";
    inline constexpr std::string_view CUSTOM_RENDEZVOUS = "custom-rendezvous-server";
    inline constexpr std::string_view DIRECT_ACCESS = "direct-access-port";
    inline constexpr std::string_view ENABLE_UDP = "enable-udp";
    inline constexpr std::string_view ENABLE_IPV6 = "enable-ipv6";
    inline constexpr std::string_view FORCE_RELAY = "force-relay";
    inline constexpr std::string_view VIDEO_CODEC = "video-codec";
    inline constexpr std::string_view AUDIO_INPUT = "audio-input-device";
    inline constexpr std::string_view AUDIO_OUTPUT = "audio-output-device";
    inline constexpr std::string_view THEME = "theme";
    inline constexpr std::string_view LANG = "lang";
    inline constexpr std::string_view VIEW_ONLY = "view-only";
    inline constexpr std::string_view SHOW_REMOTE_CURSOR = "show-remote-cursor";
    inline constexpr std::string_view LOCK_AFTER_SESSION_END = "lock-after-session-end";
    inline constexpr std::string_view ENABLE_FILE_TRANSFER = "enable-file-transfer";
    inline constexpr std::string_view ENABLE_CLIPBOARD = "enable-clipboard";
    inline constexpr std::string_view ENABLE_AUDIO = "enable-audio";
    inline constexpr std::string_view ENABLE_TUNNEL = "enable-tunnel";
    inline constexpr std::string_view STOP_SERVICE = "stop-service";
    inline constexpr std::string_view INCOMING_ONLY = "incoming-only";
    inline constexpr std::string_view OUTGOING_ONLY = "outgoing-only";
    inline constexpr std::string_view ALLOW_LAN_DISCOVERY = "allow-lan-discovery";
    inline constexpr std::string_view DIRECT_SERVER = "direct-server";
    inline constexpr std::string_view API_SERVER = "api-server";
    inline constexpr std::string_view KEY = "key";
    inline constexpr std::string_view TOKEN = "token";
    inline constexpr std::string_view APPROVE_MODE = "approve-mode";
    inline constexpr std::string_view PRIVACY_MODE = "privacy-mode";
    inline constexpr std::string_view DISPLAY_INDEX = "display-index";
    inline constexpr std::string_view SCALE = "scale";
    inline constexpr std::string_view QUALITY = "quality";
    inline constexpr std::string_view BITRATE = "bitrate";
    inline constexpr std::string_view FPS = "fps";
    inline constexpr std::string_view ENABLE_KEYBOARD = "enable-keyboard";
    inline constexpr std::string_view ENABLE_CLIPBOARD_FILE = "enable-clipboard-file";
}

// Resolution
struct Resolution {
    uint32_t width = 1920;
    uint32_t height = 1080;
    
    bool operator==(const Resolution& o) const {
        return width == o.width && height == o.height;
    }
};

// NAT type enum
enum class NatType : int32_t {
    UNKNOWN_NAT = 0,
    OPEN_INTERNET = 1,
    FULL_CONE = 2,
    RESTRICTED_CONE = 3,
    PORT_RESTRICTED_CONE = 4,
    SYMMETRIC = 5,
};

// Connection type
enum class ConnType : int32_t {
    DEFAULT_CONN = 0,
    FILE_TRANSFER = 1,
    PORT_FORWARD = 2,
    RDP = 3,
};

// Peer information
struct PeerConfig {
    std::string id;
    std::string username;
    std::string hostname;
    std::string platform;
    std::string alias;
    bool online = false;
    NatType nat_type = NatType::UNKNOWN_NAT;
    std::vector<std::string> tags;
};

// Local configuration
struct LocalConfig {
    std::string id;
    std::string password;
    std::string salt;
    std::string key_pair_sk;
    std::string key_pair_pk;
    std::map<std::string, std::string> options;
    std::vector<PeerConfig> peers;
    std::vector<std::string> favourites;
    std::vector<std::string> recent;
};

/// Configuration manager (singleton)
class Config {
public:
    static Config& instance();

    // Identity
    static std::string get_id();
    static void set_id(const std::string& id);
    
    // Key pair
    static std::pair<std::string, std::string> get_key_pair();
    static void set_key_pair(const std::string& sk, const std::string& pk);
    static void set_key_confirmed(bool confirmed);

    // Password
    static std::string get_password();
    static void set_password(const std::string& pw);
    static bool is_password_set();

    // Options
    static std::string get_option(const std::string& key);
    static void set_option(const std::string& key, const std::string& value);
    static void set_option_bool(const std::string& key, bool value);
    static bool option2bool(const std::string& key, const std::string& val);
    
    // Server settings
    static std::string get_rendezvous_server();
    static void set_rendezvous_server(const std::string& server);
    static std::string get_relay_server();
    
    // Network
    static bool is_force_relay();
    static bool is_incoming_only();
    static bool is_outgoing_only();
    static bool get_enable_udp();
    static bool get_enable_ipv6();
    static std::string get_any_listen_addr(bool ipv4);
    
    // Persistence
    void load(const std::string& path);
    void save(const std::string& path);
    void apply_option(const std::string& key, const std::string& value);

    // Peers
    void add_peer(const PeerConfig& peer);
    void remove_peer(const std::string& id);
    std::optional<PeerConfig> get_peer(const std::string& id) const;
    std::vector<PeerConfig> get_peers() const;
    void update_peer_online(const std::string& id, bool online);

private:
    Config() = default;
    LocalConfig cfg_;
    mutable std::mutex mutex_;
    std::string config_path_;
};

// Common utility functions
std::string get_version_number();
std::string get_app_name();
std::string get_hostname();
std::string get_username();
std::string get_platform_name();
std::string get_device_id();

// Base64
std::string base64_encode(const uint8_t* data, size_t len);
std::vector<uint8_t> base64_decode(const std::string& s);

// IP helpers
bool is_ip_str(const std::string& s);
bool is_ipv4_str(const std::string& s);
bool is_domain_port_str(const std::string& s);
std::string check_port(const std::string& addr, uint16_t port);
std::string ipv4_to_ipv6(const std::string& addr, bool ipv4);

// Allowed error (ignore)
template<typename T>
void allow_err(T&&) {}

// Random
uint64_t random_u64();
int32_t random_i32();
std::string random_string(size_t len);

} // namespace cppdesk::common
