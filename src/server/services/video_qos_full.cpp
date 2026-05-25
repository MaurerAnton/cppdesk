// =============================================================================
// video_qos_full.cpp — Comprehensive Video QoS Implementation
// =============================================================================
// This module provides a complete video quality-of-service management system
// including bandwidth estimation, adaptive frame control, resolution scaling,
// packet loss recovery, jitter buffering, network scoring, multi-stream
// prioritization, and QoE metric computation.
//
// Namespace: cppdesk::server
// Language:   C++20
// Logging:    spdlog
// =============================================================================

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <queue>
#include <random>
#include <ranges>
#include <set>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include <spdlog/spdlog.h>

// =============================================================================
// Forward Declarations
// =============================================================================

namespace cppdesk::server {

// Forward declare all major classes
class BandwidthEstimator;
class FramePacer;
class AdaptiveFPSController;
class ResolutionScaler;
class QualityPresetManager;
class KeyframeIntervalAdapter;
class PacketLossRecovery;
class JitterBufferManager;
class NetworkConditionScorer;
class ClientFeedbackProcessor;
class MultiStreamPrioritizer;
class QoEMetricsCalculator;
class VideoQoSManager;

// =============================================================================
// Enumerations and Constants
// =============================================================================

/// Resolution levels in the quality ladder (highest to lowest)
enum class ResolutionTier : uint8_t {
    RES_8K    = 0,  ///< 7680×4320
    RES_4K    = 1,  ///< 3840×2160
    RES_1440P = 2,  ///< 2560×1440
    RES_1080P = 3,  ///< 1920×1080
    RES_720P  = 4,  ///< 1280×720
    RES_480P  = 5,  ///< 854×480
    RES_360P  = 6,  ///< 640×360
    RES_COUNT = 7
};

/// Quality preset levels
enum class QualityPreset : uint8_t {
    ULTRA   = 0,  ///< Highest quality, maximum bitrate
    HIGH    = 1,  ///< High quality
    BALANCED = 2, ///< Balanced quality/performance
    LOW     = 3,  ///< Reduced quality, conservative
    MINIMAL = 4,  ///< Minimum viable quality
    PRESET_COUNT = 5
};

/// Forward Error Correction strength levels
enum class FECLevel : uint8_t {
    OFF     = 0,
    LOW     = 1,
    MEDIUM  = 2,
    HIGH    = 3,
    MAXIMUM = 4
};

/// Network condition categories
enum class NetworkCondition : uint8_t {
    EXCELLENT = 0,
    GOOD      = 1,
    FAIR      = 2,
    POOR      = 3,
    CRITICAL  = 4
};

/// Stream priority levels for multi-stream management
enum class StreamPriority : uint8_t {
    CRITICAL  = 0,  ///< Cannot be degraded (e.g., main speaker)
    HIGH      = 1,  ///< Important stream
    NORMAL    = 2,  ///< Default priority
    LOW       = 3,  ///< Can be aggressively degraded
    BACKGROUND = 4  ///< Lowest priority, can be paused
};

/// Packet loss recovery mode
enum class RecoveryMode : uint8_t {
    NONE        = 0,
    NACK_ONLY   = 1,
    FEC_ONLY    = 2,
    HYBRID      = 3  ///< Both NACK and FEC
};

/// Statistics tracking window sizes
namespace WindowSizes {
    constexpr size_t kShortWindow   = 30;   ///< ~1 second at 30fps
    constexpr size_t kMediumWindow  = 150;  ///< ~5 seconds at 30fps
    constexpr size_t kLongWindow    = 600;  ///< ~20 seconds at 30fps
    constexpr size_t kRTTWindow     = 100;  ///< RTT samples
    constexpr size_t kLossWindow    = 500;  ///< Packet loss tracking
}

/// Network timing constants
namespace TimingConstants {
    constexpr auto kMinFrameInterval   = std::chrono::microseconds(3333);   ///< ~300fps max
    constexpr auto kMaxFrameInterval   = std::chrono::microseconds(200000); ///< 5fps min
    constexpr auto kDefaultVSyncPeriod = std::chrono::microseconds(16667);  ///< 60Hz
    constexpr auto kRTTDefault         = std::chrono::milliseconds(50);
    constexpr auto kRTTSmoothingAlpha  = 0.125f;  ///< EWMA alpha for RTT
    constexpr auto kProbeInterval      = std::chrono::milliseconds(25);     ///< Probing interval
    constexpr auto kFeedbackInterval   = std::chrono::milliseconds(200);    ///< Client feedback interval
    constexpr auto kJitterBufferTarget = std::chrono::milliseconds(40);     ///< Target jitter buffer
    constexpr auto kTransitionDuration = std::chrono::milliseconds(2000);   ///< Smooth transition time
}

/// Bandwidth estimation constants
namespace BWEConstants {
    constexpr double kMinBandwidthBps      = 50'000.0;     ///< 50 Kbps minimum
    constexpr double kMaxBandwidthBps      = 200'000'000.0; ///< 200 Mbps maximum
    constexpr double kDefaultBandwidthBps  = 5'000'000.0;   ///< 5 Mbps default
    constexpr double kBandwidthAlpha       = 0.125;         ///< EWMA alpha for bandwidth
    constexpr double kBBRGainCycleLen      = 8;             ///< BBR gain cycle phases
    constexpr double kBBRProbeGain         = 1.25;          ///< BBR probe-up gain
    constexpr double kBBRDrainGain         = 0.75;          ///< BBR drain gain
    constexpr double kCongestionThreshold  = 0.95;          ///< Congestion threshold ratio
}

/// QoE scoring constants
namespace QoEConstants {
    constexpr double kMaxMOS        = 5.0;
    constexpr double kMinMOS        = 1.0;
    constexpr double kResolutionWeight = 0.25;
    constexpr double kFramerateWeight  = 0.20;
    constexpr double kBitrateWeight    = 0.15;
    constexpr double kLatencyWeight    = 0.20;
    constexpr double kLossWeight       = 0.20;
}

/// Resolution dimensions
struct Resolution {
    uint32_t width;
    uint32_t height;
};

constexpr std::array<Resolution, 7> kResolutionLadder = {{
    {7680, 4320}, // RES_8K
    {3840, 2160}, // RES_4K
    {2560, 1440}, // RES_1440P
    {1920, 1080}, // RES_1080P
    {1280, 720},  // RES_720P
    { 854, 480},  // RES_480P
    { 640, 360}   // RES_360P
}};

/// Default bitrate targets per resolution tier (bps)
constexpr std::array<double, 7> kDefaultBitrateTargets = {{
    50'000'000.0,  // 8K:  50 Mbps
    20'000'000.0,  // 4K:  20 Mbps
    10'000'000.0,  // 1440p: 10 Mbps
     5'000'000.0,  // 1080p: 5 Mbps
     2'500'000.0,  // 720p:  2.5 Mbps
     1'000'000.0,  // 480p:  1 Mbps
       400'000.0   // 360p:  400 Kbps
}};

/// Quality preset bitrate multipliers
constexpr std::array<double, 5> kQualityBitrateMultipliers = {{
    1.5,   // ULTRA:    150% of target
    1.15,  // HIGH:     115% of target
    1.0,   // BALANCED: 100% of target
    0.7,   // LOW:       70% of target
    0.45   // MINIMAL:   45% of target
}};

// =============================================================================
// Utility Functions
// =============================================================================

namespace detail {

/// Clamp a value to [lo, hi]
template <typename T>
constexpr T clamp(T value, T lo, T hi) noexcept {
    return std::min(std::max(value, lo), hi);
}

/// Exponential weighted moving average
class EWMA {
public:
    explicit EWMA(double alpha = 0.125, double initial = 0.0) noexcept
        : alpha_(alpha), value_(initial), initialized_(false) {}

    void update(double sample) noexcept {
        if (!initialized_) {
            value_ = sample;
            initialized_ = true;
        } else {
            value_ = alpha_ * sample + (1.0 - alpha_) * value_;
        }
    }

    [[nodiscard]] double value() const noexcept { return value_; }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    void reset(double value = 0.0) noexcept {
        value_ = value;
        initialized_ = false;
    }
    void set_alpha(double alpha) noexcept { alpha_ = alpha; }

private:
    double alpha_;
    double value_;
    bool initialized_;
};

/// Sliding window statistics
template <typename T, size_t N>
class SlidingWindow {
    static_assert(N > 0, "Window size must be positive");

public:
    void push(T value) noexcept {
        if (count_ < N) {
            buffer_[count_++] = value;
            sum_ += static_cast<double>(value);
        } else {
            sum_ -= static_cast<double>(buffer_[index_]);
            buffer_[index_] = value;
            sum_ += static_cast<double>(value);
            index_ = (index_ + 1) % N;
        }
    }

    [[nodiscard]] double mean() const noexcept {
        return count_ > 0 ? sum_ / static_cast<double>(count_) : 0.0;
    }

    [[nodiscard]] double variance() const noexcept {
        if (count_ < 2) return 0.0;
        double m = mean();
        double var = 0.0;
        for (size_t i = 0; i < count_; ++i) {
            double diff = static_cast<double>(buffer_[i]) - m;
            var += diff * diff;
        }
        return var / static_cast<double>(count_ - 1);
    }

    [[nodiscard]] double stddev() const noexcept {
        return std::sqrt(variance());
    }

    [[nodiscard]] T min() const noexcept {
        if (count_ == 0) return T{};
        return *std::min_element(buffer_.begin(), buffer_.begin() + count_);
    }

    [[nodiscard]] T max() const noexcept {
        if (count_ == 0) return T{};
        return *std::max_element(buffer_.begin(), buffer_.begin() + count_);
    }

    [[nodiscard]] size_t count() const noexcept { return count_; }
    [[nodiscard]] bool full() const noexcept { return count_ == N; }

    void clear() noexcept {
        count_ = 0;
        index_ = 0;
        sum_ = 0.0;
    }

    [[nodiscard]] double percentile(double p) const noexcept {
        if (count_ == 0) return 0.0;
        std::array<T, N> sorted;
        std::copy_n(buffer_.begin(), count_, sorted.begin());
        auto end = sorted.begin() + count_;
        std::sort(sorted.begin(), end);
        size_t idx = static_cast<size_t>(p / 100.0 * (count_ - 1));
        idx = std::min(idx, count_ - 1);
        return static_cast<double>(sorted[idx]);
    }

    /// Access the underlying buffer for iteration
    [[nodiscard]] const std::array<T, N>& buffer() const noexcept { return buffer_; }
    [[nodiscard]] T operator[](size_t i) const noexcept { return buffer_[i]; }

private:
    std::array<T, N> buffer_{};
    size_t count_ = 0;
    size_t index_ = 0;
    double sum_ = 0.0;
};

/// Rate limiter using token bucket
class TokenBucket {
public:
    TokenBucket(double rate, double burst_size)
        : rate_(rate), burst_size_(burst_size), tokens_(burst_size),
          last_refill_(std::chrono::steady_clock::now()) {}

    bool consume(double tokens) noexcept {
        refill();
        if (tokens_ >= tokens) {
            tokens_ -= tokens;
            return true;
        }
        return false;
    }

    void set_rate(double rate) noexcept { rate_ = rate; }
    [[nodiscard]] double rate() const noexcept { return rate_; }
    [[nodiscard]] double available() const noexcept {
        const_cast<TokenBucket*>(this)->refill();
        return tokens_;
    }

private:
    void refill() noexcept {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - last_refill_).count();
        tokens_ = std::min(burst_size_, tokens_ + rate_ * elapsed);
        last_refill_ = now;
    }

    double rate_;
    double burst_size_;
    double tokens_;
    std::chrono::steady_clock::time_point last_refill_;
};

/// Convert resolution tier to human-readable string
constexpr std::string_view resolution_name(ResolutionTier tier) noexcept {
    constexpr std::array names = {"8K", "4K", "1440p", "1080p", "720p", "480p", "360p"};
    return names[static_cast<size_t>(tier)];
}

/// Convert quality preset to string
constexpr std::string_view preset_name(QualityPreset preset) noexcept {
    constexpr std::array names = {"ULTRA", "HIGH", "BALANCED", "LOW", "MINIMAL"};
    return names[static_cast<size_t>(preset)];
}

/// Convert network condition to string
constexpr std::string_view condition_name(NetworkCondition cond) noexcept {
    constexpr std::array names = {"EXCELLENT", "GOOD", "FAIR", "POOR", "CRITICAL"};
    return names[static_cast<size_t>(cond)];
}

/// Timer utility for high-precision measurements
class HighResTimer {
public:
    using Clock = std::chrono::steady_clock;

    void start() noexcept { start_ = Clock::now(); }

    [[nodiscard]] double elapsed_ms() const noexcept {
        return std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    }

    [[nodiscard]] int64_t elapsed_us() const noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - start_).count();
    }

private:
    Clock::time_point start_{Clock::now()};
};

} // namespace detail

// =============================================================================
// 1. Bandwidth Estimator (BBR-inspired with TCP-like congestion control)
// =============================================================================

class BandwidthEstimator {
public:
    /// Configuration for the bandwidth estimator
    struct Config {
        double min_bandwidth_bps   = BWEConstants::kMinBandwidthBps;
        double max_bandwidth_bps   = BWEConstants::kMaxBandwidthBps;
        double initial_bandwidth   = BWEConstants::kDefaultBandwidthBps;
        double ewma_alpha          = BWEConstants::kBandwidthAlpha;
        size_t bbr_cycle_length    = static_cast<size_t>(BWEConstants::kBBRGainCycleLen);
        double probe_gain          = BWEConstants::kBBRProbeGain;
        double drain_gain          = BWEConstants::kBBRDrainGain;
        std::chrono::milliseconds rtt_default = TimingConstants::kRTTDefault;
        double rtt_alpha           = TimingConstants::kRTTSmoothingAlpha;
        std::chrono::milliseconds probe_interval = TimingConstants::kProbeInterval;
    };

    explicit BandwidthEstimator(const Config& cfg = Config{})
        : cfg_(cfg)
        , estimated_bandwidth_(cfg.initial_bandwidth)
        , srtt_(cfg.rtt_default)
        , rtt_var_(cfg.rtt_default / 2)
        , bbr_phase_(BBRPhase::STARTUP)
        , bbr_gain_cycle_idx_(0)
        , pacing_rate_(cfg.initial_bandwidth)
        , cwnd_bytes_(cfg.initial_bandwidth * 0.1) // ~100ms worth
        , in_flight_bytes_(0)
        , min_rtt_(cfg.rtt_default)
        , max_bandwidth_observed_(cfg.initial_bandwidth)
        , last_probe_time_(Clock::now())
        , delivery_rate_ewma_(cfg.ewma_alpha, cfg.initial_bandwidth)
    {
        spdlog::info("[BandwidthEstimator] Initialized: bw={:.2f} Mbps, RTT={}ms",
                     estimated_bandwidth_ / 1'000'000.0, srtt_.load().count());
    }

    // ---- Packet event callbacks --------------------------------------------

    /// Called when a packet is sent
    void on_packet_sent(uint64_t packet_id, size_t payload_bytes,
                        std::chrono::steady_clock::time_point sent_time) noexcept
    {
        std::unique_lock lock(mutex_);

        PacketInfo info;
        info.payload_bytes = payload_bytes;
        info.sent_time = sent_time;
        info.acked = false;
        flight_map_[packet_id] = info;
        in_flight_bytes_ += payload_bytes;

        // Prune old entries periodically
        if (flight_map_.size() > 10'000) {
            prune_flight_map();
        }
    }

