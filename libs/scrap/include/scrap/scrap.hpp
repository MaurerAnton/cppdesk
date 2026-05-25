#pragma once

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <chrono>
#include <array>
#include <atomic>
#include <mutex>
#include <bitset>

namespace scrap {

// ====== Image Formats ======
enum class ImageFormat : uint8_t {
    RAW    = 0,
    ABGR   = 1,
    ARGB   = 2,
    BGRA   = 3,
    RGBA   = 4,
    YUV420 = 5,
    NV12   = 6,
    GRAY8  = 7,
    BGR    = 8,
    RGB    = 9,
    YUYV   = 10,
    UYVY   = 11,
    P010   = 12,  // 10-bit planar
};

// ====== Codec Formats ======
enum class CodecFormat : uint8_t {
    RAW    = 0,
    H264   = 1,
    H265   = 2,
    VP8    = 3,
    VP9    = 4,
    AV1    = 5,
    MJPEG  = 6,
    HEVC   = 7,
    AV1_SCC = 8,
};

// ====== Scaling Algorithm ======
enum class ScaleAlgorithm : uint8_t {
    Nearest    = 0,
    Bilinear   = 1,
    Bicubic    = 2,
    Lanczos3   = 3,
    AreaAvg    = 4,
};

// ====== Color Space ======
enum class ColorSpace : uint8_t {
    BT601      = 0,  // SD
    BT709      = 1,  // HD
    BT2020     = 2,  // UHD/HDR
    BT2020_PQ  = 3,  // HDR PQ
    BT2020_HLG = 4,  // HDR HLG
    SRGB       = 5,
};

// ====== Color Range ======
enum class ColorRange : uint8_t {
    Limited  = 0,  // 16-235 / 16-240
    Full     = 1,  // 0-255
};

// ====== Codec Profile ======
enum class CodecProfile : uint8_t {
    Baseline   = 0,
    Main       = 1,
    High       = 2,
    High444    = 3,
    Main10     = 4,
    Main12     = 5,
    Professional = 6,
};

// ====== Encoder Quality Preset ======
enum class EncoderQuality : uint8_t {
    Ultrafast  = 0,
    Veryfast   = 1,
    Fast       = 2,
    Medium     = 3,
    Slow       = 4,
    Veryslow   = 5,
    Lossless   = 6,
};

// ====== Rate Control Mode ======
enum class RateControl : uint8_t {
    CBR   = 0,   // Constant Bitrate
    VBR   = 1,   // Variable Bitrate
    CQP   = 2,   // Constant QP
    CRF   = 3,   // Constant Rate Factor
    VBR_HQ = 4,  // VBR High Quality
    QVBR  = 5,   // Quality VBR
};

// ====== Encoder Backend ======
enum class EncoderBackend : uint8_t {
    Software     = 0,
    NVENC        = 1,
    AMF          = 2,
    VAAPI        = 3,
    VideoToolbox = 4,
    QSV          = 5,
    MFX          = 6,  // MediaFoundation
    Auto         = 7,
};

// ====== Hardware Adapter Type ======
enum class AdapterType : uint8_t {
    Any          = 0,
    NVIDIA       = 1,
    AMD          = 2,
    Intel        = 3,
    Apple        = 4,
};

// ====== Frame Flags ======
enum class FrameFlag : uint8_t {
    Key      = 0,
    Dirty    = 1,
    Cursor   = 2,
    Idr      = 3,
    SeqHeader = 4,
    EOS      = 5,
};
using FrameFlags = std::bitset<8>;

// ====== Dirty Rect ======
struct DirtyRect {
    uint32_t left   = 0;
    uint32_t top    = 0;
    uint32_t right  = 0;
    uint32_t bottom = 0;

    [[nodiscard]] bool valid() const noexcept { return right > left && bottom > top; }
    [[nodiscard]] uint32_t width()  const noexcept { return right  - left; }
    [[nodiscard]] uint32_t height() const noexcept { return bottom - top; }
    [[nodiscard]] uint32_t area()   const noexcept { return width() * height(); }
    [[nodiscard]] bool contains(uint32_t x, uint32_t y) const noexcept {
        return x >= left && x < right && y >= top && y < bottom;
    }
    [[nodiscard]] DirtyRect intersect(const DirtyRect& o) const noexcept {
        DirtyRect r;
        r.left   = std::max(left,   o.left);
        r.top    = std::max(top,    o.top);
        r.right  = std::min(right,  o.right);
        r.bottom = std::min(bottom, o.bottom);
        if (r.right <= r.left || r.bottom <= r.top)
            r = {};
        return r;
    }
    [[nodiscard]] DirtyRect merge(const DirtyRect& o) const noexcept {
        if (!valid()) return o;
        if (!o.valid()) return *this;
        DirtyRect r;
        r.left   = std::min(left,   o.left);
        r.top    = std::min(top,    o.top);
        r.right  = std::max(right,  o.right);
        r.bottom = std::max(bottom, o.bottom);
        return r;
    }
};

// ====== Image types ======
struct ImageRgb {
    std::vector<uint8_t> raw;
    size_t w = 0;
    size_t h = 0;
    ImageFormat fmt = ImageFormat::RGBA;
    size_t align = 1;
    ColorSpace cspace = ColorSpace::SRGB;
    ColorRange range = ColorRange::Full;

