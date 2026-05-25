#pragma once

#include "common/protocol.hpp"
#include "common/config.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <map>
#include <optional>

namespace cppdesk::client {

using namespace common;
using common::StreamPtr;

// Client-side interface (callbacks from UI)
class ClientInterface {
public:
    virtual ~ClientInterface() = default;
    virtual std::string get_id() = 0;
    virtual void update_direct(bool direct) = 0;
    virtual void update_received(bool received) = 0;
    virtual bool is_force_relay() = 0;
    
    // Login message delegates
    virtual void on_login_error(const std::string& msg) {}
    virtual void on_login_success() {}
    virtual void on_connection_ready() {}
    virtual void on_connection_closed(const std::string& reason) {}
    
    // Video
    virtual void on_video_frame(const VideoFrame& frame) {}
    virtual void on_audio_frame(const AudioFrame& frame) {}
    
    // Input
    virtual void on_cursor_data(const CursorData& cursor) {}
    virtual void on_cursor_position(int32_t x, int32_t y) {}
    
    // Clipboard
    virtual void on_clipboard_text(const std::string& text) {}
    virtual void on_clipboard_files(const std::vector<std::string>& files) {}
    
    // File transfer
    virtual void on_file_transfer_request(const std::string& path, uint64_t size) {}
    virtual void on_file_transfer_progress(uint64_t transferred, uint64_t total) {}
    virtual void on_file_transfer_done(const std::string& path) {}
    
    // Misc
    virtual void on_chat_message(const std::string& sender, const std::string& msg) {}
    virtual void on_privacy_mode_changed(bool enabled) {}
    virtual void on_switch_display(uint32_t idx) {}
    virtual void on_permission_changed(const ControlPermissions& perms) {}
    
    // Handshake
    virtual std::string get_peer_id() = 0;
    virtual std::string get_key() = 0;
    virtual std::string get_token() = 0;
    virtual ConnType get_conn_type() = 0;
};

/// Client — connects to a remote peer
class Client {
public:
    Client() = default;
    ~Client();
    
    /// Start connection to peer
    bool start(const std::string& peer_id, const std::string& key,
        const std::string& token, ConnType conn_type,
        std::shared_ptr<ClientInterface> iface);
    
    /// Close the connection
    void close();
    
    /// Check if connected
    bool is_connected() const;
    
    /// Get connection info
    std::string get_peer_id() const;
    std::string get_transport_type() const;
    
    /// Send input events
    void send_mouse_event(const MouseEvent& ev);
    void send_key_event(const KeyEvent& ev);
    void send_text(const std::string& text);
    
    /// Clipboard
    void send_clipboard_text(const std::string& text);
    void send_clipboard_files(const std::vector<std::string>& paths);
    
    /// File transfer
    void send_file(const std::string& local_path, const std::string& remote_path);
    void cancel_file_transfer();
    
    /// Control
    void switch_display(uint32_t idx);
    void set_permissions(const ControlPermissions& perms);
    void request_video_frame();
    void request_privacy_mode(bool enabled);
    
    /// Audio
    void enable_audio(bool enable);
    void set_audio_config(uint32_t sample_rate, uint32_t channels);
    
    /// Chat
    void send_chat_message(const std::string& msg);
    
    // Connection phases
    enum class Phase {
        DISCONNECTED,
        RENDEZVOUS,
        PUNCH_HOLE,
        TCP_HANDSHAKE,
        LOGIN,
        CONNECTED,
    };
    Phase get_phase() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Client-side clipboard context
struct ClientClipboardContext {
    bool is_text_required = true;
    bool is_file_required = false;
    bool running = false;
    std::string last_text;
};

// Audio recording/playback
class AudioDevice {
public:
    virtual ~AudioDevice() = default;
    virtual bool open_input(const std::string& device_name) = 0;
    virtual bool open_output(const std::string& device_name) = 0;
    virtual std::vector<int16_t> read_samples(size_t count) = 0;
    virtual bool write_samples(const std::vector<int16_t>& samples) = 0;
    virtual void close() = 0;
    virtual std::vector<std::string> list_devices() = 0;
};

// Session / connection tracking
struct SessionInfo {
    std::string peer_id;
    std::string peer_name;
    std::string connection_type; // "TCP", "UDP", "Relay"
    bool encrypted = false;
    bool view_only = false;
    Resolution peer_resolution;
    std::chrono::system_clock::time_point connected_at;
};

// Global session manager
class SessionManager {
public:
    static SessionManager& instance();
    
    void start_session(const std::string& peer_id);
    void end_session();
    bool has_active_session() const;
    SessionInfo get_current_session() const;
    void set_view_only(bool vo);
    void set_encrypted(bool enc);

private:
    SessionManager() = default;
    std::optional<SessionInfo> current_;
    mutable std::mutex mutex_;
};

// Login constants
inline constexpr const char* LOGIN_MSG_PASSWORD_EMPTY = "Empty Password";
inline constexpr const char* LOGIN_MSG_PASSWORD_WRONG = "Wrong Password";
inline constexpr const char* LOGIN_MSG_2FA_WRONG = "Wrong 2FA Code";
inline constexpr const char* LOGIN_MSG_OFFLINE = "Offline";
inline constexpr const char* LOGIN_MSG_NO_PASSWORD_ACCESS = "No Password Access";
inline constexpr const char* REQUIRE_2FA = "2FA Required";
inline constexpr const char* LOGIN_MSG_DESKTOP_NOT_INITED = "Desktop env is not inited";
inline constexpr const char* LOGIN_MSG_DESKTOP_SESSION_NOT_READY = "Desktop session not ready";
inline constexpr const char* LOGIN_SCREEN_WAYLAND = "Wayland login screen is not supported";

// Video constants
inline constexpr size_t VIDEO_QUEUE_SIZE = 120;
inline constexpr auto SEC30 = std::chrono::seconds(30);
inline constexpr auto MILLI1 = std::chrono::milliseconds(1);

// File transfer
enum class JobType {
    SEND_FILE,
    RECV_FILE,
    SEND_DIR,
    RECV_DIR,
};

// File manager
class FileManager {
public:
    FileManager() = default;
    void set_job_type(JobType type) { job_type_ = type; }
    JobType get_job_type() const { return job_type_; }
    
    void send(const std::string& local, const std::string& remote);
    void cancel();
    bool is_active() const { return active_; }
    uint64_t total_size() const { return total_size_; }
    uint64_t transferred() const { return transferred_; }
    
private:
    JobType job_type_ = JobType::SEND_FILE;
    bool active_ = false;
    uint64_t total_size_ = 0;
    uint64_t transferred_ = 0;
};

// Screenshot helper
class ScreenshotHelper {
public:
    virtual ~ScreenshotHelper() = default;
    virtual std::optional<VideoFrame> capture(uint32_t display) = 0;
    virtual std::vector<uint32_t> get_display_indices() = 0;
};

// Lan discovery client
class LanDiscovery {
public:
    LanDiscovery();
    ~LanDiscovery();
    
    void start();
    void stop();
    std::vector<PeerConfig> discover();
    
    using OnPeerFound = std::function<void(const PeerConfig&)>;
    void set_on_peer_found(OnPeerFound cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cppdesk::client
