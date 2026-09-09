// config.cpp — implementation of config.hpp
//
// Porting notes:
// - All global state (CONFIG, CONFIG2, ONLINE, APP_DIR) as function-local
//   statics with thread-safe initialization (C++11 guarantees).
// - Locking: shared_mutex for config/config2 (RWLock parity), mutex for ONLINE.
// - `get_key_pair()`: upstream lazily generates keypair via sodiumoxide
//   on first call; here we stub it to throw until crypto step lands.
// - `get_auto_id()`: upstream uses mac_address crate; stub returns empty.
// - `log::debug/error/info` → HBB_ALLOW_ERR (silent without logger).
// - Path logic mirrors `patch()` / `ProjectDirs::from("", org, APP_NAME)`
//   with macOS `Application Support` → `Preferences` and Windows root-user
//   redirection (system32/config/systemprofile → ServiceProfiles/LocalService).
// - TOML persistence uses our minimal parser/emitter (toml.hpp).

#include <hbb_common/config.hpp>

#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <shared_mutex>
#include <algorithm>

namespace hbb_common {

// --- global state (definitions for the config.hpp declarations) ----------------

std::shared_mutex& config_lock() {
    static std::shared_mutex m;
    return m;
}

std::shared_mutex& config2_lock() {
    static std::shared_mutex m;
    return m;
}

std::mutex& online_lock() {
    static std::mutex m;
    return m;
}

std::unordered_map<std::string, int64_t>& online_map() {
    static std::unordered_map<std::string, int64_t> m;
    return m;
}

std::optional<std::string>& app_dir_opt() {
    static std::optional<std::string> m;
    return m;
}

std::mt19937& rng() {
    static std::mt19937 m([]() {
        std::random_device rd;
        return rd();
    }());
    return m;
}

// --- internal TOML serialization helpers -------------------------------------

toml::Value vector_to_toml_array(const std::vector<uint8_t>& v) {
    toml::Value::Array a;
    a.reserve(v.size());
    for (uint8_t byte : v) {
        a.emplace_back(static_cast<int64_t>(byte));
    }
    return toml::Value(std::move(a));
}

toml::Value size_to_toml(const Size& s) {
    toml::Value::Array a;
    for (int i = 0; i < 4; ++i) {
        a.emplace_back(static_cast<int64_t>(s[i]));
    }
    return toml::Value(std::move(a));
}

Size toml_to_size(const toml::Value& v) {
    Size s = {0, 0, 0, 0};
    if (v.is_array()) {
        const auto& arr = std::get<toml::Value::Array>(v.data);
        for (size_t i = 0; i < arr.size() && i < 4; ++i) {
            if (arr[i].is_int()) {
                s[i] = static_cast<int32_t>(std::get<int64_t>(arr[i].data));
            }
        }
    }
    return s;
}

PeerInfoSerde toml_to_peer_info(const toml::Table& t) {
    PeerInfoSerde p;
    auto it = t.find("username");
    if (it != t.end() && it->second.is_string()) {
        p.username = std::get<std::string>(it->second.data);
    }
    it = t.find("hostname");
    if (it != t.end() && it->second.is_string()) {
        p.hostname = std::get<std::string>(it->second.data);
    }
    it = t.find("platform");
    if (it != t.end() && it->second.is_string()) {
        p.platform = std::get<std::string>(it->second.data);
    }
    return p;
}

toml::Table Config_to_toml(const Config& c) {
    toml::Table t;
    t["id"] = toml::Value(c.id);
    t["password"] = toml::Value(c.password);
    t["salt"] = toml::Value(c.salt);
    toml::Value::Array kp;
    kp.emplace_back(vector_to_toml_array(c.key_pair[0]));
    kp.emplace_back(vector_to_toml_array(c.key_pair[1]));
    t["key_pair"] = toml::Value(std::move(kp));
    t["key_confirmed"] = toml::Value(c.key_confirmed);
    toml::Value::Array kc_arr;
    for (const auto& [k, v] : c.keys_confirmed) {
        toml::Value::Array kv;
        kv.emplace_back(toml::Value(k));
        kv.emplace_back(toml::Value(v));
        kc_arr.emplace_back(toml::Value(std::move(kv)));
    }
    t["keys_confirmed"] = toml::Value(std::move(kc_arr));
    return t;
}

Config Config_from_toml(const toml::Table& root) {
    Config c;
    auto it = root.find("id");
    if (it != root.end() && it->second.is_string()) {
        c.id = std::get<std::string>(it->second.data);
    }
    it = root.find("password");
    if (it != root.end() && it->second.is_string()) {
        c.password = std::get<std::string>(it->second.data);
    }
    it = root.find("salt");
    if (it != root.end() && it->second.is_string()) {
        c.salt = std::get<std::string>(it->second.data);
    }
    it = root.find("key_pair");
    if (it != root.end() && it->second.is_array()) {
        const auto& arr = std::get<toml::Value::Array>(it->second.data);
        if (arr.size() >= 2) {
            if (arr[0].is_array()) {
                const auto& sk_arr = std::get<toml::Value::Array>(arr[0].data);
                for (const auto& v : sk_arr) {
                    if (v.is_int()) {
                        c.key_pair[0].push_back(static_cast<uint8_t>(std::get<int64_t>(v.data)));
                    }
                }
            }
            if (arr[1].is_array()) {
                const auto& pk_arr = std::get<toml::Value::Array>(arr[1].data);
                for (const auto& v : pk_arr) {
                    if (v.is_int()) {
                        c.key_pair[1].push_back(static_cast<uint8_t>(std::get<int64_t>(v.data)));
                    }
                }
            }
        }
    }
    it = root.find("key_confirmed");
    if (it != root.end() && it->second.is_bool()) {
        c.key_confirmed = std::get<bool>(it->second.data);
    }
    it = root.find("keys_confirmed");
    if (it != root.end() && it->second.is_array()) {
        const auto& arr = std::get<toml::Value::Array>(it->second.data);
        for (const auto& kv : arr) {
            if (kv.is_array()) {
                const auto& pair = std::get<toml::Value::Array>(kv.data);
                if (pair.size() >= 2 && pair[0].is_string() && pair[1].is_bool()) {
                    c.keys_confirmed[std::get<std::string>(pair[0].data)] = std::get<bool>(pair[1].data);
                }
            }
        }
    }
    return c;
}

toml::Table Config2_to_toml(const Config2& c) {
    toml::Table t;
    t["remote_id"] = toml::Value(c.remote_id);
    t["size"] = size_to_toml(c.size);
    t["rendezvous_server"] = toml::Value(c.rendezvous_server);
    t["nat_type"] = toml::Value(static_cast<int64_t>(c.nat_type));
    t["serial"] = toml::Value(static_cast<int64_t>(c.serial));
    toml::Value::Array opt;
    for (const auto& [k, v] : c.options) {
        toml::Value::Array kv;
        kv.emplace_back(toml::Value(k));
        kv.emplace_back(toml::Value(v));
        opt.emplace_back(toml::Value(std::move(kv)));
    }
    t["options"] = toml::Value(std::move(opt));
    return t;
}

Config2 Config2_from_toml(const toml::Table& root) {
    Config2 c;
    auto it = root.find("remote_id");
    if (it != root.end() && it->second.is_string()) {
        c.remote_id = std::get<std::string>(it->second.data);
    }
    it = root.find("size");
    if (it != root.end()) {
        c.size = toml_to_size(it->second);
    }
    it = root.find("rendezvous_server");
    if (it != root.end() && it->second.is_string()) {
        c.rendezvous_server = std::get<std::string>(it->second.data);
    }
    it = root.find("nat_type");
    if (it != root.end() && it->second.is_int()) {
        c.nat_type = static_cast<int32_t>(std::get<int64_t>(it->second.data));
    }
    it = root.find("serial");
    if (it != root.end() && it->second.is_int()) {
        c.serial = static_cast<int32_t>(std::get<int64_t>(it->second.data));
    }
    it = root.find("options");
    if (it != root.end() && it->second.is_array()) {
        const auto& arr = std::get<toml::Value::Array>(it->second.data);
        for (const auto& kv : arr) {
            if (kv.is_array()) {
                const auto& pair = std::get<toml::Value::Array>(kv.data);
                if (pair.size() >= 2 && pair[0].is_string() && pair[1].is_string()) {
                    c.options[std::get<std::string>(pair[0].data)] = std::get<std::string>(pair[1].data);
                }
            }
        }
    }
    return c;
}

toml::Table PeerConfig_to_toml(const PeerConfig& c) {
    toml::Table t;
    t["password"] = vector_to_toml_array(c.password);
    t["size"] = size_to_toml(c.size);
    t["size_ft"] = size_to_toml(c.size_ft);
    t["size_pf"] = size_to_toml(c.size_pf);
    t["view_style"] = toml::Value(c.view_style);
    t["image_quality"] = toml::Value(c.image_quality);
    toml::Value::Array cq;
    for (int32_t v : c.custom_image_quality) {
        cq.emplace_back(toml::Value(static_cast<int64_t>(v)));
    }
    t["custom_image_quality"] = toml::Value(std::move(cq));
    t["show_remote_cursor"] = toml::Value(c.show_remote_cursor);
    t["lock_after_session_end"] = toml::Value(c.lock_after_session_end);
    t["privacy_mode"] = toml::Value(c.privacy_mode);
    toml::Value::Array pf;
    for (const auto& [a, b, c] : c.port_forwards) {
        toml::Value::Array triple;
        triple.emplace_back(toml::Value(static_cast<int64_t>(a)));
        triple.emplace_back(toml::Value(b));
        triple.emplace_back(toml::Value(static_cast<int64_t>(c)));
        pf.emplace_back(toml::Value(std::move(triple)));
    }
    t["port_forwards"] = toml::Value(std::move(pf));
    t["direct_failures"] = toml::Value(static_cast<int64_t>(c.direct_failures));
    t["disable_audio"] = toml::Value(c.disable_audio);
    t["disable_clipboard"] = toml::Value(c.disable_clipboard);
    toml::Value::Array opt;
    for (const auto& [k, v] : c.options) {
        toml::Value::Array kv;
        kv.emplace_back(toml::Value(k));
        kv.emplace_back(toml::Value(v));
        opt.emplace_back(toml::Value(std::move(kv)));
    }
    t["options"] = toml::Value(std::move(opt));
    toml::Value::Array info_arr;
    info_arr.emplace_back(toml::Value("username"));
    info_arr.emplace_back(toml::Value(c.info.username));
    info_arr.emplace_back(toml::Value("hostname"));
    info_arr.emplace_back(toml::Value(c.info.hostname));
    info_arr.emplace_back(toml::Value("platform"));
    info_arr.emplace_back(toml::Value(c.info.platform));
    t["info"] = toml::Value(std::move(info_arr));
    return t;
}

PeerConfig PeerConfig_from_toml(const toml::Table& root) {
    PeerConfig c;
    auto it = root.find("password");
    if (it != root.end() && it->second.is_array()) {
        const auto& arr = std::get<toml::Value::Array>(it->second.data);
        for (const auto& v : arr) {
            if (v.is_int()) {
                c.password.push_back(static_cast<uint8_t>(std::get<int64_t>(v.data)));
            }
        }
    }
    it = root.find("size");
    if (it != root.end()) c.size = toml_to_size(it->second);
    it = root.find("size_ft");
    if (it != root.end()) c.size_ft = toml_to_size(it->second);
    it = root.find("size_pf");
    if (it != root.end()) c.size_pf = toml_to_size(it->second);
    it = root.find("view_style");
    if (it != root.end() && it->second.is_string()) {
        c.view_style = std::get<std::string>(it->second.data);
    }
    it = root.find("image_quality");
    if (it != root.end() && it->second.is_string()) {
        c.image_quality = std::get<std::string>(it->second.data);
    }
    it = root.find("custom_image_quality");
    if (it != root.end() && it->second.is_array()) {
        const auto& arr = std::get<toml::Value::Array>(it->second.data);
        for (const auto& v : arr) {
            if (v.is_int()) {
                c.custom_image_quality.push_back(static_cast<int32_t>(std::get<int64_t>(v.data)));
            }
        }
    }
    it = root.find("show_remote_cursor");
    if (it != root.end() && it->second.is_bool()) {
        c.show_remote_cursor = std::get<bool>(it->second.data);
    }
    it = root.find("lock_after_session_end");
    if (it != root.end() && it->second.is_bool()) {
        c.lock_after_session_end = std::get<bool>(it->second.data);
    }
    it = root.find("privacy_mode");
    if (it != root.end() && it->second.is_bool()) {
        c.privacy_mode = std::get<bool>(it->second.data);
    }
    it = root.find("port_forwards");
    if (it != root.end() && it->second.is_array()) {
        const auto& arr = std::get<toml::Value::Array>(it->second.data);
        for (const auto& trip : arr) {
            if (trip.is_array()) {
                const auto& t = std::get<toml::Value::Array>(trip.data);
                if (t.size() >= 3 && t[0].is_int() && t[1].is_string() && t[2].is_int()) {
                    c.port_forwards.emplace_back(
                        static_cast<int32_t>(std::get<int64_t>(t[0].data)),
                        std::get<std::string>(t[1].data),
                        static_cast<int32_t>(std::get<int64_t>(t[2].data)));
                }
            }
        }
    }
    it = root.find("direct_failures");
    if (it != root.end() && it->second.is_int()) {
        c.direct_failures = static_cast<int32_t>(std::get<int64_t>(it->second.data));
    }
    it = root.find("disable_audio");
    if (it != root.end() && it->second.is_bool()) {
        c.disable_audio = std::get<bool>(it->second.data);
    }
    it = root.find("disable_clipboard");
    if (it != root.end() && it->second.is_bool()) {
        c.disable_clipboard = std::get<bool>(it->second.data);
    }
    it = root.find("options");
    if (it != root.end() && it->second.is_array()) {
        const auto& arr = std::get<toml::Value::Array>(it->second.data);
        for (const auto& kv : arr) {
            if (kv.is_array()) {
                const auto& pair = std::get<toml::Value::Array>(kv.data);
                if (pair.size() >= 2 && pair[0].is_string() && pair[1].is_string()) {
                    c.options[std::get<std::string>(pair[0].data)] = std::get<std::string>(pair[1].data);
                }
            }
        }
    }
    it = root.find("info");
    if (it != root.end() && it->second.is_array()) {
        const auto& arr = std::get<toml::Value::Array>(it->second.data);
        for (size_t i = 0; i + 1 < arr.size(); i += 2) {
            if (arr[i].is_string() && arr[i + 1].is_string()) {
                const std::string k = std::get<std::string>(arr[i].data);
                const std::string v = std::get<std::string>(arr[i + 1].data);
                if (k == "username") c.info.username = v;
                else if (k == "hostname") c.info.hostname = v;
                else if (k == "platform") c.info.platform = v;
            }
        }
    }
    return c;
}

// --- Config file paths + persistence + accessors (config.rs `impl Config`) ---
//
// FIDELITY NOTE (directories-next 2.0): the exact nesting/case of the project
// directory (Linux case of APP_NAME, org nesting on macOS/Windows) is
// implemented per the crate's documented semantics but has NOT been verified
// against a real install — each branch is a small isolated function so a
// one-line fix suffices if a mismatch is found.

namespace {

namespace fs = std::filesystem;

std::string getenv_str(const char* key) {
    const char* v = ::getenv(key);
    return v ? v : "";
}

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

// `patch()` parity: Windows service-user redirect + macOS Preferences move.
fs::path patch_path(fs::path p) {
    std::string s = p.string();
#if defined(_WIN32)
    s = replace_all(s, "system32\\config\\systemprofile", "ServiceProfiles\\LocalService");
#elif defined(__APPLE__)
    s = replace_all(s, "Application Support", "Preferences");
#endif
    return fs::path(s);
}

// `ProjectDirs::from("", org, APP_NAME).config_dir()` parity (UNTESTED on
// macOS/Windows — guarded branches, see fidelity note above).
fs::path config_base_dir() {
#if defined(_WIN32)
    const std::string appdata = getenv_str("APPDATA");
    fs::path base = appdata.empty() ? fs::temp_directory_path() : fs::path(appdata);
    base /= kAppName;  // org is "" on non-macOS
    return base;
#elif defined(__APPLE__)
    fs::path base = get_home() / "Library" / "Application Support" / "com.carriez" / kAppName;
    return base;
#else
    const std::string xdg = getenv_str("XDG_CONFIG_HOME");
    fs::path base = xdg.empty() ? (get_home() / ".config") : fs::path(xdg);
    base /= kAppName;
    return base;
#endif
}

std::string read_file_str(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        return "";
    }
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void write_file_str(const fs::path& p, const std::string& content) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (f) {
        f << content;
    }
}

template <typename T, typename FromTOML>
T load_doc(const fs::path& file, FromTOML from) {
    const std::string text = read_file_str(file);
    if (text.empty()) {
        return T{};
    }
    try {
        return from(toml::parse(text).root);
    } catch (...) {
        return T{};  // log::error parity: silent without a logger
    }
}

void store_doc(const fs::path& file, const toml::Table& root) {
    toml::Document doc;
    doc.root = root;
    write_file_str(file, toml::emit(doc));
}

// Process-wide singletons (lazy_static CONFIG/CONFIG2 parity). Loaded once
// from disk on first use; all later access goes through the shared_mutexes.
Config& global_config() {
    static Config cfg = load_doc<Config>(config_file(""), Config_from_toml);
    return cfg;
}

Config2& global_config2() {
    static Config2 cfg = load_doc<Config2>(config_file("2"), Config2_from_toml);
    return cfg;
}

void store_config_locked() { store_doc(config_file(""), Config_to_toml(global_config())); }
void store_config2_locked() { store_doc(config_file("2"), Config2_to_toml(global_config2())); }

std::chrono::system_clock::time_point file_mtime_sys(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) {
        return std::chrono::system_clock::time_point{};  // UNIX_EPOCH parity
    }
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
}

