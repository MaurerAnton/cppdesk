// ============================================================================
// cppdesk — Remote Desktop Client
// io_loop.cpp — Comprehensive Asynchronous IO Event Loop Implementation
// ============================================================================
// Implements the core client IO loop: TCP/SSL connectivity, message framing,
// parsing, dispatching, video decoding pipeline, audio playback/recording,
// clipboard sync, file transfer, keep-alive, timeout detection, exponential
// backoff reconnection, bandwidth monitoring with adaptive bitrate, and
// input-event coalescing.
// ============================================================================

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

// --- asio -------------------------------------------------------------------
#include <asio.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/ssl.hpp>

// --- spdlog -----------------------------------------------------------------
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

// ---------------------------------------------------------------------------
// Public client API header (declares classes whose methods we define here)
// ---------------------------------------------------------------------------
#include "client/io_loop.hpp"
#include "client/protocol.hpp"
#include "client/video_codec.hpp"
#include "client/audio_codec.hpp"
#include "client/input_event.hpp"
#include "client/clipboard.hpp"
#include "client/file_transfer.hpp"
#include "client/message_types.hpp"

// ============================================================================
// Internal Constants & Helpers
// ============================================================================

namespace cppdesk::client::detail {

// --- Protocol constants -----------------------------------------------------
static constexpr std::size_t   kMaxMessageSize       = 64 * 1024 * 1024;  // 64 MiB
static constexpr std::size_t   kHeaderSize           = 8;
static constexpr std::size_t   kReadBufferSize       = 256 * 1024;        // 256 KiB
static constexpr std::size_t   kMaxFrameQueueSize    = 16;
static constexpr std::size_t   kMaxAudioQueueSize    = 32;
static constexpr std::size_t   kFileChunkSize        = 64 * 1024;         // 64 KiB
static constexpr auto          kHeartbeatInterval    = std::chrono::seconds(10);
static constexpr auto          kHeartbeatTimeout     = std::chrono::seconds(30);
static constexpr auto          kConnectTimeout       = std::chrono::seconds(5);
static constexpr auto          kInitialReconnectDelay = std::chrono::milliseconds(500);
static constexpr auto          kMaxReconnectDelay    = std::chrono::seconds(30);
static constexpr auto          kReconnectDelayMultiplier = 2.0f;
static constexpr auto          kBandwidthSampleWindow = std::chrono::seconds(5);
static constexpr auto          kInputCoalesceWindow  = std::chrono::milliseconds(8);
static constexpr float         kMinQuality           = 0.10f;
static constexpr float         kMaxQuality           = 1.00f;
static constexpr float         kQualityStep          = 0.05f;
static constexpr std::uint32_t kProtocolVersion      = 3;

// --- Wire-level message framing ---------------------------------------------
// | 4 bytes: message length (big-endian, excludes header)
// | 2 bytes: message type  (big-endian)
// | 2 bytes: flags         (big-endian, reserved)
// | N bytes: payload
// ---------------------------------------------------------------------------

enum class MessageType : std::uint16_t {
    // --- Control ---
    kHello           = 0x0001,
    kHelloAck        = 0x0002,
    kGoodbye         = 0x0003,
    kHeartbeat       = 0x0004,
    kHeartbeatAck    = 0x0005,
    kError           = 0x0006,
    kCapabilitiesReq = 0x0010,
    kCapabilitiesRsp = 0x0011,
    kSessionConfig   = 0x0012,

    // --- Video ---
    kVideoFrame      = 0x0100,
    kVideoKeyFrame   = 0x0101,
    kVideoAck        = 0x0102,
    kVideoQualityReq = 0x0103,

    // --- Audio (playback) ---
    kAudioPlay       = 0x0200,
    kAudioPlayStop   = 0x0201,

    // --- Audio (recording / microphone) ---
    kAudioRecord     = 0x0208,
    kAudioRecordStop = 0x0209,

    // --- Input ---
    kInputMouseMove  = 0x0300,
    kInputMouseButton= 0x0301,
    kInputMouseWheel = 0x0302,
    kInputKeyPress   = 0x0303,
    kInputKeyRelease = 0x0304,
    kInputBatch      = 0x0305,

    // --- Clipboard ---
    kClipboardOffer  = 0x0400,
    kClipboardRequest= 0x0401,
    kClipboardData   = 0x0402,
    kClipboardAck    = 0x0403,

    // --- File Transfer ---
    kFileTransferInit   = 0x0500,
    kFileTransferAccept = 0x0501,
    kFileTransferChunk  = 0x0502,
    kFileTransferAck    = 0x0503,
    kFileTransferDone   = 0x0504,
    kFileTransferCancel = 0x0505,

    // --- Display ---
    kDisplayInfo     = 0x0600,
    kDisplayResize   = 0x0601,

    // --- Reserved / Internal ---
    kReservedMax     = 0xFFFF,
};

enum class MessageFlag : std::uint16_t {
    kNone          = 0x0000,
    kCompressed    = 0x0001,
    kFragmented    = 0x0002,
    kLastFragment  = 0x0004,
    kEncrypted     = 0x0008,
};

constexpr MessageFlag operator|(MessageFlag a, MessageFlag b) {
    return static_cast<MessageFlag>(
        static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
}
constexpr MessageFlag operator&(MessageFlag a, MessageFlag b) {
    return static_cast<MessageFlag>(
        static_cast<std::uint16_t>(a) & static_cast<std::uint16_t>(b));
}
constexpr bool has_flag(MessageFlag flags, MessageFlag test) {
    return (flags & test) == test;
}

// --- Connection state machine -----------------------------------------------
enum class ConnState {
    kDisconnected,
    kResolving,
    kConnecting,
    kTlsHandshaking,
    kHandshaking,
    kCapabilitiesExchanging,
    kSessionReady,
    kDisconnecting,
    kReconnecting,
};

const char* to_string(ConnState s) {
    switch (s) {
    case ConnState::kDisconnected:          return "disconnected";
    case ConnState::kResolving:             return "resolving";
    case ConnState::kConnecting:            return "connecting";
    case ConnState::kTlsHandshaking:        return "tls_handshaking";
    case ConnState::kHandshaking:           return "handshaking";
    case ConnState::kCapabilitiesExchanging:return "capabilities_exchanging";
    case ConnState::kSessionReady:          return "session_ready";
    case ConnState::kDisconnecting:         return "disconnecting";
    case ConnState::kReconnecting:          return "reconnecting";
    default:                                return "unknown";
    }
}

// --- Wire helpers -----------------------------------------------------------
inline std::uint32_t read_u32_be(const std::uint8_t* buf) {
    return (static_cast<std::uint32_t>(buf[0]) << 24) |
           (static_cast<std::uint32_t>(buf[1]) << 16) |
           (static_cast<std::uint32_t>(buf[2]) <<  8) |
           (static_cast<std::uint32_t>(buf[3])      );
}
inline std::uint16_t read_u16_be(const std::uint8_t* buf) {
    return (static_cast<std::uint16_t>(buf[0]) << 8) |
           (static_cast<std::uint16_t>(buf[1])     );
}
inline void write_u32_be(std::uint8_t* buf, std::uint32_t v) {
    buf[0] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    buf[1] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    buf[2] = static_cast<std::uint8_t>((v >>  8) & 0xFF);
    buf[3] = static_cast<std::uint8_t>( v        & 0xFF);
}
inline void write_u16_be(std::uint8_t* buf, std::uint16_t v) {
    buf[0] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    buf[1] = static_cast<std::uint8_t>( v       & 0xFF);
}

// ============================================================================
// Forward declarations for internal classes
// ============================================================================
class BandwidthMonitor;
class VideoFrameQueue;
class AudioPlaybackPipeline;
class AudioRecordPipeline;
class ClipboardManager;
class FileTransferManager;
class InputEventCoalescer;
class MessageFramer;
class MessageDispatcher;

// ============================================================================
// ConnectionConfig — per-session connection parameters
// ============================================================================
struct ConnectionConfig {
    std::string                host;
    std::string                port         = "8443";
    bool                       use_tls      = true;
    std::string                tls_ca_path;
    std::string                tls_cert_path;
    std::string                tls_key_path;
    bool                       verify_peer  = false;  // TODO: prod should be true
    std::chrono::milliseconds  connect_timeout   = kConnectTimeout;
    std::chrono::milliseconds  heartbeat_interval = kHeartbeatInterval;
    std::chrono::milliseconds  heartbeat_timeout  = kHeartbeatTimeout;
    std::chrono::milliseconds  initial_reconnect  = kInitialReconnectDelay;
    std::chrono::milliseconds  max_reconnect      = kMaxReconnectDelay;
    float                      reconnect_multiplier = kReconnectDelayMultiplier;
    bool                       enable_audio_playback = true;
    bool                       enable_audio_record   = false;
    bool                       enable_clipboard_sync = true;
    bool                       enable_file_transfer  = true;
    std::uint32_t              target_fps        = 30;
    float                      initial_quality   = 0.75f;
};

// ============================================================================
// BandwidthMonitor — tracks throughput, computes adaptive quality
// ============================================================================
class BandwidthMonitor {
public:
    BandwidthMonitor()
        : bytes_received_(0)
        , start_time_(Clock::now())
        , current_bps_(0.0)
        , smoothed_bps_(0.0)
        , alpha_(0.3)  // EWMA smoothing factor
        , quality_(0.75f)
        , target_bps_(5'000'000.0)  // 5 Mbps default
    {}

    void record_bytes(std::size_t n) {
        auto now = Clock::now();
        std::lock_guard<std::mutex> lk(mutex_);

        bytes_received_ += n;
        auto elapsed = std::chrono::duration<double>(
            now - start_time_).count();

        if (elapsed >= 1.0) {
            double instant_bps = static_cast<double>(bytes_received_) / elapsed;
            if (smoothed_bps_ == 0.0) {
                smoothed_bps_ = instant_bps;
            } else {
                smoothed_bps_ = alpha_ * instant_bps +
                                (1.0 - alpha_) * smoothed_bps_;
            }
            current_bps_ = smoothed_bps_;
            bytes_received_ = 0;
            start_time_ = now;
            update_quality();
        }
    }

    double bandwidth_bps() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return current_bps_;
    }

    float recommended_quality() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return quality_;
    }

    void set_target_bps(double target) {
        std::lock_guard<std::mutex> lk(mutex_);
        target_bps_ = target;
    }

private:
    using Clock = std::chrono::steady_clock;

    void update_quality() {
        if (target_bps_ <= 0.0) return;
        double ratio = current_bps_ / target_bps_;

        // Map ratio to quality adjustment
        if (ratio < 0.5) {
            quality_ = std::max(kMinQuality, quality_ - kQualityStep * 2.0f);
        } else if (ratio < 0.8) {
            quality_ = std::max(kMinQuality, quality_ - kQualityStep);
        } else if (ratio > 1.2) {
            quality_ = std::min(kMaxQuality, quality_ + kQualityStep);
        } else if (ratio > 1.5) {
            quality_ = std::min(kMaxQuality, quality_ + kQualityStep * 2.0f);
        }
    }

    mutable std::mutex  mutex_;
    std::size_t         bytes_received_;
    Clock::time_point   start_time_;
    double              current_bps_;
    double              smoothed_bps_;
    double              alpha_;
    float               quality_;
    double              target_bps_;
};

// ============================================================================
// VideoFrameQueue — thread-safe bounded queue for decoded video frames
// ============================================================================
class VideoFrameQueue {
public:
    struct DecodedFrame {
        std::vector<std::uint8_t> data;
        std::uint32_t             width        = 0;
        std::uint32_t             height       = 0;
        std::uint32_t             stride       = 0;
        std::uint32_t             pixel_format = 0;  // e.g. BGRA8
        std::uint64_t             pts          = 0;  // presentation timestamp
        std::uint64_t             frame_index  = 0;
        bool                      is_keyframe  = false;
    };

    explicit VideoFrameQueue(std::size_t max_size = kMaxFrameQueueSize)
        : max_size_(max_size)
        , dropped_count_(0)
        , decoded_count_(0)
    {}

    // Push a decoded frame. Returns false if queue full (frame dropped).
    bool push(DecodedFrame frame) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (queue_.size() >= max_size_) {
            dropped_count_++;
            // Drop oldest when over capacity
            queue_.pop();
        }
        queue_.push(std::move(frame));
        decoded_count_++;
        cv_.notify_one();
        return true;
    }

    // Pop a frame, blocking until available or timeout.
    std::optional<DecodedFrame> pop(std::chrono::milliseconds timeout =
                                    std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lk(mutex_);
        if (!cv_.wait_for(lk, timeout, [this] { return !queue_.empty(); })) {
            return std::nullopt;
        }
        DecodedFrame f = std::move(queue_.front());
        queue_.pop();
        return f;
    }

    // Non-blocking try-pop.
    std::optional<DecodedFrame> try_pop() {
        std::lock_guard<std::mutex> lk(mutex_);
        if (queue_.empty()) return std::nullopt;
        DecodedFrame f = std::move(queue_.front());
        queue_.pop();
        return f;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(mutex_);
        while (!queue_.empty()) queue_.pop();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return queue_.size();
    }

    std::uint64_t dropped_count() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return dropped_count_;
    }

    std::uint64_t decoded_count() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return decoded_count_;
    }

private:
    mutable std::mutex                mutex_;
    std::condition_variable           cv_;
    std::queue<DecodedFrame>          queue_;
    std::size_t                       max_size_;
    std::uint64_t                     dropped_count_;
    std::uint64_t                     decoded_count_;
};

// ============================================================================
// AudioPlaybackPipeline — ring-buffer based audio playback pipeline
// ============================================================================
class AudioPlaybackPipeline {
public:
    struct AudioConfig {
        std::uint32_t sample_rate   = 48000;
        std::uint32_t channels      = 2;
        std::uint32_t bytes_per_sample = 2;  // s16le
        std::uint32_t frame_size_ms = 20;
    };

    explicit AudioPlaybackPipeline(const AudioConfig& cfg = AudioConfig{})
        : config_(cfg)
        , ring_buffer_(cfg.sample_rate * cfg.channels * cfg.bytes_per_sample * 2)
        , write_pos_(0)
        , read_pos_(0)
        , playing_(false)
        , underrun_count_(0)
        , total_bytes_written_(0)
        , total_bytes_played_(0)
    {
        SPDLOG_INFO("[audio] playback pipeline created: {} Hz, {} ch",
                    config_.sample_rate, config_.channels);
    }

    // Push encoded audio data for decoding and playback.
    bool push_data(const std::uint8_t* data, std::size_t len) {
        if (!playing_) return false;

        std::lock_guard<std::mutex> lk(mutex_);

        // Simple passthrough — in production this would decode the codec.
        std::size_t space = available_write();
        if (len > space) {
            // Buffer overrun — drop oldest data
            std::size_t excess = len - space;
            read_pos_ = (read_pos_ + excess) % ring_buffer_.size();
            underrun_count_++;
            SPDLOG_WARN("[audio] playback buffer overrun, dropped {} bytes", excess);
        }

        for (std::size_t i = 0; i < len; ++i) {
            ring_buffer_[write_pos_] = data[i];
            write_pos_ = (write_pos_ + 1) % ring_buffer_.size();
        }
        total_bytes_written_ += len;

        SPDLOG_TRACE("[audio] pushed {} bytes, buffer fill {}/{}",
                     len, buffered_bytes(), ring_buffer_.size());
        return true;
    }

    // Read decoded PCM samples. Returns number of bytes read.
    std::size_t read_samples(std::uint8_t* out, std::size_t max_len) {
        std::lock_guard<std::mutex> lk(mutex_);
        std::size_t avail = buffered_bytes();
        std::size_t to_read = std::min(max_len, avail);
        for (std::size_t i = 0; i < to_read; ++i) {
            out[i] = ring_buffer_[read_pos_];
            read_pos_ = (read_pos_ + 1) % ring_buffer_.size();
        }
        total_bytes_played_ += to_read;
        if (to_read < max_len) {
            underrun_count_++;
            // Fill remaining with silence
            std::memset(out + to_read, 0, max_len - to_read);
        }
        return to_read;
    }

    void start() {
        std::lock_guard<std::mutex> lk(mutex_);
        playing_ = true;
        SPDLOG_INFO("[audio] playback started");
    }

    void stop() {
        std::lock_guard<std::mutex> lk(mutex_);
        playing_ = false;
        // Reset buffer positions
        write_pos_ = 0;
        read_pos_  = 0;
        SPDLOG_INFO("[audio] playback stopped");
    }

