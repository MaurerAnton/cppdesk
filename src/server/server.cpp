#include "server/server.hpp"
#include "common/config.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <random>

namespace cppdesk::server {

// ===== GenericService =====
GenericService::GenericService(std::string name) : name_(std::move(name)) {}
std::string GenericService::name() const { return name_; }

void GenericService::on_subscribe(int32_t conn_id) {
    std::lock_guard lk(mutex_);
    subscribers_.insert(conn_id);
    spdlog::debug("Service {}: client {} subscribed (total: {})",
        name_, conn_id, subscribers_.size());
}

void GenericService::on_unsubscribe(int32_t conn_id) {
    std::lock_guard lk(mutex_);
    subscribers_.erase(conn_id);
    spdlog::debug("Service {}: client {} unsubscribed (total: {})",
        name_, conn_id, subscribers_.size());
}

bool GenericService::is_subscribed(int32_t conn_id) const {
    std::lock_guard lk(mutex_);
    return subscribers_.count(conn_id) > 0;
}

size_t GenericService::subscriber_count() const {
    std::lock_guard lk(mutex_);
    return subscribers_.size();
}

// ===== ConnInner =====
ConnInner::ConnInner(int32_t id, StreamPtr stream, const std::string& addr)
    : id_(id), stream_(std::move(stream)), addr_(addr) {}

ConnInner::~ConnInner() { close(); }

void ConnInner::send(const std::vector<uint8_t>& data) {
    if (stream_ && stream_->is_open()) {
        stream_->send(data);
    }
}

void ConnInner::close() {
    if (stream_) {
        stream_->close();
    }
}

bool ConnInner::is_open() const {
    return stream_ && stream_->is_open();
}

void ConnInner::set_control_permissions(const ControlPermissions& perms) {
    perms_ = perms;
}

ControlPermissions ConnInner::get_control_permissions() const {
    return perms_;
}

// ===== Server =====
Server::Server() {
    id_counter_ = 1000 + (std::random_device{}() % 1000);
}

Server::~Server() {
    close_all_connections();
}

ServerPtr Server::create() {
    auto srv = std::make_shared<Server>();
    
    // Add core services
    srv->add_service(std::make_unique<AudioService>());
    srv->add_service(std::make_unique<DisplayService>());
    srv->add_service(std::make_unique<ClipboardService>(ClipboardService::NAME));
    srv->add_service(std::make_unique<ClipboardService>(ClipboardService::FILE_NAME));
    
    auto display = DisplayService::primary_display_idx();
    srv->add_service(std::make_unique<VideoService>(VideoSource::MONITOR, display));
    
    srv->add_service(InputService::create_cursor());
    srv->add_service(InputService::create_position());
    srv->add_service(InputService::create_window_focus());
    
    return srv;
}

int32_t Server::add_connection(ConnPtr conn, const std::vector<std::string>& noperms) {
    std::unique_lock lk(mutex_);
    int32_t id = conn->id();
    
    // Auto-subscribe to primary services
    auto primary_display = DisplayService::primary_display_idx();
    for (auto& [name, svc] : services_) {
        if (name == VideoService::service_name(VideoSource::MONITOR, primary_display)) {
            svc->on_subscribe(id);
        } else if (name != ClipboardService::FILE_NAME) {
            if (std::find(noperms.begin(), noperms.end(), name) == noperms.end()) {
                svc->on_subscribe(id);
            }
        }
    }
    
    connections_[id] = std::move(conn);
    spdlog::info("Connection {} added (total: {})", id, connections_.size());
    return id;
}

int32_t Server::add_camera_connection(ConnPtr conn) {
    std::unique_lock lk(mutex_);
    int32_t id = conn->id();
    
    auto camera_name = VideoService::service_name(VideoSource::CAMERA, 0);
    auto it = services_.find(camera_name);
    if (it != services_.end()) {
        it->second->on_subscribe(id);
    }
    
    connections_[id] = std::move(conn);
    return id;
}

void Server::remove_connection(int32_t conn_id) {
    std::unique_lock lk(mutex_);
    for (auto& [_, svc] : services_) {
        svc->on_unsubscribe(conn_id);
    }
    connections_.erase(conn_id);
    spdlog::info("Connection {} removed (total: {})", conn_id, connections_.size());
}

void Server::close_all_connections() {
    std::unique_lock lk(mutex_);
    for (auto& [id, conn] : connections_) {
        conn->close();
    }
    connections_.clear();
}

ConnPtr Server::get_connection(int32_t conn_id) const {
    std::shared_lock lk(mutex_);
    auto it = connections_.find(conn_id);
    return it != connections_.end() ? it->second : nullptr;
}

size_t Server::connection_count() const {
    std::shared_lock lk(mutex_);
    return connections_.size();
}

void Server::add_service(std::unique_ptr<Service> service) {
    std::unique_lock lk(mutex_);
    services_[service->name()] = std::move(service);
}

void Server::remove_service(const std::string& name) {
    std::unique_lock lk(mutex_);
    services_.erase(name);
}

bool Server::has_service(const std::string& name) const {
    std::shared_lock lk(mutex_);
    return services_.count(name) > 0;
}

Service* Server::get_service(const std::string& name) {
    std::shared_lock lk(mutex_);
    auto it = services_.find(name);
    return it != services_.end() ? it->second.get() : nullptr;
}

void Server::subscribe(const std::string& service_name, ConnPtr conn, bool sub) {
    std::unique_lock lk(mutex_);
    auto it = services_.find(service_name);
    if (it == services_.end()) return;
    
    if (sub) {
        if (!it->second->is_subscribed(conn->id())) {
            it->second->on_subscribe(conn->id());
        }
    } else {
        it->second->on_unsubscribe(conn->id());
    }
}

void Server::add_primary_video_service() {
    add_service(std::make_unique<VideoService>(VideoSource::MONITOR,
        DisplayService::primary_display_idx()));
}

void Server::add_primary_camera_service() {
    add_service(std::make_unique<VideoService>(VideoSource::CAMERA, 0));
}

void Server::set_video_service_option(int display_idx, const std::string& key,
    const std::string& value) {
    std::shared_lock lk(mutex_);
    auto name = VideoService::service_name(VideoSource::MONITOR, display_idx);
    auto it = services_.find(name);
    if (it != services_.end()) {
        it->second->set_option(key, value);
    }
}

int32_t Server::next_connection_id() {
    std::unique_lock lk(mutex_);
    return ++id_counter_;
}

// ===== DisplayService =====
DisplayService::DisplayService() : GenericService(NAME) {}

int32_t DisplayService::primary_display_idx() {
    return 0;
}

// ===== VideoService =====
VideoService::VideoService(VideoSource source, int32_t display_idx)
    : GenericService(service_name(source, display_idx)),
      source_(source), display_idx_(display_idx) {}

std::string VideoService::service_name(VideoSource source, int32_t idx) {
    return (source == VideoSource::CAMERA ? "video_camera_" : "video_monitor_")
        + std::to_string(idx);
}

void VideoService::start() {
    running_ = true;
    spdlog::info("Video service {} started", name_);
}

void VideoService::stop() {
    running_ = false;
    spdlog::info("Video service {} stopped", name_);
}

void VideoService::set_option(const std::string& key, const std::string& value) {
    spdlog::debug("Video {} option {} = {}", name_, key, value);
}

// ===== AudioService =====
AudioService::AudioService() : GenericService(NAME) {}

void AudioService::start() {
    spdlog::info("Audio service started");
}

void AudioService::stop() {
    spdlog::info("Audio service stopped");
}

// ===== InputService =====
InputService::InputService(const std::string& name_suffix)
    : GenericService(name_suffix) {}

std::unique_ptr<InputService> InputService::create_cursor() {
    return std::make_unique<InputService>(NAME_CURSOR);
}

std::unique_ptr<InputService> InputService::create_position() {
    return std::make_unique<InputService>(NAME_POS);
}

std::unique_ptr<InputService> InputService::create_window_focus() {
    return std::make_unique<InputService>(NAME_WINDOW_FOCUS);
}

void InputService::start() {}
void InputService::stop() {}

// ===== ClipboardService =====
ClipboardService::ClipboardService(const std::string& name_suffix)
    : GenericService(name_suffix) {}

void ClipboardService::start() {}
void ClipboardService::stop() {}
bool ClipboardService::is_running() const { return subscriber_count() > 0; }

// ===== TerminalService =====
TerminalService::TerminalService() : GenericService(NAME) {}
void TerminalService::start() {}
void TerminalService::stop() {}

// ===== Connection =====
Connection::Connection(int32_t id, StreamPtr stream, const std::string& addr,
    ServerWeakPtr server, std::optional<ControlPermissions> perms)
    : id_(id), stream_(std::move(stream)), addr_(addr),
      server_(std::move(server)), perms_(perms) {}

Connection::~Connection() { stop(); }

void Connection::start() {
    running_ = true;
    spdlog::info("Connection {} from {} started", id_, addr_);
    
    // Message loop runs in background thread
    std::thread([this]() {
        while (running_ && stream_ && stream_->is_open()) {
            auto data = stream_->recv();
            if (data.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            // Process message...
        }
        
        // Cleanup
        if (auto srv = server_.lock()) {
            srv->remove_connection(id_);
        }
    }).detach();
}

void Connection::stop() {
    running_ = false;
    if (stream_) stream_->close();
}

bool Connection::is_running() const { return running_; }

// ===== LoginFailureCheck =====
LoginFailureCheck::LoginFailureCheck() = default;

void LoginFailureCheck::record_attempt(const std::string& addr) {
    std::lock_guard lk(mutex_);
    auto& [count, since] = failures_[addr];
    if (std::chrono::steady_clock::now() - since > std::chrono::minutes(5)) {
        count = 0;
        since = std::chrono::steady_clock::now();
    }
    count++;
}

bool LoginFailureCheck::is_blocked(const std::string& addr) const {
    std::lock_guard lk(mutex_);
    auto it = failures_.find(addr);
    if (it == failures_.end()) return false;
    auto& [count, since] = it->second;
    if (std::chrono::steady_clock::now() - since > std::chrono::minutes(5)) {
        return false;
    }
    return count >= 5;
}

void LoginFailureCheck::reset(const std::string& addr) {
    std::lock_guard lk(mutex_);
    failures_.erase(addr);
}

// ===== ChildProcessTracker =====
ChildProcessTracker::ChildProcessTracker() = default;

void ChildProcessTracker::add(int pid) {
    std::lock_guard lk(mutex_);
    pids_.push_back(pid);
}

void ChildProcessTracker::remove(int pid) {
    std::lock_guard lk(mutex_);
    pids_.erase(std::remove(pids_.begin(), pids_.end(), pid), pids_.end());
}

void ChildProcessTracker::kill_all() {
    std::lock_guard lk(mutex_);
    for (int pid : pids_) {
        kill(pid, SIGTERM);
    }
    pids_.clear();
}

// ===== PrinterService =====
PrinterService::PrinterService(const std::string& name) : GenericService(name) {}
bool PrinterService::init(const std::string&) { return true; }
void PrinterService::start() {}
void PrinterService::stop() {}

// ===== VirtualDisplayManager =====
VirtualDisplayManager::VirtualDisplayManager() {}
bool VirtualDisplayManager::install() { return false; }
bool VirtualDisplayManager::uninstall() { return false; }
bool VirtualDisplayManager::is_installed() const { return false; }
std::vector<std::string> VirtualDisplayManager::get_virtual_displays() const { return {}; }

} // namespace cppdesk::server
