// addr_mangle.hpp — translation of `AddrMangle` in libs/hbb_common/src/lib.rs.
//
// Certain routers/firewalls scan packets for IP addresses from their NAT
// pool, so the address is mangled before going on the wire. Integer layout
// (u128 math, native-endian byte order, trailing-zero trim) is ported 1:1.
//
// Portability notes:
// - `unsigned __int128` needs GCC/Clang (upstream ran on all targets, but the
//   mangled blob only ever crosses same-endian machines in practice; an MSVC
//   path arrives with the Windows platform step).
// - Upstream panics on non-IPv4 input; here that is std::invalid_argument.
// - `SocketAddrV4` maps to (ip string, port) pairs at this layer; the socket
//   wrapper that owns file descriptors arrives with the tcp/udp step.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hbb_common {

class AddrMangle {
public:
    // `ip`: dotted-decimal IPv4 quad. Throws std::invalid_argument if unparsable.
    static std::vector<uint8_t> encode(const std::string& ip, uint16_t port);
    // Returns {dotted-decimal ip, port}. Throws std::invalid_argument if len > 16.
    static std::pair<std::string, uint16_t> decode(const uint8_t* bytes, size_t len);
    static std::pair<std::string, uint16_t> decode(const std::vector<uint8_t>& bytes);
};

}  // namespace hbb_common
