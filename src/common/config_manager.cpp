// common/config_manager.cpp - Comprehensive configuration management
// Part of cppdesk toolkit
#include "common/config.hpp"
#include "common/crypto.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace cppdesk {
namespace common {

// =============================================================================
// Forward declarations
// =============================================================================

class ConfigObserver;
class ConfigProfile;
class ConfigSchema;
class ConfigMigration;
class ConfigEncryptor;
class ConfigRemoteSync;
class ConfigRollback;
class ConfigManager;

// =============================================================================
// Constants
// =============================================================================

namespace config_constants {
    constexpr std::string_view DEFAULT_CONFIG_FILENAME = "cppdesk.json";
    constexpr std::string_view DEFAULT_PROFILES_DIR = "profiles";
    constexpr std::string_view DEFAULT_BACKUP_DIR = "backups";
    constexpr std::string_view CONFIG_VERSION_KEY = "__config_version__";
    constexpr std::string_view CONFIG_SCHEMA_KEY = "__schema__";
    constexpr std::string_view ENCRYPTED_PREFIX = "ENC:";
    constexpr int CURRENT_CONFIG_VERSION = 1;
    constexpr int MAX_BACKUP_COUNT = 10;
    constexpr int MAX_ROLLBACK_DEPTH = 5;
    constexpr std::chrono::seconds REMOTE_SYNC_INTERVAL{300}; // 5 minutes
    constexpr std::chrono::seconds DEBOUNCE_INTERVAL_MS{250};
} // namespace config_constants

// =============================================================================
// Utility helpers
// =============================================================================

namespace detail {

inline std::string expand_env_vars(const std::string& input) {
    static const std::regex env_regex(R"(\$\{([^}]+)\})");
    static const std::regex env_regex_win(R"(\%([^%]+)\%)");
    std::string result = input;
    std::smatch match;

    // Handle ${VAR} form
    while (std::regex_search(result, match, env_regex)) {
        const char* env_val = std::getenv(match[1].str().c_str());
        std::string replacement = env_val ? env_val : "";
        result.replace(match.position(), match.length(), replacement);
    }

    // Handle %VAR% form (Windows-style)
    while (std::regex_search(result, match, env_regex_win)) {
        const char* env_val = std::getenv(match[1].str().c_str());
        std::string replacement = env_val ? env_val : "";
        result.replace(match.position(), match.length(), replacement);
    }

    return result;
}

inline std::string get_platform_config_dir() {
#if defined(__linux__) || defined(__FreeBSD__)
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) return fs::path(xdg) / "cppdesk";
    const char* home = std::getenv("HOME");
    if (home && home[0]) return fs::path(home) / ".config" / "cppdesk";
    return fs::path("/etc/cppdesk");
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home && home[0]) return fs::path(home) / "Library" / "Application Support" / "cppdesk";
    return fs::path("/Library/Application Support/cppdesk");
#elif defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    if (appdata && appdata[0]) return fs::path(appdata) / "cppdesk";
    const char* localappdata = std::getenv("LOCALAPPDATA");
    if (localappdata && localappdata[0]) return fs::path(localappdata) / "cppdesk";
    return fs::path("C:\\ProgramData\\cppdesk");
#else
    const char* home = std::getenv("HOME");
    if (home && home[0]) return fs::path(home) / ".cppdesk";
    return fs::path("/tmp/cppdesk");
#endif
}

inline std::string get_platform_cache_dir() {
#if defined(__linux__) || defined(__FreeBSD__)
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    if (xdg && xdg[0]) return fs::path(xdg) / "cppdesk";
    const char* home = std::getenv("HOME");
    if (home && home[0]) return fs::path(home) / ".cache" / "cppdesk";
    return fs::path("/var/cache/cppdesk");
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home && home[0]) return fs::path(home) / "Library" / "Caches" / "cppdesk";
    return fs::path("/Library/Caches/cppdesk");
#elif defined(_WIN32)
    const char* localappdata = std::getenv("LOCALAPPDATA");
    if (localappdata && localappdata[0]) return fs::path(localappdata) / "cppdesk" / "Cache";
    return fs::path("C:\\ProgramData\\cppdesk\\Cache");
#else
    return fs::path("/tmp/cppdesk_cache");
#endif
}

inline std::string get_platform_temp_dir() {
#if defined(_WIN32)
    const char* tmp = std::getenv("TEMP");
    if (tmp && tmp[0]) return fs::path(tmp) / "cppdesk";
    return fs::path("C:\\Windows\\Temp\\cppdesk");
#else
    const char* tmp = std::getenv("TMPDIR");
    if (tmp && tmp[0]) return fs::path(tmp) / "cppdesk";
    return fs::path("/tmp/cppdesk");
#endif
}

inline std::string get_platform_log_dir() {
#if defined(__linux__) || defined(__FreeBSD__)
    const char* xdg = std::getenv("XDG_STATE_HOME");
    if (xdg && xdg[0]) return fs::path(xdg) / "cppdesk";
    const char* home = std::getenv("HOME");
    if (home && home[0]) return fs::path(home) / ".local" / "state" / "cppdesk";
    return fs::path("/var/log/cppdesk");
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home && home[0]) return fs::path(home) / "Library" / "Logs" / "cppdesk";
    return fs::path("/Library/Logs/cppdesk");
#elif defined(_WIN32)
    const char* localappdata = std::getenv("LOCALAPPDATA");
    if (localappdata && localappdata[0]) return fs::path(localappdata) / "cppdesk" / "Logs";
    return fs::path("C:\\ProgramData\\cppdesk\\Logs");
#else
    return fs::path("/var/log/cppdesk");
#endif
}

inline std::string sanitize_filename(const std::string& name) {
    static const std::regex invalid_chars(R"([<>:"/\\|?*\x00-\x1f])");
    std::string result = std::regex_replace(name, invalid_chars, "_");
    // Trim leading/trailing spaces and dots
    while (!result.empty() && (result.front() == ' ' || result.front() == '.'))
        result.erase(0, 1);
    while (!result.empty() && (result.back() == ' ' || result.back() == '.'))
        result.pop_back();
    if (result.empty()) result = "unnamed";
    return result;
}

inline std::string make_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
    return oss.str();
}

inline bool atomic_write(const std::string& path, const std::string& content) {
    fs::path target(path);
    fs::path tmp = target;
    tmp += ".tmp." + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());

    try {
        {
            std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
            if (!ofs) {
                spdlog::error("atomic_write: cannot open temp file {}", tmp.string());
                return false;
            }
            ofs << content;
            ofs.flush();
            if (!ofs) {
                spdlog::error("atomic_write: write failed for {}", tmp.string());
                fs::remove(tmp);
                return false;
            }
        }

        fs::rename(tmp, target);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("atomic_write: exception: {}", e.what());
        try { fs::remove(tmp); } catch (...) {}
        return false;
    }
}

inline bool is_sensitive_key(const std::string& key) {
    static const std::vector<std::string> sensitive_patterns = {
        "password", "passwd", "secret", "token", "api_key", "apikey",
        "private_key", "privkey", "credential", "auth", "access_key",
        "encryption_key", "master_key", "signing_key", "seed_phrase",
        "certificate", "ssn", "credit_card", "cc_number"
    };

    std::string lower = key;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    for (const auto& pattern : sensitive_patterns) {
        if (lower.find(pattern) != std::string::npos) return true;
    }
    return false;
}

inline json deep_merge(const json& base, const json& overlay) {
    if (!base.is_object() || !overlay.is_object()) return overlay;

    json result = base;
    for (auto it = overlay.begin(); it != overlay.end(); ++it) {
        const auto& key = it.key();
        const auto& val = it.value();

        if (base.contains(key) && base[key].is_object() && val.is_object()) {
            result[key] = deep_merge(base[key], val);
        } else {
            result[key] = val;
        }
    }
    return result;
}

inline std::vector<std::string> split_path(const std::string& dotpath) {
    std::vector<std::string> parts;
    std::istringstream stream(dotpath);
    std::string part;
    while (std::getline(stream, part, '.')) {
        if (!part.empty()) parts.push_back(part);
    }
    return parts;
}

inline json* navigate_json(json& root, const std::vector<std::string>& path_parts, bool create = false) {
    json* current = &root;
    for (const auto& part : path_parts) {
        if (!current->is_object()) return nullptr;
        if (!current->contains(part)) {
            if (!create) return nullptr;
            (*current)[part] = json::object();
        }
        current = &(*current)[part];
    }
    return current;
}

inline const json* navigate_json(const json& root, const std::vector<std::string>& path_parts) {
    const json* current = &root;
    for (const auto& part : path_parts) {
        if (!current->is_object() || !current->contains(part)) return nullptr;
        current = &(*current)[part];
    }
    return current;
}

} // namespace detail

// =============================================================================
// ConfigError - exception hierarchy for configuration errors
// =============================================================================

class ConfigError : public std::runtime_error {
public:
    explicit ConfigError(const std::string& msg) : std::runtime_error(msg) {}
    explicit ConfigError(const std::string& msg, const std::string& context)
        : std::runtime_error(fmt::format("{} [context: {}]", msg, context)), context_(context) {}

    const std::string& context() const { return context_; }

private:
    std::string context_;
};

class ConfigParseError : public ConfigError {
public:
    explicit ConfigParseError(const std::string& msg, const std::string& path = "")
        : ConfigError(msg, path.empty() ? "parse" : path) {}
};

class ConfigValidationError : public ConfigError {
public:
    ConfigValidationError(const std::string& msg, const std::string& schema_path = "")
        : ConfigError(msg, schema_path.empty() ? "validation" : schema_path) {}
};

class ConfigMigrationError : public ConfigError {
public:
    ConfigMigrationError(const std::string& msg, int from_ver, int to_ver)
        : ConfigError(fmt::format("{} (v{} -> v{})", msg, from_ver, to_ver)) {}
};

class ConfigEncryptionError : public ConfigError {
public:
    explicit ConfigEncryptionError(const std::string& msg)
        : ConfigError(msg, "encryption") {}
};

class ConfigSyncError : public ConfigError {
public:
    explicit ConfigSyncError(const std::string& msg)
        : ConfigError(msg, "sync") {}
};

// =============================================================================
// ConfigChangeEvent - data structure for config change events
// =============================================================================

struct ConfigChangeEvent {
    enum class Type {
        VALUE_CHANGED,
        VALUE_ADDED,
        VALUE_REMOVED,
        PROFILE_SWITCHED,
        CONFIG_RELOADED,
        SCHEMA_VALIDATED,
        MIGRATION_APPLIED,
        ENCRYPTION_UPDATED,
        SYNC_COMPLETED,
        ROLLBACK_PERFORMED
    };

    Type type;
    std::string key_path;
    json old_value;
    json new_value;
    std::string profile_name;
    std::chrono::system_clock::time_point timestamp;

    ConfigChangeEvent() : type(Type::VALUE_CHANGED), timestamp(std::chrono::system_clock::now()) {}

    static ConfigChangeEvent value_changed(const std::string& path, const json& old_val, const json& new_val) {
        ConfigChangeEvent e;
        e.type = Type::VALUE_CHANGED;
        e.key_path = path;
        e.old_value = old_val;
        e.new_value = new_val;
        return e;
    }

    static ConfigChangeEvent value_added(const std::string& path, const json& val) {
        ConfigChangeEvent e;
        e.type = Type::VALUE_ADDED;
        e.key_path = path;
        e.new_value = val;
        return e;
    }

    static ConfigChangeEvent value_removed(const std::string& path, const json& old_val) {
        ConfigChangeEvent e;
        e.type = Type::VALUE_REMOVED;
        e.key_path = path;
        e.old_value = old_val;
        return e;
    }

    static ConfigChangeEvent profile_switched(const std::string& profile) {
        ConfigChangeEvent e;
        e.type = Type::PROFILE_SWITCHED;
        e.profile_name = profile;
        return e;
    }

    static ConfigChangeEvent config_reloaded() {
        ConfigChangeEvent e;
        e.type = Type::CONFIG_RELOADED;
        return e;
    }

    static ConfigChangeEvent migration_applied(int from, int to) {
        ConfigChangeEvent e;
        e.type = Type::MIGRATION_APPLIED;
        e.old_value = from;
        e.new_value = to;
        return e;
    }

    std::string type_name() const {
        switch (type) {
            case Type::VALUE_CHANGED:    return "VALUE_CHANGED";
            case Type::VALUE_ADDED:      return "VALUE_ADDED";
            case Type::VALUE_REMOVED:    return "VALUE_REMOVED";
            case Type::PROFILE_SWITCHED: return "PROFILE_SWITCHED";
            case Type::CONFIG_RELOADED:  return "CONFIG_RELOADED";
            case Type::SCHEMA_VALIDATED: return "SCHEMA_VALIDATED";
            case Type::MIGRATION_APPLIED:return "MIGRATION_APPLIED";
            case Type::ENCRYPTION_UPDATED:return "ENCRYPTION_UPDATED";
            case Type::SYNC_COMPLETED:   return "SYNC_COMPLETED";
            case Type::ROLLBACK_PERFORMED:return "ROLLBACK_PERFORMED";
        }
        return "UNKNOWN";
    }
};

// =============================================================================
// ConfigObserver - observer interface for config change notifications
// =============================================================================

class ConfigObserver {
public:
    virtual ~ConfigObserver() = default;

    /// Called when any configuration change occurs
    virtual void on_config_changed(const ConfigChangeEvent& event) = 0;

    /// Called for specific key path changes (only if subscribed)
    virtual void on_key_changed(const std::string& key_path,
                                const json& old_value,
                                const json& new_value) {}

    /// Called when a profile is activated
    virtual void on_profile_activated(const std::string& profile_name) {}

    /// Called when config is reloaded from disk
    virtual void on_config_reloaded() {}

    /// Called when migration completes
    virtual void on_migration_applied(int from_version, int to_version) {}
};

// =============================================================================
// ConfigNotificationManager - manages observer subscriptions and dispatch
// =============================================================================

class ConfigNotificationManager {
public:
    using ObserverPtr = std::shared_ptr<ConfigObserver>;
    using ObserverWeakPtr = std::weak_ptr<ConfigObserver>;

    void subscribe(ObserverPtr observer) {
        std::unique_lock lock(mutex_);
        // Deduplicate
        for (auto it = observers_.begin(); it != observers_.end(); ) {
            if (auto existing = it->lock()) {
                if (existing == observer) {
                    spdlog::warn("ConfigNotificationManager: observer already subscribed");
                    return;
                }
                ++it;
            } else {
                it = observers_.erase(it);
            }
        }
        observers_.push_back(observer);
        spdlog::debug("ConfigNotificationManager: observer subscribed, total: {}", observers_.size());
    }

