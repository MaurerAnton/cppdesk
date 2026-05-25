//============================================================================
// cppdesk - Input Service Implementation
// Comprehensive platform-aware input handling:
//   - Cursor position tracking (polling via platform APIs)
//   - Cursor shape/image capture (X11 XFixes, Windows GetCursorInfo, macOS NSCursor)
//   - Window focus tracking (currently focused window title)
//   - Mouse event simulation (SendInput on Windows, XTest on Linux, CGEvent on macOS)
//   - Keyboard event simulation with keycode mapping
//   - Text input simulation (IME-aware)
//   - Modifier key state tracking (Ctrl, Alt, Shift, Win/Cmd)
//   - Scroll wheel handling (vertical and horizontal)
//   - Relative mouse movement mode (for gaming)
//   - Multi-monitor coordinate translation
//   - Remapped keycode handling
//   - Device modifier release on disconnect
//============================================================================

#include "cppdesk/server/server.hpp"
#include "cppdesk/platform/platform.hpp"
#include "common/config.hpp"
#include "common/protocol.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Platform-specific headers
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <windowsx.h>
    #include <winuser.h>
    #include <shellapi.h>
    #include <psapi.h>
    #pragma comment(lib, "user32.lib")
    #pragma comment(lib, "gdi32.lib")
    #pragma comment(lib, "psapi.lib")
#elif defined(__APPLE__)
    #include <ApplicationServices/ApplicationServices.h>
    #include <CoreGraphics/CoreGraphics.h>
    #include <Carbon/Carbon.h>
    #include <IOKit/hidsystem/IOHIDLib.h>
    #include <IOKit/hidsystem/ev_keymap.h>
    #include <AppKit/AppKit.h>
#else // Linux
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <X11/Xatom.h>
    #include <X11/extensions/XTest.h>
    #include <X11/extensions/Xfixes.h>
    #include <X11/extensions/XInput2.h>
    #include <X11/extensions/shape.h>
    #include <X11/cursorfont.h>
    #include <string.h>
    #include <unistd.h>
#endif

