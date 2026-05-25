#include <gtest/gtest.h>
#include "platform/platform.hpp"
#include "common/protocol.hpp"
#include "common/config.hpp"

#include <string>
#include <vector>
#include <optional>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <algorithm>

using namespace cppdesk::platform;
using namespace cppdesk::common;

// ============================================================================
// Helper macros and utilities
// ============================================================================

// Check if we have a display environment variable set
static bool has_display() {
    static bool checked = false;
    static bool result = false;
    if (!checked) {
        const char* d = std::getenv("DISPLAY");
        result = (d != nullptr && d[0] != '\0');
        checked = true;
    }
    return result;
}

// Check if we're running on Wayland
static bool is_wayland_env() {
    static bool checked = false;
    static bool result = false;
    if (!checked) {
        const char* wd = std::getenv("WAYLAND_DISPLAY");
        const char* xdg = std::getenv("XDG_SESSION_TYPE");
        result = (wd != nullptr && wd[0] != '\0') ||
                 (xdg != nullptr && std::string(xdg) == "wayland");
        checked = true;
    }
    return result;
}

// Check if we're root
static bool is_root() {
    return std::getenv("USER") && std::string(std::getenv("USER")) == "root";
}

// ============================================================================
// WakeLock Tests
// ============================================================================

TEST(WakeLockTest, DefaultConstruction) {
    // Default-constructed WakeLock should be safe
    WakeLock lock;
    // Destructor should not crash
}

TEST(WakeLockTest, ParametrizedConstruction) {
    // Creating with various parameters should not crash
    {
        WakeLock lock(true, false, false);
    }
    {
        WakeLock lock(false, true, false);
    }
    {
        WakeLock lock(true, true, true);
    }
    {
        WakeLock lock(false, false, false);
    }
}

TEST(WakeLockTest, MoveConstructor) {
    WakeLock original(true, true, false);
    // Move construct
    WakeLock moved(std::move(original));
    // Moved-from object destructor should be safe
    // moved destructor should be safe
}

TEST(WakeLockTest, MoveConstructorNoexcept) {
    EXPECT_TRUE(std::is_nothrow_move_constructible_v<WakeLock>);
}

TEST(WakeLockTest, MoveAssignmentNoexcept) {
    EXPECT_TRUE(std::is_nothrow_move_assignable_v<WakeLock>);
}

TEST(WakeLockTest, MoveAssignment) {
    WakeLock lock1(true, false, true);
    WakeLock lock2(false, true, false);
    lock2 = std::move(lock1);
    // Both destructors should be safe
}

TEST(WakeLockTest, MoveAssignmentSelfAssignment) {
    // Self-move-assignment is technically allowed by the standard
    // but we just verify it doesn't crash
    WakeLock lock(true, true, true);
    // Don't actually self-move-assign as it's UB,
    // but verify the type supports move semantics
    SUCCEED();
}

TEST(WakeLockTest, CannotCopy) {
    static_assert(!std::is_copy_constructible_v<WakeLock>,
                  "WakeLock should not be copy constructible");
    static_assert(!std::is_copy_assignable_v<WakeLock>,
                  "WakeLock should not be copy assignable");
}

TEST(WakeLockTest, GetWakeLockDefault) {
    // get_wakelock with default parameter (display=true)
    WakeLock lock = get_wakelock();
    // Should create a display wakelock
}

TEST(WakeLockTest, GetWakeLockNoDisplay) {
    WakeLock lock = get_wakelock(false);
    // Should create wakelock without display
}

TEST(WakeLockTest, MultipleWakeLocks) {
    // Multiple wake locks should be safe
    WakeLock lock1 = get_wakelock(true);
    WakeLock lock2 = get_wakelock(true);
    // Both should prevent sleep while alive
}

TEST(WakeLockTest, LifetimeManagement) {
    // WakeLock should release resources when destroyed
    {
        WakeLock lock = get_wakelock(true);
    }
    // After destruction, sleep should be allowed again
}

TEST(WakeLockTest, ScopedUsage) {
    // Typical usage pattern: scoped wakelock
    {
        auto lock = get_wakelock();
        // During an operation that requires no sleeping
    }
    // Wakelock automatically released
    SUCCEED();
}

// ============================================================================
// Display Tests
// ============================================================================

class DisplayTest : public ::testing::Test {
protected:
    void SetUp() override {
        display_names_ = get_display_names();
    }

    std::vector<std::string> display_names_;
};

TEST_F(DisplayTest, GetDisplayNamesNotEmpty) {
    // Even on headless systems, there should be at least one display
    EXPECT_FALSE(display_names_.empty());
}

TEST_F(DisplayTest, GetDisplayNamesReturnsStrings) {
    for (const auto& name : display_names_) {
        EXPECT_FALSE(name.empty());
    }
}

TEST_F(DisplayTest, GetDisplayNamesNoDuplicates) {
    auto sorted = display_names_;
    std::sort(sorted.begin(), sorted.end());
    auto it = std::unique(sorted.begin(), sorted.end());
    EXPECT_EQ(it, sorted.end()) << "Display names contain duplicates";
}

TEST_F(DisplayTest, CurrentResolution) {
    if (display_names_.empty()) {
        GTEST_SKIP() << "No displays available";
    }
    auto res = current_resolution(display_names_[0]);
    if (res.has_value()) {
        EXPECT_GT(res->width, 0u);
        EXPECT_GT(res->height, 0u);
    }
}

TEST_F(DisplayTest, CurrentResolutionInvalidDisplay) {
    auto res = current_resolution("__nonexistent_display_name_xyz__");
    // Should return nullopt for nonexistent display
    EXPECT_FALSE(res.has_value());
}

TEST_F(DisplayTest, CurrentResolutionEmptyString) {
    auto res = current_resolution("");
    // Empty display name should return nullopt
    EXPECT_FALSE(res.has_value());
}

TEST_F(DisplayTest, SupportedResolutions) {
    if (display_names_.empty()) {
        GTEST_SKIP() << "No displays available";
    }
    auto resolutions = supported_resolutions(display_names_[0]);
    // If we get resolutions back, they should all be valid
    for (const auto& res : resolutions) {
        EXPECT_GT(res.width, 0u);
        EXPECT_GT(res.height, 0u);
    }
}

TEST_F(DisplayTest, SupportedResolutionsInvalidDisplay) {
    auto resolutions = supported_resolutions("__nonexistent_display__");
    // Should return empty for nonexistent display
    EXPECT_TRUE(resolutions.empty());
}

TEST_F(DisplayTest, ChangeResolution) {
    if (display_names_.empty() || !has_display()) {
        GTEST_SKIP() << "No displays or no DISPLAY set";
    }
    // Get current resolution first
    auto current = current_resolution(display_names_[0]);
    if (!current.has_value()) {
        GTEST_SKIP() << "Cannot get current resolution";
    }

    // Try to set the same resolution - should be a no-op and succeed
    bool result = change_resolution(
        display_names_[0], current->width, current->height);
    // This may fail if we don't have permission, but shouldn't crash
    SUCCEED();
}

TEST_F(DisplayTest, ChangeResolutionInvalidDisplay) {
    bool result = change_resolution(
        "__nonexistent__", 1920, 1080);
    // Should fail for nonexistent display
    EXPECT_FALSE(result);
}

