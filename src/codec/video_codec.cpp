#include "common/protocol.hpp"
#include "common/config.hpp"
#include <spdlog/spdlog.h>
#include <vector>
#include <cstring>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <optional>

namespace cppdesk::codec::video {

using namespace common;

// Codec identifiers
enum class CodecId : uint32_t {
    RAW = 0,
    H264 = 1,
    H265 = 2,
    VP8 = 3,
    VP9 = 4,
    AV1 = 5,
    MJPEG = 6,
};

struct H264Config {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 30;
    uint32_t bitrate = 2000000; // 2 Mbps
    uint32_t gop_size = 60;
    uint32_t quality = 75; // 0-100
    std::string preset = "medium"; // ultrafast, veryfast, fast, medium, slow
    std::string profile = "baseline"; // baseline, main, high
    bool cabac = true;
    void validate() {
        width = std::max(1u, std::min(width, 7680u));
        height = std::max(1u, std::min(height, 4320u));
        fps = std::max(1u, std::min(fps, 240u));
        bitrate = std::max(1000u, std::min(bitrate, 500000000u));
        gop_size = std::max(1u, std::min(gop_size, 600u));
        quality = std::max(0u, std::min(quality, 100u));
    }
};

struct H265Config {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 30;
    uint32_t bitrate = 2000000; // 2 Mbps
    uint32_t gop_size = 60;
    uint32_t quality = 75; // 0-100
    std::string preset = "medium"; // ultrafast, veryfast, fast, medium, slow
    std::string profile = "baseline"; // baseline, main, high
    bool cabac = true;
    void validate() {
        width = std::max(1u, std::min(width, 7680u));
        height = std::max(1u, std::min(height, 4320u));
        fps = std::max(1u, std::min(fps, 240u));
        bitrate = std::max(1000u, std::min(bitrate, 500000000u));
        gop_size = std::max(1u, std::min(gop_size, 600u));
        quality = std::max(0u, std::min(quality, 100u));
    }
};

struct VP8Config {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 30;
    uint32_t bitrate = 2000000; // 2 Mbps
    uint32_t gop_size = 60;
    uint32_t quality = 75; // 0-100
    uint32_t cpu_used = 4; // 0-8, lower=slower better quality
    bool lossless = false;
    void validate() {
        width = std::max(1u, std::min(width, 7680u));
        height = std::max(1u, std::min(height, 4320u));
        fps = std::max(1u, std::min(fps, 240u));
        bitrate = std::max(1000u, std::min(bitrate, 500000000u));
        gop_size = std::max(1u, std::min(gop_size, 600u));
        quality = std::max(0u, std::min(quality, 100u));
    }
};

struct VP9Config {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 30;
    uint32_t bitrate = 2000000; // 2 Mbps
    uint32_t gop_size = 60;
    uint32_t quality = 75; // 0-100
    uint32_t cpu_used = 4; // 0-8, lower=slower better quality
    bool lossless = false;
    void validate() {
        width = std::max(1u, std::min(width, 7680u));
        height = std::max(1u, std::min(height, 4320u));
        fps = std::max(1u, std::min(fps, 240u));
        bitrate = std::max(1000u, std::min(bitrate, 500000000u));
        gop_size = std::max(1u, std::min(gop_size, 600u));
        quality = std::max(0u, std::min(quality, 100u));
    }
};

struct AV1Config {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 30;
    uint32_t bitrate = 2000000; // 2 Mbps
    uint32_t gop_size = 60;
    uint32_t quality = 75; // 0-100
    uint32_t cpu_used = 4; // 0-8, lower=slower better quality
    bool lossless = false;
    void validate() {
        width = std::max(1u, std::min(width, 7680u));
        height = std::max(1u, std::min(height, 4320u));
        fps = std::max(1u, std::min(fps, 240u));
        bitrate = std::max(1000u, std::min(bitrate, 500000000u));
        gop_size = std::max(1u, std::min(gop_size, 600u));
        quality = std::max(0u, std::min(quality, 100u));
    }
};

struct MJPEGConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 30;
    uint32_t bitrate = 2000000; // 2 Mbps
    uint32_t gop_size = 60;
    uint32_t quality = 75; // 0-100
    void validate() {
        width = std::max(1u, std::min(width, 7680u));
        height = std::max(1u, std::min(height, 4320u));
        fps = std::max(1u, std::min(fps, 240u));
        bitrate = std::max(1000u, std::min(bitrate, 500000000u));
        gop_size = std::max(1u, std::min(gop_size, 600u));
        quality = std::max(0u, std::min(quality, 100u));
    }
};

