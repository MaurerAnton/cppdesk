#include "scrap/scrap.hpp"
#include <spdlog/spdlog.h>
#include <cstring>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <fstream>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreVideo/CoreVideo.h>
#endif

namespace scrap {

// ====== Utilities ======

bool would_block_if_equal(std::vector<uint8_t>& old, const std::vector<uint8_t>& b) {
    if (b == old) return false;
    old.assign(b.begin(), b.end());
    return true;
}

bool is_x11() {
#ifdef __linux__
    return getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY");
#else
    return false;
#endif
}

bool is_wayland() {
    return getenv("WAYLAND_DISPLAY") != nullptr;
}

bool is_dxgi_available() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool is_quartz_available() {
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}

// ====== Format Conversion ======

ImageRgb convert_format(const ImageRgb& src, ImageFormat target) {
    if (src.fmt == target) return src;
    ImageRgb out;
    out.w = src.w; out.h = src.h; out.fmt = target; out.align = src.align;
    out.raw.resize(src.w * src.h * 4);

    if (src.fmt == ImageFormat::BGRA && target == ImageFormat::RGBA) {
        for (size_t i = 0; i < src.w * src.h; i++) {
            out.raw[i*4+0] = src.raw[i*4+2];
            out.raw[i*4+1] = src.raw[i*4+1];
            out.raw[i*4+2] = src.raw[i*4+0];
            out.raw[i*4+3] = src.raw[i*4+3];
        }
    } else if (src.fmt == ImageFormat::ARGB && target == ImageFormat::RGBA) {
        for (size_t i = 0; i < src.w * src.h; i++) {
            out.raw[i*4+0] = src.raw[i*4+1];
            out.raw[i*4+1] = src.raw[i*4+2];
            out.raw[i*4+2] = src.raw[i*4+3];
            out.raw[i*4+3] = src.raw[i*4+0];
        }
    } else {
        std::copy(src.raw.begin(), src.raw.end(), out.raw.begin());
    }
    return out;
}

ImageRgb convert_bgra_to_rgba(const ImageRgb& src) {
    return convert_format(src, ImageFormat::RGBA);
}

ImageRgb convert_yuv_to_rgba(const std::vector<uint8_t>& yuv,
    uint32_t w, uint32_t h) {
    ImageRgb out;
    out.w = w; out.h = h; out.fmt = ImageFormat::RGBA; out.align = 1;
    out.raw.resize(w * h * 4);
    // BT.601 YUV to RGB
    for (uint32_t i = 0; i < w * h; i++) {
        int Y = yuv[i];
        int U = yuv[w * h + i / 4] - 128;
        int V = yuv[w * h + w * h / 4 + i / 4] - 128;
        int R = Y + 1.402 * V;
        int G = Y - 0.344 * U - 0.714 * V;
        int B = Y + 1.772 * U;
        out.raw[i*4+0] = static_cast<uint8_t>(std::clamp(R, 0, 255));
        out.raw[i*4+1] = static_cast<uint8_t>(std::clamp(G, 0, 255));
        out.raw[i*4+2] = static_cast<uint8_t>(std::clamp(B, 0, 255));
        out.raw[i*4+3] = 255;
    }
    return out;
}

// ====== Windows DXGI Capturer ======
#ifdef _WIN32
class DxgiCapturer : public TraitCapturer {
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGIOutputDuplication* duplication_ = nullptr;
    std::vector<DisplayInfo> displays_;
    uint32_t current_display_ = 0;
    bool gdi_mode_ = false;

public:
    DxgiCapturer() {
        enumerate_displays();
    }

    ~DxgiCapturer() override {
        if (duplication_) duplication_->Release();
        if (context_) context_->Release();
        if (device_) device_->Release();
    }

    std::optional<Frame> frame(std::chrono::milliseconds timeout) override {
        if (!duplication_ || gdi_mode_) return gdi_capture();
        Frame f;
        f.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        f.fmt = ImageFormat::BGRA;
        f.codec = CodecFormat::RAW;
        if (current_display_ < displays_.size()) {
            f.w = displays_[current_display_].width;
            f.h = displays_[current_display_].height;
            f.stride = f.w * 4;
            f.data.resize(f.w * f.h * 4, 0);
        }
        return f;
    }

    bool is_gdi() const override { return gdi_mode_; }
    bool set_gdi() override { gdi_mode_ = true; return true; }

    std::vector<DisplayInfo> displays() const override { return displays_; }