// `CHARS` parity (excludes 0/1/l/o to avoid visual ambiguity).
constexpr char kPasswordChars[] = "23456789abcdefghijkmnpqrstuvwxyz";
static_assert(sizeof(kPasswordChars) - 1 == 32, "CHARS must hold 32 symbols");

}  // namespace

fs::path get_home() {
    if (const auto& app = app_dir_opt(); app.has_value()) {
        return fs::path(*app);  // mobile parity
    }
    std::string home = getenv_str("HOME");
#if defined(_WIN32)
    if (home.empty()) {
        home = getenv_str("USERPROFILE");
    }
#endif
    if (!home.empty()) {
        return patch_path(fs::path(home));
    }
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    if (!ec) {
        return cwd;
    }
    return fs::temp_directory_path(ec);
}

fs::path config_path(const std::string& name) {
    if (const auto& app = app_dir_opt(); app.has_value()) {
        return fs::path(*app) / name;  // mobile parity
    }
    fs::path p = patch_path(config_base_dir());
    if (!name.empty()) {
        p /= name;
    }
    return p;
}

fs::path config_file(const std::string& suffix) {
    fs::path p = config_path(std::string(kAppName) + suffix);
    p.replace_extension(".toml");  // with_extension("toml") parity
    return p;
}

Config config_load() {
    std::unique_lock l(config_lock());
    global_config() = load_doc<Config>(config_file(""), Config_from_toml);
    return global_config();
}