    bool is_playing() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return playing_;
    }

    AudioConfig get_config() const { return config_; }

    std::uint64_t underrun_count() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return underrun_count_;
    }

    std::uint64_t total_bytes_written() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return total_bytes_written_;
    }

private:
    std::size_t available_write() const {
        std::size_t used = (write_pos_ >= read_pos_)
            ? (write_pos_ - read_pos_)
            : (ring_buffer_.size() - read_pos_ + write_pos_);
        return ring_buffer_.size() - used - 1;  // one byte guard
    }

    std::size_t buffered_bytes() const {
        if (write_pos_ >= read_pos_)
            return write_pos_ - read_pos_;
        else
            return ring_buffer_.size() - read_pos_ + write_pos_;
    }

    AudioConfig              config_;
    std::vector<std::uint8_t> ring_buffer_;
    std::size_t              write_pos_;
    std::size_t              read_pos_;
    bool                     playing_;
    mutable std::mutex       mutex_;
    std::uint64_t            underrun_count_;
    std::uint64_t            total_bytes_written_;
    std::uint64_t            total_bytes_played_;
};

// ============================================================================
// AudioRecordPipeline — microphone capture and encoding
// ============================================================================
class AudioRecordPipeline {
public:
    struct RecordConfig {
        std::uint32_t sample_rate    = 16000;
        std::uint32_t channels       = 1;
        std::uint32_t bytes_per_sample = 2;
        std::uint32_t frame_size_ms  = 40;
        std::uint32_t bitrate        = 32000; // 32 kbps Opus
    };

    explicit AudioRecordPipeline(const RecordConfig& cfg = RecordConfig{})
        : config_(cfg)
        , recording_(false)
        , muted_(false)
        , total_bytes_captured_(0)
        , total_bytes_sent_(0)
    {
        capture_buffer_.reserve(cfg.sample_rate * cfg.channels *
                                cfg.bytes_per_sample * cfg.frame_size_ms / 1000);
        SPDLOG_INFO("[audio] record pipeline created: {} Hz, {} ch, {} bps",
                    config_.sample_rate, config_.channels, config_.bitrate);
    }

    // Raw captured samples from microphone callback.
    void feed_samples(const std::uint8_t* data, std::size_t len) {
        if (!recording_ || muted_) return;

        std::lock_guard<std::mutex> lk(mutex_);
        total_bytes_captured_ += len;

        // Accumulate until we have a full frame
        std::size_t old_size = capture_buffer_.size();
        capture_buffer_.insert(capture_buffer_.end(), data, data + len);

        std::size_t frame_bytes = config_.sample_rate * config_.channels *
                                  config_.bytes_per_sample *
                                  config_.frame_size_ms / 1000;

        while (capture_buffer_.size() >= frame_bytes) {
            // Emit a frame for encoding + sending
            EncodedFrame frame;
            frame.data.assign(capture_buffer_.begin(),
                              capture_buffer_.begin() + frame_bytes);
            frame.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            pending_frames_.push(std::move(frame));
            capture_buffer_.erase(capture_buffer_.begin(),
                                  capture_buffer_.begin() + frame_bytes);
            total_bytes_sent_ += frame_bytes;
        }

        SPDLOG_TRACE("[audio] record fed {} bytes, {} frames pending",
                     len, pending_frames_.size());
    }

    // Pop a frame for transmission. Returns nullopt if empty.
    std::optional<EncodedFrame> pop_frame() {
        std::lock_guard<std::mutex> lk(mutex_);
        if (pending_frames_.empty()) return std::nullopt;
        EncodedFrame f = std::move(pending_frames_.front());
        pending_frames_.pop();
        return f;
    }

    void start() {
        std::lock_guard<std::mutex> lk(mutex_);
        recording_ = true;
        capture_buffer_.clear();
        while (!pending_frames_.empty()) pending_frames_.pop();
        SPDLOG_INFO("[audio] recording started");
    }

    void stop() {
        std::lock_guard<std::mutex> lk(mutex_);
        recording_ = false;
        SPDLOG_INFO("[audio] recording stopped");
    }

    void set_muted(bool muted) {
        std::lock_guard<std::mutex> lk(mutex_);
        muted_ = muted;
        if (muted) {
            capture_buffer_.clear();
            while (!pending_frames_.empty()) pending_frames_.pop();
        }
    }

    bool is_recording() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return recording_;
    }

private:
    struct EncodedFrame {
        std::vector<std::uint8_t> data;
        std::uint64_t             timestamp = 0;
    };

    RecordConfig                    config_;
    bool                            recording_;
    bool                            muted_;
    mutable std::mutex              mutex_;
    std::vector<std::uint8_t>       capture_buffer_;
    std::queue<EncodedFrame>        pending_frames_;
    std::uint64_t                   total_bytes_captured_;
    std::uint64_t                   total_bytes_sent_;
};

// ============================================================================
// ClipboardManager — monitors local clipboard and syncs with remote
// ============================================================================
class ClipboardManager {
public:
    struct ClipboardEntry {
        std::string              mime_type;
        std::vector<std::uint8_t> data;
        std::uint64_t            sequence = 0;
        std::chrono::steady_clock::time_point timestamp;
    };

    using ClipboardCallback = std::function<void(const ClipboardEntry&)>;

    ClipboardManager()
        : sequence_(0)
        , enabled_(true)
        , last_local_hash_(0)
        , poll_interval_ms_(500)
    {}

    void set_on_offer(ClipboardCallback cb) {
        std::lock_guard<std::mutex> lk(mutex_);
        on_offer_ = std::move(cb);
    }

    void set_on_request(ClipboardCallback cb) {
        std::lock_guard<std::mutex> lk(mutex_);
        on_request_ = std::move(cb);
    }

    // Called when remote sends a clipboard offer (remote content changed).
    void on_remote_offer(std::string mime_type, std::vector<std::uint8_t> data) {
        std::lock_guard<std::mutex> lk(mutex_);
        remote_entry_ = ClipboardEntry{
            std::move(mime_type),
            std::move(data),
            ++sequence_,
            std::chrono::steady_clock::now()
        };

        std::size_t hash = compute_hash(remote_entry_->data);
        if (hash != last_remote_hash_) {
            last_remote_hash_ = hash;

            // Auto-accept: request the full data
            if (on_request_) {
                on_request_(*remote_entry_);
            }
        }
    }

    // Called when remote sends clipboard data in response to our request.
    void on_remote_data(std::string mime_type, std::vector<std::uint8_t> data) {
        std::lock_guard<std::mutex> lk(mutex_);
        pending_remote_ = ClipboardEntry{
            std::move(mime_type),
            std::move(data),
            sequence_,
            std::chrono::steady_clock::now()
        };
    }

    // Check local clipboard for changes. Returns entry if changed.
    std::optional<ClipboardEntry> poll_local() {
        if (!enabled_) return std::nullopt;

        std::lock_guard<std::mutex> lk(mutex_);

        // In production, query the OS clipboard.
        // Here we track via hash of last known content.
        std::size_t current_hash = compute_local_clipboard_hash();
        if (current_hash != last_local_hash_ && current_hash != 0) {
            last_local_hash_ = current_hash;

            ClipboardEntry entry;
            entry.mime_type  = "text/plain";
            entry.data       = read_local_clipboard();
            entry.sequence   = ++sequence_;
            entry.timestamp  = std::chrono::steady_clock::now();
            local_entry_ = entry;

            if (on_offer_) {
                on_offer_(entry);
            }
            return entry;
        }
        return std::nullopt;
    }

    std::chrono::milliseconds poll_interval() const { return poll_interval_ms_; }

    void enable() {
        std::lock_guard<std::mutex> lk(mutex_);
        enabled_ = true;
        SPDLOG_INFO("[clipboard] sync enabled");
    }

    void disable() {
        std::lock_guard<std::mutex> lk(mutex_);
        enabled_ = false;
        SPDLOG_INFO("[clipboard] sync disabled");
    }

    bool is_enabled() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return enabled_;
    }

private:
    std::size_t compute_hash(const std::vector<std::uint8_t>& data) const {
        // Simple FNV-1a hash
        std::size_t h = 14695981039346656037ULL;
        for (auto b : data) {
            h ^= b;
            h *= 1099511628211ULL;
        }
        return h;
    }

    std::size_t compute_local_clipboard_hash() const {
        // In production, query OS clipboard
        return 0;  // Stub — always signals no change unless explicitly set
    }

    std::vector<std::uint8_t> read_local_clipboard() const {
        // Stub
        return {};
    }

    mutable std::mutex       mutex_;
    std::optional<ClipboardEntry> local_entry_;
    std::optional<ClipboardEntry> remote_entry_;
    std::optional<ClipboardEntry> pending_remote_;
    std::uint64_t            sequence_;
    bool                     enabled_;
    std::size_t              last_local_hash_;
    std::size_t              last_remote_hash_;
    std::chrono::milliseconds poll_interval_ms_;
    ClipboardCallback        on_offer_;
    ClipboardCallback        on_request_;
};

// ============================================================================
// FileTransferManager — manages incoming/outgoing file transfers
// ============================================================================
class FileTransferManager {
public:
    struct TransferSession {
        std::uint32_t            transfer_id;
        std::string              filename;
        std::uint64_t            total_size;
        std::uint64_t            bytes_transferred;
        std::uint32_t            chunk_size;
        std::uint32_t            total_chunks;
        std::uint32_t            last_acked_chunk;
        bool                     is_inbound;      // true = receiving
        bool                     completed;
        bool                     cancelled;
        std::string              dest_path;       // inbound save path
        std::string              source_path;     // outbound read path
        std::chrono::steady_clock::time_point started_at;
        std::chrono::steady_clock::time_point last_activity;
    };

    using TransferCallback = std::function<void(const TransferSession&)>;
    using ProgressCallback  = std::function<void(std::uint32_t transfer_id,
                                                  float progress)>;
    using CompleteCallback   = std::function<void(std::uint32_t transfer_id,
                                                  bool success)>;

    FileTransferManager()
        : next_transfer_id_(1)
        , enabled_(true)
        , default_chunk_size_(kFileChunkSize)
    {}

    // Initiate an outgoing transfer.
    std::uint32_t start_send(std::string filename,
                             std::string source_path,
                             std::uint64_t total_size) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!enabled_) return 0;

        std::uint32_t tid = next_transfer_id_++;
        TransferSession s;
        s.transfer_id    = tid;
        s.filename       = std::move(filename);
        s.source_path    = std::move(source_path);
        s.total_size     = total_size;
        s.chunk_size     = default_chunk_size_;
        s.total_chunks   = static_cast<std::uint32_t>(
            (total_size + default_chunk_size_ - 1) / default_chunk_size_);
        s.bytes_transferred = 0;
        s.last_acked_chunk   = 0;
        s.is_inbound    = false;
        s.completed     = false;
        s.cancelled     = false;
        s.started_at    = std::chrono::steady_clock::now();
        s.last_activity = s.started_at;

        sessions_[tid] = s;
        SPDLOG_INFO("[file_transfer] outbound transfer {}: {} ({} bytes, {} chunks)",
                    tid, s.filename, s.total_size, s.total_chunks);
        return tid;
    }

    // Accept an incoming transfer.
    std::uint32_t start_receive(std::uint32_t remote_tid,
                                std::string filename,
                                std::uint64_t total_size,
                                std::string dest_path) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!enabled_) return 0;

        std::uint32_t tid = next_transfer_id_++;
        TransferSession s;
        s.transfer_id    = tid;
        s.filename       = std::move(filename);
        s.dest_path      = std::move(dest_path);
        s.total_size     = total_size;
        s.chunk_size     = default_chunk_size_;
        s.total_chunks   = static_cast<std::uint32_t>(
            (total_size + default_chunk_size_ - 1) / default_chunk_size_);
        s.bytes_transferred = 0;
        s.last_acked_chunk   = 0;
        s.is_inbound    = true;
        s.completed     = false;
        s.cancelled     = false;
        s.started_at    = std::chrono::steady_clock::now();
        s.last_activity = s.started_at;

        remote_to_local_[remote_tid] = tid;
        sessions_[tid] = s;
        SPDLOG_INFO("[file_transfer] inbound transfer {}: {} ({} bytes, {} chunks)",
                    tid, s.filename, s.total_size, s.total_chunks);
        return tid;
    }

    // Process an incoming chunk.
    bool on_chunk_received(std::uint32_t remote_tid,
                           std::uint32_t chunk_index,
                           const std::uint8_t* data,
                           std::size_t data_len) {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = remote_to_local_.find(remote_tid);
        if (it == remote_to_local_.end()) {
            SPDLOG_WARN("[file_transfer] chunk for unknown remote transfer {}",
                        remote_tid);
            return false;
        }

        auto sit = sessions_.find(it->second);
        if (sit == sessions_.end() || sit->second.cancelled) return false;

        auto& s = sit->second;
        s.bytes_transferred += data_len;
        s.last_acked_chunk   = std::max(s.last_acked_chunk, chunk_index + 1);
        s.last_activity      = std::chrono::steady_clock::now();

        // In production, write chunk to dest_path file.

        if (s.bytes_transferred >= s.total_size) {
            s.completed = true;
            SPDLOG_INFO("[file_transfer] inbound transfer {} complete", s.transfer_id);
            if (on_complete_) {
                on_complete_(s.transfer_id, true);
            }
        }

        if (on_progress_) {
            float pct = s.total_size > 0
                ? static_cast<float>(s.bytes_transferred) / s.total_size
                : 0.0f;
            on_progress_(s.transfer_id, pct);
        }
        return true;
    }

    // Get the next chunk data for an outbound transfer.
    struct ChunkToSend {
        std::uint32_t             transfer_id;
        std::uint32_t             chunk_index;
        std::vector<std::uint8_t> data;
        bool                      is_last;
    };

    std::optional<ChunkToSend> next_chunk_to_send(std::uint32_t transfer_id) {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = sessions_.find(transfer_id);
        if (it == sessions_.end() || it->second.is_inbound ||
            it->second.completed || it->second.cancelled) {
            return std::nullopt;
        }

        auto& s = it->second;
        std::uint32_t next_chunk = s.last_acked_chunk;

        if (next_chunk >= s.total_chunks) return std::nullopt;

        ChunkToSend c;
        c.transfer_id = s.transfer_id;
        c.chunk_index = next_chunk;

        std::uint64_t offset = static_cast<std::uint64_t>(next_chunk) *
                               s.chunk_size;
        std::size_t   remaining = s.total_size - offset;
        std::size_t   chunk_len = std::min(static_cast<std::size_t>(s.chunk_size),
                                           remaining);
        c.data.resize(chunk_len);
        c.is_last = (next_chunk == s.total_chunks - 1);

        // In production, read from source_path at offset.
        // For now, fill with placeholder.
        std::memset(c.data.data(), 0xCC, chunk_len);

        s.last_acked_chunk = next_chunk + 1;
        s.bytes_transferred += chunk_len;
        s.last_activity = std::chrono::steady_clock::now();

        if (s.bytes_transferred >= s.total_size) {
            s.completed = true;
            SPDLOG_INFO("[file_transfer] outbound transfer {} complete",
                        s.transfer_id);
            if (on_complete_) {
                on_complete_(s.transfer_id, true);
            }
        }

        return c;
    }

    void cancel_transfer(std::uint32_t transfer_id) {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = sessions_.find(transfer_id);
        if (it != sessions_.end()) {
            it->second.cancelled = true;
            SPDLOG_INFO("[file_transfer] transfer {} cancelled", transfer_id);
        }
    }

    const TransferSession* get_session(std::uint32_t transfer_id) const {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = sessions_.find(transfer_id);
        return (it != sessions_.end()) ? &it->second : nullptr;
    }

    void set_on_complete(CompleteCallback cb) {
        std::lock_guard<std::mutex> lk(mutex_);
        on_complete_ = std::move(cb);
    }

    void set_on_progress(ProgressCallback cb) {
        std::lock_guard<std::mutex> lk(mutex_);
        on_progress_ = std::move(cb);
    }

    void enable()  {
        std::lock_guard<std::mutex> lk(mutex_);
        enabled_ = true;
    }
    void disable() {
        std::lock_guard<std::mutex> lk(mutex_);
        enabled_ = false;
    }
    bool is_enabled() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return enabled_;
    }

    // Returns list of active (non-completed, non-cancelled) transfer IDs.
    std::vector<std::uint32_t> active_transfers() const {
        std::lock_guard<std::mutex> lk(mutex_);
        std::vector<std::uint32_t> ids;
        for (const auto& [id, s] : sessions_) {
            if (!s.completed && !s.cancelled) ids.push_back(id);
        }
        return ids;
    }