    /// Called when an ACK is received for a packet
    void on_packet_acked(uint64_t packet_id,
                         std::chrono::steady_clock::time_point ack_time) noexcept
    {
        std::unique_lock lock(mutex_);

        auto it = flight_map_.find(packet_id);
        if (it == flight_map_.end() || it->second.acked) return;

        it->second.acked = true;

        auto rtt = std::chrono::duration_cast<std::chrono::milliseconds>(
            ack_time - it->second.sent_time);

        update_rtt(rtt);
        update_delivery_rate(it->second.payload_bytes, it->second.sent_time, ack_time);

        in_flight_bytes_ -= std::min(in_flight_bytes_, it->second.payload_bytes);
        bytes_acked_ += it->second.payload_bytes;

        // Update max observed bandwidth
        double instant_rate = delivery_rate_ewma_.value();
        if (instant_rate > max_bandwidth_observed_) {
            max_bandwidth_observed_ = instant_rate;
        }

        update_bbr_phase(ack_time);
        update_cwnd();
    }

    /// Called when packet loss is detected
    void on_packet_lost(uint64_t packet_id, size_t loss_count = 1) noexcept
    {
        std::unique_lock lock(mutex_);

        auto it = flight_map_.find(packet_id);
        if (it != flight_map_.end() && !it->second.acked) {
            in_flight_bytes_ -= std::min(in_flight_bytes_, it->second.payload_bytes);
            it->second.acked = false;
        }

        total_lost_packets_ += loss_count;
        recent_loss_events_.push_back(Clock::now());

        // Clean old loss events
        auto cutoff = Clock::now() - std::chrono::seconds(10);
        while (!recent_loss_events_.empty() && recent_loss_events_.front() < cutoff) {
            recent_loss_events_.pop_front();
        }

        // Loss triggers congestion response
        if (bbr_phase_ != BBRPhase::DRAIN) {
            // Multiplicative decrease on loss
            estimated_bandwidth_ *= 0.7;
            cwnd_bytes_ = std::max(cwnd_bytes_ * 0.5,
                                   cfg_.min_bandwidth_bps * 0.05);
            bbr_phase_ = BBRPhase::DRAIN;
        }
    }

    // ---- Bandwidth and congestion queries ----------------------------------

    /// Get the current estimated bandwidth in bits per second
    [[nodiscard]] double estimated_bandwidth_bps() const noexcept {
        return estimated_bandwidth_.load(std::memory_order_acquire);
    }

    /// Get smoothed round-trip time
    [[nodiscard]] std::chrono::milliseconds smoothed_rtt() const noexcept {
        return srtt_.load();
    }

    /// Get RTT variance
    [[nodiscard]] std::chrono::milliseconds rtt_variance() const noexcept {
        return rtt_var_.load();
    }

    /// Get current pacing rate
    [[nodiscard]] double pacing_rate_bps() const noexcept {
        return pacing_rate_.load(std::memory_order_acquire);
    }

    /// Get congestion window in bytes
    [[nodiscard]] double cwnd_bytes() const noexcept {
        return cwnd_bytes_.load(std::memory_order_acquire);
    }

    /// Get bytes currently in flight
    [[nodiscard]] double bytes_in_flight() const noexcept {
        return in_flight_bytes_.load(std::memory_order_acquire);
    }

    /// Get packet loss rate (0.0 to 1.0)
    [[nodiscard]] double loss_rate() const noexcept {
        std::shared_lock lock(mutex_);
        uint64_t total = total_sent_packets_.load();
        if (total == 0) return 0.0;
        return static_cast<double>(total_lost_packets_.load()) / static_cast<double>(total);
    }

    /// Get recent loss event count (last 10 seconds)
    [[nodiscard]] size_t recent_loss_count() const noexcept {
        std::shared_lock lock(mutex_);
        return recent_loss_events_.size();
    }

    /// Whether the estimator considers the link congested
    [[nodiscard]] bool is_congested() const noexcept {
        return loss_rate() > 0.02 || bbr_phase_ == BBRPhase::DRAIN;
    }

    /// Whether we're in a probing state
    [[nodiscard]] bool is_probing() const noexcept {
        return bbr_phase_ == BBRPhase::PROBE_BW;
    }

    /// Get the current BBR phase name
    [[nodiscard]] std::string_view bbr_phase_name() const noexcept {
        constexpr std::array names = {"STARTUP", "DRAIN", "PROBE_BW", "PROBE_RTT"};
        return names[static_cast<int>(bbr_phase_.load())];
    }

    /// Get delivery rate estimate
    [[nodiscard]] double delivery_rate_bps() const noexcept {
        return delivery_rate_ewma_.value();
    }

    /// Force-set bandwidth (e.g., from external measurement)
    void set_bandwidth(double bps) noexcept {
        double clamped = detail::clamp(bps, cfg_.min_bandwidth_bps, cfg_.max_bandwidth_bps);
        estimated_bandwidth_.store(clamped, std::memory_order_release);
        pacing_rate_.store(clamped, std::memory_order_release);
    }

    /// Reset estimator state
    void reset() noexcept {
        std::unique_lock lock(mutex_);
        estimated_bandwidth_ = cfg_.initial_bandwidth;
        srtt_ = cfg_.rtt_default;
        rtt_var_ = cfg_.rtt_default / 2;
        bbr_phase_ = BBRPhase::STARTUP;
        bbr_gain_cycle_idx_ = 0;
        pacing_rate_ = cfg_.initial_bandwidth;
        cwnd_bytes_ = cfg_.initial_bandwidth * 0.1;
        in_flight_bytes_ = 0;
        min_rtt_ = cfg_.rtt_default;
        max_bandwidth_observed_ = cfg_.initial_bandwidth;
        total_sent_packets_ = 0;
        total_lost_packets_ = 0;
        bytes_acked_ = 0;
        flight_map_.clear();
        recent_loss_events_.clear();
        delivery_rate_ewma_.reset(cfg_.initial_bandwidth);
    }

private:
    using Clock = std::chrono::steady_clock;

    enum class BBRPhase : uint8_t {
        STARTUP,
        DRAIN,
        PROBE_BW,
        PROBE_RTT
    };

    struct PacketInfo {
        size_t payload_bytes = 0;
        Clock::time_point sent_time;
        bool acked = false;
    };

    void update_rtt(std::chrono::milliseconds rtt) noexcept {
        auto rtt_ms = static_cast<double>(rtt.count());
        auto current_srtt = static_cast<double>(srtt_.load().count());
        auto current_var = static_cast<double>(rtt_var_.load().count());

        // Standard TCP RTT estimation
        double diff = rtt_ms - current_srtt;
        current_srtt += cfg_.rtt_alpha * diff;
        current_var += cfg_.rtt_alpha * (std::abs(diff) - current_var);

        srtt_.store(std::chrono::milliseconds(static_cast<int64_t>(current_srtt)));
        rtt_var_.store(std::chrono::milliseconds(static_cast<int64_t>(current_var)));

        // Track minimum RTT
        auto min_rtt = min_rtt_.load();
        if (rtt < min_rtt) {
            min_rtt_ = rtt;
        }
    }

    void update_delivery_rate(size_t bytes, Clock::time_point sent,
                              Clock::time_point acked) noexcept
    {
        double elapsed = std::chrono::duration<double>(acked - sent).count();
        if (elapsed > 0.0) {
            double rate = (bytes * 8.0) / elapsed;
            delivery_rate_ewma_.update(rate);
        }
    }

    void update_bbr_phase(Clock::time_point now) noexcept {
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch());

        switch (bbr_phase_) {
        case BBRPhase::STARTUP:
            // Exit startup if bandwidth hasn't increased significantly
            if (estimated_bandwidth_ >= max_bandwidth_observed_ * 0.95 &&
                flight_map_.size() > 10) {
                bbr_phase_ = BBRPhase::DRAIN;
                spdlog::debug("[BWE] Entering DRAIN phase");
            }
            break;

        case BBRPhase::DRAIN:
            // Drain until in-flight drops to BDP
            if (in_flight_bytes_ <= estimated_bandwidth_ * 0.1) {
                bbr_phase_ = BBRPhase::PROBE_BW;
                spdlog::debug("[BWE] Entering PROBE_BW phase");
            }
            break;

        case BBRPhase::PROBE_BW:
            // Cycle through gain values
            if (now - last_probe_time_ >= cfg_.probe_interval) {
                bbr_gain_cycle_idx_ = (bbr_gain_cycle_idx_ + 1) % cfg_.bbr_cycle_length;

                // Apply cyclic gain
                double gain = 1.0;
                if (bbr_gain_cycle_idx_ == 0) {
                    gain = cfg_.probe_gain;  // Probe up
                } else if (bbr_gain_cycle_idx_ == 1) {
                    gain = cfg_.drain_gain;  // Drain
                }
                // Other phases use gain = 1.0

                double new_pacing = max_bandwidth_observed_ * gain;
                new_pacing = detail::clamp(new_pacing, cfg_.min_bandwidth_bps, cfg_.max_bandwidth_bps);
                pacing_rate_.store(new_pacing, std::memory_order_release);

                // Update bandwidth estimate (smoothed)
                double alpha = cfg_.ewma_alpha;
                double new_bw = alpha * max_bandwidth_observed_ +
                               (1.0 - alpha) * estimated_bandwidth_.load();
                estimated_bandwidth_.store(new_bw, std::memory_order_release);

                last_probe_time_ = now;
            }

            // Transition to PROBE_RTT periodically
            if (now - last_rtt_probe_time_ > std::chrono::seconds(10)) {
                bbr_phase_ = BBRPhase::PROBE_RTT;
                cwnd_bytes_.store(4 * 1500, std::memory_order_release); // 4 MSS
                last_rtt_probe_time_ = now;
                spdlog::debug("[BWE] Entering PROBE_RTT phase");
            }
            break;

        case BBRPhase::PROBE_RTT:
            // Stay for ~200ms then go back
            if (now - last_rtt_probe_time_ > std::chrono::milliseconds(200)) {
                bbr_phase_ = BBRPhase::PROBE_BW;
                spdlog::debug("[BWE] Returning to PROBE_BW");
            }
            break;
        }
    }

    void update_cwnd() noexcept {
        double bw = estimated_bandwidth_.load();
        double rtt_sec = static_cast<double>(srtt_.load().count()) / 1000.0;
        double bdp = bw * rtt_sec / 8.0; // Bandwidth-Delay Product in bytes

        switch (bbr_phase_) {
        case BBRPhase::STARTUP:
            cwnd_bytes_ = bdp * 2.0; // Aggressive during startup
            break;
        case BBRPhase::DRAIN:
            cwnd_bytes_ = bdp * 0.75; // Reduce
            break;
        case BBRPhase::PROBE_BW:
            cwnd_bytes_ = bdp * 2.0; // Room for probing
            break;
        case BBRPhase::PROBE_RTT:
            cwnd_bytes_ = 4 * 1500; // Minimal
            break;
        }

        cwnd_bytes_ = detail::clamp(cwnd_bytes_.load(),
                                    4.0 * 1500.0,
                                    cfg_.max_bandwidth_bps * 0.5);
    }

    void prune_flight_map() noexcept {
        // Remove acked packets older than 5 seconds
        auto cutoff = Clock::now() - std::chrono::seconds(5);
        for (auto it = flight_map_.begin(); it != flight_map_.end();) {
            if (it->second.acked && it->second.sent_time < cutoff) {
                it = flight_map_.erase(it);
            } else {
                ++it;
            }
        }
    }

    Config cfg_;
    mutable std::shared_mutex mutex_;

    std::atomic<double> estimated_bandwidth_{BWEConstants::kDefaultBandwidthBps};
    std::atomic<std::chrono::milliseconds> srtt_{TimingConstants::kRTTDefault};
    std::atomic<std::chrono::milliseconds> rtt_var_{TimingConstants::kRTTDefault / 2};
    std::atomic<BBRPhase> bbr_phase_{BBRPhase::STARTUP};
    std::atomic<double> pacing_rate_{BWEConstants::kDefaultBandwidthBps};
    std::atomic<double> cwnd_bytes_{BWEConstants::kDefaultBandwidthBps * 0.1};
    std::atomic<double> in_flight_bytes_{0};
    std::atomic<uint64_t> total_sent_packets_{0};
    std::atomic<uint64_t> total_lost_packets_{0};

    uint64_t bytes_acked_ = 0;
    std::atomic<std::chrono::milliseconds> min_rtt_{TimingConstants::kRTTDefault};
    double max_bandwidth_observed_ = BWEConstants::kDefaultBandwidthBps;

    std::unordered_map<uint64_t, PacketInfo> flight_map_;
    std::deque<Clock::time_point> recent_loss_events_;

    detail::EWMA delivery_rate_ewma_;
    Clock::time_point last_probe_time_{Clock::now()};
    Clock::time_point last_rtt_probe_time_{Clock::now()};

    size_t bbr_gain_cycle_idx_ = 0;
};

// =============================================================================
// 2. Frame Pacer (Display VSync-Aware)
// =============================================================================

class FramePacer {
public:
    struct Config {
        std::chrono::microseconds vsync_period = TimingConstants::kDefaultVSyncPeriod;
        std::chrono::microseconds min_frame_interval = TimingConstants::kMinFrameInterval;
        std::chrono::microseconds max_frame_interval = TimingConstants::kMaxFrameInterval;
        bool vsync_snap = true;      ///< Snap frames to vsync boundaries
        double timing_slack_pct = 5.0; ///< Allowable timing slack percentage
    };

    explicit FramePacer(const Config& cfg = Config{})
        : cfg_(cfg)
        , next_frame_time_(Clock::now())
        , last_actual_frame_time_(Clock::now())
        , frame_interval_(cfg.vsync_period)
        , vsync_offset_us_(0)
        , drift_us_(0.0)
    {
        spdlog::info("[FramePacer] Initialized: vsync={}us, interval={}us",
                     cfg.vsync_period.count(), frame_interval_.load().count());
    }

    /// Set the target frame interval
    void set_frame_interval(std::chrono::microseconds interval) noexcept {
        auto clamped = detail::clamp(interval, cfg_.min_frame_interval, cfg_.max_frame_interval);
        frame_interval_.store(clamped, std::memory_order_release);
    }

    /// Set target FPS
    void set_target_fps(double fps) noexcept {
        if (fps <= 0.0) return;
        auto interval = std::chrono::microseconds(
            static_cast<int64_t>(1'000'000.0 / fps));
        set_frame_interval(interval);
    }

    /// Get current target frame interval
    [[nodiscard]] std::chrono::microseconds frame_interval() const noexcept {
        return frame_interval_.load(std::memory_order_acquire);
    }

    /// Get current target FPS
    [[nodiscard]] double target_fps() const noexcept {
        auto interval = frame_interval_.load();
        if (interval.count() == 0) return 0.0;
        return 1'000'000.0 / static_cast<double>(interval.count());
    }

    /// Get actual measured FPS over the recent window
    [[nodiscard]] double actual_fps() const noexcept {
        std::shared_lock lock(mutex_);
        if (frame_times_.count() < 2) return target_fps();
        double avg_interval = 0.0;
        for (size_t i = 1; i < frame_times_.count(); ++i) {
            avg_interval += std::chrono::duration_cast<std::chrono::microseconds>(
                frame_times_.buffer()[i] - frame_times_.buffer()[i - 1]).count();
        }
        avg_interval /= (frame_times_.count() - 1);
        return avg_interval > 0.0 ? 1'000'000.0 / avg_interval : target_fps();
    }

    /// Calculate the ideal presentation time for the next frame
    [[nodiscard]] Clock::time_point next_presentation_time() noexcept {
        std::unique_lock lock(mutex_);

        auto now = Clock::now();
        auto interval = frame_interval_.load();

        // Calculate base next time
        auto scheduled = next_frame_time_;

        // Apply vsync snapping if enabled
        if (cfg_.vsync_snap) {
            scheduled = snap_to_vsync(scheduled);
        }

        // Ensure we don't fall behind too far
        auto max_ahead = now + interval * 2;

        if (scheduled < now) {
            // We're behind — schedule immediately with drift tracking
            drift_us_ += std::chrono::duration_cast<std::chrono::microseconds>(
                now - scheduled).count();
            next_frame_time_ = now + interval;
            return now;
        }

        if (scheduled > max_ahead) {
            next_frame_time_ = max_ahead;
            return max_ahead;
        }

        next_frame_time_ = scheduled + interval;
        return scheduled;
    }