Config2 config2_load() {
    std::unique_lock l(config2_lock());
    global_config2() = load_doc<Config2>(config_file("2"), Config2_from_toml);
    return global_config2();
}

Config& config_get() { return global_config(); }    // caller must hold config_lock()
Config2& config2_get() { return global_config2(); }  // caller must hold config2_lock()

fs::path log_path() {
#if defined(__APPLE__)
    fs::path home = fs::path(getenv_str("HOME"));
    if (!home.empty()) {
        return home / ("Library/Logs/" + std::string(kAppName));
    }
#elif defined(__linux__)
    const std::string home = getenv_str("HOME");
    if (!home.empty()) {
        fs::path p = fs::path(home) / (".local/share/logs/" + std::string(kAppName));
        std::error_code ec;
        fs::create_directories(p, ec);
        return p;
    }
#endif
    if (fs::path base = config_path(""); base.has_parent_path()) {
        return base.parent_path() / "log";
    }
    return fs::path();
}

std::string ipc_path(const std::string& postfix) {
#if defined(_WIN32)
    // \\ServerName\pipe\PipeName parity (local computer = ".").
    return "\\\\.\\pipe\\" + std::string(kAppName) + "\\query" + postfix;
#else
    fs::path dir = fs::path("/tmp") / kAppName;
    std::error_code ec;
    fs::create_directory(dir, ec);
    fs::permissions(dir, fs::perms(0777), fs::perm_options::replace, ec);
    return (dir / ("ipc" + postfix)).string();
#endif
}

