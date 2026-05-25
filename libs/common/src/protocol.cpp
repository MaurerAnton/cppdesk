#include "common/protocol.hpp"
#include "common/crypto.hpp"
#include "common/config.hpp"
#include <spdlog/spdlog.h>
#include <cstring>
#include <algorithm>

namespace cppdesk::common {

// ====== Video Frame ======
VideoFrame VideoFrame::clone() const {
    VideoFrame f;
    f.display = display;
    f.width = width;
    f.height = height;
    f.codec = codec;
    f.timestamp = timestamp;
    f.keyframe = keyframe;
    f.data = data;
    f.is_monitor = is_monitor;
    return f;
}

bool VideoFrame::is_valid() const {
    return width > 0 && height > 0 && !data.empty();
}

size_t VideoFrame::pixel_count() const {
    return static_cast<size_t>(width) * height;
}

size_t VideoFrame::byte_count() const {
    return data.size();
}

// ====== Audio Frame ======
size_t AudioFrame::sample_count() const {
    if (bits_per_sample == 0) return 0;
    return data.size() / (channels * (bits_per_sample / 8));
}

double AudioFrame::duration_seconds() const {
    if (sample_rate == 0) return 0;
    return static_cast<double>(sample_count()) / sample_rate;
}

// ====== Message Serialization ======

class MessageSerializer {
public:
    static std::vector<uint8_t> serialize_login_response(const LoginResponse& r) {
        std::vector<uint8_t> out;
        out.push_back(r.success ? 1 : 0);
        // Message length-prefixed
        uint16_t msg_len = static_cast<uint16_t>(r.message.size());
        out.push_back(static_cast<uint8_t>(msg_len >> 8));
        out.push_back(static_cast<uint8_t>(msg_len & 0xFF));
        out.insert(out.end(), r.message.begin(), r.message.end());
        // Code
        out.push_back(static_cast<uint8_t>(r.code >> 24));
        out.push_back(static_cast<uint8_t>(r.code >> 16));
        out.push_back(static_cast<uint8_t>(r.code >> 8));
        out.push_back(static_cast<uint8_t>(r.code & 0xFF));
        // View only
        out.push_back(r.view_only ? 1 : 0);
        // Resolution
        out.push_back(static_cast<uint8_t>(r.resolution.width >> 24));
        out.push_back(static_cast<uint8_t>(r.resolution.width >> 16));
        out.push_back(static_cast<uint8_t>(r.resolution.width >> 8));
        out.push_back(static_cast<uint8_t>(r.resolution.width & 0xFF));
        out.push_back(static_cast<uint8_t>(r.resolution.height >> 24));
        out.push_back(static_cast<uint8_t>(r.resolution.height >> 16));
        out.push_back(static_cast<uint8_t>(r.resolution.height >> 8));
        out.push_back(static_cast<uint8_t>(r.resolution.height & 0xFF));
        return out;
    }

    static LoginResponse deserialize_login_response(const std::vector<uint8_t>& data) {
        LoginResponse r;
        if (data.size() < 14) return r;
        size_t off = 0;
        r.success = data[off++] != 0;
        uint16_t msg_len = (static_cast<uint16_t>(data[off]) << 8) | data[off+1];
        off += 2;
        if (off + msg_len <= data.size()) {
            r.message = std::string(data.begin() + off, data.begin() + off + msg_len);
            off += msg_len;
        }
        if (off + 4 <= data.size()) {
            r.code = (static_cast<int32_t>(data[off]) << 24) |
                     (static_cast<int32_t>(data[off+1]) << 16) |
                     (static_cast<int32_t>(data[off+2]) << 8) |
                     static_cast<int32_t>(data[off+3]);
            off += 4;
        }
        if (off < data.size()) r.view_only = data[off++] != 0;
        if (off + 8 <= data.size()) {
            r.resolution.width = (static_cast<uint32_t>(data[off]) << 24) |
                                 (static_cast<uint32_t>(data[off+1]) << 16) |
                                 (static_cast<uint32_t>(data[off+2]) << 8) |
                                 static_cast<uint32_t>(data[off+3]);
            off += 4;
            r.resolution.height = (static_cast<uint32_t>(data[off]) << 24) |
                                  (static_cast<uint32_t>(data[off+1]) << 16) |
                                  (static_cast<uint32_t>(data[off+2]) << 8) |
                                  static_cast<uint32_t>(data[off+3]);
        }
        return r;
    }