struct RAWConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 30;
    uint32_t bitrate = 2000000; // 2 Mbps
    uint32_t gop_size = 60;
    uint32_t quality = 75; // 0-100
    void validate() {
        width = std::max(1u, std::min(width, 7680u));
        height = std::max(1u, std::min(height, 4320u));
        fps = std::max(1u, std::min(fps, 240u));
        bitrate = std::max(1000u, std::min(bitrate, 500000000u));
        gop_size = std::max(1u, std::min(gop_size, 600u));
        quality = std::max(0u, std::min(quality, 100u));
    }
};

// Abstract codec encoder
class VideoEncoder {
public:
    virtual ~VideoEncoder() = default;
    virtual bool open(const H264Config& cfg) = 0;
    virtual std::vector<uint8_t> encode(const VideoFrame& frame) = 0;
    virtual bool flush(std::vector<uint8_t>& out) = 0;
    virtual void close() = 0;
    virtual CodecId codec_id() const = 0;
    virtual std::string codec_name() const = 0;

protected:
    std::atomic<bool> opened_{false};
    H264Config config_;
    uint64_t frame_count_ = 0;
};

// ====== H264 Encoder ======
class H264Encoder : public VideoEncoder {
public:
    H264Encoder() = default;
    ~H264Encoder() override { close(); }
    
    bool open(const H264Config& cfg) override {
        config_ = cfg;
        config_.validate();
        spdlog::info("H264 encoder opening: {}x{} @{}fps, {}bps",
            cfg.width, cfg.height, cfg.fps, cfg.bitrate);
        // Initialize H264 encoder
        opened_ = true;
        frame_count_ = 0;
        return true;
    }
    
    std::vector<uint8_t> encode(const VideoFrame& frame) override {
        if (!opened_) return {};
        frame_count_++;
        // Encode frame to H264
        std::vector<uint8_t> encoded;
        encoded.reserve(frame.data.size() / 4); // rough estimate
        // In real implementation, call FFmpeg/x264/x265/etc
        bool is_keyframe = (frame_count_ % config_.gop_size == 1);
        // Placeholder: copy raw data (would be compressed)
        encoded = frame.data; // TODO: actual H264 encoding
        return encoded;
    }
    
    bool flush(std::vector<uint8_t>& out) override {
        if (!opened_) return false;
        // Flush delayed frames
        out.clear();
        return true;
    }
    
    void close() override {
        if (opened_) {
            spdlog::info("H264 encoder closed after {} frames", frame_count_);
            opened_ = false;
        }
    }
    
    CodecId codec_id() const override { return CodecId::H264; }
    std::string codec_name() const override { return "H264"; }
    
private:
    // Real implementation would hold encoder context
    void* encoder_ctx_ = nullptr;
};

// ====== H265 Encoder ======
class H265Encoder : public VideoEncoder {
public:
    H265Encoder() = default;
    ~H265Encoder() override { close(); }
    
    bool open(const H264Config& cfg) override {
        config_ = cfg;
        config_.validate();
        spdlog::info("H265 encoder opening: {}x{} @{}fps, {}bps",
            cfg.width, cfg.height, cfg.fps, cfg.bitrate);
        // Initialize H265 encoder
        opened_ = true;
        frame_count_ = 0;
        return true;
    }
    
    std::vector<uint8_t> encode(const VideoFrame& frame) override {
        if (!opened_) return {};
        frame_count_++;
        // Encode frame to H265
        std::vector<uint8_t> encoded;
        encoded.reserve(frame.data.size() / 4); // rough estimate
        // In real implementation, call FFmpeg/x264/x265/etc
        bool is_keyframe = (frame_count_ % config_.gop_size == 1);
        // Placeholder: copy raw data (would be compressed)
        encoded = frame.data; // TODO: actual H265 encoding
        return encoded;
    }
    
    bool flush(std::vector<uint8_t>& out) override {
        if (!opened_) return false;
        // Flush delayed frames
        out.clear();
        return true;
    }
    
    void close() override {
        if (opened_) {
            spdlog::info("H265 encoder closed after {} frames", frame_count_);
            opened_ = false;
        }
    }
    
    CodecId codec_id() const override { return CodecId::H265; }
    std::string codec_name() const override { return "H265"; }
    
private:
    // Real implementation would hold encoder context
    void* encoder_ctx_ = nullptr;
};