    [[nodiscard]] bool is_valid() const { return w > 0 && h > 0 && !raw.empty(); }
    [[nodiscard]] size_t stride() const { return w * 4; }
    [[nodiscard]] size_t size_bytes() const { return raw.size(); }
    [[nodiscard]] uint8_t* data() noexcept { return raw.data(); }
    [[nodiscard]] const uint8_t* data() const noexcept { return raw.data(); }

    void clear() { raw.clear(); w = 0; h = 0; }
    void resize(size_t nw, size_t nh, ImageFormat nfmt) {
        w = nw; h = nh; fmt = nfmt;
        switch (fmt) {
            case ImageFormat::RGBA: case ImageFormat::BGRA:
            case ImageFormat::ARGB: case ImageFormat::ABGR:
                raw.resize(w * h * 4); break;
            case ImageFormat::GRAY8:
                raw.resize(w * h); break;
            case ImageFormat::YUV420:
                raw.resize(w * h + w * h / 2); break;
            case ImageFormat::NV12:
                raw.resize(w * h + w * h / 2); break;
            case ImageFormat::YUYV: case ImageFormat::UYVY:
                raw.resize(w * h * 2); break;
            case ImageFormat::P010:
                raw.resize((w * h + w * h / 2) * 2); break;
            default:
                raw.resize(w * h * 4); break;
        }
    }
};

struct ImageTexture {
    void* texture = nullptr;
    size_t w = 0;
    size_t h = 0;
    CodecFormat format = CodecFormat::RAW;
    uint32_t mip_levels = 1;
    uint32_t array_size = 1;

    [[nodiscard]] bool is_valid() const { return texture != nullptr && w > 0 && h > 0; }
};

// ====== Plane Data (for planar formats) ======
struct PlaneData {
    uint8_t* data    = nullptr;
    uint32_t width   = 0;
    uint32_t height  = 0;
    uint32_t stride  = 0;
    uint32_t size    = 0;
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
    FrameFlags flags;
    std::vector<DirtyRect> dirty_rects;
    std::vector<PlaneData> planes;
    int64_t pts = 0;
    int64_t dts = 0;
    int64_t duration = 0;
    uint32_t sequence_number = 0;

    // Cursor overlay
    bool cursor_visible = false;
    int32_t cursor_x = 0;
    int32_t cursor_y = 0;
    uint32_t cursor_w = 0;
    uint32_t cursor_h = 0;
    std::vector<uint8_t> cursor_data;

    [[nodiscard]] bool empty() const noexcept { return data.empty(); }
    [[nodiscard]] size_t size() const noexcept { return data.size(); }
    [[nodiscard]] bool has_dirty() const noexcept { return !dirty_rects.empty(); }

    void swap(Frame& other) noexcept {
        data.swap(other.data);
        std::swap(w, other.w);
        std::swap(h, other.h);
        std::swap(stride, other.stride);
        std::swap(fmt, other.fmt);
        std::swap(timestamp_us, other.timestamp_us);
        std::swap(keyframe, other.keyframe);
        std::swap(codec, other.codec);
        std::swap(flags, other.flags);
        dirty_rects.swap(other.dirty_rects);
        planes.swap(other.planes);
        std::swap(pts, other.pts);
        std::swap(dts, other.dts);
        std::swap(duration, other.duration);
        std::swap(sequence_number, other.sequence_number);
        std::swap(cursor_visible, other.cursor_visible);
        std::swap(cursor_x, other.cursor_x);
        std::swap(cursor_y, other.cursor_y);
        std::swap(cursor_w, other.cursor_w);
        std::swap(cursor_h, other.cursor_h);
        cursor_data.swap(other.cursor_data);
    }
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
    uint32_t bits_per_pixel = 32;
    bool hdr_capable = false;
    ColorSpace native_color_space = ColorSpace::SRGB;
    std::string adapter_name;
    uint32_t adapter_index = 0;
};

// ====== Frame Statistics ======
struct FrameStats {
    uint64_t total_frames       = 0;
    uint64_t captured_frames    = 0;
    uint64_t dropped_frames     = 0;
    uint64_t encoded_frames     = 0;
    uint64_t key_frames         = 0;
    uint64_t total_bytes        = 0;
    uint64_t encoded_bytes      = 0;
    double   avg_capture_ms     = 0.0;
    double   avg_encode_ms      = 0.0;
    double   avg_fps            = 0.0;
    double   current_fps        = 0.0;
    double   peak_fps           = 0.0;
    uint32_t current_bitrate    = 0;  // bps
    uint32_t dirty_rect_pct     = 0;  // % of frame changed
    int64_t  last_frame_ts      = 0;
    int64_t  session_start_ts   = 0;
    uint32_t consecutive_drops  = 0;

    void reset() {
        total_frames = 0; captured_frames = 0; dropped_frames = 0;
        encoded_frames = 0; key_frames = 0; total_bytes = 0; encoded_bytes = 0;
        avg_capture_ms = 0.0; avg_encode_ms = 0.0; avg_fps = 0.0;
        current_fps = 0.0; peak_fps = 0.0; current_bitrate = 0;
        dirty_rect_pct = 0; consecutive_drops = 0;
    }
};

// ====== Encoder Configuration ======
struct EncoderConfig {
    CodecFormat codec          = CodecFormat::H264;
    uint32_t width             = 1920;
    uint32_t height            = 1080;
    uint32_t fps_num           = 30;
    uint32_t fps_den           = 1;
    uint32_t bitrate           = 5000000;
    uint32_t max_bitrate       = 0;
    uint32_t buffer_size       = 0;
    uint32_t gop_size          = 60;
    uint32_t b_frames          = 0;
    CodecProfile profile       = CodecProfile::High;
    EncoderQuality quality     = EncoderQuality::Medium;
    RateControl rate_control   = RateControl::VBR;
    EncoderBackend backend     = EncoderBackend::Auto;
    AdapterType adapter        = AdapterType::Any;
    uint32_t adapter_index     = 0;
    uint32_t qp_min            = 0;
    uint32_t qp_max            = 51;
    uint32_t qp_i              = 23;
    uint32_t qp_p              = 23;
    uint32_t qp_b              = 23;
    int32_t crf                = 23;
    int32_t ref_frames         = 3;
    int32_t threads            = 0; // 0 = auto
    bool low_latency           = true;
    bool full_range            = false;
    ColorSpace color_space     = ColorSpace::BT709;
    ColorRange color_range     = ColorRange::Limited;
    std::string preset_override;
    std::string tune;
    std::string extra_opts;

