#include "enigo/enigo.hpp"
#include <spdlog/spdlog.h>
#include <cstring>
#include <algorithm>
#include <set>
#include <thread>
#include <regex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winuser.h>
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <unistd.h>
#endif

namespace enigo {

uint32_t ENIGO_INPUT_EXTRA_VALUE = 0;

// ====== Platform Key Maps ======

#ifdef _WIN32
static std::map<Key, uint32_t> key_to_vk = {
    {Key::Alt, VK_MENU}, {Key::AltGr, VK_RMENU},
    {Key::Backspace, VK_BACK}, {Key::CapsLock, VK_CAPITAL},
    {Key::Control, VK_CONTROL}, {Key::Delete, VK_DELETE},
    {Key::DownArrow, VK_DOWN}, {Key::End, VK_END},
    {Key::Escape, VK_ESCAPE},
    {Key::F1, VK_F1}, {Key::F2, VK_F2}, {Key::F3, VK_F3}, {Key::F4, VK_F4},
    {Key::F5, VK_F5}, {Key::F6, VK_F6}, {Key::F7, VK_F7}, {Key::F8, VK_F8},
    {Key::F9, VK_F9}, {Key::F10, VK_F10}, {Key::F11, VK_F11}, {Key::F12, VK_F12},
    {Key::F13, VK_F13}, {Key::F14, VK_F14}, {Key::F15, VK_F15},
    {Key::F16, VK_F16}, {Key::F17, VK_F17}, {Key::F18, VK_F18},
    {Key::F19, VK_F19}, {Key::F20, VK_F20},
    {Key::F21, VK_F21}, {Key::F22, VK_F22}, {Key::F23, VK_F23}, {Key::F24, VK_F24},
    {Key::Home, VK_HOME}, {Key::Insert, VK_INSERT},
    {Key::LeftArrow, VK_LEFT}, {Key::Meta, VK_LWIN},
    {Key::NumLock, VK_NUMLOCK}, {Key::PageDown, VK_NEXT},
    {Key::PageUp, VK_PRIOR}, {Key::Return, VK_RETURN},
    {Key::RightArrow, VK_RIGHT}, {Key::ScrollLock, VK_SCROLL},
    {Key::Shift, VK_SHIFT}, {Key::Space, VK_SPACE}, {Key::Tab, VK_TAB},
    {Key::UpArrow, VK_UP}, {Key::PrintScreen, VK_SNAPSHOT},
    {Key::Pause, VK_PAUSE}, {Key::Menu, VK_APPS},
    {Key::Numpad0, VK_NUMPAD0}, {Key::Numpad1, VK_NUMPAD1},
    {Key::Numpad2, VK_NUMPAD2}, {Key::Numpad3, VK_NUMPAD3},
    {Key::Numpad4, VK_NUMPAD4}, {Key::Numpad5, VK_NUMPAD5},
    {Key::Numpad6, VK_NUMPAD6}, {Key::Numpad7, VK_NUMPAD7},
    {Key::Numpad8, VK_NUMPAD8}, {Key::Numpad9, VK_NUMPAD9},
    {Key::NumpadAdd, VK_ADD}, {Key::NumpadSubtract, VK_SUBTRACT},
    {Key::NumpadMultiply, VK_MULTIPLY}, {Key::NumpadDivide, VK_DIVIDE},
    {Key::NumpadDecimal, VK_DECIMAL}, {Key::NumpadEnter, VK_RETURN},
    {Key::VolumeMute, VK_VOLUME_MUTE}, {Key::VolumeDown, VK_VOLUME_DOWN},
    {Key::VolumeUp, VK_VOLUME_UP},
    {Key::BrowserBack, VK_BROWSER_BACK}, {Key::BrowserForward, VK_BROWSER_FORWARD},
    {Key::BrowserRefresh, VK_BROWSER_REFRESH}, {Key::BrowserStop, VK_BROWSER_STOP},
    {Key::BrowserSearch, VK_BROWSER_SEARCH}, {Key::BrowserFavorites, VK_BROWSER_FAVORITES},
    {Key::BrowserHome, VK_BROWSER_HOME},
    {Key::LaunchMail, VK_LAUNCH_MAIL}, {Key::LaunchMedia, VK_LAUNCH_MEDIA_SELECT},
};

static std::map<uint32_t, Key> vk_to_key;
static bool vk_map_init = []() {
    for (auto& [k, v] : key_to_vk) vk_to_key[v] = k;
    return true;
}();
#endif

// ====== Enigo Implementation (Windows) ======
#ifdef _WIN32

struct Enigo::Impl {
    std::chrono::milliseconds delay{10};
    std::set<Key> pressed_modifiers;