fs::path icon_path() {
    fs::path p = config_path("icons");
    std::error_code ec;
    if (!fs::create_directories(p, ec) && ec) {
        return fs::temp_directory_path(ec);
    }
    return p;
}

ResolvedAddr get_any_listen_addr() {
    return ResolvedAddr{AF_INET, kBindInterface, 0};  // "0.0.0.0:0" parity
}

std::string get_option(const std::string& k) {
    std::shared_lock l(config2_lock());
    const auto it = global_config2().options.find(k);
    return it != global_config2().options.end() ? it->second : "";
}

std::unordered_map<std::string, std::string> get_options() {
    std::shared_lock l(config2_lock());
    return global_config2().options;
}

void set_options(const std::unordered_map<std::string, std::string>& v) {
    std::unique_lock l(config2_lock());
    global_config2().options = v;
    store_config2_locked();
}

void set_option(const std::string& k, const std::string& v) {
    std::unique_lock l(config2_lock());
    Config2& cfg = global_config2();
    if (k == "custom-rendezvous-server") {
        cfg.rendezvous_server = "";
    }
    const auto it = cfg.options.find(k);
    const bool same = (v.empty() && it == cfg.options.end()) ||
                      (it != cfg.options.end() && it->second == v);
    if (!same) {
        if (v.empty()) {
            cfg.options.erase(k);
        } else {
            cfg.options[k] = v;
        }
        store_config2_locked();
    }
}

