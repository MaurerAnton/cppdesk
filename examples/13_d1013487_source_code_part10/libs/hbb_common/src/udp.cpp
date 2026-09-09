// udp.cpp — implementation of udp.hpp.
#include <hbb_common/udp.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace hbb_common {
namespace {

void sock_set_timeout(int fd, int timeout_ms) {
    if (timeout_ms < 0) timeout_ms = 0;
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

}  // namespace

UdpFramedSocket::UdpFramedSocket(UdpFramedSocket&& other) noexcept
    : fd_(other.fd_), codec_(std::move(other.codec_)) {
    other.fd_ = -1;
}

UdpFramedSocket& UdpFramedSocket::operator=(UdpFramedSocket&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = other.fd_;
        codec_ = std::move(other.codec_);
        other.fd_ = -1;
    }
    return *this;
}

UdpFramedSocket::~UdpFramedSocket() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

std::optional<UdpFramedSocket> UdpFramedSocket::bind(const ResolvedAddr& addr) {
    try {
        int fd = new_socket(addr, false, false);
        return UdpFramedSocket(fd);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<UdpFramedSocket> UdpFramedSocket::bind_reuse(const ResolvedAddr& addr) {
    try {
        int fd = new_socket(addr, false, true);
        return UdpFramedSocket(fd);
    } catch (...) {
        return std::nullopt;
    }
}

bool UdpFramedSocket::send_all(const uint8_t* data, size_t len, const ResolvedAddr& peer) {
    struct sockaddr_storage sa{};
    socklen_t salen = 0;
    if (peer.family == AF_INET) {
        struct sockaddr_in* sa4 = reinterpret_cast<struct sockaddr_in*>(&sa);
        sa4->sin_family = AF_INET;
        sa4->sin_port = htons(peer.port);
        ::inet_pton(AF_INET, peer.ip.c_str(), &sa4->sin_addr);
        salen = sizeof *sa4;
    } else {
        struct sockaddr_in6* sa6 = reinterpret_cast<struct sockaddr_in6*>(&sa);
        sa6->sin6_family = AF_INET6;
        sa6->sin6_port = htons(peer.port);
        ::inet_pton(AF_INET6, peer.ip.c_str(), &sa6->sin6_addr);
        salen = sizeof *sa6;
    }
    ssize_t n = ::sendto(fd_, data, len, MSG_NOSIGNAL,
                         reinterpret_cast<struct sockaddr*>(&sa), salen);
    if (n < 0) {
        if (errno == EINTR) return send_all(data, len, peer);
        return false;
    }
    return static_cast<size_t>(n) == len;
}

bool UdpFramedSocket::send_bytes(const std::vector<uint8_t>& msg, const ResolvedAddr& peer) {
    return send_all(msg.data(), msg.size(), peer);
}

bool UdpFramedSocket::send_raw(const std::vector<uint8_t>& msg, const ResolvedAddr& peer) {
    if (msg.empty()) return true;
    std::vector<uint8_t> framed;
    codec_.encode(msg, framed);
    return send_all(framed.data(), framed.size(), peer);
}

std::optional<std::pair<std::vector<uint8_t>, ResolvedAddr>> UdpFramedSocket::recv_frame() {
    std::vector<uint8_t> buf(65536);  // Max UDP payload
    struct sockaddr_storage sa{};
    socklen_t salen = sizeof sa;
    ssize_t n = ::recvfrom(fd_, buf.data(), buf.size(), 0,
                           reinterpret_cast<struct sockaddr*>(&sa), &salen);
    if (n < 0) {
        if (errno == EINTR) return recv_frame();
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return std::nullopt;  // Timeout
        }
        return std::nullopt;
    }
    buf.resize(static_cast<size_t>(n));
    auto decoded = codec_.decode(buf);
    if (!decoded) {
        return std::nullopt;
    }
    ResolvedAddr peer{};
    peer.family = sa.ss_family;
    if (sa.ss_family == AF_INET) {
        const auto* sa4 = reinterpret_cast<const struct sockaddr_in*>(&sa);
        char ip[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &sa4->sin_addr, ip, sizeof ip);
        peer.ip = ip;
        peer.port = ntohs(sa4->sin_port);
    } else {
        const auto* sa6 = reinterpret_cast<const struct sockaddr_in6*>(&sa);
        char ip[INET6_ADDRSTRLEN];
        ::inet_ntop(AF_INET6, &sa6->sin6_addr, ip, sizeof ip);
        peer.ip = ip;
        peer.port = ntohs(sa6->sin6_port);
    }
    return std::make_pair(std::move(*decoded), std::move(peer));
}

std::optional<std::pair<std::vector<uint8_t>, ResolvedAddr>> UdpFramedSocket::next() {
    return recv_frame();
}

std::optional<std::pair<std::vector<uint8_t>, ResolvedAddr>> UdpFramedSocket::next_timeout(uint64_t timeout_ms) {
    if (timeout_ms > 0) {
        sock_set_timeout(fd_, static_cast<int>(timeout_ms));
        auto res = recv_frame();
        sock_set_timeout(fd_, 0);
        return res;
    }
    return recv_frame();
}

void UdpFramedSocket::set_timeout_ms(int timeout_ms) {
    sock_set_timeout(fd_, timeout_ms);
}

std::optional<ResolvedAddr> UdpFramedSocket::local_addr() const {
    struct sockaddr_storage sa {};
    socklen_t len = sizeof sa;
    if (::getsockname(fd_, reinterpret_cast<struct sockaddr*>(&sa), &len) != 0) {
        return std::nullopt;
    }
    ResolvedAddr out{};
    char ip[INET6_ADDRSTRLEN] = {};
    if (sa.ss_family == AF_INET) {
        const auto* sa4 = reinterpret_cast<const struct sockaddr_in*>(&sa);
        if (!::inet_ntop(AF_INET, &sa4->sin_addr, ip, sizeof ip)) {
            return std::nullopt;
        }
        out.family = AF_INET;
        out.port = ntohs(sa4->sin_port);
    } else if (sa.ss_family == AF_INET6) {
        const auto* sa6 = reinterpret_cast<const struct sockaddr_in6*>(&sa);
        if (!::inet_ntop(AF_INET6, &sa6->sin6_addr, ip, sizeof ip)) {
            return std::nullopt;
        }
        out.family = AF_INET6;
        out.port = ntohs(sa6->sin6_port);
    } else {
        return std::nullopt;
    }
    out.ip = ip;
    return out;
}

}  // namespace hbb_common