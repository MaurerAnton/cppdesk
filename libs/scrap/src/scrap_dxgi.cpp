/**
 * scrap_dxgi.cpp — Comprehensive Windows DXGI Desktop Duplication screen capture
 *
 * Features:
 *   1. DXGI Desktop Duplication API (IDXGIOutputDuplication) for GPU-accelerated capture
 *   2. Multi-monitor enumeration via EnumDisplayDevices + DXGI adapter outputs
 *   3. Hardware cursor capture (GetFramePointerShape)
 *   4. Dirty-rectangle tracking (GetFrameDirtyRects + AccumulatedRects)
 *   5. D3D11 texture → CPU-accessible staging texture copy
 *   6. BGRA → RGBA format conversion, SIMD-accelerated (SSE2/SSSE3/AVX2)
 *   7. GDI fallback mode using BitBlt + CAPTUREBLT
 *   8. Display-change detection & automatic reinitialization
 *   9. Statistics: frames captured, frames dropped, capture latency, GPU time
 *  10. DPI-awareness modes (Per-Monitor DPI)
 *
 *  Target:  C++20, spdlog logging, Windows 8+ (DXGI 1.2 required for duplication).
 */

#ifdef _WIN32

// ---------------------------------------------------------------------------
//  Windows SDK headers
// ---------------------------------------------------------------------------
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_5.h>   // IDXGIOutput5 for DuplicateOutput1 (if available)
#include <wrl/client.h> // Microsoft::WRL::ComPtr

#include <shellscalingapi.h> // SetProcessDpiAwareness, GetDpiForMonitor
#include <wingdi.h>          // BITMAPINFOHEADER
#include <psapi.h>           // GetProcessMemoryInfo (optional)

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
//  SSE / AVX intrinsics
// ---------------------------------------------------------------------------
#include <intrin.h>
#ifdef __SSE2__
#include <emmintrin.h>
#endif
#ifdef __SSSE3__
#include <tmmintrin.h>
#endif
#ifdef __AVX2__
#include <immintrin.h>
#endif

// ---------------------------------------------------------------------------
//  spdlog
// ---------------------------------------------------------------------------
#include <spdlog/spdlog.h>

// ---------------------------------------------------------------------------
//  Scrap public header (assumed to exist at this relative location)
// ---------------------------------------------------------------------------
#include "../scrap.h"

namespace scrap {

// ============================================================================
//  Forward declarations
// ============================================================================

struct DxgiAdapterInfo;
struct DxgiOutputInfo;
struct DxgiCaptureStats;
class  DxgiDesktopDuplicator;
class  DxgiCaptureEngine;
class  GdiFallbackCapturer;
struct DpiContext;

// ============================================================================
//  Utility helpers
// ============================================================================

namespace {

/**
 * Convert a Windows HRESULT into a human-readable string for logging.
 */
inline std::string hresult_str(HRESULT hr) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "HRESULT 0x%08lX", static_cast<unsigned long>(hr));
    return std::string(buf);
}

/**
 * Short name for a DXGI_FORMAT (for debug output).
 */
inline const char* dxgi_format_name(DXGI_FORMAT fmt) {
    switch (fmt) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:     return "BGRA8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM:     return "RGBA8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:return "BGRA8_SRGB";
    case DXGI_FORMAT_R10G10B10A2_UNORM:   return "R10G10B10A2";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:  return "RGBA16_FLOAT";
    default:                              return "UNKNOWN";
    }
}

/**
 * RAII helper that calls SetThreadDpiAwarenessContext and restores the
 * previous context on destruction.
 */
class ScopedDpiContext {
public:
    using DpiContextFn = decltype(&SetThreadDpiAwarenessContext);

    explicit ScopedDpiContext(DPI_AWARENESS_CONTEXT ctx)
        : fn_(SetThreadDpiAwarenessContext)
    {
        if (fn_) {
            prev_ = fn_(ctx);
        }
    }

    ~ScopedDpiContext() {
        if (fn_ && prev_) {
            fn_(prev_);
        }
    }

    ScopedDpiContext(const ScopedDpiContext&) = delete;
    ScopedDpiContext& operator=(const ScopedDpiContext&) = delete;

private:
    DpiContextFn   fn_;
    DPI_AWARENESS_CONTEXT prev_ = nullptr;
};

/**
 * Return the DPI for a given monitor handle using GetDpiForMonitor (Win 8.1+)
 * or fall back to GetDeviceCaps.
 */
inline UINT get_monitor_dpi(HMONITOR hmon) {
    UINT dpiX = 96;
    UINT dpiY = 96;

    // Try the modern API first (requires Shcore.dll / Win 8.1)
    HMODULE shcore = GetModuleHandleW(L"shcore.dll");
    if (!shcore) shcore = LoadLibraryW(L"shcore.dll");

    if (shcore) {
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);
        auto pGetDpiForMonitor = reinterpret_cast<GetDpiForMonitorFn>(
            GetProcAddress(shcore, "GetDpiForMonitor"));
        if (pGetDpiForMonitor) {
            if (SUCCEEDED(pGetDpiForMonitor(hmon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)))
                return dpiX;
        }
    }

    // Fallback to GDI
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
    }
    return dpiX;
}

/**
 * Set process DPI awareness to Per-Monitor (V2 if available, else V1).
 */
inline void set_per_monitor_dpi_awareness() {
    // Try SetProcessDpiAwarenessContext (Win 10 1703+)
    {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            using SPDPAC = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
            auto fn = reinterpret_cast<SPDPAC>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
            if (fn) {
                fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
                return;
            }
        }
    }
    // Try SetProcessDpiAwareness (Win 8.1+)
    {
        HMODULE shcore = GetModuleHandleW(L"shcore.dll");
        if (!shcore) shcore = LoadLibraryW(L"shcore.dll");
        if (shcore) {
            using SPDA = HRESULT(WINAPI*)(PROCESS_DPI_AWARENESS);
            auto fn = reinterpret_cast<SPDA>(
                GetProcAddress(shcore, "SetProcessDpiAwareness"));
            if (fn) {
                fn(PROCESS_PER_MONITOR_DPI_AWARE);
                return;
            }
        }
    }
    // Ultimate fallback
    SetProcessDPIAware();
}

/**
 * Return a monotonic timestamp in microseconds.
 */
inline int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ============================================================================
//  BGRA → RGBA SIMD conversion (SSE2 / SSSE3 / AVX2 dispatch)
// ============================================================================
//  On x86/x64, pixels are little-endian; BGRA in memory is [B,G,R,A].
//  We need to swap the B and R channels to produce [R,G,B,A].

/**
 * Scalar reference implementation — always available.
 */
inline void bgra_to_rgba_scalar(uint8_t* __restrict dst,
                                 const uint8_t* __restrict src,
                                 size_t pixel_count) {
    for (size_t i = 0; i < pixel_count; ++i) {
        const size_t off = i * 4;
        dst[off + 0] = src[off + 2];  // R ← B
        dst[off + 1] = src[off + 1];  // G ← G
        dst[off + 2] = src[off + 0];  // B ← R
        dst[off + 3] = src[off + 3];  // A ← A
    }
}

#ifdef __SSE2__
/**
 * SSE2 version — processes 4 pixels per iteration.
 * Uses _mm_shufflelo_epi16 / _mm_shufflehi_epi16 for byte swapping.
 */
inline void bgra_to_rgba_sse2(uint8_t* __restrict dst,
                               const uint8_t* __restrict src,
                               size_t pixel_count) {
    const __m128i shuffle_mask =
        _mm_setr_epi8(2, 1, 0, 3,  6, 5, 4, 7,  10, 9, 8, 11,  14, 13, 12, 15);

    size_t simd_count = pixel_count / 4;
    const __m128i* src128 = reinterpret_cast<const __m128i*>(src);
    __m128i*       dst128 = reinterpret_cast<__m128i*>(dst);

    for (size_t i = 0; i < simd_count; ++i) {
        __m128i pixel = _mm_loadu_si128(src128 + i);
        pixel = _mm_shuffle_epi8(pixel, shuffle_mask);
        _mm_storeu_si128(dst128 + i, pixel);
    }

    // remainder (0–3 pixels)
    size_t remaining = pixel_count % 4;
    if (remaining) {
        bgra_to_rgba_scalar(dst + simd_count * 16,
                             src + simd_count * 16,
                             remaining);
    }
}
#endif // __SSE2__

#ifdef __AVX2__
/**
 * AVX2 version — processes 8 pixels per iteration (256-bit).
 */
inline void bgra_to_rgba_avx2(uint8_t* __restrict dst,
                               const uint8_t* __restrict src,
                               size_t pixel_count) {
    const __m256i shuffle_mask =
        _mm256_setr_epi8(
            2, 1, 0, 3,  6, 5, 4, 7,  10, 9, 8, 11,  14, 13, 12, 15,
            2, 1, 0, 3,  6, 5, 4, 7,  10, 9, 8, 11,  14, 13, 12, 15);

    size_t simd_count = pixel_count / 8;
    const __m256i* src256 = reinterpret_cast<const __m256i*>(src);
    __m256i*       dst256 = reinterpret_cast<__m256i*>(dst);

    for (size_t i = 0; i < simd_count; ++i) {
        __m256i pixel = _mm256_loadu_si256(src256 + i);
        pixel = _mm256_shuffle_epi8(pixel, shuffle_mask);
        _mm256_storeu_si256(dst256 + i, pixel);
    }

    size_t remaining = pixel_count % 8;
    if (remaining) {
        // Fall back to SSE2 or scalar for the remainder
#ifdef __SSE2__
        bgra_to_rgba_sse2(dst + simd_count * 32,
                           src + simd_count * 32,
                           remaining);
#else
        bgra_to_rgba_scalar(dst + simd_count * 32,
                             src + simd_count * 32,
                             remaining);
#endif
    }
}
#endif // __AVX2__

/**
 * Automatic SIMD dispatch: AVX2 → SSE2 → scalar.
 */
inline void bgra_to_rgba_simd(uint8_t* __restrict dst,
                               const uint8_t* __restrict src,
                               size_t pixel_count) {
#if defined(__AVX2__)
    bgra_to_rgba_avx2(dst, src, pixel_count);
#elif defined(__SSE2__)
    bgra_to_rgba_sse2(dst, src, pixel_count);
#else
    bgra_to_rgba_scalar(dst, src, pixel_count);
#endif
}

} // anonymous namespace

// ============================================================================
//  DPI-awareness modes
// ============================================================================

enum class DpiMode : uint8_t {
    Unaware,
    SystemAware,
    PerMonitor,
    PerMonitorV2,
};

inline const char* dpi_mode_name(DpiMode m) {
    switch (m) {
    case DpiMode::Unaware:       return "unaware";
    case DpiMode::SystemAware:   return "system";
    case DpiMode::PerMonitor:    return "per-monitor";
    case DpiMode::PerMonitorV2:  return "per-monitor-v2";
    }
    return "?";
}

// ============================================================================
//  DXGI adapter info
// ============================================================================

struct DxgiAdapterInfo {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1                     desc;
    size_t                                 index = 0;

    bool is_software() const {
        return (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    }
};

// ============================================================================
//  DXGI output info (monitor)
// ============================================================================

struct DxgiOutputInfo {
    Microsoft::WRL::ComPtr<IDXGIOutput>     output;
    Microsoft::WRL::ComPtr<IDXGIOutput1>    output1;   // optional, for DuplicateOutput
    Microsoft::WRL::ComPtr<IDXGIOutput5>    output5;   // optional, for DuplicateOutput1
    DXGI_OUTPUT_DESC                        desc;
    HMONITOR                                hmonitor = nullptr;
    UINT                                    dpi      = 96;
    size_t                                  adapter_idx = 0;
    size_t                                  output_idx  = 0;
    bool                                    is_primary  = false;

    /** Device name (e.g., "\\\\.\\DISPLAY1") for EnumDisplayDevices. */
    std::string device_name() const {
        char buf[128];
        if (WideCharToMultiByte(CP_UTF8, 0, desc.DeviceName, -1,
                                 buf, sizeof(buf), nullptr, nullptr) > 0) {
            return std::string(buf);
        }
        return {};
    }
};

// ============================================================================
//  Accumulated dirty-rect list
// ============================================================================

class AccumulatedDirtyRects {
public:
    void clear() {
        std::lock_guard<std::mutex> lk(mtx_);
        rects_.clear();
        total_area_ = 0;
    }

    void add(const RECT& r) {
        if (r.left >= r.right || r.top >= r.bottom) return;
        std::lock_guard<std::mutex> lk(mtx_);
        rects_.push_back(r);
        total_area_ += static_cast<uint64_t>(r.right - r.left) *
                       static_cast<uint64_t>(r.bottom - r.top);
    }

