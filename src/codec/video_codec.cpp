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

// ============================================================
// SECTION 1: Rate Control Algorithms
// ============================================================

enum class RateControlMode : uint8_t {
    CBR = 0,  // Constant Bitrate
    VBR = 1,  // Variable Bitrate
    CQP = 2,  // Constant Quantization Parameter
    CRF = 3,  // Constant Rate Factor
    ABR = 4,  // Average Bitrate
    CAPPED_VBR = 5, // VBR with upper bound
    CAPPED_CRF = 6, // CRF with VBV constraints
};

constexpr const char* rate_control_mode_name(RateControlMode m) {
    switch (m) {
        case RateControlMode::CBR: return "CBR";
        case RateControlMode::VBR: return "VBR";
        case RateControlMode::CQP: return "CQP";
        case RateControlMode::CRF: return "CRF";
        case RateControlMode::ABR: return "ABR";
        case RateControlMode::CAPPED_VBR: return "CAPPED_VBR";
        case RateControlMode::CAPPED_CRF: return "CAPPED_CRF";
    }
    return "UNKNOWN";
}

// VBV (Video Buffering Verifier) model parameters
struct VBVParams {
    uint64_t buffer_size = 4000000;      // bytes
    uint64_t initial_fill = 3600000;     // bytes (90% fill)
    double max_bitrate = 50000000.0;     // bits per second
    bool strict_cbr = false;
    double buffer_occupancy = 0.0;       // current fill in bits
    double target_occupancy = 0.0;
};

// Detailed rate control configuration
struct RateControlConfig {
    RateControlMode mode = RateControlMode::VBR;
    uint32_t target_bitrate = 5000000;       // bps
    uint32_t max_bitrate = 10000000;          // bps
    uint32_t min_bitrate = 500000;            // bps
    double qp_min = 0.0;
    double qp_max = 51.0;
    double qp_initial = 26.0;
    double crf_value = 23.0;                  // CRF: 0-51, lower=better
    uint32_t vbv_bufsize = 4000000;           // bytes
    uint32_t vbv_maxrate = 50000000;          // bps
    double vbv_init = 0.9;                    // initial fill fraction
    uint32_t lookahead_frames = 40;
    uint32_t qp_step = 4;
    double ip_factor = 1.4;                   // I-frame QP = P-frame QP / ip_factor
    double pb_factor = 1.3;                   // B-frame QP = P-frame QP * pb_factor
    double aq_strength = 0.5;                 // adaptive quantization strength
    bool aq_mode = true;
    double qp_compress = 0.6;
    uint32_t rc_lookahead = 40;
    double scene_cut_threshold = 0.4;
    uint32_t keyint_min = 25;
    bool mb_tree = true;

    void validate() {
        target_bitrate = std::clamp(target_bitrate, 1000u, 800000000u);
        max_bitrate = std::clamp(max_bitrate, target_bitrate, 1000000000u);
        min_bitrate = std::clamp(min_bitrate, 1000u, target_bitrate);
        qp_min = std::clamp(qp_min, 0.0, 51.0);
        qp_max = std::clamp(qp_max, qp_min, 51.0);
        qp_initial = std::clamp(qp_initial, qp_min, qp_max);
        crf_value = std::clamp(crf_value, 0.0, 51.0);
        lookahead_frames = std::clamp(lookahead_frames, 0u, 250u);
        aq_strength = std::clamp(aq_strength, 0.0, 2.0);
        keyint_min = std::clamp(keyint_min, 1u, 600u);
    }
};

// Base class for all rate controllers
class RateControllerBase {
public:
    explicit RateControllerBase(const RateControlConfig& cfg)
        : config_(cfg), frame_count_(0), total_bits_(0) {
        config_.validate();
        current_qp_ = config_.qp_initial;
        spdlog::info("RateController [{}] initialized: target={}kbps, qp_init={:.1f}",
            rate_control_mode_name(config_.mode),
            config_.target_bitrate / 1000, config_.qp_initial);
    }

    virtual ~RateControllerBase() = default;

    virtual double get_qp(uint64_t frame_num, bool is_keyframe, bool is_bframe) = 0;
    virtual void report_frame(uint64_t frame_num, size_t bits, double psnr) = 0;
    virtual void set_fps(uint32_t fps) { fps_ = fps; }

    uint32_t frame_count() const { return frame_count_; }
    double current_qp() const { return current_qp_; }
    RateControlMode mode() const { return config_.mode; }

protected:
    RateControlConfig config_;
    double current_qp_;
    uint32_t fps_ = 30;
    uint64_t frame_count_;
    uint64_t total_bits_;
    VBVParams vbv_;
};

// CBR (Constant Bitrate) Rate Controller
class CBRRateController : public RateControllerBase {
public:
    explicit CBRRateController(const RateControlConfig& cfg)
        : RateControllerBase(cfg) {
        double bits_per_frame = static_cast<double>(config_.target_bitrate) / fps_;
        vbv_.buffer_size = config_.vbv_bufsize * 8;
        vbv_.initial_fill = static_cast<uint64_t>(vbv_.buffer_size * config_.vbv_init);
        vbv_.buffer_occupancy = static_cast<double>(vbv_.initial_fill);
        vbv_.target_occupancy = vbv_.buffer_occupancy;
        target_bits_per_frame_ = bits_per_frame;
        spdlog::info("CBR: target_bits_per_frame={:.0f}, vbv_buf={}bits",
            target_bits_per_frame_, vbv_.buffer_size);
    }

    double get_qp(uint64_t frame_num, bool is_keyframe, bool is_bframe) override {
        frame_count_++;

        // Adjust QP based on VBV buffer fullness
        double fill_ratio = vbv_.buffer_occupancy / static_cast<double>(vbv_.buffer_size);

        if (fill_ratio < 0.1) {
            // Buffer near empty: increase QP aggressively
            current_qp_ += 3.0;
        } else if (fill_ratio < 0.25) {
            current_qp_ += 1.5;
        } else if (fill_ratio > 0.9) {
            // Buffer near full: decrease QP to consume more bits
            current_qp_ -= 1.5;
        } else if (fill_ratio > 0.75) {
            current_qp_ -= 0.5;
        }

        // Keyframe boost: use lower QP for I-frames
        if (is_keyframe) {
            current_qp_ = std::max(config_.qp_min, current_qp_ / config_.ip_factor);
        }

        // B-frame penalty: higher QP for B-frames
        if (is_bframe) {
            current_qp_ *= config_.pb_factor;
        }

        current_qp_ = std::clamp(current_qp_, config_.qp_min, config_.qp_max);

        // Predict frame bits and update VBV occupancy
        double predicted_bits = target_bits_per_frame_ *
            std::pow(2.0, (config_.qp_initial - current_qp_) / 6.0);
        vbv_.buffer_occupancy -= predicted_bits;

        spdlog::debug("CBR frame={} qp={:.2f} fill_ratio={:.3f} pred_bits={:.0f}",
            frame_num, current_qp_, fill_ratio, predicted_bits);

        return current_qp_;
    }

    void report_frame(uint64_t frame_num, size_t bits, double psnr) override {
        total_bits_ += bits;
        double actual_bits = static_cast<double>(bits);
        // Refill VBV at target rate
        double refill = target_bits_per_frame_;
        vbv_.buffer_occupancy += refill;
        vbv_.buffer_occupancy = std::min(vbv_.buffer_occupancy,
            static_cast<double>(vbv_.buffer_size));

        // Adjust QP for next frame based on overshoot/undershoot
        double error = actual_bits - target_bits_per_frame_;
        double pct_error = error / target_bits_per_frame_;
        current_qp_ += pct_error * config_.qp_compress;

        spdlog::debug("CBR report: frame={} bits={} psnr={:.2f} occupancy={:.0f} err={:.2f}%",
            frame_num, bits, psnr, vbv_.buffer_occupancy, pct_error * 100.0);
    }

private:
    double target_bits_per_frame_ = 0.0;
};

// VBR (Variable Bitrate) Rate Controller
class VBRRateController : public RateControllerBase {
public:
    explicit VBRRateController(const RateControlConfig& cfg)
        : RateControllerBase(cfg) {
        target_bits_per_frame_ = static_cast<double>(config_.target_bitrate) / fps_;
        max_bits_per_frame_ = static_cast<double>(config_.max_bitrate) / fps_;
        spdlog::info("VBR: target={:.0f}/frame, max={:.0f}/frame, complexity_blur={:.1f}",
            target_bits_per_frame_, max_bits_per_frame_, complexity_blur_);
    }

    double get_qp(uint64_t frame_num, bool is_keyframe, bool is_bframe) override {
        frame_count_++;

        double qp = current_qp_;

        // Complexity-based QP adjustment
        if (frame_num > 0) {
            double complexity_ratio = current_complexity_ /
                std::max(1.0, average_complexity_);
            // High complexity: lower QP (use more bits)
            qp -= (complexity_ratio - 1.0) * config_.aq_strength * 3.0;
            // Low complexity: raise QP (save bits)
        }

        // Buffer-based adjustment
        double bits_used_ratio = total_bits_ > 0 ?
            static_cast<double>(total_bits_) / (frame_num * target_bits_per_frame_) : 1.0;
        if (bits_used_ratio > 1.1) {
            qp += 1.0;  // Overspending, raise QP
        } else if (bits_used_ratio < 0.9) {
            qp -= 1.0;  // Underspending, lower QP
        }

        if (is_keyframe) {
            qp = std::max(config_.qp_min, qp / config_.ip_factor);
        }
        if (is_bframe) {
            qp *= config_.pb_factor;
        }

        qp = std::clamp(qp, config_.qp_min, config_.qp_max);
        current_qp_ = qp;
        return qp;
    }

    void report_frame(uint64_t frame_num, size_t bits, double psnr) override {
        total_bits_ += bits;
        // Update complexity metrics
        double frame_complexity = static_cast<double>(bits) * std::pow(2.0, current_qp_ / 6.0);
        current_complexity_ = frame_complexity;
        average_complexity_ = average_complexity_ * complexity_blur_ +
            frame_complexity * (1.0 - complexity_blur_);

        // Budget tracking
        double expected_total = frame_num * target_bits_per_frame_;
        double budget_ratio = static_cast<double>(total_bits_) / std::max(1.0, expected_total);

        spdlog::debug("VBR report: frame={} bits={} qp={:.2f} budget={:.2f}% "
            "complex={:.0f} avg_complex={:.0f}",
            frame_num, bits, current_qp_, budget_ratio * 100.0,
            frame_complexity, average_complexity_);
    }

    void set_complexity_blur(double blur) {
        complexity_blur_ = std::clamp(blur, 0.0, 1.0);
    }

private:
    double target_bits_per_frame_ = 0.0;
    double max_bits_per_frame_ = 0.0;
    double complexity_blur_ = 0.8;
    double current_complexity_ = 1.0;
    double average_complexity_ = 1.0;
};

// CQP (Constant Quantization Parameter) Rate Controller
class CQPRateController : public RateControllerBase {
public:
    explicit CQPRateController(const RateControlConfig& cfg)
        : RateControllerBase(cfg) {
        // CQP uses fixed QP values for different frame types
        qp_i_ = std::max(config_.qp_min, config_.qp_initial / config_.ip_factor);
        qp_p_ = config_.qp_initial;
        qp_b_ = config_.qp_initial * config_.pb_factor;
        spdlog::info("CQP: QP_I={:.1f} QP_P={:.1f} QP_B={:.1f}",
            qp_i_, qp_p_, qp_b_);
    }

    double get_qp(uint64_t frame_num, bool is_keyframe, bool is_bframe) override {
        frame_count_++;
        double qp;
        if (is_keyframe) {
            qp = qp_i_;
        } else if (is_bframe) {
            qp = qp_b_;
        } else {
            qp = qp_p_;
        }
        qp = std::clamp(qp, config_.qp_min, config_.qp_max);
        current_qp_ = qp;

        spdlog::debug("CQP frame={} type={} qp={:.1f}",
            frame_num, is_keyframe ? 'I' : (is_bframe ? 'B' : 'P'), qp);
        return qp;
    }

    void report_frame(uint64_t frame_num, size_t bits, double psnr) override {
        total_bits_ += bits;
        // CQP does not adapt QP — purely informational logging
        double avg_bitrate = frame_num > 0 ?
            static_cast<double>(total_bits_) * 8.0 * fps_ / frame_num : 0.0;
        spdlog::debug("CQP report: frame={} bits={} psnr={:.2f} avg_rate={:.0f}bps",
            frame_num, bits, psnr, avg_bitrate);
    }

    void set_qp(double qp_i, double qp_p, double qp_b) {
        qp_i_ = std::clamp(qp_i, config_.qp_min, config_.qp_max);
        qp_p_ = std::clamp(qp_p, config_.qp_min, config_.qp_max);
        qp_b_ = std::clamp(qp_b, config_.qp_min, config_.qp_max);
    }

private:
    double qp_i_ = 22.0;
    double qp_p_ = 26.0;
    double qp_b_ = 30.0;
};

// CRF (Constant Rate Factor) Rate Controller
class CRFRateController : public RateControllerBase {
public:
    explicit CRFRateController(const RateControlConfig& cfg)
        : RateControllerBase(cfg) {
        crf_ = config_.crf_value;
        // CRF maps to target QP loosely; lower CRF = higher quality
        base_qp_ = crf_to_qp(crf_);
        current_qp_ = base_qp_;
        spdlog::info("CRF: crf={:.1f} base_qp={:.1f} aq_mode={} aq_strength={:.2f}",
            crf_, base_qp_, config_.aq_mode, config_.aq_strength);
    }

    double get_qp(uint64_t frame_num, bool is_keyframe, bool is_bframe) override {
        frame_count_++;

        double qp = base_qp_;

        // Adaptive QP based on frame complexity trend
        if (config_.aq_mode && frame_num > 0) {
            double complexity_delta = current_complexity_ - average_complexity_;
            // Normalize by average complexity
            double norm_delta = average_complexity_ > 0 ?
                complexity_delta / average_complexity_ : 0.0;
            qp -= norm_delta * config_.aq_strength * 6.0;
        }

        // Motion-based adjustment: high motion scenes get slightly higher QP
        if (motion_score_ > 0.7) {
            qp += 1.0;
        } else if (motion_score_ < 0.3) {
            qp -= 0.5;
        }

        if (is_keyframe) {
            qp = std::max(config_.qp_min, qp / config_.ip_factor);
        }
        if (is_bframe) {
            qp *= config_.pb_factor;
        }

        qp = std::clamp(qp, config_.qp_min, config_.qp_max);
        current_qp_ = qp;
        return qp;
    }

    void report_frame(uint64_t frame_num, size_t bits, double psnr) override {
        total_bits_ += bits;
        // Update complexity tracking
        double frame_complexity = static_cast<double>(bits) *
            std::pow(2.0, (current_qp_ - base_qp_) / 6.0);
        current_complexity_ = frame_complexity;
        average_complexity_ = average_complexity_ * 0.85 + frame_complexity * 0.15;

        spdlog::debug("CRF report: frame={} bits={} qp={:.2f} psnr={:.2f} "
            "complex={:.0f} motion={:.2f}",
            frame_num, bits, current_qp_, psnr, frame_complexity, motion_score_);
    }

