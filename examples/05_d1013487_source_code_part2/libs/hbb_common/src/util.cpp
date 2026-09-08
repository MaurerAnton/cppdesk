// util.cpp — see util.hpp.
#include <hbb_common/util.hpp>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>
#include <system_error>

namespace hbb_common {
namespace {

// `str::parse::<i32>` parity: optional sign + ASCII digits + range check.
bool parse_i32(const std::string& s) {
    if (s.empty()) {
        return false;
    }
    size_t i = 0;
    bool neg = false;
    if (s[i] == '+' || s[i] == '-') {
        neg = s[i] == '-';
        if (++i == s.size()) {
            return false;
        }
    }
    int64_t v = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') {
            return false;
        }
        v = v * 10 + (c - '0');
        if ((!neg && v > INT32_MAX) || (neg && -v < INT32_MIN)) {
            return false;
        }
    }
    return true;
}

// Closes the fd unless released (throw-safe `Socket` drop parity).
class FdGuard {
public:
    explicit FdGuard(int fd) : fd_(fd) {}
    ~FdGuard() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;
    int release() {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

private:
    int fd_;
};

}  // namespace

ResolvedAddr to_socket_addr(const std::string& host) {
    // `str::to_socket_addrs` parity: "host:port" split at the last ':'.
    const size_t colon = host.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= host.size()) {
        throw std::runtime_error("Failed to solve " + host);
    }
    const std::string node = host.substr(0, colon);
    const std::string service = host.substr(colon + 1);

    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* list = nullptr;
    if (::getaddrinfo(node.c_str(), service.c_str(), &hints, &list) != 0 || !list) {
        if (list) {
            ::freeaddrinfo(list);
        }
        throw std::runtime_error("Failed to solve " + host);
    }
    ResolvedAddr out{};
    out.family = list->ai_family;
    char ip[INET6_ADDRSTRLEN] = {};
    uint16_t port = 0;
    if (list->ai_family == AF_INET) {
        const auto* sa = reinterpret_cast<const struct sockaddr_in*>(list->ai_addr);
        ::inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof ip);
        port = ntohs(sa->sin_port);
    } else if (list->ai_family == AF_INET6) {
        const auto* sa = reinterpret_cast<const struct sockaddr_in6*>(list->ai_addr);
        ::inet_ntop(AF_INET6, &sa->sin6_addr, ip, sizeof ip);
        port = ntohs(sa->sin6_port);
    } else {
        ::freeaddrinfo(list);
        throw std::runtime_error("Failed to solve " + host);
    }
    ::freeaddrinfo(list);
    out.ip = ip;
    out.port = port;
    return out;  // first result, like upstream's `addrs[0]`
}

int new_socket(const ResolvedAddr& addr, bool tcp, bool reuse) {
    const int fd = ::socket(addr.family, tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "socket");
    }
    FdGuard guard(fd);
    if (reuse) {
        // Windows has no SO_REUSEPORT; there SO_REUSEADDR almost equals unix
        // REUSEPORT+REUSEADDR (upstream comment in new_socket, kept verbatim).
#ifdef __linux__
        const int one = 1;
        if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one) != 0) {
            throw std::system_error(errno, std::generic_category(), "SO_REUSEPORT");
        }
#endif
        if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) != 0) {
            throw std::system_error(errno, std::generic_category(), "SO_REUSEADDR");
        }
    }
    if (addr.family == AF_INET) {
        struct sockaddr_in sa {};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(addr.port);
        if (::inet_pton(AF_INET, addr.ip.c_str(), &sa.sin_addr) != 1) {
            throw std::runtime_error("new_socket: bad IPv4 address");
        }
        if (::bind(fd, reinterpret_cast<struct sockaddr*>(&sa), sizeof sa) != 0) {
            throw std::system_error(errno, std::generic_category(), "bind");
        }
    } else {
        struct sockaddr_in6 sa {};
        sa.sin6_family = AF_INET6;
        sa.sin6_port = htons(addr.port);
        if (::inet_pton(AF_INET6, addr.ip.c_str(), &sa.sin6_addr) != 1) {
            throw std::runtime_error("new_socket: bad IPv6 address");
        }
        if (::bind(fd, reinterpret_cast<struct sockaddr*>(&sa), sizeof sa) != 0) {
            throw std::system_error(errno, std::generic_category(), "bind");
        }
    }
    return guard.release();
}

std::string get_version_from_url(const std::string& url) {
    const size_t n = url.size();
    // Last '-' / '.' scanning from the end. `a`/`b` are distances from the
    // end (`rev().enumerate()` parity), so start indexes are n-1-a / n-1-b.
    size_t a_pos = std::string::npos;
    for (size_t i = n; i-- > 0;) {
        if (url[i] == '-') {
            a_pos = i;
            break;
        }
    }
    if (a_pos == std::string::npos) {
        return "";
    }
    size_t b_pos = std::string::npos;
    for (size_t i = n; i-- > 0;) {
        if (url[i] == '.') {
            b_pos = i;
            break;
        }
    }
    if (b_pos == std::string::npos) {
        return "";
    }
    const size_t a = (n - 1) - a_pos;
    const size_t b = (n - 1) - b_pos;
    if (a > b) {
        // `skip(n-b)`: extension after the last '.'; `skip(n-a)`: past '-'.
        if (parse_i32(url.substr(n - b))) {
            return url.substr(n - a);
        }
        return url.substr(n - a, a - b - 1);  // between '-' and '.', `take(a-b-1)` parity
    }
    return url.substr(n - a);
}

}  // namespace hbb_common
