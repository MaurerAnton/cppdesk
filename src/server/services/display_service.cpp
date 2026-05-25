// ============================================================================
// cppdesk — Display Service
// ============================================================================
// Platform-native display enumeration, resolution management, DPI/scale
// awareness, hotplug detection, and multi-monitor virtual desktop coordinate
// systems.  Wraps Windows (EnumDisplayDevices / DXGI), Linux (X11 XRandR),
// and macOS (CoreGraphics) behind #ifdef guards.
//
// The DisplayService class (inherits GenericService) exposes a stable
// protocol-facing API while the internal DisplayManager singleton drives
// all platform interaction and caches state.
// ============================================================================

#include "server/server.hpp"
#include "platform/platform.hpp"
#include "common/config.hpp"
#include "common/protocol.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
    #define CPPDESK_OS_WINDOWS 1
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shellscalingapi.h>
    #include <wingdi.h>
#elif defined(__APPLE__)
    #define CPPDESK_OS_MACOS 1
    #include <ApplicationServices/ApplicationServices.h>
    #include <CoreGraphics/CoreGraphics.h>
    #include <IOKit/graphics/IOGraphicsLib.h>
#else
    #define CPPDESK_OS_LINUX 1
    #include <X11/Xlib.h>
    #include <X11/extensions/Xrandr.h>
    #include <X11/extensions/Xinerama.h>
    #include <cstdlib>
    #include <climits>
#endif

// ============================================================================
// Forward declarations (internal helpers)
// ============================================================================
namespace cppdesk::server {
namespace detail {

    // --- Hotplug callback type -----------------------------------------------
    using HotplugCallback = std::function<void()>;

    // --- Display mode descriptor (one supported video mode) ------------------
    struct DisplayMode {
        uint32_t width        = 0;
        uint32_t height       = 0;
        uint32_t refresh_rate = 0;   // Hz (integer for simplicity)
        uint32_t bits_per_pixel = 32;

        bool operator==(const DisplayMode& o) const {
            return width  == o.width  &&
                   height == o.height &&
                   refresh_rate == o.refresh_rate;
        }
        bool operator<(const DisplayMode& o) const {
            if (width != o.width)   return width < o.width;
            if (height != o.height) return height < o.height;
            return refresh_rate < o.refresh_rate;
        }
    };

    // --- Full per-display descriptor -----------------------------------------
    // Mirrors what the protocol exposes, plus a few internal bookkeeping fields.
    struct DisplayDescriptor {
        // Public / protocol-facing
        uint32_t index        = 0;   // stable 0-based index
        std::string name;            // human-readable + platform id
        std::string device_path;     // OS-level persistent id (e.g. \\.\DISPLAY1)
        uint32_t width        = 0;
        uint32_t height       = 0;
        int32_t  x            = 0;   // virtual-desktop origin
        int32_t  y            = 0;
        uint32_t physical_w_mm = 0;
        uint32_t physical_h_mm = 0;
        double   scale_factor  = 1.0;
        bool     is_primary    = false;
        bool     is_builtin    = false;  // laptop panel
        bool     is_connected  = true;
        bool     is_virtual    = false;

        // Supported modes (sorted, deduped)
        std::vector<DisplayMode> supported_modes;
        DisplayMode current_mode;
    };

    // --- Virtual-desktop bounding box ----------------------------------------
    struct VirtualDesktop {
        int32_t min_x = 0;
        int32_t min_y = 0;
        int32_t max_x = 0;
        int32_t max_y = 0;
        uint32_t total_width  = 0;
        uint32_t total_height = 0;
    };

} // namespace detail
} // namespace cppdesk::server

// ============================================================================
// Platform-specific display enumeration
// ============================================================================

#if CPPDESK_OS_WINDOWS

// ---------------------------------------------------------------------------
// Windows: enumeration via EnumDisplayDevices + EnumDisplaySettings
// ---------------------------------------------------------------------------
namespace cppdesk::server {
namespace detail {

// RAII helper to load SHCore for GetDpiForMonitor (Win 8.1+)
class DpiHelper {
public:
    DpiHelper() {
        shcore_ = LoadLibraryA("Shcore.dll");
        if (shcore_) {
            get_dpi_ = reinterpret_cast<GetDpiForMonitor_t>(
                GetProcAddress(shcore_, "GetDpiForMonitor"));
        }
    }
    ~DpiHelper() { if (shcore_) FreeLibrary(shcore_); }

    bool get_dpi(HMONITOR mon, /*MONITOR_DPI_TYPE*/ int type,
                 uint32_t* dpi_x, uint32_t* dpi_y) {
        if (!get_dpi_) return false;
        return SUCCEEDED(get_dpi_(mon, type, dpi_x, dpi_y));
    }

    static DpiHelper& instance() {
        static DpiHelper h;
        return h;
    }

private:
    using GetDpiForMonitor_t = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    HMODULE shcore_ = nullptr;
    GetDpiForMonitor_t get_dpi_ = nullptr;
};

// Callback data for MonitorEnumProc
struct MonitorEnumCtx {
    std::vector<DisplayDescriptor> displays;
    HMONITOR primary_monitor = nullptr;
};

BOOL CALLBACK monitor_enum_proc(HMONITOR hMon, HDC /*hdc*/,
                                LPRECT rect, LPARAM lParam) {
    auto* ctx = reinterpret_cast<MonitorEnumCtx*>(lParam);
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return TRUE;

    DisplayDescriptor dd;
    dd.is_connected = true;
    dd.is_primary   = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    dd.x = mi.rcMonitor.left;
    dd.y = mi.rcMonitor.top;
    dd.width  = static_cast<uint32_t>(mi.rcMonitor.right  - mi.rcMonitor.left);
    dd.height = static_cast<uint32_t>(mi.rcMonitor.bottom - mi.rcMonitor.top);

    // Device name
    char dev_name[64] = {};
    WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, dev_name,
                        sizeof(dev_name), nullptr, nullptr);
    dd.name = dev_name;
    dd.device_path = dev_name;
    dd.index = static_cast<uint32_t>(ctx->displays.size());

    // DPI via Shcore (effective DPI)
    uint32_t dpi_x = 96, dpi_y = 96;
    DpiHelper::instance().get_dpi(hMon, 0 /*MDT_EFFECTIVE_DPI*/,
                                  &dpi_x, &dpi_y);
    dd.scale_factor = static_cast<double>(dpi_x) / 96.0;

    ctx->displays.push_back(std::move(dd));
    return TRUE;
}

std::vector<DisplayDescriptor> enumerate_windows() {
    std::vector<DisplayDescriptor> result;
    MonitorEnumCtx ctx;
    ctx.primary_monitor = MonitorFromPoint({0,0}, MONITOR_DEFAULTTOPRIMARY);

    EnumDisplayMonitors(nullptr, nullptr, monitor_enum_proc,
                        reinterpret_cast<LPARAM>(&ctx));

    // Enumerate supported modes for each display
    for (auto& dd : result) {
        std::set<DisplayMode> mode_set;
        DEVMODEW dm;
        dm.dmSize = sizeof(dm);
        dm.dmDriverExtra = 0;

        // Convert name to wide for EnumDisplaySettings
        wchar_t wname[64] = {};
        MultiByteToWideChar(CP_UTF8, 0, dd.name.c_str(), -1,
                            wname, sizeof(wname) / sizeof(wchar_t));

        for (DWORD i = 0; EnumDisplaySettingsW(wname, i, &dm); ++i) {
            DisplayMode mode;
            mode.width         = dm.dmPelsWidth;
            mode.height        = dm.dmPelsHeight;
            mode.refresh_rate  = dm.dmDisplayFrequency;
            mode.bits_per_pixel = dm.dmBitsPerPel;
            if (mode.width >= 640 && mode.height >= 480 && mode.bits_per_pixel >= 24) {
                mode_set.insert(mode);
            }
        }

        // Try without device name as fallback
        if (mode_set.empty()) {
            for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); ++i) {
                DisplayMode mode;
                mode.width         = dm.dmPelsWidth;
                mode.height        = dm.dmPelsHeight;
                mode.refresh_rate  = dm.dmDisplayFrequency;
                mode.bits_per_pixel = dm.dmBitsPerPel;
                if (mode.width >= 640 && mode.height >= 480 && mode.bits_per_pixel >= 24) {
                    mode_set.insert(mode);
                }
            }
        }

        dd.supported_modes.assign(mode_set.begin(), mode_set.end());

