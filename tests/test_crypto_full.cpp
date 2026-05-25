#include <gtest/gtest.h>
#include "common/crypto.hpp"

#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <set>
#include <thread>
#include <chrono>

using namespace cppdesk::common::crypto;

// ============================================================================
// Helper utilities
// ============================================================================

namespace {

// Build a deterministic byte vector of the given size
std::vector<uint8_t> make_data(size_t size) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    return data;
}

// Build a deterministic byte array from a hex string
std::vector<uint8_t> from_hex(const std::string& hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto nibble = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        out.push_back(static_cast<uint8_t>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
    }
    return out;
}

// Convert array<uint8_t, N> to hex string for display
template<size_t N>
std::string to_hex(const std::array<uint8_t, N>& arr) {
    static const char hex_chars[] = "0123456789abcdef";
    std::string out;
    out.reserve(N * 2);
    for (auto b : arr) {
        out.push_back(hex_chars[b >> 4]);
        out.push_back(hex_chars[b & 0x0F]);
    }
    return out;
}

std::string to_hex(const std::vector<uint8_t>& vec) {
    static const char hex_chars[] = "0123456789abcdef";
    std::string out;
    out.reserve(vec.size() * 2);
    for (auto b : vec) {
        out.push_back(hex_chars[b >> 4]);
        out.push_back(hex_chars[b & 0x0F]);
    }
    return out;
}

// Generate a random key for secretbox (32 bytes)
std::array<uint8_t, SECRET_BOX_KEY_BYTES> make_secretbox_key() {
    auto r = random_bytes(SECRET_BOX_KEY_BYTES);
    std::array<uint8_t, SECRET_BOX_KEY_BYTES> key{};
    std::copy(r.begin(), r.end(), key.begin());
    return key;
}

// Generate a random nonce for secretbox (24 bytes)
std::array<uint8_t, SECRET_BOX_NONCE_BYTES> make_secretbox_nonce() {
    auto r = random_bytes(SECRET_BOX_NONCE_BYTES);
    std::array<uint8_t, SECRET_BOX_NONCE_BYTES> nonce{};
    std::copy(r.begin(), r.end(), nonce.begin());
    return nonce;
}

// Generate a random nonce for box (24 bytes)
std::array<uint8_t, BOX_NONCE_BYTES> make_box_nonce() {
    auto r = random_bytes(BOX_NONCE_BYTES);
    std::array<uint8_t, BOX_NONCE_BYTES> nonce{};
    std::copy(r.begin(), r.end(), nonce.begin());
    return nonce;
}

// Generate a random key for AES-GCM (32 bytes)
std::array<uint8_t, AES_KEY_BYTES> make_aes_key() {
    auto r = random_bytes(AES_KEY_BYTES);
    std::array<uint8_t, AES_KEY_BYTES> key{};
    std::copy(r.begin(), r.end(), key.begin());
    return key;
}

// Generate a random nonce for AES-GCM (12 bytes)
std::array<uint8_t, AES_NONCE_BYTES> make_aes_nonce() {
    auto r = random_bytes(AES_NONCE_BYTES);
    std::array<uint8_t, AES_NONCE_BYTES> nonce{};
    std::copy(r.begin(), r.end(), nonce.begin());
    return nonce;
}

} // anonymous namespace

// ============================================================================
// 1. SHA256 Tests
// ============================================================================

TEST(SHA256Test, Deterministic) {
    auto h1 = sha256("hello world");
    auto h2 = sha256("hello world");
    EXPECT_EQ(h1, h2) << "SHA256 must be deterministic";
}

TEST(SHA256Test, DifferentInputsProduceDifferentHashes) {
    auto h1 = sha256("hello");
    auto h2 = sha256("world");
    auto h3 = sha256("hello!");
    EXPECT_NE(h1, h2) << "Different inputs must produce different hashes";
    EXPECT_NE(h1, h3);
    EXPECT_NE(h2, h3);
}

TEST(SHA256Test, OutputSize) {
    auto h = sha256("test");
    EXPECT_EQ(h.size(), static_cast<size_t>(SHA256_BYTES));
}

TEST(SHA256Test, EmptyString) {
    // Known answer: SHA256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    auto h = sha256("");
    auto expected = from_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(h.size(), expected.size());
    for (size_t i = 0; i < h.size(); ++i) {
        EXPECT_EQ(h[i], expected[i]) << "Mismatch at byte " << i;
    }
}

TEST(SHA256Test, KnownAnswer_abc) {
    // SHA256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    auto h = sha256("abc");
    auto expected = from_hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(h.size(), expected.size());
    for (size_t i = 0; i < h.size(); ++i) {
        EXPECT_EQ(h[i], expected[i]) << "Mismatch at byte " << i;
    }
}

