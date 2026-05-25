#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace cppdesk::common::crypto {

constexpr size_t PUBLIC_KEY_BYTES = 32;
constexpr size_t SECRET_KEY_BYTES = 32;
constexpr size_t SIGN_PUBLIC_KEY_BYTES = 32;
constexpr size_t SIGN_SECRET_KEY_BYTES = 64;
constexpr size_t SIGN_BYTES = 64;
constexpr size_t BOX_PUBLIC_KEY_BYTES = 32;
constexpr size_t BOX_SECRET_KEY_BYTES = 32;
constexpr size_t BOX_NONCE_BYTES = 24;
constexpr size_t BOX_MAC_BYTES = 16;
constexpr size_t SECRET_BOX_KEY_BYTES = 32;
constexpr size_t SECRET_BOX_NONCE_BYTES = 24;
constexpr size_t SECRET_BOX_MAC_BYTES = 16;
constexpr size_t SHA256_BYTES = 32;
constexpr size_t AES_KEY_BYTES = 32;
constexpr size_t AES_NONCE_BYTES = 12;
constexpr size_t AES_TAG_BYTES = 16;

// Key pair
struct KeyPair {
    std::array<uint8_t, PUBLIC_KEY_BYTES> pk{};
    std::array<uint8_t, SECRET_KEY_BYTES> sk{};
};

struct SignKeyPair {
    std::array<uint8_t, SIGN_PUBLIC_KEY_BYTES> pk{};
    std::array<uint8_t, SIGN_SECRET_KEY_BYTES> sk{};
};

// Generate key pairs
KeyPair generate_box_keypair();
SignKeyPair generate_sign_keypair();

// Symmetric encryption (secretbox)
std::vector<uint8_t> secretbox_encrypt(const uint8_t* data, size_t len,
    const uint8_t* key, const uint8_t* nonce);
std::vector<uint8_t> secretbox_decrypt(const uint8_t* data, size_t len,
    const uint8_t* key, const uint8_t* nonce);

// Asymmetric encryption (box)
std::vector<uint8_t> box_encrypt(const uint8_t* data, size_t len,
    const uint8_t* nonce, const uint8_t* their_pk, const uint8_t* my_sk);
std::vector<uint8_t> box_decrypt(const uint8_t* data, size_t len,
    const uint8_t* nonce, const uint8_t* their_pk, const uint8_t* my_sk);

// Signing
std::vector<uint8_t> sign(const uint8_t* data, size_t len, const uint8_t* sk);
bool sign_verify(const uint8_t* data, size_t len, const uint8_t* sig,
    const uint8_t* pk);

// Hashing
std::array<uint8_t, SHA256_BYTES> sha256(const uint8_t* data, size_t len);
std::array<uint8_t, SHA256_BYTES> sha256(const std::string& s);

// Random
std::vector<uint8_t> random_bytes(size_t len);

// AES-GCM (for stream encryption)
std::vector<uint8_t> aes_gcm_encrypt(const uint8_t* data, size_t len,
    const uint8_t* key, const uint8_t* nonce);
std::vector<uint8_t> aes_gcm_decrypt(const uint8_t* data, size_t len,
    const uint8_t* key, const uint8_t* nonce);

// Password hashing (argon2id fallback: SHA256-based)
std::string hash_password(const std::string& password, const std::string& salt);
bool verify_password(const std::string& password, const std::string& hash,
    const std::string& salt);

// Base64
std::string encode64(const uint8_t* data, size_t len);
std::vector<uint8_t> decode64(const std::string& s);

} // namespace cppdesk::common::crypto
