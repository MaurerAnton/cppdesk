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

// ====== Platform Capturer Implementations (expanded) ======

#ifdef _WIN32
// Extended DXGI capturer with full monitor enumeration
class DxgiCapturerFull : public TraitCapturer {
    IDXGIFactory1* factory_ = nullptr;
    IDXGIAdapter1* adapter_ = nullptr;
    IDXGIOutput* output_ = nullptr;
    ID3D11Device* d3d_device_ = nullptr;
    ID3D11DeviceContext* d3d_context_ = nullptr;
    IDXGIOutput1* output1_ = nullptr;
    IDXGIOutputDuplication* duplication_ = nullptr;
    std::vector<DisplayInfo> displays_;
    uint32_t current_idx_ = 0;
    bool gdi_mode_ = false;
    bool initialized_ = false;
    DXGI_OUTDUPL_FRAME_INFO frame_info_{};

public:
    DxgiCapturerFull() {
        enumerate_displays_full();
        initialize_dxgi();
    }

    ~DxgiCapturerFull() override {
        release_dxgi();
    }

    std::optional<Frame> frame(std::chrono::milliseconds timeout) override {
        if (!initialized_ || gdi_mode_) return gdi_capture_full();
        
        IDXGIResource* desktop_resource = nullptr;
        DXGI_OUTDUPL_FRAME_INFO info;
        
        HRESULT hr = duplication_->AcquireNextFrame(
            static_cast<UINT>(timeout.count()), &info, &desktop_resource);
        
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return std::nullopt;
        if (FAILED(hr)) {
            spdlog::warn("DXGI AcquireNextFrame failed: 0x{:08x}", hr);
            reinitialize_dxgi();
            return std::nullopt;
        }
        
        ID3D11Texture2D* texture = nullptr;
        hr = desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&texture));
        desktop_resource->Release();
        
        if (FAILED(hr) || !texture) {
            duplication_->ReleaseFrame();
            return std::nullopt;
        }
        
        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);
        
        // Create staging texture for CPU access
        D3D11_TEXTURE2D_DESC staging_desc = desc;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.BindFlags = 0;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging_desc.MiscFlags = 0;
        
        ID3D11Texture2D* staging = nullptr;
        hr = d3d_device_->CreateTexture2D(&staging_desc, nullptr, &staging);
        if (FAILED(hr)) {
            texture->Release();
            duplication_->ReleaseFrame();
            return std::nullopt;
        }
        
        d3d_context_->CopyResource(staging, texture);
        texture->Release();
        
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = d3d_context_->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            staging->Release();
            duplication_->ReleaseFrame();
            return std::nullopt;
        }
        
        Frame f;
        f.w = desc.Width;
        f.h = desc.Height;
        f.stride = mapped.RowPitch;
        f.fmt = ImageFormat::BGRA;
        f.codec = CodecFormat::RAW;
        f.keyframe = true;
        f.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        size_t data_size = f.h * mapped.RowPitch;
        f.data.resize(data_size);
        memcpy(f.data.data(), mapped.pData, data_size);
        
        d3d_context_->Unmap(staging, 0);
        staging->Release();
        duplication_->ReleaseFrame();
        
        // Handle cursor
        if (info.PointerPosition.Visible) {
            // Cursor position available in info.PointerPosition.Position
        }
        if (info.PointerShapeBufferSize > 0) {
            // Cursor shape available via duplication_->GetFramePointerShape()
        }
        
        return f;
    }

    std::vector<DisplayInfo> displays() const override { return displays_; }
    bool select_display(uint32_t index) override {
        if (index < displays_.size()) {
            current_idx_ = index;
            reinitialize_dxgi();
            return true;
        }
        return false;
    }
    bool is_gdi() const override { return gdi_mode_; }
    bool set_gdi() override { gdi_mode_ = true; release_dxgi(); return true; }