    void add(const std::vector<RECT>& rects) {
        std::lock_guard<std::mutex> lk(mtx_);
        for (const auto& r : rects) {
            if (r.left >= r.right || r.top >= r.bottom) continue;
            rects_.push_back(r);
            total_area_ += static_cast<uint64_t>(r.right - r.left) *
                           static_cast<uint64_t>(r.bottom - r.top);
        }
    }

    void add_union(const std::vector<RECT>& rects) {
        // Simple union: compute the bounding rect of all rects + accumulated
        if (rects.empty()) return;
        std::lock_guard<std::mutex> lk(mtx_);

        RECT bounding = rects[0];
        for (size_t i = 1; i < rects.size(); ++i) {
            bounding.left   = (std::min)(bounding.left,   rects[i].left);
            bounding.top    = (std::min)(bounding.top,    rects[i].top);
            bounding.right  = (std::max)(bounding.right,  rects[i].right);
            bounding.bottom = (std::max)(bounding.bottom, rects[i].bottom);
        }
        // Also union with existing accumulated bounding
        if (!rects_.empty()) {
            RECT& existing = rects_.front();
            rects_.clear();
            existing.left   = (std::min)(existing.left,   bounding.left);
            existing.top    = (std::min)(existing.top,    bounding.top);
            existing.right  = (std::max)(existing.right,  bounding.right);
            existing.bottom = (std::max)(existing.bottom, bounding.bottom);
            rects_.push_back(existing);
        } else {
            rects_.push_back(bounding);
        }

        total_area_ += static_cast<uint64_t>(bounding.right  - bounding.left) *
                       static_cast<uint64_t>(bounding.bottom - bounding.top);
    }

    std::vector<RECT> get_and_clear() {
        std::lock_guard<std::mutex> lk(mtx_);
        auto result = std::move(rects_);
        rects_.clear();
        total_area_ = 0;
        return result;
    }

    uint64_t total_area() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return total_area_;
    }

private:
    mutable std::mutex mtx_;
    std::vector<RECT>   rects_;
    uint64_t            total_area_ = 0;
};

// ============================================================================
//  Capture statistics (thread-safe)
// ============================================================================

struct DxgiCaptureStats {
    std::atomic<uint64_t> frames_attempted{0};
    std::atomic<uint64_t> frames_captured{0};
    std::atomic<uint64_t> frames_dropped{0};     // DXGI reported drops
    std::atomic<uint64_t> frames_timeout{0};     // acquire timeout
    std::atomic<uint64_t> frames_access_lost{0}; // DXGI_ERROR_ACCESS_LOST
    std::atomic<uint64_t> frames_other_error{0};
    std::atomic<uint64_t> frames_gdi_fallback{0};
    std::atomic<int64_t>  total_capture_time_us{0};
    std::atomic<int64_t>  total_gpu_time_us{0};
    std::atomic<int64_t>  min_capture_time_us{std::numeric_limits<int64_t>::max()};
    std::atomic<int64_t>  max_capture_time_us{0};
    std::atomic<uint64_t> reinit_count{0};

    void record_capture(int64_t cap_us, int64_t gpu_us) {
        frames_captured.fetch_add(1, std::memory_order_relaxed);
        total_capture_time_us.fetch_add(cap_us, std::memory_order_relaxed);
        total_gpu_time_us.fetch_add(gpu_us, std::memory_order_relaxed);
        // Update min (non-atomic read-modify-write, adequate for stats)
        int64_t cur_min = min_capture_time_us.load(std::memory_order_relaxed);
        while (cap_us < cur_min &&
               !min_capture_time_us.compare_exchange_weak(cur_min, cap_us)) {}

        int64_t cur_max = max_capture_time_us.load(std::memory_order_relaxed);
        while (cap_us > cur_max &&
               !max_capture_time_us.compare_exchange_weak(cur_max, cap_us)) {}
    }

    double avg_capture_time_ms() const {
        uint64_t n = frames_captured.load(std::memory_order_relaxed);
        if (n == 0) return 0.0;
        return static_cast<double>(total_capture_time_us.load()) / n / 1000.0;
    }

    std::string summary() const {
        std::ostringstream oss;
        uint64_t cap   = frames_captured;
        uint64_t drop  = frames_dropped;
        uint64_t to    = frames_timeout;
        uint64_t lost  = frames_access_lost;
        uint64_t gdi   = frames_gdi_fallback;
        oss << "captured=" << cap
            << " dropped=" << drop
            << " timeout=" << to
            << " access_lost=" << lost
            << " gdi_fallback=" << gdi
            << " reinit=" << reinit_count
            << " avg_cap_ms=" << avg_capture_time_ms();
        return oss.str();
    }
};

// ============================================================================
//  Hardware cursor shape
// ============================================================================

struct CursorShape {
    DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info;
    std::vector<uint8_t>             buffer;      // raw pixel buffer
    bool                             visible = false;

    /** Width × height in pixels of the entire cursor shape buffer. */
    UINT pixel_width()  const { return shape_info.Width; }
    UINT pixel_height() const {
        UINT h = shape_info.Height;
        // Monochrome cursors store mask + color in a single height*2 buffer
        if (shape_info.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME) {
            h /= 2;
        }
        return h;
    }
};

// ============================================================================
//  GDI fallback capturer
// ============================================================================

class GdiFallbackCapturer {
public:
    GdiFallbackCapturer() = default;
    ~GdiFallbackCapturer() { release(); }

    GdiFallbackCapturer(const GdiFallbackCapturer&) = delete;
    GdiFallbackCapturer& operator=(const GdiFallbackCapturer&) = delete;

    struct GdiFrame {
        std::vector<uint8_t> pixels;
        int                  width   = 0;
        int                  height  = 0;
        int                  stride  = 0; // bytes per row
        int                  left    = 0;
        int                  top     = 0;
    };

    /**
     * Capture the entire virtual desktop using BitBlt with CAPTUREBLT
     * to include layered windows.
     */
    bool capture(GdiFrame& frame) {
        int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int y = GetSystemMetrics(SM_YVIRTUALSCREEN);

        if (w <= 0 || h <= 0) {
            spdlog::error("[GDI] Invalid virtual screen size: {}x{}", w, h);
            return false;
        }

        HDC hdcScreen = GetDC(nullptr);
        if (!hdcScreen) {
            spdlog::error("[GDI] GetDC(nullptr) failed");
            return false;
        }

        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        if (!hdcMem) {
            spdlog::error("[GDI] CreateCompatibleDC failed");
            ReleaseDC(nullptr, hdcScreen);
            return false;
        }

        // Create a 32-bit DIB section
        BITMAPINFOHEADER bi = {};
        bi.biSize        = sizeof(BITMAPINFOHEADER);
        bi.biWidth       = w;
        bi.biHeight      = -h;  // top-down
        bi.biPlanes      = 1;
        bi.biBitCount    = 32;
        bi.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP hbmp = CreateDIBSection(hdcScreen,
                                         reinterpret_cast<BITMAPINFO*>(&bi),
                                         DIB_RGB_COLORS,
                                         &bits,
                                         nullptr, 0);
        if (!hbmp || !bits) {
            spdlog::error("[GDI] CreateDIBSection failed");
            DeleteDC(hdcMem);
            ReleaseDC(nullptr, hdcScreen);
            return false;
        }

        HGDIOBJ oldBmp = SelectObject(hdcMem, hbmp);

        // CAPTUREBLT includes layered windows (e.g., WS_EX_LAYERED)
        DWORD rop = SRCCOPY | CAPTUREBLT;
        BOOL ok = BitBlt(hdcMem, 0, 0, w, h, hdcScreen, x, y, rop);
        if (!ok) {
            spdlog::error("[GDI] BitBlt failed: {}", GetLastError());
        }

        // Copy pixel data out before destroying the bitmap
        if (ok && bits) {
            size_t total_bytes = static_cast<size_t>(w) * h * 4;
            frame.pixels.resize(total_bytes);
            std::memcpy(frame.pixels.data(), bits, total_bytes);
            frame.width  = w;
            frame.height = h;
            frame.stride = w * 4;
            frame.left   = x;
            frame.top    = y;

            // BGRA → RGBA conversion
            bgra_to_rgba_simd(frame.pixels.data(), frame.pixels.data(),
                               static_cast<size_t>(w) * h);
        }

        SelectObject(hdcMem, oldBmp);
        DeleteObject(hbmp);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);

        return ok != 0;
    }

    void release() {
        // nothing to explicitly release (all locals are RAII in capture())
    }

private:
};

// ============================================================================
//  DXGI Desktop Duplicator — per-output (per-monitor) capture
// ============================================================================

class DxgiDesktopDuplicator {
public:
    DxgiDesktopDuplicator(DxgiOutputInfo output_info,
                          Microsoft::WRL::ComPtr<ID3D11Device> d3d_device,
                          int timeout_ms = 100)
        : output_info_(std::move(output_info))
        , d3d_device_(std::move(d3d_device))
        , timeout_ms_(timeout_ms)
    {
        device_name_ = output_info_.device_name();
    }

    ~DxgiDesktopDuplicator() { release(); }

    DxgiDesktopDuplicator(const DxgiDesktopDuplicator&) = delete;
    DxgiDesktopDuplicator& operator=(const DxgiDesktopDuplicator&) = delete;

    /**
     * Initialize the duplicator for this output.
     * Must have a valid IDXGIOutput and D3D11Device.
     */
    bool initialize() {
        d3d_device_->GetImmediateContext(d3d_context_.ReleaseAndGetAddressOf());
        if (!d3d_context_) {
            spdlog::error("[DXGI:{}] GetImmediateContext failed", device_name_);
            return false;
        }

        // Try DuplicateOutput1 (DXGI 1.5) first for higher performance,
        // fall back to DuplicateOutput (DXGI 1.2).
        HRESULT hr = E_NOINTERFACE;

        if (output_info_.output5) {
            hr = output_info_.output5->DuplicateOutput1(
                d3d_device_.Get(),
                0, // flags
                0, // max supported formats (0 = all)
                DXGI_FORMAT_B8G8R8A8_UNORM,
                duplication_.ReleaseAndGetAddressOf());
            if (SUCCEEDED(hr)) {
                spdlog::info("[DXGI:{}] Using DuplicateOutput1 (DXGI 1.5)", device_name_);
                dupl_version_ = 1;
            }
        }

        if (FAILED(hr) && output_info_.output1) {
            hr = output_info_.output1->DuplicateOutput(
                d3d_device_.Get(),
                duplication_.ReleaseAndGetAddressOf());
            if (SUCCEEDED(hr)) {
                spdlog::info("[DXGI:{}] Using DuplicateOutput (DXGI 1.2)", device_name_);
                dupl_version_ = 0;
            }
        }

        if (FAILED(hr) || !duplication_) {
            spdlog::error("[DXGI:{}] DuplicateOutput failed: {}", device_name_, hresult_str(hr));
            log_dxgi_duplication_failure(hr);
            return false;
        }

        // Query the output description for frame metadata
        DXGI_OUTDUPL_DESC dup_desc = {};
        hr = duplication_->GetDesc(&dup_desc);
        if (FAILED(hr)) {
            spdlog::error("[DXGI:{}] GetDesc failed: {}", device_name_, hresult_str(hr));
            return false;
        }

        desc_          = dup_desc;
        output_width_  = desc_.ModeDesc.Width;
        output_height_ = desc_.ModeDesc.Height;
        output_format_ = desc_.ModeDesc.Format;
        desktop_coords_in_surface_ = desc_.DesktopCoordinates;

        spdlog::info("[DXGI:{}] Output: {}x{} fmt={} desktop_coords=({},{},{},{})",
                     device_name_,
                     output_width_, output_height_,
                     dxgi_format_name(output_format_),
                     desktop_coords_in_surface_.left,
                     desktop_coords_in_surface_.top,
                     desktop_coords_in_surface_.right,
                     desktop_coords_in_surface_.bottom);

        // Create staging texture for CPU readback
        return create_staging_texture();
    }