        // Set current mode from the display size we already captured
        dd.current_mode.width        = dd.width;
        dd.current_mode.height       = dd.height;
        dd.current_mode.refresh_rate = 60; // default; not easily queried via MonitorInfo
        dd.current_mode.bits_per_pixel = 32;
    }

    spdlog::debug("Windows: enumerated {} display(s)", result.size());
    return result;
}

bool change_resolution_windows(const std::string& device_path,
                               uint32_t width, uint32_t height,
                               uint32_t refresh) {
    wchar_t wname[64] = {};
    MultiByteToWideChar(CP_UTF8, 0, device_path.c_str(), -1,
                        wname, sizeof(wname) / sizeof(wchar_t));

    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    dm.dmPelsWidth  = width;
    dm.dmPelsHeight = height;
    dm.dmDisplayFrequency = refresh;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;

    LONG rc = ChangeDisplaySettingsExW(wname, &dm, nullptr,
                                        CDS_FULLSCREEN, nullptr);
    if (rc == DISP_CHANGE_SUCCESSFUL) {
        spdlog::info("Windows: changed {} to {}x{}@{}Hz",
                     device_path, width, height, refresh);
        return true;
    }

    spdlog::warn("Windows: ChangeDisplaySettings failed for {} (code {})",
                 device_path, rc);
    return false;
}

// ---------------------------------------------------------------------------
// Windows hotplug detection via WM_DISPLAYCHANGE message thread
// ---------------------------------------------------------------------------
class WindowsHotplugDetector {
public:
    WindowsHotplugDetector() = default;

    void start(std::function<void()> callback) {
        if (running_) return;
        callback_ = std::move(callback);
        running_  = true;
        thread_   = std::thread(&WindowsHotplugDetector::message_loop, this);
    }

    void stop() {
        if (!running_) return;
        // Post WM_QUIT to wake the message loop
        running_ = false;
        if (hwnd_) PostMessageW(hwnd_, WM_QUIT, 0, 0);
        if (thread_.joinable()) thread_.join();
    }

private:
    void message_loop() {
        // Register a hidden window class
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WindowsHotplugDetector::wnd_proc;
        wc.hInstance     = hInst;
        wc.lpszClassName = L"CppDeskDisplayHotplug";
        RegisterClassExW(&wc);

        hwnd_ = CreateWindowExW(0, L"CppDeskDisplayHotplug",
                                L"CppDeskDisplay", 0,
                                0, 0, 0, 0, HWND_MESSAGE,
                                nullptr, hInst, this);

        MSG msg;
        while (running_ && GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        UnregisterClassW(L"CppDeskDisplayHotplug", hInst);
    }

    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<WindowsHotplugDetector*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            self = static_cast<WindowsHotplugDetector*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self));
        }

        if (msg == WM_DISPLAYCHANGE) {
            uint32_t w = LOWORD(lParam);
            uint32_t h = HIWORD(lParam);
            uint32_t bpp = wParam;
            spdlog::info("Windows WM_DISPLAYCHANGE: {}x{} {}bpp", w, h, bpp);
            if (self && self->callback_) {
                self->callback_();
            }
        }

        if (msg == WM_DEVICECHANGE) {
            spdlog::debug("Windows WM_DEVICECHANGE");
            if (self && self->callback_) {
                self->callback_();
            }
        }

        if (msg == WM_SETTINGCHANGE &&
            lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam),
                             L"Display") == 0) {
            spdlog::debug("Windows display settings changed");
            if (self && self->callback_) {
                self->callback_();
            }
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    std::atomic<bool> running_{false};
    std::thread thread_;
    HWND hwnd_ = nullptr;
    std::function<void()> callback_;
};

} // namespace detail
} // namespace cppdesk::server

#elif CPPDESK_OS_LINUX

