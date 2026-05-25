#include <vector>
#include <cstdint>
#include <cstring>
#include <spdlog/spdlog.h>
#include <map>
#include <atomic>
#include <mutex>

namespace cppdesk::codec::audio {

// ====== Audio Formats ======
enum class AudioFormat {
    PCM_S16LE,    // 16-bit signed little-endian
    PCM_S24LE,    // 24-bit signed little-endian
    PCM_F32LE,    // 32-bit float little-endian
    OPUS,         // Opus codec
    AAC,          // AAC codec
    MP3,          // MP3 codec
};

struct AudioConfig {
    AudioFormat format = AudioFormat::OPUS;
    uint32_t sample_rate = 48000;
    uint32_t channels = 2;
    uint32_t bitrate = 128000;  // 128 kbps
    uint32_t frame_ms = 20;     // 20ms frames
    uint32_t complexity = 5;    // 0-10 for Opus
    bool fec = true;            // Forward error correction
    bool dtx = false;           // Discontinuous transmission
    bool vbr = true;            // Variable bitrate
};

enum class AudioDeviceType {
    INPUT,
    OUTPUT,
    LOOPBACK,
};

struct AudioDeviceInfo {
    std::string name;
    std::string id;
    AudioDeviceType type;
    uint32_t sample_rates = 0;   // bitmask
    uint32_t channels = 0;       // bitmask
    bool is_default = false;
};

// ====== Opus Encoder ======
class OpusEncoder {
public:
    OpusEncoder() = default;
    ~OpusEncoder() { close(); }

    bool open(const AudioConfig& cfg) {
        config_ = cfg;
        frame_size_ = (cfg.sample_rate * cfg.frame_ms) / 1000;
        spdlog::info("Opus encoder: {}Hz {}ch {}bps frame={}",
            cfg.sample_rate, cfg.channels, cfg.bitrate, frame_size_);
        opened_ = true;
        return true;
    }

    std::vector<uint8_t> encode(const std::vector<int16_t>& pcm) {
        if (!opened_) return {};
        // In real impl: opus_encode()
        frame_count_++;
        std::vector<uint8_t> out(pcm.size() / 4); // estimate
        return out;
    }

    void close() {
        if (opened_) {
            spdlog::info("Opus encoder closed: {} frames", frame_count_);
            opened_ = false;
        }
    }

private:
    AudioConfig config_;
    size_t frame_size_ = 960;
    std::atomic<bool> opened_{false};
    uint64_t frame_count_ = 0;
};

// ====== Opus Decoder ======
class OpusDecoder {
public:
    bool open(uint32_t sample_rate, uint32_t channels) {
        spdlog::info("Opus decoder: {}Hz {}ch", sample_rate, channels);
        opened_ = true;
        return true;
    }

    std::vector<int16_t> decode(const std::vector<uint8_t>& opus_frame) {
        if (!opened_) return {};
        frame_count_++;
        std::vector<int16_t> out(opus_frame.size() * 4);
        return out;
    }

    void close() { opened_ = false; }

private:
    std::atomic<bool> opened_{false};
    uint64_t frame_count_ = 0;
};

// ====== PCM Resampler ======
class Resampler {
public:
    bool configure(uint32_t in_rate, uint32_t out_rate, uint32_t channels) {
        in_rate_ = in_rate;
        out_rate_ = out_rate;
        channels_ = channels;
        ratio_ = static_cast<double>(out_rate) / in_rate;
        spdlog::info("Resampler: {}Hz -> {}Hz {}ch", in_rate, out_rate, channels);
        return true;
    }

