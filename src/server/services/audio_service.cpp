// =============================================================================
// audio_service.cpp — Real comprehensive audio service implementation
//
// Features:
//   - Audio capture: PulseAudio (Linux) / WASAPI (Windows) / CoreAudio (macOS)
//   - Audio playback: Platform-native system speaker output
//   - Opus encoding/decoding integration
//   - Audio format negotiation (sample rate, channels, bit depth)
//   - Silence detection / VAD to avoid sending empty audio
//   - Audio device enumeration and selection
//   - Volume control (system and per-stream)
//   - Mute/unmute
//   - Echo cancellation (LMS adaptive filter)
//   - Noise suppression (spectral subtraction)
//   - Audio buffering and jitter management
//   - Statistics: frames sent/received, buffer levels
//   - AudioService class extending GenericService
// =============================================================================

#include "server/server.hpp"
#include "common/protocol.hpp"
#include "common/config.hpp"
#include <spdlog/spdlog.h>

#include <vector>
#include <array>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <map>
#include <set>
#include <functional>
#include <memory>
#include <queue>
#include <numeric>

// Platform-specific audio headers
#ifdef __linux__
    #include <pulse/pulseaudio.h>
    #include <pulse/simple.h>
    #include <pulse/error.h>
    #include <pulse/volume.h>
#elif defined(_WIN32)
    #include <windows.h>
    #include <mmdeviceapi.h>
    #include <audioclient.h>
    #include <audiopolicy.h>
    #include <endpointvolume.h>
    #include <functiondiscoverykeys_devpkey.h>
    #include <comdef.h>
    #pragma comment(lib, "ole32.lib")
    #pragma comment(lib, "avrt.lib")
#elif defined(__APPLE__)
    #include <AudioToolbox/AudioToolbox.h>
    #include <AudioUnit/AudioUnit.h>
    #include <CoreAudio/CoreAudio.h>
#endif

// Opus library (expects system-installed libopus)
// If not available, we define stubs; real deployment links -lopus
#if __has_include(<opus/opus.h>)
    #include <opus/opus.h>
    #define HAS_OPUS 1
#else
    #define HAS_OPUS 0
#endif


namespace cppdesk::server {

// =============================================================================
// Section 1: Audio types and constants
// =============================================================================

// Supported audio format for negotiation
struct AudioFormat {
    uint32_t sample_rate = 48000;
    uint32_t channels = 2;
    uint32_t bits_per_sample = 16;
    uint32_t frame_duration_ms = 20;  // typical for Opus

    constexpr uint32_t bytes_per_sample() const { return bits_per_sample / 8; }
    constexpr uint32_t frame_size_bytes() const {
        return sample_rate * frame_duration_ms / 1000 * channels * bytes_per_sample();
    }
    constexpr uint32_t samples_per_frame() const {
        return sample_rate * frame_duration_ms / 1000;
    }

    bool operator==(const AudioFormat& o) const {
        return sample_rate == o.sample_rate && channels == o.channels
            && bits_per_sample == o.bits_per_sample && frame_duration_ms == o.frame_duration_ms;
    }
    bool operator!=(const AudioFormat& o) const { return !(*this == o); }

    // Negotiate: pick best common format. Prefer higher sample rate, stereo, 16-bit, 20ms frames.
    static AudioFormat negotiate(const std::vector<AudioFormat>& supported) {
        if (supported.empty()) return AudioFormat{};
        AudioFormat best = supported[0];
        for (auto& f : supported) {
            if (f.sample_rate > best.sample_rate) best = f;
            else if (f.sample_rate == best.sample_rate && f.channels > best.channels) best = f;
            else if (f.sample_rate == best.sample_rate && f.channels == best.channels
                     && f.bits_per_sample > best.bits_per_sample) best = f;
        }
        return best;
    }

    static std::vector<AudioFormat> default_formats() {
        return {
            {48000, 2, 16, 20},
            {48000, 1, 16, 20},
            {44100, 2, 16, 20},
            {44100, 1, 16, 20},
            {24000, 1, 16, 20},
            {16000, 1, 16, 20},
            {8000,  1, 16, 20},
        };
    }
};

// Device information
struct AudioDeviceInfo {
    std::string id;
    std::string name;
    bool is_input = false;
    bool is_output = false;
    bool is_default = false;
    uint32_t max_channels = 2;
    std::vector<uint32_t> supported_sample_rates;
};

// =============================================================================
// Section 2: Ring buffer — lock-free single-producer single-consumer
// =============================================================================

template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity) : capacity_(capacity + 1), buffer_(capacity_) {
        read_idx_.store(0, std::memory_order_relaxed);
        write_idx_.store(0, std::memory_order_relaxed);
    }

    bool push(const T& item) {
        size_t w = write_idx_.load(std::memory_order_relaxed);
        size_t next = (w + 1) % capacity_;
        if (next == read_idx_.load(std::memory_order_acquire)) return false; // full
        buffer_[w] = item;
        write_idx_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        size_t r = read_idx_.load(std::memory_order_relaxed);
        if (r == write_idx_.load(std::memory_order_acquire)) return false; // empty
        item = std::move(buffer_[r]);
        read_idx_.store((r + 1) % capacity_, std::memory_order_release);
        return true;
    }

    size_t size() const {
        size_t w = write_idx_.load(std::memory_order_acquire);
        size_t r = read_idx_.load(std::memory_order_acquire);
        return (w >= r) ? (w - r) : (capacity_ - r + w);
    }

    bool empty() const { return size() == 0; }
    bool full() const { return size() >= capacity_ - 1; }
    size_t capacity() const { return capacity_ - 1; }

    void clear() {
        read_idx_.store(write_idx_.load(std::memory_order_acquire), std::memory_order_release);
    }

private:
    size_t capacity_;
    std::vector<T> buffer_;
    std::atomic<size_t> read_idx_;
    std::atomic<size_t> write_idx_;
};

// Byte-oriented ring buffer for raw audio samples
class ByteRingBuffer {
public:
    explicit ByteRingBuffer(size_t capacity_bytes)
        : capacity_(capacity_bytes), buffer_(capacity_bytes) {}

    size_t write(const uint8_t* data, size_t len) {
        std::lock_guard lk(mutex_);
        size_t space = capacity_ - used_;
        size_t to_write = std::min(len, space);
        if (to_write == 0) return 0;

        size_t first = std::min(to_write, capacity_ - write_pos_);
        std::memcpy(&buffer_[write_pos_], data, first);
        if (to_write > first) {
            std::memcpy(&buffer_[0], data + first, to_write - first);
        }
        write_pos_ = (write_pos_ + to_write) % capacity_;
        used_ += to_write;
        return to_write;
    }

    size_t read(uint8_t* out, size_t len) {
        std::lock_guard lk(mutex_);
        size_t to_read = std::min(len, used_);
        if (to_read == 0) return 0;

        size_t first = std::min(to_read, capacity_ - read_pos_);
        std::memcpy(out, &buffer_[read_pos_], first);
        if (to_read > first) {
            std::memcpy(out + first, &buffer_[0], to_read - first);
        }
        read_pos_ = (read_pos_ + to_read) % capacity_;
        used_ -= to_read;
        return to_read;
    }

    size_t available() const { std::lock_guard lk(mutex_); return used_; }
    size_t free_space() const { std::lock_guard lk(mutex_); return capacity_ - used_; }
    void clear() { std::lock_guard lk(mutex_); read_pos_ = 0; write_pos_ = 0; used_ = 0; }

private:
    size_t capacity_;
    std::vector<uint8_t> buffer_;
    size_t read_pos_ = 0;
    size_t write_pos_ = 0;
    size_t used_ = 0;
    mutable std::mutex mutex_;
};

// =============================================================================
// Section 3: Simple linear resampler (nearest-neighbor + linear interpolation)
// =============================================================================

class AudioResampler {
public:
    AudioResampler() = default;

    // Resample PCM 16-bit interleaved samples
    // Simple linear interpolation for up/down sampling
    std::vector<int16_t> resample(const int16_t* in, size_t in_samples, uint32_t in_channels,
                                   uint32_t in_rate, uint32_t out_rate) {
        if (in_rate == out_rate) {
            return std::vector<int16_t>(in, in + in_samples);
        }

        double ratio = static_cast<double>(out_rate) / in_rate;
        size_t out_frames = static_cast<size_t>(in_samples / in_channels * ratio);
        std::vector<int16_t> out(out_frames * in_channels, 0);

        for (size_t ch = 0; ch < in_channels; ++ch) {
            for (size_t i = 0; i < out_frames; ++i) {
                double src_idx = static_cast<double>(i) / ratio;
                size_t idx0 = static_cast<size_t>(src_idx);
                size_t idx1 = std::min(idx0 + 1, (in_samples / in_channels) - 1);
                double frac = src_idx - idx0;

                int16_t s0 = in[idx0 * in_channels + ch];
                int16_t s1 = in[idx1 * in_channels + ch];
                out[i * in_channels + ch] = static_cast<int16_t>(
                    s0 + (s1 - s0) * frac);
            }
        }
        return out;
    }

    // Convert channel count: mono -> stereo replicates; stereo -> mono averages
    std::vector<int16_t> convert_channels(const int16_t* in, size_t in_samples,
                                           uint32_t in_channels, uint32_t out_channels) {
        if (in_channels == out_channels) {
            return std::vector<int16_t>(in, in + in_samples);
        }

        size_t in_frames = in_samples / in_channels;
        std::vector<int16_t> result(in_frames * out_channels, 0);

        if (in_channels == 1 && out_channels == 2) {
            for (size_t i = 0; i < in_frames; ++i) {
                result[i * 2] = in[i];
                result[i * 2 + 1] = in[i];
            }
        } else if (in_channels == 2 && out_channels == 1) {
            for (size_t i = 0; i < in_frames; ++i) {
                result[i] = static_cast<int16_t>((int32_t(in[i * 2]) + int32_t(in[i * 2 + 1])) / 2);
            }
        }
        return result;
    }

    // Convert bit depth
    std::vector<int16_t> convert_depth(const uint8_t* in, size_t in_bytes,
                                        uint32_t in_bits, uint32_t out_bits) {
        if (in_bits == out_bits) {
            size_t samples = in_bytes / (in_bits / 8);
            return std::vector<int16_t>(reinterpret_cast<const int16_t*>(in),
                                        reinterpret_cast<const int16_t*>(in) + samples);
        }
        spdlog::warn("Bit depth conversion {}-bit -> {}-bit not fully implemented", in_bits, out_bits);
        return {};
    }
};

// =============================================================================
// Section 4: Silence detection / Voice Activity Detection (VAD)
// =============================================================================

class SilenceDetector {
public:
    struct Config {
        double silence_threshold_db = -40.0;   // below this is silence
        double speech_threshold_db = -30.0;     // above this is speech
        size_t silence_frames_before_cut = 30;  // consecutive silent frames before cutting
        size_t speech_frames_before_start = 5;  // consecutive speech frames before resuming
    };

    explicit SilenceDetector(const Config& cfg = Config{}) : config_(cfg) {}

    // Returns true if frame is silence, false if it contains meaningful audio
    bool is_silence(const int16_t* samples, size_t count) {
        if (count == 0) return true;

        // Compute RMS energy
        double sum_sq = 0.0;
        for (size_t i = 0; i < count; ++i) {
            double s = static_cast<double>(samples[i]);
            sum_sq += s * s;
        }
        double rms = std::sqrt(sum_sq / count);
        double db = 20.0 * std::log10(std::max(rms, 1.0) / 32768.0);

        bool silent = (db < config_.silence_threshold_db);

        if (silent) {
            silence_count_++;
            speech_count_ = 0;
        } else {
            speech_count_++;
            silence_count_ = 0;
        }

        // Hysteresis: don't flip state on a single frame
        if (is_currently_silent_) {
            if (speech_count_ >= config_.speech_frames_before_start) {
                is_currently_silent_ = false;
                spdlog::debug("VAD: speech resumed ({} consecutive speech frames)", speech_count_);
            }
        } else {
            if (silence_count_ >= config_.silence_frames_before_cut) {
                is_currently_silent_ = true;
                spdlog::debug("VAD: silence detected ({} consecutive silent frames)", silence_count_);
            }
        }

        return is_currently_silent_;
    }

    bool currently_silent() const { return is_currently_silent_; }
    size_t consecutive_silence() const { return silence_count_; }
    size_t consecutive_speech() const { return speech_count_; }

    void reset() {
        silence_count_ = 0;
        speech_count_ = 0;
        is_currently_silent_ = false;
    }

private:
    Config config_;
    size_t silence_count_ = 0;
    size_t speech_count_ = 0;
    bool is_currently_silent_ = false;
};

// =============================================================================
// Section 5: Noise suppression using simple spectral subtraction
// =============================================================================

class NoiseSuppressor {
public:
    struct Config {
        bool enabled = true;
        double reduction_db = 12.0;         // amount of noise reduction
        double noise_floor_learning_rate = 0.02;
        double over_subtraction_factor = 1.5;
        size_t fft_size = 512;              // must be power of 2
    };

