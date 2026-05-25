# Encryption Details

Key Exchange: X25519 ECDH
Signing: Ed25519
Stream: AES-256-GCM with random nonces
Password Hashing: Argon2id (libsodium) or PBKDF2-SHA256 (OpenSSL)
Content Hashing: SHA-256

Key Derivation: HKDF-SHA256
Session Keys: Derived per-connection, rotated periodically