// ---------------------------------------------------------------------------
// Linux: enumeration via X11 XRandR
// ---------------------------------------------------------------------------
namespace cppdesk::server {
namespace detail {

// RAII X11 display connection with retry logic
class X11Connection {
public:
    X11Connection() {
        const char* display_name = getenv("DISPLAY");
        if (!display_name) display_name = ":0";

        for (int attempt = 0; attempt < 5; ++attempt) {
            display_ = XOpenDisplay(display_name);
            if (display_) break;
            spdlog::warn("XOpenDisplay({}) attempt {}/5 failed",
                         display_name, attempt + 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        if (!display_) {
            spdlog::error("Cannot open X display {}", display_name);
        } else {
            screen_ = DefaultScreen(display_);
            root_   = DefaultRootWindow(display_);
        }
    }

    ~X11Connection() {
        if (display_) XCloseDisplay(display_);
    }

    Display* get() const { return display_; }
    int screen() const { return screen_; }
    Window root() const { return root_; }
    bool valid() const { return display_ != nullptr; }

private:
    Display* display_ = nullptr;
    int screen_ = 0;
    Window root_ = 0;
};

// ---------------------------------------------------------------------------
// Parse EDID to extract display name
// ---------------------------------------------------------------------------
static std::string parse_edid_name(const unsigned char* edid, size_t len) {
    if (len < 128) return {};
    // EDID descriptor blocks start at offset 54, 72, 90, 108
    // Each descriptor is 18 bytes; type tag at offset 3 within descriptor
    for (int block = 0; block < 4; ++block) {
        size_t off = 54 + block * 18;
        if (off + 18 > len) break;

        // Descriptor type tag 0xFC = display name
        if (edid[off + 3] == 0xFC) {
            char name[14] = {};
            std::memcpy(name, &edid[off + 5], 13);
            // Strip trailing newlines / spaces
            for (int i = 12; i >= 0; --i) {
                if (name[i] == '\n' || name[i] == ' ' || name[i] == '\0')
                    name[i] = '\0';
                else
                    break;
            }
            if (name[0]) return std::string(name);
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Parse EDID to get physical size (mm)
// ---------------------------------------------------------------------------
static std::pair<uint32_t, uint32_t> parse_edid_size(const unsigned char* edid,
                                                      size_t len) {
    if (len < 22) return {0, 0};
    uint32_t w = edid[21]; // cm
    uint32_t h = edid[22];
    return {w * 10, h * 10}; // convert cm → mm
}

// ---------------------------------------------------------------------------
// XRandR enumeration
// ---------------------------------------------------------------------------
std::vector<DisplayDescriptor> enumerate_x11() {
    std::vector<DisplayDescriptor> result;
    X11Connection conn;
    if (!conn.valid()) {
        spdlog::error("X11: no display connection");
        return result;
    }

    Display* dpy = conn.get();
    Window root   = conn.root();

    // ------------------------------------------------------------------
    // 1. XRandR resources
    // ------------------------------------------------------------------
    XRRScreenResources* res = XRRGetScreenResources(dpy, root);
    if (!res) {
        spdlog::error("X11: XRRGetScreenResources returned null");
        return result;
    }

    // Determine primary output via XRandR
    RROutput primary_output = XRRGetOutputPrimary(dpy, root);

    // Cached atoms for EDID property queries
    Atom edid_atom = XInternAtom(dpy, "EDID", True);

    for (int i = 0; i < res->noutput; ++i) {
        RROutput output_id = res->outputs[i];
        XRROutputInfo* oi  = XRRGetOutputInfo(dpy, res, output_id);
        if (!oi) continue;

        if (oi->connection != RR_Connected || oi->crtc == 0) {
            XRRFreeOutputInfo(oi);
            continue;
        }

        XRRCrtcInfo* ci = XRRGetCrtcInfo(dpy, res, oi->crtc);
        if (!ci) {
            XRRFreeOutputInfo(oi);
            continue;
        }

        DisplayDescriptor dd;
        dd.index       = static_cast<uint32_t>(result.size());
        dd.name        = oi->name ? oi->name : "Unknown";
        dd.device_path = std::to_string(static_cast<long long>(output_id));
        dd.width       = ci->width;
        dd.height      = ci->height;
        dd.x           = ci->x;
        dd.y           = ci->y;
        dd.is_primary  = (output_id == primary_output);
        dd.is_connected = true;
        dd.current_mode.width         = ci->width;
        dd.current_mode.height        = ci->height;
        dd.current_mode.refresh_rate  = 60;
        dd.current_mode.bits_per_pixel = 32;

        // Try to read EDID for name & physical size
        if (edid_atom != None) {
            Atom actual_type;
            int actual_fmt;
            unsigned long nitems, bytes_after;
            unsigned char* prop = nullptr;

            if (XRRGetOutputProperty(dpy, output_id, edid_atom,
                                     0, 1024, False, False,
                                     AnyPropertyType,
                                     &actual_type, &actual_fmt,
                                     &nitems, &bytes_after,
                                     &prop) == Success && prop) {
                std::string edid_name = parse_edid_name(prop, nitems);
                if (!edid_name.empty()) dd.name = edid_name;

                auto [pw, ph] = parse_edid_size(prop, nitems);
                dd.physical_w_mm = pw;
                dd.physical_h_mm = ph;

                XFree(prop);
            }
        }

        // Physical size from output info
        if (dd.physical_w_mm == 0 && oi->mm_width > 0) {
            dd.physical_w_mm = oi->mm_width;
            dd.physical_h_mm = oi->mm_height;
        }

        // Check if built-in (laptop panel)
        if (oi->connection == RR_Connected && oi->subpixel_order != SubPixelUnknown) {
            // Heuristic: built-in panels often have subpixel order set
            // and their name contains eDP/LVDS/DSI
            std::string lower_name = dd.name;
            std::transform(lower_name.begin(), lower_name.end(),
                           lower_name.begin(), ::tolower);
            dd.is_builtin = (lower_name.find("edp")   != std::string::npos ||
                             lower_name.find("lvds")  != std::string::npos ||
                             lower_name.find("dsi")   != std::string::npos ||
                             lower_name.find("panel") != std::string::npos);
        }

        // ------------------------------------------------------------------
        // Scale factor: use physical size vs logical pixels
        // ------------------------------------------------------------------
        if (dd.physical_w_mm > 0 && dd.physical_h_mm > 0 && dd.width > 0) {
            double dpi_x = static_cast<double>(dd.width) /
                           (static_cast<double>(dd.physical_w_mm) / 25.4);
            dd.scale_factor = std::round(dpi_x / 96.0 * 4.0) / 4.0; // quantize to 0.25
            dd.scale_factor = std::max(0.5, std::min(dd.scale_factor, 4.0));
        } else {
            // Fallback: query Xft.dpi resource
            dd.scale_factor = 1.0;
            // Could also check GDK_SCALE, QT_SCALE_FACTOR env vars
            const char* gdk_scale = getenv("GDK_SCALE");
            if (gdk_scale) {
                try { dd.scale_factor = std::stod(gdk_scale); }
                catch (...) { dd.scale_factor = 1.0; }
            }
        }

        // Clamp scale
        dd.scale_factor = std::max(0.5, std::min(dd.scale_factor, 4.0));

        // ------------------------------------------------------------------
        // Enumerate supported modes
        // ------------------------------------------------------------------
        std::set<DisplayMode> mode_set;
        for (int m = 0; m < oi->nmode; ++m) {
            RRMode mode_id = oi->modes[m];
            for (int j = 0; j < res->nmode; ++j) {
                if (res->modes[j].id == mode_id) {
                    XRRModeInfo& mi = res->modes[j];
                    DisplayMode dm;
                    dm.width   = mi.width;
                    dm.height  = mi.height;
                    dm.bits_per_pixel = 32;

                    // Compute refresh rate from dotClock / (hTotal * vTotal)
                    if (mi.hTotal > 0 && mi.vTotal > 0 && mi.dotClock > 0) {
                        dm.refresh_rate = static_cast<uint32_t>(
                            static_cast<double>(mi.dotClock) /
                            (static_cast<double>(mi.hTotal) *
                             static_cast<double>(mi.vTotal)) + 0.5);
                    }

                    if (dm.width >= 640 && dm.height >= 480)
                        mode_set.insert(dm);
                    break;
                }
            }
        }
        dd.supported_modes.assign(mode_set.begin(), mode_set.end());

        XRRFreeCrtcInfo(ci);
        XRRFreeOutputInfo(oi);
        result.push_back(std::move(dd));
    }

    XRRFreeScreenResources(res);
    spdlog::debug("X11: enumerated {} display(s)", result.size());
    return result;
}

// ---------------------------------------------------------------------------
// XRandR resolution change
// ---------------------------------------------------------------------------
bool change_resolution_x11(const std::string& device_path,
                           uint32_t width, uint32_t height,
                           uint32_t refresh) {
    X11Connection conn;
    if (!conn.valid()) return false;

    Display* dpy = conn.get();
    Window root   = conn.root();

    RROutput output_id;
    try {
        output_id = static_cast<RROutput>(std::stoull(device_path));
    } catch (...) {
        spdlog::error("X11: invalid output id {}", device_path);
        return false;
    }

    XRRScreenResources* res = XRRGetScreenResources(dpy, root);
    if (!res) return false;

    // Find matching mode
    RRMode target_mode = 0;
    for (int i = 0; i < res->nmode; ++i) {
        XRRModeInfo& mi = res->modes[i];
        uint32_t mode_refresh = 0;
        if (mi.hTotal > 0 && mi.vTotal > 0 && mi.dotClock > 0) {
            mode_refresh = static_cast<uint32_t>(
                static_cast<double>(mi.dotClock) /
                (static_cast<double>(mi.hTotal) *
                 static_cast<double>(mi.vTotal)) + 0.5);
        }
        if (mi.width == width && mi.height == height &&
            (refresh == 0 || mode_refresh == refresh || mode_refresh == 0)) {
            target_mode = mi.id;
            break;
        }
    }

    if (!target_mode) {
        XRRFreeScreenResources(res);
        spdlog::warn("X11: no matching mode for {}x{}@{}",
                     width, height, refresh);
        return false;
    }

    // Get current CRTC
    XRROutputInfo* oi = XRRGetOutputInfo(dpy, res, output_id);
    if (!oi || oi->crtc == 0) {
        if (oi) XRRFreeOutputInfo(oi);
        XRRFreeScreenResources(res);
        return false;
    }
    RRCrtc crtc = oi->crtc;
    XRRFreeOutputInfo(oi);

    Status st = XRRSetCrtcConfig(dpy, res, crtc, CurrentTime,
                                 0, 0, target_mode, RR_Rotate_0,
                                 nullptr, 0);

    XRRFreeScreenResources(res);

    if (st == Success) {
        XFlush(dpy);
        spdlog::info("X11: changed output {} to {}x{}@{}",
                     device_path, width, height, refresh);
        return true;
    }

    spdlog::warn("X11: XRRSetCrtcConfig failed for output {}",
                 device_path);
    return false;
}

// ---------------------------------------------------------------------------
// Linux hotplug detection via XRandR events
// ---------------------------------------------------------------------------
class X11HotplugDetector {
public:
    void start(std::function<void()> callback) {
        if (running_) return;
        callback_ = std::move(callback);
        running_  = true;
        thread_   = std::thread(&X11HotplugDetector::event_loop, this);
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

private:
    void event_loop() {
        const char* display_name = getenv("DISPLAY");
        if (!display_name) display_name = ":0";

        Display* dpy = XOpenDisplay(display_name);
        if (!dpy) {
            spdlog::error("X11 hotplug: cannot open display");
            return;
        }

        Window root = DefaultRootWindow(dpy);

        // Check for XRandR extension
        int rr_event_base, rr_error_base;
        int rr_major, rr_minor;
        if (!XRRQueryExtension(dpy, &rr_event_base, &rr_error_base) ||
            !XRRQueryVersion(dpy, &rr_major, &rr_minor)) {
            spdlog::error("X11 hotplug: XRandR not available");
            XCloseDisplay(dpy);
            return;
        }

        spdlog::info("X11 hotplug: XRandR {}.{} available", rr_major, rr_minor);

        // Subscribe to screen change notify
        XRRSelectInput(dpy, root, RRScreenChangeNotifyMask |
                                  RROutputChangeNotifyMask |
                                  RRCrtcChangeNotifyMask);

        // We also need a small property-change mechanism (EDID changes)
        // RROutputPropertyNotifyMask requires selecting per-output, but we
        // can catch most events via the screen/crtc/output masks above.

        XEvent ev;
        while (running_) {
            // Use a short timeout so we can check running_ frequently
            if (!XPending(dpy)) {
                // Sleep a bit to avoid busy-waiting
                struct timeval tv;
                tv.tv_sec  = 0;
                tv.tv_usec = 200000; // 200ms
                int fd = ConnectionNumber(dpy);
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(fd, &fds);
                select(fd + 1, &fds, nullptr, nullptr, &tv);
                if (!running_) break;
                if (!XPending(dpy)) continue;
            }

            XNextEvent(dpy, &ev);

            // XRandR events use the extension event type
            int event_type = ev.type - rr_event_base;

            bool display_changed = false;

            if (event_type == RRScreenChangeNotify) {
                spdlog::info("X11: RRScreenChangeNotify");
                display_changed = true;
            } else if (event_type == RRNotify) {
                XRRNotifyEvent* ne = reinterpret_cast<XRRNotifyEvent*>(&ev);
                if (ne->subtype == RRNotify_OutputChange) {
                    spdlog::info("X11: RRNotify_OutputChange");
                    display_changed = true;
                } else if (ne->subtype == RRNotify_CrtcChange) {
                    spdlog::info("X11: RRNotify_CrtcChange");
                    display_changed = true;
                } else if (ne->subtype == RRNotify_OutputProperty) {
                    spdlog::debug("X11: RRNotify_OutputProperty");
                    display_changed = true;
                }
            } else if (ev.type == ConfigureNotify) {
                spdlog::debug("X11: ConfigureNotify on root");
                display_changed = true;
            }

            if (display_changed && callback_) {
                // Coalesce: wait 500ms for potential additional events
                auto deadline = std::chrono::steady_clock::now() +
                                std::chrono::milliseconds(500);
                while (std::chrono::steady_clock::now() < deadline) {
                    if (XPending(dpy)) {
                        XNextEvent(dpy, &ev);
                        // Just drain the queue
                    } else {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(50));
                    }
                }
                callback_();
            }
        }

        XCloseDisplay(dpy);
    }

    std::atomic<bool> running_{false};
    std::thread thread_;
    std::function<void()> callback_;
};

} // namespace detail
} // namespace cppdesk::server

#elif CPPDESK_OS_MACOS

// ---------------------------------------------------------------------------
// macOS: enumeration via CoreGraphics
// ---------------------------------------------------------------------------
namespace cppdesk::server {
namespace detail {

std::vector<DisplayDescriptor> enumerate_macos() {
    std::vector<DisplayDescriptor> result;

    CGDirectDisplayID displays[32];
    uint32_t count = 0;
    CGError err = CGGetActiveDisplayList(32, displays, &count);
    if (err != kCGErrorSuccess) {
        spdlog::error("macOS: CGGetActiveDisplayList failed (error {})",
                      static_cast<int>(err));
        return result;
    }

    // Get online display list as well (includes mirrored displays)
    CGDirectDisplayID online_displays[32];
    uint32_t online_count = 0;
    CGGetOnlineDisplayList(32, online_displays, &online_count);

    // Determine primary display (the one at (0,0) in global coords)
    CGDirectDisplayID primary_id = CGMainDisplayID();

    for (uint32_t i = 0; i < count; ++i) {
        CGDirectDisplayID did = displays[i];

        DisplayDescriptor dd;
        dd.index       = i;
        dd.device_path = std::to_string(did);
        dd.is_primary  = (did == primary_id);
        dd.is_connected = true;

        // Display bounds in global (virtual-desktop) coordinates
        CGRect bounds = CGDisplayBounds(did);
        dd.x      = static_cast<int32_t>(bounds.origin.x);
        dd.y      = static_cast<int32_t>(bounds.origin.y);
        dd.width  = static_cast<uint32_t>(bounds.size.width);
        dd.height = static_cast<uint32_t>(bounds.size.height);

        // Current mode
        CGDisplayModeRef current_mode = CGDisplayCopyDisplayMode(did);
        if (current_mode) {
            dd.current_mode.width  = static_cast<uint32_t>(
                CGDisplayModeGetWidth(current_mode));
            dd.current_mode.height = static_cast<uint32_t>(
                CGDisplayModeGetHeight(current_mode));
            dd.current_mode.refresh_rate = static_cast<uint32_t>(
                CGDisplayModeGetRefreshRate(current_mode));
            dd.current_mode.bits_per_pixel = 32;

            // Pixel encoding
            CFStringRef encoding = CGDisplayModeCopyPixelEncoding(current_mode);
            if (encoding) {
                // IO32BitDirectPixels or similar
                CFRelease(encoding);
            }
            CGDisplayModeRelease(current_mode);
        }

        // Physical size (mm)
        CGSize phys = CGDisplayScreenSize(did);
        dd.physical_w_mm = static_cast<uint32_t>(phys.width);
        dd.physical_h_mm = static_cast<uint32_t>(phys.height);

        // Scale factor: backing scale factor (retina)
        dd.scale_factor = 1.0;
        if (@available(macOS 10.7, *)) {
            // CGDisplayBackingScaleFactor is not directly available in C++,
            // but we can compute from mode count:
            //   scale = pixel_width / point_width
            // For now use the mode list to detect HiDPI
        }

        // Try to get the backing-scale-factor equivalent:
        // CGDisplayModeGetPixelWidth vs CGDisplayModeGetWidth
        CFArrayRef modes = CGDisplayCopyAllDisplayModes(did, nullptr);
        if (modes) {
            CFIndex mode_count = CFArrayGetCount(modes);
            for (CFIndex m = 0; m < mode_count; ++m) {
                CGDisplayModeRef mode = static_cast<CGDisplayModeRef>(
                    const_cast<void*>(CFArrayGetValueAtIndex(modes, m)));
                uint32_t pw = static_cast<uint32_t>(
                    CGDisplayModeGetPixelWidth(mode));
                uint32_t w  = static_cast<uint32_t>(
                    CGDisplayModeGetWidth(mode));
                if (pw > 0 && w > 0 && pw != w) {
                    dd.scale_factor = static_cast<double>(pw) /
                                      static_cast<double>(w);
                    break;
                }
            }
            CFRelease(modes);
        }

        // If scale still 1.0 but physical size suggests >200 DPI, adjust
        if (dd.scale_factor == 1.0 && dd.physical_w_mm > 0 && dd.width > 0) {
            double dpi = static_cast<double>(dd.width) /
                         (static_cast<double>(dd.physical_w_mm) / 25.4);
            if (dpi > 150) dd.scale_factor = 2.0;
        }

        dd.scale_factor = std::max(0.5, std::min(dd.scale_factor, 4.0));

        // Display name via IOKit
        {
            io_service_t service = CGDisplayIOServicePort(did);
            if (service != MACH_PORT_NULL) {
                CFDictionaryRef info = IODisplayCreateInfoDictionary(
                    service, kIODisplayOnlyPreferredName);
                if (info) {
                    CFDictionaryRef names = static_cast<CFDictionaryRef>(
                        CFDictionaryGetValue(info,
                            CFSTR(kDisplayProductName)));
                    if (names) {
                        // Try en_US first, then fall back to first entry
                        CFStringRef name_str = static_cast<CFStringRef>(
                            CFDictionaryGetValue(names,
                                CFSTR("en_US")));
                        if (!name_str) {
                            // Iterate to find any name
                            CFIndex cnt = CFDictionaryGetCount(names);
                            if (cnt > 0) {
                                const void** keys   = new const void*[cnt];
                                const void** values = new const void*[cnt];
                                CFDictionaryGetKeysAndValues(names,
                                    keys, values);
                                name_str = static_cast<CFStringRef>(values[0]);
                                delete[] keys;
                                delete[] values;
                            }
                        }
                        if (name_str) {
                            char buf[256] = {};
                            CFStringGetCString(name_str, buf,
                                               sizeof(buf),
                                               kCFStringEncodingUTF8);
                            dd.name = buf;
                        }
                    }
                    CFRelease(info);
                }
            }
        }

        if (dd.name.empty()) {
            // Fallback: "Display N" or use CGDisplay model/serial
            dd.name = "Display " + std::to_string(i + 1);

            // Try to get vendor/model from IOKit registry
            io_service_t service = CGDisplayIOServicePort(did);
            if (service != MACH_PORT_NULL) {
                CFStringRef str;
                str = static_cast<CFStringRef>(IORegistryEntrySearchCFProperty(
                    service, kIOServicePlane,
                    CFSTR("DisplayProductName"),
                    kCFAllocatorDefault,
                    kIORegistryIterateRecursively));
                if (str) {
                    char buf[256] = {};
                    CFStringGetCString(str, buf, sizeof(buf),
                                       kCFStringEncodingUTF8);
                    dd.name = buf;
                    CFRelease(str);
                }
            }
            dd.is_builtin = CGDisplayIsBuiltin(did);
        }

        // ------------------------------------------------------------------
        // Enumerate supported modes
        // ------------------------------------------------------------------
        std::set<DisplayMode> mode_set;
        CFArrayRef all_modes = CGDisplayCopyAllDisplayModes(did, nullptr);
        if (all_modes) {
            CFIndex mc = CFArrayGetCount(all_modes);
            for (CFIndex m = 0; m < mc; ++m) {
                CGDisplayModeRef mode = static_cast<CGDisplayModeRef>(
                    const_cast<void*>(CFArrayGetValueAtIndex(all_modes, m)));
                DisplayMode dm;
                dm.width  = static_cast<uint32_t>(CGDisplayModeGetWidth(mode));
                dm.height = static_cast<uint32_t>(CGDisplayModeGetHeight(mode));
                dm.refresh_rate = static_cast<uint32_t>(
                    CGDisplayModeGetRefreshRate(mode));
                dm.bits_per_pixel = 32;

                // Skip modes that would be duplicates at logical resolution
                // when we already have HiDPI variants
                if (dm.width >= 640 && dm.height >= 480) {
                    mode_set.insert(dm);
                }
            }
            CFRelease(all_modes);
        }
        dd.supported_modes.assign(mode_set.begin(), mode_set.end());
        result.push_back(std::move(dd));
    }

    spdlog::debug("macOS: enumerated {} display(s)", result.size());
    return result;
}

bool change_resolution_macos(const std::string& device_path,
                             uint32_t width, uint32_t height,
                             uint32_t refresh) {
    CGDirectDisplayID did;
    try {
        did = static_cast<CGDirectDisplayID>(std::stoull(device_path));
    } catch (...) {
        spdlog::error("macOS: invalid display id {}", device_path);
        return false;
    }

    // Find matching mode
    CFArrayRef modes = CGDisplayCopyAllDisplayModes(did, nullptr);
    if (!modes) return false;

    CGDisplayModeRef target = nullptr;
    CFIndex count = CFArrayGetCount(modes);

    for (CFIndex i = 0; i < count; ++i) {
        CGDisplayModeRef mode = static_cast<CGDisplayModeRef>(
            const_cast<void*>(CFArrayGetValueAtIndex(modes, i)));
        uint32_t mw = static_cast<uint32_t>(CGDisplayModeGetWidth(mode));
        uint32_t mh = static_cast<uint32_t>(CGDisplayModeGetHeight(mode));
        uint32_t mr = static_cast<uint32_t>(CGDisplayModeGetRefreshRate(mode));

        if (mw == width && mh == height &&
            (refresh == 0 || mr == refresh || mr == 0)) {
            target = mode;
            break;
        }
    }

    if (!target) {
        CFRelease(modes);
        spdlog::warn("macOS: no matching mode for {}x{}@{}",
                     width, height, refresh);
        return false;
    }

    // Switch mode
    CGDisplayConfigRef config;
    CGError err = CGBeginDisplayConfiguration(&config);
    if (err != kCGErrorSuccess) {
        CFRelease(modes);
        return false;
    }

    err = CGConfigureDisplayWithDisplayMode(config, did, target, nullptr);
    if (err == kCGErrorSuccess) {
        err = CGCompleteDisplayConfiguration(
            config, kCGConfigurePermanently);
        if (err == kCGErrorSuccess) {
            spdlog::info("macOS: changed display {} to {}x{}",
                         device_path, width, height);
            CFRelease(modes);
            return true;
        }
    }

    CGCancelDisplayConfiguration(config);
    CFRelease(modes);
    spdlog::warn("macOS: resolution change failed for display {}",
                 device_path);
    return false;
}

// ---------------------------------------------------------------------------
// macOS hotplug detection via CGDisplay reconfiguration callback
// ---------------------------------------------------------------------------
class MacOSHotplugDetector {
public:
    void start(std::function<void()> callback) {
        if (running_) return;
        callback_ = std::move(callback);

        // Register reconfiguration callback
        CGDisplayRegisterReconfigurationCallback(
            MacOSHotplugDetector::display_reconfig_callback,
            this);

        running_ = true;
        spdlog::info("macOS hotplug: registered CGDisplay callback");
    }

    void stop() {
        if (!running_) return;
        CGDisplayRemoveReconfigurationCallback(
            MacOSHotplugDetector::display_reconfig_callback,
            this);
        running_ = false;
    }

private:
    static void display_reconfig_callback(CGDirectDisplayID display,
                                          CGDisplayChangeSummaryFlags flags,
                                          void* userInfo) {
        auto* self = static_cast<MacOSHotplugDetector*>(userInfo);

        std::string reason;
        if (flags & kCGDisplayAddFlag)       reason = "added";
        else if (flags & kCGDisplayRemoveFlag) reason = "removed";
        else if (flags & kCGDisplayDisabledFlag) reason = "disabled";
        else if (flags & kCGDisplayEnabledFlag)  reason = "enabled";
        else if (flags & kCGDisplayMovedFlag)    reason = "moved";
        else if (flags & kCGDisplaySetModeFlag)  reason = "mode changed";
        else if (flags & kCGDisplayBeginConfigurationFlag)
            reason = "begin config";
        else
            reason = "unknown change";

        spdlog::info("macOS display reconfig: {} (flags=0x{:x})",
                     reason, static_cast<uint32_t>(flags));

        if (self && self->callback_) {
            // Small delay to let the OS finish its reconfiguration
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            self->callback_();
        }
    }

    std::atomic<bool> running_{false};
    std::function<void()> callback_;
};

} // namespace detail
} // namespace cppdesk::server

#endif // platform detection

// ============================================================================
// DisplayManager — singleton that owns all display state
// ============================================================================
namespace cppdesk::server {
namespace detail {

class DisplayManager {
public:
    static DisplayManager& instance() {
        static DisplayManager dm;
        return dm;
    }

    // --- Lifecycle ---------------------------------------------------------
    void start() {
        std::lock_guard<std::mutex> lk(mutex_);
        if (started_) return;
        started_ = true;

        // Initial enumeration
        refresh_locked();

        // Start hotplug detector
        start_hotplug_detector();

        spdlog::info("DisplayManager started with {} display(s)",
                     displays_.size());
        log_display_state();
    }

    void stop() {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!started_) return;
        started_ = false;
        stop_hotplug_detector();
        spdlog::info("DisplayManager stopped");
    }

    bool is_started() const { return started_; }

    // --- Enumeration -------------------------------------------------------
    // Returns a snapshot under lock
    std::vector<DisplayDescriptor> enumerate() {
        std::lock_guard<std::mutex> lk(mutex_);
        return displays_;
    }

    // Force a refresh (called by hotplug callback or manually)
    void refresh() {
        std::lock_guard<std::mutex> lk(mutex_);
        refresh_locked();
    }

    // --- Primary display ---------------------------------------------------
    int32_t primary_index() const {
        std::lock_guard<std::mutex> lk(mutex_);
        for (const auto& d : displays_) {
            if (d.is_primary) return static_cast<int32_t>(d.index);
        }
        return displays_.empty() ? -1 : 0;
    }

    std::optional<DisplayDescriptor> primary_display() {
        std::lock_guard<std::mutex> lk(mutex_);
        for (const auto& d : displays_) {
            if (d.is_primary) return d;
        }
        return displays_.empty()
                   ? std::nullopt
                   : std::optional<DisplayDescriptor>(displays_[0]);
    }

    // --- Query by index ----------------------------------------------------
    std::optional<DisplayDescriptor> get(uint32_t index) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (index < displays_.size()) return displays_[index];
        return std::nullopt;
    }

    // --- Resolution --------------------------------------------------------
    bool change_resolution(uint32_t index, uint32_t width, uint32_t height,
                           uint32_t refresh = 0) {
        std::string dev_path;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (index >= displays_.size()) return false;
            dev_path = displays_[index].device_path;
        }

        bool ok = false;
#if CPPDESK_OS_WINDOWS
        ok = change_resolution_windows(dev_path, width, height, refresh);
#elif CPPDESK_OS_LINUX
        ok = change_resolution_x11(dev_path, width, height, refresh);
#elif CPPDESK_OS_MACOS
        ok = change_resolution_macos(dev_path, width, height, refresh);
#endif

        if (ok) {
            stats_.resolution_changes++;
            // Refresh to pick up new state
            refresh();
        }
        return ok;
    }

    std::vector<DisplayMode> supported_modes(uint32_t index) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (index < displays_.size())
            return displays_[index].supported_modes;
        return {};
    }

    // --- Virtual desktop ---------------------------------------------------
    VirtualDesktop virtual_desktop() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return compute_virtual_desktop();
    }

