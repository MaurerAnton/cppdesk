#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <map>
#include <set>
#include <functional>
#include <tuple>

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

/// Configuration for smooth scroll with acceleration
struct SmoothScrollConfig {
    int32_t min_step = 1;           ///< Minimum pixels per step
    int32_t max_step = 100;         ///< Maximum pixels per step (capped)
    double acceleration = 0.5;      ///< Acceleration factor (0=linear, 1=max curve)
    std::chrono::milliseconds step_delay{1}; ///< Delay between each scroll step
    bool enabled = true;            ///< When false, falls back to instant scroll
};

/// Configuration for mouse drag operations
struct DragConfig {
    int32_t step_count = 20;        ///< Number of intermediate points
    std::chrono::milliseconds step_delay{5}; ///< Delay between steps
    bool humanize = false;          ///< Add slight randomization to path
    int32_t humanize_jitter = 3;    ///< Max pixel jitter for humanization
};

/// Multi-monitor geometry descriptor
struct MonitorInfo {
    int32_t id = 0;
    int32_t x = 0;      ///< Left edge (virtual coordinates)
    int32_t y = 0;      ///< Top edge (virtual coordinates)
    int32_t width = 0;
    int32_t height = 0;
    bool is_primary = false;
};

/// Cursor clip region (confine cursor to a rectangle)
struct ClipRegion {
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    bool active = false; ///< false = unclip
};

// ====== Keyboard ======

// Universal key codes (based on USB HID usage tables)
enum class Key : uint32_t {
    // --- Modifiers ---
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

    // --- Right-hand modifiers ---
    RightShift = 0xE5,
    RightControl = 0xE4,
    RightAlt = 0xE7,
    RightMeta = 0xE8,

    // --- Numpad ---
    Numpad0 = 0x62, Numpad1 = 0x59, Numpad2 = 0x5A,
    Numpad3 = 0x5B, Numpad4 = 0x5C, Numpad5 = 0x5D,
    Numpad6 = 0x5E, Numpad7 = 0x5F, Numpad8 = 0x60,
    Numpad9 = 0x61,
    NumpadAdd = 0x57, NumpadSubtract = 0x56,
    NumpadMultiply = 0x55, NumpadDivide = 0x54,
    NumpadDecimal = 0x63, NumpadEnter = 0x58,
    NumpadEqual = 0x67,

    // --- Media keys ---
    VolumeMute = 0xE9, VolumeDown = 0xEA, VolumeUp = 0xEB,
    MediaPlay = 0xCD, MediaPause = 0xCE,
    MediaStop = 0xB7, MediaNext = 0xB5, MediaPrev = 0xB6,
    MediaRewind = 0xB4, MediaFastForward = 0xB3,
    MediaRecord = 0xB2,
    MediaSelect = 0x180,
    MediaEject = 0xB8,
    MediaRandomPlay = 0xB9,
    MediaPlayPause = 0xCD, // alias

    // --- Application launch keys ---
    LaunchMail = 0xF1,
    LaunchMedia = 0xF2,
    LaunchApp1 = 0xF3,
    LaunchApp2 = 0xF4,
    LaunchCalculator = 0x192,
    LaunchFileBrowser = 0x194,
    LaunchTerminal = 0x196,
    LaunchWebBrowser = 0x190,

    // --- System power keys ---
    SystemPowerDown = 0x81,
    SystemSleep = 0x82,
    SystemWakeUp = 0x83,

    // --- Browser keys (non-overlapping range) ---
    BrowserBack = 0xE11, BrowserForward = 0xE12,
    BrowserRefresh = 0xE13, BrowserStop = 0xE14,
    BrowserSearch = 0xE15, BrowserFavorites = 0xE16,
    BrowserHome = 0xE17,

    // --- Extended control keys ---
    Help = 0x75,
    Undo = 0x7A,
    Redo = 0x79,
    Cut = 0x7B,
    Copy = 0x7C,
    Paste = 0x7D,
    Find = 0x7E,
    SelectAll = 0x91,
    ZoomIn = 0x92,
    ZoomOut = 0x93,
    ZoomReset = 0x94,

    // --- IME / Input keys ---
    IMEOn = 0x90,
    IMEOff = 0x91,     // reusing; real HID: 0x90/0x91
    KanaMode = 0x88,
    HanjaMode = 0x89,
    HangulMode = 0x8A,
    Katakana = 0x92,
    Hiragana = 0x93,
    ZenkakuHankaku = 0x94,

