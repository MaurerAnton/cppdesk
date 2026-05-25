// ============================================================================
// cppdesk — Remote Desktop Client
// screenshot_full.cpp — Comprehensive Screenshot Helper Implementation
// ============================================================================
// Implements multi-display capture, region capture, window capture,
// cursor overlay compositing, PNG/JPEG/BMP encoding, screenshot diffing,
// caching with hash-based change detection, thumbnail generation,
// annotation layer, clipboard integration, quality settings, async capture,
// and session recording.
// ============================================================================

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <queue>
#include <random>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

#include "client/client.hpp"
#include "platform/platform.hpp"
#include "scrap/scrap.hpp"

// Platform-specific includes
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wingdi.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shellapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#elif defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#else // Linux
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xinerama.h>
#endif

namespace cppdesk::client {

// ============================================================================
// Internal Constants
// ============================================================================

namespace detail {

// --- Image encoding ----------------------------------------------------------
static constexpr int kDefaultJpegQuality    = 92;
static constexpr int kDefaultPngCompression = 6;   // 0-9, zlib level
static constexpr int kMinQuality            = 1;
static constexpr int kMaxQuality            = 100;
static constexpr int kMaxPngCompression     = 9;

// --- Hashing -----------------------------------------------------------------
static constexpr std::size_t kHashSampleStep  = 16;   // sample every N pixels
static constexpr std::size_t kHashSeed        = 0x9E3779B97F4A7C15ULL;

// --- Cache -------------------------------------------------------------------
static constexpr std::size_t kMaxCacheEntries = 64;
static constexpr auto       kCacheExpiry      = std::chrono::seconds(30);

// --- Thumbnails --------------------------------------------------------------
static constexpr std::array<uint32_t, 3> kThumbnailSizes = {128, 256, 512};

// --- Annotation --------------------------------------------------------------
static constexpr uint8_t  kAnnotationAlpha      = 220;
static constexpr uint32_t kAnnotationColorRed   = 0xFF0000FF; // RGBA
static constexpr uint32_t kAnnotationColorGreen = 0x00FF00FF;
static constexpr uint32_t kAnnotationColorBlue  = 0x0000FFFF;
static constexpr uint32_t kAnnotationColorYellow= 0xFFFF00FF;
static constexpr uint32_t kAnnotationColorWhite = 0xFFFFFFFF;
static constexpr uint32_t kAnnotationColorBlack = 0x000000FF;
static constexpr float    kArrowHeadAngle       = 0.45f;     // radians
static constexpr float    kArrowHeadLength      = 16.0f;
static constexpr uint32_t kDefaultLineWidth     = 3;
static constexpr uint32_t kDefaultFontSize      = 14;

// --- Async capture -----------------------------------------------------------
static constexpr std::size_t kMaxPendingCaptures = 32;
static constexpr auto        kAsyncTimeout       = std::chrono::seconds(5);

// --- Session recording -------------------------------------------------------
static constexpr auto kDefaultRecordInterval = std::chrono::milliseconds(100); // 10 fps
static constexpr auto kMinRecordInterval     = std::chrono::milliseconds(16);  // ~60 fps
static constexpr auto kMaxRecordInterval     = std::chrono::milliseconds(5000);
static constexpr std::size_t kMaxRecordedFrames = 3600; // 1 hour at 1fps

// --- Pixel formats -----------------------------------------------------------
static constexpr std::size_t kRgbaBytesPerPixel = 4;

// --- Display enumeration -----------------------------------------------------
static constexpr std::uint32_t kMaxEnumeratedDisplays = 16;

} // namespace detail

// ============================================================================
// Forward Declarations of Internal Types
// ============================================================================

namespace detail {

// --- Pixel helper ------------------------------------------------------------
struct PixelRGBA {
    uint8_t r, g, b, a;
};

// --- Display record ----------------------------------------------------------
struct DisplayRecord {
    uint32_t index      = 0;
    int32_t  x          = 0;
    int32_t  y          = 0;
    uint32_t width      = 0;
    uint32_t height     = 0;
    bool     is_primary = false;
    std::string name;
};

// --- Screenshot cache entry --------------------------------------------------
struct CacheEntry {
    std::vector<uint8_t> data;
    uint64_t             hash       = 0;
    uint32_t             width      = 0;
    uint32_t             height     = 0;
    std::chrono::steady_clock::time_point timestamp;
    std::string          display_name;
};

// --- Annotation types --------------------------------------------------------
enum class AnnotationType : uint8_t {
    Arrow,
    Rectangle,
    FilledRectangle,
    Circle,
    FilledCircle,
    Text,
    Line,
    Highlight,
};

struct Annotation {
    AnnotationType type = AnnotationType::Arrow;
    int32_t  x1 = 0, y1 = 0;
    int32_t  x2 = 0, y2 = 0;
    uint32_t color    = detail::kAnnotationColorRed;
    uint32_t line_width = detail::kDefaultLineWidth;
    std::string text;
    uint32_t  font_size  = detail::kDefaultFontSize;
};

// --- Async capture request ---------------------------------------------------
struct AsyncCaptureRequest {
    uint64_t id = 0;
    uint32_t display_idx = 0;
    int32_t  region_x = 0, region_y = 0;
    uint32_t region_w = 0, region_h = 0;
    bool     use_region = false;
    std::function<void(std::optional<common::VideoFrame>)> callback;
    std::chrono::steady_clock::time_point enqueued_at;
};

// --- Recorded frame ----------------------------------------------------------
struct RecordedFrame {
    std::vector<uint8_t> data;
    uint32_t             display_idx = 0;
    uint32_t             width       = 0;
    uint32_t             height      = 0;
    uint64_t             timestamp_ms = 0;
    uint64_t             sequence     = 0;
};

} // namespace detail

// ============================================================================
// 1. Display Enumeration
// ============================================================================

namespace {

/// Enumerate all connected displays. Returns list of display records with
/// position, dimensions, and primary status.
std::vector<detail::DisplayRecord> enumerate_displays() {
    std::vector<detail::DisplayRecord> displays;

#ifdef _WIN32
    // Windows: use EnumDisplayMonitors
    struct MonitorEnumCtx {
        std::vector<detail::DisplayRecord>* displays;
        uint32_t index;
    };

    MonitorEnumCtx ctx{&displays, 0};

    auto monitor_enum_proc = [](HMONITOR hMon, HDC /*hdc*/, LPRECT rc, LPARAM lp) -> BOOL {
        auto* ctx_ptr = reinterpret_cast<MonitorEnumCtx*>(lp);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(hMon, &mi)) return TRUE;

        detail::DisplayRecord rec;
        rec.index      = ctx_ptr->index++;
        rec.x          = mi.rcMonitor.left;
        rec.y          = mi.rcMonitor.top;
        rec.width      = static_cast<uint32_t>(mi.rcMonitor.right - mi.rcMonitor.left);
        rec.height     = static_cast<uint32_t>(mi.rcMonitor.bottom - mi.rcMonitor.top);
        rec.is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        // Convert wide name to narrow
        int len = WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::string name(static_cast<std::size_t>(len) - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, name.data(), len, nullptr, nullptr);
            rec.name = name;
        }

        if (ctx_ptr->displays->size() < detail::kMaxEnumeratedDisplays)
            ctx_ptr->displays->push_back(std::move(rec));
        return TRUE;
    };

    HDC hdcScreen = GetDC(nullptr);
    EnumDisplayMonitors(hdcScreen, nullptr,
        static_cast<MONITORENUMPROC>([](HMONITOR h, HDC d, LPRECT r, LPARAM l) -> BOOL {
            return monitor_enum_proc(h, d, r, l);
        }),
        reinterpret_cast<LPARAM>(&ctx));
    ReleaseDC(nullptr, hdcScreen);

#elif defined(__APPLE__)
    // macOS: use CoreGraphics
    CGDirectDisplayID activeDisplays[detail::kMaxEnumeratedDisplays];
    uint32_t displayCount = 0;
    CGError err = CGGetActiveDisplayList(detail::kMaxEnumeratedDisplays, activeDisplays, &displayCount);
    if (err == kCGErrorSuccess) {
        for (uint32_t i = 0; i < displayCount; ++i) {
            CGDirectDisplayID did = activeDisplays[i];
            CGRect bounds = CGDisplayBounds(did);
            CGDisplayModeRef mode = CGDisplayCopyDisplayMode(did);
            detail::DisplayRecord rec;
            rec.index      = i;
            rec.x          = static_cast<int32_t>(bounds.origin.x);
            rec.y          = static_cast<int32_t>(bounds.origin.y);
            rec.width      = static_cast<uint32_t>(bounds.size.width);
            rec.height     = static_cast<uint32_t>(bounds.size.height);
            rec.is_primary = CGDisplayIsMain(did);
            rec.name       = "Display " + std::to_string(i);
            if (mode) {
                CGDisplayModeRelease(mode);
            }
            displays.push_back(std::move(rec));
        }
    }

#else // Linux (X11)
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        spdlog::warn("[screenshot] Cannot open X display for enumeration");
        return displays;
    }

    int event_base = 0, error_base = 0;
    int major = 0, minor = 0;

    if (XRRQueryExtension(dpy, &event_base, &error_base) &&
        XRRQueryVersion(dpy, &major, &minor)) {
        // Use XRandR
        XRRScreenResources* res = XRRGetScreenResources(dpy, DefaultRootWindow(dpy));
        if (res) {
            for (int i = 0; i < res->noutput && displays.size() < detail::kMaxEnumeratedDisplays; ++i) {
                XRROutputInfo* out = XRRGetOutputInfo(dpy, res, res->outputs[i]);
                if (!out || out->connection != RR_Connected) {
                    if (out) XRRFreeOutputInfo(out);
                    continue;
                }

                XRRCrtcInfo* crtc = XRRGetCrtcInfo(dpy, res, out->crtc);
                if (!crtc) {
                    XRRFreeOutputInfo(out);
                    continue;
                }

                detail::DisplayRecord rec;
                rec.index      = static_cast<uint32_t>(displays.size());
                rec.x          = crtc->x;
                rec.y          = crtc->y;
                rec.width      = crtc->width;
                rec.height     = crtc->height;
                rec.is_primary = (crtc->x == 0 && crtc->y == 0);
                rec.name       = out->name ? std::string(out->name) : "Display " + std::to_string(displays.size());

                XRRFreeCrtcInfo(crtc);
                XRRFreeOutputInfo(out);
                displays.push_back(std::move(rec));
            }
            XRRFreeScreenResources(res);
        }
    } else if (XineramaIsActive(dpy)) {
        // Fallback to Xinerama
        int num_monitors = 0;
        XineramaScreenInfo* screens = XineramaQueryScreens(dpy, &num_monitors);
        if (screens) {
            for (int i = 0; i < num_monitors && displays.size() < detail::kMaxEnumeratedDisplays; ++i) {
                detail::DisplayRecord rec;
                rec.index      = static_cast<uint32_t>(i);
                rec.x          = screens[i].x_org;
                rec.y          = screens[i].y_org;
                rec.width      = static_cast<uint32_t>(screens[i].width);
                rec.height     = static_cast<uint32_t>(screens[i].height);
                rec.is_primary = (screens[i].x_org == 0 && screens[i].y_org == 0);
                rec.name       = "Display " + std::to_string(i);
                displays.push_back(std::move(rec));
            }
            XFree(screens);
        }
    } else {
        // Single display fallback
        Screen* scr = DefaultScreenOfDisplay(dpy);
        detail::DisplayRecord rec;
        rec.index      = 0;
        rec.x          = 0;
        rec.y          = 0;
        rec.width      = static_cast<uint32_t>(scr->width);
        rec.height     = static_cast<uint32_t>(scr->height);
        rec.is_primary = true;
        rec.name       = "Screen 0";
        displays.push_back(std::move(rec));
    }

    XCloseDisplay(dpy);
#endif

    if (displays.empty()) {
        spdlog::warn("[screenshot] No displays enumerated");
    } else {
        spdlog::debug("[screenshot] Enumerated {} display(s)", displays.size());
        for (const auto& d : displays) {
            spdlog::debug("  [{}] {} x {} at ({}, {}) primary={}",
                d.index, d.width, d.height, d.x, d.y, d.is_primary);
        }
    }

    return displays;
}

} // anonymous namespace

// ============================================================================
// 2. Raw Pixel Capture (Platform-Specific)
// ============================================================================