    void send_input(INPUT& input) {
        auto sent = SendInput(1, &input, sizeof(input));
        if (sent != 1) {
            spdlog::error("SendInput failed: {}", GetLastError());
        }
        if (delay.count() > 0) {
            std::this_thread::sleep_for(delay);
        }
    }

    uint32_t key_to_vk_inner(Key k) {
        auto it = key_to_vk.find(k);
        return it != key_to_vk.end() ? it->second : 0;
    }
};

Enigo::Enigo() : impl_(std::make_unique<Impl>()) {
    spdlog::debug("Enigo initialized (Windows)");
}

Enigo::~Enigo() = default;

bool Enigo::mouse_move_to(int32_t x, int32_t y) {
    return SetCursorPos(x, y) != 0;
}

bool Enigo::mouse_move_relative(int32_t dx, int32_t dy) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    impl_->send_input(input);
    return true;
}

bool Enigo::mouse_down(MouseButton button) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    switch (button) {
        case MouseButton::Left:   input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; break;
        case MouseButton::Right:  input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; break;
        case MouseButton::Middle: input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN; break;
        case MouseButton::Back:   input.mi.dwFlags = MOUSEEVENTF_XDOWN; input.mi.mouseData = XBUTTON1; break;
        case MouseButton::Forward:input.mi.dwFlags = MOUSEEVENTF_XDOWN; input.mi.mouseData = XBUTTON2; break;
        default: return false;
    }
    impl_->send_input(input);
    return true;
}

bool Enigo::mouse_up(MouseButton button) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    switch (button) {
        case MouseButton::Left:   input.mi.dwFlags = MOUSEEVENTF_LEFTUP; break;
        case MouseButton::Right:  input.mi.dwFlags = MOUSEEVENTF_RIGHTUP; break;
        case MouseButton::Middle: input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP; break;
        case MouseButton::Back:   input.mi.dwFlags = MOUSEEVENTF_XUP; input.mi.mouseData = XBUTTON1; break;
        case MouseButton::Forward:input.mi.dwFlags = MOUSEEVENTF_XUP; input.mi.mouseData = XBUTTON2; break;
        default: return false;
    }
    impl_->send_input(input);
    return true;
}

bool Enigo::mouse_scroll(int32_t delta, MouseAxis axis) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = (axis == MouseAxis::Horizontal) ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(delta * WHEEL_DELTA);
    impl_->send_input(input);
    return true;
}

std::pair<int32_t, int32_t> Enigo::mouse_location() {
    POINT pt;
    GetCursorPos(&pt);
    return {pt.x, pt.y};
}