namespace cppdesk::server {

//============================================================================
// Platform Abstraction Layer (internal helpers)
//============================================================================
namespace input_detail {

//----------------------------------------------------------------------------
// Keycode Mapping: scan codes to platform-independent codes and back
//----------------------------------------------------------------------------

// Platform-independent key code definitions (USB HID-inspired)
enum class UniversalKey : uint32_t {
    NONE                = 0x0000,
    ESCAPE              = 0x0001,
    NUM_1               = 0x0002,
    NUM_2               = 0x0003,
    NUM_3               = 0x0004,
    NUM_4               = 0x0005,
    NUM_5               = 0x0006,
    NUM_6               = 0x0007,
    NUM_7               = 0x0008,
    NUM_8               = 0x0009,
    NUM_9               = 0x000A,
    NUM_0               = 0x000B,
    MINUS               = 0x000C,
    EQUALS              = 0x000D,
    BACKSPACE           = 0x000E,
    TAB                 = 0x000F,
    Q                   = 0x0010,
    W                   = 0x0011,
    E                   = 0x0012,
    R                   = 0x0013,
    T                   = 0x0014,
    Y                   = 0x0015,
    U                   = 0x0016,
    I                   = 0x0017,
    O                   = 0x0018,
    P                   = 0x0019,
    LEFT_BRACKET        = 0x001A,
    RIGHT_BRACKET       = 0x001B,
    ENTER               = 0x001C,
    LEFT_CTRL           = 0x001D,
    A                   = 0x001E,
    S                   = 0x001F,
    D                   = 0x0020,
    F                   = 0x0021,
    G                   = 0x0022,
    H                   = 0x0023,
    J                   = 0x0024,
    K                   = 0x0025,
    L                   = 0x0026,
    SEMICOLON           = 0x0027,
    APOSTROPHE          = 0x0028,
    GRAVE               = 0x0029,
    LEFT_SHIFT          = 0x002A,
    BACKSLASH           = 0x002B,
    Z                   = 0x002C,
    X                   = 0x002D,
    C                   = 0x002E,
    V                   = 0x002F,
    B                   = 0x0030,
    N                   = 0x0031,
    M                   = 0x0032,
    COMMA               = 0x0033,
    PERIOD              = 0x0034,
    SLASH               = 0x0035,
    RIGHT_SHIFT         = 0x0036,
    KP_MULTIPLY         = 0x0037,
    LEFT_ALT            = 0x0038,
    SPACE               = 0x0039,
    CAPS_LOCK           = 0x003A,
    F1                  = 0x003B,
    F2                  = 0x003C,
    F3                  = 0x003D,
    F4                  = 0x003E,
    F5                  = 0x003F,
    F6                  = 0x0040,
    F7                  = 0x0041,
    F8                  = 0x0042,
    F9                  = 0x0043,
    F10                 = 0x0044,
    NUM_LOCK            = 0x0045,
    SCROLL_LOCK         = 0x0046,
    KP_7                = 0x0047,
    KP_8                = 0x0048,
    KP_9                = 0x0049,
    KP_MINUS            = 0x004A,
    KP_4                = 0x004B,
    KP_5                = 0x004C,
    KP_6                = 0x004D,
    KP_PLUS             = 0x004E,
    KP_1                = 0x004F,
    KP_2                = 0x0050,
    KP_3                = 0x0051,
    KP_0                = 0x0052,
    KP_PERIOD           = 0x0053,
    F11                 = 0x0057,
    F12                 = 0x0058,
    KP_ENTER            = 0xE01C,
    RIGHT_CTRL          = 0xE01D,
    KP_DIVIDE           = 0xE035,
    PRINT_SCREEN        = 0xE037,
    RIGHT_ALT           = 0xE038,
    HOME                = 0xE047,
    UP                  = 0xE048,
    PAGE_UP             = 0xE049,
    LEFT                = 0xE04B,
    RIGHT               = 0xE04D,
    END                 = 0xE04F,
    DOWN                = 0xE050,
    PAGE_DOWN           = 0xE051,
    INSERT              = 0xE052,
    DELETE              = 0xE053,
    LEFT_WIN            = 0xE05B,
    RIGHT_WIN           = 0xE05C,
    MENU                = 0xE05D,
    PAUSE               = 0xE046,
};

// Modifier mask bits
enum class ModifierMask : uint8_t {
    NONE    = 0x00,
    CTRL    = 0x01,
    ALT     = 0x02,
    SHIFT   = 0x04,
    WIN     = 0x08,   // Windows key / Command key
    CAPS    = 0x10,
    NUM     = 0x20,
    SCROLL  = 0x40,
};

inline ModifierMask operator|(ModifierMask a, ModifierMask b) {
    return static_cast<ModifierMask>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline ModifierMask operator&(ModifierMask a, ModifierMask b) {
    return static_cast<ModifierMask>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline ModifierMask& operator|=(ModifierMask& a, ModifierMask b) {
    a = a | b; return a;
}
inline ModifierMask& operator&=(ModifierMask& a, ModifierMask b) {
    a = a & b; return a;
}
inline bool mod_has(ModifierMask m, ModifierMask flag) {
    return (static_cast<uint8_t>(m) & static_cast<uint8_t>(flag)) != 0;
}

//----------------------------------------------------------------------------
// Keycode Mapper: translates between universal codes and platform codes
//----------------------------------------------------------------------------
class KeycodeMapper {
public:
    static KeycodeMapper& instance() {
        static KeycodeMapper inst;
        return inst;
    }

    // Map a platform-specific keycode to a universal key
    UniversalKey platform_to_universal(uint32_t platform_code) const {
        auto it = platform_to_universal_.find(platform_code);
        if (it != platform_to_universal_.end()) return it->second;
        return UniversalKey::NONE;
    }

    // Map a universal key to a platform-specific keycode
    uint32_t universal_to_platform(UniversalKey uk) const {
        auto it = universal_to_platform_.find(uk);
        if (it != universal_to_platform_.end()) return it->second;
        return 0;
    }

    // Check if a key is a modifier
    bool is_modifier(UniversalKey uk) const {
        switch (uk) {
        case UniversalKey::LEFT_CTRL:   case UniversalKey::RIGHT_CTRL:
        case UniversalKey::LEFT_ALT:    case UniversalKey::RIGHT_ALT:
        case UniversalKey::LEFT_SHIFT:  case UniversalKey::RIGHT_SHIFT:
        case UniversalKey::LEFT_WIN:    case UniversalKey::RIGHT_WIN:
        case UniversalKey::CAPS_LOCK:   case UniversalKey::NUM_LOCK:
        case UniversalKey::SCROLL_LOCK:             return true;
        default:                                     return false;
        }
    }

    // Get the modifier mask for a universal key
    ModifierMask to_modifier_mask(UniversalKey uk) const {
        switch (uk) {
        case UniversalKey::LEFT_CTRL:
        case UniversalKey::RIGHT_CTRL:   return ModifierMask::CTRL;
        case UniversalKey::LEFT_ALT:
        case UniversalKey::RIGHT_ALT:    return ModifierMask::ALT;
        case UniversalKey::LEFT_SHIFT:
        case UniversalKey::RIGHT_SHIFT:  return ModifierMask::SHIFT;
        case UniversalKey::LEFT_WIN:
        case UniversalKey::RIGHT_WIN:    return ModifierMask::WIN;
        case UniversalKey::CAPS_LOCK:    return ModifierMask::CAPS;
        case UniversalKey::NUM_LOCK:     return ModifierMask::NUM;
        case UniversalKey::SCROLL_LOCK:  return ModifierMask::SCROLL;
        default:                         return ModifierMask::NONE;
        }
    }

    // Remap a universal keycode (for custom layouts)
    void set_remap(UniversalKey from, UniversalKey to) {
        std::lock_guard lk(remap_mutex_);
        remap_[from] = to;
    }

    UniversalKey get_remap(UniversalKey key) const {
        std::lock_guard lk(remap_mutex_);
        auto it = remap_.find(key);
        return (it != remap_.end()) ? it->second : key;
    }

    void clear_remaps() {
        std::lock_guard lk(remap_mutex_);
        remap_.clear();
    }

private:
    KeycodeMapper() { build_maps(); }

    void build_maps() {
#ifdef _WIN32
        // Windows virtual key codes -> Universal
        add_pair(VK_ESCAPE,    UniversalKey::ESCAPE);
        add_pair('1',          UniversalKey::NUM_1);
        add_pair('2',          UniversalKey::NUM_2);
        add_pair('3',          UniversalKey::NUM_3);
        add_pair('4',          UniversalKey::NUM_4);
        add_pair('5',          UniversalKey::NUM_5);
        add_pair('6',          UniversalKey::NUM_6);
        add_pair('7',          UniversalKey::NUM_7);
        add_pair('8',          UniversalKey::NUM_8);
        add_pair('9',          UniversalKey::NUM_9);
        add_pair('0',          UniversalKey::NUM_0);
        add_pair(VK_OEM_MINUS, UniversalKey::MINUS);
        add_pair(VK_OEM_PLUS,  UniversalKey::EQUALS);
        add_pair(VK_BACK,      UniversalKey::BACKSPACE);
        add_pair(VK_TAB,       UniversalKey::TAB);
        add_pair('Q',          UniversalKey::Q);
        add_pair('W',          UniversalKey::W);
        add_pair('E',          UniversalKey::E);
        add_pair('R',          UniversalKey::R);
        add_pair('T',          UniversalKey::T);
        add_pair('Y',          UniversalKey::Y);
        add_pair('U',          UniversalKey::U);
        add_pair('I',          UniversalKey::I);
        add_pair('O',          UniversalKey::O);
        add_pair('P',          UniversalKey::P);
        add_pair(VK_OEM_4,     UniversalKey::LEFT_BRACKET);
        add_pair(VK_OEM_6,     UniversalKey::RIGHT_BRACKET);
        add_pair(VK_RETURN,    UniversalKey::ENTER);
        add_pair(VK_LCONTROL,  UniversalKey::LEFT_CTRL);
        add_pair('A',          UniversalKey::A);
        add_pair('S',          UniversalKey::S);
        add_pair('D',          UniversalKey::D);
        add_pair('F',          UniversalKey::F);
        add_pair('G',          UniversalKey::G);
        add_pair('H',          UniversalKey::H);
        add_pair('J',          UniversalKey::J);
        add_pair('K',          UniversalKey::K);
        add_pair('L',          UniversalKey::L);
        add_pair(VK_OEM_1,     UniversalKey::SEMICOLON);
        add_pair(VK_OEM_7,     UniversalKey::APOSTROPHE);
        add_pair(VK_OEM_3,     UniversalKey::GRAVE);
        add_pair(VK_LSHIFT,    UniversalKey::LEFT_SHIFT);
        add_pair(VK_OEM_5,     UniversalKey::BACKSLASH);
        add_pair('Z',          UniversalKey::Z);
        add_pair('X',          UniversalKey::X);
        add_pair('C',          UniversalKey::C);
        add_pair('V',          UniversalKey::V);
        add_pair('B',          UniversalKey::B);
        add_pair('N',          UniversalKey::N);
        add_pair('M',          UniversalKey::M);
        add_pair(VK_OEM_COMMA, UniversalKey::COMMA);
        add_pair(VK_OEM_PERIOD,UniversalKey::PERIOD);
        add_pair(VK_OEM_2,     UniversalKey::SLASH);
        add_pair(VK_RSHIFT,    UniversalKey::RIGHT_SHIFT);
        add_pair(VK_MULTIPLY,  UniversalKey::KP_MULTIPLY);
        add_pair(VK_LMENU,     UniversalKey::LEFT_ALT);
        add_pair(VK_SPACE,     UniversalKey::SPACE);
        add_pair(VK_CAPITAL,   UniversalKey::CAPS_LOCK);
        add_pair(VK_F1,        UniversalKey::F1);
        add_pair(VK_F2,        UniversalKey::F2);
        add_pair(VK_F3,        UniversalKey::F3);
        add_pair(VK_F4,        UniversalKey::F4);
        add_pair(VK_F5,        UniversalKey::F5);
        add_pair(VK_F6,        UniversalKey::F6);
        add_pair(VK_F7,        UniversalKey::F7);
        add_pair(VK_F8,        UniversalKey::F8);
        add_pair(VK_F9,        UniversalKey::F9);
        add_pair(VK_F10,       UniversalKey::F10);
        add_pair(VK_F11,       UniversalKey::F11);
        add_pair(VK_F12,       UniversalKey::F12);
        add_pair(VK_NUMLOCK,   UniversalKey::NUM_LOCK);
        add_pair(VK_SCROLL,    UniversalKey::SCROLL_LOCK);
        add_pair(VK_NUMPAD7,   UniversalKey::KP_7);
        add_pair(VK_NUMPAD8,   UniversalKey::KP_8);
        add_pair(VK_NUMPAD9,   UniversalKey::KP_9);
        add_pair(VK_SUBTRACT,  UniversalKey::KP_MINUS);
        add_pair(VK_NUMPAD4,   UniversalKey::KP_4);
        add_pair(VK_NUMPAD5,   UniversalKey::KP_5);
        add_pair(VK_NUMPAD6,   UniversalKey::KP_6);
        add_pair(VK_ADD,       UniversalKey::KP_PLUS);
        add_pair(VK_NUMPAD1,   UniversalKey::KP_1);
        add_pair(VK_NUMPAD2,   UniversalKey::KP_2);
        add_pair(VK_NUMPAD3,   UniversalKey::KP_3);
        add_pair(VK_NUMPAD0,   UniversalKey::KP_0);
        add_pair(VK_DECIMAL,   UniversalKey::KP_PERIOD);
        add_pair(VK_RCONTROL,  UniversalKey::RIGHT_CTRL);
        add_pair(VK_DIVIDE,    UniversalKey::KP_DIVIDE);
        add_pair(VK_SNAPSHOT,  UniversalKey::PRINT_SCREEN);
        add_pair(VK_RMENU,     UniversalKey::RIGHT_ALT);
        add_pair(VK_HOME,      UniversalKey::HOME);
        add_pair(VK_UP,        UniversalKey::UP);
        add_pair(VK_PRIOR,     UniversalKey::PAGE_UP);
        add_pair(VK_LEFT,      UniversalKey::LEFT);
        add_pair(VK_RIGHT,     UniversalKey::RIGHT);
        add_pair(VK_END,       UniversalKey::END);
        add_pair(VK_DOWN,      UniversalKey::DOWN);
        add_pair(VK_NEXT,      UniversalKey::PAGE_DOWN);
        add_pair(VK_INSERT,    UniversalKey::INSERT);
        add_pair(VK_DELETE,    UniversalKey::DELETE);
        add_pair(VK_LWIN,      UniversalKey::LEFT_WIN);
        add_pair(VK_RWIN,      UniversalKey::RIGHT_WIN);
        add_pair(VK_APPS,      UniversalKey::MENU);
        add_pair(VK_PAUSE,     UniversalKey::PAUSE);
#elif defined(__APPLE__)
        add_pair(kVK_Escape,     UniversalKey::ESCAPE);
        add_pair(kVK_ANSI_1,     UniversalKey::NUM_1);
        add_pair(kVK_ANSI_2,     UniversalKey::NUM_2);
        add_pair(kVK_ANSI_3,     UniversalKey::NUM_3);
        add_pair(kVK_ANSI_4,     UniversalKey::NUM_4);
        add_pair(kVK_ANSI_5,     UniversalKey::NUM_5);
        add_pair(kVK_ANSI_6,     UniversalKey::NUM_6);
        add_pair(kVK_ANSI_7,     UniversalKey::NUM_7);
        add_pair(kVK_ANSI_8,     UniversalKey::NUM_8);
        add_pair(kVK_ANSI_9,     UniversalKey::NUM_9);
        add_pair(kVK_ANSI_0,     UniversalKey::NUM_0);
        add_pair(kVK_ANSI_Minus, UniversalKey::MINUS);
        add_pair(kVK_ANSI_Equal, UniversalKey::EQUALS);
        add_pair(kVK_Delete,     UniversalKey::BACKSPACE);
        add_pair(kVK_Tab,        UniversalKey::TAB);
        add_pair(kVK_ANSI_Q,     UniversalKey::Q);
        add_pair(kVK_ANSI_W,     UniversalKey::W);
        add_pair(kVK_ANSI_E,     UniversalKey::E);
        add_pair(kVK_ANSI_R,     UniversalKey::R);
        add_pair(kVK_ANSI_T,     UniversalKey::T);
        add_pair(kVK_ANSI_Y,     UniversalKey::Y);
        add_pair(kVK_ANSI_U,     UniversalKey::U);
        add_pair(kVK_ANSI_I,     UniversalKey::I);
        add_pair(kVK_ANSI_O,     UniversalKey::O);
        add_pair(kVK_ANSI_P,     UniversalKey::P);
        add_pair(kVK_ANSI_LeftBracket,  UniversalKey::LEFT_BRACKET);
        add_pair(kVK_ANSI_RightBracket, UniversalKey::RIGHT_BRACKET);
        add_pair(kVK_Return,     UniversalKey::ENTER);
        add_pair(kVK_Control,    UniversalKey::LEFT_CTRL);
        add_pair(kVK_ANSI_A,     UniversalKey::A);
        add_pair(kVK_ANSI_S,     UniversalKey::S);
        add_pair(kVK_ANSI_D,     UniversalKey::D);
        add_pair(kVK_ANSI_F,     UniversalKey::F);
        add_pair(kVK_ANSI_G,     UniversalKey::G);
        add_pair(kVK_ANSI_H,     UniversalKey::H);
        add_pair(kVK_ANSI_J,     UniversalKey::J);
        add_pair(kVK_ANSI_K,     UniversalKey::K);
        add_pair(kVK_ANSI_L,     UniversalKey::L);
        add_pair(kVK_ANSI_Semicolon,    UniversalKey::SEMICOLON);
        add_pair(kVK_ANSI_Quote,        UniversalKey::APOSTROPHE);
        add_pair(kVK_ANSI_Grave,        UniversalKey::GRAVE);
        add_pair(kVK_Shift,      UniversalKey::LEFT_SHIFT);
        add_pair(kVK_ANSI_Backslash,    UniversalKey::BACKSLASH);
        add_pair(kVK_ANSI_Z,     UniversalKey::Z);
        add_pair(kVK_ANSI_X,     UniversalKey::X);
        add_pair(kVK_ANSI_C,     UniversalKey::C);
        add_pair(kVK_ANSI_V,     UniversalKey::V);
        add_pair(kVK_ANSI_B,     UniversalKey::B);
        add_pair(kVK_ANSI_N,     UniversalKey::N);
        add_pair(kVK_ANSI_M,     UniversalKey::M);
        add_pair(kVK_ANSI_Comma,  UniversalKey::COMMA);
        add_pair(kVK_ANSI_Period, UniversalKey::PERIOD);
        add_pair(kVK_ANSI_Slash,  UniversalKey::SLASH);
        add_pair(kVK_RightShift,  UniversalKey::RIGHT_SHIFT);
        add_pair(kVK_Option,      UniversalKey::LEFT_ALT);
        add_pair(kVK_Space,       UniversalKey::SPACE);
        add_pair(kVK_CapsLock,    UniversalKey::CAPS_LOCK);
        add_pair(kVK_F1,          UniversalKey::F1);
        add_pair(kVK_F2,          UniversalKey::F2);
        add_pair(kVK_F3,          UniversalKey::F3);
        add_pair(kVK_F4,          UniversalKey::F4);
        add_pair(kVK_F5,          UniversalKey::F5);
        add_pair(kVK_F6,          UniversalKey::F6);
        add_pair(kVK_F7,          UniversalKey::F7);
        add_pair(kVK_F8,          UniversalKey::F8);
        add_pair(kVK_F9,          UniversalKey::F9);
        add_pair(kVK_F10,         UniversalKey::F10);
        add_pair(kVK_F11,         UniversalKey::F11);
        add_pair(kVK_F12,         UniversalKey::F12);
        add_pair(kVK_Home,        UniversalKey::HOME);
        add_pair(kVK_UpArrow,     UniversalKey::UP);
        add_pair(kVK_PageUp,      UniversalKey::PAGE_UP);
        add_pair(kVK_LeftArrow,   UniversalKey::LEFT);
        add_pair(kVK_RightArrow,  UniversalKey::RIGHT);
        add_pair(kVK_End,         UniversalKey::END);
        add_pair(kVK_DownArrow,   UniversalKey::DOWN);
        add_pair(kVK_PageDown,    UniversalKey::PAGE_DOWN);
        add_pair(kVK_ForwardDelete, UniversalKey::DELETE);
        add_pair(kVK_Command,     UniversalKey::LEFT_WIN);
        add_pair(kVK_RightCommand,UniversalKey::RIGHT_WIN);
#else // Linux X11
        add_pair(XK_Escape,       UniversalKey::ESCAPE);
        add_pair(XK_1,            UniversalKey::NUM_1);
        add_pair(XK_2,            UniversalKey::NUM_2);
        add_pair(XK_3,            UniversalKey::NUM_3);
        add_pair(XK_4,            UniversalKey::NUM_4);
        add_pair(XK_5,            UniversalKey::NUM_5);
        add_pair(XK_6,            UniversalKey::NUM_6);
        add_pair(XK_7,            UniversalKey::NUM_7);
        add_pair(XK_8,            UniversalKey::NUM_8);
        add_pair(XK_9,            UniversalKey::NUM_9);
        add_pair(XK_0,            UniversalKey::NUM_0);
        add_pair(XK_minus,        UniversalKey::MINUS);
        add_pair(XK_equal,        UniversalKey::EQUALS);
        add_pair(XK_BackSpace,    UniversalKey::BACKSPACE);
        add_pair(XK_Tab,          UniversalKey::TAB);
        add_pair(XK_q,            UniversalKey::Q);
        add_pair(XK_w,            UniversalKey::W);
        add_pair(XK_e,            UniversalKey::E);
        add_pair(XK_r,            UniversalKey::R);
        add_pair(XK_t,            UniversalKey::T);
        add_pair(XK_y,            UniversalKey::Y);
        add_pair(XK_u,            UniversalKey::U);
        add_pair(XK_i,            UniversalKey::I);
        add_pair(XK_o,            UniversalKey::O);
        add_pair(XK_p,            UniversalKey::P);
        add_pair(XK_bracketleft,  UniversalKey::LEFT_BRACKET);
        add_pair(XK_bracketright, UniversalKey::RIGHT_BRACKET);
        add_pair(XK_Return,       UniversalKey::ENTER);
        add_pair(XK_Control_L,    UniversalKey::LEFT_CTRL);
        add_pair(XK_a,            UniversalKey::A);
        add_pair(XK_s,            UniversalKey::S);
        add_pair(XK_d,            UniversalKey::D);
        add_pair(XK_f,            UniversalKey::F);
        add_pair(XK_g,            UniversalKey::G);
        add_pair(XK_h,            UniversalKey::H);
        add_pair(XK_j,            UniversalKey::J);
        add_pair(XK_k,            UniversalKey::K);
        add_pair(XK_l,            UniversalKey::L);
        add_pair(XK_semicolon,    UniversalKey::SEMICOLON);
        add_pair(XK_apostrophe,   UniversalKey::APOSTROPHE);
        add_pair(XK_grave,        UniversalKey::GRAVE);
        add_pair(XK_Shift_L,      UniversalKey::LEFT_SHIFT);
        add_pair(XK_backslash,    UniversalKey::BACKSLASH);
        add_pair(XK_z,            UniversalKey::Z);
        add_pair(XK_x,            UniversalKey::X);
        add_pair(XK_c,            UniversalKey::C);
        add_pair(XK_v,            UniversalKey::V);
        add_pair(XK_b,            UniversalKey::B);
        add_pair(XK_n,            UniversalKey::N);
        add_pair(XK_m,            UniversalKey::M);
        add_pair(XK_comma,        UniversalKey::COMMA);
        add_pair(XK_period,       UniversalKey::PERIOD);
        add_pair(XK_slash,        UniversalKey::SLASH);
        add_pair(XK_Shift_R,      UniversalKey::RIGHT_SHIFT);
        add_pair(XK_KP_Multiply,  UniversalKey::KP_MULTIPLY);
        add_pair(XK_Alt_L,        UniversalKey::LEFT_ALT);
        add_pair(XK_space,        UniversalKey::SPACE);
        add_pair(XK_Caps_Lock,    UniversalKey::CAPS_LOCK);
        add_pair(XK_F1,           UniversalKey::F1);
        add_pair(XK_F2,           UniversalKey::F2);
        add_pair(XK_F3,           UniversalKey::F3);
        add_pair(XK_F4,           UniversalKey::F4);
        add_pair(XK_F5,           UniversalKey::F5);
        add_pair(XK_F6,           UniversalKey::F6);
        add_pair(XK_F7,           UniversalKey::F7);
        add_pair(XK_F8,           UniversalKey::F8);
        add_pair(XK_F9,           UniversalKey::F9);
        add_pair(XK_F10,          UniversalKey::F10);
        add_pair(XK_F11,          UniversalKey::F11);
        add_pair(XK_F12,          UniversalKey::F12);
        add_pair(XK_Num_Lock,     UniversalKey::NUM_LOCK);
        add_pair(XK_Scroll_Lock,  UniversalKey::SCROLL_LOCK);
        add_pair(XK_KP_7,         UniversalKey::KP_7);
        add_pair(XK_KP_8,         UniversalKey::KP_8);
        add_pair(XK_KP_9,         UniversalKey::KP_9);
        add_pair(XK_KP_Subtract,  UniversalKey::KP_MINUS);
        add_pair(XK_KP_4,         UniversalKey::KP_4);
        add_pair(XK_KP_5,         UniversalKey::KP_5);
        add_pair(XK_KP_6,         UniversalKey::KP_6);
        add_pair(XK_KP_Add,       UniversalKey::KP_PLUS);
        add_pair(XK_KP_1,         UniversalKey::KP_1);
        add_pair(XK_KP_2,         UniversalKey::KP_2);
        add_pair(XK_KP_3,         UniversalKey::KP_3);
        add_pair(XK_KP_0,         UniversalKey::KP_0);
        add_pair(XK_KP_Decimal,   UniversalKey::KP_PERIOD);
        add_pair(XK_KP_Enter,     UniversalKey::KP_ENTER);
        add_pair(XK_Control_R,    UniversalKey::RIGHT_CTRL);
        add_pair(XK_KP_Divide,    UniversalKey::KP_DIVIDE);
        add_pair(XK_Print,        UniversalKey::PRINT_SCREEN);
        add_pair(XK_Alt_R,        UniversalKey::RIGHT_ALT);
        add_pair(XK_Home,         UniversalKey::HOME);
        add_pair(XK_Up,           UniversalKey::UP);
        add_pair(XK_Page_Up,      UniversalKey::PAGE_UP);
        add_pair(XK_Left,         UniversalKey::LEFT);
        add_pair(XK_Right,        UniversalKey::RIGHT);
        add_pair(XK_End,          UniversalKey::END);
        add_pair(XK_Down,         UniversalKey::DOWN);
        add_pair(XK_Page_Down,    UniversalKey::PAGE_DOWN);
        add_pair(XK_Insert,       UniversalKey::INSERT);
        add_pair(XK_Delete,       UniversalKey::DELETE);
        add_pair(XK_Super_L,      UniversalKey::LEFT_WIN);
        add_pair(XK_Super_R,      UniversalKey::RIGHT_WIN);
        add_pair(XK_Menu,         UniversalKey::MENU);
        add_pair(XK_Pause,        UniversalKey::PAUSE);
#endif
    }

    void add_pair(uint32_t platform, UniversalKey uk) {
        platform_to_universal_[platform] = uk;
        universal_to_platform_[uk] = platform;
    }

    std::unordered_map<uint32_t, UniversalKey> platform_to_universal_;
    std::unordered_map<UniversalKey, uint32_t> universal_to_platform_;
    mutable std::mutex remap_mutex_;
    std::unordered_map<UniversalKey, UniversalKey> remap_;
};

//----------------------------------------------------------------------------
// Modifier State Tracker: tracks current modifier key states
//----------------------------------------------------------------------------
class ModifierStateTracker {
public:
    static ModifierStateTracker& instance() {
        static ModifierStateTracker inst;
        return inst;
    }

    // Update modifier state when a modifier key is pressed/released
    void on_key(UniversalKey key, bool pressed) {
        std::lock_guard lk(mutex_);
        auto mask = KeycodeMapper::instance().to_modifier_mask(key);
        if (mask == ModifierMask::NONE) return;

        if (pressed) {
            active_mods_ |= mask;
            // Track individual modifier keys for release-on-disconnect
            if (modifier_keys_.find(key) == modifier_keys_.end()) {
                modifier_keys_.insert(key);
                modifier_press_order_.push_back(key);
            }
        } else {
            active_mods_ &= static_cast<ModifierMask>(~static_cast<uint8_t>(mask));
            modifier_keys_.erase(key);
        }
    }

    // Get current modifier mask
    ModifierMask current_modifiers() const {
        std::lock_guard lk(mutex_);
        return active_mods_;
    }

    // Check if a specific modifier is active
    bool is_ctrl()  const { return mod_has(current_modifiers(), ModifierMask::CTRL); }
    bool is_alt()   const { return mod_has(current_modifiers(), ModifierMask::ALT); }
    bool is_shift() const { return mod_has(current_modifiers(), ModifierMask::SHIFT); }
    bool is_win()   const { return mod_has(current_modifiers(), ModifierMask::WIN); }
    bool is_caps()  const { return mod_has(current_modifiers(), ModifierMask::CAPS); }

    // Get list of currently held modifier keys (for device disconnect release)
    std::vector<UniversalKey> held_modifier_keys() const {
        std::lock_guard lk(mutex_);
        return std::vector<UniversalKey>(modifier_press_order_.begin(),
                                          modifier_press_order_.end());
    }

    // Release all modifiers (called on device disconnect)
    void release_all_modifiers(std::function<void(UniversalKey, bool)> sim_fn) {
        std::vector<UniversalKey> to_release;
        {
            std::lock_guard lk(mutex_);
            to_release.assign(modifier_press_order_.rbegin(), modifier_press_order_.rend());
        }
        for (auto key : to_release) {
            sim_fn(key, false); // release
        }
        {
            std::lock_guard lk(mutex_);
            modifier_keys_.clear();
            modifier_press_order_.clear();
            active_mods_ = ModifierMask::NONE;
        }
    }

    // Clear all state
    void reset() {
        std::lock_guard lk(mutex_);
        modifier_keys_.clear();
        modifier_press_order_.clear();
        active_mods_ = ModifierMask::NONE;
    }

private:
    ModifierStateTracker() = default;
    mutable std::mutex mutex_;
    ModifierMask active_mods_ = ModifierMask::NONE;
    std::set<UniversalKey> modifier_keys_;
    std::vector<UniversalKey> modifier_press_order_;
};

//----------------------------------------------------------------------------
// Multi-Monitor Coordinate Translation
//----------------------------------------------------------------------------
class MonitorGeometry {
public:
    struct Monitor {
        std::string name;
        int32_t x = 0;       // top-left X in virtual space
        int32_t y = 0;       // top-left Y in virtual space
        int32_t width = 0;
        int32_t height = 0;
        bool is_primary = false;
    };

    static MonitorGeometry& instance() {
        static MonitorGeometry inst;
        return inst;
    }

    void refresh() {
        std::lock_guard lk(mutex_);
        monitors_.clear();

#ifdef _WIN32
        EnumDisplayMonitors(nullptr, nullptr,
            [](HMONITOR hMon, HDC, LPRECT rc, LPARAM lp) -> BOOL {
                auto* self = reinterpret_cast<MonitorGeometry*>(lp);
                MONITORINFOEX mi = {};
                mi.cbSize = sizeof(mi);
                if (GetMonitorInfo(hMon, &mi)) {
                    Monitor m;
                    m.name = mi.szDevice;
                    m.x = mi.rcMonitor.left;
                    m.y = mi.rcMonitor.top;
                    m.width = mi.rcMonitor.right - mi.rcMonitor.left;
                    m.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
                    m.is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
                    self->monitors_.push_back(m);
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(this));
#elif defined(__APPLE__)
        CGDirectDisplayID displays[16];
        uint32_t count = 0;
        if (CGGetActiveDisplayList(16, displays, &count) == kCGErrorSuccess) {
            for (uint32_t i = 0; i < count; ++i) {
                Monitor m;
                CGRect bounds = CGDisplayBounds(displays[i]);
                m.name = "display_" + std::to_string(i);
                m.x = static_cast<int32_t>(bounds.origin.x);
                m.y = static_cast<int32_t>(bounds.origin.y);
                m.width = static_cast<int32_t>(bounds.size.width);
                m.height = static_cast<int32_t>(bounds.size.height);
                m.is_primary = CGDisplayIsMain(displays[i]);
                monitors_.push_back(m);
            }
        }
#else // Linux X11
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) return;

        int screen_count = ScreenCount(dpy);
        for (int s = 0; s < screen_count; ++s) {
            Screen* scr = ScreenOfDisplay(dpy, s);
            Monitor m;
            m.name = "screen_" + std::to_string(s);
            m.x = 0;
            m.y = 0;
            m.width = scr->width;
            m.height = scr->height;
            m.is_primary = (s == DefaultScreen(dpy));
            monitors_.push_back(m);
        }

        // Try XRandR for more accurate geometry
        int rr_event, rr_error;
        if (XRRQueryExtension(dpy, &rr_event, &rr_error)) {
            XRRScreenResources* res = XRRGetScreenResources(dpy, DefaultRootWindow(dpy));
            if (res) {
                monitors_.clear();
                for (int i = 0; i < res->noutput; ++i) {
                    XRROutputInfo* out = XRRGetOutputInfo(dpy, res, res->outputs[i]);
                    if (!out || out->connection != RR_Connected || out->crtc == 0) {
                        if (out) XRRFreeOutputInfo(out);
                        continue;
                    }
                    XRRCrtcInfo* crtc = XRRGetCrtcInfo(dpy, res, out->crtc);
                    if (crtc) {
                        Monitor m;
                        m.name = out->name;
                        m.x = crtc->x;
                        m.y = crtc->y;
                        m.width = static_cast<int32_t>(crtc->width);
                        m.height = static_cast<int32_t>(crtc->height);
                        m.is_primary = (i == 0);
                        monitors_.push_back(m);
                        XRRFreeCrtcInfo(crtc);
                    }
                    XRRFreeOutputInfo(out);
                }
                XRRFreeScreenResources(res);
            }
        }
        XCloseDisplay(dpy);
#endif
    }

    // Convert virtual desktop coordinates to monitor-local coordinates
    // Returns the monitor index and local (x,y)
    std::pair<int32_t, std::pair<int32_t, int32_t>>
    virtual_to_monitor(int32_t vx, int32_t vy) const {
        std::lock_guard lk(mutex_);
        for (size_t i = 0; i < monitors_.size(); ++i) {
            const auto& m = monitors_[i];
            if (vx >= m.x && vx < m.x + m.width &&
                vy >= m.y && vy < m.y + m.height) {
                return {static_cast<int32_t>(i), {vx - m.x, vy - m.y}};
            }
        }
        // Default to primary
        if (!monitors_.empty()) {
            for (size_t i = 0; i < monitors_.size(); ++i) {
                if (monitors_[i].is_primary) {
                    return {static_cast<int32_t>(i),
                            {vx - monitors_[i].x, vy - monitors_[i].y}};
                }
            }
        }
        return {0, {vx, vy}};
    }

    // Convert monitor-local coordinates to virtual coordinates
    std::pair<int32_t, int32_t> monitor_to_virtual(
        int32_t display_idx, int32_t local_x, int32_t local_y) const {
        std::lock_guard lk(mutex_);
        if (display_idx >= 0 && static_cast<size_t>(display_idx) < monitors_.size()) {
            const auto& m = monitors_[display_idx];
            return {m.x + local_x, m.y + local_y};
        }
        return {local_x, local_y};
    }

    const std::vector<Monitor>& monitors() const {
        std::lock_guard lk(mutex_);
        return monitors_;
    }

private:
    MonitorGeometry() { refresh(); }
    mutable std::mutex mutex_;
    std::vector<Monitor> monitors_;
};

//----------------------------------------------------------------------------
// Cursor Position Tracker (polls platform cursor position)
//----------------------------------------------------------------------------
class CursorPositionTracker {
public:
    struct State {
        int32_t x = 0;
        int32_t y = 0;
        int32_t display_idx = 0;
    };

    CursorPositionTracker() = default;

    State poll() const {
        State state;

#ifdef _WIN32
        POINT pt;
        if (GetCursorPos(&pt)) {
            state.x = pt.x;
            state.y = pt.y;
        }
#elif defined(__APPLE__)
        CGEventRef event = CGEventCreate(nullptr);
        CGPoint pt = CGEventGetLocation(event);
        CFRelease(event);
        state.x = static_cast<int32_t>(pt.x);
        state.y = static_cast<int32_t>(pt.y);
#else // Linux
        Display* dpy = XOpenDisplay(nullptr);
        if (dpy) {
            Window root = DefaultRootWindow(dpy);
            Window child;
            int root_x, root_y, win_x, win_y;
            unsigned int mask;
            if (XQueryPointer(dpy, root, &root, &child,
                              &root_x, &root_y, &win_x, &win_y, &mask)) {
                state.x = root_x;
                state.y = root_y;
            }
            XCloseDisplay(dpy);
        }
#endif

        // Resolve display index
        auto [display_idx, _] =
            MonitorGeometry::instance().virtual_to_monitor(state.x, state.y);
        state.display_idx = display_idx;

        return state;
    }

    // Set cursor position (absolute)
    std::pair<int32_t, int32_t> current_position() const {
        State s = poll();
        return {s.x, s.y};
    }
};

//----------------------------------------------------------------------------
// Cursor Shape/Image Capture
//----------------------------------------------------------------------------
class CursorShapeCapturer {
public:
    static CursorShapeCapturer& instance() {
        static CursorShapeCapturer inst;
        return inst;
    }

    // Capture the current cursor image
    common::CursorData capture() {
        common::CursorData data;
        static std::atomic<uint32_t> cursor_id_counter{1};

#ifdef _WIN32
        CURSORINFO ci = {};
        ci.cbSize = sizeof(ci);
        if (!GetCursorInfo(&ci)) return data;

        data.id = cursor_id_counter.fetch_add(1);

        if (ci.flags & CURSOR_SHOWING) {
            ICONINFO ii = {};
            if (!GetIconInfo(ci.hCursor, &ii)) return data;

            BITMAP bm = {};
            if (ii.hbmColor) {
                GetObject(ii.hbmColor, sizeof(bm), &bm);
                data.width = bm.bmWidth;
                data.height = bm.bmHeight;
                data.hot_x = ii.xHotspot;
                data.hot_y = ii.yHotspot;

                // Read pixel data
                HDC hdc = GetDC(nullptr);
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, ii.hbmColor);

                BITMAPINFO bi = {};
                bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
                bi.bmiHeader.biWidth = bm.bmWidth;
                bi.bmiHeader.biHeight = -bm.bmHeight; // top-down
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;

                data.colors.resize(bm.bmWidth * bm.bmHeight * 4);
                if (!GetDIBits(memDC, ii.hbmColor, 0, bm.bmHeight,
                               data.colors.data(), &bi, DIB_RGB_COLORS)) {
                    data.colors.clear();
                }

                SelectObject(memDC, oldBmp);
                DeleteDC(memDC);
                ReleaseDC(nullptr, hdc);
            } else if (ii.hbmMask) {
                // Monochrome cursor - fallback
                GetObject(ii.hbmMask, sizeof(bm), &bm);
                data.width = bm.bmWidth;
                data.height = bm.bmHeight / 2; // mask is double-height
                data.hot_x = ii.xHotspot;
                data.hot_y = ii.yHotspot;
            }

            if (ii.hbmColor) DeleteObject(ii.hbmColor);
            if (ii.hbmMask)  DeleteObject(ii.hbmMask);
        }
#elif defined(__APPLE__)
        data.id = cursor_id_counter.fetch_add(1);
        NSCursor* cursor = [NSCursor currentSystemCursor];
        if (cursor) {
            NSImage* img = [cursor image];
            if (img) {
                NSSize size = [img size];
                data.width = static_cast<uint32_t>(size.width);
                data.height = static_cast<uint32_t>(size.height);
                data.hot_x = static_cast<int32_t>([cursor hotSpot].x);
                data.hot_y = static_cast<int32_t>([cursor hotSpot].y);

                // Get CGImage for pixel data
                CGImageRef cgImg = [img CGImageForProposedRect:nullptr
                    context:nil hints:nil];
                if (cgImg) {
                    size_t w = CGImageGetWidth(cgImg);
                    size_t h = CGImageGetHeight(cgImg);
                    data.colors.resize(w * h * 4);

                    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
                    CGContextRef ctx = CGBitmapContextCreate(
                        data.colors.data(), w, h, 8, w * 4,
                        cs, kCGImageAlphaPremultipliedLast);
                    if (ctx) {
                        CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cgImg);
                        CGContextRelease(ctx);
                    }
                    CGColorSpaceRelease(cs);
                }
            }
        }
#else // Linux X11 (XFixes)
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) return data;

        int fixes_event, fixes_error;
        if (XQueryExtension(dpy, "XFIXES", &fixes_event, &fixes_event, &fixes_error)) {
            data.id = cursor_id_counter.fetch_add(1);

            XFixesCursorImage* img = XFixesGetCursorImage(dpy);
            if (img) {
                data.width = img->width;
                data.height = img->height;
                data.hot_x = img->xhot;
                data.hot_y = img->yhot;

                // XFixes cursor images are ARGB (premultiplied)
                data.colors.resize(img->width * img->height * 4);
                for (size_t i = 0; i < static_cast<size_t>(img->width * img->height); ++i) {
                    unsigned long pixel = img->pixels[i];
                    uint8_t a = (pixel >> 24) & 0xFF;
                    uint8_t r = (pixel >> 16) & 0xFF;
                    uint8_t g = (pixel >> 8) & 0xFF;
                    uint8_t b = (pixel >> 0) & 0xFF;
                    // Convert premultiplied alpha back to straight alpha
                    if (a > 0) {
                        r = static_cast<uint8_t>((r * 255) / a);
                        g = static_cast<uint8_t>((g * 255) / a);
                        b = static_cast<uint8_t>((b * 255) / a);
                    }
                    data.colors[i * 4 + 0] = r;
                    data.colors[i * 4 + 1] = g;
                    data.colors[i * 4 + 2] = b;
                    data.colors[i * 4 + 3] = a;
                }
                XFree(img);
            }
        }
        XCloseDisplay(dpy);
#endif
        return data;
    }

    // Check if the cursor has changed since last capture
    bool has_changed(const common::CursorData& previous) const {
        auto current = capture();
        if (current.width != previous.width ||
            current.height != previous.height ||
            current.hot_x != previous.hot_x ||
            current.hot_y != previous.hot_y) {
            return true;
        }
        if (current.colors.size() != previous.colors.size()) {
            return true;
        }
        if (!current.colors.empty()) {
            return memcmp(current.colors.data(), previous.colors.data(),
                          current.colors.size()) != 0;
        }
        return false;
    }
};

//----------------------------------------------------------------------------
// Window Focus Tracker
//----------------------------------------------------------------------------
class WindowFocusTracker {
public:
    struct WindowInfo {
        std::string title;
        std::string class_name;
        uint64_t window_id = 0;
        int32_t pid = 0;
        std::string process_name;
    };

