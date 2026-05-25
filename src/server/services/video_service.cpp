#include "server/server.hpp"
#include "platform/platform.hpp"
#include "common/config.hpp"
#include "common/crypto.hpp"
#include <spdlog/spdlog.h>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <queue>
#include <algorithm>
#include <cmath>
#include <condition_variable>

namespace cppdesk::server {

// ====== Video Service Section 0 ======

// ====== Video Service Section 1 ======

// ====== Video Service Section 2 ======

// ====== Video Service Section 3 ======

// ====== Video Service Section 4 ======

// ====== Video Service Section 5 ======

// ====== Video Service Section 6 ======

// ====== Video Service Section 7 ======

// ====== Video Service Section 8 ======

// ====== Video Service Section 9 ======

// ====== Video Service Section 10 ======

// ====== Video Service Section 11 ======

// ====== Video Service Section 12 ======

// ====== Video Service Section 13 ======

// ====== Video Service Section 14 ======

// ====== Video Service Section 15 ======

// ====== Video Service Section 16 ======

// ====== Video Service Section 17 ======

// ====== Video Service Section 18 ======

// ====== Video Service Section 19 ======

// ====== Video Service Section 20 ======

// ====== Video Service Section 21 ======

// ====== Video Service Section 22 ======

// ====== Video Service Section 23 ======

// ====== Video Service Section 24 ======

// ====== Video Service Section 25 ======

// ====== Video Service Section 26 ======

// ====== Video Service Section 27 ======

// ====== Video Service Section 28 ======

// ====== Video Service Section 29 ======

struct DisplayInfo {
    uint32_t index = 0;
    std::string name;
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t refresh_rate = 60;
    bool is_primary = false;
    int32_t x = 0, y = 0;
    bool is_virtual = false;
    double scale_factor = 1.0;
};

class DisplayManager {
public:
    static DisplayManager& instance() { static DisplayManager dm; return dm; }

    std::vector<DisplayInfo> enumerate() {
        std::vector<DisplayInfo> displays;
        auto names = platform::get_display_names();
        for (size_t i = 0; i < names.size(); i++) {
            DisplayInfo di;
            di.index = static_cast<uint32_t>(i);
            di.name = names[i];
            di.is_primary = (i == 0);
            auto res = platform::current_resolution(names[i]);
            if (res) { di.width = res->width; di.height = res->height; }
            displays.push_back(di);
        }
        return displays;
    }

    DisplayInfo get_primary() {
        auto displays = enumerate();
        for (auto& d : displays) if (d.is_primary) return d;
        return displays.empty() ? DisplayInfo{} : displays[0];
    }

    bool change_resolution(uint32_t idx, uint32_t w, uint32_t h) {
        auto displays = enumerate();
        if (idx >= displays.size()) return false;
        return platform::change_resolution(displays[idx].name, w, h);
    }

    bool is_display_changed() {
        auto current = enumerate();
        if (current.size() != cached_.size()) return true;
        for (size_t i = 0; i < current.size(); i++) {
            if (current[i].width != cached_[i].width ||
                current[i].height != cached_[i].height) return true;
        }
        return false;
    }

    void update_cache() { cached_ = enumerate(); }

private:
    DisplayManager() = default;
    std::vector<DisplayInfo> cached_;
};

// Windows: DXGI Desktop Duplication
class WindowsFrameCapturer {
public:
    bool open(uint32_t display_idx) {
        display_idx_ = display_idx;
        spdlog::info("Windows capturer opened for display {}", display_idx);
        opened_ = true;
        return true;
    }
    
    std::optional<common::VideoFrame> capture() {
        if (!opened_) return std::nullopt;
        auto frame = platform::capture_screen(display_idx_);
        if (frame) {
            frame->display = display_idx_;
            frame->is_monitor = true;
            frame->timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            frames_captured_++;
        }
        return frame;
    }
    
    void close() { opened_ = false; }
    uint64_t frames() const { return frames_captured_; }
    
private:
    uint32_t display_idx_ = 0;
    bool opened_ = false;
    uint64_t frames_captured_ = 0;
};

// Linux: X11 SHM + DMA-BUF
class LinuxFrameCapturer {
public:
    bool open(uint32_t display_idx) {
        display_idx_ = display_idx;
        spdlog::info("Linux capturer opened for display {}", display_idx);
        opened_ = true;
        return true;
    }
    
    std::optional<common::VideoFrame> capture() {
        if (!opened_) return std::nullopt;
        auto frame = platform::capture_screen(display_idx_);
        if (frame) {
            frame->display = display_idx_;
            frame->is_monitor = true;
            frame->timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            frames_captured_++;
        }
        return frame;
    }
    