    explicit NoiseSuppressor(const Config& cfg = Config{}) : config_(cfg) {
        noise_floor_.resize(config_.fft_size / 2 + 1, 1.0);
        // Hann window
        window_.resize(config_.fft_size);
        for (size_t i = 0; i < config_.fft_size; ++i) {
            window_[i] = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * M_PI * i / (config_.fft_size - 1))));
        }
    }

    // Process mono float samples. Returns processed samples (same size).
    std::vector<float> process(const float* samples, size_t count) {
        if (!config_.enabled || count < config_.fft_size) {
            return std::vector<float>(samples, samples + count);
        }

        std::vector<float> result(samples, samples + count);
        size_t hop = config_.fft_size / 2;
        size_t num_bins = config_.fft_size / 2 + 1;

        for (size_t pos = 0; pos + config_.fft_size <= count; pos += hop) {
            // Window and perform real FFT approximation via DFT (simple for clarity)
            std::vector<std::complex<double>> freq = dft(&samples[pos], config_.fft_size);

            // Compute magnitude spectrum
            std::vector<double> mag(num_bins);
            for (size_t k = 0; k < num_bins; ++k) {
                mag[k] = std::abs(freq[k]);
                // Update noise floor estimate (only when likely noise)
                if (mag[k] < noise_floor_[k] * 1.5) {
                    noise_floor_[k] = (1.0 - config_.noise_floor_learning_rate) * noise_floor_[k]
                                    + config_.noise_floor_learning_rate * mag[k];
                }
                // Spectral subtraction
                double gain = std::max(0.0, mag[k] - config_.over_subtraction_factor * noise_floor_[k]);
                gain = std::max(gain, mag[k] * 0.01); // noise floor
                if (mag[k] > 0) {
                    freq[k] *= gain / mag[k];
                }
            }

            // Inverse DFT and overlap-add
            auto frame = idft(freq, config_.fft_size);
            for (size_t i = 0; i < config_.fft_size && pos + i < count; ++i) {
                result[pos + i] = static_cast<float>(frame[i].real());
            }
        }

        // Apply a simple output limiter
        for (auto& v : result) {
            v = std::clamp(v, -1.0f, 1.0f);
        }
        return result;
    }

private:
    Config config_;
    std::vector<float> window_;
    std::vector<double> noise_floor_;

    // Simple DFT for spectral processing (not FFT-optimized, but functionally correct)
    std::vector<std::complex<double>> dft(const float* samples, size_t n) {
        size_t num_bins = n / 2 + 1;
        std::vector<std::complex<double>> result(num_bins);
        for (size_t k = 0; k < num_bins; ++k) {
            double re = 0, im = 0;
            double angle = -2.0 * M_PI * k / n;
            for (size_t j = 0; j < n; ++j) {
                double s = samples[j] * window_[j];
                re += s * std::cos(angle * j);
                im += s * std::sin(angle * j);
            }
            result[k] = std::complex<double>(re, im);
        }
        return result;
    }

    std::vector<std::complex<double>> idft(const std::vector<std::complex<double>>& freq, size_t n) {
        std::vector<std::complex<double>> result(n, {0, 0});
        for (size_t j = 0; j < n; ++j) {
            double re = freq[0].real() / 2.0;
            for (size_t k = 1; k < freq.size(); ++k) {
                double angle = 2.0 * M_PI * k * j / n;
                re += freq[k].real() * std::cos(angle) - freq[k].imag() * std::sin(angle);
            }
            result[j] = std::complex<double>(re * 2.0 / n, 0);
        }
        return result;
    }
};

// =============================================================================
// Section 6: Echo cancellation — LMS adaptive filter
// =============================================================================

class EchoCanceller {
public:
    struct Config {
        bool enabled = true;
        size_t filter_length = 256;        // tap count (covers ~5.3ms at 48kHz)
        double step_size = 0.01;           // LMS convergence rate
        double leak_factor = 0.9999;       // prevent coefficient drift
        double double_talk_threshold = 0.5; // prevent adaptation during double-talk
    };

    explicit EchoCanceller(const Config& cfg = Config{}) : config_(cfg) {
        filter_weights_.resize(config_.filter_length, 0.0);
        far_end_buffer_.resize(config_.filter_length, 0);
    }

    // Process capture frame: remove echo estimated from the far-end (speaker) signal.
    // far_end: the audio being played out (reference)
    // capture: the microphone input (to be cleaned)
    // Returns: cleaned capture signal
    std::vector<int16_t> cancel(const int16_t* far_end, size_t far_len,
                                 const int16_t* capture, size_t cap_len) {
        if (!config_.enabled || far_len == 0 || cap_len == 0) {
            return std::vector<int16_t>(capture, capture + cap_len);
        }

        // Feed far-end into delay buffer
        for (size_t i = 0; i < far_len; ++i) {
            far_end_buffer_[far_end_pos_] = static_cast<double>(far_end[i]);
            far_end_pos_ = (far_end_pos_ + 1) % config_.filter_length;
        }

        std::vector<int16_t> result(cap_len);

        for (size_t n = 0; n < cap_len; ++n) {
            // Compute estimated echo from filter
            double echo_est = 0.0;
            for (size_t k = 0; k < config_.filter_length; ++k) {
                size_t idx = (far_end_pos_ + config_.filter_length - 1 - k) % config_.filter_length;
                echo_est += filter_weights_[k] * far_end_buffer_[idx];
            }

            double cap_sample = static_cast<double>(capture[n]);
            double error = cap_sample - echo_est;

            // Double-talk detection: if capture is much larger than echo estimate,
            // probably near-end speech, so reduce adaptation
            double cap_energy = cap_sample * cap_sample;
            double echo_energy = echo_est * echo_est;
            double adapt_scale = 1.0;
            if (echo_energy > 0 && cap_energy / echo_energy > config_.double_talk_threshold) {
                adapt_scale = 0.1; // reduce adaptation during double-talk
            }

            // LMS update
            double mu = config_.step_size * adapt_scale;
            for (size_t k = 0; k < config_.filter_length; ++k) {
                size_t idx = (far_end_pos_ + config_.filter_length - 1 - k) % config_.filter_length;
                filter_weights_[k] = config_.leak_factor * filter_weights_[k]
                                   + mu * error * far_end_buffer_[idx];
            }

            // Shift far-end buffer (add current playout sample; in practice this is separate)
            // Actually we rotate: the far_end buffer stays as-is; we just advance far_end_pos_
            // when new far-end data arrives. For this sample-by-sample cancellation,
            // we treat the capture sample's corresponding far-end as the buffer state.

            // Clamp output
            result[n] = static_cast<int16_t>(std::clamp(error, -32768.0, 32767.0));
        }

        return result;
    }

    void reset() {
        std::fill(filter_weights_.begin(), filter_weights_.end(), 0.0);
        std::fill(far_end_buffer_.begin(), far_end_buffer_.end(), 0.0);
        far_end_pos_ = 0;
    }

private:
    Config config_;
    std::vector<double> filter_weights_;
    std::vector<double> far_end_buffer_;
    size_t far_end_pos_ = 0;
};

// =============================================================================
// Section 7: Jitter buffer for receiving audio
// =============================================================================

class JitterBuffer {
public:
    struct Config {
        size_t max_packets = 50;                // max queued audio packets
        size_t target_playout_delay_ms = 60;    // target buffer depth
        size_t min_playout_delay_ms = 20;
        size_t max_playout_delay_ms = 200;
        size_t frame_duration_ms = 20;
        size_t sample_rate = 48000;
        size_t channels = 2;
    };

    struct AudioPacket {
        uint64_t timestamp;
        std::vector<int16_t> samples;
        size_t frame_count;
    };

    explicit JitterBuffer(const Config& cfg = Config{}) : config_(cfg) {}

    // Push a decoded audio packet (with RTP-like timestamp)
    void push(uint64_t timestamp, const std::vector<int16_t>& samples) {
        std::lock_guard lk(mutex_);
        AudioPacket pkt{timestamp, samples, samples.size() / config_.channels};
        packets_.push_back(std::move(pkt));

        // Keep sorted by timestamp
        std::sort(packets_.begin(), packets_.end(),
            [](const AudioPacket& a, const AudioPacket& b) { return a.timestamp < b.timestamp; });

        // Discard old packets
        while (packets_.size() > config_.max_packets) {
            packets_.pop_front();
            dropped_packets_++;
        }

        // Adapt playout delay based on jitter
        if (packets_.size() >= 2) {
            auto span = packets_.back().timestamp - packets_.front().timestamp;
            size_t span_ms = static_cast<size_t>(span * 1000 / config_.sample_rate);
            size_t ideal_delay = span_ms + config_.frame_duration_ms;
            ideal_delay = std::clamp(ideal_delay,
                config_.min_playout_delay_ms, config_.max_playout_delay_ms);
            // Smooth adaptation
            target_delay_ms_ = (target_delay_ms_ * 7 + ideal_delay) / 8;
        }
    }

    // Pop next frame for playout, respecting target delay
    std::optional<AudioPacket> pop(uint64_t playout_timestamp) {
        std::lock_guard lk(mutex_);
        if (packets_.empty()) return std::nullopt;

        // Find first packet with timestamp <= playout_timestamp
        // (allow playing slightly late packets if buffer is behind)
        auto it = packets_.begin();
        int64_t ahead_by = static_cast<int64_t>(it->timestamp) - static_cast<int64_t>(playout_timestamp);
        size_t ahead_ms = static_cast<size_t>(std::abs(ahead_by) * 1000 / config_.sample_rate);

        if (ahead_by > 0 && ahead_ms > config_.max_playout_delay_ms) {
            return std::nullopt; // packet too far ahead, wait
        }

        // If we're behind, skip to catch up
        while (packets_.size() > 1) {
            auto next_it = it + 1;
            int64_t next_ahead = static_cast<int64_t>(next_it->timestamp) - static_cast<int64_t>(playout_timestamp);
            size_t next_ahead_ms = static_cast<size_t>(std::abs(next_ahead) * 1000 / config_.sample_rate);
            if (next_ahead <= 0 || next_ahead_ms < ahead_ms) {
                packets_.pop_front();
                skipped_packets_++;
                it = packets_.begin();
                ahead_by = static_cast<int64_t>(it->timestamp) - static_cast<int64_t>(playout_timestamp);
                ahead_ms = static_cast<size_t>(std::abs(ahead_by) * 1000 / config_.sample_rate);
            } else {
                break;
            }
        }

        AudioPacket pkt = std::move(packets_.front());
        packets_.pop_front();
        packets_played_++;
        return pkt;
    }

    size_t size() const { std::lock_guard lk(mutex_); return packets_.size(); }
    size_t target_delay_ms() const { return target_delay_ms_.load(); }
    size_t dropped() const { return dropped_packets_.load(); }
    size_t skipped() const { return skipped_packets_.load(); }
    size_t played() const { return packets_played_.load(); }
    void clear() { std::lock_guard lk(mutex_); packets_.clear(); }

    struct Stats {
        size_t queue_depth;
        size_t target_delay_ms;
        size_t dropped;
        size_t skipped;
        size_t played;
    };
    Stats stats() const {
        return Stats{size(), target_delay_ms_.load(), dropped_packets_.load(),
                     skipped_packets_.load(), packets_played_.load()};
    }

private:
    Config config_;
    std::deque<AudioPacket> packets_;
    std::atomic<size_t> target_delay_ms_{60};
    std::atomic<size_t> dropped_packets_{0};
    std::atomic<size_t> skipped_packets_{0};
    std::atomic<size_t> packets_played_{0};
    mutable std::mutex mutex_;
};

// =============================================================================
// Section 8: Opus encoder wrapper
// =============================================================================

class OpusEncoder {
public:
    struct Config {
        uint32_t sample_rate = 48000;
        uint32_t channels = 2;
        uint32_t bitrate = 64000;           // 64 kbps
        uint32_t complexity = 5;             // 0-10
        bool vbr = true;
        bool dtx = true;                     // discontinuous transmission (silence suppression)
        bool fec = true;                     // forward error correction
        int signal_type = OPUS_AUTO;         // OPUS_AUTO, OPUS_VOICE, OPUS_MUSIC
    };

    explicit OpusEncoder(const Config& cfg = Config{}) : config_(cfg) {
        init();
    }

    ~OpusEncoder() { destroy(); }

    bool init() {
        destroy();

#if HAS_OPUS
        int err = 0;
        encoder_ = opus_encoder_create(
            static_cast<opus_int32>(config_.sample_rate),
            static_cast<int>(config_.channels),
            OPUS_APPLICATION_VOIP, // good for speech/VoIP
            &err);

        if (err != OPUS_OK || !encoder_) {
            spdlog::error("Opus encoder creation failed: {}", opus_strerror(err));
            encoder_ = nullptr;
            initialized_ = false;
            return false;
        }

        opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(config_.bitrate));
        opus_encoder_ctl(encoder_, OPUS_SET_COMPLEXITY(config_.complexity));
        opus_encoder_ctl(encoder_, OPUS_SET_VBR(config_.vbr ? 1 : 0));
        opus_encoder_ctl(encoder_, OPUS_SET_DTX(config_.dtx ? 1 : 0));
        opus_encoder_ctl(encoder_, OPUS_SET_INBAND_FEC(config_.fec ? 1 : 0));
        opus_encoder_ctl(encoder_, OPUS_SET_SIGNAL(config_.signal_type));
        opus_encoder_ctl(encoder_, OPUS_SET_PACKET_LOSS_PERC(10));