    bool select_display(uint32_t index) override {
        if (index < displays_.size()) { current_display_ = index; return true; }
        return false;
    }

private:
    void enumerate_displays() {
        DisplayInfo di;
        di.index = 0; di.name = "DISPLAY1";
        di.width = GetSystemMetrics(SM_CXSCREEN);
        di.height = GetSystemMetrics(SM_CYSCREEN);
        di.is_primary = true;
        displays_.push_back(di);
    }

    std::optional<Frame> gdi_capture() {
        HDC hdc = GetDC(nullptr);
        if (!hdc) return std::nullopt;
        Frame f;
        f.w = GetSystemMetrics(SM_CXSCREEN);
        f.h = GetSystemMetrics(SM_CYSCREEN);
        f.stride = f.w * 4;
        f.data.resize(f.w * f.h * 4);
        f.fmt = ImageFormat::BGRA;
        f.codec = CodecFormat::RAW;
        f.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, static_cast<int>(f.w), static_cast<int>(f.h));
        SelectObject(hdcMem, hbm);
        BitBlt(hdcMem, 0, 0, static_cast<int>(f.w), static_cast<int>(f.h), hdc, 0, 0, SRCCOPY);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = static_cast<int>(f.w);
        bi.bmiHeader.biHeight = -static_cast<int>(f.h);
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        GetDIBits(hdcMem, hbm, 0, static_cast<int>(f.h), f.data.data(), &bi, DIB_RGB_COLORS);
        DeleteObject(hbm); DeleteDC(hdcMem); ReleaseDC(nullptr, hdc);
        return f;
    }
};
#endif

// ====== Linux X11 Capturer ======
#ifdef __linux__
class X11Capturer : public TraitCapturer {
    Display* display_ = nullptr;
    std::vector<DisplayInfo> displays_;
    uint32_t current_display_ = 0;

public:
    X11Capturer() {
        display_ = XOpenDisplay(nullptr);
        enumerate_displays();
    }
    ~X11Capturer() override { if (display_) XCloseDisplay(display_); }

    std::optional<Frame> frame(std::chrono::milliseconds timeout) override {
        if (!display_) return std::nullopt;
        Window root = DefaultRootWindow(display_);
        XWindowAttributes attrs;
        XGetWindowAttributes(display_, root, &attrs);
        XImage* img = XGetImage(display_, root, 0, 0, attrs.width, attrs.height,
            AllPlanes, ZPixmap);
        if (!img) return std::nullopt;
        Frame f;
        f.w = static_cast<size_t>(attrs.width);
        f.h = static_cast<size_t>(attrs.height);
        f.stride = f.w * 4;
        f.fmt = ImageFormat::BGRA;
        f.codec = CodecFormat::RAW;
        f.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        f.data.resize(f.w * f.h * 4);
        for (size_t y = 0; y < f.h; y++)
            for (size_t x = 0; x < f.w; x++) {
                uint32_t p = XGetPixel(img, static_cast<int>(x), static_cast<int>(y));
                size_t i = (y * f.w + x) * 4;
                f.data[i+0] = p & 0xFF; f.data[i+1] = (p>>8)&0xFF;
                f.data[i+2] = (p>>16)&0xFF; f.data[i+3] = 0xFF;
            }
        XDestroyImage(img);
        return f;
    }

    std::vector<DisplayInfo> displays() const override { return displays_; }
    bool select_display(uint32_t index) override {
        if (index < displays_.size()) { current_display_ = index; return true; }
        return false;
    }

private:
    void enumerate_displays() {
        if (!display_) return;
        int screen = DefaultScreen(display_);
        Screen* scr = ScreenOfDisplay(display_, screen);
        DisplayInfo di; di.index = 0; di.name = ":0";
        di.width = static_cast<uint32_t>(WidthOfScreen(scr));
        di.height = static_cast<uint32_t>(HeightOfScreen(scr));
        di.is_primary = true;
        displays_.push_back(di);
    }
};
#endif

// ====== macOS Quartz Capturer ======
#ifdef __APPLE__
class QuartzCapturer : public TraitCapturer {
    std::vector<DisplayInfo> displays_;
    uint32_t current_display_ = 0;

public:
    QuartzCapturer() { enumerate_displays(); }