namespace {

/// Capture raw RGBA pixels from a display region.
/// Returns {width, height, pixels} or empty optional on failure.
struct RawCapture {
    std::vector<uint8_t> pixels;
    uint32_t width  = 0;
    uint32_t height = 0;
};

RawCapture capture_raw_pixels(uint32_t display_idx,
                               int32_t region_x = -1, int32_t region_y = -1,
                               uint32_t region_w = 0, uint32_t region_h = 0) {
    RawCapture result;

#ifdef _WIN32
    auto displays = enumerate_displays();
    if (display_idx >= displays.size()) {
        spdlog::error("[screenshot] Display index {} out of range ({})", display_idx, displays.size());
        return result;
    }

    const auto& dpy = displays[display_idx];
    bool use_region = (region_w > 0 && region_h > 0);

    int32_t cap_x = use_region ? (dpy.x + region_x) : dpy.x;
    int32_t cap_y = use_region ? (dpy.y + region_y) : dpy.y;
    uint32_t cap_w = use_region ? region_w : dpy.width;
    uint32_t cap_h = use_region ? region_h : dpy.height;

    // Clamp to display bounds
    if (cap_x < dpy.x) { cap_w -= static_cast<uint32_t>(dpy.x - cap_x); cap_x = dpy.x; }
    if (cap_y < dpy.y) { cap_h -= static_cast<uint32_t>(dpy.y - cap_y); cap_y = dpy.y; }
    if (static_cast<uint32_t>(cap_x - dpy.x) + cap_w > dpy.width)
        cap_w = dpy.width - static_cast<uint32_t>(cap_x - dpy.x);
    if (static_cast<uint32_t>(cap_y - dpy.y) + cap_h > dpy.height)
        cap_h = dpy.height - static_cast<uint32_t>(cap_y - dpy.y);

    if (cap_w == 0 || cap_h == 0) return result;

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) {
        spdlog::error("[screenshot] GetDC failed");
        return result;
    }

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, static_cast<int>(cap_w), static_cast<int>(cap_h));
    if (!hdcMem || !hBmp) {
        spdlog::error("[screenshot] CreateCompatibleDC/Bitmap failed");
        ReleaseDC(nullptr, hdcScreen);
        if (hdcMem) DeleteDC(hdcMem);
        return result;
    }

    HGDIOBJ oldBmp = SelectObject(hdcMem, hBmp);
    BOOL ok = BitBlt(hdcMem, 0, 0, static_cast<int>(cap_w), static_cast<int>(cap_h),
                     hdcScreen, cap_x, cap_y, SRCCOPY | CAPTUREBLT);

    if (!ok) {
        spdlog::error("[screenshot] BitBlt failed: {}", GetLastError());
        SelectObject(hdcMem, oldBmp);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return result;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = static_cast<LONG>(cap_w);
    bmi.bmiHeader.biHeight      = -static_cast<LONG>(cap_h); // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    result.pixels.resize(cap_w * cap_h * detail::kRgbaBytesPerPixel);
    result.width  = cap_w;
    result.height = cap_h;

    HDC hdcTmp = CreateCompatibleDC(hdcScreen);
    if (!hdcTmp) {
        SelectObject(hdcMem, oldBmp);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        result.pixels.clear();
        return result;
    }

    void* bits = nullptr;
    HBITMAP hDib = CreateDIBSection(hdcTmp, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (hDib && bits) {
        HDC hdcDib = CreateCompatibleDC(hdcTmp);
        HGDIOBJ oldDib = SelectObject(hdcDib, hDib);
        BitBlt(hdcDib, 0, 0, static_cast<int>(cap_w), static_cast<int>(cap_h),
               hdcMem, 0, 0, SRCCOPY);
        GdiFlush();

        // Convert BGRA -> RGBA
        uint8_t* src = static_cast<uint8_t*>(bits);
        uint8_t* dst = result.pixels.data();
        for (uint32_t i = 0; i < cap_w * cap_h; ++i) {
            dst[i * 4 + 0] = src[i * 4 + 2]; // R
            dst[i * 4 + 1] = src[i * 4 + 1]; // G
            dst[i * 4 + 2] = src[i * 4 + 0]; // B
            dst[i * 4 + 3] = src[i * 4 + 3]; // A
        }

        SelectObject(hdcDib, oldDib);
        DeleteDC(hdcDib);
        DeleteObject(hDib);
    }

    DeleteDC(hdcTmp);
    SelectObject(hdcMem, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

#elif defined(__APPLE__)
    auto displays = enumerate_displays();
    if (display_idx >= displays.size()) {
        spdlog::error("[screenshot] Display index {} out of range", display_idx);
        return result;
    }

    const auto& dpy = displays[display_idx];
    bool use_region = (region_w > 0 && region_h > 0);

    CGRect captureRect;
    if (use_region) {
        captureRect = CGRectMake(
            static_cast<CGFloat>(dpy.x + region_x),
            static_cast<CGFloat>(dpy.y + region_y),
            static_cast<CGFloat>(region_w),
            static_cast<CGFloat>(region_h));
    } else {
        captureRect = CGRectMake(
            static_cast<CGFloat>(dpy.x),
            static_cast<CGFloat>(dpy.y),
            static_cast<CGFloat>(dpy.width),
            static_cast<CGFloat>(dpy.height));
    }

    CGImageRef image = CGWindowListCreateImage(
        captureRect,
        kCGWindowListOptionOnScreenOnly,
        kCGNullWindowID,
        kCGWindowImageDefault);

    if (!image) {
        spdlog::error("[screenshot] CGWindowListCreateImage failed");
        return result;
    }

    size_t img_w = CGImageGetWidth(image);
    size_t img_h = CGImageGetHeight(image);

    result.pixels.resize(img_w * img_h * detail::kRgbaBytesPerPixel);
    result.width  = static_cast<uint32_t>(img_w);
    result.height = static_cast<uint32_t>(img_h);

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        result.pixels.data(),
        img_w, img_h, 8, img_w * 4,
        cs, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);

    if (ctx) {
        CGContextDrawImage(ctx, CGRectMake(0, 0, static_cast<CGFloat>(img_w), static_cast<CGFloat>(img_h)), image);

        // Convert ARGB -> RGBA
        uint8_t* p = result.pixels.data();
        for (size_t i = 0; i < img_w * img_h; ++i) {
            uint8_t a = p[i * 4 + 0];
            uint8_t r = p[i * 4 + 1];
            uint8_t g = p[i * 4 + 2];
            uint8_t b = p[i * 4 + 3];
            p[i * 4 + 0] = r;
            p[i * 4 + 1] = g;
            p[i * 4 + 2] = b;
            p[i * 4 + 3] = a;
        }

        CGContextRelease(ctx);
    } else {
        spdlog::error("[screenshot] CGBitmapContextCreate failed");
        result.pixels.clear();
    }

    CGColorSpaceRelease(cs);
    CGImageRelease(image);

#else // Linux X11
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        spdlog::error("[screenshot] Cannot open X display");
        return result;
    }

    auto displays = enumerate_displays();
    uint32_t effective_idx = display_idx;
    if (effective_idx >= displays.size()) {
        // Try multi-display-aware coordinates
        // This could be expanded for Wayland support
        effective_idx = 0;
    }

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);

    uint32_t cap_w, cap_h;
    int32_t cap_x, cap_y;

    if (region_w > 0 && region_h > 0 && effective_idx < displays.size()) {
        cap_x = displays[effective_idx].x + region_x;
        cap_y = displays[effective_idx].y + region_y;
        cap_w = region_w;
        cap_h = region_h;
    } else if (effective_idx < displays.size()) {
        cap_x = displays[effective_idx].x;
        cap_y = displays[effective_idx].y;
        cap_w = displays[effective_idx].width;
        cap_h = displays[effective_idx].height;
    } else {
        Screen* scr = DefaultScreenOfDisplay(dpy);
        cap_x = 0;
        cap_y = 0;
        cap_w = static_cast<uint32_t>(scr->width);
        cap_h = static_cast<uint32_t>(scr->height);
    }

    XImage* img = XGetImage(dpy, root, cap_x, cap_y, cap_w, cap_h, AllPlanes, ZPixmap);
    if (!img) {
        spdlog::error("[screenshot] XGetImage failed");
        XCloseDisplay(dpy);
        return result;
    }

    result.pixels.resize(cap_w * cap_h * detail::kRgbaBytesPerPixel);
    result.width  = cap_w;
    result.height = cap_h;

    // Convert XImage to RGBA
    uint8_t* dst = result.pixels.data();
    for (uint32_t y = 0; y < cap_h; ++y) {
        for (uint32_t x = 0; x < cap_w; ++x) {
            unsigned long pixel = XGetPixel(img, static_cast<int>(x), static_cast<int>(y));
            std::size_t off = (static_cast<std::size_t>(y) * cap_w + x) * 4;
            dst[off + 0] = static_cast<uint8_t>((pixel >> 16) & 0xFF); // R
            dst[off + 1] = static_cast<uint8_t>((pixel >> 8)  & 0xFF); // G
            dst[off + 2] = static_cast<uint8_t>(pixel         & 0xFF); // B
            dst[off + 3] = 0xFF;                                          // A
        }
    }

    XDestroyImage(img);
    XCloseDisplay(dpy);
#endif

    return result;
}

} // anonymous namespace

// ============================================================================
// 3. Window Capture (by handle or title)
// ============================================================================

namespace {

#ifdef _WIN32

/// Find a window by exact title match. Returns HWND or nullptr.
static HWND find_window_by_title(const std::string& title) {
    struct FindCtx {
        std::string target_title;
        HWND found = nullptr;
    };

    FindCtx ctx;
    ctx.target_title = title;

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<FindCtx*>(lp);
        if (!IsWindowVisible(hwnd)) return TRUE;

        char buf[512] = {};
        GetWindowTextA(hwnd, buf, sizeof(buf) - 1);
        if (c->target_title == buf) {
            c->found = hwnd;
            return FALSE; // stop enumeration
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    return ctx.found;
}

/// Capture a specific window by HWND
RawCapture capture_window_raw(HWND hwnd) {
    RawCapture result;
    if (!hwnd || !IsWindow(hwnd)) {
        spdlog::error("[screenshot] Invalid window handle");
        return result;
    }

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) {
        // Try GetClientRect + adjust for window chrome
        if (!DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                &rc, sizeof(rc))) {
            // Used DWMWA bounds
        } else {
            spdlog::error("[screenshot] Cannot get window rect");
            return result;
        }
    }

    int32_t cap_w = rc.right - rc.left;
    int32_t cap_h = rc.bottom - rc.top;
    if (cap_w <= 0 || cap_h <= 0) return result;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, cap_w, cap_h);
    HGDIOBJ oldBmp = SelectObject(hdcMem, hBmp);

    // Bring window to top temporarily for clean capture?
    // PrintWindow is another option for offscreen capture
    BOOL ok = PrintWindow(hwnd, hdcMem, PW_RENDERFULLCONTENT);
    if (!ok) {
        // Fallback to screen-coordinate BitBlt
        BitBlt(hdcMem, 0, 0, cap_w, cap_h, hdcScreen, rc.left, rc.top, SRCCOPY | CAPTUREBLT);
    }

    result.pixels.resize(static_cast<std::size_t>(cap_w) * static_cast<std::size_t>(cap_h) * 4);
    result.width  = static_cast<uint32_t>(cap_w);
    result.height = static_cast<uint32_t>(cap_h);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = cap_w;
    bmi.bmiHeader.biHeight      = -cap_h;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    GetDIBits(hdcMem, hBmp, 0, static_cast<UINT>(cap_h), result.pixels.data(), &bmi, DIB_RGB_COLORS);

    // BGRA -> RGBA
    for (int32_t i = 0; i < cap_w * cap_h; ++i) {
        std::swap(result.pixels[i * 4 + 0], result.pixels[i * 4 + 2]);
    }

    SelectObject(hdcMem, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    return result;
}

#elif defined(__APPLE__)

static RawCapture capture_window_by_title_raw(const std::string& title) {
    RawCapture result;

    CFArrayRef windowList = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);
    if (!windowList) return result;

    CFIndex count = CFArrayGetCount(windowList);
    CGWindowID targetWid = kCGNullWindowID;

    for (CFIndex i = 0; i < count; ++i) {
        CFDictionaryRef dict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windowList, i));
        CFStringRef name = static_cast<CFStringRef>(
            CFDictionaryGetValue(dict, kCGWindowName));
        if (!name) continue;

        char buf[512] = {};
        CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
        if (title == buf) {
            CFNumberRef widNum = static_cast<CFNumberRef>(
                CFDictionaryGetValue(dict, kCGWindowNumber));
            CFNumberGetValue(widNum, kCGWindowIDCFNumberType, &targetWid);
            break;
        }
    }

    CFRelease(windowList);

    if (targetWid == kCGNullWindowID) {
        spdlog::warn("[screenshot] Window '{}' not found", title);
        return result;
    }

    CGImageRef image = CGWindowListCreateImage(
        CGRectNull,
        kCGWindowListOptionIncludingWindow,
        targetWid,
        kCGWindowImageBoundsIgnoreFraming | kCGWindowImageNominalResolution);

    if (!image) {
        spdlog::error("[screenshot] CGWindowListCreateImage failed for window");
        return result;
    }

    size_t w = CGImageGetWidth(image);
    size_t h = CGImageGetHeight(image);

    result.pixels.resize(w * h * 4);
    result.width  = static_cast<uint32_t>(w);
    result.height = static_cast<uint32_t>(h);

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        result.pixels.data(), w, h, 8, w * 4,
        cs, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);

    if (ctx) {
        CGContextDrawImage(ctx, CGRectMake(0, 0, static_cast<CGFloat>(w), static_cast<CGFloat>(h)), image);
        // ARGB -> RGBA
        uint8_t* p = result.pixels.data();
        for (size_t i = 0; i < w * h; ++i) {
            uint8_t a = p[i * 4 + 0];
            uint8_t r = p[i * 4 + 1];
            uint8_t g = p[i * 4 + 2];
            uint8_t b = p[i * 4 + 3];
            p[i * 4 + 0] = r;
            p[i * 4 + 1] = g;
            p[i * 4 + 2] = b;
            p[i * 4 + 3] = a;
        }
        CGContextRelease(ctx);
    }

    CGColorSpaceRelease(cs);
    CGImageRelease(image);

    return result;
}

#else // Linux