    void close() { opened_ = false; }
    uint64_t frames() const { return frames_captured_; }
    
private:
    uint32_t display_idx_ = 0;
    bool opened_ = false;
    uint64_t frames_captured_ = 0;
};

// macOS: CGDisplay + IOSurface
class macOSFrameCapturer {
public:
    bool open(uint32_t display_idx) {
        display_idx_ = display_idx;
        spdlog::info("macOS capturer opened for display {}", display_idx);
        opened_ = true;
        return true;
    }
    
    std::optional<common::VideoFrame> capture() {
        if (!opened_) return std::nullopt;
        auto frame = platform::capture_screen(display_idx_);
        if (frame) {
            frame->display = display_idx_;
            frame->is_monitor = true;
            frame->timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            frames_captured_++;
        }
        return frame;
    }
    
    void close() { opened_ = false; }
    uint64_t frames() const { return frames_captured_; }
    
private:
    uint32_t display_idx_ = 0;
    bool opened_ = false;
    uint64_t frames_captured_ = 0;
};

class FrameDifferencer {
public:
    struct DirtyRect {
        uint32_t x, y, w, h;
    };

    std::vector<DirtyRect> diff(const VideoFrame& prev, const VideoFrame& curr,
        int threshold = 16) {
        if (prev.width != curr.width || prev.height != curr.height) {
            return {{0, 0, curr.width, curr.height}};
        }
        std::vector<DirtyRect> rects;
        const int block_size = 16;
        uint32_t blocks_x = (curr.width + block_size - 1) / block_size;
        uint32_t blocks_y = (curr.height + block_size - 1) / block_size;
        for (uint32_t by = 0; by < blocks_y; by++) {
            for (uint32_t bx = 0; bx < blocks_x; bx++) {
                bool dirty = false;
                for (uint32_t dy = 0; dy < block_size && !dirty; dy++) {
                    uint32_t y = by * block_size + dy;
                    if (y >= curr.height) break;
                    for (uint32_t dx = 0; dx < block_size; dx++) {
                        uint32_t x = bx * block_size + dx;
                        if (x >= curr.width) break;
                        size_t idx = (y * curr.width + x) * 4;
                        int dr = abs(curr.data[idx] - prev.data[idx]);
                        int dg = abs(curr.data[idx+1] - prev.data[idx+1]);
                        int db = abs(curr.data[idx+2] - prev.data[idx+2]);
                        if (dr + dg + db > threshold) { dirty = true; break; }
                    }
                }
                if (dirty) rects.push_back({bx*block_size, by*block_size, block_size, block_size});
            }
        }
        return rects;
    }
};

class H264CodecWrapper {
public:
    bool init(uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate) {
        spdlog::info("H264 codec: {}x{} @{}fps {}bps", w, h, fps, bitrate);
        return true;
    }
    std::vector<uint8_t> encode(const VideoFrame& frame, bool& keyframe) {
        keyframe = (frames_++ % 60 == 0);
        return frame.data; // TODO: real H264 encoding
    }
    std::optional<VideoFrame> decode(const std::vector<uint8_t>& data) {
        VideoFrame f; f.data = data; f.codec = 1; return f;
    }
    void close() {}
    uint64_t frames() const { return frames_; }
private:
    uint64_t frames_ = 0;
};

class H265CodecWrapper {
public:
    bool init(uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate) {
        spdlog::info("H265 codec: {}x{} @{}fps {}bps", w, h, fps, bitrate);
        return true;
    }
    std::vector<uint8_t> encode(const VideoFrame& frame, bool& keyframe) {
        keyframe = (frames_++ % 60 == 0);
        return frame.data; // TODO: real H265 encoding
    }
    std::optional<VideoFrame> decode(const std::vector<uint8_t>& data) {
        VideoFrame f; f.data = data; f.codec = 2; return f;
    }
    void close() {}
    uint64_t frames() const { return frames_; }
private:
    uint64_t frames_ = 0;
};

class VP8CodecWrapper {
public:
    bool init(uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate) {
        spdlog::info("VP8 codec: {}x{} @{}fps {}bps", w, h, fps, bitrate);
        return true;
    }
    std::vector<uint8_t> encode(const VideoFrame& frame, bool& keyframe) {
        keyframe = (frames_++ % 60 == 0);
        return frame.data; // TODO: real VP8 encoding
    }
    std::optional<VideoFrame> decode(const std::vector<uint8_t>& data) {
        VideoFrame f; f.data = data; f.codec = 3; return f;
    }
    void close() {}
    uint64_t frames() const { return frames_; }
private:
    uint64_t frames_ = 0;
};

class VP9CodecWrapper {
public:
    bool init(uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate) {
        spdlog::info("VP9 codec: {}x{} @{}fps {}bps", w, h, fps, bitrate);
        return true;
    }
    std::vector<uint8_t> encode(const VideoFrame& frame, bool& keyframe) {
        keyframe = (frames_++ % 60 == 0);
        return frame.data; // TODO: real VP9 encoding
    }
    std::optional<VideoFrame> decode(const std::vector<uint8_t>& data) {
        VideoFrame f; f.data = data; f.codec = 4; return f;
    }
    void close() {}
    uint64_t frames() const { return frames_; }
private:
    uint64_t frames_ = 0;
};

class AV1CodecWrapper {
public:
    bool init(uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate) {
        spdlog::info("AV1 codec: {}x{} @{}fps {}bps", w, h, fps, bitrate);
        return true;
    }
    std::vector<uint8_t> encode(const VideoFrame& frame, bool& keyframe) {
        keyframe = (frames_++ % 60 == 0);
        return frame.data; // TODO: real AV1 encoding
    }
    std::optional<VideoFrame> decode(const std::vector<uint8_t>& data) {
        VideoFrame f; f.data = data; f.codec = 5; return f;
    }
    void close() {}
    uint64_t frames() const { return frames_; }
private:
    uint64_t frames_ = 0;
};

class VideoServiceImpl {
public:
    VideoServiceImpl(VideoSource source, uint32_t idx)
        : source_(source), display_idx_(idx) {}