    /**
     * Acquire the next desktop frame. Returns false if no frame is available
     * or an error occurs. On success, the frame is locked until ReleaseFrame().
     */
    bool acquire_frame() {
        if (!duplication_) return false;

        stats_.frames_attempted.fetch_add(1, std::memory_order_relaxed);

        IDXGIResource*        desktop_resource = nullptr;
        DXGI_OUTDUPL_FRAME_INFO frame_info = {};

        HRESULT hr = duplication_->AcquireNextFrame(
            static_cast<UINT>(timeout_ms_),
            &frame_info,
            &desktop_resource);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            stats_.frames_timeout.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        if (hr == DXGI_ERROR_ACCESS_LOST) {
            stats_.frames_access_lost.fetch_add(1, std::memory_order_relaxed);
            spdlog::warn("[DXGI:{}] Access lost — reinitialization required", device_name_);
            needs_reinit_ = true;
            return false;
        }

        if (FAILED(hr)) {
            stats_.frames_other_error.fetch_add(1, std::memory_order_relaxed);
            spdlog::debug("[DXGI:{}] AcquireNextFrame failed: {}", device_name_, hresult_str(hr));
            return false;
        }

        // Wrap the resource in a ComPtr for auto-release on error
        Microsoft::WRL::ComPtr<IDXGIResource> frame_res(desktop_resource);

        // Record timing
        int64_t cap_start = now_us();

        // Check for dropped frames
        if (frame_info.AccumulatedFrames > 0) {
            stats_.frames_dropped.fetch_add(frame_info.AccumulatedFrames,
                                             std::memory_order_relaxed);
        }

        // Record GPU time
        int64_t gpu_time_us = frame_info.LastPresentTime.QuadPart; // raw value; approximate
        (void)gpu_time_us;

        // ------------------------------------------------------------------
        // 1. Query dirty rects
        // ------------------------------------------------------------------
        process_dirty_rects(frame_info);

        // ------------------------------------------------------------------
        // 2. Query mouse pointer
        // ------------------------------------------------------------------
        if (frame_info.PointerPosition.Visible) {
            process_pointer_shape(frame_info);
        } else {
            cursor_shape_.visible = false;
        }

        // ------------------------------------------------------------------
        // 3. Copy desktop texture to staging
        // ------------------------------------------------------------------
        Microsoft::WRL::ComPtr<ID3D11Texture2D> desktop_texture;
        hr = frame_res.As(&desktop_texture);
        if (FAILED(hr)) {
            spdlog::error("[DXGI:{}] QI for ID3D11Texture2D failed: {}",
                          device_name_, hresult_str(hr));
            duplication_->ReleaseFrame();
            return false;
        }

        copy_to_staging(desktop_texture.Get());

        int64_t cap_end  = now_us();
        int64_t cap_time = cap_end - cap_start;
        stats_.record_capture(cap_time, gpu_time_us);

        has_frame_ = true;
        duplication_->ReleaseFrame();
        return true;
    }

    /**
     * Release resources for this duplicator.
     */
    void release() {
        if (duplication_) {
            duplication_->ReleaseFrame();
        }
        staging_texture_.Reset();
        d3d_context_.Reset();
        duplication_.Reset();
        output_info_ = {};
    }

    // --- Accessors ---

    const DXGI_OUTDUPL_DESC& desc() const { return desc_; }
    UINT output_width()           const { return output_width_; }
    UINT output_height()          const { return output_height_; }
    RECT desktop_coords()         const { return desktop_coords_in_surface_; }
    HMONITOR monitor()            const { return output_info_.hmonitor; }
    UINT dpi()                    const { return output_info_.dpi; }
    bool needs_reinit()           const { return needs_reinit_.load(); }
    void clear_needs_reinit()           { needs_reinit_.store(false); }
    const std::string& dev_name() const { return device_name_; }

    /** Read out the current staging buffer (CPU-accessible). */
    const std::vector<uint8_t>& staging_buffer() const { return staging_buffer_; }
    size_t staging_stride() const { return staging_stride_; }
    bool has_frame() const { return has_frame_; }

    /** Dirty rect accumulated since last read. */
    std::vector<RECT> get_dirty_rects() { return accum_dirty_rects_.get_and_clear(); }

    /** Cursor shape from the most recent frame (if visible). */
    const CursorShape& cursor_shape() const { return cursor_shape_; }

    const DxgiCaptureStats& stats() const { return stats_; }

private:
    // -----------------------------------------------------------------------
    //  Create a CPU-accessible staging texture
    // -----------------------------------------------------------------------
    bool create_staging_texture() {
        D3D11_TEXTURE2D_DESC staging_desc = {};
        staging_desc.Width            = output_width_;
        staging_desc.Height           = output_height_;
        staging_desc.MipLevels        = 1;
        staging_desc.ArraySize        = 1;
        staging_desc.Format           = output_format_;  // e.g., DXGI_FORMAT_B8G8R8A8_UNORM
        staging_desc.SampleDesc.Count = 1;
        staging_desc.SampleDesc.Quality = 0;
        staging_desc.Usage            = D3D11_USAGE_STAGING;
        staging_desc.BindFlags        = 0;
        staging_desc.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;
        staging_desc.MiscFlags        = 0;

        HRESULT hr = d3d_device_->CreateTexture2D(&staging_desc, nullptr,
                                                   staging_texture_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            spdlog::error("[DXGI:{}] CreateTexture2D (staging) failed: {}",
                          device_name_, hresult_str(hr));
            return false;
        }

        // Pre-allocate staging buffer
        size_t row_pitch = output_width_ * 4; // BGRA = 4 bytes/pixel
        staging_buffer_.resize(row_pitch * output_height_);
        staging_stride_ = row_pitch;

        spdlog::debug("[DXGI:{}] Staging texture {}x{} created ({} bytes)",
                      device_name_, output_width_, output_height_,
                      staging_buffer_.size());
        return true;
    }

    // -----------------------------------------------------------------------
    //  Copy desktop texture → staging texture → CPU buffer
    // -----------------------------------------------------------------------
    void copy_to_staging(ID3D11Texture2D* desktop_tex) {
        if (!desktop_tex || !staging_texture_ || !d3d_context_) return;

        // Copy the entire resource
        d3d_context_->CopyResource(staging_texture_.Get(), desktop_tex);

        // Map staging texture for CPU read
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = d3d_context_->Map(staging_texture_.Get(), 0,
                                        D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            spdlog::error("[DXGI:{}] Map staging texture failed: {}",
                          device_name_, hresult_str(hr));
            return;
        }

        // Copy row by row (handles pitch != width*bytesPerPixel)
        const uint8_t* src_row = static_cast<const uint8_t*>(mapped.pData);
        uint8_t*       dst_row = staging_buffer_.data();

        size_t copy_bytes_per_row = (std::min)(
            static_cast<size_t>(mapped.RowPitch),
            staging_stride_);

        for (UINT y = 0; y < output_height_; ++y) {
            std::memcpy(dst_row, src_row, copy_bytes_per_row);
            src_row += mapped.RowPitch;
            dst_row += staging_stride_;
        }

        d3d_context_->Unmap(staging_texture_.Get(), 0);

        // Convert BGRA → RGBA in-place
        bgra_to_rgba_simd(staging_buffer_.data(), staging_buffer_.data(),
                           static_cast<size_t>(output_width_) * output_height_);
    }

    // -----------------------------------------------------------------------
    //  Process dirty rectangles from frame metadata
    // -----------------------------------------------------------------------
    void process_dirty_rects(const DXGI_OUTDUPL_FRAME_INFO& info) {
        if (info.TotalMetadataBufferSize == 0) return;

        // Allocate buffer for move rects + dirty rects
        std::vector<uint8_t> meta_buf(info.TotalMetadataBufferSize);
        UINT meta_buf_size = info.TotalMetadataBufferSize;

        HRESULT hr = duplication_->GetFrameDirtyRects(
            meta_buf_size,
            reinterpret_cast<RECT*>(meta_buf.data()),
            &meta_buf_size);

        if (SUCCEEDED(hr)) {
            UINT num_dirty = meta_buf_size / sizeof(RECT);
            if (num_dirty > 0) {
                const RECT* rects = reinterpret_cast<const RECT*>(meta_buf.data());
                accum_dirty_rects_.add(std::vector<RECT>(rects, rects + num_dirty));
            }
        } else if (hr != DXGI_ERROR_MORE_DATA && hr != DXGI_ERROR_INVALID_CALL) {
            spdlog::debug("[DXGI:{}] GetFrameDirtyRects: {}", device_name_, hresult_str(hr));
        }

        // Also query move rects (optional — useful for dirty-rect merging)
        meta_buf_size = info.TotalMetadataBufferSize;
        hr = duplication_->GetFrameMoveRects(
            meta_buf_size,
            reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(meta_buf.data()),
            &meta_buf_size);

        if (SUCCEEDED(hr)) {
            UINT num_move = meta_buf_size / sizeof(DXGI_OUTDUPL_MOVE_RECT);
            if (num_move > 0) {
                const DXGI_OUTDUPL_MOVE_RECT* move_rects =
                    reinterpret_cast<const DXGI_OUTDUPL_MOVE_RECT*>(meta_buf.data());
                std::vector<RECT> dirty_from_moves;
                dirty_from_moves.reserve(num_move);
                for (UINT i = 0; i < num_move; ++i) {
                    RECT r;
                    r.left   = static_cast<LONG>(move_rects[i].DestinationRect.left);
                    r.top    = static_cast<LONG>(move_rects[i].DestinationRect.top);
                    r.right  = static_cast<LONG>(move_rects[i].DestinationRect.right);
                    r.bottom = static_cast<LONG>(move_rects[i].DestinationRect.bottom);
                    dirty_from_moves.push_back(r);
                }
                accum_dirty_rects_.add(dirty_from_moves);
            }
        }
    }

    // -----------------------------------------------------------------------
    //  Process hardware cursor (pointer shape)
    // -----------------------------------------------------------------------
    void process_pointer_shape(const DXGI_OUTDUPL_FRAME_INFO& info) {
        if (info.PointerShapeBufferSize == 0) {
            cursor_shape_.visible = false;
            return;
        }

        // Allocate if needed
        if (cursor_shape_.buffer.size() < info.PointerShapeBufferSize) {
            cursor_shape_.buffer.resize(info.PointerShapeBufferSize);
        }

        UINT required_size = info.PointerShapeBufferSize;
        HRESULT hr = duplication_->GetFramePointerShape(
            required_size,
            cursor_shape_.buffer.data(),
            &required_size,
            &cursor_shape_.shape_info);

        if (SUCCEEDED(hr)) {
            cursor_shape_.visible = (info.PointerPosition.Visible != 0);
        } else {
            cursor_shape_.visible = false;
            spdlog::debug("[DXGI:{}] GetFramePointerShape failed: {}",
                          device_name_, hresult_str(hr));
        }
    }

    // -----------------------------------------------------------------------
    //  Log why DuplicateOutput failed
    // -----------------------------------------------------------------------
    void log_dxgi_duplication_failure(HRESULT hr) {
        switch (hr) {
        case E_INVALIDARG:
            spdlog::error("[DXGI:{}] DuplicateOutput E_INVALIDARG: possible driver issue "
                          "or GPU doesn't support Desktop Duplication. "
                          "Ensure WDDM 1.2+ driver.", device_name_);
            break;
        case E_ACCESSDENIED:
            spdlog::error("[DXGI:{}] DuplicateOutput E_ACCESSDENIED: "
                          "process must run at the same integrity level as the desktop.", device_name_);
            break;
        case DXGI_ERROR_UNSUPPORTED:
            spdlog::error("[DXGI:{}] DuplicateOutput DXGI_ERROR_UNSUPPORTED: "
                          "duplication not supported by this driver/output.", device_name_);
            break;
        case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:
            spdlog::error("[DXGI:{}] DuplicateOutput NOT_CURRENTLY_AVAILABLE: "
                          "output may be in use by another duplicator.", device_name_);
            break;
        default:
            spdlog::error("[DXGI:{}] DuplicateOutput failed: {}", device_name_, hresult_str(hr));
            break;
        }
    }

    // -----------------------------------------------------------------------
    //  Data members
    // -----------------------------------------------------------------------
    DxgiOutputInfo                           output_info_;
    Microsoft::WRL::ComPtr<ID3D11Device>     d3d_device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;

    std::string  device_name_;
    int          timeout_ms_   = 100;
    int          dupl_version_ = 0; // 0 = DuplicateOutput, 1 = DuplicateOutput1

    DXGI_OUTDUPL_DESC desc_     = {};
    UINT              output_width_  = 0;
    UINT              output_height_ = 0;
    DXGI_FORMAT       output_format_ = DXGI_FORMAT_UNKNOWN;
    RECT              desktop_coords_in_surface_ = {};

    // Staging
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_texture_;
    std::vector<uint8_t>                    staging_buffer_;
    size_t                                  staging_stride_ = 0;

    // State
    std::atomic<bool>     needs_reinit_{false};
    bool                  has_frame_  = false;
    AccumulatedDirtyRects accum_dirty_rects_;
    CursorShape           cursor_shape_;

    // Stats
    DxgiCaptureStats stats_;
};

