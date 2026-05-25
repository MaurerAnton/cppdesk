#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "config.hpp"

namespace cppdesk::common {

// Message types
enum class MessageType : uint32_t {
    // Rendezvous messages
    REGISTER_PEER = 0,
    PUNCH_HOLE_REQUEST = 1,
    PUNCH_HOLE_RESPONSE = 2,
    REQUEST_RELAY = 3,
    TEST_NAT = 4,
    QUERY_ONLINE = 5,
    HEARTBEAT = 6,
    
    // Control messages
    LOGIN = 10,
    LOGIN_RESPONSE = 11,
    SWITCH_DISPLAY = 12,
    SWITCH_PERMISSION = 13,
    CLOSE_CONNECTION = 14,
    
    // Video
    VIDEO_FRAME = 20,
    VIDEO_CODEC_CHANGE = 21,
    VIDEO_QUALITY_CHANGE = 22,
    
    // Audio
    AUDIO_FRAME = 30,
    AUDIO_CONFIG = 31,
    
    // Input
    MOUSE_EVENT = 40,
    KEY_EVENT = 41,
    CURSOR_DATA = 42,
    CURSOR_POSITION = 43,
    CURSOR_SHAPE = 44,
    
    // Clipboard
    CLIPBOARD_TEXT = 50,
    CLIPBOARD_FILE = 51,
    CLIPBOARD_IMAGE = 52,
    
    // File transfer
    FILE_TRANSFER_REQUEST = 60,
    FILE_TRANSFER_RESPONSE = 61,
    FILE_CHUNK = 62,
    FILE_DONE = 63,
    FILE_DIR = 64,
    
    // Misc
    MISC = 70,
    CHAT_MESSAGE = 71,
    PRIVACY_MODE = 72,
    PORT_FORWARD = 73,
    WHITEBOARD = 74,
    
    // Service
    SUBSCRIBE_SERVICE = 80,
    UNSUBSCRIBE_SERVICE = 81,
    SERVICE_DATA = 82,
};

// Thumbnail / preview
struct ThumbnailData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels; // RGBA
};

// Cursor data
struct CursorData {
    uint32_t id = 0;
    int32_t hot_x = 0;
    int32_t hot_y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> colors; // RGBA
};

// Mouse event
struct MouseEvent {
    int32_t mask = 0;
    int32_t x = 0;
    int32_t y = 0;
    
    static constexpr int32_t BUTTON_LEFT = 0x01;
    static constexpr int32_t BUTTON_RIGHT = 0x02;
    static constexpr int32_t BUTTON_WHEEL = 0x04;
    static constexpr int32_t BUTTON_BACK = 0x08;
    static constexpr int32_t BUTTON_FORWARD = 0x10;
    static constexpr int32_t TYPE_MOVE = 0;
    static constexpr int32_t TYPE_DOWN = 1;
    static constexpr int32_t TYPE_UP = 2;
    static constexpr int32_t TYPE_WHEEL = 3;
};

// Key event
struct KeyEvent {
    uint32_t keycode = 0;
    bool down = false;
    bool is_modifier = false;
    std::string sequence; // for text input
};

// Video frame
struct VideoFrame {
    uint32_t display = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t codec = 0; // 0=raw, 1=h264, 2=h265
    uint64_t timestamp = 0;
    bool keyframe = false;
    std::vector<uint8_t> data;
    bool is_monitor = true;
};

// Audio frame
struct AudioFrame {
    uint32_t sample_rate = 48000;
    uint32_t channels = 2;
    uint32_t bits_per_sample = 16;
    uint64_t timestamp = 0;
    std::vector<uint8_t> data;
};

// Login response
struct LoginResponse {
    bool success = false;
    std::string message;
    int32_t code = 0;
    bool view_only = false;
    Resolution resolution;
};

// Control permissions
struct ControlPermissions {
    bool keyboard = true;
    bool clipboard = true;
    bool file_transfer = true;
    bool audio = true;
    bool restart = false;
    bool shutdown = false;
    bool privacy_mode = false;
};

// Network stream abstraction
class Stream {
public:
    virtual ~Stream() = default;
    virtual bool send(const std::vector<uint8_t>& data) = 0;
    virtual std::vector<uint8_t> recv() = 0;
    virtual bool is_open() const = 0;
    virtual void close() = 0;
    virtual std::string local_addr() const = 0;
    virtual std::string remote_addr() const = 0;
    virtual void set_nodelay(bool on) = 0;
    virtual void set_encryption_key(const std::vector<uint8_t>& key) = 0;
};

using StreamPtr = std::shared_ptr<Stream>;

// Connection callback
using OnMessage = std::function<void(MessageType type, const std::vector<uint8_t>& data)>;
using OnClose = std::function<void(const std::string& reason)>;
using OnError = std::function<void(int code, const std::string& msg)>;

} // namespace cppdesk::common
