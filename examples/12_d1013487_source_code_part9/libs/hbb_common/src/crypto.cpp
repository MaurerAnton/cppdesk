// crypto.cpp — see crypto.hpp. libsodium called directly.
#include <hbb_common/crypto.hpp>

#include <sodium.h>

#include <stdexcept>

namespace hbb_common::crypto {
namespace {

void check_key(const std::vector<uint8_t>& key, size_t want, const char* what) {
    if (key.size() != want) {
        throw std::invalid_argument(std::string("crypto: bad ") + what);
    }
}

}  // namespace

std::pair<std::vector<uint8_t>, std::vector<uint8_t>> sign_gen_keypair() {
    std::vector<uint8_t> pk(crypto_sign_PUBLICKEYBYTES);
    std::vector<uint8_t> sk(crypto_sign_SECRETKEYBYTES);
    if (crypto_sign_keypair(pk.data(), sk.data()) != 0) {
        throw std::runtime_error("sign_gen_keypair failed");
    }
    return {pk, sk};
}

std::vector<uint8_t> sign_sign(const uint8_t* msg, size_t len,
                               const std::vector<uint8_t>& secret_key) {
    check_key(secret_key, crypto_sign_SECRETKEYBYTES, "sign secret key");
    std::vector<uint8_t> out(len + crypto_sign_BYTES);
    unsigned long long out_len = 0;
    if (crypto_sign(out.data(), &out_len, msg ? msg : reinterpret_cast<const uint8_t*>(""), len,
                    secret_key.data()) != 0) {
        throw std::runtime_error("sign_sign failed");
    }
    out.resize(out_len);
    return out;
}

std::vector<uint8_t> sign_verify(const uint8_t* signed_msg, size_t len,
                                 const std::vector<uint8_t>& public_key) {
    check_key(public_key, crypto_sign_PUBLICKEYBYTES, "sign public key");
    if (len < crypto_sign_BYTES) {
        throw std::runtime_error("sign_verify: message too short");
    }
    std::vector<uint8_t> out(len - crypto_sign_BYTES);
    unsigned long long out_len = 0;
    if (crypto_sign_open(out.data(), &out_len, signed_msg, len, public_key.data()) != 0) {
        throw std::runtime_error("sign_verify: bad signature");
    }
    out.resize(out_len);
    return out;
}

std::pair<std::vector<uint8_t>, std::vector<uint8_t>> box_gen_keypair() {
    std::vector<uint8_t> pk(crypto_box_PUBLICKEYBYTES);
    std::vector<uint8_t> sk(crypto_box_SECRETKEYBYTES);
    if (crypto_box_keypair(pk.data(), sk.data()) != 0) {
        throw std::runtime_error("box_gen_keypair failed");
    }
    return {pk, sk};
}

std::vector<uint8_t> box_seal(const uint8_t* msg, size_t len, const Nonce24& nonce,
                              const std::vector<uint8_t>& their_public_key,
                              const std::vector<uint8_t>& our_secret_key) {
    check_key(their_public_key, crypto_box_PUBLICKEYBYTES, "box public key");
    check_key(our_secret_key, crypto_box_SECRETKEYBYTES, "box secret key");
    std::vector<uint8_t> out(len + crypto_box_MACBYTES);
    if (crypto_box_easy(out.data(), msg ? msg : reinterpret_cast<const uint8_t*>(""), len,
                        nonce.data(), their_public_key.data(), our_secret_key.data()) != 0) {
        throw std::runtime_error("box_seal failed");
    }
    return out;
}

std::vector<uint8_t> box_open(const uint8_t* sealed_msg, size_t len, const Nonce24& nonce,
                              const std::vector<uint8_t>& their_public_key,
                              const std::vector<uint8_t>& our_secret_key) {
    check_key(their_public_key, crypto_box_PUBLICKEYBYTES, "box public key");
    check_key(our_secret_key, crypto_box_SECRETKEYBYTES, "box secret key");
    if (len < crypto_box_MACBYTES) {
        throw std::runtime_error("box_open: message too short");
    }
    std::vector<uint8_t> out(len - crypto_box_MACBYTES);
    if (crypto_box_open_easy(out.data(), sealed_msg, len, nonce.data(), their_public_key.data(),
                             our_secret_key.data()) != 0) {
        throw std::runtime_error("box_open: decryption failed");
    }
    return out;
}

std::vector<uint8_t> secretbox_gen_key() {
    std::vector<uint8_t> key(crypto_secretbox_KEYBYTES);
    crypto_secretbox_keygen(key.data());
    return key;
}

std::vector<uint8_t> secretbox_seal(const uint8_t* msg, size_t len, const Nonce24& nonce,
                                    const std::vector<uint8_t>& key) {
    check_key(key, crypto_secretbox_KEYBYTES, "secretbox key");
    std::vector<uint8_t> out(len + crypto_secretbox_MACBYTES);
    if (crypto_secretbox_easy(out.data(), msg ? msg : reinterpret_cast<const uint8_t*>(""), len,
                              nonce.data(), key.data()) != 0) {
        throw std::runtime_error("secretbox_seal failed");
    }
    return out;
}

std::vector<uint8_t> secretbox_open(const uint8_t* sealed_msg, size_t len, const Nonce24& nonce,
                                    const std::vector<uint8_t>& key) {
    check_key(key, crypto_secretbox_KEYBYTES, "secretbox key");
    if (len < crypto_secretbox_MACBYTES) {
        throw std::runtime_error("secretbox_open: message too short");
    }
    std::vector<uint8_t> out(len - crypto_secretbox_MACBYTES);
    if (crypto_secretbox_open_easy(out.data(), sealed_msg, len, nonce.data(), key.data()) != 0) {
        throw std::runtime_error("secretbox_open: decryption failed");
    }
    return out;
}

Nonce24 seq_nonce(uint64_t seqnum) {
    Nonce24 nonce{};
    for (int i = 0; i < 8; ++i) {  // to_ne_bytes parity (LE on all real targets)
        nonce[static_cast<size_t>(i)] = static_cast<uint8_t>((seqnum >> (8 * i)) & 0xFF);
    }
    return nonce;
}

}  // namespace hbb_common::crypto
