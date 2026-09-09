// common.cpp — see common.hpp.
#include <rustdesk/common.hpp>

#include <cppdesk/version.hpp>

#include <hbb_common/tcp.hpp>
#include <hbb_common/udp.hpp>

#include <rendezvous.pb.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <cerrno>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace rustdesk {
namespace {

using hbb_common::ResolvedAddr;

// Split "a.b.c" on '.'; unparsable parts count 0 (parse::<i64>().unwrap_or(0)).
int64_t parse_version_part(const std::string& part) {
    try {
        size_t pos = 0;
        const long long v = std::stoll(part, &pos);
        if (pos != part.size() || v < 0) {
            return 0;
        }
        return static_cast<int64_t>(v);
    } catch (...) {
        return 0;
    }
}

// test_nat_type_() parity: two rendezvous connections, compare observed ports.
bool test_nat_type_impl() {
    using namespace hbb_common;
    const ResolvedAddr rendezvous = get_rendezvous_server(0);
    ResolvedAddr server1 = rendezvous;
    ResolvedAddr server2 = rendezvous;
    server2.port = static_cast<uint16_t>(server1.port - 1);  // set_port(port-1) parity

    hbb::RendezvousMessage msg_out;
    msg_out.mutable_test_nat_request()->set_serial(get_serial());

    int32_t port1 = 0;
    int32_t port2 = 0;
    ResolvedAddr addr = get_any_listen_addr();
    for (int i = 0; i < 2; ++i) {
        auto stream = TcpFramedStream::connect(i == 0 ? server1 : server2, addr,
                                               kRendezvousTimeoutMs);
        if (!stream) {
            return false;  // `await?` parity: any error retries via the spawner
        }
        if (const auto local = stream->local_addr()) {
            addr = *local;  // re-bind the same local port (upstream parity)
        }
        if (!stream->send(msg_out)) {
            return false;
        }
        const auto reply = stream->next_timeout(3000);
        if (!reply) {
            break;
        }
        hbb::RendezvousMessage msg_in;
        if (!msg_in.ParseFromArray(reply->data(), static_cast<int>(reply->size()))) {
            break;
        }
        if (msg_in.union_case() != hbb::RendezvousMessage::kTestNatResponse) {
            break;
        }
        const hbb::TestNatResponse& tnr = msg_in.test_nat_response();
        (i == 0 ? port1 : port2) = tnr.port();
        if (tnr.has_cu()) {
            const hbb::ConfigUpdate& cu = tnr.cu();
            std::string servers;
            for (int k = 0; k < cu.rendezvous_servers_size(); ++k) {
                if (k > 0) {
                    servers += ',';
                }
                servers += cu.rendezvous_servers(k);
            }
            set_option("rendezvous-servers", servers);
            set_serial(cu.serial());
        }
    }
    const bool ok = port1 > 0 && port2 > 0;
    if (ok) {
        // Same observed port twice → cone NAT (ASYMMETRIC in upstream terms).
        set_nat_type(port1 == port2 ? hbb::ASYMMETRIC : hbb::SYMMETRIC);
    }
    return ok;
}

// test_rendezvous_server_() parity: one thread per host, join all.
void test_rendezvous_server_impl() {
    using namespace hbb_common;
    std::vector<std::thread> workers;
    for (const std::string& host : get_rendezvous_servers()) {
        workers.emplace_back([host] {
            const auto start = std::chrono::steady_clock::now();
            bool ok = false;
            try {
                const ResolvedAddr remote = to_socket_addr(check_port(host, kRendezvousPort));
                ok = TcpFramedStream::connect(remote, get_any_listen_addr(),
                                              kRendezvousTimeoutMs)
                         .has_value();
            } catch (...) {
                ok = false;
            }
            if (ok) {
                const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now() - start)
                                    .count();
                update_latency(host, static_cast<int64_t>(us));
            } else {
                update_latency(host, -1);
            }
        });
    }
    for (auto& t : workers) {
        t.join();  // join_all parity
    }
}

void check_software_update_impl() {
    using namespace hbb_common;
    std::this_thread::sleep_for(std::chrono::seconds(3));  // sleep(3.) parity
    try {
        const ResolvedAddr rendezvous = get_rendezvous_server(1000);
        auto socket = UdpFramedSocket::bind(get_any_listen_addr());
        if (!socket) {
            return;  // `?` parity
        }
        hbb::RendezvousMessage msg_out;
        msg_out.mutable_software_update()->set_url(cppdesk::kVersion);
        if (!socket->send(msg_out, rendezvous)) {
            return;
        }
        const auto reply = socket->next_timeout(30'000);
        if (!reply) {
            return;
        }
        hbb::RendezvousMessage msg_in;
        if (!msg_in.ParseFromArray(reply->first.data(), static_cast<int>(reply->first.size()))) {
            return;
        }
        if (msg_in.union_case() != hbb::RendezvousMessage::kSoftwareUpdate) {
            return;
        }
        const std::string version = get_version_from_url(msg_in.software_update().url());
        if (get_version_number(version) > get_version_number(cppdesk::kVersion)) {
            std::unique_lock l(update_url_lock());
            software_update_url() = msg_in.software_update().url();
        }
    } catch (...) {
        // allow_err! parity
    }
}

}  // namespace