    static WindowFocusTracker& instance() {
        static WindowFocusTracker inst;
        return inst;
    }

    WindowInfo poll() {
        WindowInfo info;

#ifdef _WIN32
        HWND hwnd = GetForegroundWindow();
        if (hwnd) {
            info.window_id = reinterpret_cast<uint64_t>(hwnd);

            // Get window title
            wchar_t title_buf[512] = {};
            GetWindowTextW(hwnd, title_buf, 511);
            int len = WideCharToMultiByte(CP_UTF8, 0, title_buf, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string utf8_title(len - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, title_buf, -1, &utf8_title[0], len, nullptr, nullptr);
                info.title = utf8_title;
            }

            // Get class name
            wchar_t class_buf[256] = {};
            GetClassNameW(hwnd, class_buf, 255);
            len = WideCharToMultiByte(CP_UTF8, 0, class_buf, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string utf8_class(len - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, class_buf, -1, &utf8_class[0], len, nullptr, nullptr);
                info.class_name = utf8_class;
            }

            // Get PID
            GetWindowThreadProcessId(hwnd, reinterpret_cast<DWORD*>(&info.pid));

            // Get process name
            if (info.pid > 0) {
                HANDLE hProc = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION, FALSE, info.pid);
                if (hProc) {
                    wchar_t proc_name[MAX_PATH] = {};
                    DWORD proc_size = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProc, 0, proc_name, &proc_size)) {
                        std::wstring ws(proc_name);
                        std::string ps(ws.begin(), ws.end());
                        // Extract just the filename
                        auto pos = ps.find_last_of("\\/");
                        info.process_name = (pos != std::string::npos)
                            ? ps.substr(pos + 1) : ps;
                    }
                    CloseHandle(hProc);
                }
            }
        }
#elif defined(__APPLE__)
        // macOS: Get frontmost application info using NSWorkspace
        @autoreleasepool {
            NSRunningApplication* app = [[NSWorkspace sharedWorkspace] frontmostApplication];
            if (app) {
                info.process_name = [[app localizedName] UTF8String];
                info.pid = static_cast<int32_t>([app processIdentifier]);

                // Get window title via Accessibility or CGWindow API
                CFArrayRef windowList = CGWindowListCopyWindowInfo(
                    kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                    kCGNullWindowID);
                if (windowList) {
                    CFIndex count = CFArrayGetCount(windowList);
                    for (CFIndex i = 0; i < count; ++i) {
                        CFDictionaryRef win = (CFDictionaryRef)CFArrayGetValueAtIndex(windowList, i);
                        CFNumberRef pidRef = (CFNumberRef)CFDictionaryGetValue(win, kCGWindowOwnerPID);
                        int32_t winPid = 0;
                        if (pidRef) CFNumberGetValue(pidRef, kCFNumberSInt32Type, &winPid);
                        if (winPid == info.pid) {
                            CFStringRef titleRef = (CFStringRef)CFDictionaryGetValue(win, kCGWindowName);
                            if (titleRef) {
                                char buf[512] = {};
                                CFStringGetCString(titleRef, buf, sizeof(buf), kCFStringEncodingUTF8);
                                info.title = buf;
                            }
                            CFNumberRef widRef = (CFNumberRef)CFDictionaryGetValue(win, kCGWindowNumber);
                            if (widRef) {
                                int64_t wid = 0;
                                CFNumberGetValue(widRef, kCFNumberSInt64Type, &wid);
                                info.window_id = wid;
                            }
                            break;
                        }
                    }
                    CFRelease(windowList);
                }
            }
        }
#else // Linux X11
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) return info;

        Window focused;
        int revert_to;
        XGetInputFocus(dpy, &focused, &revert_to);

        if (focused != None && focused != PointerRoot) {
            info.window_id = static_cast<uint64_t>(focused);

            // Get window title using _NET_WM_NAME or WM_NAME
            Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
            Atom utf8_string = XInternAtom(dpy, "UTF8_STRING", False);

            // Try _NET_WM_NAME first
            Atom type;
            int format;
            unsigned long nitems, bytes_after;
            unsigned char* prop = nullptr;
            if (XGetWindowProperty(dpy, focused, net_wm_name,
                    0, 1024, False, utf8_string,
                    &type, &format, &nitems, &bytes_after, &prop) == Success
                    && prop && nitems > 0) {
                info.title = std::string(reinterpret_cast<char*>(prop), nitems);
                XFree(prop);
            } else {
                // Fallback to WM_NAME
                XTextProperty text_prop;
                if (XGetTextProperty(dpy, focused, &text_prop, XA_WM_NAME) && text_prop.value) {
                    info.title = std::string(reinterpret_cast<char*>(text_prop.value),
                                              text_prop.nitems);
                    XFree(text_prop.value);
                }
            }

            // Get WM_CLASS
            XClassHint class_hint;
            if (XGetClassHint(dpy, focused, &class_hint)) {
                if (class_hint.res_name) {
                    info.class_name = class_hint.res_name;
                    XFree(class_hint.res_name);
                }
                if (class_hint.res_class) XFree(class_hint.res_class);
            }

            // Get PID from _NET_WM_PID
            Atom net_wm_pid = XInternAtom(dpy, "_NET_WM_PID", False);
            Atom cardinal = XInternAtom(dpy, "CARDINAL", False);
            if (XGetWindowProperty(dpy, focused, net_wm_pid,
                    0, 1, False, cardinal,
                    &type, &format, &nitems, &bytes_after, &prop) == Success
                    && prop && nitems > 0) {
                info.pid = *reinterpret_cast<unsigned long*>(prop);
                XFree(prop);

                // Get process name from /proc
                std::string proc_path = "/proc/" + std::to_string(info.pid) + "/comm";
                FILE* f = fopen(proc_path.c_str(), "r");
                if (f) {
                    char buf[256] = {};
                    if (fgets(buf, sizeof(buf), f)) {
                        // Remove trailing newline
                        size_t len = strlen(buf);
                        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
                        info.process_name = buf;
                    }
                    fclose(f);
                }
            }
        }
        XCloseDisplay(dpy);
