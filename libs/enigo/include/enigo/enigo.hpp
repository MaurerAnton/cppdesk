#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <map>
#include <functional>

namespace enigo {

// ====== Mouse ======

enum class MouseButton : int32_t {
    Left = 0,
    Middle = 1,
    Right = 2,
    ScrollUp = 3,    // wheel scroll events
    ScrollDown = 4,
    ScrollLeft = 5,
    ScrollRight = 6,
    Back = 7,
    Forward = 8,
};

enum class MouseAxis {
    Horizontal,
    Vertical,
};

// ====== Keyboard ======

// Universal key codes (based on USB HID usage tables)
enum class Key : uint32_t {
    // Modifiers
    Alt = 0xE2,
    AltGr = 0xE6,
    Backspace = 0x2A,
    CapsLock = 0x39,
    Control = 0xE0,    // Left Control
    Delete = 0x4C,
    DownArrow = 0x51,
    End = 0x4D,
    Escape = 0x29,
    F1 = 0x3A, F2 = 0x3B, F3 = 0x3C, F4 = 0x3D,
    F5 = 0x3E, F6 = 0x3F, F7 = 0x40, F8 = 0x41,
    F9 = 0x42, F10 = 0x43, F11 = 0x44, F12 = 0x45,
    F13 = 0x68, F14 = 0x69, F15 = 0x6A,
    F16 = 0x6B, F17 = 0x6C, F18 = 0x6D,
    F19 = 0x6E, F20 = 0x6F, F21 = 0x70,
    F22 = 0x71, F23 = 0x72, F24 = 0x73,
    Home = 0x4A,
    Insert = 0x49,
    LeftArrow = 0x50,
    Meta = 0xE3,       // Left GUI / Windows / Command
    NumLock = 0x53,
    PageDown = 0x4E,
    PageUp = 0x4B,
    Return = 0x28,     // Enter
    RightArrow = 0x4F,
    ScrollLock = 0x47,
    Shift = 0xE1,      // Left Shift
    Space = 0x2C,
    Tab = 0x2B,
    UpArrow = 0x52,
    PrintScreen = 0x46,
    Pause = 0x48,
    Menu = 0x65,       // Application / context menu

    // Numpad
    Numpad0 = 0x62, Numpad1 = 0x59, Numpad2 = 0x5A,
    Numpad3 = 0x5B, Numpad4 = 0x5C, Numpad5 = 0x5D,
    Numpad6 = 0x5E, Numpad7 = 0x5F, Numpad8 = 0x60,
    Numpad9 = 0x61,
    NumpadAdd = 0x57, NumpadSubtract = 0x56,
    NumpadMultiply = 0x55, NumpadDivide = 0x54,
    NumpadDecimal = 0x63, NumpadEnter = 0x58,

    // Media keys
    VolumeMute = 0xE9, VolumeDown = 0xEA, VolumeUp = 0xEB,
    MediaPlay = 0xCD, MediaPause = 0xCE,
    MediaStop = 0xB7, MediaNext = 0xB5, MediaPrev = 0xB6,
    MediaRewind = 0xB4, MediaFastForward = 0xB3,

    // Browser keys
    BrowserBack = 0xEA, BrowserForward = 0xEB,
    BrowserRefresh = 0xEC, BrowserStop = 0xED,
    BrowserSearch = 0xEE, BrowserFavorites = 0xEF,
    BrowserHome = 0xF0,

    // Launch keys
    LaunchMail = 0xF1, LaunchMedia = 0xF2,
    LaunchApp1 = 0xF3, LaunchApp2 = 0xF4,