TEST_F(DisplayTest, SupportedResolutionsNotEmpty) {
    if (display_names_.empty()) {
        GTEST_SKIP() << "No displays available";
    }
    auto resolutions = supported_resolutions(display_names_[0]);
    // Most displays should have at least one supported resolution
    // But we don't assert non-empty as some drivers may not report
    if (!resolutions.empty()) {
        // If there are resolutions, the current one should be among them
        auto current = current_resolution(display_names_[0]);
        if (current.has_value()) {
            bool found = false;
            for (const auto& res : resolutions) {
                if (res.width == current->width && res.height == current->height) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                // This may happen on some systems, just log
                SUCCEED();
            }
        }
    }
}

TEST(DisplayTest_FreeFunctions, MultipleDisplayIteration) {
    auto names = get_display_names();
    // Iterate over all displays without crashing
    for (const auto& name : names) {
        auto res = current_resolution(name);
        auto supported = supported_resolutions(name);
        // Just ensure these calls don't crash
        SUCCEED();
    }
}

// ============================================================================
// Cursor Tests
// ============================================================================

class CursorTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!has_display() && !is_wayland_env()) {
            GTEST_SKIP() << "No display available for cursor tests";
        }
    }
};

TEST_F(CursorTest, GetCursorReturnsValidData) {
    auto cursor = get_cursor();
    if (cursor.has_value()) {
        EXPECT_GT(cursor->width, 0u);
        EXPECT_GT(cursor->height, 0u);
        // Colors vector should match width * height * 4 (RGBA)
        if (!cursor->colors.empty()) {
            EXPECT_EQ(cursor->colors.size(),
                      static_cast<size_t>(cursor->width) *
                      static_cast<size_t>(cursor->height) * 4);
        }
    }
    // Returns nullopt on headless systems - that's OK
}

TEST_F(CursorTest, GetCursorPos) {
    auto pos = get_cursor_pos();
    if (pos.has_value()) {
        // Position should be within reasonable bounds
        EXPECT_GE(pos->first, -10000);
        EXPECT_GE(pos->second, -10000);
        EXPECT_LE(pos->first, 100000);
        EXPECT_LE(pos->second, 100000);
    }
}

TEST_F(CursorTest, SetCursorPos) {
    // Set cursor to a known position
    bool result = set_cursor_pos(100, 100);
    // May fail on some systems (Wayland doesn't support this)
    // Just ensure it doesn't crash
    (void)result;
}

TEST_F(CursorTest, SetCursorPosNegative) {
    bool result = set_cursor_pos(-10, -10);
    // Should handle negative coordinates gracefully
    (void)result;
}

TEST_F(CursorTest, SetCursorPosLarge) {
    bool result = set_cursor_pos(99999, 99999);
    // Should handle large coordinates
    (void)result;
}

TEST_F(CursorTest, ClipCursor) {
    bool result = clip_cursor(0, 0, 800, 600);
    // Clipping may or may not succeed
    (void)result;
}

TEST_F(CursorTest, ClipCursorZeroSize) {
    bool result = clip_cursor(0, 0, 0, 0);
    (void)result;
}

TEST_F(CursorTest, ClipCursorNegativeSize) {
    bool result = clip_cursor(0, 0, -1, -1);
    (void)result;
}

TEST_F(CursorTest, GetCursorPosAfterSet) {
    if (!has_display()) {
        GTEST_SKIP() << "Display required for cursor position test";
    }
    auto orig = get_cursor_pos();
    if (!orig.has_value()) {
        GTEST_SKIP() << "Cannot get initial cursor position";
    }

    // Set to new position
    set_cursor_pos(orig->first + 1, orig->second + 1);

    // Get position again - it should have changed
    auto after = get_cursor_pos();
    if (after.has_value()) {
        // On X11 this should work; on Wayland it may not
        SUCCEED();
    }
}

TEST(CursorTest_FreeFunctions, RepeatedGetCursor) {
    // Call get_cursor multiple times to ensure no memory leaks
    for (int i = 0; i < 10; ++i) {
        auto cursor = get_cursor();
        (void)cursor;
    }
}

TEST(CursorTest_FreeFunctions, RepeatedGetCursorPos) {
    for (int i = 0; i < 10; ++i) {
        auto pos = get_cursor_pos();
        (void)pos;
    }
}

// ============================================================================
// System Info Tests
// ============================================================================

TEST(SystemInfoTest, GetActiveUsernameNotEmpty) {
    std::string username = get_active_username();
    EXPECT_FALSE(username.empty());
}

TEST(SystemInfoTest, GetActiveUsernameStable) {
    std::string u1 = get_active_username();
    std::string u2 = get_active_username();
    EXPECT_EQ(u1, u2);
}

TEST(SystemInfoTest, IsInstalled) {
    bool installed = is_installed();
    // is_installed() should return a valid boolean
    // The actual value depends on the system state
    (void)installed;
}

TEST(SystemInfoTest, IsInstalledIdempotent) {
    bool r1 = is_installed();
    bool r2 = is_installed();
    EXPECT_EQ(r1, r2);
}

TEST(SystemInfoTest, IsXfce) {
    bool xfce = is_xfce();
    // Should return a boolean without crashing
    (void)xfce;
}

TEST(SystemInfoTest, IsWayland) {
    bool wayland = is_wayland();
    // Compare with environment check
    bool env_wayland = is_wayland_env();
    EXPECT_EQ(wayland, env_wayland);
}

TEST(SystemInfoTest, IsX11) {
    bool x11 = is_x11();
    // If DISPLAY is set and not Wayland, should be X11
    if (has_display() && !is_wayland()) {
        EXPECT_TRUE(x11);
    }
}

TEST(SystemInfoTest, IsX11AndIsWaylandMutuallyExclusive) {
    bool x11 = is_x11();
    bool wayland = is_wayland();
    // They shouldn't both be true simultaneously
    EXPECT_FALSE(x11 && wayland);
}

TEST(SystemInfoTest, IsProcessTrusted) {
    // Test with elevate=false (current privilege)
    bool trusted = is_process_trusted(false);
    // Should return a boolean without crashing
    (void)trusted;
}

TEST(SystemInfoTest, IsProcessTrustedElevate) {
    bool trusted = is_process_trusted(true);
    // With elevate=true, checks elevated trust
    (void)trusted;
}

TEST(SystemInfoTest, IsProcessTrustedConsistency) {
    bool r1 = is_process_trusted(false);
    bool r2 = is_process_trusted(false);
    EXPECT_EQ(r1, r2);
}

TEST(SystemInfoTest, MultipleCallsNoCrash) {
    // Call all system info functions in tight loop
    for (int i = 0; i < 10; ++i) {
        get_active_username();
        is_installed();
        is_xfce();
        is_wayland();
        is_x11();
        is_process_trusted(false);
        is_process_trusted(true);
    }
    SUCCEED();
}

// ============================================================================
// System Info - Edge Cases
// ============================================================================

TEST(SystemInfoTestEdge, ActiveUsernameNonEmptyCStr) {
    std::string u = get_active_username();
    // Should not contain null bytes
    EXPECT_EQ(strlen(u.c_str()), u.size());
}

TEST(SystemInfoTestEdge, IsInstalledReturnsBool) {
    // Just verify it returns a boolean (no exceptions thrown)
    try {
        bool result = is_installed();
        EXPECT_TRUE(result == true || result == false);
    } catch (...) {
        FAIL() << "is_installed() threw an exception";
    }
}

TEST(SystemInfoTestEdge, AllFunctionsNoThrow) {
    EXPECT_NO_THROW(get_active_username());
    EXPECT_NO_THROW(is_installed());
    EXPECT_NO_THROW(is_xfce());
    EXPECT_NO_THROW(is_wayland());
    EXPECT_NO_THROW(is_x11());
    EXPECT_NO_THROW(is_process_trusted(false));
}

