#include "platform/platform.hpp"
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <psapi.h>
#include <WtsApi32.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#endif

#include <cstring>
#include <filesystem>
#include <thread>

namespace cppdesk::platform {

struct WakeLock::Impl {
    bool active = false;
    bool display = true;
    bool idle = true;
    bool sleep = false;
    void acquire() { active = true; }
    void release() { active = false; }
};

WakeLock::WakeLock(bool display, bool idle, bool sleep)
    : impl_(std::make_unique<Impl>()) {
    impl_->display = display;
    impl_->idle = idle;
    impl_->sleep = sleep;
#ifdef _WIN32
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED |
        (display ? ES_DISPLAY_REQUIRED : 0));
#endif
    impl_->active = true;
    spdlog::debug("WakeLock acquired (display={}, idle={}, sleep={})",
        display, idle, sleep);
}

WakeLock::~WakeLock() {
    if (impl_) impl_->release();
#ifdef _WIN32
    SetThreadExecutionState(ES_CONTINUOUS);
#endif
}

WakeLock::WakeLock(WakeLock&&) noexcept = default;
WakeLock& WakeLock::operator=(WakeLock&&) noexcept = default;

WakeLock get_wakelock(bool display) {
    return WakeLock(display, true, false);
}

// Display
std::vector<std::string> get_display_names() {
    std::vector<std::string> names;
#ifdef _WIN32
    DISPLAY_DEVICEW dd = {sizeof(dd)};
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); i++) {
        if (dd.StateFlags & DISPLAY_DEVICE_ACTIVE) {
            char name[128] = {};
            WideCharToMultiByte(CP_UTF8, 0, dd.DeviceName, -1,
                name, sizeof(name), nullptr, nullptr);
            names.push_back(name);
        }
    }
#endif
    if (names.empty()) names.push_back("DISPLAY1");
    return names;
}

std::optional<common::Resolution> current_resolution(const std::string& name) {
#ifdef _WIN32
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    std::wstring wname;
    wname.assign(name.begin(), name.end());
    if (EnumDisplaySettingsW(wname.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
        return common::Resolution{
            static_cast<uint32_t>(dm.dmPelsWidth),
            static_cast<uint32_t>(dm.dmPelsHeight)
        };
    }
#endif
    return std::nullopt;
}

bool change_resolution(const std::string& name, uint32_t width, uint32_t height) {
#ifdef _WIN32
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    dm.dmPelsWidth = width;
    dm.dmPelsHeight = height;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    std::wstring wname;
    wname.assign(name.begin(), name.end());
    return ChangeDisplaySettingsExW(wname.c_str(), &dm, nullptr,
        CDS_FULLSCREEN, nullptr) == DISP_CHANGE_SUCCESSFUL;
#endif
    return false;
}

std::vector<common::Resolution> supported_resolutions(const std::string& name) {
    std::vector<common::Resolution> res;
#ifdef _WIN32
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    std::wstring wname;
    wname.assign(name.begin(), name.end());
    for (DWORD i = 0; EnumDisplaySettingsW(wname.c_str(), i, &dm); i++) {
        res.push_back({static_cast<uint32_t>(dm.dmPelsWidth),
            static_cast<uint32_t>(dm.dmPelsHeight)});
    }
#endif
    if (res.empty()) {
        res.push_back({1920, 1080});
        res.push_back({1366, 768});
        res.push_back({1280, 720});
    }
    return res;
}

// Cursor
std::optional<common::CursorData> get_cursor() {
    common::CursorData cd;
#ifdef _WIN32
    CURSORINFO ci = {sizeof(ci)};
    if (!GetCursorInfo(&ci)) return std::nullopt;
    ICONINFO ii;
    if (!GetIconInfo(ci.hCursor, &ii)) return std::nullopt;
    cd.id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ci.hCursor) & 0xFFFFFFFF);
    cd.hot_x = static_cast<int32_t>(ii.xHotspot);
    cd.hot_y = static_cast<int32_t>(ii.yHotspot);
    if (ii.hbmColor) {
        BITMAP bm;
        GetObjectW(ii.hbmColor, sizeof(bm), &bm);
        cd.width = bm.bmWidth;
        cd.height = bm.bmHeight;
        size_t size = cd.width * cd.height * 4;
        cd.colors.resize(size, 0);
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
#else
    cd.width = 32;
    cd.height = 32;
    cd.colors.resize(32 * 32 * 4, 0);
#endif
    return cd;
}

std::optional<std::pair<int32_t, int32_t>> get_cursor_pos() {
#ifdef _WIN32
    POINT pt;
    if (GetCursorPos(&pt))
        return std::make_pair(static_cast<int32_t>(pt.x), static_cast<int32_t>(pt.y));
#endif
    return std::nullopt;
}

bool set_cursor_pos(int32_t x, int32_t y) {
#ifdef _WIN32
    return SetCursorPos(x, y) != 0;
#else
    return false;
#endif
}

bool clip_cursor(int32_t x, int32_t y, int32_t w, int32_t h) {
#ifdef _WIN32
    RECT rc = {x, y, x + w, y + h};
    return ClipCursor(&rc) != 0;
#else
    return false;
#endif
}

// System info
std::string get_active_username() {
#ifdef _WIN32
    wchar_t buf[256] = {};
    DWORD size = 256;
    GetUserNameW(buf, &size);
    char name[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, name, sizeof(name), nullptr, nullptr);
    return name;
#else
    return "unknown";
#endif
}

bool is_installed() {
#ifdef _WIN32
    HKEY hkey;
    return RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\cppdesk",
        0, KEY_READ, &hkey) == ERROR_SUCCESS;
#else
    return false;
#endif
}

bool is_xfce() { return false; }
bool is_wayland() { return false; }
bool is_x11() { return false; }