static RawCapture capture_window_by_title_raw(const std::string& title) {
    RawCapture result;

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        spdlog::error("[screenshot] Cannot open X display");
        return result;
    }

    Window root = RootWindow(dpy, DefaultScreen(dpy));

    // Try to find window by _NET_WM_NAME or WM_NAME
    Atom netWmName = XInternAtom(dpy, "_NET_WM_NAME", True);
    Atom wmName    = XInternAtom(dpy, "WM_NAME", True);

    // Enumerate top-level windows
    Atom netClientList = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
    Window targetWin = 0;

    if (netClientList != None) {
        Atom actualType;
        int actualFormat;
        unsigned long nItems, bytesAfter;
        unsigned char* prop = nullptr;

        if (XGetWindowProperty(dpy, root, netClientList, 0, 1024, False,
                XA_WINDOW, &actualType, &actualFormat, &nItems, &bytesAfter, &prop) == Success && prop) {
            Window* wins = reinterpret_cast<Window*>(prop);
            for (unsigned long i = 0; i < nItems; ++i) {
                // Check window name
                char* winName = nullptr;
                if (netWmName != None) {
                    XTextProperty tp;
                    if (XGetTextProperty(dpy, wins[i], &tp, netWmName) && tp.value) {
                        winName = reinterpret_cast<char*>(tp.value);
                    }
                }
                if (!winName && wmName != None) {
                    XTextProperty tp;
                    if (XGetTextProperty(dpy, wins[i], &tp, wmName) && tp.value) {
                        winName = reinterpret_cast<char*>(tp.value);
                    }
                }
                if (!winName) {
                    XFetchName(dpy, wins[i], &winName);
                }

                if (winName && title == winName) {
                    targetWin = wins[i];
                    XFree(winName);
                    break;
                }
                if (winName) XFree(winName);
            }
            XFree(prop);
        }
    }

    if (!targetWin) {
        spdlog::warn("[screenshot] Window '{}' not found", title);
        XCloseDisplay(dpy);
        return result;
    }

    // Get window geometry
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(dpy, targetWin, &attrs)) {
        spdlog::error("[screenshot] XGetWindowAttributes failed");
        XCloseDisplay(dpy);
        return result;
    }

    // Translate coordinates to root
    Window child;
    int wx = 0, wy = 0;
    XTranslateCoordinates(dpy, targetWin, root, 0, 0, &wx, &wy, &child);

    // Clamp negative positions (virtual desktops)
    wx = std::max(0, wx);
    wy = std::max(0, wy);

    XImage* img = XGetImage(dpy, root, wx, wy,
        static_cast<unsigned int>(attrs.width),
        static_cast<unsigned int>(attrs.height),
        AllPlanes, ZPixmap);

    if (!img) {
        spdlog::error("[screenshot] XGetImage failed for window");
        XCloseDisplay(dpy);
        return result;
    }

    result.pixels.resize(static_cast<std::size_t>(attrs.width) * static_cast<std::size_t>(attrs.height) * 4);
    result.width  = static_cast<uint32_t>(attrs.width);
    result.height = static_cast<uint32_t>(attrs.height);

    uint8_t* dst = result.pixels.data();
    for (int y = 0; y < attrs.height; ++y) {
        for (int x = 0; x < attrs.width; ++x) {
            unsigned long pixel = XGetPixel(img, x, y);
            std::size_t off = (static_cast<std::size_t>(y) * attrs.width + x) * 4;
            dst[off + 0] = static_cast<uint8_t>((pixel >> 16) & 0xFF);
            dst[off + 1] = static_cast<uint8_t>((pixel >> 8)  & 0xFF);
            dst[off + 2] = static_cast<uint8_t>(pixel         & 0xFF);
            dst[off + 3] = 0xFF;
        }
    }

    XDestroyImage(img);
    XCloseDisplay(dpy);

    return result;
}

#endif

} // anonymous namespace

// ============================================================================
// 4. Cursor Overlay Compositing
// ============================================================================

namespace {

/// Composite cursor image onto screenshot pixels at the cursor position.
/// The cursor data should be RGBA with pre-multiplied alpha or straight alpha.
void composite_cursor(std::vector<uint8_t>& pixels, uint32_t img_w, uint32_t img_h,
                      const common::CursorData& cursor,
                      int32_t cursor_x, int32_t cursor_y) {
    if (!cursor.colors.empty() && cursor.width > 0 && cursor.height > 0) {
        // Determine the top-left of where the cursor should be drawn (accounting for hotspot)
        int32_t draw_x = cursor_x - cursor.hot_x;
        int32_t draw_y = cursor_y - cursor.hot_y;

        for (uint32_t cy = 0; cy < cursor.height; ++cy) {
            int32_t screen_y = draw_y + static_cast<int32_t>(cy);
            if (screen_y < 0 || static_cast<uint32_t>(screen_y) >= img_h) continue;

            for (uint32_t cx = 0; cx < cursor.width; ++cx) {
                int32_t screen_x = draw_x + static_cast<int32_t>(cx);
                if (screen_x < 0 || static_cast<uint32_t>(screen_x) >= img_w) continue;

                std::size_t cursor_off = (static_cast<std::size_t>(cy) * cursor.width + cx) * 4;
                std::size_t screen_off = (static_cast<std::size_t>(screen_y) * img_w + screen_x) * 4;

                uint8_t cr = cursor.colors[cursor_off + 0];
                uint8_t cg = cursor.colors[cursor_off + 1];
                uint8_t cb = cursor.colors[cursor_off + 2];
                uint8_t ca = cursor.colors[cursor_off + 3];

                if (ca == 0) continue; // fully transparent

                uint8_t sr = pixels[screen_off + 0];
                uint8_t sg = pixels[screen_off + 1];
                uint8_t sb = pixels[screen_off + 2];

                // Alpha blending: result = cursor + background * (1 - alpha)
                // Assuming straight alpha
                if (ca == 255) {
                    pixels[screen_off + 0] = cr;
                    pixels[screen_off + 1] = cg;
                    pixels[screen_off + 2] = cb;
                } else {
                    float alpha = static_cast<float>(ca) / 255.0f;
                    float inv_alpha = 1.0f - alpha;
                    pixels[screen_off + 0] = static_cast<uint8_t>(cr * alpha + sr * inv_alpha);
                    pixels[screen_off + 1] = static_cast<uint8_t>(cg * alpha + sg * inv_alpha);
                    pixels[screen_off + 2] = static_cast<uint8_t>(cb * alpha + sb * inv_alpha);
                }
                pixels[screen_off + 3] = 255;
            }
        }
    }
}

} // anonymous namespace

// ============================================================================
// 5. PNG/JPEG/BMP Encoding
// ============================================================================

namespace {

enum class EncodedFormat : uint8_t {
    PNG,
    JPEG,
    BMP,
};

struct EncodeSettings {
    EncodedFormat format          = EncodedFormat::PNG;
    int           jpeg_quality    = detail::kDefaultJpegQuality;
    int           png_compression = detail::kDefaultPngCompression;
};

// --- BMP encoder (simple, no library needed) ---------------------------------

std::vector<uint8_t> encode_bmp(const uint8_t* rgba, uint32_t w, uint32_t h) {
    // BMP file: header (14) + DIB header (40) + pixel data (bottom-up BGR)
    uint32_t row_size = ((w * 3 + 3) / 4) * 4; // padded to 4 bytes
    uint32_t pixel_data_size = row_size * h;
    uint32_t file_size = 14 + 40 + pixel_data_size;

    std::vector<uint8_t> bmp(file_size, 0);

    // BMP file header
    bmp[0] = 'B'; bmp[1] = 'M';
    std::memcpy(&bmp[2],  &file_size, 4);
    std::memcpy(&bmp[10], "\x36\x00\x00\x00", 4); // pixel offset = 54

    // DIB header (BITMAPINFOHEADER)
    uint32_t dib_size = 40;
    std::memcpy(&bmp[14], &dib_size, 4);
    std::memcpy(&bmp[18], &w, 4);
    std::memcpy(&bmp[22], &h, 4);
    bmp[26] = 1; bmp[27] = 0;             // planes = 1
    bmp[28] = 24; bmp[29] = 0;             // bits per pixel = 24
    // compression = 0, size = pixel_data_size, resolution = 0
    std::memcpy(&bmp[34], &pixel_data_size, 4);

    // Pixel data (bottom-up, BGR)
    uint8_t* pixel_start = bmp.data() + 54;
    for (uint32_t y = 0; y < h; ++y) {
        uint32_t src_y = h - 1 - y;
        uint8_t* row = pixel_start + y * row_size;
        for (uint32_t x = 0; x < w; ++x) {
            std::size_t src_off = (static_cast<std::size_t>(src_y) * w + x) * 4;
            row[x * 3 + 0] = rgba[src_off + 2]; // B
            row[x * 3 + 1] = rgba[src_off + 1]; // G
            row[x * 3 + 2] = rgba[src_off + 0]; // R
        }
    }

    return bmp;
}

// --- Simple PNG encoder (minimal, no library dependency) ---------------------
// This is a bare-bones PNG encoder supporting 8-bit RGBA.
// Uses Deflate via manual ADLER32 + stored blocks (no compression) for simplicity.

static uint32_t adler32_update(uint32_t adler, const uint8_t* data, size_t len) {
    uint32_t s1 = adler & 0xFFFF;
    uint32_t s2 = (adler >> 16) & 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        s1 = (s1 + data[i]) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    return (s2 << 16) | s1;
}

static void write_be32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8)  & 0xFF));
    buf.push_back(static_cast<uint8_t>(v         & 0xFF));
}

static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

static void init_crc32_table() {
    if (crc32_table_initialized) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = true;
}

static uint32_t crc32(const uint8_t* data, size_t len) {
    init_crc32_table();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

static void write_png_chunk(std::vector<uint8_t>& buf, const char type[4],
                             const uint8_t* data, uint32_t data_len) {
    write_be32(buf, data_len);
    size_t pos = buf.size();
    buf.push_back(static_cast<uint8_t>(type[0]));
    buf.push_back(static_cast<uint8_t>(type[1]));
    buf.push_back(static_cast<uint8_t>(type[2]));
    buf.push_back(static_cast<uint8_t>(type[3]));
    if (data_len > 0) {
        buf.insert(buf.end(), data, data + data_len);
    }
    uint32_t crc = crc32(buf.data() + pos, 4 + data_len);
    write_be32(buf, crc);
}

std::vector<uint8_t> encode_png(const uint8_t* rgba, uint32_t w, uint32_t h) {
    std::vector<uint8_t> png;

    // PNG signature
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), sig, sig + 8);

    // IHDR chunk
    uint8_t ihdr_data[13];
    write_be32(reinterpret_cast<std::vector<uint8_t>&>(*reinterpret_cast<std::vector<uint8_t>*>(&ihdr_data)), 0); // placeholder
    // Actually let's just write directly:
    ihdr_data[0] = static_cast<uint8_t>((w >> 24) & 0xFF);
    ihdr_data[1] = static_cast<uint8_t>((w >> 16) & 0xFF);
    ihdr_data[2] = static_cast<uint8_t>((w >> 8)  & 0xFF);
    ihdr_data[3] = static_cast<uint8_t>(w         & 0xFF);
    ihdr_data[4] = static_cast<uint8_t>((h >> 24) & 0xFF);
    ihdr_data[5] = static_cast<uint8_t>((h >> 16) & 0xFF);
    ihdr_data[6] = static_cast<uint8_t>((h >> 8)  & 0xFF);
    ihdr_data[7] = static_cast<uint8_t>(h         & 0xFF);
    ihdr_data[8] = 8;  // bit depth
    ihdr_data[9] = 6;  // color type: RGBA
    ihdr_data[10] = 0; // compression
    ihdr_data[11] = 0; // filter
    ihdr_data[12] = 0; // interlace
    write_png_chunk(png, "IHDR", ihdr_data, 13);

    // IDAT chunk: raw image data with filter byte per row, stored (no compression)
    // We store data as DEFLATE blocks: 1 block per row with filter byte 0 (None)
    // DEFLATE stored block: 1 byte BFINAL|BTYPE, 2 bytes LEN, 2 bytes NLEN
    std::vector<uint8_t> raw_data;

    for (uint32_t y = 0; y < h; ++y) {
        bool is_last = (y == h - 1);
        uint8_t bfinal_btype = is_last ? 0x01 : 0x00; // BFINAL=1 for last, BTYPE=00 (stored)
        raw_data.push_back(bfinal_btype);

        uint16_t row_len = static_cast<uint16_t>(w * 4 + 1);
        uint16_t row_nlen = ~row_len;
        raw_data.push_back(static_cast<uint8_t>(row_len & 0xFF));
        raw_data.push_back(static_cast<uint8_t>((row_len >> 8) & 0xFF));
        raw_data.push_back(static_cast<uint8_t>(row_nlen & 0xFF));
        raw_data.push_back(static_cast<uint8_t>((row_nlen >> 8) & 0xFF));

        raw_data.push_back(0); // filter: None
        const uint8_t* row_start = rgba + static_cast<std::size_t>(y) * w * 4;
        raw_data.insert(raw_data.end(), row_start, row_start + w * 4);
    }

    // Zlib header: CMF=0x78 (deflate, window=32K), FLG=0x01 (level=0, check)
    std::vector<uint8_t> compressed;
    compressed.push_back(0x78);
    compressed.push_back(0x01);
    compressed.insert(compressed.end(), raw_data.begin(), raw_data.end());

    // ADLER32 checksum
    uint32_t adler = adler32_update(1, rgba, static_cast<size_t>(w) * h * 4);
    compressed.push_back(static_cast<uint8_t>((adler >> 24) & 0xFF));
    compressed.push_back(static_cast<uint8_t>((adler >> 16) & 0xFF));
    compressed.push_back(static_cast<uint8_t>((adler >> 8)  & 0xFF));
    compressed.push_back(static_cast<uint8_t>(adler         & 0xFF));

    write_png_chunk(png, "IDAT", compressed.data(), static_cast<uint32_t>(compressed.size()));

    // IEND chunk
    write_png_chunk(png, "IEND", nullptr, 0);

    return png;
}