// ============================================================================
//  DXGI Capture Engine — orchestrates multi-monitor enumeration, capture, fallback
// ============================================================================

class DxgiCaptureEngine {
public:
    struct Config {
        int     timeout_ms           = 100;   // AcquireNextFrame timeout
        bool    use_gdi_fallback      = true;  // Enable GDI fallback on DXGI failure
        bool    capture_cursor        = true;  // Capture hardware cursor
        bool    track_dirty_rects     = true;  // Track dirty rectangles
        DpiMode dpi_mode              = DpiMode::PerMonitorV2;
        bool    prefer_integrated_gpu = false; // Favour iGPU over dGPU (power saving)
        bool    prefer_high_perf_gpu  = false; // Favour dGPU (performance)
    };

    explicit DxgiCaptureEngine(Config cfg = {})
        : config_(std::move(cfg))
    {
        set_dpi_awareness();
    }

    ~DxgiCaptureEngine() { shutdown(); }

    DxgiCaptureEngine(const DxgiCaptureEngine&) = delete;
    DxgiCaptureEngine& operator=(const DxgiCaptureEngine&) = delete;

    // -----------------------------------------------------------------------
    //  Initialization
    // -----------------------------------------------------------------------

    /**
     * Enumerate adapters, outputs, and create duplicators.
     * Returns true if at least one duplicator was created, or GDI fallback
     * is available.
     */
    bool initialize() {
        std::lock_guard<std::mutex> lk(mutex_);

        if (initialized_) {
            spdlog::warn("[DXGI Engine] Already initialized");
            return true;
        }

        spdlog::info("[DXGI Engine] Initializing...");

        // 1. Create DXGI factory
        if (!create_factory()) {
            spdlog::error("[DXGI Engine] Failed to create DXGI factory");
            if (config_.use_gdi_fallback) {
                spdlog::info("[DXGI Engine] Will use GDI fallback");
                initialized_ = true;
                using_gdi_fallback_ = true;
                return true;
            }
            return false;
        }

        // 2. Enumerate adapters and outputs
        enumerate_adapters_and_outputs();

        // 3. Create D3D11 device on the best adapter
        //    (Disable debug layer in release builds for performance)
        UINT create_flags = 0;
#ifdef _DEBUG
        create_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        D3D_FEATURE_LEVEL feature_levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };

        if (!create_d3d11_device(create_flags, feature_levels)) {
            spdlog::error("[DXGI Engine] Failed to create D3D11 device");
            if (config_.use_gdi_fallback) {
                initialized_ = true;
                using_gdi_fallback_ = true;
                return true;
            }
            return false;
        }

        // 4. Create a duplicator for each output
        create_duplicators();

        if (duplicators_.empty()) {
            spdlog::warn("[DXGI Engine] No duplicators created");
            if (config_.use_gdi_fallback) {
                spdlog::info("[DXGI Engine] Falling back to GDI");
                initialized_ = true;
                using_gdi_fallback_ = true;
                return true;
            }
            return false;
        }

        spdlog::info("[DXGI Engine] Initialized with {} duplicator(s), {} output(s)",
                     duplicators_.size(), outputs_.size());
        initialized_ = true;
        return true;
    }

    /**
     * Shutdown and release all DXGI resources.
     */
    void shutdown() {
        std::lock_guard<std::mutex> lk(mutex_);
        duplicators_.clear();
        d3d_context_.Reset();
        d3d_device_.Reset();
        dxgi_factory_.Reset();
        adapters_.clear();
        outputs_.clear();
        gdi_capturer_.release();
        initialized_         = false;
        using_gdi_fallback_  = false;
    }

    // -----------------------------------------------------------------------
    //  Capture loop
    // -----------------------------------------------------------------------

    /**
     * Capture a single frame from all active duplicators (or GDI).
     *
     * @param frame_data  Output vector of per-monitor frame data
     * @return            Number of monitors captured
     */
    int capture_frame(std::vector<scrap::CaptureData>& frame_data) {
        if (!initialized_) {
            spdlog::error("[DXGI Engine] Not initialized");
            return 0;
        }

        // GDI fallback path
        if (using_gdi_fallback_) {
            return capture_frame_gdi(frame_data);
        }

        int captured = 0;

        std::lock_guard<std::mutex> lk(mutex_);

        // Check for display changes and reinit if necessary
        bool any_needs_reinit = false;
        for (auto& dup : duplicators_) {
            if (dup->needs_reinit()) {
                any_needs_reinit = true;
                break;
            }
        }

        if (any_needs_reinit) {
            spdlog::warn("[DXGI Engine] Display change detected, reinitializing...");
            reinitialize_locked();
            if (duplicators_.empty()) return 0;
        }

        // Acquire frames from all duplicators
        for (auto& dup : duplicators_) {
            if (dup->acquire_frame()) {
                ++captured;
            }
        }

        // Build output data
        if (captured > 0) {
            frame_data.clear();
            frame_data.reserve(duplicators_.size());

            for (auto& dup : duplicators_) {
                if (!dup->has_frame()) continue;

                scrap::CaptureData cd;
                cd.width    = static_cast<int>(dup->output_width());
                cd.height   = static_cast<int>(dup->output_height());
                cd.pixels   = dup->staging_buffer(); // copy
                cd.stride   = static_cast<int>(dup->staging_stride());
                cd.left     = dup->desktop_coords().left;
                cd.top      = dup->desktop_coords().top;
                cd.dpi      = static_cast<int>(dup->dpi());
                cd.monitor  = dup->dev_name();

                if (config_.capture_cursor) {
                    const auto& cs = dup->cursor_shape();
                    cd.has_cursor  = cs.visible;
                    cd.cursor_x    = 0; // set by PointerPosition if available
                    cd.cursor_y    = 0;
                }

                if (config_.track_dirty_rects) {
                    cd.dirty_rects = dup->get_dirty_rects();
                }

                frame_data.push_back(std::move(cd));
            }
        }

        return captured;
    }

    /**
     * Check if a display change has occurred.
     * The caller should call this periodically and reinitialize if needed.
     */
    bool detect_display_change() {
        // Quick check: number of monitors changed?
        UINT current_monitor_count = 0;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            current_monitor_count = static_cast<UINT>(outputs_.size());
        }

        // Re-enumerate
        std::vector<DxgiOutputInfo> fresh_outputs;
        if (!enumerate_outputs_only(fresh_outputs)) {
            return false; // cannot determine
        }

        if (fresh_outputs.size() != current_monitor_count) {
            spdlog::info("[DXGI Engine] Monitor count changed: {} → {}",
                         current_monitor_count, fresh_outputs.size());
            return true;
        }

        // Compare device names
        {
            std::lock_guard<std::mutex> lk(mutex_);
            for (size_t i = 0; i < fresh_outputs.size() && i < outputs_.size(); ++i) {
                if (fresh_outputs[i].device_name() != outputs_[i].device_name()) {
                    return true;
                }
                // Check resolution change
                RECT frc = fresh_outputs[i].desc.DesktopCoordinates;
                RECT crc = outputs_[i].desc.DesktopCoordinates;
                if (frc.left != crc.left || frc.top != crc.top ||
                    frc.right != crc.right || frc.bottom != crc.bottom) {
                    spdlog::info("[DXGI Engine] Monitor geometry changed for {}",
                                 fresh_outputs[i].device_name());
                    return true;
                }
            }
        }

        return false;
    }

    // -----------------------------------------------------------------------
    //  Statistics
    // -----------------------------------------------------------------------

    std::string stats_summary() const {
        std::lock_guard<std::mutex> lk(mutex_);
        std::ostringstream oss;
        oss << "[DXGI Engine] ";
        if (using_gdi_fallback_) {
            oss << "GDI fallback | ";
        }
        for (const auto& dup : duplicators_) {
            oss << "[" << dup->dev_name() << "] " << dup->stats().summary() << "; ";
        }
        return oss.str();
    }

    // -----------------------------------------------------------------------
    //  Accessors
    // -----------------------------------------------------------------------

    bool is_using_gdi_fallback() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return using_gdi_fallback_;
    }

    size_t duplicator_count() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return duplicators_.size();
    }

    const std::vector<DxgiOutputInfo>& outputs() const { return outputs_; }

