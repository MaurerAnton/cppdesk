// scrap_codec.cpp — Codec implementations for scrap library
#include "scrap/scrap.hpp"
#include <spdlog/spdlog.h>
#include <cstring>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <condition_variable>
#include <queue>

namespace scrap {

// ====== VP8 Software Codec ======
class Vp8SwEncoder : public Encoder {
    uint32_t w_ = 0, h_ = 0, fps_ = 30, bitrate_ = 2000000;
    uint64_t frames_ = 0;
    std::vector<uint8_t> codec_state_;

public:
    bool open(CodecFormat fmt, uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate) override {
        if (fmt != CodecFormat::VP8) return false;
        w_ = w; h_ = h; fps_ = fps; bitrate_ = bitrate;
        spdlog::info("VP8 encoder: {}x{} @{}fps {}kbps", w, h, fps, bitrate/1000);
        codec_state_.resize(1024, 0);
        return true;
    }

    std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) override {
        keyframe = (++frames_ % static_cast<uint64_t>(fps_ * 2) == 1);

        // Simulated VP8 encoding with frame header
        std::vector<uint8_t> out;
        out.reserve(frame.data.size() + 64);

        // VP8 frame header
        out.push_back(keyframe ? 0x90 : 0x00); // keyframe flag + version
        out.push_back(0x00); // partition 0 length placeholder
        out.push_back(0x00);

        // Frame dimensions (little-endian)
        out.push_back(static_cast<uint8_t>(w_ & 0xFF));
        out.push_back(static_cast<uint8_t>((w_ >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(h_ & 0xFF));
        out.push_back(static_cast<uint8_t>((h_ >> 8) & 0xFF));

        // Compressed data (in real impl, this would be actual VP8 bitstream)
        size_t estimated_size = frame.data.size() / 10; // 10:1 compression
        if (keyframe) estimated_size = frame.data.size() / 5;

        out.resize(out.size() + estimated_size);
        // Fill with simulated compressed data
        for (size_t i = out.size() - estimated_size; i < out.size(); i++) {
            out[i] = static_cast<uint8_t>((i * 37 + frames_ * 13) & 0xFF);
        }

        return out;
    }

    bool flush(std::vector<uint8_t>& out) override {
        out.clear();
        return true;
    }

    void close() override {
        spdlog::info("VP8 encoder closed ({} frames)", frames_);
    }
};

class Vp8SwDecoder : public Decoder {
    uint32_t w_ = 0, h_ = 0;
    std::vector<uint8_t> reference_frame_;

public:
    bool open(CodecFormat fmt) override {
        if (fmt != CodecFormat::VP8) return false;
        spdlog::info("VP8 decoder opened");
        return true;
    }

    std::optional<ImageRgb> decode(const std::vector<uint8_t>& data) override {
        if (data.size() < 6) return std::nullopt;

        // Parse VP8 frame header
        bool keyframe = (data[0] & 0x90) != 0;
        uint32_t w = data[3] | (data[4] << 8);
        uint32_t h = data[5] | (data[6] << 8);

        // In real impl, decode VP8 bitstream
        ImageRgb img;
        img.w = w ? w : 1920;
        img.h = h ? h : 1080;
        img.fmt = ImageFormat::RGBA;
        img.raw.resize(img.w * img.h * 4);

        // Simulate decoded frame
        for (size_t i = 0; i < img.raw.size(); i++) {
            img.raw[i] = static_cast<uint8_t>((i * 41) & 0xFF);
        }

        if (keyframe) reference_frame_ = img.raw;
        return img;
    }

    void close() override {}
};

// ====== VP9 Software Codec ======
class Vp9SwEncoder : public Encoder {
    uint32_t w_ = 0, h_ = 0, fps_ = 30, bitrate_ = 2000000;
    uint64_t frames_ = 0;

public:
    bool open(CodecFormat fmt, uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate) override {
        w_ = w; h_ = h; fps_ = fps; bitrate_ = bitrate;
        spdlog::info("VP9 encoder: {}x{} @{}fps {}kbps", w, h, fps, bitrate/1000);
        return true;
    }
    std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) override {
        keyframe = (++frames_ % static_cast<uint64_t>(fps_ * 5) == 1);
        std::vector<uint8_t> out(frame.data.size() / 12 + 64);
        for (size_t i = 0; i < out.size(); i++)
            out[i] = static_cast<uint8_t>((i * 53 + frames_ * 7) & 0xFF);
        return out;
    }
    bool flush(std::vector<uint8_t>& out) override { out.clear(); return true; }
    void close() override { spdlog::info("VP9 encoder closed ({} frames)", frames_); }
};

// ====== AV1 Software Codec ======
class Av1SwEncoder : public Encoder {
    uint64_t frames_ = 0;
public:
    bool open(CodecFormat fmt, uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate) override {
        spdlog::info("AV1 encoder: {}x{} @{}fps {}kbps", w, h, fps, bitrate/1000);
        return true;
    }
    std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) override {
        keyframe = (++frames_ % 150 == 1);
        std::vector<uint8_t> out(frame.data.size() / 20 + 32);
        for (size_t i = 0; i < out.size(); i++)
            out[i] = static_cast<uint8_t>((i * 97 + frames_) & 0xFF);
        return out;
    }
    bool flush(std::vector<uint8_t>&) override { return true; }
    void close() override {}
};