bool Enigo::key_down(Key key) {
    auto vk = impl_->key_to_vk_inner(key);
    if (vk == 0 && key >= Key::Layout) {
        // Layout character: use Unicode input
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = 0;
        input.ki.wScan = static_cast<WORD>(static_cast<uint32_t>(key) - static_cast<uint32_t>(Key::Layout));
        input.ki.dwFlags = KEYEVENTF_UNICODE;
        impl_->send_input(input);
        return true;
    }
    if (vk == 0) return false;

    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(vk);
    // Extended key flag for some keys
    if (key == Key::AltGr || key == Key::Control ||
        key == Key::Insert || key == Key::Delete ||
        key == Key::Home || key == Key::End ||
        key == Key::PageUp || key == Key::PageDown ||
        key == Key::LeftArrow || key == Key::RightArrow ||
        key == Key::UpArrow || key == Key::DownArrow ||
        key == Key::NumpadEnter || key == Key::NumpadDivide) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    impl_->send_input(input);

    if (key == Key::Shift || key == Key::Control ||
        key == Key::Alt || key == Key::Meta || key == Key::AltGr) {
        impl_->pressed_modifiers.insert(key);
    }
    return true;
}

bool Enigo::key_up(Key key) {
    auto vk = impl_->key_to_vk_inner(key);
    if (vk == 0 && key >= Key::Layout) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = 0;
        input.ki.wScan = static_cast<WORD>(static_cast<uint32_t>(key) - static_cast<uint32_t>(Key::Layout));
        input.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        impl_->send_input(input);
        return true;
    }
    if (vk == 0) return false;

    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(vk);
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    if (key == Key::AltGr || key == Key::Control || key == Key::Insert ||
        key == Key::Delete || key == Key::Home || key == Key::End ||
        key == Key::PageUp || key == Key::PageDown ||
        key == Key::LeftArrow || key == Key::RightArrow ||
        key == Key::UpArrow || key == Key::DownArrow ||
        key == Key::NumpadEnter || key == Key::NumpadDivide) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    impl_->send_input(input);

    impl_->pressed_modifiers.erase(key);
    return true;
}

bool Enigo::key_sequence(const std::string& sequence) {
    for (char c : sequence) {
        SHORT vk = VkKeyScanA(c);
        if (vk == -1) continue;

        uint8_t key_code = vk & 0xFF;
        uint8_t shift_state = (vk >> 8) & 0xFF;

        if (shift_state & 1) key_down(Key::Shift);
        raw_key_down(key_code);
        raw_key_up(key_code);
        if (shift_state & 1) key_up(Key::Shift);
    }
    return true;
}

bool Enigo::get_key_state(Key key) {
    auto vk = impl_->key_to_vk_inner(key);
    return vk ? (GetAsyncKeyState(vk) & 0x8000) != 0 : false;
}

bool Enigo::get_caps_lock_state() {
    return (GetKeyState(VK_CAPITAL) & 1) != 0;
}

bool Enigo::get_num_lock_state() {
    return (GetKeyState(VK_NUMLOCK) & 1) != 0;
}

void Enigo::release_all() {
    for (auto mod : impl_->pressed_modifiers) {
        key_up(mod);
    }
    impl_->pressed_modifiers.clear();
}

bool Enigo::raw_key_down(uint32_t raw_code) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(raw_code);
    impl_->send_input(input);
    return true;
}

bool Enigo::raw_key_up(uint32_t raw_code) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(raw_code);
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    impl_->send_input(input);
    return true;
}

#elif defined(__linux__)
// ====== Linux X11 Implementation ======

struct Enigo::Impl {
    Display* display = nullptr;
    std::chrono::milliseconds delay{10};
    std::set<Key> pressed_modifiers;

    Impl() {
        display = XOpenDisplay(nullptr);
        if (!display) {
            spdlog::error("Enigo: Failed to open X11 display");
        }
    }
    ~Impl() {
        if (display) XCloseDisplay(display);
    }