private:
    // -----------------------------------------------------------------------
    //  DPI awareness
    // -----------------------------------------------------------------------
    void set_dpi_awareness() {
        switch (config_.dpi_mode) {
        case DpiMode::Unaware:
            // Default; do nothing
            break;
        case DpiMode::SystemAware:
            SetProcessDPIAware();
            break;
        case DpiMode::PerMonitor:
        case DpiMode::PerMonitorV2:
            set_per_monitor_dpi_awareness();
            break;
        }
        spdlog::info("[DXGI Engine] DPI mode: {}", dpi_mode_name(config_.dpi_mode));
    }

    // -----------------------------------------------------------------------
    //  Factory creation
    // -----------------------------------------------------------------------
    bool create_factory() {
        HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory2),
                                         reinterpret_cast<void**>(
                                             dxgi_factory_.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) {
            // Fall back to IDXGIFactory1
            hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                     reinterpret_cast<void**>(
                                         dxgi_factory_.ReleaseAndGetAddressOf()));
        }
        if (FAILED(hr)) {
            spdlog::error("[DXGI Engine] CreateDXGIFactory1 failed: {}", hresult_str(hr));
            return false;
        }
        return true;
    }

    // -----------------------------------------------------------------------
    //  D3D11 device creation (with adapter preference)
    // -----------------------------------------------------------------------
    bool create_d3d11_device(UINT flags, const D3D_FEATURE_LEVEL* feature_levels,
                              UINT num_levels = 2)
    {
        // Choose adapter
        IDXGIAdapter* selected_adapter = nullptr;
        if (!adapters_.empty()) {
            // Prefer hardware adapters
            for (auto& a : adapters_) {
                if (!a.is_software()) {
                    selected_adapter = a.adapter.Get();
                    break;
                }
            }
        }

        D3D_FEATURE_LEVEL chosen_level;
        HRESULT hr = D3D11CreateDevice(
            selected_adapter,
            selected_adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr,                        // software rasterizer
            flags,
            feature_levels, num_levels,
            D3D11_SDK_VERSION,
            d3d_device_.ReleaseAndGetAddressOf(),
            &chosen_level,
            d3d_context_.ReleaseAndGetAddressOf());

        if (FAILED(hr)) {
            spdlog::error("[DXGI Engine] D3D11CreateDevice failed: {}", hresult_str(hr));
            return false;
        }

        const char* fl_name = (chosen_level == D3D_FEATURE_LEVEL_11_1) ? "11.1" : "11.0";
        spdlog::info("[DXGI Engine] D3D11 device created (feature level {})", fl_name);
        return true;
    }

    // -----------------------------------------------------------------------
    //  Adapter & output enumeration
    // -----------------------------------------------------------------------

    void enumerate_adapters_and_outputs() {
        adapters_.clear();
        outputs_.clear();

        if (!dxgi_factory_) return;

        // First pass: use EnumDisplayDevices to get monitor handles and device names
        enumerate_display_devices_map();

        // Second pass: use DXGI to enumerate adapters and outputs
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;

        for (UINT adapter_idx = 0;
             dxgi_factory_->EnumAdapters1(adapter_idx, adapter.ReleaseAndGetAddressOf())
                 != DXGI_ERROR_NOT_FOUND;
             ++adapter_idx)
        {
            DxgiAdapterInfo info;
            info.adapter = adapter;
            info.index   = adapter_idx;

            if (FAILED(adapter->GetDesc1(&info.desc))) {
                continue;
            }

            // Convert adapter description for logging
            char adapter_name[256];
            WideCharToMultiByte(CP_UTF8, 0, info.desc.Description, -1,
                                adapter_name, sizeof(adapter_name), nullptr, nullptr);

            spdlog::info("[DXGI Engine] Adapter {}: {} (vendor=0x{:04X}, device=0x{:04X}, "
                         "dedicated_vram={}MB, sw={})",
                         adapter_idx, adapter_name,
                         info.desc.VendorId, info.desc.DeviceId,
                         info.desc.DedicatedVideoMemory / (1024*1024),
                         info.is_software());

            if (info.is_software() && !config_.prefer_integrated_gpu) {
                // Skip software adapter unless explicitly requested
                continue;
            }

            adapters_.push_back(std::move(info));

            // Enumerate outputs on this adapter
            enumerate_outputs_on_adapter(adapters_.back().adapter.Get(), adapter_idx);
        }

        spdlog::info("[DXGI Engine] Enumerated {} adapter(s), {} output(s)",
                     adapters_.size(), outputs_.size());
    }

    /**
     * Use EnumDisplayDevices to build a map of device name → HMONITOR.
     * This helps us later correlate DXGI outputs with actual monitors.
     */
    void enumerate_display_devices_map() {
        display_device_map_.clear();

        DISPLAY_DEVICEW dd;
        dd.cb = sizeof(dd);

        for (DWORD dev_num = 0;
             EnumDisplayDevicesW(nullptr, dev_num, &dd, 0);
             ++dev_num)
        {
            if (!(dd.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;

            // Convert wide device name to UTF-8
            char device_name[128];
            WideCharToMultiByte(CP_UTF8, 0, dd.DeviceName, -1,
                                device_name, sizeof(device_name), nullptr, nullptr);

            // Get the monitor handle for this display device
            HMONITOR hmon = nullptr;
            {
                DISPLAY_DEVICEW dd_mon;
                dd_mon.cb = sizeof(dd_mon);
                if (EnumDisplayDevicesW(dd.DeviceName, 0, &dd_mon, 0) &&
                    (dd_mon.StateFlags & DISPLAY_DEVICE_ACTIVE))
                {
                    // We can't directly get HMONITOR from EnumDisplayDevices.
                    // Instead, use the desktop coordinates to find it.
                    DEVMODEW dm = {};
                    dm.dmSize = sizeof(dm);
                    if (EnumDisplaySettingsExW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm, 0)) {
                        POINT pt = { static_cast<LONG>(dm.dmPosition.x),
                                     static_cast<LONG>(dm.dmPosition.y) };
                        hmon = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
                    }
                }
            }

            // Use MonitorEnumProc to find exact HMONITOR match by comparing device name
            struct Ctx {
                const char* target_name;
                HMONITOR    result;
            } ctx = { device_name, nullptr };

            EnumDisplayMonitors(nullptr, nullptr,
                [](HMONITOR hMon, HDC, LPRECT, LPARAM lp) -> BOOL {
                    auto* c = reinterpret_cast<Ctx*>(lp);
                    MONITORINFOEXW mi;
                    mi.cbSize = sizeof(mi);
                    if (GetMonitorInfoW(hMon, &mi)) {
                        char mon_dev[128];
                        WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1,
                                            mon_dev, sizeof(mon_dev), nullptr, nullptr);
                        if (std::strcmp(mon_dev, c->target_name) == 0) {
                            c->result = hMon;
                            return FALSE; // stop enumeration
                        }
                    }
                    return TRUE;
                }, reinterpret_cast<LPARAM>(&ctx));

            if (ctx.result) {
                display_device_map_[device_name] = ctx.result;
            }
            if (hmon && !ctx.result) {
                display_device_map_[device_name] = hmon;
            }
        }
    }

    /**
     * Enumerate outputs for a single adapter.
     */
    void enumerate_outputs_on_adapter(IDXGIAdapter1* adapter, size_t adapter_idx) {
        Microsoft::WRL::ComPtr<IDXGIOutput> output;

        for (UINT output_idx = 0;
             adapter->EnumOutputs(output_idx, output.ReleaseAndGetAddressOf())
                 != DXGI_ERROR_NOT_FOUND;
             ++output_idx)
        {
            DXGI_OUTPUT_DESC out_desc = {};
            if (FAILED(output->GetDesc(&out_desc))) {
                continue;
            }

            // Skip outputs not attached to a desktop
            if (!out_desc.AttachedToDesktop) {
                continue;
            }

            DxgiOutputInfo info;
            info.output      = output;
            info.desc        = out_desc;
            info.adapter_idx = adapter_idx;
            info.output_idx  = output_idx;

            // Query optional interfaces
            output.As(&info.output1);
            output.As(&info.output5);

            // Look up HMONITOR from the display-device map
            std::string dev_name = info.device_name();
            auto it = display_device_map_.find(dev_name);
            if (it != display_device_map_.end()) {
                info.hmonitor = it->second;
                info.dpi      = get_monitor_dpi(it->second);
            } else {
                info.dpi = 96; // default
            }

            // Determine if this is the primary monitor
            info.is_primary = (out_desc.DesktopCoordinates.left == 0 &&
                               out_desc.DesktopCoordinates.top == 0);

            auto w = out_desc.DesktopCoordinates.right - out_desc.DesktopCoordinates.left;
            auto h = out_desc.DesktopCoordinates.bottom - out_desc.DesktopCoordinates.top;

            spdlog::info("[DXGI Engine]   Output {}.{}: {} ({}x{}) @ ({},{}) "
                         "DPI={} primary={} attached={}",
                         adapter_idx, output_idx,
                         dev_name, w, h,
                         out_desc.DesktopCoordinates.left,
                         out_desc.DesktopCoordinates.top,
                         info.dpi,
                         info.is_primary,
                         out_desc.AttachedToDesktop);

            outputs_.push_back(std::move(info));
        }
    }

    /**
     * Enumerate only outputs (without adapter re-enumeration).
     * Used for display-change detection.
     */
    bool enumerate_outputs_only(std::vector<DxgiOutputInfo>& out) {
        if (!dxgi_factory_) return false;

        out.clear();
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;

        for (UINT adapter_idx = 0;
             dxgi_factory_->EnumAdapters1(adapter_idx, adapter.ReleaseAndGetAddressOf())
                 != DXGI_ERROR_NOT_FOUND;
             ++adapter_idx)
        {
            Microsoft::WRL::ComPtr<IDXGIOutput> output;
            for (UINT output_idx = 0;
                 adapter->EnumOutputs(output_idx, output.ReleaseAndGetAddressOf())
                     != DXGI_ERROR_NOT_FOUND;
                 ++output_idx)
            {
                DXGI_OUTPUT_DESC out_desc = {};
                if (FAILED(output->GetDesc(&out_desc))) continue;
                if (!out_desc.AttachedToDesktop) continue;

                DxgiOutputInfo info;
                info.output      = output;
                info.desc        = out_desc;
                info.adapter_idx = adapter_idx;
                info.output_idx  = output_idx;
                output.As(&info.output1);
                output.As(&info.output5);
                out.push_back(std::move(info));
            }
        }
        return true;
    }

    // -----------------------------------------------------------------------
    //  Duplicator creation
    // -----------------------------------------------------------------------
    void create_duplicators() {
        duplicators_.clear();

        for (auto& out_info : outputs_) {
            auto dup = std::make_unique<DxgiDesktopDuplicator>(
                out_info, d3d_device_, config_.timeout_ms);

            if (dup->initialize()) {
                dup->clear_needs_reinit();
                duplicators_.push_back(std::move(dup));
            } else {
                spdlog::warn("[DXGI Engine] Failed to create duplicator for {}",
                             out_info.device_name());
            }
        }

        if (duplicators_.empty()) {
            spdlog::error("[DXGI Engine] No duplicators could be created!");
        }
    }

    // -----------------------------------------------------------------------
    //  Reinitialization (for display changes)
    // -----------------------------------------------------------------------
    void reinitialize_locked() {
        // Release old duplicators
        duplicators_.clear();

        // Re-enumerate
        adapters_.clear();
        outputs_.clear();
        enumerate_adapters_and_outputs();

        // Re-create duplicators
        create_duplicators();

        for (auto& dup : duplicators_) {
            dup->stats().reinit_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // -----------------------------------------------------------------------
    //  GDI fallback capture
    // -----------------------------------------------------------------------
    int capture_frame_gdi(std::vector<scrap::CaptureData>& frame_data) {
        GdiFallbackCapturer::GdiFrame gframe;
        if (!gdi_capturer_.capture(gframe)) {
            stats_gdi_.frames_attempted.fetch_add(1, std::memory_order_relaxed);
            stats_gdi_.frames_other_error.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }

        stats_gdi_.frames_attempted.fetch_add(1, std::memory_order_relaxed);
        stats_gdi_.frames_captured.fetch_add(1, std::memory_order_relaxed);
        stats_gdi_.frames_gdi_fallback.fetch_add(1, std::memory_order_relaxed);

        scrap::CaptureData cd;
        cd.width   = gframe.width;
        cd.height  = gframe.height;
        cd.pixels  = std::move(gframe.pixels);
        cd.stride  = gframe.stride;
        cd.left    = gframe.left;
        cd.top     = gframe.top;
        cd.dpi     = static_cast<int>(get_monitor_dpi(nullptr));
        cd.monitor = "GDI-fallback";

        frame_data.clear();
        frame_data.push_back(std::move(cd));
        return 1;
    }

    // -----------------------------------------------------------------------
    //  Data members
    // -----------------------------------------------------------------------
    Config config_;
    mutable std::mutex mutex_;

    // DXGI core
    Microsoft::WRL::ComPtr<IDXGIFactory2>     dxgi_factory_;
    Microsoft::WRL::ComPtr<ID3D11Device>      d3d_device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context_;

    // Enumeration
    std::vector<DxgiAdapterInfo>             adapters_;
    std::vector<DxgiOutputInfo>              outputs_;
    std::unordered_map<std::string, HMONITOR> display_device_map_;

    // Duplicators (one per output/monitor)
    std::vector<std::unique_ptr<DxgiDesktopDuplicator>> duplicators_;

    // GDI
    GdiFallbackCapturer gdi_capturer_;
    DxgiCaptureStats    stats_gdi_;

    // State
    bool initialized_        = false;
    bool using_gdi_fallback_ = false;
};

// ============================================================================
//  Public API bridge — functions expected by the rest of the scrap library
// ============================================================================

namespace {

/** Singleton engine instance with shared mutex for thread safety. */
std::shared_mutex           g_engine_mutex;
std::unique_ptr<DxgiCaptureEngine> g_engine;
DxgiCaptureEngine::Config   g_config;

/**
 * Access the engine (must hold shared_lock for reads, unique_lock for writes).
 */
DxgiCaptureEngine* engine_ptr() {
    return g_engine.get();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
//  scrap_dxgi_init  —  one-time initialization
// ---------------------------------------------------------------------------

bool scrap_dxgi_init(const DxgiCaptureEngine::Config* cfg) {
    std::unique_lock lock(g_engine_mutex);

    if (g_engine) {
        spdlog::warn("[scrap_dxgi] Already initialized");
        return true;
    }

    if (cfg) {
        g_config = *cfg;
    }

    g_engine = std::make_unique<DxgiCaptureEngine>(g_config);
    bool ok = g_engine->initialize();

    if (!ok) {
        spdlog::error("[scrap_dxgi] Initialization failed");
        g_engine.reset();
    } else if (g_engine->is_using_gdi_fallback()) {
        spdlog::warn("[scrap_dxgi] Initialized with GDI fallback — DXGI Desktop Duplication unavailable");
    }

    return ok;
}

// ---------------------------------------------------------------------------
//  scrap_dxgi_shutdown
// ---------------------------------------------------------------------------

void scrap_dxgi_shutdown() {
    std::unique_lock lock(g_engine_mutex);
    if (g_engine) {
        g_engine->shutdown();
        g_engine.reset();
        spdlog::info("[scrap_dxgi] Shutdown complete");
    }
}

// ---------------------------------------------------------------------------
//  scrap_dxgi_capture  —  capture one frame
// ---------------------------------------------------------------------------

int scrap_dxgi_capture(std::vector<scrap::CaptureData>& frame_data) {
    std::shared_lock lock(g_engine_mutex);

    auto* engine = engine_ptr();
    if (!engine) {
        spdlog::error("[scrap_dxgi] Engine not initialized; call scrap_dxgi_init() first");
        return -1;
    }

    return engine->capture_frame(frame_data);
}

// ---------------------------------------------------------------------------
//  scrap_dxgi_detect_display_change  —  check for monitor topology changes
// ---------------------------------------------------------------------------

bool scrap_dxgi_detect_display_change() {
    std::shared_lock lock(g_engine_mutex);

    auto* engine = engine_ptr();
    if (!engine) return false;

    return engine->detect_display_change();
}

// ---------------------------------------------------------------------------
//  scrap_dxgi_stats  —  human-readable stats string
// ---------------------------------------------------------------------------

std::string scrap_dxgi_stats() {
    std::shared_lock lock(g_engine_mutex);

    auto* engine = engine_ptr();
    if (!engine) return "[scrap_dxgi] Not initialized";

    return engine->stats_summary();
}

// ---------------------------------------------------------------------------
//  scrap_dxgi_is_gdi_fallback
// ---------------------------------------------------------------------------

bool scrap_dxgi_is_gdi_fallback() {
    std::shared_lock lock(g_engine_mutex);

    auto* engine = engine_ptr();
    if (!engine) return false;

    return engine->is_using_gdi_fallback();
}

// ---------------------------------------------------------------------------
//  scrap_dxgi_output_count  —  number of duplicators (monitors)
// ---------------------------------------------------------------------------

int scrap_dxgi_output_count() {
    std::shared_lock lock(g_engine_mutex);

    auto* engine = engine_ptr();
    if (!engine) return 0;

    return static_cast<int>(engine->duplicator_count());
}

// ---------------------------------------------------------------------------
//  scrap_dxgi_dpi_for_monitor  —  query DPI for a specific monitor
// ---------------------------------------------------------------------------

int scrap_dxgi_dpi_for_monitor(int monitor_index) {
    std::shared_lock lock(g_engine_mutex);

    auto* engine = engine_ptr();
    if (!engine) return 96;

    const auto& outputs = engine->outputs();
    if (monitor_index < 0 || static_cast<size_t>(monitor_index) >= outputs.size()) {
        return 96;
    }

    return static_cast<int>(outputs[static_cast<size_t>(monitor_index)].dpi);
}

// ============================================================================
//  Compatibility: free-standing functions for direct duplicator usage
//  (useful for tools that want per-monitor control without the engine)
// ============================================================================

/**
 * Create a standalone duplicator for a specific monitor index.
 * Caller owns the returned pointer and must call destroy_standalone_duplicator().
 */
DxgiDesktopDuplicator* scrap_dxgi_create_standalone(int monitor_index, int timeout_ms) {
    // Minimal standalone creation — create its own D3D11 device + factory
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory2),
                                     reinterpret_cast<void**>(factory.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) return nullptr;

    // Enumerate outputs to find the requested monitor
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    std::vector<DxgiOutputInfo> all_outputs;

    for (UINT adapter_idx = 0;
         factory->EnumAdapters1(adapter_idx, adapter.ReleaseAndGetAddressOf())
             != DXGI_ERROR_NOT_FOUND;
         ++adapter_idx)
    {
        Microsoft::WRL::ComPtr<IDXGIOutput> output;
        for (UINT out_idx = 0;
             adapter->EnumOutputs(out_idx, output.ReleaseAndGetAddressOf())
                 != DXGI_ERROR_NOT_FOUND;
             ++out_idx)
        {
            DXGI_OUTPUT_DESC desc = {};
            if (FAILED(output->GetDesc(&desc))) continue;
            if (!desc.AttachedToDesktop) continue;

            DxgiOutputInfo info;
            info.output      = output;
            info.desc        = desc;
            info.adapter_idx = adapter_idx;
            info.output_idx  = out_idx;
            output.As(&info.output1);
            output.As(&info.output5);
            all_outputs.push_back(std::move(info));
        }
    }

    if (monitor_index < 0 || static_cast<size_t>(monitor_index) >= all_outputs.size()) {
        spdlog::error("[scrap_dxgi] Standalone: invalid monitor index {}", monitor_index);
        return nullptr;
    }

    // Create D3D11 device
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL fl;

    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        device.ReleaseAndGetAddressOf(), &fl, context.ReleaseAndGetAddressOf());

    if (FAILED(hr)) {
        spdlog::error("[scrap_dxgi] Standalone: D3D11CreateDevice failed: {}",
                      hresult_str(hr));
        return nullptr;
    }

    auto dup = new DxgiDesktopDuplicator(
        std::move(all_outputs[static_cast<size_t>(monitor_index)]),
        device, timeout_ms);

    if (!dup->initialize()) {
        delete dup;
        return nullptr;
    }

    return dup;
}

void scrap_dxgi_destroy_standalone(DxgiDesktopDuplicator* dup) {
    delete dup;
}

// ============================================================================
//  HDR capture support (DXGI_FORMAT_R16G16B16A16_FLOAT, etc.)
// ============================================================================
namespace {

struct HdrMetadata {
    bool     valid          = false;
    float    max_luminance  = 1000.0f;
    float    min_luminance  = 0.01f;
    float    max_content_light_level   = 0.0f;
    float    max_frame_avg_light_level = 0.0f;
    DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
};

/**
 * Query HDR metadata from an IDXGIOutput6 (Windows 10 1803+).
 */
inline HdrMetadata query_hdr_metadata(IDXGIOutput* output) {
    HdrMetadata meta;

    Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
    if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput6),
                                       reinterpret_cast<void**>(output6.ReleaseAndGetAddressOf())))) {
        return meta; // not supported
    }

    DXGI_OUTPUT_DESC1 desc1 = {};
    if (SUCCEEDED(output6->GetDesc1(&desc1))) {
        meta.color_space      = desc1.ColorSpace;
        meta.max_luminance    = desc1.MaxLuminance;
        meta.min_luminance    = desc1.MinLuminance;
        meta.max_content_light_level   = desc1.MaxFullFrameLuminance;
        meta.max_frame_avg_light_level = desc1.MaxAverageFullFrameLuminance;
        meta.valid = true;

        spdlog::info("[HDR] ColorSpace={} MaxLuminance={:.1f} MinLuminance={:.4f} "
                     "MaxCLL={:.1f} MaxFALL={:.1f}",
                     static_cast<int>(meta.color_space),
                     meta.max_luminance, meta.min_luminance,
                     meta.max_content_light_level,
                     meta.max_frame_avg_light_level);
    }

    return meta;
}