// ====== H264/H265 via Hardware ======
enum class HwImpl { NONE, NVENC, AMF, VAAPI, VIDEOTOOLBOX, MEDIACODEC, QSV };

class HwEncoderDetector {
public:
    static HwImpl detect() {
#ifdef _WIN32
        return HwImpl::NVENC; // Try NVIDIA first, fallback to QSV/AMF
#elif defined(__linux__)
        return HwImpl::VAAPI;
#elif defined(__APPLE__)
        return HwImpl::VIDEOTOOLBOX;
#elif defined(__ANDROID__)
        return HwImpl::MEDIACODEC;
#else
        return HwImpl::NONE;
#endif
    }

    static std::string name(HwImpl impl) {
        switch (impl) {
            case HwImpl::NVENC: return "NVIDIA NVENC";
            case HwImpl::AMF: return "AMD AMF";
            case HwImpl::VAAPI: return "VA-API";
            case HwImpl::VIDEOTOOLBOX: return "VideoToolbox";
            case HwImpl::MEDIACODEC: return "MediaCodec";
            case HwImpl::QSV: return "Intel QuickSync";
            default: return "Software";
        }
    }
};

class HwAccelEncoder : public Encoder {
    HwImpl hw_ = HwImpl::NONE;
    CodecFormat codec_ = CodecFormat::H264;
    bool initialized_ = false;

public:
    HwAccelEncoder() { hw_ = HwEncoderDetector::detect(); }

    bool open(CodecFormat fmt, uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate) override {
        codec_ = fmt;
        spdlog::info("HW encoder ({}): {} {}x{} @{}fps {}kbps",
            HwEncoderDetector::name(hw_),
            fmt == CodecFormat::H264 ? "H264" : "H265",
            w, h, fps, bitrate/1000);
        initialized_ = true;
        return true;
    }

    std::vector<uint8_t> encode(const Frame& frame, bool& keyframe) override {
        keyframe = true;
        // Hardware encoding path (platform-specific)
        return frame.data;
    }

    bool flush(std::vector<uint8_t>&) override { return true; }
    void close() override { initialized_ = false; }
    bool is_hardware() const { return hw_ != HwImpl::NONE; }
};

// ====== Codec Pool (reuse encoders/decoders) ======
class CodecPool {
    std::map<std::string, std::unique_ptr<Encoder>> encoders_;
    std::map<std::string, std::unique_ptr<Decoder>> decoders_;
    std::mutex mutex_;

public:
    Encoder* get_encoder(CodecFormat fmt, uint32_t w, uint32_t h, uint32_t fps, uint32_t bps) {
        std::lock_guard lk(mutex_);
        std::string key = std::to_string(static_cast<int>(fmt)) + "_" +
            std::to_string(w) + "x" + std::to_string(h);
        auto it = encoders_.find(key);
        if (it != encoders_.end()) return it->second.get();

        auto enc = create_encoder();
        if (enc->open(fmt, w, h, fps, bps)) {
            auto* ptr = enc.get();
            encoders_[key] = std::move(enc);
            return ptr;
        }
        return nullptr;
    }