    /// Notify the pacer that a frame was actually presented
    void on_frame_presented(Clock::time_point presentation_time) noexcept {
        std::unique_lock lock(mutex_);
        last_actual_frame_time_ = presentation_time;
        frame_times_.push(presentation_time);
    }

    /// Calculate sleep duration needed before next frame
    [[nodiscard]] std::chrono::microseconds time_until_next_frame() const noexcept {
        auto now = Clock::now();
        std::shared_lock lock(mutex_);
        if (next_frame_time_ <= now) return std::chrono::microseconds(0);
        return std::chrono::duration_cast<std::chrono::microseconds>(next_frame_time_ - now);
    }

    /// Set vsync offset (phase adjustment)
    void set_vsync_offset(std::chrono::microseconds offset) noexcept {
        vsync_offset_us_.store(offset.count());
    }

    /// Get accumulated timing drift
    [[nodiscard]] double drift_us() const noexcept { return drift_us_; }

    /// Reset pacer state
    void reset() noexcept {
        std::unique_lock lock(mutex_);
        next_frame_time_ = Clock::now();
        last_actual_frame_time_ = Clock::now();
        drift_us_ = 0.0;
        frame_times_.clear();
    }

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point snap_to_vsync(Clock::time_point t) const noexcept {
        auto vsync_period = cfg_.vsync_period.count();
        auto offset = vsync_offset_us_.load();

        // Convert to microseconds since epoch
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            t.time_since_epoch()).count();

        // Snap to nearest vsync boundary
        int64_t snapped = ((us - offset + vsync_period / 2) / vsync_period) * vsync_period + offset;

        return Clock::time_point(std::chrono::microseconds(snapped));
    }

    Config cfg_;
    mutable std::shared_mutex mutex_;

    Clock::time_point next_frame_time_{Clock::now()};
    Clock::time_point last_actual_frame_time_{Clock::now()};
    std::atomic<std::chrono::microseconds> frame_interval_{TimingConstants::kDefaultVSyncPeriod};
    std::atomic<int64_t> vsync_offset_us_{0};
    double drift_us_ = 0.0;

    detail::SlidingWindow<Clock::time_point, WindowSizes::kShortWindow> frame_times_;
};

// =============================================================================
// 3. Adaptive FPS Controller
// =============================================================================

class AdaptiveFPSController {
public:
    struct Config {
        double min_fps = 5.0;
        double max_fps = 120.0;
        double default_fps = 30.0;
        double content_sensitivity = 0.5;  ///< How much content change affects FPS
        double bandwidth_sensitivity = 0.3; ///< How much bandwidth affects FPS
        double fps_step_up = 2.0;          ///< FPS increment on upgrade
        double fps_step_down = 5.0;        ///< FPS decrement on downgrade
        size_t decision_interval_frames = 30; ///< Frames between FPS decisions
        double low_motion_fps = 15.0;      ///< FPS floor for low-motion content
        double high_motion_fps = 60.0;     ///< FPS ceiling for high-motion content
        double motion_threshold_low = 0.02;  ///< Below this = low motion
        double motion_threshold_high = 0.15; ///< Above this = high motion
    };

    explicit AdaptiveFPSController(const Config& cfg = Config{})
        : cfg_(cfg), current_target_fps_(cfg.default_fps),
          motion_score_ewma_(0.3, 0.05), bandwidth_ratio_ewma_(0.2, 1.0)
    {
        spdlog::info("[AdaptiveFPSController] Initialized: {:.1f}-{:.1f} FPS, default={:.1f}",
                     cfg.min_fps, cfg.max_fps, cfg.default_fps);
    }

    /// Feed content change information (0.0 = static, 1.0 = maximum motion)
    void update_content_change(double motion_score) noexcept {
        motion_score_ewma_.update(motion_score);
    }

    /// Feed bandwidth utilization ratio (available_bw / required_bw)
    void update_bandwidth_ratio(double ratio) noexcept {
        bandwidth_ratio_ewma_.update(ratio);
    }

    /// Evaluate and return the recommended target FPS
    [[nodiscard]] double evaluate() noexcept {
        std::unique_lock lock(mutex_);
        ++frame_counter_;

        if (frame_counter_ < cfg_.decision_interval_frames) {
            return current_target_fps_;
        }
        frame_counter_ = 0;

        double motion = motion_score_ewma_.value();
        double bw_ratio = bandwidth_ratio_ewma_.value();

        // Determine motion-based FPS range
        double motion_fps;
        if (motion < cfg_.motion_threshold_low) {
            motion_fps = cfg_.low_motion_fps;
        } else if (motion > cfg_.motion_threshold_high) {
            motion_fps = cfg_.high_motion_fps;
        } else {
            // Linear interpolation between thresholds
            double t = (motion - cfg_.motion_threshold_low) /
                       (cfg_.motion_threshold_high - cfg_.motion_threshold_low);
            motion_fps = cfg_.low_motion_fps +
                        t * (cfg_.high_motion_fps - cfg_.low_motion_fps);
        }

        // Bandwidth adjustment
        double bw_fps = current_target_fps_ * bw_ratio;

        // Blend motion and bandwidth recommendations
        double motion_weight = cfg_.content_sensitivity;
        double bw_weight = cfg_.bandwidth_sensitivity;
        double total_weight = motion_weight + bw_weight;
        double blended_fps = (motion_fps * motion_weight + bw_fps * bw_weight) / total_weight;

        // Apply hysteresis: step changes rather than continuous
        double new_fps = current_target_fps_;
        if (blended_fps > current_target_fps_ + cfg_.fps_step_up) {
            new_fps = current_target_fps_ + cfg_.fps_step_up;
        } else if (blended_fps < current_target_fps_ - cfg_.fps_step_down) {
            new_fps = current_target_fps_ - cfg_.fps_step_down;
        }

        // Clamp and round to nice values
        new_fps = detail::clamp(new_fps, cfg_.min_fps, cfg_.max_fps);
        new_fps = snap_to_nice_fps(new_fps);

        if (std::abs(new_fps - current_target_fps_) > 0.5) {
            spdlog::debug("[AdaptiveFPS] FPS change: {:.1f} → {:.1f} (motion={:.3f}, bw_ratio={:.2f})",
                         current_target_fps_, new_fps, motion, bw_ratio);
            current_target_fps_ = new_fps;
        }

        return current_target_fps_;
    }

    /// Get current target FPS without re-evaluating
    [[nodiscard]] double current_fps() const noexcept {
        return current_target_fps_;
    }

    /// Force-set FPS (overrides adaptive logic temporarily)
    void force_fps(double fps) noexcept {
        current_target_fps_ = detail::clamp(fps, cfg_.min_fps, cfg_.max_fps);
    }

    /// Get motion score
    [[nodiscard]] double motion_score() const noexcept {
        return motion_score_ewma_.value();
    }

    /// Reset controller state
    void reset() noexcept {
        std::unique_lock lock(mutex_);
        current_target_fps_ = cfg_.default_fps;
        motion_score_ewma_.reset(0.05);
        bandwidth_ratio_ewma_.reset(1.0);
        frame_counter_ = 0;
    }

private:
    static double snap_to_nice_fps(double fps) noexcept {
        constexpr std::array nice_fps = {5.0, 10.0, 15.0, 20.0, 24.0, 30.0, 48.0, 60.0, 90.0, 120.0};
        double best = nice_fps[0];
        double best_diff = std::abs(fps - best);
        for (double f : nice_fps) {
            double diff = std::abs(fps - f);
            if (diff < best_diff) {
                best_diff = diff;
                best = f;
            }
        }
        return best;
    }

    Config cfg_;
    mutable std::mutex mutex_;
    double current_target_fps_;
    detail::EWMA motion_score_ewma_;
    detail::EWMA bandwidth_ratio_ewma_;
    size_t frame_counter_ = 0;
};

// =============================================================================
// 4. Dynamic Resolution Scaler
// =============================================================================

class ResolutionScaler {
public:
    struct Config {
        std::array<double, 7> bitrate_targets = kDefaultBitrateTargets;
        double scale_up_threshold = 1.3;   ///< Available BW must be 1.3x target to scale up
        double scale_down_threshold = 0.85; ///< Scale down if BW drops to 85% of target
        size_t stable_frames_required = 60; ///< Frames of stability before scaling
        double hysteresis_pct = 10.0;       ///< Hysteresis percentage to prevent oscillation
        std::chrono::milliseconds cooldown = std::chrono::milliseconds(3000); ///< Min time between changes
    };

    explicit ResolutionScaler(const Config& cfg = Config{})
        : cfg_(cfg), current_tier_(ResolutionTier::RES_1080P),
          target_tier_(ResolutionTier::RES_1080P),
          stable_counter_(0), transition_active_(false),
          transition_progress_(0.0)
    {
        spdlog::info("[ResolutionScaler] Initialized: current={}, target={}",
                     detail::resolution_name(current_tier_),
                     detail::resolution_name(target_tier_));
    }

    /// Evaluate resolution based on available bandwidth
    [[nodiscard]] ResolutionTier evaluate(double available_bandwidth_bps) noexcept {
        std::unique_lock lock(mutex_);
        auto now = Clock::now();

        // Enforce cooldown
        if (now - last_change_time_ < cfg_.cooldown) {
            return current_tier_;
        }

        ResolutionTier best_tier = current_tier_;

        // Find the highest resolution we can sustain
        for (int i = 0; i < static_cast<int>(ResolutionTier::RES_COUNT); ++i) {
            auto tier = static_cast<ResolutionTier>(i);
            double target = get_bitrate_target(tier);

            // Apply hysteresis: harder to switch back to a resolution we just left
            double threshold = cfg_.scale_up_threshold;
            if (tier != current_tier_ && static_cast<int>(tier) < static_cast<int>(current_tier_)) {
                // Scaling UP: need more headroom
                threshold *= (1.0 + cfg_.hysteresis_pct / 100.0);
            }

            if (available_bandwidth_bps >= target * threshold) {
                best_tier = tier;
            } else {
                break; // Higher resolutions won't fit
            }
        }

        // If that didn't find anything suitable, find the best lower resolution
        if (available_bandwidth_bps < get_bitrate_target(best_tier) * cfg_.scale_down_threshold) {
            for (int i = static_cast<int>(ResolutionTier::RES_COUNT) - 1; i >= 0; --i) {
                auto tier = static_cast<ResolutionTier>(i);
                double target = get_bitrate_target(tier);
                if (available_bandwidth_bps >= target * cfg_.scale_down_threshold) {
                    best_tier = tier;
                    break;
                }
            }
        }

        // Stability check
        if (best_tier == target_tier_) {
            ++stable_counter_;
        } else {
            stable_counter_ = 0;
            target_tier_ = best_tier;
        }

        // Commit change only after stable period
        if (stable_counter_ >= cfg_.stable_frames_required &&
            target_tier_ != current_tier_ && !transition_active_) {
            spdlog::info("[ResolutionScaler] Scaling: {} → {} (bw={:.2f} Mbps)",
                         detail::resolution_name(current_tier_),
                         detail::resolution_name(target_tier_),
                         available_bandwidth_bps / 1'000'000.0);
            start_transition(target_tier_);
        }

        // Update transition progress
        if (transition_active_) {
            double elapsed = std::chrono::duration<double, std::milli>(
                now - transition_start_).count();
            double duration_ms = TimingConstants::kTransitionDuration.count();
            transition_progress_ = detail::clamp(elapsed / duration_ms, 0.0, 1.0);

            if (transition_progress_ >= 1.0) {
                complete_transition();
            }
        }

        return current_tier_;
    }

    /// Get current resolution
    [[nodiscard]] ResolutionTier current_tier() const noexcept { return current_tier_; }

    /// Get target resolution (may differ during transitions)
    [[nodiscard]] ResolutionTier target_tier() const noexcept { return target_tier_; }

    /// Get resolution dimensions
    [[nodiscard]] Resolution current_resolution() const noexcept {
        return kResolutionLadder[static_cast<size_t>(current_tier_)];
    }

    /// Get resolution dimensions for a specific tier
    [[nodiscard]] static Resolution resolution_for(ResolutionTier tier) noexcept {
        return kResolutionLadder[static_cast<size_t>(tier)];
    }

    /// Get bitrate target for current tier with quality multiplier
    [[nodiscard]] double bitrate_target(QualityPreset quality = QualityPreset::BALANCED) const noexcept {
        double base = get_bitrate_target(current_tier_);
        return base * kQualityBitrateMultipliers[static_cast<size_t>(quality)];
    }

    /// Get base bitrate target for a tier
    [[nodiscard]] static double get_bitrate_target(ResolutionTier tier) noexcept {
        return kDefaultBitrateTargets[static_cast<size_t>(tier)];
    }

    /// Get transition progress (0.0 = old res fully, 1.0 = new res fully)
    [[nodiscard]] double transition_progress() const noexcept {
        return transition_progress_;
    }

    /// Whether a transition is in progress
    [[nodiscard]] bool is_transitioning() const noexcept {
        return transition_active_;
    }

    /// Force a specific resolution tier
    void force_tier(ResolutionTier tier) noexcept {
        std::unique_lock lock(mutex_);
        if (tier != current_tier_) {
            spdlog::info("[ResolutionScaler] Forced: {} → {}",
                         detail::resolution_name(current_tier_),
                         detail::resolution_name(tier));
            start_transition(tier);
        }
    }

    /// Reset to default
    void reset() noexcept {
        std::unique_lock lock(mutex_);
        current_tier_ = ResolutionTier::RES_1080P;
        target_tier_ = ResolutionTier::RES_1080P;
        stable_counter_ = 0;
        transition_active_ = false;
        transition_progress_ = 0.0;
    }

private:
    using Clock = std::chrono::steady_clock;

    void start_transition(ResolutionTier to) noexcept {
        target_tier_ = to;
        transition_from_ = current_tier_;
        transition_start_ = Clock::now();
        transition_active_ = true;
        transition_progress_ = 0.0;
        last_change_time_ = transition_start_;
    }

    void complete_transition() noexcept {
        current_tier_ = target_tier_;
        transition_active_ = false;
        transition_progress_ = 1.0;
        stable_counter_ = 0;
    }

    Config cfg_;
    mutable std::mutex mutex_;

    ResolutionTier current_tier_;
    ResolutionTier target_tier_;
    ResolutionTier transition_from_{ResolutionTier::RES_1080P};
    size_t stable_counter_;
    bool transition_active_;
    double transition_progress_;
    Clock::time_point transition_start_;
    Clock::time_point last_change_time_{Clock::now()};
};

// =============================================================================
// 5. Quality Preset Manager with Smooth Transitions
// =============================================================================

class QualityPresetManager {
public:
    struct PresetParameters {
        double bitrate_multiplier;
        int min_qp;          ///< Minimum quantization parameter (lower = better quality)
        int max_qp;          ///< Maximum quantization parameter
        double crf_offset;   ///< CRF offset from default
        double psy_rd_strength; ///< Psychovisual RD strength
        double aq_strength;  ///< Adaptive quantization strength
        int ref_frames;      ///< Reference frame count
        int b_frames;        ///< B-frame count
        bool deblock_enabled;
        double deblock_strength;
    };

