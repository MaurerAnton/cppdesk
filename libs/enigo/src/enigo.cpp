#include "enigo/enigo.hpp"
#include <spdlog/spdlog.h>
#include <cstring>
#include <algorithm>
#include <set>
#include <thread>
#include <regex>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winuser.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <IOKit/hidsystem/ev_keymap.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>
#include <unistd.h>
#include <climits>
#endif

namespace enigo {

uint32_t ENIGO_INPUT_EXTRA_VALUE = 0;

// =====================================================================
//  Platform Key Maps — comprehensive lookup tables
// =====================================================================

#ifdef _WIN32

// ---- Complete Windows virtual-key mapping ----
static const std::unordered_map<Key, uint32_t> key_to_vk = {
    // Modifiers
    {Key::Alt, VK_MENU},
    {Key::AltGr, VK_RMENU},
    {Key::RightAlt, VK_RMENU},
    {Key::Control, VK_CONTROL},
    {Key::RightControl, VK_RCONTROL},
    {Key::Shift, VK_SHIFT},
    {Key::RightShift, VK_RSHIFT},
    {Key::Meta, VK_LWIN},
    {Key::RightMeta, VK_RWIN},

    // Navigation / editing
    {Key::Backspace, VK_BACK},
    {Key::Tab, VK_TAB},
    {Key::Return, VK_RETURN},
    {Key::Escape, VK_ESCAPE},
    {Key::Space, VK_SPACE},
    {Key::Delete, VK_DELETE},
    {Key::Insert, VK_INSERT},
    {Key::Home, VK_HOME},
    {Key::End, VK_END},
    {Key::PageUp, VK_PRIOR},
    {Key::PageDown, VK_NEXT},
    {Key::LeftArrow, VK_LEFT},
    {Key::RightArrow, VK_RIGHT},
    {Key::UpArrow, VK_UP},
    {Key::DownArrow, VK_DOWN},
    {Key::Menu, VK_APPS},

    // Locks
    {Key::CapsLock, VK_CAPITAL},
    {Key::NumLock, VK_NUMLOCK},
    {Key::ScrollLock, VK_SCROLL},

    // System
    {Key::PrintScreen, VK_SNAPSHOT},
    {Key::Pause, VK_PAUSE},

    // Function keys
    {Key::F1, VK_F1},   {Key::F2, VK_F2},   {Key::F3, VK_F3},   {Key::F4, VK_F4},
    {Key::F5, VK_F5},   {Key::F6, VK_F6},   {Key::F7, VK_F7},   {Key::F8, VK_F8},
    {Key::F9, VK_F9},   {Key::F10, VK_F10}, {Key::F11, VK_F11}, {Key::F12, VK_F12},
    {Key::F13, VK_F13}, {Key::F14, VK_F14}, {Key::F15, VK_F15}, {Key::F16, VK_F16},
    {Key::F17, VK_F17}, {Key::F18, VK_F18}, {Key::F19, VK_F19}, {Key::F20, VK_F20},
    {Key::F21, VK_F21}, {Key::F22, VK_F22}, {Key::F23, VK_F23}, {Key::F24, VK_F24},

    // Numpad
    {Key::Numpad0, VK_NUMPAD0}, {Key::Numpad1, VK_NUMPAD1}, {Key::Numpad2, VK_NUMPAD2},
    {Key::Numpad3, VK_NUMPAD3}, {Key::Numpad4, VK_NUMPAD4}, {Key::Numpad5, VK_NUMPAD5},
    {Key::Numpad6, VK_NUMPAD6}, {Key::Numpad7, VK_NUMPAD7}, {Key::Numpad8, VK_NUMPAD8},
    {Key::Numpad9, VK_NUMPAD9},
    {Key::NumpadAdd, VK_ADD},           {Key::NumpadSubtract, VK_SUBTRACT},
    {Key::NumpadMultiply, VK_MULTIPLY}, {Key::NumpadDivide, VK_DIVIDE},
    {Key::NumpadDecimal, VK_DECIMAL},   {Key::NumpadEnter, VK_RETURN},

    // Media
    {Key::VolumeMute, VK_VOLUME_MUTE},
    {Key::VolumeDown, VK_VOLUME_DOWN},
    {Key::VolumeUp, VK_VOLUME_UP},
    {Key::MediaPlay, VK_MEDIA_PLAY_PAUSE},
    {Key::MediaPause, VK_MEDIA_PLAY_PAUSE},
    {Key::MediaStop, VK_MEDIA_STOP},
    {Key::MediaNext, VK_MEDIA_NEXT_TRACK},
    {Key::MediaPrev, VK_MEDIA_PREV_TRACK},
    {Key::MediaPlayPause, VK_MEDIA_PLAY_PAUSE},

    // Browser
    {Key::BrowserBack, VK_BROWSER_BACK},
    {Key::BrowserForward, VK_BROWSER_FORWARD},
    {Key::BrowserRefresh, VK_BROWSER_REFRESH},
    {Key::BrowserStop, VK_BROWSER_STOP},
    {Key::BrowserSearch, VK_BROWSER_SEARCH},
    {Key::BrowserFavorites, VK_BROWSER_FAVORITES},
    {Key::BrowserHome, VK_BROWSER_HOME},

    // Launch
    {Key::LaunchMail, VK_LAUNCH_MAIL},
    {Key::LaunchMedia, VK_LAUNCH_MEDIA_SELECT},
    {Key::LaunchApp1, VK_LAUNCH_APP1},
    {Key::LaunchApp2, VK_LAUNCH_APP2},

    // IME
    {Key::KanaMode, VK_KANA},
    {Key::HangulMode, VK_HANGUL},
    {Key::HanjaMode, VK_HANJA},
    {Key::IMEOn, VK_KANA},
    {Key::IMEOff, VK_KANA},

    // Help / editing
    {Key::Help, VK_HELP},
    {Key::SelectAll, VK_SELECT},
    {Key::ZoomIn, VK_ZOOM},
};

// ---- Reverse map: VK → Key ----
static std::unordered_map<uint32_t, Key> vk_to_key;
static const bool vk_map_init = []() {
    for (const auto& [k, v] : key_to_vk) {
        if (vk_to_key.find(v) == vk_to_key.end()) {
            vk_to_key[v] = k;
        }
    }
    return true;
}();

// ---- Extended key flag detection ----
static const std::set<Key> extended_keys = {
    Key::AltGr, Key::RightAlt, Key::RightControl,
    Key::Insert, Key::Delete, Key::Home, Key::End,
    Key::PageUp, Key::PageDown,
    Key::LeftArrow, Key::RightArrow, Key::UpArrow, Key::DownArrow,
    Key::NumpadEnter, Key::NumpadDivide,
    Key::PrintScreen,
};

// ---- Scan codes for common keys ----
static const std::unordered_map<Key, uint32_t> key_to_scan = {
    {Key::Escape, 0x01},       {Key::Tab, 0x0F},
    {Key::CapsLock, 0x3A},     {Key::Shift, 0x2A},
    {Key::RightShift, 0x36},   {Key::Control, 0x1D},
    {Key::RightControl, 0xE01D},{Key::Alt, 0x38},
    {Key::RightAlt, 0xE038},   {Key::Space, 0x39},
    {Key::Meta, 0xE05B},       {Key::RightMeta, 0xE05C},
    {Key::Menu, 0xE05D},       {Key::Return, 0x1C},
    {Key::Backspace, 0x0E},    {Key::Delete, 0xE053},
    {Key::Insert, 0xE052},     {Key::Home, 0xE047},
    {Key::End, 0xE04F},        {Key::PageUp, 0xE049},
    {Key::PageDown, 0xE051},   {Key::UpArrow, 0xE048},
    {Key::DownArrow, 0xE050},  {Key::LeftArrow, 0xE04B},
    {Key::RightArrow, 0xE04D}, {Key::NumLock, 0x45},
    {Key::ScrollLock, 0x46},   {Key::PrintScreen, 0xE037},
    {Key::Pause, 0xE11D45},
    {Key::Numpad0, 0x52},      {Key::Numpad1, 0x4F},
    {Key::Numpad2, 0x50},      {Key::Numpad3, 0x51},
    {Key::Numpad4, 0x4B},      {Key::Numpad5, 0x4C},
    {Key::Numpad6, 0x4D},      {Key::Numpad7, 0x47},
    {Key::Numpad8, 0x48},      {Key::Numpad9, 0x49},
    {Key::NumpadAdd, 0x4E},    {Key::NumpadSubtract, 0x4A},
    {Key::NumpadMultiply, 0x37},{Key::NumpadDivide, 0xE035},
    {Key::NumpadDecimal, 0x53},{Key::NumpadEnter, 0xE01C},
};

// ---- VK name mapping for debug ----
static const std::unordered_map<uint32_t, const char*> vk_names = {
    {VK_LBUTTON, "VK_LBUTTON"}, {VK_RBUTTON, "VK_RBUTTON"},
    {VK_CANCEL, "VK_CANCEL"},   {VK_MBUTTON, "VK_MBUTTON"},
    {VK_BACK, "VK_BACK"},       {VK_TAB, "VK_TAB"},
    {VK_RETURN, "VK_RETURN"},   {VK_SHIFT, "VK_SHIFT"},
    {VK_CONTROL, "VK_CONTROL"}, {VK_MENU, "VK_MENU"},
    {VK_PAUSE, "VK_PAUSE"},     {VK_CAPITAL, "VK_CAPITAL"},
    {VK_ESCAPE, "VK_ESCAPE"},   {VK_SPACE, "VK_SPACE"},
    {VK_PRIOR, "VK_PRIOR"},     {VK_NEXT, "VK_NEXT"},
    {VK_END, "VK_END"},         {VK_HOME, "VK_HOME"},
    {VK_LEFT, "VK_LEFT"},       {VK_UP, "VK_UP"},
    {VK_RIGHT, "VK_RIGHT"},     {VK_DOWN, "VK_DOWN"},
    {VK_SNAPSHOT, "VK_SNAPSHOT"},{VK_INSERT, "VK_INSERT"},
    {VK_DELETE, "VK_DELETE"},   {VK_LWIN, "VK_LWIN"},
    {VK_RWIN, "VK_RWIN"},       {VK_APPS, "VK_APPS"},
    {VK_NUMLOCK, "VK_NUMLOCK"}, {VK_SCROLL, "VK_SCROLL"},
    {VK_VOLUME_MUTE, "VK_VOLUME_MUTE"},
    {VK_VOLUME_DOWN, "VK_VOLUME_DOWN"},
    {VK_VOLUME_UP, "VK_VOLUME_UP"},
    {VK_MEDIA_NEXT_TRACK, "VK_MEDIA_NEXT_TRACK"},
    {VK_MEDIA_PREV_TRACK, "VK_MEDIA_PREV_TRACK"},
    {VK_MEDIA_STOP, "VK_MEDIA_STOP"},
    {VK_MEDIA_PLAY_PAUSE, "VK_MEDIA_PLAY_PAUSE"},
    {VK_BROWSER_BACK, "VK_BROWSER_BACK"},
    {VK_BROWSER_FORWARD, "VK_BROWSER_FORWARD"},
    {VK_BROWSER_REFRESH, "VK_BROWSER_REFRESH"},
    {VK_BROWSER_STOP, "VK_BROWSER_STOP"},
    {VK_BROWSER_SEARCH, "VK_BROWSER_SEARCH"},
    {VK_BROWSER_FAVORITES, "VK_BROWSER_FAVORITES"},
    {VK_BROWSER_HOME, "VK_BROWSER_HOME"},
    {VK_LAUNCH_MAIL, "VK_LAUNCH_MAIL"},
    {VK_LAUNCH_MEDIA_SELECT, "VK_LAUNCH_MEDIA_SELECT"},
    {VK_LAUNCH_APP1, "VK_LAUNCH_APP1"},
    {VK_LAUNCH_APP2, "VK_LAUNCH_APP2"},
};

uint32_t PlatformKeyMap::to_virtual_key(Key k) {
    auto it = key_to_vk.find(k);
    return it != key_to_vk.end() ? it->second : 0;
}

Key PlatformKeyMap::from_virtual_key(uint32_t vk) {
    auto it = vk_to_key.find(vk);
    return it != vk_to_key.end() ? it->second : Key::Raw;
}

uint32_t PlatformKeyMap::to_scan_code(Key k) {
    auto it = key_to_scan.find(k);
    return it != key_to_scan.end() ? it->second : 0;
}

bool PlatformKeyMap::is_extended_key(Key k) {
    return extended_keys.count(k) > 0;
}

const char* PlatformKeyMap::virtual_key_name(uint32_t vk) {
    auto it = vk_names.find(vk);
    return it != vk_names.end() ? it->second : "VK_UNKNOWN";
}

#endif // _WIN32

// =====================================================================
//  macOS Key Maps
// =====================================================================
#ifdef __APPLE__

static const std::unordered_map<Key, uint16_t> key_to_cg = {
    {Key::Return, 36},      {Key::Tab, 48},
    {Key::Space, 49},       {Key::Delete, 51},
    {Key::Escape, 53},      {Key::Meta, 55},
    {Key::Shift, 56},       {Key::CapsLock, 57},
    {Key::Alt, 58},         {Key::Control, 59},
    {Key::RightShift, 60},  {Key::RightAlt, 61},
    {Key::RightControl, 62}, {Key::RightMeta, 54},

    // Function keys
    {Key::F1, 122},  {Key::F2, 120},  {Key::F3, 99},   {Key::F4, 118},
    {Key::F5, 96},   {Key::F6, 97},   {Key::F7, 98},   {Key::F8, 100},
    {Key::F9, 101},  {Key::F10, 109}, {Key::F11, 103}, {Key::F12, 111},
    {Key::F13, 105}, {Key::F14, 107}, {Key::F15, 113}, {Key::F16, 106},
    {Key::F17, 64},  {Key::F18, 79},  {Key::F19, 80},  {Key::F20, 90},

    // Navigation
    {Key::Home, 115},       {Key::End, 119},
    {Key::PageUp, 116},     {Key::PageDown, 121},
    {Key::LeftArrow, 123},  {Key::RightArrow, 124},
    {Key::DownArrow, 125},  {Key::UpArrow, 126},
    {Key::Insert, 114},     {Key::Backspace, 51},

    // Numpad
    {Key::Numpad0, 82},     {Key::Numpad1, 83},
    {Key::Numpad2, 84},     {Key::Numpad3, 85},
    {Key::Numpad4, 86},     {Key::Numpad5, 87},
    {Key::Numpad6, 88},     {Key::Numpad7, 89},
    {Key::Numpad8, 91},     {Key::Numpad9, 92},
    {Key::NumpadAdd, 69},   {Key::NumpadSubtract, 78},
    {Key::NumpadMultiply, 67},{Key::NumpadDivide, 75},
    {Key::NumpadDecimal, 65},{Key::NumpadEnter, 76},
    {Key::NumpadEqual, 81}, {Key::NumLock, 71},

    // Media
    {Key::VolumeMute, 74},
    {Key::VolumeDown, 75},
    {Key::VolumeUp, 73},
    {Key::MediaPlay, 74},  // same as mute on some layouts
    {Key::MediaNext, 100}, // F8 = media next on newer Macs
    {Key::MediaPrev, 98},  // F7 = media prev
    {Key::MediaEject, 14},

    // Brightness / system
    {Key::LaunchApp1, 107},  // F14 = decrease brightness
    {Key::LaunchApp2, 113},  // F15 = increase brightness
    {Key::Help, 114},        // Insert/Help key
};

static std::unordered_map<uint16_t, Key> cg_to_key;
static const bool cg_map_init = []() {
    for (const auto& [k, v] : key_to_cg) cg_to_key[v] = k;
    return true;
}();

static const std::unordered_map<uint16_t, const char*> cg_names = {
    {0, "kVK_ANSI_A"},       {1, "kVK_ANSI_S"},
    {2, "kVK_ANSI_D"},       {3, "kVK_ANSI_F"},
    {4, "kVK_ANSI_H"},       {5, "kVK_ANSI_G"},
    {6, "kVK_ANSI_Z"},       {7, "kVK_ANSI_X"},
    {8, "kVK_ANSI_C"},       {9, "kVK_ANSI_V"},
    {11, "kVK_ANSI_B"},      {12, "kVK_ANSI_Q"},
    {13, "kVK_ANSI_W"},      {14, "kVK_ANSI_E"},
    {15, "kVK_ANSI_R"},      {16, "kVK_ANSI_Y"},
    {17, "kVK_ANSI_T"},      {24, "kVK_ANSI_Equal"},
    {27, "kVK_ANSI_Minus"},  {31, "kVK_ANSI_O"},
    {32, "kVK_ANSI_U"},      {33, "kVK_ANSI_LeftBracket"},
    {34, "kVK_ANSI_I"},      {35, "kVK_ANSI_P"},
    {36, "kVK_Return"},      {37, "kVK_ANSI_L"},
    {38, "kVK_ANSI_J"},      {39, "kVK_ANSI_Quote"},
    {40, "kVK_ANSI_K"},      {41, "kVK_ANSI_Semicolon"},
    {42, "kVK_ANSI_Backslash"},{43, "kVK_ANSI_Comma"},
    {44, "kVK_ANSI_Slash"},  {45, "kVK_ANSI_N"},
    {46, "kVK_ANSI_M"},      {47, "kVK_ANSI_Period"},
    {48, "kVK_Tab"},         {49, "kVK_Space"},
    {50, "kVK_ANSI_Grave"},  {51, "kVK_Delete"},
    {53, "kVK_Escape"},      {55, "kVK_Command"},
    {56, "kVK_Shift"},       {57, "kVK_CapsLock"},
    {58, "kVK_Option"},      {59, "kVK_Control"},
    {60, "kVK_RightShift"},  {61, "kVK_RightOption"},
    {62, "kVK_RightControl"},{63, "kVK_Function"},
    {65, "kVK_ANSI_KeypadDecimal"}, {67, "kVK_ANSI_KeypadMultiply"},
    {69, "kVK_ANSI_KeypadPlus"},    {71, "kVK_ANSI_KeypadClear"},
    {73, "kVK_VolumeUp"},           {74, "kVK_VolumeDown/Mute"},
    {75, "kVK_ANSI_KeypadDivide"},  {76, "kVK_ANSI_KeypadEnter"},
    {78, "kVK_ANSI_KeypadMinus"},   {81, "kVK_ANSI_KeypadEquals"},
    {82, "kVK_ANSI_Keypad0"},       {83, "kVK_ANSI_Keypad1"},
    {84, "kVK_ANSI_Keypad2"},       {85, "kVK_ANSI_Keypad3"},
    {86, "kVK_ANSI_Keypad4"},       {87, "kVK_ANSI_Keypad5"},
    {88, "kVK_ANSI_Keypad6"},       {89, "kVK_ANSI_Keypad7"},
    {91, "kVK_ANSI_Keypad8"},       {92, "kVK_ANSI_Keypad9"},
    {96, "kVK_F5"},   {97, "kVK_F6"},   {98, "kVK_F7"},
    {99, "kVK_F3"},  {100, "kVK_F8"},  {101, "kVK_F9"},
    {103, "kVK_F11"},{105, "kVK_F13"}, {107, "kVK_F14"},
    {109, "kVK_F10"},{111, "kVK_F12"}, {113, "kVK_F15"},
    {114, "kVK_Help"},{115, "kVK_Home"},
    {116, "kVK_PageUp"},      {117, "kVK_ForwardDelete"},
    {118, "kVK_F4"},          {119, "kVK_End"},
    {120, "kVK_F2"},          {121, "kVK_PageDown"},
    {122, "kVK_F1"},          {123, "kVK_LeftArrow"},
    {124, "kVK_RightArrow"},  {125, "kVK_DownArrow"},
    {126, "kVK_UpArrow"},
};

uint16_t PlatformKeyMap::to_cg_keycode(Key k) {
    auto it = key_to_cg.find(k);
    return it != key_to_cg.end() ? it->second : 0;
}

Key PlatformKeyMap::from_cg_keycode(uint16_t code) {
    auto it = cg_to_key.find(code);
    return it != cg_to_key.end() ? it->second : Key::Raw;
}

uint64_t PlatformKeyMap::to_cg_event_flags(Key k) {
    switch (k) {
        case Key::Shift:       return kCGEventFlagMaskShift;
        case Key::RightShift:  return kCGEventFlagMaskShift;
        case Key::Control:     return kCGEventFlagMaskControl;
        case Key::RightControl:return kCGEventFlagMaskControl;
        case Key::Alt:         return kCGEventFlagMaskAlternate;
        case Key::RightAlt:    return kCGEventFlagMaskAlternate;
        case Key::Meta:        return kCGEventFlagMaskCommand;
        case Key::RightMeta:   return kCGEventFlagMaskCommand;
        case Key::CapsLock:    return kCGEventFlagMaskAlphaShift;
        case Key::NumLock:     return kCGEventFlagMaskNumericPad;
        default: return 0;
    }
}

const char* PlatformKeyMap::cg_keycode_name(uint16_t code) {
    auto it = cg_names.find(code);
    return it != cg_names.end() ? it->second : "kVK_UNKNOWN";
}

#endif // __APPLE__

// =====================================================================
//  Linux X11 Key Maps
// =====================================================================
#ifdef __linux__

static const std::unordered_map<Key, uint32_t> key_to_xks = {
    // Modifiers
    {Key::Shift, XK_Shift_L},         {Key::RightShift, XK_Shift_R},
    {Key::Control, XK_Control_L},     {Key::RightControl, XK_Control_R},
    {Key::Alt, XK_Alt_L},             {Key::RightAlt, XK_Alt_R},
    {Key::Meta, XK_Super_L},          {Key::RightMeta, XK_Super_R},
    {Key::AltGr, XK_ISO_Level3_Shift},{Key::Menu, XK_Menu},

    // Basic
    {Key::Return, XK_Return},         {Key::Escape, XK_Escape},
    {Key::Tab, XK_Tab},               {Key::Space, XK_space},
    {Key::Backspace, XK_BackSpace},   {Key::Delete, XK_Delete},
    {Key::Insert, XK_Insert},

    // Navigation
    {Key::Home, XK_Home},             {Key::End, XK_End},
    {Key::PageUp, XK_Page_Up},        {Key::PageDown, XK_Page_Down},
    {Key::LeftArrow, XK_Left},        {Key::RightArrow, XK_Right},
    {Key::UpArrow, XK_Up},            {Key::DownArrow, XK_Down},

    // Locks
    {Key::CapsLock, XK_Caps_Lock},    {Key::NumLock, XK_Num_Lock},
    {Key::ScrollLock, XK_Scroll_Lock},

    // System
    {Key::PrintScreen, XK_Print},     {Key::Pause, XK_Pause},
    {Key::Pause, XK_Break},

    // Function keys
    {Key::F1, XK_F1},   {Key::F2, XK_F2},   {Key::F3, XK_F3},   {Key::F4, XK_F4},
    {Key::F5, XK_F5},   {Key::F6, XK_F6},   {Key::F7, XK_F7},   {Key::F8, XK_F8},
    {Key::F9, XK_F9},   {Key::F10, XK_F10}, {Key::F11, XK_F11}, {Key::F12, XK_F12},
    {Key::F13, XK_F13}, {Key::F14, XK_F14}, {Key::F15, XK_F15}, {Key::F16, XK_F16},
    {Key::F17, XK_F17}, {Key::F18, XK_F18}, {Key::F19, XK_F19}, {Key::F20, XK_F20},
    {Key::F21, XK_F21}, {Key::F22, XK_F22}, {Key::F23, XK_F23}, {Key::F24, XK_F24},

    // Numpad
    {Key::Numpad0, XK_KP_0},         {Key::Numpad1, XK_KP_1},
    {Key::Numpad2, XK_KP_2},         {Key::Numpad3, XK_KP_3},
    {Key::Numpad4, XK_KP_4},         {Key::Numpad5, XK_KP_5},
    {Key::Numpad6, XK_KP_6},         {Key::Numpad7, XK_KP_7},
    {Key::Numpad8, XK_KP_8},         {Key::Numpad9, XK_KP_9},
    {Key::NumpadAdd, XK_KP_Add},     {Key::NumpadSubtract, XK_KP_Subtract},
    {Key::NumpadMultiply, XK_KP_Multiply},{Key::NumpadDivide, XK_KP_Divide},
    {Key::NumpadDecimal, XK_KP_Decimal},{Key::NumpadEnter, XK_KP_Enter},
    {Key::NumpadEqual, XK_KP_Equal},

    // Media
    {Key::VolumeMute, XF86XK_AudioMute},
    {Key::VolumeDown, XF86XK_AudioLowerVolume},
    {Key::VolumeUp, XF86XK_AudioRaiseVolume},
    {Key::MediaPlay, XF86XK_AudioPlay},
    {Key::MediaPause, XF86XK_AudioPause},
    {Key::MediaStop, XF86XK_AudioStop},
    {Key::MediaNext, XF86XK_AudioNext},
    {Key::MediaPrev, XF86XK_AudioPrev},
    {Key::MediaRecord, XF86XK_AudioRecord},
    {Key::MediaRewind, XF86XK_AudioRewind},
    {Key::MediaFastForward, XF86XK_AudioForward},
    {Key::MediaEject, XF86XK_Eject},
    {Key::MediaRandomPlay, XF86XK_AudioRandomPlay},
    {Key::MediaPlayPause, XF86XK_AudioPlay},

    // Browser
    {Key::BrowserBack, XF86XK_Back},
    {Key::BrowserForward, XF86XK_Forward},
    {Key::BrowserRefresh, XF86XK_Refresh},
    {Key::BrowserStop, XF86XK_Stop},
    {Key::BrowserSearch, XF86XK_Search},
    {Key::BrowserFavorites, XF86XK_Favorites},
    {Key::BrowserHome, XF86XK_HomePage},

    // Launch
    {Key::LaunchMail, XF86XK_Mail},
    {Key::LaunchMedia, XF86XK_AudioMedia},
    {Key::LaunchCalculator, XF86XK_Calculator},
    {Key::LaunchFileBrowser, XF86XK_Explorer},
    {Key::LaunchTerminal, XF86XK_Terminal},
    {Key::LaunchWebBrowser, XF86XK_WWW},

    // System
    {Key::SystemPowerDown, XF86XK_PowerOff},
    {Key::SystemSleep, XF86XK_Sleep},
    {Key::SystemWakeUp, XF86XK_WakeUp},

    // Misc
    {Key::Help, XK_Help},
    {Key::Undo, XK_Undo},
    {Key::Redo, XK_Redo},
    {Key::Find, XK_Find},
    {Key::Cut, XF86XK_Cut},
    {Key::Copy, XF86XK_Copy},
    {Key::Paste, XF86XK_Paste},
    {Key::SelectAll, XK_Select},
};

static std::unordered_map<uint32_t, Key> xks_to_key;
static const bool xks_map_init = []() {
    for (const auto& [k, v] : key_to_xks) xks_to_key[v] = k;
    return true;
}();

uint32_t PlatformKeyMap::to_x11_keysym(Key k) {
    auto it = key_to_xks.find(k);
    return it != key_to_xks.end() ? it->second : 0;
}

Key PlatformKeyMap::from_x11_keysym(uint32_t ks) {
    auto it = xks_to_key.find(ks);
    return it != xks_to_key.end() ? it->second : Key::Raw;
}

uint32_t PlatformKeyMap::to_x11_keycode(Key k, void* display) {
    if (!display) return 0;
    auto ks = to_x11_keysym(k);
    if (ks == 0) return 0;
    return XKeysymToKeycode(static_cast<Display*>(display), ks);
}

const char* PlatformKeyMap::x11_keysym_name(uint32_t ks) {
    // best-effort symbolic name
    switch (ks) {
        case XK_Return: return "XK_Return";
        case XK_Escape: return "XK_Escape";
        case XK_Tab: return "XK_Tab";
        case XK_space: return "XK_space";
        case XK_BackSpace: return "XK_BackSpace";
        case XK_Shift_L: return "XK_Shift_L";
        case XK_Shift_R: return "XK_Shift_R";
        case XK_Control_L: return "XK_Control_L";
        case XK_Control_R: return "XK_Control_R";
        case XK_Alt_L: return "XK_Alt_L";
        case XK_Alt_R: return "XK_Alt_R";
        case XK_Super_L: return "XK_Super_L";
        case XK_Super_R: return "XK_Super_R";
        case XK_Caps_Lock: return "XK_Caps_Lock";
        case XK_Num_Lock: return "XK_Num_Lock";
        case XK_Scroll_Lock: return "XK_Scroll_Lock";
        case XF86XK_AudioMute: return "XF86XK_AudioMute";
        case XF86XK_AudioLowerVolume: return "XF86XK_AudioLowerVolume";
        case XF86XK_AudioRaiseVolume: return "XF86XK_AudioRaiseVolume";
        default: return "XK_UNKNOWN";
    }
}

#endif // __linux__

// =====================================================================
//  Enigo Implementation - Windows
// =====================================================================
#ifdef _WIN32

struct Enigo::Impl {
    std::chrono::milliseconds delay{10};
    std::set<Key> pressed_modifiers;
    KeySequenceConfig seq_config;
    SmoothScrollConfig scroll_config;