/**
 * Check if the current output is driving an HDR display mode.
 */
inline bool is_hdr_active(IDXGIOutput* output) {
    Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
    if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput6),
                                       reinterpret_cast<void**>(output6.ReleaseAndGetAddressOf())))) {
        return false;
    }
    DXGI_OUTPUT_DESC1 desc1 = {};
    if (FAILED(output6->GetDesc1(&desc1))) return false;

    return desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
           desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 ||
           desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020 ||
           desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020;
}

} // anonymous namespace

// ============================================================================
//  Move-rect merging: compute the union of move rects with dirty rects
//  to minimize over-capture when only moved regions are dirty.
// ============================================================================

namespace {

class DirtyRectAccumulator {
public:
    /**
     * Add a dirty rect to the accumulator.  If a move rect is provided
     * then the source rect of the move is also treated as dirty.
     */
    void add_dirty(const RECT& r) {
        if (rect_is_empty(r)) return;
        std::lock_guard<std::mutex> lk(mtx_);
        rects_.push_back(r);
    }

    void add_move(const DXGI_OUTDUPL_MOVE_RECT& mr) {
        // Both the source and destination regions need to be redrawn
        RECT src;
        src.left   = static_cast<LONG>(mr.SourcePoint.x);
        src.top    = static_cast<LONG>(mr.SourcePoint.y);
        src.right  = src.left + static_cast<LONG>(mr.DestinationRect.right  - mr.DestinationRect.left);
        src.bottom = src.top  + static_cast<LONG>(mr.DestinationRect.bottom - mr.DestinationRect.top);

        RECT dst;
        dst.left   = static_cast<LONG>(mr.DestinationRect.left);
        dst.top    = static_cast<LONG>(mr.DestinationRect.top);
        dst.right  = static_cast<LONG>(mr.DestinationRect.right);
        dst.bottom = static_cast<LONG>(mr.DestinationRect.bottom);

        std::lock_guard<std::mutex> lk(mtx_);
        rects_.push_back(src);
        rects_.push_back(dst);
    }

    /**
     * Merge all accumulated rects into a minimal set (greedy union).
     * This keeps the rect count reasonable for consumers.
     */
    std::vector<RECT> merge_and_clear(int max_rects = 64) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (rects_.empty()) return {};

        // Start with a single union rect that encloses everything
        RECT bounding = rects_[0];
        for (size_t i = 1; i < rects_.size(); ++i) {
            bounding.left   = (std::min)(bounding.left,   rects_[i].left);
            bounding.top    = (std::min)(bounding.top,    rects_[i].top);
            bounding.right  = (std::max)(bounding.right,  rects_[i].right);
            bounding.bottom = (std::max)(bounding.bottom, rects_[i].bottom);
        }

        std::vector<RECT> result;
        result.push_back(bounding);
        rects_.clear();
        return result;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(mtx_);
        rects_.clear();
    }

    size_t count() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return rects_.size();
    }

private:
    static bool rect_is_empty(const RECT& r) {
        return r.left >= r.right || r.top >= r.bottom;
    }

    mutable std::mutex mtx_;
    std::vector<RECT>   rects_;
};

} // anonymous namespace

// ============================================================================
//  High-resolution frame timing via QueryPerformanceCounter
// ============================================================================

namespace {

/**
 * RAII timer that records wall-clock and QPC at construction and logs at
 * destruction.  Useful for per-frame performance diagnostics.
 */
class QpcTimer {
public:
    QpcTimer() {
        QueryPerformanceFrequency(&freq_);
        reset();
    }

    void reset() {
        QueryPerformanceCounter(&start_);
    }

    /** Elapsed time in microseconds. */
    int64_t elapsed_us() const {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return (now.QuadPart - start_.QuadPart) * 1000000LL / freq_.QuadPart;
    }

    /** Elapsed time in milliseconds (double precision). */
    double elapsed_ms() const {
        return elapsed_us() / 1000.0;
    }

    static int64_t frequency() {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }

private:
    LARGE_INTEGER freq_{};
    LARGE_INTEGER start_{};
};

} // anonymous namespace

// ============================================================================
//  Cursor-shape processor — converts DXGI pointer buffers into RGBA pixels
// ============================================================================

namespace {

/**
 * Process the raw buffer returned by GetFramePointerShape into usable
 * RGBA pixels suitable for compositing over a desktop image.
 *
 * DXGI supports three pointer shape types:
 *   - Monochrome: 1bpp AND mask followed by 1bpp XOR mask
 *   - Color:      DXGI_FORMAT_B8G8R8A8_UNORM pixels
 *   - MaskedColor: 32bpp color + 1bpp AND mask
 */
class CursorShapeProcessor {
public:
    struct CursorRGBA {
        std::vector<uint8_t> pixels;  // RGBA rows, top-down
        UINT                 width  = 0;
        UINT                 height = 0;
        UINT                 stride = 0;
        int                  hot_x  = 0;
        int                  hot_y  = 0;
    };

    /**
     * Convert a DXGI cursor shape buffer to RGBA pixels.
     */
    static bool process(const DXGI_OUTDUPL_POINTER_SHAPE_INFO& info,
                         const uint8_t* buffer, UINT buffer_size,
                         CursorRGBA& out)
    {
        out.width  = info.Width;
        out.height = info.Height;
        out.hot_x  = info.HotSpot.x;
        out.hot_y  = info.HotSpot.y;

        switch (info.Type) {
        case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME:
            return process_monochrome(info, buffer, buffer_size, out);
        case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:
            return process_color(info, buffer, buffer_size, out);
        case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR:
            return process_masked_color(info, buffer, buffer_size, out);
        default:
            spdlog::warn("[Cursor] Unknown cursor shape type: {}", info.Type);
            return false;
        }
    }

private:
    /** Monochrome cursor: AND mask (top half) + XOR mask (bottom half). */
    static bool process_monochrome(const DXGI_OUTDUPL_POINTER_SHAPE_INFO& info,
                                    const uint8_t* buffer, UINT /*buffer_size*/,
                                    CursorRGBA& out)
    {
        UINT w = info.Width;
        UINT h = info.Height / 2; // two masks stacked
        UINT pitch = info.Pitch;

        if (pitch == 0) pitch = ((w + 31) / 32) * 4; // 1 bpp → 4-byte aligned

        out.width  = w;
        out.height = h;
        out.stride = w * 4;
        out.pixels.resize(out.stride * h);

        // AND mask (top): 1 = opaque, 0 = transparent
        // XOR mask (bottom): 1 = inverted, 0 = unchanged
        //
        // Result:
        //   AND=1, XOR=1 → inverted (not used by Windows, treated as black)
        //   AND=1, XOR=0 → black
        //   AND=0, XOR=1 → white
        //   AND=0, XOR=0 → transparent

        for (UINT y = 0; y < h; ++y) {
            const uint8_t* and_row = buffer + y * pitch;
            const uint8_t* xor_row = buffer + (h + y) * pitch;
            uint8_t* rgba_row      = out.pixels.data() + y * out.stride;

            for (UINT x = 0; x < w; ++x) {
                uint8_t and_bit = (and_row[x / 8] >> (7 - (x & 7))) & 1;
                uint8_t xor_bit = (xor_row[x / 8] >> (7 - (x & 7))) & 1;

                size_t px = static_cast<size_t>(x) * 4;
                if (and_bit == 0 && xor_bit == 0) {
                    rgba_row[px + 0] = 0;
                    rgba_row[px + 1] = 0;
                    rgba_row[px + 2] = 0;
                    rgba_row[px + 3] = 0;   // transparent
                } else if (and_bit == 0 && xor_bit == 1) {
                    rgba_row[px + 0] = 255;
                    rgba_row[px + 1] = 255;
                    rgba_row[px + 2] = 255;
                    rgba_row[px + 3] = 255; // white
                } else if (and_bit == 1 && xor_bit == 0) {
                    rgba_row[px + 0] = 0;
                    rgba_row[px + 1] = 0;
                    rgba_row[px + 2] = 0;
                    rgba_row[px + 3] = 255; // black
                } else {
                    rgba_row[px + 0] = 255;
                    rgba_row[px + 1] = 255;
                    rgba_row[px + 2] = 255;
                    rgba_row[px + 3] = 255; // inverted (treat as white)
                }
            }
        }
        return true;
    }

