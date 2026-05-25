// ============================================================================
// cppdesk — Remote Desktop Client
// helper.cpp — Comprehensive Client Helper Implementations
// ============================================================================
// Implements connection quality monitoring, adaptive quality adjustment,
// display layout negotiation, keyboard layout synchronization, clipboard
// format negotiation, audio device matching, timezone sync, file transfer
// resume, keep-alive/dead-connection detection, session recording playback,
// multi-monitor coordinate translation, touch-to-mouse conversion, game
// controller input mapping, and stylus/pen pressure support.
// ============================================================================

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <queue>
#include <random>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include <asio.hpp>
#include <asio/steady_timer.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

// Internal project includes (forward declarations where possible)
#include "client/helper.hpp"
#include "client/protocol.hpp"
#include "client/message_types.hpp"

namespace cppdesk::client::helper {

// ============================================================================
// Internal Constants
// ============================================================================

namespace detail {

// --- Quality monitoring -----------------------------------------------------
static constexpr auto kQualitySampleInterval   = std::chrono::milliseconds(500);
static constexpr auto kBandwidthWindowDuration = std::chrono::seconds(10);
static constexpr auto kLatencyWindowSize       = 128;
static constexpr auto kPacketLossWindowSize    = 256;
static constexpr auto kMaxJitterSamples        = 64;
static constexpr float  kDefaultJitterFactor   = 0.15f;
static constexpr float  kRttSmoothingAlpha     = 0.125f;  // EWMA alpha

// --- Adaptive quality -------------------------------------------------------
static constexpr float kQualityFloor       = 0.10f;
static constexpr float kQualityCeil        = 1.00f;
static constexpr float kQualityStepUp      = 0.05f;
static constexpr float kQualityStepDown    = 0.10f;
static constexpr float kCongestionThreshold = 0.05f;  // 5% loss triggers step-down
static constexpr auto  kQualityAdjustInterval = std::chrono::seconds(2);

// --- Display negotiation ----------------------------------------------------
static constexpr std::uint32_t kMaxDisplays    = 16;
static constexpr std::uint32_t kMinDisplayWidth  = 320;
static constexpr std::uint32_t kMinDisplayHeight = 200;
static constexpr std::uint32_t kMaxDisplayWidth  = 7680;
static constexpr std::uint32_t kMaxDisplayHeight = 4320;

// --- Keyboard layout --------------------------------------------------------
static constexpr std::size_t kMaxLayoutNameLen = 64;
static constexpr std::size_t kMaxKeyMappings   = 512;

// --- Clipboard --------------------------------------------------------------
static constexpr std::size_t kMaxClipboardFormats = 64;
static constexpr std::size_t kMaxFormatNameLen    = 128;

// --- Audio ------------------------------------------------------------------
static constexpr std::uint32_t kAudioSampleRateMin = 8000;
static constexpr std::uint32_t kAudioSampleRateMax = 192000;
static constexpr std::uint8_t  kAudioChannelsMin   = 1;
static constexpr std::uint8_t  kAudioChannelsMax   = 8;

// --- Timezone ---------------------------------------------------------------
static constexpr auto kTimezoneSyncInterval = std::chrono::minutes(5);

// --- File transfer resume ---------------------------------------------------
static constexpr std::size_t kChecksumBlockSize = 64 * 1024;  // 64 KiB per block
static constexpr std::size_t kMaxResumeFiles    = 1024;
static constexpr auto       kResumeStateFlushInterval = std::chrono::seconds(10);

// --- Keep-alive -------------------------------------------------------------
static constexpr auto kKeepAliveInterval   = std::chrono::seconds(5);
static constexpr auto kKeepAliveTimeout    = std::chrono::seconds(30);
static constexpr auto kDeadDetectionGrace  = std::chrono::seconds(3);
static constexpr int   kMaxMissedKeepAlives = 6;

// --- Session recording playback ---------------------------------------------
static constexpr std::uint32_t kPlaybackMagic     = 0x4350444B; // "CPDK"
static constexpr std::uint32_t kPlaybackVersion    = 2;
static constexpr std::size_t  kPlaybackFrameHeader = 32;
static constexpr std::size_t  kPlaybackMaxFrameSz  = 64 * 1024 * 1024;

// --- Multi-monitor ----------------------------------------------------------
static constexpr std::uint32_t kMaxMonitors     = 16;
static constexpr std::int32_t  kVirtualOriginX  = 0;
static constexpr std::int32_t  kVirtualOriginY  = 0;

// --- Touch conversion -------------------------------------------------------
static constexpr float kTapTimeThreshold       = 0.300f;  // seconds
static constexpr float kTapDistanceThreshold   = 10.0f;   // pixels
static constexpr float kLongPressThreshold     = 0.500f;  // seconds
static constexpr float kDoubleTapInterval      = 0.400f;  // seconds
static constexpr float kSwipeMinVelocity        = 200.0f;  // pixels/sec
static constexpr float kPinchScaleThreshold     = 0.05f;

// --- Game controller --------------------------------------------------------
static constexpr float kControllerDeadZone      = 0.10f;
static constexpr float kControllerMaxAxis       = 32767.0f;
static constexpr int   kMaxControllerButtons    = 32;
static constexpr int   kMaxControllerAxes       = 8;

// --- Stylus -----------------------------------------------------------------
static constexpr float kPressureMin            = 0.0f;
static constexpr float kPressureMax            = 1.0f;
static constexpr float kTiltMin                = -90.0f;
static constexpr float kTiltMax                = 90.0f;
static constexpr std::uint16_t kMaxPressureLevels = 8192;

} // namespace detail

// ============================================================================
// 1. Connection Quality Monitoring
// ============================================================================

// --- NetworkQualitySnapshot -------------------------------------------------

NetworkQualitySnapshot::NetworkQualitySnapshot()
    : timestamp(std::chrono::steady_clock::now())
    , latency_ms(0.0f)
    , jitter_ms(0.0f)
    , packet_loss_ratio(0.0f)
    , bandwidth_bps(0.0f)
    , bytes_sent(0)
    , bytes_received(0)
    , packets_sent(0)
    , packets_received(0)
    , packets_lost(0)
    , rtt_smoothed_ms(0.0f)
    , connection_score(1.0f)
    , quality_tier(QualityTier::kUnknown)
{}

// --- ConnectionQualityMonitor::Impl -----------------------------------------

class ConnectionQualityMonitor::Impl {
public:
    Impl() = default;
    ~Impl() = default;

    // Record a latency sample (RTT in milliseconds)
    void record_latency(float rtt_ms) {
        std::lock_guard lock(mutex_);

        latency_window_.push_back(rtt_ms);
        if (latency_window_.size() > detail::kLatencyWindowSize) {
            latency_window_.pop_front();
        }

        // EWMA smoothed RTT
        if (smoothed_rtt_ms_ < 0.0f) {
            smoothed_rtt_ms_ = rtt_ms;
        } else {
            smoothed_rtt_ms_ = (detail::kRttSmoothingAlpha * rtt_ms) +
                               ((1.0f - detail::kRttSmoothingAlpha) * smoothed_rtt_ms_);
        }

        // Update jitter (RFC 3550 style)
        float jitter_delta = std::abs(rtt_ms - last_rtt_ms_);
        jitter_ms_ += (1.0f / 16.0f) * (jitter_delta - jitter_ms_);
        last_rtt_ms_ = rtt_ms;

        recalc_score();
    }

    // Record bytes sent/received for bandwidth calculation
    void record_bytes(std::uint64_t bytes_sent, std::uint64_t bytes_recv) {
        std::lock_guard lock(mutex_);

        auto now = std::chrono::steady_clock::now();
        bytes_sent_ += bytes_sent;
        bytes_recv_ += bytes_recv;

        // Slide bandwidth window
        bandwidth_samples_.push_back({now, bytes_sent, bytes_recv});
        while (!bandwidth_samples_.empty() &&
               (now - bandwidth_samples_.front().timestamp) > detail::kBandwidthWindowDuration) {
            bandwidth_samples_.pop_front();
        }

        recalc_bandwidth();
    }

    // Record packet loss
    void record_packet_loss(std::uint32_t packets_sent, std::uint32_t packets_lost) {
        std::lock_guard lock(mutex_);

        total_packets_sent_ += packets_sent;
        total_packets_lost_ += packets_lost;

        float loss_ratio = (total_packets_sent_ > 0)
            ? static_cast<float>(total_packets_lost_) / static_cast<float>(total_packets_sent_)
            : 0.0f;

        packet_loss_ratio_ = loss_ratio;

        // Keep sliding window of loss events
        loss_window_.push_back(loss_ratio);
        if (loss_window_.size() > detail::kPacketLossWindowSize) {
            loss_window_.pop_front();
        }

        recalc_score();
    }

    // Get current snapshot
    NetworkQualitySnapshot snapshot() const {
        std::lock_guard lock(mutex_);

        NetworkQualitySnapshot snap;
        snap.timestamp          = std::chrono::steady_clock::now();
        snap.latency_ms         = smoothed_rtt_ms_;
        snap.jitter_ms          = jitter_ms_;
        snap.packet_loss_ratio  = packet_loss_ratio_;
        snap.bandwidth_bps      = estimated_bandwidth_bps_;
        snap.bytes_sent         = bytes_sent_;
        snap.bytes_received     = bytes_recv_;
        snap.packets_sent       = total_packets_sent_;
        snap.packets_received   = total_packets_recv_;
        snap.packets_lost       = total_packets_lost_;
        snap.rtt_smoothed_ms    = smoothed_rtt_ms_;
        snap.connection_score   = connection_score_;
        snap.quality_tier       = quality_tier_;
        return snap;
    }

    // Get current quality tier
    QualityTier quality_tier() const {
        std::lock_guard lock(mutex_);
        return quality_tier_;
    }

    // Get connection score (0.0 = dead, 1.0 = perfect)
    float connection_score() const {
        std::lock_guard lock(mutex_);
        return connection_score_;
    }

    // Reset all statistics
    void reset() {
        std::lock_guard lock(mutex_);

        latency_window_.clear();
        loss_window_.clear();
        bandwidth_samples_.clear();
        smoothed_rtt_ms_       = -1.0f;
        last_rtt_ms_           = 0.0f;
        jitter_ms_             = 0.0f;
        packet_loss_ratio_     = 0.0f;
        estimated_bandwidth_bps_ = 0.0f;
        bytes_sent_            = 0;
        bytes_recv_            = 0;
        total_packets_sent_    = 0;
        total_packets_recv_    = 0;
        total_packets_lost_    = 0;
        connection_score_      = 1.0f;
        quality_tier_          = QualityTier::kUnknown;
    }

    // Get average latency from window
    float average_latency() const {
        std::lock_guard lock(mutex_);
        if (latency_window_.empty()) return 0.0f;
        float sum = 0.0f;
        for (float v : latency_window_) sum += v;
        return sum / static_cast<float>(latency_window_.size());
    }

    // Get percentile latency (e.g., 95th percentile)
    float latency_percentile(float pct) const {
        std::lock_guard lock(mutex_);
        if (latency_window_.empty()) return 0.0f;
        std::vector<float> sorted(latency_window_.begin(), latency_window_.end());
        std::sort(sorted.begin(), sorted.end());
        std::size_t idx = static_cast<std::size_t>(pct * static_cast<float>(sorted.size() - 1));
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return sorted[idx];
    }

    // Detect if connection is degraded
    bool is_degraded() const {
        std::lock_guard lock(mutex_);
        return quality_tier_ <= QualityTier::kFair;
    }

private:
    struct BandwidthSample {
        std::chrono::steady_clock::time_point timestamp;
        std::uint64_t bytes_sent;
        std::uint64_t bytes_recv;
    };

    void recalc_bandwidth() {
        if (bandwidth_samples_.size() < 2) {
            estimated_bandwidth_bps_ = 0.0f;
            return;
        }

        const auto& first = bandwidth_samples_.front();
        const auto& last  = bandwidth_samples_.back();
        auto duration_sec = std::chrono::duration<float>(last.timestamp - first.timestamp).count();
        if (duration_sec <= 0.0f) {
            estimated_bandwidth_bps_ = 0.0f;
            return;
        }

        std::uint64_t total_bytes = (last.bytes_sent - first.bytes_sent) +
                                    (last.bytes_recv - first.bytes_recv);
        estimated_bandwidth_bps_ = static_cast<float>(total_bytes * 8) / duration_sec;
    }

    void recalc_score() {
        // Score components (each 0..1, higher = better)
        float latency_score = 1.0f;
        if (smoothed_rtt_ms_ > 500.0f) {
            latency_score = 0.0f;
        } else if (smoothed_rtt_ms_ > 200.0f) {
            latency_score = (500.0f - smoothed_rtt_ms_) / 300.0f;
        } else if (smoothed_rtt_ms_ > 50.0f) {
            latency_score = 0.5f + (200.0f - smoothed_rtt_ms_) / 300.0f;
        } // else stays 1.0

        float loss_score = 1.0f - std::min(packet_loss_ratio_ * 20.0f, 1.0f); // 5% loss = 0
        float jitter_score = 1.0f - std::min(jitter_ms_ / 100.0f, 1.0f);

        // Weighted composite
        connection_score_ = (latency_score * 0.35f) +
                            (loss_score     * 0.40f) +
                            (jitter_score   * 0.25f);

        // Clamp
        connection_score_ = std::max(0.0f, std::min(1.0f, connection_score_));

        // Determine tier
        if (connection_score_ >= 0.90f) {
            quality_tier_ = QualityTier::kExcellent;
        } else if (connection_score_ >= 0.70f) {
            quality_tier_ = QualityTier::kGood;
        } else if (connection_score_ >= 0.40f) {
            quality_tier_ = QualityTier::kFair;
        } else if (connection_score_ >= 0.10f) {
            quality_tier_ = QualityTier::kPoor;
        } else {
            quality_tier_ = QualityTier::kBad;
        }
    }

    mutable std::mutex mutex_;

    std::deque<float> latency_window_;
    std::deque<float> loss_window_;
    std::deque<BandwidthSample> bandwidth_samples_;

    float smoothed_rtt_ms_       = -1.0f;
    float last_rtt_ms_           = 0.0f;
    float jitter_ms_             = 0.0f;
    float packet_loss_ratio_     = 0.0f;
    float estimated_bandwidth_bps_ = 0.0f;
    float connection_score_      = 1.0f;

    std::uint64_t bytes_sent_      = 0;
    std::uint64_t bytes_recv_      = 0;
    std::uint32_t total_packets_sent_ = 0;
    std::uint32_t total_packets_recv_ = 0;
    std::uint32_t total_packets_lost_ = 0;