    void set_crf(double crf) {
        crf_ = std::clamp(crf, 0.0, 51.0);
        base_qp_ = crf_to_qp(crf_);
    }

    void set_motion_score(double score) {
        motion_score_ = std::clamp(score, 0.0, 1.0);
    }

    double get_crf() const { return crf_; }

private:
    double crf_to_qp(double crf) const {
        // Approximate CRF to QP mapping (codec-dependent, here a linear approx)
        // CRF 0 = lossless -> QP 0; CRF 23 ~ QP 26; CRF 51 -> QP 51
        return crf;
    }

    double crf_ = 23.0;
    double base_qp_ = 26.0;
    double current_complexity_ = 1.0;
    double average_complexity_ = 1.0;
    double motion_score_ = 0.5;
};

// ABR (Average Bitrate) Rate Controller — one-pass average
class ABRRateController : public RateControllerBase {
public:
    explicit ABRRateController(const RateControlConfig& cfg)
        : RateControllerBase(cfg) {
        bits_per_frame_ = static_cast<double>(config_.target_bitrate) / fps_;
        spdlog::info("ABR: target={}kbps bits_per_frame={:.1f} convergence={:.2f}",
            config_.target_bitrate / 1000, bits_per_frame_, convergence_speed_);
    }

    double get_qp(uint64_t frame_num, bool is_keyframe, bool is_bframe) override {
        frame_count_++;

        double qp = current_qp_;

        // Adjust QP to converge toward target bitrate
        if (frame_num > 1) {
            double current_avg_bits = total_bits_ > 0 ?
                static_cast<double>(total_bits_) / (frame_num - 1) : bits_per_frame_;
            double ratio = current_avg_bits / bits_per_frame_;

            // Logarithmic QP adjustment: QP change ~= 6 * log2(ratio)
            double delta_qp = 6.0 * std::log2(std::max(0.1, ratio));
            qp += delta_qp * convergence_speed_;
        }

        if (is_keyframe) {
            qp = std::max(config_.qp_min, qp / config_.ip_factor);
        }
        if (is_bframe) {
            qp *= config_.pb_factor;
        }

        qp = std::clamp(qp, config_.qp_min, config_.qp_max);
        current_qp_ = qp;
        return qp;
    }

    void report_frame(uint64_t frame_num, size_t bits, double psnr) override {
        total_bits_ += bits;
        double current_avg = total_bits_ > 0 ?
            static_cast<double>(total_bits_) * 8.0 * fps_ /
            std::max(1ULL, static_cast<uint64_t>(frame_num)) : 0.0;

        spdlog::debug("ABR report: frame={} bits={} qp={:.2f} avg={:.0f}bps target={}bps",
            frame_num, bits, current_qp_, current_avg, config_.target_bitrate);
    }

    void set_convergence(double speed) {
        convergence_speed_ = std::clamp(speed, 0.1, 2.0);
    }

private:
    double bits_per_frame_ = 0.0;
    double convergence_speed_ = 0.5;
};

// Factory function for rate controllers
inline std::unique_ptr<RateControllerBase> make_rate_controller(
    const RateControlConfig& cfg) {
    switch (cfg.mode) {
        case RateControlMode::CBR:
            return std::make_unique<CBRRateController>(cfg);
        case RateControlMode::VBR:
            return std::make_unique<VBRRateController>(cfg);
        case RateControlMode::CQP:
            return std::make_unique<CQPRateController>(cfg);
        case RateControlMode::CRF:
            return std::make_unique<CRFRateController>(cfg);
        case RateControlMode::ABR:
            return std::make_unique<ABRRateController>(cfg);
        case RateControlMode::CAPPED_VBR:
        case RateControlMode::CAPPED_CRF:
        default:
            return std::make_unique<VBRRateController>(cfg);
    }
}

// ============================================================
// SECTION 2: Scene Change Detection
// ============================================================

// YCbCr histogram with 64 bins per channel
struct YUVHistogram {
    static constexpr size_t NUM_BINS = 64;
    std::array<uint32_t, NUM_BINS> y_hist{};
    std::array<uint32_t, NUM_BINS> cb_hist{};
    std::array<uint32_t, NUM_BINS> cr_hist{};
    size_t total_pixels = 0;

    void reset() {
        y_hist.fill(0);
        cb_hist.fill(0);
        cr_hist.fill(0);
        total_pixels = 0;
    }

    void add_sample(uint8_t y, uint8_t cb, uint8_t cr) {
        y_hist[y * NUM_BINS / 256]++;
        cb_hist[cb * NUM_BINS / 256]++;
        cr_hist[cr * NUM_BINS / 256]++;
        total_pixels++;
    }

    // Compute histogram difference between two histograms
    // Returns value in [0.0, 1.0]; higher = more different
    double difference(const YUVHistogram& other) const {
        if (total_pixels == 0 || other.total_pixels == 0) return 1.0;

        double diff_sum = 0.0;
        double norm_self = 0.0;
        double norm_other = 0.0;

        for (size_t i = 0; i < NUM_BINS; i++) {
            double v1 = static_cast<double>(y_hist[i]) / total_pixels;
            double v2 = static_cast<double>(other.y_hist[i]) / other.total_pixels;
            diff_sum += std::abs(v1 - v2);
            norm_self += v1;
            norm_other += v2;
        }

        // Bhattacharyya-like distance for Y channel
        double y_diff = diff_sum / 2.0;

        // Add chroma difference with lower weight
        double cb_diff_sum = 0.0;
        double cr_diff_sum = 0.0;
        for (size_t i = 0; i < NUM_BINS; i++) {
            double cb1 = static_cast<double>(cb_hist[i]) / total_pixels;
            double cb2 = static_cast<double>(other.cb_hist[i]) / other.total_pixels;
            cb_diff_sum += std::abs(cb1 - cb2);

            double cr1 = static_cast<double>(cr_hist[i]) / total_pixels;
            double cr2 = static_cast<double>(other.cr_hist[i]) / other.total_pixels;
            cr_diff_sum += std::abs(cr1 - cr2);
        }

        double chroma_diff = (cb_diff_sum + cr_diff_sum) / 4.0;
        return y_diff * 0.7 + chroma_diff * 0.3;
    }
};

// Motion vector statistics for scene change detection
struct MotionStats {
    double avg_magnitude = 0.0;
    double max_magnitude = 0.0;
    double variance = 0.0;
    size_t num_vectors = 0;
    size_t intra_count = 0;       // blocks coded as intra (no good match)
    size_t total_blocks = 0;
};

// Scene change detector combining histogram and motion-based methods
class SceneChangeDetector {
public:
    SceneChangeDetector(double threshold = 0.4, uint32_t lookahead = 40)
        : threshold_(threshold),
          lookahead_frames_(lookahead),
          frame_count_(0) {
        spdlog::info("SceneDetector: threshold={:.2f} lookahead={}",
            threshold_, lookahead_frames_);
    }

    // Analyze a frame and detect scene cuts
    // Returns true if a scene change is detected at this frame
    bool analyze_frame(const YUVHistogram& hist, const MotionStats& motion) {
        frame_count_++;
        bool scene_cut = false;

        if (frame_count_ <= 1) {
            // First frame is always a scene start
            scene_cut = true;
        } else {
            double hist_score = previous_histogram_.difference(hist);
            double motion_score = compute_motion_score(motion);

            // Combine histogram and motion metrics
            double combined_score = hist_score * 0.6 + motion_score * 0.4;

            // Adaptive threshold based on recent history
            double adaptive_threshold = get_adaptive_threshold();

            scene_cut = (combined_score > adaptive_threshold);

            spdlog::debug("SceneDetect frame={}: hist={:.3f} motion={:.3f} "
                "combined={:.3f} threshold={:.3f} cut={}",
                frame_count_, hist_score, motion_score,
                combined_score, adaptive_threshold, scene_cut ? "YES" : "no");

            // Track scores for adaptive threshold
            recent_scores_.push_back(combined_score);
            if (recent_scores_.size() > max_recent_) {
                recent_scores_.pop_front();
            }
        }

        previous_histogram_ = hist;
        previous_motion_ = motion;

        if (scene_cut) {
            spdlog::info("Scene change detected at frame {}", frame_count_);
            last_scene_frame_ = frame_count_;
        }

        return scene_cut;
    }

    // Flash detection: rapid brightness change not a real scene cut
    bool is_flash(const YUVHistogram& hist) const {
        if (frame_count_ < 2) return false;
        double diff = previous_histogram_.difference(hist);
        // Flash typically has very high difference that reverts quickly
        return diff > 0.8;
    }

    // Fade detection across multiple frames
    double detect_fade(const YUVHistogram& hist) const {
        if (recent_scores_.empty()) return 0.0;
        double avg_recent = 0.0;
        for (auto s : recent_scores_) avg_recent += s;
        avg_recent /= recent_scores_.size();
        double current_diff = previous_histogram_.difference(hist);
        return current_diff - avg_recent;
    }

    uint64_t last_scene_frame() const { return last_scene_frame_; }
    void set_threshold(double t) { threshold_ = std::clamp(t, 0.1, 0.9); }
    double get_threshold() const { return threshold_; }

    void reset() {
        frame_count_ = 0;
        last_scene_frame_ = 0;
        recent_scores_.clear();
        previous_histogram_.reset();
        previous_motion_ = MotionStats{};
    }

    size_t frames_since_last_scene() const {
        return frame_count_ - last_scene_frame_;
    }

private:
    double compute_motion_score(const MotionStats& m) const {
        if (m.total_blocks == 0) return 0.0;
        // High ratio of intra blocks indicates poor motion matching -> scene change
        double intra_ratio = static_cast<double>(m.intra_count) / m.total_blocks;
        // Normalize average magnitude to 0-1 range
        double mag_score = std::min(1.0, m.avg_magnitude / 64.0);
        return intra_ratio * 0.7 + mag_score * 0.3;
    }

    double get_adaptive_threshold() const {
        if (recent_scores_.size() < 5) return threshold_;

        // Compute statistics of recent scores
        double mean = 0.0;
        double variance = 0.0;
        for (auto s : recent_scores_) mean += s;
        mean /= recent_scores_.size();
        for (auto s : recent_scores_) {
            double d = s - mean;
            variance += d * d;
        }
        variance /= recent_scores_.size();
        double stddev = std::sqrt(variance);

        // Adaptive threshold: mean + k * stddev, bounded
        double adaptive = mean + 2.5 * stddev;
        adaptive = std::clamp(adaptive, threshold_ * 0.8, threshold_ * 1.5);
        return adaptive;
    }

    double threshold_;
    uint32_t lookahead_frames_;
    uint64_t frame_count_ = 0;
    uint64_t last_scene_frame_ = 0;

    YUVHistogram previous_histogram_;
    MotionStats previous_motion_;
    std::deque<double> recent_scores_;
    static constexpr size_t max_recent_ = 30;
};

// ============================================================
// SECTION 3: Temporal Denoising Pre-Filter
// ============================================================

struct TemporalDenoiseConfig {
    uint32_t filter_strength = 3;        // 0-6, higher = stronger
    double noise_threshold = 8.0;        // pixel value threshold
    uint32_t temporal_radius = 2;        // number of reference frames
    double motion_threshold = 16.0;      // motion vector threshold
    bool luma_only = false;              // only filter luma channel
    double chroma_weight = 0.5;          // chroma filtering weight vs luma
    bool enable_mc = true;               // motion-compensated filtering
    double blending_factor = 0.6;        // blend between original and filtered

    void validate() {
        filter_strength = std::clamp(filter_strength, 0u, 6u);
        noise_threshold = std::clamp(noise_threshold, 1.0, 100.0);
        temporal_radius = std::clamp(temporal_radius, 1u, 8u);
        motion_threshold = std::clamp(motion_threshold, 1.0, 128.0);
        chroma_weight = std::clamp(chroma_weight, 0.0, 1.0);
        blending_factor = std::clamp(blending_factor, 0.0, 1.0);
    }
};

// Temporal denoising pre-filter (motion-compensated temporal filtering)
class TemporalDenoiser {
public:
    explicit TemporalDenoiser(const TemporalDenoiseConfig& cfg)
        : config_(cfg), frame_count_(0) {
        config_.validate();
        spdlog::info("TemporalDenoiser: strength={} radius={} noise_thr={:.1f} mc={}",
            config_.filter_strength, config_.temporal_radius,
            config_.noise_threshold, config_.enable_mc ? "on" : "off");
    }

    // Apply temporal denoising to a frame using reference frames
    // frame_data: pointer to YUV420p plane data
    // stride: row stride in pixels
    // width/height: frame dimensions
    // mv_field: optional motion vector field (for motion-compensated filtering)
    void process_frame(uint8_t* frame_data[3],
                       const uint32_t stride[3],
                       uint32_t width, uint32_t height,
                       const std::vector<int16_t>* mv_field = nullptr) {
        frame_count_++;

        if (config_.filter_strength == 0) return;
        if (reference_frames_.empty() && frame_count_ > 1) {
            // First real frame: store as reference, no filtering
            store_reference(frame_data, stride, width, height);
            return;
        }

        // Compute noise level estimate from the frame
        double noise_estimate = estimate_noise_level(frame_data[0],
            stride[0], width, height);

        // Adjust filter strength based on estimated noise
        double effective_strength = config_.filter_strength *
            std::min(2.0, noise_estimate / config_.noise_threshold);

        int strength_i = static_cast<int>(effective_strength);

        // Process luma plane
        apply_temporal_filter_plane(frame_data[0], stride[0],
            width, height, 0, strength_i, mv_field,
            config_.enable_mc);

        // Process chroma planes if not luma-only
        if (!config_.luma_only) {
            double chroma_strength = effective_strength * config_.chroma_weight;
            int chroma_strength_i = static_cast<int>(chroma_strength);

            uint32_t cw = width / 2;
            uint32_t ch = height / 2;
            // Scale motion vectors for chroma if needed
            apply_temporal_filter_plane(frame_data[1], stride[1],
                cw, ch, 1, chroma_strength_i,
                nullptr, false); // Chroma typically without MC
            apply_temporal_filter_plane(frame_data[2], stride[2],
                cw, ch, 2, chroma_strength_i,
                nullptr, false);
        }

        // Store filtered frame as future reference
        store_reference(frame_data, stride, width, height);

        spdlog::debug("TemporalDenoiser: frame={} noise={:.2f} strength={:.1f}",
            frame_count_, noise_estimate, effective_strength);
    }

    void reset() {
        frame_count_ = 0;
        reference_frames_.clear();
    }

