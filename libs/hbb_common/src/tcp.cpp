// tcp.cpp — implementation of tcp.hpp.
#include <hbb_common/tcp.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace hbb_common {
namespace {

// Set socket send/receive timeout (ms). 0 = infinite (blocking).
void sock_set_timeout(int fd, int timeout_ms) {
    if (timeout_ms < 0) timeout_ms = 0;
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

}  // namespace

TcpFramedStream::TcpFramedStream(int fd) : fd_(fd) {}

TcpFramedStream::~TcpFramedStream() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

TcpFramedStream::TcpFramedStream(TcpFramedStream&& other) noexcept
    : fd_(other.fd_), codec_(std::move(other.codec_)),
      crypto_(std::move(other.crypto_)) {
    other.fd_ = -1;
}

TcpFramedStream& TcpFramedStream::operator=(TcpFramedStream&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = other.fd_;
        codec_ = std::move(other.codec_);
        crypto_ = std::move(other.crypto_);
        other.fd_ = -1;
    }
    return *this;
}

std::optional<TcpFramedStream> TcpFramedStream::connect(
    const ResolvedAddr& remote_addr,
    const ResolvedAddr& local_addr,
    uint64_t timeout_ms) {
    try {
        int fd = new_socket(local_addr, true, true);
        // Set connect timeout using select() on the socket.
        if (timeout_ms > 0) {
            sock_set_timeout(fd, static_cast<int>(timeout_ms));
        }
        if (local_addr.family == AF_INET) {
            struct sockaddr_in sa {};
            sa.sin_family = AF_INET;
            sa.sin_port = htons(remote_addr.port);
            ::inet_pton(AF_INET, remote_addr.ip.c_str(), &sa.sin_addr);
            if (::connect(fd, reinterpret_cast<struct sockaddr*>(&sa), sizeof sa) != 0) {
                if (errno != EINPROGRESS) {
                    ::close(fd);
                    return std::nullopt;
                }
            }
        } else {
            struct sockaddr_in6 sa {};
            sa.sin6_family = AF_INET6;
            sa.sin6_port = htons(remote_addr.port);
            ::inet_pton(AF_INET6, remote_addr.ip.c_str(), &sa.sin6_addr);
            if (::connect(fd, reinterpret_cast<struct sockaddr*>(&sa), sizeof sa) != 0) {
                if (errno != EINPROGRESS) {
                    ::close(fd);
                    return std::nullopt;
                }
            }
        }
        // For blocking connect, the timeout was set via SO_RCVTIMEO/SO_SNDTIMEO.
        // Check if connection succeeded.
        if (timeout_ms > 0) {
            fd_set wset;
            FD_ZERO(&wset);
            FD_SET(fd, &wset);
            struct timeval tv;
            tv.tv_sec = static_cast<long>(timeout_ms / 1000);
            tv.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
            if (::select(fd + 1, nullptr, &wset, nullptr, &tv) <= 0) {
                ::close(fd);
                return std::nullopt;
            }
            int err = 0;
            socklen_t len = sizeof err;
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
                ::close(fd);
                return std::nullopt;
            }
        }
        return TcpFramedStream(fd);
    } catch (...) {
        return std::nullopt;
    }
}

void TcpFramedStream::set_raw() {
    codec_.set_raw();
}

bool TcpFramedStream::is_secured() const {
    return crypto_.has_value();
}

void TcpFramedStream::set_key(const std::vector<uint8_t>& key) {
    if (key.size() != 24) {
        // secretbox key is 24 bytes; log/ignore but don't crash.
        return;
    }
    crypto_ = std::make_tuple(key, 0, 0);
}

