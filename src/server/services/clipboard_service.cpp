#include "server/server.hpp"
#include "platform/platform.hpp"
#include "common/config.hpp"
#include "common/crypto.hpp"
#include <spdlog/spdlog.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <condition_variable>
#include <queue>
#include <unordered_set>

namespace cppdesk::server {

using namespace common;

// ====== Clipboard Content Types ======
enum class ClipboardDataType {
    TEXT,
    IMAGE_PNG,
    IMAGE_BMP,
    FILE_LIST,
    HTML,
    RTF,
    UNKNOWN,
};

struct ClipboardEntry {
    ClipboardDataType type = ClipboardDataType::TEXT;
    std::vector<uint8_t> data;
    std::string text;
    uint64_t timestamp = 0;
    std::string source_hash;
    uint32_t sequence = 0;
};

// ====== Content Hasher (change detection) ======
class ClipboardHasher {
public:
    static std::string hash(const std::string& text) {
        auto h = crypto::sha256(reinterpret_cast<const uint8_t*>(text.data()), text.size());
        return crypto::encode64(h.data(), h.size());
    }

    static std::string hash(const std::vector<uint8_t>& data) {
        auto h = crypto::sha256(data.data(), data.size());
        return crypto::encode64(h.data(), h.size());
    }

    static bool has_changed(const std::string& old_hash, const std::string& new_hash) {
        return old_hash != new_hash;
    }
};

// ====== Clipboard Monitor (polls system clipboard) ======
class SystemClipboardMonitor {
public:
    SystemClipboardMonitor() = default;

    void start() {
        running_ = true;
        worker_ = std::thread([this]() {
            std::string last_text;
            std::string last_hash;

            while (running_) {
                auto text = platform::get_clipboard_text();
                auto current_hash = ClipboardHasher::hash(text);

                if (ClipboardHasher::has_changed(last_hash, current_hash) && !text.empty()) {
                    last_text = text;
                    last_hash = current_hash;

                    ClipboardEntry entry;
                    entry.type = ClipboardDataType::TEXT;
                    entry.text = text;
                    entry.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                    entry.source_hash = current_hash;
                    entry.sequence = sequence_++;

                    {
                        std::lock_guard lk(mutex_);
                        pending_.push(entry);
                    }
                    cv_.notify_one();

                    spdlog::debug("[clipboard] Change detected: {} chars, seq={}",
                        text.size(), entry.sequence);
                }

                // Check for file clipboard
                auto files = platform::get_clipboard_files();
                if (!files.empty()) {
                    ClipboardEntry entry;
                    entry.type = ClipboardDataType::FILE_LIST;
                    entry.sequence = sequence_++;
                    {
                        std::lock_guard lk(mutex_);
                        file_pending_.push(entry);
                    }
                    cv_.notify_one();
                }

                std::unique_lock lk(mutex_);
                cv_.wait_for(lk, CLIPBOARD_INTERVAL);
            }
        });
        spdlog::info("[clipboard] System monitor started");
    }

    void stop() {
        running_ = false;
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    std::optional<ClipboardEntry> get_pending() {
        std::lock_guard lk(mutex_);
        if (pending_.empty()) return std::nullopt;
        auto entry = pending_.front();
        pending_.pop();
        return entry;
    }

    void set_clipboard(const std::string& text) {
        platform::set_clipboard_text(text);
        last_applied_hash_ = ClipboardHasher::hash(text);
        spdlog::debug("[clipboard] Set system clipboard: {} chars", text.size());
    }

    bool was_applied(const std::string& hash) const {
        return last_applied_hash_ == hash;
    }

    size_t pending_count() const {
        std::lock_guard lk(mutex_);
        return pending_.size();
    }

private:
    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<ClipboardEntry> pending_;
    std::queue<ClipboardEntry> file_pending_;
    std::string last_applied_hash_;
    uint32_t sequence_ = 0;
};

// ====== Clipboard Synchronizer ======
class ClipboardSynchronizer {
public:
    enum class Direction {
        LOCAL_TO_REMOTE,
        REMOTE_TO_LOCAL,
        BIDIRECTIONAL,
    };