    // --- DPI / Scale -------------------------------------------------------
    double scale_factor(uint32_t index) const {
        std::lock_guard<std::mutex> lk(mutex_);
        if (index < displays_.size())
            return displays_[index].scale_factor;
        return 1.0;
    }

    // --- Display name formatting for protocol ------------------------------
    std::string format_name(uint32_t index) const {
        std::lock_guard<std::mutex> lk(mutex_);
        if (index >= displays_.size()) return "Unknown";

        const auto& d = displays_[index];
        std::ostringstream oss;
        oss << d.name;

        // Append resolution if not already in the name
        if (d.name.find(std::to_string(d.width)) == std::string::npos) {
            oss << " (" << d.width << "x" << d.height << ")";
        }

        // Indicate primary
        if (d.is_primary) oss << " [Primary]";

        return oss.str();
    }

    // Overload for full protocol-friendly descriptor
    std::string format_name(const DisplayDescriptor& d) {
        std::ostringstream oss;
        oss << d.name;
        if (d.name.find(std::to_string(d.width)) == std::string::npos) {
            oss << " (" << d.width << "x" << d.height << ")";
        }
        if (d.is_primary) oss << " [Primary]";
        return oss.str();
    }

    // --- Statistics --------------------------------------------------------
    struct DisplayStats {
        uint64_t display_count  = 0;
        uint64_t changes_detected = 0;
        uint64_t resolution_changes = 0;
        uint64_t hotplug_events = 0;
        std::chrono::steady_clock::time_point last_change;
        std::chrono::steady_clock::time_point started_at;
    };

