#include "common/crypto.hpp"
#include "common/config.hpp"

#ifdef HAS_LIBSODIUM
#include <sodium.h>
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/curve25519.h>
#include <openssl/kdf.h>
#endif

#include <cstring>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace cppdesk::common::crypto {

// ====== Internal Helpers ======

namespace {

bool is_libsodium_available() {
#ifdef HAS_LIBSODIUM
    static bool initialized = []() {
        if (sodium_init() < 0) {
            spdlog::error("Failed to initialize libsodium");
            return false;
        }
        spdlog::info("libsodium initialized");
        return true;
    }();
    return initialized;
#else
    return false;
#endif
}

void ensure_libsodium() {
    if (!is_libsodium_available()) {
        throw std::runtime_error("libsodium not available");
    }
}

template<size_t N>
std::array<uint8_t, N> bytes_to_array(const std::vector<uint8_t>& v) {
    std::array<uint8_t, N> arr{};
    size_t len = std::min(v.size(), N);
    std::copy_n(v.begin(), len, arr.begin());
    return arr;
}

std::string hex_encode(const uint8_t* data, size_t len) {
    static const char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        result.push_back(hex_chars[data[i] >> 4]);
        result.push_back(hex_chars[data[i] & 0x0F]);
    }
    return result;
}

std::vector<uint8_t> hex_decode(const std::string& hex) {
    std::vector<uint8_t> result;
    result.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        char high = hex[i];
        char low = hex[i + 1];
        uint8_t val = 0;
        if (high >= '0' && high <= '9') val = (high - '0') << 4;
        else if (high >= 'a' && high <= 'f') val = (high - 'a' + 10) << 4;
        else if (high >= 'A' && high <= 'F') val = (high - 'A' + 10) << 4;
        if (low >= '0' && low <= '9') val |= (low - '0');
        else if (low >= 'a' && low <= 'f') val |= (low - 'a' + 10);
        else if (low >= 'A' && low <= 'F') val |= (low - 'A' + 10);
        result.push_back(val);
    }
    return result;
}

} // anonymous namespace

// ====== Key Generation ======

KeyPair generate_box_keypair() {
#ifdef HAS_LIBSODIUM
    KeyPair kp{};
    crypto_box_keypair(kp.pk.data(), kp.sk.data());
    return kp;
#else
    KeyPair kp{};
    X25519_keypair(kp.pk.data(), kp.sk.data());
    return kp;
#endif
}

SignKeyPair generate_sign_keypair() {
#ifdef HAS_LIBSODIUM
    SignKeyPair kp{};
    crypto_sign_keypair(kp.pk.data(), kp.sk.data());
    return kp;
#else
    SignKeyPair kp{};
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if (ctx) {
        EVP_PKEY_keygen_init(ctx);
        EVP_PKEY_keygen(ctx, &pkey);
        EVP_PKEY_CTX_free(ctx);
    }
    if (pkey) {
        size_t pk_len = SIGN_PUBLIC_KEY_BYTES;
        size_t sk_len = SIGN_SECRET_KEY_BYTES;
        EVP_PKEY_get_raw_public_key(pkey, kp.pk.data(), &pk_len);
        EVP_PKEY_get_raw_private_key(pkey, kp.sk.data(), &sk_len);
        EVP_PKEY_free(pkey);
    } else {
        RAND_bytes(kp.sk.data(), SIGN_SECRET_KEY_BYTES);
        RAND_bytes(kp.pk.data(), SIGN_PUBLIC_KEY_BYTES);
    }
    return kp;
#endif
}

// ====== Symmetric Encryption (SecretBox) ======

std::vector<uint8_t> secretbox_encrypt(const uint8_t* data, size_t len,
    const uint8_t* key, const uint8_t* nonce) {
    if (!data || len == 0 || !key || !nonce) {
        spdlog::error("secretbox_encrypt: null input");
        return {};
    }

#ifdef HAS_LIBSODIUM
    std::vector<uint8_t> out(len + crypto_secretbox_MACBYTES);
    if (crypto_secretbox_easy(out.data(), data, len, nonce, key) != 0) {
        spdlog::error("secretbox_encrypt failed");
        return {};
    }
    return out;
#else
    // OpenSSL: use AES-256-GCM as fallback
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    std::vector<uint8_t> out(len + 16); // 16-byte tag
    int outlen = 0, tmplen = 0;

    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, nonce);
    EVP_EncryptUpdate(ctx, out.data(), &outlen, data, static_cast<int>(len));
    EVP_EncryptFinal_ex(ctx, out.data() + outlen, &tmplen);
    outlen += tmplen;

    // Get the tag
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, out.data() + outlen);
    out.resize(outlen + 16);

    EVP_CIPHER_CTX_free(ctx);
    return out;