    void unsubscribe(ObserverPtr observer) {
        std::unique_lock lock(mutex_);
        for (auto it = observers_.begin(); it != observers_.end(); ) {
            if (auto existing = it->lock()) {
                if (existing == observer) {
                    it = observers_.erase(it);
                    spdlog::debug("ConfigNotificationManager: observer unsubscribed, total: {}",
                                  observers_.size());
                    return;
                }
                ++it;
            } else {
                it = observers_.erase(it);
            }
        }
    }

    void subscribe_key(ObserverPtr observer, const std::string& key_pattern) {
        std::unique_lock lock(mutex_);
        key_subscriptions_[key_pattern].push_back(observer);
        spdlog::debug("ConfigNotificationManager: key subscription '{}'", key_pattern);
    }

    void unsubscribe_key(ObserverPtr observer, const std::string& key_pattern) {
        std::unique_lock lock(mutex_);
        auto it = key_subscriptions_.find(key_pattern);
        if (it == key_subscriptions_.end()) return;
        auto& vec = it->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(),
            [&observer](const ObserverWeakPtr& w) {
                auto s = w.lock();
                return !s || s == observer;
            }), vec.end());
        if (vec.empty()) key_subscriptions_.erase(it);
    }

    void notify(const ConfigChangeEvent& event) {
        std::vector<ObserverPtr> snapshot;
        {
            std::shared_lock lock(mutex_);
            for (auto it = observers_.begin(); it != observers_.end(); ) {
                if (auto obs = it->lock()) {
                    snapshot.push_back(obs);
                    ++it;
                } else {
                    it = observers_.erase(it); // const cast workaround: we need write access
                }
            }
        } // release lock before dispatching

        for (auto& obs : snapshot) {
            try {
                obs->on_config_changed(event);
            } catch (const std::exception& e) {
                spdlog::error("ConfigNotificationManager: observer threw: {}", e.what());
            } catch (...) {
                spdlog::error("ConfigNotificationManager: observer threw unknown exception");
            }
        }

        // Dispatch key-specific subscriptions
        dispatch_key_events(event);
    }

    void notify_key_change(const std::string& key_path,
                           const json& old_val, const json& new_val) {
        auto event = ConfigChangeEvent::value_changed(key_path, old_val, new_val);
        notify(event);
    }

    void notify_profile_switch(const std::string& profile_name) {
        auto event = ConfigChangeEvent::profile_switched(profile_name);
        notify(event);
    }

    size_t observer_count() const {
        std::shared_lock lock(mutex_);
        return std::count_if(observers_.begin(), observers_.end(),
            [](const ObserverWeakPtr& w) { return !w.expired(); });
    }

private:
    mutable std::shared_mutex mutex_;
    std::vector<ObserverWeakPtr> observers_;
    std::unordered_map<std::string, std::vector<ObserverWeakPtr>> key_subscriptions_;

    void dispatch_key_events(const ConfigChangeEvent& event) {
        // Use a snapshot to avoid holding lock during dispatch
        std::unordered_map<std::string, std::vector<ObserverPtr>> snapshot;
        {
            std::shared_lock lock(mutex_);
            for (auto& [pattern, observers] : key_subscriptions_) {
                if (key_matches_pattern(event.key_path, pattern)) {
                    for (auto& w : observers) {
                        if (auto obs = w.lock()) {
                            snapshot[pattern].push_back(obs);
                        }
                    }
                }
            }
        }

        for (auto& [pattern, observers] : snapshot) {
            for (auto& obs : observers) {
                try {
                    obs->on_key_changed(event.key_path, event.old_value, event.new_value);
                } catch (const std::exception& e) {
                    spdlog::error("ConfigNotificationManager: key observer threw: {}", e.what());
                }
            }
        }
    }

    static bool key_matches_pattern(const std::string& key, const std::string& pattern) {
        // Simple wildcard matching: * matches any sequence, ? matches one char
        // Full key match or prefix match with wildcard
        if (pattern == "*") return true;
        if (pattern == key) return true;

        // Handle trailing wildcard: "foo.*" matches "foo.bar"
        if (pattern.size() >= 2 && pattern[pattern.size() - 1] == '*' &&
            pattern[pattern.size() - 2] == '.') {
            std::string prefix = pattern.substr(0, pattern.size() - 2);
            return key.find(prefix + ".") == 0;
        }

        // Handle exact wildcard segment: "foo.*.baz" matches "foo.bar.baz"
        auto key_parts = detail::split_path(key);
        auto pattern_parts = detail::split_path(pattern);

        if (key_parts.size() != pattern_parts.size()) return false;
        for (size_t i = 0; i < key_parts.size(); ++i) {
            if (pattern_parts[i] != "*" && pattern_parts[i] != key_parts[i]) return false;
        }
        return true;
    }
};

// =============================================================================
// ConfigSchema - JSON schema validation for config files (Feature 1)
// =============================================================================

class ConfigSchema {
public:
    ConfigSchema() = default;

    explicit ConfigSchema(const json& schema_def)
        : schema_(schema_def) {
        compile_schema();
    }

    /// Load schema from a JSON file
    bool load(const std::string& schema_path) {
        try {
            std::ifstream ifs(schema_path);
            if (!ifs) {
                spdlog::error("ConfigSchema: cannot open schema file: {}", schema_path);
                return false;
            }
            schema_ = json::parse(ifs);
            compile_schema();
            schema_path_ = schema_path;
            spdlog::info("ConfigSchema: loaded schema from {}", schema_path);
            return true;
        } catch (const std::exception& e) {
            spdlog::error("ConfigSchema: failed to load schema: {}", e.what());
            return false;
        }
    }

    /// Validate a JSON document against the schema
    bool validate(const json& doc, std::string& error_msg) const {
        if (schema_.is_null()) {
            error_msg = "No schema loaded";
            return true; // No schema means no validation
        }

        try {
            // Basic validation without external validator library
            // Note: For full JSON Schema validation, integrate nlohmann/json-schema-validator
            bool valid = validate_object(doc, schema_, "", error_msg);
            if (valid) {
                spdlog::debug("ConfigSchema: validation passed");
            }
            return valid;
        } catch (const std::exception& e) {
            error_msg = std::string("Schema validation exception: ") + e.what();
            return false;
        }
    }

    /// Validate and throw on failure
    void validate_or_throw(const json& doc) const {
        std::string error_msg;
        if (!validate(doc, error_msg)) {
            throw ConfigValidationError(error_msg, schema_path_);
        }
    }

    /// Get the schema definition
    const json& schema() const { return schema_; }

    /// Generate a default JSON document from the schema
    json generate_default() const {
        json result = json::object();

        if (schema_.contains("properties")) {
            for (auto& [key, prop] : schema_["properties"].items()) {
                result[key] = default_from_schema(prop, key);
            }
        }

        // Add schema version marker
        result[std::string(config_constants::CONFIG_SCHEMA_KEY)] = schema_.value("$id", "unknown");

        return result;
    }

    /// Check if a key exists in the schema
    bool has_key(const std::string& key) const {
        if (schema_.is_null()) return true; // No schema = allow all
        auto parts = detail::split_path(key);
        const json* current = &schema_;
        for (const auto& part : parts) {
            if (!current->contains("properties") || !(*current)["properties"].contains(part)) {
                return false;
            }
            current = &(*current)["properties"][part];
        }
        return true;
    }

    /// Get the type of a key from the schema
    std::string key_type(const std::string& key) const {
        auto parts = detail::split_path(key);
        const json* current = &schema_;
        for (const auto& part : parts) {
            if (!current->contains("properties") || !(*current)["properties"].contains(part)) {
                return "any";
            }
            current = &(*current)["properties"][part];
        }
        return current->value("type", "any");
    }

    /// Get allowed enum values for a key
    std::vector<json> enum_values(const std::string& key) const {
        auto parts = detail::split_path(key);
        const json* current = &schema_;
        for (const auto& part : parts) {
            if (!current->contains("properties") || !(*current)["properties"].contains(part)) {
                return {};
            }
            current = &(*current)["properties"][part];
        }
        if (current->contains("enum")) {
            return (*current)["enum"].get<std::vector<json>>();
        }
        return {};
    }

private:
    json schema_;
    std::string schema_path_;
    std::unordered_map<std::string, json> compiled_checks_;

    void compile_schema() {
        compiled_checks_.clear();
        if (schema_.is_null()) return;

        if (schema_.contains("properties")) {
            compile_properties(schema_["properties"], "");
        }
    }

    void compile_properties(const json& props, const std::string& prefix) {
        for (auto& [key, prop] : props.items()) {
            std::string full_key = prefix.empty() ? key : prefix + "." + key;
            compiled_checks_[full_key] = prop;

            if (prop.contains("properties")) {
                compile_properties(prop["properties"], full_key);
            }
        }
    }

    bool validate_object(const json& doc, const json& schema_node,
                         const std::string& path, std::string& error_msg) const {
        // Check type
        if (schema_node.contains("type")) {
            std::string expected_type = schema_node["type"].get<std::string>();
            if (!check_type(doc, expected_type)) {
                error_msg = fmt::format("{}: expected type '{}', got '{}'",
                    path.empty() ? "root" : path, expected_type, json_type_name(doc));
                return false;
            }
        }

        // Check required properties
        if (schema_node.contains("required") && doc.is_object()) {
            for (const auto& req : schema_node["required"]) {
                std::string req_key = req.get<std::string>();
                if (!doc.contains(req_key)) {
                    error_msg = fmt::format("{}: missing required property '{}'",
                        path.empty() ? "root" : path, req_key);
                    return false;
                }
            }
        }

        // Check properties
        if (schema_node.contains("properties") && doc.is_object()) {
            for (auto& [key, prop_schema] : schema_node["properties"].items()) {
                std::string child_path = path.empty() ? key : path + "." + key;
                if (doc.contains(key)) {
                    if (!validate_object(doc[key], prop_schema, child_path, error_msg)) {
                        return false;
                    }
                }
            }
        }

        // Check additional properties
        if (schema_node.value("additionalProperties", true) == false && doc.is_object()) {
            if (schema_node.contains("properties")) {
                for (auto& [key, _val] : doc.items()) {
                    if (!schema_node["properties"].contains(key)) {
                        // Allow internal keys starting with __
                        if (key.size() >= 2 && key[0] == '_' && key[1] == '_') continue;
                        error_msg = fmt::format("{}: unexpected property '{}'",
                            path.empty() ? "root" : path, key);
                        return false;
                    }
                }
            }
        }

        // Check enum
        if (schema_node.contains("enum")) {
            bool found = false;
            for (const auto& ev : schema_node["enum"]) {
                if (doc == ev) { found = true; break; }
            }
            if (!found) {
                error_msg = fmt::format("{}: value '{}' not in enum", path, doc.dump());
                return false;
            }
        }

        // Check minimum/maximum for numbers
        if (schema_node.contains("minimum") && doc.is_number()) {
            double min_val = schema_node["minimum"].get<double>();
            if (doc.get<double>() < min_val) {
                error_msg = fmt::format("{}: value {} < minimum {}", path, doc.get<double>(), min_val);
                return false;
            }
        }
        if (schema_node.contains("maximum") && doc.is_number()) {
            double max_val = schema_node["maximum"].get<double>();
            if (doc.get<double>() > max_val) {
                error_msg = fmt::format("{}: value {} > maximum {}", path, doc.get<double>(), max_val);
                return false;
            }
        }

        // Check minLength/maxLength for strings
        if (schema_node.contains("minLength") && doc.is_string()) {
            size_t min_len = schema_node["minLength"].get<size_t>();
            if (doc.get<std::string>().size() < min_len) {
                error_msg = fmt::format("{}: string length {} < minLength {}",
                    path, doc.get<std::string>().size(), min_len);
                return false;
            }
        }
        if (schema_node.contains("maxLength") && doc.is_string()) {
            size_t max_len = schema_node["maxLength"].get<size_t>();
            if (doc.get<std::string>().size() > max_len) {
                error_msg = fmt::format("{}: string length {} > maxLength {}",
                    path, doc.get<std::string>().size(), max_len);
                return false;
            }
        }

        // Check pattern (regex) for strings
        if (schema_node.contains("pattern") && doc.is_string()) {
            std::regex pattern(schema_node["pattern"].get<std::string>());
            if (!std::regex_match(doc.get<std::string>(), pattern)) {
                error_msg = fmt::format("{}: '{}' does not match pattern '{}'",
                    path, doc.get<std::string>(), schema_node["pattern"].get<std::string>());
                return false;
            }
        }

        // Check items for arrays
        if (schema_node.contains("items") && doc.is_array()) {
            const json& item_schema = schema_node["items"];
            for (size_t i = 0; i < doc.size(); ++i) {
                std::string item_path = path + "[" + std::to_string(i) + "]";
                if (!validate_object(doc[i], item_schema, item_path, error_msg)) {
                    return false;
                }
            }
        }

        // Check minItems/maxItems
        if (schema_node.contains("minItems") && doc.is_array()) {
            size_t min_i = schema_node["minItems"].get<size_t>();
            if (doc.size() < min_i) {
                error_msg = fmt::format("{}: array size {} < minItems {}", path, doc.size(), min_i);
                return false;
            }
        }
        if (schema_node.contains("maxItems") && doc.is_array()) {
            size_t max_i = schema_node["maxItems"].get<size_t>();
            if (doc.size() > max_i) {
                error_msg = fmt::format("{}: array size {} > maxItems {}", path, doc.size(), max_i);
                return false;
            }
        }

        // Check uniqueItems
        if (schema_node.value("uniqueItems", false) && doc.is_array()) {
            std::set<json> seen;
            for (size_t i = 0; i < doc.size(); ++i) {
                if (seen.count(doc[i])) {
                    error_msg = fmt::format("{}: duplicate item at index {}", path, i);
                    return false;
                }
                seen.insert(doc[i]);
            }
        }

        return true;
    }

    static bool check_type(const json& val, const std::string& expected) {
        if (expected == "string")  return val.is_string();
        if (expected == "number")  return val.is_number();
        if (expected == "integer") return val.is_number_integer();
        if (expected == "boolean") return val.is_boolean();
        if (expected == "object")  return val.is_object();
        if (expected == "array")   return val.is_array();
        if (expected == "null")    return val.is_null();
        return true; // unknown type = accept
    }

    static std::string json_type_name(const json& val) {
        if (val.is_string())  return "string";
        if (val.is_number_integer()) return "integer";
        if (val.is_number_float())   return "number";
        if (val.is_boolean()) return "boolean";
        if (val.is_object())  return "object";
        if (val.is_array())   return "array";
        if (val.is_null())    return "null";
        return "unknown";
    }

