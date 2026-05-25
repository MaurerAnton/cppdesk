// src/platform/macos/delegate.cpp
#include <spdlog/spdlog.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace cppdesk::platform::macos {

class AppDelegate {
public:
    AppDelegate() = default;
    ~AppDelegate() = default;
    bool applicationDidFinishLaunching() { spdlog::debug("AppDelegate::applicationDidFinishLaunching"); return true; }
    bool applicationWillTerminate() { spdlog::debug("AppDelegate::applicationWillTerminate"); return true; }
    bool applicationDidBecomeActive() { spdlog::debug("AppDelegate::applicationDidBecomeActive"); return true; }
    bool applicationWillResignActive() { spdlog::debug("AppDelegate::applicationWillResignActive"); return true; }
    std::string name() const { return "AppDelegate"; }
private:
    bool initialized_ = false;
};

class EventMonitor {
public:
    EventMonitor() = default;
    ~EventMonitor() = default;
    bool start_keyboard_monitor() { spdlog::debug("EventMonitor::start_keyboard_monitor"); return true; }
    bool start_mouse_monitor() { spdlog::debug("EventMonitor::start_mouse_monitor"); return true; }
    bool stop() { spdlog::debug("EventMonitor::stop"); return true; }
    std::string name() const { return "EventMonitor"; }
private:
    bool initialized_ = false;
};

class AccessibilityHelper {
public:
    AccessibilityHelper() = default;
    ~AccessibilityHelper() = default;
    bool request_permission() { spdlog::debug("AccessibilityHelper::request_permission"); return true; }
    bool is_trusted() { spdlog::debug("AccessibilityHelper::is_trusted"); return true; }
    bool prompt_if_needed() { spdlog::debug("AccessibilityHelper::prompt_if_needed"); return true; }
    std::string name() const { return "AccessibilityHelper"; }
private:
    bool initialized_ = false;
};

} // namespace