// ====== VP8 Encoder ======
class VP8Encoder : public VideoEncoder {
public:
    VP8Encoder() = default;
    ~VP8Encoder() override { close(); }
    
    bool open(const H264Config& cfg) override {
        config_ = cfg;
        config_.validate();
        spdlog::info("VP8 encoder opening: {}x{} @{}fps, {}bps",
            cfg.width, cfg.height, cfg.fps, cfg.bitrate);
        // Initialize VP8 encoder
        opened_ = true;
        frame_count_ = 0;
        return true;
    }
    
    std::vector<uint8_t> encode(const VideoFrame& frame) override {
        if (!opened_) return {};
        frame_count_++;
        // Encode frame to VP8
        std::vector<uint8_t> encoded;
        encoded.reserve(frame.data.size() / 4); // rough estimate
        // In real implementation, call FFmpeg/x264/x265/etc
        bool is_keyframe = (frame_count_ % config_.gop_size == 1);
        // Placeholder: copy raw data (would be compressed)
        encoded = frame.data; // TODO: actual VP8 encoding
        return encoded;
    }
    
    bool flush(std::vector<uint8_t>& out) override {
        if (!opened_) return false;
        // Flush delayed frames
        out.clear();
        return true;
    }
    
    void close() override {
        if (opened_) {
            spdlog::info("VP8 encoder closed after {} frames", frame_count_);
            opened_ = false;
        }
    }
    
    CodecId codec_id() const override { return CodecId::VP8; }
    std::string codec_name() const override { return "VP8"; }
    
private:
    // Real implementation would hold encoder context
    void* encoder_ctx_ = nullptr;
};

// ====== VP9 Encoder ======
class VP9Encoder : public VideoEncoder {
public:
    VP9Encoder() = default;
    ~VP9Encoder() override { close(); }
    
    bool open(const H264Config& cfg) override {
        config_ = cfg;
        config_.validate();
        spdlog::info("VP9 encoder opening: {}x{} @{}fps, {}bps",
            cfg.width, cfg.height, cfg.fps, cfg.bitrate);
        // Initialize VP9 encoder
        opened_ = true;
        frame_count_ = 0;
        return true;
    }
    
    std::vector<uint8_t> encode(const VideoFrame& frame) override {
        if (!opened_) return {};
        frame_count_++;
        // Encode frame to VP9
        std::vector<uint8_t> encoded;
        encoded.reserve(frame.data.size() / 4); // rough estimate
        // In real implementation, call FFmpeg/x264/x265/etc
        bool is_keyframe = (frame_count_ % config_.gop_size == 1);
        // Placeholder: copy raw data (would be compressed)
        encoded = frame.data; // TODO: actual VP9 encoding
        return encoded;
    }
    
    bool flush(std::vector<uint8_t>& out) override {
        if (!opened_) return false;
        // Flush delayed frames
        out.clear();
        return true;
    }
    
    void close() override {
        if (opened_) {
            spdlog::info("VP9 encoder closed after {} frames", frame_count_);
            opened_ = false;
        }
    }
    
    CodecId codec_id() const override { return CodecId::VP9; }
    std::string codec_name() const override { return "VP9"; }
    
private:
    // Real implementation would hold encoder context
    void* encoder_ctx_ = nullptr;
};

// ====== AV1 Encoder ======
class AV1Encoder : public VideoEncoder {
public:
    AV1Encoder() = default;
    ~AV1Encoder() override { close(); }
    
    bool open(const H264Config& cfg) override {
        config_ = cfg;
        config_.validate();
        spdlog::info("AV1 encoder opening: {}x{} @{}fps, {}bps",
            cfg.width, cfg.height, cfg.fps, cfg.bitrate);
        // Initialize AV1 encoder
        opened_ = true;
        frame_count_ = 0;
        return true;
    }
    
    std::vector<uint8_t> encode(const VideoFrame& frame) override {
        if (!opened_) return {};
        frame_count_++;
        // Encode frame to AV1
        std::vector<uint8_t> encoded;
        encoded.reserve(frame.data.size() / 4); // rough estimate
        // In real implementation, call FFmpeg/x264/x265/etc
        bool is_keyframe = (frame_count_ % config_.gop_size == 1);
        // Placeholder: copy raw data (would be compressed)
        encoded = frame.data; // TODO: actual AV1 encoding
        return encoded;
    }
    