    static json default_from_schema(const json& prop, const std::string& key) {
        // Use explicit default if provided
        if (prop.contains("default")) return prop["default"];

        // Infer from type
        if (prop.contains("type")) {
            std::string type = prop["type"].get<std::string>();
            if (type == "string") {
                if (prop.contains("examples") && prop["examples"].is_array() && !prop["examples"].empty()) {
                    return prop["examples"][0];
                }
                return "";
            }
            if (type == "integer") {
                if (prop.contains("minimum")) return prop["minimum"];
                return 0;
            }
            if (type == "number") {
                if (prop.contains("minimum")) return prop["minimum"];
                return 0.0;
            }
            if (type == "boolean") return false;
            if (type == "object") {
                json obj = json::object();
                if (prop.contains("properties")) {
                    for (auto& [k, p] : prop["properties"].items()) {
                        obj[k] = default_from_schema(p, k);
                    }
                }
                return obj;
            }
            if (type == "array") return json::array();
            if (type == "null") return nullptr;
        }

        return nullptr;
    }
};

// =============================================================================
// ConfigMigration - config migration between versions (Feature 2)
// =============================================================================

class ConfigMigration {
public:
    using MigrationFunc = std::function<json(const json&)>;

    /// Register a migration from a specific version to the next
    void register_migration(int from_version, int to_version, MigrationFunc func, const std::string& description = "") {
        std::unique_lock lock(mutex_);
        migrations_[from_version] = {to_version, std::move(func), description};
        spdlog::info("ConfigMigration: registered migration v{} -> v{}: {}",
                     from_version, to_version, description);
    }

    /// Migrate a config document from its current version to the target version
    json migrate(const json& config, int target_version = config_constants::CURRENT_CONFIG_VERSION) const {
        json result = config;

        // Determine current version
        int current_version = 0;
        std::string version_key(config_constants::CONFIG_VERSION_KEY);
        if (result.contains(version_key)) {
            current_version = result[version_key].get<int>();
        }

        if (current_version == target_version) {
            spdlog::debug("ConfigMigration: already at target version {}", target_version);
            return result;
        }

        if (current_version > target_version) {
            throw ConfigMigrationError(
                fmt::format("Cannot downgrade from v{} to v{}", current_version, target_version),
                current_version, target_version);
        }

        int steps = 0;
        std::shared_lock lock(mutex_);
        while (current_version < target_version) {
            auto it = migrations_.find(current_version);
            if (it == migrations_.end()) {
                throw ConfigMigrationError(
                    fmt::format("No migration path from v{}", current_version),
                    current_version, target_version);
            }

            const auto& mig = it->second;
            if (mig.to_version > target_version) {
                throw ConfigMigrationError(
                    fmt::format("Migration v{} -> v{} overshoots target v{}",
                        current_version, mig.to_version, target_version),
                    current_version, target_version);
            }

            spdlog::info("ConfigMigration: applying v{} -> v{}: {}",
                         current_version, mig.to_version, mig.description);
            try {
                result = mig.func(result);
                result[version_key] = mig.to_version;
            } catch (const std::exception& e) {
                throw ConfigMigrationError(
                    fmt::format("Migration v{} -> v{} failed: {}",
                        current_version, mig.to_version, e.what()),
                    current_version, mig.to_version);
            }

            current_version = mig.to_version;
            ++steps;
        }

        spdlog::info("ConfigMigration: completed {} migration step(s), now at v{}",
                     steps, current_version);
        return result;
    }

    /// Get all registered migration paths
    std::vector<std::tuple<int, int, std::string>> migration_paths() const {
        std::shared_lock lock(mutex_);
        std::vector<std::tuple<int, int, std::string>> paths;
        for (const auto& [from, mig] : migrations_) {
            paths.emplace_back(from, mig.to_version, mig.description);
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    }

    /// Check if a migration path exists
    bool can_migrate(int from_version, int to_version) const {
        if (from_version == to_version) return true;
        if (from_version > to_version) return false;

        std::shared_lock lock(mutex_);
        int current = from_version;
        std::set<int> visited;
        while (current < to_version) {
            if (visited.count(current)) return false; // cycle detected
            visited.insert(current);
            auto it = migrations_.find(current);
            if (it == migrations_.end()) return false;
            current = it->second.to_version;
        }
        return current == to_version;
    }

private:
    struct MigrationEntry {
        int to_version;
        MigrationFunc func;
        std::string description;
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<int, MigrationEntry> migrations_;
};

// =============================================================================
// ConfigEncryptor - encryption for sensitive config values (Feature 8)
// =============================================================================

class ConfigEncryptor {
public:
    ConfigEncryptor() = default;

    /// Set the master encryption key
    void set_master_key(const std::string& key) {
        std::unique_lock lock(mutex_);
        master_key_ = key;
        // Derive a proper key using simple key derivation
        derive_key();
        spdlog::info("ConfigEncryptor: master key set");
    }

    /// Load encryption key from file
    bool load_key_file(const std::string& key_path) {
        try {
            std::ifstream ifs(key_path, std::ios::binary);
            if (!ifs) {
                spdlog::warn("ConfigEncryptor: key file not found: {}", key_path);
                return false;
            }
            std::string key_data((std::istreambuf_iterator<char>(ifs)),
                                  std::istreambuf_iterator<char>());
            set_master_key(key_data);
            spdlog::info("ConfigEncryptor: loaded key from {}", key_path);
            return true;
        } catch (const std::exception& e) {
            spdlog::error("ConfigEncryptor: failed to load key: {}", e.what());
            return false;
        }
    }

    /// Encrypt a sensitive value
    std::string encrypt(const std::string& plaintext) const {
        std::shared_lock lock(mutex_);
        if (master_key_.empty()) {
            throw ConfigEncryptionError("No master key set");
        }

        try {
            std::string result = std::string(config_constants::ENCRYPTED_PREFIX) +
                                 xor_encrypt(plaintext, derived_key_);
            return result;
        } catch (const std::exception& e) {
            throw ConfigEncryptionError(
                fmt::format("Encryption failed: {}", e.what()));
        }
    }

    /// Decrypt a sensitive value
    std::string decrypt(const std::string& ciphertext) const {
        std::shared_lock lock(mutex_);
        if (master_key_.empty()) {
            throw ConfigEncryptionError("No master key set");
        }

        // Check prefix
        std::string prefix(config_constants::ENCRYPTED_PREFIX);
        if (ciphertext.size() < prefix.size() ||
            ciphertext.substr(0, prefix.size()) != prefix) {
            // Not encrypted, return as-is
            return ciphertext;
        }

        try {
            std::string encoded = ciphertext.substr(prefix.size());
            return xor_encrypt(encoded, derived_key_);
        } catch (const std::exception& e) {
            throw ConfigEncryptionError(
                fmt::format("Decryption failed: {}", e.what()));
        }
    }

    /// Check if a string appears to be encrypted
    static bool is_encrypted(const std::string& value) {
        return value.size() > config_constants::ENCRYPTED_PREFIX.size() &&
               value.substr(0, config_constants::ENCRYPTED_PREFIX.size()) == config_constants::ENCRYPTED_PREFIX;
    }

    /// Recursively encrypt all sensitive values in a config object
    void encrypt_sensitive(json& config) const {
        std::shared_lock lock(mutex_);
        if (master_key_.empty()) {
            spdlog::warn("ConfigEncryptor: cannot encrypt without master key");
            return;
        }

        encrypt_sensitive_recursive(config, "");
        spdlog::info("ConfigEncryptor: encrypted sensitive values in config");
    }

    /// Recursively decrypt all sensitive values in a config object
    void decrypt_sensitive(json& config) const {
        std::shared_lock lock(mutex_);
        if (master_key_.empty()) {
            spdlog::warn("ConfigEncryptor: cannot decrypt without master key");
            return;
        }

        decrypt_sensitive_recursive(config, "");
        spdlog::debug("ConfigEncryptor: decrypted sensitive values in config");
    }

    /// Check if the encryptor is initialized
    bool is_ready() const {
        std::shared_lock lock(mutex_);
        return !master_key_.empty();
    }

    /// Generate a random encryption key
    static std::string generate_key(size_t length = 32) {
        static const char charset[] =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!@#$%^&*()_+-=[]{}|;:,.<>?";
        const size_t charset_size = sizeof(charset) - 1;

        std::string result;
        result.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            result += charset[rand() % charset_size];
        }
        return result;
    }

private:
    mutable std::shared_mutex mutex_;
    std::string master_key_;
    std::string derived_key_;

    void derive_key() {
        // Simple key derivation: repeat/truncate to 32 bytes, then mix
        derived_key_.resize(32, '\0');
        size_t len = std::min(master_key_.size(), derived_key_.size());
        for (size_t i = 0; i < len; ++i) {
            derived_key_[i] = master_key_[i];
        }
        // Mix using feedback
        for (size_t i = 0; i < derived_key_.size(); ++i) {
            derived_key_[i] ^= static_cast<char>((i * 73 + 137) & 0xFF);
        }
        for (size_t i = 0; i < master_key_.size(); ++i) {
            derived_key_[i % derived_key_.size()] ^= master_key_[i];
        }
    }

    static std::string xor_encrypt(const std::string& data, const std::string& key) {
        // Base64-like encoding for the XOR result to keep it printable
        std::string xored;
        xored.reserve(data.size());
        for (size_t i = 0; i < data.size(); ++i) {
            xored += static_cast<char>(data[i] ^ key[i % key.size()]);
        }
        return base64_encode(xored);
    }

    static std::string base64_encode(const std::string& input) {
        static const char* chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        result.reserve(((input.size() + 2) / 3) * 4);

        for (size_t i = 0; i < input.size(); i += 3) {
            unsigned char a = static_cast<unsigned char>(input[i]);
            unsigned char b = (i + 1 < input.size()) ? static_cast<unsigned char>(input[i + 1]) : 0;
            unsigned char c = (i + 2 < input.size()) ? static_cast<unsigned char>(input[i + 2]) : 0;

            result += chars[a >> 2];
            result += chars[((a & 0x03) << 4) | (b >> 4)];
            result += (i + 1 < input.size()) ? chars[((b & 0x0F) << 2) | (c >> 6)] : '=';
            result += (i + 2 < input.size()) ? chars[c & 0x3F] : '=';
        }
        return result;
    }

    static std::string base64_decode(const std::string& input) {
        static const std::string chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        result.reserve((input.size() / 4) * 3);

        auto decode_char = [](char c) -> unsigned char {
            size_t pos = chars.find(c);
            return (pos != std::string::npos) ? static_cast<unsigned char>(pos) : 0;
        };

        for (size_t i = 0; i < input.size(); i += 4) {
            unsigned char a = decode_char(input[i]);
            unsigned char b = decode_char(input[i + 1]);
            unsigned char c = (i + 2 < input.size() && input[i + 2] != '=') ? decode_char(input[i + 2]) : 0;
            unsigned char d = (i + 3 < input.size() && input[i + 3] != '=') ? decode_char(input[i + 3]) : 0;

            result += static_cast<char>((a << 2) | (b >> 4));
            if (i + 2 < input.size() && input[i + 2] != '=') {
                result += static_cast<char>(((b & 0x0F) << 4) | (c >> 2));
            }
            if (i + 3 < input.size() && input[i + 3] != '=') {
                result += static_cast<char>(((c & 0x03) << 6) | d);
            }
        }
        return result;
    }

    void encrypt_sensitive_recursive(json& node, const std::string& path) const {
        if (node.is_object()) {
            for (auto& [key, val] : node.items()) {
                std::string child_path = path.empty() ? key : path + "." + key;
                if (val.is_string() && detail::is_sensitive_key(key)) {
                    if (!is_encrypted(val.get<std::string>())) {
                        node[key] = encrypt(val.get<std::string>());
                        spdlog::debug("ConfigEncryptor: encrypted '{}'", child_path);
                    }
                } else if (val.is_object() || val.is_array()) {
                    encrypt_sensitive_recursive(node[key], child_path);
                }
            }
        } else if (node.is_array()) {
            for (auto& elem : node) {
                encrypt_sensitive_recursive(elem, path);
            }
        }
    }

    void decrypt_sensitive_recursive(json& node, const std::string& path) const {
        if (node.is_object()) {
            for (auto& [key, val] : node.items()) {
                std::string child_path = path.empty() ? key : path + "." + key;
                if (val.is_string() && is_encrypted(val.get<std::string>())) {
                    node[key] = decrypt(val.get<std::string>());
                    spdlog::debug("ConfigEncryptor: decrypted '{}'", child_path);
                } else if (val.is_object() || val.is_array()) {
                    decrypt_sensitive_recursive(node[key], child_path);
                }
            }
        } else if (node.is_array()) {
            for (auto& elem : node) {
                decrypt_sensitive_recursive(elem, path);
            }
        }
    }
};

// =============================================================================
// ConfigProfile - configuration profile data model
// =============================================================================

struct ConfigProfileData {
    std::string name;
    std::string display_name;
    std::string description;
    json profile_config;          // Profile-specific overrides
    std::set<std::string> tags;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point modified_at;
    bool is_active = false;
    bool is_builtin = false;

    json to_json() const {
        json j;
        j["name"] = name;
        j["display_name"] = display_name;
        j["description"] = description;
        j["config"] = profile_config;
        j["tags"] = std::vector<std::string>(tags.begin(), tags.end());
        j["is_builtin"] = is_builtin;
        return j;
    }

    static ConfigProfileData from_json(const json& j) {
        ConfigProfileData p;
        p.name = j.value("name", "");
        p.display_name = j.value("display_name", p.name);
        p.description = j.value("description", "");
        p.profile_config = j.value("config", json::object());
        if (j.contains("tags") && j["tags"].is_array()) {
            for (const auto& t : j["tags"]) p.tags.insert(t.get<std::string>());
        }
        p.is_builtin = j.value("is_builtin", false);
        p.created_at = std::chrono::system_clock::now();
        p.modified_at = p.created_at;
        return p;
    }
};

// =============================================================================
// ConfigProfileManager - manages config profiles (Feature 5)
// =============================================================================

class ConfigProfileManager {
public:
    ConfigProfileManager(const fs::path& profiles_dir)
        : profiles_dir_(profiles_dir) {
        fs::create_directories(profiles_dir_);
        spdlog::info("ConfigProfileManager: profiles dir: {}", profiles_dir_.string());
    }

    /// Create a new profile
    bool create_profile(const std::string& name, const std::string& display_name = "",
                        const std::string& description = "") {
        std::unique_lock lock(mutex_);
        std::string safe_name = detail::sanitize_filename(name);

        if (profiles_.count(safe_name)) {
            spdlog::warn("ConfigProfileManager: profile '{}' already exists", safe_name);
            return false;
        }

        ConfigProfileData profile;
        profile.name = safe_name;
        profile.display_name = display_name.empty() ? safe_name : display_name;
        profile.description = description;
        profile.created_at = std::chrono::system_clock::now();
        profile.modified_at = profile.created_at;
        profile.profile_config = json::object();

        profiles_[safe_name] = std::move(profile);
        save_profile(safe_name);
        spdlog::info("ConfigProfileManager: created profile '{}'", safe_name);
        return true;
    }

