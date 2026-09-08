// test_addr_mangle.cpp — CTest parity for lib.rs #[cfg(test)] test_mangle,
// plus coverage for the other pure helpers ported in this step
// (get_version_from_url; to_socket_addr against localhost).

#include <hbb_common/addr_mangle.hpp>
#include <hbb_common/util.hpp>

#include <cstdio>
#include <string>

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

using hbb_common::AddrMangle;

static void test_mangle() {
    // Upstream vector: 192.168.16.32:21116 must round-trip exactly.
    const auto blob = AddrMangle::encode("192.168.16.32", 21116);
    CHECK(!blob.empty());
    CHECK(blob.size() <= 16);
    const auto back = AddrMangle::decode(blob);
    CHECK(back.first == "192.168.16.32");
    CHECK(back.second == 21116);
    // Second address, boundary port.
    const auto blob2 = AddrMangle::encode("10.0.0.1", 65535);
    const auto back2 = AddrMangle::decode(blob2);
    CHECK(back2.first == "10.0.0.1");
    CHECK(back2.second == 65535);
    // Non-IPv4 input: upstream panics, here it throws.
    bool threw = false;
    try {
        AddrMangle::encode("::1", 80);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

static void test_version_from_url() {
    // "rustdesk-1.1.2-x86_64.exe"-style names: version between last '-' and
    // last '.' when the extension is not numeric...
    CHECK(hbb_common::get_version_from_url("rustdesk-1.1.2-x86_64.exe") == "x86_64");
    // ...but a numeric tail returns the whole suffix past the last '-'.
    CHECK(hbb_common::get_version_from_url("app-1.1.9") == "1.1.9");
    // No dash at all.
    CHECK(hbb_common::get_version_from_url("nodash.exe") == "");
}

static void test_socket_addr_localhost() {
    // 127.0.0.1 must always resolve; first-result parity with addrs[0].
    const auto addr = hbb_common::to_socket_addr("127.0.0.1:8080");
    CHECK(addr.ip == "127.0.0.1");
    CHECK(addr.port == 8080);
    bool threw = false;
    try {
        hbb_common::to_socket_addr("no-such-host.invalid:1");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

int main() {
    test_mangle();
    test_version_from_url();
    test_socket_addr_localhost();
    if (g_failures == 0) {
        std::puts("test_addr_mangle: all tests passed");
    }
    return g_failures == 0 ? 0 : 1;
}