std::vector<std::string> get_rendezvous_servers() {
    if (const std::string s = get_option("custom-rendezvous-server"); !s.empty()) {
        return {s};
    }
    // Snapshot under lock, then release before get_option() below: shared_mutex
    // is not recursive, so the option read must not nest inside this guard
    // (upstream's temporary read guard likewise ends before its get_option).
    int32_t serial = 0;
    {
        std::shared_lock l(config2_lock());
        serial = global_config2().serial;
    }
    if (serial > kSerial) {
        const std::string list = get_option("rendezvous-servers");
        std::vector<std::string> out;
        size_t start = 0;
        while (true) {
            const size_t pos = list.find(',', start);
            const std::string part =
                (pos == std::string::npos) ? list.substr(start) : list.substr(start, pos - start);
            if (part.find('.') != std::string::npos) {
                out.push_back(part);
            }
            if (pos == std::string::npos) {
                break;
            }
            start = pos + 1;
        }
        if (!out.empty()) {
            return out;
        }
    }
    return {std::begin(kRendezvousServers), std::end(kRendezvousServers)};
}

ResolvedAddr get_rendezvous_server() {
    std::string server = get_option("custom-rendezvous-server");
    if (server.empty()) {
        std::unique_lock l(config2_lock());
        server = global_config2().rendezvous_server;
    }
    if (server.empty()) {
        auto servers = get_rendezvous_servers();
        server = servers.empty() ? "" : servers.front();
    }
    if (server.find(':') == std::string::npos) {
        server += ":" + std::to_string(kRendezvousPort);
    }
    try {
        return to_socket_addr(server);
    } catch (...) {
        return get_any_listen_addr();
    }
}