    [[nodiscard]] uint32_t fps() const noexcept {
        return fps_den > 0 ? fps_num / fps_den : fps_num;
    }
    [[nodiscard]] double frame_interval_ms() const noexcept {
        double f = static_cast<double>(fps_num) / fps_den;
        return f > 0.0 ? 1000.0 / f : 33.333;
    }
};

// ====== Scaling Configuration ======
struct ScaleConfig {
    uint32_t src_w       = 0;
    uint32_t src_h       = 0;
    uint32_t dst_w       = 0;
    uint32_t dst_h       = 0;
    ScaleAlgorithm algo  = ScaleAlgorithm::Bilinear;
    ImageFormat src_fmt  = ImageFormat::RGBA;
    ImageFormat dst_fmt  = ImageFormat::RGBA;
    bool keep_aspect     = false;
    uint8_t background_r = 0;
    uint8_t background_g = 0;
    uint8_t background_b = 0;
    uint8_t background_a = 255;

    [[nodiscard]] double scale_x() const noexcept {
        return src_w > 0 ? static_cast<double>(dst_w) / src_w : 1.0;
    }
    [[nodiscard]] double scale_y() const noexcept {
        return src_h > 0 ? static_cast<double>(dst_h) / src_h : 1.0;
    }
};

// ====== Color Conversion Plan ======
struct ColorConversionPlan {
    ImageFormat src_fmt   = ImageFormat::RGBA;
    ImageFormat dst_fmt   = ImageFormat::RGBA;
    ColorSpace src_cspace = ColorSpace::SRGB;
    ColorSpace dst_cspace = ColorSpace::SRGB;
    ColorRange src_range  = ColorRange::Full;
    ColorRange dst_range  = ColorRange::Full;
    bool premultiply_alpha = false;
    bool unpremultiply_alpha = false;
    uint8_t alpha_fill    = 255;

    [[nodiscard]] bool is_identity() const noexcept {
        return src_fmt == dst_fmt && src_cspace == dst_cspace &&
               src_range == dst_range &&
               !premultiply_alpha && !unpremultiply_alpha;
    }
};

// ====== Capture Region ======
struct CaptureRegion {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;

    [[nodiscard]] bool valid() const noexcept { return w > 0 && h > 0; }
    [[nodiscard]] bool contains_point(int32_t px, int32_t py) const noexcept {
        return px >= x && px < static_cast<int32_t>(x + w) &&
               py >= y && py < static_cast<int32_t>(y + h);
    }
};

// ====== Filter State ======
struct FilterState {
    std::vector<float> coeffs;
    std::vector<float> buffer;
    uint32_t tap_count = 0;
    uint32_t phase_count = 0;
    bool initialized = false;
};

// ====== Forward declarations for platform-specific types ======
struct DxgiCaptureContext;
struct X11ShmContext;
struct QuartzContext;
struct NvencContext;
struct AmfContext;
struct VaapiContext;
struct VtContext;
struct FfmpegContext;
struct DShowContext;
struct V4l2Context;
struct AvfContext;

// ====== TraitCapturer ======
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

    // --- New virtual methods ---

    /// Capture a specific region of the display
    virtual std::optional<Frame> capture_region(const CaptureRegion& region,
        std::chrono::milliseconds timeout) {
        // Default: capture full frame, then crop
        auto f = frame(timeout);
        if (!f || !region.valid()) return f;
        if (region.x < 0 || region.y < 0 ||
            static_cast<uint32_t>(region.x) + region.w > f->w ||
            static_cast<uint32_t>(region.y) + region.h > f->h)
            return std::nullopt;

        Frame cropped;
        cropped.w = region.w;
        cropped.h = region.h;
        cropped.fmt = f->fmt;
        cropped.stride = region.w * 4;
        cropped.timestamp_us = f->timestamp_us;
        cropped.codec = f->codec;
        if (f->fmt == ImageFormat::RGBA || f->fmt == ImageFormat::BGRA ||
            f->fmt == ImageFormat::ARGB || f->fmt == ImageFormat::ABGR) {
            cropped.data.resize(region.w * region.h * 4);
            for (uint32_t row = 0; row < region.h; ++row) {
                size_t src_off = ((region.y + row) * f->w + region.x) * 4;
                size_t dst_off = row * region.w * 4;
                std::copy_n(f->data.begin() + src_off, region.w * 4,
                    cropped.data.begin() + dst_off);
            }
        }
        return cropped;
    }

    /// Get current display index
    virtual uint32_t current_display() const { return 0; }

    /// Get frame statistics
    virtual FrameStats stats() const { return FrameStats{}; }

    /// Reset statistics
    virtual void reset_stats() {}