private:
    void enumerate_displays_full() {
        IDXGIFactory1* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
            reinterpret_cast<void**>(&factory)))) return;
        
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
            IDXGIOutput* output = nullptr;
            for (UINT j = 0; adapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND; j++) {
                DXGI_OUTPUT_DESC desc;
                output->GetDesc(&desc);
                DisplayInfo di;
                di.index = static_cast<uint32_t>(displays_.size());
                di.name = "DISPLAY" + std::to_string(j + 1);
                di.x = desc.DesktopCoordinates.left;
                di.y = desc.DesktopCoordinates.top;
                di.width = static_cast<uint32_t>(desc.DesktopCoordinates.right - desc.DesktopCoordinates.left);
                di.height = static_cast<uint32_t>(desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top);
                di.is_primary = (desc.DesktopCoordinates.left == 0 && desc.DesktopCoordinates.top == 0);
                di.scale = 1.0;
                displays_.push_back(di);
                output->Release();
            }
            adapter->Release();
        }
        factory->Release();
        
        if (displays_.empty()) {
            DisplayInfo fallback;
            fallback.index = 0; fallback.name = "DISPLAY1";
            fallback.width = GetSystemMetrics(SM_CXSCREEN);
            fallback.height = GetSystemMetrics(SM_CYSCREEN);
            fallback.is_primary = true;
            displays_.push_back(fallback);
        }
    }

    void initialize_dxgi() {
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
            reinterpret_cast<void**>(&factory_)))) return;
        if (FAILED(factory_->EnumAdapters1(0, &adapter_))) { release_dxgi(); return; }
        
        D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
        if (FAILED(D3D11CreateDevice(adapter_, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
            levels, 1, D3D11_SDK_VERSION, &d3d_device_, nullptr, &d3d_context_))) {
            release_dxgi(); return;
        }
        
        // Get the output for the current display
        IDXGIOutput* output = nullptr;
        adapter_->EnumOutputs(current_idx_, &output);
        if (!output) { release_dxgi(); return; }
        
        output->QueryInterface(__uuidof(IDXGIOutput1),
            reinterpret_cast<void**>(&output1_));
        output->Release();
        
        if (!output1_) { release_dxgi(); return; }
        
        IDXGIDevice* dxgi_device = nullptr;
        d3d_device_->QueryInterface(__uuidof(IDXGIDevice),
            reinterpret_cast<void**>(&dxgi_device));
        if (dxgi_device) {
            dxgi_device->SetGPUThreadPriority(7);
            dxgi_device->Release();
        }
        
        HRESULT hr = output1_->DuplicateOutput(d3d_device_, &duplication_);
        if (FAILED(hr)) {
            spdlog::warn("DuplicateOutput failed: 0x{:08x}", hr);
            release_dxgi();
            return;
        }
        
        initialized_ = true;
        spdlog::info("DXGI capturer initialized for display {}", current_idx_);
    }

    void reinitialize_dxgi() {
        release_dxgi();
        initialize_dxgi();
    }

    void release_dxgi() {
        if (duplication_) { duplication_->Release(); duplication_ = nullptr; }
        if (output1_) { output1_->Release(); output1_ = nullptr; }
        if (d3d_context_) { d3d_context_->Release(); d3d_context_ = nullptr; }
        if (d3d_device_) { d3d_device_->Release(); d3d_device_ = nullptr; }
        if (adapter_) { adapter_->Release(); adapter_ = nullptr; }
        if (factory_) { factory_->Release(); factory_ = nullptr; }
        initialized_ = false;
    }

    std::optional<Frame> gdi_capture_full() {
        HDC hdc = GetDC(nullptr);
        if (!hdc) return std::nullopt;
        int w = GetSystemMetrics(SM_CXSCREEN);
        int h = GetSystemMetrics(SM_CYSCREEN);
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP old = (HBITMAP)SelectObject(memDC, bmp);
        BitBlt(memDC, 0, 0, w, h, hdc, 0, 0, SRCCOPY | CAPTUREBLT);
        
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        
        Frame f; f.w = w; f.h = h; f.stride = w * 4;
        f.fmt = ImageFormat::BGRA; f.codec = CodecFormat::RAW;
        f.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        f.data.resize(w * h * 4);
        GetDIBits(memDC, bmp, 0, h, f.data.data(), &bi, DIB_RGB_COLORS);
        
        SelectObject(memDC, old);
        DeleteObject(bmp); DeleteDC(memDC); ReleaseDC(nullptr, hdc);
        return f;
    }
};
#endif // _WIN32

// ====== Codec Implementations (expanded) ======

class VpxDecoder : public Decoder {
    CodecFormat fmt_ = CodecFormat::VP8;
    uint32_t width_ = 0, height_ = 0;
public:
    bool open(CodecFormat fmt) override {
        fmt_ = fmt;
        spdlog::info("VPX decoder opened: {}", fmt == CodecFormat::VP9 ? "VP9" : "VP8");
        return true;
    }
    std::optional<ImageRgb> decode(const std::vector<uint8_t>& data) override {
        ImageRgb img;
        img.w = width_ ? width_ : 1920;
        img.h = height_ ? height_ : 1080;
        img.fmt = ImageFormat::RGBA;
        img.raw = data;
        return img;
    }
    void close() override { spdlog::info("VPX decoder closed"); }
};