    static std::vector<uint8_t> serialize_mouse_event(const MouseEvent& ev) {
        std::vector<uint8_t> out(12);
        out[0] = static_cast<uint8_t>(ev.mask >> 24);
        out[1] = static_cast<uint8_t>(ev.mask >> 16);
        out[2] = static_cast<uint8_t>(ev.mask >> 8);
        out[3] = static_cast<uint8_t>(ev.mask & 0xFF);
        out[4] = static_cast<uint8_t>(ev.x >> 24);
        out[5] = static_cast<uint8_t>(ev.x >> 16);
        out[6] = static_cast<uint8_t>(ev.x >> 8);
        out[7] = static_cast<uint8_t>(ev.x & 0xFF);
        out[8] = static_cast<uint8_t>(ev.y >> 24);
        out[9] = static_cast<uint8_t>(ev.y >> 16);
        out[10] = static_cast<uint8_t>(ev.y >> 8);
        out[11] = static_cast<uint8_t>(ev.y & 0xFF);
        return out;
    }

    static MouseEvent deserialize_mouse_event(const std::vector<uint8_t>& data) {
        MouseEvent ev;
        if (data.size() < 12) return ev;
        ev.mask = (static_cast<int32_t>(data[0]) << 24) |
                  (static_cast<int32_t>(data[1]) << 16) |
                  (static_cast<int32_t>(data[2]) << 8) |
                  static_cast<int32_t>(data[3]);
        ev.x = (static_cast<int32_t>(data[4]) << 24) |
               (static_cast<int32_t>(data[5]) << 16) |
               (static_cast<int32_t>(data[6]) << 8) |
               static_cast<int32_t>(data[7]);
        ev.y = (static_cast<int32_t>(data[8]) << 24) |
               (static_cast<int32_t>(data[9]) << 16) |
               (static_cast<int32_t>(data[10]) << 8) |
               static_cast<int32_t>(data[11]);
        return ev;
    }

    static std::vector<uint8_t> serialize_video_frame(const VideoFrame& frame) {
        std::vector<uint8_t> out;
        // Header: 28 bytes
        auto push_u32 = [&out](uint32_t v) {
            out.push_back(static_cast<uint8_t>(v >> 24));
            out.push_back(static_cast<uint8_t>(v >> 16));
            out.push_back(static_cast<uint8_t>(v >> 8));
            out.push_back(static_cast<uint8_t>(v & 0xFF));
        };
        auto push_u64 = [&out](uint64_t v) {
            for (int i = 7; i >= 0; i--)
                out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
        };

        push_u32(frame.display);
        push_u32(frame.width);
        push_u32(frame.height);
        push_u32(frame.codec);
        push_u64(frame.timestamp);
        out.push_back(frame.keyframe ? 1 : 0);
        out.push_back(frame.is_monitor ? 1 : 0);
        out.push_back(0); out.push_back(0); // padding

        uint32_t data_len = static_cast<uint32_t>(frame.data.size());
        push_u32(data_len);
        out.insert(out.end(), frame.data.begin(), frame.data.end());

        return out;
    }

    static VideoFrame deserialize_video_frame(const std::vector<uint8_t>& data) {
        VideoFrame frame;
        if (data.size() < 32) return frame;
        size_t off = 0;
        auto read_u32 = [&data, &off]() -> uint32_t {
            uint32_t v = (static_cast<uint32_t>(data[off]) << 24) |
                         (static_cast<uint32_t>(data[off+1]) << 16) |
                         (static_cast<uint32_t>(data[off+2]) << 8) |
                         static_cast<uint32_t>(data[off+3]);
            off += 4;
            return v;
        };
        auto read_u64 = [&data, &off]() -> uint64_t {
            uint64_t v = 0;
            for (int i = 0; i < 8; i++) {
                v = (v << 8) | data[off++];
            }
            return v;
        };

        frame.display = read_u32();
        frame.width = read_u32();
        frame.height = read_u32();
        frame.codec = read_u32();
        frame.timestamp = read_u64();
        frame.keyframe = data[off++] != 0;
        frame.is_monitor = data[off++] != 0;
        off += 2; // padding
        uint32_t data_len = read_u32();
        if (off + data_len <= data.size()) {
            frame.data.assign(data.begin() + off, data.begin() + off + data_len);
        }
        return frame;
    }
};

// ====== Message Handler / Dispatcher ======

class MessageDispatcher {
public:
    using Handler = std::function<void(MessageType, const std::vector<uint8_t>&)>;

    void register_handler(MessageType type, Handler handler) {
        handlers_[type] = std::move(handler);
    }

    void register_default_handler(Handler handler) {
        default_handler_ = std::move(handler);
    }

    void dispatch(MessageType type, const std::vector<uint8_t>& data) {
        auto it = handlers_.find(type);
        if (it != handlers_.end()) {
            it->second(type, data);
        } else if (default_handler_) {
            default_handler_(type, data);
        }
    }

    void clear() { handlers_.clear(); }
    size_t handler_count() const { return handlers_.size(); }

private:
    std::map<MessageType, Handler> handlers_;
    Handler default_handler_;
};

// ====== Protocol State Machine ======

enum class ProtocolState {
    DISCONNECTED,
    TCP_CONNECTED,
    SECURE_HANDSHAKE,
    LOGIN_SENT,
    LOGIN_ACCEPTED,
    CONNECTED,
    ERROR,
};

class ProtocolFSM {
public:
    ProtocolFSM() = default;