    /// Get supported display count
    virtual uint32_t display_count() const { return 0; }

    /// Check if cursor capture is supported
    virtual bool cursor_capture_supported() const { return false; }

    /// Enable/disable cursor capture
    virtual void set_cursor_capture(bool enable) { (void)enable; }

    /// Check if cursor capture is enabled
    virtual bool cursor_capture_enabled() const { return false; }

    /// Get display refresh rate
    virtual uint32_t display_refresh_rate() const { return 60; }

    /// Get display bounds
    virtual bool display_bounds(uint32_t index, int32_t& x, int32_t& y,
        uint32_t& w, uint32_t& h) const {
        (void)index; (void)x; (void)y; (void)w; (void)h;
        return false;
    }

    /// Enable dirty rect detection
    virtual void set_dirty_detection(bool enable) { (void)enable; }

    /// Get last dirty rects
    virtual std::vector<DirtyRect> last_dirty_rects() const { return {}; }

    /// Check if capture is active
    virtual bool is_capturing() const { return true; }

    /// Pause capture
    virtual void pause() {}

    /// Resume capture
    virtual void resume() {}
};

// ====== Decoder ======
class Decoder {
public:
    virtual ~Decoder() = default;
    virtual bool open(CodecFormat fmt) = 0;
    virtual std::optional<ImageRgb> decode(const std::vector<uint8_t>& data) = 0;
    virtual void close() = 0;

    // New methods
    virtual bool open_with_config(CodecFormat fmt, uint32_t w, uint32_t h,
        ColorSpace cspace = ColorSpace::BT709) {
        (void)w; (void)h; (void)cspace;
        return open(fmt);
    }
    virtual std::optional<ImageRgb> decode_packet(const uint8_t* data, size_t size,
        int64_t pts = 0) {
        if (!data || size == 0) return std::nullopt;
        std::vector<uint8_t> buf(data, data + size);
        return decode(buf);
    }
    virtual bool flush() { return true; }
    virtual bool is_open() const { return false; }
    virtual int32_t width() const { return 0; }
    virtual int32_t height() const { return 0; }
};

// ====== Encoder ======
class Encoder {
public:
    virtual ~Encoder() = default;
    virtual bool open(CodecFormat fmt, uint32_t w, uint32_t h,
        uint32_t fps = 30, uint32_t bitrate = 2000000) = 0;
    virtual std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) = 0;
    virtual bool flush(std::vector<uint8_t>& out) = 0;
    virtual void close() = 0;

    // New methods
    virtual bool open_with_config(const EncoderConfig& cfg) {
        return open(cfg.codec, cfg.width, cfg.height, cfg.fps(), cfg.bitrate);
    }
    virtual std::vector<uint8_t> encode_frame(const Frame& frame, bool& keyframe) {
        return encode(frame, keyframe);
    }
    virtual bool encode_into(const Frame& frame, std::vector<uint8_t>& output,
        bool& keyframe) {
        auto result = encode(frame, keyframe);
        if (result.empty()) return false;
        output = std::move(result);
        return true;
    }
    virtual bool is_open() const { return false; }
    virtual EncoderConfig config() const { return EncoderConfig{}; }
    virtual bool reconfigure(const EncoderConfig& cfg) {
        close();
        return open_with_config(cfg);
    }
    virtual bool set_bitrate(uint32_t bitrate) {
        EncoderConfig cfg = config();
        cfg.bitrate = bitrate;
        return reconfigure(cfg);
    }
    virtual bool set_quality(EncoderQuality q) {
        EncoderConfig cfg = config();
        cfg.quality = q;
        return reconfigure(cfg);
    }
    virtual bool force_keyframe() { return false; }
    virtual int get_delay_frames() const { return 0; }
};

// ====== Recorder ======
class Recorder {
public:
    struct Context {
        uint32_t width       = 1920;
        uint32_t height      = 1080;
        uint32_t fps         = 30;
        uint32_t bitrate     = 5000000;
        CodecFormat codec    = CodecFormat::H264;
        std::string output_path;
        std::string container = "mp4";
        EncoderQuality quality = EncoderQuality::Medium;
        uint32_t gop_size    = 60;
        bool include_audio   = false;
        std::string audio_device;
        uint32_t audio_sample_rate = 48000;
        uint32_t audio_channels    = 2;
    };

    virtual ~Recorder() = default;
    virtual bool start(const Context& ctx) = 0;
    virtual bool feed(const Frame& frame) = 0;
    virtual void stop() = 0;
    virtual bool is_recording() const = 0;

    // New methods
    virtual bool feed_audio(const std::vector<uint8_t>& samples,
        uint32_t sample_count) {
        (void)samples; (void)sample_count;
        return false; // audio not supported by default
    }
    virtual bool pause() { return false; }
    virtual bool resume() { return false; }
    virtual bool is_paused() const { return false; }
    virtual int64_t duration_ms() const { return 0; }
    virtual uint64_t bytes_written() const { return 0; }
    virtual FrameStats stats() const { return FrameStats{}; }
    virtual bool split_file() { return false; } // start new segment
};

// ====== Camera ======
class Camera {
public:
    virtual ~Camera() = default;
    virtual bool open(uint32_t index = 0) = 0;
    virtual std::optional<Frame> capture() = 0;
    virtual void close() = 0;
    virtual std::vector<std::string> list_cameras() = 0;