    KeySym key_to_keysym(Key k) {
        switch (k) {
            case Key::Alt: return XK_Alt_L;
            case Key::Control: return XK_Control_L;
            case Key::Shift: return XK_Shift_L;
            case Key::Meta: return XK_Super_L;
            case Key::Return: return XK_Return;
            case Key::Escape: return XK_Escape;
            case Key::Tab: return XK_Tab;
            case Key::Space: return XK_space;
            case Key::Backspace: return XK_BackSpace;
            case Key::Delete: return XK_Delete;
            case Key::Insert: return XK_Insert;
            case Key::Home: return XK_Home;
            case Key::End: return XK_End;
            case Key::PageUp: return XK_Page_Up;
            case Key::PageDown: return XK_Page_Down;
            case Key::LeftArrow: return XK_Left;
            case Key::RightArrow: return XK_Right;
            case Key::UpArrow: return XK_Up;
            case Key::DownArrow: return XK_Down;
            case Key::CapsLock: return XK_Caps_Lock;
            case Key::NumLock: return XK_Num_Lock;
            case Key::ScrollLock: return XK_Scroll_Lock;
            case Key::PrintScreen: return XK_Print;
            case Key::Pause: return XK_Pause;
            case Key::Menu: return XK_Menu;
            case Key::F1: return XK_F1; case Key::F2: return XK_F2;
            case Key::F3: return XK_F3; case Key::F4: return XK_F4;
            case Key::F5: return XK_F5; case Key::F6: return XK_F6;
            case Key::F7: return XK_F7; case Key::F8: return XK_F8;
            case Key::F9: return XK_F9; case Key::F10: return XK_F10;
            case Key::F11: return XK_F11; case Key::F12: return XK_F12;
            case Key::VolumeMute: return XF86XK_AudioMute;
            case Key::VolumeDown: return XF86XK_AudioLowerVolume;
            case Key::VolumeUp: return XF86XK_AudioRaiseVolume;
            default: return 0;
        }
    }

    void send_key(Key k, bool down) {
        if (!display) return;
        auto ks = key_to_keysym(k);
        if (ks == 0) return;
        auto kc = XKeysymToKeycode(display, ks);
        if (kc == 0) return;
        XTestFakeKeyEvent(display, kc, down ? True : False, CurrentTime);
        XFlush(display);
        if (delay.count() > 0) {
            std::this_thread::sleep_for(delay);
        }
    }
};

Enigo::Enigo() : impl_(std::make_unique<Impl>()) {
    spdlog::debug("Enigo initialized (Linux/X11)");
}
Enigo::~Enigo() = default;

bool Enigo::mouse_move_to(int32_t x, int32_t y) {
    if (!impl_->display) return false;
    XTestFakeMotionEvent(impl_->display, 0, x, y, CurrentTime);
    XFlush(impl_->display);
    return true;
}

bool Enigo::mouse_move_relative(int32_t dx, int32_t dy) {
    if (!impl_->display) return false;
    XTestFakeRelativeMotionEvent(impl_->display, dx, dy, CurrentTime);
    XFlush(impl_->display);
    return true;
}

static uint32_t mouse_button_to_x11(MouseButton b) {
    switch (b) {
        case MouseButton::Left: return 1;
        case MouseButton::Middle: return 2;
        case MouseButton::Right: return 3;
        case MouseButton::ScrollUp: return 4;
        case MouseButton::ScrollDown: return 5;
        default: return 0;
    }
}

bool Enigo::mouse_down(MouseButton button) {
    if (!impl_->display) return false;
    auto btn = mouse_button_to_x11(button);
    if (btn == 0) return false;
    XTestFakeButtonEvent(impl_->display, btn, True, CurrentTime);
    XFlush(impl_->display);
    return true;
}

bool Enigo::mouse_up(MouseButton button) {
    if (!impl_->display) return false;
    auto btn = mouse_button_to_x11(button);
    if (btn == 0) return false;
    XTestFakeButtonEvent(impl_->display, btn, False, CurrentTime);
    XFlush(impl_->display);
    return true;
}

bool Enigo::mouse_scroll(int32_t delta, MouseAxis axis) {
    if (!impl_->display) return false;
    int btn = (axis == MouseAxis::Vertical && delta > 0) ? 4 :
              (axis == MouseAxis::Vertical) ? 5 : 6;
    for (int i = 0; i < abs(delta); i++) {
        XTestFakeButtonEvent(impl_->display, btn, True, CurrentTime);
        XTestFakeButtonEvent(impl_->display, btn, False, CurrentTime);
    }
    XFlush(impl_->display);
    return true;
}