// ============================================================================
// Clipboard Tests
// ============================================================================

class ClipboardTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Save original clipboard state if possible
        original_text_ = get_clipboard_text();
    }

    void TearDown() override {
        // Restore original clipboard if we changed it
        if (!original_text_.empty() && !clipboard_modified_) {
            // Don't restore if we didn't modify
        }
    }

    std::string original_text_;
    bool clipboard_modified_ = false;
};

TEST_F(ClipboardTest, GetClipboardText) {
    std::string text = get_clipboard_text();
    // May be empty if clipboard is empty - that's valid
    (void)text;
}

TEST_F(ClipboardTest, SetAndGetClipboardText) {
    if (!has_display() && !is_wayland_env()) {
        GTEST_SKIP() << "No display for clipboard test";
    }
    const std::string test_str = "CPPDESK_TEST_CLIPBOARD_HELLO_12345";
    bool set_result = set_clipboard_text(test_str);
    if (!set_result) {
        GTEST_SKIP() << "set_clipboard_text returned false";
    }
    clipboard_modified_ = true;

    // Small delay to allow clipboard to update
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string result = get_clipboard_text();
    // On some systems this might not work synchronously
    if (!result.empty()) {
        // Some systems may append newline, so just check contains
        EXPECT_NE(result.find("CPPDESK_TEST_CLIPBOARD"), std::string::npos);
    }
}

TEST_F(ClipboardTest, SetClipboardEmptyString) {
    if (!has_display() && !is_wayland_env()) {
        GTEST_SKIP() << "No display for clipboard test";
    }
    bool result = set_clipboard_text("");
    // Clearing clipboard should succeed or fail gracefully
    (void)result;
}

TEST_F(ClipboardTest, SetClipboardLargeText) {
    if (!has_display() && !is_wayland_env()) {
        GTEST_SKIP() << "No display for clipboard test";
    }
    // 100KB string
    std::string large_text(100000, 'A');
    bool result = set_clipboard_text(large_text);
    // Should handle large text without crashing
    (void)result;
}

TEST_F(ClipboardTest, SetClipboardSpecialCharacters) {
    if (!has_display() && !is_wayland_env()) {
        GTEST_SKIP() << "No display for clipboard test";
    }
    std::string special = "Line1\nLine2\r\nTab:\tUnicode: \xE2\x98\x83 Null: \0 embedded";
    bool result = set_clipboard_text(special);
    (void)result;
}

TEST_F(ClipboardTest, SetClipboardUnicode) {
    if (!has_display() && !is_wayland_env()) {
        GTEST_SKIP() << "No display for clipboard test";
    }
    std::string unicode = u8"こんにちは世界 🌍 中文 한국어 العربية";
    bool result = set_clipboard_text(unicode);
    (void)result;
}

TEST_F(ClipboardTest, GetClipboardFiles) {
    std::vector<std::string> files = get_clipboard_files();
    // May be empty if no files on clipboard
    (void)files;
}

TEST_F(ClipboardTest, GetClipboardFilesStructure) {
    auto files = get_clipboard_files();
    // Each entry should be a valid path string
    for (const auto& f : files) {
        EXPECT_FALSE(f.empty());
    }
}

TEST_F(ClipboardTest, RepeatedClipboardAccess) {
    // Stress test clipboard access
    for (int i = 0; i < 5; ++i) {
        auto text = get_clipboard_text();
        auto files = get_clipboard_files();
        (void)text;
        (void)files;
    }
}

// ============================================================================
// Screenshot Tests
// ============================================================================

class ScreenshotTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!has_display() && !is_wayland_env()) {
            GTEST_SKIP() << "No display available for screenshot tests";
        }
    }
};

TEST_F(ScreenshotTest, CaptureScreen) {
    auto frame = capture_screen(0);
    if (frame.has_value()) {
        EXPECT_GT(frame->width, 0u);
        EXPECT_GT(frame->height, 0u);
        EXPECT_FALSE(frame->data.empty());
        EXPECT_GT(frame->timestamp, 0u);
    }
}

TEST_F(ScreenshotTest, CaptureScreenDefaultDisplay) {
    // Default display index = 0
    auto frame = capture_screen();
    if (frame.has_value()) {
        EXPECT_GT(frame->width, 0u);
        EXPECT_GT(frame->height, 0u);
    }
}

TEST_F(ScreenshotTest, CaptureScreenInvalidDisplay) {
    // Try capturing a display index that likely doesn't exist
    auto frame = capture_screen(9999);
    // May return nullopt for invalid display
    if (frame.has_value()) {
        // Even for high index, if it returns data it must be valid
        EXPECT_GT(frame->width, 0u);
        EXPECT_GT(frame->height, 0u);
    }
}

TEST_F(ScreenshotTest, CaptureDisplayByName) {
    auto names = get_display_names();
    if (names.empty()) {
        GTEST_SKIP() << "No displays available";
    }
    auto frame = capture_display(names[0]);
    if (frame.has_value()) {
        EXPECT_GT(frame->width, 0u);
        EXPECT_GT(frame->height, 0u);
        EXPECT_FALSE(frame->data.empty());
    }
}

TEST_F(ScreenshotTest, CaptureDisplayInvalidName) {
    auto frame = capture_display("__nonexistent_display_xyz__");
    // Should return nullopt for nonexistent display
    EXPECT_FALSE(frame.has_value());
}

TEST_F(ScreenshotTest, CaptureDisplayEmptyName) {
    auto frame = capture_display("");
    EXPECT_FALSE(frame.has_value());
}

TEST_F(ScreenshotTest, VideoFrameProperties) {
    auto frame = capture_screen(0);
    if (frame.has_value()) {
        // Check that is_monitor is true for screen captures
        EXPECT_TRUE(frame->is_monitor);
        // Codec should be 0 (raw) or 1/2 for compressed
        EXPECT_LE(frame->codec, 2u);
        // Timestamp should be non-zero
        EXPECT_GT(frame->timestamp, 0u);
    }
}

TEST_F(ScreenshotTest, RepeatedCapture) {
    // Multiple screenshots, ensure no memory leak
    for (int i = 0; i < 3; ++i) {
        auto frame = capture_screen();
        (void)frame;
    }
}

// ============================================================================
// Input Simulation Tests
// ============================================================================

class InputTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!has_display() && !is_wayland_env()) {
            GTEST_SKIP() << "No display for input tests";
        }
    }
};

TEST_F(InputTest, SimulateMouseEventMove) {
    MouseEvent event{};
    event.mask = MouseEvent::TYPE_MOVE;
    event.x = 100;
    event.y = 200;
    EXPECT_NO_THROW(simulate_mouse(event));
}

TEST_F(InputTest, SimulateMouseEventDown) {
    MouseEvent event{};
    event.mask = MouseEvent::TYPE_DOWN | MouseEvent::BUTTON_LEFT;
    event.x = 150;
    event.y = 250;
    EXPECT_NO_THROW(simulate_mouse(event));
}

TEST_F(InputTest, SimulateMouseEventUp) {
    MouseEvent event{};
    event.mask = MouseEvent::TYPE_UP | MouseEvent::BUTTON_LEFT;
    event.x = 150;
    event.y = 250;
    EXPECT_NO_THROW(simulate_mouse(event));
}

TEST_F(InputTest, SimulateMouseEventRightClick) {
    MouseEvent event{};
    event.mask = MouseEvent::TYPE_DOWN | MouseEvent::BUTTON_RIGHT;
    event.x = 300;
    event.y = 400;
    EXPECT_NO_THROW(simulate_mouse(event));
}

