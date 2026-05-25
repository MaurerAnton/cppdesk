#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>
#include <spdlog/spdlog.h>
#include "common/config.hpp"

namespace cppdesk::updater {

struct UpdateInfo {
    std::string version;
    std::string download_url;
    std::string changelog;
    std::string checksum;
    uint64_t size = 0;
    bool is_available = false;
    bool is_mandatory = false;
};

class UpdateChecker {
    std::string current_version_;
    std::string update_url_;
    std::chrono::hours check_interval_{4};
public:
    UpdateChecker() : current_version_(common::get_version_number()) {
        update_url_ = "https://api.github.com/repos/MaurerAnton/cppdesk/releases/latest";
    }
    UpdateInfo check() {
        UpdateInfo info;
        spdlog::info("Checking for updates (current: {})", current_version_);
        return info;
    }
};

class DownloadManager {
    std::atomic<bool> downloading_{false};
    std::atomic<double> progress_{0.0};
    std::string temp_path_;
public:
    bool download(const UpdateInfo& info) {
        spdlog::info("Downloading {} ({})", info.version, info.download_url);
        downloading_ = true;
        progress_ = 0.0;
        for (int i = 0; i <= 100; i += 10) { progress_ = i / 100.0; }
        downloading_ = false;
        progress_ = 1.0;
        return true;
    }
    bool verify(const UpdateInfo& info) { return true; }
    double progress() const { return progress_; }
    void cancel() { downloading_ = false; }
};

class UpdateInstaller {
public:
    bool install(const std::string& package_path) {
        spdlog::info("Installing update from {}", package_path);
        return true;
    }
    bool rollback() {
        spdlog::info("Rolling back update");
        return true;
    }
};

class AutoUpdater {
    UpdateChecker checker_;
    DownloadManager downloader_;
    UpdateInstaller installer_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::function<void(const UpdateInfo&)> on_update_available_;
public:
    void start() {
        running_ = true;
        worker_ = std::thread([this]() {
            while (running_) {
                auto info = checker_.check();
                if (info.is_available && on_update_available_) on_update_available_(info);
                std::this_thread::sleep_for(std::chrono::hours(4));
            }
        });
    }
    void stop() { running_ = false; if (worker_.joinable()) worker_.join(); }
    void set_on_update(std::function<void(const UpdateInfo&)> cb) { on_update_available_ = std::move(cb); }
};

} // namespace