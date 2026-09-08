// test_compress.cpp — guards the zstd wiring of compress.cpp.
// Upstream ships no compress test; this locks the round-trip contract
// (compress -> decompress == identity) that later steps rely on.

#include <hbb_common/compress.hpp>

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

int main() {
    const std::string text =
        "rustdesk hbb_common compress round-trip — repeated repeated repeated "
        "payload to make zstd's job representative. ";
    std::string big;
    for (int i = 0; i < 200; ++i) {
        big += text;
    }
    const std::vector<uint8_t> in(big.begin(), big.end());
    const auto compressed = hbb_common::compress(in, 3);  // default level parity
    CHECK(!compressed.empty());
    CHECK(compressed.size() < in.size());
    const auto back = hbb_common::decompress(compressed);
    CHECK(back == in);
    // Empty input round-trips too.
    const auto empty_back =
        hbb_common::decompress(hbb_common::compress(std::vector<uint8_t>(), 1));
    CHECK(empty_back.empty());
    if (g_failures == 0) {
        std::puts("test_compress: all tests passed");
    }
    return g_failures == 0 ? 0 : 1;
}