std::pair<int32_t, int32_t> Enigo::mouse_location() {
    if (!impl_->display) return {0, 0};
    Window root, child;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    XQueryPointer(impl_->display, DefaultRootWindow(impl_->display),
        &root, &child, &root_x, &root_y, &win_x, &win_y, &mask);
    return {root_x, root_y};
}

bool Enigo::key_down(Key key) {
    impl_->send_key(key, true);
    impl_->pressed_modifiers.insert(key);
    return true;
}

bool Enigo::key_up(Key key) {
    impl_->send_key(key, false);
    impl_->pressed_modifiers.erase(key);
    return true;
}

bool Enigo::key_sequence(const std::string& sequence) {
    for (char c : sequence) {
        Key key = static_cast<Key>(static_cast<uint32_t>(Key::Layout) + static_cast<uint8_t>(c));
        key_down(key);
        key_up(key);
    }
    return true;
}

bool Enigo::get_key_state(Key key) {
    if (!impl_->display) return false;
    auto ks = impl_->key_to_keysym(key);
    if (ks == 0) return false;
    auto kc = XKeysymToKeycode(impl_->display, ks);
    char keys[32];
    XQueryKeymap(impl_->display, keys);
    return (keys[kc / 8] & (1 << (kc % 8))) != 0;
}

bool Enigo::get_caps_lock_state() {
    if (!impl_->display) return false;
    unsigned int state;
    XkbGetIndicatorState(impl_->display, XkbUseCoreKbd, &state);
    return state & 1;
}

bool Enigo::get_num_lock_state() {
    if (!impl_->display) return false;
    unsigned int state;
    XkbGetIndicatorState(impl_->display, XkbUseCoreKbd, &state);
    return state & 2;
}

void Enigo::release_all() {
    for (auto mod : impl_->pressed_modifiers) {
        key_up(mod);
    }
}

bool Enigo::raw_key_down(uint32_t raw_code) {
    if (!impl_->display) return false;
    XTestFakeKeyEvent(impl_->display, raw_code, True, CurrentTime);
    XFlush(impl_->display);
    return true;
}

bool Enigo::raw_key_up(uint32_t raw_code) {
    if (!impl_->display) return false;
    XTestFakeKeyEvent(impl_->display, raw_code, False, CurrentTime);
    XFlush(impl_->display);
    return true;
}

#elif defined(__APPLE__)
// ====== macOS Implementation ======

struct Enigo::Impl {
    std::chrono::milliseconds delay{10};
    std::set<Key> pressed_modifiers;

    CGEventFlags modifier_to_flags(Key k) {
        switch (k) {
            case Key::Shift: return kCGEventFlagMaskShift;
            case Key::Control: return kCGEventFlagMaskControl;
            case Key::Alt: return kCGEventFlagMaskAlternate;
            case Key::Meta: return kCGEventFlagMaskCommand;
            default: return 0;
        }
    }

    CGKeyCode key_to_cgcode(Key k) {
        switch (k) {
            case Key::Return: return 36; case Key::Space: return 49;
            case Key::Delete: return 51; case Key::Escape: return 53;
            case Key::Tab: return 48; case Key::CapsLock: return 57;
            case Key::Shift: return 56; case Key::Control: return 59;
            case Key::Alt: return 58; case Key::Meta: return 55;
            case Key::LeftArrow: return 123; case Key::RightArrow: return 124;
            case Key::DownArrow: return 125; case Key::UpArrow: return 126;
            case Key::F1: return 122; case Key::F2: return 120;
            case Key::F3: return 99; case Key::F4: return 118;
            case Key::F5: return 96; case Key::F6: return 97;
            case Key::F7: return 98; case Key::F8: return 100;
            case Key::F9: return 101; case Key::F10: return 109;
            case Key::F11: return 103; case Key::F12: return 111;
            case Key::Home: return 115; case Key::End: return 119;
            case Key::PageUp: return 116; case Key::PageDown: return 121;
            case Key::VolumeMute: return 74;
            case Key::VolumeDown: return 75;
            case Key::VolumeUp: return 73;
            default: return 0;
        }
    }
};

