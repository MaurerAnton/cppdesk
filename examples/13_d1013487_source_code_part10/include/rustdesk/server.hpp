// server.hpp — translation of src/server.rs, part 1 (step 13).
//
// Portable now: CHILD_PROCESS pid reaper (check_zombie), the Server registry
// (connections + services + id counter), and service wiring (new()).
// Deferred with owners: accept_connection_/create_tcp_connection_/
// create_relay_connection_/accept_connection/create_relay_connection (need
// Connection::start + platform identity), start_server (needs ipc + mediator
// start_all + platform). Those land with the connection/ipc/platform steps.

#pragma once

#include <rustdesk/service.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace rustdesk {

// CHILD_PROCESS parity: upstream holds std::process::Child handles pushed by
// platform code; here plain pids (platform step pushes via track_child_pid).
// pid type is intptr_t (pid_t/DWORD both fit; Windows reaper deferred).
std::mutex& child_lock();
std::vector<intptr_t>& child_processes();
void track_child_pid(intptr_t pid);
// Reaper thread parity (100ms waitpid/WNOHANG sweep; detached like upstream).
void check_zombie();

class Server {
public:
    // new() parity: builds the registry with all five services.
    static std::shared_ptr<Server> create();

    void add_connection(const ConnInner& conn, const std::vector<std::string>& noperms);
    void remove_connection(const ConnInner& conn);
    void subscribe(const std::string& name, const ConnInner& conn, bool sub);
    // id_count += 1 under lock (create_tcp_connection_ parity).
    int32_t next_id();

    // Introspection for wiring/tests (no upstream counterpart; the map is
    // otherwise private like upstream's).
    std::shared_ptr<Service> find_service(const std::string& name) const;
    bool has_connection(int32_t id) const;

    // Drop parity: joins every service worker.
    ~Server();

private:
    Server() = default;
    void add_service(std::shared_ptr<Service> service);

    mutable std::shared_mutex mutex_;
    std::unordered_map<int32_t, ConnInner> connections_;
    std::unordered_map<std::string, std::shared_ptr<Service>> services_;
    int32_t id_count_ = 0;
};

// ServerPtr parity (Arc<RwLock<Server>>; locks live inside the methods, so
// callers never hold guards across calls — each op stays atomic as upstream).
using ServerPtr = std::shared_ptr<Server>;

}  // namespace rustdesk