    static constexpr std::array<PresetParameters, 5> kPresets = {{
        // ULTRA
        {1.5,  12, 28, -6.0, 1.2, 1.0, 8, 3, true,  0.5},
        // HIGH
        {1.15, 18, 32, -3.0, 1.0, 0.9, 5, 3, true,  0.0},
        // BALANCED
        {1.0,  22, 38,  0.0, 0.8, 0.8, 3, 2, true,  0.0},
        // LOW
        {0.7,  26, 42,  3.0, 0.6, 0.6, 2, 1, true,  1.0},
        // MINIMAL
        {0.45, 30, 51,  6.0, 0.4, 0.5, 1, 0, false, 2.0}
    }};

    struct Config {
        std::chrono::milliseconds transition_duration = TimingConstants::kTransitionDuration;
        double transition_easing_power = 2.0; ///< Easing curve power (2.0 = quadratic)
    };

    explicit QualityPresetManager(const Config& cfg = Config{})
        : cfg_(cfg), current_preset_(QualityPreset::BALANCED),
          target_preset_(QualityPreset::BALANCED),
          transitioning_(false), transition_progress_(1.0)
    {
        current_params_ = kPresets[static_cast<size_t>(current_preset_)];
        spdlog::info("[QualityPresetManager] Initialized: {}",
                     detail::preset_name(current_preset_));
    }

    /// Request a preset change
    void request_preset(QualityPreset preset) noexcept {
        std::unique_lock lock(mutex_);
        if (preset == target_preset_ && !transitioning_) return;

        spdlog::info("[QualityPresetManager] Transition: {} → {}",
                     detail::preset_name(current_preset_),
                     detail::preset_name(preset));
        target_preset_ = preset;
        from_params_ = current_params_;
        to_params_ = kPresets[static_cast<size_t>(preset)];
        transition_start_ = Clock::now();
        transitioning_ = true;
        transition_progress_ = 0.0;
    }

    /// Update transition and get current interpolated parameters
    [[nodiscard]] PresetParameters update() noexcept {
        std::unique_lock lock(mutex_);

        if (!transitioning_) {
            return current_params_;
        }

        auto now = Clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(
            now - transition_start_).count();
        double duration = cfg_.transition_duration.count();
        double raw_progress = detail::clamp(elapsed / duration, 0.0, 1.0);

        // Apply easing
        transition_progress_ = ease_in_out_cubic(raw_progress);

        // Interpolate all parameters
        current_params_ = interpolate_params(from_params_, to_params_, transition_progress_);

        if (raw_progress >= 1.0) {
            current_params_ = to_params_;
            current_preset_ = target_preset_;
            transitioning_ = false;
            transition_progress_ = 1.0;
            spdlog::debug("[QualityPresetManager] Transition complete: {}",
                          detail::preset_name(current_preset_));
        }

        return current_params_;
    }

    /// Get current preset without updating
    [[nodiscard]] QualityPreset current_preset() const noexcept {
        return current_preset_;
    }

    /// Get current interpolated parameters
    [[nodiscard]] PresetParameters current_parameters() const noexcept {
        return current_params_;
    }

    /// Get transition progress
    [[nodiscard]] double transition_progress() const noexcept {
        return transition_progress_;
    }

    /// Whether currently in transition
    [[nodiscard]] bool is_transitioning() const noexcept {
        return transitioning_;
    }

    /// Get bitrate recommendation for a given base bitrate
    [[nodiscard]] double recommended_bitrate(double base_bitrate) const noexcept {
        return base_bitrate * current_params_.bitrate_multiplier;
    }

    /// Reset to default
    void reset() noexcept {
        std::unique_lock lock(mutex_);
        current_preset_ = QualityPreset::BALANCED;
        target_preset_ = QualityPreset::BALANCED;
        current_params_ = kPresets[static_cast<size_t>(QualityPreset::BALANCED)];
        transitioning_ = false;
        transition_progress_ = 1.0;
    }

private:
    using Clock = std::chrono::steady_clock;

    static PresetParameters interpolate_params(const PresetParameters& from,
                                                const PresetParameters& to,
                                                double t) noexcept
    {
        auto lerp = [t](double a, double b) { return a + (b - a) * t; };
        auto lerpi = [t](int a, int b) {
            return static_cast<int>(std::round(static_cast<double>(a) +
                                      (static_cast<double>(b) - a) * t));
        };

        PresetParameters result;
        result.bitrate_multiplier = lerp(from.bitrate_multiplier, to.bitrate_multiplier);
        result.min_qp = lerpi(from.min_qp, to.min_qp);
        result.max_qp = lerpi(from.max_qp, to.max_qp);
        result.crf_offset = lerp(from.crf_offset, to.crf_offset);
        result.psy_rd_strength = lerp(from.psy_rd_strength, to.psy_rd_strength);
        result.aq_strength = lerp(from.aq_strength, to.aq_strength);
        result.ref_frames = lerpi(from.ref_frames, to.ref_frames);
        result.b_frames = lerpi(from.b_frames, to.b_frames);
        result.deblock_enabled = t < 0.5 ? from.deblock_enabled : to.deblock_enabled;
        result.deblock_strength = lerp(from.deblock_strength, to.deblock_strength);
        return result;
    }

    static double ease_in_out_cubic(double t) noexcept {
        return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
    }

    Config cfg_;
    mutable std::mutex mutex_;

    QualityPreset current_preset_;
    QualityPreset target_preset_;
    PresetParameters current_params_;
    PresetParameters from_params_;
    PresetParameters to_params_;
    bool transitioning_;
    double transition_progress_;
    Clock::time_point transition_start_;
};

// =============================================================================
// 6. Keyframe Interval Adapter
// =============================================================================

class KeyframeIntervalAdapter {
public:
    struct Config {
        uint32_t default_interval = 60;   ///< Default: keyframe every 60 frames (~2s @ 30fps)
        uint32_t min_interval = 15;       ///< Minimum keyframe interval
        uint32_t max_interval = 300;      ///< Maximum keyframe interval (10s @ 30fps)
        double loss_scaling_factor = 2.0; ///< How aggressively loss reduces interval
        double rtt_scaling_factor = 1.5;  ///< How RTT affects interval
        bool scene_change_detection = true; ///< Insert keyframe on scene change
        size_t adaptive_window = 30;      ///< Frames for adaptation window
    };

    explicit KeyframeIntervalAdapter(const Config& cfg = Config{})
        : cfg_(cfg), current_interval_(cfg.default_interval),
          frames_since_keyframe_(0)
    {
        spdlog::info("[KeyframeIntervalAdapter] Initialized: interval={} frames",
                     current_interval_);
    }

    /// Update with current network conditions
    void update_network_conditions(double loss_rate, std::chrono::milliseconds rtt) noexcept {
        std::unique_lock lock(mutex_);

        // Base interval from loss rate
        double loss_factor = 1.0;
        if (loss_rate > 0.0) {
            loss_factor = std::max(0.2, 1.0 - loss_rate * cfg_.loss_scaling_factor * 10.0);
        }

        // RTT influence: longer RTT means more conservative (shorter) intervals
        double rtt_factor = 1.0;
        if (rtt > std::chrono::milliseconds(50)) {
            double rtt_ratio = static_cast<double>(rtt.count()) / 50.0;
            rtt_factor = std::max(0.3, 1.0 / std::sqrt(rtt_ratio));
        }

        double target_interval = cfg_.default_interval * loss_factor * rtt_factor;
        target_interval = detail::clamp(target_interval,
                                        static_cast<double>(cfg_.min_interval),
                                        static_cast<double>(cfg_.max_interval));

        // Smooth transitions
        double alpha = 0.3;
        double new_interval = alpha * target_interval +
                             (1.0 - alpha) * static_cast<double>(current_interval_);
        current_interval_ = static_cast<uint32_t>(std::round(new_interval));
    }

    /// Called every frame to track keyframe spacing
    void on_frame(bool is_keyframe, bool scene_change = false) noexcept {
        std::unique_lock lock(mutex_);

        if (is_keyframe) {
            frames_since_keyframe_ = 0;
        } else {
            ++frames_since_keyframe_;
        }

        if (scene_change && cfg_.scene_change_detection) {
            force_keyframe_ = true;
        }
    }

    /// Should we insert a keyframe now?
    [[nodiscard]] bool should_insert_keyframe() noexcept {
        std::unique_lock lock(mutex_);

        if (force_keyframe_) {
            force_keyframe_ = false;
            return true;
        }

        return frames_since_keyframe_ >= current_interval_;
    }

    /// Get current keyframe interval
    [[nodiscard]] uint32_t current_interval() const noexcept {
        return current_interval_;
    }

    /// Get frames since last keyframe
    [[nodiscard]] uint32_t frames_since_keyframe() const noexcept {
        return frames_since_keyframe_;
    }

    /// Force next frame to be a keyframe
    void force_keyframe() noexcept {
        force_keyframe_ = true;
    }

    /// Reset state
    void reset() noexcept {
        std::unique_lock lock(mutex_);
        current_interval_ = cfg_.default_interval;
        frames_since_keyframe_ = 0;
        force_keyframe_ = false;
    }

private:
    Config cfg_;
    mutable std::mutex mutex_;
    uint32_t current_interval_;
    uint32_t frames_since_keyframe_;
    bool force_keyframe_ = false;
};

// =============================================================================
// 7. Packet Loss Recovery (FEC + NACK-based Retransmission)
// =============================================================================

class PacketLossRecovery {
public:
    struct Config {
        RecoveryMode mode = RecoveryMode::HYBRID;
        FECLevel fec_level = FECLevel::MEDIUM;
        double fec_overhead_pct = 10.0;       ///< FEC overhead percentage
        size_t fec_group_size = 8;            ///< Packets per FEC group
        size_t fec_redundant_packets = 2;     ///< Redundant packets per group
        std::chrono::milliseconds nack_timeout = std::chrono::milliseconds(30);
        size_t max_nack_retransmissions = 3;
        std::chrono::milliseconds retransmission_timeout = std::chrono::milliseconds(100);
        size_t max_retransmit_queue = 128;
    };

    explicit PacketLossRecovery(const Config& cfg = Config{})
        : cfg_(cfg), fec_level_(cfg.fec_level), recovery_mode_(cfg.mode)
    {
        update_fec_params();
        spdlog::info("[PacketLossRecovery] Initialized: mode={}, FEC={}, overhead={:.1f}%",
                     recovery_mode_name(), fec_level_name(), cfg_.fec_overhead_pct);
    }

    // ---- FEC Operations ----------------------------------------------------

    /// Set FEC level
    void set_fec_level(FECLevel level) noexcept {
        std::unique_lock lock(mutex_);
        fec_level_ = level;
        update_fec_params();
        spdlog::debug("[FEC] Level changed to {}", fec_level_name());
    }

    /// Set recovery mode
    void set_recovery_mode(RecoveryMode mode) noexcept {
        std::unique_lock lock(mutex_);
        recovery_mode_ = mode;
        spdlog::debug("[Recovery] Mode changed to {}", recovery_mode_name());
    }

    /// Get recommended FEC parameters based on current loss rate
    [[nodiscard]] std::pair<size_t, size_t> fec_parameters(double loss_rate) const noexcept {
        if (recovery_mode_ == RecoveryMode::NONE || recovery_mode_ == RecoveryMode::NACK_ONLY) {
            return {0, 0};
        }

        size_t group_size = current_fec_group_size_;
        size_t redundant;

        // Scale FEC strength with loss rate
        if (loss_rate < 0.01) {
            redundant = std::max(size_t{1}, group_size / 8);
        } else if (loss_rate < 0.05) {
            redundant = std::max(size_t{1}, group_size / 4);
        } else if (loss_rate < 0.10) {
            redundant = std::max(size_t{2}, group_size / 3);
        } else {
            redundant = std::max(size_t{2}, group_size / 2);
        }

        return {group_size, redundant};
    }

    /// Get current FEC overhead ratio
    [[nodiscard]] double fec_overhead() const noexcept {
        if (current_fec_group_size_ == 0) return 0.0;
        return static_cast<double>(current_fec_redundant_) /
               static_cast<double>(current_fec_group_size_);
    }

    // ---- NACK Operations ---------------------------------------------------

    /// Register a missing packet for NACK-based recovery
    void register_missing_packet(uint64_t packet_id, uint64_t frame_id,
                                  std::chrono::steady_clock::time_point detected_at) noexcept
    {
        if (recovery_mode_ != RecoveryMode::NACK_ONLY &&
            recovery_mode_ != RecoveryMode::HYBRID) {
            return;
        }

        std::unique_lock lock(mutex_);

        NACKEntry entry;
        entry.packet_id = packet_id;
        entry.frame_id = frame_id;
        entry.first_detected = detected_at;
        entry.last_nack_sent = std::chrono::steady_clock::time_point{};
        entry.retransmission_count = 0;
        entry.resolved = false;

        pending_nacks_[packet_id] = entry;

        // Evict oldest if queue is full
        if (pending_nacks_.size() > cfg_.max_retransmit_queue) {
            auto oldest = pending_nacks_.begin();
            for (auto it = pending_nacks_.begin(); it != pending_nacks_.end(); ++it) {
                if (it->second.first_detected < oldest->second.first_detected) {
                    oldest = it;
                }
            }
            pending_nacks_.erase(oldest);
        }
    }

    /// Get list of packets that need a NACK sent (timeout-based)
    [[nodiscard]] std::vector<uint64_t> get_pending_nacks() noexcept {
        std::unique_lock lock(mutex_);
        std::vector<uint64_t> result;
        auto now = Clock::now();

        for (auto& [packet_id, entry] : pending_nacks_) {
            if (entry.resolved) continue;
            if (entry.retransmission_count >= cfg_.max_nack_retransmissions) {
                entry.resolved = true; // Give up
                continue;
            }

            auto time_since_last = entry.last_nack_sent.time_since_epoch().count() > 0
                ? now - entry.last_nack_sent
                : now - entry.first_detected;

            auto timeout = entry.retransmission_count == 0
                ? cfg_.nack_timeout
                : cfg_.retransmission_timeout;

            if (time_since_last >= timeout) {
                result.push_back(packet_id);
                entry.last_nack_sent = now;
                ++entry.retransmission_count;
                ++total_nacks_sent_;
            }
        }

        return result;
    }

    /// Mark a packet as recovered (retransmission received)
    void mark_recovered(uint64_t packet_id) noexcept {
        std::unique_lock lock(mutex_);
        auto it = pending_nacks_.find(packet_id);
        if (it != pending_nacks_.end()) {
            it->second.resolved = true;
            ++successful_recoveries_;
        }
    }

    /// Mark as irrecoverable (frame deadline passed)
    void mark_irrecoverable(uint64_t frame_id) noexcept {
        std::unique_lock lock(mutex_);
        for (auto& [packet_id, entry] : pending_nacks_) {
            if (entry.frame_id == frame_id && !entry.resolved) {
                entry.resolved = true;
                ++failed_recoveries_;
            }
        }
    }