#endif
        return info;
    }
};

//----------------------------------------------------------------------------
// Mouse Event Simulator
//----------------------------------------------------------------------------
class MouseSimulator {
public:
    static MouseSimulator& instance() {
        static MouseSimulator inst;
        return inst;
    }

    // Simulate a mouse event (move, button, wheel)
    void simulate(const common::MouseEvent& event) {
        int32_t mask = event.mask;
        spdlog::debug("MouseSim: mask=0x{:X} x={} y={}", mask, event.x, event.y);

        if (mask == common::MouseEvent::TYPE_MOVE) {
            simulate_move(event.x, event.y);
            return;
        }

        if (mask == common::MouseEvent::TYPE_WHEEL) {
            simulate_wheel(event.x, event.y); // x = delta, y = horizontal delta
            return;
        }

        // Button events
        int button = 0;
        if (mask & common::MouseEvent::BUTTON_LEFT)   button = 1;
        if (mask & common::MouseEvent::BUTTON_RIGHT)  button = 2;
        if (mask & common::MouseEvent::BUTTON_WHEEL)  button = 3;
        if (mask & common::MouseEvent::BUTTON_BACK)   button = 4;
        if (mask & common::MouseEvent::BUTTON_FORWARD) button = 5;

        bool down = (mask & 0xF0) == common::MouseEvent::TYPE_DOWN;
        bool up   = (mask & 0xF0) == common::MouseEvent::TYPE_UP;

        if (down || up) {
            simulate_button(button, down);
        }
    }