    /** Color cursor: straight BGRA pixels (already premultiplied by DXGI). */
    static bool process_color(const DXGI_OUTDUPL_POINTER_SHAPE_INFO& info,
                               const uint8_t* buffer, UINT /*buffer_size*/,
                               CursorRGBA& out)
    {
        UINT w = info.Width;
        UINT h = info.Height;
        UINT pitch = info.Pitch;
        if (pitch == 0) pitch = w * 4;

        out.width  = w;
        out.height = h;
        out.stride = w * 4;
        out.pixels.resize(out.stride * h);

        for (UINT y = 0; y < h; ++y) {
            const uint8_t* src_row = buffer + y * pitch;
            uint8_t* dst_row = out.pixels.data() + y * out.stride;
            // BGRA → RGBA
            bgra_to_rgba_simd(dst_row, src_row, w);
        }
        return true;
    }

    /** Masked color cursor: 32bpp color (top) + 1bpp mask (bottom). */
    static bool process_masked_color(const DXGI_OUTDUPL_POINTER_SHAPE_INFO& info,
                                      const uint8_t* buffer, UINT /*buffer_size*/,
                                      CursorRGBA& out)
    {
        UINT w = info.Width;
        UINT h = info.Height;

        // The mask follows the color pixels; pitch for the mask
        UINT color_pitch = info.Pitch;
        if (color_pitch == 0) color_pitch = w * 4;

        UINT mask_pitch = ((w + 31) / 32) * 4;
        // Mask starts after all color rows
        const uint8_t* mask_start = buffer + color_pitch * h;

        out.width  = w;
        out.height = h;
        out.stride = w * 4;
        out.pixels.resize(out.stride * h);

        for (UINT y = 0; y < h; ++y) {
            const uint8_t* color_row = buffer + y * color_pitch;
            const uint8_t* mask_row  = mask_start + y * mask_pitch;
            uint8_t* dst_row         = out.pixels.data() + y * out.stride;

            for (UINT x = 0; x < w; ++x) {
                size_t px = static_cast<size_t>(x) * 4;
                uint8_t mask_bit = (mask_row[x / 8] >> (7 - (x & 7))) & 1;
                if (mask_bit == 1) {
                    // AND mask bit set → opaque, use color
                    dst_row[px + 0] = color_row[px + 2]; // R
                    dst_row[px + 1] = color_row[px + 1]; // G
                    dst_row[px + 2] = color_row[px + 0]; // B
                    dst_row[px + 3] = color_row[px + 3]; // A
                } else {
                    // AND mask bit clear → transparent
                    dst_row[px + 0] = 0;
                    dst_row[px + 1] = 0;
                    dst_row[px + 2] = 0;
                    dst_row[px + 3] = 0;
                }
            }
        }
        return true;
    }
};

} // anonymous namespace

// ============================================================================
//  D3D11 Texture Pool — recycle staging textures to avoid per-frame allocation
// ============================================================================

namespace {

class D3D11TexturePool {
public:
    struct PooledTexture {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        D3D11_TEXTURE2D_DESC                     desc = {};
        int64_t                                  last_used_us = 0;
    };

    explicit D3D11TexturePool(Microsoft::WRL::ComPtr<ID3D11Device> device)
        : device_(std::move(device)) {}

    /**
     * Acquire a texture matching the given descriptor.  Reuses an existing
     * pooled texture if available; otherwise creates a new one.
     */
    Microsoft::WRL::ComPtr<ID3D11Texture2D> acquire(const D3D11_TEXTURE2D_DESC& desc) {
        std::lock_guard<std::mutex> lk(mutex_);

        // Search for exact match
        for (auto it = pool_.begin(); it != pool_.end(); ++it) {
            if (texture_desc_equal(it->desc, desc)) {
                auto tex = it->texture;
                pool_.erase(it);
                spdlog::debug("[TexPool] Reused texture {}x{} fmt={}",
                              desc.Width, desc.Height,
                              dxgi_format_name(desc.Format));
                return tex;
            }
        }

        // Create new
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        HRESULT hr = device_->CreateTexture2D(&desc, nullptr, tex.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            spdlog::error("[TexPool] CreateTexture2D failed: {}", hresult_str(hr));
            return nullptr;
        }

        total_created_++;
        spdlog::debug("[TexPool] Created texture {}x{} fmt={} (total={})",
                      desc.Width, desc.Height,
                      dxgi_format_name(desc.Format),
                      total_created_);
        return tex;
    }

    /**
     * Return a texture to the pool for future reuse.
     */
    void release(Microsoft::WRL::ComPtr<ID3D11Texture2D> tex) {
        if (!tex) return;
        D3D11_TEXTURE2D_DESC desc = {};
        tex->GetDesc(&desc);

        PooledTexture pt;
        pt.texture      = std::move(tex);
        pt.desc         = desc;
        pt.last_used_us = now_us();

        std::lock_guard<std::mutex> lk(mutex_);
        pool_.push_back(std::move(pt));

        // Limit pool size
        while (pool_.size() > max_pool_size_) {
            pool_.erase(pool_.begin());
        }
    }

    /** Evict textures older than `max_age_us` microseconds. */
    void evict_older_than(int64_t max_age_us) {
        std::lock_guard<std::mutex> lk(mutex_);
        int64_t now = now_us();
        pool_.erase(
            std::remove_if(pool_.begin(), pool_.end(),
                           [&](const PooledTexture& pt) {
                               return (now - pt.last_used_us) > max_age_us;
                           }),
            pool_.end());
    }

    void clear() {
        std::lock_guard<std::mutex> lk(mutex_);
        pool_.clear();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return pool_.size();
    }

    size_t total_created() const { return total_created_; }

private:
    static bool texture_desc_equal(const D3D11_TEXTURE2D_DESC& a,
                                   const D3D11_TEXTURE2D_DESC& b) {
        return a.Width            == b.Width
            && a.Height           == b.Height
            && a.MipLevels        == b.MipLevels
            && a.ArraySize        == b.ArraySize
            && a.Format           == b.Format
            && a.SampleDesc.Count == b.SampleDesc.Count
            && a.Usage            == b.Usage
            && a.BindFlags        == b.BindFlags
            && a.CPUAccessFlags   == b.CPUAccessFlags
            && a.MiscFlags        == b.MiscFlags;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    mutable std::mutex                   mutex_;
    std::vector<PooledTexture>           pool_;
    std::atomic<size_t>                  total_created_{0};
    static constexpr size_t              max_pool_size_ = 16;
};

} // anonymous namespace

// ============================================================================
//  Per-monitor parallel capture using a thread pool
// ============================================================================

namespace {

/**
 * Simple thread pool that captures multiple monitors in parallel.
 * Each worker calls acquire_frame() on its duplicator and copies to staging.
 */
class ParallelCapturePool {
public:
    struct Slot {
        DxgiDesktopDuplicator* duplicator = nullptr;
        bool                   success    = false;
    };

    explicit ParallelCapturePool(size_t num_threads = 0) {
        if (num_threads == 0) {
            num_threads = std::thread::hardware_concurrency();
            if (num_threads == 0) num_threads = 2;
        }
        workers_.reserve(num_threads);
    }

    /**
     * Capture from all given duplicators in parallel.
     * Returns the number that succeeded.
     */
    int capture_all(std::vector<DxgiDesktopDuplicator*>& dups) {
        if (dups.empty()) return 0;

        slots_.clear();
        slots_.resize(dups.size());
        for (size_t i = 0; i < dups.size(); ++i) {
            slots_[i].duplicator = dups[i];
            slots_[i].success    = false;
        }

        // Use std::async for simplicity; a production system might use a
        // persistent thread pool.
        std::vector<std::future<void>> futures;
        futures.reserve(dups.size());

        for (size_t i = 0; i < dups.size(); ++i) {
            futures.push_back(std::async(std::launch::async, [this, i]() {
                auto& slot = slots_[i];
                slot.success = slot.duplicator->acquire_frame();
            }));
        }

        // Wait for all to complete
        for (auto& f : futures) {
            f.get();
        }

        int success_count = 0;
        for (const auto& s : slots_) {
            if (s.success) ++success_count;
        }
        return success_count;
    }

private:
    std::vector<std::thread> workers_;
    std::vector<Slot>       slots_;
};

} // anonymous namespace

// ============================================================================
//  DXGI debug layer helpers (for development/debugging)
// ============================================================================

namespace {

/**
 * Enable the D3D11 debug layer and break-on-error.
 * Only meaningful in debug builds.
 */
inline bool enable_d3d11_debug_layer() {
#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D11Debug> debug;
    Microsoft::WRL::ComPtr<ID3D11Device> temp_dev;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> temp_ctx;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_DEBUG,
        nullptr, 0, D3D11_SDK_VERSION,
        temp_dev.ReleaseAndGetAddressOf(), nullptr,
        temp_ctx.ReleaseAndGetAddressOf());

    if (SUCCEEDED(hr) && temp_dev) {
        spdlog::info("[DXGI Debug] D3D11 debug layer enabled");
        return true;
    }

    spdlog::warn("[DXGI Debug] D3D11 debug layer not available: {}", hresult_str(hr));
    return false;
#else
    return false;
#endif
}

/**
 * Enable DXGI debug interface (requires dxgidebug.dll / DXGIGetDebugInterface).
 */
inline bool enable_dxgi_debug_layer() {
    HMODULE dxgi_debug = LoadLibraryW(L"dxgidebug.dll");
    if (!dxgi_debug) {
        spdlog::warn("[DXGI Debug] dxgidebug.dll not available");
        return false;
    }

    using DXGIGetDebugInterfaceFn = HRESULT(WINAPI*)(REFIID, void**);
    auto pDXGIGetDebugInterface = reinterpret_cast<DXGIGetDebugInterfaceFn>(
        GetProcAddress(dxgi_debug, "DXGIGetDebugInterface"));

    if (!pDXGIGetDebugInterface) {
        spdlog::warn("[DXGI Debug] DXGIGetDebugInterface not available");
        FreeLibrary(dxgi_debug);
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIInfoQueue> info_queue;
    HRESULT hr = pDXGIGetDebugInterface(
        __uuidof(IDXGIInfoQueue),
        reinterpret_cast<void**>(info_queue.ReleaseAndGetAddressOf()));

    if (SUCCEEDED(hr) && info_queue) {
        spdlog::info("[DXGI Debug] DXGI debug layer enabled");
        return true;
    }

    spdlog::warn("[DXGI Debug] IDXGIInfoQueue not available: {}", hresult_str(hr));
    FreeLibrary(dxgi_debug);
    return false;
}

} // anonymous namespace

// ============================================================================
//  Enhanced error recovery routines
// ============================================================================

namespace {

/**
 * Attempt to reset a duplicator after DXGI_ERROR_ACCESS_LOST.
 *
 * Access lost is the standard DXGI error when the desktop composition
 * changes (e.g., resolution change, full-screen exclusive switch, UAC prompt).
 * The correct recovery path is to release the duplicator and recreate it.
 */
inline bool recover_from_access_lost(DxgiDesktopDuplicator& dup) {
    spdlog::info("[Recovery] Attempting access-lost recovery for {}", dup.dev_name());

    // Small backoff delay to let the composition settle
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    dup.release();
    // The duplicator will be re-initialized by the engine on next capture
    dup.clear_needs_reinit();
    return true;
}

/**
 * Test if the current session is on the "secure desktop" (e.g., UAC prompt,
 * Ctrl+Alt+Del screen). Desktop Duplication will fail on secure desktops.
 */
inline bool is_secure_desktop() {
    HDESK hDesk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!hDesk) return false;

    wchar_t name[256];
    DWORD len = 0;
    BOOL ok = GetUserObjectInformationW(hDesk, UOI_NAME, name, sizeof(name), &len);
    CloseDesktop(hDesk);

    if (!ok) return false;

    // Winlogon desktop = secure
    return _wcsicmp(name, L"Winlogon") == 0;
}

/**
 * Check if the current session is locked (cannot duplicate).
 */
inline bool is_session_locked() {
    HDESK hDesk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!hDesk) return true;