    QualityTier quality_tier_ = QualityTier::kUnknown;
};

// --- ConnectionQualityMonitor public interface -------------------------------

ConnectionQualityMonitor::ConnectionQualityMonitor()
    : impl_(std::make_unique<Impl>()) {}

ConnectionQualityMonitor::~ConnectionQualityMonitor() = default;

void ConnectionQualityMonitor::record_latency(float rtt_ms) {
    impl_->record_latency(rtt_ms);
}

void ConnectionQualityMonitor::record_bytes(std::uint64_t sent, std::uint64_t recv) {
    impl_->record_bytes(sent, recv);
}

void ConnectionQualityMonitor::record_packet_loss(std::uint32_t sent, std::uint32_t lost) {
    impl_->record_packet_loss(sent, lost);
}

NetworkQualitySnapshot ConnectionQualityMonitor::snapshot() const {
    return impl_->snapshot();
}

QualityTier ConnectionQualityMonitor::quality_tier() const {
    return impl_->quality_tier();
}

float ConnectionQualityMonitor::connection_score() const {
    return impl_->connection_score();
}

void ConnectionQualityMonitor::reset() {
    impl_->reset();
}

float ConnectionQualityMonitor::average_latency() const {
    return impl_->average_latency();
}

float ConnectionQualityMonitor::latency_percentile(float pct) const {
    return impl_->latency_percentile(pct);
}

bool ConnectionQualityMonitor::is_degraded() const {
    return impl_->is_degraded();
}

// ============================================================================
// 2. Adaptive Quality Adjustment
// ============================================================================

// --- QualityLevel -----------------------------------------------------------

std::string_view to_string(QualityLevel level) {
    switch (level) {
        case QualityLevel::kLowest:  return "lowest";
        case QualityLevel::kLow:     return "low";
        case QualityLevel::kMedium:  return "medium";
        case QualityLevel::kHigh:    return "high";
        case QualityLevel::kHighest: return "highest";
        case QualityLevel::kAuto:    return "auto";
        default:                     return "unknown";
    }
}

QualityLevel quality_level_from_string(std::string_view s) {
    if (s == "lowest")  return QualityLevel::kLowest;
    if (s == "low")     return QualityLevel::kLow;
    if (s == "medium")  return QualityLevel::kMedium;
    if (s == "high")    return QualityLevel::kHigh;
    if (s == "highest") return QualityLevel::kHighest;
    if (s == "auto")    return QualityLevel::kAuto;
    return QualityLevel::kAuto;
}

// --- AdaptiveQualityController::Impl ----------------------------------------

class AdaptiveQualityController::Impl {
public:
    Impl() = default;

    void set_quality_monitor(std::shared_ptr<ConnectionQualityMonitor> monitor) {
        quality_monitor_ = std::move(monitor);
    }

    void set_target_fps(std::uint8_t fps) {
        target_fps_ = std::max(std::uint8_t{1}, std::min(std::uint8_t{120}, fps));
    }

    void set_min_quality(float q) {
        min_quality_ = std::max(detail::kQualityFloor, std::min(detail::kQualityCeil, q));
    }

    void set_max_quality(float q) {
        max_quality_ = std::max(detail::kQualityFloor, std::min(detail::kQualityCeil, q));
    }

    // Compute recommended quality based on current network snapshot
    AdaptiveQualityResult compute(const NetworkQualitySnapshot& snapshot) {
        AdaptiveQualityResult result;
        result.timestamp    = snapshot.timestamp;
        result.input_quality = current_quality_;
        result.input_tier   = snapshot.quality_tier;

        // If in manual mode, just use the locked quality
        if (manual_mode_) {
            result.recommended_quality = locked_quality_;
            result.recommended_fps     = target_fps_;
            result.reason              = "manual_override";
            result.adjusted            = false;
            return result;
        }

        float proposed = current_quality_;
        bool adjusted  = false;
        std::string reason;

        // Step down on high packet loss
        if (snapshot.packet_loss_ratio > detail::kCongestionThreshold) {
            proposed -= detail::kQualityStepDown;
            adjusted = true;
            reason = "packet_loss: " + std::to_string(snapshot.packet_loss_ratio);
        }
        // Step down on high latency
        else if (snapshot.rtt_smoothed_ms > 300.0f) {
            proposed -= detail::kQualityStepDown;
            adjusted = true;
            reason = "high_latency: " + std::to_string(snapshot.rtt_smoothed_ms) + "ms";
        }
        // Step up when conditions are excellent
        else if (snapshot.quality_tier >= QualityTier::kGood &&
                 snapshot.packet_loss_ratio < 0.01f &&
                 snapshot.rtt_smoothed_ms < 50.0f) {
            proposed += detail::kQualityStepUp;
            adjusted = true;
            reason = "good_conditions";
        }
        // Gradual recovery
        else if (snapshot.quality_tier >= QualityTier::kGood &&
                 consecutive_good_ > 5) {
            proposed += detail::kQualityStepUp * 0.5f;
            adjusted = true;
            reason = "sustained_good";
        }

        // Track consecutive good periods
        if (snapshot.quality_tier >= QualityTier::kGood) {
            consecutive_good_++;
        } else {
            consecutive_good_ = 0;
        }

        // Clamp
        proposed = std::max(min_quality_, std::min(max_quality_, proposed));
        proposed = std::max(detail::kQualityFloor, std::min(detail::kQualityCeil, proposed));

        // Adjust FPS based on tier
        std::uint8_t recommended_fps = target_fps_;
        switch (snapshot.quality_tier) {
            case QualityTier::kBad:
                recommended_fps = std::min(target_fps_, std::uint8_t{5});
                break;
            case QualityTier::kPoor:
                recommended_fps = std::min(target_fps_, std::uint8_t{10});
                break;
            case QualityTier::kFair:
                recommended_fps = std::min(target_fps_, std::uint8_t{15});
                break;
            case QualityTier::kGood:
                recommended_fps = std::min(target_fps_, std::uint8_t{30});
                break;
            case QualityTier::kExcellent:
                recommended_fps = target_fps_;
                break;
            default:
                break;
        }

        result.recommended_quality = proposed;
        result.recommended_fps     = recommended_fps;
        result.reason              = reason.empty() ? "stable" : reason;
        result.adjusted            = adjusted;

        // Commit
        if (adjusted) {
            current_quality_ = proposed;
            last_adjustment_ = std::chrono::steady_clock::now();
        }

        return result;
    }

    // Lock quality to a manual level
    void set_manual_quality(float quality) {
        manual_mode_    = true;
        locked_quality_ = std::max(detail::kQualityFloor,
                                   std::min(detail::kQualityCeil, quality));
    }

    void set_auto_mode() {
        manual_mode_ = false;
    }

    bool is_manual_mode() const { return manual_mode_; }

    float current_quality() const { return current_quality_; }

    // Get the bitrate hint in bps based on quality and resolution
    std::uint64_t bitrate_hint(float quality, std::uint32_t width, std::uint32_t height,
                               std::uint8_t fps) {
        // Base: bits per pixel per frame
        float bpp = 0.05f + (quality * 0.45f);  // 0.05 .. 0.50 bpp
        float raw_bps = static_cast<float>(width * height) * bpp * static_cast<float>(fps);
        // Apply h.264/h.265 compression factor (~100x)
        float compressed_bps = raw_bps / 100.0f;
        return static_cast<std::uint64_t>(compressed_bps);
    }

    void reset() {
        current_quality_  = detail::kQualityCeil;
        consecutive_good_ = 0;
        manual_mode_      = false;
    }

private:
    std::shared_ptr<ConnectionQualityMonitor> quality_monitor_;

    float current_quality_  = detail::kQualityCeil;
    float min_quality_      = detail::kQualityFloor;
    float max_quality_      = detail::kQualityCeil;
    std::uint8_t target_fps_ = 30;

    bool manual_mode_      = false;
    float locked_quality_  = detail::kQualityCeil;
    int consecutive_good_  = 0;

    std::chrono::steady_clock::time_point last_adjustment_;
};

// --- AdaptiveQualityController public interface ------------------------------

AdaptiveQualityController::AdaptiveQualityController()
    : impl_(std::make_unique<Impl>()) {}

AdaptiveQualityController::~AdaptiveQualityController() = default;

void AdaptiveQualityController::set_quality_monitor(
    std::shared_ptr<ConnectionQualityMonitor> monitor) {
    impl_->set_quality_monitor(std::move(monitor));
}

void AdaptiveQualityController::set_target_fps(std::uint8_t fps) {
    impl_->set_target_fps(fps);
}

void AdaptiveQualityController::set_min_quality(float q) {
    impl_->set_min_quality(q);
}

void AdaptiveQualityController::set_max_quality(float q) {
    impl_->set_max_quality(q);
}

AdaptiveQualityResult AdaptiveQualityController::compute(
    const NetworkQualitySnapshot& snapshot) {
    return impl_->compute(snapshot);
}

void AdaptiveQualityController::set_manual_quality(float quality) {
    impl_->set_manual_quality(quality);
}

void AdaptiveQualityController::set_auto_mode() {
    impl_->set_auto_mode();
}

bool AdaptiveQualityController::is_manual_mode() const {
    return impl_->is_manual_mode();
}

float AdaptiveQualityController::current_quality() const {
    return impl_->current_quality();
}

std::uint64_t AdaptiveQualityController::bitrate_hint(
    float quality, std::uint32_t width, std::uint32_t height, std::uint8_t fps) {
    return impl_->bitrate_hint(quality, width, height, fps);
}

void AdaptiveQualityController::reset() {
    impl_->reset();
}

// ============================================================================
// 3. Display Layout Negotiation
// ============================================================================

// --- DisplayInfo ------------------------------------------------------------

bool DisplayInfo::is_valid() const {
    return width >= detail::kMinDisplayWidth  && width <= detail::kMaxDisplayWidth &&
           height >= detail::kMinDisplayHeight && height <= detail::kMaxDisplayHeight &&
           !display_name.empty();
}

std::string DisplayInfo::to_string() const {
    std::ostringstream oss;
    oss << display_name << " (" << width << "x" << height;
    if (is_primary) oss << " primary";
    oss << " @" << refresh_rate << "Hz";
    if (dpi > 0) oss << " " << dpi << "dpi";
    oss << " x:" << origin_x << " y:" << origin_y;
    return oss.str();
}

// --- DisplayLayout ----------------------------------------------------------

bool DisplayLayout::is_valid() const {
    if (displays.empty()) return false;
    if (displays.size() > detail::kMaxDisplays) return false;
    int primary_count = 0;
    std::unordered_set<std::uint32_t> seen_ids;
    for (const auto& d : displays) {
        if (!d.is_valid()) return false;
        if (seen_ids.count(d.id)) return false;  // duplicate id
        seen_ids.insert(d.id);
        if (d.is_primary) primary_count++;
    }
    return primary_count == 1;  // exactly one primary
}

const DisplayInfo* DisplayLayout::find_primary() const {
    for (const auto& d : displays) {
        if (d.is_primary) return &d;
    }
    return nullptr;
}

const DisplayInfo* DisplayLayout::find_by_id(std::uint32_t id) const {
    for (const auto& d : displays) {
        if (d.id == id) return &d;
    }
    return nullptr;
}

const DisplayInfo* DisplayLayout::find_at_point(std::int32_t x, std::int32_t y) const {
    for (const auto& d : displays) {
        if (x >= d.origin_x && x < static_cast<std::int32_t>(d.origin_x + d.width) &&
            y >= d.origin_y && y < static_cast<std::int32_t>(d.origin_y + d.height)) {
            return &d;
        }
    }
    return nullptr;
}

void DisplayLayout::compute_virtual_bounds(std::int32_t& out_width, std::int32_t& out_height) const {
    if (displays.empty()) {
        out_width = 0;
        out_height = 0;
        return;
    }
    std::int32_t max_x = 0, max_y = 0;
    for (const auto& d : displays) {
        std::int32_t right  = static_cast<std::int32_t>(d.origin_x + d.width);
        std::int32_t bottom = static_cast<std::int32_t>(d.origin_y + d.height);
        if (right  > max_x) max_x = right;
        if (bottom > max_y) max_y = bottom;
    }
    out_width  = max_x;
    out_height = max_y;
}

// --- DisplayLayoutNegotiator::Impl ------------------------------------------

class DisplayLayoutNegotiator::Impl {
public:
    // Set the local display layout
    void set_local_layout(DisplayLayout layout) {
        std::lock_guard lock(mutex_);
        local_layout_ = std::move(layout);
        SPDLOG_DEBUG("[helper] display negotiator: local layout set, {} displays",
                     local_layout_.displays.size());
    }

    // Receive remote layout from server
    void set_remote_layout(DisplayLayout layout) {
        std::lock_guard lock(mutex_);
        remote_layout_ = std::move(layout);
        remote_layout_received_ = true;
        SPDLOG_DEBUG("[helper] display negotiator: remote layout received, {} displays",
                     remote_layout_.displays.size());
    }

    // Negotiate the best match
    NegotiatorDisplayResult negotiate(const NegotiatePreferences& prefs) {
        std::lock_guard lock(mutex_);

        NegotiatorDisplayResult result;

        if (!remote_layout_received_) {
            result.success = false;
            result.error_message = "No remote layout received";
            return result;
        }

        const auto& remote = remote_layout_.displays;
        const auto& local  = local_layout_.displays;

        if (remote.empty()) {
            result.success = false;
            result.error_message = "Remote layout is empty";
            return result;
        }

        // Strategy: match by display index if mirroring is preferred,
        // otherwise try to match resolution and position
        result.mappings.reserve(remote.size());

        for (std::size_t ri = 0; ri < remote.size(); ++ri) {
            const auto& rd = remote[ri];
            DisplayMapping mapping;
            mapping.remote_display_id = rd.id;
            mapping.remote_index      = static_cast<std::uint32_t>(ri);

            if (prefs.mirror_mode) {
                // Mirror: map remote display to local display by index
                if (ri < local.size()) {
                    mapping.local_display_id  = local[ri].id;
                    mapping.local_index       = static_cast<std::uint32_t>(ri);
                    mapping.scale_factor      = std::min(
                        static_cast<float>(local[ri].width)  / static_cast<float>(rd.width),
                        static_cast<float>(local[ri].height) / static_cast<float>(rd.height));
                } else {
                    mapping.local_display_id  = local[0].id;
                    mapping.local_index       = 0;
                    mapping.scale_factor      = std::min(
                        static_cast<float>(local[0].width)  / static_cast<float>(rd.width),
                        static_cast<float>(local[0].height) / static_cast<float>(rd.height));
                }
            } else {
                // Extended: find best local display for this remote display
                mapping = find_best_match(rd, local, prefs);
            }

            mapping.quality_level = prefs.quality_level;
            result.mappings.push_back(mapping);
        }

        result.success = true;
        result.total_displays = static_cast<std::uint32_t>(remote.size());
        return result;
    }