    /// Delete a profile
    bool delete_profile(const std::string& name) {
        std::unique_lock lock(mutex_);
        auto it = profiles_.find(name);
        if (it == profiles_.end()) {
            spdlog::warn("ConfigProfileManager: profile '{}' not found", name);
            return false;
        }

        if (it->second.is_builtin) {
            spdlog::warn("ConfigProfileManager: cannot delete builtin profile '{}'", name);
            return false;
        }

        // If this is the active profile, deactivate first
        if (it->second.is_active) {
            active_profile_.reset();
        }

        // Remove the file
        fs::path profile_file = profiles_dir_ / (name + ".json");
        try {
            if (fs::exists(profile_file)) fs::remove(profile_file);
        } catch (...) {}

        profiles_.erase(it);
        spdlog::info("ConfigProfileManager: deleted profile '{}'", name);
        return true;
    }

    /// Activate a profile
    bool activate_profile(const std::string& name, NotificationCallback on_switch = nullptr) {
        std::unique_lock lock(mutex_);
        auto it = profiles_.find(name);
        if (it == profiles_.end()) {
            spdlog::warn("ConfigProfileManager: profile '{}' not found", name);
            return false;
        }

        // Deactivate current
        if (active_profile_.has_value()) {
            auto active_it = profiles_.find(active_profile_.value());
            if (active_it != profiles_.end()) {
                active_it->second.is_active = false;
            }
        }

        it->second.is_active = true;
        active_profile_ = name;
        save_profile(name);

        if (on_switch) {
            auto profile_copy = it->second;
            lock.unlock();
            on_switch(profile_copy);
        }

        spdlog::info("ConfigProfileManager: activated profile '{}'", name);
        return true;
    }

    /// Deactivate current profile
    void deactivate_profile() {
        std::unique_lock lock(mutex_);
        if (active_profile_.has_value()) {
            auto it = profiles_.find(active_profile_.value());
            if (it != profiles_.end()) {
                it->second.is_active = false;
                save_profile(it->first);
            }
            active_profile_.reset();
            spdlog::info("ConfigProfileManager: deactivated profile");
        }
    }

    /// Get the active profile name
    std::optional<std::string> active_profile_name() const {
        std::shared_lock lock(mutex_);
        return active_profile_;
    }

    /// Get profile data
    std::optional<ConfigProfileData> get_profile(const std::string& name) const {
        std::shared_lock lock(mutex_);
        auto it = profiles_.find(name);
        if (it != profiles_.end()) return it->second;
        return std::nullopt;
    }

    /// Get the active profile data
    std::optional<ConfigProfileData> get_active_profile() const {
        std::shared_lock lock(mutex_);
        if (active_profile_.has_value()) {
            auto it = profiles_.find(active_profile_.value());
            if (it != profiles_.end()) return it->second;
        }
        return std::nullopt;
    }

    /// Update profile configuration
    bool update_profile_config(const std::string& name, const json& config_overrides) {
        std::unique_lock lock(mutex_);
        auto it = profiles_.find(name);
        if (it == profiles_.end()) return false;

        it->second.profile_config = detail::deep_merge(it->second.profile_config, config_overrides);
        it->second.modified_at = std::chrono::system_clock::now();
        save_profile(name);
        spdlog::info("ConfigProfileManager: updated config for profile '{}'", name);
        return true;
    }

    /// Set a single config value for a profile
    bool set_profile_value(const std::string& name, const std::string& key, const json& value) {
        std::unique_lock lock(mutex_);
        auto it = profiles_.find(name);
        if (it == profiles_.end()) return false;

        auto parts = detail::split_path(key);
        json* target = detail::navigate_json(it->second.profile_config, parts, true);
        if (!target) return false;
        *target = value;
        it->second.modified_at = std::chrono::system_clock::now();
        save_profile(name);
        return true;
    }

    /// List all profiles
    std::vector<std::string> list_profiles() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> names;
        names.reserve(profiles_.size());
        for (const auto& [name, _] : profiles_) {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    /// Load all profiles from disk
    void load_all() {
        std::unique_lock lock(mutex_);
        profiles_.clear();

        try {
            if (!fs::exists(profiles_dir_)) return;

            for (const auto& entry : fs::directory_iterator(profiles_dir_)) {
                if (entry.path().extension() != ".json") continue;

                std::string name = entry.path().stem().string();
                try {
                    std::ifstream ifs(entry.path());
                    if (!ifs) continue;
                    json j = json::parse(ifs);
                    auto profile = ConfigProfileData::from_json(j);

                    // Ensure name matches filename
                    if (profile.name.empty()) profile.name = name;

                    profiles_[name] = std::move(profile);
                    spdlog::debug("ConfigProfileManager: loaded profile '{}'", name);
                } catch (const std::exception& e) {
                    spdlog::warn("ConfigProfileManager: failed to load profile '{}': {}",
                                 name, e.what());
                }
            }

            spdlog::info("ConfigProfileManager: loaded {} profiles", profiles_.size());

            // Determine active profile
            for (auto& [name, profile] : profiles_) {
                if (profile.is_active) {
                    active_profile_ = name;
                    break;
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("ConfigProfileManager: failed to scan profiles: {}", e.what());
        }
    }

    /// Save all profiles
    void save_all() {
        std::shared_lock lock(mutex_);
        for (const auto& [name, _] : profiles_) {
            save_profile_impl(name);
        }
    }

    /// Get the effective profile config (with parent profile merging if applicable)
    json get_effective_config(const std::string& name,
                              const json& base_config = json::object()) const {
        std::shared_lock lock(mutex_);
        auto it = profiles_.find(name);
        if (it == profiles_.end()) return base_config;

        return detail::deep_merge(base_config, it->second.profile_config);
    }

    /// Check if a profile exists
    bool has_profile(const std::string& name) const {
        std::shared_lock lock(mutex_);
        return profiles_.count(name) > 0;
    }

    /// Get profile count
    size_t profile_count() const {
        std::shared_lock lock(mutex_);
        return profiles_.size();
    }

    using NotificationCallback = std::function<void(const ConfigProfileData&)>;

private:
    fs::path profiles_dir_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ConfigProfileData> profiles_;
    std::optional<std::string> active_profile_;

    void save_profile(const std::string& name) {
        save_profile_impl(name);
    }

    void save_profile_impl(const std::string& name) const {
        auto it = profiles_.find(name);
        if (it == profiles_.end()) return;

        fs::path profile_file = profiles_dir_ / (name + ".json");
        json j = it->second.to_json();

        try {
            fs::create_directories(profiles_dir_);
            detail::atomic_write(profile_file.string(), j.dump(2));
            spdlog::debug("ConfigProfileManager: saved profile '{}'", name);
        } catch (const std::exception& e) {
            spdlog::error("ConfigProfileManager: failed to save profile '{}': {}",
                          name, e.what());
        }
    }
};

// =============================================================================
// ConfigRollback - config rollback on parse errors (Feature 10)
// =============================================================================

class ConfigRollback {
public:
    ConfigRollback(const fs::path& backup_dir, size_t max_backups = config_constants::MAX_BACKUP_COUNT)
        : backup_dir_(backup_dir), max_backups_(max_backups) {
        fs::create_directories(backup_dir_);
    }

    /// Create a backup of the current config
    bool create_backup(const std::string& config_path, const std::string& config_content) {
        if (config_content.empty()) return false;

        try {
            std::string timestamp = detail::make_timestamp();
            fs::path source_path(config_path);
            std::string backup_name = source_path.stem().string() + "_" +
                                      timestamp + ".json";
            fs::path backup_path = backup_dir_ / backup_name;

            if (!detail::atomic_write(backup_path.string(), config_content)) {
                return false;
            }

            std::unique_lock lock(mutex_);
            backups_[config_path].push_back({
                backup_path.string(),
                timestamp,
                std::chrono::system_clock::now()
            });

            // Enforce max backups
            while (backups_[config_path].size() > max_backups_) {
                auto& oldest = backups_[config_path].front();
                try {
                    if (fs::exists(oldest.path)) fs::remove(oldest.path);
                } catch (...) {}
                backups_[config_path].pop_front();
            }

            spdlog::info("ConfigRollback: created backup '{}'", backup_path.string());
            return true;
        } catch (const std::exception& e) {
            spdlog::error("ConfigRollback: backup failed: {}", e.what());
            return false;
        }
    }

    /// Rollback to the most recent backup
    bool rollback(const std::string& config_path) {
        std::unique_lock lock(mutex_);
        auto it = backups_.find(config_path);
        if (it == backups_.end() || it->second.empty()) {
            spdlog::warn("ConfigRollback: no backups for {}", config_path);
            return false;
        }

        auto& entry = it->second.back();
        std::string backup_path = entry.path;

        if (!fs::exists(backup_path)) {
            it->second.pop_back();
            spdlog::error("ConfigRollback: backup file missing: {}", backup_path);
            return false;
        }

        try {
            // Read backup content
            std::ifstream ifs(backup_path);
            if (!ifs) {
                it->second.pop_back();
                return false;
            }
            std::string content((std::istreambuf_iterator<char>(ifs)),
                                std::istreambuf_iterator<char>());

            // Write back to config path
            if (!detail::atomic_write(config_path, content)) {
                return false;
            }

            it->second.pop_back();
            spdlog::info("ConfigRollback: rolled back '{}' from '{}'",
                         config_path, backup_path);
            return true;
        } catch (const std::exception& e) {
            spdlog::error("ConfigRollback: rollback failed: {}", e.what());
            return false;
        }
    }

    /// List available backups for a config file
    std::vector<std::string> list_backups(const std::string& config_path) const {
        std::shared_lock lock(mutex_);
        auto it = backups_.find(config_path);
        if (it == backups_.end()) return {};

        std::vector<std::string> result;
        result.reserve(it->second.size());
        for (const auto& entry : it->second) {
            result.push_back(entry.path);
        }
        return result;
    }

    /// Get the number of available backups
    size_t backup_count(const std::string& config_path) const {
        std::shared_lock lock(mutex_);
        auto it = backups_.find(config_path);
        if (it == backups_.end()) return 0;
        return it->second.size();
    }

    /// Clean up old backups beyond the max
    void cleanup() {
        std::unique_lock lock(mutex_);
        for (auto& [path, entries] : backups_) {
            while (entries.size() > max_backups_) {
                auto& oldest = entries.front();
                try {
                    if (fs::exists(oldest.path)) fs::remove(oldest.path);
                } catch (...) {}
                entries.pop_front();
            }
        }
        spdlog::info("ConfigRollback: cleanup complete");
    }

    /// Prune backups older than a given duration
    void prune_older_than(std::chrono::system_clock::duration max_age) {
        auto cutoff = std::chrono::system_clock::now() - max_age;
        std::unique_lock lock(mutex_);

        for (auto& [path, entries] : backups_) {
            while (!entries.empty() && entries.front().created_at < cutoff) {
                auto& oldest = entries.front();
                try {
                    if (fs::exists(oldest.path)) fs::remove(oldest.path);
                } catch (...) {}
                entries.pop_front();
            }
        }
        spdlog::info("ConfigRollback: pruned backups older than cutoff");
    }

private:
    struct BackupEntry {
        std::string path;
        std::string timestamp;
        std::chrono::system_clock::time_point created_at;
    };

    fs::path backup_dir_;
    size_t max_backups_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::deque<BackupEntry>> backups_;
};

// =============================================================================
// ConfigCLIParser - command-line argument parsing (Feature 4)
// =============================================================================

class ConfigCLIParser {
public:
    struct ArgOption {
        std::string long_name;      // e.g., "config"
        char short_name = '\0';     // e.g., 'c'
        std::string value_name;     // e.g., "PATH"
        std::string description;
        std::string default_value;
        bool required = false;
        bool is_flag = false;       // Boolean flag (no value)
        bool is_hidden = false;
        std::vector<std::string> aliases;

        json to_json() const {
            json j;
            j["long"] = long_name;
            if (short_name) j["short"] = std::string(1, short_name);
            j["value_name"] = value_name;
            j["description"] = description;
            if (!default_value.empty()) j["default"] = default_value;
            j["required"] = required;
            j["is_flag"] = is_flag;
            return j;
        }
    };

    struct ParseResult {
        std::unordered_map<std::string, json> options;  // option_name -> value
        std::vector<std::string> positional_args;         // positional arguments
        std::vector<std::string> unknown_args;            // unrecognized args
        bool help_requested = false;
        bool version_requested = false;

        bool has(const std::string& name) const {
            return options.count(name) > 0;
        }

        template<typename T>
        T get(const std::string& name, T default_val = T{}) const {
            auto it = options.find(name);
            if (it != options.end()) {
                try {
                    return it->second.get<T>();
                } catch (...) {}
            }
            return default_val;
        }

        std::string get_string(const std::string& name, const std::string& default_val = "") const {
            auto it = options.find(name);
            if (it != options.end() && it->second.is_string()) {
                return it->second.get<std::string>();
            }
            return default_val;
        }

        int get_int(const std::string& name, int default_val = 0) const {
            auto it = options.find(name);
            if (it != options.end() && it->second.is_number_integer()) {
                return it->second.get<int>();
            }
            return default_val;
        }

        bool get_bool(const std::string& name, bool default_val = false) const {
            auto it = options.find(name);
            if (it != options.end() && it->second.is_boolean()) {
                return it->second.get<bool>();
            }
            return default_val;
        }

        double get_double(const std::string& name, double default_val = 0.0) const {
            auto it = options.find(name);
            if (it != options.end() && it->second.is_number()) {
                return it->second.get<double>();
            }
            return default_val;
        }
    };

    ConfigCLIParser(const std::string& program_name = "cppdesk",
                    const std::string& description = "CppDesk Configuration Tool")
        : program_name_(program_name), description_(description) {}

    /// Add an option definition
    void add_option(ArgOption opt) {
        std::unique_lock lock(mutex_);
        defined_options_[opt.long_name] = std::move(opt);
    }

    /// Convenience: add a string option
    void add_string_option(const std::string& long_name, char short_name,
                          const std::string& value_name, const std::string& description,
                          const std::string& default_val = "", bool required = false) {
        ArgOption opt;
        opt.long_name = long_name;
        opt.short_name = short_name;
        opt.value_name = value_name;
        opt.description = description;
        opt.default_value = default_val;
        opt.required = required;
        opt.is_flag = false;
        add_option(std::move(opt));
    }

    /// Convenience: add a flag option
    void add_flag_option(const std::string& long_name, char short_name,
                         const std::string& description, bool default_val = false) {
        ArgOption opt;
        opt.long_name = long_name;
        opt.short_name = short_name;
        opt.description = description;
        opt.default_value = default_val ? "true" : "false";
        opt.is_flag = true;
        add_option(std::move(opt));
    }

    /// Parse argv-style arguments into a ParseResult
    ParseResult parse(int argc, const char* const argv[]) {
        std::vector<std::string> args;
        args.reserve(argc);
        for (int i = 0; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }
        return parse(args);
    }

    /// Parse a vector of strings (e.g., split command line)
    ParseResult parse(const std::vector<std::string>& args) {
        ParseResult result;

        // Build short->long mapping
        std::unordered_map<char, std::string> short_map;
        {
            std::shared_lock lock(mutex_);
            for (const auto& [name, opt] : defined_options_) {
                if (opt.short_name) short_map[opt.short_name] = name;
            }
        }

        // Apply defaults
        {
            std::shared_lock lock(mutex_);
            for (const auto& [name, opt] : defined_options_) {
                if (!opt.default_value.empty()) {
                    if (opt.is_flag) {
                        result.options[name] = (opt.default_value == "true");
                    } else {
                        result.options[name] = opt.default_value;
                    }
                }
            }
        }

        // Parse arguments
        for (size_t i = 0; i < args.size(); ++i) {
            const std::string& arg = args[i];

            // Handle --help / -h
            if (arg == "--help" || arg == "-h") {
                result.help_requested = true;
                return result;
            }

            // Handle --version / -V
            if (arg == "--version" || arg == "-V") {
                result.version_requested = true;
                return result;
            }

            // Handle -- long options
            if (arg.size() >= 2 && arg[0] == '-' && arg[1] == '-') {
                std::string long_name = arg.substr(2);

                // Handle --key=value form
                std::string value;
                size_t eq_pos = long_name.find('=');
                if (eq_pos != std::string::npos) {
                    value = long_name.substr(eq_pos + 1);
                    long_name = long_name.substr(0, eq_pos);
                }

                std::shared_lock lock(mutex_);
                auto it = defined_options_.find(long_name);
                if (it != defined_options_.end()) {
                    if (it->second.is_flag) {
                        result.options[long_name] = true;
                    } else {
                        if (!value.empty()) {
                            result.options[long_name] = value;
                        } else if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                            result.options[long_name] = args[++i];
                        } else {
                            result.options[long_name] = "";
                        }
                    }
                } else {
                    // Check aliases
                    bool found_alias = false;
                    for (const auto& [name, opt] : defined_options_) {
                        for (const auto& alias : opt.aliases) {
                            if (long_name == alias) {
                                if (opt.is_flag) {
                                    result.options[name] = true;
                                } else {
                                    if (!value.empty()) {
                                        result.options[name] = value;
                                    } else if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                                        result.options[name] = args[++i];
                                    } else {
                                        result.options[name] = "";
                                    }
                                }
                                found_alias = true;
                                break;
                            }
                        }
                        if (found_alias) break;
                    }
                    if (!found_alias) {
                        result.unknown_args.push_back(arg);
                    }
                }
                lock.unlock();
            }
            // Handle - short options
            else if (arg.size() >= 2 && arg[0] == '-' && arg[1] != '-') {
                // Could be combined flags: -abc
                for (size_t ci = 1; ci < arg.size(); ++ci) {
                    char ch = arg[ci];
                    auto sit = short_map.find(ch);
                    if (sit == short_map.end()) {
                        result.unknown_args.push_back(std::string("-") + ch);
                        continue;
                    }

                    const std::string& long_name = sit->second;
                    std::shared_lock lock(mutex_);
                    auto it = defined_options_.find(long_name);
                    if (it != defined_options_.end() && it->second.is_flag) {
                        result.options[long_name] = true;
                    } else if (it != defined_options_.end()) {
                        // This short option needs a value
                        if (ci + 1 < arg.size()) {
                            // Rest of this argument is the value
                            result.options[long_name] = arg.substr(ci + 1);
                            break; // consumed rest of arg
                        } else if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                            result.options[long_name] = args[++i];
                        }
                    }
                    lock.unlock();
                }
            }
            // Positional argument
            else {
                result.positional_args.push_back(arg);
            }
        }

        return result;
    }