    // Get noise statistics
    double last_noise_estimate() const { return last_noise_estimate_; }

private:
    // Simple per-pixel temporal filtering for one plane
    void apply_temporal_filter_plane(uint8_t* dst,
                                     uint32_t stride,
                                     uint32_t width, uint32_t height,
                                     int plane_idx, int strength,
                                     const std::vector<int16_t>* mv_field,
                                     bool use_mc) {
        if (strength <= 0) return;

        // Filter weight: strength 0-6 maps to blend factor 0.0-1.0
        double alpha = std::min(1.0, strength / 6.0);
        // Blend between original and temporally filtered
        double blend = alpha * config_.blending_factor;

        // For each row
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                size_t idx = static_cast<size_t>(y) * stride + x;
                uint8_t orig = dst[idx];

                // Get motion-compensated reference pixel or co-located
                int ref_x = static_cast<int>(x);
                int ref_y = static_cast<int>(y);

                if (use_mc && mv_field && !mv_field->empty()) {
                    size_t mv_idx = (static_cast<size_t>(y) * width + x) * 2;
                    if (mv_idx + 1 < mv_field->size()) {
                        ref_x += (*mv_field)[mv_idx];
                        ref_y += (*mv_field)[mv_idx + 1];
                    }
                }

                // Clamp reference position
                ref_x = std::clamp(ref_x, 0, static_cast<int>(width) - 1);
                ref_y = std::clamp(ref_y, 0, static_cast<int>(height) - 1);

                // Get reference pixel value
                uint8_t ref_pixel = get_reference_pixel(ref_x, ref_y, plane_idx);
                if (ref_pixel == 0 && (!use_mc || (ref_x == static_cast<int>(x) &&
                    ref_y == static_cast<int>(y)))) {
                    continue; // No reference available at this position
                }

                // Weighted blend
                int filtered = static_cast<int>(orig) +
                    static_cast<int>((static_cast<int>(ref_pixel) -
                    static_cast<int>(orig)) * blend);
                dst[idx] = static_cast<uint8_t>(std::clamp(filtered, 0, 255));
            }
        }
    }

    uint8_t get_reference_pixel(int x, int y, int plane_idx) const {
        if (reference_frames_.empty()) return 0;
        const auto& ref = reference_frames_.back();
        if (plane_idx < 0 || plane_idx >= 3) return 0;
        if (x < 0 || y < 0) return 0;

        uint32_t w = ref.width;
        uint32_t h = ref.height;
        if (plane_idx > 0) { w /= 2; h /= 2; }

        if (static_cast<uint32_t>(x) >= w || static_cast<uint32_t>(y) >= h) return 0;

        size_t idx = static_cast<size_t>(y) * ref.stride[plane_idx] + x;
        if (!ref.planes[plane_idx] || idx >= ref.plane_sizes[plane_idx]) return 0;

        return ref.planes[plane_idx][idx];
    }

    void store_reference(uint8_t* const* frame_data,
                         const uint32_t* stride,
                         uint32_t width, uint32_t height) {
        RefFrame ref;
        ref.width = width;
        ref.height = height;

        for (int p = 0; p < 3; p++) {
            uint32_t pw = (p == 0) ? width : width / 2;
            uint32_t ph = (p == 0) ? height : height / 2;
            size_t plane_bytes = static_cast<size_t>(pw) * ph;
            ref.planes[p] = new uint8_t[plane_bytes];
            ref.stride[p] = stride[p];
            ref.plane_sizes[p] = plane_bytes;

            if (frame_data[p]) {
                for (uint32_t row = 0; row < ph; row++) {
                    std::memcpy(ref.planes[p] + row * pw,
                        frame_data[p] + row * stride[p], pw);
                }
            }
        }

        // Manage reference buffer: keep only temporal_radius frames
        reference_frames_.push_back(std::move(ref));
        while (reference_frames_.size() > config_.temporal_radius + 1) {
            auto& old = reference_frames_.front();
            for (int p = 0; p < 3; p++) {
                delete[] old.planes[p];
            }
            reference_frames_.pop_front();
        }
    }

    // Simple noise level estimation using high-pass difference
    double estimate_noise_level(const uint8_t* plane, uint32_t stride,
                                 uint32_t width, uint32_t height) const {
        if (width < 3 || height < 3) return 0.0;

        double sum_sq_diff = 0.0;
        size_t count = 0;

        // Sample horizontal and vertical differences
        constexpr uint32_t step = 4; // subsample for performance
        for (uint32_t y = 1; y < height - 1; y += step) {
            for (uint32_t x = 1; x < width - 1; x += step) {
                int center = plane[y * stride + x];
                int right = plane[y * stride + x + 1];
                int down = plane[(y + 1) * stride + x];
                int diff_h = center - right;
                int diff_v = center - down;
                sum_sq_diff += diff_h * diff_h + diff_v * diff_v;
                count += 2;
            }
        }

        if (count == 0) return 0.0;
        // RMS of differences as noise estimate
        double rms = std::sqrt(sum_sq_diff / count);
        last_noise_estimate_ = rms;
        return rms;
    }

    struct RefFrame {
        uint8_t* planes[3] = {nullptr, nullptr, nullptr};
        uint32_t stride[3] = {0, 0, 0};
        uint32_t width = 0;
        uint32_t height = 0;
        size_t plane_sizes[3] = {0, 0, 0};
    };

    TemporalDenoiseConfig config_;
    uint64_t frame_count_;
    std::deque<RefFrame> reference_frames_;
    mutable double last_noise_estimate_ = 0.0;
};

// ============================================================
// SECTION 4: Deblocking Filter Configuration
// ============================================================

enum class DeblockFilterMode : uint8_t {
    DISABLED = 0,
    WEAK = 1,
    NORMAL = 2,
    STRONG = 3,
    ADAPTIVE = 4,
};

struct DeblockConfig {
    DeblockFilterMode mode = DeblockFilterMode::NORMAL;
    int32_t alpha_c0_offset = 0;   // H.264: -6 to +6; H.265: -12 to +12
    int32_t beta_offset = 0;       // H.264: -6 to +6; H.265: -12 to +12
    int32_t tc_offset = 0;         // H.265 only: -12 to +12
    bool disable_deblock_across_slice = false;
    bool chroma_deblock = true;

    // Derive actual alpha/beta from QP and offsets (H.264-style lookup)
    int32_t alpha(int32_t qp) const {
        int32_t idx = std::clamp(qp + alpha_c0_offset, 0, 51);
        return alpha_table[idx];
    }

    int32_t beta(int32_t qp) const {
        int32_t idx = std::clamp(qp + beta_offset, 0, 51);
        return beta_table[idx];
    }

    int32_t tc0(int32_t qp) const {
        int32_t idx = std::clamp(qp + tc_offset, 0, 53);
        if (idx <= 51) return tc0_table[idx];
        // Extrapolate for qp 52,53
        return tc0_table[51] + (idx - 51);
    }

    void validate() {
        alpha_c0_offset = std::clamp(alpha_c0_offset, -12, 12);
        beta_offset = std::clamp(beta_offset, -12, 12);
        tc_offset = std::clamp(tc_offset, -12, 12);
    }

    // H.264/H.265 alpha (boundary filtering strength) lookup table
    static constexpr int32_t alpha_table[52] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 4, 4, 5, 6,
        7, 8, 9,10,12,13,15,17,20,22,
        25,28,32,36,40,45,50,56,63,71,
        80,90,101,113,127,144,162,182,203,226,
        255, 255
    };

    // H.264/H.265 beta (clipping threshold) lookup table
    static constexpr int32_t beta_table[52] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 2, 2, 2, 3,
        3, 3, 3, 4, 4, 4, 6, 6, 7, 7,
        8, 8, 9, 9,10,10,11,11,12,12,
        13,13,14,14,15,15,16,16,17,17,
        18, 18
    };

    // H.265 tc0 (chroma QP offset for deblocking) lookup
    static constexpr int32_t tc0_table[54] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 5, 5, 6,
        6, 7, 8, 9,10,11,13,14,16,18,
        20,22,24,24
    };
};

// Deblocking filter controller
class DeblockingFilter {
public:
    explicit DeblockingFilter(const DeblockConfig& cfg)
        : config_(cfg) {
        config_.validate();
        spdlog::info("DeblockingFilter: mode={} alpha_offset={} beta_offset={} "
            "chroma={}",
            deblock_mode_name(config_.mode),
            config_.alpha_c0_offset, config_.beta_offset,
            config_.chroma_deblock ? "on" : "off");
    }

    // Determine if deblocking should be applied to a given edge
    bool should_filter_edge(int32_t qp_p, int32_t qp_q,
                            int32_t p0, int32_t p1, int32_t q0, int32_t q1) {
        if (config_.mode == DeblockFilterMode::DISABLED) return false;

        int32_t avg_qp = (qp_p + qp_q) / 2;
        int32_t alpha_val = config_.alpha(avg_qp);
        int32_t beta_val = config_.beta(avg_qp);

        // Filtering decision per H.264 spec (8.7.2.2)
        bool condition1 = std::abs(p0 - q0) < alpha_val;
        bool condition2 = std::abs(p1 - p0) < beta_val;
        bool condition3 = std::abs(q1 - q0) < beta_val;

        return condition1 && condition2 && condition3;
    }

    // Apply deblocking to a single vertical/horizontal edge
    // Returns filtered pixel values
    void filter_edge(int32_t& p1, int32_t& p0, int32_t& q0, int32_t& q1,
                     int32_t qp_p, int32_t qp_q, bool chroma_edge = false) {
        int32_t avg_qp = (qp_p + qp_q) / 2;
        int32_t alpha_val = config_.alpha(avg_qp);
        int32_t beta_val = config_.beta(avg_qp);
        int32_t tc = config_.tc0(avg_qp);

        if (!should_filter_edge(qp_p, qp_q, p0, p1, q0, q1)) return;

        // Strong vs weak filtering decision
        bool strong_filter =
            (std::abs(p0 - q0) < ((alpha_val >> 2) + 2)) &&
            (std::abs(p1 - p0) < (beta_val >> 3)) &&
            (std::abs(q1 - q0) < (beta_val >> 3));

        if (strong_filter && config_.mode >= DeblockFilterMode::STRONG) {
            // Strong filtering (H.264 8.7.2.3)
            int32_t p2 = p1;
            int32_t q2 = q1;

            int32_t p0_new = std::clamp((p2 + 2*p1 + 2*p0 + 2*q0 + q1 + 4) >> 3, 0, 255);
            int32_t p1_new = std::clamp((p2 + p1 + p0 + q0 + 2) >> 2, 0, 255);
            int32_t q0_new = std::clamp((p1 + 2*p0 + 2*q0 + 2*q1 + q2 + 4) >> 3, 0, 255);
            int32_t q1_new = std::clamp((p0 + q0 + q1 + q2 + 2) >> 2, 0, 255);

            p0 = p0_new;
            p1 = p1_new;
            q0 = q0_new;
            q1 = q1_new;
        } else {
            // Weak filtering (H.264 8.7.2.4)
            int32_t delta = std::clamp((((q0 - p0) << 2) + (p1 - q1) + 4) >> 3,
                -tc, tc);
            p0 = std::clamp(p0 + delta, 0, 255);
            q0 = std::clamp(q0 - delta, 0, 255);

            if (!chroma_edge) {
                int32_t delta_p1 = std::clamp((p1 + ((p0 - p1) >> 1) - (q1 << 1)) >> 1,
                    -(tc >> 1), tc >> 1);
                int32_t delta_q1 = std::clamp((q1 + ((q0 - q1) >> 1) - (p1 << 1)) >> 1,
                    -(tc >> 1), tc >> 1);
                p1 = std::clamp(p1 + delta_p1, 0, 255);
                q1 = std::clamp(q1 + delta_q1, 0, 255);
            }
        }
    }

    void set_mode(DeblockFilterMode mode) { config_.mode = mode; }
    DeblockFilterMode mode() const { return config_.mode; }

    static const char* deblock_mode_name(DeblockFilterMode m) {
        switch (m) {
            case DeblockFilterMode::DISABLED: return "disabled";
            case DeblockFilterMode::WEAK: return "weak";
            case DeblockFilterMode::NORMAL: return "normal";
            case DeblockFilterMode::STRONG: return "strong";
            case DeblockFilterMode::ADAPTIVE: return "adaptive";
        }
        return "unknown";
    }

    DeblockConfig& config() { return config_; }

private:
    DeblockConfig config_;
};

// ============================================================
// SECTION 5: SPS/PPS NAL Unit Generation (H.264 / H.265)
// ============================================================

enum class NalUnitType : uint8_t {
    // H.264 NAL types
    H264_NON_IDR = 1,
    H264_PART_A = 2,
    H264_PART_B = 3,
    H264_PART_C = 4,
    H264_IDR = 5,
    H264_SEI = 6,
    H264_SPS = 7,
    H264_PPS = 8,
    H264_AUD = 9,
    H264_FILLER = 12,
    // H.265 NAL types
    H265_VPS = 32,
    H265_SPS = 33,
    H265_PPS = 34,
    H265_AUD = 35,
    H265_IDR_W_RADL = 19,
    H265_IDR_N_LP = 20,
    H265_CRA = 21,
    H265_TRAIL_R = 0,
    H265_TRAIL_N = 1,
    H265_TSA_R = 2,
    H265_TSA_N = 3,
    H265_STSA_R = 4,
    H265_STSA_N = 5,
    H265_RADL_R = 6,
    H265_RADL_N = 7,
    H265_RASL_R = 8,
    H265_RASL_N = 9,
    H265_SEI_PREFIX = 39,
    H265_SEI_SUFFIX = 40,
};

constexpr uint8_t H264_NAL_START_CODE[4] = {0x00, 0x00, 0x00, 0x01};
constexpr uint8_t H264_NAL_3BYTE_START[3] = {0x00, 0x00, 0x01};

// H.264 SPS (Sequence Parameter Set) generation
class H264SPSBuilder {
public:
    struct SPSData {
        uint8_t profile_idc = 100;          // High profile
        uint8_t level_idc = 40;             // Level 4.0
        uint8_t chroma_format_idc = 1;      // 4:2:0
        uint32_t width = 1920;
        uint32_t height = 1080;
        uint32_t fps = 30;
        uint32_t bit_depth_luma = 8;
        uint32_t bit_depth_chroma = 8;
        bool constraint_set0 = false;
        bool constraint_set1 = false;
        bool constraint_set2 = false;
        bool constraint_set3 = false;
        bool constraint_set4 = false;
        bool constraint_set5 = false;
        uint32_t log2_max_frame_num = 4;
        uint32_t pic_order_cnt_type = 0;
        uint32_t log2_max_pic_order_cnt_lsb = 4;
        uint32_t num_ref_frames = 4;
        bool gaps_in_frame_num_allowed = false;
        bool frame_mbs_only = true;
        bool direct_8x8_inference = true;
        bool vui_present = true;
        // VUI
        bool aspect_ratio_present = false;
        bool timing_info_present = true;
        bool nal_hrd_present = false;
        bool vcl_hrd_present = false;
        bool pic_struct_present = true;
        bool bitstream_restriction = true;
        uint32_t num_units_in_tick = 1;
        uint32_t time_scale = 60;
        bool fixed_frame_rate = true;
    };

    explicit H264SPSBuilder(const SPSData& data) : sps_(data) {}