Enigo::Enigo() : impl_(std::make_unique<Impl>()) {
    spdlog::debug("Enigo initialized (macOS)");
}
Enigo::~Enigo() = default;

bool Enigo::mouse_move_to(int32_t x, int32_t y) {
    CGEventRef move = CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved,
        CGPointMake(x, y), kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, move);
    CFRelease(move);
    return true;
}

bool Enigo::mouse_move_relative(int32_t dx, int32_t dy) {
    CGEventRef move = CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved,
        CGPointZero, kCGMouseButtonLeft);
    CGEventSetIntegerValueField(move, kCGMouseEventDeltaX, dx);
    CGEventSetIntegerValueField(move, kCGMouseEventDeltaY, dy);
    CGEventPost(kCGHIDEventTap, move);
    CFRelease(move);
    return true;
}

static CGMouseButton cg_button(MouseButton b) {
    switch (b) {
        case MouseButton::Left: return kCGMouseButtonLeft;
        case MouseButton::Right: return kCGMouseButtonRight;
        case MouseButton::Middle: return kCGMouseButtonCenter;
        default: return kCGMouseButtonLeft;
    }
}

bool Enigo::mouse_down(MouseButton button) {
    CGEventType type;
    switch (button) {
        case MouseButton::Left: type = kCGEventLeftMouseDown; break;
        case MouseButton::Right: type = kCGEventRightMouseDown; break;
        case MouseButton::Middle: type = kCGEventOtherMouseDown; break;
        default: return false;
    }
    CGEventRef ev = CGEventCreateMouseEvent(nullptr, type,
        CGEventGetLocation(CGEventCreate(nullptr)), cg_button(button));
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
    return true;
}

bool Enigo::mouse_up(MouseButton button) {
    CGEventType type;
    switch (button) {
        case MouseButton::Left: type = kCGEventLeftMouseUp; break;
        case MouseButton::Right: type = kCGEventRightMouseUp; break;
        case MouseButton::Middle: type = kCGEventOtherMouseUp; break;
        default: return false;
    }
    CGEventRef ev = CGEventCreateMouseEvent(nullptr, type,
        CGEventGetLocation(CGEventCreate(nullptr)), cg_button(button));
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
    return true;
}

bool Enigo::mouse_scroll(int32_t delta, MouseAxis axis) {
    int32_t lines = delta;
    CGEventRef ev = CGEventCreateScrollWheelEvent(nullptr,
        axis == MouseAxis::Vertical ? kCGScrollEventUnitLine : kCGScrollEventUnitPixel,
        axis == MouseAxis::Vertical ? 1 : 2,
        axis == MouseAxis::Vertical ? lines : 0,
        axis == MouseAxis::Horizontal ? lines : 0);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
    return true;
}

std::pair<int32_t, int32_t> Enigo::mouse_location() {
    CGEventRef ev = CGEventCreate(nullptr);
    CGPoint pt = CGEventGetLocation(ev);
    CFRelease(ev);
    return {static_cast<int32_t>(pt.x), static_cast<int32_t>(pt.y)};
}

bool Enigo::key_down(Key key) {
    auto cg = impl_->key_to_cgcode(key);
    if (cg == 0) return false;
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, cg, true);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
    impl_->pressed_modifiers.insert(key);
    return true;
}

bool Enigo::key_up(Key key) {
    auto cg = impl_->key_to_cgcode(key);
    if (cg == 0) return false;
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, cg, false);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
    impl_->pressed_modifiers.erase(key);
    return true;
}

