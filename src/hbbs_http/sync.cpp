// hbbs_http sync module
#include <spdlog/spdlog.h>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>

namespace cppdesk::hbbs_http::sync {

class SyncManager {
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::map<std::string, std::string> config_cache_;
    std::mutex cache_mutex_;
public:
    void start() {
        running_ = true;
        worker_ = std::thread([this]() {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::seconds(30));
                sync_config();
                sync_peers();
                sync_software_updates();
            }
        });
        spdlog::info("HBBS sync started");
    }
    void stop() { running_ = false; if (worker_.joinable()) worker_.join(); }
    bool is_running() const { return running_; }
    
    void sync_config() { spdlog::debug("Syncing config..."); }
    void sync_peers() { spdlog::debug("Syncing peers..."); }
    void sync_software_updates() { spdlog::debug("Checking updates..."); }
    
    void cache_config(const std::string& key, const std::string& val) {
        std::lock_guard lk(cache_mutex_); config_cache_[key] = val;
    }
    std::string get_cached(const std::string& key) const {
        std::lock_guard lk(cache_mutex_);
        auto it = config_cache_.find(key);
        return it != config_cache_.end() ? it->second : "";
    }
};

static SyncManager g_sync;

void start() { g_sync.start(); }
void stop() { g_sync.stop(); }

} // namespace