    // Build SPS NAL unit bytes
    std::vector<uint8_t> build() const {
        BitstreamWriter bs;
        bs.start_nal(H264_NAL_START_CODE, 4);

        // NAL header
        bs.write_bits(0, 1);  // forbidden_zero_bit
        bs.write_bits(2, 2);  // nal_ref_idc
        bs.write_bits(static_cast<uint32_t>(NalUnitType::H264_SPS), 5);

        // SPS payload
        bs.write_bits(sps_.profile_idc, 8);
        bs.write_bits(sps_.constraint_set0 ? 1 : 0, 1);
        bs.write_bits(sps_.constraint_set1 ? 1 : 0, 1);
        bs.write_bits(sps_.constraint_set2 ? 1 : 0, 1);
        bs.write_bits(sps_.constraint_set3 ? 1 : 0, 1);
        bs.write_bits(sps_.constraint_set4 ? 1 : 0, 1);
        bs.write_bits(sps_.constraint_set5 ? 1 : 0, 1);
        bs.write_bits(0, 2);  // reserved
        bs.write_bits(sps_.level_idc, 8);
        bs.write_ue(sps_.chroma_format_idc == 3 ? 0 : 1); // seq_parameter_set_id

        // Chroma format info (if needed)
        if (sps_.profile_idc == 100 || sps_.profile_idc == 110 ||
            sps_.profile_idc == 122 || sps_.profile_idc == 244 ||
            sps_.profile_idc == 44 || sps_.profile_idc == 83 ||
            sps_.profile_idc == 86 || sps_.profile_idc == 118 ||
            sps_.profile_idc == 128 || sps_.profile_idc == 138) {
            bs.write_ue(sps_.chroma_format_idc);
            if (sps_.chroma_format_idc == 3) {
                bs.write_bits(0, 1); // separate_colour_plane_flag
            }
            bs.write_ue(sps_.bit_depth_luma - 8);
            bs.write_ue(sps_.bit_depth_chroma - 8);
            bs.write_bits(0, 1); // qpprime_y_zero_transform_bypass_flag
            bs.write_bits(0, 1); // seq_scaling_matrix_present_flag
        }

        bs.write_ue(sps_.log2_max_frame_num - 4);
        bs.write_ue(sps_.pic_order_cnt_type);
        if (sps_.pic_order_cnt_type == 0) {
            bs.write_ue(sps_.log2_max_pic_order_cnt_lsb - 4);
        } else if (sps_.pic_order_cnt_type == 1) {
            bs.write_bits(0, 1); // delta_pic_order_always_zero_flag
            bs.write_se(0);      // offset for non-ref pics
            bs.write_se(0);      // offset for top-to-bottom field
            bs.write_ue(0);      // num_ref_frames_in_pic_order_cnt_cycle
        }
        bs.write_ue(sps_.num_ref_frames);
        bs.write_bits(sps_.gaps_in_frame_num_allowed ? 1 : 0, 1);
        bs.write_ue((sps_.width / 16) - 1);
        bs.write_ue((sps_.height / 16) - 1);
        bs.write_bits(sps_.frame_mbs_only ? 1 : 0, 1);
        if (!sps_.frame_mbs_only) {
            bs.write_bits(0, 1); // mb_adaptive_frame_field_flag
        }
        bs.write_bits(sps_.direct_8x8_inference ? 1 : 0, 1);
        bs.write_bits(0, 1); // frame_cropping_flag (no crop)
        bs.write_bits(sps_.vui_present ? 1 : 0, 1);

        if (sps_.vui_present) {
            write_vui(bs);
        }

        bs.finish_nal();
        return bs.data();
    }

    SPSData& data() { return sps_; }

private:
    void write_vui(BitstreamWriter& bs) const {
        bs.write_bits(sps_.aspect_ratio_present ? 1 : 0, 1);
        if (sps_.aspect_ratio_present) {
            bs.write_bits(255, 8); // extended SAR
            bs.write_bits(1, 16);  // sar_width
            bs.write_bits(1, 16);  // sar_height
        }
        bs.write_bits(0, 1); // overscan_info_present_flag
        bs.write_bits(0, 1); // video_signal_type_present_flag
        bs.write_bits(0, 1); // chroma_loc_info_present_flag

        bs.write_bits(sps_.timing_info_present ? 1 : 0, 1);
        if (sps_.timing_info_present) {
            bs.write_bits(sps_.num_units_in_tick, 32);
            bs.write_bits(sps_.time_scale, 32);
            bs.write_bits(sps_.fixed_frame_rate ? 1 : 0, 1);
        }

        bs.write_bits(sps_.nal_hrd_present ? 1 : 0, 1);
        bs.write_bits(sps_.vcl_hrd_present ? 1 : 0, 1);

        if (sps_.nal_hrd_present || sps_.vcl_hrd_present) {
            // HRD parameters (simplified)
            bs.write_bits(0, 1);       // low_delay_hrd_flag
            bs.write_ue(0);            // cpb_cnt_minus1
            bs.write_bits(4, 4);       // bit_rate_scale
            bs.write_bits(6, 4);       // cpb_size_scale
        }

        bs.write_bits(sps_.pic_struct_present ? 1 : 0, 1);
        bs.write_bits(sps_.bitstream_restriction ? 1 : 0, 1);

        if (sps_.bitstream_restriction) {
            bs.write_bits(1, 1); // motion_vectors_over_pic_boundaries_flag
            bs.write_ue(0);      // max_bytes_per_pic_denom
            bs.write_ue(0);      // max_bits_per_mb_denom
            bs.write_ue(sps_.log2_max_frame_num); // log2_max_mv_length_h
            bs.write_ue(sps_.log2_max_frame_num); // log2_max_mv_length_v
            bs.write_ue(sps_.num_ref_frames);      // max_num_reorder_frames
            bs.write_ue(sps_.num_ref_frames);      // max_dec_frame_buffering
        }
    }

    SPSData sps_;
};

// H.264 PPS (Picture Parameter Set) generation
class H264PPSBuilder {
public:
    struct PPSData {
        uint32_t seq_parameter_set_id = 0;
        uint32_t pic_parameter_set_id = 0;
        bool entropy_coding_mode = true;       // CABAC
        bool pic_order_present = false;        // bottom_field_pic_order_in_frame_present
        uint32_t num_slice_groups = 1;
        uint32_t num_ref_idx_l0_active = 4;
        uint32_t num_ref_idx_l1_active = 4;
        bool weighted_pred_flag = false;
        uint8_t weighted_bipred_idc = 0;
        int32_t pic_init_qp = 26;
        int32_t pic_init_qs = 0;
        int32_t chroma_qp_index_offset = 0;
        bool deblocking_filter_control_present = true;
        bool constrained_intra_pred = false;
        bool redundant_pic_cnt_present = false;
        bool transform_8x8_mode = true;
        bool pic_scaling_matrix_present = false;
        int32_t second_chroma_qp_index_offset = 0;
    };

    explicit H264PPSBuilder(const PPSData& data) : pps_(data) {}

    std::vector<uint8_t> build() const {
        BitstreamWriter bs;
        bs.start_nal(H264_NAL_START_CODE, 4);

        // NAL header
        bs.write_bits(0, 1);
        bs.write_bits(2, 2);
        bs.write_bits(static_cast<uint32_t>(NalUnitType::H264_PPS), 5);

        // PPS payload
        bs.write_ue(pps_.pic_parameter_set_id);
        bs.write_ue(pps_.seq_parameter_set_id);
        bs.write_bits(pps_.entropy_coding_mode ? 1 : 0, 1);
        bs.write_bits(pps_.pic_order_present ? 1 : 0, 1);
        bs.write_ue(pps_.num_slice_groups - 1);

        if (pps_.num_slice_groups > 1) {
            // Slice group map (omitted for simplicity: type 0, interleaved)
            bs.write_ue(0); // slice_group_map_type = 0
        }

        bs.write_ue(pps_.num_ref_idx_l0_active - 1);
        bs.write_ue(pps_.num_ref_idx_l1_active - 1);
        bs.write_bits(pps_.weighted_pred_flag ? 1 : 0, 1);
        bs.write_bits(pps_.weighted_bipred_idc, 2);

        bs.write_se(pps_.pic_init_qp - 26);
        bs.write_se(pps_.pic_init_qs - 26);
        bs.write_se(pps_.chroma_qp_index_offset);

        bs.write_bits(pps_.deblocking_filter_control_present ? 1 : 0, 1);
        bs.write_bits(pps_.constrained_intra_pred ? 1 : 0, 1);
        bs.write_bits(pps_.redundant_pic_cnt_present ? 1 : 0, 1);

        // More RBSP data (transform_8x8, scaling matrix, chroma offsets)
        bs.write_bits(pps_.transform_8x8_mode ? 1 : 0, 1);
        bs.write_bits(pps_.pic_scaling_matrix_present ? 1 : 0, 1);
        if (pps_.pic_scaling_matrix_present) {
            // Write scaling lists (omitted for brevity)
            bs.write_bits(0, 1); // no scaling lists change
        }
        bs.write_se(pps_.second_chroma_qp_index_offset);

        bs.finish_nal();
        return bs.data();
    }

    PPSData& data() { return pps_; }

private:
    PPSData pps_;
};

// H.265 SPS generation (simplified)
class H265SPSBuilder {
public:
    struct H265SPSData {
        uint8_t sps_video_parameter_set_id = 0;
        uint8_t sps_max_sub_layers = 1;
        uint8_t sps_temporal_id_nesting = 1;
        uint8_t profile_space = 0;
        uint8_t profile_idc = 1;           // Main profile
        uint32_t level_idc = 120;           // Level 4.0 (30x)
        uint8_t chroma_format_idc = 1;      // 4:2:0
        uint8_t bit_depth_luma = 8;
        uint8_t bit_depth_chroma = 8;
        uint32_t width = 1920;
        uint32_t height = 1080;
        uint32_t fps = 30;
        uint32_t max_transform_hierarchy_depth_intra = 3;
        uint32_t max_transform_hierarchy_depth_inter = 3;
        bool scaling_list_enabled = false;
        bool amp_enabled = true;
        bool sao_enabled = true;
        bool pcm_enabled = false;
        uint32_t num_short_term_ref_pic_sets = 0;
        bool long_term_ref_pics_present = false;
        bool sps_temporal_mvp_enabled = true;
        bool strong_intra_smoothing = true;
        bool vui_present = true;
    };

    explicit H265SPSBuilder(const H265SPSData& data) : sps_(data) {}

    std::vector<uint8_t> build() const {
        BitstreamWriter bs;
        bs.start_nal(H264_NAL_START_CODE, 4);

        // NAL header (2-byte for HEVC)
        bs.write_bits(0, 1);  // forbidden
        bs.write_bits(static_cast<uint32_t>(NalUnitType::H265_SPS), 6);
        bs.write_bits(0, 6);  // layer_id
        bs.write_bits(sps_.sps_temporal_id_nesting, 3);

        // SPS payload
        bs.write_bits(sps_.sps_video_parameter_set_id, 4);
        bs.write_bits(sps_.sps_max_sub_layers - 1, 3);
        bs.write_bits(sps_.sps_temporal_id_nesting, 1);

        // Profile tier level
        bs.write_bits(sps_.profile_space, 2);
        bs.write_bits(0, 1);  // tier_flag (main tier)
        bs.write_bits(sps_.profile_idc, 5);
        bs.write_bits(0x60000000u | sps_.level_idc, 32); // profile compatibility + level

        bs.write_ue(0); // sps_seq_parameter_set_id
        bs.write_ue(sps_.chroma_format_idc);
        if (sps_.chroma_format_idc == 3) {
            bs.write_bits(0, 1); // separate_colour_plane_flag
        }
        bs.write_ue(sps_.width);
        bs.write_ue(sps_.height);

        // Conformance window (no cropping)
        bs.write_bits(0, 1); // conformance_window_flag

        bs.write_ue(sps_.bit_depth_luma - 8);
        bs.write_ue(sps_.bit_depth_chroma - 8);
        bs.write_ue(0); // log2_max_pic_order_cnt_lsb_minus4

        bs.write_bits(0, 1); // sps_sub_layer_ordering_info_present_flag
        bs.write_ue(sps_.max_transform_hierarchy_depth_intra);
        bs.write_ue(sps_.max_transform_hierarchy_depth_inter);

        bs.write_bits(sps_.scaling_list_enabled ? 1 : 0, 1);
        bs.write_bits(sps_.amp_enabled ? 1 : 0, 1);
        bs.write_bits(sps_.sao_enabled ? 1 : 0, 1);
        bs.write_bits(sps_.pcm_enabled ? 1 : 0, 1);

        if (sps_.pcm_enabled) {
            bs.write_bits(4, 4); // pcm_sample_bit_depth_luma
            bs.write_bits(4, 4); // pcm_sample_bit_depth_chroma
            bs.write_ue(0);      // log2_min_pcm_luma_coding_block_size
            bs.write_ue(0);      // log2_diff_max_min_pcm_luma_coding_block_size
            bs.write_bits(0, 1); // pcm_loop_filter_disable
        }

        bs.write_ue(sps_.num_short_term_ref_pic_sets);
        // Skip writing ref pic sets for this stub

        bs.write_bits(sps_.long_term_ref_pics_present ? 1 : 0, 1);
        bs.write_bits(sps_.sps_temporal_mvp_enabled ? 1 : 0, 1);
        bs.write_bits(sps_.strong_intra_smoothing ? 1 : 0, 1);

        bs.write_bits(sps_.vui_present ? 1 : 0, 1);

        if (sps_.vui_present) {
            // Simplified VUI for H.265
            bs.write_bits(0, 1); // aspect_ratio_info_present
            bs.write_bits(0, 1); // overscan_info_present
            bs.write_bits(0, 1); // video_signal_type_present
            bs.write_bits(0, 1); // chroma_loc_info_present
            bs.write_bits(1, 1); // timing_info_present
            bs.write_bits(1, 32); // num_units_in_tick
            bs.write_bits(sps_.fps * 2, 32); // time_scale
            bs.write_bits(0, 1); // poc_proportional_to_timing
            bs.write_bits(0, 1); // hrd_parameters_present
            bs.write_bits(0, 1); // bitstream_restriction_flag
        }

        bs.finish_nal();
        return bs.data();
    }

    H265SPSData& data() { return sps_; }

private:
    H265SPSData sps_;
};

// H.265 PPS generation (simplified)
class H265PPSBuilder {
public:
    struct H265PPSData {
        uint32_t pps_pic_parameter_set_id = 0;
        uint32_t pps_seq_parameter_set_id = 0;
        bool dependent_slice_segments = false;
        bool output_flag_present = false;
        uint32_t num_extra_slice_header_bits = 0;
        bool sign_data_hiding = false;
        bool cabac_init_present = true;
        uint32_t num_ref_idx_l0_default_active = 4;
        uint32_t num_ref_idx_l1_default_active = 4;
        int32_t init_qp = 26;
        bool constrained_intra_pred = false;
        bool transform_skip_enabled = false;
        bool cu_qp_delta_enabled = true;
        uint32_t diff_cu_qp_delta_depth = 0;
        int32_t cb_qp_offset = 0;
        int32_t cr_qp_offset = 0;
        bool pps_slice_chroma_qp_offsets_present = true;
        bool weighted_pred = false;
        bool weighted_bipred = false;
        bool transquant_bypass = false;
        bool tiles_enabled = false;
        bool entropy_coding_sync = false;
        bool loop_filter_across_tiles = true;
        bool deblocking_filter_control_present = true;
        bool pps_loop_filter_across_slices = false;
        bool pps_scaling_list_data_present = false;
        bool lists_modification = false;
        uint32_t log2_parallel_merge_level = 2;
    };

    explicit H265PPSBuilder(const H265PPSData& data) : pps_(data) {}

