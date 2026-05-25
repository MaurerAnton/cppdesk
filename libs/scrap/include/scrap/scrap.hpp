#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <chrono>

namespace scrap {

// ====== Image Formats ======
enum class ImageFormat {
    RAW,
    ABGR,
    ARGB,
    BGRA,
    RGBA,
    YUV420,
    NV12,
};

// ====== Codec Formats ======
enum class CodecFormat {
    RAW = 0,
    H264 = 1,
    H265 = 2,
    VP8 = 3,
    VP9 = 4,
    AV1 = 5,
    MJPEG = 6,
};

// ====== Image types ======
struct ImageRgb {
    std::vector<uint8_t> raw;
    size_t w = 0;
    size_t h = 0;
    ImageFormat fmt = ImageFormat::RGBA;
    size_t align = 1;

    bool is_valid() const { return w > 0 && h > 0 && !raw.empty(); }
    size_t stride() const { return w * 4; }
};

struct ImageTexture {
    void* texture = nullptr;
    size_t w = 0;
    size_t h = 0;
    CodecFormat format = CodecFormat::RAW;
};

// ====== Frame ======
struct Frame {
    std::vector<uint8_t> data;
    size_t w = 0, h = 0;
    size_t stride = 0;
    ImageFormat fmt = ImageFormat::RGBA;
    int64_t timestamp_us = 0;
    bool keyframe = true;
    CodecFormat codec = CodecFormat::RAW;
};

// ====== Display Info ======
struct DisplayInfo {
    uint32_t index = 0;
    std::string name;
    int32_t x = 0, y = 0;
    uint32_t width = 0, height = 0;
    uint32_t refresh_rate = 60;
    bool is_primary = false;
    bool is_virtual = false;
    double scale = 1.0;
};

// ====== Capturer Trait ======
class TraitCapturer {
public:
    virtual ~TraitCapturer() = default;

    /// Capture a frame with timeout
    virtual std::optional<Frame> frame(std::chrono::milliseconds timeout) = 0;

    /// Check if using GDI (Windows)
    virtual bool is_gdi() const { return false; }

    /// Switch to GDI mode (Windows)
    virtual bool set_gdi() { return false; }

    /// Get display info
    virtual std::vector<DisplayInfo> displays() const = 0;

    /// Select display
    virtual bool select_display(uint32_t index) = 0;
};

// ====== Codec ======
class Decoder {
public:
    virtual ~Decoder() = default;
    virtual bool open(CodecFormat fmt) = 0;
    virtual std::optional<ImageRgb> decode(const std::vector<uint8_t>& data) = 0;
    virtual void close() = 0;
};

class Encoder {
public:
    virtual ~Encoder() = default;
    virtual bool open(CodecFormat fmt, uint32_t w, uint32_t h,
        uint32_t fps = 30, uint32_t bitrate = 2000000) = 0;
    virtual std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) = 0;
    virtual bool flush(std::vector<uint8_t>& out) = 0;
    virtual void close() = 0;
};

// ====== Recorder ======
class Recorder {
public:
    struct Context {
        uint32_t width = 1920;
        uint32_t height = 1080;
        uint32_t fps = 30;
        uint32_t bitrate = 5000000;
        CodecFormat codec = CodecFormat::H264;
        std::string output_path;
    };

    virtual ~Recorder() = default;
    virtual bool start(const Context& ctx) = 0;
    virtual bool feed(const Frame& frame) = 0;
    virtual void stop() = 0;
    virtual bool is_recording() const = 0;
};

// ====== Camera ======
class Camera {
public:
    virtual ~Camera() = default;
    virtual bool open(uint32_t index = 0) = 0;
    virtual std::optional<Frame> capture() = 0;
    virtual void close() = 0;
    virtual std::vector<std::string> list_cameras() = 0;
};

// ====== Convert ======
ImageRgb convert_format(const ImageRgb& src, ImageFormat target);
ImageRgb convert_bgra_to_rgba(const ImageRgb& src);
ImageRgb convert_yuv_to_rgba(const std::vector<uint8_t>& yuv,
    uint32_t w, uint32_t h);

// ====== Factory ======
std::unique_ptr<TraitCapturer> create_capturer();
std::unique_ptr<Decoder> create_decoder();
std::unique_ptr<Encoder> create_encoder();
std::unique_ptr<Recorder> create_recorder();
std::unique_ptr<Camera> create_camera();

// ====== Utilities ======
bool would_block_if_equal(std::vector<uint8_t>& old, const std::vector<uint8_t>& b);
bool is_x11();
bool is_wayland();
bool is_dxgi_available();
bool is_quartz_available();

// Constants
constexpr size_t STRIDE_ALIGN = 64;
constexpr size_t HW_STRIDE_ALIGN = 0;
constexpr int PRIMARY_CAMERA_IDX = 0;

} // namespace scrap