    DisplayStats stats() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return stats_;
    }

    // --- Hotplug callback registration -------------------------------------
    // Allows external code (e.g. DisplayService) to be notified.
    uint64_t register_hotplug_callback(HotplugCallback cb) {
        std::lock_guard<std::mutex> lk(hotplug_mutex_);
        uint64_t id = next_callback_id_++;
        hotplug_callbacks_[id] = std::move(cb);
        return id;
    }

    void unregister_hotplug_callback(uint64_t id) {
        std::lock_guard<std::mutex> lk(hotplug_mutex_);
        hotplug_callbacks_.erase(id);
    }

private:
    DisplayManager() {
        stats_.started_at = std::chrono::steady_clock::now();
    }

    ~DisplayManager() {
        stop_hotplug_detector();
    }

    DisplayManager(const DisplayManager&) = delete;
    DisplayManager& operator=(const DisplayManager&) = delete;

    // --- Refresh under lock ------------------------------------------------
    void refresh_locked() {
        auto before_count = displays_.size();

#if CPPDESK_OS_WINDOWS
        displays_ = enumerate_windows();
#elif CPPDESK_OS_LINUX
        displays_ = enumerate_x11();
#elif CPPDESK_OS_MACOS
        displays_ = enumerate_macos();
#else
        displays_ = {};
#endif

        stats_.display_count = displays_.size();

        if (displays_.size() != before_count) {
            stats_.changes_detected++;
            stats_.last_change = std::chrono::steady_clock::now();
        }

        // Recompute virtual desktop
        virtual_desktop_ = compute_virtual_desktop();
    }

