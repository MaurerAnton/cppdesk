#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <spdlog/spdlog.h>
#include "platform/platform.hpp"
#include "common/config.hpp"

namespace cppdesk::clipboard {

using namespace common;

class ClipboardMonitor {
public:
    ClipboardMonitor() = default;
    
    void start() {
        running_ = true;
        worker_ = std::thread([this]() {
            std::string last;
            while (running_) {
                auto text = platform::get_clipboard_text();
                if (text != last && !text.empty()) {
                    last = text;
                    if (on_change_) on_change_(text);
                }
                std::this_thread::sleep_for(CLIPBOARD_INTERVAL);
            }
        });
    }
    
    void stop() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
    }
    
    using OnChange = std::function<void(const std::string&)>;
    void set_on_change(OnChange cb) { on_change_ = std::move(cb); }
    
private:
    std::atomic<bool> running_{false};
    std::thread worker_;
    OnChange on_change_;
};

enum class ClipboardSide {
    CLIENT,
    SERVER,
    BOTH,
};

static ClipboardMonitor g_monitor;
static std::string g_last_clipboard_text;
static std::mutex g_clipboard_mutex;

void init_clipboard(ClipboardSide side) {
    spdlog::info("Initializing clipboard (side: {})",
        side == ClipboardSide::CLIENT ? "client" : "server");
    g_monitor.start();
}

void check_clipboard() {
    auto text = platform::get_clipboard_text();
    std::lock_guard lk(g_clipboard_mutex);
    if (text != g_last_clipboard_text) {
        g_last_clipboard_text = text;
        spdlog::debug("Clipboard changed: {} chars", text.size());
    }
}

void set_clipboard_text(const std::string& text) {
    platform::set_clipboard_text(text);
    std::lock_guard lk(g_clipboard_mutex);
    g_last_clipboard_text = text;
}

std::string get_clipboard_text() {
    return platform::get_clipboard_text();
}

std::vector<std::string> get_clipboard_files() {
    return platform::get_clipboard_files();
}

bool check_clipboard_files(std::vector<std::string>& files) {
    files = get_clipboard_files();
    return !files.empty();
}

} // namespace cppdesk::clipboard