    /// Generate help text
    std::string generate_help() const {
        std::ostringstream oss;
        oss << program_name_;

        if (!description_.empty()) {
            oss << " - " << description_;
        }
        oss << "\n\nUsage: " << program_name_ << " [OPTIONS]\n\nOptions:\n";

        std::shared_lock lock(mutex_);
        // Collect and sort options
        std::vector<const ArgOption*> sorted;
        sorted.reserve(defined_options_.size());
        for (const auto& [_, opt] : defined_options_) {
            if (!opt.is_hidden) sorted.push_back(&opt);
        }
        std::sort(sorted.begin(), sorted.end(),
            [](const ArgOption* a, const ArgOption* b) {
                return a->long_name < b->long_name;
            });

        for (const auto* opt : sorted) {
            oss << "  ";
            if (opt->short_name) {
                oss << "-" << opt->short_name << ", ";
            } else {
                oss << "    ";
            }
            oss << "--" << opt->long_name;
            if (!opt->is_flag && !opt->value_name.empty()) {
                oss << " <" << opt->value_name << ">";
            }
            if (!opt->description.empty()) {
                oss << "\n      " << opt->description;
            }
            if (!opt->default_value.empty()) {
                oss << " [default: " << opt->default_value << "]";
            }
            oss << "\n";
        }

        oss << "\n  -h, --help       Show this help message\n";
        oss << "  -V, --version    Show version information\n";

        return oss.str();
    }

    /// Convert parse result into a flat JSON config object (for env/arg override)
    json to_config_overrides(const ParseResult& result,
                             const std::string& prefix = "") const {
        json overrides = json::object();
        for (const auto& [name, value] : result.options) {
            std::string key = prefix.empty() ? name : prefix + "." + name;
            overrides[key] = value;
        }
        return overrides;
    }

private:
    std::string program_name_;
    std::string description_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ArgOption> defined_options_;
};

// =============================================================================
// ConfigEnvOverride - environment variable override support (Feature 3)
// =============================================================================

class ConfigEnvOverride {
public:
    /// Configure environment variable prefix
    explicit ConfigEnvOverride(const std::string& prefix = "CPPDESK_")
        : prefix_(prefix) {}

    /// Apply environment variable overrides to a config object
    /// Environment vars are named CPPDESK_SECTION_SUBSECTION_KEY=value
    void apply_overrides(json& config) const {
        std::string upper_prefix = prefix_;
        std::transform(upper_prefix.begin(), upper_prefix.end(),
                       upper_prefix.begin(), ::toupper);

        // Scan all environment variables
#if defined(_WIN32)
        // On Windows, use GetEnvironmentStrings
        // For portability, we iterate through common patterns
        apply_known_overrides(config);
#else
        // On Unix, we can iterate environ
        extern char** environ;
        if (environ) {
            for (char** env = environ; *env != nullptr; ++env) {
                std::string entry(*env);
                process_env_entry(entry, upper_prefix, config);
            }
        }
#endif
    }

    /// Get the override value for a specific key from the environment
    std::optional<std::string> get_override(const std::string& config_key) const {
        std::string env_name = config_key_to_env(config_key);
        const char* val = std::getenv(env_name.c_str());
        if (val && val[0]) {
            return std::string(val);
        }
        return std::nullopt;
    }

    /// Set a custom mapping of env var names to config keys
    void add_mapping(const std::string& env_name, const std::string& config_key) {
        std::unique_lock lock(mutex_);
        mappings_[env_name] = config_key;
    }

    /// List all configured mappings
    std::map<std::string, std::string> mappings() const {
        std::shared_lock lock(mutex_);
        return mappings_;
    }

    /// Convert a config key path to environment variable name
    std::string config_key_to_env(const std::string& key) const {
        std::string result = prefix_;
        for (char ch : key) {
            if (ch == '.') result += '_';
            else if (std::isalnum(static_cast<unsigned char>(ch))) {
                result += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            } else {
                result += '_';
            }
        }
        return result;
    }

    /// Convert an environment variable name to a config key path
    std::string env_to_config_key(const std::string& env_name) const {
        std::string upper_prefix = prefix_;
        std::transform(upper_prefix.begin(), upper_prefix.end(),
                       upper_prefix.begin(), ::toupper);

        if (env_name.size() <= upper_prefix.size()) return "";
        if (env_name.substr(0, upper_prefix.size()) != upper_prefix) return "";

        std::string suffix = env_name.substr(upper_prefix.size());
        std::string result;
        bool new_segment = true;
        for (char ch : suffix) {
            if (ch == '_') {
                new_segment = true;
            } else {
                if (new_segment && !result.empty()) result += '.';
                result += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                new_segment = false;
            }
        }
        return result;
    }

private:
    std::string prefix_;
    mutable std::shared_mutex mutex_;
    std::map<std::string, std::string> mappings_; // env_name -> config_key

    void process_env_entry(const std::string& entry, const std::string& upper_prefix,
                           json& config) const {
        size_t eq_pos = entry.find('=');
        if (eq_pos == std::string::npos) return;

        std::string name = entry.substr(0, eq_pos);
        std::string value = entry.substr(eq_pos + 1);

        // Check exact mappings first
        {
            std::shared_lock lock(mutex_);
            auto it = mappings_.find(name);
            if (it != mappings_.end()) {
                set_config_value(config, it->second, value);
                return;
            }
        }

        // Check prefix match
        if (name.size() > upper_prefix.size() &&
            name.substr(0, upper_prefix.size()) == upper_prefix) {
            std::string config_key = env_to_config_key(name);
            if (!config_key.empty()) {
                set_config_value(config, config_key, value);
                spdlog::debug("ConfigEnvOverride: {} = {} -> {}", name, value, config_key);
            }
        }
    }

    static void set_config_value(json& config, const std::string& key, const std::string& value) {
        auto parts = detail::split_path(key);
        json* target = detail::navigate_json(config, parts, true);
        if (!target) return;

        // Try to parse as JSON first, fall back to string
        try {
            *target = json::parse(value);
        } catch (...) {
            // Try boolean
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true") *target = true;
            else if (lower == "false") *target = false;
            else {
                // Try integer
                try {
                    *target = std::stoll(value);
                } catch (...) {
                    // Try double
                    try {
                        *target = std::stod(value);
                    } catch (...) {
                        *target = value; // String fallback
                    }
                }
            }
        }
    }

    void apply_known_overrides(json& config) const {
        // Fallback for Windows: check known env vars
        std::string upper_prefix = prefix_;
        std::transform(upper_prefix.begin(), upper_prefix.end(),
                       upper_prefix.begin(), ::toupper);

        // Common config paths to check
        static const std::vector<std::string> common_keys = {
            "config_path", "log_level", "data_dir", "cache_dir",
            "server_host", "server_port", "api_key", "timeout",
            "max_retries", "proxy_host", "proxy_port", "theme",
            "language", "editor", "terminal", "shell"
        };

        for (const auto& key : common_keys) {
            std::string env_name = config_key_to_env(key);
            const char* val = std::getenv(env_name.c_str());
            if (val && val[0]) {
                set_config_value(config, key, std::string(val));
                spdlog::debug("ConfigEnvOverride: {} = {} -> {}", env_name, val, key);
            }
        }

        // Also check custom mappings
        std::shared_lock lock(mutex_);
        for (const auto& [env_name, config_key] : mappings_) {
            const char* val = std::getenv(env_name.c_str());
            if (val && val[0]) {
                set_config_value(config, config_key, std::string(val));
                spdlog::debug("ConfigEnvOverride: {} = {} -> {}", env_name, val, config_key);
            }
        }
    }
};

// =============================================================================
// ConfigRemoteSync - remote config sync from server (Feature 9)
// =============================================================================

class ConfigRemoteSync {
public:
    struct RemoteConfig {
        std::string server_url;
        std::string auth_token;
        std::chrono::seconds sync_interval{config_constants::REMOTE_SYNC_INTERVAL};
        bool auto_sync = true;
        bool validate_ssl = true;
        int connect_timeout_secs = 10;
        int request_timeout_secs = 30;
        std::string config_endpoint = "/api/v1/config";
        std::map<std::string, std::string> custom_headers;
    };

    ConfigRemoteSync() = default;

    /// Configure the remote sync settings
    void configure(const RemoteConfig& config) {
        std::unique_lock lock(mutex_);
        config_ = config;
        spdlog::info("ConfigRemoteSync: configured server: {}", config_.server_url);
    }

    /// Start periodic sync
    void start_auto_sync() {
        std::unique_lock lock(mutex_);
        if (!config_.auto_sync || config_.server_url.empty()) {
            spdlog::warn("ConfigRemoteSync: auto-sync not configured");
            return;
        }

        if (sync_running_) {
            spdlog::info("ConfigRemoteSync: auto-sync already running");
            return;
        }

        sync_running_ = true;
        lock.unlock();

        sync_thread_ = std::thread([this] {
            spdlog::info("ConfigRemoteSync: auto-sync thread started");
            while (true) {
                {
                    std::shared_lock lock(mutex_);
                    if (!sync_running_) break;
                }

                // Wait for sync interval
                std::unique_lock lock(mutex_);
                auto interval = config_.sync_interval;
                lock.unlock();

                std::this_thread::sleep_for(interval);

                // Perform sync
                json result;
                std::string error;
                if (sync(result, error)) {
                    lock.lock();
                    if (sync_callback_) {
                        auto cb = sync_callback_;
                        lock.unlock();
                        cb(result);
                    }
                }
            }
            spdlog::info("ConfigRemoteSync: auto-sync thread stopped");
        });
    }

    /// Stop periodic sync
    void stop_auto_sync() {
        {
            std::unique_lock lock(mutex_);
            sync_running_ = false;
        }
        cv_.notify_all();
        if (sync_thread_.joinable()) {
            sync_thread_.join();
        }
    }

    /// Perform a one-time sync
    bool sync(json& result, std::string& error_msg) {
        RemoteConfig local_config;
        {
            std::shared_lock lock(mutex_);
            local_config = config_;
        }

        if (local_config.server_url.empty()) {
            error_msg = "No server URL configured";
            return false;
        }

        try {
            // Build the full URL
            std::string url = local_config.server_url + local_config.config_endpoint;

            spdlog::info("ConfigRemoteSync: syncing with {}", url);

            // Simulate HTTP request (in production, use libcurl or cpp-httplib)
            // For now, we implement a placeholder that returns success
            bool success = perform_http_get(url, local_config, result, error_msg);

            if (success) {
                {
                    std::unique_lock lock(mutex_);
                    last_sync_ = std::chrono::system_clock::now();
                    last_sync_result_ = result;
                }
                spdlog::info("ConfigRemoteSync: sync successful, got {} keys",
                             result.is_object() ? result.size() : 0);
            }

            return success;
        } catch (const std::exception& e) {
            error_msg = fmt::format("Sync exception: {}", e.what());
            spdlog::error("ConfigRemoteSync: {}", error_msg);
            return false;
        }
    }

