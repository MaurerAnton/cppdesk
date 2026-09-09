// test_crypto.cpp — round-trips for the hbb_common::crypto seam (libsodium).
// Upstream has no crypto unit tests (sodiumoxide is exercised implicitly);
// these lock the exact primitives the handshake and stream encryption rely on:
// sign/verify, box seal/open, secretbox seal/open, and the seq_nonce layout.

#include <hbb_common/crypto.hpp>

#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

using namespace hbb_common::crypto;

void test_sign() {
    const auto [pk, sk] = sign_gen_keypair();
    CHECK(pk.size() == kSignPublicKeyBytes);
    CHECK(sk.size() == kSignSecretKeyBytes);
    const std::string msg = "peer-id-123";
    const auto signed_msg =
        sign_sign(reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), sk);
    CHECK(signed_msg.size() == msg.size() + kSignBytes);
    const auto back = sign_verify(signed_msg.data(), signed_msg.size(), pk);
    CHECK(back == std::vector<uint8_t>(msg.begin(), msg.end()));
    // Tampered signature fails loudly.
    auto bad = signed_msg;
    bad[0] ^= 0xFF;
    bool threw = false;
    try {
        sign_verify(bad.data(), bad.size(), pk);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    // Wrong key fails too.
    const auto [pk2, sk2] = sign_gen_keypair();
    (void)sk2;
    threw = false;
    try {
        sign_verify(signed_msg.data(), signed_msg.size(), pk2);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

void test_box() {
    const auto [pk_a, sk_a] = box_gen_keypair();
    const auto [pk_b, sk_b] = box_gen_keypair();
    const Nonce24 nonce{};
    const std::string msg = "session key material";
    const auto sealed = box_seal(reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), nonce,
                                 pk_b, sk_a);
    CHECK(sealed.size() == msg.size() + 16);
    const auto back = box_open(sealed.data(), sealed.size(), nonce, pk_a, sk_b);
    CHECK(back == std::vector<uint8_t>(msg.begin(), msg.end()));
    // Wrong nonce fails.
    Nonce24 bad_nonce{};
    bad_nonce[0] = 1;
    bool threw = false;
    try {
        box_open(sealed.data(), sealed.size(), bad_nonce, pk_a, sk_b);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

void test_secretbox() {
    const std::vector<uint8_t> key = secretbox_gen_key();
    CHECK(key.size() == kSecretBoxKeyBytes);
    const Nonce24 nonce = seq_nonce(1);
    // get_nonce parity: 24 zero bytes, first 8 = LE(1).
    CHECK(nonce[0] == 1);
    for (int i = 1; i < 24; ++i) {
        CHECK(nonce[static_cast<size_t>(i)] == 0);
    }
    const Nonce24 nonce2 = seq_nonce(0x0102030405060708ULL);
    CHECK(nonce2[0] == 0x08 && nonce2[7] == 0x01);
    const std::string msg = "frame payload";
    const auto sealed =
        secretbox_seal(reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), nonce, key);
    const auto back = secretbox_open(sealed.data(), sealed.size(), nonce, key);
    CHECK(back == std::vector<uint8_t>(msg.begin(), msg.end()));
    // Tampered ciphertext fails (this is the stream "decryption error" path).
    auto bad = sealed;
    bad[20] ^= 0x01;
    bool threw = false;
    try {
        secretbox_open(bad.data(), bad.size(), nonce, key);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

}  // namespace

int main() {
    test_sign();
    test_box();
    test_secretbox();
    if (g_failures == 0) {
        std::puts("test_crypto: all tests passed");
    }
    return g_failures == 0 ? 0 : 1;
}