    /// Clean up resolved entries
    void cleanup() noexcept {
        std::unique_lock lock(mutex_);
        for (auto it = pending_nacks_.begin(); it != pending_nacks_.end();) {
            if (it->second.resolved) {
                it = pending_nacks_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // ---- Statistics --------------------------------------------------------

    /// Get total NACKs sent
    [[nodiscard]] uint64_t total_nacks_sent() const noexcept {
        return total_nacks_sent_;
    }

    /// Get successful recoveries
    [[nodiscard]] uint64_t successful_recoveries() const noexcept {
        return successful_recoveries_;
    }

    /// Get failed recoveries
    [[nodiscard]] uint64_t failed_recoveries() const noexcept {
        return failed_recoveries_;
    }

    /// Get recovery success rate
    [[nodiscard]] double recovery_success_rate() const noexcept {
        uint64_t total = successful_recoveries_ + failed_recoveries_;
        if (total == 0) return 1.0;
        return static_cast<double>(successful_recoveries_) / static_cast<double>(total);
    }

    /// Get pending NACK count
    [[nodiscard]] size_t pending_nack_count() const noexcept {
        std::shared_lock lock(mutex_);
        return pending_nacks_.size();
    }

    /// Get FEC level as string
    [[nodiscard]] std::string_view fec_level_name() const noexcept {
        constexpr std::array names = {"OFF", "LOW", "MEDIUM", "HIGH", "MAXIMUM"};
        return names[static_cast<size_t>(fec_level_.load())];
    }

    /// Get recovery mode as string
    [[nodiscard]] std::string_view recovery_mode_name() const noexcept {
        constexpr std::array names = {"NONE", "NACK_ONLY", "FEC_ONLY", "HYBRID"};
        return names[static_cast<size_t>(recovery_mode_.load())];
    }

    /// Reset all state
    void reset() noexcept {
        std::unique_lock lock(mutex_);
        pending_nacks_.clear();
        total_nacks_sent_ = 0;
        successful_recoveries_ = 0;
        failed_recoveries_ = 0;
    }

private:
    using Clock = std::chrono::steady_clock;

    struct NACKEntry {
        uint64_t packet_id;
        uint64_t frame_id;
        Clock::time_point first_detected;
        Clock::time_point last_nack_sent;
        size_t retransmission_count;
        bool resolved;
    };

    void update_fec_params() noexcept {
        FECLevel level = fec_level_.load();
        switch (level) {
        case FECLevel::OFF:
            current_fec_group_size_ = 0;
            current_fec_redundant_ = 0;
            break;
        case FECLevel::LOW:
            current_fec_group_size_ = 8;
            current_fec_redundant_ = 1;
            break;
        case FECLevel::MEDIUM:
            current_fec_group_size_ = 8;
            current_fec_redundant_ = 2;
            break;
        case FECLevel::HIGH:
            current_fec_group_size_ = 6;
            current_fec_redundant_ = 2;
            break;
        case FECLevel::MAXIMUM:
            current_fec_group_size_ = 4;
            current_fec_redundant_ = 2;
            break;
        }
    }

    Config cfg_;
    mutable std::shared_mutex mutex_;

    std::atomic<FECLevel> fec_level_{FECLevel::MEDIUM};
    std::atomic<RecoveryMode> recovery_mode_{RecoveryMode::HYBRID};
    size_t current_fec_group_size_ = 8;
    size_t current_fec_redundant_ = 2;

    std::unordered_map<uint64_t, NACKEntry> pending_nacks_;
    uint64_t total_nacks_sent_ = 0;
    uint64_t successful_recoveries_ = 0;
    uint64_t failed_recoveries_ = 0;
};

// =============================================================================
// 8. Jitter Buffer Manager
// =============================================================================

class JitterBufferManager {
public:
    struct Config {
        std::chrono::milliseconds target_delay = TimingConstants::kJitterBufferTarget;
        std::chrono::milliseconds min_delay = std::chrono::milliseconds(10);
        std::chrono::milliseconds max_delay = std::chrono::milliseconds(500);
        size_t max_frames = 32;
        double adaptation_rate = 0.1;       ///< How quickly the buffer adapts
        double late_discard_threshold_pct = 90.0; ///< Discard if buffer exceeds this %
        bool adaptive_mode = true;
    };

    /// Represents a buffered frame
    struct BufferedFrame {
        uint64_t frame_id;
        std::chrono::steady_clock::time_point arrival_time;
        std::chrono::steady_clock::time_point expected_presentation;
        bool complete = false;
        bool presented = false;
    };

    explicit JitterBufferManager(const Config& cfg = Config{})
        : cfg_(cfg), current_target_delay_(cfg.target_delay),
          jitter_ewma_(0.15, 0.0)
    {
        spdlog::info("[JitterBuffer] Initialized: target={}ms, max_frames={}",
                     current_target_delay_.count(), cfg.max_frames);
    }

    /// Add a frame (or frame fragment) to the buffer
    [[nodiscard]] bool push_frame(uint64_t frame_id,
                                   std::chrono::steady_clock::time_point arrival,
                                   bool complete) noexcept
    {
        std::unique_lock lock(mutex_);

        // Check for duplicate
        for (const auto& f : frame_buffer_) {
            if (f.frame_id == frame_id) {
                return false; // Already buffered
            }
        }

        // Discard oldest if buffer full
        if (frame_buffer_.size() >= cfg_.max_frames) {
            auto& oldest = frame_buffer_.front();
            if (!oldest.presented) {
                ++discarded_frames_;
            }
            frame_buffer_.pop_front();
        }

        BufferedFrame bf;
        bf.frame_id = frame_id;
        bf.arrival_time = arrival;
        bf.complete = complete;
        bf.expected_presentation = arrival + current_target_delay_;

        // Update jitter estimate
        if (!frame_buffer_.empty()) {
            auto& last = frame_buffer_.back();
            double inter_arrival = std::chrono::duration<double, std::milli>(
                arrival - last.arrival_time).count();
            double expected = std::chrono::duration<double, std::milli>(
                last.expected_presentation - last.arrival_time +
                current_target_delay_).count();

            if (last_inter_arrival_ms_ > 0.0) {
                double jitter = std::abs(inter_arrival - last_inter_arrival_ms_);
                jitter_ewma_.update(jitter);
            }
            last_inter_arrival_ms_ = inter_arrival;
        }

        // Insert in frame_id order
        auto pos = std::lower_bound(frame_buffer_.begin(), frame_buffer_.end(), bf,
            [](const BufferedFrame& a, const BufferedFrame& b) {
                return a.frame_id < b.frame_id;
            });
        frame_buffer_.insert(pos, bf);

        if (cfg_.adaptive_mode) {
            adapt_target_delay();
        }

        return true;
    }

    /// Pop the next frame ready for presentation
    [[nodiscard]] std::optional<BufferedFrame> pop_ready_frame() noexcept {
        std::unique_lock lock(mutex_);

        if (frame_buffer_.empty()) {
            ++underrun_count_;
            return std::nullopt;
        }

        auto now = Clock::now();
        auto& front = frame_buffer_.front();

        if (!front.complete) {
            // Frame incomplete, but if too old, discard
            if (now > front.expected_presentation + cfg_.max_delay) {
                ++discarded_frames_;
                frame_buffer_.pop_front();
                return std::nullopt;
            }
            return std::nullopt;
        }

        if (now >= front.expected_presentation) {
            auto frame = front;
            frame.presented = true;
            frame_buffer_.pop_front();

            // Track presentation jitter
            auto lateness = std::chrono::duration<double, std::milli>(
                now - frame.expected_presentation);
            presentation_jitter_.push(static_cast<double>(lateness.count()));

            return frame;
        }

        return std::nullopt;
    }

    /// Get the current buffer occupancy (number of frames)
    [[nodiscard]] size_t occupancy() const noexcept {
        std::shared_lock lock(mutex_);
        return frame_buffer_.size();
    }

    /// Get current target delay
    [[nodiscard]] std::chrono::milliseconds target_delay() const noexcept {
        return current_target_delay_;
    }

    /// Get estimated network jitter in milliseconds
    [[nodiscard]] double estimated_jitter_ms() const noexcept {
        return jitter_ewma_.value();
    }

    /// Get underrun count
    [[nodiscard]] uint64_t underrun_count() const noexcept {
        return underrun_count_;
    }

    /// Get discarded frame count
    [[nodiscard]] uint64_t discarded_frames() const noexcept {
        return discarded_frames_;
    }

    /// Get presentation jitter statistics
    [[nodiscard]] double mean_presentation_jitter_ms() const noexcept {
        return presentation_jitter_.mean();
    }

    /// Get late frame percentage
    [[nodiscard]] double late_frame_pct() const noexcept {
        if (presentation_jitter_.count() == 0) return 0.0;
        size_t late = 0;
        for (size_t i = 0; i < presentation_jitter_.count(); ++i) {
            // We approximate by checking if mean is positive
        }
        return 0.0; // Simplified; real implementation would track actual late count
    }

    /// Clear the buffer
    void flush() noexcept {
        std::unique_lock lock(mutex_);
        frame_buffer_.clear();
    }

    /// Reset statistics
    void reset() noexcept {
        std::unique_lock lock(mutex_);
        frame_buffer_.clear();
        underrun_count_ = 0;
        discarded_frames_ = 0;
        jitter_ewma_.reset(0.0);
        last_inter_arrival_ms_ = 0.0;
        presentation_jitter_.clear();
        current_target_delay_ = cfg_.target_delay;
    }

private:
    using Clock = std::chrono::steady_clock;

    void adapt_target_delay() noexcept {
        double jitter = jitter_ewma_.value();
        double new_target = std::max(static_cast<double>(cfg_.min_delay.count()),
                                     jitter * 3.0); // 3x jitter as buffer

        new_target = detail::clamp(new_target,
                                   static_cast<double>(cfg_.min_delay.count()),
                                   static_cast<double>(cfg_.max_delay.count()));

        double current = static_cast<double>(current_target_delay_.count());
        double adapted = current + cfg_.adaptation_rate * (new_target - current);

        current_target_delay_ = std::chrono::milliseconds(static_cast<int64_t>(adapted));
    }

    Config cfg_;
    mutable std::shared_mutex mutex_;

    std::deque<BufferedFrame> frame_buffer_;
    std::chrono::milliseconds current_target_delay_;
    detail::EWMA jitter_ewma_;
    double last_inter_arrival_ms_ = 0.0;
    uint64_t underrun_count_ = 0;
    uint64_t discarded_frames_ = 0;
    detail::SlidingWindow<double, WindowSizes::kShortWindow> presentation_jitter_;
};

// =============================================================================
// 9. Network Condition Scorer (0-100)
// =============================================================================

class NetworkConditionScorer {
public:
    struct Config {
        double bandwidth_weight = 0.30;
        double latency_weight = 0.25;
        double jitter_weight = 0.20;
        double loss_weight = 0.25;

        // Thresholds for excellent/good/fair/poor/critical
        double excellent_threshold = 80.0;
        double good_threshold = 60.0;
        double fair_threshold = 40.0;
        double poor_threshold = 20.0;

        double ideal_bandwidth_mbps = 50.0;  ///< Bandwidth considered "perfect"
        double ideal_latency_ms = 10.0;      ///< Latency considered "perfect"
        double ideal_jitter_ms = 2.0;        ///< Jitter considered "perfect"
        double ideal_loss_pct = 0.0;         ///< Loss considered "perfect"

        size_t score_history_size = 30;      ///< Scores to keep for trending
    };

    /// Comprehensive network metrics snapshot
    struct NetworkMetrics {
        double bandwidth_bps = 0.0;
        std::chrono::milliseconds rtt{50};
        double jitter_ms = 0.0;
        double loss_rate = 0.0;
        double bandwidth_utilization = 0.0;
        size_t concurrent_streams = 1;
        std::chrono::steady_clock::time_point timestamp;
    };

    explicit NetworkConditionScorer(const Config& cfg = Config{})
        : cfg_(cfg), current_score_(75.0)
    {
        spdlog::info("[NetworkScorer] Initialized");
    }

    /// Compute and update the network score based on metrics
    [[nodiscard]] double evaluate(const NetworkMetrics& metrics) noexcept {
        std::unique_lock lock(mutex_);

        // Individual component scores (0-100)
        double bandwidth_score = score_bandwidth(metrics.bandwidth_bps);
        double latency_score = score_latency(metrics.rtt);
        double jitter_score = score_jitter(metrics.jitter_ms);
        double loss_score = score_loss(metrics.loss_rate);

        // Weighted composite score
        double composite = bandwidth_score * cfg_.bandwidth_weight +
                          latency_score * cfg_.latency_weight +
                          jitter_score * cfg_.jitter_weight +
                          loss_score * cfg_.loss_weight;

        // Penalize high bandwidth utilization
        if (metrics.bandwidth_utilization > 0.9) {
            double penalty = (metrics.bandwidth_utilization - 0.9) * 200.0;
            composite -= penalty;
        }

        // Penalize many concurrent streams
        if (metrics.concurrent_streams > 3) {
            double stream_penalty = (metrics.concurrent_streams - 3) * 5.0;
            composite -= stream_penalty;
        }

        composite = detail::clamp(composite, 0.0, 100.0);

        // Smooth the score
        double alpha = 0.2;
        current_score_ = alpha * composite + (1.0 - alpha) * current_score_;

        // Store in history
        score_history_.push_back(current_score_);
        if (score_history_.size() > cfg_.score_history_size) {
            score_history_.pop_front();
        }

        last_metrics_ = metrics;
        return current_score_;
    }

    /// Get the current smoothed score (0-100)
    [[nodiscard]] double current_score() const noexcept { return current_score_; }

    /// Get the network condition category
    [[nodiscard]] NetworkCondition condition() const noexcept {
        if (current_score_ >= cfg_.excellent_threshold) return NetworkCondition::EXCELLENT;
        if (current_score_ >= cfg_.good_threshold)      return NetworkCondition::GOOD;
        if (current_score_ >= cfg_.fair_threshold)      return NetworkCondition::FAIR;
        if (current_score_ >= cfg_.poor_threshold)      return NetworkCondition::POOR;
        return NetworkCondition::CRITICAL;
    }

    /// Get condition as a human-readable string
    [[nodiscard]] std::string_view condition_name() const noexcept {
        return detail::condition_name(condition());
    }

    /// Get score trend (positive = improving, negative = degrading)
    [[nodiscard]] double score_trend() const noexcept {
        if (score_history_.size() < 10) return 0.0;
        // Compare average of last 5 vs previous 5
        size_t n = score_history_.size();
        double recent = 0.0, older = 0.0;
        for (size_t i = 0; i < 5 && n - 1 - i < n; ++i) {
            recent += score_history_[n - 1 - i];
        }
        for (size_t i = 0; i < 5 && n - 6 - i < n; ++i) {
            older += score_history_[n - 6 - i];
        }
        return (recent - older) / 5.0;
    }

    /// Get individual component scores
    [[nodiscard]] std::tuple<double, double, double, double> component_scores(
        const NetworkMetrics& metrics) const noexcept
    {
        return {
            score_bandwidth(metrics.bandwidth_bps),
            score_latency(metrics.rtt),
            score_jitter(metrics.jitter_ms),
            score_loss(metrics.loss_rate)
        };
    }

    /// Get last metrics used for scoring
    [[nodiscard]] NetworkMetrics last_metrics() const noexcept {
        std::shared_lock lock(mutex_);
        return last_metrics_;
    }

    /// Reset scorer
    void reset() noexcept {
        std::unique_lock lock(mutex_);
        current_score_ = 75.0;
        score_history_.clear();
    }

private:
    static double score_bandwidth(double bps) noexcept {
        double mbps = bps / 1'000'000.0;
        if (mbps >= 50.0) return 100.0;
        if (mbps <= 0.5) return 0.0;
        // Logarithmic scale: 5 Mbps = 50, 25 Mbps = 85
        return 100.0 * std::log2(1.0 + mbps) / std::log2(51.0);
    }

    static double score_latency(std::chrono::milliseconds rtt) noexcept {
        double ms = static_cast<double>(rtt.count());
        if (ms <= 10.0) return 100.0;
        if (ms >= 300.0) return 0.0;
        // Exponential decay
        return 100.0 * std::exp(-ms / 80.0);
    }

    static double score_jitter(double jitter_ms) noexcept {
        if (jitter_ms <= 2.0) return 100.0;
        if (jitter_ms >= 50.0) return 0.0;
        return 100.0 * std::exp(-jitter_ms / 15.0);
    }

    static double score_loss(double loss_rate) noexcept {
        if (loss_rate <= 0.001) return 100.0;
        if (loss_rate >= 0.20) return 0.0;
        // Linear in log space
        return 100.0 * (1.0 - std::log10(1.0 + loss_rate * 1000.0) / 3.0);
    }

    Config cfg_;
    mutable std::shared_mutex mutex_;
    double current_score_;
    std::deque<double> score_history_;
    NetworkMetrics last_metrics_;
};

// =============================================================================
// 10. Client Feedback Processor
// =============================================================================

class ClientFeedbackProcessor {
public:
    /// Statistics reported by the client
    struct ClientStats {
        uint64_t frames_received = 0;
        uint64_t frames_decoded = 0;
        uint64_t frames_rendered = 0;
        uint64_t frames_dropped = 0;
        uint64_t packets_received = 0;
        uint64_t packets_lost = 0;
        uint64_t packets_recovered = 0;
        uint64_t nacks_sent = 0;
        double reported_bandwidth_bps = 0.0;
        std::chrono::milliseconds reported_rtt{0};
        double reported_jitter_ms = 0.0;
        double decoder_queue_ms = 0.0;
        double render_queue_ms = 0.0;
        double e2e_latency_ms = 0.0;
        ResolutionTier current_resolution = ResolutionTier::RES_1080P;
        double current_fps = 0.0;
        double mos_score = 0.0;        ///< Client-reported MOS
        uint32_t freeze_events = 0;
        double freeze_duration_ms = 0.0;
        std::chrono::steady_clock::time_point timestamp;
        std::string client_version;
        std::map<std::string, double> custom_metrics;
    };

    /// Request from client
    struct ClientRequest {
        enum class Type : uint8_t {
            RESOLUTION_CHANGE,
            FPS_CHANGE,
            BITRATE_CHANGE,
            KEYFRAME_REQUEST,
            QUALITY_PRESET_CHANGE,
            FEC_LEVEL_CHANGE,
            STREAM_PAUSE,
            STREAM_RESUME,
            CUSTOM
        };

        Type type;
        std::optional<ResolutionTier> target_resolution;
        std::optional<double> target_fps;
        std::optional<double> target_bitrate;
        std::optional<QualityPreset> target_preset;
        std::optional<FECLevel> target_fec;
        std::optional<uint32_t> stream_id;
        std::string custom_data;
        std::chrono::steady_clock::time_point timestamp;
    };

    struct Config {
        std::chrono::milliseconds stats_timeout = std::chrono::milliseconds(5000);
        std::chrono::milliseconds request_timeout = std::chrono::milliseconds(3000);
        size_t max_queued_requests = 64;
        bool auto_acknowledge = true;
    };

    explicit ClientFeedbackProcessor(const Config& cfg = Config{})
        : cfg_(cfg)
    {
        spdlog::info("[ClientFeedbackProcessor] Initialized");
    }

    /// Process incoming client statistics
    void process_stats(uint32_t client_id, ClientStats stats) noexcept {
        std::unique_lock lock(mutex_);

        auto& entry = client_data_[client_id];
        entry.latest_stats = std::move(stats);
        entry.last_stats_update = Clock::now();
        entry.stats_history.push_back(entry.latest_stats);

        // Keep limited history
        if (entry.stats_history.size() > 20) {
            entry.stats_history.pop_front();
        }

        spdlog::trace("[Feedback] Client {} stats: frames={}, loss={}, rtt={}ms, mos={:.1f}",
                      client_id, entry.latest_stats.frames_received,
                      entry.latest_stats.packets_lost,
                      entry.latest_stats.reported_rtt.count(),
                      entry.latest_stats.mos_score);
    }

    /// Process incoming client request
    void process_request(uint32_t client_id, ClientRequest request) noexcept {
        std::unique_lock lock(mutex_);

        auto& entry = client_data_[client_id];
        entry.pending_requests.push_back(std::move(request));

        if (entry.pending_requests.size() > cfg_.max_queued_requests) {
            entry.pending_requests.pop_front();
        }

        spdlog::debug("[Feedback] Client {} request type={}",
                      client_id, static_cast<int>(request.type));
    }

    /// Get pending requests for a client (clears them from queue)
    [[nodiscard]] std::vector<ClientRequest> consume_requests(uint32_t client_id) noexcept {
        std::unique_lock lock(mutex_);
        auto it = client_data_.find(client_id);
        if (it == client_data_.end()) return {};

        std::vector<ClientRequest> result;
        result.assign(
            std::make_move_iterator(it->second.pending_requests.begin()),
            std::make_move_iterator(it->second.pending_requests.end()));
        it->second.pending_requests.clear();
        return result;
    }

    /// Get latest stats for a client
    [[nodiscard]] std::optional<ClientStats> latest_stats(uint32_t client_id) const noexcept {
        std::shared_lock lock(mutex_);
        auto it = client_data_.find(client_id);
        if (it == client_data_.end()) return std::nullopt;
        return it->second.latest_stats;
    }

    /// Compute aggregate stats across all clients
    [[nodiscard]] ClientStats aggregate_stats() const noexcept {
        std::shared_lock lock(mutex_);
        ClientStats agg{};

        if (client_data_.empty()) return agg;

        for (const auto& [id, entry] : client_data_) {
            const auto& s = entry.latest_stats;
            agg.frames_received += s.frames_received;
            agg.frames_decoded += s.frames_decoded;
            agg.frames_rendered += s.frames_rendered;
            agg.frames_dropped += s.frames_dropped;
            agg.packets_received += s.packets_received;
            agg.packets_lost += s.packets_lost;
            agg.packets_recovered += s.packets_recovered;
            agg.nacks_sent += s.nacks_sent;
        }

        // Average the continuous metrics
        size_t count = client_data_.size();
        if (count > 0) {
            double total_bandwidth = 0.0, total_rtt = 0.0, total_jitter = 0.0;
            double total_mos = 0.0;
            for (const auto& [id, entry] : client_data_) {
                total_bandwidth += entry.latest_stats.reported_bandwidth_bps;
                total_rtt += entry.latest_stats.reported_rtt.count();
                total_jitter += entry.latest_stats.reported_jitter_ms;
                total_mos += entry.latest_stats.mos_score;
            }
            agg.reported_bandwidth_bps = total_bandwidth / count;
            agg.reported_rtt = std::chrono::milliseconds(static_cast<int64_t>(total_rtt / count));
            agg.reported_jitter_ms = total_jitter / count;
            agg.mos_score = total_mos / count;
        }

        agg.timestamp = Clock::now();
        return agg;
    }

    /// Get client count
    [[nodiscard]] size_t client_count() const noexcept {
        std::shared_lock lock(mutex_);
        return client_data_.size();
    }

    /// Remove a disconnected client
    void remove_client(uint32_t client_id) noexcept {
        std::unique_lock lock(mutex_);
        client_data_.erase(client_id);
        spdlog::info("[Feedback] Client {} removed", client_id);
    }

    /// Check if a client has reported recently (alive check)
    [[nodiscard]] bool is_client_alive(uint32_t client_id) const noexcept {
        std::shared_lock lock(mutex_);
        auto it = client_data_.find(client_id);
        if (it == client_data_.end()) return false;
        return (Clock::now() - it->second.last_stats_update) < cfg_.stats_timeout;
    }

    /// Get all alive client IDs
    [[nodiscard]] std::vector<uint32_t> alive_clients() const noexcept {
        std::shared_lock lock(mutex_);
        std::vector<uint32_t> clients;
        auto now = Clock::now();
        for (const auto& [id, entry] : client_data_) {
            if (now - entry.last_stats_update < cfg_.stats_timeout) {
                clients.push_back(id);
            }
        }
        return clients;
    }

    /// Reset processor
    void reset() noexcept {
        std::unique_lock lock(mutex_);
        client_data_.clear();
    }

private:
    using Clock = std::chrono::steady_clock;

    struct ClientEntry {
        ClientStats latest_stats;
        Clock::time_point last_stats_update{Clock::now()};
        std::deque<ClientStats> stats_history;
        std::deque<ClientRequest> pending_requests;
    };

    Config cfg_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<uint32_t, ClientEntry> client_data_;
};

// =============================================================================
// 11. Multi-Stream Prioritization
// =============================================================================

class MultiStreamPrioritizer {
public:
    struct StreamInfo {
        uint32_t stream_id;
        StreamPriority priority = StreamPriority::NORMAL;
        double required_bandwidth_bps = 5'000'000.0;
        double min_bandwidth_bps = 500'000.0;
        double current_allocated_bps = 0.0;
        ResolutionTier current_resolution = ResolutionTier::RES_1080P;
        double current_fps = 30.0;
        bool active = true;
        bool paused = false;
        std::string label;
        std::chrono::steady_clock::time_point last_activity;
    };

    struct Allocation {
        uint32_t stream_id;
        double allocated_bandwidth_bps;
        ResolutionTier recommended_resolution;
        double recommended_fps;
        bool paused;
    };

    struct Config {
        double critical_reserve_pct = 15.0;   ///< % bandwidth reserved for critical streams
        double headroom_pct = 10.0;           ///< % bandwidth left as headroom
        size_t max_streams = 16;
    };

    explicit MultiStreamPrioritizer(const Config& cfg = Config{})
        : cfg_(cfg)
    {
        spdlog::info("[MultiStreamPrioritizer] Initialized: max_streams={}", cfg.max_streams);
    }

    /// Register a stream
    bool register_stream(uint32_t stream_id, StreamPriority priority,
                         double required_bps, double min_bps = 500'000.0,
                         std::string label = "") noexcept
    {
        std::unique_lock lock(mutex_);

        if (streams_.size() >= cfg_.max_streams) {
            spdlog::warn("[Prioritizer] Max streams reached, cannot add {}", stream_id);
            return false;
        }

        StreamInfo info;
        info.stream_id = stream_id;
        info.priority = priority;
        info.required_bandwidth_bps = required_bps;
        info.min_bandwidth_bps = min_bps;
        info.current_allocated_bps = required_bps;
        info.last_activity = Clock::now();
        info.label = std::move(label);

        streams_[stream_id] = info;
        spdlog::info("[Prioritizer] Stream {} registered (priority={})",
                     stream_id, static_cast<int>(priority));
        return true;
    }

    /// Update stream requirements
    void update_stream(uint32_t stream_id, double required_bps,
                       bool active = true) noexcept
    {
        std::unique_lock lock(mutex_);
        auto it = streams_.find(stream_id);
        if (it == streams_.end()) return;

        it->second.required_bandwidth_bps = required_bps;
        it->second.active = active;
        it->second.last_activity = Clock::now();
    }

    /// Remove a stream
    void remove_stream(uint32_t stream_id) noexcept {
        std::unique_lock lock(mutex_);
        streams_.erase(stream_id);
        spdlog::info("[Prioritizer] Stream {} removed", stream_id);
    }

    /// Compute bandwidth allocations across all streams
    [[nodiscard]] std::vector<Allocation> allocate(double total_available_bps) noexcept {
        std::unique_lock lock(mutex_);

        std::vector<Allocation> allocations;

        if (streams_.empty()) return allocations;

        // Sort streams by priority (highest first)
        std::vector<StreamInfo*> sorted;
        for (auto& [id, info] : streams_) {
            if (info.active) sorted.push_back(&info);
        }
        std::sort(sorted.begin(), sorted.end(),
            [](const StreamInfo* a, const StreamInfo* b) {
                return static_cast<int>(a->priority) < static_cast<int>(b->priority);
            });

        double remaining = total_available_bps * (1.0 - cfg_.headroom_pct / 100.0);
        double critical_reserve = total_available_bps * cfg_.critical_reserve_pct / 100.0;

        // Phase 1: Allocate minimum bandwidth to all active streams
        for (auto* stream : sorted) {
            double min_alloc = stream->min_bandwidth_bps;
            if (min_alloc <= remaining) {
                remaining -= min_alloc;
                stream->current_allocated_bps = min_alloc;
            } else if (stream->priority == StreamPriority::CRITICAL) {
                // Critical streams get whatever is left (including reserve)
                stream->current_allocated_bps = remaining;
                remaining = 0.0;
            } else {
                // Can't even meet minimum, pause this stream
                stream->current_allocated_bps = 0.0;
                stream->paused = true;
            }
        }

        // Phase 2: Distribute remaining bandwidth proportionally by priority
        if (remaining > critical_reserve) {
            double distributable = remaining - critical_reserve;

            // Calculate total weight (higher priority = more weight)
            double total_weight = 0.0;
            for (auto* stream : sorted) {
                if (stream->paused) continue;
                double weight = priority_weight(stream->priority) *
                               (stream->required_bandwidth_bps - stream->current_allocated_bps);
                total_weight += weight;
            }

            if (total_weight > 0.0) {
                for (auto* stream : sorted) {
                    if (stream->paused) continue;
                    double need = stream->required_bandwidth_bps - stream->current_allocated_bps;
                    double weight = priority_weight(stream->priority) * need;
                    double extra = (weight / total_weight) * distributable;
                    extra = std::min(extra, need);
                    stream->current_allocated_bps += extra;
                    remaining -= extra;
                }
            }
        }

        // Phase 3: Un-pause streams if bandwidth is sufficient
        for (auto* stream : sorted) {
            if (stream->paused && stream->min_bandwidth_bps <= remaining) {
                stream->current_allocated_bps = stream->min_bandwidth_bps;
                stream->paused = false;
                remaining -= stream->min_bandwidth_bps;
            }
        }

        // Build allocation results
        for (auto* stream : sorted) {
            Allocation alloc;
            alloc.stream_id = stream->stream_id;
            alloc.allocated_bandwidth_bps = stream->current_allocated_bps;
            alloc.paused = stream->paused;

            // Recommend resolution based on allocation
            alloc.recommended_resolution =
                recommend_resolution(stream->current_allocated_bps);

            // Recommend FPS (scaled down if bandwidth is tight)
            double ratio = stream->current_allocated_bps / stream->required_bandwidth_bps;
            alloc.recommended_fps = detail::clamp(stream->current_fps * ratio, 5.0, 60.0);

            allocations.push_back(alloc);
        }

        return allocations;
    }

    /// Get stream info
    [[nodiscard]] std::optional<StreamInfo> get_stream(uint32_t stream_id) const noexcept {
        std::shared_lock lock(mutex_);
        auto it = streams_.find(stream_id);
        if (it == streams_.end()) return std::nullopt;
        return it->second;
    }

    /// Get all stream infos
    [[nodiscard]] std::vector<StreamInfo> all_streams() const noexcept {
        std::shared_lock lock(mutex_);
        std::vector<StreamInfo> result;
        for (const auto& [id, info] : streams_) {
            result.push_back(info);
        }
        return result;
    }

    /// Get total bandwidth required by all active streams
    [[nodiscard]] double total_required_bandwidth() const noexcept {
        std::shared_lock lock(mutex_);
        double total = 0.0;
        for (const auto& [id, info] : streams_) {
            if (info.active && !info.paused) {
                total += info.required_bandwidth_bps;
            }
        }
        return total;
    }

    /// Get stream count
    [[nodiscard]] size_t stream_count() const noexcept {
        std::shared_lock lock(mutex_);
        return streams_.size();
    }

    /// Reset prioritzier
    void reset() noexcept {
        std::unique_lock lock(mutex_);
        streams_.clear();
    }

private:
    using Clock = std::chrono::steady_clock;

    static double priority_weight(StreamPriority p) noexcept {
        constexpr std::array weights = {10.0, 6.0, 3.0, 1.5, 0.5};
        return weights[static_cast<size_t>(p)];
    }

    static ResolutionTier recommend_resolution(double bandwidth_bps) noexcept {
        for (int i = 0; i < static_cast<int>(ResolutionTier::RES_COUNT); ++i) {
            auto tier = static_cast<ResolutionTier>(i);
            if (bandwidth_bps >= kDefaultBitrateTargets[i] * 0.9) {
                return tier;
            }
        }
        return ResolutionTier::RES_360P;
    }

    Config cfg_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<uint32_t, StreamInfo> streams_;
};

// =============================================================================
// 12. QoE (Quality of Experience) Metrics Calculator
// =============================================================================

class QoEMetricsCalculator {
public:
    struct QoEMetrics {
        double mos;                   ///< Mean Opinion Score (1.0 - 5.0)
        double resolution_score;      ///< Resolution component (0-100)
        double framerate_score;       ///< Framerate component (0-100)
        double bitrate_score;         ///< Bitrate component (0-100)
        double latency_score;         ///< Latency component (0-100)
        double loss_score;            ///< Packet loss component (0-100)
        double freeze_impact;         ///< Impact of freeze events on QoE
        double stability_score;       ///< How stable the quality is
        double adaptation_speed;      ///< How fast quality adapts
        NetworkCondition condition;
        std::chrono::steady_clock::time_point timestamp;
    };

    struct Config {
        std::chrono::milliseconds evaluation_interval = std::chrono::milliseconds(1000);
        double freeze_penalty_per_second = 0.5; ///< MOS penalty per second frozen
        double instability_penalty = 0.1;       ///< Penalty for frequent quality changes
        size_t stability_window = 10;            ///< Window for stability calculation
    };

    explicit QoEMetricsCalculator(const Config& cfg = Config{})
        : cfg_(cfg)
    {
        spdlog::info("[QoECalculator] Initialized");
    }

    /// Compute QoE metrics from current state
    [[nodiscard]] QoEMetrics compute(
        ResolutionTier resolution,
        double actual_fps,
        double bitrate_bps,
        std::chrono::milliseconds rtt,
        double loss_rate,
        double jitter_ms,
        double freeze_duration_sec = 0.0,
        uint32_t freeze_events = 0) noexcept
    {
        QoEMetrics metrics;
        metrics.timestamp = Clock::now();

        // Resolution score: normalize by 8K pixel count
        auto res = kResolutionLadder[static_cast<size_t>(resolution)];
        double pixels = static_cast<double>(res.width) * static_cast<double>(res.height);
        double max_pixels = 7680.0 * 4320.0;
        metrics.resolution_score = 100.0 * std::pow(pixels / max_pixels, 0.5);

        // Framerate score: 60fps = 100, 30fps = 75, 15fps = 50
        metrics.framerate_score = 100.0 * (1.0 - std::exp(-actual_fps / 25.0));

        // Bitrate score
        double mbps = bitrate_bps / 1'000'000.0;
        metrics.bitrate_score = 100.0 * (1.0 - std::exp(-mbps / 8.0));

        // Latency score
        double latency_ms = static_cast<double>(rtt.count());
        metrics.latency_score = 100.0 * std::exp(-latency_ms / 100.0);

        // Loss score
        metrics.loss_score = 100.0 * std::exp(-loss_rate * 50.0);

        // Weighted MOS calculation
        double weighted = metrics.resolution_score * QoEConstants::kResolutionWeight +
                         metrics.framerate_score * QoEConstants::kFramerateWeight +
                         metrics.bitrate_score * QoEConstants::kBitrateWeight +
                         metrics.latency_score * QoEConstants::kLatencyWeight +
                         metrics.loss_score * QoEConstants::kLossWeight;

        // Scale to MOS (1-5)
        metrics.mos = 1.0 + (weighted / 100.0) * 4.0;

        // Apply freeze penalty
        if (freeze_duration_sec > 0.0) {
            double penalty = std::min(cfg_.freeze_penalty_per_second * freeze_duration_sec, 3.0);
            metrics.mos -= penalty;
            metrics.freeze_impact = penalty;
        }

        // Apply stability penalty
        double stability = compute_stability();
        metrics.stability_score = stability;
        if (stability < 70.0) {
            metrics.mos -= cfg_.instability_penalty * (1.0 - stability / 100.0) * 4.0;
        }

        // Clamp MOS
        metrics.mos = detail::clamp(metrics.mos, QoEConstants::kMinMOS, QoEConstants::kMaxMOS);

        // Determine condition
        if (metrics.mos >= 4.3)       metrics.condition = NetworkCondition::EXCELLENT;
        else if (metrics.mos >= 3.5)  metrics.condition = NetworkCondition::GOOD;
        else if (metrics.mos >= 2.5)  metrics.condition = NetworkCondition::FAIR;
        else if (metrics.mos >= 1.5)  metrics.condition = NetworkCondition::POOR;
        else                           metrics.condition = NetworkCondition::CRITICAL;

        // Store in history
        mos_history_.push_back(metrics.mos);
        if (mos_history_.size() > cfg_.stability_window * 2) {
            mos_history_.pop_front();
        }

        metrics.adaptation_speed = compute_adaptation_speed();

        return metrics;
    }

    /// Get last computed metrics
    [[nodiscard]] QoEMetrics last_metrics() const noexcept {
        return last_metrics_;
    }

    /// Get MOS history
    [[nodiscard]] std::vector<double> mos_history() const noexcept {
        std::shared_lock lock(mutex_);
        return {mos_history_.begin(), mos_history_.end()};
    }

    /// Get average MOS over history
    [[nodiscard]] double average_mos() const noexcept {
        std::shared_lock lock(mutex_);
        if (mos_history_.empty()) return 0.0;
        double sum = std::accumulate(mos_history_.begin(), mos_history_.end(), 0.0);
        return sum / static_cast<double>(mos_history_.size());
    }

    /// Get MOS percentile (e.g., 95th percentile = worst 5% of time)
    [[nodiscard]] double mos_percentile(double pct) const noexcept {
        std::shared_lock lock(mutex_);
        if (mos_history_.empty()) return 0.0;

        std::vector<double> sorted(mos_history_.begin(), mos_history_.end());
        std::sort(sorted.begin(), sorted.end());

        size_t idx = static_cast<size_t>(pct / 100.0 * (sorted.size() - 1));
        idx = std::min(idx, sorted.size() - 1);
        return sorted[idx];
    }

    /// Reset calculator
    void reset() noexcept {
        std::unique_lock lock(mutex_);
        mos_history_.clear();
        resolution_changes_.clear();
    }

    /// Notify of a resolution change for stability tracking
    void notify_resolution_change() noexcept {
        std::unique_lock lock(mutex_);
        resolution_changes_.push_back(Clock::now());
        auto cutoff = Clock::now() - std::chrono::seconds(30);
        while (!resolution_changes_.empty() && resolution_changes_.front() < cutoff) {
            resolution_changes_.pop_front();
        }
    }

private:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] double compute_stability() const noexcept {
        if (mos_history_.size() < 2) return 100.0;

        double mean = std::accumulate(mos_history_.begin(), mos_history_.end(), 0.0) /
                     static_cast<double>(mos_history_.size());

        double variance = 0.0;
        for (double v : mos_history_) {
            variance += (v - mean) * (v - mean);
        }
        variance /= static_cast<double>(mos_history_.size());

        double stddev = std::sqrt(variance);
        // Stability = 100 when stddev=0, 0 when stddev >= 1.5
        return detail::clamp(100.0 - (stddev / 1.5) * 100.0, 0.0, 100.0);
    }

    [[nodiscard]] double compute_adaptation_speed() const noexcept {
        if (resolution_changes_.size() < 2) return 0.0;
        auto window = std::chrono::seconds(30);
        size_t changes = 0;
        auto cutoff = Clock::now() - window;
        for (const auto& t : resolution_changes_) {
            if (t >= cutoff) ++changes;
        }
        return static_cast<double>(changes) / 30.0; // changes per second
    }

    Config cfg_;
    mutable std::shared_mutex mutex_;
    QoEMetrics last_metrics_;
    std::deque<double> mos_history_;
    std::deque<Clock::time_point> resolution_changes_;
};

// =============================================================================
// 13. Video QoS Manager (Orchestrator)
// =============================================================================

class VideoQoSManager {
public:
    /// Aggregate QoS decision output
    struct QoSDecision {
        ResolutionTier recommended_resolution = ResolutionTier::RES_1080P;
        double recommended_fps = 30.0;
        double target_bitrate_bps = 5'000'000.0;
        uint32_t keyframe_interval = 60;
        QualityPreset quality_preset = QualityPreset::BALANCED;
        FECLevel fec_level = FECLevel::MEDIUM;
        RecoveryMode recovery_mode = RecoveryMode::HYBRID;
        double fec_overhead = 0.1;
        std::chrono::milliseconds target_jitter_buffer{40};
        double network_score = 75.0;
        double estimated_bandwidth_bps = 5'000'000.0;
        double loss_rate = 0.0;
        std::chrono::milliseconds rtt{50};
        double mos = 4.0;
        NetworkCondition condition = NetworkCondition::GOOD;
        bool should_insert_keyframe = false;
        std::chrono::milliseconds frame_interval{33333};
        std::chrono::steady_clock::time_point timestamp;
    };

    struct Config {
        BandwidthEstimator::Config bwe_config;
        FramePacer::Config pacer_config;
        AdaptiveFPSController::Config fps_config;
        ResolutionScaler::Config scaler_config;
        QualityPresetManager::Config preset_config;
        KeyframeIntervalAdapter::Config keyframe_config;
        PacketLossRecovery::Config recovery_config;
        JitterBufferManager::Config jitter_config;
        NetworkConditionScorer::Config scorer_config;
        ClientFeedbackProcessor::Config feedback_config;
        MultiStreamPrioritizer::Config prioritizer_config;
        QoEMetricsCalculator::Config qoe_config;
        std::chrono::milliseconds evaluation_interval = std::chrono::milliseconds(33);
    };

    explicit VideoQoSManager(const Config& cfg = Config{})
        : cfg_(cfg)
        , bandwidth_estimator_(cfg.bwe_config)
        , frame_pacer_(cfg.pacer_config)
        , fps_controller_(cfg.fps_config)
        , resolution_scaler_(cfg.scaler_config)
        , quality_presets_(cfg.preset_config)
        , keyframe_adapter_(cfg.keyframe_config)
        , packet_recovery_(cfg.recovery_config)
        , jitter_buffer_(cfg.jitter_config)
        , network_scorer_(cfg.scorer_config)
        , feedback_processor_(cfg.feedback_config)
        , stream_prioritizer_(cfg.prioritizer_config)
        , qoe_calculator_(cfg.qoe_config)
    {
        spdlog::info("[VideoQoSManager] Fully initialized with all subsystems");
    }

    // ---- Access to subsystems ----------------------------------------------

    [[nodiscard]] BandwidthEstimator& bandwidth_estimator() noexcept { return bandwidth_estimator_; }
    [[nodiscard]] FramePacer& frame_pacer() noexcept { return frame_pacer_; }
    [[nodiscard]] AdaptiveFPSController& fps_controller() noexcept { return fps_controller_; }
    [[nodiscard]] ResolutionScaler& resolution_scaler() noexcept { return resolution_scaler_; }
    [[nodiscard]] QualityPresetManager& quality_presets() noexcept { return quality_presets_; }
    [[nodiscard]] KeyframeIntervalAdapter& keyframe_adapter() noexcept { return keyframe_adapter_; }
    [[nodiscard]] PacketLossRecovery& packet_recovery() noexcept { return packet_recovery_; }
    [[nodiscard]] JitterBufferManager& jitter_buffer() noexcept { return jitter_buffer_; }
    [[nodiscard]] NetworkConditionScorer& network_scorer() noexcept { return network_scorer_; }
    [[nodiscard]] ClientFeedbackProcessor& feedback_processor() noexcept { return feedback_processor_; }
    [[nodiscard]] MultiStreamPrioritizer& stream_prioritizer() noexcept { return stream_prioritizer_; }
    [[nodiscard]] QoEMetricsCalculator& qoe_calculator() noexcept { return qoe_calculator_; }

    // ---- Main evaluation cycle ---------------------------------------------

    /// Evaluate all subsystems and produce a QoS decision
    [[nodiscard]] QoSDecision evaluate() noexcept {
        QoSDecision decision;
        decision.timestamp = std::chrono::steady_clock::now();

        // Step 1: Gather network metrics
        double estimated_bw = bandwidth_estimator_.estimated_bandwidth_bps();
        auto rtt = bandwidth_estimator_.smoothed_rtt();
        double loss_rate = bandwidth_estimator_.loss_rate();
        double pacing_rate = bandwidth_estimator_.pacing_rate_bps();
        double jitter_ms = jitter_buffer_.estimated_jitter_ms();

        // Step 2: Network scoring
        NetworkConditionScorer::NetworkMetrics net_metrics;
        net_metrics.bandwidth_bps = estimated_bw;
        net_metrics.rtt = rtt;
        net_metrics.jitter_ms = jitter_ms;
        net_metrics.loss_rate = loss_rate;
        net_metrics.concurrent_streams = stream_prioritizer_.stream_count();
        net_metrics.timestamp = decision.timestamp;

        double network_score = network_scorer_.evaluate(net_metrics);
        auto condition = network_scorer_.condition();

        // Step 3: Resolution scaling
        ResolutionTier resolution = resolution_scaler_.evaluate(estimated_bw);

        // Step 4: Quality preset adaptation based on network condition
        QualityPreset target_preset;
        switch (condition) {
        case NetworkCondition::EXCELLENT: target_preset = QualityPreset::ULTRA; break;
        case NetworkCondition::GOOD:      target_preset = QualityPreset::HIGH; break;
        case NetworkCondition::FAIR:      target_preset = QualityPreset::BALANCED; break;
        case NetworkCondition::POOR:      target_preset = QualityPreset::LOW; break;
        case NetworkCondition::CRITICAL:  target_preset = QualityPreset::MINIMAL; break;
        }
        quality_presets_.request_preset(target_preset);
        quality_presets_.update();

        // Step 5: FPS adaptation
        double base_bitrate = ResolutionScaler::get_bitrate_target(resolution);
        double required_bitrate = quality_presets_.recommended_bitrate(base_bitrate);
        double bw_ratio = estimated_bw / std::max(required_bitrate, 1.0);

        fps_controller_.update_bandwidth_ratio(bw_ratio);
        double target_fps = fps_controller_.evaluate();

        // Step 6: Frame pacing
        frame_pacer_.set_target_fps(target_fps);

        // Step 7: Keyframe interval
        keyframe_adapter_.update_network_conditions(loss_rate, rtt);

        // Step 8: Packet recovery adaptation
        adapt_recovery(condition, loss_rate);

        // Step 9: Jitter buffer management
        // (handled internally as frames arrive)

        // Step 10: QoE computation
        auto qoe = qoe_calculator_.compute(
            resolution, target_fps, estimated_bw,
            rtt, loss_rate, jitter_ms);

        // Step 11: Handle client feedback
        process_pending_feedback();

        // Step 12: Multi-stream allocation
        auto allocations = stream_prioritizer_.allocate(estimated_bw);

        // Fill decision
        decision.recommended_resolution = resolution;
        decision.recommended_fps = target_fps;
        decision.target_bitrate_bps = required_bitrate;
        decision.keyframe_interval = keyframe_adapter_.current_interval();
        decision.quality_preset = quality_presets_.current_preset();
        decision.fec_level = packet_recovery_.fec_level_name() == "MEDIUM"
            ? FECLevel::MEDIUM : FECLevel::LOW;
        decision.recovery_mode = packet_recovery_.recovery_mode_name() == "HYBRID"
            ? RecoveryMode::HYBRID : RecoveryMode::NACK_ONLY;
        decision.fec_overhead = packet_recovery_.fec_overhead();
        decision.target_jitter_buffer = jitter_buffer_.target_delay();
        decision.network_score = network_score;
        decision.estimated_bandwidth_bps = estimated_bw;
        decision.loss_rate = loss_rate;
        decision.rtt = rtt;
        decision.mos = qoe.mos;
        decision.condition = condition;
        decision.should_insert_keyframe = keyframe_adapter_.should_insert_keyframe();
        decision.frame_interval = frame_pacer_.frame_interval();

        return decision;
    }

    /// Feed packet event to bandwidth estimator
    void on_packet_sent(uint64_t packet_id, size_t payload_bytes) noexcept {
        bandwidth_estimator_.on_packet_sent(packet_id, payload_bytes,
                                             std::chrono::steady_clock::now());
    }

    void on_packet_acked(uint64_t packet_id) noexcept {
        bandwidth_estimator_.on_packet_acked(packet_id,
                                              std::chrono::steady_clock::now());
    }

    void on_packet_lost(uint64_t packet_id, size_t loss_count = 1) noexcept {
        bandwidth_estimator_.on_packet_lost(packet_id, loss_count);
        packet_recovery_.register_missing_packet(packet_id, last_frame_id_,
                                                  std::chrono::steady_clock::now());
    }

    /// Feed frame events
    void on_frame_encoded(uint64_t frame_id, size_t frame_bytes, bool is_keyframe,
                          double motion_score = 0.0, bool scene_change = false) noexcept
    {
        last_frame_id_ = frame_id;
        fps_controller_.update_content_change(motion_score);
        keyframe_adapter_.on_frame(is_keyframe, scene_change);

        // Estimate instantaneous bitrate
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(
            now - last_frame_time_).count();
        if (elapsed > 0.0) {
            double instant_bps = (frame_bytes * 8.0) / elapsed;
            // Feed back to estimator indirectly through ack mechanism
        }
        last_frame_time_ = now;
    }

    void on_frame_presented() noexcept {
        frame_pacer_.on_frame_presented(std::chrono::steady_clock::now());
    }

    /// Feed client feedback
    void on_client_stats(uint32_t client_id,
                         ClientFeedbackProcessor::ClientStats stats) noexcept
    {
        feedback_processor_.process_stats(client_id, std::move(stats));
    }

    void on_client_request(uint32_t client_id,
                           ClientFeedbackProcessor::ClientRequest request) noexcept
    {
        feedback_processor_.process_request(client_id, std::move(request));
    }

    /// Get next presentation time for frame pacing
    [[nodiscard]] std::chrono::steady_clock::time_point next_presentation_time() noexcept {
        return frame_pacer_.next_presentation_time();
    }

    /// Get time until next frame should be presented
    [[nodiscard]] std::chrono::microseconds time_until_next_frame() const noexcept {
        return frame_pacer_.time_until_next_frame();
    }

    /// Get comprehensive status report
    [[nodiscard]] std::string status_report() const noexcept {
        auto decision = const_cast<VideoQoSManager*>(this)->evaluate();

        char buf[1024];
        snprintf(buf, sizeof(buf),
            "VideoQoS Status:\n"
            "  Bandwidth: %.2f Mbps (est), %.2f Mbps (pacing)\n"
            "  RTT: %lldms, Loss: %.2f%%, Jitter: %.1fms\n"
            "  Resolution: %s, FPS: %.1f, Bitrate: %.2f Mbps\n"
            "  Quality: %s, Keyframe interval: %u\n"
            "  Network: %.1f/100 (%s)\n"
            "  QoE MOS: %.2f, Condition: %s\n"
            "  Recovery: FEC=%.1f%% overhead, %llu nacks sent\n"
            "  Buffer: %zu frames, %lldms target\n"
            "  Streams: %zu active",
            decision.estimated_bandwidth_bps / 1'000'000.0,
            bandwidth_estimator_.pacing_rate_bps() / 1'000'000.0,
            static_cast<long long>(decision.rtt.count()),
            decision.loss_rate * 100.0,
            decision.target_jitter_buffer.count() > 0
                ? jitter_buffer_.estimated_jitter_ms() : 0.0,
            std::string(detail::resolution_name(decision.recommended_resolution)).c_str(),
            decision.recommended_fps,
            decision.target_bitrate_bps / 1'000'000.0,
            std::string(detail::preset_name(decision.quality_preset)).c_str(),
            decision.keyframe_interval,
            decision.network_score,
            std::string(detail::condition_name(decision.condition)).c_str(),
            decision.mos,
            std::string(detail::condition_name(decision.condition)).c_str(),
            decision.fec_overhead * 100.0,
            static_cast<unsigned long long>(packet_recovery_.total_nacks_sent()),
            jitter_buffer_.occupancy(),
            static_cast<long long>(decision.target_jitter_buffer.count()),
            stream_prioritizer_.stream_count());

        return std::string(buf);
    }

    /// Reset all subsystems
    void reset() noexcept {
        bandwidth_estimator_.reset();
        frame_pacer_.reset();
        fps_controller_.reset();
        resolution_scaler_.reset();
        quality_presets_.reset();
        keyframe_adapter_.reset();
        packet_recovery_.reset();
        jitter_buffer_.reset();
        network_scorer_.reset();
        feedback_processor_.reset();
        stream_prioritizer_.reset();
        qoe_calculator_.reset();
        spdlog::info("[VideoQoSManager] All subsystems reset");
    }

private:
    void adapt_recovery(NetworkCondition condition, double loss_rate) noexcept {
        switch (condition) {
        case NetworkCondition::EXCELLENT:
            packet_recovery_.set_fec_level(FECLevel::LOW);
            packet_recovery_.set_recovery_mode(RecoveryMode::NACK_ONLY);
            break;
        case NetworkCondition::GOOD:
            packet_recovery_.set_fec_level(FECLevel::MEDIUM);
            packet_recovery_.set_recovery_mode(RecoveryMode::HYBRID);
            break;
        case NetworkCondition::FAIR:
            packet_recovery_.set_fec_level(FECLevel::HIGH);
            packet_recovery_.set_recovery_mode(RecoveryMode::HYBRID);
            break;
        case NetworkCondition::POOR:
            packet_recovery_.set_fec_level(FECLevel::HIGH);
            packet_recovery_.set_recovery_mode(RecoveryMode::FEC_ONLY);
            break;
        case NetworkCondition::CRITICAL:
            packet_recovery_.set_fec_level(FECLevel::MAXIMUM);
            packet_recovery_.set_recovery_mode(RecoveryMode::FEC_ONLY);
            break;
        }

        // Fine-tune based on loss rate
        if (loss_rate > 0.05 && condition < NetworkCondition::CRITICAL) {
            packet_recovery_.set_fec_level(FECLevel::HIGH);
            packet_recovery_.set_recovery_mode(RecoveryMode::HYBRID);
        }
    }

    void process_pending_feedback() noexcept {
        auto clients = feedback_processor_.alive_clients();
        for (uint32_t client_id : clients) {
            auto requests = feedback_processor_.consume_requests(client_id);
            for (const auto& req : requests) {
                handle_client_request(client_id, req);
            }
        }
    }

    void handle_client_request(uint32_t client_id,
                                const ClientFeedbackProcessor::ClientRequest& req) noexcept
    {
        using Type = ClientFeedbackProcessor::ClientRequest::Type;

        switch (req.type) {
        case Type::RESOLUTION_CHANGE:
            if (req.target_resolution) {
                resolution_scaler_.force_tier(*req.target_resolution);
                qoe_calculator_.notify_resolution_change();
                spdlog::info("[QoS] Client {} requested resolution change to {}",
                             client_id,
                             detail::resolution_name(*req.target_resolution));
            }
            break;

        case Type::FPS_CHANGE:
            if (req.target_fps) {
                fps_controller_.force_fps(*req.target_fps);
                spdlog::info("[QoS] Client {} requested FPS change to {:.1f}",
                             client_id, *req.target_fps);
            }
            break;

        case Type::BITRATE_CHANGE:
            if (req.target_bitrate) {
                bandwidth_estimator_.set_bandwidth(*req.target_bitrate);
                spdlog::info("[QoS] Client {} requested bitrate: {:.2f} Mbps",
                             client_id, *req.target_bitrate / 1'000'000.0);
            }
            break;

        case Type::KEYFRAME_REQUEST:
            keyframe_adapter_.force_keyframe();
            spdlog::info("[QoS] Client {} requested keyframe", client_id);
            break;

        case Type::QUALITY_PRESET_CHANGE:
            if (req.target_preset) {
                quality_presets_.request_preset(*req.target_preset);
                spdlog::info("[QoS] Client {} requested quality: {}",
                             client_id, detail::preset_name(*req.target_preset));
            }
            break;

        case Type::FEC_LEVEL_CHANGE:
            if (req.target_fec) {
                packet_recovery_.set_fec_level(*req.target_fec);
                spdlog::info("[QoS] Client {} requested FEC: {}",
                             client_id, packet_recovery_.fec_level_name());
            }
            break;

        case Type::STREAM_PAUSE:
        case Type::STREAM_RESUME:
        case Type::CUSTOM:
            spdlog::debug("[QoS] Client {} request type={} (handled externally)",
                          client_id, static_cast<int>(req.type));
            break;
        }
    }

    Config cfg_;

    BandwidthEstimator bandwidth_estimator_;
    FramePacer frame_pacer_;
    AdaptiveFPSController fps_controller_;
    ResolutionScaler resolution_scaler_;
    QualityPresetManager quality_presets_;
    KeyframeIntervalAdapter keyframe_adapter_;
    PacketLossRecovery packet_recovery_;
    JitterBufferManager jitter_buffer_;
    NetworkConditionScorer network_scorer_;
    ClientFeedbackProcessor feedback_processor_;
    MultiStreamPrioritizer stream_prioritizer_;
    QoEMetricsCalculator qoe_calculator_;

    uint64_t last_frame_id_ = 0;
    std::chrono::steady_clock::time_point last_frame_time_{
        std::chrono::steady_clock::now()};
};

// =============================================================================
// 14. Periodic QoE Reporter (background statistics)
// =============================================================================

class PeriodicQoEReporter {
public:
    using ReportCallback = std::function<void(const QoEMetricsCalculator::QoEMetrics&)>;

    struct Config {
        std::chrono::milliseconds report_interval = std::chrono::milliseconds(5000);
        bool log_to_spdlog = true;
        bool enable_periodic_reporting = true;
    };

    PeriodicQoEReporter(VideoQoSManager& manager, const Config& cfg = Config{})
        : manager_(manager), cfg_(cfg), running_(false)
    {
        spdlog::info("[PeriodicQoEReporter] Initialized: interval={}ms",
                     cfg.report_interval.count());
    }

    ~PeriodicQoEReporter() {
        stop();
    }

    /// Start periodic reporting
    void start() noexcept {
        if (running_.exchange(true)) return;

        report_thread_ = std::jthread([this](std::stop_token stoken) {
            spdlog::info("[PeriodicQoEReporter] Started");

            while (!stoken.stop_requested()) {
                std::this_thread::sleep_for(cfg_.report_interval);

                auto decision = manager_.evaluate();
                auto qoe = manager_.qoe_calculator().last_metrics();

                if (cfg_.log_to_spdlog) {
                    spdlog::info(
                        "[QoE Report] MOS={:.2f} | BW={:.2f}Mbps | RTT={}ms | "
                        "Loss={:.2f}% | Res={} | FPS={:.1f} | Score={:.0f}/100 | {}",
                        qoe.mos,
                        decision.estimated_bandwidth_bps / 1'000'000.0,
                        decision.rtt.count(),
                        decision.loss_rate * 100.0,
                        detail::resolution_name(decision.recommended_resolution),
                        decision.recommended_fps,
                        decision.network_score,
                        detail::condition_name(decision.condition));
                }

                // Invoke custom callbacks
                std::shared_lock lock(callback_mutex_);
                for (const auto& cb : callbacks_) {
                    if (cb) cb(qoe);
                }
            }

            spdlog::info("[PeriodicQoEReporter] Stopped");
        });
    }

    /// Stop periodic reporting
    void stop() noexcept {
        running_.store(false);
        if (report_thread_.joinable()) {
            report_thread_.request_stop();
            report_thread_.join();
        }
    }

    /// Register a custom report callback
    void register_callback(ReportCallback callback) noexcept {
        std::unique_lock lock(callback_mutex_);
        callbacks_.push_back(std::move(callback));
    }

    /// Check if running
    [[nodiscard]] bool is_running() const noexcept {
        return running_.load();
    }

private:
    VideoQoSManager& manager_;
    Config cfg_;
    std::atomic<bool> running_;
    std::jthread report_thread_;
    mutable std::shared_mutex callback_mutex_;
    std::vector<ReportCallback> callbacks_;
};

// =============================================================================
// 15. Factory Functions
// =============================================================================

/// Create a default VideoQoSManager with sensible defaults
[[nodiscard]] inline std::unique_ptr<VideoQoSManager> create_default_qos_manager() {
    VideoQoSManager::Config cfg;

    // BWE defaults
    cfg.bwe_config.min_bandwidth_bps = 100'000.0;
    cfg.bwe_config.max_bandwidth_bps = 500'000'000.0;
    cfg.bwe_config.initial_bandwidth = 10'000'000.0;

    // Pacer defaults
    cfg.pacer_config.vsync_period = std::chrono::microseconds(16667);
    cfg.pacer_config.vsync_snap = true;

    // FPS defaults
    cfg.fps_config.min_fps = 5.0;
    cfg.fps_config.max_fps = 120.0;
    cfg.fps_config.default_fps = 30.0;

    // Scaler defaults: use the standard ladder

    // Quality defaults
    cfg.preset_config.transition_duration = std::chrono::milliseconds(2000);

    // Keyframe defaults
    cfg.keyframe_config.default_interval = 60;
    cfg.keyframe_config.min_interval = 15;
    cfg.keyframe_config.max_interval = 300;

    // Recovery defaults
    cfg.recovery_config.mode = RecoveryMode::HYBRID;
    cfg.recovery_config.fec_level = FECLevel::MEDIUM;

    // Jitter buffer defaults
    cfg.jitter_config.target_delay = std::chrono::milliseconds(40);
    cfg.jitter_config.max_frames = 32;

    // Network scorer defaults
    cfg.scorer_config = NetworkConditionScorer::Config{};

    // Feedback processor defaults
    cfg.feedback_config.stats_timeout = std::chrono::milliseconds(5000);

    // Prioritizer defaults
    cfg.prioritizer_config.max_streams = 16;

    // QoE calculator defaults
    cfg.qoe_config.evaluation_interval = std::chrono::milliseconds(1000);

    return std::make_unique<VideoQoSManager>(cfg);
}

/// Create a high-performance QoS manager for high-bandwidth scenarios
[[nodiscard]] inline std::unique_ptr<VideoQoSManager> create_high_performance_qos_manager() {
    auto mgr = create_default_qos_manager();

    // Configure for high bandwidth
    mgr->bandwidth_estimator().set_bandwidth(50'000'000.0);
    mgr->resolution_scaler().force_tier(ResolutionTier::RES_4K);
    mgr->quality_presets().request_preset(QualityPreset::ULTRA);
    mgr->fps_controller().force_fps(60.0);

    return mgr;
}

/// Create a conservative QoS manager for unstable networks
[[nodiscard]] inline std::unique_ptr<VideoQoSManager> create_conservative_qos_manager() {
    VideoQoSManager::Config cfg;

    cfg.bwe_config.min_bandwidth_bps = 200'000.0;
    cfg.bwe_config.initial_bandwidth = 2'000'000.0;
    cfg.bwe_config.probe_gain = 1.1; // Less aggressive probing

    cfg.fps_config.default_fps = 24.0;
    cfg.fps_config.max_fps = 30.0;

    cfg.scaler_config.scale_up_threshold = 1.5; // More conservative scaling
    cfg.scaler_config.scale_down_threshold = 0.9;
    cfg.scaler_config.stable_frames_required = 120;

    cfg.recovery_config.mode = RecoveryMode::FEC_ONLY;
    cfg.recovery_config.fec_level = FECLevel::HIGH;

    cfg.jitter_config.target_delay = std::chrono::milliseconds(80);
    cfg.jitter_config.max_frames = 48;

    cfg.keyframe_config.default_interval = 30; // More frequent keyframes

    return std::make_unique<VideoQoSManager>(cfg);
}

} // namespace cppdesk::server

// =============================================================================
// End of video_qos_full.cpp
// =============================================================================