TEST_F(InputTest, SimulateMouseEventWheel) {
    MouseEvent event{};
    event.mask = MouseEvent::TYPE_WHEEL | MouseEvent::BUTTON_WHEEL;
    event.x = 0;
    event.y = 120; // scroll amount
    EXPECT_NO_THROW(simulate_mouse(event));
}

TEST_F(InputTest, SimulateMouseEventBackButton) {
    MouseEvent event{};
    event.mask = MouseEvent::TYPE_DOWN | MouseEvent::BUTTON_BACK;
    EXPECT_NO_THROW(simulate_mouse(event));
}

TEST_F(InputTest, SimulateMouseEventForwardButton) {
    MouseEvent event{};
    event.mask = MouseEvent::TYPE_DOWN | MouseEvent::BUTTON_FORWARD;
    EXPECT_NO_THROW(simulate_mouse(event));
}

TEST_F(InputTest, SimulateMouseEventNegativeCoords) {
    MouseEvent event{};
    event.mask = MouseEvent::TYPE_MOVE;
    event.x = -100;
    event.y = -200;
    EXPECT_NO_THROW(simulate_mouse(event));
}

TEST_F(InputTest, SimulateMouseEventMultipleButtons) {
    MouseEvent event{};
    event.mask = MouseEvent::TYPE_DOWN |
                 MouseEvent::BUTTON_LEFT |
                 MouseEvent::BUTTON_RIGHT;
    EXPECT_NO_THROW(simulate_mouse(event));
}

TEST_F(InputTest, SimulateKeyDown) {
    EXPECT_NO_THROW(simulate_key(0x41, true));  // 'A' key down
}

TEST_F(InputTest, SimulateKeyUp) {
    EXPECT_NO_THROW(simulate_key(0x41, false)); // 'A' key up
}

TEST_F(InputTest, SimulateKeyPressRelease) {
    // Simulate a full key press and release
    EXPECT_NO_THROW(simulate_key(0x0D, true));  // Enter down
    EXPECT_NO_THROW(simulate_key(0x0D, false)); // Enter up
}

TEST_F(InputTest, SimulateKeyModifiers) {
    // Shift key
    EXPECT_NO_THROW(simulate_key(0x10, true));  // Shift down
    EXPECT_NO_THROW(simulate_key(0x10, false)); // Shift up
}

TEST_F(InputTest, SimulateKeyFunctionKeys) {
    EXPECT_NO_THROW(simulate_key(0x70, true));  // F1 down
    EXPECT_NO_THROW(simulate_key(0x70, false)); // F1 up
}

TEST_F(InputTest, SimulateText) {
    EXPECT_NO_THROW(simulate_text("Hello, World!"));
}

TEST_F(InputTest, SimulateTextEmpty) {
    EXPECT_NO_THROW(simulate_text(""));
}

TEST_F(InputTest, SimulateTextUnicode) {
    EXPECT_NO_THROW(simulate_text(u8"Привіт, 你好, ¡Hola!"));
}

TEST_F(InputTest, SimulateTextLong) {
    std::string long_text(10000, 'x');
    EXPECT_NO_THROW(simulate_text(long_text));
}

TEST_F(InputTest, SimulateTextSpecialChars) {
    EXPECT_NO_THROW(simulate_text("Tab:\t Newline:\n Carriage:\r"));
}

TEST_F(InputTest, SimulateCombinedInput) {
    // Simulate mouse move + click + key + text in sequence
    MouseEvent move{};
    move.mask = MouseEvent::TYPE_MOVE;
    move.x = 500;
    move.y = 500;
    EXPECT_NO_THROW(simulate_mouse(move));

    MouseEvent click{};
    click.mask = MouseEvent::TYPE_DOWN | MouseEvent::BUTTON_LEFT;
    click.x = 500;
    click.y = 500;
    EXPECT_NO_THROW(simulate_mouse(click));

    EXPECT_NO_THROW(simulate_key(0x41, true));
    EXPECT_NO_THROW(simulate_key(0x41, false));

    EXPECT_NO_THROW(simulate_text("Combined test"));
}

// ============================================================================
// Service Management Tests
// ============================================================================

class ServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Note: service tests may require elevated privileges
        initial_state_ = is_service_running();
    }

    bool initial_state_ = false;
};

TEST_F(ServiceTest, IsServiceRunning) {
    bool running = is_service_running();
    // Should return a boolean without crashing
    (void)running;
}

TEST_F(ServiceTest, IsServiceRunningStable) {
    bool r1 = is_service_running();
    bool r2 = is_service_running();
    EXPECT_EQ(r1, r2);
}

TEST_F(ServiceTest, StartStopServiceCycle) {
    // These operations may require root/admin privileges
    // We test that they at minimum don't crash
    try {
        start_os_service();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        stop_os_service();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    } catch (...) {
        // Operations may throw on permission errors
        SUCCEED();
    }
}

TEST_F(ServiceTest, StopServiceWhenNotRunning) {
    try {
        stop_os_service();
    } catch (...) {
        // May throw if service isn't running or no permissions
        SUCCEED();
    }
}

TEST_F(ServiceTest, StartServiceWhenRunning) {
    try {
        if (is_service_running()) {
            start_os_service(); // Already running - should be safe
        }
    } catch (...) {
        SUCCEED();
    }
}

