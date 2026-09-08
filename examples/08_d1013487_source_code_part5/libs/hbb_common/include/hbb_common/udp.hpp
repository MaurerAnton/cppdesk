// udp.hpp — translation of libs/hbb_common/src/udp.rs (transport layer).
//
// Framed UDP socket with the same API surface as the Rust FramedSocket.
// Differences from Rust:
// - Blocking sockets with timeouts (no async).
// - `tokio_util::udp::UdpFramed<BytesCodec>` → manual framing using BytesCodec.
// - `ToSocketAddrs` → our ResolvedAddr.
// - `super::new_socket` already ported in util.cpp.

#pragma once

#include <hbb_common/bytes_codec.hpp>
#include <hbb_common/util.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hbb_common {

class UdpFramedSocket {
public:
    // Bind to local address (like `FramedSocket::new`).
    static std::optional<UdpFramedSocket> bind(const ResolvedAddr& addr);

    // Bind with SO_REUSEADDR (like `FramedSocket::new_reuse`).
    static std::optional<UdpFramedSocket> bind_reuse(const ResolvedAddr& addr);

    // Destructor closes the fd.
    ~UdpFramedSocket();

    UdpFramedSocket(const UdpFramedSocket&) = delete;
    UdpFramedSocket& operator=(const UdpFramedSocket&) = delete;
    UdpFramedSocket(UdpFramedSocket&& other) noexcept;
    UdpFramedSocket& operator=(UdpFramedSocket&& other) noexcept;

    // Send a protobuf message to a specific peer address (serializes via
    // SerializeAsString; the Rust side calls Message::write_to_bytes).
    template <typename Msg>
    bool send(const Msg& msg, const ResolvedAddr& peer) {
        const std::string bytes = msg.SerializeAsString();
        return send_raw(std::vector<uint8_t>(bytes.begin(), bytes.end()), peer);
    }

    // Send raw bytes to a peer.
    bool send_raw(const std::vector<uint8_t>& msg, const ResolvedAddr& peer);

    // Send static bytes (already-encoded frame) to a peer.
    bool send_bytes(const std::vector<uint8_t>& msg, const ResolvedAddr& peer);

    // Receive next frame. Returns nullopt on timeout/EOF, or {payload, peer_addr}.
    std::optional<std::pair<std::vector<uint8_t>, ResolvedAddr>> next();

    // Receive with explicit timeout (ms).
    std::optional<std::pair<std::vector<uint8_t>, ResolvedAddr>> next_timeout(uint64_t timeout_ms);

    int fd() const { return fd_; }

private:
    explicit UdpFramedSocket(int fd) : fd_(fd) {}

    int fd_ = -1;
    BytesCodec codec_;

    bool send_all(const uint8_t* data, size_t len, const ResolvedAddr& peer);
    std::optional<std::pair<std::vector<uint8_t>, ResolvedAddr>> recv_frame();
    void set_timeout_ms(int timeout_ms);
};

}  // namespace hbb_common