    // Move mouse to absolute position
    void simulate_move(int32_t x, int32_t y) {
#ifdef _WIN32
        double screen_width = static_cast<double>(GetSystemMetrics(SM_CXSCREEN));
        double screen_height = static_cast<double>(GetSystemMetrics(SM_CYSCREEN));

        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
        input.mi.dx = static_cast<LONG>((x / screen_width) * 65535.0);
        input.mi.dy = static_cast<LONG>((y / screen_height) * 65535.0);
        SendInput(1, &input, sizeof(INPUT));

        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
        input.mi.dx = static_cast<LONG>((x / screen_width) * 65535.0);
        input.mi.dy = static_cast<LONG>((y / screen_height) * 65535.0);
        SendInput(1, &input, sizeof(INPUT));
#elif defined(__APPLE__)
        CGPoint pt = CGPointMake(static_cast<CGFloat>(x), static_cast<CGFloat>(y));
        CGEventRef moveEvent = CGEventCreateMouseEvent(
            nullptr, kCGEventMouseMoved, pt, kCGMouseButtonLeft);
        CGEventPost(kCGHIDEventTap, moveEvent);
        CFRelease(moveEvent);
        // Send twice for reliability
        moveEvent = CGEventCreateMouseEvent(
            nullptr, kCGEventMouseMoved, pt, kCGMouseButtonLeft);
        CGEventPost(kCGHIDEventTap, moveEvent);
        CFRelease(moveEvent);
#else // Linux X11
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) return;

        Window root = DefaultRootWindow(dpy);
        XTestFakeMotionEvent(dpy, -1, x, y, CurrentTime);
        XSync(dpy, False);
        // Some WMs need a warp for reliability
        XWarpPointer(dpy, None, root, 0, 0, 0, 0, x, y);
        XFlush(dpy);
        XCloseDisplay(dpy);
#endif
    }

    // Move mouse relatively (for gaming mode)
    void simulate_relative_move(int32_t dx, int32_t dy) {
#ifdef _WIN32
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_MOVE; // relative
        input.mi.dx = dx;
        input.mi.dy = dy;
        SendInput(1, &input, sizeof(INPUT));
#elif defined(__APPLE__)
        CGEventRef moveEvent = CGEventCreateMouseEvent(
            nullptr, kCGEventMouseMoved,
            CGPointMake(static_cast<CGFloat>(dx), static_cast<CGFloat>(dy)),
            kCGMouseButtonLeft);
        CGEventSetIntegerValueField(moveEvent, kCGMouseEventDeltaX, dx);
        CGEventSetIntegerValueField(moveEvent, kCGMouseEventDeltaY, dy);
        CGEventPost(kCGHIDEventTap, moveEvent);
        CFRelease(moveEvent);
#else // Linux
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) return;
        XTestFakeRelativeMotionEvent(dpy, dx, dy, CurrentTime);
        XSync(dpy, False);
        XCloseDisplay(dpy);
#endif
    }

    // Simulate button press/release
    void simulate_button(int button, bool down) {
#ifdef _WIN32
        DWORD flags = 0;
        switch (button) {
        case 1: flags = down ? MOUSEEVENTF_LEFTDOWN   : MOUSEEVENTF_LEFTUP;   break;
        case 2: flags = down ? MOUSEEVENTF_RIGHTDOWN  : MOUSEEVENTF_RIGHTUP;  break;
        case 3: flags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
        case 4: flags = down ? MOUSEEVENTF_XDOWN      : MOUSEEVENTF_XUP;      break;
        case 5: flags = down ? MOUSEEVENTF_XDOWN      : MOUSEEVENTF_XUP;      break;
        default: return;
        }

        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = flags;
        if (button >= 4) {
            input.mi.mouseData = (button == 4) ? XBUTTON1 : XBUTTON2;
        }
        SendInput(1, &input, sizeof(INPUT));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#elif defined(__APPLE__)
        CGEventType eventType;
        CGMouseButton cgButton;
        switch (button) {
        case 1: eventType = down ? kCGEventLeftMouseDown   : kCGEventLeftMouseUp;    cgButton = kCGMouseButtonLeft;   break;
        case 2: eventType = down ? kCGEventRightMouseDown  : kCGEventRightMouseUp;   cgButton = kCGMouseButtonRight;  break;
        case 3: eventType = down ? kCGEventOtherMouseDown  : kCGEventOtherMouseUp;   cgButton = kCGMouseButtonCenter; break;
        default: return;
        }
        CGPoint pt = CGEventGetLocation(CGEventCreate(nullptr));
        CGEventRef btnEvent = CGEventCreateMouseEvent(
            nullptr, eventType, pt, cgButton);
        CGEventPost(kCGHIDEventTap, btnEvent);
        CFRelease(btnEvent);
#else // Linux
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) return;
        int x_btn = 0;
        switch (button) {
        case 1: x_btn = 1; break;
        case 2: x_btn = 3; break;
        case 3: x_btn = 2; break;
        case 4: x_btn = 8; break;
        case 5: x_btn = 9; break;
        default: XCloseDisplay(dpy); return;
        }
        XTestFakeButtonEvent(dpy, x_btn, down, CurrentTime);
        XSync(dpy, False);
        XCloseDisplay(dpy);
#endif
    }

    // Simulate scroll wheel (vertical and horizontal)
    void simulate_wheel(int32_t vertical_delta, int32_t horizontal_delta) {
        // Vertical wheel
        if (vertical_delta != 0) {
            simulate_wheel_axis(vertical_delta, false);
        }
        // Horizontal wheel
        if (horizontal_delta != 0) {
            simulate_wheel_axis(horizontal_delta, true);
        }
    }

private:
    void simulate_wheel_axis(int32_t delta, bool horizontal) {
        int32_t clicks = delta / 120; // WHEEL_DELTA = 120
        int32_t abs_clicks = (clicks > 0) ? clicks : -clicks;
        bool direction = (clicks > 0);

        for (int32_t i = 0; i < abs_clicks; ++i) {
#ifdef _WIN32
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
            input.mi.mouseData = direction ? 120 : -120;
            SendInput(1, &input, sizeof(INPUT));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
#elif defined(__APPLE__)
            CGEventRef scrollEvent = CGEventCreateScrollWheelEvent(
                nullptr,
                horizontal ? kCGScrollEventUnitPixel : kCGScrollEventUnitLine,
                horizontal ? 2 : 1, // wheel count
                horizontal ? static_cast<int32_t>(delta / 120) : 0,
                horizontal ? 0 : static_cast<int32_t>(delta / 120));
            CGEventPost(kCGHIDEventTap, scrollEvent);
            CFRelease(scrollEvent);
            break; // CG sends the full delta at once
#else // Linux
            Display* dpy = XOpenDisplay(nullptr);
            if (!dpy) return;
            int btn = horizontal ? (direction ? 7 : 6) : (direction ? 5 : 4);
            XTestFakeButtonEvent(dpy, btn, true, CurrentTime);
            XSync(dpy, False);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            XTestFakeButtonEvent(dpy, btn, false, CurrentTime);
            XSync(dpy, False);
            XCloseDisplay(dpy);
#endif
        }
    }
};

//----------------------------------------------------------------------------
// Keyboard Event Simulator
//----------------------------------------------------------------------------
class KeyboardSimulator {
public:
    static KeyboardSimulator& instance() {
        static KeyboardSimulator inst;
        return inst;
    }

    // Simulate a key press or release
    void simulate_key(uint32_t universal_keycode, bool down) {
        auto& mapper = KeycodeMapper::instance();
        UniversalKey uk = static_cast<UniversalKey>(universal_keycode);

        // Apply remapping
        uk = mapper.get_remap(uk);

        uint32_t platform_code = mapper.universal_to_platform(uk);
        if (platform_code == 0) {
            spdlog::warn("KeyboardSim: unknown universal key 0x{:04X}", universal_keycode);
            return;
        }

        spdlog::trace("KeyboardSim: key 0x{:X} {} platform=0x{:X}",
            universal_keycode, down ? "DOWN" : "UP", platform_code);

        // Track modifier state
        ModifierStateTracker::instance().on_key(uk, down);

        do_simulate_key(platform_code, down);
    }

    // Simulate text input (IME-aware via platform clipboard/synthesized keystrokes)
    void simulate_text(const std::string& text) {
        if (text.empty()) return;
        spdlog::debug("KeyboardSim: text input \"{}\" ({} bytes)", text, text.size());

#ifdef _WIN32
        // Use SendInput with KEYBDINPUT for each character
        // For Unicode, we use KEYEVENTF_UNICODE
        size_t len = text.size();
        std::vector<INPUT> inputs(len * 2); // down + up per char

        for (size_t i = 0; i < len; ++i) {
            wchar_t wc = static_cast<wchar_t>(static_cast<unsigned char>(text[i]));

            // For ASCII, use VK approach; for non-ASCII, use Unicode
            if (wc < 128) {
                SHORT vk = VkKeyScanA(static_cast<char>(wc));
                BYTE vk_code = LOBYTE(vk);
                bool need_shift = (HIBYTE(vk) & 1) != 0;

                size_t idx = i * 2;
                if (need_shift) {
                    // Press shift
                    inputs[idx].type = INPUT_KEYBOARD;
                    inputs[idx].ki.wVk = VK_SHIFT;
                    inputs[idx].ki.dwFlags = 0;
                    idx = i * 2;
                }

                inputs[idx].type = INPUT_KEYBOARD;
                inputs[idx].ki.wVk = vk_code;
                inputs[idx].ki.dwFlags = 0;

                inputs[idx + 1].type = INPUT_KEYBOARD;
                inputs[idx + 1].ki.wVk = vk_code;
                inputs[idx + 1].ki.dwFlags = KEYEVENTF_KEYUP;

                if (need_shift) {
                    // Release shift after
                }
            } else {
                // Unicode
                size_t idx = i * 2;
                inputs[idx].type = INPUT_KEYBOARD;
                inputs[idx].ki.wVk = 0;
                inputs[idx].ki.wScan = wc;
                inputs[idx].ki.dwFlags = KEYEVENTF_UNICODE;

                inputs[idx + 1].type = INPUT_KEYBOARD;
                inputs[idx + 1].ki.wVk = 0;
                inputs[idx + 1].ki.wScan = wc;
                inputs[idx + 1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            }
        }
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
#elif defined(__APPLE__)
        // Use CGEventPost for each character
        UniChar chars[256];
        CFStringRef str = CFStringCreateWithBytes(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8*>(text.data()),
            text.size(),
            kCFStringEncodingUTF8,
            false);
        if (str) {
            CFIndex len = CFStringGetLength(str);
            for (CFIndex i = 0; i < len && i < 256; ++i) {
                chars[i] = CFStringGetCharacterAtIndex(str, i);
            }

            CGEventRef keyDown = CGEventCreateKeyboardEvent(nullptr, 0, true);
            CGEventRef keyUp = CGEventCreateKeyboardEvent(nullptr, 0, false);

            if (keyDown && keyUp) {
                CGEventKeyboardSetUnicodeString(keyDown, std::min(len, CFIndex(256)), chars);
                CGEventKeyboardSetUnicodeString(keyUp, std::min(len, CFIndex(256)), chars);
                CGEventPost(kCGHIDEventTap, keyDown);
                CGEventPost(kCGHIDEventTap, keyUp);
            }

            if (keyDown) CFRelease(keyDown);
            if (keyUp)   CFRelease(keyUp);
            CFRelease(str);
        }
#else // Linux X11
        // Use XTest fake key events via clipboard/character-by-character
        // Simpler approach: use XTest for common ASCII characters
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) return;

        for (char c : text) {
            KeySym ks = static_cast<KeySym>(static_cast<unsigned char>(c));
            KeyCode kc = XKeysymToKeycode(dpy, ks);
            if (kc != 0) {
                // Check if shift is needed
                bool needs_shift = false;
                if (c >= 'A' && c <= 'Z') needs_shift = true;
                if (strchr("~!@#$%^&*()_+{}|:\"<>?", c)) needs_shift = true;

                if (needs_shift) {
                    XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), True, CurrentTime);
                }

                XTestFakeKeyEvent(dpy, kc, True, CurrentTime);
                XTestFakeKeyEvent(dpy, kc, False, CurrentTime);

                if (needs_shift) {
                    XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), False, CurrentTime);
                }

                XSync(dpy, False);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        XCloseDisplay(dpy);
#endif
    }

    // Release all held keys (for disconnect safety)
    void release_all_held_keys() {
        ModifierStateTracker::instance().release_all_modifiers(
            [this](UniversalKey uk, bool down) { simulate_key(static_cast<uint32_t>(uk), down); });
    }