    // Get suggested resolution for a remote display
    DisplaySize suggest_resolution(std::uint32_t remote_display_id) {
        std::lock_guard lock(mutex_);
        DisplaySize sz{1920, 1080};

        if (!remote_layout_received_) return sz;

        const auto* rd = remote_layout_.find_by_id(remote_display_id);
        if (rd) {
            sz.width  = rd->width;
            sz.height = rd->height;
        }
        return sz;
    }

    const DisplayLayout& local_layout() const {
        std::lock_guard lock(mutex_);
        return local_layout_;
    }

    const DisplayLayout& remote_layout() const {
        std::lock_guard lock(mutex_);
        return remote_layout_;
    }

    bool has_remote_layout() const {
        std::lock_guard lock(mutex_);
        return remote_layout_received_;
    }

    void reset() {
        std::lock_guard lock(mutex_);
        local_layout_  = DisplayLayout{};
        remote_layout_ = DisplayLayout{};
        remote_layout_received_ = false;
    }

private:
    DisplayMapping find_best_match(const DisplayInfo& remote,
                                   const std::vector<DisplayInfo>& locals,
                                   const NegotiatePreferences& prefs) {
        DisplayMapping best;
        best.remote_display_id = remote.id;

        if (locals.empty()) {
            best.local_display_id  = remote.id;
            best.local_index       = 0;
            best.scale_factor      = 1.0f;
            return best;
        }

        // Scoring: prefer matching resolution, then position proximity
        float best_score = -1.0f;
        for (std::size_t i = 0; i < locals.size(); ++i) {
            const auto& ld = locals[i];

            float res_score = 1.0f - std::abs(
                static_cast<float>(ld.width * ld.height) /
                static_cast<float>(remote.width * remote.height) - 1.0f);
            res_score = std::max(0.0f, std::min(1.0f, res_score));

            float pos_score = 1.0f;
            if (prefs.prefer_position_match) {
                float dx = std::abs(static_cast<float>(ld.origin_x - remote.origin_x));
                float dy = std::abs(static_cast<float>(ld.origin_y - remote.origin_y));
                float max_dim = 10000.0f;
                pos_score = 1.0f - std::min((dx + dy) / max_dim, 1.0f);
            }

            float primary_bonus = (ld.is_primary && prefs.prefer_primary) ? 0.2f : 0.0f;

            float score = res_score * 0.6f + pos_score * 0.3f + primary_bonus;

            if (score > best_score) {
                best_score = score;
                best.local_display_id = ld.id;
                best.local_index      = static_cast<std::uint32_t>(i);
                best.scale_factor     = std::min(
                    static_cast<float>(ld.width)  / static_cast<float>(remote.width),
                    static_cast<float>(ld.height) / static_cast<float>(remote.height));
            }
        }

        if (best_score < 0.0f) {
            best.local_display_id = locals[0].id;
            best.local_index      = 0;
            best.scale_factor     = 1.0f;
        }

        return best;
    }

    mutable std::mutex mutex_;
    DisplayLayout local_layout_;
    DisplayLayout remote_layout_;
    bool remote_layout_received_ = false;
};

// --- DisplayLayoutNegotiator public interface -------------------------------

DisplayLayoutNegotiator::DisplayLayoutNegotiator()
    : impl_(std::make_unique<Impl>()) {}

DisplayLayoutNegotiator::~DisplayLayoutNegotiator() = default;

void DisplayLayoutNegotiator::set_local_layout(DisplayLayout layout) {
    impl_->set_local_layout(std::move(layout));
}

void DisplayLayoutNegotiator::set_remote_layout(DisplayLayout layout) {
    impl_->set_remote_layout(std::move(layout));
}

NegotiatorDisplayResult DisplayLayoutNegotiator::negotiate(
    const NegotiatePreferences& prefs) {
    return impl_->negotiate(prefs);
}

DisplaySize DisplayLayoutNegotiator::suggest_resolution(
    std::uint32_t remote_display_id) {
    return impl_->suggest_resolution(remote_display_id);
}

const DisplayLayout& DisplayLayoutNegotiator::local_layout() const {
    return impl_->local_layout();
}

const DisplayLayout& DisplayLayoutNegotiator::remote_layout() const {
    return impl_->remote_layout();
}

bool DisplayLayoutNegotiator::has_remote_layout() const {
    return impl_->has_remote_layout();
}

void DisplayLayoutNegotiator::reset() {
    impl_->reset();
}

// ============================================================================
// 4. Keyboard Layout Synchronization
// ============================================================================

// --- KeyboardLayout ---------------------------------------------------------

std::string KeyboardLayout::to_short_string() const {
    return language_code + "-" + variant;
}

bool KeyboardLayout::operator==(const KeyboardLayout& other) const {
    return layout_id == other.layout_id &&
           language_code == other.language_code &&
           variant == other.variant;
}

// --- KeyMapping -------------------------------------------------------------

bool KeyMapping::operator==(const KeyMapping& other) const {
    return scancode == other.scancode && keycode == other.keycode &&
           modifiers == other.modifiers && character == other.character;
}

// --- KeyboardLayoutSync::Impl -----------------------------------------------

class KeyboardLayoutSync::Impl {
public:
    void set_local_layout(KeyboardLayout layout) {
        std::lock_guard lock(mutex_);
        local_layout_ = std::move(layout);
        SPDLOG_DEBUG("[helper] keyboard sync: local layout set to {}-{}",
                     local_layout_.language_code, local_layout_.variant);
    }

    void set_remote_layout(KeyboardLayout layout) {
        std::lock_guard lock(mutex_);
        remote_layout_ = std::move(layout);
        remote_received_ = true;
    }

    // Build a mapping from local scancodes to remote keycodes
    std::vector<KeyMapping> build_mapping() {
        std::lock_guard lock(mutex_);

        std::vector<KeyMapping> mappings;

        if (!remote_received_) return mappings;

        // If layouts match exactly, no remapping needed — identity mapping
        if (local_layout_ == remote_layout_) {
            SPDLOG_DEBUG("[helper] keyboard sync: layouts identical, identity mapping");
            return mappings;
        }

        // Build mapping based on known layout differences
        mappings = generate_layout_mapping(local_layout_, remote_layout_);

        SPDLOG_DEBUG("[helper] keyboard sync: built {} key mappings", mappings.size());
        return mappings;
    }

    // Check if layouts are compatible without remapping
    bool are_layouts_compatible() const {
        std::lock_guard lock(mutex_);
        return local_layout_ == remote_layout_;
    }

    bool needs_remapping() const {
        std::lock_guard lock(mutex_);
        return remote_received_ && !(local_layout_ == remote_layout_);
    }

    const KeyboardLayout& local_layout() const {
        std::lock_guard lock(mutex_);
        return local_layout_;
    }

    const KeyboardLayout& remote_layout() const {
        std::lock_guard lock(mutex_);
        return remote_layout_;
    }

    void reset() {
        std::lock_guard lock(mutex_);
        local_layout_   = KeyboardLayout{};
        remote_layout_  = KeyboardLayout{};
        remote_received_ = false;
    }

private:
    // Generate key mappings between two layouts
    // This handles common layout differences (QWERTY<->AZERTY, US<->UK, etc.)
    std::vector<KeyMapping> generate_layout_mapping(
        const KeyboardLayout& local, const KeyboardLayout& remote) {

        std::vector<KeyMapping> mappings;

        // Known layout difference tables
        // AZERTY vs QWERTY differences
        static const std::unordered_map<std::string, std::unordered_map<char, char>> kLayoutDiff = {
            {"fr-azerty", {
                {'a','q'},{'z','w'},{'q','a'},{'w','z'},{'m',','},{','m'},
                {'1','&'},{'2','~'},{'3','#'},{'4','\''},{'5','('},
                {'6','-'},{'7','`'},{'8','_'},{'9','^'},{'0','@'},
            }},
            {"de-qwertz", {
                {'y','z'},{'z','y'},
                {'-','?'},{'=','`'},
            }},
            {"uk-qwerty", {
                {'#','\\'},{'\\','#'},{'@','"'},{'"','@'},
                {'~','|'},{'|','~'},
            }},
        };

        std::string key = local.language_code + "-" + local.variant;
        auto it = kLayoutDiff.find(key);
        if (it != kLayoutDiff.end()) {
            for (const auto& [local_ch, remote_ch] : it->second) {
                KeyMapping km;
                km.character = std::string(1, remote_ch);
                km.source_char = std::string(1, local_ch);
                mappings.push_back(km);
            }
        }

        return mappings;
    }

    mutable std::mutex mutex_;
    KeyboardLayout local_layout_;
    KeyboardLayout remote_layout_;
    bool remote_received_ = false;
};

// --- KeyboardLayoutSync public interface ------------------------------------

KeyboardLayoutSync::KeyboardLayoutSync()
    : impl_(std::make_unique<Impl>()) {}

KeyboardLayoutSync::~KeyboardLayoutSync() = default;

void KeyboardLayoutSync::set_local_layout(KeyboardLayout layout) {
    impl_->set_local_layout(std::move(layout));
}

void KeyboardLayoutSync::set_remote_layout(KeyboardLayout layout) {
    impl_->set_remote_layout(std::move(layout));
}

std::vector<KeyMapping> KeyboardLayoutSync::build_mapping() {
    return impl_->build_mapping();
}

bool KeyboardLayoutSync::are_layouts_compatible() const {
    return impl_->are_layouts_compatible();
}

bool KeyboardLayoutSync::needs_remapping() const {
    return impl_->needs_remapping();
}

const KeyboardLayout& KeyboardLayoutSync::local_layout() const {
    return impl_->local_layout();
}

const KeyboardLayout& KeyboardLayoutSync::remote_layout() const {
    return impl_->remote_layout();
}

void KeyboardLayoutSync::reset() {
    impl_->reset();
}

// ============================================================================
// 5. Clipboard Format Negotiation
// ============================================================================

// --- ClipboardFormat --------------------------------------------------------

bool ClipboardFormat::operator==(const ClipboardFormat& other) const {
    return format_id == other.format_id && mime_type == other.mime_type;
}

std::size_t ClipboardFormatHash::operator()(const ClipboardFormat& f) const {
    return std::hash<std::uint32_t>{}(f.format_id) ^
           (std::hash<std::string>{}(f.mime_type) << 1);
}

// --- ClipboardFormatNegotiator::Impl ----------------------------------------

class ClipboardFormatNegotiator::Impl {
public:
    // Register a locally supported format
    void register_local_format(ClipboardFormat format) {
        std::lock_guard lock(mutex_);
        local_formats_.push_back(std::move(format));
        std::sort(local_formats_.begin(), local_formats_.end(),
                  [](const auto& a, const auto& b) { return a.priority > b.priority; });
    }

    // Advertise remote formats
    void set_remote_formats(std::vector<ClipboardFormat> formats) {
        std::lock_guard lock(mutex_);
        remote_formats_ = std::move(formats);
        remote_received_ = true;
    }

    // Find best common format for the given MIME type
    std::optional<ClipboardFormat> find_best_common(
        std::string_view preferred_mime) {
        std::lock_guard lock(mutex_);

        if (!remote_received_) return std::nullopt;

        // Try preferred MIME type first
        for (const auto& local : local_formats_) {
            if (local.mime_type == preferred_mime) {
                for (const auto& remote : remote_formats_) {
                    if (remote.mime_type == preferred_mime) {
                        ClipboardFormat negotiated = local;
                        negotiated.format_id = remote.format_id;
                        return negotiated;
                    }
                }
            }
        }

        // Fall back to priority-ordered common formats
        for (const auto& local : local_formats_) {
            for (const auto& remote : remote_formats_) {
                if (local.mime_type == remote.mime_type) {
                    ClipboardFormat negotiated = local;
                    negotiated.format_id = remote.format_id;
                    return negotiated;
                }
            }
        }

        return std::nullopt;
    }

    // List all common formats
    std::vector<ClipboardFormat> list_common_formats() {
        std::lock_guard lock(mutex_);

        std::vector<ClipboardFormat> common;
        if (!remote_received_) return common;

        std::unordered_set<std::string> seen_mimes;
        for (const auto& local : local_formats_) {
            for (const auto& remote : remote_formats_) {
                if (local.mime_type == remote.mime_type &&
                    !seen_mimes.count(local.mime_type)) {
                    ClipboardFormat cf = local;
                    cf.format_id = remote.format_id;
                    common.push_back(cf);
                    seen_mimes.insert(local.mime_type);
                }
            }
        }
        return common;
    }

    // Check if a specific format is supported by both sides
    bool is_format_supported(std::string_view mime_type) const {
        std::lock_guard lock(mutex_);

        bool local_ok = std::any_of(local_formats_.begin(), local_formats_.end(),
            [&](const auto& f) { return f.mime_type == mime_type; });
        bool remote_ok = std::any_of(remote_formats_.begin(), remote_formats_.end(),
            [&](const auto& f) { return f.mime_type == mime_type; });

        return local_ok && remote_ok;
    }

    const std::vector<ClipboardFormat>& local_formats() const {
        std::lock_guard lock(mutex_);
        return local_formats_;
    }

    const std::vector<ClipboardFormat>& remote_formats() const {
        std::lock_guard lock(mutex_);
        return remote_formats_;
    }

    bool has_remote_formats() const {
        std::lock_guard lock(mutex_);
        return remote_received_;
    }