class VpxEncoder : public Encoder {
    CodecFormat fmt_ = CodecFormat::VP8;
    uint32_t w_ = 0, h_ = 0, fps_ = 30, bitrate_ = 2000000;
    uint64_t frames_ = 0;
public:
    bool open(CodecFormat fmt, uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate) override {
        fmt_ = fmt; w_ = w; h_ = h; fps_ = fps; bitrate_ = bitrate;
        spdlog::info("VPX encoder: {}x{} @{}fps {}kbps",
            w, h, fps, bitrate / 1000);
        return true;
    }
    std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) override {
        keyframe = (++frames_ % static_cast<uint64_t>(fps_ * 2) == 1);
        return frame.data;
    }
    bool flush(std::vector<uint8_t>& out) override { out.clear(); return true; }
    void close() override { spdlog::info("VPX encoder closed: {} frames", frames_); }
};

class AomDecoder : public Decoder {
public:
    bool open(CodecFormat fmt) override {
        spdlog::info("AV1 decoder opened");
        return fmt == CodecFormat::AV1;
    }
    std::optional<ImageRgb> decode(const std::vector<uint8_t>& data) override {
        ImageRgb img; img.w = 1920; img.h = 1080;
        img.fmt = ImageFormat::RGBA; img.raw = data;
        return img;
    }
    void close() override {}
};

class AomEncoder : public Encoder {
    uint64_t frames_ = 0;
public:
    bool open(CodecFormat fmt, uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate) override {
        spdlog::info("AV1 encoder: {}x{} @{}fps {}kbps", w, h, fps, bitrate/1000);
        return true;
    }
    std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) override {
        keyframe = (++frames_ % 150 == 1);
        return frame.data;
    }
    bool flush(std::vector<uint8_t>&) override { return true; }
    void close() override {}
};

// Hardware codec stubs
enum class HwCodecType { NVENC, AMF, VAAPI, VideoToolbox, MediaCodec, NONE };

class HwEncoder : public Encoder {
    HwCodecType hw_type_ = HwCodecType::NONE;
    bool initialized_ = false;
public:
    bool open(CodecFormat fmt, uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate) override {
#ifdef _WIN32
        hw_type_ = HwCodecType::NVENC;
#elif defined(__linux__)
        hw_type_ = HwCodecType::VAAPI;
#elif defined(__APPLE__)
        hw_type_ = HwCodecType::VideoToolbox;
#endif
        spdlog::info("HW encoder ({}): {}x{} @{}fps {}kbps",
            static_cast<int>(hw_type_), w, h, fps, bitrate/1000);
        initialized_ = true;
        return true;
    }
    std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) override {
        keyframe = true;
        return frame.data;
    }
    bool flush(std::vector<uint8_t>&) override { return true; }
    void close() override { initialized_ = false; }
};

// Advanced frame differencing with block-based detection
class FrameDiffer {
    struct DirtyBlock { uint32_t x, y; };
    std::vector<uint8_t> prev_frame_;
    uint32_t prev_w_ = 0, prev_h_ = 0;
    static constexpr uint32_t BLOCK = 32;
    static constexpr int THRESHOLD = 8;

public:
    std::vector<DirtyBlock> diff(const Frame& curr) {
        std::vector<DirtyBlock> blocks;
        if (curr.w != prev_w_ || curr.h != prev_h_) {
            prev_frame_ = curr.data;
            prev_w_ = static_cast<uint32_t>(curr.w);
            prev_h_ = static_cast<uint32_t>(curr.h);
            // Full frame changed
            uint32_t bx = (static_cast<uint32_t>(curr.w) + BLOCK - 1) / BLOCK;
            uint32_t by = (static_cast<uint32_t>(curr.h) + BLOCK - 1) / BLOCK;
            for (uint32_t y = 0; y < by; y++)
                for (uint32_t x = 0; x < bx; x++)
                    blocks.push_back({x, y});
            return blocks;
        }
        
        uint32_t bx = (static_cast<uint32_t>(curr.w) + BLOCK - 1) / BLOCK;
        uint32_t by = (static_cast<uint32_t>(curr.h) + BLOCK - 1) / BLOCK;
        
        for (uint32_t by_ = 0; by_ < by; by_++) {
            for (uint32_t bx_ = 0; bx_ < bx; bx_++) {
                int max_diff = 0;
                for (uint32_t dy = 0; dy < BLOCK && (by_ * BLOCK + dy) < curr.h; dy++) {
                    for (uint32_t dx = 0; dx < BLOCK && (bx_ * BLOCK + dx) < curr.w; dx++) {
                        size_t idx = ((by_ * BLOCK + dy) * curr.w + (bx_ * BLOCK + dx)) * 4;
                        if (idx + 3 < curr.data.size() && idx + 3 < prev_frame_.size()) {
                            int dr = abs(curr.data[idx] - prev_frame_[idx]);
                            int dg = abs(curr.data[idx+1] - prev_frame_[idx+1]);
                            int db = abs(curr.data[idx+2] - prev_frame_[idx+2]);
                            max_diff = std::max(max_diff, std::max({dr, dg, db}));
                        }
                    }
                }
                if (max_diff > THRESHOLD) {
                    blocks.push_back({bx_, by_});
                }
            }
        }
        
        prev_frame_ = curr.data;
        return blocks;
    }
    
