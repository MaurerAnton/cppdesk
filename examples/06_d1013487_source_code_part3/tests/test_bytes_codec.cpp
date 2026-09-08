// test_bytes_codec.cpp — CTest parity for bytes_codec.rs #[cfg(test)]
// (test_codec1..test_codec6, assertion-for-assertion).

#include <hbb_common/bytes_codec.hpp>

#include <cstdio>

using hbb_common::BytesCodec;

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

static void test_codec1() {
    BytesCodec codec;
    std::vector<uint8_t> buf;
    std::vector<uint8_t> bytes(0x3F, 1);
    bool ok = true;
    try {
        codec.encode(bytes, buf);
    } catch (...) {
        ok = false;
    }
    CHECK(ok);
    const std::vector<uint8_t> saved = buf;
    CHECK(buf.size() == 0x3F + 1);
    auto res = codec.decode(buf);
    CHECK(res.has_value());
    if (res) {
        CHECK(res->size() == 0x3F);
        CHECK((*res)[0] == 1);
    }
    // Feed the header byte alone, then the rest: both must pend (Ok(None)).
    BytesCodec codec2;
    std::vector<uint8_t> buf2;
    CHECK(!codec2.decode(buf2).has_value());
    buf2.insert(buf2.end(), saved.begin(), saved.begin() + 1);
    CHECK(!codec2.decode(buf2).has_value());
    buf2.insert(buf2.end(), saved.begin() + 1, saved.end());
    res = codec2.decode(buf2);
    CHECK(res.has_value());
    if (res) {
        CHECK(res->size() == 0x3F);
        CHECK((*res)[0] == 1);
    }
}

static void test_codec2() {
    BytesCodec codec;
    std::vector<uint8_t> buf;
    bool ok = true;
    try {
        codec.encode(std::vector<uint8_t>(), buf);  // `"".into()` parity
    } catch (...) {
        ok = false;
    }
    CHECK(ok);
    CHECK(buf.size() == 1);
    std::vector<uint8_t> bytes(0x3F + 1, 2);
    try {
        codec.encode(bytes, buf);
    } catch (...) {
        ok = false;
    }
    CHECK(ok);
    CHECK(buf.size() == 0x3F + 2 + 2);
    auto res = codec.decode(buf);
    CHECK(res.has_value());
    if (res) {
        CHECK(res->size() == 0);
    }
    res = codec.decode(buf);
    CHECK(res.has_value());
    if (res) {
        CHECK(res->size() == 0x3F + 1);
        CHECK((*res)[0] == 2);
    }
}

static void test_codec3() {
    BytesCodec codec;
    std::vector<uint8_t> buf;
    std::vector<uint8_t> bytes(0x3F - 1, 3);
    bool ok = true;
    try {
        codec.encode(bytes, buf);
    } catch (...) {
        ok = false;
    }
    CHECK(ok);
    CHECK(buf.size() == 0x3F + 1 - 1);
    auto res = codec.decode(buf);
    CHECK(res.has_value());
    if (res) {
        CHECK(res->size() == 0x3F - 1);
        CHECK((*res)[0] == 3);
    }
}

static void test_codec4() {
    BytesCodec codec;
    std::vector<uint8_t> buf;
    std::vector<uint8_t> bytes(0x3FFF, 4);
    bool ok = true;
    try {
        codec.encode(bytes, buf);
    } catch (...) {
        ok = false;
    }
    CHECK(ok);
    CHECK(buf.size() == 0x3FFF + 2);
    auto res = codec.decode(buf);
    CHECK(res.has_value());
    if (res) {
        CHECK(res->size() == 0x3FFF);
        CHECK((*res)[0] == 4);
    }
}

static void test_codec5() {
    BytesCodec codec;
    std::vector<uint8_t> buf;
    std::vector<uint8_t> bytes(0x3FFFFF, 5);
    bool ok = true;
    try {
        codec.encode(bytes, buf);
    } catch (...) {
        ok = false;
    }
    CHECK(ok);
    CHECK(buf.size() == 0x3FFFFF + 3);
    auto res = codec.decode(buf);
    CHECK(res.has_value());
    if (res) {
        CHECK(res->size() == 0x3FFFFF);
        CHECK((*res)[0] == 5);
    }
}

static void test_codec6() {
    BytesCodec codec;
    std::vector<uint8_t> buf;
    std::vector<uint8_t> bytes(0x3FFFFF + 1, 6);
    bool ok = true;
    try {
        codec.encode(bytes, buf);
    } catch (...) {
        ok = false;
    }
    CHECK(ok);
    const std::vector<uint8_t> saved = buf;
    CHECK(buf.size() == 0x3FFFFF + 4 + 1);
    auto res = codec.decode(buf);
    CHECK(res.has_value());
    if (res) {
        CHECK(res->size() == 0x3FFFFF + 1);
        CHECK((*res)[0] == 6);
    }
    // Split-feed the 4-byte header 1+5+rest: first two chunks must pend.
    BytesCodec codec2;
    std::vector<uint8_t> buf2;
    buf2.insert(buf2.end(), saved.begin(), saved.begin() + 1);
    CHECK(!codec2.decode(buf2).has_value());
    buf2.insert(buf2.end(), saved.begin() + 1, saved.begin() + 6);
    CHECK(!codec2.decode(buf2).has_value());
    buf2.insert(buf2.end(), saved.begin() + 6, saved.end());
    res = codec2.decode(buf2);
    CHECK(res.has_value());
    if (res) {
        CHECK(res->size() == 0x3FFFFF + 1);
        CHECK((*res)[0] == 6);
    }
}

int main() {
    test_codec1();
    test_codec2();
    test_codec3();
    test_codec4();
    test_codec5();
    test_codec6();
    if (g_failures == 0) {
        std::puts("test_bytes_codec: all 6 codec tests passed");
    }
    return g_failures == 0 ? 0 : 1;
}