    void reset() {
        std::lock_guard lock(mutex_);
        local_formats_.clear();
        remote_formats_.clear();
        remote_received_ = false;
    }

private:
    mutable std::mutex mutex_;
    std::vector<ClipboardFormat> local_formats_;
    std::vector<ClipboardFormat> remote_formats_;
    bool remote_received_ = false;
};

// --- ClipboardFormatNegotiator public interface -----------------------------

ClipboardFormatNegotiator::ClipboardFormatNegotiator()
    : impl_(std::make_unique<Impl>()) {}

ClipboardFormatNegotiator::~ClipboardFormatNegotiator() = default;

void ClipboardFormatNegotiator::register_local_format(ClipboardFormat format) {
    impl_->register_local_format(std::move(format));
}

void ClipboardFormatNegotiator::set_remote_formats(
    std::vector<ClipboardFormat> formats) {
    impl_->set_remote_formats(std::move(formats));
}

std::optional<ClipboardFormat> ClipboardFormatNegotiator::find_best_common(
    std::string_view preferred_mime) {
    return impl_->find_best_common(preferred_mime);
}

std::vector<ClipboardFormat> ClipboardFormatNegotiator::list_common_formats() {
    return impl_->list_common_formats();
}

bool ClipboardFormatNegotiator::is_format_supported(
    std::string_view mime_type) const {
    return impl_->is_format_supported(mime_type);
}

const std::vector<ClipboardFormat>& ClipboardFormatNegotiator::local_formats() const {
    return impl_->local_formats();
}

const std::vector<ClipboardFormat>& ClipboardFormatNegotiator::remote_formats() const {
    return impl_->remote_formats();
}

bool ClipboardFormatNegotiator::has_remote_formats() const {
    return impl_->has_remote_formats();
}

void ClipboardFormatNegotiator::reset() {
    impl_->reset();
}

// ============================================================================
// 6. Audio Device Selection & Format Matching
// ============================================================================

// --- AudioFormat ------------------------------------------------------------

bool AudioFormat::is_valid() const {
    return sample_rate >= detail::kAudioSampleRateMin &&
           sample_rate <= detail::kAudioSampleRateMax &&
           channels >= detail::kAudioChannelsMin &&
           channels <= detail::kAudioChannelsMax &&
           bit_depth == 8 || bit_depth == 16 || bit_depth == 24 || bit_depth == 32;
}

std::string AudioFormat::to_string() const {
    std::ostringstream oss;
    oss << sample_rate << "Hz " << static_cast<int>(channels) << "ch "
        << static_cast<int>(bit_depth) << "bit "
        << (is_float ? "float" : "int") << " "
        << (is_signed ? "signed" : "unsigned");
    return oss.str();
}

bool AudioFormat::operator==(const AudioFormat& other) const {
    return sample_rate == other.sample_rate &&
           channels == other.channels &&
           bit_depth == other.bit_depth &&
           is_float == other.is_float &&
           is_signed == other.is_signed;
}

// --- AudioDevice ------------------------------------------------------------

std::string AudioDevice::to_string() const {
    std::ostringstream oss;
    oss << name << " [" << device_id << "] "
        << (is_input ? "input" : "output")
        << (is_default ? " (default)" : "");
    return oss.str();
}

// --- AudioDeviceMatcher::Impl -----------------------------------------------

class AudioDeviceMatcher::Impl {
public:
    void add_local_device(AudioDevice device) {
        std::lock_guard lock(mutex_);
        if (device.is_input) {
            local_inputs_.push_back(std::move(device));
        } else {
            local_outputs_.push_back(std::move(device));
        }
    }

    void set_remote_formats(std::vector<AudioFormat> formats, bool is_input) {
        std::lock_guard lock(mutex_);
        if (is_input) {
            remote_input_formats_ = std::move(formats);
        } else {
            remote_output_formats_ = std::move(formats);
        }
        remote_formats_received_ = true;
    }

    // Find best matching audio format
    AudioMatchResult match_format(bool is_input) {
        std::lock_guard lock(mutex_);

        AudioMatchResult result;
        const auto& remote_formats = is_input ? remote_input_formats_
                                              : remote_output_formats_;
        const auto& local_devices  = is_input ? local_inputs_ : local_outputs_;

        if (remote_formats.empty()) {
            result.success = false;
            result.error = "No remote formats advertised";
            return result;
        }

        if (local_devices.empty()) {
            result.success = false;
            result.error = "No local devices available";
            return result;
        }

        // Prefer default device
        const AudioDevice* device = &local_devices[0];
        for (const auto& d : local_devices) {
            if (d.is_default) { device = &d; break; }
        }
        result.device = *device;

        // Find closest format match from remote's advertised formats
        AudioFormat desired{48000, 2, 16, false, true};  // CD-quality default
        float best_score = -1.0f;

        for (const auto& rf : remote_formats) {
            float score = rate_format(rf, desired);
            if (score > best_score) {
                best_score = score;
                result.format = rf;
            }
        }

        // Also check what the local device natively supports
        if (best_score < 0.0f) {
            // Fallback: use the first remote format
            result.format = remote_formats[0];
        }

        result.success = true;
        return result;
    }

    // List compatible audio formats between local devices and remote
    std::vector<AudioFormat> compatible_formats(bool is_input) const {
        std::lock_guard lock(mutex_);

        std::vector<AudioFormat> result;
        const auto& remote_formats = is_input ? remote_input_formats_
                                              : remote_output_formats_;
        // Return all remote formats that pass basic validation
        for (const auto& f : remote_formats) {
            if (f.is_valid()) {
                result.push_back(f);
            }
        }
        return result;
    }

    const std::vector<AudioDevice>& local_inputs() const {
        std::lock_guard lock(mutex_);
        return local_inputs_;
    }

    const std::vector<AudioDevice>& local_outputs() const {
        std::lock_guard lock(mutex_);
        return local_outputs_;
    }

    bool has_remote_formats() const {
        std::lock_guard lock(mutex_);
        return remote_formats_received_;
    }

    void reset() {
        std::lock_guard lock(mutex_);
        local_inputs_.clear();
        local_outputs_.clear();
        remote_input_formats_.clear();
        remote_output_formats_.clear();
        remote_formats_received_ = false;
    }

private:
    // Score a format against the desired format (higher = better match)
    float rate_format(const AudioFormat& candidate, const AudioFormat& desired) {
        float score = 0.0f;

        // Sample rate: exact match is best, otherwise ratio
        if (candidate.sample_rate == desired.sample_rate) {
            score += 2.0f;
        } else {
            float ratio = std::min(
                static_cast<float>(candidate.sample_rate) / static_cast<float>(desired.sample_rate),
                static_cast<float>(desired.sample_rate) / static_cast<float>(candidate.sample_rate));
            score += ratio;
        }

        // Channels: exact match best, more channels OK (can be downmixed)
        if (candidate.channels == desired.channels) {
            score += 1.0f;
        } else if (candidate.channels >= desired.channels) {
            score += 0.5f;
        }

        // Bit depth: higher is generally better
        if (candidate.bit_depth == desired.bit_depth) {
            score += 1.0f;
        } else if (candidate.bit_depth >= desired.bit_depth) {
            score += 0.5f;
        }

        return score;
    }

    mutable std::mutex mutex_;
    std::vector<AudioDevice> local_inputs_;
    std::vector<AudioDevice> local_outputs_;
    std::vector<AudioFormat> remote_input_formats_;
    std::vector<AudioFormat> remote_output_formats_;
    bool remote_formats_received_ = false;
};

// --- AudioDeviceMatcher public interface ------------------------------------

AudioDeviceMatcher::AudioDeviceMatcher()
    : impl_(std::make_unique<Impl>()) {}

AudioDeviceMatcher::~AudioDeviceMatcher() = default;

void AudioDeviceMatcher::add_local_device(AudioDevice device) {
    impl_->add_local_device(std::move(device));
}

void AudioDeviceMatcher::set_remote_formats(std::vector<AudioFormat> formats, bool is_input) {
    impl_->set_remote_formats(std::move(formats), is_input);
}

AudioMatchResult AudioDeviceMatcher::match_format(bool is_input) {
    return impl_->match_format(is_input);
}

std::vector<AudioFormat> AudioDeviceMatcher::compatible_formats(bool is_input) const {
    return impl_->compatible_formats(is_input);
}

const std::vector<AudioDevice>& AudioDeviceMatcher::local_inputs() const {
    return impl_->local_inputs();
}

const std::vector<AudioDevice>& AudioDeviceMatcher::local_outputs() const {
    return impl_->local_outputs();
}

bool AudioDeviceMatcher::has_remote_formats() const {
    return impl_->has_remote_formats();
}

void AudioDeviceMatcher::reset() {
    impl_->reset();
}

// ============================================================================
// 7. Timezone Offset Synchronization
// ============================================================================

// --- TimezoneOffset ---------------------------------------------------------

std::string TimezoneOffset::to_iso8601() const {
    // Format: ±HH:MM
    char buf[8];
    int total_min = std::abs(offset_minutes);
    int hh = total_min / 60;
    int mm = total_min % 60;
    std::snprintf(buf, sizeof(buf), "%c%02d:%02d",
                  offset_minutes >= 0 ? '+' : '-', hh, mm);
    return buf;
}

std::string TimezoneOffset::to_display_string() const {
    if (iana_name.empty()) {
        return "UTC" + to_iso8601();
    }
    return iana_name + " (UTC" + to_iso8601() + ")";
}

// --- TimezoneSync -----------------------------------------------------------

TimezoneSync::TimezoneSync()
    : local_offset_()
    , remote_offset_()
    , remote_received_(false)
    , synchronized_(false) {}

void TimezoneSync::set_local_offset(TimezoneOffset offset) {
    std::lock_guard lock(mutex_);
    local_offset_ = std::move(offset);
    SPDLOG_DEBUG("[helper] timezone sync: local offset = {}",
                 local_offset_.to_display_string());
}

void TimezoneSync::set_remote_offset(TimezoneOffset offset) {
    std::lock_guard lock(mutex_);
    remote_offset_ = std::move(offset);
    remote_received_ = true;
    SPDLOG_DEBUG("[helper] timezone sync: remote offset = {}",
                 remote_offset_.to_display_string());
}

bool TimezoneSync::synchronize() {
    std::lock_guard lock(mutex_);

    if (!remote_received_) {
        SPDLOG_WARN("[helper] timezone sync: cannot sync, no remote offset");
        return false;
    }

    // Compute the delta between local and remote time
    delta_minutes_ = local_offset_.offset_minutes - remote_offset_.offset_minutes;
    synchronized_ = true;

    SPDLOG_INFO("[helper] timezone sync: delta = {} minutes", delta_minutes_);
    return true;
}

int TimezoneSync::delta_minutes() const {
    std::lock_guard lock(mutex_);
    return delta_minutes_;
}

// Convert a remote timestamp to local time
std::chrono::system_clock::time_point TimezoneSync::remote_to_local(
    std::chrono::system_clock::time_point remote_time) const {
    std::lock_guard lock(mutex_);
    if (!synchronized_) return remote_time;
    return remote_time + std::chrono::minutes(delta_minutes_);
}

// Convert a local timestamp to remote time
std::chrono::system_clock::time_point TimezoneSync::local_to_remote(
    std::chrono::system_clock::time_point local_time) const {
    std::lock_guard lock(mutex_);
    if (!synchronized_) return local_time;
    return local_time - std::chrono::minutes(delta_minutes_);
}

bool TimezoneSync::is_synchronized() const {
    std::lock_guard lock(mutex_);
    return synchronized_;
}

const TimezoneOffset& TimezoneSync::local_offset() const {
    std::lock_guard lock(mutex_);
    return local_offset_;
}

const TimezoneOffset& TimezoneSync::remote_offset() const {
    std::lock_guard lock(mutex_);
    return remote_offset_;
}

void TimezoneSync::reset() {
    std::lock_guard lock(mutex_);
    local_offset_  = TimezoneOffset{};
    remote_offset_ = TimezoneOffset{};
    remote_received_ = false;
    synchronized_    = false;
    delta_minutes_   = 0;
}

// Convenience: auto-detect local timezone offset
TimezoneOffset detect_local_timezone() {
    TimezoneOffset tz;
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    // Use gmtime/localtime to compute offset
    std::tm utc_tm{};
    std::tm local_tm{};
#ifdef _WIN32
    gmtime_s(&utc_tm, &time_t_now);
    localtime_s(&local_tm, &time_t_now);
#else
    gmtime_r(&time_t_now, &utc_tm);
    localtime_r(&time_t_now, &local_tm);
#endif

    // Compute offset in minutes
    int utc_min  = utc_tm.tm_hour * 60 + utc_tm.tm_min;
    int local_min = local_tm.tm_hour * 60 + local_tm.tm_min;

    // Handle day wrap
    int day_diff = local_tm.tm_yday - utc_tm.tm_yday;
    if (day_diff < -1) day_diff += 365;
    if (day_diff > 1) day_diff -= 365;

    tz.offset_minutes = (local_min - utc_min) + (day_diff * 24 * 60);

    // Get IANA timezone name on supported platforms
#if defined(__linux__) || defined(__APPLE__)
    if (std::filesystem::exists("/etc/localtime")) {
        try {
            tz.iana_name = std::filesystem::read_symlink("/etc/localtime")
                               .filename()
                               .string();
            // Check if it's a relative path inside zoneinfo
            auto p = std::filesystem::read_symlink("/etc/localtime");
            auto str = p.string();
            auto pos = str.find("zoneinfo/");
            if (pos != std::string::npos) {
                tz.iana_name = str.substr(pos + 9);
            } else {
                pos = str.find("zoneinfo\\");
                if (pos != std::string::npos) {
                    tz.iana_name = str.substr(pos + 9);
                }
            }
        } catch (...) {
            // Fallback: use offset-only
        }
    }
#endif

    return tz;
}

// ============================================================================
// 8. File Transfer Resume Support (Checksum-Based)
// ============================================================================

// --- FileChecksum -----------------------------------------------------------

std::string FileChecksum::to_hex() const {
    std::ostringstream oss;
    oss << algorithm << ":";
    for (auto b : hash) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(b);
    }
    return oss.str();
}

bool FileChecksum::operator==(const FileChecksum& other) const {
    return algorithm == other.algorithm && hash == other.hash;
}

// --- FileTransferState ------------------------------------------------------

std::string_view to_string(FileTransferState s) {
    switch (s) {
        case FileTransferState::kPending:      return "pending";
        case FileTransferState::kInProgress:   return "in_progress";
        case FileTransferState::kPaused:       return "paused";
        case FileTransferState::kCompleted:    return "completed";
        case FileTransferState::kFailed:       return "failed";
        case FileTransferState::kVerifying:    return "verifying";
        default:                               return "unknown";
    }
}

// --- FileTransferResumeManager::Impl ----------------------------------------

class FileTransferResumeManager::Impl {
public:
    // Register a new file transfer
    std::uint64_t register_transfer(const std::string& file_path,
                                     std::uint64_t file_size) {
        std::lock_guard lock(mutex_);

        static std::atomic<std::uint64_t> next_id{1};
        std::uint64_t id = next_id.fetch_add(1);

        ResumeEntry entry;
        entry.transfer_id  = id;
        entry.file_path    = file_path;
        entry.file_size    = file_size;
        entry.bytes_transferred = 0;
        entry.state        = FileTransferState::kPending;
        entry.created_at   = std::chrono::steady_clock::now();

        entries_[id] = std::move(entry);
        SPDLOG_DEBUG("[helper] file transfer registered: id={}, path={}, size={}",
                     id, file_path, file_size);
        return id;
    }

    // Update progress
    void update_progress(std::uint64_t transfer_id, std::uint64_t bytes_transferred) {
        std::lock_guard lock(mutex_);

        auto it = entries_.find(transfer_id);
        if (it == entries_.end()) {
            SPDLOG_WARN("[helper] file transfer: unknown id {}", transfer_id);
            return;
        }

        it->second.bytes_transferred = bytes_transferred;
        it->second.state = FileTransferState::kInProgress;
        it->second.last_update = std::chrono::steady_clock::now();
    }