    bool start() {
        running_ = true;
        spdlog::info("Video service {} starting (display {})",
            source_ == VideoSource::MONITOR ? "monitor" : "camera", display_idx_);

        capturer_ = create_capturer();
        if (capturer_) capturer_->open(display_idx_);

        worker_ = std::thread(&VideoServiceImpl::capture_loop, this);
        return true;
    }

    void stop() {
        running_ = false;
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        if (capturer_) capturer_->close();
    }

    void set_quality(uint32_t quality) { quality_ = std::clamp(quality, 1u, 100u); }
    void set_fps(uint32_t fps) { target_fps_ = std::clamp(fps, 1u, 120u); }
    void set_bitrate(uint32_t bps) { target_bitrate_ = bps; }
    void set_codec(uint32_t c) { codec_id_ = c; }
    void set_scale(double s) { scale_ = std::clamp(s, 0.1, 3.0); }

    struct Stats {
        uint64_t frames_captured = 0;
        uint64_t frames_encoded = 0;
        uint64_t frames_sent = 0;
        uint64_t bytes_sent = 0;
        double actual_fps = 0;
        double encode_time_ms = 0;
        uint32_t queue_depth = 0;
        uint32_t bitrate = 0;
    };
    Stats get_stats() const { return stats_; }

private:
    VideoSource source_;
    uint32_t display_idx_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::unique_ptr<WindowsFrameCapturer> capturer_;
    FrameDifferencer differ_;
    uint32_t quality_ = 75, target_fps_ = 30;
    uint32_t target_bitrate_ = 2000000, codec_id_ = 1;
    double scale_ = 1.0;
    Stats stats_;
    VideoFrame last_frame_;

    std::unique_ptr<WindowsFrameCapturer> create_capturer() {
        return std::make_unique<WindowsFrameCapturer>();
    }

    void capture_loop() {
        using namespace std::chrono;
        auto frame_interval = milliseconds(1000 / target_fps_);
        auto last_capture = steady_clock::now();
        auto stats_reset = steady_clock::now();
        uint64_t frames_this_second = 0;

        while (running_) {
            auto now = steady_clock::now();
            auto elapsed = now - last_capture;

            if (elapsed >= frame_interval) {
                auto t0 = steady_clock::now();
                auto frame = capturer_->capture();
                auto t1 = steady_clock::now();

                if (frame) {
                    stats_.frames_captured++;
                    stats_.encode_time_ms = duration<double, milli>(t1 - t0).count();
                    last_frame_ = *frame;
                    frames_this_second++;
                }
                last_capture = now;
            }

            if (now - stats_reset >= seconds(1)) {
                stats_.actual_fps = static_cast<double>(frames_this_second);
                frames_this_second = 0;
                stats_reset = now;
            }

            std::unique_lock lk(mutex_);
            cv_.wait_for(lk, milliseconds(1));
        }
    }
};

} // namespace cppdesk::server