    ClipboardSynchronizer(Direction dir = Direction::BIDIRECTIONAL)
        : direction_(dir) {}

    void enable() {
        enabled_ = true;
        monitor_.start();
        spdlog::info("[clipboard] Sync enabled (direction: {})",
            static_cast<int>(direction_));
    }

    void disable() {
        enabled_ = false;
        monitor_.stop();
        spdlog::info("[clipboard] Sync disabled");
    }

    bool is_enabled() const { return enabled_; }

    void set_direction(Direction dir) {
        direction_ = dir;
        spdlog::debug("[clipboard] Direction changed to {}", static_cast<int>(dir));
    }

    void process_pending(std::function<void(const ClipboardEntry&)> on_send) {
        while (auto entry = monitor_.get_pending()) {
            if (on_send) {
                on_send(*entry);
            }
            stats_.entries_synced++;
        }
    }

    void apply_remote(const ClipboardEntry& entry) {
        if (!enabled_) return;
        if (direction_ == Direction::LOCAL_TO_REMOTE) return;

        stats_.entries_received++;
        stats_.last_received = std::chrono::steady_clock::now();

        switch (entry.type) {
            case ClipboardDataType::TEXT:
                if (!monitor_.was_applied(entry.source_hash)) {
                    monitor_.set_clipboard(entry.text);
                    stats_.text_applied++;
                }
                break;
            case ClipboardDataType::FILE_LIST:
                // Handle file list clipboard
                stats_.files_applied++;
                break;
            default:
                spdlog::debug("[clipboard] Unknown remote type: {}",
                    static_cast<int>(entry.type));
                break;
        }
    }

    struct SyncStats {
        uint64_t entries_synced = 0;
        uint64_t entries_received = 0;
        uint64_t text_applied = 0;
        uint64_t files_applied = 0;
        std::chrono::steady_clock::time_point last_received;
        std::chrono::steady_clock::time_point last_sent;
    };

    SyncStats get_stats() const { return stats_; }

private:
    Direction direction_ = Direction::BIDIRECTIONAL;
    std::atomic<bool> enabled_{false};
    SystemClipboardMonitor monitor_;
    SyncStats stats_;
};

// ====== File Clipboard Handler ======
class FileClipboardHandler {
public:
    bool is_supported() {
#ifdef _WIN32
        return true; // CF_HDROP
#elif defined(__linux__)
        return true; // x-special/gnome-copied-files
#elif defined(__APPLE__)
        return true; // NSFilenamesPboardType
#else
        return false;
#endif
    }

    std::vector<std::string> get_file_list() {
        return platform::get_clipboard_files();
    }

    bool set_file_list(const std::vector<std::string>& paths) {
        // Platform-specific file clipboard setting
        (void)paths;
        return is_supported();
    }

    struct FileClipInfo {
        std::vector<std::string> paths;
        uint64_t total_size = 0;
        bool is_cut = false; // cut vs copy
    };

    FileClipInfo parse(const std::vector<uint8_t>& data) {
        FileClipInfo info;
        // Parse file list from clipboard data
        std::string text(data.begin(), data.end());
        size_t pos = 0;
        while (pos < text.size()) {
            auto nl = text.find('
', pos);
            if (nl == std::string::npos) {
                if (pos < text.size()) info.paths.push_back(text.substr(pos));
                break;
            }
            info.paths.push_back(text.substr(pos, nl - pos));
            pos = nl + 1;
        }
        return info;
    }
};

// ====== ClipboardService (GenericService subclass) ======
class ClipboardServiceImpl : public GenericService {
public:
    ClipboardServiceImpl(const std::string& name)
        : GenericService(name) {
        if (name == ClipboardService::FILE_NAME) {
            is_file_service_ = true;
        }
    }

    void start() override {
        if (is_file_service_) {
            file_handler_ = std::make_unique<FileClipboardHandler>();
            if (!file_handler_->is_supported()) {
                spdlog::warn("[clipboard] File clipboard not supported on this platform");
                return;
            }
        }
        synchronizer_ = std::make_unique<ClipboardSynchronizer>();
        synchronizer_->enable();
        spdlog::info("[clipboard] {} service started", name_);
    }

