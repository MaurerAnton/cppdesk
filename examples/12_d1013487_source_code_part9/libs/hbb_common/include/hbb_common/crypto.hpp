// crypto.hpp — translation of the sodiumoxide surface used at this commit.
//
// Upstream uses sodiumoxide (libsodium bindings) in three places:
//   hbb_common config  -> sign::gen_keypair (lazy keypair creation)
//   hbb_common tcp     -> secretbox::{seal, open} (stream encryption)
//   src/client         -> sign::{verify, gen_keypair}, box_::{gen_keypair, seal},
//                         secretbox::{gen_key, seal}
// Here libsodium is called directly (system package, offline-safe lookup).
// All sizes mirror sodiumoxide constants (which mirror libsodium's).
// Errors throw std::runtime_error (sodiumoxide returns Err(()) on failure).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace hbb_common::crypto {

// sign::PUBLICKEYBYTES / SECRETKEYBYTES / SIGNATUREBYTES parity.
inline constexpr size_t kSignPublicKeyBytes = 32;
inline constexpr size_t kSignSecretKeyBytes = 64;
inline constexpr size_t kSignBytes = 64;

// box_::{PUBLICKEYBYTES, SECRETKEYBYTES, NONCEBYTES, MACBYTES} parity.
inline constexpr size_t kBoxPublicKeyBytes = 32;
inline constexpr size_t kBoxSecretKeyBytes = 32;
inline constexpr size_t kBoxNonceBytes = 24;

// secretbox::{KEYBYTES, NONCEBYTES, MACBYTES} parity.
inline constexpr size_t kSecretBoxKeyBytes = 32;
inline constexpr size_t kSecretBoxNonceBytes = 24;

using Nonce24 = std::array<uint8_t, kSecretBoxNonceBytes>;

// sign::gen_keypair parity: returns {public_key, secret_key}.
std::pair<std::vector<uint8_t>, std::vector<uint8_t>> sign_gen_keypair();

// sign::sign parity (message + 64-byte signature prepended).
std::vector<uint8_t> sign_sign(const uint8_t* msg, size_t len,
                               const std::vector<uint8_t>& secret_key);

// sign::verify parity: returns the message bytes; throws on bad signature.
std::vector<uint8_t> sign_verify(const uint8_t* signed_msg, size_t len,
                                 const std::vector<uint8_t>& public_key);

// box_::gen_keypair parity: returns {public_key, secret_key}.
std::pair<std::vector<uint8_t>, std::vector<uint8_t>> box_gen_keypair();

// box_::seal parity (authenticated public-key encryption).
std::vector<uint8_t> box_seal(const uint8_t* msg, size_t len, const Nonce24& nonce,
                              const std::vector<uint8_t>& their_public_key,
                              const std::vector<uint8_t>& our_secret_key);

// box_::open parity: throws on failure.
std::vector<uint8_t> box_open(const uint8_t* sealed_msg, size_t len, const Nonce24& nonce,
                              const std::vector<uint8_t>& their_public_key,
                              const std::vector<uint8_t>& our_secret_key);

// secretbox::gen_key parity.
std::vector<uint8_t> secretbox_gen_key();

// secretbox::seal / secretbox::open parity (open throws on failure).
std::vector<uint8_t> secretbox_seal(const uint8_t* msg, size_t len, const Nonce24& nonce,
                                    const std::vector<uint8_t>& key);
std::vector<uint8_t> secretbox_open(const uint8_t* sealed_msg, size_t len, const Nonce24& nonce,
                                    const std::vector<uint8_t>& key);

// Nonce construction shared by both stream directions (get_nonce parity):
// 24 zero bytes with the first 8 = little-endian seqnum.
Nonce24 seq_nonce(uint64_t seqnum);

}  // namespace hbb_common::crypto