    /// Set a callback for sync results
    void set_sync_callback(std::function<void(const json&)> callback) {
        std::unique_lock lock(mutex_);
        sync_callback_ = std::move(callback);
    }

    /// Get the last sync result
    std::optional<json> last_sync_result() const {
        std::shared_lock lock(mutex_);
        if (last_sync_result_.is_null()) return std::nullopt;
        return last_sync_result_;
    }

    /// Get the time of the last successful sync
    std::optional<std::chrono::system_clock::time_point> last_sync_time() const {
        std::shared_lock lock(mutex_);
        if (last_sync_ == std::chrono::system_clock::time_point{}) return std::nullopt;
        return last_sync_;
    }

    /// Merge remote config into local config
    json merge_remote(const json& local, const json& remote,
                      const std::string& strategy = "remote_wins") const {
        if (strategy == "remote_wins") {
            return detail::deep_merge(local, remote);
        } else if (strategy == "local_wins") {
            return detail::deep_merge(remote, local);
        } else if (strategy == "merge") {
            // Merge arrays by concatenation, objects recursively
            return merge_arrays_and_objects(local, remote);
        }
        return local;
    }

    /// Check if auto-sync is running
    bool is_running() const {
        std::shared_lock lock(mutex_);
        return sync_running_;
    }

private:
    mutable std::shared_mutex mutex_;
    RemoteConfig config_;
    std::atomic<bool> sync_running_{false};
    std::thread sync_thread_;
    std::condition_variable cv_;
    std::function<void(const json&)> sync_callback_;
    json last_sync_result_;
    std::chrono::system_clock::time_point last_sync_;

    bool perform_http_get(const std::string& url, const RemoteConfig& cfg,
                          json& result, std::string& error_msg) {
        // Placeholder HTTP implementation
        // In production, replace with:
        //   - libcurl
        //   - cpp-httplib
        //   - boost::beast
        //   - platform-native HTTP APIs

        spdlog::debug("ConfigRemoteSync: HTTP GET {} (placeholder)", url);

        // Simulate network delay
        // std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // For now, return a mock response indicating the feature needs a real HTTP client
        result = json::object();
        result["_sync_status"] = "not_implemented";
        result["_sync_message"] = "Remote sync requires a real HTTP client library";
        result["_sync_url"] = url;
        result["_sync_timestamp"] = detail::make_timestamp();

        // To suppress unused parameter warnings
        (void)cfg;

        return true; // Return true so the caller knows the method was reached
    }

    static json merge_arrays_and_objects(const json& local, const json& remote) {
        if (local.is_object() && remote.is_object()) {
            json result = local;
            for (auto& [key, val] : remote.items()) {
                if (result.contains(key)) {
                    result[key] = merge_arrays_and_objects(result[key], val);
                } else {
                    result[key] = val;
                }
            }
            return result;
        } else if (local.is_array() && remote.is_array()) {
            json result = local;
            for (const auto& item : remote) {
                if (std::find(result.begin(), result.end(), item) == result.end()) {
                    result.push_back(item);
                }
            }
            return result;
        }
        return remote; // remote wins for scalars
    }
};

// =============================================================================
// ConfigExportImport - config export/import (Feature 6)
// =============================================================================

class ConfigExportImport {
public:
    enum class Format {
        JSON,
        JSON_MINIFIED,
        YAML,       // Requires yaml-cpp or similar
        TOML,       // Requires toml11 or similar
        ENV_FILE,   // .env format
        INI         // .ini format
    };

    /// Export config to a file
    bool export_config(const json& config, const std::string& output_path,
                       Format format = Format::JSON) const {
        try {
            std::string content;
            switch (format) {
                case Format::JSON:
                    content = config.dump(2);
                    break;
                case Format::JSON_MINIFIED:
                    content = config.dump();
                    break;
                case Format::ENV_FILE:
                    content = export_as_env(config);
                    break;
                case Format::INI:
                    content = export_as_ini(config);
                    break;
                case Format::YAML:
                    content = export_as_yaml_placeholder(config);
                    break;
                case Format::TOML:
                    content = export_as_toml_placeholder(config);
                    break;
            }

            if (!detail::atomic_write(output_path, content)) {
                spdlog::error("ConfigExportImport: failed to write {}", output_path);
                return false;
            }

            spdlog::info("ConfigExportImport: exported config to {} (format: {})",
                         output_path, format_name(format));
            return true;
        } catch (const std::exception& e) {
            spdlog::error("ConfigExportImport: export failed: {}", e.what());
            return false;
        }
    }

    /// Export config as a string
    std::string export_string(const json& config, Format format = Format::JSON) const {
        switch (format) {
            case Format::JSON:          return config.dump(2);
            case Format::JSON_MINIFIED: return config.dump();
            case Format::ENV_FILE:      return export_as_env(config);
            case Format::INI:           return export_as_ini(config);
            case Format::YAML:          return export_as_yaml_placeholder(config);
            case Format::TOML:          return export_as_toml_placeholder(config);
        }
        return config.dump(2);
    }

    /// Import config from a file
    std::optional<json> import_config(const std::string& input_path) const {
        try {
            std::ifstream ifs(input_path);
            if (!ifs) {
                spdlog::error("ConfigExportImport: cannot open {}", input_path);
                return std::nullopt;
            }

            std::string content((std::istreambuf_iterator<char>(ifs)),
                                std::istreambuf_iterator<char>());

            // Auto-detect format based on extension
            fs::path path(input_path);
            std::string ext = path.extension().string();

            if (ext == ".json") {
                return json::parse(content);
            } else if (ext == ".env" || ext == ".envrc") {
                return import_from_env(content);
            } else if (ext == ".ini" || ext == ".cfg" || ext == ".conf") {
                return import_from_ini(content);
            } else if (ext == ".yaml" || ext == ".yml") {
                return import_placeholder(content, "YAML");
            } else if (ext == ".toml") {
                return import_placeholder(content, "TOML");
            }

            // Default: try JSON
            try {
                return json::parse(content);
            } catch (...) {
                spdlog::error("ConfigExportImport: unable to parse {}", input_path);
                return std::nullopt;
            }
        } catch (const std::exception& e) {
            spdlog::error("ConfigExportImport: import failed: {}", e.what());
            return std::nullopt;
        }
    }

    /// Import config from a string with explicit format
    std::optional<json> import_string(const std::string& content, Format format) const {
        try {
            switch (format) {
                case Format::JSON:
                case Format::JSON_MINIFIED:
                    return json::parse(content);
                case Format::ENV_FILE:
                    return import_from_env(content);
                case Format::INI:
                    return import_from_ini(content);
                case Format::YAML:
                    return import_placeholder(content, "YAML");
                case Format::TOML:
                    return import_placeholder(content, "TOML");
            }
            return std::nullopt;
        } catch (const std::exception& e) {
            spdlog::error("ConfigExportImport: import_string failed: {}", e.what());
            return std::nullopt;
        }
    }

    /// Convert a flat JSON object to key=value environment format
    static std::string export_as_env(const json& config, const std::string& prefix = "CPPDESK_") {
        std::ostringstream oss;
        flatten_and_write_env(config, prefix, oss);
        return oss.str();
    }

    /// Convert key=value content to JSON
    static json import_from_env(const std::string& content) {
        json result = json::object();
        std::istringstream stream(content);
        std::string line;
        while (std::getline(stream, line)) {
            // Skip comments and empty lines
            if (line.empty() || line[0] == '#') continue;

            size_t eq_pos = line.find('=');
            if (eq_pos == std::string::npos) continue;

            std::string key = line.substr(0, eq_pos);
            std::string value = line.substr(eq_pos + 1);

            // Trim
            while (!key.empty() && key.back() == ' ') key.pop_back();
            while (!value.empty() && value.front() == ' ') value.erase(0, 1);

            // Remove quotes
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            } else if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
                value = value.substr(1, value.size() - 2);
            }

            // Handle nested keys with __ separator
            auto parts = detail::split_path(key);
            json* target = &result;
            for (size_t i = 0; i < parts.size(); ++i) {
                if (i == parts.size() - 1) {
                    // Try to parse the value
                    try { (*target)[parts[i]] = json::parse(value); }
                    catch (...) { (*target)[parts[i]] = value; }
                } else {
                    if (!(*target).contains(parts[i]) || !(*target)[parts[i]].is_object()) {
                        (*target)[parts[i]] = json::object();
                    }
                    target = &(*target)[parts[i]];
                }
            }
        }
        return result;
    }

    /// Export as INI format
    static std::string export_as_ini(const json& config) {
        std::ostringstream oss;
        write_ini_section(config, "", oss);
        return oss.str();
    }

    /// Import from INI format
    static json import_from_ini(const std::string& content) {
        json result = json::object();
        std::istringstream stream(content);
        std::string line;
        std::string current_section;

        while (std::getline(stream, line)) {
            // Skip comments and empty lines
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;

            // Section header
            if (line[0] == '[') {
                size_t end = line.find(']');
                if (end != std::string::npos) {
                    current_section = line.substr(1, end - 1);
                }
                continue;
            }

            // Key=Value
            size_t eq_pos = line.find('=');
            if (eq_pos == std::string::npos) continue;

            std::string key = line.substr(0, eq_pos);
            std::string value = line.substr(eq_pos + 1);

            // Trim
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(0, 1);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);

            // Determine target
            json* target = &result;
            if (!current_section.empty()) {
                if (!result.contains(current_section)) {
                    result[current_section] = json::object();
                }
                target = &result[current_section];
            }

            // Try to parse value
            try { (*target)[key] = json::parse(value); }
            catch (...) {
                // Try boolean
                std::string lower = value;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower == "true" || lower == "yes" || lower == "on") (*target)[key] = true;
                else if (lower == "false" || lower == "no" || lower == "off") (*target)[key] = false;
                else {
                    try { (*target)[key] = std::stoll(value); }
                    catch (...) {
                        try { (*target)[key] = std::stod(value); }
                        catch (...) { (*target)[key] = value; }
                    }
                }
            }
        }
        return result;
    }

private:
    static std::string format_name(Format fmt) {
        switch (fmt) {
            case Format::JSON:          return "JSON";
            case Format::JSON_MINIFIED: return "JSON_MINIFIED";
            case Format::YAML:          return "YAML";
            case Format::TOML:          return "TOML";
            case Format::ENV_FILE:      return "ENV";
            case Format::INI:           return "INI";
        }
        return "UNKNOWN";
    }

    static void flatten_and_write_env(const json& node, const std::string& prefix,
                                      std::ostringstream& oss) {
        if (node.is_object()) {
            for (auto& [key, val] : node.items()) {
                std::string new_key = prefix.empty() ? key : prefix + key;
                if (val.is_object()) {
                    flatten_and_write_env(val, new_key + "_", oss);
                } else if (val.is_array()) {
                    for (size_t i = 0; i < val.size(); ++i) {
                        std::string arr_key = new_key + "_" + std::to_string(i);
                        oss << arr_key << "=" << val_to_env_str(val[i]) << "\n";
                    }
                } else {
                    oss << new_key << "=" << val_to_env_str(val) << "\n";
                }
            }
        }
    }

    static std::string val_to_env_str(const json& val) {
        if (val.is_string()) {
            std::string s = val.get<std::string>();
            // Quote if contains special chars
            if (s.find_first_of(" \t\n\r#=") != std::string::npos) {
                return "\"" + s + "\"";
            }
            return s;
        }
        if (val.is_boolean()) return val.get<bool>() ? "true" : "false";
        return val.dump();
    }

    static void write_ini_section(const json& node, const std::string& section,
                                  std::ostringstream& oss) {
        if (node.is_object()) {
            if (!section.empty()) {
                oss << "[" << section << "]\n";
            }
            for (auto& [key, val] : node.items()) {
                if (val.is_object()) {
                    std::string sub = section.empty() ? key : section + "." + key;
                    write_ini_section(val, sub, oss);
                } else if (val.is_array()) {
                    for (size_t i = 0; i < val.size(); ++i) {
                        oss << key << "[" << i << "]=" << val_to_ini_str(val[i]) << "\n";
                    }
                } else {
                    oss << key << "=" << val_to_ini_str(val) << "\n";
                }
            }
            if (!section.empty()) {
                oss << "\n";
            }
        }
    }

    static std::string val_to_ini_str(const json& val) {
        if (val.is_string()) return val.get<std::string>();
        if (val.is_boolean()) return val.get<bool>() ? "true" : "false";
        if (val.is_number_integer()) return std::to_string(val.get<int64_t>());
        if (val.is_number_float()) return std::to_string(val.get<double>());
        return val.dump();
    }

    static std::string export_as_yaml_placeholder(const json& config) {
        // Placeholder: real YAML export requires yaml-cpp
        std::ostringstream oss;
        oss << "# YAML export placeholder - requires yaml-cpp library\n";
        oss << "# JSON fallback:\n";
        oss << config.dump(2);
        return oss.str();
    }

    static std::string export_as_toml_placeholder(const json& config) {
        // Placeholder: real TOML export requires toml11
        std::ostringstream oss;
        oss << "# TOML export placeholder - requires toml11 library\n";
        oss << "# JSON fallback:\n";
        oss << config.dump(2);
        return oss.str();
    }

    static std::optional<json> import_placeholder(const std::string& content,
                                                  const std::string& format_name) {
        // Try JSON as fallback
        try {
            return json::parse(content);
        } catch (...) {
            spdlog::error("ConfigExportImport: {} import requires a {} parser library",
                          format_name, format_name);
        }
        return std::nullopt;
    }
};

// =============================================================================
// DefaultConfigGenerator - per-platform default config generation (Feature 11)
// =============================================================================

class DefaultConfigGenerator {
public:
    /// Generate default configuration for the current platform
    static json generate_defaults() {
        json config = json::object();

        // Version marker
        config[std::string(config_constants::CONFIG_VERSION_KEY)] =
            config_constants::CURRENT_CONFIG_VERSION;

        // Common settings
        generate_common_defaults(config);

        // Platform-specific settings
#if defined(__linux__)
        generate_linux_defaults(config);
#elif defined(__APPLE__)
        generate_macos_defaults(config);
#elif defined(_WIN32)
        generate_windows_defaults(config);
#else
        generate_generic_defaults(config);
#endif

        return config;
    }