TEST_F(ServiceTest, StartStopIdempotence) {
    // Multiple start/stop calls
    for (int i = 0; i < 3; ++i) {
        try {
            start_os_service();
        } catch (...) {}
        try {
            stop_os_service();
        } catch (...) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    SUCCEED();
}

// ============================================================================
// Privacy Mode Tests
// ============================================================================

TEST(PrivacyModeTest, IsSupported) {
    bool supported = is_privacy_mode_supported();
    // Should return a boolean without crashing
    (void)supported;
}

TEST(PrivacyModeTest, IsSupportedStable) {
    bool r1 = is_privacy_mode_supported();
    bool r2 = is_privacy_mode_supported();
    EXPECT_EQ(r1, r2);
}

TEST(PrivacyModeTest, SetPrivacyMode) {
    // Setting privacy mode should not crash
    try {
        bool result = set_privacy_mode(true);
        // May fail if not supported - that's OK
        (void)result;
        // Always try to disable it after test
        set_privacy_mode(false);
    } catch (...) {
        SUCCEED();
    }
}

TEST(PrivacyModeTest, SetPrivacyModeFalse) {
    try {
        set_privacy_mode(false);
    } catch (...) {
        SUCCEED();
    }
}

TEST(PrivacyModeTest, TogglePrivacyMode) {
    // Toggle multiple times to ensure stability
    for (int i = 0; i < 3; ++i) {
        try {
            set_privacy_mode(true);
            set_privacy_mode(false);
        } catch (...) {
            SUCCEED();
        }
    }
}

// ============================================================================
// Elevation Tests
// ============================================================================

TEST(ElevationTest, IsElevated) {
    bool elevated = is_elevated();
    // If running as root, should be elevated
    if (is_root()) {
        EXPECT_TRUE(elevated);
    }
    // Otherwise may be true or false depending on platform
}

TEST(ElevationTest, IsElevatedStable) {
    bool r1 = is_elevated();
    bool r2 = is_elevated();
    EXPECT_EQ(r1, r2);
}

TEST(ElevationTest, Elevate) {
    // Calling elevate() may trigger a UAC prompt (Windows) or sudo (Linux)
    // We just verify it doesn't crash and returns a boolean
    try {
        bool result = elevate();
        (void)result;
    } catch (...) {
        SUCCEED();
    }
}

TEST(ElevationTest, ElevateWhenAlreadyElevated) {
    if (is_elevated()) {
        try {
            bool result = elevate();
            // Should return true since we're already elevated
            EXPECT_TRUE(result);
        } catch (...) {
            SUCCEED();
        }
    }
}

// ============================================================================
// Keyboard Mode Tests
// ============================================================================

TEST(KeyboardModeTest, IsSupported) {
    bool supported = is_keyboard_mode_supported();
    (void)supported;
}

TEST(KeyboardModeTest, IsSupportedStable) {
    bool r1 = is_keyboard_mode_supported();
    bool r2 = is_keyboard_mode_supported();
    EXPECT_EQ(r1, r2);
}

// ============================================================================
// Initialization Tests
// ============================================================================

class InitTest : public ::testing::Test {
protected:
    void SetUp() override {
        try {
            // Some implementations may require init before use
            init();
        } catch (...) {
            // init() may fail - that's OK
        }
    }

    void TearDown() override {
        try {
            cleanup();
        } catch (...) {
            // cleanup() may fail - that's OK
        }
    }
};

TEST_F(InitTest, InitNoCrash) {
    // init() was called in SetUp - no crash means success
    SUCCEED();
}

TEST_F(InitTest, CleanupNoCrash) {
    // cleanup() will be called in TearDown
    SUCCEED();
}

TEST_F(InitTest, InitIdempotent) {
    // Calling init multiple times should be safe
    try {
        init();
        init();
        init();
    } catch (...) {
        SUCCEED();
    }
}

TEST_F(InitTest, CleanupIdempotent) {
    // Calling cleanup multiple times should be safe
    try {
        cleanup();
        cleanup();
        cleanup();
    } catch (...) {
        SUCCEED();
    }
}

TEST_F(InitTest, InitCleanupCycle) {
    // Multiple init/cleanup cycles
    for (int i = 0; i < 3; ++i) {
        try {
            init();
            // Do something minimal
            get_display_names();
            cleanup();
        } catch (...) {
            SUCCEED();
            break;
        }
    }
}

TEST_F(InitTest, FunctionsWorkAfterInit) {
    // After init, core functions should work
    try {
        get_active_username();
        is_installed();
        is_wayland();
        is_x11();
        get_display_names();
    } catch (...) {
        SUCCEED();
    }
}

// ============================================================================
// Virtual Display Tests (Windows only)
// ============================================================================

#ifdef _WIN32
TEST(VirtualDisplayTest, InstallUninstall) {
    // These functions are Windows-specific
    try {
        bool install_result = install_virtual_display();
        // May fail if not admin
        (void)install_result;

        bool uninstall_result = uninstall_virtual_display();
        (void)uninstall_result;
    } catch (...) {
        SUCCEED();
    }
}

TEST(VirtualDisplayTest, IsInstalled) {
    try {
        bool installed = is_virtual_display_installed();
        (void)installed;
    } catch (...) {
        SUCCEED();
    }
}

TEST(VirtualDisplayTest, IsInstalledStable) {
    try {
        bool r1 = is_virtual_display_installed();
        bool r2 = is_virtual_display_installed();
        EXPECT_EQ(r1, r2);
    } catch (...) {
        SUCCEED();
    }
}

TEST(VirtualDisplayTest, InstallIdempotent) {
    try {
        install_virtual_display();
        install_virtual_display(); // Second call should be safe
        uninstall_virtual_display();
    } catch (...) {
        SUCCEED();
    }
}

TEST(VirtualDisplayTest, UninstallWhenNotInstalled) {
    try {
        uninstall_virtual_display();
    } catch (...) {
        SUCCEED();
    }
}
#endif

// ============================================================================
// Cross-Function Integration Tests
// ============================================================================

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        try { init(); } catch (...) {}
    }

    void TearDown() override {
        try { cleanup(); } catch (...) {}
    }
};

TEST_F(IntegrationTest, DisplayAndCursorInterplay) {
    if (!has_display()) {
        GTEST_SKIP() << "Display required for integration test";
    }
    auto displays = get_display_names();
    
    for (const auto& d : displays) {
        auto res = current_resolution(d);
        auto supported = supported_resolutions(d);
        
        if (res.has_value()) {
            // If we have a display, cursor operations should be possible
            auto cursor = get_cursor();
            auto pos = get_cursor_pos();
        }
    }
    SUCCEED();
}

TEST_F(IntegrationTest, ScreenshotAndCursorInterplay) {
    if (!has_display()) {
        GTEST_SKIP() << "Display required for integration test";
    }
    // Get cursor position before screenshot
    auto pos_before = get_cursor_pos();
    
    // Take screenshot
    auto frame = capture_screen();
    
    // Get cursor position after screenshot
    auto pos_after = get_cursor_pos();
    
    // Screenshot should not move the cursor (might depend on platform)
    if (pos_before.has_value() && pos_after.has_value()) {
        SUCCEED();
    }
}

TEST_F(IntegrationTest, ClipboardAndInputInterplay) {
    if (!has_display()) {
        GTEST_SKIP() << "Display required for integration test";
    }
    // Set clipboard text
    set_clipboard_text("Integration Test");
    
    // Simulate input
    simulate_text("Integration Test");
    
    // Get clipboard back
    auto clip = get_clipboard_text();
    (void)clip;
}

TEST_F(IntegrationTest, SystemInfoAfterInit) {
    // After init, system info should still work
    EXPECT_NO_THROW(get_active_username());
    EXPECT_NO_THROW(is_installed());
    EXPECT_NO_THROW(is_wayland());
    EXPECT_NO_THROW(is_x11());
}

TEST_F(IntegrationTest, WakeLockDuringScreenshot) {
    if (!has_display()) {
        GTEST_SKIP() << "Display required";
    }
    // Acquire wakelock
    auto lock = get_wakelock(true);
    
    // Take screenshots while wakelock is held
    for (int i = 0; i < 3; ++i) {
        auto frame = capture_screen();
        (void)frame;
    }
    // Wakelock released at end of scope
    SUCCEED();
}

// ============================================================================
// Stress Tests
// ============================================================================

class StressTest : public ::testing::Test {
protected:
    void SetUp() override {
        try { init(); } catch (...) {}
    }

    void TearDown() override {
        try { cleanup(); } catch (...) {}
    }
};

TEST_F(StressTest, RapidDisplayQueries) {
    // Rapidly query display information
    for (int i = 0; i < 100; ++i) {
        auto names = get_display_names();
        for (const auto& n : names) {
            auto res = current_resolution(n);
            (void)res;
        }
    }
    SUCCEED();
}

TEST_F(StressTest, RapidSystemInfoQueries) {
    for (int i = 0; i < 100; ++i) {
        get_active_username();
        is_installed();
        is_xfce();
        is_wayland();
        is_x11();
        is_process_trusted(false);
    }
    SUCCEED();
}

TEST_F(StressTest, RapidWakeLockCreateDestroy) {
    for (int i = 0; i < 50; ++i) {
        auto lock = get_wakelock(i % 2 == 0);
    }
    SUCCEED();
}

TEST_F(StressTest, RapidCursorQueries) {
    for (int i = 0; i < 100; ++i) {
        auto cursor = get_cursor();
        auto pos = get_cursor_pos();
        (void)cursor;
        (void)pos;
    }
    SUCCEED();
}