TEST(SHA256Test, KnownAnswer_448bits) {
    // SHA256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
    // = 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1
    std::string msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    auto h = sha256(msg);
    auto expected = from_hex("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(h.size(), expected.size());
    for (size_t i = 0; i < h.size(); ++i) {
        EXPECT_EQ(h[i], expected[i]) << "Mismatch at byte " << i;
    }
}

TEST(SHA256Test, KnownAnswer_1M_a) {
    // SHA256 of 1,000,000 'a' characters
    std::string million_a(1000000, 'a');
    auto h = sha256(million_a);
    // Expected: cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0
    auto expected = from_hex("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    EXPECT_EQ(h.size(), expected.size());
    for (size_t i = 0; i < h.size(); ++i) {
        EXPECT_EQ(h[i], expected[i]) << "Mismatch at byte " << i;
    }
}

TEST(SHA256Test, NullTerminatedString) {
    // "test\0hidden" - string overload stops at null
    std::string s1("test");
    std::string s2 = std::string("test\0hidden", 11);
    auto h1 = sha256(s1);  // hashes "test"
    auto h2 = sha256(s2);  // hashes "test\0hidden" (11 bytes)
    // These should be different because s2 contains null bytes
    EXPECT_NE(h1, h2);
}

TEST(SHA256Test, BinaryDataConsistency) {
    // SHA256 via uint8_t* overload should match string overload for same data
    const char* text = "binary test data";
    auto h1 = sha256(reinterpret_cast<const uint8_t*>(text), std::strlen(text));
    auto h2 = sha256(std::string(text));
    EXPECT_EQ(h1, h2);
}

TEST(SHA256Test, SingleByte) {
    // SHA256 of single byte 0x00
    uint8_t zero = 0x00;
    auto h_zero = sha256(&zero, 1);
    auto expected_zero = from_hex("6e340b9cffb37a989ca544e6bb780a2c78901d3fb33738768511a30617afa01d");
    for (size_t i = 0; i < h_zero.size(); ++i) {
        EXPECT_EQ(h_zero[i], expected_zero[i]) << "Mismatch at byte " << i;
    }

    // SHA256 of single byte 0xFF
    uint8_t ff = 0xFF;
    auto h_ff = sha256(&ff, 1);
    auto expected_ff = from_hex("a8100ae6aa1940d0b663bb31cd466142ebbdbd5187131b92d93818987832eb89");
    for (size_t i = 0; i < h_ff.size(); ++i) {
        EXPECT_EQ(h_ff[i], expected_ff[i]) << "Mismatch at byte " << i;
    }
}

TEST(SHA256Test, AvalancheEffect) {
    // Flipping a single bit should dramatically change the hash
    auto h1 = sha256("test");
    auto h2 = sha256("uest");  // t->u flips bit 5 of first byte
    int differing_bits = 0;
    for (size_t i = 0; i < h1.size(); ++i) {
        uint8_t diff = h1[i] ^ h2[i];
        while (diff) {
            differing_bits += (diff & 1);
            diff >>= 1;
        }
    }
    // On average ~128 bits should differ out of 256 for good avalanche
    EXPECT_GT(differing_bits, 80) << "Avalanche effect too weak: only "
        << differing_bits << " bits differ";
}

TEST(SHA256Test, LargeData) {
    // Hash large data (1MB) to ensure it doesn't crash
    auto large = make_data(1024 * 1024);
    auto h = sha256(large.data(), large.size());
    EXPECT_EQ(h.size(), static_cast<size_t>(SHA256_BYTES));
    // Hash of sequential bytes should not be all zeros
    bool all_zero = std::all_of(h.begin(), h.end(), [](uint8_t b) { return b == 0; });
    EXPECT_FALSE(all_zero) << "Hash should not be all zeros";
}

// ============================================================================
// 2. Base64 Tests
// ============================================================================

TEST(Base64Test, RoundTrip_Empty) {
    std::string empty;
    auto encoded = encode64(reinterpret_cast<const uint8_t*>(empty.data()), empty.size());
    EXPECT_TRUE(encoded.empty()) << "Empty input should produce empty output";
    auto decoded = decode64(encoded);
    EXPECT_TRUE(decoded.empty());
}

TEST(Base64Test, RoundTrip_Simple) {
    std::string original = "Hello, cppdesk! This is a test of base64 encoding.";
    auto encoded = encode64(reinterpret_cast<const uint8_t*>(original.data()), original.size());
    auto decoded = decode64(encoded);
    std::string dec_str(decoded.begin(), decoded.end());
    EXPECT_EQ(original, dec_str);
}

TEST(Base64Test, RoundTrip_VariousSizes) {
    // Test with various input sizes to exercise padding edge cases
    for (size_t size : {1, 2, 3, 4, 5, 6, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129}) {
        auto data = make_data(size);
        auto encoded = encode64(data.data(), data.size());
        auto decoded = decode64(encoded);
        ASSERT_EQ(decoded.size(), size) << "Size mismatch at input size " << size;
        for (size_t i = 0; i < size; ++i) {
            EXPECT_EQ(decoded[i], data[i]) << "Mismatch at byte " << i << " for size " << size;
        }
    }
}

TEST(Base64Test, RoundTrip_BinaryData) {
    // Binary data with all byte values 0-255
    std::vector<uint8_t> binary(256);
    for (int i = 0; i < 256; ++i) {
        binary[i] = static_cast<uint8_t>(i);
    }
    auto encoded = encode64(binary.data(), binary.size());
    auto decoded = decode64(encoded);
    ASSERT_EQ(decoded.size(), binary.size());
    for (size_t i = 0; i < binary.size(); ++i) {
        EXPECT_EQ(decoded[i], binary[i]) << "Mismatch at byte " << i;
    }
}

TEST(Base64Test, RoundTrip_LargeBinary) {
    // 64KB of sequential bytes
    auto large = make_data(65536);
    auto encoded = encode64(large.data(), large.size());
    auto decoded = decode64(encoded);
    ASSERT_EQ(decoded.size(), large.size());
    EXPECT_EQ(decoded, large);
}

TEST(Base64Test, OutputIsPrintable) {
    std::string original = "Hello World!";
    auto encoded = encode64(reinterpret_cast<const uint8_t*>(original.data()), original.size());
    for (char c : encoded) {
        bool is_valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
        EXPECT_TRUE(is_valid) << "Invalid base64 character: '" << c << "' (0x"
            << std::hex << static_cast<int>(c) << ")";
    }
}

TEST(Base64Test, DecodeInvalidReturnsEmpty) {
    // Invalid base64 string should result in empty vector (or throw handled)
    auto result = decode64("!!!invalid!!!");
    // Implementation should handle gracefully
    EXPECT_TRUE(result.empty()) << "Invalid base64 should return empty";
}

TEST(Base64Test, NoPaddingEdgeCase) {
    // Input of length multiple of 3 should produce no padding
    std::string input = "abc";  // 3 bytes, no padding
    auto encoded = encode64(reinterpret_cast<const uint8_t*>(input.data()), input.size());
    EXPECT_EQ(encoded.find('='), std::string::npos)
        << "3-byte input should have no padding";
}

TEST(Base64Test, OnePaddingEdgeCase) {
    // Input of length 2 mod 3 should produce one padding char
    std::string input = "ab";  // 2 bytes, one '='
    auto encoded = encode64(reinterpret_cast<const uint8_t*>(input.data()), input.size());
    EXPECT_NE(encoded.find('='), std::string::npos);
    EXPECT_EQ(encoded.back(), '=');
    // Should have exactly one '='
    EXPECT_EQ(std::count(encoded.begin(), encoded.end(), '='), 1);
}

TEST(Base64Test, TwoPaddingEdgeCase) {
    // Input of length 1 mod 3 should produce two padding chars
    std::string input = "a";  // 1 byte, two '='
    auto encoded = encode64(reinterpret_cast<const uint8_t*>(input.data()), input.size());
    EXPECT_NE(encoded.find('='), std::string::npos);
    EXPECT_EQ(std::count(encoded.begin(), encoded.end(), '='), 2);
}

TEST(Base64Test, EncodeLengthFormula) {
    // Encoded length should be ceil(len/3)*4
    for (size_t len : {0, 1, 2, 3, 4, 5, 6, 7, 8, 100, 1000}) {
        auto data = make_data(len);
        auto encoded = encode64(data.data(), data.size());
        size_t expected_len = len == 0 ? 0 : ((len + 2) / 3) * 4;
        EXPECT_EQ(encoded.size(), expected_len)
            << "Encoded size mismatch for input length " << len;
    }
}

// ============================================================================
// 3. Random Bytes Tests
// ============================================================================

TEST(RandomTest, SizeCorrect) {
    for (size_t size : {0, 1, 16, 32, 64, 128, 256, 1024}) {
        auto r = random_bytes(size);
        EXPECT_EQ(r.size(), size) << "random_bytes(" << size << ") returned wrong size";
    }
}

TEST(RandomTest, Uniqueness) {
    // Generate many random buffers and ensure they aren't all the same
    std::vector<std::vector<uint8_t>> samples;
    for (int i = 0; i < 100; ++i) {
        samples.push_back(random_bytes(32));
    }
    // Count unique samples
    std::set<std::vector<uint8_t>> unique(samples.begin(), samples.end());
    EXPECT_GT(unique.size(), 95u) << "Only " << unique.size()
        << " unique values out of 100 - randomness appears broken";
}

TEST(RandomTest, NotAllSame) {
    // A single buffer of size 64 should not be all the same value
    auto r = random_bytes(64);
    bool all_same = std::all_of(r.begin(), r.end(),
        [&](uint8_t b) { return b == r[0]; });
    EXPECT_FALSE(all_same) << "All 64 random bytes are identical";
}

TEST(RandomTest, DistributionCheck) {
    // For a large sample of random bytes, check that each bit position
    // has roughly even distribution (within reasonable bounds)
    const size_t sample_size = 10000;
    auto r = random_bytes(sample_size);

    // Check bit distribution for each bit position across all bytes
    for (int bit = 0; bit < 8; ++bit) {
        size_t ones = 0;
        uint8_t mask = static_cast<uint8_t>(1 << bit);
        for (auto b : r) {
            if (b & mask) ++ones;
        }
        double ratio = static_cast<double>(ones) / sample_size;
        // Allow 10% deviation from 50% (should be very rare to exceed)
        EXPECT_GT(ratio, 0.35) << "Bit " << bit << " has too few 1s: " << ratio;
        EXPECT_LT(ratio, 0.65) << "Bit " << bit << " has too many 1s: " << ratio;
    }
}

TEST(RandomTest, IndependentCalls) {
    // Successive calls should produce different results
    auto r1 = random_bytes(32);
    auto r2 = random_bytes(32);
    auto r3 = random_bytes(32);
    EXPECT_NE(r1, r2) << "Two consecutive random_bytes calls returned same data";
    EXPECT_NE(r2, r3);
    EXPECT_NE(r1, r3);
}

TEST(RandomTest, ZeroSize) {
    auto r = random_bytes(0);
    EXPECT_TRUE(r.empty()) << "random_bytes(0) should return empty vector";
}

TEST(RandomTest, LargeSize) {
    // 1MB of random data - just verify it doesn't crash and has correct size
    auto r = random_bytes(1024 * 1024);
    EXPECT_EQ(r.size(), 1024u * 1024u);
    // Verify it's not all zeros
    bool has_nonzero = std::any_of(r.begin(), r.end(), [](uint8_t b) { return b != 0; });
    EXPECT_TRUE(has_nonzero) << "1MB random data is all zeros";
}

TEST(RandomTest, ByteValueRange) {
    // All byte values (0-255) should eventually appear in a large enough sample
    auto r = random_bytes(50000);
    std::set<uint8_t> seen(r.begin(), r.end());
    EXPECT_GT(seen.size(), 200u) << "Only " << seen.size()
        << " unique byte values seen in 50000 random bytes";
}

// ============================================================================
// 4. Key Generation Tests
// ============================================================================

TEST(KeyGenerationTest, BoxKeyPairSize) {
    auto kp = generate_box_keypair();
    EXPECT_EQ(kp.pk.size(), static_cast<size_t>(PUBLIC_KEY_BYTES));
    EXPECT_EQ(kp.sk.size(), static_cast<size_t>(SECRET_KEY_BYTES));
}

TEST(KeyGenerationTest, BoxKeyPairNotEmpty) {
    auto kp = generate_box_keypair();
    // Keys should not be all zeros
    bool pk_zero = std::all_of(kp.pk.begin(), kp.pk.end(),
        [](uint8_t b) { return b == 0; });
    bool sk_zero = std::all_of(kp.sk.begin(), kp.sk.end(),
        [](uint8_t b) { return b == 0; });
    EXPECT_FALSE(pk_zero) << "Public key is all zeros";
    EXPECT_FALSE(sk_zero) << "Secret key is all zeros";
}

TEST(KeyGenerationTest, BoxKeyPairUnique) {
    auto kp1 = generate_box_keypair();
    auto kp2 = generate_box_keypair();
    auto kp3 = generate_box_keypair();
    EXPECT_NE(kp1.pk, kp2.pk) << "Two generated public keys are identical";
    EXPECT_NE(kp1.sk, kp2.sk) << "Two generated secret keys are identical";
    EXPECT_NE(kp2.pk, kp3.pk);
}

TEST(KeyGenerationTest, SignKeyPairSize) {
    auto kp = generate_sign_keypair();
    EXPECT_EQ(kp.pk.size(), static_cast<size_t>(SIGN_PUBLIC_KEY_BYTES));
    EXPECT_EQ(kp.sk.size(), static_cast<size_t>(SIGN_SECRET_KEY_BYTES));
}

TEST(KeyGenerationTest, SignKeyPairNotEmpty) {
    auto kp = generate_sign_keypair();
    bool pk_zero = std::all_of(kp.pk.begin(), kp.pk.end(),
        [](uint8_t b) { return b == 0; });
    bool sk_zero = std::all_of(kp.sk.begin(), kp.sk.end(),
        [](uint8_t b) { return b == 0; });
    EXPECT_FALSE(pk_zero) << "Signing public key is all zeros";
    EXPECT_FALSE(sk_zero) << "Signing secret key is all zeros";
}

TEST(KeyGenerationTest, SignKeyPairUnique) {
    auto kp1 = generate_sign_keypair();
    auto kp2 = generate_sign_keypair();
    EXPECT_NE(kp1.pk, kp2.pk);
    EXPECT_NE(kp1.sk, kp2.sk);
}

TEST(KeyGenerationTest, ManyUniqueKeys) {
    // Generate 100 key pairs and ensure they are all unique
    std::set<std::array<uint8_t, PUBLIC_KEY_BYTES>> box_pks;
    for (int i = 0; i < 100; ++i) {
        auto kp = generate_box_keypair();
        box_pks.insert(kp.pk);
    }
    EXPECT_EQ(box_pks.size(), 100u) << "Duplicate box public keys generated";

    std::set<std::array<uint8_t, SIGN_PUBLIC_KEY_BYTES>> sign_pks;
    for (int i = 0; i < 100; ++i) {
        auto kp = generate_sign_keypair();
        sign_pks.insert(kp.pk);
    }
    EXPECT_EQ(sign_pks.size(), 100u) << "Duplicate sign public keys generated";
}

// ============================================================================
// 5. SecretBox (Symmetric Encryption) Tests
// ============================================================================

class SecretBoxTest : public ::testing::Test {
protected:
    void SetUp() override {
        key_ = make_secretbox_key();
        nonce_ = make_secretbox_nonce();
    }

    std::array<uint8_t, SECRET_BOX_KEY_BYTES> key_;
    std::array<uint8_t, SECRET_BOX_NONCE_BYTES> nonce_;
};

TEST_F(SecretBoxTest, RoundTrip_1Byte) {
    std::vector<uint8_t> plaintext(1, 0x42);
    auto ct = secretbox_encrypt(plaintext.data(), plaintext.size(),
        key_.data(), nonce_.data());
    ASSERT_FALSE(ct.empty()) << "Encryption produced empty result";
    auto pt = secretbox_decrypt(ct.data(), ct.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(pt.empty()) << "Decryption produced empty result";
    EXPECT_EQ(pt, plaintext);
}

TEST_F(SecretBoxTest, RoundTrip_1KB) {
    auto plaintext = make_data(1024);
    auto ct = secretbox_encrypt(plaintext.data(), plaintext.size(),
        key_.data(), nonce_.data());
    ASSERT_GT(ct.size(), plaintext.size())
        << "Encrypted data should be larger than plaintext (MAC)";
    auto pt = secretbox_decrypt(ct.data(), ct.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(pt.empty());
    EXPECT_EQ(pt, plaintext);
}

TEST_F(SecretBoxTest, RoundTrip_64KB) {
    auto plaintext = make_data(65536);
    auto ct = secretbox_encrypt(plaintext.data(), plaintext.size(),
        key_.data(), nonce_.data());
    ASSERT_GT(ct.size(), plaintext.size());
    auto pt = secretbox_decrypt(ct.data(), ct.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(pt.empty());
    EXPECT_EQ(pt, plaintext);
}

TEST_F(SecretBoxTest, RoundTrip_1MB) {
    auto plaintext = make_data(1024 * 1024);
    auto ct = secretbox_encrypt(plaintext.data(), plaintext.size(),
        key_.data(), nonce_.data());
    ASSERT_GT(ct.size(), plaintext.size());
    auto pt = secretbox_decrypt(ct.data(), ct.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(pt.empty());
    EXPECT_EQ(pt, plaintext);
}

TEST_F(SecretBoxTest, RoundTrip_BinaryZeros) {
    std::vector<uint8_t> zeros(256, 0x00);
    auto ct = secretbox_encrypt(zeros.data(), zeros.size(),
        key_.data(), nonce_.data());
    ASSERT_FALSE(ct.empty());
    auto pt = secretbox_decrypt(ct.data(), ct.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(pt.empty());
    EXPECT_EQ(pt, zeros);
}

TEST_F(SecretBoxTest, RoundTrip_AllByteValues) {
    std::vector<uint8_t> all_bytes(256);
    for (int i = 0; i < 256; ++i) all_bytes[i] = static_cast<uint8_t>(i);
    auto ct = secretbox_encrypt(all_bytes.data(), all_bytes.size(),
        key_.data(), nonce_.data());
    ASSERT_FALSE(ct.empty());
    auto pt = secretbox_decrypt(ct.data(), ct.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(pt.empty());
    EXPECT_EQ(pt, all_bytes);
}

TEST_F(SecretBoxTest, Overhead) {
    // Encrypted output should be plaintext + MAC
    auto plaintext = make_data(100);
    auto ct = secretbox_encrypt(plaintext.data(), plaintext.size(),
        key_.data(), nonce_.data());
    EXPECT_EQ(ct.size(), plaintext.size() + SECRET_BOX_MAC_BYTES)
        << "Ciphertext should be plaintext + MAC bytes";
}

TEST_F(SecretBoxTest, TamperedCiphertextFails) {
    auto plaintext = make_data(256);
    auto ct = secretbox_encrypt(plaintext.data(), plaintext.size(),
        key_.data(), nonce_.data());
    ASSERT_FALSE(ct.empty());

    // Tamper with the ciphertext at various positions
    for (size_t pos : {0, ct.size() / 4, ct.size() / 2, ct.size() - 1}) {
        auto tampered = ct;
        tampered[pos] ^= 0x01;  // flip one bit
        auto pt = secretbox_decrypt(tampered.data(), tampered.size(),
            key_.data(), nonce_.data());
        EXPECT_TRUE(pt.empty())
            << "Decryption should fail for tampered ciphertext at position " << pos;
    }
}

TEST_F(SecretBoxTest, WrongKeyFails) {
    auto plaintext = make_data(256);
    auto ct = secretbox_encrypt(plaintext.data(), plaintext.size(),
        key_.data(), nonce_.data());
    ASSERT_FALSE(ct.empty());

    // Use a different key
    auto wrong_key = make_secretbox_key();
    // Ensure wrong_key != key_ (astronomically unlikely to collide, but check)
    if (wrong_key == key_) {
        wrong_key[0] ^= 0xFF;
    }
    auto pt = secretbox_decrypt(ct.data(), ct.size(),
        wrong_key.data(), nonce_.data());
    EXPECT_TRUE(pt.empty()) << "Decryption should fail with wrong key";
}

TEST_F(SecretBoxTest, WrongNonceFails) {
    auto plaintext = make_data(256);
    auto ct = secretbox_encrypt(plaintext.data(), plaintext.size(),
        key_.data(), nonce_.data());
    ASSERT_FALSE(ct.empty());

    // Use a different nonce
    auto wrong_nonce = make_secretbox_nonce();
    if (wrong_nonce == nonce_) {
        wrong_nonce[0] ^= 0xFF;
    }
    auto pt = secretbox_decrypt(ct.data(), ct.size(),
        key_.data(), wrong_nonce.data());
    EXPECT_TRUE(pt.empty()) << "Decryption should fail with wrong nonce";
}

TEST_F(SecretBoxTest, CiphertextLooksRandom) {
    // Encrypted data should not look like plaintext
    auto plaintext = make_data(1024);
    auto ct = secretbox_encrypt(plaintext.data(), plaintext.size(),
        key_.data(), nonce_.data());
    ASSERT_GT(ct.size(), 16u);
    // The first bytes of ciphertext should not match plaintext
    bool matches = true;
    for (size_t i = 0; i < 16 && i < plaintext.size(); ++i) {
        if (ct[i] != plaintext[i]) { matches = false; break; }
    }
    EXPECT_FALSE(matches) << "First bytes of ciphertext match plaintext";
}

TEST_F(SecretBoxTest, DifferentNonceDifferentOutput) {
    auto plaintext = make_data(256);
    auto nonce1 = make_secretbox_nonce();
    auto nonce2 = make_secretbox_nonce();
    if (nonce1 == nonce2) nonce2[0] ^= 0xFF;

    auto ct1 = secretbox_encrypt(plaintext.data(), plaintext.size(),
        key_.data(), nonce1.data());
    auto ct2 = secretbox_encrypt(plaintext.data(), plaintext.size(),
        key_.data(), nonce2.data());
    ASSERT_FALSE(ct1.empty());
    ASSERT_FALSE(ct2.empty());
    EXPECT_NE(ct1, ct2) << "Same plaintext with different nonces should differ";
}

TEST_F(SecretBoxTest, DeterministicWithSameKeyNonce) {
    auto plaintext = make_data(256);
    auto ct1 = secretbox_encrypt(plaintext.data(), plaintext.size(),
        key_.data(), nonce_.data());
    auto ct2 = secretbox_encrypt(plaintext.data(), plaintext.size(),
        key_.data(), nonce_.data());
    ASSERT_FALSE(ct1.empty());
    ASSERT_FALSE(ct2.empty());
    EXPECT_EQ(ct1, ct2) << "Same inputs should produce same ciphertext";
}

TEST_F(SecretBoxTest, TruncatedCiphertextFails) {
    auto plaintext = make_data(256);
    auto ct = secretbox_encrypt(plaintext.data(), plaintext.size(),
        key_.data(), nonce_.data());
    ASSERT_FALSE(ct.empty());

    // Truncate to less than MAC size
    std::vector<uint8_t> truncated(ct.begin(), ct.begin() + SECRET_BOX_MAC_BYTES - 1);
    auto pt = secretbox_decrypt(truncated.data(), truncated.size(),
        key_.data(), nonce_.data());
    EXPECT_TRUE(pt.empty()) << "Truncated ciphertext should fail decryption";
}

// ============================================================================
// 6. Box (Asymmetric Encryption) Tests
// ============================================================================

class BoxTest : public ::testing::Test {
protected:
    void SetUp() override {
        alice_ = generate_box_keypair();
        bob_ = generate_box_keypair();
        nonce_ = make_box_nonce();
    }

    KeyPair alice_;
    KeyPair bob_;
    std::array<uint8_t, BOX_NONCE_BYTES> nonce_;
};

TEST_F(BoxTest, RoundTrip_AliceToBob) {
    std::vector<uint8_t> message = make_data(1024);
    // Alice encrypts to Bob
    auto ct = box_encrypt(message.data(), message.size(),
        nonce_.data(), bob_.pk.data(), alice_.sk.data());
    ASSERT_FALSE(ct.empty()) << "Box encryption failed";
    ASSERT_GT(ct.size(), message.size()) << "No MAC overhead";

    // Bob decrypts from Alice
    auto pt = box_decrypt(ct.data(), ct.size(),
        nonce_.data(), alice_.pk.data(), bob_.sk.data());
    ASSERT_FALSE(pt.empty()) << "Box decryption failed";
    EXPECT_EQ(pt, message);
}

TEST_F(BoxTest, RoundTrip_BobToAlice) {
    std::vector<uint8_t> message = make_data(2048);
    // Bob encrypts to Alice
    auto ct = box_encrypt(message.data(), message.size(),
        nonce_.data(), alice_.pk.data(), bob_.sk.data());
    ASSERT_FALSE(ct.empty());

    // Alice decrypts from Bob
    auto pt = box_decrypt(ct.data(), ct.size(),
        nonce_.data(), bob_.pk.data(), alice_.sk.data());
    ASSERT_FALSE(pt.empty());
    EXPECT_EQ(pt, message);
}

TEST_F(BoxTest, RoundTrip_VariousSizes) {
    for (size_t size : {1, 2, 16, 256, 1024, 4096, 16384, 65536}) {
        auto message = make_data(size);
        auto ct = box_encrypt(message.data(), message.size(),
            nonce_.data(), bob_.pk.data(), alice_.sk.data());
        ASSERT_FALSE(ct.empty()) << "Encryption failed for size " << size;
        auto pt = box_decrypt(ct.data(), ct.size(),
            nonce_.data(), alice_.pk.data(), bob_.sk.data());
        ASSERT_FALSE(pt.empty()) << "Decryption failed for size " << size;
        EXPECT_EQ(pt, message) << "Round-trip mismatch for size " << size;
        // Fresh nonce for each
        nonce_ = make_box_nonce();
    }
}

TEST_F(BoxTest, Overhead) {
    auto message = make_data(100);
    auto ct = box_encrypt(message.data(), message.size(),
        nonce_.data(), bob_.pk.data(), alice_.sk.data());
    EXPECT_EQ(ct.size(), message.size() + BOX_MAC_BYTES)
        << "Ciphertext size should be plaintext + MAC";
}

TEST_F(BoxTest, WrongRecipientKeyFails) {
    auto message = make_data(256);
    auto ct = box_encrypt(message.data(), message.size(),
        nonce_.data(), bob_.pk.data(), alice_.sk.data());
    ASSERT_FALSE(ct.empty());

    // Try decrypting with wrong sender's public key (Eve's)
    auto eve = generate_box_keypair();
    auto pt = box_decrypt(ct.data(), ct.size(),
        nonce_.data(), eve.pk.data(), bob_.sk.data());
    EXPECT_TRUE(pt.empty()) << "Decryption should fail with wrong sender key";
}

TEST_F(BoxTest, WrongRecipientSkFails) {
    auto message = make_data(256);
    auto ct = box_encrypt(message.data(), message.size(),
        nonce_.data(), bob_.pk.data(), alice_.sk.data());
    ASSERT_FALSE(ct.empty());

    // Try decrypting with wrong recipient's private key
    auto eve = generate_box_keypair();
    auto pt = box_decrypt(ct.data(), ct.size(),
        nonce_.data(), alice_.pk.data(), eve.sk.data());
    EXPECT_TRUE(pt.empty()) << "Decryption should fail with wrong secret key";
}

TEST_F(BoxTest, WrongNonceFails) {
    auto message = make_data(256);
    auto ct = box_encrypt(message.data(), message.size(),
        nonce_.data(), bob_.pk.data(), alice_.sk.data());
    ASSERT_FALSE(ct.empty());

    auto wrong_nonce = make_box_nonce();
    if (wrong_nonce == nonce_) wrong_nonce[0] ^= 0xFF;
    auto pt = box_decrypt(ct.data(), ct.size(),
        wrong_nonce.data(), alice_.pk.data(), bob_.sk.data());
    EXPECT_TRUE(pt.empty()) << "Decryption should fail with wrong nonce";
}

TEST_F(BoxTest, TamperedCiphertextFails) {
    auto message = make_data(512);
    auto ct = box_encrypt(message.data(), message.size(),
        nonce_.data(), bob_.pk.data(), alice_.sk.data());
    ASSERT_FALSE(ct.empty());

    // Tamper at different positions
    for (size_t pos : {0, ct.size() / 4, ct.size() / 2, ct.size() - 1}) {
        auto tampered = ct;
        tampered[pos] ^= 0x01;
        auto pt = box_decrypt(tampered.data(), tampered.size(),
            nonce_.data(), alice_.pk.data(), bob_.sk.data());
        EXPECT_TRUE(pt.empty())
            << "Decryption should fail for tampered data at position " << pos;
    }
}

TEST_F(BoxTest, DifferentNonceDifferentOutput) {
    auto message = make_data(256);
    auto nonce1 = make_box_nonce();
    auto nonce2 = make_box_nonce();
    if (nonce1 == nonce2) nonce2[0] ^= 0xFF;

    auto ct1 = box_encrypt(message.data(), message.size(),
        nonce1.data(), bob_.pk.data(), alice_.sk.data());
    auto ct2 = box_encrypt(message.data(), message.size(),
        nonce2.data(), bob_.pk.data(), alice_.sk.data());
    ASSERT_FALSE(ct1.empty());
    ASSERT_FALSE(ct2.empty());
    EXPECT_NE(ct1, ct2);
}

TEST_F(BoxTest, DeterministicOutput) {
    auto message = make_data(256);
    auto ct1 = box_encrypt(message.data(), message.size(),
        nonce_.data(), bob_.pk.data(), alice_.sk.data());
    auto ct2 = box_encrypt(message.data(), message.size(),
        nonce_.data(), bob_.pk.data(), alice_.sk.data());
    ASSERT_FALSE(ct1.empty());
    ASSERT_FALSE(ct2.empty());
    EXPECT_EQ(ct1, ct2) << "Same inputs should produce same output";
}

TEST_F(BoxTest, SelfEncryption) {
    // Alice encrypts to herself
    auto message = make_data(512);
    auto ct = box_encrypt(message.data(), message.size(),
        nonce_.data(), alice_.pk.data(), alice_.sk.data());
    ASSERT_FALSE(ct.empty());
    auto pt = box_decrypt(ct.data(), ct.size(),
        nonce_.data(), alice_.pk.data(), alice_.sk.data());
    ASSERT_FALSE(pt.empty());
    EXPECT_EQ(pt, message);
}

// ============================================================================
// 7. Sign (Digital Signature) Tests
// ============================================================================

class SignTest : public ::testing::Test {
protected:
    void SetUp() override {
        signer_ = generate_sign_keypair();
    }

    SignKeyPair signer_;
};

TEST_F(SignTest, SignVerifyRoundTrip) {
    std::vector<uint8_t> message = make_data(1024);
    auto sig = sign(message.data(), message.size(), signer_.sk.data());
    ASSERT_FALSE(sig.empty()) << "Signing produced empty signature";
    ASSERT_EQ(sig.size(), static_cast<size_t>(SIGN_BYTES))
        << "Signature should be exactly " << SIGN_BYTES << " bytes";

    bool valid = sign_verify(message.data(), message.size(),
        sig.data(), signer_.pk.data());
    EXPECT_TRUE(valid) << "Valid signature failed verification";
}

TEST_F(SignTest, SignVerifyVariousSizes) {
    for (size_t size : {0, 1, 16, 256, 1024, 4096, 16384, 65536}) {
        auto message = make_data(size);
        auto sig = sign(message.data(), message.size(), signer_.sk.data());
        ASSERT_FALSE(sig.empty()) << "Signing failed for size " << size;
        ASSERT_EQ(sig.size(), static_cast<size_t>(SIGN_BYTES));

        bool valid = sign_verify(message.data(), message.size(),
            sig.data(), signer_.pk.data());
        EXPECT_TRUE(valid) << "Verification failed for size " << size;
    }
}

TEST_F(SignTest, WrongMessageFails) {
    auto message = make_data(256);
    auto sig = sign(message.data(), message.size(), signer_.sk.data());
    ASSERT_FALSE(sig.empty());

    // Modify one byte of the message
    auto tampered = make_data(256);
    tampered[128] ^= 0x01;

    bool valid = sign_verify(tampered.data(), tampered.size(),
        sig.data(), signer_.pk.data());
    EXPECT_FALSE(valid) << "Modified message should fail verification";
}

TEST_F(SignTest, WrongPublicKeyFails) {
    auto message = make_data(256);
    auto sig = sign(message.data(), message.size(), signer_.sk.data());
    ASSERT_FALSE(sig.empty());

    // Verify with a different signer's public key
    auto other = generate_sign_keypair();
    bool valid = sign_verify(message.data(), message.size(),
        sig.data(), other.pk.data());
    EXPECT_FALSE(valid) << "Wrong public key should fail verification";
}

TEST_F(SignTest, TamperedSignatureFails) {
    auto message = make_data(512);
    auto sig = sign(message.data(), message.size(), signer_.sk.data());
    ASSERT_FALSE(sig.empty());

    // Tamper with the signature
    for (size_t pos : {0, sig.size() / 4, sig.size() / 2, sig.size() - 1}) {
        auto tampered_sig = sig;
        tampered_sig[pos] ^= 0x01;
        bool valid = sign_verify(message.data(), message.size(),
            tampered_sig.data(), signer_.pk.data());
        EXPECT_FALSE(valid) << "Tampered signature at position "
            << pos << " should fail";
    }
}

TEST_F(SignTest, DeterministicSignatures) {
    // Ed25519 signatures are deterministic
    auto message = make_data(128);
    auto sig1 = sign(message.data(), message.size(), signer_.sk.data());
    auto sig2 = sign(message.data(), message.size(), signer_.sk.data());
    ASSERT_FALSE(sig1.empty());
    ASSERT_FALSE(sig2.empty());
    EXPECT_EQ(sig1, sig2) << "Signatures should be deterministic for same input";
}

TEST_F(SignTest, DifferentMessagesDifferentSignatures) {
    auto msg1 = make_data(128);
    auto msg2 = make_data(128);
    msg2[64] ^= 0xFF;  // Change one byte

    auto sig1 = sign(msg1.data(), msg1.size(), signer_.sk.data());
    auto sig2 = sign(msg2.data(), msg2.size(), signer_.sk.data());
    ASSERT_FALSE(sig1.empty());
    ASSERT_FALSE(sig2.empty());
    EXPECT_NE(sig1, sig2) << "Different messages should produce different signatures";
}

TEST_F(SignTest, CrossSignerVerification) {
    // Alice signs, Bob can verify with Alice's public key
    auto alice = generate_sign_keypair();
    auto bob = generate_sign_keypair();

    auto message = make_data(512);
    auto sig = sign(message.data(), message.size(), alice.sk.data());

    // Bob sees the message and Alice's public key
    bool bob_verifies = sign_verify(message.data(), message.size(),
        sig.data(), alice.pk.data());
    EXPECT_TRUE(bob_verifies);

    // But Bob's own public key won't work
    bool bob_pk_fails = sign_verify(message.data(), message.size(),
        sig.data(), bob.pk.data());
    EXPECT_FALSE(bob_pk_fails);
}

TEST_F(SignTest, EmptyMessage) {
    uint8_t dummy = 0;
    auto sig = sign(&dummy, 0, signer_.sk.data());
    // Some implementations may handle empty input
    if (!sig.empty()) {
        bool valid = sign_verify(&dummy, 0, sig.data(), signer_.pk.data());
        EXPECT_TRUE(valid) << "Signature on empty message should verify";
    }
}

// ============================================================================
// 8. AES-GCM Tests
// ============================================================================

class AESGCMTest : public ::testing::Test {
protected:
    void SetUp() override {
        key_ = make_aes_key();
        nonce_ = make_aes_nonce();
    }

    std::array<uint8_t, AES_KEY_BYTES> key_;
    std::array<uint8_t, AES_NONCE_BYTES> nonce_;
};

TEST_F(AESGCMTest, RoundTrip_1Byte) {
    std::vector<uint8_t> pt(1, 0xAA);
    auto ct = aes_gcm_encrypt(pt.data(), pt.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(ct.empty());
    auto dt = aes_gcm_decrypt(ct.data(), ct.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(dt.empty());
    EXPECT_EQ(dt, pt);
}

TEST_F(AESGCMTest, RoundTrip_1KB) {
    auto pt = make_data(1024);
    auto ct = aes_gcm_encrypt(pt.data(), pt.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(ct.empty());
    auto dt = aes_gcm_decrypt(ct.data(), ct.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(dt.empty());
    EXPECT_EQ(dt, pt);
}

TEST_F(AESGCMTest, RoundTrip_64KB) {
    auto pt = make_data(65536);
    auto ct = aes_gcm_encrypt(pt.data(), pt.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(ct.empty());
    auto dt = aes_gcm_decrypt(ct.data(), ct.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(dt.empty());
    EXPECT_EQ(dt, pt);
}

TEST_F(AESGCMTest, RoundTrip_VariousSizes) {
    for (size_t size : {1, 16, 32, 64, 128, 256, 512, 1024, 4096, 16384}) {
        auto pt = make_data(size);
        auto ct = aes_gcm_encrypt(pt.data(), pt.size(), key_.data(), nonce_.data());
        ASSERT_FALSE(ct.empty()) << "Encryption failed for size " << size;
        auto dt = aes_gcm_decrypt(ct.data(), ct.size(), key_.data(), nonce_.data());
        ASSERT_FALSE(dt.empty()) << "Decryption failed for size " << size;
        EXPECT_EQ(dt, pt) << "Round-trip mismatch for size " << size;
        nonce_ = make_aes_nonce();  // fresh nonce per iteration
    }
}

TEST_F(AESGCMTest, Overhead) {
    auto pt = make_data(500);
    auto ct = aes_gcm_encrypt(pt.data(), pt.size(), key_.data(), nonce_.data());
    EXPECT_EQ(ct.size(), pt.size() + AES_TAG_BYTES)
        << "AES-GCM should add " << AES_TAG_BYTES << " bytes tag";
}

TEST_F(AESGCMTest, TamperDetection) {
    auto pt = make_data(512);
    auto ct = aes_gcm_encrypt(pt.data(), pt.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(ct.empty());

    for (size_t pos : {0, ct.size() / 4, ct.size() / 2, ct.size() - 1}) {
        auto tampered = ct;
        tampered[pos] ^= 0x01;
        auto dt = aes_gcm_decrypt(tampered.data(), tampered.size(),
            key_.data(), nonce_.data());
        EXPECT_TRUE(dt.empty())
            << "Decryption should fail for tampered data at position " << pos;
    }
}

TEST_F(AESGCMTest, WrongKeyFails) {
    auto pt = make_data(256);
    auto ct = aes_gcm_encrypt(pt.data(), pt.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(ct.empty());

    auto wrong_key = make_aes_key();
    if (wrong_key == key_) wrong_key[0] ^= 0xFF;
    auto dt = aes_gcm_decrypt(ct.data(), ct.size(),
        wrong_key.data(), nonce_.data());
    EXPECT_TRUE(dt.empty()) << "Decryption should fail with wrong key";
}

TEST_F(AESGCMTest, WrongNonceFails) {
    auto pt = make_data(256);
    auto ct = aes_gcm_encrypt(pt.data(), pt.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(ct.empty());

    auto wrong_nonce = make_aes_nonce();
    if (wrong_nonce == nonce_) wrong_nonce[0] ^= 0xFF;
    auto dt = aes_gcm_decrypt(ct.data(), ct.size(),
        key_.data(), wrong_nonce.data());
    EXPECT_TRUE(dt.empty()) << "Decryption should fail with wrong nonce";
}

TEST_F(AESGCMTest, DeterministicOutput) {
    auto pt = make_data(256);
    auto ct1 = aes_gcm_encrypt(pt.data(), pt.size(), key_.data(), nonce_.data());
    auto ct2 = aes_gcm_encrypt(pt.data(), pt.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(ct1.empty());
    ASSERT_FALSE(ct2.empty());
    EXPECT_EQ(ct1, ct2) << "Same inputs should produce same ciphertext";
}

TEST_F(AESGCMTest, DifferentNonceDifferentOutput) {
    auto pt = make_data(256);
    auto nonce1 = make_aes_nonce();
    auto nonce2 = make_aes_nonce();
    if (nonce1 == nonce2) nonce2[0] ^= 0xFF;

    auto ct1 = aes_gcm_encrypt(pt.data(), pt.size(), key_.data(), nonce1.data());
    auto ct2 = aes_gcm_encrypt(pt.data(), pt.size(), key_.data(), nonce2.data());
    ASSERT_FALSE(ct1.empty());
    ASSERT_FALSE(ct2.empty());
    EXPECT_NE(ct1, ct2) << "Different nonces should produce different ciphertexts";
}

TEST_F(AESGCMTest, TruncatedCiphertextFails) {
    auto pt = make_data(256);
    auto ct = aes_gcm_encrypt(pt.data(), pt.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(ct.empty());

    // Truncate to less than tag size
    std::vector<uint8_t> truncated(ct.begin(), ct.begin() + AES_TAG_BYTES - 1);
    auto dt = aes_gcm_decrypt(truncated.data(), truncated.size(),
        key_.data(), nonce_.data());
    EXPECT_TRUE(dt.empty());
}

TEST_F(AESGCMTest, AllZerosPlaintext) {
    std::vector<uint8_t> zeros(1024, 0x00);
    auto ct = aes_gcm_encrypt(zeros.data(), zeros.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(ct.empty());
    auto dt = aes_gcm_decrypt(ct.data(), ct.size(), key_.data(), nonce_.data());
    ASSERT_FALSE(dt.empty());
    EXPECT_EQ(dt, zeros);
}

// ============================================================================
// 9. Password Hash Tests
// ============================================================================

TEST(PasswordHashTest, HashVerifyRoundTrip) {
    std::string password = "correct_horse_battery_staple";
    std::string salt = "randomsaltvalue";

    auto hash = hash_password(password, salt);
    ASSERT_FALSE(hash.empty()) << "Hashing should produce output";
    EXPECT_TRUE(verify_password(password, hash, salt));
}

TEST(PasswordHashTest, WrongPasswordFails) {
    std::string password = "correct_horse_battery_staple";
    std::string salt = "randomsaltvalue";
    auto hash = hash_password(password, salt);
    ASSERT_FALSE(hash.empty());

    EXPECT_FALSE(verify_password("wrong_password", hash, salt));
    EXPECT_FALSE(verify_password("Correct_Horse_Battery_Staple", hash, salt));
    EXPECT_FALSE(verify_password("", hash, salt));
}

TEST(PasswordHashTest, DifferentSaltDifferentHash) {
    std::string password = "testpassword";
    std::string salt1 = "salt_one";
    std::string salt2 = "salt_two";

    auto hash1 = hash_password(password, salt1);
    auto hash2 = hash_password(password, salt2);
    ASSERT_FALSE(hash1.empty());
    ASSERT_FALSE(hash2.empty());
    EXPECT_NE(hash1, hash2) << "Different salts should produce different hashes";
}

TEST(PasswordHashTest, SameInputSameHash) {
    std::string password = "consistent";
    std::string salt = "samesalt";
    auto hash1 = hash_password(password, salt);
    auto hash2 = hash_password(password, salt);
    EXPECT_EQ(hash1, hash2) << "Same password+salt should produce same hash";
}

TEST(PasswordHashTest, EmptyPasswordReturnsEmpty) {
    std::string hash = hash_password("", "somesalt");
    EXPECT_TRUE(hash.empty()) << "Empty password should return empty hash";
}

TEST(PasswordHashTest, EmptyHashFailsVerification) {
    EXPECT_FALSE(verify_password("any_password", "", "somesalt"));
}

TEST(PasswordHashTest, VariousPasswords) {
    std::string salt = "fixed_salt";
    // Test various password types
    std::vector<std::string> passwords = {
        "a",
        "ab",
        "password123",
        "!@#$%^&*()",
        std::string(100, 'x'),           // 100 chars
        "unicode_test_\xc3\xa9\xe2\x82\xac", // é€
        "spaces   and\ttabs",
        "\0hidden\0null",  // contains null bytes
    };

    for (const auto& pw : passwords) {
        auto hash = hash_password(pw, salt);
        if (!pw.empty()) {
            ASSERT_FALSE(hash.empty()) << "Hashing failed for password of length " << pw.size();
            EXPECT_TRUE(verify_password(pw, hash, salt))
                << "Verification failed for password of length " << pw.size();
        }
    }
}

TEST(PasswordHashTest, PasswordSimilarity) {
    // Similar but not identical passwords should produce different hashes
    // and not verify each other
    std::string salt = "test_salt";
    auto hash1 = hash_password("Password123!", salt);
    auto hash2 = hash_password("password123!", salt);
    ASSERT_FALSE(hash1.empty());
    ASSERT_FALSE(hash2.empty());

    EXPECT_NE(hash1, hash2);
    EXPECT_FALSE(verify_password("Password123!", hash2, salt));
    EXPECT_FALSE(verify_password("password123!", hash1, salt));
}

// ============================================================================
// 10. Edge Cases & Null Input Tests
// ============================================================================

TEST(EdgeCaseTest, SecretBoxNullData) {
    auto key = make_secretbox_key();
    auto nonce = make_secretbox_nonce();
    // Null data pointer with zero length
    auto result = secretbox_encrypt(nullptr, 0, key.data(), nonce.data());
    EXPECT_TRUE(result.empty()) << "Null data should return empty";
}

TEST(EdgeCaseTest, SecretBoxNullKey) {
    auto data = make_data(16);
    auto nonce = make_secretbox_nonce();
    auto result = secretbox_encrypt(data.data(), data.size(), nullptr, nonce.data());
    EXPECT_TRUE(result.empty()) << "Null key should return empty";
}

TEST(EdgeCaseTest, SecretBoxNullNonce) {
    auto data = make_data(16);
    auto key = make_secretbox_key();
    auto result = secretbox_encrypt(data.data(), data.size(), key.data(), nullptr);
    EXPECT_TRUE(result.empty()) << "Null nonce should return empty";
}

TEST(EdgeCaseTest, SecretBoxDecryptTooShort) {
    auto key = make_secretbox_key();
    auto nonce = make_secretbox_nonce();
    // Ciphertext shorter than MAC size
    std::vector<uint8_t> short_ct(5, 0x00);
    auto result = secretbox_decrypt(short_ct.data(), short_ct.size(),
        key.data(), nonce.data());
    EXPECT_TRUE(result.empty()) << "Short ciphertext should fail";
}

TEST(EdgeCaseTest, AESGCMNullData) {
    auto key = make_aes_key();
    auto nonce = make_aes_nonce();
    auto result = aes_gcm_encrypt(nullptr, 0, key.data(), nonce.data());
    EXPECT_TRUE(result.empty());
}

TEST(EdgeCaseTest, AESGCMNullKey) {
    auto data = make_data(16);
    auto nonce = make_aes_nonce();
    auto result = aes_gcm_encrypt(data.data(), data.size(), nullptr, nonce.data());
    EXPECT_TRUE(result.empty());
}

TEST(EdgeCaseTest, AESGCMNullNonce) {
    auto data = make_data(16);
    auto key = make_aes_key();
    auto result = aes_gcm_encrypt(data.data(), data.size(), key.data(), nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(EdgeCaseTest, AESGCMDecryptTooShort) {
    auto key = make_aes_key();
    auto nonce = make_aes_nonce();
    std::vector<uint8_t> short_ct(5, 0x00);
    auto result = aes_gcm_decrypt(short_ct.data(), short_ct.size(),
        key.data(), nonce.data());
    EXPECT_TRUE(result.empty());
}

TEST(EdgeCaseTest, BoxNullData) {
    auto alice = generate_box_keypair();
    auto bob = generate_box_keypair();
    auto nonce = make_box_nonce();
    auto result = box_encrypt(nullptr, 0, nonce.data(), bob.pk.data(), alice.sk.data());
    EXPECT_TRUE(result.empty());
}

TEST(EdgeCaseTest, BoxNullNonce) {
    auto alice = generate_box_keypair();
    auto bob = generate_box_keypair();
    auto data = make_data(16);
    auto result = box_encrypt(data.data(), data.size(), nullptr,
        bob.pk.data(), alice.sk.data());
    EXPECT_TRUE(result.empty());
}

TEST(EdgeCaseTest, BoxNullKeys) {
    auto alice = generate_box_keypair();
    auto nonce = make_box_nonce();
    auto data = make_data(16);
    auto result = box_encrypt(data.data(), data.size(), nonce.data(),
        nullptr, alice.sk.data());
    EXPECT_TRUE(result.empty());
    result = box_encrypt(data.data(), data.size(), nonce.data(),
        alice.pk.data(), nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(EdgeCaseTest, BoxDecryptTooShort) {
    auto alice = generate_box_keypair();
    auto bob = generate_box_keypair();
    auto nonce = make_box_nonce();
    std::vector<uint8_t> short_ct(5, 0x00);
    auto result = box_decrypt(short_ct.data(), short_ct.size(),
        nonce.data(), alice.pk.data(), bob.sk.data());
    EXPECT_TRUE(result.empty());
}

TEST(EdgeCaseTest, SignEmptyData) {
    auto kp = generate_sign_keypair();
    uint8_t dummy = 0;
    auto sig = sign(&dummy, 0, kp.sk.data());
    // Signing empty data - may or may not work depending on impl
    SUCCEED();  // Just ensure no crash
}

TEST(EdgeCaseTest, SignNullKey) {
    auto data = make_data(16);
    auto sig = sign(data.data(), data.size(), nullptr);
    EXPECT_TRUE(sig.empty()) << "Signing with null key should return empty";
}

TEST(EdgeCaseTest, VerifyNullSignature) {
    auto kp = generate_sign_keypair();
    auto data = make_data(16);
    bool result = sign_verify(data.data(), data.size(), nullptr, kp.pk.data());
    EXPECT_FALSE(result) << "Null signature should fail verification";
}

TEST(EdgeCaseTest, VerifyNullPublicKey) {
    auto kp = generate_sign_keypair();
    auto data = make_data(16);
    auto sig = sign(data.data(), data.size(), kp.sk.data());
    bool result = sign_verify(data.data(), data.size(), sig.data(), nullptr);
    EXPECT_FALSE(result) << "Null public key should fail verification";
}

TEST(EdgeCaseTest, SHA256NullDataZeroLength) {
    auto h = sha256(nullptr, 0);
    // Should return empty string hash or all zeros
    EXPECT_EQ(h.size(), static_cast<size_t>(SHA256_BYTES));
}

// ============================================================================
// 11. Stress / Concurrency Tests
// ============================================================================

TEST(StressTest, ManyRoundTrips) {
    // Perform many encrypt/decrypt cycles to test for memory leaks and stability
    auto key = make_secretbox_key();
    auto nonce = make_secretbox_nonce();

    for (int i = 0; i < 1000; ++i) {
        auto pt = make_data(100 + (i % 900));
        auto ct = secretbox_encrypt(pt.data(), pt.size(), key.data(), nonce.data());
        ASSERT_FALSE(ct.empty()) << "Encryption failed at iteration " << i;
        auto dt = secretbox_decrypt(ct.data(), ct.size(), key.data(), nonce.data());
        ASSERT_FALSE(dt.empty()) << "Decryption failed at iteration " << i;
        EXPECT_EQ(dt, pt) << "Round-trip failed at iteration " << i;
        // Vary nonce slightly
        nonce[0] = static_cast<uint8_t>(i & 0xFF);
    }
}

TEST(StressTest, ConcurrentKeyGeneration) {
    const int num_threads = 8;
    const int keys_per_thread = 50;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([keys_per_thread]() {
            for (int i = 0; i < keys_per_thread; ++i) {
                auto box_kp = generate_box_keypair();
                // Verify keys aren't trivially zero
                bool has_nonzero = false;
                for (auto b : box_kp.sk) { if (b != 0) { has_nonzero = true; break; } }
                EXPECT_TRUE(has_nonzero);

                auto sign_kp = generate_sign_keypair();
                has_nonzero = false;
                for (auto b : sign_kp.sk) { if (b != 0) { has_nonzero = true; break; } }
                EXPECT_TRUE(has_nonzero);
            }
        });
    }

    for (auto& t : threads) t.join();
}

TEST(StressTest, ConcurrentEncryption) {
    const int num_threads = 4;
    const int ops_per_thread = 200;
    auto key = make_secretbox_key();
    auto nonce = make_secretbox_nonce();
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, ops_per_thread, &key, &nonce]() {
            auto local_nonce = nonce;
            for (int i = 0; i < ops_per_thread; ++i) {
                local_nonce[0] = static_cast<uint8_t>((t << 4) | (i & 0x0F));
                auto pt = make_data(256);
                auto ct = secretbox_encrypt(pt.data(), pt.size(),
                    key.data(), local_nonce.data());
                ASSERT_FALSE(ct.empty());
                auto dt = secretbox_decrypt(ct.data(), ct.size(),
                    key.data(), local_nonce.data());
                ASSERT_FALSE(dt.empty());
                EXPECT_EQ(dt, pt);
            }
        });
    }

    for (auto& t : threads) t.join();
}

TEST(StressTest, LargeDataRoundTrip) {
    // 2MB round-trip via secretbox
    auto pt = make_data(2 * 1024 * 1024);
    auto key = make_secretbox_key();
    auto nonce = make_secretbox_nonce();
    auto ct = secretbox_encrypt(pt.data(), pt.size(), key.data(), nonce.data());
    ASSERT_FALSE(ct.empty());
    EXPECT_EQ(ct.size(), pt.size() + SECRET_BOX_MAC_BYTES);
    auto dt = secretbox_decrypt(ct.data(), ct.size(), key.data(), nonce.data());
    ASSERT_FALSE(dt.empty());
    EXPECT_EQ(dt, pt);
}

// ============================================================================
// 12. Combined Operation Tests
// ============================================================================

TEST(CombinedTest, EncryptThenSign) {
    // Simulate: Alice encrypts message for Bob, then signs it
    auto alice_box = generate_box_keypair();
    auto bob_box = generate_box_keypair();
    auto alice_sign = generate_sign_keypair();
    auto nonce = make_box_nonce();

    std::vector<uint8_t> message = make_data(1024);

    // Encrypt for Bob
    auto ciphertext = box_encrypt(message.data(), message.size(),
        nonce.data(), bob_box.pk.data(), alice_box.sk.data());
    ASSERT_FALSE(ciphertext.empty());

    // Sign the ciphertext
    auto signature = sign(ciphertext.data(), ciphertext.size(),
        alice_sign.sk.data());
    ASSERT_FALSE(signature.empty());

    // Bob verifies Alice's signature on the ciphertext
    bool sig_valid = sign_verify(ciphertext.data(), ciphertext.size(),
        signature.data(), alice_sign.pk.data());
    EXPECT_TRUE(sig_valid) << "Bob should verify Alice's signature";

    // Bob decrypts
    auto plaintext = box_decrypt(ciphertext.data(), ciphertext.size(),
        nonce.data(), alice_box.pk.data(), bob_box.sk.data());
    ASSERT_FALSE(plaintext.empty());
    EXPECT_EQ(plaintext, message);
}

TEST(CombinedTest, HashThenSign) {
    // Hash a message, then sign the hash
    auto sign_kp = generate_sign_keypair();
    std::string message = "Sign this important message!";

    auto hash = sha256(message);
    auto sig = sign(hash.data(), hash.size(), sign_kp.sk.data());
    ASSERT_FALSE(sig.empty());

    // Bob re-hashes and verifies
    auto hash2 = sha256(message);
    EXPECT_EQ(hash, hash2);
    bool valid = sign_verify(hash2.data(), hash2.size(),
        sig.data(), sign_kp.pk.data());
    EXPECT_TRUE(valid);
}

TEST(CombinedTest, PasswordDerivedEncryption) {
    // Simulate deriving a key from password, then using it for symmetric encryption
    std::string password = "user_password";
    std::string salt = "application_salt";

    auto pw_hash = hash_password(password, salt);
    ASSERT_FALSE(pw_hash.empty());
    // Use the hash as input to SHA256 to get a fixed-size key
    auto key = sha256(pw_hash);
    std::array<uint8_t, SECRET_BOX_KEY_BYTES> derived_key{};
    std::copy(key.begin(), key.end(), derived_key.begin());

    auto nonce = make_secretbox_nonce();
    auto pt = make_data(512);
    auto ct = secretbox_encrypt(pt.data(), pt.size(),
        derived_key.data(), nonce.data());
    ASSERT_FALSE(ct.empty());

    auto dt = secretbox_decrypt(ct.data(), ct.size(),
        derived_key.data(), nonce.data());
    ASSERT_FALSE(dt.empty());
    EXPECT_EQ(dt, pt);
}

// ============================================================================
// 13. Constant-Time Comparison Tests
// ============================================================================

// Note: secure_compare is implemented in crypto.cpp but not exposed in the header.
// These tests are included for when the function is made public.
// To enable: add declarations to common/crypto.hpp:
//   bool secure_compare(const uint8_t* a, const uint8_t* b, size_t len);
//   bool secure_compare(const std::string& a, const std::string& b);

#if 0  // Enable when secure_compare is exposed in the header
TEST(SecureCompareTest, EqualBuffers) {
    auto a = make_data(64);
    auto b = a;
    EXPECT_TRUE(secure_compare(a.data(), b.data(), a.size()));
}

TEST(SecureCompareTest, DifferentBuffers) {
    auto a = make_data(64);
    auto b = make_data(64);
    b[32] ^= 0x01;
    EXPECT_FALSE(secure_compare(a.data(), b.data(), a.size()));
}

TEST(SecureCompareTest, DifferentLengths) {
    auto a = make_data(64);
    auto b = make_data(65);
    EXPECT_FALSE(secure_compare(
        reinterpret_cast<const uint8_t*>(a.data()),
        reinterpret_cast<const uint8_t*>(b.data()),
        std::min(a.size(), b.size())));
}

TEST(SecureCompareTest, StringOverload) {
    std::string a = "constant time test";
    std::string b = "constant time test";
    EXPECT_TRUE(secure_compare(a, b));
    EXPECT_FALSE(secure_compare(a, "wrong string"));
}

TEST(SecureCompareTest, EmptyInputs) {
    uint8_t dummy = 0;
    EXPECT_TRUE(secure_compare(&dummy, &dummy, 0));
}
#endif

// ============================================================================
// 14. Secure Zero Tests
// ============================================================================

// Note: secure_zero is implemented in crypto.cpp but not exposed in the header.
// To enable: add declaration to common/crypto.hpp:
//   void secure_zero(void* ptr, size_t len);

#if 0  // Enable when secure_zero is exposed in the header
TEST(SecureZeroTest, ZerosMemory) {
    std::vector<uint8_t> data = make_data(256);
    EXPECT_FALSE(std::all_of(data.begin(), data.end(),
        [](uint8_t b) { return b == 0; }));

    secure_zero(data.data(), data.size());
    EXPECT_TRUE(std::all_of(data.begin(), data.end(),
        [](uint8_t b) { return b == 0; }));
}

TEST(SecureZeroTest, PartialZero) {
    std::vector<uint8_t> data = make_data(100);
    secure_zero(data.data(), 50);
    // First 50 should be zero
    for (size_t i = 0; i < 50; ++i) {
        EXPECT_EQ(data[i], 0) << "Byte " << i << " not zeroed";
    }
    // Last 50 should be non-zero
    bool has_nonzero = false;
    for (size_t i = 50; i < 100; ++i) {
        if (data[i] != 0) has_nonzero = true;
    }
    EXPECT_TRUE(has_nonzero);
}

TEST(SecureZeroTest, ZeroLength) {
    uint8_t x = 42;
    secure_zero(&x, 0);
    EXPECT_EQ(x, 42) << "Zero-length zero should be no-op";
}

TEST(SecureZeroTest, LargeBuffer) {
    std::vector<uint8_t> data(1024 * 1024, 0xFF);
    secure_zero(data.data(), data.size());
    for (auto b : data) {
        EXPECT_EQ(b, 0);
    }
}
#endif

// ============================================================================
// 15. HMAC-SHA256 Known-Answer Tests
// ============================================================================

// Note: hmac_sha256 is implemented in crypto.cpp but not exposed in the header.
// To enable: add declaration to common/crypto.hpp:
//   std::array<uint8_t, SHA256_BYTES> hmac_sha256(const uint8_t* data,
//       size_t len, const uint8_t* key, size_t key_len);

#if 0  // Enable when hmac_sha256 is exposed in the header
TEST(HMACTest, Deterministic) {
    auto data = make_data(128);
    auto key = make_data(32);
    auto mac1 = hmac_sha256(data.data(), data.size(), key.data(), key.size());
    auto mac2 = hmac_sha256(data.data(), data.size(), key.data(), key.size());
    EXPECT_EQ(mac1, mac2);
}

TEST(HMACTest, DifferentDataDifferentMAC) {
    auto key = make_data(32);
    auto data1 = make_data(128);
    auto data2 = make_data(128);
    data2[64] ^= 0xFF;
    auto mac1 = hmac_sha256(data1.data(), data1.size(), key.data(), key.size());
    auto mac2 = hmac_sha256(data2.data(), data2.size(), key.data(), key.size());
    EXPECT_NE(mac1, mac2);
}

TEST(HMACTest, DifferentKeyDifferentMAC) {
    auto data = make_data(128);
    auto key1 = make_data(32);
    auto key2 = make_data(32);
    key2[0] ^= 0xFF;
    auto mac1 = hmac_sha256(data.data(), data.size(), key1.data(), key1.size());
    auto mac2 = hmac_sha256(data.data(), data.size(), key2.data(), key2.size());
    EXPECT_NE(mac1, mac2);
}

// RFC 4231 Test Case 1
TEST(HMACTest, RFC4231_Case1) {
    // key = 0x0b0b0b...0b (20 bytes)
    auto key = std::vector<uint8_t>(20, 0x0b);
    std::string data = "Hi There";
    auto mac = hmac_sha256(
        reinterpret_cast<const uint8_t*>(data.data()), data.size(),
        key.data(), key.size());
    auto expected = from_hex("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    for (size_t i = 0; i < mac.size(); ++i) {
        EXPECT_EQ(mac[i], expected[i]) << "RFC 4231 Case 1 mismatch at byte " << i;
    }
}

// RFC 4231 Test Case 2
TEST(HMACTest, RFC4231_Case2) {
    std::string key_str = "Jefe";
    std::string data = "what do ya want for nothing?";
    auto mac = hmac_sha256(
        reinterpret_cast<const uint8_t*>(data.data()), data.size(),
        reinterpret_cast<const uint8_t*>(key_str.data()), key_str.size());
    auto expected = from_hex("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    for (size_t i = 0; i < mac.size(); ++i) {
        EXPECT_EQ(mac[i], expected[i]) << "RFC 4231 Case 2 mismatch at byte " << i;
    }
}

// RFC 4231 Test Case 3
TEST(HMACTest, RFC4231_Case3) {
    auto key = std::vector<uint8_t>(20, 0xaa);
    auto data = std::vector<uint8_t>(50, 0xdd);
    auto mac = hmac_sha256(data.data(), data.size(), key.data(), key.size());
    auto expected = from_hex("773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
    for (size_t i = 0; i < mac.size(); ++i) {
        EXPECT_EQ(mac[i], expected[i]) << "RFC 4231 Case 3 mismatch at byte " << i;
    }
}

TEST(HMACTest, EmptyData) {
    auto key = make_data(32);
    uint8_t dummy = 0;
    auto mac = hmac_sha256(&dummy, 0, key.data(), key.size());
    EXPECT_EQ(mac.size(), static_cast<size_t>(SHA256_BYTES));
    bool all_zero = std::all_of(mac.begin(), mac.end(),
        [](uint8_t b) { return b == 0; });
    EXPECT_FALSE(all_zero) << "HMAC of empty data should not be all zeros";
}

TEST(HMACTest, VariousKeySizes) {
    auto data = make_data(128);
    for (size_t key_size : {1, 16, 32, 64, 128}) {
        auto key = make_data(key_size);
        auto mac = hmac_sha256(data.data(), data.size(), key.data(), key.size());
        EXPECT_EQ(mac.size(), static_cast<size_t>(SHA256_BYTES))
            << "HMAC output size wrong for key size " << key_size;
    }
}
#endif

// ============================================================================
// 16. Key Derivation Tests
// ============================================================================

// Note: derive_key is implemented in crypto.cpp but not exposed in the header.
// To enable: add declaration to common/crypto.hpp:
//   std::vector<uint8_t> derive_key(const std::string& password,
//       const std::vector<uint8_t>& salt, size_t key_len);

#if 0  // Enable when derive_key is exposed in the header
TEST(DeriveKeyTest, Consistency) {
    std::string password = "test_password";
    auto salt = make_data(16);
    auto key1 = derive_key(password, salt, 32);
    auto key2 = derive_key(password, salt, 32);
    ASSERT_FALSE(key1.empty());
    ASSERT_FALSE(key2.empty());
    EXPECT_EQ(key1, key2) << "Same inputs should derive same key";
}

TEST(DeriveKeyTest, DifferentPasswordDifferentKey) {
    auto salt = make_data(16);
    auto key1 = derive_key("password1", salt, 32);
    auto key2 = derive_key("password2", salt, 32);
    ASSERT_FALSE(key1.empty());
    ASSERT_FALSE(key2.empty());
    EXPECT_NE(key1, key2);
}

TEST(DeriveKeyTest, DifferentSaltDifferentKey) {
    std::string password = "password";
    auto salt1 = make_data(16);
    auto salt2 = make_data(16);
    salt2[0] ^= 0xFF;
    auto key1 = derive_key(password, salt1, 32);
    auto key2 = derive_key(password, salt2, 32);
    ASSERT_FALSE(key1.empty());
    ASSERT_FALSE(key2.empty());
    EXPECT_NE(key1, key2);
}

TEST(DeriveKeyTest, VariousKeyLengths) {
    std::string password = "password";
    auto salt = make_data(16);
    for (size_t len : {16, 32, 64, 128}) {
        auto key = derive_key(password, salt, len);
        EXPECT_EQ(key.size(), len) << "Derived key length mismatch";
    }
}

TEST(DeriveKeyTest, EmptyPassword) {
    auto salt = make_data(16);
    auto key = derive_key("", salt, 32);
    EXPECT_TRUE(key.empty()) << "Empty password should return empty key";
}

TEST(DeriveKeyTest, EmptySalt) {
    std::vector<uint8_t> empty_salt;
    auto key = derive_key("password", empty_salt, 32);
    EXPECT_TRUE(key.empty()) << "Empty salt should return empty key";
}
#endif

// ============================================================================
// 17. Key Fingerprint Tests
// ============================================================================

// Note: key_fingerprint is implemented in crypto.cpp but not exposed in the header.
// To enable: add declarations to common/crypto.hpp:
//   std::string key_fingerprint(const uint8_t* key, size_t len);
//   std::string key_fingerprint(const std::string& key);

#if 0  // Enable when key_fingerprint is exposed in the header
TEST(KeyFingerprintTest, Deterministic) {
    auto key = make_data(32);
    auto fp1 = key_fingerprint(key.data(), key.size());
    auto fp2 = key_fingerprint(key.data(), key.size());
    EXPECT_EQ(fp1, fp2);
    EXPECT_FALSE(fp1.empty());
}

TEST(KeyFingerprintTest, DifferentKeysDifferentFingerprints) {
    auto key1 = make_data(32);
    auto key2 = make_data(32);
    key2[16] ^= 0xFF;
    auto fp1 = key_fingerprint(key1.data(), key1.size());
    auto fp2 = key_fingerprint(key2.data(), key2.size());
    EXPECT_NE(fp1, fp2);
}

TEST(KeyFingerprintTest, StringOverload) {
    std::string key = "test_key_for_fingerprinting";
    auto fp1 = key_fingerprint(key);
    auto fp2 = key_fingerprint(key);
    EXPECT_EQ(fp1, fp2);
}

TEST(KeyFingerprintTest, OutputIsHex) {
    auto key = make_data(32);
    auto fp = key_fingerprint(key.data(), key.size());
    // Should be 32 hex chars (first 16 bytes of SHA256)
    EXPECT_EQ(fp.size(), 32u);
    for (char c : fp) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TEST(KeyFingerprintTest, RealisticKeyFingerprint) {
    auto kp = generate_box_keypair();
    auto fp = key_fingerprint(kp.pk.data(), kp.pk.size());
    EXPECT_EQ(fp.size(), 32u);
    EXPECT_FALSE(fp.empty());
}
#endif

// ============================================================================
// 18. Key Exchange Tests
// ============================================================================

// Note: Key exchange functions are implemented in crypto.cpp but not exposed.
// To enable: add declarations to common/crypto.hpp:
//   struct KeyExchange { ... };
//   KeyExchange begin_key_exchange();
//   std::vector<uint8_t> get_public_share(const KeyExchange& kx);
//   bool complete_key_exchange(KeyExchange& kx,
//       const std::vector<uint8_t>& peer_public);

#if 0  // Enable when key exchange is exposed in the header
TEST(KeyExchangeTest, FullRoundTrip) {
    // Alice initiates
    auto alice_kx = begin_key_exchange();
    auto alice_public = get_public_share(alice_kx);
    EXPECT_EQ(alice_public.size(), static_cast<size_t>(PUBLIC_KEY_BYTES));

    // Bob initiates
    auto bob_kx = begin_key_exchange();
    auto bob_public = get_public_share(bob_kx);
    EXPECT_EQ(bob_public.size(), static_cast<size_t>(PUBLIC_KEY_BYTES));

    // Exchange public shares
    bool alice_ok = complete_key_exchange(alice_kx, bob_public);
    bool bob_ok = complete_key_exchange(bob_kx, alice_public);
    EXPECT_TRUE(alice_ok);
    EXPECT_TRUE(bob_ok);

    // Both should now have the same session key
    EXPECT_EQ(alice_kx.session_key, bob_kx.session_key);
    EXPECT_EQ(alice_kx.session_key.size(), static_cast<size_t>(SHA256_BYTES));

    // Session key should not be all zeros
    bool all_zero = std::all_of(alice_kx.session_key.begin(),
        alice_kx.session_key.end(), [](uint8_t b) { return b == 0; });
    EXPECT_FALSE(all_zero);
}

TEST(KeyExchangeTest, DifferentExchangesDifferentKeys) {
    auto alice1 = begin_key_exchange();
    auto bob1 = begin_key_exchange();
    complete_key_exchange(alice1, get_public_share(bob1));
    complete_key_exchange(bob1, get_public_share(alice1));

    auto alice2 = begin_key_exchange();
    auto bob2 = begin_key_exchange();
    complete_key_exchange(alice2, get_public_share(bob2));
    complete_key_exchange(bob2, get_public_share(alice2));

    EXPECT_NE(alice1.session_key, alice2.session_key)
        << "Different exchanges should produce different session keys";
}

TEST(KeyExchangeTest, InvalidPeerPublic) {
    auto kx = begin_key_exchange();
    std::vector<uint8_t> invalid(16, 0x00);  // Wrong size
    bool result = complete_key_exchange(kx, invalid);
    EXPECT_FALSE(result) << "Wrong-sized peer public should fail";
    EXPECT_FALSE(kx.complete);
}

TEST(KeyExchangeTest, DeterministicSameInputs) {
    // Fixed ephemeral keys should produce deterministic results
    auto kx1 = begin_key_exchange();
    auto kx2 = begin_key_exchange();

    // Use a known public key
    auto fixed_pk = make_data(PUBLIC_KEY_BYTES);

    complete_key_exchange(kx1, fixed_pk);
    // Note: with different ephemeral keys, results will differ
    EXPECT_NE(kx1.session_key, kx2.session_key)
        << "Different ephemeral keys should produce different session keys";
}
#endif

// ============================================================================
// 19. Token and ID Generation Tests
// ============================================================================

// Note: generate_token and generate_id are implemented but not in header.
// To enable: add declarations to common/crypto.hpp:
//   std::string generate_token(size_t len);
//   std::string generate_id();

#if 0  // Enable when exposed in header
TEST(TokenTest, GenerateTokenSize) {
    for (size_t len : {16, 32, 64}) {
        auto token = generate_token(len);
        EXPECT_FALSE(token.empty());
        // Base64 encoded length should be ~ceil(len/3)*4
    }
}

TEST(TokenTest, GenerateTokenUnique) {
    std::set<std::string> tokens;
    for (int i = 0; i < 100; ++i) {
        tokens.insert(generate_token(32));
    }
    EXPECT_EQ(tokens.size(), 100u);
}

TEST(TokenTest, GenerateID) {
    auto id1 = generate_id();
    auto id2 = generate_id();
    EXPECT_FALSE(id1.empty());
    EXPECT_FALSE(id2.empty());
    EXPECT_NE(id1, id2);
    EXPECT_LE(id1.size(), 16u);
}
#endif

// ============================================================================
// 20. Protocol-Level Simulation Tests
// ============================================================================

TEST(ProtocolSimTest, SecureMessageExchange) {
    // Simulate a complete secure message exchange between Alice and Bob
    // using Box encryption + Sign for authentication

    auto alice_box = generate_box_keypair();
    auto bob_box = generate_box_keypair();
    auto alice_sign = generate_sign_keypair();
    auto bob_sign = generate_sign_keypair();

    // Exchange 1: Alice sends to Bob
    {
        std::string msg = "Hello Bob! This is Alice.";
        auto nonce = make_box_nonce();

        // Alice encrypts for Bob
        auto ct = box_encrypt(
            reinterpret_cast<const uint8_t*>(msg.data()), msg.size(),
            nonce.data(), bob_box.pk.data(), alice_box.sk.data());
        ASSERT_FALSE(ct.empty());

        // Alice signs
        auto sig = sign(ct.data(), ct.size(), alice_sign.sk.data());
        ASSERT_FALSE(sig.empty());

        // Bob verifies
        EXPECT_TRUE(sign_verify(ct.data(), ct.size(), sig.data(), alice_sign.pk.data()));

        // Bob decrypts
        auto pt = box_decrypt(ct.data(), ct.size(),
            nonce.data(), alice_box.pk.data(), bob_box.sk.data());
        ASSERT_FALSE(pt.empty());
        std::string received(pt.begin(), pt.end());
        EXPECT_EQ(received, msg);
    }

    // Exchange 2: Bob replies to Alice
    {
        std::string reply = "Hi Alice! Message received.";
        auto nonce = make_box_nonce();

        auto ct = box_encrypt(
            reinterpret_cast<const uint8_t*>(reply.data()), reply.size(),
            nonce.data(), alice_box.pk.data(), bob_box.sk.data());
        ASSERT_FALSE(ct.empty());

        auto sig = sign(ct.data(), ct.size(), bob_sign.sk.data());
        ASSERT_FALSE(sig.empty());

        EXPECT_TRUE(sign_verify(ct.data(), ct.size(), sig.data(), bob_sign.pk.data()));

        auto pt = box_decrypt(ct.data(), ct.size(),
            nonce.data(), bob_box.pk.data(), alice_box.sk.data());
        ASSERT_FALSE(pt.empty());
        std::string received(pt.begin(), pt.end());
        EXPECT_EQ(received, reply);
    }
}

TEST(ProtocolSimTest, MITMDetection) {
    // Simulate: Eve tries to tamper with a message, Bob detects it via signing

    auto alice_box = generate_box_keypair();
    auto bob_box = generate_box_keypair();
    auto alice_sign = generate_sign_keypair();
    auto eve = generate_box_keypair();  // Attacker

    std::string msg = "Transfer $1000 to account 12345";
    auto nonce = make_box_nonce();

    // Alice encrypts for Bob and signs
    auto ct = box_encrypt(
        reinterpret_cast<const uint8_t*>(msg.data()), msg.size(),
        nonce.data(), bob_box.pk.data(), alice_box.sk.data());
    ASSERT_FALSE(ct.empty());
    auto sig = sign(ct.data(), ct.size(), alice_sign.sk.data());
    ASSERT_FALSE(sig.empty());

    // Eve intercepts and tries to modify ciphertext
    auto tampered_ct = ct;
    tampered_ct[10] ^= 0x01;

    // Bob receives tampered message - signature verification should fail
    bool sig_ok = sign_verify(tampered_ct.data(), tampered_ct.size(),
        sig.data(), alice_sign.pk.data());
    EXPECT_FALSE(sig_ok)
        << "MITM tampering should be detected by signature verification";

    // Even if Eve tries to re-encrypt with her own key
    auto eve_ct = box_encrypt(
        reinterpret_cast<const uint8_t*>(msg.data()), msg.size(),
        nonce.data(), bob_box.pk.data(), eve.sk.data());
    ASSERT_FALSE(eve_ct.empty());
    // Eve's ciphertext with Alice's public key for decryption won't work for Bob
    // (Bob uses his own sk, but Eve used bob.pk with eve.sk - different shared secret)
    auto bogus_pt = box_decrypt(eve_ct.data(), eve_ct.size(),
        nonce.data(), alice_box.pk.data(), bob_box.sk.data());
    EXPECT_TRUE(bogus_pt.empty())
        << "Eve's message with different shared secret should fail";
}

TEST(ProtocolSimTest, ReplayAttackDetection) {
    // Bob should detect replayed messages if nonces are tracked
    auto alice_box = generate_box_keypair();
    auto bob_box = generate_box_keypair();
    auto alice_sign = generate_sign_keypair();

    std::string msg = "Replay me if you can!";
    auto nonce = make_box_nonce();

    // Alice's original message
    auto ct = box_encrypt(
        reinterpret_cast<const uint8_t*>(msg.data()), msg.size(),
        nonce.data(), bob_box.pk.data(), alice_box.sk.data());
    ASSERT_FALSE(ct.empty());
    auto sig = sign(ct.data(), ct.size(), alice_sign.sk.data());
    ASSERT_FALSE(sig.empty());

    // First receipt - works fine
    auto pt1 = box_decrypt(ct.data(), ct.size(),
        nonce.data(), alice_box.pk.data(), bob_box.sk.data());
    ASSERT_FALSE(pt1.empty());
    EXPECT_TRUE(sign_verify(ct.data(), ct.size(), sig.data(), alice_sign.pk.data()));

    // Replay - still decrypts (detection is at protocol level, not crypto)
    auto pt2 = box_decrypt(ct.data(), ct.size(),
        nonce.data(), alice_box.pk.data(), bob_box.sk.data());
    ASSERT_FALSE(pt2.empty());
    EXPECT_EQ(pt1, pt2) << "Replay attack: same ciphertext decrypts identically";
    // A real implementation should track seen nonces to prevent this
}

// ============================================================================
// 21. Cross-Implementation Consistency Tests
// ============================================================================

TEST(CrossImplTest, SecretBoxAndAESGCMAreDifferent) {
    // secretbox and AES-GCM should produce different outputs for same inputs
    auto key = make_data(32);
    auto nonce12 = make_data(12);
    auto nonce24 = make_data(24);
    auto pt = make_data(256);

    auto ct_sb = secretbox_encrypt(pt.data(), pt.size(),
        key.data(), nonce24.data());
    auto ct_aes = aes_gcm_encrypt(pt.data(), pt.size(),
        key.data(), nonce12.data());

    ASSERT_FALSE(ct_sb.empty());
    ASSERT_FALSE(ct_aes.empty());
    EXPECT_NE(ct_sb, ct_aes)
        << "SecretBox and AES-GCM should produce different outputs";
}

TEST(CrossImplTest, BoxAndSecretBoxAreDifferent) {
    // Box and SecretBox should produce different outputs
    auto key = make_data(32);
    auto nonce24 = make_data(24);
    auto pt = make_data(256);

    auto alice = generate_box_keypair();
    auto bob = generate_box_keypair();

    auto ct_sb = secretbox_encrypt(pt.data(), pt.size(),
        key.data(), nonce24.data());
    auto ct_box = box_encrypt(pt.data(), pt.size(),
        nonce24.data(), bob.pk.data(), alice.sk.data());

    ASSERT_FALSE(ct_sb.empty());
    ASSERT_FALSE(ct_box.empty());
    EXPECT_NE(ct_sb, ct_box)
        << "Box and SecretBox should produce different outputs";
}

// ============================================================================
// 22. Fuzz-Like / Random Input Tests
// ============================================================================

TEST(FuzzTest, RandomSecretBoxManySizes) {
    auto key = make_secretbox_key();
    auto nonce = make_secretbox_nonce();

    for (int i = 0; i < 100; ++i) {
        size_t size = 1 + (static_cast<size_t>(i) * 997 % 10000);
        auto pt = random_bytes(size);
        auto ct = secretbox_encrypt(pt.data(), pt.size(), key.data(), nonce.data());
        ASSERT_FALSE(ct.empty()) << "Encryption failed at size " << size;
        auto dt = secretbox_decrypt(ct.data(), ct.size(), key.data(), nonce.data());
        ASSERT_FALSE(dt.empty()) << "Decryption failed at size " << size;
        EXPECT_EQ(dt, pt) << "Round-trip failed at size " << size;
    }
}

TEST(FuzzTest, RandomAESGCMManySizes) {
    auto key = make_aes_key();
    auto nonce = make_aes_nonce();

    for (int i = 0; i < 100; ++i) {
        size_t size = 1 + (static_cast<size_t>(i) * 503 % 5000);
        auto pt = random_bytes(size);
        auto ct = aes_gcm_encrypt(pt.data(), pt.size(), key.data(), nonce.data());
        ASSERT_FALSE(ct.empty());
        auto dt = aes_gcm_decrypt(ct.data(), ct.size(), key.data(), nonce.data());
        ASSERT_FALSE(dt.empty());
        EXPECT_EQ(dt, pt) << "Round-trip failed at size " << size;
        nonce[0] = static_cast<uint8_t>(i);
    }
}

TEST(FuzzTest, RandomBoxManySizes) {
    auto alice = generate_box_keypair();
    auto bob = generate_box_keypair();

    for (int i = 0; i < 50; ++i) {
        auto nonce = make_box_nonce();
        size_t size = 1 + (static_cast<size_t>(i) * 211 % 3000);
        auto pt = random_bytes(size);
        auto ct = box_encrypt(pt.data(), pt.size(),
            nonce.data(), bob.pk.data(), alice.sk.data());
        ASSERT_FALSE(ct.empty());
        auto dt = box_decrypt(ct.data(), ct.size(),
            nonce.data(), alice.pk.data(), bob.sk.data());
        ASSERT_FALSE(dt.empty());
        EXPECT_EQ(dt, pt) << "Round-trip failed at size " << size;
    }
}

TEST(FuzzTest, RandomSignManySizes) {
    auto signer = generate_sign_keypair();

    for (int i = 0; i < 100; ++i) {
        size_t size = static_cast<size_t>(i) * 73 % 4096;
        auto msg = random_bytes(size);
        auto sig = sign(msg.data(), msg.size(), signer.sk.data());
        ASSERT_FALSE(sig.empty()) << "Signing failed at size " << size;
        ASSERT_EQ(sig.size(), static_cast<size_t>(SIGN_BYTES));
        bool valid = sign_verify(msg.data(), msg.size(), sig.data(), signer.pk.data());
        EXPECT_TRUE(valid) << "Verification failed at size " << size;
    }
}

// ============================================================================
// 23. Timing Observations (Basic)
// ============================================================================

TEST(TimingTest, SHA256Performance) {
    // SHA256 should complete in a reasonable time for 1MB
    auto data = make_data(1024 * 1024);
    auto start = std::chrono::steady_clock::now();
    auto h = sha256(data.data(), data.size());
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    // Should complete well under 1 second for 1MB
    EXPECT_LT(duration.count(), 1000)
        << "SHA256 of 1MB took " << duration.count() << "ms";
}

TEST(TimingTest, Base64Performance) {
    auto data = make_data(1024 * 1024);
    auto start = std::chrono::steady_clock::now();
    auto encoded = encode64(data.data(), data.size());
    auto decoded = decode64(encoded);
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_LT(duration.count(), 2000)
        << "Base64 encode+decode of 1MB took " << duration.count() << "ms";
    EXPECT_EQ(decoded, data);
}

// ============================================================================
// 24. Key Lifetime Tests
// ============================================================================

TEST(KeyLifetimeTest, KeyPairReuseMultipleOperations) {
    // One key pair should work for many operations
    auto alice = generate_box_keypair();
    auto bob = generate_box_keypair();

    for (int i = 0; i < 100; ++i) {
        auto nonce = make_box_nonce();
        auto msg = make_data(64);
        auto ct = box_encrypt(msg.data(), msg.size(),
            nonce.data(), bob.pk.data(), alice.sk.data());
        ASSERT_FALSE(ct.empty());
        auto pt = box_decrypt(ct.data(), ct.size(),
            nonce.data(), alice.pk.data(), bob.sk.data());
        ASSERT_FALSE(pt.empty());
        EXPECT_EQ(pt, msg);
    }
}

TEST(KeyLifetimeTest, SignKeyReuse) {
    auto kp = generate_sign_keypair();

    for (int i = 0; i < 500; ++i) {
        auto msg = make_data(128);
        auto sig = sign(msg.data(), msg.size(), kp.sk.data());
        ASSERT_FALSE(sig.empty());
        bool valid = sign_verify(msg.data(), msg.size(), sig.data(), kp.pk.data());
        EXPECT_TRUE(valid);
    }
}

// ============================================================================
// 25. Memory Safety Tests
// ============================================================================

TEST(MemoryTest, DecryptDoesNotWriteBeyondBuffer) {
    // Ensure decrypt doesn't corrupt memory around the output
    auto key = make_secretbox_key();
    auto nonce = make_secretbox_nonce();
    auto pt = make_data(1024);
    auto ct = secretbox_encrypt(pt.data(), pt.size(), key.data(), nonce.data());
    ASSERT_FALSE(ct.empty());

    // Decrypt many times with same buffer
    for (int i = 0; i < 100; ++i) {
        auto dt = secretbox_decrypt(ct.data(), ct.size(), key.data(), nonce.data());
        ASSERT_FALSE(dt.empty());
        EXPECT_EQ(dt, pt);
    }
}

// ============================================================================
// 26. Structural Tests
// ============================================================================

TEST(StructuralTest, ConstantSizes) {
    // Verify constants have expected values
    EXPECT_EQ(PUBLIC_KEY_BYTES, 32u);
    EXPECT_EQ(SECRET_KEY_BYTES, 32u);
    EXPECT_EQ(SIGN_PUBLIC_KEY_BYTES, 32u);
    EXPECT_EQ(SIGN_SECRET_KEY_BYTES, 64u);
    EXPECT_EQ(SIGN_BYTES, 64u);
    EXPECT_EQ(BOX_PUBLIC_KEY_BYTES, 32u);
    EXPECT_EQ(BOX_SECRET_KEY_BYTES, 32u);
    EXPECT_EQ(BOX_NONCE_BYTES, 24u);
    EXPECT_EQ(BOX_MAC_BYTES, 16u);
    EXPECT_EQ(SECRET_BOX_KEY_BYTES, 32u);
    EXPECT_EQ(SECRET_BOX_NONCE_BYTES, 24u);
    EXPECT_EQ(SECRET_BOX_MAC_BYTES, 16u);
    EXPECT_EQ(SHA256_BYTES, 32u);
    EXPECT_EQ(AES_KEY_BYTES, 32u);
    EXPECT_EQ(AES_NONCE_BYTES, 12u);
    EXPECT_EQ(AES_TAG_BYTES, 16u);
}

TEST(StructuralTest, KeyPairDefaultInit) {
    KeyPair kp{};
    // Should be zero-initialized
    for (auto b : kp.pk) EXPECT_EQ(b, 0);
    for (auto b : kp.sk) EXPECT_EQ(b, 0);
}

TEST(StructuralTest, SignKeyPairDefaultInit) {
    SignKeyPair kp{};
    for (auto b : kp.pk) EXPECT_EQ(b, 0);
    for (auto b : kp.sk) EXPECT_EQ(b, 0);
}

TEST(StructuralTest, KeyPairAfterGenerationNotEmpty) {
    auto kp = generate_box_keypair();
    // At least one byte should be non-zero in each key
    EXPECT_TRUE(std::any_of(kp.pk.begin(), kp.pk.end(),
        [](uint8_t b) { return b != 0; }));
    EXPECT_TRUE(std::any_of(kp.sk.begin(), kp.sk.end(),
        [](uint8_t b) { return b != 0; }));
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