    bool can_send(MessageType type) const {
        switch (state_) {
            case ProtocolState::DISCONNECTED:
                return false;
            case ProtocolState::TCP_CONNECTED:
                return type == MessageType::LOGIN;
            case ProtocolState::SECURE_HANDSHAKE:
                return false; // only handshake messages
            case ProtocolState::LOGIN_SENT:
                return false; // waiting for response
            case ProtocolState::LOGIN_ACCEPTED:
            case ProtocolState::CONNECTED:
                return type != MessageType::LOGIN;
            case ProtocolState::ERROR:
                return false;
        }
        return false;
    }

    void transition(ProtocolState new_state) {
        auto old = state_;
        state_ = new_state;
        spdlog::debug("Protocol state: {} -> {}", static_cast<int>(old),
            static_cast<int>(new_state));
    }

    ProtocolState state() const { return state_; }
    bool is_connected() const { return state_ == ProtocolState::CONNECTED; }
    bool is_error() const { return state_ == ProtocolState::ERROR; }

private:
    ProtocolState state_ = ProtocolState::DISCONNECTED;
};

// ====== KeepAlive ======

class KeepAlive {
public:
    KeepAlive(std::chrono::seconds interval = std::chrono::seconds(10))
        : interval_(interval) {}

    void start(StreamPtr stream) {
        stream_ = std::move(stream);
        running_ = true;
        worker_ = std::thread([this]() {
            while (running_) {
                std::this_thread::sleep_for(interval_);
                if (running_ && stream_ && stream_->is_open()) {
                    // Send heartbeat
                    std::vector<uint8_t> heartbeat;
                    heartbeat.push_back(static_cast<uint8_t>(MessageType::HEARTBEAT));
                    stream_->send(heartbeat);
                    heartbeat_count_++;
                }
            }
        });
    }

    void stop() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
    }

    uint64_t heartbeat_count() const { return heartbeat_count_; }

private:
    std::chrono::seconds interval_;
    StreamPtr stream_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::atomic<uint64_t> heartbeat_count_{0};
};

// ====== Replay Protection ======

class ReplayProtection {
public:
    bool check_and_record(uint32_t seq) {
        std::lock_guard lk(mutex_);
        // Simple sliding window of 1024 sequence numbers
        if (seq <= last_seq_ && last_seq_ - seq < 1024) {
            size_t idx = seq % 1024;
            if (seen_[idx]) {
                spdlog::warn("Replay detected: seq {}", seq);
                return false;
            }
        }
        seen_[seq % 1024] = true;
        if (seq > last_seq_) last_seq_ = seq;
        return true;
    }

    void reset() {
        std::lock_guard lk(mutex_);
        std::memset(seen_, 0, sizeof(seen_));
        last_seq_ = 0;
    }

private:
    bool seen_[1024] = {};
    uint32_t last_seq_ = 0;
    std::mutex mutex_;
};

// ====== Statistics ======

struct ConnectionStats {
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    uint64_t messages_sent = 0;
    uint64_t messages_received = 0;
    uint64_t video_frames_sent = 0;
    uint64_t video_frames_received = 0;
    uint64_t audio_frames_sent = 0;
    uint64_t audio_frames_received = 0;
    uint64_t mouse_events = 0;
    uint64_t key_events = 0;
    uint64_t clipboard_events = 0;
    std::chrono::system_clock::time_point connected_at;
    std::chrono::steady_clock::time_point last_activity;

    double duration_seconds() const {
        return std::chrono::duration<double>(
            std::chrono::system_clock::now() - connected_at).count();
    }

    double send_bandwidth() const {
        auto dur = duration_seconds();
        return dur > 0 ? bytes_sent / dur : 0;
    }

    double recv_bandwidth() const {
        auto dur = duration_seconds();
        return dur > 0 ? bytes_received / dur : 0;
    }

    void record_sent(size_t bytes, MessageType type) {
        bytes_sent += bytes;
        messages_sent++;
        switch (type) {
            case MessageType::VIDEO_FRAME: video_frames_sent++; break;
            case MessageType::AUDIO_FRAME: audio_frames_sent++; break;
            case MessageType::MOUSE_EVENT: mouse_events++; break;
            case MessageType::KEY_EVENT: key_events++; break;
            case MessageType::CLIPBOARD_TEXT: clipboard_events++; break;
            default: break;
        }
        last_activity = std::chrono::steady_clock::now();
    }

    void record_received(size_t bytes, MessageType type) {
        bytes_received += bytes;
        messages_received++;
        switch (type) {
            case MessageType::VIDEO_FRAME: video_frames_received++; break;
            case MessageType::AUDIO_FRAME: audio_frames_received++; break;
            default: break;
        }
        last_activity = std::chrono::steady_clock::now();
    }
};

} // namespace cppdesk::common