bool is_process_trusted(bool elevate) {
#ifdef _WIN32
    HANDLE token;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elev;
        DWORD size = sizeof(elev);
        BOOL ok = GetTokenInformation(token, TokenElevation, &elev, size, &size);
        CloseHandle(token);
        if (ok) return elevate ? elev.TokenIsElevated : true;
    }
#endif
    return true;
}

// Service
void start_os_service() {
#ifdef _WIN32
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return;
    SC_HANDLE svc = OpenServiceW(scm, L"cppdesk-server", SERVICE_START);
    if (svc) { StartServiceW(svc, 0, nullptr); CloseServiceHandle(svc); }
    CloseServiceHandle(scm);
#endif
}

void stop_os_service() {
#ifdef _WIN32
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return;
    SC_HANDLE svc = OpenServiceW(scm, L"cppdesk-server", SERVICE_STOP);
    if (svc) {
        SERVICE_STATUS st;
        ControlService(svc, SERVICE_CONTROL_STOP, &st);
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
#endif
}

bool is_service_running() {
#ifdef _WIN32
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, L"cppdesk-server", SERVICE_QUERY_STATUS);
    if (!svc) { CloseServiceHandle(scm); return false; }
    SERVICE_STATUS st;
    BOOL ok = QueryServiceStatus(svc, &st);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok && st.dwCurrentState == SERVICE_RUNNING;
#else
    return false;
#endif
}

// Clipboard
std::string get_clipboard_text() {
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return "";
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (!hData) { CloseClipboard(); return ""; }
    char* text = static_cast<char*>(GlobalLock(hData));
    std::string result(text ? text : "");
    GlobalUnlock(hData);
    CloseClipboard();
    return result;
#else
    return "";
#endif
}

bool set_clipboard_text(const std::string& text) {
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (!hGlobal) { CloseClipboard(); return false; }
    char* ptr = static_cast<char*>(GlobalLock(hGlobal));
    memcpy(ptr, text.c_str(), text.size() + 1);
    GlobalUnlock(hGlobal);
    SetClipboardData(CF_TEXT, hGlobal);
    CloseClipboard();
#endif
    return true;
}

std::vector<std::string> get_clipboard_files() { return {}; }

// Screenshot
std::optional<common::VideoFrame> capture_screen(uint32_t display_idx) {
    common::VideoFrame frame;
#ifdef _WIN32
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, w, h);
    SelectObject(hdcMem, hBitmap);
    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, 0, 0, SRCCOPY);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    frame.width = static_cast<uint32_t>(w);
    frame.height = static_cast<uint32_t>(h);
    frame.data.resize(w * h * 4);
    GetDIBits(hdcMem, hBitmap, 0, h, frame.data.data(), &bi, DIB_RGB_COLORS);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    frame.display = display_idx;
    frame.codec = 0;
#endif
    return frame;
}

std::optional<common::VideoFrame> capture_display(const std::string& name) {
    return capture_screen(0);
}

// Input
void simulate_mouse(const common::MouseEvent& ev) {
#ifdef _WIN32
    int type = ev.mask & 0x07;
    if (type == common::MouseEvent::TYPE_MOVE) {
        SetCursorPos(ev.x, ev.y);
    } else {
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dx = ev.x;
        input.mi.dy = ev.y;
        if (type == common::MouseEvent::TYPE_DOWN) {
            if (ev.mask & common::MouseEvent::BUTTON_LEFT)
                input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            else if (ev.mask & common::MouseEvent::BUTTON_RIGHT)
                input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        } else if (type == common::MouseEvent::TYPE_UP) {
            if (ev.mask & common::MouseEvent::BUTTON_LEFT)
                input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            else if (ev.mask & common::MouseEvent::BUTTON_RIGHT)
                input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        } else if (type == common::MouseEvent::TYPE_WHEEL) {
            input.mi.dwFlags = MOUSEEVENTF_WHEEL;
            input.mi.mouseData = WHEEL_DELTA;
        }
        SendInput(1, &input, sizeof(input));
    }
#endif
}

void simulate_key(uint32_t keycode, bool down) {
#ifdef _WIN32
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(keycode);
    if (!down) input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(input));
#endif
}

void simulate_text(const std::string& text) {
    for (char c : text) {
#ifdef _WIN32
        SHORT vk = VkKeyScanA(c);
        if (vk != -1) {
            simulate_key(static_cast<uint32_t>(vk & 0xFF), true);
            if (vk & 0x100) simulate_key(VK_SHIFT, true);
            simulate_key(static_cast<uint32_t>(vk & 0xFF), false);
            if (vk & 0x100) simulate_key(VK_SHIFT, false);
        }
#endif
    }
}

bool is_keyboard_mode_supported() { return true; }
void init() {
    spdlog::info("Platform (Windows) initialized");
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
}
void cleanup() {
#ifdef _WIN32
    CoUninitialize();
#endif
}

// Privacy
bool set_privacy_mode(bool enabled) {
    spdlog::info("Privacy mode: {}", enabled ? "enabled" : "disabled");
    return true;
}
bool is_privacy_mode_supported() { return true; }

// Elevation
bool is_elevated() {
#ifdef _WIN32
    return IsUserAnAdmin() != 0;
#else
    return false;
#endif
}
bool elevate() { return false; }

// Virtual Display (Windows-specific)
bool install_virtual_display() {
#ifdef _WIN32
    spdlog::info("Installing virtual display driver...");
    return false;
#else
    return false;
#endif
}
bool uninstall_virtual_display() {
#ifdef _WIN32
    return false;
#else
    return false;
#endif
}
bool is_virtual_display_installed() {
    return false;
}

} // namespace cppdesk::platform