    // Compute and store checksum for a completed transfer
    bool finalize_with_checksum(std::uint64_t transfer_id,
                                 const FileChecksum& checksum) {
        std::lock_guard lock(mutex_);

        auto it = entries_.find(transfer_id);
        if (it == entries_.end()) return false;

        it->second.checksum = checksum;
        it->second.state = FileTransferState::kVerifying;
        return true;
    }

    // Mark as completed
    void mark_completed(std::uint64_t transfer_id) {
        std::lock_guard lock(mutex_);

        auto it = entries_.find(transfer_id);
        if (it == entries_.end()) return;

        it->second.state = FileTransferState::kCompleted;
        it->second.completed_at = std::chrono::steady_clock::now();
    }

    // Mark as failed with error
    void mark_failed(std::uint64_t transfer_id, const std::string& error) {
        std::lock_guard lock(mutex_);

        auto it = entries_.find(transfer_id);
        if (it == entries_.end()) return;

        it->second.state = FileTransferState::kFailed;
        it->second.error_message = error;
    }

    // Check if a file can be resumed
    ResumeCheckResult check_resumable(const std::string& file_path,
                                       std::uint64_t expected_size) {
        std::lock_guard lock(mutex_);

        ResumeCheckResult result;

        // Look for an in-progress or paused transfer for this file
        for (const auto& [id, entry] : entries_) {
            if (entry.file_path == file_path &&
                (entry.state == FileTransferState::kInProgress ||
                 entry.state == FileTransferState::kPaused)) {

                // Check if the partial file exists and matches expected size
                std::error_code ec;
                auto partial_size = std::filesystem::file_size(file_path, ec);
                if (!ec && partial_size <= expected_size) {
                    result.can_resume        = true;
                    result.transfer_id       = id;
                    result.bytes_transferred  = entry.bytes_transferred;
                    result.resume_offset      = partial_size;
                    result.matching_checksums = false; // need to verify

                    // Verify checksum of partial data if available
                    if (!entry.checksum.hash.empty()) {
                        auto partial_checksum = compute_file_checksum(
                            file_path, 0, partial_size, entry.checksum.algorithm);
                        result.matching_checksums = (partial_checksum == entry.checksum);
                        result.partial_checksum = partial_checksum;
                    }

                    return result;
                }
            }
        }

        result.can_resume = false;
        return result;
    }

    // Get resume state for a transfer
    std::optional<ResumeEntry> get_state(std::uint64_t transfer_id) const {
        std::lock_guard lock(mutex_);

        auto it = entries_.find(transfer_id);
        if (it == entries_.end()) return std::nullopt;
        return it->second;
    }

    // Pause a transfer (for later resume)
    void pause(std::uint64_t transfer_id) {
        std::lock_guard lock(mutex_);

        auto it = entries_.find(transfer_id);
        if (it == entries_.end()) return;
        it->second.state = FileTransferState::kPaused;
    }

    // Resume a paused transfer
    bool resume(std::uint64_t transfer_id) {
        std::lock_guard lock(mutex_);

        auto it = entries_.find(transfer_id);
        if (it == entries_.end()) return false;
        if (it->second.state != FileTransferState::kPaused) return false;
        it->second.state = FileTransferState::kInProgress;
        return true;
    }

    // List all active transfers
    std::vector<ResumeEntry> list_active() const {
        std::lock_guard lock(mutex_);

        std::vector<ResumeEntry> active;
        for (const auto& [id, entry] : entries_) {
            if (entry.state == FileTransferState::kInProgress ||
                entry.state == FileTransferState::kPaused ||
                entry.state == FileTransferState::kPending) {
                active.push_back(entry);
            }
        }
        return active;
    }