    std::vector<uint8_t> build() const {
        BitstreamWriter bs;
        bs.start_nal(H264_NAL_START_CODE, 4);

        // NAL header
        bs.write_bits(0, 1);
        bs.write_bits(static_cast<uint32_t>(NalUnitType::H265_PPS), 6);
        bs.write_bits(0, 6); // layer_id
        bs.write_bits(1, 3); // temporal_id

        // PPS payload
        bs.write_ue(pps_.pps_pic_parameter_set_id);
        bs.write_ue(pps_.pps_seq_parameter_set_id);
        bs.write_bits(pps_.dependent_slice_segments ? 1 : 0, 1);
        bs.write_bits(pps_.output_flag_present ? 1 : 0, 1);
        bs.write_bits(pps_.num_extra_slice_header_bits, 3);
        bs.write_bits(pps_.sign_data_hiding ? 1 : 0, 1);
        bs.write_bits(pps_.cabac_init_present ? 1 : 0, 1);
        bs.write_ue(pps_.num_ref_idx_l0_default_active - 1);
        bs.write_ue(pps_.num_ref_idx_l1_default_active - 1);
        bs.write_se(pps_.init_qp - 26);
        bs.write_bits(pps_.constrained_intra_pred ? 1 : 0, 1);
        bs.write_bits(pps_.transform_skip_enabled ? 1 : 0, 1);
        bs.write_bits(pps_.cu_qp_delta_enabled ? 1 : 0, 1);

        if (pps_.cu_qp_delta_enabled) {
            bs.write_ue(pps_.diff_cu_qp_delta_depth);
        }

        bs.write_se(pps_.cb_qp_offset);
        bs.write_se(pps_.cr_qp_offset);
        bs.write_bits(pps_.pps_slice_chroma_qp_offsets_present ? 1 : 0, 1);
        bs.write_bits(pps_.weighted_pred ? 1 : 0, 1);
        bs.write_bits(pps_.weighted_bipred ? 1 : 0, 1);
        bs.write_bits(pps_.transquant_bypass ? 1 : 0, 1);
        bs.write_bits(pps_.tiles_enabled ? 1 : 0, 1);
        bs.write_bits(pps_.entropy_coding_sync ? 1 : 0, 1);

        if (pps_.tiles_enabled) {
            // Tile structure (omitted for brevity — uniform spacing)
            bs.write_bits(1, 1); // tiles_uniform_spacing_flag
            bs.write_ue(0);      // num_tile_columns_minus1
            bs.write_ue(0);      // num_tile_rows_minus1
        }

        bs.write_bits(pps_.loop_filter_across_tiles ? 1 : 0, 1);
        bs.write_bits(pps_.deblocking_filter_control_present ? 1 : 0, 1);

        if (pps_.deblocking_filter_control_present) {
            bs.write_bits(pps_.pps_loop_filter_across_slices ? 1 : 0, 1);
            bs.write_bits(0, 1); // deblocking_filter_override_enabled
            bs.write_bits(0, 1); // pps_deblocking_filter_disabled
            bs.write_se(0);      // beta_offset_div2
            bs.write_se(0);      // tc_offset_div2
        }

        bs.write_bits(pps_.pps_scaling_list_data_present ? 1 : 0, 1);
        bs.write_bits(pps_.lists_modification ? 1 : 0, 1);
        bs.write_ue(pps_.log2_parallel_merge_level - 2);

        bs.finish_nal();
        return bs.data();
    }

    H265PPSData& data() { return pps_; }

private:
    H265PPSData pps_;
};

// ============================================================
// SECTION 6: SEI Message Handling
// ============================================================

enum class SEIMessageType : uint32_t {
    BUFFERING_PERIOD = 0,
    PIC_TIMING = 1,
    PAN_SCAN_RECT = 2,
    FILLER_PAYLOAD = 3,
    USER_DATA_REGISTERED = 4,
    USER_DATA_UNREGISTERED = 5,
    RECOVERY_POINT = 6,
    FRAME_PACKING = 45,
    DISPLAY_ORIENTATION = 47,
    MASTERING_DISPLAY_COLOUR_VOLUME = 137,
    CONTENT_LIGHT_LEVEL = 144,
    ALTERNATIVE_TRANSFER = 147,
    AMBIENT_VIEWING = 148,
};

// SEI message base
struct SEIMessage {
    SEIMessageType type;
    std::vector<uint8_t> payload;

    virtual ~SEIMessage() = default;
    virtual std::vector<uint8_t> serialize() const = 0;
};

// Pic Timing SEI (H.264 D.2.2 / H.265 D.3.3)
struct PicTimingSEI : SEIMessage {
    uint32_t cpb_removal_delay = 0;
    uint32_t dpb_output_delay = 0;
    bool pic_struct_present = false;
    uint8_t pic_struct = 0;        // 0=frame, 1=top, 2=bottom, etc.
    uint32_t clock_timestamp_flags = 0;
    uint32_t ct_type = 0;          // clock timestamp type
    bool nuit_field_based = false;
    bool full_timestamp = false;
    bool discontinuity = false;
    bool cnt_dropped = false;
    uint32_t n_frames = 0;
    uint32_t seconds = 0;
    uint32_t minutes = 0;
    uint32_t hours = 0;
    uint32_t time_offset = 0;

    PicTimingSEI() { type = SEIMessageType::PIC_TIMING; }

    std::vector<uint8_t> serialize() const override {
        BitstreamWriter bs;

        // SEI NAL header
        bs.write_bits(static_cast<uint32_t>(NalUnitType::H264_SEI), 8);
        bs.write_bits(static_cast<uint32_t>(type), 8);

        // Payload size (variable-length coding)
        size_t payload_start = bs.bit_pos();

        // Write CPB removal delay
        bs.write_bits(cpb_removal_delay, 32);
        bs.write_bits(dpb_output_delay, 32);

        if (pic_struct_present) {
            bs.write_bits(pic_struct, 4);
            // Clock timestamps
            for (int i = 0; i < 3; i++) {
                bool ct_flag = (clock_timestamp_flags >> i) & 1;
                bs.write_bits(ct_flag ? 1 : 0, 1);
                if (ct_flag) {
                    bs.write_bits(ct_type, 2);
                    bs.write_bits(nuit_field_based ? 1 : 0, 1);
                    bs.write_bits(full_timestamp ? 1 : 0, 1);
                    bs.write_bits(discontinuity ? 1 : 0, 1);
                    bs.write_bits(cnt_dropped ? 1 : 0, 1);
                    bs.write_bits(n_frames, 8);

                    if (full_timestamp) {
                        bs.write_bits(seconds, 6);
                        bs.write_bits(minutes, 6);
                        bs.write_bits(hours, 5);
                    } else {
                        bs.write_bits(seconds, 32); // seconds_flag + seconds_value
                    }

                    if (nuit_field_based) {
                        bs.write_bits(time_offset, 32);
                    }
                }
            }
        }

        // Finish SEI with rbsp_trailing_bits
        bs.finish_nal();

        return bs.data();
    }
};

// Mastering Display Colour Volume SEI (H.265 D.3.28 / H.264)
struct MasteringDisplaySEI : SEIMessage {
    // Display primaries in 0.00002 unit increments (ITU-R BT.2020)
    uint16_t display_primaries_x[3] = {13250, 7500, 34000};  // approx BT.2020
    uint16_t display_primaries_y[3] = {34500, 3000, 16000};
    uint16_t white_point_x = 15635;   // D65
    uint16_t white_point_y = 16450;
    uint32_t max_display_mastering_luminance = 10000000; // 1000 cd/m^2 (0.0001 cd/m^2 units)
    uint32_t min_display_mastering_luminance = 50;       // 0.005 cd/m^2

    MasteringDisplaySEI() { type = SEIMessageType::MASTERING_DISPLAY_COLOUR_VOLUME; }

    std::vector<uint8_t> serialize() const override {
        BitstreamWriter bs;
        bs.write_bits(static_cast<uint32_t>(NalUnitType::H264_SEI), 8);
        bs.write_bits(static_cast<uint32_t>(type), 8);

        // Payload (24 bytes for H.265 mastering display info)
        for (int i = 0; i < 3; i++) {
            bs.write_bits(display_primaries_x[i], 16);
            bs.write_bits(display_primaries_y[i], 16);
        }
        bs.write_bits(white_point_x, 16);
        bs.write_bits(white_point_y, 16);
        bs.write_bits(max_display_mastering_luminance, 32);
        bs.write_bits(min_display_mastering_luminance, 32);

        bs.finish_nal();
        return bs.data();
    }

    // Configure for common color spaces
    void set_bt709() {
        // BT.709 primaries
        display_primaries_x[0] = 15000; display_primaries_y[0] = 30000;
        display_primaries_x[1] = 15000; display_primaries_y[1] = 6000;
        display_primaries_x[2] = 32000; display_primaries_y[2] = 16500;
        white_point_x = 15635; white_point_y = 16450; // D65
    }

    void set_bt2020() {
        // BT.2020 primaries
        display_primaries_x[0] = 35400; display_primaries_y[0] = 14600;
        display_primaries_x[1] = 8500;  display_primaries_y[1] = 39850;
        display_primaries_x[2] = 6550;  display_primaries_y[2] = 2300;
        white_point_x = 15635; white_point_y = 16450;
    }

    void set_dci_p3() {
        display_primaries_x[0] = 34000; display_primaries_y[0] = 16000;
        display_primaries_x[1] = 13250; display_primaries_y[1] = 34500;
        display_primaries_x[2] = 7500;  display_primaries_y[2] = 3000;
        white_point_x = 15700; white_point_y = 17550;
    }
};

// Content Light Level SEI (H.265 D.3.35)
struct ContentLightLevelSEI : SEIMessage {
    uint16_t max_content_light_level = 1000;    // MaxCLL (cd/m^2)
    uint16_t max_picture_average_light_level = 400; // MaxFALL (cd/m^2)

    ContentLightLevelSEI() { type = SEIMessageType::CONTENT_LIGHT_LEVEL; }

    std::vector<uint8_t> serialize() const override {
        BitstreamWriter bs;
        bs.write_bits(static_cast<uint32_t>(NalUnitType::H264_SEI), 8);
        bs.write_bits(static_cast<uint32_t>(type), 8);

        bs.write_bits(max_content_light_level, 16);
        bs.write_bits(max_picture_average_light_level, 16);

        bs.finish_nal();
        return bs.data();
    }
};

// Buffering Period SEI
struct BufferingPeriodSEI : SEIMessage {
    uint32_t seq_parameter_set_id = 0;
    bool rap_cpb_params_present = false;
    bool cpb_cnt_minus1_present = false;
    uint32_t initial_cpb_removal_delay = 1000;
    uint32_t initial_alt_cpb_removal_delay = 1000;

    BufferingPeriodSEI() { type = SEIMessageType::BUFFERING_PERIOD; }

    std::vector<uint8_t> serialize() const override {
        BitstreamWriter bs;
        bs.write_bits(static_cast<uint32_t>(NalUnitType::H264_SEI), 8);
        bs.write_bits(static_cast<uint32_t>(type), 8);

        bs.write_ue(seq_parameter_set_id);
        bs.write_bits(rap_cpb_params_present ? 1 : 0, 1);
        bs.write_bits(cpb_cnt_minus1_present ? 1 : 0, 1);

        bs.write_bits(initial_cpb_removal_delay, 32); // assuming NalHrdBpPresentFlag
        bs.write_bits(initial_cpb_removal_delay, 32); // initial_cpb_removal_delay_offset

        if (rap_cpb_params_present) {
            bs.write_bits(initial_alt_cpb_removal_delay, 32);
            bs.write_bits(initial_alt_cpb_removal_delay, 32);
        }

        bs.finish_nal();
        return bs.data();
    }
};

// Recovery Point SEI
struct RecoveryPointSEI : SEIMessage {
    int32_t recovery_frame_cnt = 0;
    bool exact_match = true;
    bool broken_link = false;

    RecoveryPointSEI() { type = SEIMessageType::RECOVERY_POINT; }

    std::vector<uint8_t> serialize() const override {
        BitstreamWriter bs;
        bs.write_bits(static_cast<uint32_t>(NalUnitType::H264_SEI), 8);
        bs.write_bits(static_cast<uint32_t>(type), 8);

        bs.write_ue(static_cast<uint32_t>(std::max(0, recovery_frame_cnt)));
        bs.write_bits(exact_match ? 1 : 0, 1);
        bs.write_bits(broken_link ? 1 : 0, 1);

        bs.finish_nal();
        return bs.data();
    }
};

// SEI Message aggregator
class SEIMessageBuilder {
public:
    SEIMessageBuilder() = default;

    void add_timing(uint32_t cpb_removal, uint32_t dpb_output) {
        auto sei = std::make_shared<PicTimingSEI>();
        sei->cpb_removal_delay = cpb_removal;
        sei->dpb_output_delay = dpb_output;
        messages_.push_back(sei);
    }

    void add_mastering_display(const MasteringDisplaySEI& md) {
        messages_.push_back(std::make_shared<MasteringDisplaySEI>(md));
    }

    void add_content_light_level(uint16_t max_cll, uint16_t max_fall) {
        auto sei = std::make_shared<ContentLightLevelSEI>();
        sei->max_content_light_level = max_cll;
        sei->max_picture_average_light_level = max_fall;
        messages_.push_back(sei);
    }

    void add_buffering_period(uint32_t init_delay) {
        auto sei = std::make_shared<BufferingPeriodSEI>();
        sei->initial_cpb_removal_delay = init_delay;
        messages_.push_back(sei);
    }

    void add_recovery_point(int32_t frame_cnt) {
        auto sei = std::make_shared<RecoveryPointSEI>();
        sei->recovery_frame_cnt = frame_cnt;
        messages_.push_back(sei);
    }

    // Build combined SEI NAL unit containing all messages
    std::vector<uint8_t> build() const {
        std::vector<uint8_t> output;
        for (const auto& msg : messages_) {
            auto data = msg->serialize();
            output.insert(output.end(), data.begin(), data.end());
        }
        return output;
    }

    void clear() { messages_.clear(); }
    size_t count() const { return messages_.size(); }

private:
    std::vector<std::shared_ptr<SEIMessage>> messages_;
};

// ============================================================
// SECTION 7: Reference Frame Management (L0/L1 Lists)
// ============================================================

struct ReferenceFrame {
    uint32_t poc = 0;               // Picture Order Count
    uint32_t frame_num = 0;
    bool is_long_term = false;
    bool is_keyframe = false;
    int32_t temporal_id = 0;
    uint8_t* data = nullptr;        // frame pixel data
    size_t data_size = 0;
    int32_t qp = 26;
    double quality_score = 0.0;     // higher = better reference
    uint64_t decode_order = 0;

    // For temporal layer hierarchy
    uint8_t layer = 0;
    bool is_reference = true;
};

// Reference picture list manager for H.264 / H.265
class ReferenceFrameManager {
public:
    ReferenceFrameManager(uint32_t max_ref_frames = 16)
        : max_ref_frames_(max_ref_frames) {
        spdlog::info("RefFrameManager: max_ref_frames={}", max_ref_frames_);
    }

