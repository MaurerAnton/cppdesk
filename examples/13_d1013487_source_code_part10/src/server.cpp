// server.cpp — see server.hpp (part 1: registry + reaper + wiring).
#include <rustdesk/server.hpp>

#include <rustdesk/audio_service.hpp>
#include <rustdesk/clipboard_service.hpp>
#include <rustdesk/input_service.hpp>
#include <rustdesk/video_service.hpp>

#include <chrono>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/wait.h>
#endif

namespace rustdesk {

std::mutex& child_lock() {
    static std::mutex m;
    return m;
}

std::vector<intptr_t>& child_processes() {
    static std::vector<intptr_t> pids;
    return pids;
}

void track_child_pid(intptr_t pid) {
    std::unique_lock l(child_lock());
    child_processes().push_back(pid);
}

void check_zombie() {
    // Reaper parity: sweep every 100ms, drop reaped children.
    std::thread([] {
        while (true) {
            {
                std::unique_lock l(child_lock());
                auto& pids = child_processes();
                size_t i = 0;
                while (i < pids.size()) {
#if defined(__unix__) || defined(__APPLE__)
                    // try_wait() parity: remove only positively-reaped pids.
                    const pid_t ret =
                        ::waitpid(static_cast<pid_t>(pids[i]), nullptr, WNOHANG);
                    if (ret > 0) {
                        pids.erase(pids.begin() + static_cast<ptrdiff_t>(i));
                    } else {
                        ++i;
                    }
#else
                    // Windows reaper deferred to the platform step.
                    ++i;
#endif
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }).detach();
}

std::shared_ptr<Server> Server::create() {
    auto server = std::shared_ptr<Server>(new Server());
    server->add_service(std::make_shared<GenericService>(audio_service::create()));
    server->add_service(std::make_shared<GenericService>(video_service::create()));
    server->add_service(std::make_shared<GenericService>(clipboard_service::create()));
    server->add_service(std::make_shared<input_service::MouseCursorService>(
        input_service::new_cursor()));
    server->add_service(std::make_shared<GenericService>(input_service::new_pos()));
    return server;
}

void Server::add_service(std::shared_ptr<Service> service) {
    std::unique_lock l(mutex_);
    const std::string name = service->name();
    services_.emplace(name, std::move(service));
}

void Server::add_connection(const ConnInner& conn, const std::vector<std::string>& noperms) {
    std::unique_lock l(mutex_);
    for (const auto& [name, service] : services_) {
        bool skip = false;
        for (const auto& np : noperms) {
            if (np == name) {
                skip = true;
                break;
            }
        }
        if (!skip) {
            service->on_subscribe(conn);
        }
    }
    connections_.emplace(conn.get_id(), conn);
}

void Server::remove_connection(const ConnInner& conn) {
    std::unique_lock l(mutex_);
    for (const auto& [name, service] : services_) {
        (void)name;
        service->on_unsubscribe(conn.get_id());
    }
    connections_.erase(conn.get_id());
}

void Server::subscribe(const std::string& name, const ConnInner& conn, bool sub) {
    std::unique_lock l(mutex_);
    const auto it = services_.find(name);
    if (it == services_.end()) {
        return;
    }
    const std::shared_ptr<Service>& service = it->second;
    if (service->is_subed(conn.get_id()) == sub) {
        return;
    }
    if (sub) {
        service->on_subscribe(conn);
    } else {
        service->on_unsubscribe(conn.get_id());
    }
}

int32_t Server::next_id() {
    std::unique_lock l(mutex_);
    id_count_ += 1;
    return id_count_;
}

std::shared_ptr<Service> Server::find_service(const std::string& name) const {
    std::shared_lock l(mutex_);
    const auto it = services_.find(name);
    return it != services_.end() ? it->second : nullptr;
}

bool Server::has_connection(int32_t id) const {
    std::shared_lock l(mutex_);
    return connections_.count(id) > 0;
}

Server::~Server() {
    // Drop parity: join every service worker. (Services map destruction alone
    // would abandon threads; join first while state is reachable.)
    std::unique_lock l(mutex_);
    for (const auto& [name, service] : services_) {
        (void)name;
        service->join();
    }
}

}  // namespace rustdesk