    // --- Language / layout keys ---
    Lang1 = 0x8B,      // Korean Hangul/English
    Lang2 = 0x8C,      // Korean Hanja
    Lang3 = 0x8D,      // Japanese Katakana
    Lang4 = 0x8E,      // Japanese Hiragana
    Lang5 = 0x8F,      // Japanese Zenkaku/Hankaku

    // --- Layout-specific character (platform-dependent) ---
    Layout = 0x1000,   // Base offset for layout chars
    Raw = 0x2000,      // Raw keycode (platform-specific)
};

/// Complete modifier state snapshot
struct ModifierState {
    bool shift_left   = false;
    bool shift_right  = false;
    bool ctrl_left    = false;
    bool ctrl_right   = false;
    bool alt_left     = false;
    bool alt_right    = false;
    bool meta_left    = false;
    bool meta_right   = false;
    bool caps_lock    = false;
    bool num_lock     = false;
    bool scroll_lock  = false;

    /// True if any shift is pressed
    [[nodiscard]] bool any_shift() const { return shift_left || shift_right; }
    /// True if any control is pressed
    [[nodiscard]] bool any_ctrl()  const { return ctrl_left || ctrl_right; }
    /// True if any alt is pressed
    [[nodiscard]] bool any_alt()   const { return alt_left || alt_right; }
    /// True if any meta is pressed
    [[nodiscard]] bool any_meta()  const { return meta_left || meta_right; }
    /// True if the classic Ctrl+Alt+Shift+Win combo is active
    [[nodiscard]] bool is_classic_mod() const {
        return any_shift() && any_ctrl() && any_alt() && any_meta();
    }
};

/// Configuration for key repeat simulation
struct KeyRepeatConfig {
    int32_t count = 1;              ///< Number of repeats (excluding initial press)
    std::chrono::milliseconds press_duration{30};  ///< How long each press holds
    std::chrono::milliseconds release_duration{10};///< How long each release holds
    std::chrono::milliseconds inter_delay{50};     ///< Delay between repeats
};

/// Configuration for key sequence (Unicode input context)
struct KeySequenceConfig {
    bool use_unicode_fallback = true; ///< Fall back to Unicode input if VK mapping fails
    std::chrono::milliseconds char_delay{1}; ///< Delay between characters
};

// ====== Platform-specific keycode maps ======

/// Static lookup tables mapping enigo::Key to/from platform native keycodes.
/// Extended with comprehensive entries for all supported platforms.
struct PlatformKeyMap {
#ifdef _WIN32
    /// Convert enigo Key → Windows virtual key (VK_*)
    static uint32_t to_virtual_key(Key k);
    /// Convert Windows virtual key → enigo Key
    static Key from_virtual_key(uint32_t vk);
    /// Get scan code for a Key (for SendInput wScan field)
    static uint32_t to_scan_code(Key k);
    /// Get extended-key flag for SendInput
    static bool is_extended_key(Key k);
    /// Full VK_* name lookup for debugging
    static const char* virtual_key_name(uint32_t vk);
#endif
#ifdef __APPLE__
    /// Convert enigo Key → macOS CGKeyCode (kVK_*)
    static uint16_t to_cg_keycode(Key k);
    /// Convert macOS CGKeyCode → enigo Key
    static Key from_cg_keycode(uint16_t code);
    /// Get CGEventFlags mask for a modifier Key
    static uint64_t to_cg_event_flags(Key k);
    /// Full keycode name for debugging
    static const char* cg_keycode_name(uint16_t code);
#endif
#ifdef __linux__
    /// Convert enigo Key → X11 KeySym (XK_*)
    static uint32_t to_x11_keysym(Key k);
    /// Convert X11 KeySym → enigo Key
    static Key from_x11_keysym(uint32_t ks);
    /// Convert enigo Key → X11 keycode (hardware-dependent; needs Display*)
    static uint32_t to_x11_keycode(Key k, void* display);
    /// Full keysym name for debugging
    static const char* x11_keysym_name(uint32_t ks);
#endif
};

// ====== DSL Parser ======

/// DSL token types: plain text, special key, modifier start/end, repeat, timing
struct DslToken {
    enum Type : uint8_t {
        TEXT = 0,
        KEY,
        MODIFIER_START,
        MODIFIER_END,
        RAW_KEY,
        REPEAT_KEY,     ///< {KEY N} — repeat key N times
        DELAY,          ///< {DELAY 500ms} — pause
        TIMED_MOD_START,///< {+SHIFT 100ms} — press modifier with timing
        TIMED_MOD_END,  ///< {-SHIFT 100ms} — release modifier with timing
        CHORD,          ///< {KEY1+KEY2} — press both together (chord)
    };
    Type type = TEXT;
    std::string text;           // for TEXT tokens
    Key key = Key::Space;       // for KEY / RAW_KEY / REPEAT_KEY tokens
    std::vector<Key> modifiers; // active modifiers at this point
    int32_t repeat_count = 1;   // for REPEAT_KEY
    std::chrono::milliseconds timing{0}; // for DELAY / TIMED_MOD_*
    std::vector<Key> chord_keys; // for CHORD tokens
};