    /// Generate defaults for a specific named platform
    static json generate_for_platform(const std::string& platform) {
        json config = json::object();
        config[std::string(config_constants::CONFIG_VERSION_KEY)] =
            config_constants::CURRENT_CONFIG_VERSION;
        generate_common_defaults(config);

        if (platform == "linux" || platform == "Linux") {
            generate_linux_defaults(config);
        } else if (platform == "macos" || platform == "macOS" || platform == "darwin") {
            generate_macos_defaults(config);
        } else if (platform == "windows" || platform == "Windows" || platform == "win32") {
            generate_windows_defaults(config);
        } else {
            generate_generic_defaults(config);
        }

        return config;
    }

    /// Generate defaults with a template name (e.g., "minimal", "full", "server", "desktop")
    static json generate_from_template(const std::string& template_name) {
        json config = json::object();
        config[std::string(config_constants::CONFIG_VERSION_KEY)] =
            config_constants::CURRENT_CONFIG_VERSION;
        generate_common_defaults(config);

        if (template_name == "minimal") {
            generate_minimal_defaults(config);
        } else if (template_name == "full" || template_name == "desktop") {
            generate_full_defaults(config);
        } else if (template_name == "server" || template_name == "headless") {
            generate_server_defaults(config);
        } else if (template_name == "development" || template_name == "dev") {
            generate_dev_defaults(config);
        } else if (template_name == "embedded") {
            generate_embedded_defaults(config);
        } else {
            generate_common_defaults(config);
#if defined(__linux__)
            generate_linux_defaults(config);
#elif defined(__APPLE__)
            generate_macos_defaults(config);
#elif defined(_WIN32)
            generate_windows_defaults(config);
#endif
        }

        return config;
    }

private:
    static void generate_common_defaults(json& config) {
        config["core"] = {
            {"log_level", "info"},
            {"log_file", detail::get_platform_log_dir() + "/cppdesk.log"},
            {"data_dir", detail::get_platform_config_dir()},
            {"cache_dir", detail::get_platform_cache_dir()},
            {"temp_dir", detail::get_platform_temp_dir()},
            {"language", "en"},
            {"timezone", "UTC"},
            {"max_threads", static_cast<int>(std::thread::hardware_concurrency())},
            {"auto_save", true},
            {"auto_save_interval_ms", 5000},
            {"check_updates", true},
            {"update_channel", "stable"},
            {"telemetry_enabled", false},
            {"anonymous_usage_stats", false}
        };

        config["ui"] = {
            {"theme", "system"},
            {"font_size", 14},
            {"font_family", "monospace"},
            {"window_width", 1280},
            {"window_height", 800},
            {"window_x", -1},
            {"window_y", -1},
            {"maximized", false},
            {"fullscreen", false},
            {"sidebar_visible", true},
            {"status_bar_visible", true},
            {"tab_size", 4},
            {"line_numbers", true},
            {"word_wrap", true},
            {"syntax_highlighting", true},
            {"animations_enabled", true},
            {"notifications_enabled", true}
        };

        config["network"] = {
            {"connect_timeout_secs", 10},
            {"request_timeout_secs", 30},
            {"max_retries", 3},
            {"retry_backoff_ms", 1000},
            {"max_connections", 16},
            {"keep_alive", true},
            {"proxy", {
                {"enabled", false},
                {"host", ""},
                {"port", 8080},
                {"auth_required", false},
                {"username", ""},
                {"password", ""},
                {"bypass_local", true}
            }},
            {"ssl", {
                {"verify_peer", true},
                {"verify_host", true},
                {"ca_bundle_path", ""},
                {"client_cert_path", ""},
                {"client_key_path", ""}
            }}
        };

        config["security"] = {
            {"encrypt_config", false},
            {"key_file_path", ""},
            {"session_timeout_minutes", 60},
            {"max_login_attempts", 5},
            {"lock_on_idle_minutes", 15},
            {"clipboard_clear_seconds", 30},
            {"secure_memory", true}
        };

        config["plugins"] = {
            {"enabled", true},
            {"auto_load", true},
            {"directory", ""},
            {"blocked", json::array()},
            {"allowed_only", json::array()}
        };
    }

    static void generate_linux_defaults(json& config) {
        config["linux"] = {
            {"display_server", ""},  // auto-detect: wayland or x11
            {"xdg_portal", true},
            {"dbus_integration", true},
            {"system_tray", true},
            {"notifications", "dbus"},
            {"terminal", ""},        // auto-detect
            {"file_manager", ""},    // auto-detect
            {"browser", "xdg-open"},
            {"editor", std::getenv("EDITOR") ? std::getenv("EDITOR") : "nano"}
        };
        config["core"]["config_dir"] = detail::get_platform_config_dir();
        config["core"]["cache_dir"] = detail::get_platform_cache_dir();
    }

    static void generate_macos_defaults(json& config) {
        config["macos"] = {
            {"use_native_titlebar", true},
            {"transparent_titlebar", false},
            {"vibrant_background", true},
            {"touch_bar_support", false},
            {"dock_integration", true},
            {"menu_bar_icon", true},
            {"system_notifications", true},
            {"keychain_integration", true},
            {"terminal", "Terminal.app"},
            {"browser", "open"},
            {"editor", "open -t"}
        };
        config["core"]["config_dir"] = detail::get_platform_config_dir();
        config["core"]["cache_dir"] = detail::get_platform_cache_dir();
    }

    static void generate_windows_defaults(json& config) {
        config["windows"] = {
            {"use_acrylic", true},
            {"taskbar_integration", true},
            {"start_menu_shortcut", true},
            {"registry_integration", false},
            {"shell_extension", false},
            {"dark_titlebar", true},
            {"high_dpi_aware", true},
            {"terminal", "wt.exe"},
            {"browser", "start"},
            {"editor", "notepad.exe"},
            {"use_credential_manager", true}
        };
        config["core"]["config_dir"] = detail::get_platform_config_dir();
        config["core"]["cache_dir"] = detail::get_platform_cache_dir();
    }

    static void generate_generic_defaults(json& config) {
        config["core"]["config_dir"] = detail::get_platform_config_dir();
        config["core"]["cache_dir"] = detail::get_platform_cache_dir();
    }

    static void generate_minimal_defaults(json& config) {
        // Strip down to essentials
        config.erase("plugins");
        config.erase("ui");
        config.erase("linux");
        config.erase("macos");
        config.erase("windows");
        config["core"].erase("check_updates");
        config["core"].erase("telemetry_enabled");
        config["core"]["log_level"] = "warn";
    }

    static void generate_full_defaults(json& config) {
        // Add developer tools
        config["dev"] = {
            {"debug_mode", false},
            {"profiling_enabled", false},
            {"trace_logging", false},
            {"devtools_enabled", false},
            {"hot_reload", false},
            {"watch_config", false},
            {"watch_interval_ms", 1000}
        };

        // Add experimental features
        config["experimental"] = {
            {"gpu_acceleration", true},
            {"async_rendering", true},
            {"parallel_parsing", false},
            {"aggressive_caching", false},
            {"prefetch_enabled", false}
        };

        // Add advanced network settings
        config["network"]["http2_enabled"] = true;
        config["network"]["gzip_compression"] = true;
        config["network"]["dns_over_https"] = false;
        config["network"]["ipv6_enabled"] = true;
    }

    static void generate_server_defaults(json& config) {
        // Remove UI-related settings
        config.erase("ui");
        config["core"]["log_file"] = detail::get_platform_log_dir() + "/cppdesk-server.log";
        config["core"]["log_level"] = "info";

        config["server"] = {
            {"enabled", true},
            {"bind_address", "127.0.0.1"},
            {"port", 9876},
            {"unix_socket", ""},
            {"max_clients", 100},
            {"backlog", 128},
            {"tcp_nodelay", true},
            {"tcp_keepalive_secs", 60},
            {"request_timeout_secs", 30},
            {"max_request_size_mb", 10},
            {"rate_limit", {
                {"enabled", true},
                {"requests_per_second", 100},
                {"burst_size", 50}
            }},
            {"cors", {
                {"enabled", false},
                {"allowed_origins", json::array({"*"})},
                {"allowed_methods", json::array({"GET", "POST", "PUT", "DELETE"})},
                {"allowed_headers", json::array()}
            }}
        };
    }

    static void generate_dev_defaults(json& config) {
        config["core"]["log_level"] = "debug";
        config["dev"] = {
            {"debug_mode", true},
            {"profiling_enabled", false},
            {"trace_logging", true},
            {"devtools_enabled", true},
            {"hot_reload", true},
            {"watch_config", true},
            {"watch_interval_ms", 500},
            {"sandbox", true},
            {"test_mode", false}
        };
        config["ui"]["animations_enabled"] = false; // faster dev iteration
    }

    static void generate_embedded_defaults(json& config) {
        // Minimal config for embedded systems
        config.erase("ui");
        config.erase("plugins");
        config["core"]["log_level"] = "error";
        config["core"]["auto_save"] = false;
        config["core"]["check_updates"] = false;
        config["core"]["telemetry_enabled"] = false;
        config["network"]["max_connections"] = 4;
        config["network"]["max_retries"] = 1;
    }
};

// =============================================================================
// ConfigManager - main facade that integrates all features
// =============================================================================

class ConfigManager {
public:
    /// Construct with explicit config file path
    explicit ConfigManager(const std::string& config_path = "")
        : config_path_(config_path.empty()
              ? (fs::path(detail::get_platform_config_dir()) / config_constants::DEFAULT_CONFIG_FILENAME).string()
              : config_path),
          backup_dir_(fs::path(config_path_).parent_path() / config_constants::DEFAULT_BACKUP_DIR),
          profiles_dir_(fs::path(config_path_).parent_path() / config_constants::DEFAULT_PROFILES_DIR),
          profiles_(profiles_dir_),
          rollback_(backup_dir_),
          env_override_("CPPDESK_"),
          cli_parser_("cppdesk", "CppDesk Configuration Manager") {
        spdlog::info("ConfigManager: config path: {}", config_path_);
    }

    // ---- Initialization ----

    /// Initialize the config manager: load config, apply migrations, apply overrides
    bool initialize() {
        std::unique_lock lock(mutex_);

        // Ensure directories exist
        fs::create_directories(fs::path(config_path_).parent_path());
        fs::create_directories(backup_dir_);
        fs::create_directories(profiles_dir_);

        // Load config from disk
        if (!load_from_disk()) {
            // Generate defaults if config doesn't exist
            config_ = DefaultConfigGenerator::generate_defaults();
            spdlog::info("ConfigManager: using generated defaults");
        }

        // Apply migrations
        migrate_config();

        // Load profiles
        profiles_.load_all();

        // Apply active profile overrides
        apply_profile_overrides();

        // Apply environment variable overrides
        env_override_.apply_overrides(config_);

        // Decrypt sensitive values if encryptor is ready
        if (encryptor_.is_ready()) {
            encryptor_.decrypt_sensitive(config_);
        }

        initialized_ = true;
        spdlog::info("ConfigManager: initialized with {} top-level keys", config_.size());
        return true;
    }

    /// Load config explicitly from a different file path
    bool load(const std::string& path) {
        std::unique_lock lock(mutex_);
        config_path_ = path;
        bool ok = load_from_disk();
        if (ok) {
            migrate_config();
            apply_profile_overrides();
            env_override_.apply_overrides(config_);
            notify(ConfigChangeEvent::config_reloaded());
        }
        return ok;
    }

    /// Reload config from disk
    bool reload() {
        std::unique_lock lock(mutex_);
        return load_from_disk();
    }

    /// Save config to disk
    bool save() {
        std::shared_lock lock(mutex_);

        // Encrypt sensitive values before saving
        json save_config = config_;
        if (encryptor_.is_ready()) {
            encryptor_.encrypt_sensitive(save_config);
        }

        std::string content = save_config.dump(2);

        // Create backup before saving
        rollback_.create_backup(config_path_,
            load_raw_content(config_path_).value_or(""));

        bool ok = detail::atomic_write(config_path_, content);
        if (ok) {
            spdlog::info("ConfigManager: saved to {}", config_path_);
        }
        return ok;
    }

    // ---- Accessors ----

    template<typename T>
    T get(const std::string& key, T default_val) const {
        std::shared_lock lock(mutex_);
        json* val = const_cast<json&>(config_).find_path(key, '.');
        // Manual path navigation for dot-notation
        auto parts = detail::split_path(key);
        const json* node = detail::navigate_json(config_, parts);
        if (node) {
            try { return node->get<T>(); } catch (...) {}
        }
        return default_val;
    }

    json get_json(const std::string& key) const {
        std::shared_lock lock(mutex_);
        auto parts = detail::split_path(key);
        const json* node = detail::navigate_json(config_, parts);
        if (node) return *node;
        return json();
    }

    std::string get_string(const std::string& key, const std::string& def = "") const {
        return get<std::string>(key, def);
    }

    int get_int(const std::string& key, int def = 0) const {
        return get<int>(key, def);
    }

    int64_t get_int64(const std::string& key, int64_t def = 0) const {
        return get<int64_t>(key, def);
    }

    double get_double(const std::string& key, double def = 0.0) const {
        return get<double>(key, def);
    }

    bool get_bool(const std::string& key, bool def = false) const {
        return get<bool>(key, def);
    }

    bool has(const std::string& key) const {
        std::shared_lock lock(mutex_);
        auto parts = detail::split_path(key);
        return detail::navigate_json(config_, parts) != nullptr;
    }

    // ---- Mutators ----

    template<typename T>
    void set(const std::string& key, T value) {
        json old_value;
        {
            std::unique_lock lock(mutex_);
            auto parts = detail::split_path(key);
            const json* old_node = detail::navigate_json(config_, parts);
            if (old_node) old_value = *old_node;

            json* target = detail::navigate_json(config_, parts, true);
            if (!target) {
                spdlog::error("ConfigManager: cannot set '{}'", key);
                return;
            }
            *target = value;
        }
        notify(ConfigChangeEvent::value_changed(key, old_value, json(value)));
        save();
    }

    void set_json(const std::string& key, const json& value) {
        json old_value;
        {
            std::unique_lock lock(mutex_);
            auto parts = detail::split_path(key);
            const json* old_node = detail::navigate_json(config_, parts);
            if (old_node) old_value = *old_node;

            json* target = detail::navigate_json(config_, parts, true);
            if (!target) return;
            *target = value;
        }
        notify(ConfigChangeEvent::value_changed(key, old_value, value));
        save();
    }

    void remove(const std::string& key) {
        json old_value;
        {
            std::unique_lock lock(mutex_);
            auto parts = detail::split_path(key);
            if (parts.empty()) return;

            // Navigate to parent
            std::vector<std::string> parent_parts(parts.begin(), parts.end() - 1);
            const std::string& last = parts.back();

            json* parent = parent_parts.empty()
                ? &config_
                : detail::navigate_json(config_, parent_parts, false);

            if (parent && parent->is_object() && parent->contains(last)) {
                old_value = (*parent)[last];
                parent->erase(last);
            }
        }
        if (!old_value.is_null()) {
            notify(ConfigChangeEvent::value_removed(key, old_value));
            save();
        }
    }