std::mutex& content_lock() {
    static std::mutex m;
    return m;
}

std::string& shared_content() {
    static std::string s;
    return s;
}

std::mutex& update_url_lock() {
    static std::mutex m;
    return m;
}

std::string& software_update_url() {
    static std::string s;
    return s;
}

bool valid_for_numlock(const hbb::KeyEvent& evt) {
    if (evt.union_case() == hbb::KeyEvent::kControlKey) {
        const int v = static_cast<int>(evt.control_key());
        return (v >= hbb::Numpad0 && v <= hbb::Numpad9) || v == hbb::Decimal;
    }
    return false;
}

bool valid_for_capslock(const hbb::KeyEvent& evt) {
    if (evt.union_case() == hbb::KeyEvent::kChr) {
        const uint32_t ch = evt.chr();
        return ch >= 'a' && ch <= 'z';
    }
    return false;
}

bool is_control_key(const hbb::KeyEvent& evt, hbb::ControlKey key) {
    return evt.union_case() == hbb::KeyEvent::kControlKey && evt.control_key() == key;
}

bool is_modifier(const hbb::KeyEvent& evt) {
    if (evt.union_case() != hbb::KeyEvent::kControlKey) {
        return false;
    }
    const auto v = evt.control_key();
    return v == hbb::Alt || v == hbb::Shift || v == hbb::Control || v == hbb::Meta;
}

int64_t get_time() {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    return static_cast<int64_t>(ms);
}

std::string check_port(const std::string& host, int32_t port) {
    if (host.find(':') == std::string::npos) {
        return host + ":" + std::to_string(port);
    }
    return host;
}

std::string test_if_valid_server(std::string host) {
    if (host.find(':') == std::string::npos) {
        host += ":0";
    }
    try {
        hbb_common::to_socket_addr(host);
        return "";
    } catch (const std::exception& e) {
        return e.what();
    }
}

int64_t get_version_number(const std::string& v) {
    int64_t n = 0;
    size_t start = 0;
    while (true) {
        const size_t pos = v.find('.', start);
        const std::string part =
            (pos == std::string::npos) ? v.substr(start) : v.substr(start, pos - start);
        n = n * 1000 + parse_version_part(part);
        if (pos == std::string::npos) {
            break;
        }
        start = pos + 1;
    }
    return n;
}

std::string username() {
#if defined(__unix__) || defined(__APPLE__)
    char buf[256] = {};
    if (::getlogin_r(buf, sizeof buf) == 0 && buf[0] != '\0') {
        // trim_end_matches('\0') parity (getlogin_r NUL-pads the buffer).
        return std::string(buf, strnlen(buf, sizeof buf));
    }
#endif
    if (const char* env = std::getenv("USER")) {
        return env;
    }
    if (const char* env = std::getenv("USERNAME")) {
        return env;
    }
    return "";
}

void run_me(const std::vector<std::string>& args) {
    // current_exe() parity via /proc/self/exe (Linux; platform step refines).
    char exe[4096] = {};
    const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) {
        throw std::system_error(errno, std::generic_category(), "readlink /proc/self/exe");
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        throw std::system_error(errno, std::generic_category(), "fork");
    }
    if (pid == 0) {
        // Child: detach and exec (double-fork so the caller never reaps).
        if (::fork() != 0) {
            _exit(0);
        }
        ::setsid();
        std::vector<char*> argv;
        argv.push_back(exe);
        std::vector<std::string> owned = args;
        for (auto& a : owned) {
            argv.push_back(a.data());
        }
        argv.push_back(nullptr);
        ::execv(exe, argv.data());
        _exit(127);
    }
    int status = 0;  // reap the intermediate child only
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
}

void test_nat_type() {
    std::thread([] {
        while (true) {
            if (test_nat_type_impl()) {
                break;  // Ok(true) parity
            }
            // Err(_) parity: silent (log::error needs the logging step).
            if (hbb_common::get_nat_type() != 0) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(12));
        }
    }).detach();
}

void test_rendezvous_server() {
    std::thread(test_rendezvous_server_impl).detach();
}

void check_software_update() {
    std::thread(check_software_update_impl).detach();
}

hbb_common::ResolvedAddr get_rendezvous_server(uint64_t /*timeout_ms*/) {
    return hbb_common::get_rendezvous_server();  // IPC override deferred to ipc step
}

int32_t get_nat_type(uint64_t /*timeout_ms*/) {
    return hbb_common::get_nat_type();  // IPC override deferred to ipc step
}

}  // namespace rustdesk