    VirtualDesktop compute_virtual_desktop() const {
        VirtualDesktop vd;
        if (displays_.empty()) return vd;

        vd.min_x = INT_MAX;
        vd.min_y = INT_MAX;
        vd.max_x = INT_MIN;
        vd.max_y = INT_MIN;

        for (const auto& d : displays_) {
            vd.min_x = std::min(vd.min_x, d.x);
            vd.min_y = std::min(vd.min_y, d.y);
            vd.max_x = std::max(vd.max_x, static_cast<int32_t>(d.x + d.width));
            vd.max_y = std::max(vd.max_y, static_cast<int32_t>(d.y + d.height));
        }

        vd.total_width  = static_cast<uint32_t>(vd.max_x - vd.min_x);
        vd.total_height = static_cast<uint32_t>(vd.max_y - vd.min_y);
        return vd;
    }

    // --- Hotplug detector --------------------------------------------------
    void start_hotplug_detector() {
        if (hotplug_running_) return;

        auto cb = [this]() {
            this->on_hotplug_event();
        };

#if CPPDESK_OS_WINDOWS
        hotplug_detector_ = std::make_unique<WindowsHotplugDetector>();
        hotplug_detector_->start(std::move(cb));
#elif CPPDESK_OS_LINUX
        hotplug_detector_ = std::make_unique<X11HotplugDetector>();
        hotplug_detector_->start(std::move(cb));
#elif CPPDESK_OS_MACOS
        hotplug_detector_ = std::make_unique<MacOSHotplugDetector>();
        hotplug_detector_->start(std::move(cb));
#endif
        hotplug_running_ = true;
    }

    void stop_hotplug_detector() {
        if (!hotplug_running_) return;
#if CPPDESK_OS_WINDOWS
        if (hotplug_detector_) {
            static_cast<WindowsHotplugDetector*>(
                hotplug_detector_.get())->stop();
        }
#elif CPPDESK_OS_LINUX
        if (hotplug_detector_) {
            static_cast<X11HotplugDetector*>(
                hotplug_detector_.get())->stop();
        }
#elif CPPDESK_OS_MACOS
        if (hotplug_detector_) {
            static_cast<MacOSHotplugDetector*>(
                hotplug_detector_.get())->stop();
        }
#endif
        hotplug_detector_.reset();
        hotplug_running_ = false;
    }

    void on_hotplug_event() {
        spdlog::info("DisplayManager: hotplug event detected");
        {
            std::lock_guard<std::mutex> lk(mutex_);
            stats_.hotplug_events++;
        }

        // Small delay to let the OS settle
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // Refresh display list
        refresh();

        // Notify external callbacks
        std::vector<HotplugCallback> callbacks;
        {
            std::lock_guard<std::mutex> lk(hotplug_mutex_);
            for (const auto& [id, cb] : hotplug_callbacks_) {
                callbacks.push_back(cb);
            }
        }
        for (auto& cb : callbacks) {
            cb();
        }
    }

    // --- Logging -----------------------------------------------------------
    void log_display_state() const {
        for (const auto& d : displays_) {
            spdlog::info(
                "  Display [{}] \"{}\": {}x{} @ ({},{}) scale={:.2f} "
                "primary={} builtin={} modes={}",
                d.index, d.name, d.width, d.height,
                d.x, d.y, d.scale_factor,
                d.is_primary, d.is_builtin,
                d.supported_modes.size());
        }

        auto vd = virtual_desktop_;
        spdlog::info("  Virtual desktop: {}x{} (from {},{} to {},{})",
                     vd.total_width, vd.total_height,
                     vd.min_x, vd.min_y, vd.max_x, vd.max_y);
    }

    // --- State -------------------------------------------------------------
    mutable std::mutex mutex_;
    std::atomic<bool> started_{false};
    std::vector<DisplayDescriptor> displays_;
    VirtualDesktop virtual_desktop_;
    DisplayStats stats_;

    // Hotplug
    std::mutex hotplug_mutex_;
    std::atomic<bool> hotplug_running_{false};
    std::unique_ptr<void, void(*)(void*)> hotplug_detector_{nullptr,
        [](void*) {}};
    // We store it as void* because the concrete types differ by platform.
    // A cleaner alternative would be a base class with virtual start/stop,
    // but for simplicity we use type-erased unique_ptr with a no-op deleter
    // and static_cast in start/stop.
    uint64_t next_callback_id_ = 1;
    std::unordered_map<uint64_t, HotplugCallback> hotplug_callbacks_;
};

// ---------------------------------------------------------------------------
// Platform-erased hotplug detector holder
// We need to actually store typed unique_ptr, so let's do it properly:
// ---------------------------------------------------------------------------
namespace {
    struct HotplugDetectorBase {
        virtual ~HotplugDetectorBase() = default;
        virtual void start(std::function<void()> cb) = 0;
        virtual void stop() = 0;
    };

#if CPPDESK_OS_WINDOWS
    struct HotplugDetectorImpl : HotplugDetectorBase {
        WindowsHotplugDetector det;
        void start(std::function<void()> cb) override { det.start(std::move(cb)); }
        void stop() override { det.stop(); }
    };
#elif CPPDESK_OS_LINUX
    struct HotplugDetectorImpl : HotplugDetectorBase {
        X11HotplugDetector det;
        void start(std::function<void()> cb) override { det.start(std::move(cb)); }
        void stop() override { det.stop(); }
    };
#elif CPPDESK_OS_MACOS
    struct HotplugDetectorImpl : HotplugDetectorBase {
        MacOSHotplugDetector det;
        void start(std::function<void()> cb) override { det.start(std::move(cb)); }
        void stop() override { det.stop(); }
    };
#else
    struct HotplugDetectorImpl : HotplugDetectorBase {
        void start(std::function<void()> cb) override {
            spdlog::warn("Hotplug not supported on this platform");
        }
        void stop() override {}
    };
#endif
} // anonymous namespace

// Fix the DisplayManager to use the proper typed holder.
// We patch the start/stop methods:
void DisplayManager::start_hotplug_detector_proper() {
    if (hotplug_running_) return;
    auto cb = [this]() { this->on_hotplug_event(); };
    auto impl = std::make_unique<HotplugDetectorImpl>();
    impl->start(std::move(cb));
    typed_detector_ = std::move(impl);
    hotplug_running_ = true;
}

void DisplayManager::stop_hotplug_detector_proper() {
    if (!hotplug_running_) return;
    if (typed_detector_) {
        typed_detector_->stop();
        typed_detector_.reset();
    }
    hotplug_running_ = false;
}

} // namespace detail
} // namespace cppdesk::server