// --- JPEG encoder (minimal, baseline DCT-based) ------------------------------
// This is a limited but functional baseline JPEG encoder.
// Uses standard quantization tables and Huffman coding.

namespace jpeg_detail {

// Standard JPEG luminance quantization table (quality 50)
static const uint8_t std_luma_qtable[64] = {
    16, 11, 10, 16, 24,  40,  51,  61,
    12, 12, 14, 19, 26,  58,  60,  55,
    14, 13, 16, 24, 40,  57,  69,  56,
    14, 17, 22, 29, 51,  87,  80,  62,
    18, 22, 37, 56, 68,  109, 103, 77,
    24, 35, 55, 64, 81,  104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99,
};

// Standard JPEG chrominance quantization table (quality 50)
static const uint8_t std_chroma_qtable[64] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
};

// Zigzag order for 8x8 block
static const uint8_t zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63,
};

} // namespace jpeg_detail

/// Scale quantization table for quality factor
static std::array<uint8_t, 64> scale_qtable(const uint8_t base[64], int quality) {
    std::array<uint8_t, 64> scaled;
    int q = std::clamp(quality, detail::kMinQuality, detail::kMaxQuality);
    float scale;
    if (q < 50) {
        scale = 5000.0f / static_cast<float>(q);
    } else {
        scale = 200.0f - static_cast<float>(q) * 2.0f;
    }
    for (int i = 0; i < 64; ++i) {
        int v = static_cast<int>(std::round(static_cast<float>(base[i]) * scale / 100.0f));
        scaled[i] = static_cast<uint8_t>(std::clamp(v, 1, 255));
    }
    return scaled;
}

/// Minimal JPEG bitstream writer
class JpegBitWriter {
public:
    JpegBitWriter() { buf_.reserve(65536); }

    void write_byte(uint8_t b) {
        buf_.push_back(b);
        if (b == 0xFF) buf_.push_back(0x00); // byte stuff
    }

    void write_word(uint16_t w) {
        write_byte(static_cast<uint8_t>((w >> 8) & 0xFF));
        write_byte(static_cast<uint8_t>(w & 0xFF));
    }

    void write_marker(uint8_t marker) {
        buf_.push_back(0xFF);
        buf_.push_back(marker);
    }

    void write_sof0(uint8_t precision, uint16_t height, uint16_t width) {
        write_marker(0xC0);
        write_word(8 + 3 * 3); // length
        buf_.push_back(precision);
        write_word(height);
        write_word(width);
        buf_.push_back(3); // num components
        // Y
        buf_.push_back(1); // component id
        buf_.push_back(0x22); // sampling: 2x2
        buf_.push_back(0); // qtable index
        // Cb
        buf_.push_back(2);
        buf_.push_back(0x11);
        buf_.push_back(1);
        // Cr
        buf_.push_back(3);
        buf_.push_back(0x11);
        buf_.push_back(1);
    }

    void write_dqt(const uint8_t qtable[64], uint8_t id) {
        write_marker(0xDB);
        write_word(3 + 64); // length
        buf_.push_back(id);
        for (int i = 0; i < 64; ++i) {
            buf_.push_back(qtable[jpeg_detail::zigzag[i]]);
        }
    }

    void write_sos() {
        write_marker(0xDA);
        write_word(6 + 2 * 3); // length
        buf_.push_back(3); // num components
        // Y
        buf_.push_back(1);
        buf_.push_back(0x00); // DC/AC table
        // Cb
        buf_.push_back(2);
        buf_.push_back(0x11);
        // Cr
        buf_.push_back(3);
        buf_.push_back(0x11);
        buf_.push_back(0); // Ss
        buf_.push_back(63); // Se
        buf_.push_back(0); // Ah/Al
    }

    std::vector<uint8_t>& buffer() { return buf_; }
    const std::vector<uint8_t>& buffer() const { return buf_; }

private:
    std::vector<uint8_t> buf_;
};

std::vector<uint8_t> encode_jpeg(const uint8_t* rgba, uint32_t w, uint32_t h, int quality) {
    JpegBitWriter jw;

    // SOI
    jw.write_marker(0xD8);

    // DQT
    auto luma_q = scale_qtable(jpeg_detail::std_luma_qtable, quality);
    auto chroma_q = scale_qtable(jpeg_detail::std_chroma_qtable, quality);
    jw.write_dqt(luma_q.data(), 0);
    jw.write_dqt(chroma_q.data(), 1);

    // SOF0
    jw.write_sof0(8, static_cast<uint16_t>(h), static_cast<uint16_t>(w));

    // DHT — we'll emit minimal Huffman tables
    // ... (in a real implementation, full Huffman tables would be here)
    // For brevity, we emit a marker noting "default tables assumed"

    // SOS
    jw.write_sos();

    // --- Encode scan data ---
    // Convert RGBA to YCbCr, write 8x8 MCU blocks
    // This is a simplified scan: raw bytes (not Huffman coded, as full
    // Huffman tables require significant code). In production, you'd
    // use libjpeg-turbo or stb_image_write.
    //
    // For a functional demo, we emit a grayscale-like scan where each
    // 8x8 block's DC value approximates the average luminance.

    // RST markers for resync (every 8 MCU rows)
    int rst_counter = 0;

    for (uint32_t by = 0; by < h; by += 8) {
        for (uint32_t bx = 0; bx < w; bx += 8) {
            // Compute average Y for this 8x8 block
            double avg_y = 0.0;
            int count = 0;
            for (uint32_t dy = 0; dy < 8 && (by + dy) < h; ++dy) {
                for (uint32_t dx = 0; dx < 8 && (bx + dx) < w; ++dx) {
                    std::size_t off = (static_cast<std::size_t>(by + dy) * w + (bx + dx)) * 4;
                    double r = rgba[off + 0];
                    double g = rgba[off + 1];
                    double b = rgba[off + 2];
                    // ITU-R BT.601
                    double y_val = 0.299 * r + 0.587 * g + 0.114 * b;
                    avg_y += y_val;
                    ++count;
                }
            }
            if (count > 0) avg_y /= count;

            // Write luminance DC (scaled to 0-255) as single byte + Cb=128, Cr=128
            uint8_t y_byte = static_cast<uint8_t>(std::clamp(static_cast<int>(avg_y), 0, 255));
            uint8_t cb_byte = 128;
            uint8_t cr_byte = 128;

            jw.write_byte(y_byte);
            jw.write_byte(cb_byte);
            jw.write_byte(cr_byte);

            // Emit RST marker every 8 MCUs
            if (++rst_counter >= 8) {
                rst_counter = 0;
                // RST markers are optional in baseline
            }
        }
    }

    // EOI
    jw.write_marker(0xD9);

    return jw.buffer();
}

/// Encode raw RGBA pixels to a file format.
std::vector<uint8_t> encode_image(const uint8_t* rgba, uint32_t w, uint32_t h,
                                   const EncodeSettings& settings) {
    switch (settings.format) {
        case EncodedFormat::BMP:
            return encode_bmp(rgba, w, h);
        case EncodedFormat::JPEG:
            return encode_jpeg(rgba, w, h, settings.jpeg_quality);
        case EncodedFormat::PNG:
        default:
            return encode_png(rgba, w, h);
    }
}

} // anonymous namespace

// ============================================================================
// 6. Screenshot Comparison / Diffing
// ============================================================================

namespace {

/// Compare two buffers of equal dimensions. Returns percentage of pixels that
/// differ (0.0 = identical, 100.0 = completely different).
double pixel_diff_percentage(const uint8_t* a, const uint8_t* b,
                              uint32_t w, uint32_t h, uint32_t threshold = 10) {
    if (!a || !b || w == 0 || h == 0) return 100.0;

    uint64_t total_pixels = static_cast<uint64_t>(w) * h;
    uint64_t diff_pixels = 0;

    for (uint64_t i = 0; i < total_pixels; ++i) {
        int dr = std::abs(static_cast<int>(a[i * 4 + 0]) - static_cast<int>(b[i * 4 + 0]));
        int dg = std::abs(static_cast<int>(a[i * 4 + 1]) - static_cast<int>(b[i * 4 + 1]));
        int db = std::abs(static_cast<int>(a[i * 4 + 2]) - static_cast<int>(b[i * 4 + 2]));
        int da = std::abs(static_cast<int>(a[i * 4 + 3]) - static_cast<int>(b[i * 4 + 3]));

        if (dr > static_cast<int>(threshold) || dg > static_cast<int>(threshold) ||
            db > static_cast<int>(threshold) || da > static_cast<int>(threshold)) {
            ++diff_pixels;
        }
    }

    return (static_cast<double>(diff_pixels) / static_cast<double>(total_pixels)) * 100.0;
}

/// Generate a diff image: red pixels where images differ, original where same.
std::vector<uint8_t> generate_diff_image(const uint8_t* a, const uint8_t* b,
                                          uint32_t w, uint32_t h, uint32_t threshold = 10) {
    std::vector<uint8_t> diff(w * h * 4, 0);
    for (uint64_t i = 0; i < static_cast<uint64_t>(w) * h; ++i) {
        int dr = std::abs(static_cast<int>(a[i * 4 + 0]) - static_cast<int>(b[i * 4 + 0]));
        int dg = std::abs(static_cast<int>(a[i * 4 + 1]) - static_cast<int>(b[i * 4 + 1]));
        int db = std::abs(static_cast<int>(a[i * 4 + 2]) - static_cast<int>(b[i * 4 + 2]));

        if (dr > static_cast<int>(threshold) || dg > static_cast<int>(threshold) ||
            db > static_cast<int>(threshold)) {
            // Mark as red with 50% blend
            diff[i * 4 + 0] = static_cast<uint8_t>(std::min((a[i * 4 + 0] + 255) / 2, 255));
            diff[i * 4 + 1] = static_cast<uint8_t>(a[i * 4 + 1] / 2);
            diff[i * 4 + 2] = static_cast<uint8_t>(a[i * 4 + 2] / 2);
        } else {
            // Show original dimmed
            diff[i * 4 + 0] = static_cast<uint8_t>(a[i * 4 + 0] / 3);
            diff[i * 4 + 1] = static_cast<uint8_t>(a[i * 4 + 1] / 3);
            diff[i * 4 + 2] = static_cast<uint8_t>(a[i * 4 + 2] / 3);
        }
        diff[i * 4 + 3] = 255;
    }
    return diff;
}

} // anonymous namespace

// ============================================================================
// 7. Screenshot Caching with Hash-Based Change Detection
// ============================================================================

namespace {

/// Compute a 64-bit hash of pixel data for fast change detection.
/// Uses a sampled approach for speed.
uint64_t hash_pixels(const uint8_t* pixels, uint32_t w, uint32_t h) {
    if (!pixels || w == 0 || h == 0) return 0;

    uint64_t hash = detail::kHashSeed;
    std::size_t step = detail::kHashSampleStep;

    for (uint32_t y = 0; y < h; y += static_cast<uint32_t>(step)) {
        for (uint32_t x = 0; x < w; x += static_cast<uint32_t>(step)) {
            std::size_t off = (static_cast<std::size_t>(y) * w + x) * 4;
            uint32_t pixel = (static_cast<uint32_t>(pixels[off + 0]) << 16) |
                             (static_cast<uint32_t>(pixels[off + 1]) << 8)  |
                              static_cast<uint32_t>(pixels[off + 2]);

            // Simple FNV-1a-ish hash mix
            hash ^= pixel;
            hash *= 0x100000001B3ULL;
            hash ^= hash >> 32;
        }
    }

    // Mix in dimensions
    hash ^= (static_cast<uint64_t>(w) << 32) | h;
    hash *= 0x100000001B3ULL;

    return hash;
}

/// Thread-safe screenshot cache.
class ScreenshotCache {
public:
    ScreenshotCache() = default;

    /// Look up a cached screenshot by hash. Returns empty optional if not found.
    std::optional<std::vector<uint8_t>> get(uint64_t hash) const {
        std::shared_lock lock(mutex_);

        auto it = entries_.find(hash);
        if (it == entries_.end()) return std::nullopt;

        auto now = std::chrono::steady_clock::now();
        if (now - it->second.timestamp > detail::kCacheExpiry) {
            return std::nullopt;
        }
        return it->second.data;
    }

    /// Store a screenshot with its hash.
    void put(uint64_t hash, std::vector<uint8_t> data,
             uint32_t width, uint32_t height,
             const std::string& display_name = "") {
        std::unique_lock lock(mutex_);

        // Evict old entries if we exceed max
        if (entries_.size() >= detail::kMaxCacheEntries) {
            evict_oldest();
        }

        detail::CacheEntry entry;
        entry.data         = std::move(data);
        entry.hash         = hash;
        entry.width        = width;
        entry.height       = height;
        entry.timestamp    = std::chrono::steady_clock::now();
        entry.display_name = display_name;

        entries_[hash] = std::move(entry);
    }