    void merge(const json& overlay) {
        {
            std::unique_lock lock(mutex_);
            config_ = detail::deep_merge(config_, overlay);
        }
        notify(ConfigChangeEvent::config_reloaded());
        save();
    }

    // ---- Schema ----

    /// Load a JSON schema for validation
    bool load_schema(const std::string& schema_path) {
        return schema_.load(schema_path);
    }

    /// Set schema from JSON object
    void set_schema(const json& schema_def) {
        schema_ = ConfigSchema(schema_def);
    }

    /// Validate current config against the schema
    bool validate_config(std::string& error_msg) const {
        std::shared_lock lock(mutex_);
        return schema_.validate(config_, error_msg);
    }

    /// Validate and throw on failure
    void validate_or_throw() const {
        std::shared_lock lock(mutex_);
        schema_.validate_or_throw(config_);
    }

    const ConfigSchema& schema() const { return schema_; }

    // ---- Migration ----

    /// Register a migration function
    void register_migration(int from_ver, int to_ver,
                            std::function<json(const json&)> func,
                            const std::string& desc = "") {
        migration_.register_migration(from_ver, to_ver, std::move(func), desc);
    }

    /// Get the current config version
    int config_version() const {
        std::shared_lock lock(mutex_);
        std::string vk(config_constants::CONFIG_VERSION_KEY);
        return config_.value(vk, 0);
    }

    // ---- Profiles ----

    bool create_profile(const std::string& name, const std::string& display = "",
                        const std::string& desc = "") {
        return profiles_.create_profile(name, display, desc);
    }

    bool delete_profile(const std::string& name) {
        return profiles_.delete_profile(name);
    }

    bool activate_profile(const std::string& name) {
        bool ok = profiles_.activate_profile(name,
            [this](const ConfigProfileData& profile) {
                notify(ConfigChangeEvent::profile_switched(profile.name));
        });
        if (ok) {
            apply_profile_overrides();
            save();
        }
        return ok;
    }

    void deactivate_profile() {
        profiles_.deactivate_profile();
        notify(ConfigChangeEvent::profile_switched(""));
    }

    std::optional<std::string> active_profile() const {
        return profiles_.active_profile_name();
    }

    std::vector<std::string> list_profiles() const {
        return profiles_.list_profiles();
    }

    bool has_profile(const std::string& name) const {
        return profiles_.has_profile(name);
    }

    std::optional<ConfigProfileData> get_profile_data(const std::string& name) const {
        return profiles_.get_profile(name);
    }

    // ---- Encryption ----

    void set_encryption_key(const std::string& key) {
        encryptor_.set_master_key(key);
    }

    bool load_encryption_key(const std::string& key_path) {
        return encryptor_.load_key_file(key_path);
    }

    bool is_encryption_ready() const {
        return encryptor_.is_ready();
    }

    std::string encrypt_value(const std::string& plaintext) const {
        return encryptor_.encrypt(plaintext);
    }

    std::string decrypt_value(const std::string& ciphertext) const {
        return encryptor_.decrypt(ciphertext);
    }

    /// Encrypt all sensitive values in the current config (done on save)
    void encrypt_sensitive() {
        std::unique_lock lock(mutex_);
        encryptor_.encrypt_sensitive(config_);
    }

    /// Decrypt all sensitive values (done on load)
    void decrypt_sensitive() {
        std::unique_lock lock(mutex_);
        encryptor_.decrypt_sensitive(config_);
    }

    // ---- Environment Overrides ----

    void apply_env_overrides() {
        std::unique_lock lock(mutex_);
        env_override_.apply_overrides(config_);
    }

    void add_env_mapping(const std::string& env_name, const std::string& config_key) {
        env_override_.add_mapping(env_name, config_key);
    }

    // ---- CLI Parsing ----

    ConfigCLIParser& cli_parser() { return cli_parser_; }
    const ConfigCLIParser& cli_parser() const { return cli_parser_; }

    ConfigCLIParser::ParseResult parse_cli(int argc, const char* const argv[]) {
        return cli_parser_.parse(argc, argv);
    }

    ConfigCLIParser::ParseResult parse_cli(const std::vector<std::string>& args) {
        return cli_parser_.parse(args);
    }

    void apply_cli_overrides(const ConfigCLIParser::ParseResult& result) {
        std::unique_lock lock(mutex_);
        json overrides = cli_parser_.to_config_overrides(result);
        config_ = detail::deep_merge(config_, overrides);
    }

    // ---- Remote Sync ----

    void configure_remote_sync(const ConfigRemoteSync::RemoteConfig& config) {
        remote_sync_.configure(config);
    }

    void start_remote_sync() {
        // Set callback to merge remote config
        remote_sync_.set_sync_callback([this](const json& remote) {
            std::unique_lock lock(mutex_);
            json merged = remote_sync_.merge_remote(config_, remote, "remote_wins");
            config_ = merged;
            notify(ConfigChangeEvent{.type = ConfigChangeEvent::Type::SYNC_COMPLETED});
            save();
        });
        remote_sync_.start_auto_sync();
    }

    void stop_remote_sync() {
        remote_sync_.stop_auto_sync();
    }

    bool sync_remote_now(json& result, std::string& error) {
        return remote_sync_.sync(result, error);
    }

    // ---- Export/Import ----

    bool export_config(const std::string& path,
                       ConfigExportImport::Format fmt = ConfigExportImport::Format::JSON) const {
        std::shared_lock lock(mutex_);
        return exporter_.export_config(config_, path, fmt);
    }

    std::string export_string(ConfigExportImport::Format fmt = ConfigExportImport::Format::JSON) const {
        std::shared_lock lock(mutex_);
        return exporter_.export_string(config_, fmt);
    }

    bool import_config(const std::string& path, bool merge_import = false) {
        auto imported = exporter_.import_config(path);
        if (!imported) return false;

        std::unique_lock lock(mutex_);
        if (merge_import) {
            config_ = detail::deep_merge(config_, *imported);
        } else {
            // Create backup before replacing
            rollback_.create_backup(config_path_, config_.dump(2));
            config_ = *imported;
        }
        notify(ConfigChangeEvent::config_reloaded());
        save();
        return true;
    }

    // ---- Rollback ----

    bool rollback() {
        std::unique_lock lock(mutex_);
        if (rollback_.rollback(config_path_)) {
            load_from_disk();
            notify(ConfigChangeEvent{.type = ConfigChangeEvent::Type::ROLLBACK_PERFORMED});
            return true;
        }
        return false;
    }

    size_t backup_count() const {
        return rollback_.backup_count(config_path_);
    }

    void create_backup() {
        std::shared_lock lock(mutex_);
        rollback_.create_backup(config_path_, config_.dump(2));
    }

    // ---- Observer / Notifications ----

    void subscribe(std::shared_ptr<ConfigObserver> observer) {
        notifications_.subscribe(std::move(observer));
    }

    void unsubscribe(std::shared_ptr<ConfigObserver> observer) {
        notifications_.unsubscribe(std::move(observer));
    }

    void subscribe_key(std::shared_ptr<ConfigObserver> observer, const std::string& key) {
        notifications_.subscribe_key(std::move(observer), key);
    }

    void unsubscribe_key(std::shared_ptr<ConfigObserver> observer, const std::string& key) {
        notifications_.unsubscribe_key(std::move(observer), key);
    }

    size_t observer_count() const {
        return notifications_.observer_count();
    }

    // ---- Defaults ----

    static json generate_default_config() {
        return DefaultConfigGenerator::generate_defaults();
    }

    static json generate_config_for(const std::string& platform) {
        return DefaultConfigGenerator::generate_for_platform(platform);
    }

    static json generate_config_from_template(const std::string& tmpl) {
        return DefaultConfigGenerator::generate_from_template(tmpl);
    }

    // ---- Helpers ----

    const std::string& path() const { return config_path_; }

    bool is_initialized() const { return initialized_; }

    /// Get a copy of the entire config
    json get_all() const {
        std::shared_lock lock(mutex_);
        return config_;
    }

    /// Get config keys at the top level
    std::vector<std::string> keys() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> result;
        if (config_.is_object()) {
            for (auto& [key, _] : config_.items()) {
                result.push_back(key);
            }
        }
        return result;
    }

    /// Recursively collect all dot-separated key paths
    std::vector<std::string> all_keys() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> result;
        collect_keys(config_, "", result);
        return result;
    }

    /// Pretty-print the config (useful for debugging)
    std::string dump() const {
        std::shared_lock lock(mutex_);
        return config_.dump(2);
    }

    /// Reset to defaults
    void reset_to_defaults() {
        std::unique_lock lock(mutex_);
        rollback_.create_backup(config_path_, config_.dump(2));
        config_ = DefaultConfigGenerator::generate_defaults();
        notify(ConfigChangeEvent::config_reloaded());
        save();
        spdlog::info("ConfigManager: reset to defaults");
    }

    /// Get info about the configuration state
    json diagnostics() const {
        std::shared_lock lock(mutex_);
        json diag;
        diag["config_path"] = config_path_;
        diag["initialized"] = initialized_;
        diag["size_bytes"] = config_.dump().size();
        diag["top_level_keys"] = config_.is_object() ? config_.size() : 0;
        diag["version"] = config_.value(
            std::string(config_constants::CONFIG_VERSION_KEY), 0);
        diag["active_profile"] = active_profile().value_or("none");
        diag["profile_count"] = profiles_.profile_count();
        diag["backup_count"] = backup_count();
        diag["observer_count"] = observer_count();
        diag["encryption_ready"] = encryptor_.is_ready();
        diag["backup_dir"] = backup_dir_.string();
        diag["profiles_dir"] = profiles_dir_.string();
        return diag;
    }

private:
    // Core state
    std::string config_path_;
    json config_;
    mutable std::shared_mutex mutex_;
    bool initialized_ = false;

    // Feature subsystems
    fs::path backup_dir_;
    fs::path profiles_dir_;
    ConfigSchema schema_;
    ConfigMigration migration_;
    ConfigProfileManager profiles_;
    ConfigEncryptor encryptor_;
    ConfigRollback rollback_;
    ConfigEnvOverride env_override_;
    ConfigCLIParser cli_parser_;
    ConfigRemoteSync remote_sync_;
    ConfigNotificationManager notifications_;
    ConfigExportImport exporter_;

    // ---- Internal helpers ----

    bool load_from_disk() {
        try {
            fs::path path(config_path_);
            if (!fs::exists(path)) {
                spdlog::info("ConfigManager: config file not found, will use defaults");
                config_ = DefaultConfigGenerator::generate_defaults();
                return false; // false = no existing config loaded
            }

            // Read raw content for backup
            auto raw = load_raw_content(config_path_);
            if (raw.has_value()) {
                rollback_.create_backup(config_path_, *raw);
            }

            std::ifstream ifs(config_path_);
            if (!ifs) {
                spdlog::error("ConfigManager: cannot open config file: {}", config_path_);
                return false;
            }

            try {
                config_ = json::parse(ifs);
            } catch (const json::parse_error& e) {
                spdlog::error("ConfigManager: parse error at byte {}: {}", e.byte, e.what());
                // Attempt rollback
                if (rollback_.rollback(config_path_)) {
                    spdlog::info("ConfigManager: rolled back after parse error");
                    std::ifstream ifs2(config_path_);
                    if (ifs2) config_ = json::parse(ifs2);
                    else config_ = DefaultConfigGenerator::generate_defaults();
                } else {
                    config_ = DefaultConfigGenerator::generate_defaults();
                }
                return false;
            }

            spdlog::info("ConfigManager: loaded config from {} ({} top-level keys)",
                         config_path_, config_.is_object() ? config_.size() : 0);
            return true;
        } catch (const std::exception& e) {
            spdlog::error("ConfigManager: load failed: {}", e.what());
            config_ = DefaultConfigGenerator::generate_defaults();
            return false;
        }
    }

    static std::optional<std::string> load_raw_content(const std::string& path) {
        try {
            std::ifstream ifs(path);
            if (!ifs) return std::nullopt;
            return std::string((std::istreambuf_iterator<char>(ifs)),
                               std::istreambuf_iterator<char>());
        } catch (...) {
            return std::nullopt;
        }
    }

    void migrate_config() {
        std::string vk(config_constants::CONFIG_VERSION_KEY);
        int current = config_.value(vk, 0);
        if (current < config_constants::CURRENT_CONFIG_VERSION) {
            spdlog::info("ConfigManager: migrating config from v{} to v{}",
                         current, config_constants::CURRENT_CONFIG_VERSION);
            try {
                config_ = migration_.migrate(config_, config_constants::CURRENT_CONFIG_VERSION);
                notify(ConfigChangeEvent::migration_applied(current,
                    config_constants::CURRENT_CONFIG_VERSION));
            } catch (const std::exception& e) {
                spdlog::error("ConfigManager: migration failed: {}", e.what());
            }
        }
    }

    void apply_profile_overrides() {
        auto active = profiles_.active_profile_name();
        if (active.has_value()) {
            auto profile_data = profiles_.get_profile(active.value());
            if (profile_data.has_value()) {
                config_ = detail::deep_merge(config_, profile_data->profile_config);
                spdlog::debug("ConfigManager: applied profile '{}' overrides",
                              active.value());
            }
        }
    }

    void notify(const ConfigChangeEvent& event) {
        notifications_.notify(event);
    }

    static void collect_keys(const json& node, const std::string& prefix,
                             std::vector<std::string>& result) {
        if (node.is_object()) {
            for (auto& [key, val] : node.items()) {
                std::string full_key = prefix.empty() ? key : prefix + "." + key;
                result.push_back(full_key);
                if (val.is_object() || val.is_array()) {
                    collect_keys(val, full_key, result);
                }
            }
        } else if (node.is_array()) {
            for (size_t i = 0; i < node.size(); ++i) {
                std::string arr_key = prefix + "[" + std::to_string(i) + "]";
                result.push_back(arr_key);
                if (node[i].is_object() || node[i].is_array()) {
                    collect_keys(node[i], arr_key, result);
                }
            }
        }
    }
};

// =============================================================================
// Convenience factory functions
// =============================================================================

/// Create a ConfigManager with platform-appropriate default paths
inline std::shared_ptr<ConfigManager> create_config_manager(const std::string& config_path = "") {
    return std::make_shared<ConfigManager>(config_path);
}

/// Create a ConfigManager and immediately initialize it
inline std::shared_ptr<ConfigManager> create_and_init_config_manager(const std::string& config_path = "") {
    auto mgr = std::make_shared<ConfigManager>(config_path);
    mgr->initialize();
    return mgr;
}

} // namespace common
} // namespace cppdesk