    // --- Low-level SendInput helper ---
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
        return PlatformKeyMap::to_virtual_key(k);
    }

    // --- Full SendInput keyboard event with extended flags ---
    void send_keyboard_event(Key k, bool down, uint32_t extra_flags = 0) {
        auto vk = key_to_vk_inner(k);
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = static_cast<WORD>(vk);
        input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        input.ki.dwFlags |= extra_flags;

        // Extended key flag
        if (PlatformKeyMap::is_extended_key(k)) {
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }

        // Scan code for better compatibility
        auto sc = PlatformKeyMap::to_scan_code(k);
        if (sc != 0) {
            input.ki.wScan = static_cast<WORD>(sc & 0xFF);
            if (sc & 0xE000) {
                input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            }
        }

        input.ki.dwExtraInfo = ENIGO_INPUT_EXTRA_VALUE;
        send_input(input);
    }

    // --- Send Unicode character via KEYEVENTF_UNICODE ---
    void send_unicode_char(uint32_t codepoint, bool down) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = 0;
        input.ki.wScan = static_cast<WORD>(codepoint);
        input.ki.dwFlags = KEYEVENTF_UNICODE;
        if (!down) input.ki.dwFlags |= KEYEVENTF_KEYUP;
        input.ki.dwExtraInfo = ENIGO_INPUT_EXTRA_VALUE;
        send_input(input);
    }
};

