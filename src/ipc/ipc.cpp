#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <spdlog/spdlog.h>

namespace cppdesk::ipc {

struct Data {
    enum class Type { TEXT, FILE_LIST, COMMAND, DISPLAY_INFO, SERVICE_DATA };
    Type type = Type::TEXT;
    std::vector<uint8_t> payload;
    std::string sender;
    int32_t conn_id = 0;
};

class IpcServer {
public:
    IpcServer() = default;
    void start(const std::string& socket_path) {
        spdlog::info("IPC server starting on: {}", socket_path);
    }
    void stop() {}
    void send(int32_t conn_id, const Data& data) {}
    std::vector<Data> receive(int32_t conn_id) { return {}; }
};

class IpcClient {
public:
    IpcClient() = default;
    bool connect(const std::string& socket_path) {
        spdlog::info("IPC client connecting to: {}", socket_path);
        return true;
    }
    void disconnect() {}
    void send(const Data& data) {}
    Data receive() { return {}; }
    bool is_connected() const { return false; }
};

static IpcServer g_ipc_server;
static IpcClient g_ipc_client;

void init_ipc_server() { g_ipc_server.start("/tmp/cppdesk-ipc"); }
void init_ipc_client() { g_ipc_client.connect("/tmp/cppdesk-ipc"); }

} // namespace cppdesk::ipc