    void stop() override {
        if (synchronizer_) synchronizer_->disable();
        spdlog::info("[clipboard] {} service stopped", name_);
    }

    void on_subscribe(int32_t conn_id) override {
        GenericService::on_subscribe(conn_id);
        spdlog::info("[clipboard] Client {} subscribed to {}", conn_id, name_);
    }

    void on_unsubscribe(int32_t conn_id) override {
        GenericService::on_unsubscribe(conn_id);
        spdlog::info("[clipboard] Client {} unsubscribed from {}", conn_id, name_);
    }

    // Poll for changes and return entries to send
    std::vector<ClipboardEntry> poll_changes() {
        std::vector<ClipboardEntry> entries;
        if (!synchronizer_) return entries;

        synchronizer_->process_pending([&entries](const ClipboardEntry& e) {
            entries.push_back(e);
        });
        return entries;
    }

    void apply_remote_entry(const ClipboardEntry& entry) {
        if (synchronizer_) {
            synchronizer_->apply_remote(entry);
        }
    }

    // Check if clipboard changed externally
    bool has_pending() const {
        return synchronizer_ && synchronizer_->is_enabled();
    }

    struct ServiceStats {
        uint64_t text_syncs = 0;
        uint64_t file_syncs = 0;
        uint64_t entries_received = 0;
        size_t subscriber_count = 0;
    };

    ServiceStats get_service_stats() const {
        ServiceStats s;
        s.subscriber_count = subscriber_count();
        if (synchronizer_) {
            auto ss = synchronizer_->get_stats();
            s.text_syncs = ss.text_applied;
            s.entries_received = ss.entries_received;
        }
        return s;
    }

    bool is_file_service() const { return is_file_service_; }

private:
    bool is_file_service_ = false;
    std::unique_ptr<ClipboardSynchronizer> synchronizer_;
    std::unique_ptr<FileClipboardHandler> file_handler_;
};

// ====== Clipboard Compression ======
class ClipboardCompressor {
public:
    static std::vector<uint8_t> compress(const std::string& text, size_t min_size = 256) {
        if (text.size() < min_size) {
            // Don't compress small texts
            std::vector<uint8_t> out(1 + text.size());
            out[0] = 0; // flag: uncompressed
            std::copy(text.begin(), text.end(), out.begin() + 1);
            return out;
        }
        // Simple RLE compression for text
        std::vector<uint8_t> out;
        out.push_back(1); // flag: compressed
        out.reserve(text.size());
        for (size_t i = 0; i < text.size();) {
            char c = text[i];
            size_t run = 1;
            while (i + run < text.size() && text[i + run] == c && run < 255) run++;
            out.push_back(static_cast<uint8_t>(c));
            out.push_back(static_cast<uint8_t>(run));
            i += run;
        }
        return out;
    }

    static std::string decompress(const std::vector<uint8_t>& data) {
        if (data.empty()) return "";
        if (data[0] == 0) {
            return std::string(data.begin() + 1, data.end());
        }
        std::string out;
        for (size_t i = 1; i + 1 < data.size(); i += 2) {
            out.append(data[i + 1], static_cast<char>(data[i]));
        }
        return out;
    }
};

// ====== Clipboard Rate Limiter ======
class ClipboardRateLimiter {
public:
    ClipboardRateLimiter(size_t max_per_second = 5)
        : max_per_second_(max_per_second) {}

    bool allow() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - window_start_).count();

        if (elapsed >= 1.0) {
            count_ = 0;
            window_start_ = now;
        }

        if (count_ >= max_per_second_) {
            spdlog::warn("[clipboard] Rate limit exceeded: {}/s", count_);
            return false;
        }

        count_++;
        return true;
    }

private:
    size_t max_per_second_;
    size_t count_ = 0;
    std::chrono::steady_clock::time_point window_start_ = std::chrono::steady_clock::now();
};

} // namespace cppdesk::server