private:
    void do_simulate_key(uint32_t platform_code, bool down) {
#ifdef _WIN32
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = static_cast<WORD>(platform_code);
        input.ki.wScan = static_cast<WORD>(MapVirtualKey(platform_code, MAPVK_VK_TO_VSC));
        if (!down) {
            input.ki.dwFlags = KEYEVENTF_KEYUP;
        }
        // Extended keys
        switch (platform_code) {
        case VK_RCONTROL: case VK_RMENU: case VK_INSERT:
        case VK_HOME: case VK_PRIOR: case VK_DELETE:
        case VK_END: case VK_NEXT: case VK_LEFT:
        case VK_RIGHT: case VK_UP: case VK_DOWN:
        case VK_DIVIDE: case VK_SNAPSHOT: case VK_NUMLOCK:
        case VK_RWIN: case VK_LWIN:
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            break;
        }
        SendInput(1, &input, sizeof(INPUT));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#elif defined(__APPLE__)
        CGEventRef keyEvent = CGEventCreateKeyboardEvent(nullptr,
            static_cast<CGKeyCode>(platform_code), down);
        if (keyEvent) {
            CGEventPost(kCGHIDEventTap, keyEvent);
            CFRelease(keyEvent);
        }
#else // Linux
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) return;
        XTestFakeKeyEvent(dpy, static_cast<KeyCode>(platform_code), down ? True : False, CurrentTime);
        XSync(dpy, False);
        XCloseDisplay(dpy);
#endif
    }
};

//----------------------------------------------------------------------------
// Relative Mouse Mode: tracks accumulated deltas for gaming-like input
//----------------------------------------------------------------------------
class RelativeMouseMode {
public:
    RelativeMouseMode() = default;

    void enable() {
        std::lock_guard lk(mutex_);
        enabled_ = true;
        accum_x_ = 0;
        accum_y_ = 0;
        spdlog::info("Relative mouse mode enabled");
    }

    void disable() {
        std::lock_guard lk(mutex_);
        enabled_ = false;
        accum_x_ = 0;
        accum_y_ = 0;
        spdlog::info("Relative mouse mode disabled");
    }

    bool is_enabled() const {
        std::lock_guard lk(mutex_);
        return enabled_;
    }

    // Accumulate relative deltas
    void add_delta(int32_t dx, int32_t dy) {
        if (!is_enabled()) return;
        std::lock_guard lk(mutex_);
        accum_x_ += dx;
        accum_y_ += dy;
    }

    // Flush accumulated deltas and simulate
    void flush() {
        std::lock_guard lk(mutex_);
        if (!enabled_ || (accum_x_ == 0 && accum_y_ == 0)) return;

        MouseSimulator::instance().simulate_relative_move(accum_x_, accum_y_);
        accum_x_ = 0;
        accum_y_ = 0;
    }

    // Get accumulated values
    std::pair<int32_t, int32_t> get_accumulated() const {
        std::lock_guard lk(mutex_);
        return {accum_x_, accum_y_};
    }

private:
    mutable std::mutex mutex_;
    bool enabled_ = false;
    int32_t accum_x_ = 0;
    int32_t accum_y_ = 0;
};

//----------------------------------------------------------------------------
// Scancode Normalizer: converts raw scan codes to consistent internal format
//----------------------------------------------------------------------------
class ScancodeNormalizer {
public:
    // Normalize a platform-dependent scancode to our universal format
    static UniversalKey normalize(uint32_t raw_scan, bool extended) {
        // Extended key prefix handling
        uint32_t effective = raw_scan;
        if (extended && raw_scan < 0x100) {
            effective |= 0xE000;
        }

        // Special handling for pause/break which sends a weird sequence
        if (raw_scan == 0xE11D45) return UniversalKey::PAUSE;

        return KeycodeMapper::instance().platform_to_universal(effective);
    }

    // Convert a universal key back to a tuple of (scan_code, is_extended)
    static std::pair<uint32_t, bool> denormalize(UniversalKey uk) {
        uint32_t code = KeycodeMapper::instance().universal_to_platform(uk);
        bool extended = (code & 0xE000) == 0xE000;
        if (extended) code &= 0xFF;
        return {code, extended};
    }
};

} // namespace input_detail

//============================================================================
// InputService — unified implementation with per-subtype threading
//============================================================================

// The InputService extends GenericService. We use the same class for
// cursor_pos, cursor, and window_focus subtypes, with behavior determined
// by the service name.

InputService::InputService(const std::string& name_suffix)
    : GenericService(name_suffix) {}

std::unique_ptr<InputService> InputService::create_cursor() {
    return std::make_unique<InputService>(NAME_CURSOR);
}

std::unique_ptr<InputService> InputService::create_position() {
    return std::make_unique<InputService>(NAME_POS);
}

std::unique_ptr<InputService> InputService::create_window_focus() {
    return std::make_unique<InputService>(NAME_WINDOW_FOCUS);
}

void InputService::start() {
    if (running_.exchange(true)) {
        spdlog::warn("InputService {} already running", name_);
        return;
    }

    spdlog::info("InputService {} starting", name_);

    // Determine polling interval based on service type
    auto interval = std::chrono::milliseconds(50);
    if (name_ == NAME_WINDOW_FOCUS) {
        interval = std::chrono::milliseconds(200);
    }

    // Launch polling thread
    poll_thread_ = std::thread([this, interval]() {
        spdlog::debug("InputService {} poll thread started ({}ms interval)",
            name_, interval.count());

        // State cache for detecting changes
        int32_t last_x = -1, last_y = -1;
        common::CursorData last_cursor_data;
        std::string last_window_title;

        while (running_) {
            auto start_time = std::chrono::steady_clock::now();

            // ===== Cursor Position =====
            if (name_ == NAME_POS) {
                input_detail::CursorPositionTracker tracker;
                auto state = tracker.poll();
                if (state.x != last_x || state.y != last_y) {
                    last_x = state.x;
                    last_y = state.y;
                    // Build position data and notify subscribers
                    notify_subscribers_position(state.x, state.y, state.display_idx);
                }
            }

            // ===== Cursor Shape/Image =====
            if (name_ == NAME_CURSOR) {
                auto& capturer = input_detail::CursorShapeCapturer::instance();
                if (capturer.has_changed(last_cursor_data)) {
                    last_cursor_data = capturer.capture();
                    notify_subscribers_cursor(last_cursor_data);
                }
            }

            // ===== Window Focus =====
            if (name_ == NAME_WINDOW_FOCUS) {
                auto& focus_tracker = input_detail::WindowFocusTracker::instance();
                auto info = focus_tracker.poll();
                if (info.title != last_window_title) {
                    last_window_title = info.title;
                    notify_subscribers_window_focus(info);
                }
            }

            // Sleep remaining time
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed < interval) {
                std::this_thread::sleep_for(interval - elapsed);
            }
        }

        spdlog::debug("InputService {} poll thread stopped", name_);
    });
}

void InputService::stop() {
    if (!running_.exchange(false)) return;

    spdlog::info("InputService {} stopping", name_);

    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }
}

//----------------------------------------------------------------------------
// Notification helpers: encode and send data to subscribers
//----------------------------------------------------------------------------

void InputService::notify_subscribers_position(
    int32_t x, int32_t y, int32_t display_idx) {

    std::lock_guard lk(mutex_);
    if (subscribers_.empty()) return;

    // Encode: 4 bytes x + 4 bytes y + 4 bytes display_idx
    std::vector<uint8_t> data(12);
    auto* p = data.data();
    memcpy(p, &x, 4);           p += 4;
    memcpy(p, &y, 4);           p += 4;
    memcpy(p, &display_idx, 4);

    // Notify via the server (handled externally)
    spdlog::trace("InputService::cursor_pos update: ({}, {})", x, y);
    // The actual send happens through the server connection
}

void InputService::notify_subscribers_cursor(
    const common::CursorData& cursor) {

    std::lock_guard lk(mutex_);
    if (subscribers_.empty()) return;

    // Encode cursor data
    // Format: id(4) hot_x(4) hot_y(4) width(4) height(4) data_len(4) colors(data_len)
    size_t header_size = 24;
    size_t data_size = cursor.colors.size();
    std::vector<uint8_t> payload(header_size + data_size);
    auto* p = payload.data();

    uint32_t id = cursor.id;
    int32_t hot_x = cursor.hot_x;
    int32_t hot_y = cursor.hot_y;
    uint32_t w = cursor.width;
    uint32_t h = cursor.height;
    uint32_t colors_len = static_cast<uint32_t>(data_size);

    memcpy(p, &id, 4);         p += 4;
    memcpy(p, &hot_x, 4);      p += 4;
    memcpy(p, &hot_y, 4);      p += 4;
    memcpy(p, &w, 4);          p += 4;
    memcpy(p, &h, 4);          p += 4;
    memcpy(p, &colors_len, 4); p += 4;
    if (data_size > 0) {
        memcpy(p, cursor.colors.data(), data_size);
    }

    spdlog::trace("InputService::cursor update: {}x{} id={}",
        cursor.width, cursor.height, cursor.id);
}

void InputService::notify_subscribers_window_focus(
    const input_detail::WindowFocusTracker::WindowInfo& info) {

    std::lock_guard lk(mutex_);
    if (subscribers_.empty()) return;

    spdlog::debug("InputService::window_focus update: \"{}\" pid={}",
        info.title, info.pid);

    // Encode: json-like or binary encoding of window info
    // Format: title_len(4) + title + class_len(4) + class + window_id(8) + pid(4) + proc_len(4) + proc
    uint32_t title_len = static_cast<uint32_t>(info.title.size());
    uint32_t class_len = static_cast<uint32_t>(info.class_name.size());
    uint32_t proc_len  = static_cast<uint32_t>(info.process_name.size());

    size_t total = 4 + title_len + 4 + class_len + 8 + 4 + 4 + proc_len;
    std::vector<uint8_t> payload(total);
    auto* p = payload.data();

    memcpy(p, &title_len, 4); p += 4;
    if (title_len > 0) {
        memcpy(p, info.title.data(), title_len);
        p += title_len;
    }
    memcpy(p, &class_len, 4); p += 4;
    if (class_len > 0) {
        memcpy(p, info.class_name.data(), class_len);
        p += class_len;
    }
    memcpy(p, &info.window_id, 8); p += 8;
    memcpy(p, &info.pid, 4);       p += 4;
    memcpy(p, &proc_len, 4);       p += 4;
    if (proc_len > 0) {
        memcpy(p, info.process_name.data(), proc_len);
    }
}

//============================================================================
// InputServiceProxy: Global entry point for input simulation from clients
//============================================================================

class InputServiceProxy {
public:
    static InputServiceProxy& instance() {
        static InputServiceProxy inst;
        return inst;
    }

    // Handle an incoming mouse event from a client
    void handle_mouse_event(const common::MouseEvent& event) {
        spdlog::trace("InputServiceProxy::mouse_event mask=0x{:X} x={} y={}",
            event.mask, event.x, event.y);
        input_detail::MouseSimulator::instance().simulate(event);
    }

    // Handle an incoming key event from a client
    void handle_key_event(const common::KeyEvent& event) {
        // If the event has a text sequence, use text input (IME-aware)
        if (!event.sequence.empty()) {
            spdlog::trace("InputServiceProxy::key_event sequence: \"{}\"", event.sequence);
            input_detail::KeyboardSimulator::instance().simulate_text(event.sequence);
            return;
        }

        // Otherwise, simulate a key press/release
        spdlog::trace("InputServiceProxy::key_event keycode=0x{:X} down={}",
            event.keycode, event.down);

        auto& mapper = input_detail::KeycodeMapper::instance();

        // Apply remapping
        UniversalKey uk = static_cast<UniversalKey>(event.keycode);
        uk = mapper.get_remap(uk);

        // If this is a modifier key, also update the tracker
        if (mapper.is_modifier(uk)) {
            input_detail::ModifierStateTracker::instance().on_key(uk, event.down);
        }

        input_detail::KeyboardSimulator::instance().simulate_key(event.keycode, event.down);
    }

    // Handle a relative mouse delta (gaming mode)
    void handle_relative_mouse(int32_t dx, int32_t dy) {
        auto& rel = input_detail::RelativeMouseMode();
        if (rel.is_enabled()) {
            rel.add_delta(dx, dy);
            rel.flush();
        } else {
            input_detail::MouseSimulator::instance().simulate_relative_move(dx, dy);
        }
    }

    // Enable relative mouse mode
    void enable_relative_mouse() {
        input_detail::RelativeMouseMode().enable();
    }

    // Disable relative mouse mode
    void disable_relative_mouse() {
        input_detail::RelativeMouseMode().disable();
    }

    // Set cursor position (absolute)
    void set_cursor_position(int32_t x, int32_t y, int32_t display_idx) {
        // If display_idx specified, translate from monitor-local to virtual
        if (display_idx >= 0) {
            auto [vx, vy] = input_detail::MonitorGeometry::instance()
                .monitor_to_virtual(display_idx, x, y);
            x = vx;
            y = vy;
        }
        input_detail::MouseSimulator::instance().simulate_move(x, y);
    }

    // Refresh multi-monitor geometry
    void refresh_monitor_geometry() {
        input_detail::MonitorGeometry::instance().refresh();
    }

    // Get current modifier state
    uint8_t get_modifier_mask() const {
        auto mods = input_detail::ModifierStateTracker::instance().current_modifiers();
        return static_cast<uint8_t>(mods);
    }

