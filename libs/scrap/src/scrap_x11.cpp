// scrap_x11.cpp — Linux X11 + macOS Quartz screen capture
#include "scrap/scrap.hpp"
#include <spdlog/spdlog.h>
#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>
#include <vector>
#include <mutex>

#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xfixes.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#endif

#ifdef __APPLE__
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <IOSurface/IOSurface.h>
#import <AppKit/AppKit.h>
#endif

namespace scrap {

// ===== Linux X11 Capturer =====
#ifdef __linux__

class X11ShmCapturer : public TraitCapturer {
    Display* display_ = nullptr;
    std::vector<DisplayInfo> displays_;
    uint32_t current_ = 0;
    bool use_shm_ = true;
    bool initialized_ = false;

    // SHM state
    XShmSegmentInfo shm_info_{};
    XImage* shm_image_ = nullptr;

    // Non-SHM fallback
    XImage* fallback_image_ = nullptr;

public:
    X11ShmCapturer() {
        XInitThreads();
        display_ = XOpenDisplay(nullptr);
        if (!display_) {
            spdlog::error("X11ShmCapturer: failed to open display");
            return;
        }
        use_shm_ = XShmQueryExtension(display_);
        enumerate_displays();
        initialized_ = true;
        spdlog::info("X11 SHM capturer ready (SHM: {})", use_shm_);
    }

    ~X11ShmCapturer() override {
        cleanup_images();
        if (display_) XCloseDisplay(display_);
    }

    std::optional<Frame> frame(std::chrono::milliseconds timeout) override {
        if (!initialized_ || !display_) return std::nullopt;
        if (current_ >= displays_.size()) return std::nullopt;

        auto& di = displays_[current_];
        Window root = RootWindow(display_, DefaultScreen(display_));

        if (use_shm_) {
            return capture_shm(root, di);
        } else {
            return capture_fallback(root, di);
        }
    }

    std::vector<DisplayInfo> displays() const override { return displays_; }

    bool select_display(uint32_t idx) override {
        if (idx < displays_.size()) { current_ = idx; return true; }
        return false;
    }

    bool is_gdi() const override { return !use_shm_; }
    bool set_gdi() override { use_shm_ = false; cleanup_images(); return true; }

private:
    std::optional<Frame> capture_shm(Window root, const DisplayInfo& di) {
        if (!shm_image_) {
            create_shm_image(di);
        }
        if (!shm_image_) return capture_fallback(root, di);

        XShmGetImage(display_, root, shm_image_, di.x, di.y, AllPlanes);

        Frame f;
        f.w = static_cast<size_t>(di.width);
        f.h = static_cast<size_t>(di.height);
        f.stride = shm_image_->bytes_per_line;
        f.fmt = ImageFormat::BGRA;
        f.codec = CodecFormat::RAW;
        f.keyframe = true;
        f.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        size_t data_size = f.h * f.stride;
        f.data.resize(data_size);
        memcpy(f.data.data(), shm_image_->data, data_size);

        return f;
    }

    std::optional<Frame> capture_fallback(Window root, const DisplayInfo& di) {
        XImage* img = XGetImage(display_, root, di.x, di.y, di.width, di.height,
            AllPlanes, ZPixmap);
        if (!img) return std::nullopt;

        Frame f;
        f.w = static_cast<size_t>(di.width);
        f.h = static_cast<size_t>(di.height);
        f.stride = img->bytes_per_line;
        f.fmt = ImageFormat::BGRA;
        f.codec = CodecFormat::RAW;
        f.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        size_t row_size = f.w * 4;
        f.data.resize(f.h * row_size);
        for (size_t y = 0; y < f.h; y++) {
            memcpy(f.data.data() + y * row_size,
                reinterpret_cast<char*>(img->data) + y * f.stride, row_size);
        }

        XDestroyImage(img);
        return f;
    }

    void create_shm_image(const DisplayInfo& di) {
        int screen = DefaultScreen(display_);
        Visual* visual = DefaultVisual(display_, screen);
        int depth = DefaultDepth(display_, screen);

        shm_image_ = XShmCreateImage(display_, visual, depth, ZPixmap, nullptr,
            &shm_info_, di.width, di.height);
        if (!shm_image_) { use_shm_ = false; return; }

        shm_info_.shmid = shmget(IPC_PRIVATE,
            static_cast<size_t>(shm_image_->bytes_per_line) * shm_image_->height,
            IPC_CREAT | 0666);
        if (shm_info_.shmid < 0) { XDestroyImage(shm_image_); shm_image_ = nullptr; use_shm_ = false; return; }

        shm_info_.shmaddr = shm_image_->data = static_cast<char*>(shmat(shm_info_.shmid, nullptr, 0));
        shm_info_.readOnly = False;

        XShmAttach(display_, &shm_info_);
        XSync(display_, False);

        // Mark segment for deletion after attach
        shmctl(shm_info_.shmid, IPC_RMID, nullptr);
    }

    void cleanup_images() {
        if (shm_image_) {
            XShmDetach(display_, &shm_info_);
            XDestroyImage(shm_image_);
            shmdt(shm_info_.shmaddr);
            shm_image_ = nullptr;
        }
        if (fallback_image_) {
            XDestroyImage(fallback_image_);
            fallback_image_ = nullptr;
        }
    }