    // Add a newly coded frame as a reference candidate
    void add_reference(const ReferenceFrame& frame) {
        if (!frame.is_reference) return;

        // Mark and remove oldest if full
        if (reference_frames_.size() >= max_ref_frames_) {
            // Find best non-reference or oldest to evict
            auto it = std::min_element(reference_frames_.begin(),
                reference_frames_.end(),
                [](const auto& a, const auto& b) {
                    // Prefer evicting non-reference frames, then oldest
                    if (a.is_long_term != b.is_long_term)
                        return !a.is_long_term;
                    return a.decode_order < b.decode_order;
                });
            if (it != reference_frames_.end()) {
                spdlog::debug("Evicting ref frame POC={} from DPB", it->poc);
                reference_frames_.erase(it);
            }
        }

        reference_frames_.push_back(frame);

        // Sort by POC for easier L0/L1 construction
        std::sort(reference_frames_.begin(), reference_frames_.end(),
            [](const auto& a, const auto& b) {
                return a.poc < b.poc;
            });

        spdlog::debug("Added ref frame POC={} type={} DPB_size={}",
            frame.poc, frame.is_keyframe ? 'I' : 'P', reference_frames_.size());
    }

    // Build L0 reference list (preceding frames first)
    std::vector<uint32_t> build_l0_list(uint32_t current_poc,
                                         bool is_p_slice = true) const {
        std::vector<uint32_t> l0;

        // Short-term references before current POC
        for (const auto& ref : reference_frames_) {
            if (ref.poc < current_poc && !ref.is_long_term) {
                l0.push_back(ref.poc);
            }
        }

        // Sort: closer POC = higher priority
        std::sort(l0.begin(), l0.end(),
            [current_poc](uint32_t a, uint32_t b) {
                return (current_poc - a) < (current_poc - b);
            });

        // Long-term references (appended at end for P-slices)
        for (const auto& ref : reference_frames_) {
            if (ref.is_long_term) {
                l0.push_back(ref.poc);
            }
        }

        // Cap to max active references (typically 4 for P-slices)
        if (is_p_slice && l0.size() > 4) {
            l0.resize(4);
        }

        spdlog::debug("Built L0 list: current_poc={} size={}", current_poc, l0.size());
        return l0;
    }

    // Build L1 reference list (succeeding/alternating frames first)
    std::vector<uint32_t> build_l1_list(uint32_t current_poc) const {
        std::vector<uint32_t> l1;

        // Short-term references after current POC
        for (const auto& ref : reference_frames_) {
            if (ref.poc > current_poc && !ref.is_long_term) {
                l1.push_back(ref.poc);
            }
        }

        // Sort: closer POC = higher priority
        std::sort(l1.begin(), l1.end(),
            [current_poc](uint32_t a, uint32_t b) {
                return (a - current_poc) < (b - current_poc);
            });

        // If L1 is empty (e.g., P-slice case), copy L0
        if (l1.empty()) {
            auto l0 = build_l0_list(current_poc, false);
            l1.assign(l0.begin(), l0.end());
        }

        // Cap to max active references (typically 4)
        if (l1.size() > 4) {
            l1.resize(4);
        }

        spdlog::debug("Built L1 list: current_poc={} size={}", current_poc, l1.size());
        return l1;
    }

    // Mark frames as unused for reference (sliding window)
    void mark_unused(uint32_t poc) {
        auto it = std::find_if(reference_frames_.begin(), reference_frames_.end(),
            [poc](const auto& ref) { return ref.poc == poc; });
        if (it != reference_frames_.end()) {
            reference_frames_.erase(it);
        }
    }

    // Mark all frames as unused (IDR)
    void flush() {
        reference_frames_.clear();
        spdlog::debug("DPB flushed (IDR)");
    }

    // Promote a frame to long-term reference
    void promote_to_long_term(uint32_t poc) {
        for (auto& ref : reference_frames_) {
            if (ref.poc == poc) {
                ref.is_long_term = true;
                spdlog::debug("Promoted POC={} to long-term reference", poc);
                return;
            }
        }
    }

    // Demote a long-term reference back to short-term
    void demote_from_long_term(uint32_t poc) {
        for (auto& ref : reference_frames_) {
            if (ref.poc == poc) {
                ref.is_long_term = false;
                spdlog::debug("Demoted POC={} from long-term reference", poc);
                return;
            }
        }
    }

    // Find best reference for motion estimation (closest POC)
    const ReferenceFrame* find_best_ref(uint32_t current_poc,
                                         bool allow_future = false) const {
        const ReferenceFrame* best = nullptr;
        int64_t best_dist = INT64_MAX;

        for (const auto& ref : reference_frames_) {
            int64_t dist = static_cast<int64_t>(ref.poc) -
                static_cast<int64_t>(current_poc);
            if (!allow_future && dist > 0) continue;

            int64_t abs_dist = std::abs(dist);
            // Weighted: prefer closer POC, higher quality
            double score = abs_dist - ref.quality_score * 10.0;
            if (score < best_dist) {
                best_dist = static_cast<int64_t>(score);
                best = &ref;
            }
        }

        return best;
    }

    size_t dpb_size() const { return reference_frames_.size(); }
    uint32_t max_ref_frames() const { return max_ref_frames_; }

    // Dump DPB state
    void dump_dpb() const {
        spdlog::info("DPB state ({} frames):", reference_frames_.size());
        for (const auto& ref : reference_frames_) {
            spdlog::info("  POC={} frame_num={} layer={} type={} lt={} qp={} quality={:.2f}",
                ref.poc, ref.frame_num, ref.layer,
                ref.is_keyframe ? 'I' : 'P',
                ref.is_long_term ? 'Y' : 'N',
                ref.qp, ref.quality_score);
        }
    }

private:
    uint32_t max_ref_frames_;
    std::vector<ReferenceFrame> reference_frames_;
};

// ============================================================
// SECTION 8: Motion Estimation Search Patterns
// ============================================================

struct MotionVector {
    int16_t x = 0;
    int16_t y = 0;
    uint32_t cost = 0;  // SAD or SATD cost

    bool operator==(const MotionVector& other) const {
        return x == other.x && y == other.y;
    }
};

struct SearchRange {
    int16_t min_x = -64;
    int16_t max_x = 63;
    int16_t min_y = -64;
    int16_t max_y = 63;
};

// Abstract motion estimation search pattern
class MESearchPattern {
public:
    virtual ~MESearchPattern() = default;
    virtual std::string name() const = 0;

    // Perform search starting from a predictor
    // Returns best motion vector found
    virtual MotionVector search(
        const uint8_t* current_block,    // current block pixels
        const uint8_t* reference_plane,   // reference frame pixels
        uint32_t block_width,
        uint32_t block_height,
        uint32_t ref_stride,
        int16_t pred_x, int16_t pred_y,
        const SearchRange& range,
        uint32_t lambda = 0               // rate-distortion lambda
    ) = 0;

protected:
    // Compute SAD (Sum of Absolute Differences) between two blocks
    static uint32_t compute_sad(
        const uint8_t* cur, const uint8_t* ref,
        uint32_t bw, uint32_t bh,
        uint32_t cur_stride, uint32_t ref_stride) {
        uint32_t sad = 0;
        for (uint32_t y = 0; y < bh; y++) {
            for (uint32_t x = 0; x < bw; x++) {
                int diff = static_cast<int>(cur[y * cur_stride + x]) -
                    static_cast<int>(ref[y * ref_stride + x]);
                sad += std::abs(diff);
            }
        }
        return sad;
    }

    // Compute MV cost (bits for encoding the MV)
    static uint32_t mv_cost(int16_t mx, int16_t my, int16_t pmx, int16_t pmy,
                            uint32_t lambda) {
        if (lambda == 0) return 0;
        // Bits estimate: each component difference costs ~log2(abs(diff)+1)*2 bits
        auto bits_for = [](int16_t d) -> uint32_t {
            uint32_t abs_d = std::abs(d);
            if (abs_d == 0) return 1;
            uint32_t bits = 0;
            while (abs_d > 0) { bits++; abs_d >>= 1; }
            return bits * 2 + 1;
        };
        uint32_t cost = (bits_for(mx - pmx) + bits_for(my - pmy)) * lambda;
        return cost;
    }
};

// Diamond search pattern (4-point diamond, good for small motion)
class DiamondSearch : public MESearchPattern {
public:
    std::string name() const override { return "Diamond"; }

    MotionVector search(
        const uint8_t* current_block,
        const uint8_t* reference_plane,
        uint32_t block_width,
        uint32_t block_height,
        uint32_t ref_stride,
        int16_t pred_x, int16_t pred_y,
        const SearchRange& range,
        uint32_t lambda = 0
    ) override {
        // Diamond search points at each step
        static constexpr int16_t diamond_pattern[4][2] = {
            { 0, -1},  // up
            { 0,  1},  // down
            {-1,  0},  // left
            { 1,  0},  // right
        };

        static constexpr int16_t diamond_large[4][2] = {
            { 0, -2}, { 0,  2}, {-2,  0}, { 2,  0},
        };

        MotionVector best;
        best.x = pred_x;
        best.y = pred_y;

        const uint8_t* ref_at = reference_plane +
            static_cast<ptrdiff_t>(pred_y) * ref_stride + pred_x;
        best.cost = compute_sad(current_block, ref_at,
            block_width, block_height, block_width, ref_stride);

        bool improved = true;
        uint32_t step_size = 2; // Start with large diamond

        while (improved) {
            improved = false;
            const auto& pattern = (step_size == 2) ? diamond_large : diamond_pattern;

            for (int i = 0; i < 4; i++) {
                int16_t cx = best.x + pattern[i][0] * static_cast<int16_t>(step_size);
                int16_t cy = best.y + pattern[i][1] * static_cast<int16_t>(step_size);

                // Range check
                if (cx < range.min_x || cx > range.max_x ||
                    cy < range.min_y || cy > range.max_y) {
                    continue;
                }

                const uint8_t* cand_ref = reference_plane +
                    static_cast<ptrdiff_t>(cy) * ref_stride + cx;
                uint32_t sad = compute_sad(current_block, cand_ref,
                    block_width, block_height, block_width, ref_stride);
                uint32_t total_cost = sad + mv_cost(cx, cy, pred_x, pred_y, lambda);

                if (total_cost < best.cost) {
                    best.x = cx;
                    best.y = cy;
                    best.cost = total_cost;
                    improved = true;
                }
            }

            if (step_size == 2 && !improved) {
                step_size = 1; // Switch to small diamond
                improved = true;
            }
        }

        return best;
    }
};

// Hexagon search pattern (7-point hexagon, TZSearch-like)
class HexagonSearch : public MESearchPattern {
public:
    std::string name() const override { return "Hexagon"; }

    MotionVector search(
        const uint8_t* current_block,
        const uint8_t* reference_plane,
        uint32_t block_width,
        uint32_t block_height,
        uint32_t ref_stride,
        int16_t pred_x, int16_t pred_y,
        const SearchRange& range,
        uint32_t lambda = 0
    ) override {
        // Hexagon pattern points (7-point including center)
        static constexpr int16_t hex_pattern_16[7][2] = {
            { 0, -4}, { 2, -2}, { 2,  2},
            { 0,  4}, {-2,  2}, {-2, -2},
            { 0,  0},  // center
        };

        static constexpr int16_t hex_pattern_8[7][2] = {
            { 0, -2}, { 1, -1}, { 1,  1},
            { 0,  2}, {-1,  1}, {-1, -1},
            { 0,  0},
        };

        static constexpr int16_t hex_pattern_4[7][2] = {
            { 0, -1}, { 1, -1}, { 1,  0},
            { 0,  1}, {-1,  0}, {-1, -1},
            { 0,  0},
        };

        MotionVector best;
        best.x = pred_x;
        best.y = pred_y;
        const uint8_t* ref_at = reference_plane +
            static_cast<ptrdiff_t>(pred_y) * ref_stride + pred_x;
        best.cost = compute_sad(current_block, ref_at,
            block_width, block_height, block_width, ref_stride);

        // Coarse-to-fine hexagon search
        const auto& coarse = hex_pattern_16;
        const auto& medium = hex_pattern_8;
        const auto& fine = hex_pattern_4;

        // Coarse search
        bool improved;
        do {
            improved = false;
            for (int i = 0; i < 7; i++) {
                int16_t cx = best.x + coarse[i][0] * 4;
                int16_t cy = best.y + coarse[i][1] * 4;

                if (cx < range.min_x || cx > range.max_x ||
                    cy < range.min_y || cy > range.max_y) continue;

                const uint8_t* cand_ref = reference_plane +
                    static_cast<ptrdiff_t>(cy) * ref_stride + cx;
                uint32_t sad = compute_sad(current_block, cand_ref,
                    block_width, block_height, block_width, ref_stride);
                uint32_t total_cost = sad + mv_cost(cx, cy, pred_x, pred_y, lambda);

                if (total_cost < best.cost) {
                    best.x = cx; best.y = cy; best.cost = total_cost;
                    improved = true;
                }
            }
        } while (improved);

        // Medium refinement
        do {
            improved = false;
            for (int i = 0; i < 7; i++) {
                int16_t cx = best.x + medium[i][0] * 2;
                int16_t cy = best.y + medium[i][1] * 2;

                if (cx < range.min_x || cx > range.max_x ||
                    cy < range.min_y || cy > range.max_y) continue;

                const uint8_t* cand_ref = reference_plane +
                    static_cast<ptrdiff_t>(cy) * ref_stride + cx;
                uint32_t sad = compute_sad(current_block, cand_ref,
                    block_width, block_height, block_width, ref_stride);
                uint32_t total_cost = sad + mv_cost(cx, cy, pred_x, pred_y, lambda);

                if (total_cost < best.cost) {
                    best.x = cx; best.y = cy; best.cost = total_cost;
                    improved = true;
                }
            }
        } while (improved);

        // Fine refinement
        do {
            improved = false;
            for (int i = 0; i < 7; i++) {
                int16_t cx = best.x + fine[i][0];
                int16_t cy = best.y + fine[i][1];

                if (cx < range.min_x || cx > range.max_x ||
                    cy < range.min_y || cy > range.max_y) continue;

                const uint8_t* cand_ref = reference_plane +
                    static_cast<ptrdiff_t>(cy) * ref_stride + cx;
                uint32_t sad = compute_sad(current_block, cand_ref,
                    block_width, block_height, block_width, ref_stride);
                uint32_t total_cost = sad + mv_cost(cx, cy, pred_x, pred_y, lambda);

                if (total_cost < best.cost) {
                    best.x = cx; best.y = cy; best.cost = total_cost;
                    improved = true;
                }
            }
        } while (improved);

        return best;
    }
};

// UMH (Uneven Multi-Hexagon) search pattern — x264-style
class UMHSearch : public MESearchPattern {
public:
    std::string name() const override { return "UMH"; }