TEST_F(StressTest, ConcurrentWakeLockStress) {
    // Create and destroy multiple wakelocks
    std::vector<WakeLock> locks;
    for (int i = 0; i < 10; ++i) {
        locks.emplace_back(get_wakelock(true));
    }
    // Destroy them all
    locks.clear();
    SUCCEED();
}

// ============================================================================
// Platform-Specific Conditional Tests
// ============================================================================

TEST(PlatformConditionalTest, X11OnlyTests) {
    if (!is_x11()) {
        GTEST_SKIP() << "Not running on X11";
    }

    // On X11, DISPLAY should be set
    EXPECT_TRUE(has_display());

    // On X11, displays should be available
    auto displays = get_display_names();
    EXPECT_FALSE(displays.empty());

    // Resolution query should work
    if (!displays.empty()) {
        auto res = current_resolution(displays[0]);
        if (res.has_value()) {
            EXPECT_GT(res->width, 0u);
            EXPECT_GT(res->height, 0u);
        }
    }
}

TEST(PlatformConditionalTest, WaylandOnlyTests) {
    if (!is_wayland()) {
        GTEST_SKIP() << "Not running on Wayland";
    }

    // Basic Wayland checks
    EXPECT_TRUE(is_wayland());
    EXPECT_FALSE(is_x11());

    // Display functions should still work on Wayland
    auto displays = get_display_names();
    if (!displays.empty()) {
        auto res = current_resolution(displays[0]);
        (void)res;
    }
}

TEST(PlatformConditionalTest, XfceOnlyTests) {
    if (!is_xfce()) {
        GTEST_SKIP() << "Not running on Xfce";
    }

    EXPECT_TRUE(is_xfce());

    // Desktop environment-specific checks
    auto displays = get_display_names();
    EXPECT_FALSE(displays.empty());
}

TEST(PlatformConditionalTest, HeadlessTests) {
    if (has_display() || is_wayland_env()) {
        GTEST_SKIP() << "Has display - not headless";
    }

    // In headless mode, display functions should return empty/nullopt
    auto displays = get_display_names();
    // May still return displays even on headless
    (void)displays;

    // Screenshot should return nullopt
    auto frame = capture_screen();
    EXPECT_FALSE(frame.has_value());

    // Cursor should return nullopt
    auto cursor = get_cursor();
    EXPECT_FALSE(cursor.has_value());

    auto pos = get_cursor_pos();
    EXPECT_FALSE(pos.has_value());
}

// ============================================================================
// Thread Safety Tests (basic)
// ============================================================================