class DslParser {
public:
    /// Parse a DSL string like "hello {+SHIFT}WORLD{-SHIFT}!"
    /// Supports:
    ///   {+MOD} to press, {-MOD} to release, {KEY} for special keys
    ///   {KEY 5} for repeat, {DELAY 100ms} for timing
    ///   {+SHIFT 100ms} for timed modifier hold
    ///   {KEY1+KEY2} for chords
    ///   Nested modifiers: {{+CTRL}{+SHIFT}A{-SHIFT}{-CTRL}}
    static std::vector<DslToken> parse(const std::string& dsl);

    /// Convert tokens back to a canonical DSL string
    static std::string unparse(const std::vector<DslToken>& tokens);

    /// Check whether a string is a valid DSL key name
    static bool is_valid_key_name(const std::string& name);

    /// Get all registered DSL key names
    static std::vector<std::string> registered_key_names();
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

    // --- MouseControllable ---
    bool mouse_move_to(int32_t x, int32_t y) override;
    bool mouse_move_relative(int32_t dx, int32_t dy) override;
    bool mouse_down(MouseButton button) override;
    bool mouse_up(MouseButton button) override;
    bool mouse_scroll(int32_t delta, MouseAxis axis = MouseAxis::Vertical) override;
    std::pair<int32_t, int32_t> mouse_location() override;

    // --- KeyboardControllable ---
    bool key_down(Key key) override;
    bool key_up(Key key) override;
    bool key_sequence(const std::string& sequence) override;
    bool get_key_state(Key key) override;
    bool get_caps_lock_state() override;
    bool get_num_lock_state() override;
    void release_all() override;

    // ==== Extended Mouse API ====

    /// Smooth scroll with acceleration curve
    /// @param delta  Total scroll amount (lines or pixels)
    /// @param axis   Scroll axis
    /// @param config Acceleration and step configuration
    bool mouse_smooth_scroll(int32_t delta, MouseAxis axis, const SmoothScrollConfig& config = {});

    /// Drag from start to end with configurable steps
    /// @param start_x, start_y  Starting position
    /// @param end_x, end_y      Ending position
    /// @param button            Mouse button to hold
    /// @param config            Step count, delay, humanization
    bool drag(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y,
              MouseButton button = MouseButton::Left,
              const DragConfig& config = {});

    /// Simple convenience overload
    bool drag(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y,
              MouseButton button);

    /// Confine cursor to a rectangular region (or unclip if ClipRegion::active=false)
    /// Platform-dependent — uses ClipCursor on Windows, CGAssociateMouseAndMouseCursorPosition on macOS,
    /// XGrabPointer/XFixes on Linux.
    bool mouse_clip(const ClipRegion& region);

    /// Remove any active cursor clip
    bool mouse_unclip();

    /// Translate coordinates from one monitor's space to another
    /// Returns the translated (x, y) or std::nullopt on failure
    std::optional<std::pair<int32_t, int32_t>> monitor_translate(
        int32_t x, int32_t y, int32_t from_monitor_id, int32_t to_monitor_id);

    /// Enumerate all connected monitors
    std::vector<MonitorInfo> monitor_list();

    /// Get the monitor containing point (x, y). Returns monitor id, or -1 if not found.
    int32_t monitor_at(int32_t x, int32_t y);

    /// Get primary monitor info
    MonitorInfo primary_monitor();

    // ==== Extended Keyboard API ====

    /// Get complete modifier state (Ctrl, Alt, Shift, Meta, CapsLock, NumLock, ScrollLock)
    ModifierState get_modifier_state();

    /// Simulate key repeat: press-release cycle repeated N times
    /// @param key   The key to repeat
    /// @param config Repeat count and timings
    bool key_repeat(Key key, const KeyRepeatConfig& config = {});

    /// Type a Unicode string directly (bypassing keyboard layout)
    /// Uses platform-specific Unicode input mechanisms:
    ///   Windows: KEYEVENTF_UNICODE
    ///   macOS:   CGEventKeyboardSetUnicodeString
    ///   Linux:   XTest with XKeysymToKeycode (best-effort)
    bool key_sequence_unicode(const std::string& text);