    MotionVector search(
        const uint8_t* current_block,
        const uint8_t* reference_plane,
        uint32_t block_width,
        uint32_t block_height,
        uint32_t ref_stride,
        int16_t pred_x, int16_t pred_y,
        const SearchRange& range,
        uint32_t lambda = 0
    ) override {
        std::vector<MotionVector> candidates;

        // Step 1: Collect predictors (median MV, zero MV, spatial neighbors)
        candidates.push_back({pred_x, pred_y, 0});
        candidates.push_back({0, 0, 0});

        // Spatial neighbor predictors (left, top, top-right)
        candidates.push_back({static_cast<int16_t>(pred_x - 8), pred_y, 0});
        candidates.push_back({pred_x, static_cast<int16_t>(pred_y - 8), 0});
        candidates.push_back({static_cast<int16_t>(pred_x + 8), static_cast<int16_t>(pred_y - 8), 0});

        // Step 2: Evaluate all candidates
        MotionVector best;
        best.cost = UINT32_MAX;

        for (auto& cand : candidates) {
            int16_t cx = std::clamp(cand.x, range.min_x, range.max_x);
            int16_t cy = std::clamp(cand.y, range.min_y, range.max_y);

            const uint8_t* cand_ref = reference_plane +
                static_cast<ptrdiff_t>(cy) * ref_stride + cx;
            uint32_t sad = compute_sad(current_block, cand_ref,
                block_width, block_height, block_width, ref_stride);
            uint32_t total_cost = sad + mv_cost(cx, cy, pred_x, pred_y, lambda);

            if (total_cost < best.cost) {
                best.x = cx; best.y = cy; best.cost = total_cost;
            }
        }

        // Step 3: Multi-hexagon grid search around best predictor
        // Uneven multi-hexagon: rings at distances 1,2,3,4,6,8,12,16,24,32,48,64
        static constexpr uint32_t umh_radii[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64};
        static constexpr size_t num_radii = sizeof(umh_radii) / sizeof(umh_radii[0]);

        for (size_t r_idx = 0; r_idx < num_radii; r_idx++) {
            uint32_t radius = umh_radii[r_idx];
            bool found_better = false;

            // Sample points on a hexagon-like ring at this radius
            // We sample 6 points per ring
            static constexpr int16_t hex_angles[6][2] = {
                {1, 0}, {1, 1}, {0, 1}, {-1, 0}, {-1, -1}, {0, -1}
            };

            for (int a = 0; a < 6; a++) {
                int16_t cx = best.x + hex_angles[a][0] * static_cast<int16_t>(radius);
                int16_t cy = best.y + hex_angles[a][1] * static_cast<int16_t>(radius);

                if (cx < range.min_x || cx > range.max_x ||
                    cy < range.min_y || cy > range.max_y) continue;

                const uint8_t* cand_ref = reference_plane +
                    static_cast<ptrdiff_t>(cy) * ref_stride + cx;
                uint32_t sad = compute_sad(current_block, cand_ref,
                    block_width, block_height, block_width, ref_stride);
                uint32_t total_cost = sad + mv_cost(cx, cy, pred_x, pred_y, lambda);

                if (total_cost < best.cost) {
                    best.x = cx; best.y = cy; best.cost = total_cost;
                    found_better = true;
                }
            }

            // Early termination: if no improvement at this radius and we're
            // past the first few rings, stop searching larger radii
            if (!found_better && r_idx >= 3) {
                break;
            }
        }

        // Step 4: Final refinement with small diamond around best
        static constexpr int16_t refine[4][2] = {
            {0, -1}, {0, 1}, {-1, 0}, {1, 0}
        };
        bool refined;
        do {
            refined = false;
            for (int i = 0; i < 4; i++) {
                int16_t cx = best.x + refine[i][0];
                int16_t cy = best.y + refine[i][1];

                if (cx < range.min_x || cx > range.max_x ||
                    cy < range.min_y || cy > range.max_y) continue;

                const uint8_t* cand_ref = reference_plane +
                    static_cast<ptrdiff_t>(cy) * ref_stride + cx;
                uint32_t sad = compute_sad(current_block, cand_ref,
                    block_width, block_height, block_width, ref_stride);
                uint32_t total_cost = sad + mv_cost(cx, cy, pred_x, pred_y, lambda);

                if (total_cost < best.cost) {
                    best.x = cx; best.y = cy; best.cost = total_cost;
                    refined = true;
                }
            }
        } while (refined);

        return best;
    }
};

// Motion search pattern factory
inline std::unique_ptr<MESearchPattern> make_search_pattern(const std::string& name) {
    if (name == "diamond" || name == "dia") {
        return std::make_unique<DiamondSearch>();
    } else if (name == "hexagon" || name == "hex") {
        return std::make_unique<HexagonSearch>();
    } else if (name == "umh") {
        return std::make_unique<UMHSearch>();
    }
    // Default to hexagon
    spdlog::warn("Unknown ME search pattern '{}', defaulting to hexagon", name);
    return std::make_unique<HexagonSearch>();
}

// ============================================================
// SECTION 9: SAO (Sample Adaptive Offset) Configuration
// ============================================================

enum class SAOType : uint8_t {
    NONE = 0,
    BAND_OFFSET = 1,
    EDGE_OFFSET = 2,
};

enum class SAOEOClass : uint8_t {
    EO_0 = 0,  // horizontal (0 degrees)
    EO_1 = 1,  // vertical (90 degrees)
    EO_2 = 2,  // 135 degrees (diagonal)
    EO_3 = 3,  // 45 degrees (anti-diagonal)
};

// SAO parameters for one CTU
struct SAOCTUParams {
    SAOType type_luma = SAOType::NONE;
    SAOType type_chroma = SAOType::NONE;
    SAOEOClass eo_class_luma = SAOEOClass::EO_0;
    SAOEOClass eo_class_chroma = SAOEOClass::EO_0;

    // Band offset: 4 consecutive bands, each with an offset
    int8_t band_position_luma = 0;     // starting band [0-31]
    int8_t band_position_cb = 0;
    int8_t band_position_cr = 0;

    // Offsets: 4 offsets for BO, 4 offsets for EO categories
    int8_t offsets_luma[4] = {0, 0, 0, 0};
    int8_t offsets_cb[4] = {0, 0, 0, 0};
    int8_t offsets_cr[4] = {0, 0, 0, 0};

    bool merge_up = false;
    bool merge_left = false;

    // For EO: sign values [-2,-1,0,1,2]
    uint8_t sign_luma = 0;
    uint8_t sign_chroma = 0;
};

// SAO configuration and parameter estimation
class SAOConfigurator {
public:
    SAOConfigurator(bool enable_sao = true)
        : sao_enabled_(enable_sao) {
        spdlog::info("SAOConfigurator: enabled={}", sao_enabled_);
    }

    // Estimate SAO parameters for a CTU
    // Returns optimized SAO parameters
    SAOCTUParams estimate_params(
        const uint8_t* original,       // original (pre-deblock) CTU pixels
        const uint8_t* reconstructed,   // reconstructed (post-deblock) CTU pixels
        uint32_t ctu_size,              // CTU width/height (32 or 64)
        uint32_t stride,                // frame stride
        int32_t qp
    ) {
        SAOCTUParams best_params;
        if (!sao_enabled_) return best_params;

        double best_cost = std::numeric_limits<double>::max();

        // Try all SAO types: OFF, EO, BO
        // Try OFF first
        double off_cost = compute_distortion(original, reconstructed,
            ctu_size, stride, nullptr);
        best_cost = off_cost;

        // Try Edge Offset (all 4 classes)
        for (int eo_class = 0; eo_class < 4; eo_class++) {
            SAOCTUParams params;
            params.type_luma = SAOType::EDGE_OFFSET;
            params.eo_class_luma = static_cast<SAOEOClass>(eo_class);

            // Estimate optimal offsets for this EO class
            estimate_eo_offsets(original, reconstructed, ctu_size, stride,
                params.eo_class_luma, qp, params.offsets_luma);

            double cost = compute_distortion(original, reconstructed,
                ctu_size, stride, &params);
            double rate_penalty = compute_sao_rate(params);

            if (cost + rate_penalty < best_cost) {
                best_cost = cost + rate_penalty;
                best_params = params;
                best_params.type_chroma = SAOType::NONE; // simplified
            }
        }

        // Try Band Offset
        for (int band_start = 0; band_start < 28; band_start++) {
            SAOCTUParams params;
            params.type_luma = SAOType::BAND_OFFSET;
            params.band_position_luma = static_cast<int8_t>(band_start);

            estimate_bo_offsets(original, reconstructed, ctu_size, stride,
                band_start, qp, params.offsets_luma);

            double cost = compute_distortion(original, reconstructed,
                ctu_size, stride, &params);
            double rate_penalty = compute_sao_rate(params);

            if (cost + rate_penalty < best_cost) {
                best_cost = cost + rate_penalty;
                best_params = params;
                best_params.type_chroma = SAOType::NONE;
            }
        }

        // Apply best merge flags (simplified: always allow merge)
        best_params.merge_up = true;
        best_params.merge_left = true;

        spdlog::debug("SAO CTU params: type={} eo_class={} band_pos={} "
            "offsets=[{},{},{},{}] cost={:.1f}",
            static_cast<int>(best_params.type_luma),
            static_cast<int>(best_params.eo_class_luma),
            best_params.band_position_luma,
            best_params.offsets_luma[0], best_params.offsets_luma[1],
            best_params.offsets_luma[2], best_params.offsets_luma[3],
            best_cost);

        return best_params;
    }

    // Apply SAO to a CTU using the given parameters
    void apply_sao(uint8_t* pixels, uint32_t ctu_size, uint32_t stride,
                   const SAOCTUParams& params) {
        if (params.type_luma == SAOType::NONE) return;

        // Create a copy of input pixels
        std::vector<uint8_t> input(ctu_size * stride);
        for (uint32_t y = 0; y < ctu_size; y++) {
            std::memcpy(input.data() + y * stride,
                pixels + y * stride, ctu_size);
        }

        if (params.type_luma == SAOType::EDGE_OFFSET) {
            apply_eo(pixels, input.data(), ctu_size, stride,
                params.eo_class_luma, params.offsets_luma);
        } else if (params.type_luma == SAOType::BAND_OFFSET) {
            apply_bo(pixels, input.data(), ctu_size, stride,
                params.band_position_luma, params.offsets_luma);
        }
    }

    void set_enabled(bool enabled) { sao_enabled_ = enabled; }
    bool is_enabled() const { return sao_enabled_; }

private:
    void apply_eo(uint8_t* dst, const uint8_t* src,
                  uint32_t size, uint32_t stride,
                  SAOEOClass eo_class, const int8_t* offsets) {
        // Get neighbor offsets based on EO class
        int dx1 = 0, dy1 = 0, dx2 = 0, dy2 = 0;
        switch (eo_class) {
            case SAOEOClass::EO_0: dx1 = -1; dx2 = 1; break;   // horizontal
            case SAOEOClass::EO_1: dy1 = -1; dy2 = 1; break;   // vertical
            case SAOEOClass::EO_2: dx1 = -1; dy1 = -1; dx2 = 1; dy2 = 1; break; // 135 deg
            case SAOEOClass::EO_3: dx1 = 1; dy1 = -1; dx2 = -1; dy2 = 1; break;  // 45 deg
        }

        for (uint32_t y = 0; y < size; y++) {
            for (uint32_t x = 0; x < size; x++) {
                int n1_x = static_cast<int>(x) + dx1;
                int n1_y = static_cast<int>(y) + dy1;
                int n2_x = static_cast<int>(x) + dx2;
                int n2_y = static_cast<int>(y) + dy2;

                // Boundary check
                if (n1_x < 0 || n1_x >= static_cast<int>(size) ||
                    n1_y < 0 || n1_y >= static_cast<int>(size) ||
                    n2_x < 0 || n2_x >= static_cast<int>(size) ||
                    n2_y < 0 || n2_y >= static_cast<int>(size)) {
                    continue;
                }

                uint8_t c = src[y * stride + x];
                uint8_t n1 = src[n1_y * stride + n1_x];
                uint8_t n2 = src[n2_y * stride + n2_x];

                // Edge classification (Table 9-4 in HEVC spec)
                int edge_idx = -1;
                if (c < n1 && c < n2) edge_idx = 0;        // local min
                else if (c < n1 && c == n2) edge_idx = 1;   // one-sided min
                else if (c < n2 && c == n1) edge_idx = 1;
                else if (c > n1 && c == n2) edge_idx = 2;   // one-sided max
                else if (c > n2 && c == n1) edge_idx = 2;
                else if (c > n1 && c > n2) edge_idx = 3;    // local max

                if (edge_idx >= 0) {
                    int val = static_cast<int>(c) + offsets[edge_idx];
                    dst[y * stride + x] = static_cast<uint8_t>(std::clamp(val, 0, 255));
                }
            }
        }
    }

    void apply_bo(uint8_t* dst, const uint8_t* src,
                  uint32_t size, uint32_t stride,
                  int band_start, const int8_t* offsets) {
        for (uint32_t y = 0; y < size; y++) {
            for (uint32_t x = 0; x < size; x++) {
                uint8_t pixel = src[y * stride + x];
                // Map pixel value to band index (0-31)
                int band = pixel >> 3; // 256/32 = 8 levels per band
                int offset_idx = band - band_start;
                if (offset_idx >= 0 && offset_idx < 4) {
                    int val = static_cast<int>(pixel) + offsets[offset_idx];
                    dst[y * stride + x] = static_cast<uint8_t>(std::clamp(val, 0, 255));
                }
            }
        }
    }

    void estimate_eo_offsets(const uint8_t* orig, const uint8_t* recon,
                              uint32_t size, uint32_t stride,
                              SAOEOClass eo_class, int32_t qp,
                              int8_t* out_offsets) {
        // Accumulate errors per edge category
        int64_t sum_diff[4] = {0, 0, 0, 0};
        int64_t count[4] = {0, 0, 0, 0};

        int dx1, dy1, dx2, dy2;
        switch (eo_class) {
            case SAOEOClass::EO_0: dx1 = -1; dy1 = 0; dx2 = 1; dy2 = 0; break;
            case SAOEOClass::EO_1: dx1 = 0; dy1 = -1; dx2 = 0; dy2 = 1; break;
            case SAOEOClass::EO_2: dx1 = -1; dy1 = -1; dx2 = 1; dy2 = 1; break;
            case SAOEOClass::EO_3: dx1 = 1; dy1 = -1; dx2 = -1; dy2 = 1; break;
        }

        for (uint32_t y = 0; y < size; y++) {
            for (uint32_t x = 0; x < size; x++) {
                int n1_x = static_cast<int>(x) + dx1;
                int n1_y = static_cast<int>(y) + dy1;
                int n2_x = static_cast<int>(x) + dx2;
                int n2_y = static_cast<int>(y) + dy2;

                if (n1_x < 0 || n1_x >= static_cast<int>(size) ||
                    n1_y < 0 || n1_y >= static_cast<int>(size) ||
                    n2_x < 0 || n2_x >= static_cast<int>(size) ||
                    n2_y < 0 || n2_y >= static_cast<int>(size)) continue;

                uint8_t c = recon[y * stride + x];
                uint8_t n1 = recon[n1_y * stride + n1_x];
                uint8_t n2 = recon[n2_y * stride + n2_x];

                int edge_idx = -1;
                if (c < n1 && c < n2) edge_idx = 0;
                else if (c < n1 && c == n2) edge_idx = 1;
                else if (c < n2 && c == n1) edge_idx = 1;
                else if (c > n1 && c == n2) edge_idx = 2;
                else if (c > n2 && c == n1) edge_idx = 2;
                else if (c > n1 && c > n2) edge_idx = 3;

                if (edge_idx >= 0) {
                    int diff = static_cast<int>(orig[y * stride + x]) -
                        static_cast<int>(c);
                    sum_diff[edge_idx] += diff;
                    count[edge_idx]++;
                }
            }
        }

        // Compute optimal offsets (clipped to [-7, 7] for HEVC)
        for (int i = 0; i < 4; i++) {
            if (count[i] > 0) {
                int32_t avg = static_cast<int32_t>(sum_diff[i] / count[i]);
                out_offsets[i] = static_cast<int8_t>(std::clamp(avg, -7, 7));
            } else {
                out_offsets[i] = 0;
            }
        }
    }