#endif
}

std::vector<uint8_t> secretbox_decrypt(const uint8_t* data, size_t len,
    const uint8_t* key, const uint8_t* nonce) {
    if (!data || len < SECRET_BOX_MAC_BYTES || !key || !nonce) {
        spdlog::error("secretbox_decrypt: invalid input (len={})", len);
        return {};
    }

#ifdef HAS_LIBSODIUM
    std::vector<uint8_t> out(len - crypto_secretbox_MACBYTES);
    if (crypto_secretbox_open_easy(out.data(), data, len, nonce, key) != 0) {
        spdlog::error("secretbox_decrypt: authentication failed");
        return {};
    }
    return out;
#else
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    // Last 16 bytes are the tag
    size_t ciphertext_len = len - 16;

    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, nonce);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
        const_cast<uint8_t*>(data + ciphertext_len));

    std::vector<uint8_t> out(ciphertext_len);
    int outlen = 0, tmplen = 0;

    EVP_DecryptUpdate(ctx, out.data(), &outlen, data, static_cast<int>(ciphertext_len));
    int ret = EVP_DecryptFinal_ex(ctx, out.data() + outlen, &tmplen);

    EVP_CIPHER_CTX_free(ctx);

    if (ret <= 0) {
        spdlog::error("secretbox_decrypt: authentication failed");
        return {};
    }

    out.resize(outlen + tmplen);
    return out;
#endif
}

// ====== Asymmetric Encryption (Box) ======

std::vector<uint8_t> box_encrypt(const uint8_t* data, size_t len,
    const uint8_t* nonce, const uint8_t* their_pk, const uint8_t* my_sk) {
    if (!data || !nonce || !their_pk || !my_sk) {
        spdlog::error("box_encrypt: null input");
        return {};
    }

#ifdef HAS_LIBSODIUM
    std::vector<uint8_t> out(len + crypto_box_MACBYTES);
    if (crypto_box_easy(out.data(), data, len, nonce, their_pk, my_sk) != 0) {
        spdlog::error("box_encrypt failed");
        return {};
    }
    return out;
#else
    // Derive shared key using X25519
    uint8_t shared_key[32];
    X25519(shared_key, my_sk, their_pk);

    // Use shared key with AES-256-GCM
    std::vector<uint8_t> out(len + 16);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int outlen = 0, tmplen = 0;

    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, shared_key, nonce);
    EVP_EncryptUpdate(ctx, out.data(), &outlen, data, static_cast<int>(len));
    EVP_EncryptFinal_ex(ctx, out.data() + outlen, &tmplen);
    outlen += tmplen;

    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, out.data() + outlen);
    out.resize(outlen + 16);

    EVP_CIPHER_CTX_free(ctx);
    return out;
#endif
}

std::vector<uint8_t> box_decrypt(const uint8_t* data, size_t len,
    const uint8_t* nonce, const uint8_t* their_pk, const uint8_t* my_sk) {
    if (!data || len < BOX_MAC_BYTES || !nonce || !their_pk || !my_sk) {
        spdlog::error("box_decrypt: invalid input");
        return {};
    }

#ifdef HAS_LIBSODIUM
    std::vector<uint8_t> out(len - crypto_box_MACBYTES);
    if (crypto_box_open_easy(out.data(), data, len, nonce, their_pk, my_sk) != 0) {
        spdlog::error("box_decrypt: authentication failed");
        return {};
    }
    return out;
#else
    uint8_t shared_key[32];
    X25519(shared_key, my_sk, their_pk);

    size_t ciphertext_len = len - 16;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int outlen = 0, tmplen = 0;

    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, shared_key, nonce);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
        const_cast<uint8_t*>(data + ciphertext_len));

    std::vector<uint8_t> out(ciphertext_len);
    EVP_DecryptUpdate(ctx, out.data(), &outlen, data, static_cast<int>(ciphertext_len));
    int ret = EVP_DecryptFinal_ex(ctx, out.data() + outlen, &tmplen);

    EVP_CIPHER_CTX_free(ctx);

    if (ret <= 0) {
        spdlog::error("box_decrypt: authentication failed");
        return {};
    }

    out.resize(outlen + tmplen);
    return out;