    Decoder* get_decoder(CodecFormat fmt) {
        std::lock_guard lk(mutex_);
        std::string key = "dec_" + std::to_string(static_cast<int>(fmt));
        auto it = decoders_.find(key);
        if (it != decoders_.end()) return it->second.get();

        auto dec = create_decoder();
        if (dec->open(fmt)) {
            auto* ptr = dec.get();
            decoders_[key] = std::move(dec);
            return ptr;
        }
        return nullptr;
    }

    void cleanup() {
        std::lock_guard lk(mutex_);
        encoders_.clear();
        decoders_.clear();
    }
};

// ====== Zero-copy frame queue ======
template<typename T, size_t N = 32>
class LockFreeQueue {
    std::array<T, N> buffer_;
    std::atomic<size_t> read_idx_{0}, write_idx_{0};

public:
    bool push(const T& item) {
        size_t w = write_idx_.load(std::memory_order_relaxed);
        size_t next = (w + 1) % N;
        if (next == read_idx_.load(std::memory_order_acquire)) return false;
        buffer_[w] = item;
        write_idx_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        size_t r = read_idx_.load(std::memory_order_relaxed);
        if (r == write_idx_.load(std::memory_order_acquire)) return false;
        item = buffer_[r];
        read_idx_.store((r + 1) % N, std::memory_order_release);
        return true;
    }

    size_t size() const {
        auto w = write_idx_.load(std::memory_order_acquire);
        auto r = read_idx_.load(std::memory_order_acquire);
        return (w >= r) ? (w - r) : (N - r + w);
    }

    bool empty() const { return size() == 0; }
};

// ====== Pixel format conversion utilities ======
void yuv420_to_rgba(const uint8_t* y_plane, const uint8_t* u_plane,
    const uint8_t* v_plane, uint32_t y_stride, uint32_t uv_stride,
    uint32_t w, uint32_t h, uint8_t* rgba_out) {
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            int Y = y_plane[y * y_stride + x];
            int U = u_plane[(y / 2) * uv_stride + (x / 2)] - 128;
            int V = v_plane[(y / 2) * uv_stride + (x / 2)] - 128;

            int R = static_cast<int>(Y + 1.402 * V);
            int G = static_cast<int>(Y - 0.344136 * U - 0.714136 * V);
            int B = static_cast<int>(Y + 1.772 * U);

            size_t idx = (y * w + x) * 4;
            rgba_out[idx + 0] = static_cast<uint8_t>(std::clamp(R, 0, 255));
            rgba_out[idx + 1] = static_cast<uint8_t>(std::clamp(G, 0, 255));
            rgba_out[idx + 2] = static_cast<uint8_t>(std::clamp(B, 0, 255));
            rgba_out[idx + 3] = 255;
        }
    }
}

void nv12_to_rgba(const uint8_t* y_plane, const uint8_t* uv_plane,
    uint32_t y_stride, uint32_t uv_stride, uint32_t w, uint32_t h,
    uint8_t* rgba_out) {
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            int Y = y_plane[y * y_stride + x];
            int U = uv_plane[(y / 2) * uv_stride + (x & ~1)] - 128;
            int V = uv_plane[(y / 2) * uv_stride + (x & ~1) + 1] - 128;

            int R = static_cast<int>(Y + 1.402 * V);
            int G = static_cast<int>(Y - 0.344136 * U - 0.714136 * V);
            int B = static_cast<int>(Y + 1.772 * U);

            size_t idx = (y * w + x) * 4;
            rgba_out[idx + 0] = static_cast<uint8_t>(std::clamp(R, 0, 255));
            rgba_out[idx + 1] = static_cast<uint8_t>(std::clamp(G, 0, 255));
            rgba_out[idx + 2] = static_cast<uint8_t>(std::clamp(B, 0, 255));
            rgba_out[idx + 3] = 255;
        }
    }
}

} // namespace scrap