        initialized_ = true;
        spdlog::info("Opus encoder initialized: {}hz {}ch {}bps",
            config_.sample_rate, config_.channels, config_.bitrate);
        return true;
#else
        spdlog::warn("Opus not available — using passthrough encoder");
        initialized_ = true; // "soft" init for passthrough
        return true;
#endif
    }

    // Encode PCM 16-bit interleaved samples to Opus packet
    std::vector<uint8_t> encode(const int16_t* pcm, size_t frame_size, bool& is_silence_frame) {
#if HAS_OPUS
        if (!initialized_ || !encoder_) {
            is_silence_frame = true;
            return {};
        }

        std::vector<uint8_t> output(4096); // max Opus packet size
        opus_int32 encoded_bytes = opus_encode(
            encoder_,
            pcm,
            static_cast<opus_int32>(frame_size),
            output.data(),
            static_cast<opus_int32>(output.size()));

        if (encoded_bytes < 0) {
            spdlog::error("Opus encode error: {}", opus_strerror(encoded_bytes));
            is_silence_frame = true;
            return {};
        }

        frames_encoded_++;
        bytes_encoded_ += encoded_bytes;
        output.resize(encoded_bytes);

        // Detect silence in Opus output (DTX may produce tiny frames)
        if (encoded_bytes <= 2) {
            silence_frames_encoded_++;
            is_silence_frame = true;
        } else {
            is_silence_frame = false;
        }

        return output;
#else
        // Passthrough: just package raw PCM
        (void)is_silence_frame;
        size_t byte_len = frame_size * sizeof(int16_t);
        frames_encoded_++;
        bytes_encoded_ += byte_len;
        is_silence_frame = false;
        return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(pcm),
                                     reinterpret_cast<const uint8_t*>(pcm) + byte_len);
#endif
    }

    void set_bitrate(uint32_t bps) {
        config_.bitrate = bps;
#if HAS_OPUS
        if (encoder_) opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bps));
#endif
    }
    void set_complexity(uint32_t cplx) {
        config_.complexity = std::min(cplx, 10u);
#if HAS_OPUS
        if (encoder_) opus_encoder_ctl(encoder_, OPUS_SET_COMPLEXITY(config_.complexity));
#endif
    }
    void set_vbr(bool on) {
        config_.vbr = on;
#if HAS_OPUS
        if (encoder_) opus_encoder_ctl(encoder_, OPUS_SET_VBR(on ? 1 : 0));
#endif
    }

    uint64_t frames_encoded() const { return frames_encoded_.load(); }
    uint64_t bytes_encoded() const { return bytes_encoded_.load(); }
    uint64_t silence_frames() const { return silence_frames_encoded_.load(); }
    uint32_t sample_rate() const { return config_.sample_rate; }
    uint32_t channels() const { return config_.channels; }

private:
    void destroy() {
#if HAS_OPUS
        if (encoder_) { opus_encoder_destroy(encoder_); encoder_ = nullptr; }
#endif
        initialized_ = false;
    }

    Config config_;
#if HAS_OPUS
    OpusEncoder* encoder_ = nullptr;
#endif
    bool initialized_ = false;
    std::atomic<uint64_t> frames_encoded_{0};
    std::atomic<uint64_t> bytes_encoded_{0};
    std::atomic<uint64_t> silence_frames_encoded_{0};
};

// =============================================================================
// Section 9: Opus decoder wrapper
// =============================================================================

class OpusDecoder {
public:
    struct Config {
        uint32_t sample_rate = 48000;
        uint32_t channels = 2;
        uint32_t frame_size = 960;           // samples per channel per frame (20ms at 48kHz)
    };

    explicit OpusDecoder(const Config& cfg = Config{}) : config_(cfg) {
        init();
    }

    ~OpusDecoder() { destroy(); }

    bool init() {
        destroy();

#if HAS_OPUS
        int err = 0;
        decoder_ = opus_decoder_create(
            static_cast<opus_int32>(config_.sample_rate),
            static_cast<int>(config_.channels),
            &err);

        if (err != OPUS_OK || !decoder_) {
            spdlog::error("Opus decoder creation failed: {}", opus_strerror(err));
            decoder_ = nullptr;
            initialized_ = false;
            return false;
        }

        initialized_ = true;
        spdlog::info("Opus decoder initialized: {}hz {}ch",
            config_.sample_rate, config_.channels);
        return true;
#else
        spdlog::warn("Opus not available — using passthrough decoder");
        initialized_ = true;
        return true;
#endif
    }

    // Decode Opus packet to PCM 16-bit interleaved samples
    std::vector<int16_t> decode(const uint8_t* data, size_t len, bool& fec) {
        fec = false;
#if HAS_OPUS
        if (!initialized_ || !decoder_) return {};

        std::vector<int16_t> pcm(config_.frame_size * config_.channels);
        int decoded_frames = opus_decode(
            decoder_,
            data,
            static_cast<opus_int32>(len),
            pcm.data(),
            static_cast<opus_int32>(config_.frame_size),
            0); // 0 = normal decode, 1 = FEC

        if (decoded_frames < 0) {
            spdlog::error("Opus decode error: {}", opus_strerror(decoded_frames));
            // Try FEC recovery
            decoded_frames = opus_decode(decoder_, nullptr, 0, pcm.data(),
                                          static_cast<opus_int32>(config_.frame_size), 1);
            if (decoded_frames > 0) fec = true;
        }

        if (decoded_frames > 0) {
            frames_decoded_++;
            bytes_decoded_ += len;
            pcm.resize(decoded_frames * config_.channels);
            // PLC (packet loss concealment) used above via fec flag
            fec_frames_ += fec ? 1 : 0;
            return pcm;
        }
        fec_losses_++;
        return {};
#else
        // Passthrough: treat raw data as PCM16 samples
        (void)fec;
        size_t samples = len / sizeof(int16_t);
        frames_decoded_++;
        bytes_decoded_ += len;
        return std::vector<int16_t>(reinterpret_cast<const int16_t*>(data),
                                     reinterpret_cast<const int16_t*>(data) + samples);
#endif
    }

    uint64_t frames_decoded() const { return frames_decoded_.load(); }
    uint64_t bytes_decoded() const { return bytes_decoded_.load(); }
    uint64_t fec_frames() const { return fec_frames_.load(); }
    uint64_t plc_losses() const { return fec_losses_.load(); }
    uint32_t sample_rate() const { return config_.sample_rate; }
    uint32_t channels() const { return config_.channels; }

private:
    void destroy() {
#if HAS_OPUS
        if (decoder_) { opus_decoder_destroy(decoder_); decoder_ = nullptr; }
#endif
        initialized_ = false;
    }

    Config config_;
#if HAS_OPUS
    OpusDecoder* decoder_ = nullptr;
#endif
    bool initialized_ = false;
    std::atomic<uint64_t> frames_decoded_{0};
    std::atomic<uint64_t> bytes_decoded_{0};
    std::atomic<uint64_t> fec_frames_{0};
    std::atomic<uint64_t> fec_losses_{0};
};

// =============================================================================
// Section 10: Platform audio capture abstraction
// =============================================================================

class AudioCapturer {
public:
    virtual ~AudioCapturer() = default;
    virtual bool open(const AudioDeviceInfo& device, const AudioFormat& fmt) = 0;
    virtual void close() = 0;
    virtual size_t read(int16_t* buffer, size_t frames) = 0; // returns frames read
    virtual bool is_open() const = 0;
    virtual AudioFormat format() const = 0;
    virtual std::string device_name() const = 0;
};

// =============================================================================
// Section 11: Linux PulseAudio capture
// =============================================================================

#ifdef __linux__

class PulseAudioCapturer : public AudioCapturer {
public:
    ~PulseAudioCapturer() override { close(); }

    bool open(const AudioDeviceInfo& device, const AudioFormat& fmt) override {
        close();

        format_ = fmt;
        device_name_ = device.name.empty() ? "default" : device.id;

        int error = 0;
        pa_sample_spec ss;
        ss.format = PA_SAMPLE_S16LE;
        ss.rate = fmt.sample_rate;
        ss.channels = static_cast<uint8_t>(fmt.channels);

        pa_buffer_attr attr;
        attr.maxlength = static_cast<uint32_t>(-1);
        attr.tlength = static_cast<uint32_t>(-1);
        attr.prebuf = static_cast<uint32_t>(-1);
        attr.minreq = static_cast<uint32_t>(-1);
        attr.fragsize = fmt.samples_per_frame() * fmt.bytes_per_sample() * fmt.channels;

        stream_ = pa_simple_new(
            nullptr,                        // default server
            "cppdesk_audio",                // application name
            PA_STREAM_RECORD,               // direction
            device_name_.c_str(),           // device
            "Audio Capture",                // stream description
            &ss,                            // sample format
            nullptr,                        // channel map (default)
            &attr,                          // buffering attributes
            &error);

        if (!stream_) {
            spdlog::error("PulseAudio capture open failed: {}", pa_strerror(error));
            return false;
        }

        opened_ = true;
        spdlog::info("PulseAudio capturer opened: device={} rate={} ch={}",
            device_name_, fmt.sample_rate, fmt.channels);
        return true;
    }

    void close() override {
        if (stream_) {
            pa_simple_free(stream_);
            stream_ = nullptr;
        }
        opened_ = false;
    }

    size_t read(int16_t* buffer, size_t frames) override {
        if (!opened_ || !stream_) return 0;

        int error = 0;
        size_t bytes_requested = frames * format_.channels * sizeof(int16_t);
        size_t bytes_read = 0;

        int ret = pa_simple_read(stream_, buffer, bytes_requested, &error);
        if (ret < 0) {
            spdlog::error("PulseAudio read error: {}", pa_strerror(error));
            return 0;
        }
        bytes_read = bytes_requested;

        return bytes_read / (format_.channels * sizeof(int16_t));
    }

    bool is_open() const override { return opened_; }
    AudioFormat format() const override { return format_; }
    std::string device_name() const override { return device_name_; }

private:
    pa_simple* stream_ = nullptr;
    bool opened_ = false;
    AudioFormat format_;
    std::string device_name_;
};

#endif // __linux__

// =============================================================================
// Section 12: Windows WASAPI capture
// =============================================================================

#ifdef _WIN32