#endif
}

// ====== Signing ======

std::vector<uint8_t> sign(const uint8_t* data, size_t len, const uint8_t* sk) {
    if (!data || !sk) return {};

#ifdef HAS_LIBSODIUM
    std::vector<uint8_t> sig(crypto_sign_BYTES);
    unsigned long long siglen = 0;
    if (crypto_sign_detached(sig.data(), &siglen, data, len, sk) != 0) {
        spdlog::error("sign failed");
        return {};
    }
    sig.resize(siglen);
    return sig;
#else
    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
        sk, SIGN_SECRET_KEY_BYTES);
    if (!pkey) return {};

    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    EVP_DigestSignInit(md_ctx, nullptr, nullptr, nullptr, pkey);
    EVP_DigestSign(md_ctx, nullptr, nullptr, data, len);

    size_t siglen = SIGN_BYTES;
    std::vector<uint8_t> sig(siglen);
    EVP_DigestSign(md_ctx, sig.data(), &siglen, data, len);

    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pkey);
    sig.resize(siglen);
    return sig;
#endif
}

bool sign_verify(const uint8_t* data, size_t len, const uint8_t* sig,
    const uint8_t* pk) {
    if (!data || !sig || !pk) return false;

#ifdef HAS_LIBSODIUM
    return crypto_sign_verify_detached(sig, data, len, pk) == 0;
#else
    EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
        pk, SIGN_PUBLIC_KEY_BYTES);
    if (!pkey) return false;

    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    EVP_DigestVerifyInit(md_ctx, nullptr, nullptr, nullptr, pkey);
    int result = EVP_DigestVerify(md_ctx, sig, SIGN_BYTES, data, len);

    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pkey);
    return result == 1;
#endif
}

// ====== Hashing ======

std::array<uint8_t, SHA256_BYTES> sha256(const uint8_t* data, size_t len) {
    std::array<uint8_t, SHA256_BYTES> hash{};
#ifdef HAS_LIBSODIUM
    crypto_hash_sha256(hash.data(), data, len);
#else
    SHA256(data, len, hash.data());
#endif
    return hash;
}

std::array<uint8_t, SHA256_BYTES> sha256(const std::string& s) {
    return sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// HMAC-SHA256
std::array<uint8_t, SHA256_BYTES> hmac_sha256(const uint8_t* data, size_t len,
    const uint8_t* key, size_t key_len) {
    std::array<uint8_t, SHA256_BYTES> mac{};
#ifdef HAS_LIBSODIUM
    crypto_auth_hmacsha256_state state;
    crypto_auth_hmacsha256_init(&state, key, key_len);
    crypto_auth_hmacsha256_update(&state, data, len);
    crypto_auth_hmacsha256_final(&state, mac.data());
#else
    unsigned int maclen = SHA256_BYTES;
    HMAC(EVP_sha256(), key, static_cast<int>(key_len), data, len,
        mac.data(), &maclen);
#endif
    return mac;
}

// ====== Random ======

std::vector<uint8_t> random_bytes(size_t len) {
    std::vector<uint8_t> buf(len);
#ifdef HAS_LIBSODIUM
    randombytes_buf(buf.data(), len);
#else
    RAND_bytes(buf.data(), static_cast<int>(len));
#endif
    return buf;
}

std::string random_hex_string(size_t len) {
    auto bytes = random_bytes(len);
    return hex_encode(bytes.data(), bytes.size());
}

uint64_t random_u64_range(uint64_t min, uint64_t max) {
    auto bytes = random_bytes(8);
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) val = (val << 8) | bytes[i];
    return min + (val % (max - min + 1));
}

// ====== AES-GCM (Stream Encryption) ======