    // Device disconnect: release all held modifiers
    void on_disconnect() {
        spdlog::info("InputServiceProxy: releasing held keys on disconnect");
        input_detail::KeyboardSimulator::instance().release_all_held_keys();
        input_detail::ModifierStateTracker::instance().reset();
    }

    // Keycode remapping
    void set_key_remap(UniversalKey from, UniversalKey to) {
        input_detail::KeycodeMapper::instance().set_remap(from, to);
        spdlog::debug("InputServiceProxy: remap key set");
    }

    void clear_key_remaps() {
        input_detail::KeycodeMapper::instance().clear_remaps();
        spdlog::debug("InputServiceProxy: all key remaps cleared");
    }

    // Text input
    void simulate_text(const std::string& text) {
        input_detail::KeyboardSimulator::instance().simulate_text(text);
    }

private:
    InputServiceProxy() = default;
};

//============================================================================
// Platform-specific initialization
//============================================================================

namespace {

// One-time initialization of input subsystems
std::once_flag input_init_flag;

void ensure_input_initialized() {
    std::call_once(input_init_flag, []() {
        spdlog::info("Initializing input subsystem...");

        // Warm up keycode mapper
        input_detail::KeycodeMapper::instance();

        // Refresh monitor geometry
        input_detail::MonitorGeometry::instance().refresh();

        spdlog::info("Input subsystem initialized");
    });
}

} // anonymous namespace

//============================================================================
// Free functions for external callers (used by Connection/session handling)
//============================================================================

void handle_client_mouse_event(const common::MouseEvent& event) {
    ensure_input_initialized();
    InputServiceProxy::instance().handle_mouse_event(event);
}

void handle_client_key_event(const common::KeyEvent& event) {
    ensure_input_initialized();
    InputServiceProxy::instance().handle_key_event(event);
}

void handle_client_text_input(const std::string& text) {
    ensure_input_initialized();
    InputServiceProxy::instance().simulate_text(text);
}

void handle_client_relative_mouse(int32_t dx, int32_t dy) {
    ensure_input_initialized();
    InputServiceProxy::instance().handle_relative_mouse(dx, dy);
}

void set_client_cursor_position(int32_t x, int32_t y, int32_t display_idx) {
    ensure_input_initialized();
    InputServiceProxy::instance().set_cursor_position(x, y, display_idx);
}

void enable_relative_mouse_mode() {
    ensure_input_initialized();
    InputServiceProxy::instance().enable_relative_mouse();
}

void disable_relative_mouse_mode() {
    ensure_input_initialized();
    InputServiceProxy::instance().disable_relative_mouse();
}

void release_all_input_on_disconnect() {
    InputServiceProxy::instance().on_disconnect();
}

void refresh_input_monitors() {
    input_detail::MonitorGeometry::instance().refresh();
}

void set_input_key_remap(uint32_t from_uk, uint32_t to_uk) {
    ensure_input_initialized();
    InputServiceProxy::instance().set_key_remap(
        static_cast<input_detail::UniversalKey>(from_uk),
        static_cast<input_detail::UniversalKey>(to_uk));
}

void clear_input_key_remaps() {
    InputServiceProxy::instance().clear_key_remaps();
}

uint8_t get_input_modifier_mask() {
    return InputServiceProxy::instance().get_modifier_mask();
}

std::pair<int32_t, int32_t> get_cursor_position() {
    ensure_input_initialized();
    input_detail::CursorPositionTracker tracker;
    return tracker.current_position();
}

common::CursorData get_cursor_data() {
    ensure_input_initialized();
    return input_detail::CursorShapeCapturer::instance().capture();
}

std::string get_window_focus_title() {
    ensure_input_initialized();
    return input_detail::WindowFocusTracker::instance().poll().title;
}

//============================================================================
// InputEventQueue: Ordered input event processing with priority
//============================================================================
namespace input_detail {

enum class InputPriority : uint8_t {
    HIGH    = 0,  // Keyboard modifiers, disconnect cleanup
    NORMAL  = 1,  // Mouse clicks, key presses
    LOW     = 2,  // Mouse moves, wheel events
};

struct QueuedInputEvent {
    InputPriority priority = InputPriority::NORMAL;
    uint64_t timestamp = 0; // monotonic ms
    std::function<void()> action;
    bool operator<(const QueuedInputEvent& other) const {
        if (priority != other.priority) {
            return static_cast<uint8_t>(priority) > static_cast<uint8_t>(other.priority);
        }
        return timestamp > other.timestamp; // earlier = higher priority in priority_queue
    }
};

class InputEventQueue {
public:
    static InputEventQueue& instance() {
        static InputEventQueue inst;
        return inst;
    }

    void start() {
        if (running_.exchange(true)) return;
        spdlog::info("InputEventQueue starting");
        worker_ = std::thread(&InputEventQueue::run, this);
    }

    void stop() {
        running_ = false;
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        spdlog::info("InputEventQueue stopped");
    }

    void enqueue(InputPriority pri, std::function<void()> action) {
        QueuedInputEvent ev;
        ev.priority = pri;
        ev.timestamp = monotonic_ms();
        ev.action = std::move(action);

        {
            std::lock_guard lk(mutex_);
            queue_.push(std::move(ev));
        }
        cv_.notify_one();
    }

    size_t pending() const {
        std::lock_guard lk(mutex_);
        return queue_.size();
    }

private:
    void run() {
        while (running_) {
            QueuedInputEvent ev;
            {
                std::unique_lock lk(mutex_);
                cv_.wait(lk, [this] { return !queue_.empty() || !running_; });
                if (!running_) break;
                if (queue_.empty()) continue;
                ev = std::move(const_cast<QueuedInputEvent&>(queue_.top()));
                queue_.pop();
            }

            if (ev.action) {
                try {
                    ev.action();
                } catch (const std::exception& e) {
                    spdlog::error("InputEventQueue: exception in action: {}", e.what());
                } catch (...) {
                    spdlog::error("InputEventQueue: unknown exception in action");
                }
            }
        }
    }

    static uint64_t monotonic_ms() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    std::atomic<bool> running_{false};
    std::priority_queue<QueuedInputEvent> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
};

//----------------------------------------------------------------------------
// Drag-and-Drop Simulator
//----------------------------------------------------------------------------
class DragSimulator {
public:
    static DragSimulator& instance() {
        static DragSimulator inst;
        return inst;
    }

    void start_drag(int32_t x, int32_t y) {
        std::lock_guard lk(mutex_);
        if (dragging_) return;

        dragging_ = true;
        drag_start_x_ = x;
        drag_start_y_ = y;

        // Press left mouse button at start position
        auto& sim = MouseSimulator::instance();
        sim.simulate_move(x, y);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        sim.simulate_button(1, true); // left button down

        spdlog::debug("Drag started at ({}, {})", x, y);
    }

    void update_drag(int32_t x, int32_t y) {
        std::lock_guard lk(mutex_);
        if (!dragging_) return;
        MouseSimulator::instance().simulate_move(x, y);
        spdlog::trace("Drag update to ({}, {})", x, y);
    }

    void end_drag(int32_t x, int32_t y) {
        std::lock_guard lk(mutex_);
        if (!dragging_) return;

        auto& sim = MouseSimulator::instance();
        sim.simulate_move(x, y);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        sim.simulate_button(1, false); // left button up

        dragging_ = false;
        spdlog::debug("Drag ended at ({}, {})", x, y);
    }

    void cancel_drag() {
        std::lock_guard lk(mutex_);
        if (!dragging_) return;

        // Move back to start
        MouseSimulator::instance().simulate_move(drag_start_x_, drag_start_y_);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        MouseSimulator::instance().simulate_button(1, false); // release

        dragging_ = false;
        spdlog::debug("Drag cancelled");
    }

    bool is_dragging() const {
        std::lock_guard lk(mutex_);
        return dragging_;
    }

private:
    mutable std::mutex mutex_;
    bool dragging_ = false;
    int32_t drag_start_x_ = 0;
    int32_t drag_start_y_ = 0;
};

//----------------------------------------------------------------------------
// Double/Triple Click Detector
//----------------------------------------------------------------------------
class MultiClickDetector {
public:
    static MultiClickDetector& instance() {
        static MultiClickDetector inst;
        return inst;
    }

    enum ClickCount {
        SINGLE = 1,
        DOUBLE = 2,
        TRIPLE = 3,
    };

    ClickCount record_click(int button, int32_t x, int32_t y) {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard lk(mutex_);

        // Check if within double-click threshold
        auto elapsed = now - last_click_time_;
        bool same_button = (button == last_button_);
        bool near_position = (std::abs(x - last_click_x_) <= click_tolerance_px_) &&
                             (std::abs(y - last_click_y_) <= click_tolerance_px_);

        if (elapsed < double_click_timeout_ && same_button && near_position) {
            click_count_++;
            if (click_count_ > 3) click_count_ = 1; // wrap
        } else {
            click_count_ = 1;
        }

        last_click_time_ = now;
        last_button_ = button;
        last_click_x_ = x;
        last_click_y_ = y;

        return static_cast<ClickCount>(click_count_);
    }

    void reset() {
        std::lock_guard lk(mutex_);
        click_count_ = 0;
    }

    void set_double_click_timeout(std::chrono::milliseconds ms) {
        std::lock_guard lk(mutex_);
        double_click_timeout_ = ms;
    }

    void set_click_tolerance(int32_t pixels) {
        std::lock_guard lk(mutex_);
        click_tolerance_px_ = pixels;
    }

private:
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point last_click_time_;
    int last_button_ = 0;
    int32_t last_click_x_ = 0;
    int32_t last_click_y_ = 0;
    int click_count_ = 0;
    std::chrono::milliseconds double_click_timeout_{500}; // typical OS default
    int32_t click_tolerance_px_ = 4;
};

//----------------------------------------------------------------------------
// Auto-Repeat Key Handler
//----------------------------------------------------------------------------
class AutoRepeatHandler {
public:
    static AutoRepeatHandler& instance() {
        static AutoRepeatHandler inst;
        return inst;
    }

    // Called when a key is pressed
    void key_down(uint32_t keycode) {
        std::lock_guard lk(mutex_);
        auto& state = key_states_[keycode];
        state.held = true;
        state.press_time = std::chrono::steady_clock::now();
        state.repeat_count = 0;
    }

    // Called when a key is released
    void key_up(uint32_t keycode) {
        std::lock_guard lk(mutex_);
        auto it = key_states_.find(keycode);
        if (it != key_states_.end()) {
            it->second.held = false;
        }
    }

    // Check if a repeat event should be generated for a held key
    bool should_repeat(uint32_t keycode, int& out_repeat_count) {
        std::lock_guard lk(mutex_);
        auto it = key_states_.find(keycode);
        if (it == key_states_.end() || !it->second.held) return false;

        auto elapsed = std::chrono::steady_clock::now() - it->second.press_time;
        auto initial_delay = std::chrono::milliseconds(500); // typical OS: 500ms
        auto repeat_rate  = std::chrono::milliseconds(30);   // typical OS: ~33/s

        if (elapsed < initial_delay) return false;

        int expected_repeats = static_cast<int>(
            (elapsed - initial_delay).count() / repeat_rate.count());

        if (expected_repeats > it->second.repeat_count) {
            it->second.repeat_count = expected_repeats;
            out_repeat_count = expected_repeats;
            return true;
        }

        return false;
    }

    // Release all held keys (called on disconnect or focus loss)
    void release_all() {
        std::lock_guard lk(mutex_);
        for (auto& [keycode, state] : key_states_) {
            state.held = false;
            state.repeat_count = 0;
        }
    }

    // Set custom repeat parameters
    void set_repeat_params(std::chrono::milliseconds initial_delay,
                           std::chrono::milliseconds repeat_rate_ms) {
        std::lock_guard lk(mutex_);
        initial_delay_ = initial_delay;
        repeat_rate_ = repeat_rate_ms;
    }

private:
    struct KeyState {
        bool held = false;
        std::chrono::steady_clock::time_point press_time;
        int repeat_count = 0;
    };

    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, KeyState> key_states_;
    std::chrono::milliseconds initial_delay_{500};
    std::chrono::milliseconds repeat_rate_{30};
};

//----------------------------------------------------------------------------
// Input Latency Tracker
//----------------------------------------------------------------------------
class InputLatencyTracker {
public:
    static InputLatencyTracker& instance() {
        static InputLatencyTracker inst;
        return inst;
    }

    void record_receive(uint64_t client_timestamp) {
        std::lock_guard lk(mutex_);
        auto now = monotonic_us();
        auto latency = now - client_timestamp;
        total_samples_++;
        total_latency_ += latency;
        if (latency > max_latency_) max_latency_ = latency;
        if (latency < min_latency_) min_latency_ = latency;

        // Keep sliding window
        recent_.push_back(latency);
        if (recent_.size() > recent_window_) {
            sum_recent_ -= recent_.front();
            recent_.pop_front();
        }
        sum_recent_ += latency;
    }

    void record_simulate(uint64_t event_id) {
        std::lock_guard lk(mutex_);
        last_simulate_us_ = monotonic_us();
        simulate_count_++;
    }