    /// Type a single Unicode codepoint
    bool key_unicode_codepoint(uint32_t codepoint);

    /// Set configuration for key_sequence
    void set_sequence_config(const KeySequenceConfig& config);

    /// Get current key sequence configuration
    KeySequenceConfig get_sequence_config() const;

    /// Set smooth scroll configuration
    void set_smooth_scroll_config(const SmoothScrollConfig& config);
    SmoothScrollConfig get_smooth_scroll_config() const;

    // --- Utilities ---

    /// Check if input simulation is available on this platform
    bool is_available() const;

    /// Set inter-event delay (used between key/button events)
    void set_delay(std::chrono::milliseconds delay);

    /// Get current inter-event delay
    std::chrono::milliseconds get_delay() const;

    /// Send a raw platform keycode press
    bool raw_key_down(uint32_t raw_code);

    /// Send a raw platform keycode release
    bool raw_key_up(uint32_t raw_code);

    /// Check if a modifier key is being tracked as pressed
    bool is_modifier_pressed(Key mod) const;

    /// Simulate a key chord (press all keys, then release in reverse order)
    bool key_chord(const std::vector<Key>& keys);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ====== Extra input value (for Windows hook-based input) ======
extern uint32_t ENIGO_INPUT_EXTRA_VALUE;

// ====== Utility Functions ======

/// Check if a Key is a modifier (Shift, Ctrl, Alt, Meta, AltGr)
inline bool is_modifier_key(Key k) {
    switch (k) {
        case Key::Shift: case Key::RightShift:
        case Key::Control: case Key::RightControl:
        case Key::Alt: case Key::RightAlt: case Key::AltGr:
        case Key::Meta: case Key::RightMeta:
            return true;
        default: return false;
    }
}

/// Check if a Key is a numpad key
inline bool is_numpad_key(Key k) {
    uint32_t v = static_cast<uint32_t>(k);
    return (v >= static_cast<uint32_t>(Key::Numpad0) &&
            v <= static_cast<uint32_t>(Key::NumpadEqual)) ||
           v == static_cast<uint32_t>(Key::NumLock);
}

/// Check if a Key is a media key
inline bool is_media_key(Key k) {
    uint32_t v = static_cast<uint32_t>(k);
    return (v == static_cast<uint32_t>(Key::VolumeMute) ||
            v == static_cast<uint32_t>(Key::VolumeDown) ||
            v == static_cast<uint32_t>(Key::VolumeUp) ||
            (v >= static_cast<uint32_t>(Key::MediaPlay) &&
             v <= static_cast<uint32_t>(Key::MediaPlayPause)));
}

/// Check if a Key is a browser key
inline bool is_browser_key(Key k) {
    uint32_t v = static_cast<uint32_t>(k);
    return v >= static_cast<uint32_t>(Key::BrowserBack) &&
           v <= static_cast<uint32_t>(Key::BrowserHome);
}

/// Check if a Key is a launch key
inline bool is_launch_key(Key k) {
    uint32_t v = static_cast<uint32_t>(k);
    return (v >= static_cast<uint32_t>(Key::LaunchMail) &&
            v <= static_cast<uint32_t>(Key::LaunchWebBrowser)) ||
           v == static_cast<uint32_t>(Key::LaunchCalculator) ||
           v == static_cast<uint32_t>(Key::LaunchFileBrowser) ||
           v == static_cast<uint32_t>(Key::LaunchTerminal);
}

/// Check if a Key is a system power key
inline bool is_system_power_key(Key k) {
    uint32_t v = static_cast<uint32_t>(k);
    return v >= static_cast<uint32_t>(Key::SystemPowerDown) &&
           v <= static_cast<uint32_t>(Key::SystemWakeUp);
}

/// Get a human-readable name for a Key
inline const char* key_name(Key k) {
    switch (k) {
        case Key::Shift: return "Shift";
        case Key::RightShift: return "RightShift";
        case Key::Control: return "Control";
        case Key::RightControl: return "RightControl";
        case Key::Alt: return "Alt";
        case Key::RightAlt: return "RightAlt";
        case Key::AltGr: return "AltGr";
        case Key::Meta: return "Meta";
        case Key::RightMeta: return "RightMeta";
        case Key::Return: return "Return";
        case Key::Escape: return "Escape";
        case Key::Tab: return "Tab";
        case Key::Space: return "Space";
        case Key::Backspace: return "Backspace";
        case Key::Delete: return "Delete";
        case Key::Insert: return "Insert";
        case Key::Home: return "Home";
        case Key::End: return "End";
        case Key::PageUp: return "PageUp";
        case Key::PageDown: return "PageDown";
        case Key::LeftArrow: return "LeftArrow";
        case Key::RightArrow: return "RightArrow";
        case Key::UpArrow: return "UpArrow";
        case Key::DownArrow: return "DownArrow";
        case Key::CapsLock: return "CapsLock";
        case Key::NumLock: return "NumLock";
        case Key::ScrollLock: return "ScrollLock";
        case Key::PrintScreen: return "PrintScreen";
        case Key::Pause: return "Pause";
        case Key::Menu: return "Menu";
        case Key::VolumeMute: return "VolumeMute";
        case Key::VolumeDown: return "VolumeDown";
        case Key::VolumeUp: return "VolumeUp";
        case Key::MediaPlay: return "MediaPlay";
        case Key::MediaPause: return "MediaPause";
        case Key::MediaStop: return "MediaStop";
        case Key::MediaNext: return "MediaNext";
        case Key::MediaPrev: return "MediaPrev";
        case Key::BrowserBack: return "BrowserBack";
        case Key::BrowserForward: return "BrowserForward";
        case Key::BrowserRefresh: return "BrowserRefresh";
        case Key::BrowserStop: return "BrowserStop";
        case Key::BrowserSearch: return "BrowserSearch";
        case Key::BrowserFavorites: return "BrowserFavorites";
        case Key::BrowserHome: return "BrowserHome";
        case Key::LaunchMail: return "LaunchMail";
        case Key::LaunchMedia: return "LaunchMedia";
        case Key::LaunchCalculator: return "LaunchCalculator";
        case Key::SystemPowerDown: return "SystemPowerDown";
        case Key::SystemSleep: return "SystemSleep";
        case Key::SystemWakeUp: return "SystemWakeUp";
        case Key::Help: return "Help";
        case Key::Undo: return "Undo";
        case Key::Redo: return "Redo";
        case Key::Cut: return "Cut";
        case Key::Copy: return "Copy";
        case Key::Paste: return "Paste";
        case Key::Find: return "Find";
        case Key::SelectAll: return "SelectAll";
        case Key::ZoomIn: return "ZoomIn";
        case Key::ZoomOut: return "ZoomOut";
        case Key::ZoomReset: return "ZoomReset";
        default: return "Unknown";
    }
}

/// Get a human-readable name for a MouseButton
inline const char* mouse_button_name(MouseButton b) {
    switch (b) {
        case MouseButton::Left: return "Left";
        case MouseButton::Middle: return "Middle";
        case MouseButton::Right: return "Right";
        case MouseButton::ScrollUp: return "ScrollUp";
        case MouseButton::ScrollDown: return "ScrollDown";
        case MouseButton::ScrollLeft: return "ScrollLeft";
        case MouseButton::ScrollRight: return "ScrollRight";
        case MouseButton::Back: return "Back";
        case MouseButton::Forward: return "Forward";
        default: return "Unknown";
    }
}

/// Convert a MouseButton to a platform key (for compatibility with keyboard-controlled mouse)
inline Key mouse_button_to_key(MouseButton b) {
    switch (b) {
        case MouseButton::Left: return static_cast<Key>(0x1001);
        case MouseButton::Right: return static_cast<Key>(0x1002);
        case MouseButton::Middle: return static_cast<Key>(0x1003);
        case MouseButton::Back: return static_cast<Key>(0x1004);
        case MouseButton::Forward: return static_cast<Key>(0x1005);
        default: return Key::Space;
    }
}

/// System power action types
enum class PowerAction {
    Shutdown,
    Reboot,
    Sleep,
    Hibernate,
    Logoff,
    LockScreen,
};

/// Send a system power action (platform-specific)
/// Returns true if the action was successfully initiated.
bool send_power_action(PowerAction action);

/// Get the current system uptime in milliseconds
uint64_t system_uptime_ms();

/// Convenience: type a string with modifiers held down
/// Example: type_with_modifiers("hello", {Key::Shift}) types "HELLO"
inline void type_with_modifiers(KeyboardControllable& kb, const std::string& text,
                                const std::vector<Key>& mods) {
    for (auto m : mods) kb.key_down(m);
    kb.key_sequence(text);
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) kb.key_up(*it);
}

/// Convenience: simulate a keyboard shortcut like Ctrl+C
/// Example: keyboard_shortcut(enigo_instance, {Key::Control}, Key::C)
inline void keyboard_shortcut(KeyboardControllable& kb,
                              const std::vector<Key>& mods, Key main_key) {
    for (auto m : mods) kb.key_down(m);
    kb.key_click(main_key);
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) kb.key_up(*it);
}

} // namespace enigo
