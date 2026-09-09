// tcp.hpp — translation of libs/hbb_common/src/tcp.rs (transport layer).
//
// This provides a framed TCP stream with the same API surface as the Rust
// FramedStream. Key differences from Rust:
// - No async/await: uses blocking sockets with timeouts (SO_RCVTIMEO/SO_SNDTIMEO).
// - Encryption (sodiumoxide secretbox) is a stub until the crypto step lands.
// - `tokio_util::codec::Framed<BytesCodec>` → manual framing using our BytesCodec.
// - `super::timeout` → SO_RCVTIMEO/SO_SNDTIMEO + select() fallback.
// - `super::new_socket` already ported in util.cpp.
//
// The API mirrors the Rust methods so higher layers (client/server) can be
// ported with minimal changes when their turn comes.

#pragma once

#include <hbb_common/bytes_codec.hpp>
#include <hbb_common/util.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hbb_common {

class TcpFramedStream {
public:
    // Default backlog for listeners (parity with DEFAULT_BACKLOG = 128).
    static constexpr int kDefaultBacklog = 128;

    // Construct from an already-connected fd (takes ownership).
    explicit TcpFramedStream(int fd);

    // Destructor closes the fd.
    ~TcpFramedStream();

    TcpFramedStream(const TcpFramedStream&) = delete;
    TcpFramedStream& operator=(const TcpFramedStream&) = delete;
    TcpFramedStream(TcpFramedStream&& other) noexcept;
    TcpFramedStream& operator=(TcpFramedStream&& other) noexcept;

    // Connect to remote_addr with optional local bind and timeout (ms).
    // Mirrors `FramedStream::new(remote_addr, local_addr, ms_timeout)`.
    // Returns nullopt on timeout or connection failure.
    static std::optional<TcpFramedStream> connect(
        const ResolvedAddr& remote_addr,
        const ResolvedAddr& local_addr,
        uint64_t timeout_ms);

    // Switch to raw mode (disable length-delimited framing).
    void set_raw();

    // Whether a secretbox key is active.
    bool is_secured() const;

    // Send a protobuf message (serializes via SerializeAsString; the Rust
    // side calls Message::write_to_bytes — same wire bytes).
    // Template for any protobuf-generated message type.
    template <typename Msg>
    bool send(const Msg& msg) {
        const std::string bytes = msg.SerializeAsString();
        return send_raw(std::vector<uint8_t>(bytes.begin(), bytes.end()));
    }

    // Send raw bytes (length-delimited unless raw mode).
    bool send_raw(const std::vector<uint8_t>& msg);

    // Send pre-constructed bytes (for already-encoded frames).
    bool send_bytes(const std::vector<uint8_t>& msg);

    // Receive next frame. Returns nullopt on timeout/EOF, empty vector on
    // protocol error, or the decoded payload.
    std::optional<std::vector<uint8_t>> next();

    // Receive with explicit timeout (ms). Overrides socket timeout for this call.
    std::optional<std::vector<uint8_t>> next_timeout(uint64_t timeout_ms);

    // Install encryption key (parity with `set_key(Key)`).
    // Stub: records key but does not encrypt until crypto step.
    void set_key(const std::vector<uint8_t>& key);

    // Get the underlying fd (for advanced use / platform layer).
    int fd() const { return fd_; }

    // Local address of the socket (getsockname). Used to re-bind the same
    // local port across successive rendezvous connections
    // (`socket.get_ref().local_addr()` parity in test_nat_type_).
    // Returns nullopt if the address cannot be determined.
    std::optional<ResolvedAddr> local_addr() const;

private:
    // Encryption state: (key, outbound_seq, inbound_seq). seq starts at 0.
    // In crypto step: key = sodiumoxide::crypto::secretbox::Key (24 bytes).
    // Nonce construction: 24-byte nonce with first 8 bytes = LE(seqnum).
    std::optional<std::tuple<std::vector<uint8_t>, uint64_t, uint64_t>> crypto_;

    int fd_ = -1;
    BytesCodec codec_;

    // Internal send/receive helpers.
    bool send_all(const uint8_t* data, size_t len);
    std::optional<std::vector<uint8_t>> recv_frame();

    // Timeout helpers (set SO_RCVTIMEO/SO_SNDTIMEO).
    void set_timeout_ms(int timeout_ms);
    void set_timeout_ms_for_call(int timeout_ms);
    void restore_timeout();
    int saved_rcv_timeout_ms_ = 0;
    int saved_snd_timeout_ms_ = 0;
};

// TCP listener wrapper (parity with `new_listener`).
class TcpListener {
public:
    // Bind to address. If `reuse` is true, uses new_socket with reuse opts.
    static std::optional<TcpListener> bind(const ResolvedAddr& addr, bool reuse = false);

    // Accept next connection (blocking with timeout).
    std::optional<TcpFramedStream> accept(uint64_t timeout_ms = 0);

    int fd() const { return fd_; }

    ~TcpListener();

private:
    explicit TcpListener(int fd) : fd_(fd) {}
    int fd_ = -1;
};

}  // namespace hbb_common