Enigo::Enigo() : impl_(std::make_unique<Impl>()) {
    spdlog::debug("Enigo initialized (Windows)");
}

Enigo::~Enigo() = default;

// ---- Mouse ----
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
        case MouseButton::Left:    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; break;
        case MouseButton::Right:   input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; break;
        case MouseButton::Middle:  input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN; break;
        case MouseButton::Back:    input.mi.dwFlags = MOUSEEVENTF_XDOWN; input.mi.mouseData = XBUTTON1; break;
        case MouseButton::Forward: input.mi.dwFlags = MOUSEEVENTF_XDOWN; input.mi.mouseData = XBUTTON2; break;
        default: return false;
    }
    impl_->send_input(input);
    return true;
}

bool Enigo::mouse_up(MouseButton button) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    switch (button) {
        case MouseButton::Left:    input.mi.dwFlags = MOUSEEVENTF_LEFTUP; break;
        case MouseButton::Right:   input.mi.dwFlags = MOUSEEVENTF_RIGHTUP; break;
        case MouseButton::Middle:  input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP; break;
        case MouseButton::Back:    input.mi.dwFlags = MOUSEEVENTF_XUP; input.mi.mouseData = XBUTTON1; break;
        case MouseButton::Forward: input.mi.dwFlags = MOUSEEVENTF_XUP; input.mi.mouseData = XBUTTON2; break;
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

// ---- Smooth scroll with acceleration ----
bool Enigo::mouse_smooth_scroll(int32_t delta, MouseAxis axis, const SmoothScrollConfig& config) {
    if (!config.enabled || delta == 0) {
        return mouse_scroll(delta, axis);
    }

    int32_t remaining = std::abs(delta);
    int32_t direction = (delta > 0) ? 1 : -1;
    int32_t total_steps = 0;

    // Calculate total steps based on acceleration curve
    for (int32_t d = remaining; d > 0; d -= std::max(config.min_step,
        static_cast<int32_t>(config.min_step + (config.max_step - config.min_step) *
            (1.0 - std::pow(1.0 - config.acceleration, static_cast<double>(d) / remaining))))) {
        total_steps++;
    }

    remaining = std::abs(delta);
    for (int i = 0; remaining > 0 && i < total_steps; i++) {
        double progress = static_cast<double>(i) / total_steps;
        double curve = std::pow(progress, 1.0 + config.acceleration * 2.0);
        int32_t step_size = static_cast<int32_t>(
            config.min_step + (config.max_step - config.min_step) * curve);
        step_size = std::min(step_size, remaining);
        step_size = std::max(step_size, config.min_step);

        bool ok = mouse_scroll(direction * step_size, axis);
        if (!ok) return false;
        remaining -= step_size;
        if (remaining > 0 && config.step_delay.count() > 0) {
            std::this_thread::sleep_for(config.step_delay);
        }
    }
    return true;
}

// ---- Drag with configurable steps ----
bool Enigo::drag(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y,
                 MouseButton button, const DragConfig& config) {
    mouse_move_to(start_x, start_y);
    if (config.step_delay.count() > 0) std::this_thread::sleep_for(config.step_delay);
    mouse_down(button);
    if (config.step_delay.count() > 0) std::this_thread::sleep_for(config.step_delay);

    int32_t steps = std::max(1, config.step_count);
    for (int32_t i = 1; i <= steps; i++) {
        double t = static_cast<double>(i) / steps;
        int32_t cx = start_x + static_cast<int32_t>((end_x - start_x) * t);
        int32_t cy = start_y + static_cast<int32_t>((end_y - start_y) * t);
        if (config.humanize && i < steps) {
            cx += (rand() % (config.humanize_jitter * 2 + 1)) - config.humanize_jitter;
            cy += (rand() % (config.humanize_jitter * 2 + 1)) - config.humanize_jitter;
        }
        mouse_move_to(cx, cy);
        if (config.step_delay.count() > 0) {
            std::this_thread::sleep_for(config.step_delay);
        }
    }
    mouse_up(button);
    return true;
}

bool Enigo::drag(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y,
                 MouseButton button) {
    return drag(start_x, start_y, end_x, end_y, button, DragConfig{});
}

// ---- Cursor clipping ----
bool Enigo::mouse_clip(const ClipRegion& region) {
    if (!region.active) return mouse_unclip();
    RECT r = {region.x, region.y, region.x + region.width, region.y + region.height};
    return ClipCursor(&r) != 0;
}

bool Enigo::mouse_unclip() {
    return ClipCursor(nullptr) != 0;
}

// ---- Multi-monitor translation ----
std::vector<MonitorInfo> Enigo::monitor_list() {
    std::vector<MonitorInfo> out;
    int id = 0;
    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR hm, HDC, LPRECT rect, LPARAM lp) -> BOOL {
            auto* list = reinterpret_cast<std::vector<MonitorInfo>*>(lp);
            MONITORINFOEX mi = {sizeof(mi)};
            GetMonitorInfo(hm, &mi);
            MonitorInfo info;
            info.id = static_cast<int32_t>(list->size());
            info.x = mi.rcMonitor.left;
            info.y = mi.rcMonitor.top;
            info.width = mi.rcMonitor.right - mi.rcMonitor.left;
            info.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
            info.is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            list->push_back(info);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&out));
    return out;
}