    bool flush(std::vector<uint8_t>& out) override {
        if (!opened_) return false;
        // Flush delayed frames
        out.clear();
        return true;
    }
    
    void close() override {
        if (opened_) {
            spdlog::info("AV1 encoder closed after {} frames", frame_count_);
            opened_ = false;
        }
    }
    
    CodecId codec_id() const override { return CodecId::AV1; }
    std::string codec_name() const override { return "AV1"; }
    
private:
    // Real implementation would hold encoder context
    void* encoder_ctx_ = nullptr;
};

// ====== Video Decoder ======
class VideoDecoder {
public:
    virtual ~VideoDecoder() = default;
    virtual bool open(CodecId codec) = 0;
    virtual std::optional<VideoFrame> decode(const std::vector<uint8_t>& data) = 0;
    virtual void close() = 0;
    virtual CodecId codec_id() const = 0;
};

class H264Decoder : public VideoDecoder {
public:
    bool open(CodecId codec) override {
        spdlog::info("H264 decoder opened");
        return true;
    }
    std::optional<VideoFrame> decode(const std::vector<uint8_t>& data) override {
        VideoFrame frame;
        frame.codec = static_cast<uint32_t>(CodecId::H264);
        frame.data = data;
        return frame;
    }
    void close() override {}
    CodecId codec_id() const override { return CodecId::H264; }
};

class H265Decoder : public VideoDecoder {
public:
    bool open(CodecId codec) override {
        spdlog::info("H265 decoder opened");
        return true;
    }
    std::optional<VideoFrame> decode(const std::vector<uint8_t>& data) override {
        VideoFrame frame;
        frame.codec = static_cast<uint32_t>(CodecId::H265);
        frame.data = data;
        return frame;
    }
    void close() override {}
    CodecId codec_id() const override { return CodecId::H265; }
};

class VP8Decoder : public VideoDecoder {
public:
    bool open(CodecId codec) override {
        spdlog::info("VP8 decoder opened");
        return true;
    }
    std::optional<VideoFrame> decode(const std::vector<uint8_t>& data) override {
        VideoFrame frame;
        frame.codec = static_cast<uint32_t>(CodecId::VP8);
        frame.data = data;
        return frame;
    }
    void close() override {}
    CodecId codec_id() const override { return CodecId::VP8; }
};

class VP9Decoder : public VideoDecoder {
public:
    bool open(CodecId codec) override {
        spdlog::info("VP9 decoder opened");
        return true;
    }
    std::optional<VideoFrame> decode(const std::vector<uint8_t>& data) override {
        VideoFrame frame;
        frame.codec = static_cast<uint32_t>(CodecId::VP9);
        frame.data = data;
        return frame;
    }
    void close() override {}
    CodecId codec_id() const override { return CodecId::VP9; }
};

class AV1Decoder : public VideoDecoder {
public:
    bool open(CodecId codec) override {
        spdlog::info("AV1 decoder opened");
        return true;
    }
    std::optional<VideoFrame> decode(const std::vector<uint8_t>& data) override {
        VideoFrame frame;
        frame.codec = static_cast<uint32_t>(CodecId::AV1);
        frame.data = data;
        return frame;
    }
    void close() override {}
    CodecId codec_id() const override { return CodecId::AV1; }
};

class MJPEGDecoder : public VideoDecoder {
public:
    bool open(CodecId codec) override {
        spdlog::info("MJPEG decoder opened");
        return true;
    }
    std::optional<VideoFrame> decode(const std::vector<uint8_t>& data) override {
        VideoFrame frame;
        frame.codec = static_cast<uint32_t>(CodecId::MJPEG);
        frame.data = data;
        return frame;
    }
    void close() override {}
    CodecId codec_id() const override { return CodecId::MJPEG; }
};

class RAWDecoder : public VideoDecoder {
public:
    bool open(CodecId codec) override {
        spdlog::info("RAW decoder opened");
        return true;
    }
    std::optional<VideoFrame> decode(const std::vector<uint8_t>& data) override {
        VideoFrame frame;
        frame.codec = static_cast<uint32_t>(CodecId::RAW);
        frame.data = data;
        return frame;
    }
    void close() override {}
    CodecId codec_id() const override { return CodecId::RAW; }
};