private:
    mutable std::mutex                          mutex_;
    std::unordered_map<std::uint32_t, TransferSession> sessions_;
    std::unordered_map<std::uint32_t, std::uint32_t>   remote_to_local_;
    std::uint32_t                               next_transfer_id_;
    bool                                        enabled_;
    std::uint32_t                               default_chunk_size_;
    CompleteCallback                            on_complete_;
    ProgressCallback                            on_progress_;
};

// ============================================================================
// InputEventCoalescer — batches and deduplicates input events
// ============================================================================
class InputEventCoalescer {
public:
    struct MouseMoveEvent {
        std::int32_t  x          = 0;
        std::int32_t  y          = 0;
        std::uint64_t timestamp  = 0;
    };

    struct MouseButtonEvent {
        std::uint8_t  button     = 0;
        bool          pressed    = false;
        std::int32_t  x          = 0;
        std::int32_t  y          = 0;
        std::uint64_t timestamp  = 0;
    };

    struct MouseWheelEvent {
        std::int32_t  delta_x    = 0;
        std::int32_t  delta_y    = 0;
        std::uint64_t timestamp  = 0;
    };

    struct KeyEvent {
        std::uint32_t keycode    = 0;
        bool          pressed    = false;
        bool          repeat     = false;
        std::uint64_t timestamp  = 0;
    };

    struct CoalescedBatch {
        std::optional<MouseMoveEvent>  mouse_move;
        std::vector<MouseButtonEvent>  mouse_buttons;
        std::vector<MouseWheelEvent>   mouse_wheels;
        std::vector<KeyEvent>          keys;
        std::uint64_t                  batch_id;
        std::chrono::steady_clock::time_point created_at;
    };

    using FlushCallback = std::function<void(const CoalescedBatch&)>;

    explicit InputEventCoalescer(
        std::chrono::milliseconds window = kInputCoalesceWindow)
        : coalesce_window_(window)
        , batch_id_(0)
        , enabled_(true)
        , last_flush_(std::chrono::steady_clock::now())
    {
        pending_.created_at = std::chrono::steady_clock::now();
    }

    void set_on_flush(FlushCallback cb) {
        std::lock_guard<std::mutex> lk(mutex_);
        on_flush_ = std::move(cb);
    }

    // --- Event feeding methods -----------------------------------------------

    void mouse_move(std::int32_t x, std::int32_t y, std::uint64_t ts = 0) {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lk(mutex_);

        // Coalesce: only keep the latest mouse move position
        pending_.mouse_move = MouseMoveEvent{x, y, ts ? ts : now_us()};
        maybe_flush_locked();
    }

    void mouse_button(std::uint8_t button, bool pressed,
                      std::int32_t x, std::int32_t y, std::uint64_t ts = 0) {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lk(mutex_);
        pending_.mouse_buttons.push_back(
            MouseButtonEvent{button, pressed, x, y, ts ? ts : now_us()});
        maybe_flush_locked();
    }

    void mouse_wheel(std::int32_t dx, std::int32_t dy, std::uint64_t ts = 0) {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lk(mutex_);
        pending_.mouse_wheels.push_back(
            MouseWheelEvent{dx, dy, ts ? ts : now_us()});

        // Merge consecutive wheel deltas
        if (pending_.mouse_wheels.size() > 1) {
            auto& prev = pending_.mouse_wheels[pending_.mouse_wheels.size() - 2];
            auto& cur  = pending_.mouse_wheels.back();
            prev.delta_x += cur.delta_x;
            prev.delta_y += cur.delta_y;
            pending_.mouse_wheels.pop_back();
        }
        maybe_flush_locked();
    }

    void key_event(std::uint32_t keycode, bool pressed, bool repeat = false,
                   std::uint64_t ts = 0) {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lk(mutex_);

        // Deduplicate: if the same key event already exists, replace it
        bool replaced = false;
        for (auto& ke : pending_.keys) {
            if (ke.keycode == keycode) {
                ke.pressed   = pressed;
                ke.repeat    = repeat;
                ke.timestamp = ts ? ts : now_us();
                replaced     = true;
                break;
            }
        }
        if (!replaced) {
            pending_.keys.push_back(
                KeyEvent{keycode, pressed, repeat, ts ? ts : now_us()});
        }
        maybe_flush_locked();
    }

    // Force immediate flush.
    void flush() {
        std::lock_guard<std::mutex> lk(mutex_);
        do_flush_locked();
    }

    // Called periodically by the IO loop to flush coalesced batches.
    void tick() {
        std::lock_guard<std::mutex> lk(mutex_);
        maybe_flush_locked();
    }

    void enable()  {
        std::lock_guard<std::mutex> lk(mutex_);
        enabled_ = true;
    }
    void disable() {
        std::lock_guard<std::mutex> lk(mutex_);
        enabled_ = false;
    }
    bool is_enabled() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return enabled_;
    }

private:
    static std::uint64_t now_us() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void maybe_flush_locked() {
        auto now = std::chrono::steady_clock::now();
        if (now - last_flush_ >= coalesce_window_ &&
            has_pending_events_locked()) {
            do_flush_locked();
        }
    }

    bool has_pending_events_locked() const {
        return pending_.mouse_move.has_value() ||
               !pending_.mouse_buttons.empty() ||
               !pending_.mouse_wheels.empty() ||
               !pending_.keys.empty();
    }

    void do_flush_locked() {
        if (!has_pending_events_locked()) return;

        pending_.batch_id = batch_id_++;
        last_flush_ = std::chrono::steady_clock::now();

        if (on_flush_) {
            on_flush_(pending_);
        }

        // Reset pending batch
        pending_ = CoalescedBatch{};
        pending_.created_at = last_flush_;
    }

    mutable std::mutex              mutex_;
    CoalescedBatch                  pending_;
    std::chrono::milliseconds       coalesce_window_;
    std::uint64_t                   batch_id_;
    bool                            enabled_;
    FlushCallback                   on_flush_;
    std::chrono::steady_clock::time_point last_flush_;
};

// ============================================================================
// MessageFramer — length-prefixed message framing over TCP
// ============================================================================
class MessageFramer {
public:
    enum class ParseState {
        kWaitingHeader,
        kWaitingPayload,
        kError,
    };

    struct ParsedMessage {
        MessageType                type;
        MessageFlag                flags;
        std::vector<std::uint8_t>  payload;
    };

    using MessageCallback = std::function<void(ParsedMessage&&)>;

    explicit MessageFramer(std::size_t max_message_size = kMaxMessageSize)
        : max_message_size_(max_message_size)
        , bytes_received_(0)
        , messages_parsed_(0)
        , parse_errors_(0)
    {
        header_buf_.resize(kHeaderSize);
    }

    void set_on_message(MessageCallback cb) {
        on_message_ = std::move(cb);
    }

    // Feed raw bytes from the socket. Parses complete messages.
    void feed(const std::uint8_t* data, std::size_t len) {
        bytes_received_ += len;

        for (std::size_t i = 0; i < len; /* manual advance */) {
            switch (state_) {
            case ParseState::kWaitingHeader: {
                std::size_t needed = kHeaderSize - header_offset_;
                std::size_t take   = std::min(needed, len - i);
                std::memcpy(header_buf_.data() + header_offset_,
                            data + i, take);
                header_offset_ += take;
                i += take;

                if (header_offset_ == kHeaderSize) {
                    std::uint32_t payload_len = read_u32_be(header_buf_.data());
                    current_type_  = static_cast<MessageType>(
                        read_u16_be(header_buf_.data() + 4));
                    current_flags_ = static_cast<MessageFlag>(
                        read_u16_be(header_buf_.data() + 6));

                    if (payload_len > max_message_size_) {
                        SPDLOG_ERROR("[framer] message too large: {} > {}",
                                     payload_len, max_message_size_);
                        parse_errors_++;
                        state_ = ParseState::kError;
                        return;
                    }

                    payload_buf_.resize(payload_len);
                    payload_offset_ = 0;

                    if (payload_len == 0) {
                        // Zero-length payload — message complete
                        emit_message();
                        header_offset_ = 0;
                    } else {
                        state_ = ParseState::kWaitingPayload;
                    }
                }
                break;
            }

            case ParseState::kWaitingPayload: {
                std::size_t needed = payload_buf_.size() - payload_offset_;
                std::size_t take   = std::min(needed, len - i);
                std::memcpy(payload_buf_.data() + payload_offset_,
                            data + i, take);
                payload_offset_ += take;
                i += take;

                if (payload_offset_ == payload_buf_.size()) {
                    emit_message();
                    header_offset_ = 0;
                    state_ = ParseState::kWaitingHeader;
                }
                break;
            }

            case ParseState::kError:
                // Discard all data in error state
                i = len;
                break;
            }
        }
    }

    // Serialize a message into wire format.
    static std::vector<std::uint8_t> serialize(MessageType type,
                                               MessageFlag flags,
                                               const std::uint8_t* payload,
                                               std::size_t payload_len) {
        std::vector<std::uint8_t> buf(kHeaderSize + payload_len);
        write_u32_be(buf.data(), static_cast<std::uint32_t>(payload_len));
        write_u16_be(buf.data() + 4, static_cast<std::uint16_t>(type));
        write_u16_be(buf.data() + 6, static_cast<std::uint16_t>(flags));
        if (payload && payload_len > 0) {
            std::memcpy(buf.data() + kHeaderSize, payload, payload_len);
        }
        return buf;
    }

    static std::vector<std::uint8_t> serialize(MessageType type,
                                               const std::vector<std::uint8_t>& payload,
                                               MessageFlag flags = MessageFlag::kNone) {
        return serialize(type, flags, payload.data(), payload.size());
    }

    void reset() {
        state_         = ParseState::kWaitingHeader;
        header_offset_ = 0;
        payload_offset_= 0;
        payload_buf_.clear();
    }

    std::uint64_t bytes_received() const { return bytes_received_; }
    std::uint64_t messages_parsed() const { return messages_parsed_; }
    std::uint64_t parse_errors()   const { return parse_errors_; }

private:
    void emit_message() {
        messages_parsed_++;
        SPDLOG_TRACE("[framer] parsed message type=0x{:04x} len={} flags=0x{:04x}",
                     static_cast<std::uint16_t>(current_type_),
                     payload_buf_.size(),
                     static_cast<std::uint16_t>(current_flags_));

        if (on_message_) {
            ParsedMessage msg;
            msg.type    = current_type_;
            msg.flags   = current_flags_;
            msg.payload = std::move(payload_buf_);
            on_message_(std::move(msg));
        }
        payload_buf_.clear();
    }

    ParseState                  state_ = ParseState::kWaitingHeader;
    std::size_t                 max_message_size_;
    std::vector<std::uint8_t>   header_buf_;
    std::size_t                 header_offset_ = 0;
    MessageType                 current_type_  = MessageType::kReservedMax;
    MessageFlag                 current_flags_ = MessageFlag::kNone;
    std::vector<std::uint8_t>   payload_buf_;
    std::size_t                 payload_offset_ = 0;
    MessageCallback             on_message_;
    std::uint64_t               bytes_received_;
    std::uint64_t               messages_parsed_;
    std::uint64_t               parse_errors_;
};

// ============================================================================
// MessageDispatcher — routes parsed messages to appropriate handlers
// ============================================================================
class MessageDispatcher {
public:
    using HandlerFunc = std::function<void(MessageType, MessageFlag,
                                           const std::uint8_t*, std::size_t)>;
    using GenericHandler = std::function<void(MessageType, MessageFlag,
                                              const std::uint8_t*, std::size_t)>;

    MessageDispatcher() = default;

    // Register a handler for a specific message type.
    void register_handler(MessageType type, HandlerFunc handler) {
        std::lock_guard<std::mutex> lk(mutex_);
        handlers_[type] = std::move(handler);
    }

    // Register a catch-all handler for unregistered types.
    void register_fallback(GenericHandler handler) {
        std::lock_guard<std::mutex> lk(mutex_);
        fallback_ = std::move(handler);
    }

    // Dispatch a parsed message.
    void dispatch(MessageFramer::ParsedMessage&& msg) {
        std::lock_guard<std::mutex> lk(mutex_);

        auto it = handlers_.find(msg.type);
        if (it != handlers_.end()) {
            SPDLOG_TRACE("[dispatch] type=0x{:04x} -> registered handler",
                         static_cast<std::uint16_t>(msg.type));
            it->second(msg.type, msg.flags,
                       msg.payload.data(), msg.payload.size());
        } else if (fallback_) {
            SPDLOG_TRACE("[dispatch] type=0x{:04x} -> fallback handler",
                         static_cast<std::uint16_t>(msg.type));
            fallback_(msg.type, msg.flags,
                      msg.payload.data(), msg.payload.size());
        } else {
            SPDLOG_WARN("[dispatch] no handler for message type 0x{:04x}",
                        static_cast<std::uint16_t>(msg.type));
        }
    }

private:
    mutable std::mutex                      mutex_;
    std::unordered_map<MessageType, HandlerFunc> handlers_;
    GenericHandler                          fallback_;
};

// ============================================================================
// Message Serialization Helpers — build binary payloads for each message type
// ============================================================================

// --- Hello ---
static std::vector<std::uint8_t> build_hello_payload(
    const std::string& client_name,
    std::uint32_t protocol_version = kProtocolVersion)
{
    std::uint16_t name_len = static_cast<std::uint16_t>(client_name.size());
    std::vector<std::uint8_t> p(4 + 2 + name_len);
    write_u32_be(p.data(), protocol_version);       // offset 0: version
    write_u16_be(p.data() + 4, name_len);           // offset 4: name length
    std::memcpy(p.data() + 6, client_name.data(), name_len);
    return p;
}

static std::vector<std::uint8_t> build_goodbye_payload(
    std::uint32_t reason_code = 0, const std::string& reason = "")
{
    std::uint16_t reason_len = static_cast<std::uint16_t>(reason.size());
    std::vector<std::uint8_t> p(4 + 2 + reason_len);
    write_u32_be(p.data(), reason_code);
    write_u16_be(p.data() + 4, reason_len);
    std::memcpy(p.data() + 6, reason.data(), reason_len);
    return p;
}

// --- Capabilities ---
struct Capability {
    std::uint16_t code;
    std::uint32_t value;
};
static std::vector<std::uint8_t> build_capabilities_payload(
    const std::vector<Capability>& caps)
{
    std::vector<std::uint8_t> p(caps.size() * 6);
    for (std::size_t i = 0; i < caps.size(); ++i) {
        write_u16_be(p.data() + i * 6,     caps[i].code);
        write_u32_be(p.data() + i * 6 + 2, caps[i].value);
    }
    return p;
}

// --- Video Frame ---
static std::vector<std::uint8_t> build_video_frame_payload(
    std::uint32_t width, std::uint32_t height,
    std::uint32_t codec, std::uint64_t frame_index,
    bool is_keyframe, const std::uint8_t* encoded_data,
    std::size_t encoded_len)
{
    std::vector<std::uint8_t> p(4+4+4+8+1+encoded_len);
    std::size_t off = 0;
    write_u32_be(p.data() + off, width);        off += 4;
    write_u32_be(p.data() + off, height);       off += 4;
    write_u32_be(p.data() + off, codec);        off += 4;
    // frame_index (8 bytes, little-endian for simplicity)
    p[off+0] = static_cast<std::uint8_t>(frame_index & 0xFF);
    p[off+1] = static_cast<std::uint8_t>((frame_index >> 8) & 0xFF);
    p[off+2] = static_cast<std::uint8_t>((frame_index >> 16) & 0xFF);
    p[off+3] = static_cast<std::uint8_t>((frame_index >> 24) & 0xFF);
    p[off+4] = static_cast<std::uint8_t>((frame_index >> 32) & 0xFF);
    p[off+5] = static_cast<std::uint8_t>((frame_index >> 40) & 0xFF);
    p[off+6] = static_cast<std::uint8_t>((frame_index >> 48) & 0xFF);
    p[off+7] = static_cast<std::uint8_t>((frame_index >> 56) & 0xFF);
    off += 8;
    p[off] = is_keyframe ? 1 : 0;               off += 1;
    std::memcpy(p.data() + off, encoded_data, encoded_len);
    return p;
}