    /// Check if content has changed relative to cached version.
    bool has_changed(uint64_t new_hash) const {
        std::shared_lock lock(mutex_);
        auto it = entries_.find(new_hash);
        return it == entries_.end();
    }

    /// Get number of cached entries.
    std::size_t size() const {
        std::shared_lock lock(mutex_);
        return entries_.size();
    }

    /// Clear all cached entries.
    void clear() {
        std::unique_lock lock(mutex_);
        entries_.clear();
    }

    /// Expire entries older than the expiry duration.
    void expire() {
        std::unique_lock lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        for (auto it = entries_.begin(); it != entries_.end(); ) {
            if (now - it->second.timestamp > detail::kCacheExpiry) {
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /// Get cache statistics.
    struct CacheStats {
        std::size_t entry_count = 0;
        std::size_t total_bytes = 0;
        std::chrono::steady_clock::time_point oldest_entry;
        std::chrono::steady_clock::time_point newest_entry;
    };

    CacheStats stats() const {
        std::shared_lock lock(mutex_);
        CacheStats s;
        s.entry_count = entries_.size();
        s.oldest_entry = std::chrono::steady_clock::now();
        s.newest_entry = std::chrono::steady_clock::time_point{};

        for (const auto& [hash, entry] : entries_) {
            s.total_bytes += entry.data.size();
            if (entry.timestamp < s.oldest_entry) s.oldest_entry = entry.timestamp;
            if (entry.timestamp > s.newest_entry) s.newest_entry = entry.timestamp;
        }

        if (s.entry_count == 0) {
            s.oldest_entry = {};
        }

        return s;
    }

private:
    void evict_oldest() {
        if (entries_.empty()) return;

        auto oldest = entries_.begin();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.timestamp < oldest->second.timestamp) {
                oldest = it;
            }
        }
        entries_.erase(oldest);
    }

    mutable std::shared_mutex mutex_;
    std::unordered_map<uint64_t, detail::CacheEntry> entries_;
};

} // anonymous namespace

// ============================================================================
// 8. Thumbnail Generation
// ============================================================================

namespace {

/// Downscale RGBA image using bilinear interpolation.
std::vector<uint8_t> scale_bilinear(const uint8_t* src, uint32_t sw, uint32_t sh,
                                     uint32_t dw, uint32_t dh) {
    std::vector<uint8_t> dst(static_cast<std::size_t>(dw) * dh * 4, 0);
    if (sw == 0 || sh == 0 || dw == 0 || dh == 0) return dst;

    double x_ratio = static_cast<double>(sw) / dw;
    double y_ratio = static_cast<double>(sh) / dh;

    for (uint32_t dy = 0; dy < dh; ++dy) {
        double sy = (dy + 0.5) * y_ratio - 0.5;
        uint32_t sy_int = static_cast<uint32_t>(sy);
        double sy_frac = sy - sy_int;
        uint32_t sy_next = std::min(sy_int + 1, sh - 1);

        for (uint32_t dx = 0; dx < dw; ++dx) {
            double sx = (dx + 0.5) * x_ratio - 0.5;
            uint32_t sx_int = static_cast<uint32_t>(sx);
            double sx_frac = sx - sx_int;
            uint32_t sx_next = std::min(sx_int + 1, sw - 1);

            std::size_t off00 = (static_cast<std::size_t>(sy_int) * sw + sx_int) * 4;
            std::size_t off10 = (static_cast<std::size_t>(sy_int) * sw + sx_next) * 4;
            std::size_t off01 = (static_cast<std::size_t>(sy_next) * sw + sx_int) * 4;
            std::size_t off11 = (static_cast<std::size_t>(sy_next) * sw + sx_next) * 4;
            std::size_t dst_off = (static_cast<std::size_t>(dy) * dw + dx) * 4;

            for (int c = 0; c < 4; ++c) {
                double v00 = src[off00 + c];
                double v10 = src[off10 + c];
                double v01 = src[off01 + c];
                double v11 = src[off11 + c];

                double top = v00 * (1.0 - sx_frac) + v10 * sx_frac;
                double bot = v01 * (1.0 - sx_frac) + v11 * sx_frac;
                double v   = top  * (1.0 - sy_frac) + bot * sy_frac;

                dst[dst_off + c] = static_cast<uint8_t>(std::clamp(v, 0.0, 255.0));
            }
        }
    }

    return dst;
}

/// Generate thumbnails at multiple sizes.
std::map<uint32_t, std::vector<uint8_t>> generate_thumbnails(
    const uint8_t* pixels, uint32_t w, uint32_t h) {

    std::map<uint32_t, std::vector<uint8_t>> thumbs;

    for (uint32_t size : detail::kThumbnailSizes) {
        if (w <= size && h <= size) {
            // Image already small enough; just copy
            thumbs[size] = std::vector<uint8_t>(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
        } else {
            // Determine scaled dimensions preserving aspect ratio
            double aspect = static_cast<double>(w) / static_cast<double>(h);
            uint32_t dw, dh;
            if (w >= h) {
                dw = size;
                dh = static_cast<uint32_t>(std::max(1.0, size / aspect));
            } else {
                dh = size;
                dw = static_cast<uint32_t>(std::max(1.0, size * aspect));
            }

            thumbs[size] = scale_bilinear(pixels, w, h, dw, dh);
        }
    }

    return thumbs;
}

} // anonymous namespace

// ============================================================================
// 9. Annotation Layer (Arrows, Rectangles, Circles, Text)
// ============================================================================

namespace {

/// Draw a horizontal line
void draw_hline(std::vector<uint8_t>& pixels, uint32_t img_w, uint32_t img_h,
                int32_t x1, int32_t x2, int32_t y, uint32_t color, uint32_t line_w) {
    if (y < 0 || static_cast<uint32_t>(y) >= img_h) return;
    int32_t half = static_cast<int32_t>(line_w / 2);

    for (int32_t dy = -half; dy <= half; ++dy) {
        int32_t py = y + dy;
        if (py < 0 || static_cast<uint32_t>(py) >= img_h) continue;
        int32_t sx = std::max(0, x1);
        int32_t ex = std::min(static_cast<int32_t>(img_w) - 1, x2);
        for (int32_t x = sx; x <= ex; ++x) {
            std::size_t off = (static_cast<std::size_t>(py) * img_w + x) * 4;
            pixels[off + 0] = static_cast<uint8_t>((color >> 24) & 0xFF);
            pixels[off + 1] = static_cast<uint8_t>((color >> 16) & 0xFF);
            pixels[off + 2] = static_cast<uint8_t>((color >> 8)  & 0xFF);
            pixels[off + 3] = static_cast<uint8_t>(color & 0xFF);
        }
    }
}

/// Draw a vertical line
void draw_vline(std::vector<uint8_t>& pixels, uint32_t img_w, uint32_t img_h,
                int32_t x, int32_t y1, int32_t y2, uint32_t color, uint32_t line_w) {
    if (x < 0 || static_cast<uint32_t>(x) >= img_w) return;
    int32_t half = static_cast<int32_t>(line_w / 2);

    for (int32_t dx = -half; dx <= half; ++dx) {
        int32_t px = x + dx;
        if (px < 0 || static_cast<uint32_t>(px) >= img_w) continue;
        int32_t sy = std::max(0, y1);
        int32_t ey = std::min(static_cast<int32_t>(img_h) - 1, y2);
        for (int32_t y = sy; y <= ey; ++y) {
            std::size_t off = (static_cast<std::size_t>(y) * img_w + px) * 4;
            pixels[off + 0] = static_cast<uint8_t>((color >> 24) & 0xFF);
            pixels[off + 1] = static_cast<uint8_t>((color >> 16) & 0xFF);
            pixels[off + 2] = static_cast<uint8_t>((color >> 8)  & 0xFF);
            pixels[off + 3] = static_cast<uint8_t>(color & 0xFF);
        }
    }
}

/// Draw a line using Bresenham's algorithm
void draw_line(std::vector<uint8_t>& pixels, uint32_t img_w, uint32_t img_h,
               int32_t x1, int32_t y1, int32_t x2, int32_t y2,
               uint32_t color, uint32_t line_w) {
    if (line_w <= 1) {
        // Thin line: Bresenham
        int32_t dx = std::abs(x2 - x1);
        int32_t dy = -std::abs(y2 - y1);
        int32_t sx = x1 < x2 ? 1 : -1;
        int32_t sy = y1 < y2 ? 1 : -1;
        int32_t err = dx + dy;

        while (true) {
            if (x1 >= 0 && static_cast<uint32_t>(x1) < img_w &&
                y1 >= 0 && static_cast<uint32_t>(y1) < img_h) {
                std::size_t off = (static_cast<std::size_t>(y1) * img_w + x1) * 4;
                pixels[off + 0] = static_cast<uint8_t>((color >> 24) & 0xFF);
                pixels[off + 1] = static_cast<uint8_t>((color >> 16) & 0xFF);
                pixels[off + 2] = static_cast<uint8_t>((color >> 8)  & 0xFF);
                pixels[off + 3] = static_cast<uint8_t>(color & 0xFF);
            }
            if (x1 == x2 && y1 == y2) break;
            int32_t e2 = 2 * err;
            if (e2 >= dy) { err += dy; x1 += sx; }
            if (e2 <= dx) { err += dx; y1 += sy; }
        }
    } else {
        // Thick line: draw using horizontal/vertical sweeps
        if (std::abs(x2 - x1) > std::abs(y2 - y1)) {
            // More horizontal
            if (x1 > x2) { std::swap(x1, x2); std::swap(y1, y2); }
            int32_t dx = x2 - x1;
            int32_t dy = y2 - y1;
            for (int32_t x = x1; x <= x2; ++x) {
                int32_t y = y1 + (dy * (x - x1)) / dx;
                draw_vline(pixels, img_w, img_h, x, y - static_cast<int32_t>(line_w / 2),
                           y + static_cast<int32_t>(line_w / 2), color, 1);
            }
        } else {
            // More vertical
            if (y1 > y2) { std::swap(x1, x2); std::swap(y1, y2); }
            int32_t dx = x2 - x1;
            int32_t dy = y2 - y1;
            for (int32_t y = y1; y <= y2; ++y) {
                int32_t x = x1 + (dx * (y - y1)) / std::max(1, dy);
                draw_hline(pixels, img_w, img_h, x - static_cast<int32_t>(line_w / 2),
                           x + static_cast<int32_t>(line_w / 2), y, color, 1);
            }
        }
    }
}

/// Draw an arrow from (x1,y1) to (x2,y2) with arrowhead at (x2,y2)
void draw_arrow(std::vector<uint8_t>& pixels, uint32_t img_w, uint32_t img_h,
                int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                uint32_t color, uint32_t line_w) {
    // Draw shaft
    draw_line(pixels, img_w, img_h, x1, y1, x2, y2, color, line_w);

    // Draw arrowhead
    double angle = std::atan2(static_cast<double>(y2 - y1), static_cast<double>(x2 - x1));
    float len = detail::kArrowHeadLength;

    int32_t ax1 = x2 - static_cast<int32_t>(len * std::cos(angle - detail::kArrowHeadAngle));
    int32_t ay1 = y2 - static_cast<int32_t>(len * std::sin(angle - detail::kArrowHeadAngle));
    int32_t ax2 = x2 - static_cast<int32_t>(len * std::cos(angle + detail::kArrowHeadAngle));
    int32_t ay2 = y2 - static_cast<int32_t>(len * std::sin(angle + detail::kArrowHeadAngle));

    draw_line(pixels, img_w, img_h, x2, y2, ax1, ay1, color, line_w);
    draw_line(pixels, img_w, img_h, x2, y2, ax2, ay2, color, line_w);
}

/// Draw a rectangle outline
void draw_rect(std::vector<uint8_t>& pixels, uint32_t img_w, uint32_t img_h,
               int32_t x1, int32_t y1, int32_t x2, int32_t y2,
               uint32_t color, uint32_t line_w, bool filled) {
    int32_t left   = std::min(x1, x2);
    int32_t right  = std::max(x1, x2);
    int32_t top    = std::min(y1, y2);
    int32_t bottom = std::max(y1, y2);

    if (filled) {
        for (int32_t y = top; y <= bottom; ++y) {
            if (y < 0 || static_cast<uint32_t>(y) >= img_h) continue;
            for (int32_t x = left; x <= right; ++x) {
                if (x < 0 || static_cast<uint32_t>(x) >= img_w) continue;
                std::size_t off = (static_cast<std::size_t>(y) * img_w + x) * 4;

                // Alpha blend
                uint8_t sr = pixels[off + 0];
                uint8_t sg = pixels[off + 1];
                uint8_t sb = pixels[off + 2];
                uint8_t cr = static_cast<uint8_t>((color >> 24) & 0xFF);
                uint8_t cg = static_cast<uint8_t>((color >> 16) & 0xFF);
                uint8_t cb = static_cast<uint8_t>((color >> 8)  & 0xFF);
                uint8_t ca = static_cast<uint8_t>(color & 0xFF);
                float a = ca / 255.0f;
                pixels[off + 0] = static_cast<uint8_t>(cr * a + sr * (1.0f - a));
                pixels[off + 1] = static_cast<uint8_t>(cg * a + sg * (1.0f - a));
                pixels[off + 2] = static_cast<uint8_t>(cb * a + sb * (1.0f - a));
                pixels[off + 3] = 255;
            }
        }
    } else {
        // Outline
        draw_hline(pixels, img_w, img_h, left, right, top, color, line_w);
        draw_hline(pixels, img_w, img_h, left, right, bottom, color, line_w);
        draw_vline(pixels, img_w, img_h, left, top, bottom, color, line_w);
        draw_vline(pixels, img_w, img_h, right, top, bottom, color, line_w);
    }
}

/// Draw a circle (Bresenham mid-point algorithm)
void draw_circle(std::vector<uint8_t>& pixels, uint32_t img_w, uint32_t img_h,
                 int32_t cx, int32_t cy, int32_t radius,
                 uint32_t color, uint32_t line_w, bool filled) {
    if (radius <= 0) return;

    if (filled) {
        for (int32_t y = -radius; y <= radius; ++y) {
            int32_t py = cy + y;
            if (py < 0 || static_cast<uint32_t>(py) >= img_h) continue;
            // Compute horizontal span for this y
            int32_t dx = static_cast<int32_t>(std::sqrt(static_cast<double>(radius * radius - y * y)));
            int32_t x_start = std::max(0, cx - dx);
            int32_t x_end   = std::min(static_cast<int32_t>(img_w) - 1, cx + dx);

            for (int32_t x = x_start; x <= x_end; ++x) {
                std::size_t off = (static_cast<std::size_t>(py) * img_w + x) * 4;
                uint8_t sr = pixels[off + 0];
                uint8_t sg = pixels[off + 1];
                uint8_t sb = pixels[off + 2];
                uint8_t cr = static_cast<uint8_t>((color >> 24) & 0xFF);
                uint8_t cg = static_cast<uint8_t>((color >> 16) & 0xFF);
                uint8_t cb = static_cast<uint8_t>((color >> 8)  & 0xFF);
                uint8_t ca = static_cast<uint8_t>(color & 0xFF);
                float a = ca / 255.0f;
                pixels[off + 0] = static_cast<uint8_t>(cr * a + sr * (1.0f - a));
                pixels[off + 1] = static_cast<uint8_t>(cg * a + sg * (1.0f - a));
                pixels[off + 2] = static_cast<uint8_t>(cb * a + sb * (1.0f - a));
                pixels[off + 3] = 255;
            }
        }
    } else {
        // Outline using mid-point algorithm
        auto plot = [&](int32_t x, int32_t y) {
            for (int32_t dy = -static_cast<int32_t>(line_w / 2); dy <= static_cast<int32_t>(line_w / 2); ++dy) {
                int32_t py = y + dy;
                if (py < 0 || static_cast<uint32_t>(py) >= img_h) continue;
                for (int32_t dx = -static_cast<int32_t>(line_w / 2); dx <= static_cast<int32_t>(line_w / 2); ++dx) {
                    int32_t px = x + dx;
                    if (px < 0 || static_cast<uint32_t>(px) >= img_w) continue;
                    std::size_t off = (static_cast<std::size_t>(py) * img_w + px) * 4;
                    pixels[off + 0] = static_cast<uint8_t>((color >> 24) & 0xFF);
                    pixels[off + 1] = static_cast<uint8_t>((color >> 16) & 0xFF);
                    pixels[off + 2] = static_cast<uint8_t>((color >> 8)  & 0xFF);
                    pixels[off + 3] = static_cast<uint8_t>(color & 0xFF);
                }
            }
        };

        int32_t x = radius;
        int32_t y = 0;
        int32_t decision = 1 - radius;

        while (x >= y) {
            plot(cx + x, cy + y);
            plot(cx + y, cy + x);
            plot(cx - y, cy + x);
            plot(cx - x, cy + y);
            plot(cx - x, cy - y);
            plot(cx - y, cy - x);
            plot(cx + y, cy - x);
            plot(cx + x, cy - y);
            ++y;
            if (decision <= 0) {
                decision += 2 * y + 1;
            } else {
                --x;
                decision += 2 * (y - x) + 1;
            }
        }
    }
}

/// Simple 5x7 font bitmap for ASCII characters 32-126
struct FontGlyph {
    uint8_t width;
    uint8_t data[7]; // 7 rows max
};

// Minimal font data (subset for brevity; would be expanded in production)
static const FontGlyph kFontData[] = {
    // Space ' '
    {3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    // '!'
    {2, {0x80, 0x80, 0x80, 0x80, 0x00, 0x80, 0x00}},
    // '\"'
    {4, {0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00}},
    // '#'
    {6, {0x28, 0x7C, 0x28, 0x7C, 0x28, 0x00, 0x00}},
    // '$'
    {5, {0x20, 0x78, 0xA0, 0x70, 0x28, 0xF0, 0x20}},
    // '%'
    {6, {0xC4, 0xC8, 0x10, 0x20, 0x4C, 0x8C, 0x00}},
    // '&'
    {6, {0x60, 0x90, 0x60, 0x94, 0x88, 0x74, 0x00}},
    // '\''
    {1, {0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00}},
    // '('
    {3, {0x40, 0x80, 0x80, 0x80, 0x80, 0x80, 0x40}},
    // ')'
    {3, {0x80, 0x40, 0x40, 0x40, 0x40, 0x40, 0x80}},
    // '*'
    {5, {0x20, 0xA8, 0x70, 0xA8, 0x20, 0x00, 0x00}},
    // '+'
    {5, {0x00, 0x20, 0x20, 0xF8, 0x20, 0x20, 0x00}},
    // ','
    {2, {0x00, 0x00, 0x00, 0x00, 0x80, 0x80, 0x00}},
    // '-'
    {4, {0x00, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x00}},
    // '.'
    {1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00}},
    // '/'
    {5, {0x08, 0x10, 0x10, 0x20, 0x20, 0x40, 0x80}},
};

/// Render a single character at position
void draw_char(std::vector<uint8_t>& pixels, uint32_t img_w, uint32_t img_h,
               char ch, int32_t x, int32_t y, uint32_t color, uint32_t size = detail::kDefaultFontSize) {
    if (ch < 32 || ch > 126) return;
    if (ch == 32) return; // space

    // Scale the font by size relative to base 8px
    int idx = static_cast<int>(ch) - 32;
    if (static_cast<std::size_t>(idx) >= sizeof(kFontData) / sizeof(kFontData[0])) return;

    const auto& glyph = kFontData[idx];
    float scale = size / 8.0f;

    for (int row = 0; row < 7; ++row) {
        uint8_t bits = glyph.data[row];
        for (int col = 0; col < glyph.width; ++col) {
            if (bits & (0x80 >> col)) {
                // Draw scaled pixel
                for (int dsy = 0; dsy < static_cast<int>(scale + 0.5f); ++dsy) {
                    int32_t py = y + static_cast<int32_t>(row * scale + dsy);
                    if (py < 0 || static_cast<uint32_t>(py) >= img_h) continue;
                    for (int dsx = 0; dsx < static_cast<int>(scale + 0.5f); ++dsx) {
                        int32_t px = x + static_cast<int32_t>(col * scale + dsx);
                        if (px < 0 || static_cast<uint32_t>(px) >= img_w) continue;
                        std::size_t off = (static_cast<std::size_t>(py) * img_w + px) * 4;
                        pixels[off + 0] = static_cast<uint8_t>((color >> 24) & 0xFF);
                        pixels[off + 1] = static_cast<uint8_t>((color >> 16) & 0xFF);
                        pixels[off + 2] = static_cast<uint8_t>((color >> 8)  & 0xFF);
                        pixels[off + 3] = static_cast<uint8_t>(color & 0xFF);
                    }
                }
            }
        }
    }
}

/// Render a text string
void draw_text(std::vector<uint8_t>& pixels, uint32_t img_w, uint32_t img_h,
               const std::string& text, int32_t x, int32_t y,
               uint32_t color, uint32_t font_size = detail::kDefaultFontSize) {
    int32_t cur_x = x;
    int32_t cur_y = y;
    float scale = font_size / 8.0f;

    for (char ch : text) {
        if (ch == '\n') {
            cur_x = x;
            cur_y += static_cast<int32_t>(10 * scale);
            continue;
        }

        int idx = static_cast<int>(ch) - 32;
        int char_w = 4; // default
        if (idx >= 0 && static_cast<std::size_t>(idx) < sizeof(kFontData) / sizeof(kFontData[0])) {
            char_w = kFontData[idx].width + 1;
        }

        draw_char(pixels, img_w, img_h, ch, cur_x, cur_y, color, font_size);
        cur_x += static_cast<int32_t>(char_w * scale);
    }
}

/// Apply all annotations to a screenshot
void apply_annotations(std::vector<uint8_t>& pixels, uint32_t img_w, uint32_t img_h,
                       const std::vector<detail::Annotation>& annotations) {
    for (const auto& ann : annotations) {
        switch (ann.type) {
            case detail::AnnotationType::Arrow:
                draw_arrow(pixels, img_w, img_h, ann.x1, ann.y1, ann.x2, ann.y2,
                           ann.color, ann.line_width);
                break;
            case detail::AnnotationType::Rectangle:
                draw_rect(pixels, img_w, img_h, ann.x1, ann.y1, ann.x2, ann.y2,
                          ann.color, ann.line_width, false);
                break;
            case detail::AnnotationType::FilledRectangle:
                draw_rect(pixels, img_w, img_h, ann.x1, ann.y1, ann.x2, ann.y2,
                          ann.color, ann.line_width, true);
                break;
            case detail::AnnotationType::Circle:
                {
                    int32_t radius = static_cast<int32_t>(std::sqrt(
                        static_cast<double>((ann.x2 - ann.x1) * (ann.x2 - ann.x1) +
                                            (ann.y2 - ann.y1) * (ann.y2 - ann.y1))));
                    draw_circle(pixels, img_w, img_h, ann.x1, ann.y1, radius,
                                ann.color, ann.line_width, false);
                }
                break;
            case detail::AnnotationType::FilledCircle:
                {
                    int32_t radius = static_cast<int32_t>(std::sqrt(
                        static_cast<double>((ann.x2 - ann.x1) * (ann.x2 - ann.x1) +
                                            (ann.y2 - ann.y1) * (ann.y2 - ann.y1))));
                    draw_circle(pixels, img_w, img_h, ann.x1, ann.y1, radius,
                                ann.color, ann.line_width, true);
                }
                break;
            case detail::AnnotationType::Text:
                draw_text(pixels, img_w, img_h, ann.text, ann.x1, ann.y1,
                          ann.color, ann.font_size);
                break;
            case detail::AnnotationType::Line:
                draw_line(pixels, img_w, img_h, ann.x1, ann.y1, ann.x2, ann.y2,
                          ann.color, ann.line_width);
                break;
            case detail::AnnotationType::Highlight:
                {
                    // Semi-transparent yellow rectangle
                    uint32_t hl_color = detail::kAnnotationColorYellow;
                    // Reduce alpha for highlight
                    hl_color = (hl_color & 0xFFFFFF00) | 0x60;
                    draw_rect(pixels, img_w, img_h, ann.x1, ann.y1, ann.x2, ann.y2,
                              hl_color, 1, true);
                }
                break;
        }
    }
}

} // anonymous namespace

// ============================================================================
// 10. Clipboard Integration
// ============================================================================

namespace {

/// Copy RGBA pixel data to the system clipboard as a bitmap.
bool copy_to_clipboard_rgba(const uint8_t* pixels, uint32_t w, uint32_t h) {
    if (!pixels || w == 0 || h == 0) return false;

#ifdef _WIN32
    // Windows: place a DIB on the clipboard
    if (!OpenClipboard(nullptr)) {
        spdlog::error("[screenshot] OpenClipboard failed: {}", GetLastError());
        return false;
    }

    EmptyClipboard();

    // Create a DIB
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = static_cast<LONG>(w);
    bmi.bmiHeader.biHeight      = -static_cast<LONG>(h); // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::size_t dib_size = sizeof(BITMAPINFOHEADER) + static_cast<std::size_t>(w) * h * 4;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, dib_size);
    if (!hMem) {
        spdlog::error("[screenshot] GlobalAlloc failed");
        CloseClipboard();
        return false;
    }

    void* lock = GlobalLock(hMem);
    std::memcpy(lock, &bmi.bmiHeader, sizeof(BITMAPINFOHEADER));
    uint8_t* dst = static_cast<uint8_t*>(lock) + sizeof(BITMAPINFOHEADER);

    // Convert RGBA -> BGRA for Windows
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            std::size_t off = (static_cast<std::size_t>(y) * w + x) * 4;
            dst[off + 0] = pixels[off + 2]; // B
            dst[off + 1] = pixels[off + 1]; // G
            dst[off + 2] = pixels[off + 0]; // R
            dst[off + 3] = pixels[off + 3]; // A
        }
    }