    // Layout-specific character (platform-dependent)
    Layout = 0x1000,   // Base offset for layout chars
    Raw = 0x2000,      // Raw keycode (platform-specific)
};

// ====== Platform-specific keycode maps ======

struct PlatformKeyMap {
#ifdef _WIN32
    static uint32_t to_virtual_key(Key k);
    static Key from_virtual_key(uint32_t vk);
#endif
#ifdef __APPLE__
    static uint16_t to_cg_keycode(Key k);
    static Key from_cg_keycode(uint16_t code);
#endif
#ifdef __linux__
    static uint32_t to_x11_keycode(Key k);
    static Key from_x11_keycode(uint32_t code);
#endif
};

// ====== DSL Parser ======

// DSL token types: plain text, special key, modifier start/end
struct DslToken {
    enum Type { TEXT, KEY, MODIFIER_START, MODIFIER_END, RAW_KEY };
    Type type = TEXT;
    std::string text;      // for TEXT tokens
    Key key = Key::Space;  // for KEY tokens
    std::vector<Key> modifiers; // active modifiers at this point
};

class DslParser {
public:
    // Parse a DSL string like "hello {+SHIFT}WORLD{-SHIFT}!"
    // Supports: {+MOD} to press, {-MOD} to release, {KEY} for special keys
    static std::vector<DslToken> parse(const std::string& dsl);
    static std::string unparse(const std::vector<DslToken>& tokens);
};

// ====== Mouse Controller ======

class MouseControllable {
public:
    virtual ~MouseControllable() = default;

    /// Move mouse to absolute coordinates
    virtual bool mouse_move_to(int32_t x, int32_t y) = 0;

    /// Move mouse relative to current position
    virtual bool mouse_move_relative(int32_t dx, int32_t dy) = 0;

    /// Press a mouse button
    virtual bool mouse_down(MouseButton button) = 0;

    /// Release a mouse button
    virtual bool mouse_up(MouseButton button) = 0;

    /// Click a mouse button (down + up)
    virtual bool mouse_click(MouseButton button) {
        return mouse_down(button) && mouse_up(button);
    }

    /// Scroll the mouse wheel
    virtual bool mouse_scroll(int32_t delta, MouseAxis axis = MouseAxis::Vertical) = 0;

    /// Get current mouse position
    virtual std::pair<int32_t, int32_t> mouse_location() = 0;
};

// ====== Keyboard Controller ======

class KeyboardControllable {
public:
    virtual ~KeyboardControllable() = default;

    /// Press a key
    virtual bool key_down(Key key) = 0;

    /// Release a key
    virtual bool key_up(Key key) = 0;

    /// Click a key (down + up)
    virtual bool key_click(Key key) {
        return key_down(key) && key_up(key);
    }

    /// Type a sequence of characters (Unicode text input)
    virtual bool key_sequence(const std::string& sequence) = 0;

    /// Type a DSL sequence like "hello {+SHIFT}world{-SHIFT}"
    virtual bool key_sequence_dsl(const std::string& dsl);

    /// Get current state of a key (pressed or not)
    virtual bool get_key_state(Key key) = 0;

    /// Check if CapsLock is on
    virtual bool get_caps_lock_state() = 0;

    /// Check if NumLock is on
    virtual bool get_num_lock_state() = 0;

    /// Release all pressed keys (panic/reset)
    virtual void release_all() = 0;
};

// ====== Main Enigo Controller ======

/// Cross-platform input simulation
class Enigo : public MouseControllable, public KeyboardControllable {
public:
    Enigo();
    ~Enigo() override;

    // MouseControllable
    bool mouse_move_to(int32_t x, int32_t y) override;
    bool mouse_move_relative(int32_t dx, int32_t dy) override;
    bool mouse_down(MouseButton button) override;
    bool mouse_up(MouseButton button) override;
    bool mouse_scroll(int32_t delta, MouseAxis axis = MouseAxis::Vertical) override;
    std::pair<int32_t, int32_t> mouse_location() override;

    // KeyboardControllable
    bool key_down(Key key) override;
    bool key_up(Key key) override;
    bool key_sequence(const std::string& sequence) override;
    bool get_key_state(Key key) override;
    bool get_caps_lock_state() override;
    bool get_num_lock_state() override;
    void release_all() override;

    // Additional
    bool is_available() const;
    void set_delay(std::chrono::milliseconds delay);
    std::chrono::milliseconds get_delay() const;

    // Raw platform keycode
    bool raw_key_down(uint32_t raw_code);
    bool raw_key_up(uint32_t raw_code);

    // Modifier tracking
    bool is_modifier_pressed(Key mod) const;

    // Drag operations
    bool drag(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y,
        MouseButton button = MouseButton::Left);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ====== Extra input value (for Windows hook-based input) ======
extern uint32_t ENIGO_INPUT_EXTRA_VALUE;

} // namespace enigo