    void enumerate_displays() {
        if (!display_) return;
        int event_base, error_base;
        if (!XRRQueryExtension(display_, &event_base, &error_base)) {
            // Fallback: single display from screen dimensions
            int screen = DefaultScreen(display_);
            Screen* scr = ScreenOfDisplay(display_, screen);
            DisplayInfo di;
            di.index = 0; di.name = ":0.0";
            di.width = static_cast<uint32_t>(WidthOfScreen(scr));
            di.height = static_cast<uint32_t>(HeightOfScreen(scr));
            di.is_primary = true;
            displays_.push_back(di);
            return;
        }

        XRRScreenResources* res = XRRGetScreenResources(display_,
            RootWindow(display_, DefaultScreen(display_)));
        if (!res) return;

        for (int i = 0; i < res->noutput; i++) {
            XRROutputInfo* out = XRRGetOutputInfo(display_, res, res->outputs[i]);
            if (!out || out->connection != RR_Connected) {
                if (out) XRRFreeOutputInfo(out);
                continue;
            }

            XRRCrtcInfo* crtc = XRRGetCrtcInfo(display_, res, out->crtc);
            if (!crtc) { XRRFreeOutputInfo(out); continue; }

            DisplayInfo di;
            di.index = static_cast<uint32_t>(displays_.size());
            di.name = out->name ? std::string(out->name, out->nameLen) : ":0." + std::to_string(i);
            di.x = crtc->x; di.y = crtc->y;
            di.width = crtc->width; di.height = crtc->height;
            di.is_primary = (di.x == 0 && di.y == 0);

            displays_.push_back(di);

            XRRFreeCrtcInfo(crtc);
            XRRFreeOutputInfo(out);
        }
        XRRFreeScreenResources(res);

        if (displays_.empty()) {
            DisplayInfo di; di.index = 0; di.name = ":0.0";
            int screen = DefaultScreen(display_);
            Screen* scr = ScreenOfDisplay(display_, screen);
            di.width = static_cast<uint32_t>(WidthOfScreen(scr));
            di.height = static_cast<uint32_t>(HeightOfScreen(scr));
            di.is_primary = true;
            displays_.push_back(di);
        }
    }
};

#endif // __linux__

// ===== macOS Quartz Capturer =====
#ifdef __APPLE__

class QuartzCapturerFull : public TraitCapturer {
    std::vector<DisplayInfo> displays_;
    uint32_t current_ = 0;
    CGDisplayStreamRef stream_ = nullptr;
    dispatch_queue_t queue_ = nullptr;
    IOSurfaceRef current_surface_ = nullptr;
    std::mutex surface_mutex_;

public:
    QuartzCapturerFull() {
        queue_ = dispatch_queue_create("com.cppdesk.scrap.quartz", nullptr);
        enumerate_displays();
        spdlog::info("Quartz capturer ready ({} displays)", displays_.size());
    }

    ~QuartzCapturerFull() override {
        stop_stream();
        if (queue_) dispatch_release(queue_);
    }

    std::optional<Frame> frame(std::chrono::milliseconds timeout) override {
        if (current_ >= displays_.size()) return std::nullopt;
        auto& di = displays_[current_];

        CGDirectDisplayID did = static_cast<CGDirectDisplayID>(di.index);
        CGImageRef img = CGDisplayCreateImage(did);
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
        CGContextRef ctx = CGBitmapContextCreate(f.data.data(), f.w, f.h, 8,
            f.w * 4, cs, kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
        CGContextDrawImage(ctx, CGRectMake(0, 0, f.w, f.h), img);
        CGContextRelease(ctx);
        CGColorSpaceRelease(cs);
        CGImageRelease(img);

        return f;
    }

    std::vector<DisplayInfo> displays() const override { return displays_; }

    bool select_display(uint32_t idx) override {
        if (idx < displays_.size()) { current_ = idx; return true; }
        return false;
    }

private:
    void enumerate_displays() {
        uint32_t count = 0;
        CGGetActiveDisplayList(0, nullptr, &count);
        std::vector<CGDirectDisplayID> ids(count);
        CGGetActiveDisplayList(count, ids.data(), &count);

        for (uint32_t i = 0; i < count; i++) {
            CGDirectDisplayID did = ids[i];
            DisplayInfo di;
            di.index = did;
            di.name = "Display " + std::to_string(i);
            di.width = static_cast<uint32_t>(CGDisplayPixelsWide(did));
            di.height = static_cast<uint32_t>(CGDisplayPixelsHigh(did));
            di.is_primary = CGDisplayIsMain(did);
            di.refresh_rate = static_cast<uint32_t>(CGDisplayModeGetRefreshRate(
                CGDisplayCopyDisplayMode(did)));
            displays_.push_back(di);
        }

        if (displays_.empty()) {
            DisplayInfo di; di.index = CGMainDisplayID();
            di.name = "Built-in Display"; di.is_primary = true;
            di.width = static_cast<uint32_t>(CGDisplayPixelsWide(di.index));
            di.height = static_cast<uint32_t>(CGDisplayPixelsHigh(di.index));
            displays_.push_back(di);
        }
    }

    void start_stream(CGDirectDisplayID did) {
        // CGDisplayStream for efficient capture
        stop_stream();
    }

    void stop_stream() {
        if (stream_) {
            CGDisplayStreamStop(stream_);
            CFRelease(stream_);
            stream_ = nullptr;
        }
        if (current_surface_) {
            CFRelease(current_surface_);
            current_surface_ = nullptr;
        }
    }
};

#endif // __APPLE__

// ===== Factory update =====
std::unique_ptr<TraitCapturer> create_capturer_x11() {
#ifdef __linux__
    return std::make_unique<X11ShmCapturer>();
#endif
    return nullptr;
}

std::unique_ptr<TraitCapturer> create_capturer_quartz() {
#ifdef __APPLE__
    return std::make_unique<QuartzCapturerFull>();
#endif
    return nullptr;
}

} // namespace scrap