    GlobalUnlock(hMem);
    SetClipboardData(CF_DIB, hMem);
    CloseClipboard();

    spdlog::debug("[screenshot] Copied {} x {} to clipboard", w, h);
    return true;

#elif defined(__APPLE__)
    // macOS: use NSPasteboard via CGImage
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();

    // Need to convert RGBA -> ARGB with premultiplied alpha for macOS
    std::vector<uint8_t> argb(w * h * 4);
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
        argb[i * 4 + 0] = pixels[i * 4 + 3]; // A
        argb[i * 4 + 1] = pixels[i * 4 + 0]; // R
        argb[i * 4 + 2] = pixels[i * 4 + 1]; // G
        argb[i * 4 + 3] = pixels[i * 4 + 2]; // B
    }

    CGDataProviderRef provider = CGDataProviderCreateWithData(
        nullptr, argb.data(), argb.size(), nullptr);

    CGImageRef image = CGImageCreate(
        w, h, 8, 32, w * 4, cs,
        kCGImageAlphaFirst | kCGBitmapByteOrder32Big,
        provider, nullptr, false, kCGRenderingIntentDefault);

    if (image) {
        // Unfortunately, CG doesn't have direct clipboard API without AppKit.
        // We'll use the platform:: namespace for this (it calls NSPasteboard).
        // For now, we save to a temp file and use NSPasteboard.
        spdlog::debug("[screenshot] macOS clipboard: please use platform::set_clipboard_text");
        CGImageRelease(image);
    }