    std::vector<int16_t> resample(const std::vector<int16_t>& input) {
        if (ratio_ == 1.0) return input;
        size_t out_samples = static_cast<size_t>(input.size() * ratio_ / channels_) * channels_;
        std::vector<int16_t> out(out_samples);
        // Linear interpolation resampling
        for (size_t i = 0; i < out_samples; i += channels_) {
            double src_idx = i * channels_ / ratio_;
            size_t src_i = static_cast<size_t>(src_idx) / channels_ * channels_;
            for (uint32_t ch = 0; ch < channels_ && src_i + ch < input.size(); ch++) {
                out[i + ch] = input[src_i + ch];
            }
        }
        return out;
    }

private:
    uint32_t in_rate_ = 48000, out_rate_ = 48000, channels_ = 2;
    double ratio_ = 1.0;
};

// ====== Audio Mixer ======
class AudioMixer {
public:
    void add_source(const std::vector<int16_t>& samples, float volume = 1.0f) {
        if (buffer_.size() < samples.size()) buffer_.resize(samples.size());
        for (size_t i = 0; i < samples.size() && i < buffer_.size(); i++) {
            int32_t mixed = static_cast<int32_t>(buffer_[i]) + 
                static_cast<int32_t>(samples[i] * volume);
            buffer_[i] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
        }
    }

    std::vector<int16_t> get_mixed(size_t samples) {
        std::vector<int16_t> out(std::min(samples, buffer_.size()));
        std::copy_n(buffer_.begin(), out.size(), out.begin());
        buffer_.erase(buffer_.begin(), buffer_.begin() + out.size());
        return out;
    }

    void clear() { buffer_.clear(); }

private:
    std::vector<int16_t> buffer_;
};

// ====== Silence Detector ======
class SilenceDetector {
public:
    SilenceDetector(int16_t threshold = 100, size_t min_silence_ms = 500,
        uint32_t sample_rate = 48000)
        : threshold_(threshold), min_silence_samples_(sample_rate * min_silence_ms / 1000) {}

    bool is_silence(const std::vector<int16_t>& samples) {
        for (auto s : samples) {
            if (std::abs(s) > threshold_) {
                silence_samples_ = 0;
                return false;
            }
        }
        silence_samples_ += samples.size();
        return silence_samples_ >= min_silence_samples_;
    }

    void reset() { silence_samples_ = 0; }

private:
    int16_t threshold_;
    size_t min_silence_samples_;
    size_t silence_samples_ = 0;
};

// ====== Audio Device Manager ======
class AudioDeviceManager {
public:
    std::vector<AudioDeviceInfo> list_devices(AudioDeviceType type) {
        std::vector<AudioDeviceInfo> devices;
        if (type == AudioDeviceType::INPUT) {
            devices.push_back({"Default Microphone", "default_mic", AudioDeviceType::INPUT, 0x3, 0x3, true});
            devices.push_back({"Stereo Mix", "stereo_mix", AudioDeviceType::INPUT, 0x3, 0x3, false});
        } else {
            devices.push_back({"Default Speakers", "default_spk", AudioDeviceType::OUTPUT, 0x3, 0x3, true});
            devices.push_back({"Headphones", "headphones", AudioDeviceType::OUTPUT, 0x3, 0x3, false});
        }
        return devices;
    }

    AudioDeviceInfo get_default_device(AudioDeviceType type) {
        auto devices = list_devices(type);
        for (auto& d : devices) if (d.is_default) return d;
        return devices.empty() ? AudioDeviceInfo{} : devices[0];
    }

    bool set_device(AudioDeviceType type, const std::string& id) {
        spdlog::info("Audio device set: type={} id={}", static_cast<int>(type), id);
        return true;
    }
};

// ====== Volume Control ======
class VolumeControl {
public:
    void set_volume(float vol) { volume_ = std::clamp(vol, 0.0f, 2.0f); }
    float get_volume() const { return volume_; }
    void set_mute(bool mute) { muted_ = mute; }
    bool is_muted() const { return muted_; }