    double average_latency_ms() const {
        std::lock_guard lk(mutex_);
        if (total_samples_ == 0) return 0.0;
        return static_cast<double>(total_latency_) / total_samples_ / 1000.0;
    }

    double recent_latency_ms() const {
        std::lock_guard lk(mutex_);
        if (recent_.empty()) return 0.0;
        return static_cast<double>(sum_recent_) / recent_.size() / 1000.0;
    }

    uint64_t max_latency_us() const {
        std::lock_guard lk(mutex_);
        return max_latency_;
    }

    uint64_t min_latency_us() const {
        std::lock_guard lk(mutex_);
        return min_latency_;
    }

    uint64_t total_events() const {
        std::lock_guard lk(mutex_);
        return total_samples_;
    }

    void reset() {
        std::lock_guard lk(mutex_);
        total_samples_ = 0;
        total_latency_ = 0;
        max_latency_ = 0;
        min_latency_ = UINT64_MAX;
        recent_.clear();
        sum_recent_ = 0;
        simulate_count_ = 0;
    }

private:
    static uint64_t monotonic_us() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    mutable std::mutex mutex_;
    uint64_t total_samples_ = 0;
    uint64_t total_latency_ = 0;
    uint64_t max_latency_ = 0;
    uint64_t min_latency_ = UINT64_MAX;
    uint64_t simulate_count_ = 0;
    uint64_t last_simulate_us_ = 0;
    std::deque<uint64_t> recent_;
    uint64_t sum_recent_ = 0;
    static constexpr size_t recent_window_ = 100;
};

//----------------------------------------------------------------------------
// Input Validator: filters invalid/spam input
//----------------------------------------------------------------------------
class InputValidator {
public:
    static InputValidator& instance() {
        static InputValidator inst;
        return inst;
    }

    // Check if a mouse event is valid (within screen bounds, reasonable delta)
    bool validate_mouse_event(const common::MouseEvent& event) {
        int32_t screen_w = 65536; // reasonable upper bound for virtual desktop
        int32_t screen_h = 65536;

        if (event.x < -screen_w || event.x > screen_w * 2) {
            spdlog::warn("InputValidator: mouse X out of bounds: {}", event.x);
            return false;
        }
        if (event.y < -screen_h || event.y > screen_h * 2) {
            spdlog::warn("InputValidator: mouse Y out of bounds: {}", event.y);
            return false;
        }

        // Rate limiting for mouse move events
        if (event.mask == common::MouseEvent::TYPE_MOVE) {
            return check_rate_limit("mouse_move", 200); // max 200/s
        }

        return true;
    }

    // Check if a key event is valid
    bool validate_key_event(const common::KeyEvent& event) {
        if (event.keycode == 0 && event.sequence.empty()) {
            spdlog::warn("InputValidator: empty key event");
            return false;
        }

        // Rate limiting for key events
        return check_rate_limit("key_event", 100); // max 100/s
    }

    // Check bounds for cursor position
    bool validate_cursor_position(int32_t x, int32_t y) {
        int32_t max_dim = 65536;
        return (x >= -max_dim && x <= max_dim * 2 &&
                y >= -max_dim && y <= max_dim * 2);
    }

    // Configure rate limit for a category
    void set_rate_limit(const std::string& category, uint32_t max_per_second) {
        std::lock_guard lk(mutex_);
        rate_limits_[category] = max_per_second;
    }

private:
    bool check_rate_limit(const std::string& category, uint32_t default_max) {
        std::lock_guard lk(mutex_);
        auto now = std::chrono::steady_clock::now();
        auto& tracker = rate_trackers_[category];

        // Reset window if needed
        if (now - tracker.window_start > std::chrono::seconds(1)) {
            tracker.window_start = now;
            tracker.count = 0;
        }

        tracker.count++;
        uint32_t limit = rate_limits_.count(category) ? rate_limits_[category] : default_max;
        return tracker.count <= limit;
    }

    struct RateTracker {
        std::chrono::steady_clock::time_point window_start = std::chrono::steady_clock::now();
        uint32_t count = 0;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, RateTracker> rate_trackers_;
    std::unordered_map<std::string, uint32_t> rate_limits_;
};

//----------------------------------------------------------------------------
// Cursor Clipping Manager
//----------------------------------------------------------------------------
class CursorClipManager {
public:
    static CursorClipManager& instance() {
        static CursorClipManager inst;
        return inst;
    }

    // Clip cursor to a rectangle on a specific display
    void apply_clip(int32_t display_idx, int32_t x, int32_t y, int32_t w, int32_t h) {
        std::lock_guard lk(mutex_);

        int32_t vx = x, vy = y;
        if (display_idx >= 0) {
            auto [virt_x, virt_y] = MonitorGeometry::instance()
                .monitor_to_virtual(display_idx, x, y);
            vx = virt_x;
            vy = virt_y;
        }

        clip_active_ = true;
        clip_rect_ = {vx, vy, vx + w, vy + h};

#ifdef _WIN32
        RECT r = {vx, vy, vx + w, vy + h};
        ClipCursor(&r);
#elif defined(__APPLE__)
        // macOS: CGCursorIsVisible + CGAssociateMouseAndMouseCursorPosition
        // No direct cursor clipping API on macOS; use event tap or DDHidLib
        // This is a best-effort: we use CGWarpMouseCursorPosition to keep in bounds
        CGPoint pt = CGEventGetLocation(CGEventCreate(nullptr));
        if (pt.x < vx) pt.x = vx;
        if (pt.y < vy) pt.y = vy;
        if (pt.x > vx + w) pt.x = vx + w;
        if (pt.y > vy + h) pt.y = vy + h;
        CGWarpMouseCursorPosition(pt);
#else // Linux
        Display* dpy = XOpenDisplay(nullptr);
        if (dpy) {
            Window root = DefaultRootWindow(dpy);
            // Use XGrabPointer for confinement
            int result = XGrabPointer(dpy, root, True,
                PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
                GrabModeAsync, GrabModeAsync, root, None, CurrentTime);
            if (result != GrabSuccess) {
                spdlog::warn("CursorClip: XGrabPointer failed with code {}", result);
            }
            XCloseDisplay(dpy);
        }
#endif
        spdlog::debug("Cursor clip applied: [{},{}]-[{}x{}]", vx, vy, w, h);
    }

    void clear_clip() {
        std::lock_guard lk(mutex_);
        if (!clip_active_) return;
        clip_active_ = false;

#ifdef _WIN32
        ClipCursor(nullptr);
#else // Linux
        Display* dpy = XOpenDisplay(nullptr);
        if (dpy) {
            XUngrabPointer(dpy, CurrentTime);
            XCloseDisplay(dpy);
        }
#endif
        spdlog::debug("Cursor clip cleared");
    }

    bool is_clipped() const {
        std::lock_guard lk(mutex_);
        return clip_active_;
    }

    // Update clip position on monitor geometry change
    void refresh_clip() {
        std::lock_guard lk(mutex_);
        if (clip_active_) {
#ifdef _WIN32
            RECT r = {clip_rect_[0], clip_rect_[1], clip_rect_[2], clip_rect_[3]};
            ClipCursor(&r);
#endif
        }
    }

private:
    mutable std::mutex mutex_;
    bool clip_active_ = false;
    int32_t clip_rect_[4] = {};
};

//----------------------------------------------------------------------------
// Media/Extended Key Mapping Tables
//----------------------------------------------------------------------------
class MediaKeyMapper {
public:
    static MediaKeyMapper& instance() {
        static MediaKeyMapper inst;
        return inst;
    }

    // Map media key names to universal key codes
    UniversalKey map_media(const std::string& name) const {
        auto it = media_keys_.find(name);
        return (it != media_keys_.end()) ? it->second : UniversalKey::NONE;
    }

    std::string unmap_media(UniversalKey key) const {
        for (auto& [name, uk] : media_keys_) {
            if (uk == key) return name;
        }
        return "";
    }

private:
    MediaKeyMapper() {
        media_keys_["volume_mute"]       = static_cast<UniversalKey>(0xE020);
        media_keys_["volume_down"]       = static_cast<UniversalKey>(0xE02E);
        media_keys_["volume_up"]         = static_cast<UniversalKey>(0xE030);
        media_keys_["media_next"]        = static_cast<UniversalKey>(0xE019);
        media_keys_["media_prev"]        = static_cast<UniversalKey>(0xE010);
        media_keys_["media_stop"]        = static_cast<UniversalKey>(0xE024);
        media_keys_["media_play_pause"]  = static_cast<UniversalKey>(0xE022);
        media_keys_["browser_back"]      = static_cast<UniversalKey>(0xE06A);
        media_keys_["browser_forward"]   = static_cast<UniversalKey>(0xE069);
        media_keys_["browser_refresh"]   = static_cast<UniversalKey>(0xE067);
        media_keys_["browser_home"]      = static_cast<UniversalKey>(0xE032);
        media_keys_["browser_search"]    = static_cast<UniversalKey>(0xE065);
        media_keys_["browser_favorites"] = static_cast<UniversalKey>(0xE066);
        media_keys_["launch_mail"]       = static_cast<UniversalKey>(0xE06C);
        media_keys_["launch_calc"]       = static_cast<UniversalKey>(0xE021);
        media_keys_["launch_media"]      = static_cast<UniversalKey>(0xE06D);
        media_keys_["app_1"]             = static_cast<UniversalKey>(0xE06B);
        media_keys_["app_2"]             = static_cast<UniversalKey>(0xE021);
        media_keys_["sleep"]             = static_cast<UniversalKey>(0xE05F);
        media_keys_["wake"]              = static_cast<UniversalKey>(0xE063);
        media_keys_["power"]             = static_cast<UniversalKey>(0xE05E);
    }

    std::unordered_map<std::string, UniversalKey> media_keys_;
};

} // namespace input_detail

//============================================================================
// Additional free functions exposed to server/connection layer
//============================================================================

void input_event_queue_start() {
    input_detail::InputEventQueue::instance().start();
}

void input_event_queue_stop() {
    input_detail::InputEventQueue::instance().stop();
}

void input_drag_start(int32_t x, int32_t y) {
    ensure_input_initialized();
    input_detail::DragSimulator::instance().start_drag(x, y);
}

void input_drag_update(int32_t x, int32_t y) {
    input_detail::DragSimulator::instance().update_drag(x, y);
}

void input_drag_end(int32_t x, int32_t y) {
    input_detail::DragSimulator::instance().end_drag(x, y);
}

void input_drag_cancel() {
    input_detail::DragSimulator::instance().cancel_drag();
}

bool input_is_dragging() {
    return input_detail::DragSimulator::instance().is_dragging();
}

int input_record_multi_click(int button, int32_t x, int32_t y) {
    auto count = input_detail::MultiClickDetector::instance()
        .record_click(button, x, y);
    return static_cast<int>(count);
}

void input_reset_multi_click() {
    input_detail::MultiClickDetector::instance().reset();
}

void input_key_auto_repeat_down(uint32_t keycode) {
    input_detail::AutoRepeatHandler::instance().key_down(keycode);
}

void input_key_auto_repeat_up(uint32_t keycode) {
    input_detail::AutoRepeatHandler::instance().key_up(keycode);
}

bool input_should_auto_repeat(uint32_t keycode, int& repeat_count) {
    return input_detail::AutoRepeatHandler::instance().should_repeat(keycode, repeat_count);
}

void input_release_all_held() {
    input_detail::AutoRepeatHandler::instance().release_all();
}

void input_record_latency(uint64_t client_timestamp_us) {
    input_detail::InputLatencyTracker::instance().record_receive(client_timestamp_us);
}

double input_average_latency_ms() {
    return input_detail::InputLatencyTracker::instance().average_latency_ms();
}

double input_recent_latency_ms() {
    return input_detail::InputLatencyTracker::instance().recent_latency_ms();
}

uint64_t input_total_events() {
    return input_detail::InputLatencyTracker::instance().total_events();
}

void input_reset_latency_stats() {
    input_detail::InputLatencyTracker::instance().reset();
}

bool input_validate_mouse_event(const common::MouseEvent& event) {
    return input_detail::InputValidator::instance().validate_mouse_event(event);
}

bool input_validate_key_event(const common::KeyEvent& event) {
    return input_detail::InputValidator::instance().validate_key_event(event);
}

void input_set_rate_limit(const std::string& category, uint32_t max_per_sec) {
    input_detail::InputValidator::instance().set_rate_limit(category, max_per_sec);
}

void input_clip_cursor(int32_t display_idx, int32_t x, int32_t y,
                        int32_t w, int32_t h) {
    input_detail::CursorClipManager::instance().apply_clip(display_idx, x, y, w, h);
}

void input_unclip_cursor() {
    input_detail::CursorClipManager::instance().clear_clip();
}

bool input_is_cursor_clipped() {
    return input_detail::CursorClipManager::instance().is_clipped();
}

void input_refresh_clip() {
    input_detail::CursorClipManager::instance().refresh_clip();
}

uint32_t input_map_media_key(const std::string& name) {
    return static_cast<uint32_t>(
        input_detail::MediaKeyMapper::instance().map_media(name));
}

std::string input_unmap_media_key(uint32_t key) {
    return input_detail::MediaKeyMapper::instance().unmap_media(
        static_cast<input_detail::UniversalKey>(key));
}

} // namespace cppdesk::server
