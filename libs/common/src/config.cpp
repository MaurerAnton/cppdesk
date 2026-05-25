#include "common/config.hpp"
#include "common/crypto.hpp"
#include <fstream>
#include <filesystem>
#include <random>
#include <algorithm>

namespace cppdesk::common {

namespace {
    std::string get_config_path() {
        const char* home = getenv("HOME");
        if (!home) home = getenv("USERPROFILE");
        if (!home) home = "/tmp";
        std::filesystem::path p(home);
        p /= ".config/cppdesk";
        std::filesystem::create_directories(p);
        p /= "config.json";
        return p.string();
    }
}

Config& Config::instance() {
    static Config inst;
    return inst;
}

std::string Config::get_id() {
    auto& cfg = instance();
    std::lock_guard lk(cfg.mutex_);
    if (cfg.cfg_.id.empty()) {
        cfg.cfg_.id = get_device_id();
        if (cfg.cfg_.id.empty()) {
            cfg.cfg_.id = "cppdesk-" + crypto::encode64(
                crypto::random_bytes(8).data(), 8).substr(0, 12);
        }
    }
    return cfg.cfg_.id;
}

void Config::set_id(const std::string& id) {
    auto& cfg = instance();
    std::lock_guard lk(cfg.mutex_);
    cfg.cfg_.id = id;
}

std::pair<std::string, std::string> Config::get_key_pair() {
    auto& cfg = instance();
    std::lock_guard lk(cfg.mutex_);
    if (cfg.cfg_.key_pair_sk.empty()) {
        auto kp = crypto::generate_sign_keypair();
        cfg.cfg_.key_pair_sk = std::string(
            reinterpret_cast<char*>(kp.sk.data()), crypto::SIGN_SECRET_KEY_BYTES);
        cfg.cfg_.key_pair_pk = std::string(
            reinterpret_cast<char*>(kp.pk.data()), crypto::SIGN_PUBLIC_KEY_BYTES);
    }
    return {cfg.cfg_.key_pair_sk, cfg.cfg_.key_pair_pk};
}

void Config::set_key_pair(const std::string& sk, const std::string& pk) {
    auto& cfg = instance();
    std::lock_guard lk(cfg.mutex_);
    cfg.cfg_.key_pair_sk = sk;
    cfg.cfg_.key_pair_pk = pk;
}

void Config::set_key_confirmed(bool confirmed) {
    // Placeholder
    (void)confirmed;
}

std::string Config::get_password() {
    auto& cfg = instance();
    std::lock_guard lk(cfg.mutex_);
    return cfg.cfg_.password;
}

void Config::set_password(const std::string& pw) {
    auto& cfg = instance();
    std::lock_guard lk(cfg.mutex_);
    cfg.cfg_.password = pw;
}

bool Config::is_password_set() {
    return !get_password().empty();
}

std::string Config::get_option(const std::string& key) {
    auto& cfg = instance();
    std::lock_guard lk(cfg.mutex_);
    auto it = cfg.cfg_.options.find(key);
    return it != cfg.cfg_.options.end() ? it->second : "";
}

void Config::set_option(const std::string& key, const std::string& value) {
    auto& cfg = instance();
    std::lock_guard lk(cfg.mutex_);
    cfg.cfg_.options[key] = value;
}

void Config::set_option_bool(const std::string& key, bool value) {
    set_option(key, value ? "true" : "false");
}

bool Config::option2bool(const std::string&, const std::string& val) {
    return val == "true" || val == "Y" || val == "1" || val == "yes";
}

std::string Config::get_rendezvous_server() {
    auto server = get_option("custom-rendezvous-server");
    if (!server.empty()) return server;
    return RENDEZVOUS_SERVERS[0];
}

void Config::set_rendezvous_server(const std::string& server) {
    set_option("custom-rendezvous-server", server);
}

std::string Config::get_relay_server() {
    auto server = get_option("relay-server");
    return server.empty() ? get_rendezvous_server() : server;
}

bool Config::is_force_relay() {
    return option2bool("force-relay", get_option("force-relay"));
}

bool Config::is_incoming_only() {
    return option2bool("incoming-only", get_option("incoming-only"));
}

bool Config::is_outgoing_only() {
    return option2bool("outgoing-only", get_option("outgoing-only"));
}

bool Config::get_enable_udp() {
    return !option2bool("disable-udp", get_option("disable-udp"));
}

bool Config::get_enable_ipv6() {
    return option2bool("enable-ipv6", get_option("enable-ipv6"));
}

std::string Config::get_any_listen_addr(bool ipv4) {
    return ipv4 ? "0.0.0.0" : "::";
}

void Config::load(const std::string& path) {
    std::lock_guard lk(mutex_);
    config_path_ = path.empty() ? get_config_path() : path;
    try {
        if (std::filesystem::exists(config_path_)) {
            std::ifstream f(config_path_);
            json j = json::parse(f);
            if (j.contains("id")) cfg_.id = j["id"].get<std::string>();
            if (j.contains("password")) cfg_.password = j["password"].get<std::string>();
            if (j.contains("salt")) cfg_.salt = j["salt"].get<std::string>();
            if (j.contains("key_pair_sk")) cfg_.key_pair_sk = j["key_pair_sk"].get<std::string>();
            if (j.contains("key_pair_pk")) cfg_.key_pair_pk = j["key_pair_pk"].get<std::string>();
            if (j.contains("options") && j["options"].is_object()) {
                for (auto& [k, v] : j["options"].items()) {
                    cfg_.options[k] = v.get<std::string>();
                }
            }
        }
    } catch (...) {}
}

void Config::save(const std::string& path) {
    std::lock_guard lk(mutex_);
    auto p = path.empty() ? config_path_ : path;
    if (p.empty()) p = get_config_path();
    try {
        json j;
        j["id"] = cfg_.id;
        j["password"] = cfg_.password;
        j["salt"] = cfg_.salt;
        j["key_pair_sk"] = cfg_.key_pair_sk;
        j["key_pair_pk"] = cfg_.key_pair_pk;
        j["options"] = cfg_.options;
        std::ofstream f(p);
        f << j.dump(4);
    } catch (...) {}
}

void Config::apply_option(const std::string& key, const std::string& value) {
    set_option(key, value);
}

void Config::add_peer(const PeerConfig& peer) {
    std::lock_guard lk(mutex_);
    auto it = std::find_if(cfg_.peers.begin(), cfg_.peers.end(),
        [&](const PeerConfig& p) { return p.id == peer.id; });
    if (it != cfg_.peers.end()) *it = peer;
    else cfg_.peers.push_back(peer);
}

void Config::remove_peer(const std::string& id) {
    std::lock_guard lk(mutex_);
    cfg_.peers.erase(std::remove_if(cfg_.peers.begin(), cfg_.peers.end(),
        [&](const PeerConfig& p) { return p.id == id; }), cfg_.peers.end());
}

std::optional<PeerConfig> Config::get_peer(const std::string& id) const {
    std::lock_guard lk(mutex_);
    auto it = std::find_if(cfg_.peers.begin(), cfg_.peers.end(),
        [&](const PeerConfig& p) { return p.id == id; });
    if (it != cfg_.peers.end()) return *it;
    return std::nullopt;
}

std::vector<PeerConfig> Config::get_peers() const {
    std::lock_guard lk(mutex_);
    return cfg_.peers;
}

void Config::update_peer_online(const std::string& id, bool online) {
    std::lock_guard lk(mutex_);
    auto it = std::find_if(cfg_.peers.begin(), cfg_.peers.end(),
        [&](const PeerConfig& p) { return p.id == id; });
    if (it != cfg_.peers.end()) it->online = online;
}

// Utility implementations
std::string get_version_number() {
    return "1.3.0-cpp";
}

std::string get_app_name() {
    return "cppdesk";
}

std::string get_hostname() {
    char buf[256] = {};
    gethostname(buf, sizeof(buf));
    return buf;
}

std::string get_username() {
    const char* user = getenv("USER");
    if (!user) user = getenv("USERNAME");
    return user ? user : "unknown";
}

std::string get_platform_name() {
#if defined(_WIN32) || defined(_WIN64)
    return "Windows";
#elif defined(__APPLE__)
    return "Mac OS";
#elif defined(__ANDROID__)
    return "Android";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

// Generate a device ID
std::string get_device_id() {
    std::string host = get_hostname();
    std::string user = get_username();
    std::string combined = host + ":" + user + ":" + get_platform_name();
    auto hash = crypto::sha256(combined);
    return crypto::encode64(hash.data(), 8).substr(0, 12);
}

std::string base64_encode(const uint8_t* data, size_t len) {
    return crypto::encode64(data, len);
}

std::vector<uint8_t> base64_decode(const std::string& s) {
    return crypto::decode64(s);
}

bool is_ip_str(const std::string& s) {
    // Simple check: count dots and digits
    int dots = 0;
    for (char c : s) {
        if (c == '.') dots++;
        else if (!isdigit(c)) return false;
    }
    return dots == 3;
}

bool is_ipv4_str(const std::string& s) {
    return is_ip_str(s);
}

bool is_domain_port_str(const std::string& s) {
    auto pos = s.find(':');
    return pos != std::string::npos && pos > 0 && pos < s.size() - 1;
}

std::string check_port(const std::string& addr, uint16_t port) {
    if (addr.find(':') != std::string::npos) return addr;
    return addr + ":" + std::to_string(port);
}

std::string ipv4_to_ipv6(const std::string& addr, bool ipv4) {
    if (ipv4) return addr;
    // Return as-is for now
    return addr;
}

uint64_t random_u64() {
    static std::mt19937_64 rng(std::random_device{}());
    return rng();
}

int32_t random_i32() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int32_t> dist;
    return dist(rng);
}

std::string random_string(size_t len) {
    static const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, sizeof(chars) - 2);
    std::string s(len, '\0');
    for (size_t i = 0; i < len; i++) s[i] = chars[dist(rng)];
    return s;
}

} // namespace cppdesk::common