// --- Audio Play ---
static std::vector<std::uint8_t> build_audio_play_payload(
    std::uint32_t codec, std::uint32_t sample_rate,
    std::uint32_t channels, std::uint64_t pts,
    const std::uint8_t* data, std::size_t data_len)
{
    std::vector<std::uint8_t> p(4+4+4+8+data_len);
    std::size_t off = 0;
    write_u32_be(p.data() + off, codec);         off += 4;
    write_u32_be(p.data() + off, sample_rate);   off += 4;
    write_u32_be(p.data() + off, channels);      off += 4;
    p[off+0] = static_cast<std::uint8_t>(pts & 0xFF);
    p[off+1] = static_cast<std::uint8_t>((pts >> 8) & 0xFF);
    p[off+2] = static_cast<std::uint8_t>((pts >> 16) & 0xFF);
    p[off+3] = static_cast<std::uint8_t>((pts >> 24) & 0xFF);
    p[off+4] = static_cast<std::uint8_t>((pts >> 32) & 0xFF);
    p[off+5] = static_cast<std::uint8_t>((pts >> 40) & 0xFF);
    p[off+6] = static_cast<std::uint8_t>((pts >> 48) & 0xFF);
    p[off+7] = static_cast<std::uint8_t>((pts >> 56) & 0xFF);
    off += 8;
    std::memcpy(p.data() + off, data, data_len);
    return p;
}

// --- Audio Record ---
static std::vector<std::uint8_t> build_audio_record_payload(
    std::uint32_t codec, std::uint32_t sample_rate,
    std::uint32_t channels, std::uint64_t pts,
    const std::uint8_t* data, std::size_t data_len)
{
    // Same format as playback — just different message type
    return build_audio_play_payload(codec, sample_rate, channels,
                                    pts, data, data_len);
}

// --- Input Events ---
static std::vector<std::uint8_t> build_mouse_move_payload(
    std::int32_t x, std::int32_t y, std::uint64_t timestamp)
{
    std::vector<std::uint8_t> p(16);
    std::size_t off = 0;
    write_u32_be(p.data() + off, static_cast<std::uint32_t>(x)); off += 4;
    write_u32_be(p.data() + off, static_cast<std::uint32_t>(y)); off += 4;
    p[off+0] = static_cast<std::uint8_t>(timestamp & 0xFF);
    p[off+1] = static_cast<std::uint8_t>((timestamp >> 8) & 0xFF);
    p[off+2] = static_cast<std::uint8_t>((timestamp >> 16) & 0xFF);
    p[off+3] = static_cast<std::uint8_t>((timestamp >> 24) & 0xFF);
    p[off+4] = static_cast<std::uint8_t>((timestamp >> 32) & 0xFF);
    p[off+5] = static_cast<std::uint8_t>((timestamp >> 40) & 0xFF);
    p[off+6] = static_cast<std::uint8_t>((timestamp >> 48) & 0xFF);
    p[off+7] = static_cast<std::uint8_t>((timestamp >> 56) & 0xFF);
    return p;
}

static std::vector<std::uint8_t> build_mouse_button_payload(
    std::uint8_t button, bool pressed, std::int32_t x, std::int32_t y,
    std::uint64_t timestamp)
{
    std::vector<std::uint8_t> p(18);
    std::size_t off = 0;
    p[off] = button;                                 off += 1;
    p[off] = pressed ? 1 : 0;                        off += 1;
    write_u32_be(p.data() + off, static_cast<std::uint32_t>(x)); off += 4;
    write_u32_be(p.data() + off, static_cast<std::uint32_t>(y)); off += 4;
    p[off+0] = static_cast<std::uint8_t>(timestamp & 0xFF);
    p[off+1] = static_cast<std::uint8_t>((timestamp >> 8) & 0xFF);
    p[off+2] = static_cast<std::uint8_t>((timestamp >> 16) & 0xFF);
    p[off+3] = static_cast<std::uint8_t>((timestamp >> 24) & 0xFF);
    p[off+4] = static_cast<std::uint8_t>((timestamp >> 32) & 0xFF);
    p[off+5] = static_cast<std::uint8_t>((timestamp >> 40) & 0xFF);
    p[off+6] = static_cast<std::uint8_t>((timestamp >> 48) & 0xFF);
    p[off+7] = static_cast<std::uint8_t>((timestamp >> 56) & 0xFF);
    return p;
}

static std::vector<std::uint8_t> build_mouse_wheel_payload(
    std::int32_t delta_x, std::int32_t delta_y, std::uint64_t timestamp)
{
    std::vector<std::uint8_t> p(16);
    std::size_t off = 0;
    write_u32_be(p.data() + off, static_cast<std::uint32_t>(delta_x)); off += 4;
    write_u32_be(p.data() + off, static_cast<std::uint32_t>(delta_y)); off += 4;
    p[off+0] = static_cast<std::uint8_t>(timestamp & 0xFF);
    p[off+1] = static_cast<std::uint8_t>((timestamp >> 8) & 0xFF);
    p[off+2] = static_cast<std::uint8_t>((timestamp >> 16) & 0xFF);
    p[off+3] = static_cast<std::uint8_t>((timestamp >> 24) & 0xFF);
    p[off+4] = static_cast<std::uint8_t>((timestamp >> 32) & 0xFF);
    p[off+5] = static_cast<std::uint8_t>((timestamp >> 40) & 0xFF);
    p[off+6] = static_cast<std::uint8_t>((timestamp >> 48) & 0xFF);
    p[off+7] = static_cast<std::uint8_t>((timestamp >> 56) & 0xFF);
    return p;
}

static std::vector<std::uint8_t> build_key_event_payload(
    std::uint32_t keycode, bool pressed, bool repeat, std::uint64_t timestamp)
{
    std::vector<std::uint8_t> p(14);
    std::size_t off = 0;
    write_u32_be(p.data() + off, keycode);           off += 4;
    p[off] = pressed ? 1 : 0;                        off += 1;
    p[off] = repeat  ? 1 : 0;                        off += 1;
    p[off+0] = static_cast<std::uint8_t>(timestamp & 0xFF);
    p[off+1] = static_cast<std::uint8_t>((timestamp >> 8) & 0xFF);
    p[off+2] = static_cast<std::uint8_t>((timestamp >> 16) & 0xFF);
    p[off+3] = static_cast<std::uint8_t>((timestamp >> 24) & 0xFF);
    p[off+4] = static_cast<std::uint8_t>((timestamp >> 32) & 0xFF);
    p[off+5] = static_cast<std::uint8_t>((timestamp >> 40) & 0xFF);
    p[off+6] = static_cast<std::uint8_t>((timestamp >> 48) & 0xFF);
    p[off+7] = static_cast<std::uint8_t>((timestamp >> 56) & 0xFF);
    return p;
}

static std::vector<std::uint8_t> build_input_batch_payload(
    const InputEventCoalescer::CoalescedBatch& batch)
{
    // Simple TLV encoding for the batch
    // Format: |count:2| for each event type, followed by individual events
    std::vector<std::uint8_t> p;

    // Helper to append a raw buffer
    auto append = [&](const std::vector<std::uint8_t>& buf) {
        p.insert(p.end(), buf.begin(), buf.end());
    };

    // Mouse move count + data
    std::uint8_t mm_count = batch.mouse_move.has_value() ? 1 : 0;
    p.push_back(mm_count);
    if (batch.mouse_move.has_value()) {
        append(build_mouse_move_payload(
            batch.mouse_move->x, batch.mouse_move->y,
            batch.mouse_move->timestamp));
    }

    // Mouse button count
    std::uint16_t mb_count = static_cast<std::uint16_t>(batch.mouse_buttons.size());
    p.push_back(static_cast<std::uint8_t>(mb_count & 0xFF));
    p.push_back(static_cast<std::uint8_t>((mb_count >> 8) & 0xFF));
    for (auto& mb : batch.mouse_buttons) {
        append(build_mouse_button_payload(mb.button, mb.pressed,
                                          mb.x, mb.y, mb.timestamp));
    }

    // Mouse wheel count
    std::uint16_t mw_count = static_cast<std::uint16_t>(batch.mouse_wheels.size());
    p.push_back(static_cast<std::uint8_t>(mw_count & 0xFF));
    p.push_back(static_cast<std::uint8_t>((mw_count >> 8) & 0xFF));
    for (auto& mw : batch.mouse_wheels) {
        append(build_mouse_wheel_payload(mw.delta_x, mw.delta_y, mw.timestamp));
    }

    // Key event count
    std::uint16_t ke_count = static_cast<std::uint16_t>(batch.keys.size());
    p.push_back(static_cast<std::uint8_t>(ke_count & 0xFF));
    p.push_back(static_cast<std::uint8_t>((ke_count >> 8) & 0xFF));
    for (auto& ke : batch.keys) {
        append(build_key_event_payload(ke.keycode, ke.pressed,
                                       ke.repeat, ke.timestamp));
    }

    // Batch metadata
    p.push_back(static_cast<std::uint8_t>(batch.batch_id & 0xFF));
    p.push_back(static_cast<std::uint8_t>((batch.batch_id >> 8) & 0xFF));
    p.push_back(static_cast<std::uint8_t>((batch.batch_id >> 16) & 0xFF));
    p.push_back(static_cast<std::uint8_t>((batch.batch_id >> 24) & 0xFF));

    return p;
}

// --- Clipboard ---
static std::vector<std::uint8_t> build_clipboard_offer_payload(
    std::uint64_t sequence, const std::string& mime_type,
    const std::vector<std::uint8_t>& hash_prefix)
{
    std::uint16_t mime_len = static_cast<std::uint16_t>(mime_type.size());
    std::uint16_t prefix_len = static_cast<std::uint16_t>(
        std::min(hash_prefix.size(), std::size_t(32)));
    std::vector<std::uint8_t> p(8 + 2 + mime_len + 2 + prefix_len);
    std::size_t off = 0;
    p[off+0] = static_cast<std::uint8_t>(sequence & 0xFF);
    p[off+1] = static_cast<std::uint8_t>((sequence >> 8) & 0xFF);
    p[off+2] = static_cast<std::uint8_t>((sequence >> 16) & 0xFF);
    p[off+3] = static_cast<std::uint8_t>((sequence >> 24) & 0xFF);
    p[off+4] = static_cast<std::uint8_t>((sequence >> 32) & 0xFF);
    p[off+5] = static_cast<std::uint8_t>((sequence >> 40) & 0xFF);
    p[off+6] = static_cast<std::uint8_t>((sequence >> 48) & 0xFF);
    p[off+7] = static_cast<std::uint8_t>((sequence >> 56) & 0xFF);
    off += 8;
    write_u16_be(p.data() + off, mime_len);  off += 2;
    std::memcpy(p.data() + off, mime_type.data(), mime_len); off += mime_len;
    write_u16_be(p.data() + off, prefix_len); off += 2;
    std::memcpy(p.data() + off, hash_prefix.data(), prefix_len);
    return p;
}