// ============================================================================
// DisplayService — protocol-facing service extending GenericService
// ============================================================================
namespace cppdesk::server {

// ---------------------------------------------------------------------------
// Helper: translate DisplayDescriptor → JSON for protocol messages
// ---------------------------------------------------------------------------
static nlohmann::json display_to_json(const detail::DisplayDescriptor& dd) {
    nlohmann::json j;
    j["index"]         = dd.index;
    j["name"]          = dd.name;
    j["device_path"]   = dd.device_path;
    j["width"]         = dd.width;
    j["height"]        = dd.height;
    j["x"]             = dd.x;
    j["y"]             = dd.y;
    j["physical_w_mm"] = dd.physical_w_mm;
    j["physical_h_mm"] = dd.physical_h_mm;
    j["scale_factor"]  = dd.scale_factor;
    j["is_primary"]    = dd.is_primary;
    j["is_builtin"]    = dd.is_builtin;
    j["is_connected"]  = dd.is_connected;
    j["is_virtual"]    = dd.is_virtual;

    // Current mode
    nlohmann::json cm;
    cm["width"]        = dd.current_mode.width;
    cm["height"]       = dd.current_mode.height;
    cm["refresh_rate"] = dd.current_mode.refresh_rate;
    cm["bpp"]          = dd.current_mode.bits_per_pixel;
    j["current_mode"]  = cm;

    // Supported modes (only include first 64 to avoid overwhelming the protocol)
    nlohmann::json modes = nlohmann::json::array();
    size_t mode_limit = std::min(dd.supported_modes.size(), size_t(64));
    for (size_t i = 0; i < mode_limit; ++i) {
        const auto& m = dd.supported_modes[i];
        nlohmann::json mj;
        mj["w"] = m.width;
        mj["h"] = m.height;
        mj["r"] = m.refresh_rate;
        modes.push_back(mj);
    }
    j["supported_modes"]     = modes;
    j["supported_mode_count"] = dd.supported_modes.size();

    // Formatted name
    j["formatted_name"] = detail::DisplayManager::instance().format_name(dd);

    return j;
}

static nlohmann::json virtual_desktop_to_json(const detail::VirtualDesktop& vd) {
    nlohmann::json j;
    j["min_x"]        = vd.min_x;
    j["min_y"]        = vd.min_y;
    j["max_x"]        = vd.max_x;
    j["max_y"]        = vd.max_y;
    j["total_width"]  = vd.total_width;
    j["total_height"] = vd.total_height;
    return j;
}

// ---------------------------------------------------------------------------
// DisplayService implementation
// ---------------------------------------------------------------------------
DisplayService::DisplayService()
    : GenericService(NAME) {
    spdlog::info("DisplayService constructed");
}

// ---------------------------------------------------------------------------
// Static helper: primary display index
// ---------------------------------------------------------------------------
int32_t DisplayService::primary_display_idx() {
    return detail::DisplayManager::instance().primary_index();
}

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------
void DisplayService::start() {
    std::lock_guard<std::mutex> lk(impl_mutex_);
    if (started_) return;

    auto& dm = detail::DisplayManager::instance();

    // Register for hotplug events so we can push updates to subscribers
    hotplug_callback_id_ = dm.register_hotplug_callback([this]() {
        this->push_display_update();
    });

    dm.start();
    started_ = true;
    spdlog::info("DisplayService started ({} displays, primary={})",
                 dm.enumerate().size(),
                 dm.primary_index());
}

void DisplayService::stop() {
    std::lock_guard<std::mutex> lk(impl_mutex_);
    if (!started_) return;

    auto& dm = detail::DisplayManager::instance();
    if (hotplug_callback_id_ != 0) {
        dm.unregister_hotplug_callback(hotplug_callback_id_);
        hotplug_callback_id_ = 0;
    }

    // Don't stop the DisplayManager here — it's a singleton shared across
    // services.  It will be stopped when the server shuts down.
    started_ = false;
    spdlog::info("DisplayService stopped");
}

// ---------------------------------------------------------------------------
// Protocol: build the full display list payload
// ---------------------------------------------------------------------------
nlohmann::json DisplayService::build_display_list() const {
    auto& dm = detail::DisplayManager::instance();
    auto displays = dm.enumerate();

    nlohmann::json result;
    result["type"] = "display_list";
    result["display_count"] = displays.size();
    result["primary_index"] = dm.primary_index();

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : displays) {
        arr.push_back(display_to_json(d));
    }
    result["displays"] = arr;

    // Virtual desktop info
    result["virtual_desktop"] = virtual_desktop_to_json(dm.virtual_desktop());

    // Statistics
    auto stats = dm.stats();
    result["stats"]["changes_detected"]   = stats.changes_detected;
    result["stats"]["resolution_changes"] = stats.resolution_changes;
    result["stats"]["hotplug_events"]     = stats.hotplug_events;

    return result;
}

// ---------------------------------------------------------------------------
// Protocol: get info for a specific display
// ---------------------------------------------------------------------------
nlohmann::json DisplayService::get_display_info(uint32_t index) const {
    auto& dm = detail::DisplayManager::instance();
    auto dd = dm.get(index);

    nlohmann::json result;
    result["type"] = "display_info";
    result["index"] = index;

    if (dd) {
        result["display"] = display_to_json(*dd);
        result["found"] = true;
        result["formatted_name"] = dm.format_name(index);
        result["scale_factor"] = dd->scale_factor;
    } else {
        result["found"] = false;
        result["error"] = "Display index out of range";
    }

    return result;
}

// ---------------------------------------------------------------------------
// Protocol: list supported resolutions for a display
// ---------------------------------------------------------------------------
nlohmann::json DisplayService::get_supported_resolutions(uint32_t index) const {
    auto& dm = detail::DisplayManager::instance();
    auto modes = dm.supported_modes(index);

    nlohmann::json result;
    result["type"] = "supported_resolutions";
    result["display_index"] = index;

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& m : modes) {
        nlohmann::json mj;
        mj["width"]        = m.width;
        mj["height"]       = m.height;
        mj["refresh_rate"] = m.refresh_rate;
        mj["bpp"]          = m.bits_per_pixel;
        arr.push_back(mj);
    }
    result["modes"] = arr;
    result["count"] = modes.size();

    return result;
}

// ---------------------------------------------------------------------------
// Protocol: change resolution
// ---------------------------------------------------------------------------
nlohmann::json DisplayService::set_resolution(uint32_t index,
                                              uint32_t width,
                                              uint32_t height,
                                              uint32_t refresh) {
    auto& dm = detail::DisplayManager::instance();
    bool ok = dm.change_resolution(index, width, height, refresh);

    nlohmann::json result;
    result["type"] = "resolution_change_result";
    result["display_index"] = index;
    result["success"] = ok;
    result["requested"]["width"]  = width;
    result["requested"]["height"] = height;
    result["requested"]["refresh"] = refresh;

    if (ok) {
        // Read back actual state
        auto dd = dm.get(index);
        if (dd) {
            result["current"]["width"]  = dd->width;
            result["current"]["height"] = dd->height;
        }
    } else {
        result["error"] = "Failed to change resolution";
    }

    return result;
}