void reset_online() {
    std::unique_lock l(online_lock());
    online_map().clear();
}

void update_latency(const std::string& host, int64_t latency) {
    {
        std::unique_lock l(online_lock());
        online_map()[host] = latency;
    }
    std::string best;
    int64_t best_delay = INT64_MAX;
    {
        std::unique_lock l(online_lock());
        for (const auto& [h, d] : online_map()) {
            if (d > 0 && d < best_delay) {
                best_delay = d;
                best = h;
            }
        }
    }
    if (!best.empty()) {
        std::unique_lock l(config2_lock());
        Config2& cfg = global_config2();
        if (best != cfg.rendezvous_server) {
            cfg.rendezvous_server = best;
            store_config2_locked();
        }
    }
}

void set_id(const std::string& id) {
    std::unique_lock l(config_lock());
    if (id == global_config().id) {
        return;
    }
    global_config().id = id;
    store_config_locked();
}

std::string get_id() {
    std::string id;
    {
        std::shared_lock l(config_lock());
        id = global_config().id;
    }
    if (id.empty()) {
        if (const std::string tmp = get_auto_id(); !tmp.empty()) {
            id = tmp;
            set_id(id);
        }
    }
    return id;
}

std::string get_auto_id() {
    return "";  // mac_address deferred to the platform step
}