static std::vector<std::uint8_t> build_clipboard_request_payload(
    std::uint64_t sequence)
{
    std::vector<std::uint8_t> p(8);
    p[0] = static_cast<std::uint8_t>(sequence & 0xFF);
    p[1] = static_cast<std::uint8_t>((sequence >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((sequence >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((sequence >> 24) & 0xFF);
    p[4] = static_cast<std::uint8_t>((sequence >> 32) & 0xFF);
    p[5] = static_cast<std::uint8_t>((sequence >> 40) & 0xFF);
    p[6] = static_cast<std::uint8_t>((sequence >> 48) & 0xFF);
    p[7] = static_cast<std::uint8_t>((sequence >> 56) & 0xFF);
    return p;
}

static std::vector<std::uint8_t> build_clipboard_data_payload(
    std::uint64_t sequence, const std::string& mime_type,
    const std::vector<std::uint8_t>& data)
{
    std::uint16_t mime_len = static_cast<std::uint16_t>(mime_type.size());
    std::vector<std::uint8_t> p(8 + 2 + mime_len + data.size());
    std::size_t off = 0;
    p[off+0] = static_cast<std::uint8_t>(sequence & 0xFF);
    p[off+1] = static_cast<std::uint8_t>((sequence >> 8) & 0xFF);
    p[off+2] = static_cast<std::uint8_t>((sequence >> 16) & 0xFF);
    p[off+3] = static_cast<std::uint8_t>((sequence >> 24) & 0xFF);
    p[off+4] = static_cast<std::uint8_t>((sequence >> 32) & 0xFF);
    p[off+5] = static_cast<std::uint8_t>((sequence >> 40) & 0xFF);
    p[off+6] = static_cast<std::uint8_t>((sequence >> 48) & 0xFF);
    p[off+7] = static_cast<std::uint8_t>((sequence >> 56) & 0xFF);
    off += 8;
    write_u16_be(p.data() + off, mime_len);  off += 2;
    std::memcpy(p.data() + off, mime_type.data(), mime_len); off += mime_len;
    std::memcpy(p.data() + off, data.data(), data.size());
    return p;
}

// --- File Transfer ---
static std::vector<std::uint8_t> build_file_transfer_init_payload(
    std::uint32_t transfer_id, const std::string& filename,
    std::uint64_t total_size, std::uint32_t chunk_size)
{
    std::uint16_t fname_len = static_cast<std::uint16_t>(filename.size());
    std::vector<std::uint8_t> p(4 + 2 + fname_len + 8 + 4);
    std::size_t off = 0;
    write_u32_be(p.data() + off, transfer_id);       off += 4;
    write_u16_be(p.data() + off, fname_len);          off += 2;
    std::memcpy(p.data() + off, filename.data(), fname_len); off += fname_len;
    p[off+0] = static_cast<std::uint8_t>(total_size & 0xFF);
    p[off+1] = static_cast<std::uint8_t>((total_size >> 8) & 0xFF);
    p[off+2] = static_cast<std::uint8_t>((total_size >> 16) & 0xFF);
    p[off+3] = static_cast<std::uint8_t>((total_size >> 24) & 0xFF);
    p[off+4] = static_cast<std::uint8_t>((total_size >> 32) & 0xFF);
    p[off+5] = static_cast<std::uint8_t>((total_size >> 40) & 0xFF);
    p[off+6] = static_cast<std::uint8_t>((total_size >> 48) & 0xFF);
    p[off+7] = static_cast<std::uint8_t>((total_size >> 56) & 0xFF);
    off += 8;
    write_u32_be(p.data() + off, chunk_size);
    return p;
}

static std::vector<std::uint8_t> build_file_transfer_chunk_payload(
    std::uint32_t transfer_id, std::uint32_t chunk_index,
    const std::uint8_t* data, std::size_t data_len)
{
    std::vector<std::uint8_t> p(4 + 4 + data_len);
    std::size_t off = 0;
    write_u32_be(p.data() + off, transfer_id); off += 4;
    write_u32_be(p.data() + off, chunk_index); off += 4;
    std::memcpy(p.data() + off, data, data_len);
    return p;
}

static std::vector<std::uint8_t> build_file_transfer_ack_payload(
    std::uint32_t transfer_id, std::uint32_t chunk_index)
{
    std::vector<std::uint8_t> p(8);
    write_u32_be(p.data(),     transfer_id);
    write_u32_be(p.data() + 4, chunk_index);
    return p;
}

static std::vector<std::uint8_t> build_file_transfer_done_payload(
    std::uint32_t transfer_id, bool success)
{
    std::vector<std::uint8_t> p(5);
    write_u32_be(p.data(), transfer_id);
    p[4] = success ? 1 : 0;
    return p;
}

// --- Display ---
static std::vector<std::uint8_t> build_display_resize_payload(
    std::uint32_t width, std::uint32_t height, float dpi_scale)
{
    std::vector<std::uint8_t> p(12);
    write_u32_be(p.data(),     width);
    write_u32_be(p.data() + 4, height);
    std::uint32_t dpi_bits;
    std::memcpy(&dpi_bits, &dpi_scale, sizeof(dpi_bits));
    write_u32_be(p.data() + 8, dpi_bits);
    return p;
}

// --- Video Quality Request ---
static std::vector<std::uint8_t> build_video_quality_req_payload(float quality)
{
    std::vector<std::uint8_t> p(4);
    std::uint32_t q_bits;
    std::memcpy(&q_bits, &quality, sizeof(q_bits));
    write_u32_be(p.data(), q_bits);
    return p;
}

// ============================================================================
// Payload Parsing Helpers — extract fields from received message payloads
// ============================================================================

struct ParsedHello {
    std::uint32_t protocol_version;
    std::string   server_name;
};
static std::optional<ParsedHello> parse_hello(const std::uint8_t* data,
                                               std::size_t len) {
    if (len < 6) return std::nullopt;
    ParsedHello h;
    h.protocol_version = read_u32_be(data);
    std::uint16_t name_len = read_u16_be(data + 4);
    if (len < 6 + name_len) return std::nullopt;
    h.server_name.assign(reinterpret_cast<const char*>(data + 6), name_len);
    return h;
}

struct ParsedVideoFrame {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t codec;
    std::uint64_t frame_index;
    bool          is_keyframe;
    const std::uint8_t* encoded_data;
    std::size_t   encoded_len;
};
static std::optional<ParsedVideoFrame> parse_video_frame(const std::uint8_t* data,
                                                          std::size_t len) {
    if (len < 21) return std::nullopt;
    ParsedVideoFrame f;
    f.width       = read_u32_be(data);
    f.height      = read_u32_be(data + 4);
    f.codec       = read_u32_be(data + 8);
    f.frame_index =
        (static_cast<std::uint64_t>(data[12])      ) |
        (static_cast<std::uint64_t>(data[13]) <<  8) |
        (static_cast<std::uint64_t>(data[14]) << 16) |
        (static_cast<std::uint64_t>(data[15]) << 24) |
        (static_cast<std::uint64_t>(data[16]) << 32) |
        (static_cast<std::uint64_t>(data[17]) << 40) |
        (static_cast<std::uint64_t>(data[18]) << 48) |
        (static_cast<std::uint64_t>(data[19]) << 56);
    f.is_keyframe  = data[20] != 0;
    f.encoded_data = data + 21;
    f.encoded_len  = len - 21;
    return f;
}

struct ParsedAudioPlay {
    std::uint32_t codec;
    std::uint32_t sample_rate;
    std::uint32_t channels;
    std::uint64_t pts;
    const std::uint8_t* data;
    std::size_t   data_len;
};
static std::optional<ParsedAudioPlay> parse_audio_play(const std::uint8_t* data,
                                                        std::size_t len) {
    if (len < 20) return std::nullopt;
    ParsedAudioPlay a;
    a.codec       = read_u32_be(data);
    a.sample_rate = read_u32_be(data + 4);
    a.channels    = read_u32_be(data + 8);
    a.pts =
        (static_cast<std::uint64_t>(data[12])      ) |
        (static_cast<std::uint64_t>(data[13]) <<  8) |
        (static_cast<std::uint64_t>(data[14]) << 16) |
        (static_cast<std::uint64_t>(data[15]) << 24) |
        (static_cast<std::uint64_t>(data[16]) << 32) |
        (static_cast<std::uint64_t>(data[17]) << 40) |
        (static_cast<std::uint64_t>(data[18]) << 48) |
        (static_cast<std::uint64_t>(data[19]) << 56);
    a.data     = data + 20;
    a.data_len = len - 20;
    return a;
}

struct ParsedFileTransferInit {
    std::uint32_t transfer_id;
    std::string   filename;
    std::uint64_t total_size;
    std::uint32_t chunk_size;
};
static std::optional<ParsedFileTransferInit> parse_file_transfer_init(
    const std::uint8_t* data, std::size_t len) {
    if (len < 18) return std::nullopt;
    ParsedFileTransferInit f;
    f.transfer_id = read_u32_be(data);
    std::uint16_t fname_len = read_u16_be(data + 4);
    if (len < 6 + fname_len + 12) return std::nullopt;
    f.filename.assign(reinterpret_cast<const char*>(data + 6), fname_len);
    std::size_t off = 6 + fname_len;
    f.total_size =
        (static_cast<std::uint64_t>(data[off+0])      ) |
        (static_cast<std::uint64_t>(data[off+1]) <<  8) |
        (static_cast<std::uint64_t>(data[off+2]) << 16) |
        (static_cast<std::uint64_t>(data[off+3]) << 24) |
        (static_cast<std::uint64_t>(data[off+4]) << 32) |
        (static_cast<std::uint64_t>(data[off+5]) << 40) |
        (static_cast<std::uint64_t>(data[off+6]) << 48) |
        (static_cast<std::uint64_t>(data[off+7]) << 56);
    off += 8;
    f.chunk_size = read_u32_be(data + off);
    return f;
}

struct ParsedFileTransferChunk {
    std::uint32_t transfer_id;
    std::uint32_t chunk_index;
    const std::uint8_t* data;
    std::size_t   data_len;
};
static std::optional<ParsedFileTransferChunk> parse_file_transfer_chunk(
    const std::uint8_t* data, std::size_t len) {
    if (len < 8) return std::nullopt;
    ParsedFileTransferChunk c;
    c.transfer_id = read_u32_be(data);
    c.chunk_index = read_u32_be(data + 4);
    c.data        = data + 8;
    c.data_len    = len - 8;
    return c;
}

// ============================================================================
} // namespace cppdesk::client::detail

// ============================================================================
// Main IO Loop Engine — Public API class defined in io_loop.hpp
// ============================================================================

namespace cppdesk::client {

using namespace detail;

// ============================================================================
// IoLoop::Impl — PIMPL idiom for stable ABI and compile firewall
// ============================================================================
struct IoLoop::Impl : public std::enable_shared_from_this<IoLoop::Impl> {
    // --- Configuration ---
    ConnectionConfig           config_;
    std::atomic<ConnState>     state_{ConnState::kDisconnected};

    // --- ASIO infrastructure ---
    asio::io_context           io_ctx_;
    std::unique_ptr<asio::ip::tcp::socket> socket_;
    std::unique_ptr<asio::ssl::stream<asio::ip::tcp::socket>> ssl_stream_;
    asio::ssl::context         ssl_ctx_;
    asio::steady_timer         heartbeat_timer_;
    asio::steady_timer         heartbeat_timeout_timer_;
    asio::steady_timer         reconnect_timer_;
    asio::steady_timer         clipboard_timer_;
    asio::steady_timer         audio_record_timer_;
    asio::steady_timer         file_transfer_timer_;
    asio::steady_timer         stats_timer_;
    asio::steady_timer         input_flush_timer_;
    asio::ip::tcp::resolver    resolver_;

    // --- IO workers ---
    std::vector<std::thread>   worker_threads_;
    std::unique_ptr<asio::io_context::work> work_guard_;

    // --- Internal components ---
    MessageFramer              framer_;
    MessageDispatcher          dispatcher_;
    std::unique_ptr<BandwidthMonitor>    bandwidth_;
    std::unique_ptr<VideoFrameQueue>     video_queue_;
    std::unique_ptr<AudioPlaybackPipeline> audio_playback_;
    std::unique_ptr<AudioRecordPipeline>  audio_record_;
    std::unique_ptr<ClipboardManager>     clipboard_;
    std::unique_ptr<FileTransferManager>  file_transfer_;
    std::unique_ptr<InputEventCoalescer>  input_coalescer_;

    // --- Reconnect state ---
    std::chrono::milliseconds  reconnect_delay_;
    int                        reconnect_attempts_;

    // --- Heartbeat tracking ---
    std::chrono::steady_clock::time_point last_heartbeat_sent_;
    std::chrono::steady_clock::time_point last_heartbeat_recv_;
    std::uint64_t              heartbeat_seq_;

    // --- Read buffer ---
    std::vector<std::uint8_t>  read_buf_;

    // --- Stats ---
    std::uint64_t              total_bytes_sent_;
    std::uint64_t              total_bytes_rcvd_;
    std::uint64_t              total_messages_sent_;
    std::uint64_t              total_messages_rcvd_;
    std::chrono::steady_clock::time_point session_started_;

    // --- Session info ---
    std::string                client_name_;
    std::string                server_name_;
    std::uint32_t              server_protocol_version_;
    std::uint32_t              local_display_width_;
    std::uint32_t              local_display_height_;

    // --- Callbacks ---
    IoLoop::Callbacks          callbacks_;

    // --- Construction ---
    Impl(const ConnectionConfig& cfg)
        : config_(cfg)
        , ssl_ctx_(asio::ssl::context::tls_client)
        , heartbeat_timer_(io_ctx_)
        , heartbeat_timeout_timer_(io_ctx_)
        , reconnect_timer_(io_ctx_)
        , clipboard_timer_(io_ctx_)
        , audio_record_timer_(io_ctx_)
        , file_transfer_timer_(io_ctx_)
        , stats_timer_(io_ctx_)
        , input_flush_timer_(io_ctx_)
        , resolver_(io_ctx_)
        , reconnect_delay_(cfg.initial_reconnect)
        , reconnect_attempts_(0)
        , heartbeat_seq_(0)
        , total_bytes_sent_(0)
        , total_bytes_rcvd_(0)
        , total_messages_sent_(0)
        , total_messages_rcvd_(0)
        , server_protocol_version_(0)
        , local_display_width_(1920)
        , local_display_height_(1080)
    {
        read_buf_.resize(kReadBufferSize);

        // Initialize sub-components
        bandwidth_        = std::make_unique<BandwidthMonitor>();
        video_queue_      = std::make_unique<VideoFrameQueue>(kMaxFrameQueueSize);
        audio_playback_   = std::make_unique<AudioPlaybackPipeline>();
        audio_record_     = std::make_unique<AudioRecordPipeline>();
        clipboard_        = std::make_unique<ClipboardManager>();
        file_transfer_    = std::make_unique<FileTransferManager>();
        input_coalescer_  = std::make_unique<InputEventCoalescer>(kInputCoalesceWindow);

        // Configure SSL
        if (config_.use_tls) {
            ssl_ctx_.set_default_verify_paths();
            if (!config_.verify_peer) {
                ssl_ctx_.set_verify_mode(asio::ssl::verify_none);
            }
        }

        // Wire up the framer -> dispatcher pipeline
        framer_.set_on_message([this](MessageFramer::ParsedMessage&& msg) {
            total_messages_rcvd_++;
            bandwidth_->record_bytes(msg.payload.size() + kHeaderSize);
            dispatcher_.dispatch(std::move(msg));
        });

        // Wire up input coalescer -> send batch
        input_coalescer_->set_on_flush(
            [this](const InputEventCoalescer::CoalescedBatch& batch) {
                auto payload = build_input_batch_payload(batch);
                send_message(MessageType::kInputBatch, payload);
            });

        // Wire up clipboard offer callback
        clipboard_->set_on_offer(
            [this](const ClipboardManager::ClipboardEntry& entry) {
                auto payload = build_clipboard_offer_payload(
                    entry.sequence, entry.mime_type,
                    std::vector<std::uint8_t>(32, 0)); // stub hash
                send_message(MessageType::kClipboardOffer, payload);
            });

        clipboard_->set_on_request(
            [this](const ClipboardManager::ClipboardEntry& entry) {
                auto payload = build_clipboard_request_payload(entry.sequence);
                send_message(MessageType::kClipboardRequest, payload);
            });

        // Register all message dispatchers
        register_handlers();

        SPDLOG_INFO("[io_loop] implementation initialized");
    }

    ~Impl() {
        SPDLOG_INFO("[io_loop] shutting down implementation");
        disconnect();
        stop();
    }

    // ========================================================================
    // Public API methods
    // ========================================================================

    void set_callbacks(Callbacks&& cbs) {
        callbacks_ = std::move(cbs);
    }

    void set_client_name(const std::string& name) {
        client_name_ = name;
    }

    void set_display_size(std::uint32_t w, std::uint32_t h) {
        local_display_width_  = w;
        local_display_height_ = h;
    }

    // ------ Start / Stop ------

    void start(std::size_t num_threads = 2) {
        if (worker_threads_.size() > 0) {
            SPDLOG_WARN("[io_loop] already running");
            return;
        }

        SPDLOG_INFO("[io_loop] starting with {} worker threads", num_threads);
        work_guard_ = std::make_unique<asio::io_context::work>(io_ctx_);

        for (std::size_t i = 0; i < num_threads; ++i) {
            worker_threads_.emplace_back([this, i] {
                SPDLOG_DEBUG("[io_loop] worker thread {} started", i);
                try {
                    io_ctx_.run();
                } catch (const std::exception& e) {
                    SPDLOG_ERROR("[io_loop] worker thread {} exception: {}", i, e.what());
                }
                SPDLOG_DEBUG("[io_loop] worker thread {} stopped", i);
            });
        }

        // Begin connection attempt
        state_.store(ConnState::kDisconnected);
        asio::post(io_ctx_, [this] { begin_connect(); });
    }

    void stop() {
        SPDLOG_INFO("[io_loop] stopping");
        work_guard_.reset();
        io_ctx_.stop();

        for (auto& t : worker_threads_) {
            if (t.joinable()) t.join();
        }
        worker_threads_.clear();
        SPDLOG_INFO("[io_loop] all worker threads joined");
    }

    // ------ Connection Management ------

    void connect() {
        asio::post(io_ctx_, [this] { begin_connect(); });
    }

    void disconnect() {
        asio::post(io_ctx_, [this] { do_disconnect(0, "client_initiated"); });
    }

    ConnState state() const {
        return state_.load();
    }

    bool is_connected() const {
        return state_.load() == ConnState::kSessionReady;
    }

    // ------ Input Events (from platform layer) ------

    void input_mouse_move(std::int32_t x, std::int32_t y) {
        input_coalescer_->mouse_move(x, y);
    }

    void input_mouse_button(std::uint8_t button, bool pressed,
                            std::int32_t x, std::int32_t y) {
        input_coalescer_->mouse_button(button, pressed, x, y);
    }

    void input_mouse_wheel(std::int32_t dx, std::int32_t dy) {
        input_coalescer_->mouse_wheel(dx, dy);
    }

    void input_key(std::uint32_t keycode, bool pressed, bool repeat) {
        input_coalescer_->key_event(keycode, pressed, repeat);
    }

    void input_flush() {
        input_coalescer_->flush();
    }

    // ------ Audio Recording ------

    void audio_record_start() {
        audio_record_->start();
    }

    void audio_record_stop() {
        audio_record_->stop();
    }

    void audio_record_feed(const std::uint8_t* data, std::size_t len) {
        audio_record_->feed_samples(data, len);
    }

    void audio_record_mute(bool muted) {
        audio_record_->set_muted(muted);
    }

    // ------ File Transfer ------

    std::uint32_t file_transfer_send(const std::string& filename,
                                     const std::string& source_path,
                                     std::uint64_t total_size) {
        return file_transfer_->start_send(filename, source_path, total_size);
    }

    void file_transfer_cancel(std::uint32_t transfer_id) {
        file_transfer_->cancel_transfer(transfer_id);
    }

    // ------ Clipboard ------

    void clipboard_enable()  { clipboard_->enable(); }
    void clipboard_disable() { clipboard_->disable(); }

    // ------ Quality Control ------

    float video_quality() const {
        return bandwidth_->recommended_quality();
    }

    double bandwidth_bps() const {
        return bandwidth_->bandwidth_bps();
    }

    // ------ Video Frame Access ------

    std::optional<VideoFrameQueue::DecodedFrame> dequeue_frame(
        std::chrono::milliseconds timeout) {
        return video_queue_->pop(timeout);
    }

    std::optional<VideoFrameQueue::DecodedFrame> try_dequeue_frame() {
        return video_queue_->try_pop();
    }

    // ------ Audio Playback Access ------

    std::size_t audio_read_samples(std::uint8_t* out, std::size_t max_len) {
        return audio_playback_->read_samples(out, max_len);
    }

    // ========================================================================
    // Connection flow
    // ========================================================================

    void begin_connect() {
        ConnState expected = ConnState::kDisconnected;
        if (state_.compare_exchange_strong(expected, ConnState::kResolving)) {
            SPDLOG_INFO("[io_loop] beginning connection to {}:{}",
                        config_.host, config_.port);
            reconnect_attempts_ = 0;
            do_resolve();
        } else if (expected == ConnState::kReconnecting) {
            // Already in a reconnect cycle — let it proceed
            SPDLOG_DEBUG("[io_loop] connect called while reconnecting, ignoring");
        } else {
            SPDLOG_WARN("[io_loop] connect called in state {}",
                        to_string(expected));
        }
    }

    void do_resolve() {
        state_.store(ConnState::kResolving);

        auto self = shared_from_this();
        resolver_.async_resolve(
            config_.host, config_.port,
            [this, self](const asio::error_code& ec,
                         asio::ip::tcp::resolver::results_type endpoints) {
                if (ec) {
                    SPDLOG_ERROR("[io_loop] resolve failed: {}", ec.message());
                    schedule_reconnect();
                    return;
                }
                for (const auto& ep : endpoints) {
                    SPDLOG_DEBUG("[io_loop] resolved: {}", ep.endpoint());
                }
                do_connect(endpoints);
            });
    }

    void do_connect(asio::ip::tcp::resolver::results_type endpoints) {
        state_.store(ConnState::kConnecting);

        // Create new socket
        socket_ = std::make_unique<asio::ip::tcp::socket>(io_ctx_);

        auto self = shared_from_this();
        asio::async_connect(*socket_, endpoints,
            [this, self](const asio::error_code& ec,
                         const asio::ip::tcp::endpoint& ep) {
                if (ec) {
                    SPDLOG_ERROR("[io_loop] connect to {} failed: {}",
                                 ep, ec.message());
                    socket_.reset();
                    schedule_reconnect();
                    return;
                }
                SPDLOG_INFO("[io_loop] TCP connected to {}", ep);

                if (config_.use_tls) {
                    do_tls_handshake();
                } else {
                    do_handshake();
                }
            });
    }

    void do_tls_handshake() {
        state_.store(ConnState::kTlsHandshaking);

        ssl_stream_ = std::make_unique<
            asio::ssl::stream<asio::ip::tcp::socket>>(
                std::move(*socket_), ssl_ctx_);
        socket_.reset();

        auto self = shared_from_this();
        ssl_stream_->async_handshake(
            asio::ssl::stream_base::client,
            [this, self](const asio::error_code& ec) {
                if (ec) {
                    SPDLOG_ERROR("[io_loop] TLS handshake failed: {}", ec.message());
                    ssl_stream_.reset();
                    schedule_reconnect();
                    return;
                }
                SPDLOG_INFO("[io_loop] TLS handshake complete");
                do_handshake();
            });
    }

    void do_handshake() {
        state_.store(ConnState::kHandshaking);

        auto payload = build_hello_payload(client_name_, kProtocolVersion);
        send_message(MessageType::kHello, payload);
        SPDLOG_INFO("[io_loop] sent Hello (protocol v{})", kProtocolVersion);

        // Start read loop
        begin_read();
        // Start heartbeat timeout
        reset_heartbeat_timeout();
    }

    void do_disconnect(std::uint32_t reason_code, const std::string& reason) {
        auto current = state_.load();
        if (current == ConnState::kDisconnected ||
            current == ConnState::kDisconnecting) {
            return;
        }
        state_.store(ConnState::kDisconnecting);

        SPDLOG_INFO("[io_loop] disconnecting: {} (code={})", reason, reason_code);

        // Cancel all timers
        heartbeat_timer_.cancel();
        heartbeat_timeout_timer_.cancel();
        reconnect_timer_.cancel();
        clipboard_timer_.cancel();
        audio_record_timer_.cancel();
        file_transfer_timer_.cancel();
        stats_timer_.cancel();
        input_flush_timer_.cancel();

        // Send Goodbye if we still have a connection
        auto goodbye = build_goodbye_payload(reason_code, reason);
        send_message(MessageType::kGoodbye, goodbye);

        // Close socket
        if (ssl_stream_) {
            asio::error_code ec;
            ssl_stream_->lowest_layer().close(ec);
            ssl_stream_.reset();
        }
        if (socket_) {
            asio::error_code ec;
            socket_->close(ec);
            socket_.reset();
        }

        // Reset components
        framer_.reset();
        audio_playback_->stop();
        audio_record_->stop();

        state_.store(ConnState::kDisconnected);

        if (callbacks_.on_disconnected) {
            callbacks_.on_disconnected(reason_code, reason);
        }
    }

    // ========================================================================
    // Reconnect logic with exponential backoff
    // ========================================================================

    void schedule_reconnect() {
        state_.store(ConnState::kReconnecting);
        reconnect_attempts_++;

        auto delay = reconnect_delay_;
        SPDLOG_WARN("[io_loop] scheduling reconnect attempt {} in {} ms",
                    reconnect_attempts_, delay.count());

        auto self = shared_from_this();
        reconnect_timer_.expires_after(delay);
        reconnect_timer_.async_wait(
            [this, self](const asio::error_code& ec) {
                if (ec == asio::error::operation_aborted) return;

                // Exponential backoff with jitter
                auto next_delay = std::chrono::milliseconds(
                    static_cast<long long>(
                        reconnect_delay_.count() * config_.reconnect_multiplier));
                if (next_delay > config_.max_reconnect) {
                    next_delay = config_.max_reconnect;
                }
                // Add random jitter: +/- 20%
                static thread_local std::mt19937 rng(std::random_device{}());
                std::uniform_int_distribution<int> jitter(-20, 20);
                auto jitter_ms = next_delay.count() * jitter(rng) / 100;
                reconnect_delay_ = std::chrono::milliseconds(
                    next_delay.count() + jitter_ms);
                if (reconnect_delay_.count() < 0) reconnect_delay_ = kInitialReconnectDelay;

                SPDLOG_INFO("[io_loop] attempting reconnect (attempt {})",
                            reconnect_attempts_);
                begin_connect();
            });
    }

    // ========================================================================
    // Read loop
    // ========================================================================

    void begin_read() {
        auto buf = asio::buffer(read_buf_.data(), read_buf_.size());
        auto self = shared_from_this();

        auto handler = [this, self](const asio::error_code& ec,
                                    std::size_t bytes_transferred) {
            if (ec) {
                if (ec == asio::error::operation_aborted ||
                    ec == asio::error::eof) {
                    SPDLOG_INFO("[io_loop] read closed: {}", ec.message());
                } else {
                    SPDLOG_ERROR("[io_loop] read error: {}", ec.message());
                }
                handle_connection_loss();
                return;
            }

            total_bytes_rcvd_ += bytes_transferred;
            reset_heartbeat_timeout();

            // Feed data to the framer
            framer_.feed(read_buf_.data(), bytes_transferred);

            // Continue reading
            begin_read();
        };

        if (ssl_stream_) {
            ssl_stream_->async_read_some(buf, handler);
        } else if (socket_) {
            socket_->async_read_some(buf, handler);
        }
    }

    // ========================================================================
    // Send message
    // ========================================================================

    void send_message(MessageType type, const std::vector<std::uint8_t>& payload,
                      MessageFlag flags = MessageFlag::kNone) {
        auto wire = MessageFramer::serialize(type, flags,
                                              payload.data(), payload.size());

        auto self = shared_from_this();
        auto buf  = asio::buffer(wire.data(), wire.size());

        auto handler = [this, self, wire = std::move(wire)](
            const asio::error_code& ec, std::size_t bytes_transferred) {
            if (ec) {
                SPDLOG_ERROR("[io_loop] send error: {}", ec.message());
                handle_connection_loss();
                return;
            }
            total_bytes_sent_ += bytes_transferred;
            total_messages_sent_++;
        };

        if (ssl_stream_) {
            asio::async_write(*ssl_stream_, buf, handler);
        } else if (socket_) {
            asio::async_write(*socket_, buf, handler);
        }
    }

    // ========================================================================
    // Heartbeat
    // ========================================================================

    void start_heartbeat() {
        SPDLOG_INFO("[io_loop] starting heartbeat timer (interval={}ms)",
                    config_.heartbeat_interval.count());
        heartbeat_seq_ = 0;
        schedule_heartbeat();
    }

    void schedule_heartbeat() {
        auto self = shared_from_this();
        heartbeat_timer_.expires_after(config_.heartbeat_interval);
        heartbeat_timer_.async_wait(
            [this, self](const asio::error_code& ec) {
                if (ec == asio::error::operation_aborted) return;
                if (state_.load() != ConnState::kSessionReady) return;

                heartbeat_seq_++;
                std::vector<std::uint8_t> payload(8);
                payload[0] = static_cast<std::uint8_t>(heartbeat_seq_ & 0xFF);
                payload[1] = static_cast<std::uint8_t>((heartbeat_seq_ >> 8) & 0xFF);
                payload[2] = static_cast<std::uint8_t>((heartbeat_seq_ >> 16) & 0xFF);
                payload[3] = static_cast<std::uint8_t>((heartbeat_seq_ >> 24) & 0xFF);
                payload[4] = static_cast<std::uint8_t>((heartbeat_seq_ >> 32) & 0xFF);
                payload[5] = static_cast<std::uint8_t>((heartbeat_seq_ >> 40) & 0xFF);
                payload[6] = static_cast<std::uint8_t>((heartbeat_seq_ >> 48) & 0xFF);
                payload[7] = static_cast<std::uint8_t>((heartbeat_seq_ >> 56) & 0xFF);

                send_message(MessageType::kHeartbeat, payload);
                last_heartbeat_sent_ = std::chrono::steady_clock::now();

                SPDLOG_TRACE("[io_loop] heartbeat sent seq={}", heartbeat_seq_);
                schedule_heartbeat();
            });
    }

    void reset_heartbeat_timeout() {
        last_heartbeat_recv_ = std::chrono::steady_clock::now();

        auto self = shared_from_this();
        heartbeat_timeout_timer_.cancel();
        heartbeat_timeout_timer_.expires_after(config_.heartbeat_timeout);
        heartbeat_timeout_timer_.async_wait(
            [this, self](const asio::error_code& ec) {
                if (ec == asio::error::operation_aborted) return;
                SPDLOG_WARN("[io_loop] heartbeat timeout — no data received for {}ms",
                            config_.heartbeat_timeout.count());
                handle_connection_loss();
            });
    }

    // ========================================================================
    // Connection loss handler
    // ========================================================================

    void handle_connection_loss() {
        auto current = state_.load();
        if (current == ConnState::kDisconnected ||
            current == ConnState::kReconnecting ||
            current == ConnState::kDisconnecting) {
            return;
        }

        SPDLOG_WARN("[io_loop] connection lost in state {}", to_string(current));

        // Close existing connection cleanly
        if (ssl_stream_) {
            asio::error_code ec;
            ssl_stream_->lowest_layer().close(ec);
            ssl_stream_.reset();
        }
        if (socket_) {
            asio::error_code ec;
            socket_->close(ec);
            socket_.reset();
        }

        framer_.reset();
        audio_playback_->stop();
        audio_record_->stop();

        // Cancel timers
        heartbeat_timer_.cancel();
        heartbeat_timeout_timer_.cancel();

        // Reset reconnect delay on new connection loss sequence
        reconnect_delay_ = config_.initial_reconnect;

        // Start reconnect
        schedule_reconnect();
    }

    // ========================================================================
    // Message handler registration
    // ========================================================================

    void register_handlers() {
        // --- Control messages ---
        dispatcher_.register_handler(MessageType::kHelloAck,
            [this](MessageType, MessageFlag, const std::uint8_t* data, std::size_t len) {
                auto hello = parse_hello(data, len);
                if (hello) {
                    server_name_ = hello->server_name;
                    server_protocol_version_ = hello->protocol_version;
                    SPDLOG_INFO("[io_loop] received HelloAck from '{}' (protocol v{})",
                                server_name_, server_protocol_version_);
                    on_handshake_complete();
                } else {
                    SPDLOG_ERROR("[io_loop] malformed HelloAck");
                }
            });

        dispatcher_.register_handler(MessageType::kGoodbye,
            [this](MessageType, MessageFlag, const std::uint8_t*, std::size_t) {
                SPDLOG_INFO("[io_loop] received Goodbye from server");
                do_disconnect(0, "server_initiated");
            });

        dispatcher_.register_handler(MessageType::kHeartbeat,
            [this](MessageType, MessageFlag, const std::uint8_t*, std::size_t) {
                // Respond with HeartbeatAck
                send_message(MessageType::kHeartbeatAck, {});
                SPDLOG_TRACE("[io_loop] heartbeat received, ack sent");
            });

        dispatcher_.register_handler(MessageType::kHeartbeatAck,
            [this](MessageType, MessageFlag, const std::uint8_t*, std::size_t) {
                SPDLOG_TRACE("[io_loop] heartbeat ack received");
            });

        dispatcher_.register_handler(MessageType::kError,
            [this](MessageType, MessageFlag, const std::uint8_t* data, std::size_t len) {
                std::string err_msg(reinterpret_cast<const char*>(data),
                                    std::min(len, std::size_t(256)));
                SPDLOG_ERROR("[io_loop] server error: {}", err_msg);
                if (callbacks_.on_error) {
                    callbacks_.on_error(0, err_msg);
                }
            });

        // --- Capabilities ---
        dispatcher_.register_handler(MessageType::kCapabilitiesReq,
            [this](MessageType, MessageFlag, const std::uint8_t*, std::size_t) {
                std::vector<Capability> caps = {
                    {1, 1920},   // max width
                    {2, 1080},   // max height
                    {3, 30},     // max fps
                    {4, 0},      // codec: 0=H264
                    {5, config_.enable_audio_playback ? 1u : 0u},
                    {6, config_.enable_audio_record   ? 1u : 0u},
                    {7, config_.enable_clipboard_sync ? 1u : 0u},
                    {8, config_.enable_file_transfer  ? 1u : 0u},
                };
                auto payload = build_capabilities_payload(caps);
                send_message(MessageType::kCapabilitiesRsp, payload);
                SPDLOG_INFO("[io_loop] sent capabilities response");
            });

        dispatcher_.register_handler(MessageType::kSessionConfig,
            [this](MessageType, MessageFlag, const std::uint8_t*, std::size_t) {
                SPDLOG_INFO("[io_loop] session config received");
                on_session_ready();
            });

        // --- Video frames ---
        dispatcher_.register_handler(MessageType::kVideoFrame,
            [this](MessageType, MessageFlag, const std::uint8_t* data, std::size_t len) {
                handle_video_frame(data, len, false);
            });
        dispatcher_.register_handler(MessageType::kVideoKeyFrame,
            [this](MessageType, MessageFlag, const std::uint8_t* data, std::size_t len) {
                handle_video_frame(data, len, true);
            });

        dispatcher_.register_handler(MessageType::kVideoAck,
            [this](MessageType, MessageFlag, const std::uint8_t*, std::size_t) {
                SPDLOG_TRACE("[io_loop] video ack received");
            });

        dispatcher_.register_handler(MessageType::kVideoQualityReq,
            [this](MessageType, MessageFlag, const std::uint8_t* data, std::size_t len) {
                if (len >= 4) {
                    float quality;
                    std::uint32_t bits = read_u32_be(data);
                    std::memcpy(&quality, &bits, sizeof(quality));
                    SPDLOG_INFO("[io_loop] server requested quality: {:.2f}", quality);
                    bandwidth_->set_target_bps(quality * 10'000'000.0);
                }
            });

        // --- Audio playback ---
        dispatcher_.register_handler(MessageType::kAudioPlay,
            [this](MessageType, MessageFlag, const std::uint8_t* data, std::size_t len) {
                handle_audio_play(data, len);
            });

        dispatcher_.register_handler(MessageType::kAudioPlayStop,
            [this](MessageType, MessageFlag, const std::uint8_t*, std::size_t) {
                audio_playback_->stop();
                SPDLOG_INFO("[io_loop] audio playback stopped by server");
            });

        // --- Clipboard ---
        dispatcher_.register_handler(MessageType::kClipboardOffer,
            [this](MessageType, MessageFlag, const std::uint8_t* data, std::size_t len) {
                // Parse: sequence(8) + mime_type_len(2) + mime_type + prefix_len(2) + prefix
                if (len < 12) return;
                std::uint64_t seq =
                    (static_cast<std::uint64_t>(data[0])      ) |
                    (static_cast<std::uint64_t>(data[1]) <<  8) |
                    (static_cast<std::uint64_t>(data[2]) << 16) |
                    (static_cast<std::uint64_t>(data[3]) << 24) |
                    (static_cast<std::uint64_t>(data[4]) << 32) |
                    (static_cast<std::uint64_t>(data[5]) << 40) |
                    (static_cast<std::uint64_t>(data[6]) << 48) |
                    (static_cast<std::uint64_t>(data[7]) << 56);
                std::uint16_t mime_len = read_u16_be(data + 8);
                if (len < 10 + mime_len + 2) return;
                std::string mime(reinterpret_cast<const char*>(data + 10), mime_len);
                std::size_t off = 10 + mime_len;
                std::uint16_t prefix_len = read_u16_be(data + off);
                off += 2;
                if (len < off + prefix_len) return;
                std::vector<std::uint8_t> prefix(data + off, data + off + prefix_len);

                clipboard_->on_remote_offer(std::move(mime), std::move(prefix));
                SPDLOG_DEBUG("[io_loop] clipboard offer seq={} mime={}", seq, mime);
            });

        dispatcher_.register_handler(MessageType::kClipboardData,
            [this](MessageType, MessageFlag, const std::uint8_t* data, std::size_t len) {
                if (len < 12) return;
                std::uint16_t mime_len = read_u16_be(data + 8);
                if (len < 10 + mime_len) return;
                std::string mime(reinterpret_cast<const char*>(data + 10), mime_len);
                std::size_t off = 10 + mime_len;
                std::vector<std::uint8_t> clip_data(data + off, data + len);

                clipboard_->on_remote_data(std::move(mime), std::move(clip_data));
                SPDLOG_DEBUG("[io_loop] clipboard data received: {} bytes", len - off);
            });

        dispatcher_.register_handler(MessageType::kClipboardAck,
            [this](MessageType, MessageFlag, const std::uint8_t*, std::size_t) {
                SPDLOG_TRACE("[io_loop] clipboard ack received");
            });

        // --- File Transfer ---
        dispatcher_.register_handler(MessageType::kFileTransferInit,
            [this](MessageType, MessageFlag, const std::uint8_t* data, std::size_t len) {
                auto init = parse_file_transfer_init(data, len);
                if (init) {
                    SPDLOG_INFO("[io_loop] file transfer init from server: {} ({} bytes)",
                                init->filename, init->total_size);
                    // Auto-accept?
                    std::string dest = "/tmp/cppdesk_received/" + init->filename;
                    auto local_tid = file_transfer_->start_receive(
                        init->transfer_id, init->filename,
                        init->total_size, dest);
                    if (local_tid > 0) {
                        // Send accept
                        auto ack = build_file_transfer_ack_payload(
                            init->transfer_id, 0);
                        send_message(MessageType::kFileTransferAccept, ack);
                    }
                }
            });

        dispatcher_.register_handler(MessageType::kFileTransferChunk,
            [this](MessageType, MessageFlag, const std::uint8_t* data, std::size_t len) {
                auto chunk = parse_file_transfer_chunk(data, len);
                if (chunk) {
                    file_transfer_->on_chunk_received(
                        chunk->transfer_id, chunk->chunk_index,
                        chunk->data, chunk->data_len);
                    // Send ack
                    auto ack = build_file_transfer_ack_payload(
                        chunk->transfer_id, chunk->chunk_index);
                    send_message(MessageType::kFileTransferAck, ack);
                }
            });

        dispatcher_.register_handler(MessageType::kFileTransferDone,
            [this](MessageType, MessageFlag, const std::uint8_t* data, std::size_t len) {
                if (len >= 5) {
                    std::uint32_t tid = read_u32_be(data);
                    bool success = data[4] != 0;
                    SPDLOG_INFO("[io_loop] file transfer {} done (success={})",
                                tid, success);
                    if (!success) {
                        file_transfer_->cancel_transfer(tid);
                    }
                }
            });

        dispatcher_.register_handler(MessageType::kFileTransferCancel,
            [this](MessageType, MessageFlag, const std::uint8_t* data, std::size_t len) {
                if (len >= 4) {
                    std::uint32_t tid = read_u32_be(data);
                    SPDLOG_INFO("[io_loop] file transfer {} cancelled by server", tid);
                    file_transfer_->cancel_transfer(tid);
                }
            });

        // --- Display ---
        dispatcher_.register_handler(MessageType::kDisplayResize,
            [this](MessageType, MessageFlag, const std::uint8_t* data, std::size_t len) {
                if (len >= 8) {
                    std::uint32_t w = read_u32_be(data);
                    std::uint32_t h = read_u32_be(data + 4);
                    SPDLOG_INFO("[io_loop] server display resize: {}x{}", w, h);
                    if (callbacks_.on_display_resize) {
                        callbacks_.on_display_resize(w, h);
                    }
                }
            });

        // --- Fallback ---
        dispatcher_.register_fallback(
            [this](MessageType type, MessageFlag flags,
                   const std::uint8_t* data, std::size_t len) {
                SPDLOG_DEBUG("[io_loop] unhandled message type=0x{:04x} len={}",
                             static_cast<std::uint16_t>(type), len);
            });

        SPDLOG_INFO("[io_loop] message handlers registered");
    }

    // ========================================================================
    // Session lifecycle
    // ========================================================================

    void on_handshake_complete() {
        SPDLOG_INFO("[io_loop] handshake complete, exchanging capabilities");
        state_.store(ConnState::kCapabilitiesExchanging);

        // Send our capabilities
        std::vector<Capability> caps = {
            {1, local_display_width_},
            {2, local_display_height_},
            {3, config_.target_fps},
            {4, 0},  // supported codec: H264
            {5, config_.enable_audio_playback ? 1u : 0u},
            {6, config_.enable_audio_record   ? 1u : 0u},
            {7, config_.enable_clipboard_sync ? 1u : 0u},
            {8, config_.enable_file_transfer  ? 1u : 0u},
        };
        auto payload = build_capabilities_payload(caps);
        send_message(MessageType::kCapabilitiesRsp, payload);

        // Also send display info
        auto display = build_display_resize_payload(
            local_display_width_, local_display_height_, 1.0f);
        send_message(MessageType::kDisplayInfo, display);

        // Start periodic timers and await session config
        start_heartbeat();
        start_periodic_timers();
    }

    void on_session_ready() {
        state_.store(ConnState::kSessionReady);
        session_started_ = std::chrono::steady_clock::now();
        reconnect_attempts_ = 0;
        reconnect_delay_ = config_.initial_reconnect;

        SPDLOG_INFO("[io_loop] session ready");

        // Start audio playback
        if (config_.enable_audio_playback) {
            audio_playback_->start();
        }

        // Notify callback
        if (callbacks_.on_connected) {
            callbacks_.on_connected();
        }
    }

    // ========================================================================
    // Periodic timers
    // ========================================================================

    void start_periodic_timers() {
        start_clipboard_timer();
        start_audio_record_timer();
        start_file_transfer_timer();
        start_stats_timer();
        start_input_flush_timer();
    }

    // --- Clipboard poll timer ---
    void start_clipboard_timer() {
        if (!config_.enable_clipboard_sync) return;

        auto self = shared_from_this();
        clipboard_timer_.expires_after(clipboard_->poll_interval());
        clipboard_timer_.async_wait(
            [this, self](const asio::error_code& ec) {
                if (ec == asio::error::operation_aborted) return;
                if (state_.load() == ConnState::kSessionReady) {
                    clipboard_->poll_local();
                }
                start_clipboard_timer();
            });
    }

    // --- Audio record timer (poll for pending frames) ---
    void start_audio_record_timer() {
        if (!config_.enable_audio_record) return;

        auto self = shared_from_this();
        audio_record_timer_.expires_after(std::chrono::milliseconds(20));
        audio_record_timer_.async_wait(
            [this, self](const asio::error_code& ec) {
                if (ec == asio::error::operation_aborted) return;
                if (state_.load() == ConnState::kSessionReady) {
                    while (auto frame = audio_record_->pop_frame()) {
                        auto payload = build_audio_record_payload(
                            0,  // codec: Opus
                            audio_record_->RecordConfig{}.sample_rate,
                            audio_record_->RecordConfig{}.channels,
                            frame->timestamp,
                            frame->data.data(), frame->data.size());
                        send_message(MessageType::kAudioRecord, payload);
                    }
                }
                start_audio_record_timer();
            });
    }

    // --- File transfer chunk sender ---
    void start_file_transfer_timer() {
        if (!config_.enable_file_transfer) return;

        auto self = shared_from_this();
        file_transfer_timer_.expires_after(std::chrono::milliseconds(10));
        file_transfer_timer_.async_wait(
            [this, self](const asio::error_code& ec) {
                if (ec == asio::error::operation_aborted) return;
                if (state_.load() == ConnState::kSessionReady) {
                    auto active = file_transfer_->active_transfers();
                    for (auto tid : active) {
                        auto chunk = file_transfer_->next_chunk_to_send(tid);
                        if (chunk) {
                            auto payload = build_file_transfer_chunk_payload(
                                tid, chunk->chunk_index,
                                chunk->data.data(), chunk->data.size());
                            send_message(MessageType::kFileTransferChunk, payload);
                        }
                    }
                }
                start_file_transfer_timer();
            });
    }

    // --- Stats logging timer ---
    void start_stats_timer() {
        auto self = shared_from_this();
        stats_timer_.expires_after(std::chrono::seconds(10));
        stats_timer_.async_wait(
            [this, self](const asio::error_code& ec) {
                if (ec == asio::error::operation_aborted) return;

                auto elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - session_started_).count();

                SPDLOG_INFO("[io_loop] stats: sent={} rcvd={} msgs_sent={} "
                            "msgs_rcvd={} bw={:.1f} Mbps quality={:.2f} "
                            "video_decoded={} dropped={} audio_underruns={}",
                            total_bytes_sent_, total_bytes_rcvd_,
                            total_messages_sent_, total_messages_rcvd_,
                            bandwidth_->bandwidth_bps() / 1'000'000.0,
                            bandwidth_->recommended_quality(),
                            video_queue_->decoded_count(),
                            video_queue_->dropped_count(),
                            audio_playback_->underrun_count());

                start_stats_timer();
            });
    }

    // --- Input event flush timer ---
    void start_input_flush_timer() {
        auto self = shared_from_this();
        input_flush_timer_.expires_after(kInputCoalesceWindow);
        input_flush_timer_.async_wait(
            [this, self](const asio::error_code& ec) {
                if (ec == asio::error::operation_aborted) return;
                if (state_.load() == ConnState::kSessionReady) {
                    input_coalescer_->tick();
                }
                start_input_flush_timer();
            });
    }

    // ========================================================================
    // Video frame handling
    // ========================================================================

    void handle_video_frame(const std::uint8_t* data, std::size_t len,
                            bool keyframe_hint) {
        auto parsed = parse_video_frame(data, len);
        if (!parsed) {
            SPDLOG_WARN("[io_loop] malformed video frame");
            return;
        }

        // In production, this would call into an actual decoder.
        // Here we simulate "decoding" by treating the data as raw RGB/BGRA.

        VideoFrameQueue::DecodedFrame df;
        df.width        = parsed->width;
        df.height       = parsed->height;
        df.stride       = parsed->width * 4;  // assume BGRA
        df.pixel_format = 0;  // BGRA8
        df.frame_index  = parsed->frame_index;
        df.is_keyframe  = parsed->is_keyframe || keyframe_hint;
        df.pts          = parsed->frame_index;  // simplified

        // Simulate decode: copy data as-is, or create placeholder
        if (parsed->encoded_len > 0) {
            df.data.assign(parsed->encoded_data,
                           parsed->encoded_data + parsed->encoded_len);
        } else {
            // Placeholder frame
            df.data.resize(parsed->width * parsed->height * 4);
            std::memset(df.data.data(), 0x80, df.data.size());
        }

        if (!video_queue_->push(std::move(df))) {
            SPDLOG_WARN("[io_loop] video frame dropped (queue full)");
        }

        // Send adaptive quality update
        float q = bandwidth_->recommended_quality();
        auto q_payload = build_video_quality_req_payload(q);
        // Throttle quality updates to avoid spamming
        static std::chrono::steady_clock::time_point last_q_update;
        auto now = std::chrono::steady_clock::now();
        if (now - last_q_update > std::chrono::seconds(2)) {
            send_message(MessageType::kVideoQualityReq, q_payload);
            last_q_update = now;
        }

        SPDLOG_TRACE("[io_loop] video frame #{} decoded ({}x{}, {} bytes)",
                     parsed->frame_index, parsed->width,
                     parsed->height, parsed->encoded_len);
    }

    // ========================================================================
    // Audio playback handling
    // ========================================================================

    void handle_audio_play(const std::uint8_t* data, std::size_t len) {
        auto parsed = parse_audio_play(data, len);
        if (!parsed) {
            SPDLOG_WARN("[io_loop] malformed audio play packet");
            return;
        }

        if (!audio_playback_->is_playing()) {
            audio_playback_->start();
        }

        audio_playback_->push_data(parsed->data, parsed->data_len);

        SPDLOG_TRACE("[io_loop] audio play: {} bytes, codec={}, {}Hz",
                     parsed->data_len, parsed->codec, parsed->sample_rate);
    }
};

// ============================================================================
// IoLoop — Public API implementation (delegates to Impl via PIMPL)
// ============================================================================

IoLoop::IoLoop(const ConnectionConfig& config)
    : impl_(std::make_shared<Impl>(config))
{
    SPDLOG_INFO("[io_loop] created");
}

IoLoop::~IoLoop() {
    SPDLOG_INFO("[io_loop] destroyed");
}

void IoLoop::set_callbacks(Callbacks cbs) {
    impl_->set_callbacks(std::move(cbs));
}

void IoLoop::set_client_name(const std::string& name) {
    impl_->set_client_name(name);
}

void IoLoop::set_display_size(std::uint32_t w, std::uint32_t h) {
    impl_->set_display_size(w, h);
}

void IoLoop::start(std::size_t num_threads) {
    impl_->start(num_threads);
}

void IoLoop::stop() {
    impl_->stop();
}

void IoLoop::connect() {
    impl_->connect();
}

void IoLoop::disconnect() {
    impl_->disconnect();
}

IoLoop::ConnState IoLoop::state() const {
    return impl_->state();
}

bool IoLoop::is_connected() const {
    return impl_->is_connected();
}

void IoLoop::input_mouse_move(std::int32_t x, std::int32_t y) {
    impl_->input_mouse_move(x, y);
}

void IoLoop::input_mouse_button(std::uint8_t btn, bool pressed,
                                std::int32_t x, std::int32_t y) {
    impl_->input_mouse_button(btn, pressed, x, y);
}

void IoLoop::input_mouse_wheel(std::int32_t dx, std::int32_t dy) {
    impl_->input_mouse_wheel(dx, dy);
}

void IoLoop::input_key(std::uint32_t keycode, bool pressed, bool repeat) {
    impl_->input_key(keycode, pressed, repeat);
}

void IoLoop::input_flush() {
    impl_->input_flush();
}

void IoLoop::audio_record_start() {
    impl_->audio_record_start();
}

void IoLoop::audio_record_stop() {
    impl_->audio_record_stop();
}

void IoLoop::audio_record_feed(const std::uint8_t* data, std::size_t len) {
    impl_->audio_record_feed(data, len);
}

void IoLoop::audio_record_mute(bool muted) {
    impl_->audio_record_mute(muted);
}

std::uint32_t IoLoop::file_transfer_send(const std::string& filename,
                                         const std::string& source_path,
                                         std::uint64_t total_size) {
    return impl_->file_transfer_send(filename, source_path, total_size);
}

void IoLoop::file_transfer_cancel(std::uint32_t transfer_id) {
    impl_->file_transfer_cancel(transfer_id);
}

void IoLoop::clipboard_enable() {
    impl_->clipboard_enable();
}

void IoLoop::clipboard_disable() {
    impl_->clipboard_disable();
}

float IoLoop::video_quality() const {
    return impl_->video_quality();
}

double IoLoop::bandwidth_bps() const {
    return impl_->bandwidth_bps();
}

std::optional<VideoFrameQueue::DecodedFrame> IoLoop::dequeue_frame(
    std::chrono::milliseconds timeout) {
    return impl_->dequeue_frame(timeout);
}

std::optional<VideoFrameQueue::DecodedFrame> IoLoop::try_dequeue_frame() {
    return impl_->try_dequeue_frame();
}

std::size_t IoLoop::audio_read_samples(std::uint8_t* out, std::size_t max_len) {
    return impl_->audio_read_samples(out, max_len);
}

// ============================================================================
// Factory function
// ============================================================================

std::unique_ptr<IoLoop> IoLoop::create(const IoLoop::Config& cfg) {
    return std::make_unique<IoLoop>(
        ConnectionConfig{
            .host                 = cfg.host,
            .port                 = cfg.port,
            .use_tls              = cfg.use_tls,
            .tls_ca_path          = cfg.tls_ca_path,
            .tls_cert_path        = cfg.tls_cert_path,
            .tls_key_path         = cfg.tls_key_path,
            .verify_peer          = cfg.verify_peer,
            .enable_audio_playback = cfg.enable_audio_playback,
            .enable_audio_record   = cfg.enable_audio_record,
            .enable_clipboard_sync = cfg.enable_clipboard_sync,
            .enable_file_transfer  = cfg.enable_file_transfer,
            .target_fps            = cfg.target_fps,
            .initial_quality       = cfg.initial_quality,
        });
}

// ============================================================================
// Additional IoLoop public methods — extended API
// ============================================================================

std::string IoLoop::server_name() const {
    return impl_->server_name_;
}

std::uint32_t IoLoop::server_protocol_version() const {
    return impl_->server_protocol_version_;
}

std::uint64_t IoLoop::total_bytes_sent() const {
    return impl_->total_bytes_sent_;
}

std::uint64_t IoLoop::total_bytes_received() const {
    return impl_->total_bytes_rcvd_;
}

std::uint64_t IoLoop::total_messages_sent() const {
    return impl_->total_messages_sent_;
}

std::uint64_t IoLoop::total_messages_received() const {
    return impl_->total_messages_rcvd_;
}

std::uint64_t IoLoop::video_frames_decoded() const {
    return impl_->video_queue_->decoded_count();
}

std::uint64_t IoLoop::video_frames_dropped() const {
    return impl_->video_queue_->dropped_count();
}

std::uint64_t IoLoop::audio_underrun_count() const {
    return impl_->audio_playback_->underrun_count();
}

std::chrono::seconds IoLoop::session_uptime() const {
    auto elapsed = std::chrono::steady_clock::now() - impl_->session_started_;
    return std::chrono::duration_cast<std::chrono::seconds>(elapsed);
}

// ============================================================================
// IoLoop::Impl — additional extended methods
// ============================================================================

// --- Connection health diagnostics ---
void IoLoop::Impl::log_diagnostics() const {
    SPDLOG_INFO("══════════ Connection Diagnostics ══════════");
    SPDLOG_INFO("  State:              {}", to_string(state_.load()));
    SPDLOG_INFO("  Server:             {}:{}", config_.host, config_.port);
    SPDLOG_INFO("  TLS:                {}", config_.use_tls ? "yes" : "no");
    SPDLOG_INFO("  Sent/Received:      {}/{} bytes",
                total_bytes_sent_, total_bytes_rcvd_);
    SPDLOG_INFO("  Messages:           {} sent / {} rcvd",
                total_messages_sent_, total_messages_rcvd_);
    SPDLOG_INFO("  Framer:             {} parsed / {} errors",
                framer_.messages_parsed(), framer_.parse_errors());
    SPDLOG_INFO("  Bandwidth:          {:.1f} Mbps",
                bandwidth_->bandwidth_bps() / 1'000'000.0);
    SPDLOG_INFO("  Quality:            {:.2f}", bandwidth_->recommended_quality());
    SPDLOG_INFO("  Video queue:        {}/{} frames ({} decoded / {} dropped)",
                video_queue_->size(), kMaxFrameQueueSize,
                video_queue_->decoded_count(), video_queue_->dropped_count());
    SPDLOG_INFO("  Audio playback:     {} ({} undruns)",
                audio_playback_->is_playing() ? "active" : "stopped",
                audio_playback_->underrun_count());
    SPDLOG_INFO("  Audio recording:    {}",
                audio_record_->is_recording() ? "active" : "stopped");
    SPDLOG_INFO("  Clipboard sync:     {}",
                clipboard_->is_enabled() ? "enabled" : "disabled");
    SPDLOG_INFO("  File transfers:     {} active",
                file_transfer_->active_transfers().size());
    SPDLOG_INFO("  Reconnect attempts: {}", reconnect_attempts_);
    SPDLOG_INFO("  Reconnect delay:    {} ms", reconnect_delay_.count());
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - session_started_);
    SPDLOG_INFO("  Session uptime:     {} s", uptime.count());
    SPDLOG_INFO("════════════════════════════════════════════");
}

// --- Manual reconnect with custom delay override ---
void IoLoop::Impl::reconnect_now(std::chrono::milliseconds initial_delay) {
    if (state_.load() == ConnState::kReconnecting) {
        SPDLOG_INFO("[io_loop] already reconnecting");
        return;
    }
    reconnect_delay_ = initial_delay;
    reconnect_attempts_ = 0;
    schedule_reconnect();
}

// --- Graceful session suspend / resume ---
void IoLoop::Impl::suspend_session() {
    SPDLOG_INFO("[io_loop] suspending session");
    heartbeat_timer_.cancel();
    clipboard_timer_.cancel();
    audio_record_timer_.cancel();
    file_transfer_timer_.cancel();

    if (ssl_stream_) {
        asio::error_code ec;
        ssl_stream_->lowest_layer().shutdown(
            asio::ip::tcp::socket::shutdown_both, ec);
    }
}

void IoLoop::Impl::resume_session() {
    SPDLOG_INFO("[io_loop] resuming session");
    if (state_.load() == ConnState::kSessionReady) {
        start_heartbeat();
        start_periodic_timers();
    }
}

// --- Force keyframe request ---
void IoLoop::Impl::request_keyframe() {
    if (state_.load() != ConnState::kSessionReady) return;
    SPDLOG_INFO("[io_loop] requesting keyframe from server");

    // A VideoAck with a special flag or empty VideoQualityReq can signal
    // "please send a keyframe". Here we send a quality request with
    // a distinguished value to trigger a keyframe.
    std::vector<std::uint8_t> payload(4);
    float marker = -1.0f;  // Negative quality = keyframe request
    std::uint32_t bits;
    std::memcpy(&bits, &marker, sizeof(bits));
    write_u32_be(payload.data(), bits);
    send_message(MessageType::kVideoQualityReq, payload);
}

// --- Video bitrate target configuration ---
void IoLoop::Impl::set_video_target_bps(double target_bps) {
    bandwidth_->set_target_bps(target_bps);
    SPDLOG_INFO("[io_loop] video target bitrate updated: {:.1f} Mbps",
                target_bps / 1'000'000.0);
}

// --- Audio device configuration ---
void IoLoop::Impl::set_audio_playback_config(
    std::uint32_t sample_rate, std::uint32_t channels) {
    // In production this would reconfigure the audio pipeline.
    // Here we log and restart playback.
    SPDLOG_INFO("[io_loop] audio playback config change: {}Hz {}ch",
                sample_rate, channels);
    audio_playback_->stop();
    audio_playback_->start();
}

void IoLoop::Impl::set_audio_record_config(
    std::uint32_t sample_rate, std::uint32_t channels) {
    SPDLOG_INFO("[io_loop] audio record config change: {}Hz {}ch",
                sample_rate, channels);
    // Record pipeline would be reconfigured
}

// --- Clipboard synchronization helpers ---
void IoLoop::Impl::set_clipboard_poll_interval(std::chrono::milliseconds ms) {
    // The clipboard manager interval is set at construction time;
    // in production, reconfigure the timer.
    SPDLOG_INFO("[io_loop] clipboard poll interval: {} ms", ms.count());
}

// --- Extended file transfer API ---
void IoLoop::Impl::file_transfer_set_download_dir(const std::string& dir) {
    SPDLOG_INFO("[io_loop] file transfer download dir: {}", dir);
    download_dir_ = dir;
}

// ---------------------------------------------------------------------------
// Connection quality assessment — used by adaptive algorithms
// ---------------------------------------------------------------------------
IoLoop::ConnectionQuality IoLoop::Impl::assess_connection_quality() const {
    double bw = bandwidth_->bandwidth_bps();

    if (bw < 500'000)   return IoLoop::ConnectionQuality::kPoor;
    if (bw < 2'000'000) return IoLoop::ConnectionQuality::kFair;
    if (bw < 8'000'000) return IoLoop::ConnectionQuality::kGood;
    return IoLoop::ConnectionQuality::kExcellent;
}

// ---------------------------------------------------------------------------
// Serialize full session state for crash-recovery or telemetry
// ---------------------------------------------------------------------------
std::string IoLoop::Impl::serialize_session_state() const {
    // Produce a JSON-like snapshot of all session state
    std::ostringstream oss;
    oss << "{"
        << "\"state\":\"" << to_string(state_.load()) << "\","
        << "\"server\":\"" << server_name_ << "\","
        << "\"protocol_version\":" << server_protocol_version_ << ","
        << "\"reconnect_attempts\":" << reconnect_attempts_ << ","
        << "\"bytes_sent\":" << total_bytes_sent_ << ","
        << "\"bytes_rcvd\":" << total_bytes_rcvd_ << ","
        << "\"msgs_sent\":" << total_messages_sent_ << ","
        << "\"msgs_rcvd\":" << total_messages_rcvd_ << ","
        << "\"bandwidth_bps\":" << bandwidth_->bandwidth_bps() << ","
        << "\"quality\":" << bandwidth_->recommended_quality() << ","
        << "\"video_decoded\":" << video_queue_->decoded_count() << ","
        << "\"video_dropped\":" << video_queue_->dropped_count() << ","
        << "\"audio_underruns\":" << audio_playback_->underrun_count() << ","
        << "\"framed_parsed\":" << framer_.messages_parsed() << ","
        << "\"framed_errors\":" << framer_.parse_errors()
        << "}";
    return oss.str();
}

// ---------------------------------------------------------------------------
// Backpressure-aware throttling — called when video queue grows too large
// ---------------------------------------------------------------------------
void IoLoop::Impl::check_video_backpressure() {
    auto qsize = video_queue_->size();
    if (qsize > kMaxFrameQueueSize * 3 / 4) {
        // Queue is filling up too fast — reduce quality to throttle server
        float current_q = bandwidth_->recommended_quality();
        float reduced   = std::max(kMinQuality, current_q - kQualityStep * 3.0f);
        SPDLOG_WARN("[io_loop] video backpressure detected (queue={}/{}), "
                    "reducing quality {:.2f} → {:.2f}",
                    qsize, kMaxFrameQueueSize, current_q, reduced);
        auto payload = build_video_quality_req_payload(reduced);
        send_message(MessageType::kVideoQualityReq, payload);
    }
}

// ---------------------------------------------------------------------------
// Graceful drain: flush pending messages before disconnect
// ---------------------------------------------------------------------------
void IoLoop::Impl::graceful_drain(std::chrono::milliseconds timeout) {
    SPDLOG_INFO("[io_loop] graceful drain started (timeout={}ms)", timeout.count());

    // Send any pending clipboard data
    clipboard_->poll_local();

    // Flush any coalesced input events
    input_coalescer_->flush();

    // Allow queued ASIO writes to complete
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        // Poll the io_context to process pending writes
        std::size_t completed = io_ctx_.poll_one();
        if (completed == 0) break;  // No more pending work
    }

    SPDLOG_INFO("[io_loop] graceful drain complete");
}

// ---------------------------------------------------------------------------
// NAT traversal keep-alive — sends small UDP-style probes over TCP
// ---------------------------------------------------------------------------
void IoLoop::Impl::send_nat_keepalive() {
    if (state_.load() != ConnState::kSessionReady) return;

    // Empty heartbeat works as NAT keepalive
    send_message(MessageType::kHeartbeat,
                 std::vector<std::uint8_t>(1, static_cast<std::uint8_t>(
                     heartbeat_seq_ & 0xFF)));
    SPDLOG_TRACE("[io_loop] NAT keepalive sent");
}

// ---------------------------------------------------------------------------
// Latency measurement via round-trip time tracking
// ---------------------------------------------------------------------------
void IoLoop::Impl::measure_latency() {
    if (state_.load() != ConnState::kSessionReady) return;

    latency_seq_++;
    auto now = std::chrono::steady_clock::now();
    pending_latency_probes_[latency_seq_] = now;

    std::vector<std::uint8_t> payload(8);
    payload[0] = 0xFF;  // marker: latency probe
    payload[1] = static_cast<std::uint8_t>(latency_seq_ & 0xFF);
    payload[2] = static_cast<std::uint8_t>((latency_seq_ >> 8) & 0xFF);
    payload[3] = static_cast<std::uint8_t>((latency_seq_ >> 16) & 0xFF);
    payload[4] = static_cast<std::uint8_t>((latency_seq_ >> 24) & 0xFF);
    payload[5] = static_cast<std::uint8_t>((latency_seq_ >> 32) & 0xFF);
    payload[6] = static_cast<std::uint8_t>((latency_seq_ >> 40) & 0xFF);
    payload[7] = static_cast<std::uint8_t>((latency_seq_ >> 48) & 0xFF);

    send_message(MessageType::kHeartbeat, payload);
}

void IoLoop::Impl::record_latency_response(const std::uint8_t* data, std::size_t len) {
    if (len < 8 || data[0] != 0xFF) return;  // Not a latency probe response

    std::uint64_t seq =
        (static_cast<std::uint64_t>(data[1])      ) |
        (static_cast<std::uint64_t>(data[2]) <<  8) |
        (static_cast<std::uint64_t>(data[3]) << 16) |
        (static_cast<std::uint64_t>(data[4]) << 24) |
        (static_cast<std::uint64_t>(data[5]) << 32) |
        (static_cast<std::uint64_t>(data[6]) << 40) |
        (static_cast<std::uint64_t>(data[7]) << 48);

    auto it = pending_latency_probes_.find(seq);
    if (it != pending_latency_probes_.end()) {
        auto rtt = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - it->second);
        rtt_samples_.push_back(rtt);
        if (rtt_samples_.size() > 64) {
            rtt_samples_.pop_front();
        }
        pending_latency_probes_.erase(it);

        // Compute smoothed RTT
        auto sum = std::chrono::milliseconds(0);
        for (auto& s : rtt_samples_) sum += s;
        smoothed_rtt_ = std::chrono::milliseconds(
            sum.count() / static_cast<long long>(rtt_samples_.size()));

        SPDLOG_TRACE("[io_loop] RTT: {} ms (smoothed: {} ms)",
                     rtt.count(), smoothed_rtt_.count());
    }
}

std::chrono::milliseconds IoLoop::Impl::smoothed_rtt() const {
    return smoothed_rtt_;
}

// ============================================================================
// Deadline timer & connection timeout machinery
// ============================================================================

void IoLoop::Impl::start_connect_timeout() {
    auto self = shared_from_this();
    connect_timeout_timer_ = std::make_unique<asio::steady_timer>(io_ctx_);
    connect_timeout_timer_->expires_after(config_.connect_timeout);
    connect_timeout_timer_->async_wait(
        [this, self](const asio::error_code& ec) {
            if (ec == asio::error::operation_aborted) return;

            auto st = state_.load();
            if (st != ConnState::kSessionReady &&
                st != ConnState::kDisconnected &&
                st != ConnState::kDisconnecting) {
                SPDLOG_WARN("[io_loop] connection timeout in state {}",
                            to_string(st));
                handle_connection_loss();
            }
        });
}

// ============================================================================
// Thread-safe message enqueue for external callers
// ============================================================================

void IoLoop::Impl::enqueue_message(MessageType type,
                                   std::vector<std::uint8_t> payload) {
    asio::post(io_ctx_, [this, type, p = std::move(payload)]() mutable {
        send_message(type, p);
    });
}

// ============================================================================
// Graceful shutdown sequence — coordinated stop with drain
// ============================================================================

void IoLoop::Impl::shutdown(std::chrono::milliseconds drain_timeout) {
    SPDLOG_INFO("[io_loop] shutdown sequence initiated");

    // Step 1: Stop accepting new input
    input_coalescer_->disable();

    // Step 2: Drain pending messages
    graceful_drain(drain_timeout);

    // Step 3: Send goodbye
    do_disconnect(0, "client_shutdown");

    // Step 4: Stop all timers explicitly
    heartbeat_timer_.cancel();
    heartbeat_timeout_timer_.cancel();
    reconnect_timer_.cancel();
    clipboard_timer_.cancel();
    audio_record_timer_.cancel();
    file_transfer_timer_.cancel();
    stats_timer_.cancel();
    input_flush_timer_.cancel();

    // Step 5: Stop worker threads
    stop();
    SPDLOG_INFO("[io_loop] shutdown complete");
}

} // namespace cppdesk::client

// ============================================================================
// End of io_loop.cpp
// ============================================================================