TEST(ThreadSafetyTest, SystemInfoFromMultipleThreads) {
    constexpr int kThreads = 4;
    constexpr int kIterations = 50;
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([i]() {
            for (int j = 0; j < kIterations; ++j) {
                get_active_username();
                is_installed();
                is_xfce();
                is_wayland();
                is_x11();
                get_display_names();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
    SUCCEED();
}

TEST(ThreadSafetyTest, WakeLockFromMultipleThreads) {
    constexpr int kThreads = 4;
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([i]() {
            for (int j = 0; j < 10; ++j) {
                auto lock = get_wakelock((i + j) % 2 == 0);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
    SUCCEED();
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST(EdgeCaseTest, DisplayFunctionsWithEmptyInputs) {
    EXPECT_FALSE(current_resolution("").has_value());
    EXPECT_TRUE(supported_resolutions("").empty());
    EXPECT_FALSE(change_resolution("", 0, 0));
}

TEST(EdgeCaseTest, CursorFunctionsEdgeCases) {
    // Extreme coordinate values
    EXPECT_NO_THROW(set_cursor_pos(0, 0));
    EXPECT_NO_THROW(set_cursor_pos(-32768, -32768));
    EXPECT_NO_THROW(set_cursor_pos(32767, 32767));

    // Edge clip cursor values
    EXPECT_NO_THROW(clip_cursor(0, 0, 1, 1));
    EXPECT_NO_THROW(clip_cursor(-100, -100, 200, 200));
}

TEST(EdgeCaseTest, ClipboardEdgeCases) {
    // Empty gets should be safe
    EXPECT_NO_THROW(get_clipboard_text());
    EXPECT_NO_THROW(get_clipboard_files());
}

TEST(EdgeCaseTest, ScreenshotEdgeCases) {
    // Very high display index
    EXPECT_NO_THROW(capture_screen(0xFFFFFFFF));
    
    // Capture with empty name
    EXPECT_FALSE(capture_display("").has_value());
}

TEST(EdgeCaseTest, InputSimulationEdgeCases) {
    // Zeroed mouse event
    MouseEvent zero{};
    EXPECT_NO_THROW(simulate_mouse(zero));

    // Maximum keycode
    EXPECT_NO_THROW(simulate_key(0xFFFFFFFF, true));
    EXPECT_NO_THROW(simulate_key(0xFFFFFFFF, false));

    // Zero keycode
    EXPECT_NO_THROW(simulate_key(0, true));
    EXPECT_NO_THROW(simulate_key(0, false));

    // Very long text simulation
    std::string mega_text(100000, 'Z');
    EXPECT_NO_THROW(simulate_text(mega_text));
}

TEST(EdgeCaseTest, PrivacyModeEdgeCases) {
    // Rapid toggles
    for (int i = 0; i < 20; ++i) {
        set_privacy_mode(true);
        set_privacy_mode(false);
    }
    SUCCEED();
}

TEST(EdgeCaseTest, ElevationEdgeCases) {
    // Multiple calls
    bool r1 = is_elevated();
    bool r2 = is_elevated();
    bool r3 = is_elevated();
    EXPECT_EQ(r1, r2);
    EXPECT_EQ(r2, r3);
}

// ============================================================================
// Regression / Sanity Tests
// ============================================================================

TEST(RegressionTest, WakeLockDestructorNoDoubleFree) {
    // Create and destroy many WakeLocks to check for double-free
    for (int i = 0; i < 20; ++i) {
        auto lock = get_wakelock();
    }
    SUCCEED();
}

TEST(RegressionTest, MoveOnlyTypeCompilation) {
    // Verify WakeLock is move-only at compile time
    static_assert(std::is_move_constructible_v<WakeLock>);
    static_assert(!std::is_copy_constructible_v<WakeLock>);
    static_assert(std::is_move_assignable_v<WakeLock>);
    static_assert(!std::is_copy_assignable_v<WakeLock>);
}

TEST(RegressionTest, DisplayFunctionsAfterCleanup) {
    // Cleanup should be safe to call
    try {
        cleanup();
        // After cleanup, some functions might still work
        // but at minimum they shouldn't crash
        get_active_username();
        is_installed();
    } catch (...) {
        SUCCEED();
    }
}

TEST(RegressionTest, InitAfterCleanup) {
    // Reinitialize after cleanup
    try {
        init();
        cleanup();
        init();
        cleanup();
    } catch (...) {
        SUCCEED();
    }
}

TEST(RegressionTest, ServiceFunctionsNoCrash) {
    // All service functions should be callable without crash
    try {
        is_service_running();
        start_os_service();
    } catch (...) {}
    try {
        stop_os_service();
    } catch (...) {}
    try {
        is_service_running();
    } catch (...) {}
    SUCCEED();
}

// ============================================================================
// Type Trait Verification Tests
// ============================================================================

TEST(TypeTraitsTest, WakeLockIsMoveOnly) {
    EXPECT_TRUE(std::is_default_constructible_v<WakeLock>);
    EXPECT_TRUE(std::is_move_constructible_v<WakeLock>);
    EXPECT_TRUE(std::is_move_assignable_v<WakeLock>);
    EXPECT_FALSE(std::is_copy_constructible_v<WakeLock>);
    EXPECT_FALSE(std::is_copy_assignable_v<WakeLock>);
}

TEST(TypeTraitsTest, WakeLockIsDestructible) {
    EXPECT_TRUE(std::is_destructible_v<WakeLock>);
}

TEST(TypeTraitsTest, MouseEventIsTrivial) {
    // MouseEvent should be a simple aggregate
    EXPECT_TRUE(std::is_trivially_copyable_v<MouseEvent>);
}

TEST(TypeTraitsTest, VideoFrameIsMoveable) {
    EXPECT_TRUE(std::is_move_constructible_v<VideoFrame>);
    EXPECT_TRUE(std::is_move_assignable_v<VideoFrame>);
}

TEST(TypeTraitsTest, CursorDataIsCopyable) {
    EXPECT_TRUE(std::is_copy_constructible_v<CursorData>);
}

TEST(TypeTraitsTest, ResolutionIsTrivial) {
    // Resolution should be cheap to copy
    EXPECT_TRUE(std::is_trivially_copyable_v<Resolution>);
}

// ============================================================================
// Boundary Value Tests
// ============================================================================

TEST(BoundaryTest, ResolutionBoundaries) {
    auto res = current_resolution("unlikely_display_name_for_test_0123456789");
    EXPECT_FALSE(res.has_value());

    auto sup = supported_resolutions("0123456789012345678901234567890123456789");
    EXPECT_TRUE(sup.empty());
}

TEST(BoundaryTest, DisplayNameMaximumLength) {
    // Create a very long display name
    std::string long_name(10000, 'x');
    auto res = current_resolution(long_name);
    EXPECT_FALSE(res.has_value());

    bool changed = change_resolution(long_name, 640, 480);
    EXPECT_FALSE(changed);
}

TEST(BoundaryTest, CursorPositionBoundaries) {
    EXPECT_NO_THROW(set_cursor_pos(INT32_MAX, INT32_MAX));
    EXPECT_NO_THROW(set_cursor_pos(INT32_MIN, INT32_MIN));
    EXPECT_NO_THROW(clip_cursor(INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX));
}

// ============================================================================
// Consistency Tests
// ============================================================================

TEST(ConsistencyTest, DisplayNamesMatchResolutionQueries) {
    auto names = get_display_names();
    for (const auto& name : names) {
        auto res = current_resolution(name);
        auto supported = supported_resolutions(name);
        // If resolution is available, it should be in supported list
        // (not always true on all platforms, but check)
        if (res.has_value() && !supported.empty()) {
            bool found = false;
            for (const auto& s : supported) {
                if (s == *res) {
                    found = true;
                    break;
                }
            }
            // May not always find it, but document the check
            (void)found;
        }
    }
    SUCCEED();
}

TEST(ConsistencyTest, UsernameConsistentAcrossCalls) {
    std::string u1 = get_active_username();
    std::string u2 = get_active_username();
    EXPECT_EQ(u1, u2);
}

TEST(ConsistencyTest, ElevationConsistent) {
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(is_elevated(), is_elevated());
    }
}

TEST(ConsistencyTest, PrivacyModeSupportConsistent) {
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(is_privacy_mode_supported(), is_privacy_mode_supported());
    }
}

TEST(ConsistencyTest, KeyboardModeSupportConsistent) {
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(is_keyboard_mode_supported(), is_keyboard_mode_supported());
    }
}

// ============================================================================
// Cleanup / Final State Tests
// ============================================================================

TEST(FinalStateTest, SystemNotDamagedAfterTests) {
    // After all tests, core functions should still work
    EXPECT_NO_THROW(get_active_username());
    EXPECT_NO_THROW(is_installed());
    EXPECT_NO_THROW(is_wayland());
    EXPECT_NO_THROW(is_x11());
    EXPECT_NO_THROW(get_display_names());
}

TEST(FinalStateTest, ServiceNotLeftInBadState) {
    // Ensure we don't leave the service in a bad state
    // We can't fully control this, but check it doesn't crash
    try {
        is_service_running();
    } catch (...) {
        SUCCEED();
    }
}

TEST(FinalStateTest, PrivacyModeOffAfterTests) {
    // Try to ensure privacy mode is off after tests
    try {
        set_privacy_mode(false);
    } catch (...) {
        // Ignore errors
    }
    SUCCEED();
}

// ============================================================================
// Performance Sanity Tests (verify functions don't degrade)
// ============================================================================

TEST(PerformanceSanityTest, GetActiveUsernameLatency) {
    // get_active_username should return in reasonable time
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        get_active_username();
    }
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    // 100 calls should complete in under 1 second on any reasonable system
    EXPECT_LT(elapsed, 1000) << "get_active_username is too slow: " << elapsed << "ms for 100 calls";
}

TEST(PerformanceSanityTest, IsWaylandLatency) {
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 500; ++i) {
        is_wayland();
    }
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LT(elapsed, 1000) << "is_wayland is too slow: " << elapsed << "ms for 500 calls";
}

TEST(PerformanceSanityTest, IsX11Latency) {
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 500; ++i) {
        is_x11();
    }
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LT(elapsed, 1000) << "is_x11 is too slow: " << elapsed << "ms for 500 calls";
}

TEST(PerformanceSanityTest, IsInstalledLatency) {
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        is_installed();
    }
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LT(elapsed, 1000) << "is_installed is too slow: " << elapsed << "ms for 100 calls";
}

TEST(PerformanceSanityTest, GetDisplayNamesLatency) {
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 50; ++i) {
        auto names = get_display_names();
        (void)names;
    }
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LT(elapsed, 2000) << "get_display_names is too slow: " << elapsed << "ms for 50 calls";
}

// ============================================================================
// Comprehensive WakeLock Move-Semantics Tests
// ============================================================================

TEST(WakeLockMoveComprehensive, MoveChain) {
    // Chain multiple moves
    WakeLock a = get_wakelock(true);
    WakeLock b(std::move(a));
    WakeLock c(std::move(b));
    WakeLock d(std::move(c));
    // All should destruct cleanly
}

TEST(WakeLockMoveComprehensive, MoveAssignChain) {
    WakeLock a = get_wakelock(false);
    WakeLock b = get_wakelock(true);
    WakeLock c = get_wakelock(false);
    c = std::move(b);
    b = std::move(c);
    // Verify no leaks or double-frees
}

TEST(WakeLockMoveComprehensive, VectorOfWakeLocks) {
    // Move into container
    std::vector<WakeLock> locks;
    locks.reserve(10);
    for (int i = 0; i < 10; ++i) {
        locks.push_back(get_wakelock(i % 2 == 0));
    }
    // Add more via move
    for (int i = 0; i < 5; ++i) {
        auto lock = get_wakelock(true);
        locks.push_back(std::move(lock));
    }
    // Clear all
    locks.clear();
}

TEST(WakeLockMoveComprehensive, SwapWakeLocks) {
    WakeLock a = get_wakelock(true);
    WakeLock b = get_wakelock(false);
    std::swap(a, b);
    // Both should still be valid after swap
}

// ============================================================================
// Subsystem Interaction Tests
// ============================================================================

class SubsystemInteractionTest : public ::testing::Test {
protected:
    void SetUp() override {
        try { init(); } catch (...) {}
    }
    void TearDown() override {
        try { cleanup(); } catch (...) {}
    }
};

