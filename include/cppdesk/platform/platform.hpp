#pragma once

#include "common/protocol.hpp"
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <memory>

namespace cppdesk::platform {

// Wake lock to prevent sleep
class WakeLock {
public:
    WakeLock() = default;
    explicit WakeLock(bool display, bool idle, bool sleep);
    ~WakeLock();
    WakeLock(WakeLock&&) noexcept;
    WakeLock& operator=(WakeLock&&) noexcept;
    WakeLock(const WakeLock&) = delete;
    WakeLock& operator=(const WakeLock&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Get wake lock
WakeLock get_wakelock(bool display = true);

// Display management
std::vector<std::string> get_display_names();
std::optional<common::Resolution> current_resolution(const std::string& name);
bool change_resolution(const std::string& name, uint32_t width, uint32_t height);
std::vector<common::Resolution> supported_resolutions(const std::string& name);

// Cursor
std::optional<common::CursorData> get_cursor();
std::optional<std::pair<int32_t, int32_t>> get_cursor_pos();
bool set_cursor_pos(int32_t x, int32_t y);
bool clip_cursor(int32_t x, int32_t y, int32_t w, int32_t h);

// System info
std::string get_active_username();
bool is_installed();
bool is_xfce();
bool is_wayland();
bool is_x11();
bool is_process_trusted(bool elevate);

// Service management
void start_os_service();
void stop_os_service();
bool is_service_running();

// Clipboard
std::string get_clipboard_text();
bool set_clipboard_text(const std::string& text);
std::vector<std::string> get_clipboard_files();

// Screenshot
std::optional<common::VideoFrame> capture_screen(uint32_t display_idx = 0);
std::optional<common::VideoFrame> capture_display(const std::string& name);

// Input simulation
void simulate_mouse(const common::MouseEvent& event);
void simulate_key(uint32_t keycode, bool down);
void simulate_text(const std::string& text);

// Keyboard mode support
bool is_keyboard_mode_supported();

// Platform-specific initialization
void init();
void cleanup();

// Privacy mode
bool set_privacy_mode(bool enabled);
bool is_privacy_mode_supported();

// UAC/elevation
bool is_elevated();
bool elevate();

// Virtual display (Windows only)
#ifdef _WIN32
bool install_virtual_display();
bool uninstall_virtual_display();
bool is_virtual_display_installed();
#endif

} // namespace cppdesk::platform