    // New methods
    virtual bool open_by_name(const std::string& name) {
        auto cams = list_cameras();
        for (size_t i = 0; i < cams.size(); ++i) {
            if (cams[i] == name) return open(static_cast<uint32_t>(i));
        }
        return false;
    }
    virtual bool is_open() const { return false; }
    virtual bool set_resolution(uint32_t w, uint32_t h) {
        (void)w; (void)h;
        return false;
    }
    virtual bool set_fps(uint32_t fps) { (void)fps; return false; }
    virtual bool set_format(ImageFormat fmt) { (void)fmt; return false; }
    virtual std::pair<uint32_t, uint32_t> resolution() const { return {0, 0}; }
    virtual uint32_t camera_fps() const { return 0; }
    virtual bool has_autofocus() const { return false; }
    virtual bool set_autofocus(bool enable) { (void)enable; return false; }
};

// ====== Scaler ======
class Scaler {
public:
    Scaler() = default;
    explicit Scaler(const ScaleConfig& cfg);
    ~Scaler();

    Scaler(const Scaler&) = delete;
    Scaler& operator=(const Scaler&) = delete;
    Scaler(Scaler&&) noexcept;
    Scaler& operator=(Scaler&&) noexcept;

    /// Configure scaler
    bool configure(const ScaleConfig& cfg);

    /// Scale an image
    bool scale(const ImageRgb& src, ImageRgb& dst);

    /// Scale a frame
    bool scale_frame(const Frame& src, Frame& dst);

    /// Scale plane data directly
    bool scale_planes(const uint8_t* src, uint32_t src_w, uint32_t src_h,
        uint32_t src_stride, uint8_t* dst, uint32_t dst_w, uint32_t dst_h,
        uint32_t dst_stride, ScaleAlgorithm algo);

    /// Get current config
    [[nodiscard]] const ScaleConfig& config() const noexcept { return config_; }

    /// Check if configured
    [[nodiscard]] bool is_configured() const noexcept { return configured_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ScaleConfig config_;
    bool configured_ = false;
};

// ====== Frame Differencer ======
class FrameDifferencer {
public:
    FrameDifferencer();
    ~FrameDifferencer();

    FrameDifferencer(const FrameDifferencer&) = delete;
    FrameDifferencer& operator=(const FrameDifferencer&) = delete;

    /// Configure differencer
    void configure(uint32_t w, uint32_t h, ImageFormat fmt,
        uint32_t block_size = 16, uint32_t threshold = 8);

    /// Compute diff between two frames
    /// Returns vector of dirty rects; empty means no change
    std::vector<DirtyRect> diff(const Frame& prev, const Frame& curr);

    /// Compute diff from packed data
    std::vector<DirtyRect> diff_data(const uint8_t* prev, const uint8_t* curr,
        uint32_t w, uint32_t h, uint32_t stride);

    /// Check if frame has changed
    bool has_changed(const Frame& prev, const Frame& curr);

    /// Get hash of current frame (for quick comparison)
    uint64_t hash_frame(const Frame& frame) const;

    /// Get block size
    [[nodiscard]] uint32_t block_size() const noexcept { return block_size_; }

    /// Set sensitivity (0-255, lower = more sensitive)
    void set_sensitivity(uint8_t threshold) { threshold_ = threshold; }

    /// Reset internal state
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    uint32_t block_size_  = 16;
    uint32_t threshold_   = 8;
    uint32_t frame_w_     = 0;
    uint32_t frame_h_     = 0;
    ImageFormat fmt_      = ImageFormat::RGBA;
};

// ====== FPS Limiter ======
class FPSLimiter {
public:
    explicit FPSLimiter(uint32_t target_fps = 60);
    ~FPSLimiter() = default;

    /// Set target FPS
    void set_target_fps(uint32_t fps);

    /// Wait until next frame time
    /// Returns: true if waited, false if already past deadline
    bool wait();

    /// Check if it's time for next frame (non-blocking)
    bool ready() const;

    /// Mark a frame as captured (updates timing)
    void frame_captured();

    /// Reset timing
    void reset();

    /// Get current FPS
    [[nodiscard]] double current_fps() const noexcept { return current_fps_; }

    /// Get target FPS
    [[nodiscard]] uint32_t target_fps() const noexcept { return target_fps_; }

    /// Get frame interval in microseconds
    [[nodiscard]] int64_t frame_interval_us() const noexcept {
        return target_fps_ > 0 ? 1000000LL / target_fps_ : 0;
    }

    /// Get time since last frame in microseconds
    [[nodiscard]] int64_t elapsed_since_last_us() const;

    /// Get number of frames dropped (throttled)
    [[nodiscard]] uint64_t frames_throttled() const noexcept { return throttled_; }

private:
    uint32_t target_fps_;
    double current_fps_ = 0.0;
    uint64_t throttled_ = 0;
    std::chrono::steady_clock::time_point last_frame_;
    std::chrono::steady_clock::time_point fps_window_start_;
    uint64_t fps_window_count_ = 0;
    mutable std::mutex mutex_;
};

// ====== Statistics Tracker ======
class StatsTracker {
public:
    StatsTracker();
    ~StatsTracker() = default;

    void frame_captured(uint64_t size_bytes, double capture_ms);
    void frame_encoded(uint64_t size_bytes, double encode_ms, bool keyframe);
    void frame_dropped();
    void set_dirty_pct(uint32_t pct);