int32_t Enigo::monitor_at(int32_t x, int32_t y) {
    POINT pt = {x, y};
    HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
    if (!hm) return -1;
    auto monitors = monitor_list();
    MONITORINFOEX mi = {sizeof(mi)};
    GetMonitorInfo(hm, &mi);
    for (const auto& m : monitors) {
        if (m.x == mi.rcMonitor.left && m.y == mi.rcMonitor.top) return m.id;
    }
    return -1;
}

std::optional<std::pair<int32_t, int32_t>> Enigo::monitor_translate(
    int32_t x, int32_t y, int32_t from_id, int32_t to_id) {
    auto monitors = monitor_list();
    if (from_id < 0 || to_id < 0 ||
        static_cast<size_t>(from_id) >= monitors.size() ||
        static_cast<size_t>(to_id) >= monitors.size())
        return std::nullopt;
    const auto& src = monitors[from_id];
    const auto& dst = monitors[to_id];
    double fx = static_cast<double>(x - src.x) / src.width;
    double fy = static_cast<double>(y - src.y) / src.height;
    return std::make_pair(
        static_cast<int32_t>(dst.x + fx * dst.width),
        static_cast<int32_t>(dst.y + fy * dst.height));
}

MonitorInfo Enigo::primary_monitor() {
    auto monitors = monitor_list();
    for (const auto& m : monitors) {
        if (m.is_primary) return m;
    }
    return monitors.empty() ? MonitorInfo{} : monitors[0];
}

// ---- Keyboard ----
bool Enigo::key_down(Key key) {
    auto vk = impl_->key_to_vk_inner(key);
    if (vk == 0 && key >= Key::Layout) {
        // Layout character: use Unicode input
        uint32_t ch = static_cast<uint32_t>(key) - static_cast<uint32_t>(Key::Layout);
        impl_->send_unicode_char(ch, true);
        return true;
    }
    if (vk == 0) return false;
    impl_->send_keyboard_event(key, true);

    if (key == Key::Shift || key == Key::RightShift ||
        key == Key::Control || key == Key::RightControl ||
        key == Key::Alt || key == Key::AltGr || key == Key::RightAlt ||
        key == Key::Meta || key == Key::RightMeta) {
        impl_->pressed_modifiers.insert(key);
    }
    return true;
}

bool Enigo::key_up(Key key) {
    auto vk = impl_->key_to_vk_inner(key);
    if (vk == 0 && key >= Key::Layout) {
        uint32_t ch = static_cast<uint32_t>(key) - static_cast<uint32_t>(Key::Layout);
        impl_->send_unicode_char(ch, false);
        return true;
    }
    if (vk == 0) return false;
    impl_->send_keyboard_event(key, false);
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
        if (shift_state & 2) key_down(Key::Control);
        if (shift_state & 4) key_down(Key::Alt);

        raw_key_down(key_code);
        raw_key_up(key_code);

        if (shift_state & 4) key_up(Key::Alt);
        if (shift_state & 2) key_up(Key::Control);
        if (shift_state & 1) key_up(Key::Shift);

        if (impl_->seq_config.char_delay.count() > 0) {
            std::this_thread::sleep_for(impl_->seq_config.char_delay);
        }
    }
    return true;
}

bool Enigo::key_sequence_unicode(const std::string& text) {
    for (char c : text) {
        impl_->send_unicode_char(static_cast<uint8_t>(c), true);
        impl_->send_unicode_char(static_cast<uint8_t>(c), false);
    }
    return true;
}

bool Enigo::key_unicode_codepoint(uint32_t codepoint) {
    impl_->send_unicode_char(codepoint, true);
    impl_->send_unicode_char(codepoint, false);
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
    input.ki.dwExtraInfo = ENIGO_INPUT_EXTRA_VALUE;
    impl_->send_input(input);
    return true;
}

bool Enigo::raw_key_up(uint32_t raw_code) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(raw_code);
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    input.ki.dwExtraInfo = ENIGO_INPUT_EXTRA_VALUE;
    impl_->send_input(input);
    return true;
}

