// bytes_codec.cpp — see bytes_codec.hpp.
#include <hbb_common/bytes_codec.hpp>

#include <stdexcept>

namespace hbb_common {

BytesCodec::BytesCodec() = default;

void BytesCodec::set_raw() {
    raw_ = true;
}

void BytesCodec::set_max_packet_length(size_t n) {
    max_packet_length_ = n;
}

std::optional<size_t> BytesCodec::decode_head(std::vector<uint8_t>& buf) {
    if (buf.empty()) {
        return std::nullopt;
    }
    const size_t head_len = static_cast<size_t>((buf[0] & 0x3) + 1);
    if (buf.size() < head_len) {
        return std::nullopt;
    }
    size_t n = buf[0];
    if (head_len > 1) {
        n |= static_cast<size_t>(buf[1]) << 8;
    }
    if (head_len > 2) {
        n |= static_cast<size_t>(buf[2]) << 16;
    }
    if (head_len > 3) {
        n |= static_cast<size_t>(buf[3]) << 24;
    }
    n >>= 2;
    if (n > max_packet_length_) {
        throw std::length_error("Too big packet");
    }
    buf.erase(buf.begin(), buf.begin() + head_len);  // `src.advance(head_len)` parity
    return n;
}

std::optional<std::vector<uint8_t>> BytesCodec::decode_data(
    size_t n, std::vector<uint8_t>& buf) {
    if (buf.size() < n) {
        return std::nullopt;
    }
    std::vector<uint8_t> out(buf.begin(), buf.begin() + n);
    buf.erase(buf.begin(), buf.begin() + n);  // `src.split_to(n)` parity
    return out;
}

std::optional<std::vector<uint8_t>> BytesCodec::decode(std::vector<uint8_t>& buf) {
    if (raw_) {
        if (buf.empty()) {
            return std::nullopt;
        }
        std::vector<uint8_t> out;
        out.swap(buf);
        buf.clear();
        return out;  // `src.split_to(len)` parity
    }
    size_t n = pending_;
    if (state_ == State::Head) {
        const auto head = decode_head(buf);
        if (!head) {
            return std::nullopt;
        }
        n = *head;
        state_ = State::Data;
        pending_ = n;
    }
    const auto data = decode_data(n, buf);
    if (!data) {
        return std::nullopt;
    }
    state_ = State::Head;
    pending_ = 0;
    return data;
}

void BytesCodec::encode(const uint8_t* data, size_t len, std::vector<uint8_t>& out) {
    if (raw_) {
        out.insert(out.end(), data, data + len);
        return;
    }
    if (len <= 0x3F) {
        out.push_back(static_cast<uint8_t>(len << 2));  // `put_u8` parity
    } else if (len <= 0x3FFF) {
        const auto h = static_cast<uint16_t>((len << 2) | 0x1);
        out.push_back(static_cast<uint8_t>(h & 0xFF));  // `put_u16_le` parity
        out.push_back(static_cast<uint8_t>(h >> 8));
    } else if (len <= 0x3FFFFF) {
        const auto h = static_cast<uint32_t>((len << 2) | 0x2);
        out.push_back(static_cast<uint8_t>(h & 0xFF));
        out.push_back(static_cast<uint8_t>((h >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(h >> 16));
    } else if (len <= 0x3FFFFFFFu) {
        const auto h = static_cast<uint32_t>((len << 2) | 0x3);
        out.push_back(static_cast<uint8_t>(h & 0xFF));  // `put_u32_le` parity
        out.push_back(static_cast<uint8_t>((h >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((h >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>(h >> 24));
    } else {
        throw std::overflow_error("Overflow");
    }
    out.insert(out.end(), data, data + len);
}

void BytesCodec::encode(const std::vector<uint8_t>& data, std::vector<uint8_t>& out) {
    encode(data.data(), data.size(), out);
}

}  // namespace hbb_common