std::string get_auto_password() {
    std::string out;
    out.reserve(6);
    for (int i = 0; i < 6; ++i) {
        out += kPasswordChars[rng()() % 32];  // `rng.gen::<usize>() % CHARS.len()` parity
    }
    return out;
}

void update_id() {
    // to-do (upstream): how about if one ip register a lot of ids?
    const std::string old = get_id();
    std::uniform_int_distribution<int32_t> dist(1'000'000'000, 1'999'999'999);
    set_id(std::to_string(dist(rng())));  // gen_range(1e9, 2e9) parity
    (void)old;                            // log::info parity: silent without logger
}

bool get_key_confirmed() {
    std::shared_lock l(config_lock());
    return global_config().key_confirmed;
}

void set_key_confirmed(bool v) {
    std::unique_lock l(config_lock());
    Config& cfg = global_config();
    if (cfg.key_confirmed == v) {
        return;
    }
    cfg.key_confirmed = v;
    if (!v) {
        cfg.keys_confirmed.clear();
    }
    store_config_locked();
}

bool get_host_key_confirmed(const std::string& host) {
    std::shared_lock l(config_lock());
    const auto it = global_config().keys_confirmed.find(host);
    return it != global_config().keys_confirmed.end() && it->second;
}

void set_host_key_confirmed(const std::string& host, bool v) {
    if (get_host_key_confirmed(host) == v) {
        return;
    }
    std::unique_lock l(config_lock());
    global_config().keys_confirmed[host] = v;
    store_config_locked();
}

void set_key_pair(const std::array<std::vector<uint8_t>, 2>& pair) {
    std::unique_lock l(config_lock());
    global_config().key_pair = pair;
    store_config_locked();
}

std::array<std::vector<uint8_t>, 2> get_key_pair() {
    // Lock held across lazy-gen so it runs at most once (upstream comment).
    std::unique_lock l(config_lock());
    Config& cfg = global_config();
    if (cfg.key_pair[0].empty()) {
        // sodiumoxide::sign::gen_keypair deferred to the crypto step.
        throw std::runtime_error("get_key_pair: key generation deferred to crypto step");
    }
    return cfg.key_pair;
}

void set_password(const std::string& password) {
    std::unique_lock l(config_lock());
    if (password == global_config().password) {
        return;
    }
    global_config().password = password;
    store_config_locked();
}

std::string get_password() {
    std::string password;
    {
        std::shared_lock l(config_lock());
        password = global_config().password;
    }
    if (password.empty()) {
        password = get_auto_password();
        set_password(password);
    }
    return password;
}

void set_salt(const std::string& salt) {
    std::unique_lock l(config_lock());
    if (salt == global_config().salt) {
        return;
    }
    global_config().salt = salt;
    store_config_locked();
}

std::string get_salt() {
    std::string salt;
    {
        std::shared_lock l(config_lock());
        salt = global_config().salt;
    }
    if (salt.empty()) {
        salt = get_auto_password();
        set_salt(salt);
    }
    return salt;
}