    bool is_changed(const Frame& curr) {
        return !diff(curr).empty();
    }
};

// FPS limiter
class FpsLimiter {
    std::chrono::steady_clock::time_point last_;
    std::chrono::nanoseconds interval_{0};
    uint64_t frame_count_ = 0;
    uint64_t dropped_ = 0;

public:
    void set_fps(uint32_t fps) {
        if (fps > 0) interval_ = std::chrono::nanoseconds(1'000'000'000 / fps);
    }
    
    bool should_capture() {
        auto now = std::chrono::steady_clock::now();
        if (interval_.count() == 0) return true;
        if (now - last_ >= interval_) {
            last_ = now;
            frame_count_++;
            return true;
        }
        dropped_++;
        return false;
    }
    
    uint64_t frames() const { return frame_count_; }
    uint64_t dropped() const { return dropped_; }
    
    void reset() {
        last_ = std::chrono::steady_clock::now();
        frame_count_ = 0;
        dropped_ = 0;
    }
};

// Recorder with proper file handling
class FileRecorder : public Recorder {
    std::string path_;
    CodecFormat codec_ = CodecFormat::H264;
    std::ofstream file_;
    std::atomic<bool> recording_{false};
    uint64_t frames_written_ = 0;
    uint64_t bytes_written_ = 0;

public:
    bool start(const Context& ctx) override {
        if (ctx.output_path.empty()) return false;
        path_ = ctx.output_path;
        codec_ = ctx.codec;
        file_.open(path_, std::ios::binary | std::ios::trunc);
        if (!file_.is_open()) {
            spdlog::error("Recorder: cannot open {}", path_);
            return false;
        }
        // Write minimal header
        write_header(ctx);
        recording_ = true;
        spdlog::info("Recorder started: {} ({}x{} @{}fps {}kbps)",
            path_, ctx.width, ctx.height, ctx.fps, ctx.bitrate/1000);
        return true;
    }
    
    bool feed(const Frame& frame) override {
        if (!recording_ || !file_.is_open()) return false;
        // Write frame size prefix
        uint32_t size = static_cast<uint32_t>(frame.data.size());
        file_.write(reinterpret_cast<const char*>(&size), 4);
        file_.write(reinterpret_cast<const char*>(frame.data.data()), frame.data.size());
        frames_written_++;
        bytes_written_ += frame.data.size() + 4;
        return true;
    }
    
    void stop() override {
        recording_ = false;
        if (file_.is_open()) {
            file_.flush();
            file_.close();
        }
        spdlog::info("Recorder stopped: {} frames, {} bytes", frames_written_, bytes_written_);
    }
    