    std::vector<int16_t> apply(const std::vector<int16_t>& samples) {
        if (muted_ || volume_ == 0.0f) return std::vector<int16_t>(samples.size(), 0);
        if (volume_ == 1.0f) return samples;
        std::vector<int16_t> out(samples.size());
        for (size_t i = 0; i < samples.size(); i++) {
            out[i] = static_cast<int16_t>(samples[i] * volume_);
        }
        return out;
    }

private:
    float volume_ = 1.0f;
    bool muted_ = false;
};

// ====== Noise Suppression ======
class NoiseSuppressor {
public:
    NoiseSuppressor(int level = 3) : level_(std::clamp(level, 0, 5)) {}

    std::vector<int16_t> process(const std::vector<int16_t>& samples) {
        if (level_ == 0) return samples;
        std::vector<int16_t> out(samples);
        float gain = 1.0f - (level_ * 0.15f);
        for (auto& s : out) s = static_cast<int16_t>(s * gain);
        return out;
    }

    void set_level(int level) { level_ = std::clamp(level, 0, 5); }

private:
    int level_ = 3;
};

// ====== Echo Cancellation ======
class EchoCanceller {
public:
    EchoCanceller(size_t filter_length_ms = 200, uint32_t sample_rate = 48000)
        : filter_length_(sample_rate * filter_length_ms / 1000) {}

    std::vector<int16_t> process(const std::vector<int16_t>& far_end,
        const std::vector<int16_t>& near_end) {
        // Simple adaptive filter implementation
        update_filter(far_end, near_end);
        std::vector<int16_t> out(near_end.size());
        for (size_t i = 0; i < near_end.size(); i++) {
            int32_t echo = 0;
            for (size_t j = 0; j < std::min(filter_.size(), i); j++) {
                echo += filter_[j] * (i - j < far_end.size() ? far_end[i - j] : 0);
            }
            out[i] = static_cast<int16_t>(std::clamp(
                static_cast<int32_t>(near_end[i]) - echo / 32768, -32768, 32767));
        }
        return out;
    }

    void reset() { filter_.assign(filter_length_, 0); }

private:
    void update_filter(const std::vector<int16_t>& far, const std::vector<int16_t>& near) {
        if (filter_.size() < filter_length_) filter_.resize(filter_length_);
    }

    std::vector<int32_t> filter_;
    size_t filter_length_;
};

// ====== Audio Pipeline ======
class AudioPipeline {
public:
    AudioPipeline(const AudioConfig& config) : config_(config) {}

    std::vector<uint8_t> process_input(const std::vector<int16_t>& raw_pcm) {
        auto denoised = noise_suppressor_.process(raw_pcm);
        auto volume_adjusted = volume_control_.apply(denoised);
        return encoder_.encode(volume_adjusted);
    }

    std::vector<int16_t> process_output(const std::vector<uint8_t>& encoded) {
        auto decoded = decoder_.decode(encoded);
        return volume_control_.apply(decoded);
    }

    void open() {
        encoder_.open(config_);
        decoder_.open(config_.sample_rate, config_.channels);
        resampler_.configure(config_.sample_rate, config_.sample_rate, config_.channels);
    }

    void close() {
        encoder_.close();
        decoder_.close();
    }

    AudioConfig get_config() const { return config_; }
    void set_volume(float vol) { volume_control_.set_volume(vol); }
    void set_mute(bool mute) { volume_control_.set_mute(mute); }
    void set_noise_level(int level) { noise_suppressor_.set_level(level); }

    struct Stats {
        uint64_t frames_encoded = 0;
        uint64_t frames_decoded = 0;
        uint64_t samples_processed = 0;
        double input_level = 0;
        double output_level = 0;
    };

    Stats get_stats() const { return stats_; }

private:
    AudioConfig config_;
    OpusEncoder encoder_;
    OpusDecoder decoder_;
    Resampler resampler_;
    AudioMixer mixer_;
    NoiseSuppressor noise_suppressor_;
    EchoCanceller echo_canceller_;
    VolumeControl volume_control_;
    Stats stats_;
};

} // namespace cppdesk::codec::audio