// ====== Codec Factory ======
class CodecFactory {
public:
    static std::unique_ptr<VideoEncoder> create_encoder(CodecId id) {
        switch (id) {
            case CodecId::H264: return std::make_unique<H264Encoder>();
            case CodecId::H265: return std::make_unique<H265Encoder>();
            case CodecId::VP8: return std::make_unique<VP8Encoder>();
            case CodecId::VP9: return std::make_unique<VP9Encoder>();
            case CodecId::AV1: return std::make_unique<AV1Encoder>();
            case CodecId::RAW:
            case CodecId::MJPEG:
            default: return std::make_unique<H264Encoder>();
        }
    }
    
    static std::unique_ptr<VideoDecoder> create_decoder(CodecId id) {
        switch (id) {
            case CodecId::H264: return std::make_unique<H264Decoder>();
            case CodecId::H265: return std::make_unique<H265Decoder>();
            case CodecId::VP8: return std::make_unique<VP8Decoder>();
            case CodecId::VP9: return std::make_unique<VP9Decoder>();
            case CodecId::AV1: return std::make_unique<AV1Decoder>();
            case CodecId::MJPEG: return std::make_unique<MJPEGDecoder>();
            case CodecId::RAW:
            default: return std::make_unique<RAWDecoder>();
        }
    }
    
    static std::vector<std::pair<CodecId, std::string>> get_supported_codecs() {
        return {
            {CodecId::H264, "H264"},
            {CodecId::H265, "H265"},
            {CodecId::VP8, "VP8"},
            {CodecId::VP9, "VP9"},
            {CodecId::AV1, "AV1"},
            {CodecId::MJPEG, "MJPEG"},
            {CodecId::RAW, "RAW"},
        };
    }
    
    static CodecId codec_from_name(const std::string& name) {
        if (name == "H264") return CodecId::H264;
        if (name == "H265") return CodecId::H265;
        if (name == "VP8") return CodecId::VP8;
        if (name == "VP9") return CodecId::VP9;
        if (name == "AV1") return CodecId::AV1;
        if (name == "MJPEG") return CodecId::MJPEG;
        if (name == "RAW") return CodecId::RAW;
        return CodecId::H264;
    }
};

// Quality presets
struct QualityPreset {
    std::string name;
    uint32_t bitrate;
    uint32_t fps;
    uint32_t quality;
};

const std::vector<QualityPreset> QUALITY_PRESETS = {
    {"best", 50000000, 60, 100},
    {"high", 10000000, 60, 90},
    {"medium", 5000000, 30, 75},
    {"balanced", 2000000, 30, 60},
    {"low", 1000000, 15, 50},
    {"minimum", 500000, 10, 30},
};

const std::vector<Resolution> RESOLUTION_PRESETS = {
    {7680, 4320}, // 8K
    {3840, 2160}, // 4K
    {2560, 1440}, // QHD
    {1920, 1080}, // FHD
    {1366, 768}, // HD
    {1280, 720}, // HD-Ready
    {1024, 768}, // XGA
    {800, 600}, // SVGA
};

// Adaptive bitrate controller
class BitrateController {
public:
    BitrateController(uint32_t initial_bitrate = 5000000)
        : current_bitrate_(initial_bitrate),
          min_bitrate_(500000), max_bitrate_(50000000),
          target_bitrate_(initial_bitrate) {}
    
    void set_limits(uint32_t min, uint32_t max) {
        min_bitrate_ = min;
        max_bitrate_ = max;
    }
    
    uint32_t update(double loss_rate, double rtt_ms) {
        if (loss_rate > 0.05) {
            current_bitrate_ = static_cast<uint32_t>(current_bitrate_ * 0.8);
        } else if (loss_rate < 0.01 && rtt_ms < 50) {
            current_bitrate_ = static_cast<uint32_t>(current_bitrate_ * 1.05);
        }
        current_bitrate_ = std::clamp(current_bitrate_, min_bitrate_, max_bitrate_);
        return current_bitrate_;
    }
    
    uint32_t current() const { return current_bitrate_; }
    
    struct Stats {
        uint32_t bitrate;
        double loss_rate;
        double rtt_ms;
        int64_t frames_encoded;
        int64_t bytes_encoded;
    };
    
    Stats get_stats() const {
        return {current_bitrate_, last_loss_, last_rtt_,
            frames_, bytes_};
    }
    
private:
    std::atomic<uint32_t> current_bitrate_;
    uint32_t min_bitrate_, max_bitrate_, target_bitrate_;
    double last_loss_ = 0, last_rtt_ = 0;
    int64_t frames_ = 0, bytes_ = 0;
};

} // namespace cppdesk::codec::video