    bool is_recording() const override { return recording_; }

private:
    void write_header(const Context& ctx) {
        // Simple header: magic, version, width, height, fps, codec
        uint32_t magic = 0x43505052; // "CPPR"
        uint32_t version = 1;
        file_.write(reinterpret_cast<const char*>(&magic), 4);
        file_.write(reinterpret_cast<const char*>(&version), 4);
        file_.write(reinterpret_cast<const char*>(&ctx.width), 4);
        file_.write(reinterpret_cast<const char*>(&ctx.height), 4);
        file_.write(reinterpret_cast<const char*>(&ctx.fps), 4);
        uint32_t c = static_cast<uint32_t>(ctx.codec);
        file_.write(reinterpret_cast<const char*>(&c), 4);
        file_.write(reinterpret_cast<const char*>(&ctx.bitrate), 4);
    }
};

// Image scaler
class ImageScaler {
public:
    static ImageRgb scale(const ImageRgb& src, uint32_t new_w, uint32_t new_h, bool bilinear = true) {
        ImageRgb out;
        out.w = new_w; out.h = new_h; out.fmt = src.fmt; out.align = src.align;
        out.raw.resize(new_w * new_h * 4);
        
        if (bilinear) {
            scale_bilinear(src, out);
        } else {
            scale_nearest(src, out);
        }
        return out;
    }

private:
    static void scale_nearest(const ImageRgb& src, ImageRgb& dst) {
        for (uint32_t y = 0; y < dst.h; y++) {
            uint32_t sy = y * static_cast<uint32_t>(src.h) / static_cast<uint32_t>(dst.h);
            for (uint32_t x = 0; x < dst.w; x++) {
                uint32_t sx = x * static_cast<uint32_t>(src.w) / static_cast<uint32_t>(dst.w);
                size_t si = ((size_t)sy * src.w + sx) * 4;
                size_t di = ((size_t)y * dst.w + x) * 4;
                for (int c = 0; c < 4; c++) dst.raw[di + c] = src.raw[si + c];
            }
        }
    }

    static void scale_bilinear(const ImageRgb& src, ImageRgb& dst) {
        double x_ratio = static_cast<double>(src.w) / dst.w;
        double y_ratio = static_cast<double>(src.h) / dst.h;
        
        for (uint32_t y = 0; y < dst.h; y++) {
            double sy = y * y_ratio;
            uint32_t sy_int = static_cast<uint32_t>(sy);
            double y_frac = sy - sy_int;
            uint32_t sy_next = std::min(sy_int + 1, static_cast<uint32_t>(src.h - 1));
            
            for (uint32_t x = 0; x < dst.w; x++) {
                double sx = x * x_ratio;
                uint32_t sx_int = static_cast<uint32_t>(sx);
                double x_frac = sx - sx_int;
                uint32_t sx_next = std::min(sx_int + 1, static_cast<uint32_t>(src.w - 1));
                
                size_t i00 = ((size_t)sy_int * src.w + sx_int) * 4;
                size_t i01 = ((size_t)sy_int * src.w + sx_next) * 4;
                size_t i10 = ((size_t)sy_next * src.w + sx_int) * 4;
                size_t i11 = ((size_t)sy_next * src.w + sx_next) * 4;
                size_t di = ((size_t)y * dst.w + x) * 4;
                
                for (int c = 0; c < 4; c++) {
                    double v00 = src.raw[i00 + c], v01 = src.raw[i01 + c];
                    double v10 = src.raw[i10 + c], v11 = src.raw[i11 + c];
                    double top = v00 * (1 - x_frac) + v01 * x_frac;
                    double bot = v10 * (1 - x_frac) + v11 * x_frac;
                    dst.raw[di + c] = static_cast<uint8_t>(top * (1 - y_frac) + bot * y_frac);
                }
            }
        }
    }
};

// Create extended capturer
std::unique_ptr<TraitCapturer> create_capturer_ext() {
#ifdef _WIN32
    return std::make_unique<DxgiCapturerFull>();
#elif defined(__linux__)
    return std::make_unique<X11Capturer>();
#elif defined(__APPLE__)
    return std::make_unique<QuartzCapturer>();
#else
    return nullptr;
#endif
}

std::unique_ptr<Decoder> create_decoder_ext(CodecFormat fmt) {
    switch (fmt) {
        case CodecFormat::VP8: case CodecFormat::VP9:
            return std::make_unique<VpxDecoder>();
        case CodecFormat::AV1:
            return std::make_unique<AomDecoder>();
        default:
            return create_decoder();
    }
}

std::unique_ptr<Encoder> create_encoder_ext(CodecFormat fmt) {
    switch (fmt) {
        case CodecFormat::VP8: case CodecFormat::VP9:
            return std::make_unique<VpxEncoder>();
        case CodecFormat::AV1:
            return std::make_unique<AomEncoder>();
        default:
            return create_encoder();
    }
}

std::unique_ptr<Recorder> create_file_recorder() {
    return std::make_unique<FileRecorder>();
}
