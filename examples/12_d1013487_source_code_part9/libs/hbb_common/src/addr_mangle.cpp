// addr_mangle.cpp — see addr_mangle.hpp.
#include <hbb_common/addr_mangle.hpp>

#include <arpa/inet.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace hbb_common {
namespace {

// `u128` parity. Little-endian hosts only in practice (same caveat as
// upstream, whose to_ne_bytes blob only round-trips on one endianness);
// fail loudly anywhere the assumption breaks.
void put_u128_ne(unsigned __int128 v, std::array<uint8_t, 16>& out) {
    static_assert(sizeof(unsigned __int128) == 16, "need a 128-bit int type");
    static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "LE host required");
    std::memcpy(out.data(), &v, 16);  // `to_ne_bytes` parity
}

unsigned __int128 get_u128_ne(const std::array<uint8_t, 16>& in) {
    unsigned __int128 v = 0;
    std::memcpy(&v, in.data(), 16);  // `from_ne_bytes` parity
    return v;
}

}  // namespace

std::vector<uint8_t> AddrMangle::encode(const std::string& ip, uint16_t port) {
    struct in_addr addr4 {};
    if (::inet_pton(AF_INET, ip.c_str(), &addr4) != 1) {
        throw std::invalid_argument("AddrMangle::encode: not an IPv4 address");
    }
    uint32_t ip_u32 = 0;
    std::memcpy(&ip_u32, &addr4.s_addr, 4);

    // `(now.as_micros() as u32) as u128` parity (wrapping truncation included).
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const auto tm = static_cast<unsigned __int128>(static_cast<uint32_t>(micros));

    const auto v = ((static_cast<unsigned __int128>(ip_u32) + tm) << 49) | (tm << 17) |
                   (static_cast<unsigned __int128>(port) + (tm & 0xFFFFu));
    std::array<uint8_t, 16> bytes{};
    put_u128_ne(v, bytes);
    size_t n_padding = 0;
    for (auto it = bytes.rbegin(); it != bytes.rend(); ++it) {  // `.rev()` parity
        if (*it == 0) {
            ++n_padding;
        } else {
            break;
        }
    }
    return std::vector<uint8_t>(bytes.begin(), bytes.begin() + (16 - n_padding));
}

std::pair<std::string, uint16_t> AddrMangle::decode(const uint8_t* bytes, size_t len) {
    if (len > 16) {
        throw std::invalid_argument("AddrMangle::decode: blob longer than 16 bytes");
    }
    std::array<uint8_t, 16> padded{};
    std::memcpy(padded.data(), bytes, len);
    const unsigned __int128 number = get_u128_ne(padded);

    const auto tm = static_cast<uint32_t>((number >> 17) & 0xFFFFFFFFu);
    const auto ip_u32 = static_cast<uint32_t>((number >> 49) - tm);
    // `to_ne_bytes` + `Ipv4Addr::new(ip[0..3])` + Display parity: same 4 bytes,
    // same dotted order on the same endianness.
    std::array<uint8_t, 4> ip_bytes{};
    std::memcpy(ip_bytes.data(), &ip_u32, 4);
    char ip_str[INET_ADDRSTRLEN] = {};
    std::snprintf(ip_str, sizeof ip_str, "%u.%u.%u.%u", ip_bytes[0], ip_bytes[1],
                  ip_bytes[2], ip_bytes[3]);
    const auto port =
        static_cast<uint16_t>((number & 0xFFFFFFu) - (tm & 0xFFFFu));  // `as u16` parity
    return {std::string(ip_str), port};
}

std::pair<std::string, uint16_t> AddrMangle::decode(const std::vector<uint8_t>& bytes) {
    return decode(bytes.data(), bytes.size());
}

}  // namespace hbb_common