    [[nodiscard]] FrameStats snapshot() const;
    void reset();
    void log_summary() const; // writes to spdlog

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ====== Convert (existing + new signatures) ======
ImageRgb convert_format(const ImageRgb& src, ImageFormat target);
ImageRgb convert_bgra_to_rgba(const ImageRgb& src);
ImageRgb convert_yuv_to_rgba(const std::vector<uint8_t>& yuv,
    uint32_t w, uint32_t h);

// New conversion functions
ImageRgb convert_nv12_to_rgba(const uint8_t* nv12, uint32_t w, uint32_t h);
ImageRgb convert_yuv420_to_rgba(const uint8_t* y_plane, uint32_t y_stride,
    const uint8_t* u_plane, uint32_t u_stride,
    const uint8_t* v_plane, uint32_t v_stride,
    uint32_t w, uint32_t h, ColorSpace cspace = ColorSpace::BT709);
ImageRgb convert_argb_to_rgba(const ImageRgb& src);
ImageRgb convert_abgr_to_rgba(const ImageRgb& src);
ImageRgb convert_rgb_to_rgba(const ImageRgb& src);
ImageRgb convert_bgr_to_rgba(const ImageRgb& src);
ImageRgb convert_gray_to_rgba(const ImageRgb& src);
ImageRgb convert_yuyv_to_rgba(const uint8_t* yuyv, uint32_t w, uint32_t h);
ImageRgb convert_uyvy_to_rgba(const uint8_t* uyvy, uint32_t w, uint32_t h);
ImageRgb convert_p010_to_rgba(const uint8_t* p010, uint32_t w, uint32_t h);

void convert_rgba_to_yuv420(const uint8_t* rgba, uint32_t w, uint32_t h,
    uint8_t* y_plane, uint32_t y_stride,
    uint8_t* u_plane, uint32_t u_stride,
    uint8_t* v_plane, uint32_t v_stride);
void convert_rgba_to_nv12(const uint8_t* rgba, uint32_t w, uint32_t h,
    uint8_t* nv12, uint32_t stride);

void premultiply_alpha(uint8_t* rgba, uint32_t w, uint32_t h, uint32_t stride);
void unpremultiply_alpha(uint8_t* rgba, uint32_t w, uint32_t h, uint32_t stride);
void fill_alpha(uint8_t* rgba, uint32_t w, uint32_t h, uint32_t stride, uint8_t alpha);

// ====== Image Scaling (free functions) ======
ImageRgb scale_image(const ImageRgb& src, uint32_t dst_w, uint32_t dst_h,
    ScaleAlgorithm algo = ScaleAlgorithm::Bilinear);
bool scale_image_into(const uint8_t* src, uint32_t src_w, uint32_t src_h,
    uint32_t src_stride, ImageFormat src_fmt,
    uint8_t* dst, uint32_t dst_w, uint32_t dst_h,
    uint32_t dst_stride, ImageFormat dst_fmt,
    ScaleAlgorithm algo = ScaleAlgorithm::Bilinear);

void scale_nearest(const uint8_t* src, uint32_t src_w, uint32_t src_h,
    uint32_t src_stride, uint8_t* dst, uint32_t dst_w, uint32_t dst_h,
    uint32_t dst_stride, uint32_t bpp);
void scale_bilinear(const uint8_t* src, uint32_t src_w, uint32_t src_h,
    uint32_t src_stride, uint8_t* dst, uint32_t dst_w, uint32_t dst_h,
    uint32_t dst_stride, uint32_t bpp);
void scale_bicubic(const uint8_t* src, uint32_t src_w, uint32_t src_h,
    uint32_t src_stride, uint8_t* dst, uint32_t dst_w, uint32_t dst_h,
    uint32_t dst_stride, uint32_t bpp);

// ====== Factory ======
std::unique_ptr<TraitCapturer> create_capturer();
std::unique_ptr<Decoder> create_decoder();
std::unique_ptr<Encoder> create_encoder();
std::unique_ptr<Recorder> create_recorder();
std::unique_ptr<Camera> create_camera();

// New factory functions
std::unique_ptr<Encoder> create_encoder_with_config(const EncoderConfig& cfg);
std::unique_ptr<Encoder> create_hw_encoder(EncoderBackend backend);
std::unique_ptr<Recorder> create_ffmpeg_recorder();
std::unique_ptr<Scaler> create_scaler(const ScaleConfig& cfg);
std::unique_ptr<FrameDifferencer> create_differencer();
std::unique_ptr<FPSLimiter> create_fps_limiter(uint32_t fps = 60);
std::unique_ptr<StatsTracker> create_stats_tracker();

// ====== Utilities ======
bool would_block_if_equal(std::vector<uint8_t>& old, const std::vector<uint8_t>& b);
bool is_x11();
bool is_wayland();
bool is_dxgi_available();
bool is_quartz_available();
bool is_nvenc_available();
bool is_amf_available();
bool is_vaapi_available();
bool is_videotoolbox_available();
bool is_qsv_available();
EncoderBackend detect_best_encoder_backend();
std::string codec_name(CodecFormat fmt);
std::string backend_name(EncoderBackend backend);
std::string image_format_name(ImageFormat fmt);

// ====== Constants ======
constexpr size_t STRIDE_ALIGN     = 64;
constexpr size_t HW_STRIDE_ALIGN  = 0;
constexpr int    PRIMARY_CAMERA_IDX = 0;
constexpr uint32_t DEFAULT_CAPTURE_TIMEOUT_MS = 5000;
constexpr uint32_t MAX_DIRTY_RECTS = 256;
constexpr uint32_t DEFAULT_DIRTY_BLOCK_SIZE = 16;
constexpr uint32_t DEFAULT_FPS = 60;
constexpr uint32_t DEFAULT_BITRATE = 5000000;
constexpr uint32_t MAX_PLANES = 4;

// ====== Platform-specific Capturer Implementations ======

#ifdef _WIN32
class DxgiCapturer : public TraitCapturer {
public:
    DxgiCapturer();
    ~DxgiCapturer() override;