bool TcpFramedStream::send_all(const uint8_t* data, size_t len) {
    while (len > 0) {
        ssize_t n = ::send(fd_, data, len, MSG_NOSIGNAL);
        if (n <= 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout on send.
                return false;
            }
            return false;
        }
        data += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

bool TcpFramedStream::send_bytes(const std::vector<uint8_t>& msg) {
    return send_all(msg.data(), msg.size());
}

bool TcpFramedStream::send_raw(const std::vector<uint8_t>& msg) {
    if (msg.empty()) return true;
    if (crypto_) {
        // TODO(crypto step): implement secretbox encryption with nonce.
        // For now, just pass through with a warning (or assert in debug).
        // Parity: upstream encrypts if key is set.
    }
    std::vector<uint8_t> framed;
    codec_.encode(msg, framed);
    return send_all(framed.data(), framed.size());
}

std::optional<std::vector<uint8_t>> TcpFramedStream::recv_frame() {
    std::vector<uint8_t> buf(4096);
    while (true) {
        ssize_t n = ::recv(fd_, buf.data(), buf.size(), 0);
        if (n == 0) {
            return std::nullopt;  // EOF
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return std::nullopt;  // Timeout
            }
            return std::nullopt;  // Error
        }
        buf.resize(static_cast<size_t>(n));
        auto decoded = codec_.decode(buf);
        if (decoded) {
            if (crypto_) {
                // TODO(crypto step): implement secretbox decryption with nonce.
            }
            return decoded;
        }
        // Need more data; continue reading (this is a simplified loop;
        // a full impl would keep a persistent read buffer).
    }
}

std::optional<std::vector<uint8_t>> TcpFramedStream::next() {
    return recv_frame();
}

std::optional<std::vector<uint8_t>> TcpFramedStream::next_timeout(uint64_t timeout_ms) {
    if (timeout_ms > 0) {
        set_timeout_ms_for_call(static_cast<int>(timeout_ms));
        auto res = recv_frame();
        restore_timeout();
        return res;
    }
    return recv_frame();
}

void TcpFramedStream::set_timeout_ms(int timeout_ms) {
    sock_set_timeout(fd_, timeout_ms);
}

void TcpFramedStream::set_timeout_ms_for_call(int timeout_ms) {
    struct timeval tv;
    socklen_t len = sizeof tv;
    ::getsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, &len);
    saved_rcv_timeout_ms_ = static_cast<int>(tv.tv_sec * 1000 + tv.tv_usec / 1000);
    ::getsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, &len);
    saved_snd_timeout_ms_ = static_cast<int>(tv.tv_sec * 1000 + tv.tv_usec / 1000);
    sock_set_timeout(fd_, timeout_ms);
}

void TcpFramedStream::restore_timeout() {
    if (saved_rcv_timeout_ms_ > 0 || saved_snd_timeout_ms_ > 0) {
        sock_set_timeout(fd_, std::max(saved_rcv_timeout_ms_, saved_snd_timeout_ms_));
    }
    saved_rcv_timeout_ms_ = 0;
    saved_snd_timeout_ms_ = 0;
}

// --- TcpListener ------------------------------------------------------------

std::optional<TcpListener> TcpListener::bind(const ResolvedAddr& addr, bool reuse) {
    try {
        int fd = new_socket(addr, true, reuse);
        if (::listen(fd, TcpFramedStream::kDefaultBacklog) != 0) {
            ::close(fd);
            return std::nullopt;
        }
        return TcpListener(fd);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<TcpFramedStream> TcpListener::accept(uint64_t timeout_ms) {
    if (timeout_ms > 0) {
        sock_set_timeout(fd_, static_cast<int>(timeout_ms));
    }
    struct sockaddr_storage sa{};
    socklen_t salen = sizeof sa;
    int cfd = ::accept(fd_, reinterpret_cast<struct sockaddr*>(&sa), &salen);
    if (timeout_ms > 0) {
        sock_set_timeout(fd_, 0);  // restore blocking
    }
    if (cfd < 0) {
        return std::nullopt;
    }
    return TcpFramedStream(cfd);
}

TcpListener::~TcpListener() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

}  // namespace hbb_common