// ---- Extended keyboard features ----
ModifierState Enigo::get_modifier_state() {
    ModifierState ms;
    ms.shift_left  = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
    ms.shift_right = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
    ms.ctrl_left   = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0;
    ms.ctrl_right  = (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0;
    ms.alt_left    = (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0;
    ms.alt_right   = (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
    ms.meta_left   = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0;
    ms.meta_right  = (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
    ms.caps_lock   = (GetKeyState(VK_CAPITAL) & 1) != 0;
    ms.num_lock    = (GetKeyState(VK_NUMLOCK) & 1) != 0;
    ms.scroll_lock = (GetKeyState(VK_SCROLL) & 1) != 0;
    return ms;
}

bool Enigo::key_repeat(Key key, const KeyRepeatConfig& config) {
    auto vk = impl_->key_to_vk_inner(key);
    if (vk == 0) {
        // Try as Layout character
        if (key >= Key::Layout) {
            uint32_t ch = static_cast<uint32_t>(key) - static_cast<uint32_t>(Key::Layout);
            for (int32_t i = 0; i < config.count; i++) {
                impl_->send_unicode_char(ch, true);
                if (config.press_duration.count() > 0)
                    std::this_thread::sleep_for(config.press_duration);
                impl_->send_unicode_char(ch, false);
                if (config.release_duration.count() > 0)
                    std::this_thread::sleep_for(config.release_duration);
                if (i < config.count - 1 && config.inter_delay.count() > 0)
                    std::this_thread::sleep_for(config.inter_delay);
            }
            return true;
        }
        return false;
    }

    for (int32_t i = 0; i < config.count; i++) {
        impl_->send_keyboard_event(key, true);
        if (config.press_duration.count() > 0)
            std::this_thread::sleep_for(config.press_duration);
        impl_->send_keyboard_event(key, false);
        if (config.release_duration.count() > 0)
            std::this_thread::sleep_for(config.release_duration);
        if (i < config.count - 1 && config.inter_delay.count() > 0)
            std::this_thread::sleep_for(config.inter_delay);
    }
    return true;
}

bool Enigo::key_chord(const std::vector<Key>& keys) {
    if (keys.empty()) return true;
    // Press all
    for (auto k : keys) key_down(k);
    // Release in reverse
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) key_up(*it);
    return true;
}

// ---- Config ----
void Enigo::set_sequence_config(const KeySequenceConfig& config) {
    impl_->seq_config = config;
}
KeySequenceConfig Enigo::get_sequence_config() const { return impl_->seq_config; }

void Enigo::set_smooth_scroll_config(const SmoothScrollConfig& config) {
    impl_->scroll_config = config;
}
SmoothScrollConfig Enigo::get_smooth_scroll_config() const { return impl_->scroll_config; }

// =====================================================================
//  Enigo Implementation - Linux X11
// =====================================================================
#elif defined(__linux__)

struct Enigo::Impl {
    Display* display = nullptr;
    std::chrono::milliseconds delay{10};
    std::set<Key> pressed_modifiers;
    KeySequenceConfig seq_config;
    SmoothScrollConfig scroll_config;
    ClipRegion active_clip;
    std::mutex display_mutex;

    Impl() {
        display = XOpenDisplay(nullptr);
        if (!display) {
            spdlog::error("Enigo: Failed to open X11 display");
        } else {
            spdlog::debug("Enigo initialized (Linux/X11) on {}", DisplayString(display));
        }
    }
    ~Impl() {
        if (active_clip.active) {
            XFixesDestroyRegion(display, 0); // best-effort unclip
        }
        if (display) XCloseDisplay(display);
    }

    void flush_and_delay() {
        if (display) XFlush(display);
        if (delay.count() > 0) {
            std::this_thread::sleep_for(delay);
        }
    }

    /// Send a fake key event via XTest
    void send_xkey(Key k, bool down) {
        if (!display) return;
        auto ks = PlatformKeyMap::to_x11_keysym(k);
        if (ks == 0) {
            spdlog::warn("Enigo: No X11 keysym for key {}", static_cast<uint32_t>(k));
            return;
        }
        auto kc = XKeysymToKeycode(display, ks);
        if (kc == 0) {
            spdlog::warn("Enigo: No X11 keycode for keysym 0x{:x}", ks);
            return;
        }
        XTestFakeKeyEvent(display, kc, down ? True : False, CurrentTime);
        flush_and_delay();
    }

    /// Send mouse button via XTest
    bool x_mouse_button(uint32_t btn, bool down) {
        if (!display) return false;
        XTestFakeButtonEvent(display, btn, down ? True : False, CurrentTime);
        flush_and_delay();
        return true;
    }
};

Enigo::Enigo() : impl_(std::make_unique<Impl>()) {}
Enigo::~Enigo() = default;

// ---- Mouse ----
bool Enigo::mouse_move_to(int32_t x, int32_t y) {
    if (!impl_->display) return false;
    XTestFakeMotionEvent(impl_->display, 0, x, y, CurrentTime);
    impl_->flush_and_delay();
    return true;
}

bool Enigo::mouse_move_relative(int32_t dx, int32_t dy) {
    if (!impl_->display) return false;
    XTestFakeRelativeMotionEvent(impl_->display, dx, dy, CurrentTime);
    impl_->flush_and_delay();
    return true;
}

static uint32_t mouse_button_to_x11(MouseButton b) {
    switch (b) {
        case MouseButton::Left:  return 1;
        case MouseButton::Middle:return 2;
        case MouseButton::Right: return 3;
        case MouseButton::ScrollUp: return 4;
        case MouseButton::ScrollDown: return 5;
        case MouseButton::ScrollLeft: return 6;
        case MouseButton::ScrollRight: return 7;
        case MouseButton::Back: return 8;
        case MouseButton::Forward: return 9;
        default: return 0;
    }
}

bool Enigo::mouse_down(MouseButton button) {
    return impl_->x_mouse_button(mouse_button_to_x11(button), true);
}

bool Enigo::mouse_up(MouseButton button) {
    return impl_->x_mouse_button(mouse_button_to_x11(button), false);
}

bool Enigo::mouse_scroll(int32_t delta, MouseAxis axis) {
    if (!impl_->display) return false;
    int btn;
    if (axis == MouseAxis::Vertical)
        btn = (delta > 0) ? 4 : 5;
    else
        btn = (delta > 0) ? 6 : 7;
    for (int32_t i = 0; i < std::abs(delta); i++) {
        XTestFakeButtonEvent(impl_->display, btn, True, CurrentTime);
        XTestFakeButtonEvent(impl_->display, btn, False, CurrentTime);
    }
    impl_->flush_and_delay();
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

// ---- Smooth scroll ----
bool Enigo::mouse_smooth_scroll(int32_t delta, MouseAxis axis, const SmoothScrollConfig& config) {
    if (!config.enabled || delta == 0) return mouse_scroll(delta, axis);

    int32_t remaining = std::abs(delta);
    int32_t direction = (delta > 0) ? 1 : -1;
    int32_t btn = (axis == MouseAxis::Vertical) ?
        ((direction > 0) ? 4 : 5) : ((direction > 0) ? 6 : 7);

    int32_t total_steps = std::max(1,
        static_cast<int32_t>(remaining / static_cast<double>(config.min_step)));
    total_steps = std::min(total_steps, remaining);

    for (int32_t i = 0; i < total_steps; i++) {
        double progress = static_cast<double>(i) / total_steps;
        double curve = std::pow(progress, 1.0 + config.acceleration * 2.0);
        int32_t steps_here = std::max(1, static_cast<int32_t>(
            config.min_step + (config.max_step - config.min_step) * curve));
        steps_here = std::min(steps_here, remaining);

        for (int32_t s = 0; s < steps_here; s++) {
            XTestFakeButtonEvent(impl_->display, btn, True, CurrentTime);
            XTestFakeButtonEvent(impl_->display, btn, False, CurrentTime);
        }
        remaining -= steps_here;
        if (remaining <= 0) break;
        if (config.step_delay.count() > 0)
            std::this_thread::sleep_for(config.step_delay);
    }
    impl_->flush_and_delay();
    return true;
}

// ---- Drag ----
bool Enigo::drag(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y,
                 MouseButton button, const DragConfig& config) {
    mouse_move_to(start_x, start_y);
    if (config.step_delay.count() > 0) std::this_thread::sleep_for(config.step_delay);
    mouse_down(button);
    if (config.step_delay.count() > 0) std::this_thread::sleep_for(config.step_delay);

    int32_t steps = std::max(1, config.step_count);
    for (int32_t i = 1; i <= steps; i++) {
        double t = static_cast<double>(i) / steps;
        int32_t cx = start_x + static_cast<int32_t>((end_x - start_x) * t);
        int32_t cy = start_y + static_cast<int32_t>((end_y - start_y) * t);
        if (config.humanize && i < steps) {
            cx += (rand() % (config.humanize_jitter * 2 + 1)) - config.humanize_jitter;
            cy += (rand() % (config.humanize_jitter * 2 + 1)) - config.humanize_jitter;
        }
        XTestFakeMotionEvent(impl_->display, 0, cx, cy, CurrentTime);
        if (config.step_delay.count() > 0)
            std::this_thread::sleep_for(config.step_delay);
    }
    XFlush(impl_->display);
    mouse_up(button);
    return true;
}

bool Enigo::drag(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y,
                 MouseButton button) {
    return drag(start_x, start_y, end_x, end_y, button, DragConfig{});
}

// ---- Cursor clipping (via XFixes) ----
bool Enigo::mouse_clip(const ClipRegion& region) {
    if (!impl_->display) return false;
    if (!region.active) return mouse_unclip();

    // XFixes-based cursor confinement
    int event_base, error_base;
    if (!XFixesQueryExtension(impl_->display, &event_base, &error_base)) {
        spdlog::warn("Enigo: XFixes not available for cursor clipping");
        // Fallback: XGrabPointer (aggressive but works)
        return XGrabPointer(impl_->display, DefaultRootWindow(impl_->display),
            True, 0, GrabModeAsync, GrabModeAsync,
            DefaultRootWindow(impl_->display), None, CurrentTime) == GrabSuccess;
    }

    // Try creating a barrier (subtle approach — won't work perfectly but better than nothing)
    // Best-effort: use XGrabPointer as a simpler clip
    impl_->active_clip = region;
    return XGrabPointer(impl_->display, DefaultRootWindow(impl_->display),
        False,
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync, GrabModeAsync, None, None, CurrentTime) == GrabSuccess;
}

bool Enigo::mouse_unclip() {
    if (!impl_->display) return false;
    impl_->active_clip.active = false;
    XUngrabPointer(impl_->display, CurrentTime);
    XFlush(impl_->display);
    return true;
}

// ---- Multi-monitor via Xinerama ----
std::vector<MonitorInfo> Enigo::monitor_list() {
    std::vector<MonitorInfo> out;
    if (!impl_->display) return out;

    int num = 0;
    XineramaScreenInfo* screens = XineramaQueryScreens(impl_->display, &num);
    if (!screens || num <= 0) {
        // Fallback: single monitor from root window
        Screen* screen = DefaultScreenOfDisplay(impl_->display);
        MonitorInfo mi;
        mi.id = 0;
        mi.x = 0; mi.y = 0;
        mi.width = WidthOfScreen(screen);
        mi.height = HeightOfScreen(screen);
        mi.is_primary = true;
        out.push_back(mi);
        return out;
    }

    for (int i = 0; i < num; i++) {
        MonitorInfo mi;
        mi.id = static_cast<int32_t>(i);
        mi.x = screens[i].x_org;
        mi.y = screens[i].y_org;
        mi.width = screens[i].width;
        mi.height = screens[i].height;
        mi.is_primary = (screens[i].x_org == 0 && screens[i].y_org == 0);
        out.push_back(mi);
    }
    XFree(screens);
    return out;
}

int32_t Enigo::monitor_at(int32_t x, int32_t y) {
    auto monitors = monitor_list();
    for (const auto& m : monitors) {
        if (x >= m.x && x < m.x + m.width && y >= m.y && y < m.y + m.height)
            return m.id;
    }
    return -1;
}

std::optional<std::pair<int32_t, int32_t>> Enigo::monitor_translate(
    int32_t x, int32_t y, int32_t from_id, int32_t to_id) {
    auto monitors = monitor_list();
    if (from_id < 0 || to_id < 0 ||
        static_cast<size_t>(from_id) >= monitors.size() ||
        static_cast<size_t>(to_id) >= monitors.size())
        return std::nullopt;
    const auto& src = monitors[from_id];
    const auto& dst = monitors[to_id];
    double fx = src.width > 0 ? static_cast<double>(x - src.x) / src.width : 0.5;
    double fy = src.height > 0 ? static_cast<double>(y - src.y) / src.height : 0.5;
    return std::make_pair(
        static_cast<int32_t>(dst.x + fx * dst.width),
        static_cast<int32_t>(dst.y + fy * dst.height));
}

MonitorInfo Enigo::primary_monitor() {
    auto monitors = monitor_list();
    for (const auto& m : monitors) if (m.is_primary) return m;
    return monitors.empty() ? MonitorInfo{} : monitors[0];
}

// ---- Keyboard ----
bool Enigo::key_down(Key key) {
    impl_->send_xkey(key, true);
    if (key == Key::Shift || key == Key::RightShift ||
        key == Key::Control || key == Key::RightControl ||
        key == Key::Alt || key == Key::AltGr || key == Key::RightAlt ||
        key == Key::Meta || key == Key::RightMeta) {
        impl_->pressed_modifiers.insert(key);
    }
    return true;
}

bool Enigo::key_up(Key key) {
    impl_->send_xkey(key, false);
    impl_->pressed_modifiers.erase(key);
    return true;
}

bool Enigo::key_sequence(const std::string& sequence) {
    for (char c : sequence) {
        Key key = static_cast<Key>(static_cast<uint32_t>(Key::Layout) + static_cast<uint8_t>(c));
        key_down(key);
        key_up(key);
        if (impl_->seq_config.char_delay.count() > 0)
            std::this_thread::sleep_for(impl_->seq_config.char_delay);
    }
    return true;
}

bool Enigo::key_sequence_unicode(const std::string& text) {
    // Linux: use XTest with UCS→KeySym→Keycode lookup
    if (!impl_->display) return false;
    for (char c : text) {
        // Direct UCS-to-KeySym mapping
        KeySym ks = static_cast<KeySym>(static_cast<uint8_t>(c));
        auto kc = XKeysymToKeycode(impl_->display, ks);
        if (kc == 0) {
            // Try Latin-1 fallback
            ks = static_cast<KeySym>(static_cast<uint8_t>(c)) | 0x01000000;
            kc = XKeysymToKeycode(impl_->display, ks);
        }
        if (kc != 0) {
            XTestFakeKeyEvent(impl_->display, kc, True, CurrentTime);
            XTestFakeKeyEvent(impl_->display, kc, False, CurrentTime);
        }
    }
    impl_->flush_and_delay();
    return true;
}

bool Enigo::key_unicode_codepoint(uint32_t codepoint) {
    if (!impl_->display) return false;
    auto kc = XKeysymToKeycode(impl_->display, codepoint);
    if (kc != 0) {
        XTestFakeKeyEvent(impl_->display, kc, True, CurrentTime);
        XTestFakeKeyEvent(impl_->display, kc, False, CurrentTime);
        impl_->flush_and_delay();
    }
    return kc != 0;
}

bool Enigo::get_key_state(Key key) {
    if (!impl_->display) return false;
    auto ks = PlatformKeyMap::to_x11_keysym(key);
    if (ks == 0) return false;
    auto kc = XKeysymToKeycode(impl_->display, ks);
    if (kc == 0) return false;
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
    for (auto mod : impl_->pressed_modifiers) key_up(mod);
    impl_->pressed_modifiers.clear();
}

bool Enigo::raw_key_down(uint32_t raw_code) {
    if (!impl_->display) return false;
    XTestFakeKeyEvent(impl_->display, raw_code, True, CurrentTime);
    impl_->flush_and_delay();
    return true;
}

bool Enigo::raw_key_up(uint32_t raw_code) {
    if (!impl_->display) return false;
    XTestFakeKeyEvent(impl_->display, raw_code, False, CurrentTime);
    impl_->flush_and_delay();
    return true;
}

// ---- Extended keyboard features ----
ModifierState Enigo::get_modifier_state() {
    ModifierState ms;
    if (!impl_->display) return ms;

    // Query Xkb modifier state
    XkbStateRec xkb_state;
    XkbGetState(impl_->display, XkbUseCoreKbd, &xkb_state);

    ms.shift_left  = (xkb_state.mods & ShiftMask) != 0;
    ms.shift_right = ms.shift_left; // X11 doesn't distinguish left/right well
    ms.ctrl_left   = (xkb_state.mods & ControlMask) != 0;
    ms.ctrl_right  = ms.ctrl_left;
    ms.alt_left    = (xkb_state.mods & Mod1Mask) != 0;
    ms.alt_right   = ms.alt_left;
    ms.meta_left   = (xkb_state.mods & Mod4Mask) != 0;
    ms.meta_right  = ms.meta_left;

    ms.caps_lock   = (xkb_state.locked_mods & LockMask) != 0;
    ms.num_lock    = (xkb_state.locked_mods & Mod2Mask) != 0;
    ms.scroll_lock = (xkb_state.locked_mods & Mod3Mask) != 0;

    return ms;
}

bool Enigo::key_repeat(Key key, const KeyRepeatConfig& config) {
    auto ks = PlatformKeyMap::to_x11_keysym(key);
    if (ks == 0 && key >= Key::Layout) {
        // Layout character
        for (int32_t i = 0; i < config.count; i++) {
            key_down(key);
            if (config.press_duration.count() > 0)
                std::this_thread::sleep_for(config.press_duration);
            key_up(key);
            if (config.release_duration.count() > 0)
                std::this_thread::sleep_for(config.release_duration);
            if (i < config.count - 1 && config.inter_delay.count() > 0)
                std::this_thread::sleep_for(config.inter_delay);
        }
        return true;
    }
    if (ks == 0 || !impl_->display) return false;

    auto kc = XKeysymToKeycode(impl_->display, ks);
    for (int32_t i = 0; i < config.count; i++) {
        XTestFakeKeyEvent(impl_->display, kc, True, CurrentTime);
        if (config.press_duration.count() > 0)
            std::this_thread::sleep_for(config.press_duration);
        XTestFakeKeyEvent(impl_->display, kc, False, CurrentTime);
        if (config.release_duration.count() > 0)
            std::this_thread::sleep_for(config.release_duration);
        if (i < config.count - 1 && config.inter_delay.count() > 0)
            std::this_thread::sleep_for(config.inter_delay);
    }
    impl_->flush_and_delay();
    return true;
}

bool Enigo::key_chord(const std::vector<Key>& keys) {
    if (keys.empty()) return true;
    for (auto k : keys) key_down(k);
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) key_up(*it);
    return true;
}

void Enigo::set_sequence_config(const KeySequenceConfig& config) {
    impl_->seq_config = config;
}
KeySequenceConfig Enigo::get_sequence_config() const { return impl_->seq_config; }

void Enigo::set_smooth_scroll_config(const SmoothScrollConfig& config) {
    impl_->scroll_config = config;
}
SmoothScrollConfig Enigo::get_smooth_scroll_config() const { return impl_->scroll_config; }

// =====================================================================
//  Enigo Implementation - macOS
// =====================================================================
#elif defined(__APPLE__)

struct Enigo::Impl {
    std::chrono::milliseconds delay{10};
    std::set<Key> pressed_modifiers;
    KeySequenceConfig seq_config;
    SmoothScrollConfig scroll_config;
    ClipRegion active_clip;

    void delay_if_needed() {
        if (delay.count() > 0) std::this_thread::sleep_for(delay);
    }

    CGKeyCode key_to_cgcode(Key k) {
        return static_cast<CGKeyCode>(PlatformKeyMap::to_cg_keycode(k));
    }

    CGEventFlags modifier_to_flags(Key k) {
        return static_cast<CGEventFlags>(PlatformKeyMap::to_cg_event_flags(k));
    }

    CGMouseButton cg_mouse_button(MouseButton b) {
        switch (b) {
            case MouseButton::Left:   return kCGMouseButtonLeft;
            case MouseButton::Right:  return kCGMouseButtonRight;
            case MouseButton::Middle: return kCGMouseButtonCenter;
            default: return kCGMouseButtonLeft;
        }
    }

    /// Build keyboard event with full modifier flags
    CGEventRef create_keyboard_event(CGKeyCode code, bool down) {
        CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, code, down);
        // Set active modifier flags
        CGEventFlags flags = 0;
        for (auto mod : pressed_modifiers) {
            flags |= modifier_to_flags(mod);
        }
        CGEventSetFlags(ev, flags);
        return ev;
    }
};

Enigo::Enigo() : impl_(std::make_unique<Impl>()) {
    spdlog::debug("Enigo initialized (macOS)");
}
Enigo::~Enigo() = default;

// ---- Mouse ----
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

bool Enigo::mouse_down(MouseButton button) {
    CGEventType type;
    switch (button) {
        case MouseButton::Left:   type = kCGEventLeftMouseDown; break;
        case MouseButton::Right:  type = kCGEventRightMouseDown; break;
        case MouseButton::Middle: type = kCGEventOtherMouseDown; break;
        case MouseButton::Back:   type = kCGEventOtherMouseDown; break;
        case MouseButton::Forward:type = kCGEventOtherMouseDown; break;
        default: return false;
    }
    CGEventRef ev = CGEventCreateMouseEvent(nullptr, type,
        CGEventGetLocation(CGEventCreate(nullptr)),
        impl_->cg_mouse_button(button));
    CGEventSetIntegerValueField(ev, kCGMouseEventButtonNumber,
        static_cast<int64_t>(button == MouseButton::Back ? 3 : button == MouseButton::Forward ? 4 : 0));
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
    return true;
}

bool Enigo::mouse_up(MouseButton button) {
    CGEventType type;
    switch (button) {
        case MouseButton::Left:   type = kCGEventLeftMouseUp; break;
        case MouseButton::Right:  type = kCGEventRightMouseUp; break;
        case MouseButton::Middle: type = kCGEventOtherMouseUp; break;
        case MouseButton::Back:   type = kCGEventOtherMouseUp; break;
        case MouseButton::Forward:type = kCGEventOtherMouseUp; break;
        default: return false;
    }
    CGEventRef ev = CGEventCreateMouseEvent(nullptr, type,
        CGEventGetLocation(CGEventCreate(nullptr)),
        impl_->cg_mouse_button(button));
    CGEventSetIntegerValueField(ev, kCGMouseEventButtonNumber,
        static_cast<int64_t>(button == MouseButton::Back ? 3 : button == MouseButton::Forward ? 4 : 0));
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

// ---- Smooth scroll ----
bool Enigo::mouse_smooth_scroll(int32_t delta, MouseAxis axis, const SmoothScrollConfig& config) {
    if (!config.enabled || delta == 0) return mouse_scroll(delta, axis);

    int32_t remaining = std::abs(delta);
    int32_t direction = (delta > 0) ? 1 : -1;

    int32_t total_steps = std::max(1,
        static_cast<int32_t>(remaining / static_cast<double>(config.min_step)));
    total_steps = std::min(total_steps, remaining);

    for (int32_t i = 0; i < total_steps; i++) {
        double progress = static_cast<double>(i) / total_steps;
        double curve = std::pow(progress, 1.0 + config.acceleration * 2.0);
        int32_t step_size = std::max(1, static_cast<int32_t>(
            config.min_step + (config.max_step - config.min_step) * curve));
        step_size = std::min(step_size, remaining);

        CGEventRef ev = CGEventCreateScrollWheelEvent(nullptr,
            axis == MouseAxis::Vertical ? kCGScrollEventUnitLine : kCGScrollEventUnitPixel,
            axis == MouseAxis::Vertical ? 1 : 2,
            axis == MouseAxis::Vertical ? direction * step_size : 0,
            axis == MouseAxis::Horizontal ? direction * step_size : 0);
        CGEventPost(kCGHIDEventTap, ev);
        CFRelease(ev);

        remaining -= step_size;
        if (remaining <= 0) break;
        if (config.step_delay.count() > 0)
            std::this_thread::sleep_for(config.step_delay);
    }
    return true;
}

// ---- Drag ----
bool Enigo::drag(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y,
                 MouseButton button, const DragConfig& config) {
    mouse_move_to(start_x, start_y);
    if (config.step_delay.count() > 0) std::this_thread::sleep_for(config.step_delay);
    mouse_down(button);
    if (config.step_delay.count() > 0) std::this_thread::sleep_for(config.step_delay);

    int32_t steps = std::max(1, config.step_count);
    for (int32_t i = 1; i <= steps; i++) {
        double t = static_cast<double>(i) / steps;
        int32_t cx = start_x + static_cast<int32_t>((end_x - start_x) * t);
        int32_t cy = start_y + static_cast<int32_t>((end_y - start_y) * t);
        if (config.humanize && i < steps) {
            cx += (rand() % (config.humanize_jitter * 2 + 1)) - config.humanize_jitter;
            cy += (rand() % (config.humanize_jitter * 2 + 1)) - config.humanize_jitter;
        }
        mouse_move_to(cx, cy);
        if (config.step_delay.count() > 0)
            std::this_thread::sleep_for(config.step_delay);
    }
    mouse_up(button);
    return true;
}

bool Enigo::drag(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y,
                 MouseButton button) {
    return drag(start_x, start_y, end_x, end_y, button, DragConfig{});
}

// ---- Cursor clipping (macOS — CGAssociateMouseAndMouseCursorPosition) ----
bool Enigo::mouse_clip(const ClipRegion& region) {
    if (!region.active) return mouse_unclip();
    impl_->active_clip = region;
    // macOS doesn't have a direct cursor clip API equivalent.
    // CGAssociateMouseAndMouseCursorPosition can disconnect the mouse from the cursor,
    // which combined with warping can simulate confinement.
    // Best-effort: store the region and warp back if cursor escapes.
    // CGWarpMouseCursorPosition can be used to enforce bounds.
    // For now, just set a flag — a polling thread would be needed for enforcement.
    CGPoint center = CGPointMake(region.x + region.width/2.0, region.y + region.height/2.0);
    CGWarpMouseCursorPosition(center);
    CGAssociateMouseAndMouseCursorPosition(true);
    return true;
}

bool Enigo::mouse_unclip() {
    impl_->active_clip.active = false;
    CGAssociateMouseAndMouseCursorPosition(true);
    return true;
}

// ---- Multi-monitor via CGDisplay ----
std::vector<MonitorInfo> Enigo::monitor_list() {
    std::vector<MonitorInfo> out;
    uint32_t count = 0;
    CGGetActiveDisplayList(0, nullptr, &count);
    if (count == 0) return out;

    std::vector<CGDirectDisplayID> displays(count);
    CGGetActiveDisplayList(count, displays.data(), &count);

    for (uint32_t i = 0; i < count; i++) {
        auto disp = displays[i];
        CGRect bounds = CGDisplayBounds(disp);
        MonitorInfo mi;
        mi.id = static_cast<int32_t>(i);
        mi.x = static_cast<int32_t>(bounds.origin.x);
        mi.y = static_cast<int32_t>(bounds.origin.y);
        mi.width = static_cast<int32_t>(bounds.size.width);
        mi.height = static_cast<int32_t>(bounds.size.height);
        mi.is_primary = CGDisplayIsMain(disp);
        out.push_back(mi);
    }
    return out;
}

int32_t Enigo::monitor_at(int32_t x, int32_t y) {
    auto monitors = monitor_list();
    for (const auto& m : monitors) {
        if (x >= m.x && x < m.x + m.width && y >= m.y && y < m.y + m.height)
            return m.id;
    }
    return -1;
}

std::optional<std::pair<int32_t, int32_t>> Enigo::monitor_translate(
    int32_t x, int32_t y, int32_t from_id, int32_t to_id) {
    auto monitors = monitor_list();
    if (from_id < 0 || to_id < 0 ||
        static_cast<size_t>(from_id) >= monitors.size() ||
        static_cast<size_t>(to_id) >= monitors.size())
        return std::nullopt;
    const auto& src = monitors[from_id];
    const auto& dst = monitors[to_id];
    double fx = src.width > 0 ? static_cast<double>(x - src.x) / src.width : 0.5;
    double fy = src.height > 0 ? static_cast<double>(y - src.y) / src.height : 0.5;
    return std::make_pair(
        static_cast<int32_t>(dst.x + fx * dst.width),
        static_cast<int32_t>(dst.y + fy * dst.height));
}

MonitorInfo Enigo::primary_monitor() {
    auto monitors = monitor_list();
    for (const auto& m : monitors) if (m.is_primary) return m;
    return monitors.empty() ? MonitorInfo{} : monitors[0];
}

// ---- Keyboard ----
bool Enigo::key_down(Key key) {
    auto cg = impl_->key_to_cgcode(key);
    if (cg == 0) return false;
    CGEventRef ev = impl_->create_keyboard_event(cg, true);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
    impl_->delay_if_needed();
    impl_->pressed_modifiers.insert(key);
    return true;
}

bool Enigo::key_up(Key key) {
    auto cg = impl_->key_to_cgcode(key);
    if (cg == 0) return false;
    CGEventRef ev = impl_->create_keyboard_event(cg, false);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
    impl_->delay_if_needed();
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

        if (impl_->seq_config.char_delay.count() > 0)
            std::this_thread::sleep_for(impl_->seq_config.char_delay);
    }
    return true;
}

bool Enigo::key_sequence_unicode(const std::string& text) {
    return key_sequence(text); // macOS key_sequence already uses Unicode
}

bool Enigo::key_unicode_codepoint(uint32_t codepoint) {
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, 0, true);
    UniChar ch = static_cast<UniChar>(codepoint);
    CGEventKeyboardSetUnicodeString(ev, 1, &ch);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);

    ev = CGEventCreateKeyboardEvent(nullptr, 0, false);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
    return true;
}