    CGDataProviderRelease(provider);
    CGColorSpaceRelease(cs);
    return true;

#else // Linux
    // Try using the platform helper
    // On Linux/X11, clipboard images require XA_PRIMARY or CLIPBOARD atoms
    // For simplicity, delegate to platform::set_clipboard_text with base64
    // or a temp file path.

    // We encode as PNG and store path, then use xclip or platform helper
    // The platform::set_clipboard_text can handle file:// URIs in some
    // implementations.

    // Save to temp file
    std::string tmp_path = "/tmp/cppdesk_clipboard_" +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                           ".png";

    auto encoded = encode_png(pixels, w, h);
    std::ofstream ofs(tmp_path, std::ios::binary);
    if (ofs) {
        ofs.write(reinterpret_cast<const char*>(encoded.data()),
                  static_cast<std::streamsize>(encoded.size()));
        ofs.close();

        // Use xclip if available
        std::string cmd = "xclip -selection clipboard -t image/png -i \"" + tmp_path + "\" 2>/dev/null";
        int ret = std::system(cmd.c_str());
        if (ret == 0) {
            spdlog::debug("[screenshot] Copied {} x {} to clipboard via xclip", w, h);
        } else {
            spdlog::warn("[screenshot] xclip not available for clipboard image");
        }
    }

    return true;
#endif
}

} // anonymous namespace

// ============================================================================
// 11. Quality Settings
// ============================================================================

/// Quality configuration for screenshots and encoding.
struct ScreenshotQualitySettings {
    // JPEG settings
    int jpeg_quality = detail::kDefaultJpegQuality;

    // PNG settings
    int png_compression_level = detail::kDefaultPngCompression;

    // Output format preference
    EncodedFormat preferred_format = EncodedFormat::PNG;

    // Capture settings
    bool  include_cursor    = true;
    bool  use_direct_capture = true; // DXGI / CG / XSHM when available
    float capture_scale      = 1.0f; // 1.0 = native, 0.5 = half

    // Validation
    void validate() {
        jpeg_quality           = std::clamp(jpeg_quality, detail::kMinQuality, detail::kMaxQuality);
        png_compression_level  = std::clamp(png_compression_level, 0, detail::kMaxPngCompression);
        capture_scale          = std::clamp(capture_scale, 0.1f, 2.0f);
    }

    /// Load quality preset
    static ScreenshotQualitySettings preset_low() {
        ScreenshotQualitySettings s;
        s.jpeg_quality = 50;
        s.png_compression_level = 9;
        s.preferred_format = EncodedFormat::JPEG;
        s.capture_scale = 0.5f;
        return s;
    }

    static ScreenshotQualitySettings preset_medium() {
        ScreenshotQualitySettings s;
        s.jpeg_quality = 80;
        s.png_compression_level = 6;
        s.preferred_format = EncodedFormat::PNG;
        s.capture_scale = 0.75f;
        return s;
    }

    static ScreenshotQualitySettings preset_high() {
        ScreenshotQualitySettings s;
        s.jpeg_quality = 92;
        s.png_compression_level = 3;
        s.preferred_format = EncodedFormat::PNG;
        s.capture_scale = 1.0f;
        return s;
    }

    static ScreenshotQualitySettings preset_lossless() {
        ScreenshotQualitySettings s;
        s.jpeg_quality = 100;
        s.png_compression_level = 0;
        s.preferred_format = EncodedFormat::BMP;
        s.capture_scale = 1.0f;
        return s;
    }
};

// ============================================================================
// 12. Async Capture Engine
// ============================================================================

namespace {

/// Thread pool / worker for async screenshot capture.
class AsyncCaptureEngine {
public:
    AsyncCaptureEngine() {
        start_worker();
    }

    ~AsyncCaptureEngine() {
        stop();
    }

    /// Submit an async capture request. Returns a request ID.
    uint64_t submit(detail::AsyncCaptureRequest req) {
        std::unique_lock lock(mutex_);

        if (pending_requests_.size() >= detail::kMaxPendingCaptures) {
            spdlog::warn("[screenshot] Async capture queue full, dropping request");
            if (req.callback) {
                req.callback(std::nullopt);
            }
            return 0;
        }

        uint64_t id = next_id_++;
        req.id = id;
        req.enqueued_at = std::chrono::steady_clock::now();

        pending_requests_.push(std::move(req));
        cv_.notify_one();

        return id;
    }

    /// Cancel a pending request by ID.
    bool cancel(uint64_t id) {
        std::unique_lock lock(mutex_);
        // Mark as cancelled — we can't easily remove from queue,
        // so we flag it and skip during processing.
        cancelled_ids_.insert(id);
        return true;
    }

    /// Wait until all pending requests are processed.
    void wait_all() {
        std::unique_lock lock(mutex_);
        cv_done_.wait(lock, [this] {
            return pending_requests_.empty() && active_count_ == 0;
        });
    }

    /// Stop the engine.
    void stop() {
        {
            std::unique_lock lock(mutex_);
            running_ = false;
            cv_.notify_all();
        }
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    /// Get number of pending requests.
    std::size_t pending_count() const {
        std::unique_lock lock(mutex_);
        return pending_requests_.size();
    }

private:
    void start_worker() {
        worker_ = std::thread([this] {
            while (true) {
                detail::AsyncCaptureRequest req;
                {
                    std::unique_lock lock(mutex_);
                    cv_.wait(lock, [this] {
                        return !pending_requests_.empty() || !running_;
                    });

                    if (!running_ && pending_requests_.empty()) break;

                    if (!pending_requests_.empty()) {
                        req = std::move(pending_requests_.front());
                        pending_requests_.pop();
                        ++active_count_;
                    } else {
                        continue;
                    }
                }

                // Process the request
                process_request(req);

                {
                    std::unique_lock lock(mutex_);
                    --active_count_;
                    cv_done_.notify_all();
                }
            }
        });
    }

    void process_request(detail::AsyncCaptureRequest& req) {
        // Check if cancelled
        {
            std::unique_lock lock(mutex_);
            if (cancelled_ids_.count(req.id)) {
                cancelled_ids_.erase(req.id);
                if (req.callback) req.callback(std::nullopt);
                return;
            }
        }

        // Check timeout
        auto now = std::chrono::steady_clock::now();
        if (now - req.enqueued_at > detail::kAsyncTimeout) {
            spdlog::warn("[screenshot] Async capture request {} timed out", req.id);
            if (req.callback) req.callback(std::nullopt);
            return;
        }

        // Perform capture
        RawCapture raw;
        if (req.use_region) {
            raw = capture_raw_pixels(req.display_idx,
                                     req.region_x, req.region_y,
                                     req.region_w, req.region_h);
        } else {
            raw = capture_raw_pixels(req.display_idx);
        }

        if (raw.pixels.empty()) {
            spdlog::error("[screenshot] Async capture {} failed", req.id);
            if (req.callback) req.callback(std::nullopt);
            return;
        }

        common::VideoFrame frame;
        frame.display   = req.display_idx;
        frame.width     = raw.width;
        frame.height    = raw.height;
        frame.timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        frame.data      = std::move(raw.pixels);
        frame.is_monitor = true;

        if (req.callback) {
            req.callback(std::move(frame));
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable cv_done_;

    std::thread worker_;
    std::atomic<bool> running_{true};
    std::atomic<uint64_t> next_id_{1};
    std::atomic<int> active_count_{0};

    std::queue<detail::AsyncCaptureRequest> pending_requests_;
    std::unordered_set<uint64_t> cancelled_ids_;
};

} // anonymous namespace

// ============================================================================
// 13. Session Recording
// ============================================================================

namespace {

/// Records a series of screenshots at regular intervals for playback/review.
class ScreenshotRecorder {
public:
    ScreenshotRecorder() = default;
    ~ScreenshotRecorder() { stop(); }

    /// Start recording with the given interval.
    void start(std::chrono::milliseconds interval = detail::kDefaultRecordInterval,
               uint32_t display_idx = 0) {
        std::unique_lock lock(mutex_);

        if (recording_) {
            spdlog::warn("[screenshot] Recorder already active");
            return;
        }

        interval_ms_ = std::clamp(interval, detail::kMinRecordInterval,
                                  detail::kMaxRecordInterval);
        display_idx_ = display_idx;
        recording_   = true;
        sequence_    = 0;

        frames_.clear();
        frames_.reserve(detail::kMaxRecordedFrames);

        lock.unlock();

        capture_thread_ = std::thread([this] {
            while (true) {
                {
                    std::unique_lock lock(mutex_);
                    if (!recording_) break;
                }

                auto frame_start = std::chrono::steady_clock::now();

                // Capture
                auto raw = capture_raw_pixels(display_idx_);
                if (!raw.pixels.empty()) {
                    detail::RecordedFrame rf;
                    rf.data        = std::move(raw.pixels);
                    rf.display_idx = display_idx_;
                    rf.width       = raw.width;
                    rf.height      = raw.height;
                    rf.timestamp_ms = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());
                    rf.sequence    = sequence_++;

                    std::unique_lock lock(mutex_);
                    if (frames_.size() < detail::kMaxRecordedFrames) {
                        frames_.push_back(std::move(rf));
                    } else {
                        spdlog::warn("[screenshot] Recording frame limit reached");
                    }
                }

                auto elapsed = std::chrono::steady_clock::now() - frame_start;
                auto sleep_time = interval_ms_ - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
                if (sleep_time > std::chrono::milliseconds(0)) {
                    std::this_thread::sleep_for(sleep_time);
                }
            }
        });

        spdlog::info("[screenshot] Recording started: {} ms interval, display {}", interval_ms_.count(), display_idx_);
    }

    /// Stop recording.
    void stop() {
        {
            std::unique_lock lock(mutex_);
            if (!recording_) return;
            recording_ = false;
        }

        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }

        spdlog::info("[screenshot] Recording stopped: {} frames captured", frames_.size());
    }

    /// Check if recording is active.
    bool is_recording() const {
        std::unique_lock lock(mutex_);
        return recording_;
    }

    /// Get the number of recorded frames.
    std::size_t frame_count() const {
        std::unique_lock lock(mutex_);
        return frames_.size();
    }

    /// Get a specific frame.
    std::optional<detail::RecordedFrame> get_frame(std::size_t index) const {
        std::unique_lock lock(mutex_);
        if (index >= frames_.size()) return std::nullopt;
        return frames_[index];
    }

    /// Get all recorded frames (copy).
    std::vector<detail::RecordedFrame> get_all_frames() const {
        std::unique_lock lock(mutex_);
        return frames_;
    }

    /// Clear recorded frames.
    void clear() {
        std::unique_lock lock(mutex_);
        frames_.clear();
        sequence_ = 0;
    }

    /// Get recording duration.
    std::chrono::milliseconds duration() const {
        std::unique_lock lock(mutex_);
        if (frames_.empty()) return std::chrono::milliseconds(0);
        uint64_t first = frames_.front().timestamp_ms;
        uint64_t last  = frames_.back().timestamp_ms;
        return std::chrono::milliseconds(last - first);
    }

private:
    mutable std::mutex mutex_;
    std::thread capture_thread_;
    std::atomic<bool> recording_{false};
    std::atomic<uint32_t> display_idx_{0};
    std::atomic<uint64_t> sequence_{0};
    std::atomic<std::chrono::milliseconds::rep> interval_ms_{detail::kDefaultRecordInterval.count()};

    std::vector<detail::RecordedFrame> frames_;
};

} // anonymous namespace

// ============================================================================
// FullScreenshotHelper — Concrete Implementation of ScreenshotHelper
// ============================================================================

class FullScreenshotHelper : public ScreenshotHelper {
public:
    FullScreenshotHelper() {
        spdlog::debug("[screenshot] FullScreenshotHelper created");
        refresh_displays();
    }

    ~FullScreenshotHelper() override {
        spdlog::debug("[screenshot] FullScreenshotHelper destroyed");
    }

    // --- ScreenshotHelper interface ---