std::vector<uint8_t> aes_gcm_encrypt(const uint8_t* data, size_t len,
    const uint8_t* key, const uint8_t* nonce) {
    if (!data || !key || !nonce) return {};

#ifdef HAS_LIBSODIUM
    std::vector<uint8_t> out(len + crypto_aead_aes256gcm_ABYTES);
    unsigned long long outlen = 0;
    if (crypto_aead_aes256gcm_encrypt(out.data(), &outlen, data, len,
        nullptr, 0, nullptr, nonce, key) != 0) {
        spdlog::error("aes_gcm_encrypt failed");
        return {};
    }
    out.resize(outlen);
    return out;
#else
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    std::vector<uint8_t> out(len + AES_TAG_BYTES);
    int outlen = 0, tmplen = 0;

    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, nonce);
    EVP_EncryptUpdate(ctx, out.data(), &outlen, data, static_cast<int>(len));
    EVP_EncryptFinal_ex(ctx, out.data() + outlen, &tmplen);
    outlen += tmplen;

    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AES_TAG_BYTES,
        out.data() + outlen);
    out.resize(outlen + AES_TAG_BYTES);

    EVP_CIPHER_CTX_free(ctx);
    return out;
#endif
}

std::vector<uint8_t> aes_gcm_decrypt(const uint8_t* data, size_t len,
    const uint8_t* key, const uint8_t* nonce) {
    if (!data || len < AES_TAG_BYTES || !key || !nonce) return {};

#ifdef HAS_LIBSODIUM
    std::vector<uint8_t> out(len);
    unsigned long long outlen = 0;
    if (crypto_aead_aes256gcm_decrypt(out.data(), &outlen, nullptr,
        data, len, nullptr, 0, nonce, key) != 0) {
        spdlog::error("aes_gcm_decrypt failed: auth error");
        return {};
    }
    out.resize(outlen);
    return out;
#else
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    size_t ciphertext_len = len - AES_TAG_BYTES;

    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, nonce);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, AES_TAG_BYTES,
        const_cast<uint8_t*>(data + ciphertext_len));

    std::vector<uint8_t> out(ciphertext_len);
    int outlen = 0, tmplen = 0;

    EVP_DecryptUpdate(ctx, out.data(), &outlen, data,
        static_cast<int>(ciphertext_len));
    int ret = EVP_DecryptFinal_ex(ctx, out.data() + outlen, &tmplen);

    EVP_CIPHER_CTX_free(ctx);

    if (ret <= 0) {
        spdlog::error("aes_gcm_decrypt: authentication failed");
        return {};
    }

    out.resize(outlen + tmplen);
    return out;
#endif
}

// ====== Password Hashing ======

std::string hash_password(const std::string& password, const std::string& salt) {
    if (password.empty()) return "";

#ifdef HAS_LIBSODIUM
    std::vector<uint8_t> out(crypto_pwhash_STRBYTES);
    if (crypto_pwhash_str(reinterpret_cast<char*>(out.data()),
        password.c_str(), password.size(),
        crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        spdlog::error("Password hashing failed");
        return "";
    }
    return std::string(reinterpret_cast<char*>(out.data()));
#else
    // PBKDF2-SHA256 fallback
    const int iterations = 100000;
    std::array<uint8_t, 32> derived{};

    PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
        reinterpret_cast<const uint8_t*>(salt.c_str()),
        static_cast<int>(salt.size()), iterations,
        EVP_sha256(), 32, derived.data());

    return encode64(derived.data(), derived.size());
#endif
}

bool verify_password(const std::string& password, const std::string& hash,
    const std::string& salt) {
    if (password.empty() || hash.empty()) return false;

#ifdef HAS_LIBSODIUM
    return crypto_pwhash_str_verify(hash.c_str(), password.c_str(),
        password.size()) == 0;
#else
    auto computed = hash_password(password, salt);
    return computed == hash;
#endif
}

// ====== Base64 ======

std::string encode64(const uint8_t* data, size_t len) {
    if (!data || len == 0) return "";

#ifdef HAS_LIBSODIUM
    size_t max_len = sodium_base64_ENCODED_LEN(len, sodium_base64_VARIANT_ORIGINAL);
    std::string out(max_len, '\0');
    sodium_bin2base64(out.data(), max_len, data, len,
        sodium_base64_VARIANT_ORIGINAL);
    auto null_pos = out.find('\0');
    if (null_pos != std::string::npos) out.resize(null_pos);
    return out;
#else
    int outlen = ((len + 2) / 3) * 4 + 1;
    std::string out(outlen, '\0');
    int encoded_len = EVP_EncodeBlock(
        reinterpret_cast<uint8_t*>(out.data()), data, static_cast<int>(len));
    out.resize(encoded_len);
    // Remove trailing newline if present
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }
    return out;