class WasapiCapturer : public AudioCapturer {
public:
    WasapiCapturer() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    }

    ~WasapiCapturer() override {
        close();
        CoUninitialize();
    }

    bool open(const AudioDeviceInfo& device, const AudioFormat& fmt) override {
        close();
        format_ = fmt;
        device_name_ = device.name;

        HRESULT hr;

        // Create device enumerator
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator_));
        if (FAILED(hr)) {
            spdlog::error("WASAPI: failed to create device enumerator: 0x{:x}", hr);
            return false;
        }

        // Get the default capture device (or specific device)
        IMMDevice* dev = nullptr;
        if (device.id.empty()) {
            hr = enumerator_->GetDefaultAudioEndpoint(eCapture, eConsole, &dev);
        } else {
            // Convert device id (string) to wide and get device
            int wide_len = MultiByteToWideChar(CP_UTF8, 0, device.id.c_str(), -1, nullptr, 0);
            std::wstring wide_id(wide_len, 0);
            MultiByteToWideChar(CP_UTF8, 0, device.id.c_str(), -1, &wide_id[0], wide_len);
            hr = enumerator_->GetDevice(wide_id.c_str(), &dev);
        }

        if (FAILED(hr) || !dev) {
            spdlog::error("WASAPI: failed to get capture device: 0x{:x}", hr);
            return false;
        }

        // Activate audio client
        hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(&audio_client_));
        dev->Release();
        if (FAILED(hr)) {
            spdlog::error("WASAPI: failed to activate audio client: 0x{:x}", hr);
            return false;
        }

        // Set wave format
        WAVEFORMATEX wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = static_cast<WORD>(fmt.channels);
        wfx.nSamplesPerSec = fmt.sample_rate;
        wfx.wBitsPerSample = static_cast<WORD>(fmt.bits_per_sample);
        wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        wfx.cbSize = 0;

        REFERENCE_TIME requested_duration = static_cast<REFERENCE_TIME>(fmt.frame_duration_ms) * 10000;
        hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
            requested_duration, 0, &wfx, nullptr);

        if (FAILED(hr)) {
            spdlog::error("WASAPI: failed to initialize audio client: 0x{:x}", hr);
            audio_client_->Release();
            audio_client_ = nullptr;
            return false;
        }

        // Get buffer size
        hr = audio_client_->GetBufferSize(&buffer_frames_);
        if (FAILED(hr)) {
            spdlog::error("WASAPI: failed to get buffer size: 0x{:x}", hr);
        }

        // Create event for buffer ready notification
        capture_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        hr = audio_client_->SetEventHandle(capture_event_);
        if (FAILED(hr)) {
            spdlog::error("WASAPI: failed to set event handle: 0x{:x}", hr);
        }

        // Get capture client
        hr = audio_client_->GetService(__uuidof(IAudioCaptureClient),
            reinterpret_cast<void**>(&capture_client_));
        if (FAILED(hr)) {
            spdlog::error("WASAPI: failed to get capture client: 0x{:x}", hr);
            return false;
        }

        // Start streaming
        hr = audio_client_->Start();
        if (FAILED(hr)) {
            spdlog::error("WASAPI: failed to start stream: 0x{:x}", hr);
            return false;
        }

        opened_ = true;
        spdlog::info("WASAPI capturer opened: device={} rate={} ch={} buf_frames={}",
            device_name_, fmt.sample_rate, fmt.channels, buffer_frames_);
        return true;
    }

    void close() override {
        if (audio_client_) {
            audio_client_->Stop();
        }
        if (capture_client_) {
            capture_client_->Release();
            capture_client_ = nullptr;
        }
        if (audio_client_) {
            audio_client_->Release();
            audio_client_ = nullptr;
        }
        if (enumerator_) {
            enumerator_->Release();
            enumerator_ = nullptr;
        }
        if (capture_event_) {
            CloseHandle(capture_event_);
            capture_event_ = nullptr;
        }
        opened_ = false;
    }

    size_t read(int16_t* buffer, size_t frames) override {
        if (!opened_ || !capture_client_) return 0;

        // Wait for data with short timeout
        DWORD wait_result = WaitForSingleObject(capture_event_, 100); // 100ms timeout
        if (wait_result != WAIT_OBJECT_0) return 0;

        size_t total_frames_read = 0;
        size_t frames_remaining = frames;

        while (frames_remaining > 0) {
            BYTE* data = nullptr;
            UINT32 frames_available = 0;
            DWORD flags = 0;
            UINT64 dev_pos = 0;
            UINT64 qpc_pos = 0;

            HRESULT hr = capture_client_->GetBuffer(&data, &frames_available, &flags, &dev_pos, &qpc_pos);
            if (FAILED(hr)) {
                spdlog::error("WASAPI: GetBuffer failed: 0x{:x}", hr);
                break;
            }

            if (frames_available == 0) break;

            size_t to_copy = std::min(static_cast<size_t>(frames_available), frames_remaining);
            size_t bytes_to_copy = to_copy * format_.channels * sizeof(int16_t);
            std::memcpy(buffer + total_frames_read * format_.channels, data, bytes_to_copy);

            hr = capture_client_->ReleaseBuffer(frames_available);
            if (FAILED(hr)) {
                spdlog::error("WASAPI: ReleaseBuffer failed: 0x{:x}", hr);
            }

            total_frames_read += to_copy;
            frames_remaining -= to_copy;

            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                // Buffer contained silence; still return the data (silence detector handles it)
            }
        }

        return total_frames_read;
    }

    bool is_open() const override { return opened_; }
    AudioFormat format() const override { return format_; }
    std::string device_name() const override { return device_name_; }

private:
    IMMDeviceEnumerator* enumerator_ = nullptr;
    IAudioClient* audio_client_ = nullptr;
    IAudioCaptureClient* capture_client_ = nullptr;
    HANDLE capture_event_ = nullptr;
    bool opened_ = false;
    AudioFormat format_;
    std::string device_name_;
    UINT32 buffer_frames_ = 0;
};

#endif // _WIN32

// =============================================================================
// Section 13: macOS CoreAudio capture
// =============================================================================

#ifdef __APPLE__

class CoreAudioCapturer : public AudioCapturer {
public:
    ~CoreAudioCapturer() override { close(); }

    bool open(const AudioDeviceInfo& device, const AudioFormat& fmt) override {
        close();
        format_ = fmt;
        device_name_ = device.name;

        // Find the default input device (or specific device)
        AudioDeviceID device_id = 0;
        UInt32 prop_size = sizeof(device_id);

        if (device.id.empty()) {
            AudioObjectPropertyAddress addr = {
                kAudioHardwarePropertyDefaultInputDevice,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain
            };
            OSStatus status = AudioObjectGetPropertyData(
                kAudioObjectSystemObject, &addr, 0, nullptr, &prop_size, &device_id);
            if (status != noErr) {
                spdlog::error("CoreAudio: failed to get default input device: {}", status);
                return false;
            }
        } else {
            device_id = static_cast<AudioDeviceID>(std::stoul(device.id));
        }

        // Describe the audio format
        AudioStreamBasicDescription desc = {};
        desc.mSampleRate = static_cast<Float64>(fmt.sample_rate);
        desc.mFormatID = kAudioFormatLinearPCM;
        desc.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
        desc.mBitsPerChannel = static_cast<UInt32>(fmt.bits_per_sample);
        desc.mChannelsPerFrame = static_cast<UInt32>(fmt.channels);
        desc.mFramesPerPacket = 1;
        desc.mBytesPerFrame = desc.mChannelsPerFrame * desc.mBitsPerChannel / 8;
        desc.mBytesPerPacket = desc.mBytesPerFrame * desc.mFramesPerPacket;

        // Create AudioUnit
        AudioComponentDescription comp_desc = {};
        comp_desc.componentType = kAudioUnitType_Output;
        comp_desc.componentSubType = kAudioUnitSubType_HALOutput;
        comp_desc.componentManufacturer = kAudioUnitManufacturer_Apple;

        AudioComponent component = AudioComponentFindNext(nullptr, &comp_desc);
        if (!component) {
            spdlog::error("CoreAudio: failed to find AudioComponent");
            return false;
        }

        OSStatus status = AudioComponentInstanceNew(component, &audio_unit_);
        if (status != noErr) {
            spdlog::error("CoreAudio: failed to create AudioUnit: {}", status);
            return false;
        }

        // Enable input
        UInt32 enable = 1;
        status = AudioUnitSetProperty(audio_unit_,
            kAudioOutputUnitProperty_EnableIO,
            kAudioUnitScope_Input, 1, &enable, sizeof(enable));
        if (status != noErr) {
            spdlog::error("CoreAudio: failed to enable input: {}", status);
            AudioComponentInstanceDispose(audio_unit_);
            audio_unit_ = nullptr;
            return false;
        }

        // Disable output
        enable = 0;
        AudioUnitSetProperty(audio_unit_,
            kAudioOutputUnitProperty_EnableIO,
            kAudioUnitScope_Output, 0, &enable, sizeof(enable));

        // Set device
        status = AudioUnitSetProperty(audio_unit_,
            kAudioOutputUnitProperty_CurrentDevice,
            kAudioUnitScope_Global, 0, &device_id, sizeof(device_id));
        if (status != noErr) {
            spdlog::error("CoreAudio: failed to set device: {}", status);
        }

        // Set format
        status = AudioUnitSetProperty(audio_unit_,
            kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Output, 1, &desc, sizeof(desc));
        if (status != noErr) {
            spdlog::error("CoreAudio: failed to set stream format: {}", status);
        }

        // Set render callback
        AURenderCallbackStruct callback;
        callback.inputProc = &CoreAudioCapturer::render_callback;
        callback.inputProcRefCon = this;
        status = AudioUnitSetProperty(audio_unit_,
            kAudioOutputUnitProperty_SetInputCallback,
            kAudioUnitScope_Global, 0, &callback, sizeof(callback));
        if (status != noErr) {
            spdlog::error("CoreAudio: failed to set callback: {}", status);
        }

        // Initialize
        status = AudioUnitInitialize(audio_unit_);
        if (status != noErr) {
            spdlog::error("CoreAudio: failed to initialize AudioUnit: {}", status);
            AudioComponentInstanceDispose(audio_unit_);
            audio_unit_ = nullptr;
            return false;
        }

        // Start
        status = AudioOutputUnitStart(audio_unit_);
        if (status != noErr) {
            spdlog::error("CoreAudio: failed to start AudioUnit: {}", status);
            AudioUnitUninitialize(audio_unit_);
            AudioComponentInstanceDispose(audio_unit_);
            audio_unit_ = nullptr;
            return false;
        }

        opened_ = true;
        capture_buffer_.clear();
        spdlog::info("CoreAudio capturer opened: device={} rate={} ch={}",
            device_name_, fmt.sample_rate, fmt.channels);
        return true;
    }

    void close() override {
        if (audio_unit_) {
            AudioOutputUnitStop(audio_unit_);
            AudioUnitUninitialize(audio_unit_);
            AudioComponentInstanceDispose(audio_unit_);
            audio_unit_ = nullptr;
        }
        opened_ = false;
    }

    size_t read(int16_t* buffer, size_t frames) override {
        if (!opened_) return 0;

        std::lock_guard lk(capture_mutex_);
        size_t bytes_needed = frames * format_.channels * sizeof(int16_t);
        size_t available = capture_buffer_.available();

        if (available < bytes_needed) {
            // Not enough data; return what we can or wait via short spin
            return 0;
        }

        return capture_buffer_.read(reinterpret_cast<uint8_t*>(buffer), bytes_needed)
               / (format_.channels * sizeof(int16_t));
    }

    bool is_open() const override { return opened_; }
    AudioFormat format() const override { return format_; }
    std::string device_name() const override { return device_name_; }

private:
    static OSStatus render_callback(void* inRefCon,
        AudioUnitRenderActionFlags* ioActionFlags,
        const AudioTimeStamp* inTimeStamp,
        UInt32 inBusNumber,
        UInt32 inNumberFrames,
        AudioBufferList* ioData) {

        auto* self = static_cast<CoreAudioCapturer*>(inRefCon);
        if (!self || !self->audio_unit_) return noErr;

        AudioBufferList buffer_list;
        buffer_list.mNumberBuffers = 1;
        buffer_list.mBuffers[0].mNumberChannels = self->format_.channels;
        buffer_list.mBuffers[0].mDataByteSize = inNumberFrames * self->format_.channels * sizeof(int16_t);
        buffer_list.mBuffers[0].mData = malloc(buffer_list.mBuffers[0].mDataByteSize);

        OSStatus status = AudioUnitRender(self->audio_unit_,
            ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, &buffer_list);

        if (status == noErr) {
            std::lock_guard lk(self->capture_mutex_);
            self->capture_buffer_.write(
                static_cast<const uint8_t*>(buffer_list.mBuffers[0].mData),
                buffer_list.mBuffers[0].mDataByteSize);
        }

        free(buffer_list.mBuffers[0].mData);

        // Silence the output side
        if (ioData) {
            for (UInt32 i = 0; i < ioData->mNumberBuffers; ++i) {
                memset(ioData->mBuffers[i].mData, 0, ioData->mBuffers[i].mDataByteSize);
            }
        }

        return noErr;
    }

    AudioUnit audio_unit_ = nullptr;
    bool opened_ = false;
    AudioFormat format_;
    std::string device_name_;
    ByteRingBuffer capture_buffer_{1024 * 1024}; // 1MB ring buffer
    std::mutex capture_mutex_;
};

#endif // __APPLE__

// =============================================================================
// Section 14: Platform audio playback abstraction
// =============================================================================

class AudioPlayer {
public:
    virtual ~AudioPlayer() = default;
    virtual bool open(const AudioDeviceInfo& device, const AudioFormat& fmt) = 0;
    virtual void close() = 0;
    virtual size_t write(const int16_t* buffer, size_t frames) = 0;
    virtual bool is_open() const = 0;
    virtual AudioFormat format() const = 0;
    virtual void set_volume(double vol) = 0;  // 0.0 - 1.0
    virtual double volume() const = 0;
    virtual void set_mute(bool mute) = 0;
    virtual bool is_muted() const = 0;
};

// =============================================================================
// Section 15: Linux PulseAudio playback
// =============================================================================

#ifdef __linux__

class PulseAudioPlayer : public AudioPlayer {
public:
    ~PulseAudioPlayer() override { close(); }

    bool open(const AudioDeviceInfo& device, const AudioFormat& fmt) override {
        close();
        format_ = fmt;
        device_name_ = device.name.empty() ? "default" : device.id;

        int error = 0;
        pa_sample_spec ss;
        ss.format = PA_SAMPLE_S16LE;
        ss.rate = fmt.sample_rate;
        ss.channels = static_cast<uint8_t>(fmt.channels);

        pa_buffer_attr attr;
        attr.maxlength = static_cast<uint32_t>(-1);
        attr.tlength = fmt.samples_per_frame() * fmt.bytes_per_sample() * fmt.channels * 4;
        attr.prebuf = static_cast<uint32_t>(-1);
        attr.minreq = static_cast<uint32_t>(-1);
        attr.fragsize = static_cast<uint32_t>(-1);

        stream_ = pa_simple_new(
            nullptr,
            "cppdesk_audio",
            PA_STREAM_PLAYBACK,
            device_name_.c_str(),
            "Audio Playback",
            &ss,
            nullptr,
            &attr,
            &error);

        if (!stream_) {
            spdlog::error("PulseAudio playback open failed: {}", pa_strerror(error));
            return false;
        }

        opened_ = true;
        spdlog::info("PulseAudio player opened: device={} rate={} ch={}",
            device_name_, fmt.sample_rate, fmt.channels);
        return true;
    }

    void close() override {
        if (stream_) {
            pa_simple_flush(stream_, nullptr);
            pa_simple_free(stream_);
            stream_ = nullptr;
        }
        opened_ = false;
    }