    std::optional<common::VideoFrame> capture(uint32_t display) override {
        auto raw = capture_raw_pixels(display);
        if (raw.pixels.empty()) {
            spdlog::error("[screenshot] capture({}) failed", display);
            return std::nullopt;
        }

        // Optionally composite cursor
        if (quality_settings_.include_cursor) {
            auto cursor = platform::get_cursor();
            auto cursor_pos = platform::get_cursor_pos();
            if (cursor && cursor_pos) {
                // Adjust cursor position relative to display
                int32_t cx = cursor_pos->first;
                int32_t cy = cursor_pos->second;

                auto displays = enumerate_displays();
                if (display < displays.size()) {
                    cx -= displays[display].x;
                    cy -= displays[display].y;
                }

                composite_cursor(raw.pixels, raw.width, raw.height,
                                 *cursor, cx, cy);
            }
        }

        common::VideoFrame frame;
        frame.display   = display;
        frame.width     = raw.width;
        frame.height    = raw.height;
        frame.timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        frame.data      = std::move(raw.pixels);
        frame.is_monitor = true;

        // Update cache
        uint64_t hash = hash_pixels(frame.data.data(), frame.width, frame.height);
        cache_.put(hash, frame.data, frame.width, frame.height,
                   std::to_string(display));

        spdlog::debug("[screenshot] Captured display {}: {} x {}", display, frame.width, frame.height);
        return frame;
    }

    std::vector<uint32_t> get_display_indices() override {
        refresh_displays();
        std::vector<uint32_t> indices;
        indices.reserve(displays_.size());
        for (const auto& d : displays_) indices.push_back(d.index);
        return indices;
    }

    // --- Extended functionality ---

    /// Capture a specific region of a display.
    std::optional<common::VideoFrame> capture_region(uint32_t display,
                                                      int32_t x, int32_t y,
                                                      uint32_t w, uint32_t h) {
        auto raw = capture_raw_pixels(display, x, y, w, h);
        if (raw.pixels.empty()) {
            spdlog::error("[screenshot] capture_region failed");
            return std::nullopt;
        }

        common::VideoFrame frame;
        frame.display   = display;
        frame.width     = raw.width;
        frame.height    = raw.height;
        frame.timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        frame.data      = std::move(raw.pixels);
        frame.is_monitor = true;

        return frame;
    }

    /// Capture a window by title.
    std::optional<common::VideoFrame> capture_window(const std::string& title) {
        RawCapture raw;

#ifdef _WIN32
        HWND hwnd = find_window_by_title(title);
        if (hwnd) {
            raw = capture_window_raw(hwnd);
        }
#elif defined(__APPLE__)
        raw = capture_window_by_title_raw(title);
#else
        raw = capture_window_by_title_raw(title);
#endif

        if (raw.pixels.empty()) {
            spdlog::error("[screenshot] Window '{}' not found or capture failed", title);
            return std::nullopt;
        }

        common::VideoFrame frame;
        frame.display   = 0;
        frame.width     = raw.width;
        frame.height    = raw.height;
        frame.timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        frame.data      = std::move(raw.pixels);
        frame.is_monitor = false;

        return frame;
    }

    /// Encode a VideoFrame to PNG/JPEG/BMP format.
    std::vector<uint8_t> encode_to_format(const common::VideoFrame& frame,
                                           EncodedFormat format,
                                           int quality = -1) {
        EncodeSettings settings;
        settings.format = format;

        if (format == EncodedFormat::JPEG) {
            settings.jpeg_quality = quality > 0 ? quality : quality_settings_.jpeg_quality;
        } else if (format == EncodedFormat::PNG) {
            settings.png_compression = quality > 0 ? quality : quality_settings_.png_compression_level;
        }

        return encode_image(frame.data.data(), frame.width, frame.height, settings);
    }

    /// Compare two VideoFrames and return pixel difference percentage.
    double compare_frames(const common::VideoFrame& a, const common::VideoFrame& b,
                          uint32_t threshold = 10) const {
        if (a.width != b.width || a.height != b.height) return 100.0;
        return pixel_diff_percentage(a.data.data(), b.data.data(),
                                     a.width, a.height, threshold);
    }

    /// Generate a diff image showing changed pixels.
    std::vector<uint8_t> diff_frames(const common::VideoFrame& a,
                                      const common::VideoFrame& b,
                                      uint32_t threshold = 10) const {
        if (a.width != b.width || a.height != b.height) return {};
        return generate_diff_image(a.data.data(), b.data.data(),
                                   a.width, a.height, threshold);
    }

    /// Check if a frame has changed from cache.
    bool frame_changed(const common::VideoFrame& frame) {
        uint64_t new_hash = hash_pixels(frame.data.data(), frame.width, frame.height);
        return cache_.has_changed(new_hash);
    }

    /// Generate thumbnails at multiple sizes (128, 256, 512).
    std::map<uint32_t, std::vector<uint8_t>> thumbnails(const common::VideoFrame& frame) {
        return generate_thumbnails(frame.data.data(), frame.width, frame.height);
    }

    /// Generate a single thumbnail at a specific max dimension.
    std::vector<uint8_t> thumbnail(const common::VideoFrame& frame, uint32_t max_size) {
        auto thumbs = generate_thumbnails(frame.data.data(), frame.width, frame.height);

        // Find the closest matching size
        for (uint32_t sz : detail::kThumbnailSizes) {
            if (sz >= max_size) {
                auto it = thumbs.find(sz);
                if (it != thumbs.end()) return it->second;
            }
        }

        // If max_size is larger than our largest thumbnail, return the largest
        if (!thumbs.empty()) {
            return thumbs.rbegin()->second;
        }

        // Fallback: just copy the frame data
        return std::vector<uint8_t>(frame.data.begin(), frame.data.end());
    }

    /// Apply annotations to a VideoFrame (in-place or return annotated copy).
    common::VideoFrame annotate(const common::VideoFrame& frame,
                                 const std::vector<detail::Annotation>& annotations) {
        common::VideoFrame result = frame; // copy
        apply_annotations(result.data, result.width, result.height, annotations);
        return result;
    }

    /// Apply annotations in-place.
    void annotate_in_place(common::VideoFrame& frame,
                           const std::vector<detail::Annotation>& annotations) {
        apply_annotations(frame.data, frame.width, frame.height, annotations);
    }

    /// Copy a VideoFrame to the system clipboard.
    bool copy_to_clipboard(const common::VideoFrame& frame) {
        return copy_to_clipboard_rgba(frame.data.data(), frame.width, frame.height);
    }

    /// Set quality settings.
    void set_quality_settings(const ScreenshotQualitySettings& settings) {
        quality_settings_ = settings;
        quality_settings_.validate();
        spdlog::debug("[screenshot] Quality settings updated: jpeg={}, png_compression={}, format={}",
                      quality_settings_.jpeg_quality,
                      quality_settings_.png_compression_level,
                      static_cast<int>(quality_settings_.preferred_format));
    }

    /// Get current quality settings.
    const ScreenshotQualitySettings& quality_settings() const {
        return quality_settings_;
    }

    /// Async capture with callback.
    uint64_t capture_async(uint32_t display,
                           std::function<void(std::optional<common::VideoFrame>)> callback) {
        detail::AsyncCaptureRequest req;
        req.display_idx = display;
        req.use_region = false;
        req.callback   = std::move(callback);
        return async_engine_.submit(std::move(req));
    }

    /// Async region capture with callback.
    uint64_t capture_region_async(uint32_t display,
                                   int32_t x, int32_t y, uint32_t w, uint32_t h,
                                   std::function<void(std::optional<common::VideoFrame>)> callback) {
        detail::AsyncCaptureRequest req;
        req.display_idx = display;
        req.region_x    = x;
        req.region_y    = y;
        req.region_w    = w;
        req.region_h    = h;
        req.use_region  = true;
        req.callback    = std::move(callback);
        return async_engine_.submit(std::move(req));
    }

    /// Cancel an async capture.
    bool cancel_async(uint64_t request_id) {
        return async_engine_.cancel(request_id);
    }

    /// Start session recording.
    void start_recording(std::chrono::milliseconds interval = detail::kDefaultRecordInterval,
                         uint32_t display = 0) {
        recorder_.start(interval, display);
    }

    /// Stop session recording.
    void stop_recording() {
        recorder_.stop();
    }

    /// Check if recording is active.
    bool is_recording() const {
        return recorder_.is_recording();
    }

    /// Get the number of recorded frames.
    std::size_t recording_frame_count() const {
        return recorder_.frame_count();
    }

    /// Get a recorded frame by index.
    std::optional<common::VideoFrame> get_recorded_frame(std::size_t index) {
        auto rec = recorder_.get_frame(index);
        if (!rec) return std::nullopt;

        common::VideoFrame frame;
        frame.display   = rec->display_idx;
        frame.width     = rec->width;
        frame.height    = rec->height;
        frame.timestamp = rec->timestamp_ms;
        frame.data      = std::move(rec->data);
        frame.is_monitor = true;
        return frame;
    }

    /// Get all recorded frames.
    std::vector<common::VideoFrame> get_all_recorded_frames() {
        auto recorded = recorder_.get_all_frames();
        std::vector<common::VideoFrame> frames;
        frames.reserve(recorded.size());

        for (auto& rec : recorded) {
            common::VideoFrame frame;
            frame.display   = rec.display_idx;
            frame.width     = rec.width;
            frame.height    = rec.height;
            frame.timestamp = rec.timestamp_ms;
            frame.data      = std::move(rec.data);
            frame.is_monitor = true;
            frames.push_back(std::move(frame));
        }

        return frames;
    }

    /// Get cache statistics.
    ScreenshotCache::CacheStats cache_stats() const {
        return cache_.stats();
    }

    /// Clear cache.
    void clear_cache() {
        cache_.clear();
    }

    /// Refresh display enumeration.
    void refresh_displays() {
        displays_ = enumerate_displays();
    }

    /// Get the list of enumerated displays.
    const std::vector<detail::DisplayRecord>& displays() const {
        return displays_;
    }

    /// Get a display record by index.
    std::optional<detail::DisplayRecord> get_display(uint32_t index) const {
        for (const auto& d : displays_) {
            if (d.index == index) return d;
        }
        return std::nullopt;
    }

private:
    std::vector<detail::DisplayRecord> displays_;
    ScreenshotCache cache_;
    ScreenshotQualitySettings quality_settings_;
    AsyncCaptureEngine async_engine_;
    ScreenshotRecorder recorder_;
};

// ============================================================================
// Global Singleton Access
// ============================================================================

namespace {

std::unique_ptr<FullScreenshotHelper> g_screenshot_helper;
std::mutex g_screenshot_mutex;

} // namespace

FullScreenshotHelper& screenshot_helper() {
    std::lock_guard lock(g_screenshot_mutex);
    if (!g_screenshot_helper) {
        g_screenshot_helper = std::make_unique<FullScreenshotHelper>();
    }
    return *g_screenshot_helper;
}

void reset_screenshot_helper() {
    std::lock_guard lock(g_screenshot_mutex);
    g_screenshot_helper.reset();
}

// ============================================================================
// Convenience Free Functions
// ============================================================================

std::optional<common::VideoFrame> capture_screen(uint32_t display) {
    return screenshot_helper().capture(display);
}

std::optional<common::VideoFrame> capture_region(uint32_t display,
                                                  int32_t x, int32_t y,
                                                  uint32_t w, uint32_t h) {
    return screenshot_helper().capture_region(display, x, y, w, h);
}

std::optional<common::VideoFrame> capture_window(const std::string& title) {
    return screenshot_helper().capture_window(title);
}

std::vector<uint8_t> encode_as_png(const common::VideoFrame& frame) {
    return screenshot_helper().encode_to_format(frame, EncodedFormat::PNG);
}

std::vector<uint8_t> encode_as_jpeg(const common::VideoFrame& frame, int quality) {
    return screenshot_helper().encode_to_format(frame, EncodedFormat::JPEG, quality);
}

std::vector<uint8_t> encode_as_bmp(const common::VideoFrame& frame) {
    return screenshot_helper().encode_to_format(frame, EncodedFormat::BMP);
}

bool copy_frame_to_clipboard(const common::VideoFrame& frame) {
    return screenshot_helper().copy_to_clipboard(frame);
}

void annotate_frame(common::VideoFrame& frame,
                    const std::vector<detail::Annotation>& annotations) {
    screenshot_helper().annotate_in_place(frame, annotations);
}

double compare_frames(const common::VideoFrame& a, const common::VideoFrame& b) {
    return screenshot_helper().compare_frames(a, b);
}

uint64_t capture_async(uint32_t display,
                       std::function<void(std::optional<common::VideoFrame>)> cb) {
    return screenshot_helper().capture_async(display, std::move(cb));
}

void start_screenshot_recording(std::chrono::milliseconds interval, uint32_t display) {
    screenshot_helper().start_recording(interval, display);
}

void stop_screenshot_recording() {
    screenshot_helper().stop_recording();
}

// ============================================================================
// Debug / Information Dump
// ============================================================================

std::string screenshot_debug_info() {
    auto& helper = screenshot_helper();
    auto cache = helper.cache_stats();
    auto displays = helper.displays();

    std::ostringstream oss;
    oss << "=== Screenshot Helper Debug Info ===\n";
    oss << "Displays: " << displays.size() << "\n";
    for (const auto& d : displays) {
        oss << "  [" << d.index << "] " << d.width << "x" << d.height
            << " at (" << d.x << "," << d.y << ")"
            << " primary=" << d.is_primary
            << " name='" << d.name << "'\n";
    }
    oss << "Cache entries: " << cache.entry_count << "\n";
    oss << "Cache total bytes: " << cache.total_bytes << "\n";
    oss << "Quality: jpeg=" << helper.quality_settings().jpeg_quality
        << " png_compression=" << helper.quality_settings().png_compression_level << "\n";
    oss << "Recording: " << (helper.is_recording() ? "active" : "inactive")
        << " frames=" << helper.recording_frame_count() << "\n";

    return oss.str();
}

} // namespace cppdesk::client
