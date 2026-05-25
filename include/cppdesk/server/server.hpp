#pragma once

#include "common/protocol.hpp"
#include "common/config.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <map>
#include <unordered_map>
#include <shared_mutex>

namespace cppdesk::server {

using namespace common;
using common::StreamPtr;

// Service interface — pluggable services (video, audio, clipboard, input, etc.)
class Service {
public:
    virtual ~Service() = default;
    virtual std::string name() const = 0;
    virtual void on_subscribe(int32_t conn_id) = 0;
    virtual void on_unsubscribe(int32_t conn_id) = 0;
    virtual bool is_subscribed(int32_t conn_id) const = 0;
    virtual void set_option(const std::string& key, const std::string& value) {}
    virtual void start() {}
    virtual void stop() {}
    virtual size_t subscriber_count() const = 0;
};

// Connection inner type
class ConnInner {
public:
    ConnInner(int32_t id, StreamPtr stream, const std::string& addr);
    ~ConnInner();
    
    int32_t id() const { return id_; }
    std::string addr() const { return addr_; }
    StreamPtr stream() { return stream_; }
    void send(const std::vector<uint8_t>& data);
    void close();
    bool is_open() const;
    void set_control_permissions(const ControlPermissions& perms);
    ControlPermissions get_control_permissions() const;

private:
    int32_t id_;
    StreamPtr stream_;
    std::string addr_;
    ControlPermissions perms_;
};

using ConnPtr = std::shared_ptr<ConnInner>;

// Generic service template
class GenericService : public Service {
public:
    explicit GenericService(std::string name);
    std::string name() const override;
    void on_subscribe(int32_t conn_id) override;
    void on_unsubscribe(int32_t conn_id) override;
    bool is_subscribed(int32_t conn_id) const override;
    size_t subscriber_count() const override;

protected:
    std::string name_;
    std::unordered_set<int32_t> subscribers_;
    mutable std::mutex mutex_;
};

// Subscriber trait
class Subscriber {
public:
    virtual ~Subscriber() = default;
    virtual void on_data(const std::vector<uint8_t>& data) = 0;
};

// Server — manages services and connections
class Server {
public:
    Server();
    ~Server();
    
    using ServerPtr = std::shared_ptr<Server>;
    using ServerWeakPtr = std::weak_ptr<Server>;
    
    static ServerPtr create();
    
    // Connections
    int32_t add_connection(ConnPtr conn, const std::vector<std::string>& noperms = {});
    int32_t add_camera_connection(ConnPtr conn);
    void remove_connection(int32_t conn_id);
    void close_all_connections();
    ConnPtr get_connection(int32_t conn_id) const;
    size_t connection_count() const;
    
    // Services
    void add_service(std::unique_ptr<Service> service);
    void remove_service(const std::string& name);
    bool has_service(const std::string& name) const;
    Service* get_service(const std::string& name);
    
    void subscribe(const std::string& service_name, ConnPtr conn, bool sub);
    void add_primary_video_service();
    void add_primary_camera_service();
    
    // Service options
    void set_video_service_option(int display_idx, const std::string& key,
        const std::string& value);
    
    // ID generation
    int32_t next_connection_id();
    
private:
    std::map<int32_t, ConnPtr> connections_;
    std::map<std::string, std::unique_ptr<Service>> services_;
    int32_t id_counter_ = 1000;
    mutable std::shared_mutex mutex_;
};

using ServerPtr = Server::ServerPtr;

// ===== Services =====

// Display service
class DisplayService : public GenericService {
public:
    DisplayService();
    static constexpr const char* NAME = "display";
    static int32_t primary_display_idx();
};

// Video source types
enum class VideoSource {
    MONITOR,
    CAMERA,
    VIRTUAL,
};

// Video service
class VideoService : public GenericService {
public:
    VideoService(VideoSource source, int32_t display_idx);
    static std::string service_name(VideoSource source, int32_t idx);
    
    void start() override;
    void stop() override;
    void set_option(const std::string& key, const std::string& value) override;
    
    static constexpr const char* MONITOR_PREFIX = "video_monitor_";
    static constexpr const char* CAMERA_PREFIX = "video_camera_";
    
private:
    VideoSource source_;
    int32_t display_idx_;
    std::atomic<bool> running_{false};
};

// Audio service
class AudioService : public GenericService {
public:
    AudioService();
    static constexpr const char* NAME = "audio";
    
    void start() override;
    void stop() override;
};

// Input service (cursor, keyboard)
class InputService : public GenericService {
public:
    InputService(const std::string& name_suffix);
    
    static std::unique_ptr<InputService> create_cursor();
    static std::unique_ptr<InputService> create_position();
    static std::unique_ptr<InputService> create_window_focus();
    
    static constexpr const char* NAME_CURSOR = "cursor";
    static constexpr const char* NAME_POS = "cursor_pos";
    static constexpr const char* NAME_WINDOW_FOCUS = "window_focus";
    
    void start() override;
    void stop() override;
};

// Clipboard service
class ClipboardService : public GenericService {
public:
    explicit ClipboardService(const std::string& name_suffix);
    
    static constexpr const char* NAME = "clipboard";
    static constexpr const char* FILE_NAME = "clipboard_file";
    
    void start() override;
    void stop() override;
    bool is_running() const;
};

// Terminal service
class TerminalService : public GenericService {
public:
    TerminalService();
    static constexpr const char* NAME = "terminal";
    
    void start() override;
    void stop() override;
};

// Connection management
class Connection {
public:
    Connection(int32_t id, StreamPtr stream, const std::string& addr,
        ServerWeakPtr server, std::optional<ControlPermissions> perms);
    ~Connection();
    
    void start();
    void stop();
    bool is_running() const;
    
private:
    int32_t id_;
    StreamPtr stream_;
    std::string addr_;
    ServerWeakPtr server_;
    std::optional<ControlPermissions> perms_;
    std::atomic<bool> running_{false};
};

// Login failure check
class LoginFailureCheck {
public:
    LoginFailureCheck();
    void record_attempt(const std::string& addr);
    bool is_blocked(const std::string& addr) const;
    void reset(const std::string& addr);

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::pair<int, std::chrono::steady_clock::time_point>> failures_;
};

// RDP input (Linux xdg-desktop-portal)
struct RdpInput {
    bool enabled = false;
    void start() {}
    void stop() {}
};

// Virtual display manager (Windows)
class VirtualDisplayManager {
public:
    VirtualDisplayManager();
    bool install();
    bool uninstall();
    bool is_installed() const;
    std::vector<std::string> get_virtual_displays() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Printer service (Windows)
class PrinterService : public GenericService {
public:
    PrinterService(const std::string& name);
    static constexpr const char* NAME = "printer";
    static bool init(const std::string& driver_name);
    
    void start() override;
    void stop() override;
};

// Child process tracker
class ChildProcessTracker {
public:
    ChildProcessTracker();
    void add(int pid);
    void remove(int pid);
    void kill_all();
    
private:
    std::mutex mutex_;
    std::vector<int> pids_;
};

} // namespace cppdesk::server