    std::optional<Frame> frame(std::chrono::milliseconds timeout) override;
    bool is_gdi() const override;
    bool set_gdi() override;
    std::vector<DisplayInfo> displays() const override;
    bool select_display(uint32_t index) override;

    // Extended interface
    std::optional<Frame> capture_region(const CaptureRegion& region,
        std::chrono::milliseconds timeout) override;
    uint32_t current_display() const override { return current_display_; }
    FrameStats stats() const override;
    void reset_stats() override;
    uint32_t display_count() const override { return display_count_; }
    bool cursor_capture_supported() const override;
    void set_cursor_capture(bool enable) override;
    bool cursor_capture_enabled() const override { return cursor_enabled_; }
    uint32_t display_refresh_rate() const override;
    bool display_bounds(uint32_t index, int32_t& x, int32_t& y,
        uint32_t& w, uint32_t& h) const override;
    void set_dirty_detection(bool enable) override;
    std::vector<DirtyRect> last_dirty_rects() const override;
    bool is_capturing() const override;
    void pause() override;
    void resume() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    uint32_t current_display_ = 0;
    uint32_t display_count_   = 0;
    bool cursor_enabled_      = true;
    bool dirty_detection_     = false;
    bool gdi_fallback_        = false;
    bool paused_              = false;
    mutable std::mutex capture_mutex_;
};
#endif // _WIN32

#ifdef __linux__
class X11Capturer : public TraitCapturer {
public:
    X11Capturer();
    ~X11Capturer() override;

    std::optional<Frame> frame(std::chrono::milliseconds timeout) override;
    bool is_gdi() const override { return false; }
    bool set_gdi() override { return false; }
    std::vector<DisplayInfo> displays() const override;
    bool select_display(uint32_t index) override;

    // Extended interface
    uint32_t current_display() const override { return current_display_; }
    FrameStats stats() const override;
    void reset_stats() override;
    uint32_t display_count() const override { return display_count_; }
    bool cursor_capture_supported() const override;
    void set_cursor_capture(bool enable) override;
    bool cursor_capture_enabled() const override { return cursor_enabled_; }
    uint32_t display_refresh_rate() const override;
    bool display_bounds(uint32_t index, int32_t& x, int32_t& y,
        uint32_t& w, uint32_t& h) const override;
    void set_dirty_detection(bool enable) override;
    std::vector<DirtyRect> last_dirty_rects() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    uint32_t current_display_ = 0;
    uint32_t display_count_   = 0;
    bool cursor_enabled_      = false;
    bool dirty_detection_     = false;
    bool shm_available_       = false;
    mutable std::mutex capture_mutex_;
};
#endif // __linux__

#ifdef __APPLE__
class QuartzCapturer : public TraitCapturer {
public:
    QuartzCapturer();
    ~QuartzCapturer() override;

    std::optional<Frame> frame(std::chrono::milliseconds timeout) override;
    bool is_gdi() const override { return false; }
    bool set_gdi() override { return false; }
    std::vector<DisplayInfo> displays() const override;
    bool select_display(uint32_t index) override;

    // Extended interface
    std::optional<Frame> capture_region(const CaptureRegion& region,
        std::chrono::milliseconds timeout) override;
    uint32_t current_display() const override { return current_display_; }
    FrameStats stats() const override;
    void reset_stats() override;
    uint32_t display_count() const override { return display_count_; }
    bool cursor_capture_supported() const override { return false; }
    void set_cursor_capture(bool enable) override;
    bool cursor_capture_enabled() const override { return cursor_enabled_; }
    uint32_t display_refresh_rate() const override;
    bool display_bounds(uint32_t index, int32_t& x, int32_t& y,
        uint32_t& w, uint32_t& h) const override;
    bool is_capturing() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    uint32_t current_display_ = 0;
    uint32_t display_count_   = 0;
    bool cursor_enabled_      = false;
    mutable std::mutex capture_mutex_;
};
#endif // __APPLE__

// ====== Encoder Implementations ======

class SoftwareEncoder : public Encoder {
public:
    SoftwareEncoder();
    ~SoftwareEncoder() override;

    bool open(CodecFormat fmt, uint32_t w, uint32_t h,
        uint32_t fps = 30, uint32_t bitrate = 2000000) override;
    std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) override;
    bool flush(std::vector<uint8_t>& out) override;
    void close() override;

    bool open_with_config(const EncoderConfig& cfg) override;
    bool is_open() const override;
    EncoderConfig config() const override;
    bool reconfigure(const EncoderConfig& cfg) override;
    bool set_bitrate(uint32_t bitrate) override;
    bool set_quality(EncoderQuality q) override;
    bool force_keyframe() override;
    int get_delay_frames() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#ifdef _WIN32
class NvencEncoder : public Encoder {
public:
    NvencEncoder();
    ~NvencEncoder() override;

    bool open(CodecFormat fmt, uint32_t w, uint32_t h,
        uint32_t fps = 30, uint32_t bitrate = 2000000) override;
    std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) override;
    bool flush(std::vector<uint8_t>& out) override;
    void close() override;