    size_t write(const int16_t* buffer, size_t frames) override {
        if (!opened_ || !stream_) return 0;

        int error = 0;
        size_t bytes = frames * format_.channels * sizeof(int16_t);
        int ret = pa_simple_write(stream_, buffer, bytes, &error);
        if (ret < 0) {
            spdlog::error("PulseAudio write error: {}", pa_strerror(error));
            return 0;
        }
        return frames;
    }

    bool is_open() const override { return opened_; }
    AudioFormat format() const override { return format_; }
    void set_volume(double vol) override {
        vol = std::clamp(vol, 0.0, 1.0);
        volume_ = vol;
    }
    double volume() const override { return volume_; }
    void set_mute(bool mute) override { muted_ = mute; }
    bool is_muted() const override { return muted_; }

private:
    pa_simple* stream_ = nullptr;
    bool opened_ = false;
    AudioFormat format_;
    std::string device_name_;
    double volume_ = 1.0;
    bool muted_ = false;
};

#endif // __linux__

// =============================================================================
// Section 16: Windows WASAPI playback
// =============================================================================

#ifdef _WIN32

class WasapiPlayer : public AudioPlayer {
public:
    WasapiPlayer() { CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~WasapiPlayer() override { close(); CoUninitialize(); }

    bool open(const AudioDeviceInfo& device, const AudioFormat& fmt) override {
        close();
        format_ = fmt;
        device_name_ = device.name;

        HRESULT hr;

        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator_));
        if (FAILED(hr)) { spdlog::error("WASAPI play: enumerator failed"); return false; }

        IMMDevice* dev = nullptr;
        if (device.id.empty()) {
            hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
        } else {
            int wide_len = MultiByteToWideChar(CP_UTF8, 0, device.id.c_str(), -1, nullptr, 0);
            std::wstring wide_id(wide_len, 0);
            MultiByteToWideChar(CP_UTF8, 0, device.id.c_str(), -1, &wide_id[0], wide_len);
            hr = enumerator_->GetDevice(wide_id.c_str(), &dev);
        }
        if (FAILED(hr) || !dev) { spdlog::error("WASAPI play: no device"); return false; }

        hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(&audio_client_));
        dev->Release();
        if (FAILED(hr)) { spdlog::error("WASAPI play: activate failed"); return false; }

        WAVEFORMATEX wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = static_cast<WORD>(fmt.channels);
        wfx.nSamplesPerSec = fmt.sample_rate;
        wfx.wBitsPerSample = static_cast<WORD>(fmt.bits_per_sample);
        wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

        REFERENCE_TIME buf_dur = static_cast<REFERENCE_TIME>(fmt.frame_duration_ms) * 10000 * 2;
        hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK, buf_dur, 0, &wfx, nullptr);
        if (FAILED(hr)) { spdlog::error("WASAPI play: init failed"); return false; }

        hr = audio_client_->GetBufferSize(&buffer_frames_);
        if (FAILED(hr)) { spdlog::error("WASAPI play: get buffer size failed"); }

        hr = audio_client_->GetService(__uuidof(IAudioRenderClient),
            reinterpret_cast<void**>(&render_client_));
        if (FAILED(hr)) { spdlog::error("WASAPI play: get render client failed"); return false; }

        play_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        audio_client_->SetEventHandle(play_event_);

        hr = audio_client_->Start();
        if (FAILED(hr)) { spdlog::error("WASAPI play: start failed"); return false; }

        opened_ = true;
        spdlog::info("WASAPI player opened: device={} rate={} ch={} buffer={}",
            device_name_, fmt.sample_rate, fmt.channels, buffer_frames_);
        return true;
    }

    void close() override {
        if (audio_client_) audio_client_->Stop();
        if (render_client_) { render_client_->Release(); render_client_ = nullptr; }
        if (audio_client_) { audio_client_->Release(); audio_client_ = nullptr; }
        if (enumerator_) { enumerator_->Release(); enumerator_ = nullptr; }
        if (play_event_) { CloseHandle(play_event_); play_event_ = nullptr; }
        opened_ = false;
    }

    size_t write(const int16_t* buffer, size_t frames) override {
        if (!opened_ || !render_client_) return 0;

        BYTE* data = nullptr;
        HRESULT hr = render_client_->GetBuffer(static_cast<UINT32>(frames), &data);
        if (FAILED(hr)) {
            // Buffer full; wait for event
            WaitForSingleObject(play_event_, 10);
            return 0;
        }

        size_t bytes = frames * format_.channels * sizeof(int16_t);
        if (muted_) {
            std::memset(data, 0, bytes);
        } else {
            // Apply volume
            if (volume_ < 0.999) {
                auto* out = reinterpret_cast<int16_t*>(data);
                for (size_t i = 0; i < frames * format_.channels; ++i) {
                    out[i] = static_cast<int16_t>(buffer[i] * volume_);
                }
            } else {
                std::memcpy(data, buffer, bytes);
            }
        }

        render_client_->ReleaseBuffer(static_cast<UINT32>(frames), 0);
        return frames;
    }

    bool is_open() const override { return opened_; }
    AudioFormat format() const override { return format_; }
    void set_volume(double vol) override { volume_ = std::clamp(vol, 0.0, 1.0); }
    double volume() const override { return volume_; }
    void set_mute(bool mute) override { muted_ = mute; }
    bool is_muted() const override { return muted_; }

private:
    IMMDeviceEnumerator* enumerator_ = nullptr;
    IAudioClient* audio_client_ = nullptr;
    IAudioRenderClient* render_client_ = nullptr;
    HANDLE play_event_ = nullptr;
    bool opened_ = false;
    AudioFormat format_;
    std::string device_name_;
    UINT32 buffer_frames_ = 0;
    double volume_ = 1.0;
    bool muted_ = false;
};

#endif // _WIN32

// =============================================================================
// Section 17: macOS CoreAudio playback
// =============================================================================

#ifdef __APPLE__

class CoreAudioPlayer : public AudioPlayer {
public:
    ~CoreAudioPlayer() override { close(); }

    bool open(const AudioDeviceInfo& device, const AudioFormat& fmt) override {
        close();
        format_ = fmt;
        device_name_ = device.name;

        AudioDeviceID device_id = 0;
        UInt32 prop_size = sizeof(device_id);

        if (device.id.empty()) {
            AudioObjectPropertyAddress addr = {
                kAudioHardwarePropertyDefaultOutputDevice,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain
            };
            OSStatus status = AudioObjectGetPropertyData(
                kAudioObjectSystemObject, &addr, 0, nullptr, &prop_size, &device_id);
            if (status != noErr) {
                spdlog::error("CoreAudio play: failed to get default output device");
                return false;
            }
        } else {
            device_id = static_cast<AudioDeviceID>(std::stoul(device.id));
        }

        AudioStreamBasicDescription desc = {};
        desc.mSampleRate = static_cast<Float64>(fmt.sample_rate);
        desc.mFormatID = kAudioFormatLinearPCM;
        desc.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
        desc.mBitsPerChannel = static_cast<UInt32>(fmt.bits_per_sample);
        desc.mChannelsPerFrame = static_cast<UInt32>(fmt.channels);
        desc.mFramesPerPacket = 1;
        desc.mBytesPerFrame = desc.mChannelsPerFrame * desc.mBitsPerChannel / 8;
        desc.mBytesPerPacket = desc.mBytesPerFrame;

        AudioComponentDescription comp_desc = {};
        comp_desc.componentType = kAudioUnitType_Output;
        comp_desc.componentSubType = kAudioUnitSubType_DefaultOutput;
        comp_desc.componentManufacturer = kAudioUnitManufacturer_Apple;

        AudioComponent component = AudioComponentFindNext(nullptr, &comp_desc);
        if (!component) return false;

        OSStatus status = AudioComponentInstanceNew(component, &audio_unit_);
        if (status != noErr) return false;

        status = AudioUnitSetProperty(audio_unit_,
            kAudioOutputUnitProperty_CurrentDevice,
            kAudioUnitScope_Global, 0, &device_id, sizeof(device_id));

        status = AudioUnitSetProperty(audio_unit_,
            kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Input, 0, &desc, sizeof(desc));

        AURenderCallbackStruct callback;
        callback.inputProc = &CoreAudioPlayer::play_callback;
        callback.inputProcRefCon = this;
        AudioUnitSetProperty(audio_unit_,
            kAudioUnitProperty_SetRenderCallback,
            kAudioUnitScope_Input, 0, &callback, sizeof(callback));

        status = AudioUnitInitialize(audio_unit_);
        if (status != noErr) return false;

        status = AudioOutputUnitStart(audio_unit_);
        if (status != noErr) return false;

        opened_ = true;
        play_buffer_.clear();
        spdlog::info("CoreAudio player opened: device={} rate={} ch={}",
            device_name_, fmt.sample_rate, fmt.channels);
        return true;
    }

    void close() override {
        if (audio_unit_) {
            AudioOutputUnitStop(audio_unit_);
            AudioUnitUninitialize(audio_unit_);
            AudioComponentInstanceDispose(audio_unit_);
            audio_unit_ = nullptr;
        }
        opened_ = false;
    }

    size_t write(const int16_t* buffer, size_t frames) override {
        if (!opened_) return 0;
        std::lock_guard lk(play_mutex_);

        size_t bytes = frames * format_.channels * sizeof(int16_t);
        if (muted_) {
            std::vector<uint8_t> silence(bytes, 0);
            play_buffer_.write(silence.data(), bytes);
        } else if (volume_ < 0.999) {
            std::vector<int16_t> adjusted(frames * format_.channels);
            for (size_t i = 0; i < frames * format_.channels; ++i) {
                adjusted[i] = static_cast<int16_t>(buffer[i] * volume_);
            }
            play_buffer_.write(reinterpret_cast<const uint8_t*>(adjusted.data()), bytes);
        } else {
            play_buffer_.write(reinterpret_cast<const uint8_t*>(buffer), bytes);
        }
        return frames;
    }

    bool is_open() const override { return opened_; }
    AudioFormat format() const override { return format_; }
    void set_volume(double vol) override { volume_ = std::clamp(vol, 0.0, 1.0); }
    double volume() const override { return volume_; }
    void set_mute(bool mute) override { muted_ = mute; }
    bool is_muted() const override { return muted_; }

private:
    static OSStatus play_callback(void* inRefCon,
        AudioUnitRenderActionFlags* ioActionFlags,
        const AudioTimeStamp* inTimeStamp,
        UInt32 inBusNumber,
        UInt32 inNumberFrames,
        AudioBufferList* ioData) {

        auto* self = static_cast<CoreAudioPlayer*>(inRefCon);
        if (!self || !ioData) return noErr;

        std::lock_guard lk(self->play_mutex_);
        size_t bytes_needed = inNumberFrames * self->format_.channels * sizeof(int16_t);

        for (UInt32 i = 0; i < ioData->mNumberBuffers; ++i) {
            size_t available = self->play_buffer_.available();
            if (available >= bytes_needed) {
                self->play_buffer_.read(
                    static_cast<uint8_t*>(ioData->mBuffers[i].mData), bytes_needed);
            } else {
                // Underrun: output silence
                std::memset(ioData->mBuffers[i].mData, 0, bytes_needed);
                self->underruns_++;
            }
            ioData->mBuffers[i].mDataByteSize = static_cast<UInt32>(bytes_needed);
        }

        *ioActionFlags = 0;
        return noErr;
    }

    AudioUnit audio_unit_ = nullptr;
    bool opened_ = false;
    AudioFormat format_;
    std::string device_name_;
    ByteRingBuffer play_buffer_{256 * 1024};
    std::mutex play_mutex_;
    double volume_ = 1.0;
    bool muted_ = false;
    size_t underruns_ = 0;
};

#endif // __APPLE__

// =============================================================================
// Section 18: Device enumeration
// =============================================================================

class AudioDeviceEnumerator {
public:
    static std::vector<AudioDeviceInfo> enumerate_inputs() {
        std::vector<AudioDeviceInfo> devices;

#ifdef __linux__
        // PulseAudio enumeration via pa_mainloop
        pa_mainloop* ml = pa_mainloop_new();
        pa_context* ctx = pa_context_new(pa_mainloop_get_api(ml), "cppdesk_enum");
        pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr);