// ---------------------------------------------------------------------------
// Protocol: get DPI / scale info for all displays
// ---------------------------------------------------------------------------
nlohmann::json DisplayService::get_dpi_info() const {
    auto& dm = detail::DisplayManager::instance();
    auto displays = dm.enumerate();

    nlohmann::json result;
    result["type"] = "dpi_info";

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : displays) {
        nlohmann::json dj;
        dj["index"]        = d.index;
        dj["name"]         = d.name;
        dj["scale_factor"] = d.scale_factor;
        dj["physical_w_mm"] = d.physical_w_mm;
        dj["physical_h_mm"] = d.physical_h_mm;

        // Compute DPI
        if (d.physical_w_mm > 0 && d.width > 0) {
            dj["dpi_x"] = std::round(
                static_cast<double>(d.width) /
                (static_cast<double>(d.physical_w_mm) / 25.4));
        } else {
            dj["dpi_x"] = 96.0 * d.scale_factor;
        }
        if (d.physical_h_mm > 0 && d.height > 0) {
            dj["dpi_y"] = std::round(
                static_cast<double>(d.height) /
                (static_cast<double>(d.physical_h_mm) / 25.4));
        } else {
            dj["dpi_y"] = 96.0 * d.scale_factor;
        }

        arr.push_back(dj);
    }
    result["displays"] = arr;

    return result;
}

// ---------------------------------------------------------------------------
// Protocol: get virtual desktop layout
// ---------------------------------------------------------------------------
nlohmann::json DisplayService::get_virtual_desktop() const {
    auto& dm = detail::DisplayManager::instance();
    auto vd = dm.virtual_desktop();
    return virtual_desktop_to_json(vd);
}

// ---------------------------------------------------------------------------
// Protocol: get display statistics
// ---------------------------------------------------------------------------
nlohmann::json DisplayService::get_display_stats() const {
    auto& dm = detail::DisplayManager::instance();
    auto stats = dm.stats();
    auto displays = dm.enumerate();

    nlohmann::json result;
    result["type"] = "display_stats";
    result["display_count"]      = displays.size();
    result["changes_detected"]   = stats.changes_detected;
    result["resolution_changes"] = stats.resolution_changes;
    result["hotplug_events"]     = stats.hotplug_events;

    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        now - stats.started_at).count();
    result["uptime_seconds"] = uptime;

    if (stats.changes_detected > 0) {
        auto since_change = std::chrono::duration_cast<std::chrono::seconds>(
            now - stats.last_change).count();
        result["seconds_since_last_change"] = since_change;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Push display update to all subscribers
// ---------------------------------------------------------------------------
void DisplayService::push_display_update() {
    spdlog::debug("DisplayService: pushing display update to subscribers");

    // Re-read display list
    auto payload = build_display_list();

    // Serialize to string (subscribers would receive this as binary)
    std::string payload_str = payload.dump();

    // Notify subscribers — the actual send mechanism is handled by Server
    // through the subscriber pattern. Here we store the latest payload
    // so it can be picked up by the video service or other consumers.
    {
        std::lock_guard<std::mutex> lk(impl_mutex_);
        last_display_payload_ = std::move(payload_str);
        push_version_++;
    }

    spdlog::info("DisplayService: update pushed (version {})",
                 push_version_);
}

// ---------------------------------------------------------------------------
// Accessors for the latest display payload (poll-based)
// ---------------------------------------------------------------------------
std::string DisplayService::last_payload() const {
    std::lock_guard<std::mutex> lk(impl_mutex_);
    return last_display_payload_;
}

uint64_t DisplayService::push_version() const {
    return push_version_;
}

// ============================================================================
// Coordinate-system helpers
// ============================================================================

int32_t DisplayService::display_index_at(int32_t x, int32_t y) const {
    auto& dm = detail::DisplayManager::instance();
    auto displays = dm.enumerate();

    for (const auto& d : displays) {
        if (x >= d.x && x < static_cast<int32_t>(d.x + d.width) &&
            y >= d.y && y < static_cast<int32_t>(d.y + d.height)) {
            return static_cast<int32_t>(d.index);
        }
    }
    return dm.primary_index(); // fallback
}

bool DisplayService::point_on_display(int32_t x, int32_t y,
                                      uint32_t index) const {
    auto& dm = detail::DisplayManager::instance();
    auto dd = dm.get(index);
    if (!dd) return false;

    return (x >= dd->x &&
            x < static_cast<int32_t>(dd->x + dd->width) &&
            y >= dd->y &&
            y < static_cast<int32_t>(dd->y + dd->height));
}

void DisplayService::global_to_display(int32_t global_x, int32_t global_y,
                                       uint32_t index,
                                       int32_t& out_x, int32_t& out_y) const {
    auto& dm = detail::DisplayManager::instance();
    auto dd = dm.get(index);
    if (!dd) {
        out_x = global_x;
        out_y = global_y;
        return;
    }
    out_x = global_x - dd->x;
    out_y = global_y - dd->y;
}

void DisplayService::display_to_global(int32_t display_x, int32_t display_y,
                                       uint32_t index,
                                       int32_t& out_x, int32_t& out_y) const {
    auto& dm = detail::DisplayManager::instance();
    auto dd = dm.get(index);
    if (!dd) {
        out_x = display_x;
        out_y = display_y;
        return;
    }
    out_x = display_x + dd->x;
    out_y = display_y + dd->y;
}

// ============================================================================
// Mode validation helpers
// ============================================================================

bool DisplayService::is_valid_mode(uint32_t index,
                                   uint32_t width, uint32_t height,
                                   uint32_t refresh) const {
    auto& dm = detail::DisplayManager::instance();
    auto modes = dm.supported_modes(index);

    for (const auto& m : modes) {
        if (m.width == width && m.height == height &&
            (refresh == 0 || m.refresh_rate == refresh || m.refresh_rate == 0)) {
            return true;
        }
    }
    return false;
}

std::optional<detail::DisplayMode> DisplayService::best_mode(
    uint32_t index, uint32_t width, uint32_t height) const {
    auto& dm = detail::DisplayManager::instance();
    auto modes = dm.supported_modes(index);

    // Exact match
    for (const auto& m : modes) {
        if (m.width == width && m.height == height) return m;
    }

    // Closest match (within 10% aspect ratio tolerance)
    if (height == 0 || width == 0) return std::nullopt;
    double target_aspect = static_cast<double>(width) / height;

    const detail::DisplayMode* best = nullptr;
    double best_score = 1e9;

    for (const auto& m : modes) {
        if (m.height == 0) continue;
        double aspect = static_cast<double>(m.width) / m.height;
        double aspect_diff = std::abs(aspect - target_aspect);

        if (aspect_diff < 0.1) { // within 10% aspect ratio
            double dist = std::abs(static_cast<double>(m.width) - width) +
                          std::abs(static_cast<double>(m.height) - height);
            if (dist < best_score) {
                best_score = dist;
                best = &m;
            }
        }
    }

    if (best) return *best;
    return std::nullopt;
}

// ============================================================================
// Debug / admin helpers
// ============================================================================

std::string DisplayService::dump_state() const {
    auto& dm = detail::DisplayManager::instance();
    auto displays = dm.enumerate();
    auto vd = dm.virtual_desktop();
    auto stats = dm.stats();

    std::ostringstream oss;
    oss << "=== DisplayService State ===\n";
    oss << "Displays: " << displays.size() << "\n";
    oss << "Primary index: " << dm.primary_index() << "\n";
    oss << "Virtual desktop: " << vd.total_width << "x"
        << vd.total_height << " ("
        << vd.min_x << "," << vd.min_y << ")-("
        << vd.max_x << "," << vd.max_y << ")\n";
    oss << "Changes detected: " << stats.changes_detected << "\n";
    oss << "Resolution changes: " << stats.resolution_changes << "\n";
    oss << "Hotplug events: " << stats.hotplug_events << "\n";
    oss << "Push version: " << push_version() << "\n\n";

    for (const auto& d : displays) {
        oss << "  [" << d.index << "] \"" << d.name << "\"\n";
        oss << "    Resolution: " << d.width << "x" << d.height << "\n";
        oss << "    Position: (" << d.x << ", " << d.y << ")\n";
        oss << "    Scale: " << d.scale_factor << "\n";
        oss << "    Physical: " << d.physical_w_mm << "x"
            << d.physical_h_mm << " mm\n";
        oss << "    Primary: " << (d.is_primary ? "yes" : "no")
            << "  Builtin: " << (d.is_builtin ? "yes" : "no") << "\n";
        oss << "    Modes: " << d.supported_modes.size() << "\n";
        oss << "    Path: " << d.device_path << "\n\n";
    }

    return oss.str();
}

} // namespace cppdesk::server