bool Enigo::get_key_state(Key key) {
    return CGEventSourceKeyState(kCGEventSourceStateHIDSystemState,
        impl_->key_to_cgcode(key));
}

bool Enigo::get_caps_lock_state() {
    return CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState)
        & kCGEventFlagMaskAlphaShift;
}

bool Enigo::get_num_lock_state() {
    return CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState)
        & kCGEventFlagMaskNumericPad;
}

void Enigo::release_all() {
    for (auto mod : impl_->pressed_modifiers) key_up(mod);
    impl_->pressed_modifiers.clear();
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

// ---- Extended keyboard features ----
ModifierState Enigo::get_modifier_state() {
    ModifierState ms;
    CGEventFlags flags = CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState);

    ms.shift_left  = (flags & kCGEventFlagMaskShift) != 0;
    ms.shift_right = ms.shift_left;  // macOS doesn't separate left/right
    ms.ctrl_left   = (flags & kCGEventFlagMaskControl) != 0;
    ms.ctrl_right  = ms.ctrl_left;
    ms.alt_left    = (flags & kCGEventFlagMaskAlternate) != 0;
    ms.alt_right   = ms.alt_left;
    ms.meta_left   = (flags & kCGEventFlagMaskCommand) != 0;
    ms.meta_right  = ms.meta_left;
    ms.caps_lock   = (flags & kCGEventFlagMaskAlphaShift) != 0;
    ms.num_lock    = (flags & kCGEventFlagMaskNumericPad) != 0;
    ms.scroll_lock = (flags & kCGEventFlagMaskScrollLock) != 0;
    return ms;
}

