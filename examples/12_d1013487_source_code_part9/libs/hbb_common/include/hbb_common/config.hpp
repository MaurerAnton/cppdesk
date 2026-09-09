// config.hpp — translation of libs/hbb_common/src/config.rs (full).
//
// Idiomatic mapping:
// - `Arc<RwLock<Config>>` / `Arc<RwLock<Config2>>` → static `std::shared_mutex`
//   guards (single-writer / multi-reader). Global state is process-wide like
//   `lazy_static` in Rust.
// - `Arc<Mutex<HashMap>>` ONLINE → `std::mutex` + `std::unordered_map`.
// - Android/iOS `APP_DIR` → process-global `std::optional<std::string>`.
// - `confy` load/store → our minimal TOML parser (toml.hpp).
// - `sodiumoxide::sign::gen_keypair` → `crypto::sign_gen_keypair` (libsodium,
//   step 12); `key_pair` lazy-gens on first `get_key_pair` like upstream.
// - `mac_address::get_mac_address` → deferred (platform step); `get_auto_id()`
//   returns empty for now.
// - `rand::thread_rng` → `std::mt19937` seeded once.
// - `log::debug!/error!/info!` → `HBB_ALLOW_ERR` macro (silent, like upstream
//   without initialized logger). A real logger arrives with the logging step.
// - All `pub fn` methods keep their names + signatures (arguments by value /
//   reference as upstream). Return-by-value for small structs (`Size` as
//   `std::array<int32_t,4>`).
// - `CONFIG2.write().unwrap().rendezvous_server.clone()` pattern: read
//   guarded copy, release lock, operate on copy — identical to Rust.
// - Config file paths: `ProjectDirs::from("", org, APP_NAME)` →
//   `$XDG_CONFIG_HOME/RustDesk` (Linux/Unix), `~/Library/Preferences/RustDesk`
//   (macOS), `%APPDATA%/RustDesk` (Windows). `patch()` for Windows root-user
//   redirection carried over 1:1.
// - Constants: exact strings/values from config.rs (ICON via generated icon.hpp).

#pragma once

#include <hbb_common/bytes_codec.hpp>
#include <hbb_common/compress.hpp>
#include <hbb_common/toml.hpp>
#include <hbb_common/util.hpp>

#include <array>
#include <filesystem>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace hbb_common {

inline constexpr char kAppName[] = "RustDesk";
inline constexpr char kBindInterface[] = "0.0.0.0";
inline constexpr uint64_t kRendezvousTimeoutMs = 12000;
inline constexpr uint64_t kConnectTimeoutMs = 18000;
inline constexpr int32_t kCompressLevel = 3;
inline constexpr int32_t kSerial = 0;
inline constexpr int32_t kRendezvousPort = 21116;
inline constexpr int32_t kRelayPort = 21117;

inline constexpr const char* kRendezvousServers[] = {
    "rs-sg.rustdesk.com",
    "rs-cn.rustdesk.com",
};
inline constexpr size_t kRendezvousServersCount = 2;

using Size = std::array<int32_t, 4>;

struct Config {
    std::string id = "";
    std::string password = "";
    std::string salt = "";
    std::array<std::vector<uint8_t>, 2> key_pair = {std::vector<uint8_t>(), std::vector<uint8_t>()};  // sk, pk
    bool key_confirmed = false;
    std::unordered_map<std::string, bool> keys_confirmed;
};

struct Config2 {
    std::string remote_id = "";
    Size size = {0, 0, 0, 0};
    std::string rendezvous_server = "";
    int32_t nat_type = 0;
    int32_t serial = 0;
    std::unordered_map<std::string, std::string> options;
};

struct PeerInfoSerde {
    std::string username = "";
    std::string hostname = "";
    std::string platform = "";
};

struct PeerConfig {
    std::vector<uint8_t> password;
    Size size = {0, 0, 0, 0};
    Size size_ft = {0, 0, 0, 0};
    Size size_pf = {0, 0, 0, 0};
    std::string view_style = "";
    std::string image_quality = "";
    std::vector<int32_t> custom_image_quality;
    bool show_remote_cursor = false;
    bool lock_after_session_end = false;
    bool privacy_mode = false;
    std::vector<std::tuple<int32_t, std::string, int32_t>> port_forwards;
    int32_t direct_failures = 0;
    bool disable_audio = false;
    bool disable_clipboard = false;
    std::unordered_map<std::string, std::string> options;
    PeerInfoSerde info;
};

// --- global state (process-wide, like lazy_static) ----------------------------

std::shared_mutex& config_lock();
std::shared_mutex& config2_lock();
std::mutex& online_lock();
std::unordered_map<std::string, int64_t>& online_map();

std::optional<std::string>& app_dir_opt();  // Android/iOS

std::mt19937& rng();  // seeded once

// --- public API (names + signatures match config.rs) --------------------------

// load is implicit on first access via get_*; no explicit load() call needed.
Config config_load();
Config2 config2_load();
Config& config_get();  // read lock held by caller
Config2& config2_get();  // read lock held by caller

// getters/setters (lock, read/write, store if changed; return values by copy)

std::string get_auto_id();          // mac_address deferred → empty string
std::string get_auto_password();
bool get_key_confirmed();
void set_key_confirmed(bool v);
bool get_host_key_confirmed(const std::string& host);
void set_host_key_confirmed(const std::string& host, bool v);
void set_key_pair(const std::array<std::vector<uint8_t>, 2>& pair);
std::array<std::vector<uint8_t>, 2> get_key_pair();

std::string get_id();  // auto-generates via get_auto_id() if empty
void set_id(const std::string& id);
std::unordered_map<std::string, std::string> get_options();
void set_options(const std::unordered_map<std::string, std::string>& v);
std::string get_option(const std::string& k);
void set_option(const std::string& k, const std::string& v);
void update_id();

void set_password(const std::string& password);
std::string get_password();

void set_salt(const std::string& salt);
std::string get_salt();

Size get_size();
void set_size(int32_t x, int32_t y, int32_t w, int32_t h);

void set_remote_id(const std::string& remote_id);
std::string get_remote_id();

// Config2 direct getters (read lock)
int32_t get_nat_type();
void set_nat_type(int32_t nat_type);
int32_t get_serial();
void set_serial(int32_t serial);

// file helpers (upstream names + signatures)
std::filesystem::path config_file(const std::string& suffix = "");
std::filesystem::path config_path(const std::string& name);
std::filesystem::path get_home();

std::filesystem::path log_path();

std::string ipc_path(const std::string& postfix);
std::filesystem::path icon_path();

ResolvedAddr get_any_listen_addr();
ResolvedAddr get_rendezvous_server();
std::vector<std::string> get_rendezvous_servers();

void reset_online();
void update_latency(const std::string& host, int64_t latency);

void import_config(const std::string& from);
std::string save_tmp();

// PeerConfig persistence
PeerConfig peer_load(const std::string& id);
void peer_store(const PeerConfig& cfg, const std::string& id);
void peer_remove(const std::string& id);
std::vector<std::tuple<std::string, std::chrono::system_clock::time_point, PeerInfoSerde>> peers();

// --- internal TOML serialization (exposed for tests) --------------------------

toml::Table Config_to_toml(const Config&);
Config Config_from_toml(const toml::Table&);
toml::Table Config2_to_toml(const Config2&);
Config2 Config2_from_toml(const toml::Table&);
toml::Table PeerConfig_to_toml(const PeerConfig&);
PeerConfig PeerConfig_from_toml(const toml::Table&);

}  // namespace hbb_common