    // Clean up completed/failed entries older than a duration
    void cleanup(std::chrono::seconds max_age) {
        std::lock_guard lock(mutex_);

        auto now = std::chrono::steady_clock::now();
        for (auto it = entries_.begin(); it != entries_.end(); ) {
            const auto& entry = it->second;
            if ((entry.state == FileTransferState::kCompleted ||
                 entry.state == FileTransferState::kFailed) &&
                entry.completed_at.has_value() &&
                (now - *entry.completed_at) > max_age) {
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void reset() {
        std::lock_guard lock(mutex_);
        entries_.clear();
    }

    // Compute checksum for a file region
    static FileChecksum compute_file_checksum(const std::string& file_path,
                                               std::uint64_t offset,
                                               std::uint64_t length,
                                               const std::string& algorithm) {
        FileChecksum result;
        result.algorithm = algorithm;

        if (algorithm == "sha256") {
            // Simplified: just read and compute a simple hash for demo
            // In production, use OpenSSL or Crypto++ SHA-256
            result.hash.resize(32, 0);
            std::ifstream file(file_path, std::ios::binary);
            if (file) {
                file.seekg(static_cast<std::streamoff>(offset));
                std::vector<char> buf(std::min(length,
                    static_cast<std::uint64_t>(detail::kChecksumBlockSize)));
                std::uint64_t remaining = length;
                std::uint64_t simple_hash = 0;
                while (remaining > 0 && file.read(buf.data(),
                        std::min(static_cast<std::streamsize>(buf.size()),
                                 static_cast<std::streamsize>(remaining)))) {
                    auto read_count = static_cast<std::size_t>(file.gcount());
                    for (std::size_t i = 0; i < read_count; ++i) {
                        simple_hash = simple_hash * 31 +
                            static_cast<std::uint8_t>(buf[i]);
                    }
                    remaining -= read_count;
                }
                // Store simple_hash in first 8 bytes
                for (int i = 0; i < 8; ++i) {
                    result.hash[i] = static_cast<std::uint8_t>(
                        (simple_hash >> (i * 8)) & 0xFF);
                }
            }
        } else {
            // Default: CRC32-style simple hash
            result.hash.resize(4, 0);
            std::ifstream file(file_path, std::ios::binary);
            if (file) {
                file.seekg(static_cast<std::streamoff>(offset));
                std::vector<char> buf(detail::kChecksumBlockSize);
                std::uint32_t crc = 0xFFFFFFFF;
                std::uint64_t remaining = length;
                while (remaining > 0 && file.read(buf.data(),
                        std::min(static_cast<std::streamsize>(buf.size()),
                                 static_cast<std::streamsize>(remaining)))) {
                    auto count = static_cast<std::size_t>(file.gcount());
                    for (std::size_t i = 0; i < count; ++i) {
                        crc ^= static_cast<std::uint8_t>(buf[i]);
                        for (int b = 0; b < 8; ++b) {
                            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
                        }
                    }
                    remaining -= count;
                }
                crc ^= 0xFFFFFFFF;
                result.hash[0] = static_cast<std::uint8_t>(crc & 0xFF);
                result.hash[1] = static_cast<std::uint8_t>((crc >> 8) & 0xFF);
                result.hash[2] = static_cast<std::uint8_t>((crc >> 16) & 0xFF);
                result.hash[3] = static_cast<std::uint8_t>((crc >> 24) & 0xFF);
            }
        }

        return result;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, ResumeEntry> entries_;
};

// --- FileTransferResumeManager public interface -----------------------------

FileTransferResumeManager::FileTransferResumeManager()
    : impl_(std::make_unique<Impl>()) {}

FileTransferResumeManager::~FileTransferResumeManager() = default;

std::uint64_t FileTransferResumeManager::register_transfer(
    const std::string& file_path, std::uint64_t file_size) {
    return impl_->register_transfer(file_path, file_size);
}

void FileTransferResumeManager::update_progress(
    std::uint64_t transfer_id, std::uint64_t bytes_transferred) {
    impl_->update_progress(transfer_id, bytes_transferred);
}

bool FileTransferResumeManager::finalize_with_checksum(
    std::uint64_t transfer_id, const FileChecksum& checksum) {
    return impl_->finalize_with_checksum(transfer_id, checksum);
}

void FileTransferResumeManager::mark_completed(std::uint64_t transfer_id) {
    impl_->mark_completed(transfer_id);
}

void FileTransferResumeManager::mark_failed(
    std::uint64_t transfer_id, const std::string& error) {
    impl_->mark_failed(transfer_id, error);
}

ResumeCheckResult FileTransferResumeManager::check_resumable(
    const std::string& file_path, std::uint64_t expected_size) {
    return impl_->check_resumable(file_path, expected_size);
}

std::optional<ResumeEntry> FileTransferResumeManager::get_state(
    std::uint64_t transfer_id) const {
    return impl_->get_state(transfer_id);
}

void FileTransferResumeManager::pause(std::uint64_t transfer_id) {
    impl_->pause(transfer_id);
}

bool FileTransferResumeManager::resume(std::uint64_t transfer_id) {
    return impl_->resume(transfer_id);
}

std::vector<ResumeEntry> FileTransferResumeManager::list_active() const {
    return impl_->list_active();
}

void FileTransferResumeManager::cleanup(std::chrono::seconds max_age) {
    impl_->cleanup(max_age);
}

void FileTransferResumeManager::reset() {
    impl_->reset();
}

FileChecksum FileTransferResumeManager::compute_file_checksum(
    const std::string& file_path, std::uint64_t offset,
    std::uint64_t length, const std::string& algorithm) {
    return Impl::compute_file_checksum(file_path, offset, length, algorithm);
}

// ============================================================================
// 9. Connection Keep-Alive & Dead Connection Detection
// ============================================================================

// --- KeepAliveManager::Impl -------------------------------------------------

class KeepAliveManager::Impl {
public:
    explicit Impl(asio::io_context& io_ctx)
        : io_ctx_(io_ctx)
        , timer_(io_ctx)
        , last_activity_(std::chrono::steady_clock::now())
        , missed_keepalives_(0)
        , running_(false)
        , dead_(false) {}

    void start(KeepAliveConfig config) {
        std::lock_guard lock(mutex_);
        config_ = config;
        running_ = true;
        dead_ = false;
        missed_keepalives_ = 0;
        last_activity_ = std::chrono::steady_clock::now();
        schedule_next();
    }

    void stop() {
        std::lock_guard lock(mutex_);
        running_ = false;
        asio::error_code ec;
        timer_.cancel(ec);
    }

    // Called when any data is received — resets dead timer
    void on_activity() {
        std::lock_guard lock(mutex_);
        last_activity_ = std::chrono::steady_clock::now();
        missed_keepalives_ = 0;  // any data counts as keepalive
        dead_ = false;
    }

    // Called when a keepalive response is received
    void on_keepalive_ack() {
        std::lock_guard lock(mutex_);
        missed_keepalives_ = 0;
        dead_ = false;
        last_activity_ = std::chrono::steady_clock::now();
    }

    // Check if connection is considered dead
    bool is_dead() const {
        std::lock_guard lock(mutex_);
        return dead_;
    }

    // Get seconds since last activity
    float seconds_since_last_activity() const {
        std::lock_guard lock(mutex_);
        return std::chrono::duration<float>(
            std::chrono::steady_clock::now() - last_activity_).count();
    }

    // Register callback for when keepalive should be sent
    void set_on_keepalive(std::function<void()> callback) {
        std::lock_guard lock(mutex_);
        on_keepalive_ = std::move(callback);
    }

    // Register callback for when connection is detected as dead
    void set_on_dead(std::function<void()> callback) {
        std::lock_guard lock(mutex_);
        on_dead_ = std::move(callback);
    }

    int missed_count() const {
        std::lock_guard lock(mutex_);
        return missed_keepalives_;
    }

    void reset() {
        std::lock_guard lock(mutex_);
        running_ = false;
        dead_ = false;
        missed_keepalives_ = 0;
        last_activity_ = std::chrono::steady_clock::now();
        asio::error_code ec;
        timer_.cancel(ec);
    }

private:
    void schedule_next() {
        if (!running_) return;

        timer_.expires_after(config_.interval);
        timer_.async_wait([this](const asio::error_code& ec) {
            if (ec == asio::error::operation_aborted) return;
            if (!running_) return;

            std::lock_guard lock(mutex_);

            // Check for dead connection
            auto elapsed = std::chrono::steady_clock::now() - last_activity_;
            if (elapsed > config_.timeout + detail::kDeadDetectionGrace) {
                missed_keepalives_++;
                SPDLOG_WARN("[helper] keepalive: no activity for {}s, missed={}/{}",
                            std::chrono::duration<float>(elapsed).count(),
                            missed_keepalives_, config_.max_missed);

                if (missed_keepalives_ >= config_.max_missed) {
                    dead_ = true;
                    SPDLOG_ERROR("[helper] keepalive: connection declared DEAD");
                    if (on_dead_) {
                        on_dead_();
                    }
                    return;  // Don't reschedule
                }
            }

            // Send keepalive
            if (on_keepalive_) {
                on_keepalive_();
            }

            // Reschedule
            schedule_next();
        });
    }

    asio::io_context& io_ctx_;
    asio::steady_timer timer_;
    KeepAliveConfig config_{
        detail::kKeepAliveInterval,
        detail::kKeepAliveTimeout,
        detail::kMaxMissedKeepAlives
    };

    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point last_activity_;
    int missed_keepalives_;
    bool running_;
    bool dead_;

    std::function<void()> on_keepalive_;
    std::function<void()> on_dead_;
};

// --- KeepAliveManager public interface --------------------------------------

KeepAliveManager::KeepAliveManager(asio::io_context& io_ctx)
    : impl_(std::make_unique<Impl>(io_ctx)) {}

KeepAliveManager::~KeepAliveManager() {
    impl_->stop();
}

void KeepAliveManager::start(KeepAliveConfig config) {
    impl_->start(std::move(config));
}

void KeepAliveManager::stop() {
    impl_->stop();
}

void KeepAliveManager::on_activity() {
    impl_->on_activity();
}

void KeepAliveManager::on_keepalive_ack() {
    impl_->on_keepalive_ack();
}

bool KeepAliveManager::is_dead() const {
    return impl_->is_dead();
}

float KeepAliveManager::seconds_since_last_activity() const {
    return impl_->seconds_since_last_activity();
}

void KeepAliveManager::set_on_keepalive(std::function<void()> callback) {
    impl_->set_on_keepalive(std::move(callback));
}

void KeepAliveManager::set_on_dead(std::function<void()> callback) {
    impl_->set_on_dead(std::move(callback));
}

int KeepAliveManager::missed_count() const {
    return impl_->missed_count();
}

void KeepAliveManager::reset() {
    impl_->reset();
}

// ============================================================================
// 10. Session Recording Playback Helper
// ============================================================================

// --- SessionRecordingHeader -------------------------------------------------

std::string SessionRecordingHeader::to_string() const {
    std::ostringstream oss;
    oss << "Recording v" << version
        << " " << width << "x" << height
        << " " << frame_count << " frames"
        << " duration: " << duration_ms << "ms";
    return oss.str();
}

// --- SessionRecordingFrame --------------------------------------------------

// --- PlaybackHelper::Impl ---------------------------------------------------

class PlaybackHelper::Impl {
public:
    // Open a recording file
    bool open(const std::string& file_path) {
        std::lock_guard lock(mutex_);

        file_.open(file_path, std::ios::binary);
        if (!file_) {
            SPDLOG_ERROR("[helper] playback: cannot open {}", file_path);
            return false;
        }

        // Read header
        if (!read_header()) {
            file_.close();
            return false;
        }

        current_frame_ = 0;
        playback_position_ = std::chrono::milliseconds(0);
        ended_ = false;
        SPDLOG_INFO("[helper] playback: opened {}, {} frames",
                    file_path, header_.frame_count);
        return true;
    }

    // Close the recording
    void close() {
        std::lock_guard lock(mutex_);
        file_.close();
        current_frame_ = 0;
        ended_ = true;
    }

    // Read next frame
    std::optional<SessionRecordingFrame> read_next_frame() {
        std::lock_guard lock(mutex_);

        if (!file_ || ended_) return std::nullopt;

        if (current_frame_ >= header_.frame_count) {
            ended_ = true;
            return std::nullopt;
        }

        // Read frame header
        std::array<std::uint8_t, detail::kPlaybackFrameHeader> frame_hdr{};
        file_.read(reinterpret_cast<char*>(frame_hdr.data()), frame_hdr.size());
        if (!file_) {
            ended_ = true;
            return std::nullopt;
        }

        SessionRecordingFrame frame;
        frame.frame_index  = read_u32_le(frame_hdr.data() + 0);
        frame.timestamp_ms = read_u64_le(frame_hdr.data() + 4);
        frame.frame_type   = static_cast<FrameType>(frame_hdr[12]);
        frame.data_size    = read_u64_le(frame_hdr.data() + 16);
        frame.key_frame    = (frame_hdr[24] != 0);

        if (frame.data_size > detail::kPlaybackMaxFrameSz) {
            SPDLOG_WARN("[helper] playback: frame {} too large ({})",
                        frame.frame_index, frame.data_size);
            ended_ = true;
            return std::nullopt;
        }

        // Read frame data
        frame.data.resize(frame.data_size);
        file_.read(reinterpret_cast<char*>(frame.data.data()),
                   static_cast<std::streamsize>(frame.data_size));
        if (!file_) {
            ended_ = true;
            return std::nullopt;
        }

        current_frame_++;
        playback_position_ = std::chrono::milliseconds(frame.timestamp_ms);

        return frame;
    }

    // Seek to a specific frame
    bool seek_to_frame(std::uint32_t frame_index) {
        std::lock_guard lock(mutex_);

        if (!file_) return false;
        if (frame_index >= header_.frame_count) return false;

        // Calculate position: header + frame_index * (frame_header + avg_frame_size)
        // We need to scan linearly since frames can be variable size

        // Reset to beginning of data
        file_.seekg(data_offset_, std::ios::beg);

        current_frame_ = 0;
        ended_ = false;

        while (current_frame_ < frame_index) {
            std::array<std::uint8_t, detail::kPlaybackFrameHeader> fhdr{};
            file_.read(reinterpret_cast<char*>(fhdr.data()), fhdr.size());
            if (!file_) return false;

            std::uint64_t fsize = read_u64_le(fhdr.data() + 16);
            file_.seekg(static_cast<std::streamoff>(fsize), std::ios::cur);
            if (!file_) return false;

            current_frame_++;
        }

        return true;
    }

    // Seek to a timestamp
    bool seek_to_time(std::chrono::milliseconds ms) {
        std::lock_guard lock(mutex_);

        if (!file_) return false;

        // Reset and scan forward
        file_.seekg(data_offset_, std::ios::beg);
        current_frame_ = 0;
        ended_ = false;

        while (current_frame_ < header_.frame_count) {
            auto pos_before = file_.tellg();

            std::array<std::uint8_t, detail::kPlaybackFrameHeader> fhdr{};
            file_.read(reinterpret_cast<char*>(fhdr.data()), fhdr.size());
            if (!file_) break;

            std::uint64_t timestamp = read_u64_le(fhdr.data() + 4);
            std::uint64_t fsize     = read_u64_le(fhdr.data() + 16);

            if (std::chrono::milliseconds(timestamp) >= ms) {
                // Go back to this frame
                file_.seekg(pos_before, std::ios::beg);
                return true;
            }

            file_.seekg(static_cast<std::streamoff>(fsize), std::ios::cur);
            if (!file_) break;

            current_frame_++;
        }

        ended_ = true;
        return false;
    }

    // Get current position info
    std::uint32_t current_frame_index() const {
        std::lock_guard lock(mutex_);
        return current_frame_;
    }

    std::chrono::milliseconds current_time() const {
        std::lock_guard lock(mutex_);
        return playback_position_;
    }

    std::chrono::milliseconds total_duration() const {
        std::lock_guard lock(mutex_);
        return std::chrono::milliseconds(header_.duration_ms);
    }

    const SessionRecordingHeader& header() const {
        std::lock_guard lock(mutex_);
        return header_;
    }

    bool is_open() const {
        std::lock_guard lock(mutex_);
        return file_.is_open();
    }

    bool is_ended() const {
        std::lock_guard lock(mutex_);
        return ended_;
    }

    // Get progress as 0.0 .. 1.0
    float progress() const {
        std::lock_guard lock(mutex_);
        if (header_.frame_count == 0) return 0.0f;
        return static_cast<float>(current_frame_) /
               static_cast<float>(header_.frame_count);
    }

private:
    bool read_header() {
        // Magic: 4 bytes
        std::array<char, 4> magic{};
        file_.read(magic.data(), 4);
        if (!file_ || std::memcmp(magic.data(), "CPDK", 4) != 0) {
            SPDLOG_ERROR("[helper] playback: invalid magic");
            return false;
        }

        // Version: 4 bytes
        std::array<std::uint8_t, 4> ver{};
        file_.read(reinterpret_cast<char*>(ver.data()), 4);
        header_.version = read_u32_le(ver.data());

        // Width, Height: 4 bytes each
        std::array<std::uint8_t, 4> w{}, h{};
        file_.read(reinterpret_cast<char*>(w.data()), 4);
        file_.read(reinterpret_cast<char*>(h.data()), 4);
        header_.width  = read_u32_le(w.data());
        header_.height = read_u32_le(h.data());

        // Frame count: 8 bytes
        std::array<std::uint8_t, 8> fc{};
        file_.read(reinterpret_cast<char*>(fc.data()), 8);
        header_.frame_count = read_u64_le(fc.data());

        // Duration: 8 bytes
        std::array<std::uint8_t, 8> dur{};
        file_.read(reinterpret_cast<char*>(dur.data()), 8);
        header_.duration_ms = read_u64_le(dur.data());

        // Codec: 32 bytes
        file_.read(reinterpret_cast<char*>(header_.codec.data()),
                   header_.codec.size());

        // Metadata size: 4 bytes
        std::array<std::uint8_t, 4> meta_sz{};
        file_.read(reinterpret_cast<char*>(meta_sz.data()), 4);
        std::uint32_t meta_size = read_u32_le(meta_sz.data());

        // Metadata
        if (meta_size > 0) {
            header_.metadata.resize(meta_size);
            file_.read(reinterpret_cast<char*>(header_.metadata.data()),
                       static_cast<std::streamsize>(meta_size));
        }

        // Record data offset
        data_offset_ = static_cast<std::uint64_t>(file_.tellg());

        return true;
    }

    static std::uint32_t read_u32_le(const std::uint8_t* data) {
        return static_cast<std::uint32_t>(data[0]) |
               (static_cast<std::uint32_t>(data[1]) << 8) |
               (static_cast<std::uint32_t>(data[2]) << 16) |
               (static_cast<std::uint32_t>(data[3]) << 24);
    }

    static std::uint64_t read_u64_le(const std::uint8_t* data) {
        return static_cast<std::uint64_t>(data[0]) |
               (static_cast<std::uint64_t>(data[1]) << 8) |
               (static_cast<std::uint64_t>(data[2]) << 16) |
               (static_cast<std::uint64_t>(data[3]) << 24) |
               (static_cast<std::uint64_t>(data[4]) << 32) |
               (static_cast<std::uint64_t>(data[5]) << 40) |
               (static_cast<std::uint64_t>(data[6]) << 48) |
               (static_cast<std::uint64_t>(data[7]) << 56);
    }

    mutable std::mutex mutex_;
    std::ifstream file_;
    SessionRecordingHeader header_;
    std::uint64_t data_offset_ = 0;
    std::uint32_t current_frame_ = 0;
    std::chrono::milliseconds playback_position_{0};
    bool ended_ = false;
};

// --- PlaybackHelper public interface ----------------------------------------

PlaybackHelper::PlaybackHelper()
    : impl_(std::make_unique<Impl>()) {}

PlaybackHelper::~PlaybackHelper() = default;

bool PlaybackHelper::open(const std::string& file_path) {
    return impl_->open(file_path);
}

void PlaybackHelper::close() {
    impl_->close();
}

std::optional<SessionRecordingFrame> PlaybackHelper::read_next_frame() {
    return impl_->read_next_frame();
}

bool PlaybackHelper::seek_to_frame(std::uint32_t frame_index) {
    return impl_->seek_to_frame(frame_index);
}

bool PlaybackHelper::seek_to_time(std::chrono::milliseconds ms) {
    return impl_->seek_to_time(ms);
}

std::uint32_t PlaybackHelper::current_frame_index() const {
    return impl_->current_frame_index();
}

std::chrono::milliseconds PlaybackHelper::current_time() const {
    return impl_->current_time();
}

std::chrono::milliseconds PlaybackHelper::total_duration() const {
    return impl_->total_duration();
}

const SessionRecordingHeader& PlaybackHelper::header() const {
    return impl_->header();
}

bool PlaybackHelper::is_open() const {
    return impl_->is_open();
}

bool PlaybackHelper::is_ended() const {
    return impl_->is_ended();
}

float PlaybackHelper::progress() const {
    return impl_->progress();
}

// ============================================================================
// 11. Multi-Monitor Coordinate Translation Helpers
// ============================================================================

// --- MonitorGeometry --------------------------------------------------------

bool MonitorGeometry::contains(std::int32_t x, std::int32_t y) const {
    return x >= origin_x && x < static_cast<std::int32_t>(origin_x + width) &&
           y >= origin_y && y < static_cast<std::int32_t>(origin_y + height);
}

bool MonitorGeometry::intersects(const MonitorGeometry& other) const {
    return !(origin_x + static_cast<std::int32_t>(width)  <= other.origin_x ||
             other.origin_x + static_cast<std::int32_t>(other.width) <= origin_x ||
             origin_y + static_cast<std::int32_t>(height) <= other.origin_y ||
             other.origin_y + static_cast<std::int32_t>(other.height) <= origin_y);
}

// --- MultiMonitorMapper -----------------------------------------------------

MultiMonitorMapper::MultiMonitorMapper() = default;

void MultiMonitorMapper::add_monitor(MonitorGeometry monitor) {
    monitors_.push_back(std::move(monitor));
}

void MultiMonitorMapper::clear_monitors() {
    monitors_.clear();
}

// Translate a point from virtual desktop coordinates to a specific monitor's local coords
std::optional<MonitorPoint> MultiMonitorMapper::virtual_to_monitor(
    std::int32_t virtual_x, std::int32_t virtual_y) const {

    for (std::size_t i = 0; i < monitors_.size(); ++i) {
        const auto& m = monitors_[i];
        if (m.contains(virtual_x, virtual_y)) {
            MonitorPoint mp;
            mp.monitor_index = static_cast<std::uint32_t>(i);
            mp.monitor_id    = m.id;
            mp.local_x       = virtual_x - m.origin_x;
            mp.local_y       = virtual_y - m.origin_y;
            mp.is_primary    = m.is_primary;
            return mp;
        }
    }

    // Point is not on any monitor — clamp to nearest
    if (monitors_.empty()) return std::nullopt;

    std::int32_t best_dist = std::numeric_limits<std::int32_t>::max();
    std::size_t best_idx = 0;

    for (std::size_t i = 0; i < monitors_.size(); ++i) {
        const auto& m = monitors_[i];
        std::int32_t cx = std::max(m.origin_x,
            std::min(virtual_x, static_cast<std::int32_t>(m.origin_x + m.width - 1)));
        std::int32_t cy = std::max(m.origin_y,
            std::min(virtual_y, static_cast<std::int32_t>(m.origin_y + m.height - 1)));
        std::int32_t dx = virtual_x - cx;
        std::int32_t dy = virtual_y - cy;
        std::int32_t dist = dx * dx + dy * dy;
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = i;
        }
    }

    const auto& m = monitors_[best_idx];
    MonitorPoint mp;
    mp.monitor_index = static_cast<std::uint32_t>(best_idx);
    mp.monitor_id    = m.id;
    mp.local_x       = std::max(0, std::min(static_cast<std::int32_t>(m.width - 1),
                                             virtual_x - m.origin_x));
    mp.local_y       = std::max(0, std::min(static_cast<std::int32_t>(m.height - 1),
                                             virtual_y - m.origin_y));
    mp.is_primary    = m.is_primary;
    return mp;
}

// Translate from monitor-local coordinates to virtual desktop coordinates
MonitorPoint MultiMonitorMapper::monitor_to_virtual(
    std::uint32_t monitor_index, std::int32_t local_x, std::int32_t local_y) const {

    if (monitor_index >= monitors_.size()) {
        MonitorPoint mp;
        mp.local_x = local_x;
        mp.local_y = local_y;
        return mp;
    }

    const auto& m = monitors_[monitor_index];
    MonitorPoint mp;
    mp.monitor_index = monitor_index;
    mp.monitor_id    = m.id;
    mp.local_x       = m.origin_x + local_x;
    mp.local_y       = m.origin_y + local_y;
    mp.is_primary    = m.is_primary;
    return mp;
}

// Get the virtual desktop bounding box
void MultiMonitorMapper::virtual_bounds(std::int32_t& out_x, std::int32_t& out_y,
                                         std::int32_t& out_w, std::int32_t& out_h) const {
    if (monitors_.empty()) {
        out_x = out_y = out_w = out_h = 0;
        return;
    }

    std::int32_t min_x = monitors_[0].origin_x;
    std::int32_t min_y = monitors_[0].origin_y;
    std::int32_t max_x = monitors_[0].origin_x + static_cast<std::int32_t>(monitors_[0].width);
    std::int32_t max_y = monitors_[0].origin_y + static_cast<std::int32_t>(monitors_[0].height);

    for (const auto& m : monitors_) {
        min_x = std::min(min_x, m.origin_x);
        min_y = std::min(min_y, m.origin_y);
        max_x = std::max(max_x, m.origin_x + static_cast<std::int32_t>(m.width));
        max_y = std::max(max_y, m.origin_y + static_cast<std::int32_t>(m.height));
    }

    out_x = min_x;
    out_y = min_y;
    out_w = max_x - min_x;
    out_h = max_y - min_y;
}

// Find which monitor a point is on (index only)
std::optional<std::uint32_t> MultiMonitorMapper::find_monitor(
    std::int32_t virtual_x, std::int32_t virtual_y) const {
    for (std::size_t i = 0; i < monitors_.size(); ++i) {
        if (monitors_[i].contains(virtual_x, virtual_y)) {
            return static_cast<std::uint32_t>(i);
        }
    }
    return std::nullopt;
}

// Scale a point from one monitor's coordinate space to another
MonitorPoint MultiMonitorMapper::scale_between_monitors(
    const MonitorPoint& source,
    std::uint32_t target_monitor_index) const {

    MonitorPoint result;
    result.monitor_index = target_monitor_index;

    if (source.monitor_index >= monitors_.size() ||
        target_monitor_index >= monitors_.size()) {
        return source;
    }

    const auto& src = monitors_[source.monitor_index];
    const auto& dst = monitors_[target_monitor_index];

    result.monitor_id = dst.id;
    result.is_primary = dst.is_primary;

    // Scale preserving aspect ratio
    float scale_x = static_cast<float>(dst.width)  / static_cast<float>(src.width);
    float scale_y = static_cast<float>(dst.height) / static_cast<float>(src.height);
    float scale   = std::min(scale_x, scale_y);

    result.local_x = static_cast<std::int32_t>(static_cast<float>(source.local_x) * scale);
    result.local_y = static_cast<std::int32_t>(static_cast<float>(source.local_y) * scale);

    // Center if the scaled dimensions don't fill the target
    std::int32_t scaled_w = static_cast<std::int32_t>(static_cast<float>(src.width) * scale);
    std::int32_t scaled_h = static_cast<std::int32_t>(static_cast<float>(src.height) * scale);
    result.local_x += (static_cast<std::int32_t>(dst.width)  - scaled_w) / 2;
    result.local_y += (static_cast<std::int32_t>(dst.height) - scaled_h) / 2;

    return result;
}

const std::vector<MonitorGeometry>& MultiMonitorMapper::monitors() const {
    return monitors_;
}

// ============================================================================
// 12. Touch Input to Mouse Event Conversion
// ============================================================================

// --- TouchPoint -------------------------------------------------------------

// --- TouchToMouseConverter::Impl --------------------------------------------

class TouchToMouseConverter::Impl {
public:
    explicit Impl(TouchConversionConfig config)
        : config_(std::move(config))
        , touch_active_(false)
        , last_tap_time_(std::chrono::steady_clock::time_point::min())
        , tap_count_(0)
        , long_press_fired_(false)
        , is_dragging_(false) {}

    // Process a touch event, returns converted mouse events
    std::vector<ConvertedMouseEvent> process_touch(const TouchPoint& touch) {
        std::vector<ConvertedMouseEvent> events;

        auto now = std::chrono::steady_clock::now();

        switch (touch.phase) {
            case TouchPhase::kBegan:
                events = handle_touch_began(touch, now);
                break;
            case TouchPhase::kMoved:
                events = handle_touch_moved(touch, now);
                break;
            case TouchPhase::kEnded:
                events = handle_touch_ended(touch, now);
                break;
            case TouchPhase::kCancelled:
                events = handle_touch_cancelled(touch);
                break;
            case TouchPhase::kStationary:
                // No conversion for stationary touches
                break;
        }

        return events;
    }

    // Process multi-touch (for gestures)
    std::vector<ConvertedMouseEvent> process_multi_touch(
        const std::vector<TouchPoint>& touches) {

        std::vector<ConvertedMouseEvent> events;

        if (touches.size() == 2 && config_.enable_gestures) {
            events = handle_two_finger_gesture(touches);
        }

        return events;
    }

    void set_config(const TouchConversionConfig& config) {
        config_ = config;
    }

    const TouchConversionConfig& config() const { return config_; }

    void reset() {
        touch_active_ = false;
        tap_count_ = 0;
        long_press_fired_ = false;
        is_dragging_ = false;
        start_touch_ = TouchPoint{};
        last_touch_ = TouchPoint{};
    }

private:
    std::vector<ConvertedMouseEvent> handle_touch_began(
        const TouchPoint& touch,
        std::chrono::steady_clock::time_point now) {

        std::vector<ConvertedMouseEvent> events;

        touch_active_      = true;
        start_touch_       = touch;
        last_touch_        = touch;
        long_press_fired_  = false;
        is_dragging_       = false;

        // Move mouse to touch position
        ConvertedMouseEvent move_evt;
        move_evt.type       = MouseEventType::kMove;
        move_evt.x          = touch.x;
        move_evt.y          = touch.y;
        move_evt.timestamp  = now;
        move_evt.source     = InputSource::kTouch;
        events.push_back(move_evt);

        // Schedule long-press detection
        long_press_start_ = now;

        return events;
    }

    std::vector<ConvertedMouseEvent> handle_touch_moved(
        const TouchPoint& touch,
        std::chrono::steady_clock::time_point now) {

        std::vector<ConvertedMouseEvent> events;

        if (!touch_active_) return events;

        // Check if we should start dragging
        if (!is_dragging_) {
            float dx = touch.x - start_touch_.x;
            float dy = touch.y - start_touch_.y;
            float dist = std::sqrt(dx * dx + dy * dy);

            if (dist > detail::kTapDistanceThreshold) {
                // Start drag — left button down
                is_dragging_ = true;
                ConvertedMouseEvent down_evt;
                down_evt.type      = MouseEventType::kButtonDown;
                down_evt.button    = MouseButton::kLeft;
                down_evt.x         = start_touch_.x;
                down_evt.y         = start_touch_.y;
                down_evt.timestamp = now;
                down_evt.source    = InputSource::kTouch;
                events.push_back(down_evt);
            }
        }

        // Send move event
        ConvertedMouseEvent move_evt;
        move_evt.type      = MouseEventType::kMove;
        move_evt.x         = touch.x;
        move_evt.y         = touch.y;
        move_evt.timestamp = now;
        move_evt.source    = InputSource::kTouch;

        if (is_dragging_) {
            move_evt.type = MouseEventType::kDrag;
        }

        events.push_back(move_evt);

        // Check for long press
        if (!long_press_fired_ && config_.enable_long_press) {
            auto elapsed = std::chrono::duration<float>(now - long_press_start_).count();
            if (elapsed >= detail::kLongPressThreshold) {
                long_press_fired_ = true;

                ConvertedMouseEvent right_evt;
                right_evt.type      = MouseEventType::kButtonDown;
                right_evt.button    = MouseButton::kRight;
                right_evt.x         = touch.x;
                right_evt.y         = touch.y;
                right_evt.timestamp = now;
                right_evt.source    = InputSource::kTouch;
                events.push_back(right_evt);

                ConvertedMouseEvent up_evt = right_evt;
                up_evt.type = MouseEventType::kButtonUp;
                events.push_back(up_evt);
            }
        }

        last_touch_ = touch;
        return events;
    }

    std::vector<ConvertedMouseEvent> handle_touch_ended(
        const TouchPoint& touch,
        std::chrono::steady_clock::time_point now) {

        std::vector<ConvertedMouseEvent> events;

        if (!touch_active_) return events;

        if (is_dragging_) {
            // End drag — release button
            ConvertedMouseEvent up_evt;
            up_evt.type      = MouseEventType::kButtonUp;
            up_evt.button    = MouseButton::kLeft;
            up_evt.x         = touch.x;
            up_evt.y         = touch.y;
            up_evt.timestamp = now;
            up_evt.source    = InputSource::kTouch;
            events.push_back(up_evt);
        } else {
            // This was a tap — handle click / double-click
            auto elapsed_since_last = std::chrono::duration<float>(
                now - last_tap_time_).count();

            if (elapsed_since_last < detail::kDoubleTapInterval && tap_count_ > 0) {
                tap_count_++;
                // Double-click
                ConvertedMouseEvent dbl_evt;
                dbl_evt.type      = MouseEventType::kDoubleClick;
                dbl_evt.button    = MouseButton::kLeft;
                dbl_evt.x         = touch.x;
                dbl_evt.y         = touch.y;
                dbl_evt.timestamp = now;
                dbl_evt.source    = InputSource::kTouch;
                dbl_evt.click_count = tap_count_;
                events.push_back(dbl_evt);
            } else {
                tap_count_ = 1;
                // Single click
                ConvertedMouseEvent down_evt;
                down_evt.type      = MouseEventType::kButtonDown;
                down_evt.button    = MouseButton::kLeft;
                down_evt.x         = touch.x;
                down_evt.y         = touch.y;
                down_evt.timestamp = now;
                down_evt.source    = InputSource::kTouch;
                events.push_back(down_evt);

                ConvertedMouseEvent up_evt = down_evt;
                up_evt.type = MouseEventType::kButtonUp;
                events.push_back(up_evt);
            }

            last_tap_time_ = now;
        }

        touch_active_ = false;
        return events;
    }

    std::vector<ConvertedMouseEvent> handle_touch_cancelled(
        const TouchPoint& /*touch*/) {

        std::vector<ConvertedMouseEvent> events;

        if (is_dragging_) {
            ConvertedMouseEvent up_evt;
            up_evt.type   = MouseEventType::kButtonUp;
            up_evt.button = MouseButton::kLeft;
            up_evt.x      = last_touch_.x;
            up_evt.y      = last_touch_.y;
            up_evt.source = InputSource::kTouch;
            events.push_back(up_evt);
        }

        touch_active_ = false;
        is_dragging_  = false;
        return events;
    }

    std::vector<ConvertedMouseEvent> handle_two_finger_gesture(
        const std::vector<TouchPoint>& touches) {

        std::vector<ConvertedMouseEvent> events;

        if (touches.size() != 2) return events;

        const auto& t0 = touches[0];
        const auto& t1 = touches[1];

        // Compute distance between fingers
        float dx = t1.x - t0.x;
        float dy = t1.y - t0.y;
        float dist = std::sqrt(dx * dx + dy * dy);

        // Center point
        float cx = (t0.x + t1.x) * 0.5f;
        float cy = (t0.y + t1.y) * 0.5f;

        auto now = std::chrono::steady_clock::now();

        if (pinch_active_) {
            float scale = dist / pinch_start_distance_;
            if (std::abs(scale - 1.0f) > detail::kPinchScaleThreshold) {
                // Scroll event (pinch = ctrl+scroll)
                ConvertedMouseEvent scroll_evt;
                scroll_evt.type      = MouseEventType::kScroll;
                scroll_evt.x         = static_cast<std::int32_t>(cx);
                scroll_evt.y         = static_cast<std::int32_t>(cy);
                scroll_evt.timestamp = now;
                scroll_evt.source    = InputSource::kTouch;
                scroll_evt.scroll_delta = (scale > 1.0f) ? -1.0f : 1.0f;
                events.push_back(scroll_evt);
            }
        } else {
            // Start pinch
            pinch_active_ = true;
            pinch_start_distance_ = dist;
        }

        return events;
    }

    TouchConversionConfig config_;
    bool touch_active_;
    bool is_dragging_;
    bool long_press_fired_;
    bool pinch_active_ = false;
    float pinch_start_distance_ = 0.0f;

    TouchPoint start_touch_;
    TouchPoint last_touch_;
    std::chrono::steady_clock::time_point last_tap_time_;
    std::chrono::steady_clock::time_point long_press_start_;
    int tap_count_;
};

// --- TouchToMouseConverter public interface ---------------------------------

TouchToMouseConverter::TouchToMouseConverter(TouchConversionConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

TouchToMouseConverter::~TouchToMouseConverter() = default;

std::vector<ConvertedMouseEvent> TouchToMouseConverter::process_touch(
    const TouchPoint& touch) {
    return impl_->process_touch(touch);
}

std::vector<ConvertedMouseEvent> TouchToMouseConverter::process_multi_touch(
    const std::vector<TouchPoint>& touches) {
    return impl_->process_multi_touch(touches);
}

void TouchToMouseConverter::set_config(const TouchConversionConfig& config) {
    impl_->set_config(config);
}

const TouchConversionConfig& TouchToMouseConverter::config() const {
    return impl_->config();
}

void TouchToMouseConverter::reset() {
    impl_->reset();
}

// ============================================================================
// 13. Game Controller Input Mapping
// ============================================================================

// --- GameControllerState ----------------------------------------------------

// --- ControllerMapping ------------------------------------------------------

// --- ControllerMapper::Impl -------------------------------------------------

class ControllerMapper::Impl {
public:
    // Map a raw controller state to standardized output
    ControllerMappingResult map_state(const GameControllerState& state) {
        ControllerMappingResult result;
        result.timestamp = state.timestamp;
        result.controller_index = state.controller_index;

        // Apply dead zone to axes
        std::array<float, detail::kMaxControllerAxes> processed_axes{};
        for (int i = 0; i < detail::kMaxControllerAxes && i < state.num_axes; ++i) {
            float val = state.axes[i];
            if (std::abs(val) < detail::kControllerDeadZone) {
                val = 0.0f;
            } else {
                // Rescale: map [deadzone..1.0] -> [0.0..1.0]
                float sign = (val > 0.0f) ? 1.0f : -1.0f;
                val = sign * (std::abs(val) - detail::kControllerDeadZone) /
                      (1.0f - detail::kControllerDeadZone);
            }
            processed_axes[i] = val;
        }

        // Map to mouse/keyboard events based on configuration
        if (config_.left_stick_mouse) {
            result.mouse_delta_x = processed_axes[static_cast<int>(ControllerAxis::kLeftX)];
            result.mouse_delta_y = processed_axes[static_cast<int>(ControllerAxis::kLeftY)];
        } else if (config_.right_stick_mouse) {
            result.mouse_delta_x = processed_axes[static_cast<int>(ControllerAxis::kRightX)];
            result.mouse_delta_y = processed_axes[static_cast<int>(ControllerAxis::kRightY)];
        }

        // D-pad to arrow keys
        result.dpad_up    = (state.buttons & (1 << static_cast<int>(ControllerButton::kDPadUp))) != 0;
        result.dpad_down  = (state.buttons & (1 << static_cast<int>(ControllerButton::kDPadDown))) != 0;
        result.dpad_left  = (state.buttons & (1 << static_cast<int>(ControllerButton::kDPadLeft))) != 0;
        result.dpad_right = (state.buttons & (1 << static_cast<int>(ControllerButton::kDPadRight))) != 0;

        // Face buttons to keyboard
        result.button_a = (state.buttons & (1 << static_cast<int>(ControllerButton::kA))) != 0;
        result.button_b = (state.buttons & (1 << static_cast<int>(ControllerButton::kB))) != 0;
        result.button_x = (state.buttons & (1 << static_cast<int>(ControllerButton::kX))) != 0;
        result.button_y = (state.buttons & (1 << static_cast<int>(ControllerButton::kY))) != 0;

        // Triggers
        result.left_trigger  = processed_axes[static_cast<int>(ControllerAxis::kLeftTrigger)];
        result.right_trigger = processed_axes[static_cast<int>(ControllerAxis::kRightTrigger)];

        // Shoulder buttons
        result.left_bumper  = (state.buttons & (1 << static_cast<int>(ControllerButton::kLeftBumper))) != 0;
        result.right_bumper = (state.buttons & (1 << static_cast<int>(ControllerButton::kRightBumper))) != 0;

        result.processed_axes = state.axes;
        result.raw_buttons    = state.buttons;

        return result;
    }

    // Set mapping configuration
    void set_config(ControllerConfig config) {
        std::lock_guard lock(mutex_);
        config_ = std::move(config);
    }

    const ControllerConfig& config() const {
        std::lock_guard lock(mutex_);
        return config_;
    }

    // Map a button to a keyboard scancode
    std::optional<std::uint32_t> button_to_key(ControllerButton button) const {
        std::lock_guard lock(mutex_);

        auto it = config_.button_to_key.find(button);
        if (it != config_.button_to_key.end()) {
            return it->second;
        }

        // Default mappings
        switch (button) {
            case ControllerButton::kA:           return 0x1E; // A key
            case ControllerButton::kB:           return 0x30; // B key
            case ControllerButton::kX:           return 0x2D; // X key
            case ControllerButton::kY:           return 0x15; // Y key
            case ControllerButton::kStart:       return 0x1C; // Enter
            case ControllerButton::kBack:        return 0x01; // Escape
            case ControllerButton::kDPadUp:      return 0x48; // Up arrow
            case ControllerButton::kDPadDown:    return 0x50; // Down arrow
            case ControllerButton::kDPadLeft:    return 0x4B; // Left arrow
            case ControllerButton::kDPadRight:   return 0x4D; // Right arrow
            default: return std::nullopt;
        }
    }

    // Get a human-readable name for a controller button
    static std::string_view button_name(ControllerButton button) {
        switch (button) {
            case ControllerButton::kA:           return "A";
            case ControllerButton::kB:           return "B";
            case ControllerButton::kX:           return "X";
            case ControllerButton::kY:           return "Y";
            case ControllerButton::kStart:       return "Start";
            case ControllerButton::kBack:        return "Back";
            case ControllerButton::kGuide:       return "Guide";
            case ControllerButton::kDPadUp:      return "DPad Up";
            case ControllerButton::kDPadDown:    return "DPad Down";
            case ControllerButton::kDPadLeft:    return "DPad Left";
            case ControllerButton::kDPadRight:   return "DPad Right";
            case ControllerButton::kLeftBumper:  return "LB";
            case ControllerButton::kRightBumper: return "RB";
            case ControllerButton::kLeftStick:   return "L3";
            case ControllerButton::kRightStick:  return "R3";
            default: return "Unknown";
        }
    }

    // Get a human-readable name for a controller axis
    static std::string_view axis_name(ControllerAxis axis) {
        switch (axis) {
            case ControllerAxis::kLeftX:        return "Left Stick X";
            case ControllerAxis::kLeftY:        return "Left Stick Y";
            case ControllerAxis::kRightX:       return "Right Stick X";
            case ControllerAxis::kRightY:       return "Right Stick Y";
            case ControllerAxis::kLeftTrigger:  return "Left Trigger";
            case ControllerAxis::kRightTrigger: return "Right Trigger";
            default: return "Unknown";
        }
    }

    void reset() {
        std::lock_guard lock(mutex_);
        config_ = ControllerConfig{};
    }

private:
    mutable std::mutex mutex_;
    ControllerConfig config_;
};

// --- ControllerMapper public interface --------------------------------------

ControllerMapper::ControllerMapper()
    : impl_(std::make_unique<Impl>()) {}

ControllerMapper::~ControllerMapper() = default;

ControllerMappingResult ControllerMapper::map_state(
    const GameControllerState& state) {
    return impl_->map_state(state);
}

void ControllerMapper::set_config(ControllerConfig config) {
    impl_->set_config(std::move(config));
}

const ControllerConfig& ControllerMapper::config() const {
    return impl_->config();
}

std::optional<std::uint32_t> ControllerMapper::button_to_key(
    ControllerButton button) const {
    return impl_->button_to_key(button);
}

std::string_view ControllerMapper::button_name(ControllerButton button) {
    return Impl::button_name(button);
}

std::string_view ControllerMapper::axis_name(ControllerAxis axis) {
    return Impl::axis_name(axis);
}

void ControllerMapper::reset() {
    impl_->reset();
}

// ============================================================================
// 14. Stylus / Pen Pressure Support
// ============================================================================

// --- StylusState ------------------------------------------------------------

std::string StylusState::to_string() const {
    std::ostringstream oss;
    oss << "Stylus: x=" << x << " y=" << y
        << " pressure=" << pressure
        << " tilt=(" << tilt_x << "," << tilt_y << ")"
        << " buttons=0x" << std::hex << buttons
        << " " << (is_eraser ? "[eraser]" : "[pen]")
        << " " << (in_proximity ? "(proximity)" : "(contact)");
    return oss.str();
}

// --- StylusPressureMapper ---------------------------------------------------

StylusPressureMapper::StylusPressureMapper() = default;

// Map pressure value to a tool parameter (e.g., brush size, opacity)
float StylusPressureMapper::map_pressure_to_parameter(
    float pressure, PressureTarget target) const {

    // Apply pressure curve
    float curved = apply_pressure_curve(pressure);

    switch (target) {
        case PressureTarget::kOpacity:
            // Full opacity range
            return curved;

        case PressureTarget::kBrushSize:
            // Map to 0.5x to 3.0x brush size
            return 0.5f + curved * 2.5f;

        case PressureTarget::kFlow:
            // Flow rate: 5% to 100%
            return 0.05f + curved * 0.95f;

        case PressureTarget::kScatter:
            // More scatter at low pressure
            return 1.0f - curved * 0.9f;

        case PressureTarget::kHardness:
            return 0.1f + curved * 0.9f;

        default:
            return curved;
    }
}

// Apply a pressure curve transformation
float StylusPressureMapper::apply_pressure_curve(float pressure) const {
    // Clamp
    pressure = std::max(detail::kPressureMin, std::min(detail::kPressureMax, pressure));

    switch (curve_type_) {
        case PressureCurve::kLinear:
            return pressure;

        case PressureCurve::kSoft:
            // Soft curve: sqrt-like response (easier to get mid pressures)
            return std::sqrt(pressure);

        case PressureCurve::kFirm:
            // Firm curve: square-like response (harder to get low pressures)
            return pressure * pressure;

        case PressureCurve::kCustom:
            // Use provided control points for a cubic bezier
            return evaluate_custom_curve(pressure);

        default:
            return pressure;
    }
}

// Set the pressure curve
void StylusPressureMapper::set_curve(PressureCurve curve) {
    curve_type_ = curve;
}

void StylusPressureMapper::set_custom_curve_points(
    float cp1x, float cp1y, float cp2x, float cp2y) {
    cp1x_ = cp1x; cp1y_ = cp1y;
    cp2x_ = cp2x; cp2y_ = cp2y;
    curve_type_ = PressureCurve::kCustom;
}

// Normalize tilt angles to -1..1 range
float StylusPressureMapper::normalize_tilt(float tilt_degrees) const {
    return std::max(-1.0f, std::min(1.0f,
        tilt_degrees / detail::kTiltMax));
}

// Compute cursor offset from tilt (for brush preview)
std::pair<float, float> StylusPressureMapper::tilt_to_offset(
    float tilt_x, float tilt_y, float max_offset_pixels) const {
    float nx = normalize_tilt(tilt_x);
    float ny = normalize_tilt(tilt_y);
    return {nx * max_offset_pixels, ny * max_offset_pixels};
}

// Generate tilt-based rotation angle (for brush rotation)
float StylusPressureMapper::tilt_to_rotation(float tilt_x, float tilt_y) const {
    return std::atan2(tilt_y, tilt_x) * 180.0f / 3.14159265358979323846f;
}

// Map pressure to an 8-bit value (0-255)
std::uint8_t StylusPressureMapper::pressure_to_byte(float pressure) const {
    float curved = apply_pressure_curve(pressure);
    return static_cast<std::uint8_t>(curved * 255.0f);
}

// Map pressure to a 16-bit value (0-65535)
std::uint16_t StylusPressureMapper::pressure_to_short(float pressure) const {
    float curved = apply_pressure_curve(pressure);
    return static_cast<std::uint16_t>(curved * 65535.0f);
}

// Convert stylus state to a mouse event with pressure data
ConvertedMouseEvent StylusPressureMapper::stylus_to_mouse_event(
    const StylusState& stylus) const {

    ConvertedMouseEvent evt;
    evt.timestamp = std::chrono::steady_clock::now();
    evt.x         = static_cast<std::int32_t>(stylus.x);
    evt.y         = static_cast<std::int32_t>(stylus.y);
    evt.source    = InputSource::kStylus;
    evt.pressure  = stylus.pressure;

    // Check if eraser tip
    if (stylus.is_eraser) {
        evt.button = MouseButton::kRight;
    }

    // Determine event type
    if (stylus.in_proximity && !last_in_proximity_) {
        evt.type = MouseEventType::kMove; // cursor appears
    } else if (!stylus.in_proximity && last_in_proximity_) {
        evt.type = MouseEventType::kLeave;
    } else if (stylus.pressure > 0.0f && last_pressure_ == 0.0f) {
        evt.type   = MouseEventType::kButtonDown;
        evt.button = stylus.is_eraser ? MouseButton::kRight : MouseButton::kLeft;
    } else if (stylus.pressure == 0.0f && last_pressure_ > 0.0f) {
        evt.type   = MouseEventType::kButtonUp;
        evt.button = stylus.is_eraser ? MouseButton::kRight : MouseButton::kLeft;
    } else if (stylus.pressure > 0.0f) {
        evt.type = MouseEventType::kDrag;
    } else {
        evt.type = MouseEventType::kMove;
    }

    // Store tilt as extended data
    evt.tilt_x = stylus.tilt_x;
    evt.tilt_y = stylus.tilt_y;

    // Store for next call
    last_pressure_     = stylus.pressure;
    last_in_proximity_ = stylus.in_proximity;

    return evt;
}

PressureCurve StylusPressureMapper::curve() const {
    return curve_type_;
}

void StylusPressureMapper::reset() {
    curve_type_ = PressureCurve::kLinear;
    last_pressure_ = 0.0f;
    last_in_proximity_ = false;
}

// --- Private helpers --------------------------------------------------------

float StylusPressureMapper::evaluate_custom_curve(float t) const {
    // Cubic bezier: B(t) = (1-t)^3*P0 + 3(1-t)^2*t*P1 + 3(1-t)*t^2*P2 + t^3*P3
    // P0 = (0,0), P3 = (1,1). We only need the y-component.
    // P1 = (cp1x, cp1y), P2 = (cp2x, cp2y)
    // We need to solve for the y value at a given x (t is not linear in x).

    // Simplified: use t directly as x for now (works for monotonic curves)
    float u = 1.0f - t;
    float y = 3.0f * u * u * t * cp1y_ +
              3.0f * u * t * t * cp2y_ +
              t * t * t;
    return std::max(0.0f, std::min(1.0f, y));
}

// ============================================================================
// Dedicated Stylus/tablet support
// ============================================================================

// --- StylusTabletHelper -----------------------------------------------------

class StylusTabletHelper::Impl {
public:
    void set_state(const StylusState& state) {
        std::lock_guard lock(mutex_);
        current_state_ = state;
        if (!initialized_) {
            prev_state_ = state;
            initialized_ = true;
        }
    }

    StylusState current_state() const {
        std::lock_guard lock(mutex_);
        return current_state_;
    }

    // Detect if stylus just made contact
    bool just_made_contact() const {
        std::lock_guard lock(mutex_);
        return current_state_.pressure > 0.0f && prev_state_.pressure == 0.0f;
    }

    // Detect if stylus just lifted
    bool just_lifted() const {
        std::lock_guard lock(mutex_);
        return current_state_.pressure == 0.0f && prev_state_.pressure > 0.0f;
    }

    // Get velocity (pixels/second)
    float velocity() const {
        std::lock_guard lock(mutex_);
        auto dt = std::chrono::duration<float>(
            current_state_.timestamp - prev_state_.timestamp).count();
        if (dt <= 0.0f) return 0.0f;

        float dx = current_state_.x - prev_state_.x;
        float dy = current_state_.y - prev_state_.y;
        return std::sqrt(dx * dx + dy * dy) / dt;
    }

    // Commit current state as previous (call after processing)
    void commit() {
        std::lock_guard lock(mutex_);
        prev_state_ = current_state_;
    }

    // Get pressure change
    float pressure_delta() const {
        std::lock_guard lock(mutex_);
        return current_state_.pressure - prev_state_.pressure;
    }

    // Check if tilt changed significantly
    bool tilt_changed(float threshold_degrees) const {
        std::lock_guard lock(mutex_);
        float dtx = std::abs(current_state_.tilt_x - prev_state_.tilt_x);
        float dty = std::abs(current_state_.tilt_y - prev_state_.tilt_y);
        return dtx > threshold_degrees || dty > threshold_degrees;
    }

    void reset() {
        std::lock_guard lock(mutex_);
        initialized_ = false;
        current_state_ = StylusState{};
        prev_state_    = StylusState{};
    }

private:
    mutable std::mutex mutex_;
    StylusState current_state_;
    StylusState prev_state_;
    bool initialized_ = false;
};

// --- StylusTabletHelper public interface ------------------------------------

StylusTabletHelper::StylusTabletHelper()
    : impl_(std::make_unique<Impl>()) {}

StylusTabletHelper::~StylusTabletHelper() = default;

void StylusTabletHelper::set_state(const StylusState& state) {
    impl_->set_state(state);
}

StylusState StylusTabletHelper::current_state() const {
    return impl_->current_state();
}

bool StylusTabletHelper::just_made_contact() const {
    return impl_->just_made_contact();
}

bool StylusTabletHelper::just_lifted() const {
    return impl_->just_lifted();
}

float StylusTabletHelper::velocity() const {
    return impl_->velocity();
}

void StylusTabletHelper::commit() {
    impl_->commit();
}

float StylusTabletHelper::pressure_delta() const {
    return impl_->pressure_delta();
}

bool StylusTabletHelper::tilt_changed(float threshold_degrees) const {
    return impl_->tilt_changed(threshold_degrees);
}

void StylusTabletHelper::reset() {
    impl_->reset();
}

} // namespace cppdesk::client::helper

// ============================================================================
// End of helper.cpp
// ============================================================================
