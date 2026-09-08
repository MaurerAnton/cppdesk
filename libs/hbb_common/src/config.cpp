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

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <shared_mutex>

namespace hbb_common {
namespace {

// --- global state ------------------------------------------------------------

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

}  // namespace

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

}  // namespace hbb_common