        // Wait for context ready (simplified)
        int retry = 0;
        while (pa_context_get_state(ctx) != PA_CONTEXT_READY && retry < 50) {
            pa_mainloop_iterate(ml, 0, nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            retry++;
        }

        if (pa_context_get_state(ctx) == PA_CONTEXT_READY) {
            // Get sources (input devices)
            pa_operation* op = pa_context_get_source_info_list(ctx, source_list_cb, &devices);
            while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
                pa_mainloop_iterate(ml, 0, nullptr);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            pa_operation_unref(op);
        }

        pa_context_disconnect(ctx);
        pa_context_unref(ctx);
        pa_mainloop_free(ml);

#elif defined(_WIN32)
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IMMDeviceEnumerator* enumerator = nullptr;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
        if (SUCCEEDED(hr) && enumerator) {
            IMMDeviceCollection* collection = nullptr;
            hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);
            if (SUCCEEDED(hr) && collection) {
                UINT count = 0;
                collection->GetCount(&count);
                for (UINT i = 0; i < count; ++i) {
                    IMMDevice* dev = nullptr;
                    if (SUCCEEDED(collection->Item(i, &dev)) && dev) {
                        AudioDeviceInfo info;
                        info.is_input = true;

                        LPWSTR dev_id = nullptr;
                        dev->GetId(&dev_id);
                        if (dev_id) {
                            int len = WideCharToMultiByte(CP_UTF8, 0, dev_id, -1, nullptr, 0, nullptr, nullptr);
                            info.id.resize(len - 1);
                            WideCharToMultiByte(CP_UTF8, 0, dev_id, -1, &info.id[0], len, nullptr, nullptr);
                            CoTaskMemFree(dev_id);
                        }

                        IPropertyStore* props = nullptr;
                        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
                            PROPVARIANT var;
                            PropVariantInit(&var);
                            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var))) {
                                int len2 = WideCharToMultiByte(CP_UTF8, 0, var.pwszVal, -1, nullptr, 0, nullptr, nullptr);
                                info.name.resize(len2 - 1);
                                WideCharToMultiByte(CP_UTF8, 0, var.pwszVal, -1, &info.name[0], len2, nullptr, nullptr);
                                PropVariantClear(&var);
                            }
                            props->Release();
                        }
                        if (info.name.empty()) info.name = "Capture Device " + std::to_string(i);

                        dev->Release();
                        devices.push_back(info);
                    }
                }
                collection->Release();
            }
            enumerator->Release();
        }
        CoUninitialize();

#elif defined(__APPLE__)
        // CoreAudio: enumerate input devices
        UInt32 prop_size;
        AudioObjectPropertyAddress addr = {
            kAudioHardwarePropertyDevices,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &prop_size);
        UInt32 device_count = prop_size / sizeof(AudioDeviceID);
        std::vector<AudioDeviceID> device_ids(device_count);
        AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &prop_size, device_ids.data());

        for (auto dev_id : device_ids) {
            // Check if device has input channels
            AudioObjectPropertyAddress input_addr = {
                kAudioDevicePropertyStreamConfiguration,
                kAudioDevicePropertyScopeInput,
                kAudioObjectPropertyElementMain
            };
            AudioObjectGetPropertyDataSize(dev_id, &input_addr, 0, nullptr, &prop_size);
            if (prop_size > 0) {
                auto* buf_list = reinterpret_cast<AudioBufferList*>(malloc(prop_size));
                if (AudioObjectGetPropertyData(dev_id, &input_addr, 0, nullptr, &prop_size, buf_list) == noErr) {
                    UInt32 total_channels = 0;
                    for (UInt32 i = 0; i < buf_list->mNumberBuffers; ++i) {
                        total_channels += buf_list->mBuffers[i].mNumberChannels;
                    }
                    if (total_channels > 0) {
                        AudioDeviceInfo info;
                        info.id = std::to_string(dev_id);
                        info.is_input = true;
                        info.max_channels = total_channels;

                        // Get name
                        AudioObjectPropertyAddress name_addr = {
                            kAudioObjectPropertyName,
                            kAudioDevicePropertyScopeInput,
                            kAudioObjectPropertyElementMain
                        };
                        CFStringRef name_ref = nullptr;
                        UInt32 name_size = sizeof(name_ref);
                        if (AudioObjectGetPropertyData(dev_id, &name_addr, 0, nullptr, &name_size, &name_ref) == noErr) {
                            char name_buf[256];
                            CFStringGetCString(name_ref, name_buf, sizeof(name_buf), kCFStringEncodingUTF8);
                            info.name = name_buf;
                            CFRelease(name_ref);
                        }
                        if (info.name.empty()) info.name = "Input Device " + std::to_string(dev_id);

                        devices.push_back(info);
                    }
                }
                free(buf_list);
            }
        }
#endif

        // Always add a default device
        if (devices.empty()) {
            AudioDeviceInfo def;
            def.id = "default";
            def.name = "Default Capture Device";
            def.is_input = true;
            def.is_default = true;
            devices.push_back(def);
        }

        return devices;
    }

    static std::vector<AudioDeviceInfo> enumerate_outputs() {
        std::vector<AudioDeviceInfo> devices;
        // Similar to enumerate_inputs but for output devices
        // Simplified: return at least a default
        AudioDeviceInfo def;
        def.id = "default";
        def.name = "Default Playback Device";
        def.is_output = true;
        def.is_default = true;
        devices.push_back(def);
        return devices;
    }

private:
#ifdef __linux__
    static void source_list_cb(pa_context*, const pa_source_info* info, int eol, void* userdata) {
        if (eol || !info) return;
        auto* devices = static_cast<std::vector<AudioDeviceInfo>*>(userdata);
        AudioDeviceInfo dev;
        dev.id = info->name;
        dev.name = info->description ? info->description : info->name;
        dev.is_input = true;
        devices->push_back(dev);
    }
#endif
};

// =============================================================================
// Section 19: Volume control manager
// =============================================================================

class VolumeManager {
public:
    void set_system_volume(double vol) {
        vol = std::clamp(vol, 0.0, 1.0);
        system_volume_ = vol;
        spdlog::debug("System volume set to {:.0f}%", vol * 100.0);
    }

    void set_stream_volume(int32_t stream_id, double vol) {
        vol = std::clamp(vol, 0.0, 1.0);
        std::lock_guard lk(mutex_);
        stream_volumes_[stream_id] = vol;
    }

    double get_stream_volume(int32_t stream_id) const {
        std::lock_guard lk(mutex_);
        auto it = stream_volumes_.find(stream_id);
        return it != stream_volumes_.end() ? it->second : 1.0;
    }

    void set_mute(bool mute) {
        muted_ = mute;
        spdlog::debug("System mute: {}", mute ? "on" : "off");
    }

    void set_stream_mute(int32_t stream_id, bool mute) {
        std::lock_guard lk(mutex_);
        stream_mutes_[stream_id] = mute;
    }

    bool is_muted() const { return muted_.load(); }

    bool is_stream_muted(int32_t stream_id) const {
        std::lock_guard lk(mutex_);
        auto it = stream_mutes_.find(stream_id);
        return it != stream_mutes_.end() ? it->second : false;
    }

    double system_volume() const { return system_volume_.load(); }

    void remove_stream(int32_t stream_id) {
        std::lock_guard lk(mutex_);
        stream_volumes_.erase(stream_id);
        stream_mutes_.erase(stream_id);
    }

private:
    std::atomic<double> system_volume_{1.0};
    std::atomic<bool> muted_{false};
    std::map<int32_t, double> stream_volumes_;
    std::map<int32_t, bool> stream_mutes_;
    mutable std::mutex mutex_;
};

// =============================================================================
// Section 20: Audio stream statistics tracker
// =============================================================================

class AudioStatsTracker {
public:
    struct Snapshot {
        // Capture
        uint64_t capture_frames = 0;
        uint64_t capture_bytes = 0;
        uint64_t silence_frames_dropped = 0;
        uint64_t frames_encoded = 0;
        uint64_t bytes_encoded = 0;
        uint64_t opus_silence_frames = 0;
        double capture_volume_peak = 0.0;
        size_t capture_buffer_level = 0;

        // Playback
        uint64_t playout_frames = 0;
        uint64_t playout_bytes = 0;
        uint64_t underruns = 0;
        size_t playout_buffer_level = 0;
        size_t jitter_buffer_depth = 0;
        size_t jitter_target_delay_ms = 0;
        uint64_t jitter_dropped = 0;
        uint64_t jitter_skipped = 0;

        // Network
        uint64_t packets_sent = 0;
        uint64_t packets_received = 0;
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
        uint64_t packets_lost = 0;      // estimated from sequence gaps
        uint64_t fec_recoveries = 0;

        // Timing
        double avg_capture_latency_ms = 0.0;
        double avg_encode_time_us = 0.0;
        double avg_decode_time_us = 0.0;
        double avg_playout_latency_ms = 0.0;
    };

    // Capture-side counters
    void record_capture_frame(size_t bytes, bool was_silence) {
        capture_frames_++;
        capture_bytes_ += bytes;
        if (was_silence) silence_dropped_++;
    }

    void record_encoded_frame(size_t bytes, bool opus_silence, double encode_us) {
        frames_encoded_++;
        bytes_encoded_ += bytes;
        if (opus_silence) opus_silences_++;
        encode_time_sum_ += encode_us;
        encode_time_count_++;
    }

    void record_capture_peak(double peak) {
        auto prev = peak_max_.load();
        while (peak > prev && !peak_max_.compare_exchange_weak(prev, peak)) {}
    }

    void set_capture_buffer_level(size_t lvl) { capture_buf_level_.store(lvl); }

    // Playback-side counters
    void record_playout_frame(size_t bytes) {
        playout_frames_++;
        playout_bytes_ += bytes;
    }

    void record_underrun() { underruns_++; }

    void set_playout_buffer_level(size_t lvl) { playout_buf_level_.store(lvl); }

    // Jitter buffer
    void set_jitter_stats(size_t depth, size_t target_ms, uint64_t dropped, uint64_t skipped) {
        jitter_depth_.store(depth);
        jitter_target_ms_.store(target_ms);
        jitter_dropped_.store(dropped);
        jitter_skipped_.store(skipped);
    }

    // Network
    void record_packet_sent(size_t bytes) { packets_sent_++; bytes_sent_ += bytes; }
    void record_packet_received(size_t bytes) { packets_recv_++; bytes_recv_ += bytes; }
    void record_packet_lost() { packets_lost_++; }
    void record_fec_recovery() { fec_recoveries_++; }

    // Latency
    void record_playout_latency(double ms) {
        playout_lat_sum_ += ms;
        playout_lat_count_++;
    }

    // Build snapshot (resets peak, averages)
    Snapshot snapshot() {
        Snapshot s;
        s.capture_frames = capture_frames_.load();
        s.capture_bytes = capture_bytes_.load();
        s.silence_frames_dropped = silence_dropped_.load();
        s.frames_encoded = frames_encoded_.load();
        s.bytes_encoded = bytes_encoded_.load();
        s.opus_silence_frames = opus_silences_.load();
        s.capture_volume_peak = peak_max_.exchange(0.0);
        s.capture_buffer_level = capture_buf_level_.load();

        s.playout_frames = playout_frames_.load();
        s.playout_bytes = playout_bytes_.load();
        s.underruns = underruns_.load();
        s.playout_buffer_level = playout_buf_level_.load();
        s.jitter_buffer_depth = jitter_depth_.load();
        s.jitter_target_delay_ms = jitter_target_ms_.load();
        s.jitter_dropped = jitter_dropped_.load();
        s.jitter_skipped = jitter_skipped_.load();

        s.packets_sent = packets_sent_.load();
        s.packets_received = packets_recv_.load();
        s.bytes_sent = bytes_sent_.load();
        s.bytes_received = bytes_recv_.load();
        s.packets_lost = packets_lost_.load();
        s.fec_recoveries = fec_recoveries_.load();

        auto ec = encode_time_count_.load();
        s.avg_encode_time_us = ec > 0 ? encode_time_sum_.load() / ec : 0.0;
        auto dc = playout_lat_count_.load();
        s.avg_playout_latency_ms = dc > 0 ? playout_lat_sum_.load() / dc : 0.0;

        return s;
    }

private:
    std::atomic<uint64_t> capture_frames_{0};
    std::atomic<uint64_t> capture_bytes_{0};
    std::atomic<uint64_t> silence_dropped_{0};
    std::atomic<uint64_t> frames_encoded_{0};
    std::atomic<uint64_t> bytes_encoded_{0};
    std::atomic<uint64_t> opus_silences_{0};
    std::atomic<double> peak_max_{0.0};
    std::atomic<size_t> capture_buf_level_{0};

    std::atomic<uint64_t> playout_frames_{0};
    std::atomic<uint64_t> playout_bytes_{0};
    std::atomic<uint64_t> underruns_{0};
    std::atomic<size_t> playout_buf_level_{0};
    std::atomic<size_t> jitter_depth_{0};
    std::atomic<size_t> jitter_target_ms_{0};
    std::atomic<uint64_t> jitter_dropped_{0};
    std::atomic<uint64_t> jitter_skipped_{0};

    std::atomic<uint64_t> packets_sent_{0};
    std::atomic<uint64_t> packets_recv_{0};
    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<uint64_t> bytes_recv_{0};
    std::atomic<uint64_t> packets_lost_{0};
    std::atomic<uint64_t> fec_recoveries_{0};

    std::atomic<double> encode_time_sum_{0.0};
    std::atomic<uint64_t> encode_time_count_{0};
    std::atomic<double> playout_lat_sum_{0.0};
    std::atomic<uint64_t> playout_lat_count_{0};
};

// =============================================================================
// Section 21: AudioServiceImpl — the core audio engine
// =============================================================================

class AudioServiceImpl {
public:
    AudioServiceImpl() {
        spdlog::info("AudioServiceImpl created");
    }

    ~AudioServiceImpl() { stop(); }

    // ===== Lifecycle =====

