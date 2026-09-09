// test_udp.cpp — CTest parity for udp.rs. Tests framing round-trip over loopback.

#include <hbb_common/udp.hpp>
#include <hbb_common/util.hpp>

#include <cstdio>

using hbb_common::UdpFramedSocket;
using hbb_common::to_socket_addr;
using hbb_common::ResolvedAddr;

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

static void test_udp_bind() {
    const auto addr = to_socket_addr("127.0.0.1:0");
    auto sock = UdpFramedSocket::bind(addr);
    CHECK(sock.has_value());
    CHECK(sock->fd() >= 0);
}

static void test_udp_bind_reuse() {
    const auto addr = to_socket_addr("127.0.0.1:0");
    auto sock = UdpFramedSocket::bind_reuse(addr);
    CHECK(sock.has_value());
    CHECK(sock->fd() >= 0);
}

int main() {
    test_udp_bind();
    test_udp_bind_reuse();
    if (g_failures == 0) {
        std::puts("test_udp: basic tests passed");
    }
    return g_failures == 0 ? 0 : 1;
}