bool Enigo::key_sequence(const std::string& sequence) {
    for (char c : sequence) {
        CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, 0, true);
        UniChar ch = static_cast<UniChar>(c);
        CGEventKeyboardSetUnicodeString(ev, 1, &ch);
        CGEventPost(kCGHIDEventTap, ev);
        CFRelease(ev);

        ev = CGEventCreateKeyboardEvent(nullptr, 0, false);
        CGEventPost(kCGHIDEventTap, ev);
        CFRelease(ev);
    }
    return true;
}

bool Enigo::get_key_state(Key key) {
    return CGEventSourceKeyState(kCGEventSourceStateHIDSystemState,
        impl_->key_to_cgcode(key));
}

bool Enigo::get_caps_lock_state() {
    return CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState) & kCGEventFlagMaskAlphaShift;
}

bool Enigo::get_num_lock_state() {
    return CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState) & kCGEventFlagMaskNumericPad;
}

void Enigo::release_all() {
    for (auto mod : impl_->pressed_modifiers) key_up(mod);
}

bool Enigo::raw_key_down(uint32_t raw_code) {
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(raw_code), true);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
    return true;
}

bool Enigo::raw_key_up(uint32_t raw_code) {
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(raw_code), false);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
    return true;
}

#endif

// ====== Common Methods (all platforms) ======

bool Enigo::is_available() const {
#ifdef _WIN32
    return true;
#elif defined(__linux__)
    return impl_->display != nullptr;
#else
    return true;
#endif
}

void Enigo::set_delay(std::chrono::milliseconds delay) {
    impl_->delay = delay;
}

std::chrono::milliseconds Enigo::get_delay() const {
    return impl_->delay;
}

bool Enigo::is_modifier_pressed(Key mod) const {
    return impl_->pressed_modifiers.count(mod) > 0;
}

bool Enigo::drag(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y,
    MouseButton button) {
    mouse_move_to(start_x, start_y);
    mouse_down(button);
    // Move in steps for smooth drag
    int steps = 20;
    for (int i = 1; i <= steps; i++) {
        int32_t cx = start_x + (end_x - start_x) * i / steps;
        int32_t cy = start_y + (end_y - start_y) * i / steps;
        mouse_move_to(cx, cy);
        if (impl_->delay.count() > 0) {
            std::this_thread::sleep_for(impl_->delay);
        }
    }
    mouse_up(button);
    return true;
}

bool Enigo::key_sequence_dsl(const std::string& dsl) {
    auto tokens = DslParser::parse(dsl);
    for (auto& tok : tokens) {
        switch (tok.type) {
            case DslToken::TEXT:
                key_sequence(tok.text);
                break;
            case DslToken::KEY:
                key_click(tok.key);
                break;
            case DslToken::MODIFIER_START:
                for (auto& mod : tok.modifiers) key_down(mod);
                break;
            case DslToken::MODIFIER_END:
                for (auto& mod : tok.modifiers) key_up(mod);
                break;
            case DslToken::RAW_KEY:
                raw_key_down(static_cast<uint32_t>(tok.key));
                raw_key_up(static_cast<uint32_t>(tok.key));
                break;
        }
    }
    return true;
}

// ====== DSL Parser ======