    void estimate_bo_offsets(const uint8_t* orig, const uint8_t* recon,
                              uint32_t size, uint32_t stride,
                              int band_start, int32_t qp,
                              int8_t* out_offsets) {
        int64_t sum_diff[4] = {0, 0, 0, 0};
        int64_t count[4] = {0, 0, 0, 0};

        for (uint32_t y = 0; y < size; y++) {
            for (uint32_t x = 0; x < size; x++) {
                uint8_t pixel = recon[y * stride + x];
                int band = pixel >> 3;
                int offset_idx = band - band_start;
                if (offset_idx >= 0 && offset_idx < 4) {
                    int diff = static_cast<int>(orig[y * stride + x]) -
                        static_cast<int>(pixel);
                    sum_diff[offset_idx] += diff;
                    count[offset_idx]++;
                }
            }
        }

        for (int i = 0; i < 4; i++) {
            if (count[i] > 0) {
                int32_t avg = static_cast<int32_t>(sum_diff[i] / count[i]);
                out_offsets[i] = static_cast<int8_t>(std::clamp(avg, -7, 7));
            } else {
                out_offsets[i] = 0;
            }
        }
    }

    double compute_distortion(const uint8_t* orig, const uint8_t* recon,
                               uint32_t size, uint32_t stride,
                               const SAOCTUParams* params) const {
        double total_dist = 0.0;
        for (uint32_t y = 0; y < size; y++) {
            for (uint32_t x = 0; x < size; x++) {
                int diff = static_cast<int>(orig[y * stride + x]) -
                    static_cast<int>(recon[y * stride + x]);
                total_dist += std::abs(diff);
            }
        }
        return total_dist;
    }

    double compute_sao_rate(const SAOCTUParams& params) const {
        // Rough rate estimate for signalling SAO params
        // Type: 2 bits, EO class: 2 bits, band position: 5 bits,
        // 4 offsets * 3 bits = 12 bits, merge flags: 2 bits
        double bits = 2.0 + 2.0 + 5.0 + (4.0 * 3.0) + 2.0;
        return bits * 8.0; // scaled cost
    }

    bool sao_enabled_ = true;
};

// ============================================================
// SECTION 10: ALF (Adaptive Loop Filter) Stubs
// ============================================================

// ALF filter shape types (HEVC/VVC)
enum class ALFFilterShape : uint8_t {
    SHAPE_5x5 = 0,
    SHAPE_7x7_DIAMOND = 1,
    SHAPE_9x9_DIAMOND = 2,
};

// ALF coefficient set
struct ALFCoefficients {
    static constexpr size_t MAX_COEFFS_5x5 = 7;   // 5x5 diamond
    static constexpr size_t MAX_COEFFS_7x7 = 13;   // 7x7 diamond
    static constexpr size_t MAX_COEFFS_9x9 = 21;   // 9x9 diamond

    int16_t coeffs[21] = {};       // filter coefficients (Q6 fixed-point)
    size_t num_coeffs = 0;
    bool is_chroma = false;
};

// ALF classification parameters
struct ALFClassificationParams {
    uint32_t num_classes = 25;       // number of classification bins
    uint32_t activity_factor = 2;    // Laplacian activity scaling
    uint32_t direction_factor = 2;   // directionality weight
    bool class_merge_enabled = false;
    uint8_t merge_table[25] = {};    // class merge indices
};

// ALF slice-level configuration
struct ALFSliceConfig {
    bool alf_enabled = false;
    bool chroma_alf_enabled = false;
    bool cc_alf_enabled = false;          // cross-component ALF (VVC)
    uint32_t num_alf_aps = 0;
    bool temporal_filter_enabled = false;
    uint32_t luma_filter_set_index = 0;
    uint32_t chroma_filter_set_index = 0;
    ALFClassificationParams classification;
};

// ALF (Adaptive Loop Filter) stub — base interface
class AdaptiveLoopFilter {
public:
    explicit AdaptiveLoopFilter(const ALFSliceConfig& config)
        : config_(config) {
        spdlog::info("ALF: enabled={} chroma={} cc_alf={} shape={} classes={}",
            config_.alf_enabled, config_.chroma_alf_enabled,
            config_.cc_alf_enabled, "7x7_diamond",
            config_.classification.num_classes);
    }

    virtual ~AdaptiveLoopFilter() = default;

    // Configure filter from slice-level parameters
    virtual bool configure(const ALFSliceConfig& cfg) {
        config_ = cfg;
        if (config_.alf_enabled) {
            spdlog::info("ALF configured: luma_filter_set={} chroma_filter_set={}",
                config_.luma_filter_set_index,
                config_.chroma_filter_set_index);
        }
        return true;
    }

    // Apply ALF to a reconstructed frame
    // This is a stub — full implementation requires filter coefficient
    // derivation, classification, and per-pixel filtering
    virtual bool apply_filter(
        uint8_t* recon_plane[3],     // Y, Cb, Cr planes (in-place)
        const uint32_t stride[3],
        uint32_t width, uint32_t height,
        const std::vector<ALFCoefficients>& filter_sets
    ) {
        if (!config_.alf_enabled) {
            spdlog::debug("ALF: skipped (disabled)");
            return false;
        }

        spdlog::debug("ALF: applying to {}x{} frame with {} filter sets",
            width, height, filter_sets.size());

        // Apply luma ALF
        if (!filter_sets.empty()) {
            apply_alf_plane(recon_plane[0], stride[0],
                width, height, filter_sets[0]);
        }

        // Apply chroma ALF if enabled
        if (config_.chroma_alf_enabled && filter_sets.size() > 1) {
            uint32_t cw = width / 2;
            uint32_t ch = height / 2;
            apply_alf_plane(recon_plane[1], stride[1], cw, ch, filter_sets[1]);
            apply_alf_plane(recon_plane[2], stride[2], cw, ch, filter_sets[1]);
        }

        return true;
    }

    // Compute classification map for a frame (stub)
    virtual std::vector<uint8_t> compute_classification_map(
        const uint8_t* luma_plane,
        uint32_t stride, uint32_t width, uint32_t height
    ) const {
        std::vector<uint8_t> class_map(width * height, 0);
        // Stub: assign all blocks to class 0
        spdlog::debug("ALF classification: {}x{} -> {} classes (all class 0 stub)",
            width, height, config_.classification.num_classes);
        return class_map;
    }

    // Derive optimal filter coefficients using Wiener filter (stub)
    virtual ALFCoefficients derive_filter_coefficients(
        const uint8_t* original,
        const uint8_t* reconstructed,
        uint32_t stride, uint32_t width, uint32_t height,
        bool is_chroma = false
    ) {
        ALFCoefficients coeffs;
        coeffs.is_chroma = is_chroma;

        // Stub: identity filter (center tap = 64 in Q6, all others 0)
        coeffs.num_coeffs = ALFCoefficients::MAX_COEFFS_7x7;
        std::fill(std::begin(coeffs.coeffs), std::end(coeffs.coeffs), 0);
        coeffs.coeffs[6] = 64; // center coefficient for 7x7 diamond

        spdlog::debug("ALF coeff derivation: {}x{} (stub — identity filter)",
            width, height);
        return coeffs;
    }

    // Reset filter state
    virtual void reset() {
        spdlog::debug("ALF reset");
    }

    // Get current configuration
    const ALFSliceConfig& config() const { return config_; }

    // Check if ALF is active
    bool is_active() const { return config_.alf_enabled; }

protected:
    // Apply 7x7 diamond ALF to one color plane
    virtual void apply_alf_plane(
        uint8_t* plane, uint32_t stride,
        uint32_t width, uint32_t height,
        const ALFCoefficients& coeffs
    ) {
        if (!coeffs.num_coeffs) return;

        int padding = 3; // for 7x7 diamond

        // Work on a copy
        std::vector<uint8_t> src(height * stride);
        std::memcpy(src.data(), plane, height * stride);

        // 7x7 diamond filter pattern offsets (row, col) with center at (3,3)
        static constexpr int offsets_7x7[13][2] = {
            {0, 0},                                    // row 0, center col
            {1, -1}, {1, 0}, {1, 1},                   // row 1
            {2, -2}, {2, -1}, {2, 0}, {2, 1}, {2, 2}, // row 2
            {3, -3}, {3, -2}, {3, -1}, {3, 0},         // row 3 (center row first 4)
            // mirrored for bottom half
        };

        // Apply filter (simplified: use symmetric 7x7 diamond)
        static constexpr int offsets_sym[13][2] = {
            {-3, 0},
            {-2, -1}, {-2, 0}, {-2, 1},
            {-1, -2}, {-1, -1}, {-1, 0}, {-1, 1}, {-1, 2},
            {0, -3}, {0, -2}, {0, -1}, {0, 0},
            // Positive half handled by symmetry
        };

        for (uint32_t y = padding; y < height - padding; y++) {
            for (uint32_t x = padding; x < width - padding; x++) {
                int32_t filtered = 0;
                int total_weight = 0;

                // Top half of diamond (13 taps including center)
                for (size_t i = 0; i < 13; i++) {
                    int ry = static_cast<int>(y) + offsets_sym[i][0];
                    int rx = static_cast<int>(x) + offsets_sym[i][1];

                    uint8_t pixel = src[ry * stride + rx];
                    int w = coeffs.coeffs[i];
                    filtered += pixel * w;
                    total_weight += std::abs(w);
                }

                // Bottom half of diamond (mirrored, same weights)
                for (size_t i = 0; i < 12; i++) {
                    int ry = static_cast<int>(y) - offsets_sym[i][0];
                    int rx = static_cast<int>(x) - offsets_sym[i][1];

                    if (ry != static_cast<int>(y) || rx != static_cast<int>(x)) {
                        uint8_t pixel = src[ry * stride + rx];
                        int w = coeffs.coeffs[i];
                        filtered += pixel * w;
                        total_weight += std::abs(w);
                    }
                }

                // Center coefficient already counted once in top half
                // Normalize (divide by 64 for Q6 fixed-point)
                int32_t out = (filtered + 32) / 64;
                plane[y * stride + x] = static_cast<uint8_t>(std::clamp(out, 0, 255));
            }
        }
    }

    ALFSliceConfig config_;
};

// Cross-Component ALF (CC-ALF) stub for VVC
class CrossComponentALF : public AdaptiveLoopFilter {
public:
    explicit CrossComponentALF(const ALFSliceConfig& config)
        : AdaptiveLoopFilter(config) {
        spdlog::info("CC-ALF: enabled={}", config.cc_alf_enabled);
    }

    // Apply cross-component filtering (luma -> chroma correction)
    bool apply_cc_alf(
        const uint8_t* luma_plane,
        uint8_t* cb_plane,
        uint8_t* cr_plane,
        uint32_t stride_luma,
        uint32_t stride_chroma,
        uint32_t width, uint32_t height,
        const ALFCoefficients& cc_coeffs_cb,
        const ALFCoefficients& cc_coeffs_cr
    ) {
        if (!config_.cc_alf_enabled) return false;

        uint32_t cw = width / 2;
        uint32_t ch = height / 2;

        // Stub implementation: identity
        spdlog::debug("CC-ALF: processing {}x{} chroma planes", cw, ch);

        for (uint32_t cy = 0; cy < ch; cy++) {
            for (uint32_t cx = 0; cx < cw; cx++) {
                // Map chroma position to luma (4:2:0)
                uint32_t ly = cy * 2;
                uint32_t lx = cx * 2;

                int32_t luma_val = luma_plane[ly * stride_luma + lx];

                // Apply CC filter coefficients (stub: no change)
                int cb_val = static_cast<int>(cb_plane[cy * stride_chroma + cx]);
                int cr_val = static_cast<int>(cr_plane[cy * stride_chroma + cx]);

                // Stub: pass-through
                cb_plane[cy * stride_chroma + cx] = static_cast<uint8_t>(
                    std::clamp(cb_val, 0, 255));
                cr_plane[cy * stride_chroma + cx] = static_cast<uint8_t>(
                    std::clamp(cr_val, 0, 255));
            }
        }

        return true;
    }
};

// ============================================================
// SECTION: BitstreamWriter utility (used by SPS/PPS/SEI builders)
// ============================================================

class BitstreamWriter {
public:
    BitstreamWriter() : bit_buf_(0), bits_in_buf_(0) {}

    void start_nal(const uint8_t* start_code, size_t len) {
        data_.insert(data_.end(), start_code, start_code + len);
        bit_buf_ = 0;
        bits_in_buf_ = 0;
    }

    void write_bits(uint64_t value, size_t num_bits) {
        while (num_bits > 0) {
            size_t room = 64 - bits_in_buf_;
            size_t to_write = std::min(num_bits, room);
            uint64_t mask = (to_write == 64) ? ~0ULL : ((1ULL << to_write) - 1);
            bit_buf_ |= ((value >> (num_bits - to_write)) & mask)
                << (room - to_write);
            bits_in_buf_ += to_write;
            num_bits -= to_write;
            if (bits_in_buf_ >= 8) {
                flush_bytes();
            }
        }
    }

    void write_ue(uint32_t value) {
        // Unsigned Exp-Golomb coding
        uint32_t bits = value + 1;
        size_t num_bits = 0;
        uint32_t temp = bits;
        while (temp > 1) { num_bits++; temp >>= 1; }
        // Leading zeros
        write_bits(0, num_bits);
        // Value + 1 in binary
        write_bits(bits, num_bits + 1);
    }

    void write_se(int32_t value) {
        // Signed Exp-Golomb: map signed to unsigned
        uint32_t uval = (value <= 0) ?
            static_cast<uint32_t>(-value * 2) :
            static_cast<uint32_t>(value * 2 - 1);
        write_ue(uval);
    }

    void finish_nal() {
        // RBSP trailing bits: 1 followed by zeros to byte-align
        write_bits(1, 1);
        while (bits_in_buf_ > 0) {
            write_bits(0, 1);
        }
        flush_bytes();
    }

    size_t bit_pos() const {
        return data_.size() * 8 + bits_in_buf_;
    }

    std::vector<uint8_t>& data() { return data_; }
    const std::vector<uint8_t>& data() const { return data_; }

    void clear() {
        data_.clear();
        bit_buf_ = 0;
        bits_in_buf_ = 0;
    }

private:
    void flush_bytes() {
        while (bits_in_buf_ >= 8) {
            bits_in_buf_ -= 8;
            uint8_t byte = static_cast<uint8_t>(bit_buf_ >> bits_in_buf_);
            data_.push_back(byte);
            // Emulation prevention: insert 0x03 after 0x00 0x00
            if (byte == 0x00 && data_.size() >= 2 &&
                data_[data_.size() - 2] == 0x00) {
                data_.push_back(0x03);
            }
        }
    }

    std::vector<uint8_t> data_;
    uint64_t bit_buf_;
    size_t bits_in_buf_;
};

} // namespace cppdesk::codec::video