Size get_size() {
    std::shared_lock l(config2_lock());
    return global_config2().size;
}

void set_size(int32_t x, int32_t y, int32_t w, int32_t h) {
    const Size size{x, y, w, h};
    std::unique_lock l(config2_lock());
    Config2& cfg = global_config2();
    if (size == cfg.size || size[2] < 300 || size[3] < 300) {
        return;
    }
    cfg.size = size;
    store_config2_locked();
}

void set_remote_id(const std::string& remote_id) {
    std::unique_lock l(config2_lock());
    if (remote_id == global_config2().remote_id) {
        return;
    }
    global_config2().remote_id = remote_id;
    store_config2_locked();
}

std::string get_remote_id() {
    std::shared_lock l(config2_lock());
    return global_config2().remote_id;
}

int32_t get_nat_type() {
    std::shared_lock l(config2_lock());
    return global_config2().nat_type;
}

void set_nat_type(int32_t nat_type) {
    std::unique_lock l(config2_lock());
    if (nat_type == global_config2().nat_type) {
        return;
    }
    global_config2().nat_type = nat_type;
    store_config2_locked();
}

int32_t get_serial() {
    std::shared_lock l(config2_lock());
    return std::max(global_config2().serial, kSerial);
}

void set_serial(int32_t serial) {
    std::unique_lock l(config2_lock());
    if (serial == global_config2().serial) {
        return;
    }
    global_config2().serial = serial;
    store_config2_locked();
}

void import_config(const std::string& from) {
    config_load();  // load first to create path (upstream comment)
    config2_load();
    HBB_ALLOW_ERR(fs::copy_file(from, config_file(""), fs::copy_options::overwrite_existing));
    HBB_ALLOW_ERR(fs::copy_file(replace_all(from, ".toml", "2.toml"), config_file("2"),
                                 fs::copy_options::overwrite_existing));
}

std::string save_tmp() {
    // NOTE: read lock held across the copies (upstream keeps `_lock` alive
    // deliberately — "do not use let _, which will be dropped immediately").
    std::shared_lock l(config_lock());
    const std::string path2 = config_file("2").string() + "_tmp";
    HBB_ALLOW_ERR(fs::copy_file(config_file("2"), path2, fs::copy_options::overwrite_existing));
    const std::string path = config_file("").string() + "_tmp";
    HBB_ALLOW_ERR(fs::copy_file(config_file(""), path, fs::copy_options::overwrite_existing));
    return path;
}

PeerConfig peer_load(const std::string& id) {
    std::shared_lock l(config_lock());  // held "for lock" (upstream comment)
    return load_doc<PeerConfig>((config_path("peers") / id).replace_extension(".toml"),
                                PeerConfig_from_toml);
}

void peer_store(const PeerConfig& cfg, const std::string& id) {
    std::shared_lock l(config_lock());  // held "for lock" (upstream comment)
    store_doc((config_path("peers") / id).replace_extension(".toml"), PeerConfig_to_toml(cfg));
}

void peer_remove(const std::string& id) {
    std::error_code ec;
    fs::remove((config_path("peers") / id).replace_extension(".toml"), ec);
}

std::vector<std::tuple<std::string, std::chrono::system_clock::time_point, PeerInfoSerde>> peers() {
    using Entry = std::tuple<std::string, std::chrono::system_clock::time_point, PeerInfoSerde>;
    std::vector<Entry> out;
    std::error_code ec;
    fs::directory_iterator it(config_path("peers"), ec);
    if (ec) {
        return out;
    }
    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const fs::path& p = entry.path();
        if (p.extension() != ".toml") {
            continue;
        }
        const std::string id = p.stem().string();
        const PeerInfoSerde info = peer_load(id).info;
        if (info.platform.empty()) {
            fs::remove(p, ec);  // drop stale entries without platform (upstream parity)
            continue;
        }
        out.emplace_back(id, file_mtime_sys(p), info);
    }
    std::sort(out.begin(), out.end(),
              [](const Entry& a, const Entry& b) { return std::get<1>(a) > std::get<1>(b); });
    return out;
}

}  // namespace hbb_common