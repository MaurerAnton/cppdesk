// bytes_codec.hpp — translation of libs/hbb_common/src/bytes_codec.rs.
//
// Length-delimited framing: 1/2/3/4-byte little-endian header holding
// (len << 2 | tag), tag = header length - 1. `tokio_util::codec::Decoder` /
// `Encoder<Bytes>` become methods on a buffer object: Rust mutates a BytesMut
// in place; here decode() consumes from the front of a byte vector, which is
// the same observable framing.
//
// Error mapping: `Err(InvalidData "Too big packet")` becomes
// std::length_error; `Err(InvalidInput "Overflow")` becomes
// std::overflow_error; `Ok(None)` (need more bytes) becomes std::nullopt.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hbb_common {

class BytesCodec {
public:
    BytesCodec();

    // Raw mode: decode() yields everything buffered; encode() appends as-is.
    void set_raw();
    // Reject packets larger than n on decode (`usize::MAX` by default).
    void set_max_packet_length(size_t n);

    std::optional<std::vector<uint8_t>> decode(std::vector<uint8_t>& buf);
    void encode(const uint8_t* data, size_t len, std::vector<uint8_t>& out);
    void encode(const std::vector<uint8_t>& data, std::vector<uint8_t>& out);

private:
    enum class State { Head, Data };

    // Returns the payload length and consumes the header, or nullopt when the
    // header is incomplete.
    std::optional<size_t> decode_head(std::vector<uint8_t>& buf);
    static std::optional<std::vector<uint8_t>> decode_data(size_t n,
                                                            std::vector<uint8_t>& buf);

    State state_ = State::Head;
    size_t pending_ = 0;  // valid only in State::Data (`DecodeState::Data(n)` parity)
    bool raw_ = false;
    size_t max_packet_length_ = static_cast<size_t>(-1);  // `usize::MAX` parity
};

}  // namespace hbb_common