#endif
}

std::vector<uint8_t> decode64(const std::string& s) {
    if (s.empty()) return {};

#ifdef HAS_LIBSODIUM
    size_t max_len = s.size() / 4 * 3 + 2;
    std::vector<uint8_t> out(max_len);
    size_t real_len = 0;
    if (sodium_base642bin(out.data(), max_len, s.c_str(), s.size(),
        nullptr, &real_len, nullptr, sodium_base64_VARIANT_ORIGINAL) != 0) {
        spdlog::error("Base64 decode failed");
        return {};
    }
    out.resize(real_len);
    return out;
#else
    std::vector<uint8_t> out(s.size());
    int outlen = EVP_DecodeBlock(out.data(),
        reinterpret_cast<const uint8_t*>(s.c_str()), static_cast<int>(s.size()));

    // Handle padding
    int padding = 0;
    if (s.size() >= 2) {
        if (s[s.size() - 1] == '=') padding++;
        if (s[s.size() - 2] == '=') padding++;
    }
    out.resize(outlen - padding);
    return out;
#endif
}

// ====== Additional Utilities ======

// Constant-time comparison (prevents timing attacks)
bool secure_compare(const uint8_t* a, const uint8_t* b, size_t len) {
#ifdef HAS_LIBSODIUM
    return sodium_memcmp(a, b, len) == 0;
#else
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
#endif
}

bool secure_compare(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    return secure_compare(
        reinterpret_cast<const uint8_t*>(a.data()),
        reinterpret_cast<const uint8_t*>(b.data()), a.size());
}

// Zero memory (prevents sensitive data from lingering)
void secure_zero(void* ptr, size_t len) {
#ifdef HAS_LIBSODIUM
    sodium_memzero(ptr, len);
#else
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    while (len--) *p++ = 0;
#endif
}

// Derive encryption key from password
std::vector<uint8_t> derive_key(const std::string& password,
    const std::vector<uint8_t>& salt, size_t key_len) {
    if (password.empty() || salt.empty()) return {};

#ifdef HAS_LIBSODIUM
    std::vector<uint8_t> key(key_len);
    if (crypto_pwhash(key.data(), key_len, password.c_str(), password.size(),
        salt.data(), crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE, crypto_pwhash_ALG_DEFAULT) != 0) {
        spdlog::error("Key derivation failed");
        return {};
    }
    return key;
#else
    std::vector<uint8_t> key(key_len);
    PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
        salt.data(), static_cast<int>(salt.size()), 100000,
        EVP_sha256(), static_cast<int>(key_len), key.data());
    return key;
#endif
}

// Generate a secure random token
std::string generate_token(size_t len) {
    auto bytes = random_bytes(len);
    return encode64(bytes.data(), bytes.size());
}

// Generate a random ID
std::string generate_id() {
    auto bytes = random_bytes(12);
    return encode64(bytes.data(), bytes.size()).substr(0, 16);
}

// Fingerprint for host key verification
std::string key_fingerprint(const uint8_t* key, size_t len) {
    auto hash = sha256(key, len);
    return hex_encode(hash.data(), 16); // first 16 bytes of hash
}

std::string key_fingerprint(const std::string& key) {
    return key_fingerprint(
        reinterpret_cast<const uint8_t*>(key.data()), key.size());
}

// ====== Key Exchange ======

struct KeyExchange {
    KeyPair ephemeral;
    std::array<uint8_t, SHA256_BYTES> session_key;
    bool complete = false;

    KeyExchange() : ephemeral(generate_box_keypair()) {}
};

KeyExchange begin_key_exchange() {
    return KeyExchange{};
}

std::vector<uint8_t> get_public_share(const KeyExchange& kx) {
    return std::vector<uint8_t>(kx.ephemeral.pk.begin(), kx.ephemeral.pk.end());
}

bool complete_key_exchange(KeyExchange& kx,
    const std::vector<uint8_t>& peer_public) {
    if (peer_public.size() != PUBLIC_KEY_BYTES) return false;

    uint8_t shared[32] = {};
    X25519(shared, kx.ephemeral.sk.data(), peer_public.data());

    kx.session_key = sha256(shared, 32);
    kx.complete = true;
    return true;
}

} // namespace cppdesk::common::crypto