bool Enigo::key_repeat(Key key, const KeyRepeatConfig& config) {
    auto cg = impl_->key_to_cgcode(key);
    if (cg == 0) return false;

    for (int32_t i = 0; i < config.count; i++) {
        CGEventRef ev = impl_->create_keyboard_event(cg, true);
        CGEventPost(kCGHIDEventTap, ev);
        CFRelease(ev);
        if (config.press_duration.count() > 0)
            std::this_thread::sleep_for(config.press_duration);

        ev = impl_->create_keyboard_event(cg, false);
        CGEventPost(kCGHIDEventTap, ev);
        CFRelease(ev);
        if (config.release_duration.count() > 0)
            std::this_thread::sleep_for(config.release_duration);

        if (i < config.count - 1 && config.inter_delay.count() > 0)
            std::this_thread::sleep_for(config.inter_delay);
    }
    return true;
}

bool Enigo::key_chord(const std::vector<Key>& keys) {
    if (keys.empty()) return true;
    for (auto k : keys) key_down(k);
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) key_up(*it);
    return true;
}

void Enigo::set_sequence_config(const KeySequenceConfig& config) {
    impl_->seq_config = config;
}
KeySequenceConfig Enigo::get_sequence_config() const { return impl_->seq_config; }

void Enigo::set_smooth_scroll_config(const SmoothScrollConfig& config) {
    impl_->scroll_config = config;
}
SmoothScrollConfig Enigo::get_smooth_scroll_config() const { return impl_->scroll_config; }

#endif

// =====================================================================
//  Common Methods (all platforms)
// =====================================================================

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

// =====================================================================
//  DSL Parser — comprehensive implementation
// =====================================================================

static const std::unordered_map<std::string, Key> dsl_key_map = {
    // Modifiers
    {"SHIFT", Key::Shift},         {"LSHIFT", Key::Shift},
    {"RSHIFT", Key::RightShift},   {"CTRL", Key::Control},
    {"CONTROL", Key::Control},     {"LCTRL", Key::Control},
    {"RCTRL", Key::RightControl},  {"ALT", Key::Alt},
    {"LALT", Key::Alt},            {"RALT", Key::RightAlt},
    {"ALTGR", Key::AltGr},         {"META", Key::Meta},
    {"CMD", Key::Meta},            {"SUPER", Key::Meta},
    {"WIN", Key::Meta},            {"LMETA", Key::Meta},
    {"RMETA", Key::RightMeta},

    // Whitespace
    {"ENTER", Key::Return},        {"RETURN", Key::Return},
    {"TAB", Key::Tab},             {"SPACE", Key::Space},
    {"ESC", Key::Escape},          {"ESCAPE", Key::Escape},
    {"BS", Key::Backspace},        {"BACKSPACE", Key::Backspace},
    {"DEL", Key::Delete},          {"DELETE", Key::Delete},
    {"INS", Key::Insert},          {"INSERT", Key::Insert},

    // Navigation
    {"UP", Key::UpArrow},          {"DOWN", Key::DownArrow},
    {"LEFT", Key::LeftArrow},      {"RIGHT", Key::RightArrow},
    {"HOME", Key::Home},           {"END", Key::End},
    {"PGUP", Key::PageUp},         {"PAGEUP", Key::PageUp},
    {"PGDN", Key::PageDown},       {"PAGEDOWN", Key::PageDown},

    // Locks
    {"CAPS", Key::CapsLock},       {"CAPSLOCK", Key::CapsLock},
    {"NUMLOCK", Key::NumLock},     {"SCROLLLOCK", Key::ScrollLock},

    // Function keys
    {"F1", Key::F1},   {"F2", Key::F2},   {"F3", Key::F3},   {"F4", Key::F4},
    {"F5", Key::F5},   {"F6", Key::F6},   {"F7", Key::F7},   {"F8", Key::F8},
    {"F9", Key::F9},   {"F10", Key::F10}, {"F11", Key::F11}, {"F12", Key::F12},
    {"F13", Key::F13}, {"F14", Key::F14}, {"F15", Key::F15}, {"F16", Key::F16},
    {"F17", Key::F17}, {"F18", Key::F18}, {"F19", Key::F19}, {"F20", Key::F20},
    {"F21", Key::F21}, {"F22", Key::F22}, {"F23", Key::F23}, {"F24", Key::F24},

    // System
    {"PRINTSCREEN", Key::PrintScreen}, {"PRTSC", Key::PrintScreen},
    {"PAUSE", Key::Pause},             {"BREAK", Key::Pause},
    {"MENU", Key::Menu},               {"APPS", Key::Menu},

    // Numpad
    {"NUMPAD0", Key::Numpad0}, {"NUMPAD1", Key::Numpad1}, {"NUMPAD2", Key::Numpad2},
    {"NUMPAD3", Key::Numpad3}, {"NUMPAD4", Key::Numpad4}, {"NUMPAD5", Key::Numpad5},
    {"NUMPAD6", Key::Numpad6}, {"NUMPAD7", Key::Numpad7}, {"NUMPAD8", Key::Numpad8},
    {"NUMPAD9", Key::Numpad9},
    {"NUMPAD_ADD", Key::NumpadAdd},         {"NUMPAD_SUBTRACT", Key::NumpadSubtract},
    {"NUMPAD_MULTIPLY", Key::NumpadMultiply},{"NUMPAD_DIVIDE", Key::NumpadDivide},
    {"NUMPAD_DECIMAL", Key::NumpadDecimal}, {"NUMPAD_ENTER", Key::NumpadEnter},

    // Media
    {"VOLUME_MUTE", Key::VolumeMute},       {"MUTE", Key::VolumeMute},
    {"VOLUME_DOWN", Key::VolumeDown},       {"VOL_DOWN", Key::VolumeDown},
    {"VOLUME_UP", Key::VolumeUp},           {"VOL_UP", Key::VolumeUp},
    {"MEDIA_PLAY", Key::MediaPlay},         {"MEDIA_PAUSE", Key::MediaPause},
    {"MEDIA_STOP", Key::MediaStop},         {"MEDIA_NEXT", Key::MediaNext},
    {"MEDIA_PREV", Key::MediaPrev},         {"MEDIA_PLAY_PAUSE", Key::MediaPlayPause},

    // Browser
    {"BROWSER_BACK", Key::BrowserBack},     {"BROWSER_FORWARD", Key::BrowserForward},
    {"BROWSER_REFRESH", Key::BrowserRefresh},{"BROWSER_STOP", Key::BrowserStop},
    {"BROWSER_SEARCH", Key::BrowserSearch}, {"BROWSER_FAVORITES", Key::BrowserFavorites},
    {"BROWSER_HOME", Key::BrowserHome},

    // Launch
    {"LAUNCH_MAIL", Key::LaunchMail},       {"MAIL", Key::LaunchMail},
    {"LAUNCH_MEDIA", Key::LaunchMedia},     {"LAUNCH_CALC", Key::LaunchCalculator},
    {"CALCULATOR", Key::LaunchCalculator},
};