    bool start() {
        if (running_) return true;

        spdlog::info("Audio service starting...");

        // Default format
        format_ = AudioFormat{48000, 2, 16, 20};

        // Initialize encoder
        if (!encoder_.init()) {
            spdlog::error("Failed to init Opus encoder");
            return false;
        }

        // Initialize decoder
        if (!decoder_.init()) {
            spdlog::error("Failed to init Opus decoder");
            return false;
        }

        // Open capturer
        auto input_devices = AudioDeviceEnumerator::enumerate_inputs();
        if (input_devices.empty()) {
            spdlog::error("No audio input devices found");
            return false;
        }

        input_device_ = input_devices[0]; // default input

#if defined(__linux__)
        capturer_ = std::make_unique<PulseAudioCapturer>();
#elif defined(_WIN32)
        capturer_ = std::make_unique<WasapiCapturer>();
#elif defined(__APPLE__)
        capturer_ = std::make_unique<CoreAudioCapturer>();
#else
        spdlog::error("No audio capturer available for this platform");
        return false;
#endif

        if (!capturer_->open(input_device_, format_)) {
            spdlog::error("Failed to open audio capturer");
            return false;
        }

        // Open player
        auto output_devices = AudioDeviceEnumerator::enumerate_outputs();
        if (!output_devices.empty()) {
            output_device_ = output_devices[0];

#if defined(__linux__)
            player_ = std::make_unique<PulseAudioPlayer>();
#elif defined(_WIN32)
            player_ = std::make_unique<WasapiPlayer>();
#elif defined(__APPLE__)
            player_ = std::make_unique<CoreAudioPlayer>();
#endif

            if (player_) {
                if (!player_->open(output_device_, format_)) {
                    spdlog::warn("Failed to open audio player — playback disabled");
                    player_.reset();
                }
            }
        }

        // Prep jitter buffer
        JitterBuffer::Config jb_cfg;
        jb_cfg.sample_rate = format_.sample_rate;
        jb_cfg.channels = format_.channels;
        jb_cfg.frame_duration_ms = format_.frame_duration_ms;
        jitter_buffer_ = JitterBuffer(jb_cfg);

        // Start worker threads
        running_ = true;
        capture_thread_ = std::thread(&AudioServiceImpl::capture_loop, this);
        playback_thread_ = std::thread(&AudioServiceImpl::playback_loop, this);

        spdlog::info("Audio service started: {}hz {}ch {}ms frames",
            format_.sample_rate, format_.channels, format_.frame_duration_ms);
        return true;
    }

    void stop() {
        if (!running_) return;

        spdlog::info("Audio service stopping...");
        running_ = false;

        capture_cv_.notify_all();
        playback_cv_.notify_all();

        if (capture_thread_.joinable()) capture_thread_.join();
        if (playback_thread_.joinable()) playback_thread_.join();

        if (capturer_) { capturer_->close(); capturer_.reset(); }
        if (player_) { player_->close(); player_.reset(); }

        spdlog::info("Audio service stopped");
    }

    bool is_running() const { return running_.load(); }

    // ===== Device management =====

    std::vector<AudioDeviceInfo> get_input_devices() {
        return AudioDeviceEnumerator::enumerate_inputs();
    }

    std::vector<AudioDeviceInfo> get_output_devices() {
        return AudioDeviceEnumerator::enumerate_outputs();
    }

    bool select_input_device(const std::string& device_id) {
        auto devices = AudioDeviceEnumerator::enumerate_inputs();
        for (auto& d : devices) {
            if (d.id == device_id) {
                input_device_ = d;
                spdlog::info("Selected input device: {}", d.name);
                // Reopen capturer if running
                if (running_) {
                    capturer_->close();
                    capturer_->open(input_device_, format_);
                }
                return true;
            }
        }
        return false;
    }

    bool select_output_device(const std::string& device_id) {
        auto devices = AudioDeviceEnumerator::enumerate_outputs();
        for (auto& d : devices) {
            if (d.id == device_id) {
                output_device_ = d;
                spdlog::info("Selected output device: {}", d.name);
                if (running_ && player_) {
                    player_->close();
                    player_->open(output_device_, format_);
                }
                return true;
            }
        }
        return false;
    }

    // ===== Format negotiation =====

    std::vector<AudioFormat> get_supported_formats() const {
        return AudioFormat::default_formats();
    }

    bool negotiate_format(const std::vector<AudioFormat>& client_formats) {
        auto server_formats = get_supported_formats();

        // Find best common format
        for (auto& sf : server_formats) {
            for (auto& cf : client_formats) {
                if (sf.sample_rate == cf.sample_rate &&
                    sf.channels == cf.channels &&
                    sf.bits_per_sample == cf.bits_per_sample) {
                    format_ = sf;
                    spdlog::info("Format negotiated: {}hz {}ch {}bit",
                        format_.sample_rate, format_.channels, format_.bits_per_sample);
                    return true;
                }
            }
        }

        // Fallback: use best server format
        format_ = AudioFormat::negotiate(server_formats);
        spdlog::warn("No common format; using {}", format_.sample_rate);
        return true;
    }

    AudioFormat current_format() const { return format_; }

    // ===== Options =====

    void set_opus_bitrate(uint32_t bps) {
        encoder_.set_bitrate(std::clamp(bps, 6000u, 510000u));
    }

    void set_opus_complexity(uint32_t c) {
        encoder_.set_complexity(std::min(c, 10u));
    }

    void set_silence_threshold(double db) {
        SilenceDetector::Config cfg;
        cfg.silence_threshold_db = db;
        cfg.speech_threshold_db = db + 10.0;
        silence_detector_ = SilenceDetector(cfg);
    }

    void set_echo_cancellation(bool on) {
        EchoCanceller::Config cfg;
        cfg.enabled = on;
        echo_canceller_ = EchoCanceller(cfg);
    }

    void set_noise_suppression(bool on) {
        NoiseSuppressor::Config cfg;
        cfg.enabled = on;
        noise_suppressor_ = NoiseSuppressor(cfg);
    }

    void set_volume(double vol) { volume_mgr_.set_system_volume(vol); }
    double volume() const { return volume_mgr_.system_volume(); }
    void set_mute(bool m) { volume_mgr_.set_mute(m); }
    bool is_muted() const { return volume_mgr_.is_muted(); }

    // ===== Data I/O (for network layer) =====

    // Get encoded audio ready for sending (non-blocking)
    // Returns false if no data ready
    bool get_encoded_frame(std::vector<uint8_t>& out_data, uint64_t& out_timestamp, bool& is_silence) {
        AudioPacket pkt;
        if (!encode_ring_.pop(pkt)) return false;
        out_data = std::move(pkt.data);
        out_timestamp = pkt.timestamp;
        is_silence = pkt.is_silence;
        return true;
    }

    // Push received encoded audio for playback
    void push_encoded_frame(const uint8_t* data, size_t len, uint64_t timestamp) {
        // Decode immediately and queue for jitter buffer
        bool fec = false;
        auto start = std::chrono::high_resolution_clock::now();
        auto pcm = decoder_.decode(data, len, fec);
        auto end = std::chrono::high_resolution_clock::now();
        double decode_us = std::chrono::duration<double, std::micro>(end - start).count();

        stats_.record_packet_received(len);
        if (fec) stats_.record_fec_recovery();

        if (!pcm.empty()) {
            jitter_buffer_.push(timestamp, pcm);
            stats_.record_playout_latency(decode_us / 1000.0);
        } else {
            stats_.record_packet_lost();
        }
    }

    // ===== Statistics =====

    AudioStatsTracker::Snapshot get_stats() {
        // Update jitter stats
        auto jb_stats = jitter_buffer_.stats();
        stats_.set_jitter_stats(jb_stats.queue_depth, jb_stats.target_delay_ms,
                                 jb_stats.dropped, jb_stats.skipped);
        return stats_.snapshot();
    }

    // ===== Direct audio injection (for testing / virtual sources) =====

    void inject_capture_pcm(const int16_t* pcm, size_t samples) {
        if (!running_) return;

        pcm_ring_.write(reinterpret_cast<const uint8_t*>(pcm),
                        samples * format_.channels * sizeof(int16_t));
    }

private:
    struct AudioPacket {
        std::vector<uint8_t> data;
        uint64_t timestamp;
        bool is_silence;
    };

    // ===== Capture loop =====

