// IPC module - inter-process communication for cppdesk
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace cppdesk::ipc {

enum class IpcMessageType : uint32_t {
    CONNECT,
    DISCONNECT,
    DATA,
    PING,
    PONG,
    ERROR,
    SHUTDOWN,
    CONFIG_UPDATE,
    PEER_UPDATE,
    FILE_TRANSFER,
    AUDIO_DATA,
    VIDEO_DATA,
    CLIPBOARD_DATA,
    INPUT_EVENT,
    SERVICE_COMMAND,
};

struct IpcMessage {
    IpcMessageType type = IpcMessageType::DATA;
    uint32_t sequence = 0;
    int32_t sender_pid = 0;
    std::vector<uint8_t> payload;
    uint64_t timestamp = 0;
};

class IpcChannel {
protected:
    std::string name_;
    std::atomic<bool> connected_{false};
    uint32_t seq_counter_ = 0;
public:
    explicit IpcChannel(std::string name) : name_(std::move(name)) {}
    virtual ~IpcChannel() = default;
    virtual bool send(const IpcMessage& msg) = 0;
    virtual std::optional<IpcMessage> recv(std::chrono::milliseconds timeout) = 0;
    virtual void close() = 0;
    bool is_connected() const { return connected_; }
    std::string name() const { return name_; }
};

class IpcServer : public IpcChannel {
    std::thread accept_thread_;
    std::vector<std::unique_ptr<IpcChannel>> clients_;
    std::mutex clients_mutex_;
    int server_fd_ = -1;
public:
    explicit IpcServer(const std::string& name) : IpcChannel(name) {}
    bool start();
    void stop();
    bool send(const IpcMessage& msg) override;
    std::optional<IpcMessage> recv(std::chrono::milliseconds timeout) override;
    void close() override;
    void broadcast(const IpcMessage& msg);
    size_t client_count() const;
};

class IpcClient : public IpcChannel {
    int client_fd_ = -1;
    std::thread recv_thread_;
    std::queue<IpcMessage> recv_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
public:
    explicit IpcClient(const std::string& name) : IpcChannel(name) {}
    bool connect(const std::string& server_path);
    bool send(const IpcMessage& msg) override;
    std::optional<IpcMessage> recv(std::chrono::milliseconds timeout) override;
    void close() override;
};

} // namespace