    bool open_with_config(const EncoderConfig& cfg) override;
    bool is_open() const override;
    EncoderConfig config() const override;
    bool reconfigure(const EncoderConfig& cfg) override;
    bool set_bitrate(uint32_t bitrate) override;
    bool force_keyframe() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class AmfEncoder : public Encoder {
public:
    AmfEncoder();
    ~AmfEncoder() override;

    bool open(CodecFormat fmt, uint32_t w, uint32_t h,
        uint32_t fps = 30, uint32_t bitrate = 2000000) override;
    std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) override;
    bool flush(std::vector<uint8_t>& out) override;
    void close() override;

    bool open_with_config(const EncoderConfig& cfg) override;
    bool is_open() const override;
    EncoderConfig config() const override;
    bool reconfigure(const EncoderConfig& cfg) override;
    bool set_bitrate(uint32_t bitrate) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class QsvEncoder : public Encoder {
public:
    QsvEncoder();
    ~QsvEncoder() override;

    bool open(CodecFormat fmt, uint32_t w, uint32_t h,
        uint32_t fps = 30, uint32_t bitrate = 2000000) override;
    std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) override;
    bool flush(std::vector<uint8_t>& out) override;
    void close() override;

    bool open_with_config(const EncoderConfig& cfg) override;
    bool is_open() const override;
    EncoderConfig config() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#endif // _WIN32

#ifdef __linux__
class VaapiEncoder : public Encoder {
public:
    VaapiEncoder();
    ~VaapiEncoder() override;

    bool open(CodecFormat fmt, uint32_t w, uint32_t h,
        uint32_t fps = 30, uint32_t bitrate = 2000000) override;
    std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) override;
    bool flush(std::vector<uint8_t>& out) override;
    void close() override;

    bool open_with_config(const EncoderConfig& cfg) override;
    bool is_open() const override;
    EncoderConfig config() const override;
    bool reconfigure(const EncoderConfig& cfg) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#endif // __linux__

#ifdef __APPLE__
class VideoToolboxEncoder : public Encoder {
public:
    VideoToolboxEncoder();
    ~VideoToolboxEncoder() override;

    bool open(CodecFormat fmt, uint32_t w, uint32_t h,
        uint32_t fps = 30, uint32_t bitrate = 2000000) override;
    std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) override;
    bool flush(std::vector<uint8_t>& out) override;
    void close() override;

    bool open_with_config(const EncoderConfig& cfg) override;
    bool is_open() const override;
    EncoderConfig config() const override;
    bool reconfigure(const EncoderConfig& cfg) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#endif // __APPLE__

// ====== FFmpeg-based Recorder ======
class FFmpegRecorder : public Recorder {
public:
    FFmpegRecorder();
    ~FFmpegRecorder() override;

    bool start(const Context& ctx) override;
    bool feed(const Frame& frame) override;
    void stop() override;
    bool is_recording() const override;

    bool feed_audio(const std::vector<uint8_t>& samples,
        uint32_t sample_count) override;
    bool pause() override;
    bool resume() override;
    bool is_paused() const override;
    int64_t duration_ms() const override;
    uint64_t bytes_written() const override;
    FrameStats stats() const override;
    bool split_file() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ====== Platform-specific Camera Implementations ======

#ifdef _WIN32
class DShowCamera : public Camera {
public:
    DShowCamera();
    ~DShowCamera() override;

    bool open(uint32_t index = 0) override;
    std::optional<Frame> capture() override;
    void close() override;
    std::vector<std::string> list_cameras() override;

    bool open_by_name(const std::string& name) override;
    bool is_open() const override;
    bool set_resolution(uint32_t w, uint32_t h) override;
    bool set_fps(uint32_t fps) override;
    bool set_format(ImageFormat fmt) override;
    std::pair<uint32_t, uint32_t> resolution() const override;
    uint32_t camera_fps() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#endif // _WIN32

#ifdef __linux__
class V4L2Camera : public Camera {
public:
    V4L2Camera();
    ~V4L2Camera() override;

    bool open(uint32_t index = 0) override;
    std::optional<Frame> capture() override;
    void close() override;
    std::vector<std::string> list_cameras() override;

    bool open_by_name(const std::string& name) override;
    bool is_open() const override;
    bool set_resolution(uint32_t w, uint32_t h) override;
    bool set_fps(uint32_t fps) override;
    bool set_format(ImageFormat fmt) override;
    std::pair<uint32_t, uint32_t> resolution() const override;
    uint32_t camera_fps() const override;
    bool has_autofocus() const override;
    bool set_autofocus(bool enable) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#endif // __linux__

#ifdef __APPLE__
class AVFCamera : public Camera {
public:
    AVFCamera();
    ~AVFCamera() override;

    bool open(uint32_t index = 0) override;
    std::optional<Frame> capture() override;
    void close() override;
    std::vector<std::string> list_cameras() override;

    bool open_by_name(const std::string& name) override;
    bool is_open() const override;
    bool set_resolution(uint32_t w, uint32_t h) override;
    bool set_fps(uint32_t fps) override;
    bool set_format(ImageFormat fmt) override;
    std::pair<uint32_t, uint32_t> resolution() const override;
    uint32_t camera_fps() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#endif // __APPLE__

// ====== Utility: CRC64 for frame hashing ======
uint64_t crc64_fast(const uint8_t* data, size_t len, uint64_t seed = 0);

// ====== Utility: aligned allocation ======
uint8_t* alloc_aligned(size_t size, size_t alignment = STRIDE_ALIGN);
void free_aligned(uint8_t* ptr);

} // namespace scrap