bool DslParser::is_valid_key_name(const std::string& name) {
    return dsl_key_map.find(name) != dsl_key_map.end();
}

std::vector<std::string> DslParser::registered_key_names() {
    std::vector<std::string> out;
    out.reserve(dsl_key_map.size());
    for (const auto& [name, _] : dsl_key_map) out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

// Helper: parse a duration like "100ms", "1s", "500us"
static std::optional<std::chrono::milliseconds> parse_duration(const std::string& s) {
    if (s.empty()) return std::nullopt;
    try {
        std::string num_str;
        size_t i = 0;
        while (i < s.size() && (std::isdigit(s[i]) || s[i] == '.' || s[i] == '-')) {
            num_str += s[i++];
        }
        if (num_str.empty()) return std::nullopt;
        double value = std::stod(num_str);
        std::string unit = s.substr(i);
        if (unit == "ms" || unit.empty())
            return std::chrono::milliseconds(static_cast<int64_t>(value));
        if (unit == "s")
            return std::chrono::milliseconds(static_cast<int64_t>(value * 1000));
        if (unit == "us")
            return std::chrono::milliseconds(static_cast<int64_t>(value / 1000));
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

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

            // --- DELAY ---
            if (cmd.rfind("DELAY", 0) == 0 || cmd.rfind("delay", 0) == 0) {
                std::string dur_str = cmd.size() > 5 ? cmd.substr(cmd[0] == 'D' ? 5 : 5) : "";
                // Skip whitespace after DELAY
                size_t ns = 0;
                while (ns < dur_str.size() && std::isspace(dur_str[ns])) ns++;
                dur_str = dur_str.substr(ns);
                auto dur = parse_duration(dur_str);
                tokens.push_back({DslToken::DELAY, "", Key::Space, {}, 0,
                    dur.value_or(std::chrono::milliseconds{100})});
                continue;
            }

            // --- Modifier ± with optional timing ---
            if (cmd[0] == '+' || cmd[0] == '-') {
                bool add = cmd[0] == '+';
                std::string rest = cmd.substr(1);

                // Check for space-separated timing: +SHIFT 100ms
                std::string key_name;
                std::string timing_str;
                auto space_pos = rest.find(' ');
                if (space_pos != std::string::npos) {
                    key_name = rest.substr(0, space_pos);
                    timing_str = rest.substr(space_pos + 1);
                } else {
                    key_name = rest;
                }

                auto it = dsl_key_map.find(key_name);
                if (it != dsl_key_map.end()) {
                    auto dur = parse_duration(timing_str);
                    if (add) {
                        active_mods.insert(it->second);
                        auto type = dur.has_value() ? DslToken::TIMED_MOD_START
                                                     : DslToken::MODIFIER_START;
                        tokens.push_back({type, "", Key::Space, {it->second}, 0,
                            dur.value_or(std::chrono::milliseconds{0})});
                    } else {
                        active_mods.erase(it->second);
                        auto type = dur.has_value() ? DslToken::TIMED_MOD_END
                                                     : DslToken::MODIFIER_END;
                        tokens.push_back({type, "", Key::Space, {it->second}, 0,
                            dur.value_or(std::chrono::milliseconds{0})});
                    }
                }
                continue;
            }

            // --- Chord: KEY1+KEY2+KEY3 ---
            if (cmd.find('+') != std::string::npos) {
                std::vector<Key> chord_keys;
                bool valid = true;
                size_t start = 0;
                while (start < cmd.size()) {
                    auto plus = cmd.find('+', start);
                    std::string part = cmd.substr(start, plus - start);
                    auto it = dsl_key_map.find(part);
                    if (it != dsl_key_map.end()) {
                        chord_keys.push_back(it->second);
                    } else {
                        valid = false;
                        break;
                    }
                    if (plus == std::string::npos) break;
                    start = plus + 1;
                }
                if (valid && chord_keys.size() >= 2) {
                    DslToken tok;
                    tok.type = DslToken::CHORD;
                    tok.chord_keys = chord_keys;
                    tokens.push_back(tok);
                    continue;
                }
                // Fall through to treat as KEY with repeat
            }

            // --- Repeat: KEY N or KEY count ---
            auto space_idx = cmd.find(' ');
            if (space_idx != std::string::npos) {
                std::string key_part = cmd.substr(0, space_idx);
                std::string count_part = cmd.substr(space_idx + 1);
                auto it = dsl_key_map.find(key_part);
                if (it != dsl_key_map.end()) {
                    try {
                        int32_t count = std::stoi(count_part);
                        if (count > 0 && count <= 10000) {
                            tokens.push_back({DslToken::REPEAT_KEY, "", it->second, {}, count});
                            continue;
                        }
                    } catch (...) {
                        // Not a valid count; fall through
                    }
                }
            }

            // --- Simple KEY ---
            auto it = dsl_key_map.find(cmd);
            if (it != dsl_key_map.end()) {
                tokens.push_back({DslToken::KEY, "", it->second});
            } else if (cmd.size() == 1) {
                // Single character: treat as text
                tokens.push_back({DslToken::TEXT, cmd});
            } else {
                // Unknown — treat as literal text with braces
                tokens.push_back({DslToken::TEXT, "{" + cmd + "}"});
            }
        } else {
            // Plain text
            size_t next = dsl.find('{', pos);
            if (next == std::string::npos) next = dsl.size();
            tokens.push_back({DslToken::TEXT, dsl.substr(pos, next - pos)});
            pos = next;
        }
    }

    // If any modifiers are still active, auto-release at end
    if (!active_mods.empty()) {
        DslToken rel;
        rel.type = DslToken::MODIFIER_END;
        rel.modifiers.assign(active_mods.begin(), active_mods.end());
        tokens.push_back(rel);
    }

    return tokens;
}

std::string DslParser::unparse(const std::vector<DslToken>& tokens) {
    std::string result;
    for (const auto& tok : tokens) {
        switch (tok.type) {
            case DslToken::TEXT:
                result += tok.text;
                break;
            case DslToken::KEY: {
                std::string name = "?";
                for (const auto& [k, v] : dsl_key_map) {
                    if (v == tok.key) { name = k; break; }
                }
                result += "{" + name + "}";
                break;
            }
            case DslToken::REPEAT_KEY: {
                std::string name = "?";
                for (const auto& [k, v] : dsl_key_map) {
                    if (v == tok.key) { name = k; break; }
                }
                result += "{" + name + " " + std::to_string(tok.repeat_count) + "}";
                break;
            }
            case DslToken::CHORD: {
                result += "{";
                for (size_t i = 0; i < tok.chord_keys.size(); i++) {
                    if (i > 0) result += "+";
                    for (const auto& [k, v] : dsl_key_map) {
                        if (v == tok.chord_keys[i]) { result += k; break; }
                    }
                }
                result += "}";
                break;
            }
            case DslToken::DELAY:
                result += "{DELAY " + std::to_string(tok.timing.count()) + "ms}";
                break;
            case DslToken::TIMED_MOD_START:
            case DslToken::TIMED_MOD_END:
            case DslToken::MODIFIER_START:
            case DslToken::MODIFIER_END: {
                // Reconstruct original modifier commands (best effort)
                break; // unparse only focuses on output text
            }
            default: break;
        }
    }
    return result;
}

// =====================================================================
//  key_sequence_dsl — platform-independent execution of parsed DSL tokens
// =====================================================================

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
            case DslToken::REPEAT_KEY:
                key_repeat(tok.key, KeyRepeatConfig{
                    tok.repeat_count,
                    std::chrono::milliseconds{30},
                    std::chrono::milliseconds{10},
                    std::chrono::milliseconds{50}
                });
                break;
            case DslToken::CHORD:
                key_chord(tok.chord_keys);
                break;
            case DslToken::DELAY:
                if (tok.timing.count() > 0)
                    std::this_thread::sleep_for(tok.timing);
                break;
            case DslToken::MODIFIER_START:
                for (auto& mod : tok.modifiers) key_down(mod);
                break;
            case DslToken::MODIFIER_END:
                for (auto& mod : tok.modifiers) key_up(mod);
                break;
            case DslToken::TIMED_MOD_START:
                for (auto& mod : tok.modifiers) key_down(mod);
                if (tok.timing.count() > 0)
                    std::this_thread::sleep_for(tok.timing);
                break;
            case DslToken::TIMED_MOD_END:
                for (auto& mod : tok.modifiers) key_up(mod);
                if (tok.timing.count() > 0)
                    std::this_thread::sleep_for(tok.timing);
                break;
            case DslToken::RAW_KEY:
                raw_key_down(static_cast<uint32_t>(tok.key));
                raw_key_up(static_cast<uint32_t>(tok.key));
                break;
        }
    }
    return true;
}

// =====================================================================
//  KeyboardControllable::key_sequence_dsl (virtual default impl)
// =====================================================================

bool KeyboardControllable::key_sequence_dsl(const std::string& dsl) {
    auto tokens = DslParser::parse(dsl);
    for (auto& tok : tokens) {
        switch (tok.type) {
            case DslToken::TEXT:
                key_sequence(tok.text);
                break;
            case DslToken::KEY:
            case DslToken::REPEAT_KEY:
                key_click(tok.key);
                break;
            case DslToken::MODIFIER_START:
                for (auto& mod : tok.modifiers) key_down(mod);
                break;
            case DslToken::MODIFIER_END:
                for (auto& mod : tok.modifiers) key_up(mod);
                break;
            case DslToken::DELAY:
                if (tok.timing.count() > 0)
                    std::this_thread::sleep_for(tok.timing);
                break;
            default: break;
        }
    }
    return true;
}

} // namespace enigo