    void capture_loop() {
        spdlog::info("Audio capture loop started");
        size_t frame_samples = format_.samples_per_frame();
        size_t frame_bytes = frame_samples * format_.channels * sizeof(int16_t);
        std::vector<int16_t> frame_buffer(frame_samples * format_.channels);

        uint64_t frame_counter = 0;
        auto last_stats = std::chrono::steady_clock::now();

        while (running_) {
            // Read from capturer
            size_t frames_read = capturer_->read(frame_buffer.data(), frame_samples);

            if (frames_read == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            size_t actual_samples = frames_read * format_.channels;
            frame_counter++;

            // Check for silence
            bool silent = silence_detector_.is_silence(frame_buffer.data(), actual_samples);

            // Compute peak
            double peak = 0.0;
            for (size_t i = 0; i < actual_samples; ++i) {
                peak = std::max(peak, std::abs(static_cast<double>(frame_buffer[i]) / 32768.0));
            }
            stats_.record_capture_peak(peak);

            if (!silent) {
                // Apply echo cancellation if far-end audio is available
                if (echo_canceller_enabled_) {
                    std::lock_guard lk(far_end_mutex_);
                    if (!far_end_ring_.empty()) {
                        std::vector<int16_t> far_buf(frame_samples * format_.channels);
                        size_t far_avail = far_end_ring_.available() / sizeof(int16_t);
                        size_t far_read = std::min(far_avail, far_buf.size());
                        far_end_ring_.read(reinterpret_cast<uint8_t*>(far_buf.data()),
                                           far_read * sizeof(int16_t));

                        auto cleaned = echo_canceller_.cancel(
                            far_buf.data(), far_read,
                            frame_buffer.data(), actual_samples);
                        if (!cleaned.empty()) {
                            frame_buffer.assign(cleaned.begin(),
                                cleaned.begin() + std::min(cleaned.size(), frame_buffer.size()));
                        }
                    }
                }

                // Apply noise suppression (on first channel as proxy)
                if (noise_suppressor_enabled_) {
                    std::vector<float> float_samples(actual_samples);
                    for (size_t i = 0; i < actual_samples; ++i) {
                        float_samples[i] = frame_buffer[i] / 32768.0f;
                    }
                    auto cleaned = noise_suppressor_.process(float_samples.data(), float_samples.size());
                    for (size_t i = 0; i < actual_samples && i < cleaned.size(); ++i) {
                        frame_buffer[i] = static_cast<int16_t>(std::clamp(cleaned[i] * 32768.0f, -32768.0f, 32767.0f));
                    }
                }
            }

            // Encode with Opus
            auto encode_start = std::chrono::high_resolution_clock::now();
            bool opus_silence = false;
            auto encoded = encoder_.encode(frame_buffer.data(), actual_samples / format_.channels, opus_silence);
            auto encode_end = std::chrono::high_resolution_clock::now();
            double encode_us = std::chrono::duration<double, std::micro>(encode_end - encode_start).count();

            // Only send non-silent frames or periodic silence for keepalive
            if (!silent || (frame_counter % 50 == 0)) {
                uint64_t ts = frame_counter * format_.frame_duration_ms * format_.sample_rate / 1000;
                AudioPacket pkt{std::move(encoded), ts, silent};
                encode_ring_.push(std::move(pkt));
                stats_.record_encoded_frame(encoded.size(), opus_silence, encode_us);
            }

            stats_.record_capture_frame(frame_bytes, silent);
            stats_.set_capture_buffer_level(encode_ring_.size());
        }
        spdlog::info("Audio capture loop stopped");
    }

    // ===== Playback loop =====

    void playback_loop() {
        spdlog::info("Audio playback loop started");
        size_t frame_samples = format_.samples_per_frame();
        std::vector<int16_t> out_buffer(frame_samples * format_.channels);

        uint64_t playout_ts = 0;

        while (running_) {
            if (!player_ || !player_->is_open()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            // Get next frame from jitter buffer
            auto pkt = jitter_buffer_.pop(playout_ts);

            if (pkt) {
                // Write to far-end ring for echo cancellation
                {
                    std::lock_guard lk(far_end_mutex_);
                    far_end_ring_.write(
                        reinterpret_cast<const uint8_t*>(pkt->samples.data()),
                        pkt->samples.size() * sizeof(int16_t));
                }

                // Fill output buffer
                size_t to_copy = std::min(pkt->samples.size(), out_buffer.size());
                std::copy(pkt->samples.begin(), pkt->samples.begin() + to_copy, out_buffer.begin());
                if (to_copy < out_buffer.size()) {
                    std::fill(out_buffer.begin() + to_copy, out_buffer.end(), 0);
                }

                // Play through system
                size_t written = player_->write(out_buffer.data(), frame_samples);
                if (written > 0) {
                    stats_.record_playout_frame(written * format_.channels * sizeof(int16_t));
                }

                playout_ts += frame_samples * format_.frame_duration_ms * format_.sample_rate / 1000;
            } else {
                // No data: underrun — play silence
                std::fill(out_buffer.begin(), out_buffer.end(), 0);
                player_->write(out_buffer.data(), frame_samples);
                stats_.record_underrun();
                playout_ts += frame_samples * format_.frame_duration_ms * format_.sample_rate / 1000;
            }

            stats_.set_playout_buffer_level(jitter_buffer_.size());

            // Pace to frame rate
            std::this_thread::sleep_for(std::chrono::milliseconds(format_.frame_duration_ms / 2));
        }
        spdlog::info("Audio playback loop stopped");
    }

    // ===== Members =====

    std::atomic<bool> running_{false};

    // Format
    AudioFormat format_{48000, 2, 16, 20};

    // Devices
    AudioDeviceInfo input_device_;
    AudioDeviceInfo output_device_;

    // Platform-specific I/O
    std::unique_ptr<AudioCapturer> capturer_;
    std::unique_ptr<AudioPlayer> player_;

    // Opus codec wrappers
    OpusEncoder encoder_{};
    OpusDecoder decoder_{};

    // Audio processing
    SilenceDetector silence_detector_;
    EchoCanceller echo_canceller_;
    NoiseSuppressor noise_suppressor_;
    bool echo_canceller_enabled_ = false;
    bool noise_suppressor_enabled_ = false;

    // Volume
    VolumeManager volume_mgr_;

    // Ring buffers
    RingBuffer<AudioPacket> encode_ring_{64};  // encoded frames waiting for network send
    ByteRingBuffer pcm_ring_{1024 * 1024};     // raw PCM from virtual inject

    // Far-end ring buffer for echo cancellation reference
    ByteRingBuffer far_end_ring_{256 * 1024};
    std::mutex far_end_mutex_;

    // Jitter buffer for incoming audio
    JitterBuffer jitter_buffer_;

    // Threads
    std::thread capture_thread_;
    std::thread playback_thread_;
    std::condition_variable capture_cv_;
    std::condition_variable playback_cv_;

    // Statistics
    AudioStatsTracker stats_;
};

// =============================================================================
// Section 22: AudioService — the GenericService subclass exposed to the server
// =============================================================================

AudioService::AudioService() : GenericService(NAME) {
    impl_ = std::make_unique<AudioServiceImpl>();
}

AudioService::~AudioService() {
    stop();
}

void AudioService::start() {
    if (impl_->is_running()) return;

    if (!impl_->start()) {
        spdlog::error("AudioService: failed to start audio engine");
        return;
    }

    spdlog::info("AudioService started (subscribers: {})", subscriber_count());
}

void AudioService::stop() {
    if (!impl_->is_running()) return;
    impl_->stop();
    spdlog::info("AudioService stopped");
}

void AudioService::set_option(const std::string& key, const std::string& value) {
    if (key == "bitrate") {
        impl_->set_opus_bitrate(static_cast<uint32_t>(std::stoul(value)));
    } else if (key == "complexity") {
        impl_->set_opus_complexity(static_cast<uint32_t>(std::stoul(value)));
    } else if (key == "silence_threshold") {
        impl_->set_silence_threshold(std::stod(value));
    } else if (key == "echo_cancellation") {
        impl_->set_echo_cancellation(value == "true" || value == "1");
    } else if (key == "noise_suppression") {
        impl_->set_noise_suppression(value == "true" || value == "1");
    } else if (key == "volume") {
        impl_->set_volume(std::stod(value));
    } else if (key == "mute") {
        impl_->set_mute(value == "true" || value == "1");
    } else if (key == "input_device") {
        impl_->select_input_device(value);
    } else if (key == "output_device") {
        impl_->select_output_device(value);
    } else if (key == "format") {
        // Format string: "48000:2:16:20"
        // Parse and negotiate
        std::vector<AudioFormat> formats;
        AudioFormat fmt;
        if (sscanf(value.c_str(), "%u:%u:%u:%u",
               &fmt.sample_rate, &fmt.channels, &fmt.bits_per_sample, &fmt.frame_duration_ms) == 4) {
            formats.push_back(fmt);
            impl_->negotiate_format(formats);
        }
    } else {
        spdlog::warn("AudioService: unknown option '{}' = '{}'", key, value);
    }
}

// Public methods matching the internal AudioServiceImpl
std::vector<AudioDeviceInfo> AudioService::get_input_devices() {
    return impl_->get_input_devices();
}

std::vector<AudioDeviceInfo> AudioService::get_output_devices() {
    return impl_->get_output_devices();
}

bool AudioService::select_input_device(const std::string& id) {
    return impl_->select_input_device(id);
}

bool AudioService::select_output_device(const std::string& id) {
    return impl_->select_output_device(id);
}

bool AudioService::negotiate_format(const std::vector<AudioFormat>& client_formats) {
    return impl_->negotiate_format(client_formats);
}

AudioFormat AudioService::current_format() const {
    return impl_->current_format();
}

bool AudioService::get_encoded_frame(std::vector<uint8_t>& out, uint64_t& ts, bool& sil) {
    return impl_->get_encoded_frame(out, ts, sil);
}

void AudioService::push_encoded_frame(const uint8_t* data, size_t len, uint64_t ts) {
    impl_->push_encoded_frame(data, len, ts);
}

AudioStatsTracker::Snapshot AudioService::get_stats() {
    return impl_->get_stats();
}

// =============================================================================
// Section 23: AudioMixer — mix multiple audio streams for output
// =============================================================================

class AudioMixer {
public:
    struct MixConfig {
        uint32_t channels = 2;
        size_t max_streams = 16;
        double master_gain = 1.0;
    };

    explicit AudioMixer(const MixConfig& cfg = MixConfig{}) : config_(cfg) {}

    // Mix multiple PCM16 interleaved frames into one output frame
    // Input: vector of (samples, gain). All must have same channel count and frame size.
    // Returns mixed output or empty if no inputs.
    std::vector<int16_t> mix(const std::vector<std::pair<const int16_t*, double>>& streams,
                              size_t frames_per_channel) {
        if (streams.empty() || frames_per_channel == 0) return {};

        size_t total_samples = frames_per_channel * config_.channels;
        std::vector<int32_t> accum(total_samples, 0);
        size_t count = 0;

        for (auto& [samples, gain] : streams) {
            if (!samples) continue;
            double effective_gain = std::clamp(gain, 0.0, 2.0);
            for (size_t i = 0; i < total_samples; ++i) {
                accum[i] += static_cast<int32_t>(samples[i] * effective_gain);
            }
            count++;
        }

        if (count == 0) return {};

        // Apply master gain and clamp to int16 range
        std::vector<int16_t> out(total_samples);
        double mg = std::clamp(config_.master_gain, 0.0, 1.0);
        for (size_t i = 0; i < total_samples; ++i) {
            int32_t val = static_cast<int32_t>(accum[i] * mg);
            out[i] = static_cast<int16_t>(std::clamp(val, -32768, 32767));
        }

        return out;
    }

    void set_master_gain(double g) { config_.master_gain = std::clamp(g, 0.0, 1.0); }
    double master_gain() const { return config_.master_gain; }

private:
    MixConfig config_;
};

// =============================================================================
// Section 24: AudioSession — per-connection audio state
// =============================================================================

class AudioSession {
public:
    explicit AudioSession(int32_t conn_id) : conn_id_(conn_id) {}

    int32_t conn_id() const { return conn_id_; }

    // Client's negotiated format
    void set_format(const AudioFormat& fmt) { format_ = fmt; }
    AudioFormat format() const { return format_; }

    // Per-stream volume/mute
    void set_volume(double v) { volume_ = std::clamp(v, 0.0, 1.0); }
    double volume() const { return volume_; }
    void set_mute(bool m) { muted_ = m; }
    bool is_muted() const { return muted_; }

    // Session stats
    uint64_t packets_sent_to_client() const { return sent_count_.load(); }
    uint64_t packets_received_from_client() const { return recv_count_.load(); }
    uint64_t bytes_sent_to_client() const { return sent_bytes_.load(); }
    uint64_t bytes_received_from_client() const { return recv_bytes_.load(); }

    void record_sent(size_t bytes) { sent_count_++; sent_bytes_ += bytes; }
    void record_received(size_t bytes) { recv_count_++; recv_bytes_ += bytes; }

    // Activity tracking
    auto last_activity() const { return last_activity_; }
    void touch() { last_activity_ = std::chrono::steady_clock::now(); }
    bool is_idle(std::chrono::seconds timeout = std::chrono::seconds(30)) const {
        return std::chrono::steady_clock::now() - last_activity_ > timeout;
    }

    // Buffer for outgoing audio to this client
    ByteRingBuffer& outbound_buffer() { return outbound_buf_; }

private:
    int32_t conn_id_;
    AudioFormat format_{48000, 2, 16, 20};
    double volume_ = 1.0;
    bool muted_ = false;

    std::atomic<uint64_t> sent_count_{0};
    std::atomic<uint64_t> recv_count_{0};
    std::atomic<uint64_t> sent_bytes_{0};
    std::atomic<uint64_t> recv_bytes_{0};

    std::chrono::steady_clock::time_point last_activity_{std::chrono::steady_clock::now()};
    ByteRingBuffer outbound_buf_{128 * 1024}; // 128KB per session
};

// =============================================================================
// Section 25: AudioSessionManager — tracks per-connection audio sessions
// =============================================================================

class AudioSessionManager {
public:
    AudioSession* get_or_create(int32_t conn_id) {
        std::lock_guard lk(mutex_);
        auto it = sessions_.find(conn_id);
        if (it != sessions_.end()) {
            it->second->touch();
            return it->second.get();
        }
        auto session = std::make_unique<AudioSession>(conn_id);
        auto* ptr = session.get();
        sessions_[conn_id] = std::move(session);
        spdlog::debug("Audio session created for conn {}", conn_id);
        return ptr;
    }

    AudioSession* get(int32_t conn_id) {
        std::lock_guard lk(mutex_);
        auto it = sessions_.find(conn_id);
        return it != sessions_.end() ? it->second.get() : nullptr;
    }

    void remove(int32_t conn_id) {
        std::lock_guard lk(mutex_);
        sessions_.erase(conn_id);
        spdlog::debug("Audio session removed for conn {}", conn_id);
    }

    void cleanup_idle(std::chrono::seconds timeout = std::chrono::seconds(60)) {
        std::lock_guard lk(mutex_);
        for (auto it = sessions_.begin(); it != sessions_.end(); ) {
            if (it->second->is_idle(timeout)) {
                spdlog::debug("Removing idle audio session conn {}", it->first);
                it = sessions_.erase(it);
            } else {
                ++it;
            }
        }
    }

    size_t count() const {
        std::lock_guard lk(mutex_);
        return sessions_.size();
    }

    std::vector<int32_t> active_ids() const {
        std::lock_guard lk(mutex_);
        std::vector<int32_t> ids;
        for (auto& [id, _] : sessions_) ids.push_back(id);
        return ids;
    }

private:
    std::map<int32_t, std::unique_ptr<AudioSession>> sessions_;
    mutable std::mutex mutex_;
};

// =============================================================================
// Section 26: Audio frame dispatcher — sends encoded frames to subscribers
// =============================================================================

class AudioFrameDispatcher {
public:
    using SendCallback = std::function<void(int32_t conn_id, const std::vector<uint8_t>& data,
                                             uint64_t timestamp, bool is_silence)>;

    explicit AudioFrameDispatcher(SendCallback cb) : send_cb_(std::move(cb)) {}

    // Dispatch one encoded frame to all subscribers
    void dispatch(const std::vector<uint8_t>& encoded, uint64_t timestamp,
                  bool is_silence, const std::vector<int32_t>& subscriber_ids) {
        if (is_silence && silence_skip_counter_++ < max_skip_silence_) {
            return; // Skip silence frames most of the time
        }
        silence_skip_counter_ = 0;

        for (int32_t conn_id : subscriber_ids) {
            if (send_cb_) {
                send_cb_(conn_id, encoded, timestamp, is_silence);
            }
            dispatched_frames_++;
            dispatched_bytes_ += encoded.size();
        }
    }

    void dispatch_silence_keepalive(const std::vector<int32_t>& subscriber_ids,
                                     uint64_t timestamp, size_t frame_ms) {
        // Send a minimal keepalive packet so the client knows the stream is alive
        if (subscriber_ids.empty()) return;

        // Minimal Opus silence frame (or just a tiny marker)
        std::vector<uint8_t> keepalive = {0xFC}; // Opus silent frame indicator

        for (int32_t conn_id : subscriber_ids) {
            if (send_cb_) {
                send_cb_(conn_id, keepalive, timestamp, true);
            }
        }
        keepalive_sent_++;
    }

    uint64_t dispatched_frames() const { return dispatched_frames_.load(); }
    uint64_t dispatched_bytes() const { return dispatched_bytes_.load(); }
    uint64_t keepalive_sent() const { return keepalive_sent_.load(); }

private:
    SendCallback send_cb_;
    size_t max_skip_silence_ = 30; // keep 1 in 30 silence frames
    size_t silence_skip_counter_ = 0;
    std::atomic<uint64_t> dispatched_frames_{0};
    std::atomic<uint64_t> dispatched_bytes_{0};
    std::atomic<uint64_t> keepalive_sent_{0};
};

} // namespace cppdesk::server