    std::optional<Frame> frame(std::chrono::milliseconds timeout) override {
        CGImageRef img = CGDisplayCreateImage(CGMainDisplayID());
        if (!img) return std::nullopt;
        Frame f;
        f.w = static_cast<size_t>(CGImageGetWidth(img));
        f.h = static_cast<size_t>(CGImageGetHeight(img));
        f.stride = f.w * 4;
        f.fmt = ImageFormat::BGRA;
        f.codec = CodecFormat::RAW;
        f.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        f.data.resize(f.w * f.h * 4);
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(f.data.data(), f.w, f.h, 8, f.stride, cs,
            kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
        CGContextDrawImage(ctx, CGRectMake(0, 0, f.w, f.h), img);
        CGContextRelease(ctx); CGColorSpaceRelease(cs); CGImageRelease(img);
        return f;
    }

    std::vector<DisplayInfo> displays() const override { return displays_; }
    bool select_display(uint32_t index) override {
        if (index < displays_.size()) { current_display_ = index; return true; }
        return false;
    }

private:
    void enumerate_displays() {
        DisplayInfo di; di.index = 0; di.name = "Built-in Display";
        auto mid = CGMainDisplayID();
        di.width = static_cast<uint32_t>(CGDisplayPixelsWide(mid));
        di.height = static_cast<uint32_t>(CGDisplayPixelsHigh(mid));
        di.is_primary = true;
        displays_.push_back(di);
    }
};
#endif

// ====== Factory ======

std::unique_ptr<TraitCapturer> create_capturer() {
#ifdef _WIN32
    return std::make_unique<DxgiCapturer>();
#elif defined(__linux__)
    return std::make_unique<X11Capturer>();
#elif defined(__APPLE__)
    return std::make_unique<QuartzCapturer>();
#else
    return nullptr;
#endif
}

// ====== Simple Decoder ======
class SimpleDecoder : public Decoder {
    CodecFormat fmt_ = CodecFormat::RAW;
public:
    bool open(CodecFormat fmt) override { fmt_ = fmt; return true; }
    std::optional<ImageRgb> decode(const std::vector<uint8_t>& data) override {
        ImageRgb img; img.raw = data; img.w = 1920; img.h = 1080;
        img.fmt = ImageFormat::RGBA; return img;
    }
    void close() override {}
};

// ====== Simple Encoder ======
class SimpleEncoder : public Encoder {
    CodecFormat fmt_ = CodecFormat::H264;
    uint32_t w_ = 1920, h_ = 1080;
    uint32_t frame_count_ = 0;
public:
    bool open(CodecFormat fmt, uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate) override {
        fmt_ = fmt; w_ = w; h_ = h;
        spdlog::info("Encoder: {}x{} @{}fps {}bps", w, h, fps, bitrate);
        return true;
    }
    std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) override {
        keyframe = (++frame_count_ % 60 == 0);
        return frame.data;
    }
    bool flush(std::vector<uint8_t>&) override { return true; }
    void close() override {}
};

// ====== Recorder ======
class SimpleRecorder : public Recorder {
    std::atomic<bool> recording_{false};
    std::ofstream file_;
public:
    bool start(const Context& ctx) override {
        if (ctx.output_path.empty()) return false;
        file_.open(ctx.output_path, std::ios::binary);
        recording_ = file_.is_open();
        return recording_;
    }
    bool feed(const Frame& frame) override {
        if (!recording_) return false;
        file_.write(reinterpret_cast<const char*>(frame.data.data()), frame.data.size());
        return true;
    }
    void stop() override { recording_ = false; file_.close(); }
    bool is_recording() const override { return recording_; }
};

// ====== Camera ======
class SimpleCamera : public Camera {
    uint32_t index_ = 0;
public:
    bool open(uint32_t index) override { index_ = index; spdlog::info("Camera {} opened", index); return true; }
    std::optional<Frame> capture() override {
        Frame f; f.w = 640; f.h = 480; f.fmt = ImageFormat::RGBA;
        f.data.resize(640*480*4, 128); return f;
    }
    void close() override {}
    std::vector<std::string> list_cameras() override { return {"Default Camera"}; }
};

std::unique_ptr<Decoder> create_decoder() { return std::make_unique<SimpleDecoder>(); }
std::unique_ptr<Encoder> create_encoder() { return std::make_unique<SimpleEncoder>(); }
std::unique_ptr<Recorder> create_recorder() { return std::make_unique<SimpleRecorder>(); }
std::unique_ptr<Camera> create_camera() { return std::make_unique<SimpleCamera>(); }

} // namespace scrap