    wchar_t name[256];
    DWORD len = 0;
    BOOL ok = GetUserObjectInformationW(hDesk, UOI_NAME, name, sizeof(name), &len);
    CloseDesktop(hDesk);

    if (!ok) return true;

    // Default desktop = unlocked
    return _wcsicmp(name, L"Default") != 0;
}

/**
 * Comprehensive duplicator health check.
 */
inline bool check_duplicator_health(DxgiDesktopDuplicator& dup) {
    if (dup.needs_reinit()) return false;
    if (is_secure_desktop()) {
        spdlog::debug("[Health] Secure desktop active, deferring capture");
        return false;
    }
    if (is_session_locked()) {
        spdlog::debug("[Health] Session locked, deferring capture");
        return false;
    }
    return true;
}

} // anonymous namespace

// ============================================================================
//  Multi-monitor geometry helpers
// ============================================================================

namespace {

/**
 * Given a point in virtual screen coordinates, find which DXGI output
 * contains it.
 */
inline int find_output_at_point(int x, int y,
                                 const std::vector<DxgiOutputInfo>& outputs)
{
    for (size_t i = 0; i < outputs.size(); ++i) {
        const auto& rc = outputs[i].desc.DesktopCoordinates;
        if (x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/**
 * Compute the bounding rectangle of all outputs.
 */
inline RECT bounding_rect_of_outputs(const std::vector<DxgiOutputInfo>& outputs) {
    if (outputs.empty()) return {0, 0, 0, 0};

    RECT r = outputs[0].desc.DesktopCoordinates;
    for (size_t i = 1; i < outputs.size(); ++i) {
        const auto& rc = outputs[i].desc.DesktopCoordinates;
        r.left   = (std::min)(r.left,   rc.left);
        r.top    = (std::min)(r.top,    rc.top);
        r.right  = (std::max)(r.right,  rc.right);
        r.bottom = (std::max)(r.bottom, rc.bottom);
    }
    return r;
}

/**
 * Check if two output rectangles overlap (unusual but possible with
 * mirrored displays).
 */
inline bool outputs_overlap(const DxgiOutputInfo& a, const DxgiOutputInfo& b) {
    const auto& ra = a.desc.DesktopCoordinates;
    const auto& rb = b.desc.DesktopCoordinates;
    return ra.left < rb.right && ra.right > rb.left &&
           ra.top  < rb.bottom && ra.bottom > rb.top;
}

} // anonymous namespace

// ============================================================================
//  Shared texture handle export (for IPC with other processes)
// ============================================================================

namespace {

/**
 * Create a shared NT handle for a D3D11 texture that can be imported
 * by another D3D11 device (in the same or different process).
 *
 * This is useful for zero-copy sharing of captured frames with an
 * encoder or renderer process.
 */
inline HANDLE export_shared_texture_handle(ID3D11Device* device,
                                            ID3D11Texture2D* texture)
{
    if (!device || !texture) return nullptr;

    Microsoft::WRL::ComPtr<IDXGIResource> dxgi_resource;
    HRESULT hr = texture->QueryInterface(__uuidof(IDXGIResource),
                                          reinterpret_cast<void**>(
                                              dxgi_resource.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        spdlog::error("[SharedTex] QI for IDXGIResource failed: {}", hresult_str(hr));
        return nullptr;
    }

    HANDLE shared_handle = nullptr;
    hr = dxgi_resource->GetSharedHandle(&shared_handle);
    if (FAILED(hr)) {
        // Try CreateSharedHandle if the texture was not created with
        // D3D11_RESOURCE_MISC_SHARED / SHARED_NTHANDLE
        Microsoft::WRL::ComPtr<IDXGIResource1> dxgi_res1;
        hr = texture->QueryInterface(__uuidof(IDXGIResource1),
                                      reinterpret_cast<void**>(
                                          dxgi_res1.ReleaseAndGetAddressOf()));
        if (SUCCEEDED(hr)) {
            hr = dxgi_res1->CreateSharedHandle(
                nullptr,
                DXGI_SHARED_RESOURCE_READ,
                nullptr,
                &shared_handle);
        }
    }

    if (FAILED(hr) || !shared_handle) {
        spdlog::error("[SharedTex] Get/Create shared handle failed: {}", hresult_str(hr));
        return nullptr;
    }

    spdlog::debug("[SharedTex] Exported shared handle 0x{:X}",
                  reinterpret_cast<uintptr_t>(shared_handle));
    return shared_handle;
}

} // anonymous namespace

// ============================================================================
//  RGBA → BGRA conversion (reverse direction) for sending data to D3D
// ============================================================================

namespace {

inline void rgba_to_bgra_scalar(uint8_t* __restrict dst,
                                 const uint8_t* __restrict src,
                                 size_t pixel_count) {
    for (size_t i = 0; i < pixel_count; ++i) {
        const size_t off = i * 4;
        dst[off + 0] = src[off + 2];  // B ← R
        dst[off + 1] = src[off + 1];  // G ← G
        dst[off + 2] = src[off + 0];  // R ← B
        dst[off + 3] = src[off + 3];  // A ← A
    }
}

#ifdef __SSE2__
inline void rgba_to_bgra_sse2(uint8_t* __restrict dst,
                               const uint8_t* __restrict src,
                               size_t pixel_count) {
    // Same shuffle mask as BGRA→RGBA — it's symmetric
    const __m128i shuffle_mask =
        _mm_setr_epi8(2, 1, 0, 3,  6, 5, 4, 7,  10, 9, 8, 11,  14, 13, 12, 15);

    size_t simd_count = pixel_count / 4;
    const __m128i* src128 = reinterpret_cast<const __m128i*>(src);
    __m128i*       dst128 = reinterpret_cast<__m128i*>(dst);

    for (size_t i = 0; i < simd_count; ++i) {
        __m128i pixel = _mm_loadu_si128(src128 + i);
        pixel = _mm_shuffle_epi8(pixel, shuffle_mask);
        _mm_storeu_si128(dst128 + i, pixel);
    }

    size_t remaining = pixel_count % 4;
    if (remaining) {
        rgba_to_bgra_scalar(dst + simd_count * 16,
                             src + simd_count * 16,
                             remaining);
    }
}
#endif

#ifdef __AVX2__
inline void rgba_to_bgra_avx2(uint8_t* __restrict dst,
                               const uint8_t* __restrict src,
                               size_t pixel_count) {
    const __m256i shuffle_mask =
        _mm256_setr_epi8(
            2, 1, 0, 3,  6, 5, 4, 7,  10, 9, 8, 11,  14, 13, 12, 15,
            2, 1, 0, 3,  6, 5, 4, 7,  10, 9, 8, 11,  14, 13, 12, 15);

    size_t simd_count = pixel_count / 8;
    const __m256i* src256 = reinterpret_cast<const __m256i*>(src);
    __m256i*       dst256 = reinterpret_cast<__m256i*>(dst);

    for (size_t i = 0; i < simd_count; ++i) {
        __m256i pixel = _mm256_loadu_si256(src256 + i);
        pixel = _mm256_shuffle_epi8(pixel, shuffle_mask);
        _mm256_storeu_si256(dst256 + i, pixel);
    }

    size_t remaining = pixel_count % 8;
    if (remaining) {
#ifdef __SSE2__
        rgba_to_bgra_sse2(dst + simd_count * 32,
                           src + simd_count * 32,
                           remaining);
#else
        rgba_to_bgra_scalar(dst + simd_count * 32,
                             src + simd_count * 32,
                             remaining);
#endif
    }
}
#endif

inline void rgba_to_bgra_simd(uint8_t* __restrict dst,
                               const uint8_t* __restrict src,
                               size_t pixel_count) {
#if defined(__AVX2__)
    rgba_to_bgra_avx2(dst, src, pixel_count);
#elif defined(__SSE2__)
    rgba_to_bgra_sse2(dst, src, pixel_count);
#else
    rgba_to_bgra_scalar(dst, src, pixel_count);
#endif
}

} // anonymous namespace

// ============================================================================
//  Performance counters & GPU timing queries
// ============================================================================

namespace {

/**
 * GPU timestamp query using D3D11 disjoint + timestamp queries.
 * Allows measuring exact GPU time for the CopyResource operation.
 */
class GpuTimer {
public:
    explicit GpuTimer(Microsoft::WRL::ComPtr<ID3D11Device> device)
        : device_(std::move(device)) {}

    bool initialize() {
        if (initialized_) return true;

        // Check for timer support
        D3D11_FEATURE_DATA_D3D11_OPTIONS opts = {};
        HRESULT hr = device_->CheckFeatureSupport(
            D3D11_FEATURE_D3D11_OPTIONS, &opts, sizeof(opts));
        if (FAILED(hr) || !opts.TimestampAndTimerSupport) {
            spdlog::warn("[GPUTimer] Timestamp queries not supported");
            return false;
        }

        D3D11_QUERY_DESC qdesc = {};
        qdesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        hr = device_->CreateQuery(&qdesc, disjoint_query_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        qdesc.Query = D3D11_QUERY_TIMESTAMP;
        hr = device_->CreateQuery(&qdesc, start_query_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;
        hr = device_->CreateQuery(&qdesc, end_query_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return false;

        initialized_ = true;
        return true;
    }

    void begin(ID3D11DeviceContext* ctx) {
        if (!initialized_ || !ctx) return;
        ctx->Begin(disjoint_query_.Get());
        ctx->End(start_query_.Get());
    }

    /** End timing and return elapsed GPU time in microseconds, or -1 on error. */
    double end(ID3D11DeviceContext* ctx) {
        if (!initialized_ || !ctx) return -1.0;

        ctx->End(end_query_.Get());
        ctx->End(disjoint_query_.Get());

        // Wait for data
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
        while (ctx->GetData(disjoint_query_.Get(), &disjoint, sizeof(disjoint), 0) == S_FALSE) {
            std::this_thread::yield();
        }

        if (disjoint.Disjoint) {
            spdlog::debug("[GPUTimer] Disjoint sample — discarding");
            return -1.0;
        }

        UINT64 start_ts = 0, end_ts = 0;
        while (ctx->GetData(start_query_.Get(), &start_ts, sizeof(start_ts), 0) == S_FALSE) {
            std::this_thread::yield();
        }
        while (ctx->GetData(end_query_.Get(), &end_ts, sizeof(end_ts), 0) == S_FALSE) {
            std::this_thread::yield();
        }

        if (end_ts <= start_ts) return 0.0;

        double elapsed_us = static_cast<double>(end_ts - start_ts) /
                            static_cast<double>(disjoint.Frequency) * 1000000.0;
        return elapsed_us;
    }

private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11Query> disjoint_query_;
    Microsoft::WRL::ComPtr<ID3D11Query> start_query_;
    Microsoft::WRL::ComPtr<ID3D11Query> end_query_;
    bool initialized_ = false;
};

} // anonymous namespace

// ============================================================================
//  Full-frame validation (checksum / hash for detecting unchanged frames)
// ============================================================================

namespace {

/**
 * Fast 64-bit hash of a pixel buffer (used to detect no-change frames).
 * Uses FNV-1a with early exit if enough bytes differ.
 */
inline uint64_t frame_hash_fnv1a(const uint8_t* data, size_t len) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

/**
 * Check if two frame buffers are identical (memcmp with early exit).
 */
inline bool frames_identical(const uint8_t* a, const uint8_t* b, size_t len) {
    return std::memcmp(a, b, len) == 0;
}

/**
 * Compute the percentage of pixels that differ between two frames.
 * Useful for adaptive quality / throttling.
 */
inline double frame_difference_percent(const uint8_t* a, const uint8_t* b,
                                        size_t pixel_count) {
    if (pixel_count == 0) return 0.0;
    size_t diff_count = 0;
    // Sample every 4th pixel for performance (skip alpha)
    for (size_t i = 0; i < pixel_count; i += 4) {
        size_t off = i * 4;
        // Compare R, G, B; ignore A
        if (a[off] != b[off] || a[off+1] != b[off+1] || a[off+2] != b[off+2]) {
            ++diff_count;
        }
    }
    return 100.0 * static_cast<double>(diff_count) / (pixel_count / 4);
}

} // anonymous namespace

} // namespace scrap

// ============================================================================
//  Windows-specific helper: get last error as string (for diagnostics)
// ============================================================================
namespace scrap::detail {

std::string windows_last_error_string() {
    DWORD err = GetLastError();
    if (err == 0) return "no error";
    char* msgBuf = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&msgBuf), 0, nullptr);
    std::string result(msgBuf ? msgBuf : "unknown error");
    if (msgBuf) LocalFree(msgBuf);
    // Trim trailing newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    return result;
}

} // namespace scrap::detail

#endif // _WIN32