static std::map<std::string, Key> dsl_key_map = {
    {"SHIFT", Key::Shift}, {"CTRL", Key::Control}, {"CONTROL", Key::Control},
    {"ALT", Key::Alt}, {"META", Key::Meta}, {"CMD", Key::Meta},
    {"SUPER", Key::Meta}, {"WIN", Key::Meta}, {"ENTER", Key::Return},
    {"RETURN", Key::Return}, {"TAB", Key::Tab}, {"SPACE", Key::Space},
    {"ESC", Key::Escape}, {"ESCAPE", Key::Escape},
    {"BS", Key::Backspace}, {"BACKSPACE", Key::Backspace},
    {"DEL", Key::Delete}, {"DELETE", Key::Delete},
    {"UP", Key::UpArrow}, {"DOWN", Key::DownArrow},
    {"LEFT", Key::LeftArrow}, {"RIGHT", Key::RightArrow},
    {"HOME", Key::Home}, {"END", Key::End},
    {"PGUP", Key::PageUp}, {"PGDN", Key::PageDown},
    {"INS", Key::Insert}, {"INSERT", Key::Insert},
    {"CAPS", Key::CapsLock}, {"NUMLOCK", Key::NumLock},
    {"F1", Key::F1}, {"F2", Key::F2}, {"F3", Key::F3}, {"F4", Key::F4},
    {"F5", Key::F5}, {"F6", Key::F6}, {"F7", Key::F7}, {"F8", Key::F8},
    {"F9", Key::F9}, {"F10", Key::F10}, {"F11", Key::F11}, {"F12", Key::F12},
    {"VOLUME_MUTE", Key::VolumeMute}, {"VOLUME_DOWN", Key::VolumeDown},
    {"VOLUME_UP", Key::VolumeUp},
};

std::vector<DslToken> DslParser::parse(const std::string& dsl) {
    std::vector<DslToken> tokens;
    std::set<Key> active_mods;
    size_t pos = 0;

    while (pos < dsl.size()) {
        if (dsl[pos] == '{') {
            auto end = dsl.find('}', pos);
            if (end == std::string::npos) {
                // No closing brace: treat as literal
                tokens.push_back({DslToken::TEXT, std::string(1, dsl[pos])});
                pos++;
                continue;
            }
            std::string cmd = dsl.substr(pos + 1, end - pos - 1);
            pos = end + 1;

            if (cmd.empty()) {
                tokens.push_back({DslToken::TEXT, "{}"});
                continue;
            }

            if (cmd[0] == '+' || cmd[0] == '-') {
                bool add = cmd[0] == '+';
                std::string key_name = cmd.substr(1);
                auto it = dsl_key_map.find(key_name);
                if (it != dsl_key_map.end()) {
                    if (add) {
                        active_mods.insert(it->second);
                        tokens.push_back({DslToken::MODIFIER_START, "", Key::Space, {it->second}});
                    } else {
                        active_mods.erase(it->second);
                        tokens.push_back({DslToken::MODIFIER_END, "", Key::Space, {it->second}});
                    }
                }
            } else {
                auto it = dsl_key_map.find(cmd);
                if (it != dsl_key_map.end()) {
                    tokens.push_back({DslToken::KEY, "", it->second});
                } else if (cmd.size() == 1) {
                    tokens.push_back({DslToken::TEXT, cmd});
                } else {
                    tokens.push_back({DslToken::TEXT, "{" + cmd + "}"});
                }
            }
        } else {
            // Plain text
            size_t next = dsl.find('{', pos);
            if (next == std::string::npos) next = dsl.size();
            tokens.push_back({DslToken::TEXT, dsl.substr(pos, next - pos)});
            pos = next;
        }
    }
    return tokens;
}

std::string DslParser::unparse(const std::vector<DslToken>& tokens) {
    std::string result;
    std::set<Key> active_mods;
    for (auto& tok : tokens) {
        switch (tok.type) {
            case DslToken::TEXT:
                result += tok.text;
                break;
            case DslToken::KEY: {
                std::string name;
                for (auto& [k, v] : dsl_key_map) {
                    if (v == tok.key) { name = k; break; }
                }
                result += "{" + (name.empty() ? "?" : name) + "}";
                break;
            }
            case DslToken::MODIFIER_START:
                for (auto& mod : tok.modifiers) active_mods.insert(mod);
                break;
            case DslToken::MODIFIER_END:
                for (auto& mod : tok.modifiers) active_mods.erase(mod);
                break;
            default: break;
        }
    }
    return result;
}

} // namespace enigo