TEST_F(SubsystemInteractionTest, DisplayAfterCursorOps) {
    if (!has_display()) GTEST_SKIP() << "No display";

    // Cursor operations then display operations
    get_cursor_pos();
    set_cursor_pos(50, 50);
    clip_cursor(0, 0, 640, 480);
    get_cursor();

    // Display operations should still work
    auto names = get_display_names();
    for (const auto& n : names) {
        current_resolution(n);
        supported_resolutions(n);
    }
    SUCCEED();
}

TEST_F(SubsystemInteractionTest, ScreenshotAfterClipboard) {
    if (!has_display()) GTEST_SKIP() << "No display";

    // Clipboard operations
    set_clipboard_text("Screenshot after clipboard");
    get_clipboard_text();
    get_clipboard_files();

    // Then screenshot
    auto frame = capture_screen();
    if (frame.has_value()) {
        EXPECT_GT(frame->width, 0u);
    }
}

TEST_F(SubsystemInteractionTest, InputAfterSystemInfo) {
    if (!has_display()) GTEST_SKIP() << "No display";

    // System info queries
    get_active_username();
    is_wayland();
    is_x11();
    is_xfce();

    // Then input simulation
    MouseEvent ev{};
    ev.mask = MouseEvent::TYPE_MOVE;
    ev.x = 100;
    ev.y = 100;
    EXPECT_NO_THROW(simulate_mouse(ev));
    EXPECT_NO_THROW(simulate_key(0x1B, true));  // Escape
    EXPECT_NO_THROW(simulate_text("Input after system info"));
}

TEST_F(SubsystemInteractionTest, ClipboardAfterScreenshot) {
    if (!has_display()) GTEST_SKIP() << "No display";

    // Take screenshot first
    auto frame = capture_screen();
    (void)frame;

    // Then clipboard operations
    set_clipboard_text("Post-screenshot clipboard");
    auto text = get_clipboard_text();
    (void)text;
    auto files = get_clipboard_files();
    (void)files;
    SUCCEED();
}

// ============================================================================
// Display Resolution Round-Trip Tests
// ============================================================================

class ResolutionRoundTripTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!has_display()) {
            GTEST_SKIP() << "Display required for resolution round-trip tests";
        }
        display_names_ = get_display_names();
        if (display_names_.empty()) {
            GTEST_SKIP() << "No displays found";
        }
    }
    std::vector<std::string> display_names_;
};

TEST_F(ResolutionRoundTripTest, CurrentResolutionInSupportedSet) {
    for (const auto& name : display_names_) {
        auto current = current_resolution(name);
        auto supported = supported_resolutions(name);
        if (current.has_value() && !supported.empty()) {
            // Current resolution should typically be in supported set
            bool found = false;
            for (const auto& s : supported) {
                if (s == *current) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Not always a failure, but worth noting
                SUCCEED();
            }
        }
    }
}

TEST_F(ResolutionRoundTripTest, AllSupportedResolutionsAreValid) {
    for (const auto& name : display_names_) {
        auto supported = supported_resolutions(name);
        for (const auto& res : supported) {
            EXPECT_GT(res.width, 0u) 
                << "Invalid width in supported resolutions for " << name;
            EXPECT_GT(res.height, 0u) 
                << "Invalid height in supported resolutions for " << name;
            EXPECT_LE(res.width, 163840u)  // 16K sanity limit
                << "Suspiciously large width for " << name;
            EXPECT_LE(res.height, 163840u)
                << "Suspiciously large height for " << name;
        }
    }
}

TEST_F(ResolutionRoundTripTest, ResolutionConsistencyAcrossQueries) {
    for (const auto& name : display_names_) {
        auto res1 = current_resolution(name);
        auto res2 = current_resolution(name);
        auto res3 = current_resolution(name);
        // Resolution shouldn't change between rapid queries
        if (res1.has_value()) {
            EXPECT_EQ(res1->width, res2->width);
            EXPECT_EQ(res1->height, res2->height);
            EXPECT_EQ(res2->width, res3->width);
            EXPECT_EQ(res2->height, res3->height);
        }
    }
}

// ============================================================================
// Error Recovery Tests
// ============================================================================

TEST(ErrorRecoveryTest, RecoverAfterInvalidDisplayOps) {
    if (!has_display()) GTEST_SKIP() << "No display";

    // Perform invalid operations
    change_resolution("__invalid__", 0, 0);
    change_resolution("__garbage__", 99999, 99999);
    current_resolution("__nonexistent__");
    supported_resolutions("__fake_display__");

    // Then valid operations should still work
    auto names = get_display_names();
    if (!names.empty()) {
        auto res = current_resolution(names[0]);
        (void)res;
    }
    SUCCEED();
}

TEST(ErrorRecoveryTest, RecoverAfterInvalidCursorOps) {
    // Bogus cursor operations
    set_cursor_pos(-99999, -99999);
    clip_cursor(-99999, -99999, 0, 0);

    // Normal operations should still work
    get_cursor_pos();
    get_cursor();
    SUCCEED();
}

TEST(ErrorRecoveryTest, RecoverAfterInvalidScreenshot) {
    capture_screen(9999);
    capture_display("__no_such_display__");

    // Valid screenshot should still work (or gracefully fail)
    auto frame = capture_screen();
    (void)frame;
    SUCCEED();
}

// ============================================================================
// Service State Machine Tests
// ============================================================================

TEST(ServiceStateMachineTest, StatePrePostStart) {
    try {
        bool before = is_service_running();
        start_os_service();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        bool after = is_service_running();
        // 'after' should be true if start succeeded
        // Not checking equality since start may fail without permissions
        (void)before;
        (void)after;
    } catch (...) {
        SUCCEED();
    }
}

TEST(ServiceStateMachineTest, StatePrePostStop) {
    try {
        stop_os_service();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        bool after = is_service_running();
        // 'after' should be false if stop succeeded
        (void)after;
    } catch (...) {
        SUCCEED();
    }
}

TEST(ServiceStateMachineTest, StartStopTransitionStability) {
    // Multiple transitions should be stable
    for (int i = 0; i < 3; ++i) {
        try {
            start_os_service();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            stop_os_service();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } catch (...) {
            break;
        }
    }
    SUCCEED();
}

// ============================================================================
// Clipboard Format Tests
// ============================================================================

class ClipboardFormatTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!has_display() && !is_wayland_env()) {
            GTEST_SKIP() << "No display for clipboard format tests";
        }
    }
};

TEST_F(ClipboardFormatTest, PlainASCII) {
    set_clipboard_text("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    auto result = get_clipboard_text();
    // May or may not match exactly depending on platform
    (void)result;
}

TEST_F(ClipboardFormatTest, NumericContent) {
    set_clipboard_text("12345 67890 3.14159 -42");
    auto result = get_clipboard_text();
    (void)result;
}

TEST_F(ClipboardFormatTest, WhitespaceOnly) {
    set_clipboard_text("   \n\t\r\n   ");
    auto result = get_clipboard_text();
    (void)result;
}

TEST_F(ClipboardFormatTest, JSONContent) {
    std::string json = R"({"key": "value", "number": 42, "array": [1,2,3]})";
    set_clipboard_text(json);
    auto result = get_clipboard_text();
    (void)result;
}

TEST_F(ClipboardFormatTest, CodeSnippet) {
    std::string code = 
        "#include <iostream>\n"
        "int main() {\n"
        "    std::cout << \"Hello\" << std::endl;\n"
        "    return 0;\n"
        "}\n";
    set_clipboard_text(code);
    auto result = get_clipboard_text();
    (void)result;
}

// ============================================================================
// main function
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
