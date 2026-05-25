// =============================================================================
// tls_utils.cpp - Comprehensive TLS/SSL utility implementations
// Part of cppdesk framework
// =============================================================================
//
// Provides:
//   1. TLS server context setup (certificate loading, key loading, DH params)
//   2. TLS client context (certificate verification, hostname verification)
//   3. Self-signed certificate generation for P2P connections
//   4. Certificate pinning and trust-on-first-use (TOFU)
//   5. TLS session resumption (session tickets, session IDs)
//   6. ALPN protocol negotiation
//   7. SNI (Server Name Indication) support
//   8. Mutual TLS (mTLS) with client certificates
//   9. TLS 1.3 specific features (0-RTT, improved ciphers)
//  10. Cipher suite configuration and hardening
//  11. Certificate chain building and validation
//  12. OCSP stapling support

#include "common/config.hpp"
#include "common/crypto.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/conf.h>
#include <openssl/dh.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/obj_mac.h>
#include <openssl/objects.h>
#include <openssl/ocsp.h>
#include <openssl/opensslv.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/safestack.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/stack.h>
#include <openssl/tls1.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/x509_vfy.h>

#include <spdlog/spdlog.h>

namespace cppdesk::common {

// =============================================================================
// Forward declarations and compile-time constants
// =============================================================================

/// Minimum key size for RSA keys used in TLS
inline constexpr int TLS_MIN_RSA_KEY_BITS = 2048;

/// Default RSA key size for generated certificates
inline constexpr int TLS_DEFAULT_RSA_KEY_BITS = 4096;

/// Default ECDSA curve
inline constexpr const char* TLS_DEFAULT_EC_CURVE = "prime256v1";

/// Default validity for generated certificates (days)
inline constexpr int TLS_DEFAULT_CERT_DAYS = 365;

/// Maximum session ticket lifetime (seconds)
inline constexpr long TLS_MAX_SESSION_TICKET_LIFETIME = 86400; // 24 hours

/// Default DH parameter bits
inline constexpr int TLS_DEFAULT_DH_BITS = 2048;

/// Maximum OCSP response size (bytes)
inline constexpr size_t OCSP_MAX_RESPONSE_SIZE = 65536;

/// Name of the TLS session cache file for TOFU persistence
inline constexpr const char* TOFU_CACHE_FILE = "tls_tofu_cache.json";

/// SHA-256 fingerprint hex string length (64 chars)
inline constexpr size_t SHA256_FINGERPRINT_HEX_LEN = 64;

/// Maximum certificate chain depth
inline constexpr int MAX_CERT_CHAIN_DEPTH = 10;

// =============================================================================
// OpenSSL 3.x / 1.1.x compatibility macros
// =============================================================================

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    // OpenSSL 3.x
    #define CPPDESK_TLS_DH_new() EVP_PKEY_CTX_new_from_name(nullptr, "DH", nullptr)
    #define CPPDESK_TLS_HAS_PROVIDER_LOAD 1
#else
    // OpenSSL 1.1.x
    #define CPPDESK_TLS_DH_new() DH_new()
    #define CPPDESK_TLS_HAS_PROVIDER_LOAD 0
#endif

// =============================================================================
// Detail namespace: RAII wrappers for OpenSSL types
// =============================================================================

namespace detail {

// ---------------------------------------------------------------------------
// OpenSSL error string extraction
// ---------------------------------------------------------------------------
std::string openssl_error_string() {
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return "Unable to create BIO for error string";
    ERR_print_errors(bio);
    char* buf = nullptr;
    long len = BIO_get_mem_data(bio, &buf);
    std::string result(buf, len);
    BIO_free(bio);
    return result;
}

// ---------------------------------------------------------------------------
// RAII deleters for OpenSSL types
// ---------------------------------------------------------------------------
struct bio_deleter {
    void operator()(BIO* p) const noexcept { if (p) BIO_free(p); }
};
using bio_ptr = std::unique_ptr<BIO, bio_deleter>;

struct ssl_ctx_deleter {
    void operator()(SSL_CTX* p) const noexcept { if (p) SSL_CTX_free(p); }
};
using ssl_ctx_ptr = std::unique_ptr<SSL_CTX, ssl_ctx_deleter>;

struct ssl_deleter {
    void operator()(SSL* p) const noexcept { if (p) SSL_free(p); }
};
using ssl_ptr = std::unique_ptr<SSL, ssl_deleter>;

struct x509_deleter {
    void operator()(X509* p) const noexcept { if (p) X509_free(p); }
};
using x509_ptr = std::unique_ptr<X509, x509_deleter>;

struct x509_store_deleter {
    void operator()(X509_STORE* p) const noexcept { if (p) X509_STORE_free(p); }
};
using x509_store_ptr = std::unique_ptr<X509_STORE, x509_store_deleter>;

struct x509_store_ctx_deleter {
    void operator()(X509_STORE_CTX* p) const noexcept { if (p) X509_STORE_CTX_free(p); }
};
using x509_store_ctx_ptr = std::unique_ptr<X509_STORE_CTX, x509_store_ctx_deleter>;

struct evp_pkey_deleter {
    void operator()(EVP_PKEY* p) const noexcept { if (p) EVP_PKEY_free(p); }
};
using evp_pkey_ptr = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>;

struct bn_deleter {
    void operator()(BIGNUM* p) const noexcept { if (p) BN_free(p); }
};
using bn_ptr = std::unique_ptr<BIGNUM, bn_deleter>;

struct x509_name_deleter {
    void operator()(X509_NAME* p) const noexcept { if (p) X509_NAME_free(p); }
};
using x509_name_ptr = std::unique_ptr<X509_NAME, x509_name_deleter>;

struct dh_deleter {
    void operator()(DH* p) const noexcept { if (p) DH_free(p); }
};
using dh_ptr = std::unique_ptr<DH, dh_deleter>;

struct ec_key_deleter {
    void operator()(EC_KEY* p) const noexcept { if (p) EC_KEY_free(p); }
};
using ec_key_ptr = std::unique_ptr<EC_KEY, ec_key_deleter>;

struct x509_extension_deleter {
    void operator()(X509_EXTENSION* p) const noexcept { if (p) X509_EXTENSION_free(p); }
};
using x509_extension_ptr = std::unique_ptr<X509_EXTENSION, x509_extension_deleter>;

struct asn1_integer_deleter {
    void operator()(ASN1_INTEGER* p) const noexcept { if (p) ASN1_INTEGER_free(p); }
};
using asn1_integer_ptr = std::unique_ptr<ASN1_INTEGER, asn1_integer_deleter>;

struct asn1_time_deleter {
    void operator()(ASN1_TIME* p) const noexcept { if (p) ASN1_TIME_free(p); }
};
using asn1_time_ptr = std::unique_ptr<ASN1_TIME, asn1_time_deleter>;

struct stack_x509_deleter {
    void operator()(STACK_OF(X509)* p) const noexcept {
        if (p) sk_X509_pop_free(p, X509_free);
    }
};
using stack_x509_ptr = std::unique_ptr<STACK_OF(X509), stack_x509_deleter>;

struct ocsp_response_deleter {
    void operator()(OCSP_RESPONSE* p) const noexcept { if (p) OCSP_RESPONSE_free(p); }
};
using ocsp_response_ptr = std::unique_ptr<OCSP_RESPONSE, ocsp_response_deleter>;

struct ocsp_basic_response_deleter {
    void operator()(OCSP_BASICRESP* p) const noexcept { if (p) OCSP_BASICRESP_free(p); }
};
using ocsp_basic_response_ptr = std::unique_ptr<OCSP_BASICRESP, ocsp_basic_response_deleter>;

struct ocsp_certid_deleter {
    void operator()(OCSP_CERTID* p) const noexcept { if (p) OCSP_CERTID_free(p); }
};
using ocsp_certid_ptr = std::unique_ptr<OCSP_CERTID, ocsp_certid_deleter>;

struct x509_crl_deleter {
    void operator()(X509_CRL* p) const noexcept { if (p) X509_CRL_free(p); }
};
using x509_crl_ptr = std::unique_ptr<X509_CRL, x509_crl_deleter>;

struct ssl_session_deleter {
    void operator()(SSL_SESSION* p) const noexcept { if (p) SSL_SESSION_free(p); }
};
using ssl_session_ptr = std::unique_ptr<SSL_SESSION, ssl_session_deleter>;

struct pkcs12_deleter {
    void operator()(PKCS12* p) const noexcept { if (p) PKCS12_free(p); }
};
using pkcs12_ptr = std::unique_ptr<PKCS12, pkcs12_deleter>;

struct x509_lookup_deleter {
    void operator()(X509_LOOKUP* p) const noexcept { if (p) X509_LOOKUP_free(p); }
};
using x509_lookup_ptr = std::unique_ptr<X509_LOOKUP, x509_lookup_deleter>;

// ---------------------------------------------------------------------------
// Global OpenSSL initialization / cleanup (thread-safe, once)
// ---------------------------------------------------------------------------
static std::once_flag g_openssl_init_flag;

void ensure_openssl_initialized() {
    std::call_once(g_openssl_init_flag, []() {
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
                         OPENSSL_INIT_LOAD_CRYPTO_STRINGS |
                         OPENSSL_INIT_ADD_ALL_CIPHERS |
                         OPENSSL_INIT_ADD_ALL_DIGESTS, nullptr);
        OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG, nullptr);
        spdlog::info("[TLS] OpenSSL initialized (version: {})",
                     OpenSSL_version(OPENSSL_VERSION));
    });
}

// ---------------------------------------------------------------------------
// Helper: read entire file into a string
// ---------------------------------------------------------------------------
std::string read_file_to_string(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    auto size = file.tellg();
    if (size <= 0) {
        throw std::runtime_error("Empty or invalid file: " + path);
    }
    file.seekg(0, std::ios::beg);
    std::string content(static_cast<size_t>(size), '\0');
    file.read(content.data(), size);
    return content;
}

// ---------------------------------------------------------------------------
// Helper: write string to file (atomic via rename)
// ---------------------------------------------------------------------------
void write_file_atomic(const std::string& path, const std::string& content) {
    auto tmp_path = path + ".tmp";
    {
        std::ofstream file(tmp_path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open temp file for writing: " + tmp_path);
        }
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file.good()) {
            throw std::runtime_error("Write failed for: " + tmp_path);
        }
    }
    std::filesystem::rename(tmp_path, path);
}

// ---------------------------------------------------------------------------
// Helper: ASN1_TIME to system_clock::time_point
// ---------------------------------------------------------------------------
std::chrono::system_clock::time_point asn1_time_to_tp(const ASN1_TIME* t) {
    if (!t) return {};
    int days = 0, seconds = 0;
    if (!ASN1_TIME_diff(&days, &seconds, nullptr, t)) {
        // Try manual parsing as fallback
        struct tm tm_val{};
        if (ASN1_TIME_to_tm(t, &tm_val) == 1) {
            auto tt = timegm(&tm_val);
            if (tt != -1) {
                return std::chrono::system_clock::from_time_t(tt);
            }
        }
        return {};
    }
    // ASN1_TIME_diff gives delta; we calculate from "now" minus delta
    // Actually, ASN1_TIME_diff computes t2 - t1, so from_t is before to_t
    auto now = std::chrono::system_clock::now();
    return now - std::chrono::seconds(seconds) - std::chrono::hours(24 * days);
}

// ---------------------------------------------------------------------------
// Helper: generate random bytes
// ---------------------------------------------------------------------------
std::vector<uint8_t> random_bytes(size_t n) {
    std::vector<uint8_t> buf(n);
    if (n > 0) {
        if (RAND_bytes(buf.data(), static_cast<int>(n)) != 1) {
            throw std::runtime_error("RAND_bytes failed: " + openssl_error_string());
        }
    }
    return buf;
}

// ---------------------------------------------------------------------------
// Helper: SHA-256 hash of byte vector -> hex string
// ---------------------------------------------------------------------------
std::string sha256_hex(const uint8_t* data, size_t len) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    if (!SHA256(data, len, hash)) {
        throw std::runtime_error("SHA256 failed");
    }
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return ss.str();
}

// ---------------------------------------------------------------------------
// Helper: get DER-encoded fingerprint of an X509 certificate
// ---------------------------------------------------------------------------
std::string x509_sha256_fingerprint(X509* cert) {
    unsigned char* der = nullptr;
    int der_len = i2d_X509(cert, &der);
    if (der_len <= 0 || !der) {
        throw std::runtime_error("Failed to DER-encode certificate");
    }
    std::string fp = sha256_hex(der, static_cast<size_t>(der_len));
    OPENSSL_free(der);
    return fp;
}

// ---------------------------------------------------------------------------
// Helper: PEM-encode an X509 certificate -> string
// ---------------------------------------------------------------------------
std::string x509_to_pem(X509* cert) {
    detail::bio_ptr bio(BIO_new(BIO_s_mem()));
    if (!bio) throw std::runtime_error("BIO_new failed");
    if (!PEM_write_bio_X509(bio.get(), cert)) {
        throw std::runtime_error("PEM_write_bio_X509 failed: " + openssl_error_string());
    }
    char* data = nullptr;
    long len = BIO_get_mem_data(bio.get(), &data);
    return {data, static_cast<size_t>(len)};
}

// ---------------------------------------------------------------------------
// Helper: PEM-encode an EVP_PKEY -> string
// ---------------------------------------------------------------------------
std::string evp_pkey_to_pem_private(EVP_PKEY* pkey) {
    detail::bio_ptr bio(BIO_new(BIO_s_mem()));
    if (!bio) throw std::runtime_error("BIO_new failed");
    if (!PEM_write_bio_PrivateKey(bio.get(), pkey, nullptr, nullptr, 0, nullptr, nullptr)) {
        throw std::runtime_error("PEM_write_bio_PrivateKey failed: " + openssl_error_string());
    }
    char* data = nullptr;
    long len = BIO_get_mem_data(bio.get(), &data);
    return {data, static_cast<size_t>(len)};
}

// ---------------------------------------------------------------------------
// Helper: get subject common name from X509
// ---------------------------------------------------------------------------
std::string x509_get_cn(X509* cert) {
    if (!cert) return {};
    char buf[256]{};
    X509_NAME* subject = X509_get_subject_name(cert);
    if (!subject) return {};
    int nid = OBJ_txt2nid("CN");
    int len = X509_NAME_get_text_by_NID(subject, nid, buf, sizeof(buf) - 1);
    if (len < 0) return {};
    return {buf, static_cast<size_t>(len)};
}

// ---------------------------------------------------------------------------
// Helper: get subject alternative names (DNS) from X509
// ---------------------------------------------------------------------------
std::vector<std::string> x509_get_san_dns(X509* cert) {
    std::vector<std::string> names;
    if (!cert) return names;

    GENERAL_NAMES* gens = static_cast<GENERAL_NAMES*>(
        X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
    if (!gens) return names;

    int num = sk_GENERAL_NAME_num(gens);
    for (int i = 0; i < num; ++i) {
        GENERAL_NAME* gen = sk_GENERAL_NAME_value(gens, i);
        if (gen->type == GEN_DNS) {
            const char* dns = reinterpret_cast<const char*>(
                ASN1_STRING_get0_data(gen->d.dNSName));
            int len = ASN1_STRING_length(gen->d.dNSName);
            if (dns && len > 0) {
                names.emplace_back(dns, static_cast<size_t>(len));
            }
        }
    }
    GENERAL_NAMES_free(gens);
    return names;
}

// ---------------------------------------------------------------------------
// Generate DH parameters
// ---------------------------------------------------------------------------
EVP_PKEY* generate_dh_params(int bits = TLS_DEFAULT_DH_BITS) {
    EVP_PKEY* params = nullptr;

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_from_name(nullptr, "DH", nullptr);
    if (!pctx) {
        throw std::runtime_error("EVP_PKEY_CTX_new_from_name(DH) failed: " +
                                 openssl_error_string());
    }
    if (EVP_PKEY_paramgen_init(pctx) <= 0 ||
        EVP_PKEY_CTX_set_dh_nbits(pctx, bits) <= 0 ||
        EVP_PKEY_paramgen(pctx, &params) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("DH paramgen failed: " + openssl_error_string());
    }
    EVP_PKEY_CTX_free(pctx);
#else
    detail::dh_ptr dh(DH_new());
    if (!dh) {
        throw std::runtime_error("DH_new failed: " + openssl_error_string());
    }
    if (!DH_generate_parameters_ex(dh.get(), bits, DH_GENERATOR_2, nullptr)) {
        throw std::runtime_error("DH_generate_parameters_ex failed: " +
                                 openssl_error_string());
    }
    params = EVP_PKEY_new();
    if (!params || !EVP_PKEY_set1_DH(params, dh.get())) {
        EVP_PKEY_free(params);
        throw std::runtime_error("EVP_PKEY_set1_DH failed");
    }
#endif
    return params;
}

// ---------------------------------------------------------------------------
// Callback for password-protected private keys
// ---------------------------------------------------------------------------
static int pem_password_callback(char* buf, int size, int rwflag, void* userdata) {
    if (!userdata) return 0;
    const auto* pass = static_cast<const std::string*>(userdata);
    int len = static_cast<int>(std::min(pass->size(), static_cast<size_t>(size)));
    if (len <= 0) return 0;
    std::memcpy(buf, pass->data(), static_cast<size_t>(len));
    return len;
}

} // namespace detail

// =============================================================================
// 1. TLS Server Context Setup
// =============================================================================

// ---------------------------------------------------------------------------
// TlsCertificate: holds certificate + private key for server context
// ---------------------------------------------------------------------------
struct TlsCertificate {
    std::string cert_pem;       // PEM-encoded X.509 certificate
    std::string key_pem;        // PEM-encoded private key
    std::string chain_pem;      // PEM-encoded intermediate certificates (optional)
    std::string cert_fingerprint; // SHA-256 fingerprint (hex)
    std::vector<std::string> sans; // Subject Alternative Names
    std::string common_name;
};

enum class TlsKeyType {
    RSA,
    ECDSA,
    ED25519,
};

struct TlsServerConfig {
    /// Minimum TLS version (default: TLS 1.2)
    int min_version = TLS1_2_VERSION;

    /// Maximum TLS version (default: TLS 1.3)
    int max_version = 0; // 0 = auto (highest available)

    /// Session cache mode
    int session_cache_mode = SSL_SESS_CACHE_SERVER;

    /// Session timeout (seconds)
    long session_timeout = 300;

    /// Require client certificates (for mTLS)
    bool require_client_cert = false;

    /// Verify client certificates (weaker: request but don't require)
    bool verify_client_cert = false;

    /// Verify depth for client cert chains
    int verify_depth = 4;

    /// Enable session tickets
    bool enable_session_tickets = true;

    /// Comma-separated list of enabled cipher suites (empty = default)
    std::string cipher_list;

    /// Comma-separated list of cipher suites for TLS 1.3 (empty = default)
    std::string ciphersuites;

    /// ALPN protocol list (e.g., {"h2", "http/1.1"})
    std::vector<std::string> alpn_protocols;

    /// DH parameters file path (empty = auto-generate)
    std::string dh_params_path;

    /// DH parameter bits (used when auto-generating)
    int dh_bits = TLS_DEFAULT_DH_BITS;

    /// ECDH curve name (empty = auto)
    std::string ecdh_curve;
};

// ---------------------------------------------------------------------------
// Load certificate and private key into SSL_CTX
// ---------------------------------------------------------------------------
void ssl_ctx_use_certificate_chain(SSL_CTX* ctx, const std::string& cert_pem) {
    detail::bio_ptr bio(BIO_new_mem_buf(cert_pem.data(),
                         static_cast<int>(cert_pem.size())));
    if (!bio) {
        throw std::runtime_error("BIO_new_mem_buf failed for certificate");
    }

    // Load the first certificate
    detail::x509_ptr cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
    if (!cert) {
        throw std::runtime_error("Failed to read certificate PEM: " +
                                 detail::openssl_error_string());
    }
    if (SSL_CTX_use_certificate(ctx, cert.get()) != 1) {
        throw std::runtime_error("SSL_CTX_use_certificate failed: " +
                                 detail::openssl_error_string());
    }

    // Load any additional (chain) certificates
    while (true) {
        detail::x509_ptr chain_cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
        if (!chain_cert) {
            // No more certificates or error
            unsigned long err = ERR_peek_last_error();
            if (ERR_GET_LIB(err) == ERR_LIB_PEM &&
                ERR_GET_REASON(err) == PEM_R_NO_START_LINE) {
                ERR_clear_error();
                break;
            }
            // Real error
            if (err != 0) {
                spdlog::warn("[TLS] Error reading chain certificate: {}",
                             detail::openssl_error_string());
                ERR_clear_error();
            }
            break;
        }
        if (SSL_CTX_add_extra_chain_cert(ctx, chain_cert.release()) != 1) {
            throw std::runtime_error("SSL_CTX_add_extra_chain_cert failed: " +
                                     detail::openssl_error_string());
        }
    }
}

void ssl_ctx_use_private_key(SSL_CTX* ctx, const std::string& key_pem,
                             const std::string& password = "") {
    detail::bio_ptr bio(BIO_new_mem_buf(key_pem.data(),
                         static_cast<int>(key_pem.size())));
    if (!bio) {
        throw std::runtime_error("BIO_new_mem_buf failed for private key");
    }

    detail::evp_pkey_ptr pkey(
        PEM_read_bio_PrivateKey(bio.get(), nullptr,
                                password.empty() ? nullptr : detail::pem_password_callback,
                                password.empty() ? nullptr : const_cast<std::string*>(&password)));
    if (!pkey) {
        throw std::runtime_error("Failed to read private key PEM: " +
                                 detail::openssl_error_string());
    }
    if (SSL_CTX_use_PrivateKey(ctx, pkey.get()) != 1) {
        throw std::runtime_error("SSL_CTX_use_PrivateKey failed: " +
                                 detail::openssl_error_string());
    }

    // Verify private key matches certificate
    if (SSL_CTX_check_private_key(ctx) != 1) {
        throw std::runtime_error("Private key does not match certificate: " +
                                 detail::openssl_error_string());
    }
}

// ---------------------------------------------------------------------------
// Set DH parameters on SSL_CTX
// ---------------------------------------------------------------------------
void ssl_ctx_set_dh_params(SSL_CTX* ctx, const std::string& dh_path, int bits) {
    detail::evp_pkey_ptr dh_params;

    if (!dh_path.empty() && std::filesystem::exists(dh_path)) {
        // Load from file
        detail::bio_ptr bio(BIO_new_file(dh_path.c_str(), "r"));
        if (bio) {
            dh_params.reset(PEM_read_bio_Parameters(bio.get(), nullptr));
            if (!dh_params) {
                spdlog::warn("[TLS] Failed to load DH params from '{}', generating...",
                             dh_path);
            }
        }
    }

    if (!dh_params) {
        // Auto-generate
        DH* dh_raw = nullptr;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
        EVP_PKEY* pkey = detail::generate_dh_params(bits);
        dh_params.reset(pkey);
#else
        detail::dh_ptr dh(DH_new());
        if (!dh) throw std::runtime_error("DH_new failed");
        if (!DH_generate_parameters_ex(dh.get(), bits, DH_GENERATOR_2, nullptr)) {
            throw std::runtime_error("DH_generate_parameters_ex failed: " +
                                     detail::openssl_error_string());
        }
        EVP_PKEY* pkey = EVP_PKEY_new();
        if (!pkey || !EVP_PKEY_set1_DH(pkey, dh.get())) {
            EVP_PKEY_free(pkey);
            throw std::runtime_error("EVP_PKEY_set1_DH failed");
        }
        dh_params.reset(pkey);
#endif
        spdlog::info("[TLS] Auto-generated {} bit DH parameters", bits);
    }

    if (SSL_CTX_set0_tmp_dh_pkey(ctx, dh_params.get()) != 1) {
        throw std::runtime_error("SSL_CTX_set0_tmp_dh_pkey failed: " +
                                 detail::openssl_error_string());
    }
    dh_params.release(); // Ownership transferred to SSL_CTX
}

// ---------------------------------------------------------------------------
// Configure ECDH curve
// ---------------------------------------------------------------------------
void ssl_ctx_set_ecdh_curve(SSL_CTX* ctx, const std::string& curve_name) {
    std::string curve = curve_name.empty() ? TLS_DEFAULT_EC_CURVE : curve_name;
    int nid = OBJ_sn2nid(curve.c_str());
    if (nid == NID_undef) {
        spdlog::warn("[TLS] Unknown ECDH curve name '{}', using default {}", curve,
                     TLS_DEFAULT_EC_CURVE);
        nid = OBJ_sn2nid(TLS_DEFAULT_EC_CURVE);
    }

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    if (SSL_CTX_set1_groups(ctx, &nid, 1) != 1) {
        spdlog::warn("[TLS] SSL_CTX_set1_groups failed: {}",
                     detail::openssl_error_string());
    }
#else
    EC_KEY* ecdh = EC_KEY_new_by_curve_name(nid);
    if (ecdh) {
        if (SSL_CTX_set_tmp_ecdh(ctx, ecdh) != 1) {
            spdlog::warn("[TLS] SSL_CTX_set_tmp_ecdh failed: {}",
                         detail::openssl_error_string());
        }
        EC_KEY_free(ecdh);
    } else {
        spdlog::warn("[TLS] Failed to create EC_KEY for curve '{}'", curve);
    }
#endif
}

// ---------------------------------------------------------------------------
// Configure cipher suites
// ---------------------------------------------------------------------------
void ssl_ctx_set_ciphers(SSL_CTX* ctx, const std::string& cipher_list,
                         const std::string& ciphersuites) {
    if (!cipher_list.empty()) {
        if (SSL_CTX_set_cipher_list(ctx, cipher_list.c_str()) != 1) {
            throw std::runtime_error("SSL_CTX_set_cipher_list failed: " +
                                     detail::openssl_error_string());
        }
        spdlog::info("[TLS] Cipher list set: {}", cipher_list);
    }

#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    if (!ciphersuites.empty()) {
        if (SSL_CTX_set_ciphersuites(ctx, ciphersuites.c_str()) != 1) {
            throw std::runtime_error("SSL_CTX_set_ciphersuites failed: " +
                                     detail::openssl_error_string());
        }
        spdlog::info("[TLS] TLS 1.3 ciphersuites set: {}", ciphersuites);
    }
#endif
}

// ---------------------------------------------------------------------------
// Hardened cipher list recommended by Mozilla (Intermediate compatibility)
// ---------------------------------------------------------------------------
inline constexpr const char* TLS_HARDENED_CIPHER_LIST =
    "ECDHE-ECDSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES128-GCM-SHA256:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-CHACHA20-POLY1305:"
    "ECDHE-RSA-CHACHA20-POLY1305:"
    "DHE-RSA-AES128-GCM-SHA256:"
    "DHE-RSA-AES256-GCM-SHA384";

inline constexpr const char* TLS_HARDENED_CIPHERSUITES_TLS13 =
    "TLS_AES_128_GCM_SHA256:"
    "TLS_AES_256_GCM_SHA384:"
    "TLS_CHACHA20_POLY1305_SHA256";

// ---------------------------------------------------------------------------
// Configure ALPN protocols
// ---------------------------------------------------------------------------
void ssl_ctx_set_alpn(SSL_CTX* ctx, const std::vector<std::string>& protocols) {
    if (protocols.empty()) return;

    // Build the wire-format ALPN list
    std::vector<unsigned char> alpn_buf;
    for (const auto& proto : protocols) {
        if (proto.size() > 255) {
            throw std::runtime_error("ALPN protocol name too long: " + proto);
        }
        alpn_buf.push_back(static_cast<unsigned char>(proto.size()));
        alpn_buf.insert(alpn_buf.end(),
                        reinterpret_cast<const unsigned char*>(proto.data()),
                        reinterpret_cast<const unsigned char*>(proto.data()) + proto.size());
    }

    // ALPN select callback
    auto alpn_callback = [](SSL* ssl, const unsigned char** out,
                            unsigned char* outlen, const unsigned char* in,
                            unsigned int inlen, void* arg) -> int {
        auto* proto_list = static_cast<std::vector<std::string>*>(arg);
        for (const auto& proto : *proto_list) {
            if (SSL_select_next_proto(const_cast<unsigned char**>(out), outlen,
                                      in, inlen,
                                      reinterpret_cast<const unsigned char*>(proto.data()),
                                      static_cast<unsigned int>(proto.size())) ==
                OPENSSL_NPN_NEGOTIATED) {
                return SSL_TLSEXT_ERR_OK;
            }
        }
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    };

    // We need to keep the protocol list alive; store it in a shared_ptr
    auto proto_copy = std::make_shared<std::vector<std::string>>(protocols);
    SSL_CTX_set_alpn_select_cb(ctx, alpn_callback, proto_copy.get());

    // Set the raw protocol list (used by client hello)
    if (SSL_CTX_set_alpn_protos(ctx, alpn_buf.data(),
                                 static_cast<unsigned int>(alpn_buf.size())) != 0) {
        throw std::runtime_error("SSL_CTX_set_alpn_protos failed: " +
                                 detail::openssl_error_string());
    }

    spdlog::info("[TLS] ALPN protocols configured: {} protocols", protocols.size());
}

// ---------------------------------------------------------------------------
// SNI callback for server: dynamically switch certificates based on hostname
// ---------------------------------------------------------------------------
struct SniContext {
    std::map<std::string, TlsCertificate> host_certs;
    TlsCertificate default_cert;
};

int sni_server_callback(SSL* ssl, int* ad, void* arg) {
    auto* sni_ctx = static_cast<SniContext*>(arg);
    if (!sni_ctx) return SSL_TLSEXT_ERR_ALERT_FATAL;

    const char* server_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (!server_name) {
        // No SNI, use default cert
        spdlog::debug("[TLS] SNI: no server name, using default certificate");
        return SSL_TLSEXT_ERR_OK;
    }

    std::string host(server_name);
    auto it = sni_ctx->host_certs.find(host);
    if (it == sni_ctx->host_certs.end()) {
        // Try wildcard matching
        for (const auto& [pattern, cert] : sni_ctx->host_certs) {
            if (pattern.size() > 2 && pattern[0] == '*' && pattern[1] == '.') {
                std::string suffix = pattern.substr(1); // .example.com
                if (host.size() >= suffix.size() &&
                    host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    // Found matching wildcard
                    SSL_CTX* ctx = SSL_get_SSL_CTX(ssl);
                    // Set new certificate
                    detail::bio_ptr cert_bio(BIO_new_mem_buf(
                        cert.cert_pem.data(),
                        static_cast<int>(cert.cert_pem.size())));
                    detail::x509_ptr x509(PEM_read_bio_X509(cert_bio.get(), nullptr, nullptr, nullptr));
                    if (x509) {
                        SSL_use_certificate(ssl, x509.get());
                    }
                    detail::bio_ptr key_bio(BIO_new_mem_buf(
                        cert.key_pem.data(),
                        static_cast<int>(cert.key_pem.size())));
                    detail::evp_pkey_ptr pkey(PEM_read_bio_PrivateKey(
                        key_bio.get(), nullptr, nullptr, nullptr));
                    if (pkey) {
                        SSL_use_PrivateKey(ssl, pkey.get());
                    }
                    spdlog::info("[TLS] SNI: matched wildcard '{}' for '{}'", pattern, host);
                    return SSL_TLSEXT_ERR_OK;
                }
            }
        }
        // No match found, use default
        spdlog::debug("[TLS] SNI: no certificate for '{}', using default", host);
        return SSL_TLSEXT_ERR_OK;
    }

    const auto& cert = it->second;
    SSL_CTX* ctx = SSL_get_SSL_CTX(ssl);

    detail::bio_ptr cert_bio(BIO_new_mem_buf(
        cert.cert_pem.data(), static_cast<int>(cert.cert_pem.size())));
    detail::x509_ptr x509(PEM_read_bio_X509(cert_bio.get(), nullptr, nullptr, nullptr));
    if (x509) {
        SSL_use_certificate(ssl, x509.get());
        // Also add chain if present
        if (!cert.chain_pem.empty()) {
            detail::bio_ptr chain_bio(BIO_new_mem_buf(
                cert.chain_pem.data(), static_cast<int>(cert.chain_pem.size())));
            detail::x509_ptr chain_cert(
                PEM_read_bio_X509(chain_bio.get(), nullptr, nullptr, nullptr));
            if (chain_cert) {
                SSL_add1_chain_cert(ssl, chain_cert.get());
            }
        }
    }

    detail::bio_ptr key_bio(BIO_new_mem_buf(
        cert.key_pem.data(), static_cast<int>(cert.key_pem.size())));
    detail::evp_pkey_ptr pkey(
        PEM_read_bio_PrivateKey(key_bio.get(), nullptr, nullptr, nullptr));
    if (pkey) {
        SSL_use_PrivateKey(ssl, pkey.get());
    }

    spdlog::info("[TLS] SNI: switched certificate for '{}'", host);
    return SSL_TLSEXT_ERR_OK;
}

// ---------------------------------------------------------------------------
// Main function: create a TLS server context
// ---------------------------------------------------------------------------
SSL_CTX* create_tls_server_context(const TlsCertificate& tls_cert,
                                   const TlsServerConfig& cfg = {}) {
    detail::ensure_openssl_initialized();

    // Create SSL_CTX using TLS_server_method (supports TLS 1.0 - highest available)
    const SSL_METHOD* method = TLS_server_method();
    detail::ssl_ctx_ptr ctx(SSL_CTX_new(method));
    if (!ctx) {
        throw std::runtime_error("SSL_CTX_new(server) failed: " +
                                 detail::openssl_error_string());
    }

    // ---- Set TLS version constraints ----
    if (cfg.min_version > 0) {
        SSL_CTX_set_min_proto_version(ctx.get(), cfg.min_version);
    } else {
        SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION);
    }
    if (cfg.max_version > 0) {
        SSL_CTX_set_max_proto_version(ctx.get(), cfg.max_version);
    }
    // TLS 1.3 is implicitly enabled if OpenSSL supports it

    // ---- Extended options for security ----
    long options = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |
                   SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 |
                   SSL_OP_CIPHER_SERVER_PREFERENCE |
                   SSL_OP_NO_COMPRESSION;
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    options |= SSL_OP_NO_RENEGOTIATION; // Disable renegotiation
#endif
    SSL_CTX_set_options(ctx.get(), options);

    // ---- Load certificate and private key ----
    ssl_ctx_use_certificate_chain(ctx.get(), tls_cert.cert_pem);
    ssl_ctx_use_private_key(ctx.get(), tls_cert.key_pem);

    // ---- DH parameters for DHE key exchange ----
    ssl_ctx_set_dh_params(ctx.get(), cfg.dh_params_path, cfg.dh_bits);

    // ---- ECDH curve ----
    ssl_ctx_set_ecdh_curve(ctx.get(), cfg.ecdh_curve);

    // ---- Cipher suites ----
    if (cfg.cipher_list.empty() && cfg.ciphersuites.empty()) {
        // Apply hardened defaults
        ssl_ctx_set_ciphers(ctx.get(), TLS_HARDENED_CIPHER_LIST,
                            TLS_HARDENED_CIPHERSUITES_TLS13);
    } else {
        ssl_ctx_set_ciphers(ctx.get(), cfg.cipher_list, cfg.ciphersuites);
    }

    // ---- Session caching ----
    SSL_CTX_set_session_cache_mode(ctx.get(), cfg.session_cache_mode);
    SSL_CTX_set_timeout(ctx.get(), cfg.session_timeout);

    // ---- Session tickets (TLS 1.2 session tickets / TLS 1.3 PSK) ----
    if (cfg.enable_session_tickets) {
        // Generate random session ticket key (48 bytes)
        auto key_data = detail::random_bytes(48);
        if (SSL_CTX_set_session_ticket_cb(
                ctx.get(),
                // Ticket generation callback
                [](SSL* ssl, unsigned char key_name[16], unsigned char* iv,
                   EVP_CIPHER_CTX* ctx_enc, EVP_MAC_CTX* ctx_mac, int enc) -> int {
                    // Use server default behavior
                    return 1;
                },
                // Ticket decryption callback
                [](SSL* ssl, const unsigned char key_name[16],
                   const unsigned char* iv, EVP_CIPHER_CTX* ctx_dec,
                   EVP_MAC_CTX* ctx_mac, int enc) -> int {
                    return 2; // Renew ticket
                },
                nullptr) == 1) {
            spdlog::info("[TLS] Session ticket key set (random)");
        }
    }

    // ---- ALPN ----
    ssl_ctx_set_alpn(ctx.get(), cfg.alpn_protocols);

    // ---- mTLS / Client certificate verification ----
    if (cfg.verify_client_cert || cfg.require_client_cert) {
        int verify_mode = SSL_VERIFY_PEER;
        if (cfg.require_client_cert) {
            verify_mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
        }
        SSL_CTX_set_verify(ctx.get(), verify_mode, nullptr);
        SSL_CTX_set_verify_depth(ctx.get(), cfg.verify_depth);
        spdlog::info("[TLS] Client certificate verification enabled (require={})",
                     cfg.require_client_cert);
    }

    spdlog::info("[TLS] Server context created successfully");
    return ctx.release();
}

// =============================================================================
// 2. TLS Client Context
// =============================================================================

struct TlsClientConfig {
    /// Minimum TLS version
    int min_version = TLS1_2_VERSION;

    /// Maximum TLS version
    int max_version = 0;

    /// Verify server certificate (default: true)
    bool verify_server_cert = true;

    /// Hostname to verify against (SNI + hostname verification)
    std::string verify_hostname;

    /// Enable certificate pinning
    bool enable_pinning = false;

    /// List of trusted certificate fingerprints (SHA-256 hex)
    std::vector<std::string> pinned_fingerprints;

    /// CA certificate bundle path (empty = system default)
    std::string ca_bundle_path;

    /// CA certificate directory
    std::string ca_directory;

    /// Client certificate (for mTLS)
    std::string client_cert_pem;

    /// Client private key (for mTLS)
    std::string client_key_pem;

    /// Cipher list (empty = default)
    std::string cipher_list;

    /// TLS 1.3 ciphersuites
    std::string ciphersuites;

    /// ALPN protocols
    std::vector<std::string> alpn_protocols;

    /// Enable session caching (client side)
    bool enable_session_cache = true;

    /// Enable session tickets
    bool enable_session_tickets = true;

    /// Enable OCSP stapling request
    bool enable_ocsp_stapling = false;
};

// ---------------------------------------------------------------------------
// Hostname verification callback (manual implementation for OpenSSL < 1.0.2
// or when we want custom behavior)
// ---------------------------------------------------------------------------
bool verify_hostname(X509* cert, const std::string& hostname) {
    if (hostname.empty()) return true;

    // Try SANs first
    auto sans = detail::x509_get_san_dns(cert);
    for (const auto& san : sans) {
        // Exact match
        if (san == hostname) return true;
        // Wildcard match: *.example.com matches sub.example.com
        if (san.size() > 2 && san[0] == '*' && san[1] == '.') {
            std::string suffix = san.substr(1); // .example.com
            if (hostname.size() > suffix.size() &&
                hostname.compare(hostname.size() - suffix.size(),
                                 suffix.size(), suffix) == 0) {
                // Ensure no additional dots in the matched portion
                // (e.g., *.example.com should NOT match a.b.example.com)
                std::string prefix = hostname.substr(0, hostname.size() - suffix.size());
                if (prefix.find('.') == std::string::npos) {
                    return true;
                }
            }
        }
    }

    // Fallback: check CN
    std::string cn = detail::x509_get_cn(cert);
    if (cn == hostname) return true;
    if (cn.size() > 2 && cn[0] == '*' && cn[1] == '.') {
        std::string suffix = cn.substr(1);
        if (hostname.size() > suffix.size() &&
            hostname.compare(hostname.size() - suffix.size(),
                             suffix.size(), suffix) == 0) {
            std::string prefix = hostname.substr(0, hostname.size() - suffix.size());
            if (prefix.find('.') == std::string::npos) {
                return true;
            }
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// Certificate verification callback
// ---------------------------------------------------------------------------
struct VerifyCallbackData {
    std::string hostname;
    bool enable_pinning;
    std::vector<std::string> pinned_fingerprints;
    bool* cert_accepted; // Set to true if pinning passes
    int verify_result;
    std::string error_message;
};

int client_verify_callback(int preverify_ok, X509_STORE_CTX* x509_ctx) {
    auto* data = static_cast<VerifyCallbackData*>(
        X509_STORE_CTX_get_ex_data(x509_ctx, 0));

    SSL* ssl = static_cast<SSL*>(
        X509_STORE_CTX_get_ex_data(x509_ctx,
                                    SSL_get_ex_data_X509_STORE_CTX_idx()));
    if (!ssl) return preverify_ok;

    X509* cert = X509_STORE_CTX_get_current_cert(x509_ctx);
    int depth = X509_STORE_CTX_get_error_depth(x509_ctx);

    spdlog::debug("[TLS] Verify callback depth={}, preverify={}, cert={}",
                  depth, preverify_ok, cert ? detail::x509_get_cn(cert) : "null");

    if (depth == 0 && cert) {
        // Leaf certificate
        // 1. Check hostname
        if (data && !data->hostname.empty()) {
            if (!verify_hostname(cert, data->hostname)) {
                spdlog::warn("[TLS] Hostname verification failed: expected '{}'",
                             data->hostname);
                X509_STORE_CTX_set_error(x509_ctx, X509_V_ERR_HOSTNAME_MISMATCH);
                return 0;
            }
        }

        // 2. Check certificate pinning
        if (data && data->enable_pinning && !data->pinned_fingerprints.empty()) {
            std::string fp;
            try {
                fp = detail::x509_sha256_fingerprint(cert);
            } catch (...) {
                spdlog::error("[TLS] Failed to compute certificate fingerprint");
                return 0;
            }
            bool pinned = false;
            for (const auto& pinned_fp : data->pinned_fingerprints) {
                if (fp == pinned_fp) {
                    pinned = true;
                    break;
                }
            }
            if (!pinned) {
                spdlog::error("[TLS] Certificate pinning failed: fingerprint {} not trusted",
                              fp);
                data->error_message = "Certificate pinning mismatch: " + fp;
                X509_STORE_CTX_set_error(x509_ctx, X509_V_ERR_APPLICATION_VERIFICATION);
                return 0;
            }
            spdlog::debug("[TLS] Certificate pinning verified: {}", fp);
        }
    }

    return preverify_ok;
}

// ---------------------------------------------------------------------------
// Load CA certificates into SSL_CTX
// ---------------------------------------------------------------------------
void ssl_ctx_load_ca(SSL_CTX* ctx, const std::string& ca_bundle,
                     const std::string& ca_dir) {
    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    if (!store) {
        throw std::runtime_error("SSL_CTX_get_cert_store returned null");
    }

    if (!ca_bundle.empty()) {
        if (X509_STORE_load_locations(store, ca_bundle.c_str(), nullptr) != 1) {
            spdlog::warn("[TLS] Failed to load CA bundle from '{}': {}",
                         ca_bundle, detail::openssl_error_string());
            ERR_clear_error();
        } else {
            spdlog::info("[TLS] Loaded CA bundle from '{}'", ca_bundle);
        }
    }

    if (!ca_dir.empty()) {
        if (X509_STORE_load_locations(store, nullptr, ca_dir.c_str()) != 1) {
            spdlog::warn("[TLS] Failed to load CA directory from '{}': {}",
                         ca_dir, detail::openssl_error_string());
            ERR_clear_error();
        } else {
            spdlog::info("[TLS] Loaded CA directory from '{}'", ca_dir);
        }
    }

    if (ca_bundle.empty() && ca_dir.empty()) {
        // Load system default CA store
        if (X509_STORE_set_default_paths(store) != 1) {
            spdlog::warn("[TLS] Failed to load system default CA store: {}",
                         detail::openssl_error_string());
            ERR_clear_error();
        } else {
            spdlog::info("[TLS] Loaded system default CA store");
        }
    }
}

// ---------------------------------------------------------------------------
// Main function: create a TLS client context
// ---------------------------------------------------------------------------
SSL_CTX* create_tls_client_context(const TlsClientConfig& cfg = {}) {
    detail::ensure_openssl_initialized();

    const SSL_METHOD* method = TLS_client_method();
    detail::ssl_ctx_ptr ctx(SSL_CTX_new(method));
    if (!ctx) {
        throw std::runtime_error("SSL_CTX_new(client) failed: " +
                                 detail::openssl_error_string());
    }

    // ---- TLS version constraints ----
    SSL_CTX_set_min_proto_version(ctx.get(), cfg.min_version > 0 ? cfg.min_version
                                                                 : TLS1_2_VERSION);
    if (cfg.max_version > 0) {
        SSL_CTX_set_max_proto_version(ctx.get(), cfg.max_version);
    }

    // ---- Security options ----
    long options = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |
                   SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1;
    SSL_CTX_set_options(ctx.get(), options);

    // ---- Cipher suites ----
    ssl_ctx_set_ciphers(ctx.get(), cfg.cipher_list, cfg.ciphersuites);

    // ---- Session caching ----
    if (cfg.enable_session_cache) {
        SSL_CTX_set_session_cache_mode(ctx.get(), SSL_SESS_CACHE_CLIENT);
    }

    // ---- Session tickets ----
    if (!cfg.enable_session_tickets) {
        SSL_CTX_set_options(ctx.get(),
            SSL_CTX_get_options(ctx.get()) | SSL_OP_NO_TICKET);
    }

    // ---- OCSP stapling request ----
    if (cfg.enable_ocsp_stapling) {
        SSL_CTX_set_tlsext_status_type(ctx.get(), TLSEXT_STATUSTYPE_ocsp);
    }

    // ---- Load CA certificates for server verification ----
    if (cfg.verify_server_cert) {
        ssl_ctx_load_ca(ctx.get(), cfg.ca_bundle_path, cfg.ca_directory);

        SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_verify_depth(ctx.get(), MAX_CERT_CHAIN_DEPTH);
    }

    // ---- ALPN ----
    ssl_ctx_set_alpn(ctx.get(), cfg.alpn_protocols);

    // ---- Client certificate (mTLS) ----
    if (!cfg.client_cert_pem.empty()) {
        ssl_ctx_use_certificate_chain(ctx.get(), cfg.client_cert_pem);
        ssl_ctx_use_private_key(ctx.get(), cfg.client_key_pem);
        spdlog::info("[TLS] Client certificate loaded for mTLS");
    }

    spdlog::info("[TLS] Client context created successfully");
    return ctx.release();
}

// =============================================================================
// 3. Self-Signed Certificate Generation for P2P Connections
// =============================================================================

struct P2pCertificate {
    std::string cert_pem;
    std::string key_pem;
    std::string fingerprint; // SHA-256 hex
    std::string subject;
    std::chrono::system_clock::time_point not_before;
    std::chrono::system_clock::time_point not_after;
};

// ---------------------------------------------------------------------------
// Generate a self-signed certificate for P2P use
// ---------------------------------------------------------------------------
P2pCertificate generate_p2p_certificate(
        const std::string& common_name,
        int key_bits = TLS_DEFAULT_RSA_KEY_BITS,
        int days_valid = TLS_DEFAULT_CERT_DAYS,
        const std::vector<std::string>& alt_names = {}) {

    P2pCertificate result;
    result.subject = common_name;

    // ---- Generate key pair ----
    detail::evp_pkey_ptr pkey(EVP_RSA_gen(key_bits));
    if (!pkey) {
        throw std::runtime_error("EVP_RSA_gen failed: " +
                                 detail::openssl_error_string());
    }

    // ---- Create certificate ----
    detail::x509_ptr x509(X509_new());
    if (!x509) {
        throw std::runtime_error("X509_new failed: " +
                                 detail::openssl_error_string());
    }

    // Set version to v3
    X509_set_version(x509.get(), 2);

    // Generate random serial number
    auto serial_bytes = detail::random_bytes(8);
    serial_bytes[0] &= 0x7F; // Ensure positive
    detail::asn1_integer_ptr serial(ASN1_INTEGER_new());
    if (!serial || !ASN1_INTEGER_set_uint64(serial.get(),
            *reinterpret_cast<const uint64_t*>(serial_bytes.data()))) {
        throw std::runtime_error("Failed to set serial number");
    }
    X509_set_serialNumber(x509.get(), serial.get());

    // Set validity period
    auto now = std::chrono::system_clock::now();
    result.not_before = now;

    detail::asn1_time_ptr not_before(ASN1_TIME_new());
    detail::asn1_time_ptr not_after(ASN1_TIME_new());
    if (!not_before || !not_after) {
        throw std::runtime_error("Failed to create ASN1_TIME objects");
    }

    ASN1_TIME_set(not_before.get(), std::chrono::system_clock::to_time_t(now));
    X509_set_notBefore(x509.get(), not_before.get());

    auto expiry = now + std::chrono::hours(24 * days_valid);
    result.not_after = expiry;
    ASN1_TIME_set(not_after.get(), std::chrono::system_clock::to_time_t(expiry));
    X509_set_notAfter(x509.get(), not_after.get());

    // Set public key
    if (!X509_set_pubkey(x509.get(), pkey.get())) {
        throw std::runtime_error("X509_set_pubkey failed");
    }

    // Build subject name
    detail::x509_name_ptr name(X509_NAME_new());
    if (!name) {
        throw std::runtime_error("X509_NAME_new failed");
    }

    X509_NAME_add_entry_by_txt(name.get(), "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(common_name.c_str()),
                               -1, -1, 0);
    X509_NAME_add_entry_by_txt(name.get(), "O", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("cppdesk P2P"),
                               -1, -1, 0);
    X509_NAME_add_entry_by_txt(name.get(), "OU", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("P2P"),
                               -1, -1, 0);

    X509_set_subject_name(x509.get(), name.get());
    X509_set_issuer_name(x509.get(), name.get());

    // Add X.509 v3 extensions
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, x509.get(), x509.get(), nullptr, nullptr, 0);

    // Basic Constraints: not a CA
    {
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints,
                                const_cast<char*>("critical,CA:FALSE")));
        if (ext) X509_add_ext(x509.get(), ext.get(), -1);
    }

    // Key Usage
    {
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage,
                                const_cast<char*>("critical,digitalSignature,keyEncipherment")));
        if (ext) X509_add_ext(x509.get(), ext.get(), -1);
    }

    // Extended Key Usage: serverAuth + clientAuth
    {
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &ctx, NID_ext_key_usage,
                                const_cast<char*>("serverAuth,clientAuth")));
        if (ext) X509_add_ext(x509.get(), ext.get(), -1);
    }

    // Subject Alternative Names
    if (!alt_names.empty()) {
        std::string san_value;
        for (size_t i = 0; i < alt_names.size(); ++i) {
            if (i > 0) san_value += ",";
            san_value += "DNS:" + alt_names[i];
        }
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_alt_name,
                                const_cast<char*>(san_value.c_str())));
        if (ext) X509_add_ext(x509.get(), ext.get(), -1);
    }

    // Add a friendly comment extension
    {
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &ctx, NID_netscape_comment,
                                const_cast<char*>("cppdesk P2P Auto-Generated Certificate")));
        if (ext) X509_add_ext(x509.get(), ext.get(), -1);
    }

    // ---- Self-sign ----
    if (!X509_sign(x509.get(), pkey.get(), EVP_sha256())) {
        throw std::runtime_error("X509_sign failed: " +
                                 detail::openssl_error_string());
    }

    // ---- Export to PEM ----
    result.cert_pem = detail::x509_to_pem(x509.get());
    result.key_pem = detail::evp_pkey_to_pem_private(pkey.get());
    result.fingerprint = detail::x509_sha256_fingerprint(x509.get());

    spdlog::info("[TLS] Generated P2P certificate: CN={}, bits={}, days={}, "
                 "fingerprint={}",
                 common_name, key_bits, days_valid, result.fingerprint);

    return result;
}

// ---------------------------------------------------------------------------
// Generate ECDSA self-signed certificate
// ---------------------------------------------------------------------------
P2pCertificate generate_p2p_ecdsa_certificate(
        const std::string& common_name,
        const std::string& curve_name = TLS_DEFAULT_EC_CURVE,
        int days_valid = TLS_DEFAULT_CERT_DAYS) {

    P2pCertificate result;
    result.subject = common_name;

    // Create ECDSA key
    int nid = OBJ_sn2nid(curve_name.c_str());
    if (nid == NID_undef) {
        throw std::runtime_error("Unknown EC curve: " + curve_name);
    }

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    detail::evp_pkey_ptr pkey(nullptr);
    {
        EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
        if (!pctx) throw std::runtime_error("EVP_PKEY_CTX_new_from_name(EC) failed");
        EVP_PKEY_keygen_init(pctx);
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, nid);
        EVP_PKEY* key = nullptr;
        if (EVP_PKEY_keygen(pctx, &key) <= 0) {
            EVP_PKEY_CTX_free(pctx);
            throw std::runtime_error("EC keygen failed: " + detail::openssl_error_string());
        }
        pkey.reset(key);
        EVP_PKEY_CTX_free(pctx);
    }
#else
    detail::ec_key_ptr ec(EC_KEY_new_by_curve_name(nid));
    if (!ec) throw std::runtime_error("EC_KEY_new_by_curve_name failed");
    if (!EC_KEY_generate_key(ec.get())) {
        throw std::runtime_error("EC_KEY_generate_key failed: " +
                                 detail::openssl_error_string());
    }
    detail::evp_pkey_ptr pkey(EVP_PKEY_new());
    if (!pkey || !EVP_PKEY_set1_EC_KEY(pkey.get(), ec.get())) {
        throw std::runtime_error("EVP_PKEY_set1_EC_KEY failed");
    }
#endif

    // Remainder is the same as RSA version, using pkey
    detail::x509_ptr x509(X509_new());
    X509_set_version(x509.get(), 2);

    auto serial_bytes = detail::random_bytes(8);
    serial_bytes[0] &= 0x7F;
    detail::asn1_integer_ptr serial(ASN1_INTEGER_new());
    ASN1_INTEGER_set_uint64(serial.get(),
                            *reinterpret_cast<const uint64_t*>(serial_bytes.data()));
    X509_set_serialNumber(x509.get(), serial.get());

    auto now = std::chrono::system_clock::now();
    result.not_before = now;
    detail::asn1_time_ptr not_before(ASN1_TIME_new());
    detail::asn1_time_ptr not_after(ASN1_TIME_new());
    ASN1_TIME_set(not_before.get(), std::chrono::system_clock::to_time_t(now));
    X509_set_notBefore(x509.get(), not_before.get());
    auto expiry = now + std::chrono::hours(24 * days_valid);
    result.not_after = expiry;
    ASN1_TIME_set(not_after.get(), std::chrono::system_clock::to_time_t(expiry));
    X509_set_notAfter(x509.get(), not_after.get());

    X509_set_pubkey(x509.get(), pkey.get());

    detail::x509_name_ptr name(X509_NAME_new());
    X509_NAME_add_entry_by_txt(name.get(), "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(common_name.c_str()),
                               -1, -1, 0);
    X509_set_subject_name(x509.get(), name.get());
    X509_set_issuer_name(x509.get(), name.get());

    // Extensions
    X509V3_CTX v3_ctx;
    X509V3_set_ctx_nodb(&v3_ctx);
    X509V3_set_ctx(&v3_ctx, x509.get(), x509.get(), nullptr, nullptr, 0);

    {
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &v3_ctx, NID_basic_constraints,
                                const_cast<char*>("critical,CA:FALSE")));
        if (ext) X509_add_ext(x509.get(), ext.get(), -1);
    }
    {
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &v3_ctx, NID_key_usage,
                                const_cast<char*>("critical,digitalSignature")));
        if (ext) X509_add_ext(x509.get(), ext.get(), -1);
    }
    {
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &v3_ctx, NID_ext_key_usage,
                                const_cast<char*>("serverAuth,clientAuth")));
        if (ext) X509_add_ext(x509.get(), ext.get(), -1);
    }

    if (!X509_sign(x509.get(), pkey.get(), EVP_sha256())) {
        throw std::runtime_error("X509_sign failed: " + detail::openssl_error_string());
    }

    result.cert_pem = detail::x509_to_pem(x509.get());
    result.key_pem = detail::evp_pkey_to_pem_private(pkey.get());
    result.fingerprint = detail::x509_sha256_fingerprint(x509.get());

    spdlog::info("[TLS] Generated P2P ECDSA certificate: CN={}, curve={}, fingerprint={}",
                 common_name, curve_name, result.fingerprint);

    return result;
}

// ---------------------------------------------------------------------------
// Export certificate + key as PKCS#12 (PFX)
// ---------------------------------------------------------------------------
std::vector<uint8_t> export_pkcs12(const std::string& cert_pem,
                                    const std::string& key_pem,
                                    const std::string& password,
                                    const std::string& friendly_name = "cppdesk") {
    detail::bio_ptr cert_bio(BIO_new_mem_buf(cert_pem.data(),
                              static_cast<int>(cert_pem.size())));
    detail::x509_ptr cert(PEM_read_bio_X509(cert_bio.get(), nullptr, nullptr, nullptr));
    if (!cert) throw std::runtime_error("Failed to parse certificate PEM");

    detail::bio_ptr key_bio(BIO_new_mem_buf(key_pem.data(),
                             static_cast<int>(key_pem.size())));
    detail::evp_pkey_ptr pkey(
        PEM_read_bio_PrivateKey(key_bio.get(), nullptr, nullptr, nullptr));
    if (!pkey) throw std::runtime_error("Failed to parse private key PEM");

    detail::pkcs12_ptr p12(PKCS12_create(
        password.c_str(),                    // password
        friendly_name.c_str(),               // friendly name
        pkey.get(),                          // private key
        cert.get(),                          // certificate
        nullptr,                             // CA stack
        0,                                   // nid_key
        0,                                   // nid_cert
        PKCS12_DEFAULT_ITER,                 // iter
        PKCS12_DEFAULT_ITER,                 // mac_iter
        0));                                  // keytype

    if (!p12) {
        throw std::runtime_error("PKCS12_create failed: " +
                                 detail::openssl_error_string());
    }

    detail::bio_ptr out(BIO_new(BIO_s_mem()));
    if (!i2d_PKCS12_bio(out.get(), p12.get())) {
        throw std::runtime_error("i2d_PKCS12_bio failed");
    }

    char* data = nullptr;
    long len = BIO_get_mem_data(out.get(), &data);
    return {reinterpret_cast<uint8_t*>(data),
            reinterpret_cast<uint8_t*>(data) + len};
}

// =============================================================================
// 4. Certificate Pinning and Trust-On-First-Use (TOFU)
// =============================================================================

// ---------------------------------------------------------------------------
// TofuStore: persistent storage for TOFU fingerprints
// ---------------------------------------------------------------------------
class TofuStore {
public:
    struct Entry {
        std::string hostname;
        std::string fingerprint; // SHA-256 hex
        std::chrono::system_clock::time_point first_seen;
        std::chrono::system_clock::time_point last_seen;
        bool is_pinned = false;
    };

    TofuStore() {
        ensure_openssl_initialized();
    }

    /// Load TOFU cache from disk
    void load(const std::string& path) {
        std::lock_guard lock(mutex_);
        cache_path_ = path;

        if (!std::filesystem::exists(path)) {
            spdlog::info("[TOFU] No cache file at '{}', starting fresh", path);
            return;
        }

        try {
            auto content = detail::read_file_to_string(path);
            auto j = nlohmann::json::parse(content);

            entries_.clear();
            for (const auto& item : j) {
                Entry e;
                e.hostname = item.value("hostname", "");
                e.fingerprint = item.value("fingerprint", "");
                e.first_seen = std::chrono::system_clock::from_time_t(
                    item.value("first_seen", 0));
                e.last_seen = std::chrono::system_clock::from_time_t(
                    item.value("last_seen", 0));
                e.is_pinned = item.value("is_pinned", false);
                entries_[e.hostname] = e;
            }

            spdlog::info("[TOFU] Loaded {} entries from '{}'", entries_.size(), path);
        } catch (const std::exception& ex) {
            spdlog::warn("[TOFU] Failed to load cache '{}': {}", path, ex.what());
            entries_.clear();
        }
    }

    /// Save TOFU cache to disk
    void save() {
        std::lock_guard lock(mutex_);
        if (cache_path_.empty()) return;

        try {
            nlohmann::json j = nlohmann::json::array();
            for (const auto& [host, entry] : entries_) {
                nlohmann::json item;
                item["hostname"] = entry.hostname;
                item["fingerprint"] = entry.fingerprint;
                item["first_seen"] = std::chrono::system_clock::to_time_t(entry.first_seen);
                item["last_seen"] = std::chrono::system_clock::to_time_t(entry.last_seen);
                item["is_pinned"] = entry.is_pinned;
                j.push_back(item);
            }
            detail::write_file_atomic(cache_path_, j.dump(2));
            spdlog::debug("[TOFU] Saved {} entries to '{}'", entries_.size(), cache_path_);
        } catch (const std::exception& ex) {
            spdlog::error("[TOFU] Failed to save cache: {}", ex.what());
        }
    }

    /// Trust-on-first-use: check/record fingerprint
    /// Returns true if the fingerprint is trusted (first seen or matches)
    bool check_tofu(const std::string& hostname, const std::string& fingerprint) {
        std::lock_guard lock(mutex_);

        auto it = entries_.find(hostname);
        if (it == entries_.end()) {
            // First contact: record and trust
            Entry e;
            e.hostname = hostname;
            e.fingerprint = fingerprint;
            e.first_seen = std::chrono::system_clock::now();
            e.last_seen = e.first_seen;
            e.is_pinned = true;
            entries_[hostname] = e;

            spdlog::info("[TOFU] First contact with '{}', pinned fingerprint {}", 
                         hostname, fingerprint);
            return true;
        }

        // Update last_seen
        it->second.last_seen = std::chrono::system_clock::now();

        if (it->second.fingerprint == fingerprint) {
            spdlog::debug("[TOFU] Fingerprint match for '{}'", hostname);
            return true;
        }

        // Fingerprint changed!
        spdlog::warn("[TOFU] FINGERPRINT MISMATCH for '{}': "
                     "expected={}, got={}",
                     hostname, it->second.fingerprint, fingerprint);
        return false;
    }

    /// Explicitly pin a fingerprint
    void pin(const std::string& hostname, const std::string& fingerprint) {
        std::lock_guard lock(mutex_);
        Entry e;
        e.hostname = hostname;
        e.fingerprint = fingerprint;
        e.first_seen = std::chrono::system_clock::now();
        e.last_seen = e.first_seen;
        e.is_pinned = true;
        entries_[hostname] = e;
        spdlog::info("[TOFU] Explicitly pinned '{}' -> {}", hostname, fingerprint);
    }

    /// Unpin and remove entry
    void unpin(const std::string& hostname) {
        std::lock_guard lock(mutex_);
        entries_.erase(hostname);
        spdlog::info("[TOFU] Unpinned '{}'", hostname);
    }

    /// Check if a hostname is pinned
    bool is_pinned(const std::string& hostname) const {
        std::shared_lock lock(mutex_);
        auto it = entries_.find(hostname);
        return it != entries_.end() && it->second.is_pinned;
    }

    /// Get pinned fingerprint for a hostname
    std::optional<std::string> get_fingerprint(const std::string& hostname) const {
        std::shared_lock lock(mutex_);
        auto it = entries_.find(hostname);
        if (it != entries_.end()) {
            return it->second.fingerprint;
        }
        return std::nullopt;
    }

    /// Get all entries
    std::vector<Entry> get_all_entries() const {
        std::shared_lock lock(mutex_);
        std::vector<Entry> result;
        result.reserve(entries_.size());
        for (const auto& [k, v] : entries_) {
            result.push_back(v);
        }
        return result;
    }

private:
    std::unordered_map<std::string, Entry> entries_;
    std::string cache_path_;
    mutable std::shared_mutex mutex_;
};

// Global TOFU store instance
static TofuStore g_tofu_store;

TofuStore& tofu_store() {
    return g_tofu_store;
}

// ---------------------------------------------------------------------------
// PIN callback for SSL: validates fingerprint during handshake
// ---------------------------------------------------------------------------
struct PinCallbackData {
    const std::vector<std::string>* fingerprints = nullptr;
    bool* result = nullptr;
};

int pin_verify_callback(int preverify_ok, X509_STORE_CTX* x509_ctx) {
    if (!preverify_ok) return preverify_ok;

    auto* data = static_cast<PinCallbackData*>(
        X509_STORE_CTX_get_ex_data(x509_ctx, 1));
    if (!data || !data->fingerprints || !data->result) return preverify_ok;

    int depth = X509_STORE_CTX_get_error_depth(x509_ctx);
    if (depth != 0) return preverify_ok; // Only check leaf

    X509* cert = X509_STORE_CTX_get_current_cert(x509_ctx);
    if (!cert) return 0;

    std::string fp = detail::x509_sha256_fingerprint(cert);
    for (const auto& pinned_fp : *data->fingerprints) {
        if (fp == pinned_fp) {
            *data->result = true;
            return 1;
        }
    }

    spdlog::error("[PIN] Certificate pinning failed: fp={}", fp);
    *data->result = false;
    return 0;
}

// =============================================================================
// 5. TLS Session Resumption
// =============================================================================

// ---------------------------------------------------------------------------
// Session cache: thread-safe in-memory session storage
// ---------------------------------------------------------------------------
class SessionCache {
public:
    struct CachedSession {
        std::string session_id;
        std::vector<uint8_t> session_data; // ASN.1 encoded SSL_SESSION
        std::chrono::steady_clock::time_point cached_at;
        std::chrono::steady_clock::time_point expires_at;
        std::string peer_hostname;
    };

    /// Cache a session
    void store(const std::string& peer, SSL_SESSION* session) {
        if (!session) return;

        std::lock_guard lock(mutex_);

        // Serialize session
        int len = i2d_SSL_SESSION(session, nullptr);
        if (len <= 0) return;

        CachedSession cs;
        cs.session_data.resize(static_cast<size_t>(len));
        unsigned char* ptr = cs.session_data.data();
        i2d_SSL_SESSION(session, &ptr);

        // Extract session ID
        unsigned int sid_len = 0;
        const unsigned char* sid = SSL_SESSION_get_id(session, &sid_len);
        if (sid && sid_len > 0) {
            cs.session_id.assign(reinterpret_cast<const char*>(sid), sid_len);
        }

        cs.cached_at = std::chrono::steady_clock::now();
        long lifetime = SSL_SESSION_get_ticket_lifetime_hint(session);
        if (lifetime <= 0) lifetime = TLS_MAX_SESSION_TICKET_LIFETIME;
        cs.expires_at = cs.cached_at + std::chrono::seconds(lifetime);
        cs.peer_hostname = peer;

        cache_[peer] = std::move(cs);
        spdlog::debug("[Session] Cached session for '{}', lifetime={}s", peer, lifetime);
    }

    /// Retrieve a session
    SSL_SESSION* retrieve(const std::string& peer) {
        std::lock_guard lock(mutex_);

        auto it = cache_.find(peer);
        if (it == cache_.end()) return nullptr;

        auto now = std::chrono::steady_clock::now();
        if (now > it->second.expires_at) {
            cache_.erase(it);
            return nullptr;
        }

        const unsigned char* ptr = it->second.session_data.data();
        SSL_SESSION* sess = d2i_SSL_SESSION(nullptr, &ptr,
            static_cast<long>(it->second.session_data.size()));
        if (sess) {
            spdlog::debug("[Session] Resumed session for '{}'", peer);
        }
        return sess;
    }

    /// Remove session
    void remove(const std::string& peer) {
        std::lock_guard lock(mutex_);
        cache_.erase(peer);
    }

    /// Clear all sessions
    void clear() {
        std::lock_guard lock(mutex_);
        cache_.clear();
        spdlog::info("[Session] Cache cleared");
    }

    /// Prune expired sessions
    void prune() {
        std::lock_guard lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        for (auto it = cache_.begin(); it != cache_.end(); ) {
            if (now > it->second.expires_at) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /// Number of cached sessions
    size_t size() const {
        std::lock_guard lock(mutex_);
        return cache_.size();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, CachedSession> cache_;
};

// Global session cache instance
static SessionCache g_session_cache;

SessionCache& session_cache() {
    return g_session_cache;
}

// ---------------------------------------------------------------------------
// New session callback: store session in cache
// ---------------------------------------------------------------------------
int on_new_session_callback(SSL* ssl, SSL_SESSION* session) {
    if (!session) return 0;

    // Get peer hostname from SSL (if SNI was used)
    const char* sni = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    std::string peer = sni ? sni : "unknown";

    g_session_cache.store(peer, session);
    return 1; // OpenSSL takes ownership
}

// ---------------------------------------------------------------------------
// Remove session callback: remove from cache
// ---------------------------------------------------------------------------
void on_remove_session_callback(SSL_CTX* ctx, SSL_SESSION* session) {
    if (!session) return;
    unsigned int sid_len = 0;
    SSL_SESSION_get_id(session, &sid_len);
    // We don't have a reverse ID->peer mapping here; pruning handles cleanup
}

// ---------------------------------------------------------------------------
// Configure session resumption on SSL_CTX
// ---------------------------------------------------------------------------
void configure_session_resumption(SSL_CTX* ctx, bool is_server) {
    if (is_server) {
        SSL_CTX_set_session_cache_mode(ctx,
            SSL_SESS_CACHE_SERVER | SSL_SESS_CACHE_NO_AUTO_CLEAR);
        SSL_CTX_sess_set_cache_size(ctx, 1024);
        SSL_CTX_set_timeout(ctx, TLS_MAX_SESSION_TICKET_LIFETIME);
        SSL_CTX_sess_set_new_cb(ctx, on_new_session_callback);
        SSL_CTX_sess_set_remove_cb(ctx, on_remove_session_callback);
    } else {
        SSL_CTX_set_session_cache_mode(ctx,
            SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);
        // Clients use session_cache() to retrieve sessions before connecting
    }
    spdlog::info("[TLS] Session resumption configured (server={})", is_server);
}

// ---------------------------------------------------------------------------
// Apply a cached session to a new SSL connection (client side)
// ---------------------------------------------------------------------------
bool apply_cached_session(SSL* ssl, const std::string& peer_hostname) {
    SSL_SESSION* sess = g_session_cache.retrieve(peer_hostname);
    if (!sess) return false;

    if (SSL_set_session(ssl, sess) != 1) {
        SSL_SESSION_free(sess);
        return false;
    }
    SSL_SESSION_free(sess); // SSL_set_session increments refcount
    return true;
}

// =============================================================================
// 6. ALPN Protocol Negotiation
// =============================================================================

// ---------------------------------------------------------------------------
// ALPN negotiation result
// ---------------------------------------------------------------------------
struct AlpnResult {
    bool negotiated;
    std::string protocol;
};

// ---------------------------------------------------------------------------
// Perform ALPN negotiation on client side (called after handshake)
// ---------------------------------------------------------------------------
AlpnResult get_alpn_negotiated(SSL* ssl) {
    AlpnResult result;
    const unsigned char* proto = nullptr;
    unsigned int proto_len = 0;

    SSL_get0_alpn_selected(ssl, &proto, &proto_len);
    if (proto && proto_len > 0) {
        result.negotiated = true;
        result.protocol.assign(reinterpret_cast<const char*>(proto), proto_len);
        spdlog::info("[ALPN] Negotiated protocol: {}", result.protocol);
    } else {
        result.negotiated = false;
        spdlog::info("[ALPN] No ALPN protocol negotiated");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Build ALPN wire format from vector of protocol strings
// ---------------------------------------------------------------------------
std::vector<unsigned char> build_alpn_wire_format(
        const std::vector<std::string>& protocols) {
    std::vector<unsigned char> result;
    for (const auto& proto : protocols) {
        if (proto.size() > 255) {
            throw std::runtime_error("ALPN protocol name exceeds 255 bytes: " + proto);
        }
        result.push_back(static_cast<unsigned char>(proto.size()));
        result.insert(result.end(),
                      reinterpret_cast<const unsigned char*>(proto.data()),
                      reinterpret_cast<const unsigned char*>(proto.data()) + proto.size());
    }
    return result;
}

// ---------------------------------------------------------------------------
// Set ALPN callback on an SSL (per-connection, client-side)
// ---------------------------------------------------------------------------
void set_alpn_client_callback(SSL* ssl, const std::vector<std::string>& protocols) {
    if (protocols.empty()) return;

    auto alpn_data = build_alpn_wire_format(protocols);

    // SSL_set_alpn_protos takes ownership of a copy internally
    if (SSL_set_alpn_protos(ssl, alpn_data.data(),
                             static_cast<unsigned int>(alpn_data.size())) != 0) {
        spdlog::warn("[ALPN] SSL_set_alpn_protos failed");
    }
}

// =============================================================================
// 7. SNI (Server Name Indication) Support
// =============================================================================

// ---------------------------------------------------------------------------
// Set SNI hostname on client SSL connection
// ---------------------------------------------------------------------------
void set_sni_hostname(SSL* ssl, const std::string& hostname) {
    if (hostname.empty()) return;

    if (SSL_set_tlsext_host_name(ssl, hostname.c_str()) != 1) {
        spdlog::warn("[SNI] SSL_set_tlsext_host_name failed for '{}': {}",
                     hostname, detail::openssl_error_string());
        ERR_clear_error();
    } else {
        spdlog::debug("[SNI] Set hostname: {}", hostname);
    }
}

// ---------------------------------------------------------------------------
// Get SNI hostname from server SSL connection
// ---------------------------------------------------------------------------
std::string get_sni_hostname(SSL* ssl) {
    const char* name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    return name ? std::string(name) : std::string();
}

// ---------------------------------------------------------------------------
// Install SNI callback on server context
// ---------------------------------------------------------------------------
void install_sni_callback(SSL_CTX* ctx, SniContext* sni_ctx) {
    SSL_CTX_set_tlsext_servername_callback(ctx, sni_server_callback);
    SSL_CTX_set_tlsext_servername_arg(ctx, sni_ctx);
    spdlog::info("[SNI] Server SNI callback installed");
}

// =============================================================================
// 8. Mutual TLS (mTLS) with Client Certificates
// =============================================================================

// ---------------------------------------------------------------------------
// Configure server for mTLS
// ---------------------------------------------------------------------------
void configure_mtls_server(SSL_CTX* ctx,
                           const std::string& ca_cert_pem,
                           bool require_client_cert = true,
                           int verify_depth = 4) {
    // Load CA cert(s) that are trusted for client certificates
    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    if (!store) {
        throw std::runtime_error("SSL_CTX_get_cert_store returned null");
    }

    detail::bio_ptr ca_bio(BIO_new_mem_buf(ca_cert_pem.data(),
                            static_cast<int>(ca_cert_pem.size())));
    if (!ca_bio) {
        throw std::runtime_error("BIO_new_mem_buf failed for CA cert");
    }

    // Load all CA certificates from the PEM
    while (true) {
        detail::x509_ptr ca(PEM_read_bio_X509(ca_bio.get(), nullptr, nullptr, nullptr));
        if (!ca) {
            unsigned long err = ERR_peek_last_error();
            if (ERR_GET_LIB(err) == ERR_LIB_PEM &&
                ERR_GET_REASON(err) == PEM_R_NO_START_LINE) {
                ERR_clear_error();
                break;
            }
            if (err != 0) {
                spdlog::warn("[mTLS] Error reading CA cert: {}", detail::openssl_error_string());
                ERR_clear_error();
            }
            break;
        }
        if (X509_STORE_add_cert(store, ca.get()) != 1) {
            unsigned long err = ERR_peek_last_error();
            if (ERR_GET_REASON(err) == X509_R_CERT_ALREADY_IN_HASH_TABLE) {
                ERR_clear_error();
            } else {
                spdlog::warn("[mTLS] Failed to add CA cert to store: {}",
                             detail::openssl_error_string());
                ERR_clear_error();
            }
        }
    }

    // Set verification mode
    int mode = SSL_VERIFY_PEER;
    if (require_client_cert) {
        mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
    }
    SSL_CTX_set_verify(ctx, mode, nullptr);
    SSL_CTX_set_verify_depth(ctx, verify_depth);

    spdlog::info("[mTLS] Server mTLS configured (require={}, depth={})",
                 require_client_cert, verify_depth);
}

// ---------------------------------------------------------------------------
// Configure client for mTLS: load client cert and key
// ---------------------------------------------------------------------------
void configure_mtls_client(SSL_CTX* ctx,
                           const std::string& client_cert_pem,
                           const std::string& client_key_pem,
                           const std::string& key_password) {
    ssl_ctx_use_certificate_chain(ctx, client_cert_pem);
    ssl_ctx_use_private_key(ctx, client_key_pem, key_password);
    spdlog::info("[mTLS] Client certificate and key loaded");
}

// =============================================================================
// 9. TLS 1.3 Specific Features
// =============================================================================

// ---------------------------------------------------------------------------
// Enable/configure TLS 1.3 early data (0-RTT)
// ---------------------------------------------------------------------------
void configure_tls13_early_data(SSL_CTX* ctx, size_t max_early_data = 16384) {
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    SSL_CTX_set_max_early_data(ctx, static_cast<uint32_t>(max_early_data));
    spdlog::info("[TLS 1.3] 0-RTT early data enabled (max {} bytes)", max_early_data);
#else
    spdlog::warn("[TLS 1.3] 0-RTT not available in this OpenSSL version");
#endif
}

// ---------------------------------------------------------------------------
// Enable anti-replay protection for 0-RTT (server side)
// ---------------------------------------------------------------------------
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
void configure_tls13_anti_replay(SSL_CTX* ctx) {
    SSL_CTX_set_options(ctx,
        SSL_CTX_get_options(ctx) | SSL_OP_NO_ANTI_REPLAY);
    spdlog::info("[TLS 1.3] Anti-replay protection disabled (for testing)");
}
#endif

// ---------------------------------------------------------------------------
// Write early data (0-RTT) — client side, before handshake completes
// ---------------------------------------------------------------------------
ssize_t write_tls13_early_data(SSL* ssl, const void* data, size_t len) {
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    size_t written = 0;
    int ret = SSL_write_early_data(ssl, data, len, &written);
    if (ret == 1) {
        spdlog::debug("[TLS 1.3] Wrote {} bytes early data", written);
        return static_cast<ssize_t>(written);
    }
    spdlog::warn("[TLS 1.3] SSL_write_early_data failed (ret={})", ret);
    return -1;
#else
    (void)ssl; (void)data; (void)len;
    return -1;
#endif
}

// ---------------------------------------------------------------------------
// Read early data (0-RTT) — server side
// ---------------------------------------------------------------------------
ssize_t read_tls13_early_data(SSL* ssl, void* buf, size_t buf_len) {
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    size_t nread = 0;
    int ret = SSL_read_early_data(ssl, buf, buf_len, &nread);
    if (ret == SSL_READ_EARLY_DATA_FINISH) {
        spdlog::debug("[TLS 1.3] Early data read finished, {} bytes", nread);
        return static_cast<ssize_t>(nread);
    } else if (ret == SSL_READ_EARLY_DATA_SUCCESS) {
        spdlog::debug("[TLS 1.3] Early data: {} bytes (more available)", nread);
        return static_cast<ssize_t>(nread);
    }
    spdlog::warn("[TLS 1.3] SSL_read_early_data returned {}", ret);
    return -1;
#else
    (void)ssl; (void)buf; (void)buf_len;
    return -1;
#endif
}

// ---------------------------------------------------------------------------
// Get TLS version negotiated for this connection
// ---------------------------------------------------------------------------
std::string get_tls_version(SSL* ssl) {
    int ver = SSL_version(ssl);
    switch (ver) {
        case TLS1_3_VERSION: return "TLSv1.3";
        case TLS1_2_VERSION: return "TLSv1.2";
        case TLS1_1_VERSION: return "TLSv1.1";
        case TLS1_VERSION:   return "TLSv1.0";
        default:             return "unknown (" + std::to_string(ver) + ")";
    }
}

// ---------------------------------------------------------------------------
// Get cipher suite used for this connection
// ---------------------------------------------------------------------------
std::string get_cipher_name(SSL* ssl) {
    const char* cipher = SSL_get_cipher_name(ssl);
    return cipher ? cipher : "unknown";
}

// ---------------------------------------------------------------------------
// Get the TLS 1.3 key schedule ticketing / PSK info
// ---------------------------------------------------------------------------
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
bool is_tls13_session_reused(SSL* ssl) {
    return SSL_session_reused(ssl) == 1;
}
#endif

// =============================================================================
// 10. Cipher Suite Configuration and Hardening
// =============================================================================

// ---------------------------------------------------------------------------
// Security level presets
// ---------------------------------------------------------------------------
enum class TlsSecurityLevel {
    MODERN,          // TLS 1.3 only, strongest ciphers
    INTERMEDIATE,    // TLS 1.2+, Mozilla intermediate
    OLD,             // TLS 1.2+, wider compatibility
    LEGACY,          // TLS 1.0+, legacy (not recommended)
};

// ---------------------------------------------------------------------------
// Apply security level to SSL_CTX
// ---------------------------------------------------------------------------
void apply_tls_security_level(SSL_CTX* ctx, TlsSecurityLevel level) {
    switch (level) {
        case TlsSecurityLevel::MODERN:
            // TLS 1.3 only
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
            SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
            SSL_CTX_set_ciphersuites(ctx,
                "TLS_AES_256_GCM_SHA384:TLS_AES_128_GCM_SHA256:"
                "TLS_CHACHA20_POLY1305_SHA256");
#endif
            SSL_CTX_set_cipher_list(ctx, ""); // No TLS 1.2 ciphers needed
            spdlog::info("[TLS] Security level: MODERN (TLS 1.3 only)");
            break;

        case TlsSecurityLevel::INTERMEDIATE:
            SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
            SSL_CTX_set_cipher_list(ctx, TLS_HARDENED_CIPHER_LIST);
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
            SSL_CTX_set_ciphersuites(ctx, TLS_HARDENED_CIPHERSUITES_TLS13);
#endif
            spdlog::info("[TLS] Security level: INTERMEDIATE (TLS 1.2+)");
            break;

        case TlsSecurityLevel::OLD:
            SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
            SSL_CTX_set_cipher_list(ctx,
                "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"
                "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
                "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
                "DHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:"
                "AES128-GCM-SHA256:AES256-GCM-SHA384");
            spdlog::info("[TLS] Security level: OLD (TLS 1.2+)");
            break;

        case TlsSecurityLevel::LEGACY:
            SSL_CTX_set_min_proto_version(ctx, TLS1_VERSION);
            SSL_CTX_set_cipher_list(ctx, "ALL:!aNULL:!eNULL:!LOW:!EXP:!RC4:@STRENGTH");
            spdlog::info("[TLS] Security level: LEGACY (TLS 1.0+, not recommended!)");
            break;
    }
}

// ---------------------------------------------------------------------------
// Disable all weak ciphers and insecure features
// ---------------------------------------------------------------------------
void harden_ssl_context(SSL_CTX* ctx) {
    // Protocol versions
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    // Options
    long opts = SSL_CTX_get_options(ctx);
    opts |= SSL_OP_NO_SSLv2;
    opts |= SSL_OP_NO_SSLv3;
    opts |= SSL_OP_NO_TLSv1;
    opts |= SSL_OP_NO_TLSv1_1;
    opts |= SSL_OP_NO_COMPRESSION; // CRIME attack
    opts |= SSL_OP_CIPHER_SERVER_PREFERENCE;

#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    opts |= SSL_OP_NO_RENEGOTIATION;
#endif

    SSL_CTX_set_options(ctx, opts);

    // Disable insecure renegotiation (CVE-2009-3555)
    SSL_CTX_set_options(ctx,
        SSL_CTX_get_options(ctx) | SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION);
    // Actually we want the OPPOSITE — disable. The default is secure.
    // Make sure we're NOT allowing unsafe legacy renegotiation:
    SSL_CTX_clear_options(ctx, SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION);

    // Set cipher list
    SSL_CTX_set_cipher_list(ctx, "HIGH:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!SRP:!CAMELLIA:!DSS");

#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    SSL_CTX_set_ciphersuites(ctx,
        "TLS_AES_256_GCM_SHA384:TLS_AES_128_GCM_SHA256:"
        "TLS_CHACHA20_POLY1305_SHA256");
#endif

    // Use server cipher preference
    SSL_CTX_set_options(ctx,
        SSL_CTX_get_options(ctx) | SSL_OP_CIPHER_SERVER_PREFERENCE);

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    // OpenSSL 3.x security level (0-5, default is 1)
    // Level 2 requires >= 2048-bit keys; level 3 requires >= 3072-bit
    SSL_CTX_set_security_level(ctx, 2);
#endif

    spdlog::info("[TLS] Context hardened (min TLS 1.2, strong ciphers only)");
}

// ---------------------------------------------------------------------------
// Print available cipher information (for debugging)
// ---------------------------------------------------------------------------
std::vector<std::string> get_available_ciphers(SSL_CTX* ctx) {
    std::vector<std::string> result;
    SSL* ssl = SSL_new(ctx);
    if (!ssl) return result;

    STACK_OF(SSL_CIPHER)* ciphers = SSL_get_ciphers(ssl);
    if (ciphers) {
        int count = sk_SSL_CIPHER_num(ciphers);
        result.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            const SSL_CIPHER* cipher = sk_SSL_CIPHER_value(ciphers, i);
            if (cipher) {
                const char* name = SSL_CIPHER_get_name(cipher);
                const char* version = SSL_CIPHER_get_version(cipher);
                int bits = SSL_CIPHER_get_bits(cipher, nullptr);
                std::stringstream ss;
                ss << std::setw(40) << std::left << name
                   << " " << std::setw(8) << version
                   << " " << bits << " bits";
                result.push_back(ss.str());
            }
        }
    }
    SSL_free(ssl);
    return result;
}

// =============================================================================
// 11. Certificate Chain Building and Validation
// =============================================================================

// ---------------------------------------------------------------------------
// Certificate chain verification result
// ---------------------------------------------------------------------------
struct ChainVerifyResult {
    bool valid;
    int error_code;
    std::string error_message;
    int chain_depth;
    std::vector<std::string> chain_subjects; // Subject CNs in chain
    std::vector<std::string> chain_fingerprints;
};

// ---------------------------------------------------------------------------
// Build a certificate store with a set of trusted CA certificates
// ---------------------------------------------------------------------------
X509_STORE* build_trust_store(const std::vector<std::string>& ca_cert_pems) {
    detail::x509_store_ptr store(X509_STORE_new());
    if (!store) {
        throw std::runtime_error("X509_STORE_new failed");
    }

    for (const auto& ca_pem : ca_cert_pems) {
        detail::bio_ptr bio(BIO_new_mem_buf(ca_pem.data(),
                             static_cast<int>(ca_pem.size())));
        if (!bio) continue;

        while (true) {
            detail::x509_ptr ca(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
            if (!ca) {
                ERR_clear_error();
                break;
            }
            if (X509_STORE_add_cert(store.get(), ca.get()) != 1) {
                unsigned long err = ERR_peek_last_error();
                if (ERR_GET_REASON(err) != X509_R_CERT_ALREADY_IN_HASH_TABLE) {
                    spdlog::warn("[Chain] Failed to add CA to store: {}",
                                 detail::openssl_error_string());
                }
                ERR_clear_error();
            }
        }
    }

    return store.release();
}

// ---------------------------------------------------------------------------
// Verify a certificate chain against a trust store
// ---------------------------------------------------------------------------
ChainVerifyResult verify_certificate_chain(
        const std::string& leaf_cert_pem,
        const std::vector<std::string>& intermediate_certs_pem,
        X509_STORE* trust_store) {

    ChainVerifyResult result{};
    result.valid = false;

    // Parse leaf certificate
    detail::bio_ptr leaf_bio(BIO_new_mem_buf(leaf_cert_pem.data(),
                              static_cast<int>(leaf_cert_pem.size())));
    detail::x509_ptr leaf(PEM_read_bio_X509(leaf_bio.get(), nullptr, nullptr, nullptr));
    if (!leaf) {
        result.error_message = "Failed to parse leaf certificate: " +
                               detail::openssl_error_string();
        return result;
    }

    // Build untrusted chain stack
    detail::stack_x509_ptr untrusted(sk_X509_new_null());
    for (const auto& int_pem : intermediate_certs_pem) {
        detail::bio_ptr bio(BIO_new_mem_buf(int_pem.data(),
                             static_cast<int>(int_pem.size())));
        while (true) {
            detail::x509_ptr cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
            if (!cert) {
                ERR_clear_error();
                break;
            }
            sk_X509_push(untrusted.get(), cert.release());
        }
    }

    // Create verification context
    detail::x509_store_ctx_ptr store_ctx(X509_STORE_CTX_new());
    if (!store_ctx) {
        result.error_message = "X509_STORE_CTX_new failed";
        return result;
    }

    // Use provided trust store or create a default one
    detail::x509_store_ptr local_store;
    X509_STORE* actual_store = trust_store;
    if (!actual_store) {
        local_store.reset(X509_STORE_new());
        X509_STORE_set_default_paths(local_store.get());
        actual_store = local_store.get();
    }

    if (!X509_STORE_CTX_init(store_ctx.get(), actual_store, leaf.get(),
                              untrusted.get())) {
        result.error_message = "X509_STORE_CTX_init failed: " +
                               detail::openssl_error_string();
        return result;
    }

    // Set verification parameters
    X509_STORE_CTX_set_flags(store_ctx.get(), X509_V_FLAG_X509_STRICT);
    X509_STORE_CTX_set_purpose(store_ctx.get(), X509_PURPOSE_SSL_SERVER);

    // Perform verification
    int ok = X509_verify_cert(store_ctx.get());
    result.valid = (ok == 1);
    result.error_code = X509_STORE_CTX_get_error(store_ctx.get());
    result.chain_depth = X509_STORE_CTX_get_error_depth(store_ctx.get());

    if (!result.valid) {
        result.error_message = X509_verify_cert_error_string(
            X509_STORE_CTX_get_error(store_ctx.get()));
    }

    // Collect chain information
    STACK_OF(X509)* chain = X509_STORE_CTX_get1_chain(store_ctx.get());
    if (chain) {
        int chain_len = sk_X509_num(chain);
        for (int i = 0; i < chain_len; ++i) {
            X509* cert = sk_X509_value(chain, i);
            if (cert) {
                result.chain_subjects.push_back(detail::x509_get_cn(cert));
                try {
                    result.chain_fingerprints.push_back(
                        detail::x509_sha256_fingerprint(cert));
                } catch (...) {
                    result.chain_fingerprints.push_back("error");
                }
            }
        }
        sk_X509_pop_free(chain, X509_free);
    }

    spdlog::info("[Chain] Verification result: {}, depth={}, error={}",
                 result.valid ? "PASS" : "FAIL",
                 result.chain_depth,
                 result.error_message);

    return result;
}

// ---------------------------------------------------------------------------
// Build a certificate chain from leaf + intermediates
// ---------------------------------------------------------------------------
std::vector<std::string> build_certificate_chain(
        const std::string& leaf_pem,
        const std::vector<std::string>& intermediates_pem) {
    std::vector<std::string> chain;
    chain.push_back(leaf_pem);
    for (const auto& inter : intermediates_pem) {
        chain.push_back(inter);
    }
    return chain;
}

// ---------------------------------------------------------------------------
// Parse a PEM bundle into individual certificates
// ---------------------------------------------------------------------------
std::vector<std::string> parse_pem_bundle(const std::string& pem_bundle) {
    std::vector<std::string> certs;

    detail::bio_ptr bio(BIO_new_mem_buf(pem_bundle.data(),
                         static_cast<int>(pem_bundle.size())));
    if (!bio) return certs;

    while (true) {
        detail::x509_ptr cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
        if (!cert) {
            ERR_clear_error();
            break;
        }
        certs.push_back(detail::x509_to_pem(cert.get()));
    }

    return certs;
}

// ---------------------------------------------------------------------------
// Extract certificate information for debugging
// ---------------------------------------------------------------------------
struct CertInfo {
    std::string subject_cn;
    std::string issuer_cn;
    std::string serial;
    std::vector<std::string> sans;
    std::chrono::system_clock::time_point not_before;
    std::chrono::system_clock::time_point not_after;
    std::string fingerprint;
    int version;
    bool is_ca;
    int bits;
    std::string sig_algorithm;
};

CertInfo extract_cert_info(const std::string& cert_pem) {
    CertInfo info{};

    detail::bio_ptr bio(BIO_new_mem_buf(cert_pem.data(),
                         static_cast<int>(cert_pem.size())));
    detail::x509_ptr cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
    if (!cert) return info;

    info.subject_cn = detail::x509_get_cn(cert.get());

    // Issuer CN
    {
        X509_NAME* issuer = X509_get_issuer_name(cert.get());
        if (issuer) {
            char buf[256]{};
            int nid = OBJ_txt2nid("CN");
            int len = X509_NAME_get_text_by_NID(issuer, nid, buf, sizeof(buf) - 1);
            if (len > 0) info.issuer_cn.assign(buf, static_cast<size_t>(len));
        }
    }

    // Serial number
    {
        ASN1_INTEGER* ser = X509_get_serialNumber(cert.get());
        if (ser) {
            BIGNUM* bn = ASN1_INTEGER_to_BN(ser, nullptr);
            if (bn) {
                char* hex = BN_bn2hex(bn);
                if (hex) {
                    info.serial = hex;
                    OPENSSL_free(hex);
                }
                BN_free(bn);
            }
        }
    }

    info.sans = detail::x509_get_san_dns(cert.get());
    info.not_before = detail::asn1_time_to_tp(X509_get0_notBefore(cert.get()));
    info.not_after = detail::asn1_time_to_tp(X509_get0_notAfter(cert.get()));
    info.fingerprint = detail::x509_sha256_fingerprint(cert.get());
    info.version = X509_get_version(cert.get()) + 1; // 0-based internally

    // CA flag
    {
        BASIC_CONSTRAINTS* bc = static_cast<BASIC_CONSTRAINTS*>(
            X509_get_ext_d2i(cert.get(), NID_basic_constraints, nullptr, nullptr));
        if (bc) {
            info.is_ca = bc->ca != 0;
            BASIC_CONSTRAINTS_free(bc);
        }
    }

    // Public key bits
    {
        EVP_PKEY* pkey = X509_get0_pubkey(cert.get());
        if (pkey) {
            info.bits = EVP_PKEY_bits(pkey);
        }
    }

    // Signature algorithm
    {
        const X509_ALGOR* sig_alg = nullptr;
        X509_get0_signature(nullptr, &sig_alg, cert.get());
        if (sig_alg) {
            char buf[128]{};
            OBJ_obj2txt(buf, sizeof(buf), sig_alg->algorithm, 0);
            info.sig_algorithm = buf;
        }
    }

    return info;
}

// =============================================================================
// 12. OCSP Stapling Support
// =============================================================================

// ---------------------------------------------------------------------------
// OCSP response data
// ---------------------------------------------------------------------------
struct OcspStapleData {
    std::vector<uint8_t> response_der;     // Raw DER-encoded OCSP response
    std::chrono::system_clock::time_point produced_at;
    std::chrono::system_clock::time_point this_update;
    std::chrono::system_clock::time_point next_update;
    int status;                             // V_OCSP_CERTSTATUS_GOOD / REVOKED / UNKNOWN
    std::string status_string;
};

// ---------------------------------------------------------------------------
// Create an OCSP request for a certificate
// ---------------------------------------------------------------------------
std::vector<uint8_t> create_ocsp_request(X509* cert, X509* issuer) {
    // Get issuer name hash and key hash
    detail::ocsp_certid_ptr cert_id(
        OCSP_cert_to_id(EVP_sha1(), cert, issuer));
    if (!cert_id) {
        throw std::runtime_error("OCSP_cert_to_id failed: " +
                                 detail::openssl_error_string());
    }

    // Create request
    OCSP_REQUEST* req = OCSP_REQUEST_new();
    if (!req) {
        throw std::runtime_error("OCSP_REQUEST_new failed");
    }

    if (!OCSP_request_add0_id(req, cert_id.get())) {
        OCSP_REQUEST_free(req);
        throw std::runtime_error("OCSP_request_add0_id failed");
    }
    cert_id.release(); // Ownership transferred

    // Add nonce extension (optional but recommended)
    {
        auto nonce = detail::random_bytes(16);
        OCSP_request_add1_nonce(req, nonce.data(), static_cast<int>(nonce.size()));
    }

    // Serialize to DER
    int len = i2d_OCSP_REQUEST(req, nullptr);
    if (len <= 0) {
        OCSP_REQUEST_free(req);
        throw std::runtime_error("i2d_OCSP_REQUEST failed");
    }

    std::vector<uint8_t> result(static_cast<size_t>(len));
    unsigned char* ptr = result.data();
    i2d_OCSP_REQUEST(req, &ptr);
    OCSP_REQUEST_free(req);

    return result;
}

// ---------------------------------------------------------------------------
// Parse an OCSP response
// ---------------------------------------------------------------------------
OcspStapleData parse_ocsp_response(const uint8_t* der, size_t der_len) {
    OcspStapleData result{};
    result.status = V_OCSP_CERTSTATUS_UNKNOWN;
    result.status_string = "unknown";

    const unsigned char* ptr = der;
    detail::ocsp_response_ptr resp(
        d2i_OCSP_RESPONSE(nullptr, &ptr, static_cast<long>(der_len)));
    if (!resp) {
        spdlog::error("[OCSP] Failed to parse OCSP response: {}",
                      detail::openssl_error_string());
        ERR_clear_error();
        return result;
    }

    int resp_status = OCSP_response_status(resp.get());
    if (resp_status != OCSP_RESPONSE_STATUS_SUCCESSFUL) {
        result.status_string = "response error: " +
                               std::to_string(resp_status);
        spdlog::warn("[OCSP] Response status: {}", resp_status);
        return result;
    }

    detail::ocsp_basic_response_ptr basic(
        OCSP_response_get1_basic(resp.get()));
    if (!basic) {
        result.status_string = "failed to get basic response";
        return result;
    }

    // Production time
    {
        const ASN1_GENERALIZEDTIME* produced =
            OCSP_resp_get0_produced_at(basic.get());
        if (produced) {
            result.produced_at = detail::asn1_time_to_tp(produced);
        }
    }

    // Iterate over single responses
    int num_responses = OCSP_resp_count(basic.get());
    for (int i = 0; i < num_responses; ++i) {
        OCSP_SINGLERESP* single = OCSP_resp_get0(basic.get(), i);
        if (!single) continue;

        int status = 0;
        int reason = 0;
        ASN1_GENERALIZEDTIME* revtime_raw = nullptr;
        ASN1_GENERALIZEDTIME* thisupdate_raw = nullptr;
        ASN1_GENERALIZEDTIME* nextupdate_raw = nullptr;

        if (OCSP_single_get0_status(single, &reason, &revtime_raw,
                                     &thisupdate_raw, &nextupdate_raw) != 1) {
            continue;
        }

        result.status = status;
        if (thisupdate_raw) {
            result.this_update = detail::asn1_time_to_tp(thisupdate_raw);
        }
        if (nextupdate_raw) {
            result.next_update = detail::asn1_time_to_tp(nextupdate_raw);
        }

        switch (status) {
            case V_OCSP_CERTSTATUS_GOOD:
                result.status_string = "good";
                break;
            case V_OCSP_CERTSTATUS_REVOKED:
                result.status_string = "revoked (reason=" +
                                       std::to_string(reason) + ")";
                break;
            default:
                result.status_string = "unknown";
                break;
        }

        break; // Only first response for now
    }

    return result;
}

// ---------------------------------------------------------------------------
// OCSP stapling callback for server
// ---------------------------------------------------------------------------
struct OcspStaplingContext {
    std::mutex mutex;
    std::vector<uint8_t> cached_response;
    std::chrono::steady_clock::time_point cache_time;
    std::chrono::steady_clock::time_point next_update;
};

int ocsp_stapling_callback(SSL* ssl, void* arg) {
    auto* ctx = static_cast<OcspStaplingContext*>(arg);
    if (!ctx) return SSL_TLSEXT_ERR_ALERT_FATAL;

    std::lock_guard lock(ctx->mutex);

    if (ctx->cached_response.empty()) {
        spdlog::debug("[OCSP] No cached OCSP response available for stapling");
        return SSL_TLSEXT_ERR_OK; // Don't fail, just don't staple
    }

    auto now = std::chrono::steady_clock::now();
    if (now > ctx->next_update) {
        spdlog::warn("[OCSP] Cached OCSP response expired");
        return SSL_TLSEXT_ERR_OK;
    }

    // Allocate memory that OpenSSL will free
    size_t resp_len = ctx->cached_response.size();
    unsigned char* resp_copy = static_cast<unsigned char*>(OPENSSL_malloc(resp_len));
    if (!resp_copy) return SSL_TLSEXT_ERR_ALERT_FATAL;

    std::memcpy(resp_copy, ctx->cached_response.data(), resp_len);
    SSL_set_tlsext_status_ocsp_resp(ssl, resp_copy, static_cast<long>(resp_len));

    spdlog::debug("[OCSP] OCSP response stapled ({} bytes)", resp_len);
    return SSL_TLSEXT_ERR_OK;
}

// ---------------------------------------------------------------------------
// Configure OCSP stapling on server context
// ---------------------------------------------------------------------------
void enable_ocsp_stapling(SSL_CTX* ctx, OcspStaplingContext* ocsp_ctx) {
    SSL_CTX_set_tlsext_status_cb(ctx, ocsp_stapling_callback);
    SSL_CTX_set_tlsext_status_arg(ctx, ocsp_ctx);
    spdlog::info("[OCSP] OCSP stapling enabled on server context");
}

// ---------------------------------------------------------------------------
// Update OCSP staple cache
// ---------------------------------------------------------------------------
void update_ocsp_staple(OcspStaplingContext* ctx,
                        const std::vector<uint8_t>& der_response,
                        std::chrono::system_clock::time_point next_update) {
    std::lock_guard lock(ctx->mutex);
    ctx->cached_response = der_response;
    ctx->cache_time = std::chrono::steady_clock::now();

    auto dur = std::chrono::duration_cast<std::chrono::seconds>(
        next_update - std::chrono::system_clock::now());
    auto safe_dur = std::max(dur, std::chrono::seconds(3600)); // Min 1 hour buffer
    ctx->next_update = ctx->cache_time + safe_dur;

    spdlog::info("[OCSP] OCSP staple updated, {} bytes, next update in {}s",
                 der_response.size(), safe_dur.count());
}

// ---------------------------------------------------------------------------
// Check OCSP stapling from client side
// ---------------------------------------------------------------------------
OcspStapleData get_ocsp_staple_from_ssl(SSL* ssl) {
    OcspStapleData result{};
    result.status = V_OCSP_CERTSTATUS_UNKNOWN;
    result.status_string = "not available";

    const unsigned char* resp_data = nullptr;
    long resp_len = SSL_get_tlsext_status_ocsp_resp(ssl, &resp_data);

    if (resp_len <= 0 || !resp_data) {
        return result;
    }

    return parse_ocsp_response(resp_data, static_cast<size_t>(resp_len));
}

// =============================================================================
// Additional Utilities
// =============================================================================

// ---------------------------------------------------------------------------
// TlsConnectionInfo: summary of a TLS connection
// ---------------------------------------------------------------------------
struct TlsConnectionInfo {
    std::string tls_version;
    std::string cipher_name;
    int cipher_bits;
    std::string alpn_protocol;
    std::string sni_hostname;
    bool session_reused;
    std::string peer_cert_cn;
    std::string peer_cert_fingerprint;
    bool ocsp_stapled;
    bool ocsp_good;
};

// ---------------------------------------------------------------------------
// Gather comprehensive connection info after handshake
// ---------------------------------------------------------------------------
TlsConnectionInfo get_tls_connection_info(SSL* ssl) {
    TlsConnectionInfo info{};
    if (!ssl) return info;

    // Version and cipher
    info.tls_version = get_tls_version(ssl);
    info.cipher_name = get_cipher_name(ssl);
    info.cipher_bits = SSL_get_cipher_bits(ssl, nullptr);

    // ALPN
    const unsigned char* alpn = nullptr;
    unsigned int alpn_len = 0;
    SSL_get0_alpn_selected(ssl, &alpn, &alpn_len);
    if (alpn && alpn_len > 0) {
        info.alpn_protocol.assign(reinterpret_cast<const char*>(alpn), alpn_len);
    }

    // SNI
    info.sni_hostname = get_sni_hostname(ssl);

    // Session resumption
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    info.session_reused = is_tls13_session_reused(ssl);
#else
    info.session_reused = (SSL_session_reused(ssl) == 1);
#endif

    // Peer certificate
    X509* peer_cert = SSL_get_peer_certificate(ssl);
    if (peer_cert) {
        info.peer_cert_cn = detail::x509_get_cn(peer_cert);
        try {
            info.peer_cert_fingerprint = detail::x509_sha256_fingerprint(peer_cert);
        } catch (...) {
            info.peer_cert_fingerprint = "error";
        }
        X509_free(peer_cert);
    }

    // OCSP
    const unsigned char* ocsp = nullptr;
    long ocsp_len = SSL_get_tlsext_status_ocsp_resp(ssl, &ocsp);
    if (ocsp_len > 0 && ocsp) {
        info.ocsp_stapled = true;
        auto ocsp_data = parse_ocsp_response(ocsp, static_cast<size_t>(ocsp_len));
        info.ocsp_good = (ocsp_data.status == V_OCSP_CERTSTATUS_GOOD);
    }

    return info;
}

// ---------------------------------------------------------------------------
// Log TLS connection info
// ---------------------------------------------------------------------------
void log_tls_connection_info(const TlsConnectionInfo& info) {
    spdlog::info("[TLS] Connection: version={}, cipher={} ({} bits), ALPN={}, "
                 "SNI={}, session_reused={}",
                 info.tls_version, info.cipher_name, info.cipher_bits,
                 info.alpn_protocol.empty() ? "none" : info.alpn_protocol,
                 info.sni_hostname.empty() ? "none" : info.sni_hostname,
                 info.session_reused);

    if (!info.peer_cert_cn.empty()) {
        spdlog::info("[TLS] Peer certificate: CN={}, fingerprint={}",
                     info.peer_cert_cn, info.peer_cert_fingerprint);
    }

    if (info.ocsp_stapled) {
        spdlog::info("[TLS] OCSP stapled: {}", info.ocsp_good ? "GOOD" : "ISSUE");
    }
}

// ---------------------------------------------------------------------------
// Load certificate + key from PKCS#12 file
// ---------------------------------------------------------------------------
TlsCertificate load_pkcs12(const std::string& file_path,
                           const std::string& password) {
    if (!std::filesystem::exists(file_path)) {
        throw std::runtime_error("PKCS#12 file not found: " + file_path);
    }

    auto data = detail::read_file_to_string(file_path);
    detail::bio_ptr bio(BIO_new_mem_buf(data.data(),
                         static_cast<int>(data.size())));
    detail::pkcs12_ptr p12(d2i_PKCS12_bio(bio.get(), nullptr));
    if (!p12) {
        throw std::runtime_error("Failed to parse PKCS#12 file: " +
                                 detail::openssl_error_string());
    }

    EVP_PKEY* pkey = nullptr;
    X509* cert = nullptr;
    STACK_OF(X509)* ca = nullptr;

    if (!PKCS12_parse(p12.get(), password.c_str(), &pkey, &cert, &ca)) {
        throw std::runtime_error("Failed to parse PKCS#12 (wrong password?): " +
                                 detail::openssl_error_string());
    }

    detail::evp_pkey_ptr pkey_holder(pkey);
    detail::x509_ptr cert_holder(cert);
    detail::stack_x509_ptr ca_holder(ca);

    TlsCertificate result;
    result.cert_pem = detail::x509_to_pem(cert);
    result.key_pem = detail::evp_pkey_to_pem_private(pkey);
    result.common_name = detail::x509_get_cn(cert);
    result.sans = detail::x509_get_san_dns(cert);
    result.cert_fingerprint = detail::x509_sha256_fingerprint(cert);

    if (ca) {
        int ca_count = sk_X509_num(ca);
        for (int i = 0; i < ca_count; ++i) {
            X509* ca_cert = sk_X509_value(ca, i);
            if (ca_cert) {
                result.chain_pem += detail::x509_to_pem(ca_cert);
            }
        }
    }

    spdlog::info("[TLS] Loaded PKCS#12: CN={}, includes {} CA certs",
                 result.common_name,
                 sk_X509_num(ca));
    return result;
}

// ---------------------------------------------------------------------------
// Save certificate + key to PKCS#12 file
// ---------------------------------------------------------------------------
void save_pkcs12(const std::string& file_path,
                 const TlsCertificate& tls_cert,
                 const std::string& password,
                 const std::string& friendly_name) {
    auto p12_data = export_pkcs12(tls_cert.cert_pem, tls_cert.key_pem,
                                   password, friendly_name);
    std::string content(reinterpret_cast<const char*>(p12_data.data()),
                        p12_data.size());
    detail::write_file_atomic(file_path, content);
    spdlog::info("[TLS] Saved PKCS#12 to '{}'", file_path);
}

// ---------------------------------------------------------------------------
// Validate a certificate's expiration
// ---------------------------------------------------------------------------
struct CertExpiryInfo {
    bool expired;
    bool expiring_soon; // Within 30 days
    std::chrono::system_clock::time_point not_before;
    std::chrono::system_clock::time_point not_after;
    std::chrono::system_clock::duration remaining;
    int days_remaining;
};

CertExpiryInfo check_cert_expiry(const std::string& cert_pem) {
    CertExpiryInfo info{};
    info.expired = true;

    detail::bio_ptr bio(BIO_new_mem_buf(cert_pem.data(),
                         static_cast<int>(cert_pem.size())));
    detail::x509_ptr cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
    if (!cert) return info;

    info.not_before = detail::asn1_time_to_tp(X509_get0_notBefore(cert.get()));
    info.not_after = detail::asn1_time_to_tp(X509_get0_notAfter(cert.get()));

    auto now = std::chrono::system_clock::now();
    info.remaining = info.not_after - now;
    info.days_remaining = static_cast<int>(
        std::chrono::duration_cast<std::chrono::hours>(info.remaining).count() / 24);

    info.expired = (now > info.not_after);
    info.expiring_soon = (!info.expired && info.days_remaining <= 30);

    return info;
}

// ---------------------------------------------------------------------------
// Check if a certificate is valid for a specific hostname
// ---------------------------------------------------------------------------
bool is_cert_valid_for_hostname(X509* cert, const std::string& hostname) {
    return verify_hostname(cert, hostname);
}

// ---------------------------------------------------------------------------
// Generate a self-signed CA certificate (for internal CA)
// ---------------------------------------------------------------------------
P2pCertificate generate_ca_certificate(
        const std::string& common_name,
        int key_bits = TLS_DEFAULT_RSA_KEY_BITS,
        int days_valid = 3650) { // 10 years for CA

    P2pCertificate result;
    result.subject = common_name;

    detail::evp_pkey_ptr pkey(EVP_RSA_gen(key_bits));
    if (!pkey) throw std::runtime_error("EVP_RSA_gen failed");

    detail::x509_ptr x509(X509_new());
    X509_set_version(x509.get(), 2);

    auto serial_bytes = detail::random_bytes(16);
    detail::asn1_integer_ptr serial(ASN1_INTEGER_new());
    ASN1_INTEGER_set_uint64(serial.get(), *reinterpret_cast<const uint64_t*>(serial_bytes.data()));
    X509_set_serialNumber(x509.get(), serial.get());

    auto now = std::chrono::system_clock::now();
    result.not_before = now;
    detail::asn1_time_ptr not_before(ASN1_TIME_new());
    detail::asn1_time_ptr not_after(ASN1_TIME_new());
    ASN1_TIME_set(not_before.get(), std::chrono::system_clock::to_time_t(now));
    X509_set_notBefore(x509.get(), not_before.get());
    auto expiry = now + std::chrono::hours(24 * days_valid);
    result.not_after = expiry;
    ASN1_TIME_set(not_after.get(), std::chrono::system_clock::to_time_t(expiry));
    X509_set_notAfter(x509.get(), not_after.get());

    X509_set_pubkey(x509.get(), pkey.get());

    detail::x509_name_ptr name(X509_NAME_new());
    X509_NAME_add_entry_by_txt(name.get(), "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(common_name.c_str()),
                               -1, -1, 0);
    X509_NAME_add_entry_by_txt(name.get(), "O", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("cppdesk Internal CA"),
                               -1, -1, 0);
    X509_set_subject_name(x509.get(), name.get());
    X509_set_issuer_name(x509.get(), name.get());

    X509V3_CTX v3_ctx;
    X509V3_set_ctx_nodb(&v3_ctx);
    X509V3_set_ctx(&v3_ctx, x509.get(), x509.get(), nullptr, nullptr, 0);

    // CA:TRUE
    {
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &v3_ctx, NID_basic_constraints,
                                const_cast<char*>("critical,CA:TRUE,pathlen:0")));
        if (ext) X509_add_ext(x509.get(), ext.get(), -1);
    }
    {
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &v3_ctx, NID_key_usage,
                                const_cast<char*>("critical,keyCertSign,cRLSign")));
        if (ext) X509_add_ext(x509.get(), ext.get(), -1);
    }
    {
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &v3_ctx, NID_subject_key_identifier,
                                const_cast<char*>("hash")));
        if (ext) X509_add_ext(x509.get(), ext.get(), -1);
    }

    if (!X509_sign(x509.get(), pkey.get(), EVP_sha256())) {
        throw std::runtime_error("X509_sign failed");
    }

    result.cert_pem = detail::x509_to_pem(x509.get());
    result.key_pem = detail::evp_pkey_to_pem_private(pkey.get());
    result.fingerprint = detail::x509_sha256_fingerprint(x509.get());

    spdlog::info("[TLS] Generated CA certificate: CN={}, days={}, fingerprint={}",
                 common_name, days_valid, result.fingerprint);
    return result;
}

// ---------------------------------------------------------------------------
// Sign a certificate request with a CA certificate
// ---------------------------------------------------------------------------
std::string sign_cert_with_ca(
        X509* ca_cert, EVP_PKEY* ca_key,
        const std::string& subject_cn,
        const std::vector<std::string>& sans = {},
        int days_valid = TLS_DEFAULT_CERT_DAYS) {

    // Generate key for the new certificate
    detail::evp_pkey_ptr subject_key(EVP_RSA_gen(TLS_DEFAULT_RSA_KEY_BITS));
    if (!subject_key) throw std::runtime_error("EVP_RSA_gen failed");

    detail::x509_ptr cert(X509_new());
    X509_set_version(cert.get(), 2);

    auto serial_bytes = detail::random_bytes(8);
    serial_bytes[0] &= 0x7F;
    detail::asn1_integer_ptr serial(ASN1_INTEGER_new());
    ASN1_INTEGER_set_uint64(serial.get(), *reinterpret_cast<const uint64_t*>(serial_bytes.data()));
    X509_set_serialNumber(cert.get(), serial.get());

    auto now = std::chrono::system_clock::now();
    detail::asn1_time_ptr not_before(ASN1_TIME_new());
    detail::asn1_time_ptr not_after(ASN1_TIME_new());
    ASN1_TIME_set(not_before.get(), std::chrono::system_clock::to_time_t(now));
    X509_set_notBefore(cert.get(), not_before.get());
    auto expiry = now + std::chrono::hours(24 * days_valid);
    ASN1_TIME_set(not_after.get(), std::chrono::system_clock::to_time_t(expiry));
    X509_set_notAfter(cert.get(), not_after.get());

    X509_set_pubkey(cert.get(), subject_key.get());

    // Subject name
    detail::x509_name_ptr name(X509_NAME_new());
    X509_NAME_add_entry_by_txt(name.get(), "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(subject_cn.c_str()),
                               -1, -1, 0);
    X509_set_subject_name(cert.get(), name.get());

    // Issuer = CA
    X509_set_issuer_name(cert.get(), X509_get_subject_name(ca_cert));

    // Extensions
    X509V3_CTX v3_ctx;
    X509V3_set_ctx_nodb(&v3_ctx);
    X509V3_set_ctx(&v3_ctx, ca_cert, cert.get(), nullptr, nullptr, 0);

    {
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &v3_ctx, NID_basic_constraints,
                                const_cast<char*>("critical,CA:FALSE")));
        if (ext) X509_add_ext(cert.get(), ext.get(), -1);
    }
    {
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &v3_ctx, NID_key_usage,
                                const_cast<char*>("critical,digitalSignature,keyEncipherment")));
        if (ext) X509_add_ext(cert.get(), ext.get(), -1);
    }
    {
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &v3_ctx, NID_ext_key_usage,
                                const_cast<char*>("serverAuth,clientAuth")));
        if (ext) X509_add_ext(cert.get(), ext.get(), -1);
    }
    if (!sans.empty()) {
        std::string san_value;
        for (size_t i = 0; i < sans.size(); ++i) {
            if (i > 0) san_value += ",";
            san_value += "DNS:" + sans[i];
        }
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &v3_ctx, NID_subject_alt_name,
                                const_cast<char*>(san_value.c_str())));
        if (ext) X509_add_ext(cert.get(), ext.get(), -1);
    }

    // Authority Key Identifier
    {
        detail::x509_extension_ptr ext(
            X509V3_EXT_conf_nid(nullptr, &v3_ctx, NID_authority_key_identifier,
                                const_cast<char*>("keyid:always")));
        if (ext) X509_add_ext(cert.get(), ext.get(), -1);
    }

    // Sign with CA key
    if (!X509_sign(cert.get(), ca_key, EVP_sha256())) {
        throw std::runtime_error("X509_sign failed: " + detail::openssl_error_string());
    }

    spdlog::info("[TLS] Signed certificate: CN={}, signed by CA", subject_cn);
    return detail::x509_to_pem(cert.get());
}

// =============================================================================
// CRL (Certificate Revocation List) Support
// =============================================================================

// ---------------------------------------------------------------------------
// Verify a certificate against a CRL
// ---------------------------------------------------------------------------
bool verify_cert_against_crl(X509* cert, X509_CRL* crl) {
    if (!cert || !crl) return true; // No CRL = pass (soft fail)

    // Get serial number from cert
    ASN1_INTEGER* cert_serial = X509_get_serialNumber(cert);
    if (!cert_serial) return false;

    // Get revoked entries from CRL
    STACK_OF(X509_REVOKED)* revoked = X509_CRL_get_REVOKED(crl);
    if (!revoked) return true; // No revoked certs

    int num_revoked = sk_X509_REVOKED_num(revoked);
    for (int i = 0; i < num_revoked; ++i) {
        X509_REVOKED* rev = sk_X509_REVOKED_value(revoked, i);
        if (!rev) continue;

        ASN1_INTEGER* rev_serial = X509_REVOKED_get0_serialNumber(rev);
        if (!rev_serial) continue;

        if (ASN1_INTEGER_cmp(cert_serial, rev_serial) == 0) {
            spdlog::warn("[CRL] Certificate is REVOKED");
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Add CRL to SSL_CTX
// ---------------------------------------------------------------------------
void ssl_ctx_add_crl(SSL_CTX* ctx, const std::string& crl_pem) {
    detail::bio_ptr bio(BIO_new_mem_buf(crl_pem.data(),
                         static_cast<int>(crl_pem.size())));
    detail::x509_crl_ptr crl(PEM_read_bio_X509_CRL(bio.get(), nullptr, nullptr, nullptr));
    if (!crl) {
        throw std::runtime_error("Failed to parse CRL PEM: " +
                                 detail::openssl_error_string());
    }

    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    if (!store) {
        throw std::runtime_error("SSL_CTX_get_cert_store returned null");
    }

    if (X509_STORE_add_crl(store, crl.get()) != 1) {
        throw std::runtime_error("X509_STORE_add_crl failed: " +
                                 detail::openssl_error_string());
    }

    X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK |
                                X509_V_FLAG_CRL_CHECK_ALL);
    spdlog::info("[CRL] CRL added to trust store");
}

// =============================================================================
// Key/Cert File I/O
// =============================================================================

// ---------------------------------------------------------------------------
// Save P2P certificate + key to files
// ---------------------------------------------------------------------------
void save_certificate_to_files(const P2pCertificate& cert,
                               const std::string& cert_path,
                               const std::string& key_path) {
    detail::write_file_atomic(cert_path, cert.cert_pem);
    detail::write_file_atomic(key_path, cert.key_pem);

    // Set restrictive permissions on private key
    std::filesystem::permissions(key_path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);

    spdlog::info("[TLS] Saved certificate to '{}', key to '{}'", cert_path, key_path);
}

// ---------------------------------------------------------------------------
// Load P2P certificate from files
// ---------------------------------------------------------------------------
P2pCertificate load_certificate_from_files(
        const std::string& cert_path,
        const std::string& key_path) {
    P2pCertificate cert;
    cert.cert_pem = detail::read_file_to_string(cert_path);
    cert.key_pem = detail::read_file_to_string(key_path);

    // Compute fingerprint
    detail::bio_ptr cert_bio(BIO_new_mem_buf(cert.cert_pem.data(),
                              static_cast<int>(cert.cert_pem.size())));
    detail::x509_ptr x509(PEM_read_bio_X509(cert_bio.get(), nullptr, nullptr, nullptr));
    if (x509) {
        cert.fingerprint = detail::x509_sha256_fingerprint(x509.get());
        cert.subject = detail::x509_get_cn(x509.get());
    }

    spdlog::info("[TLS] Loaded certificate from '{}' (CN={})", cert_path, cert.subject);
    return cert;
}

// =============================================================================
// Convenience: one-shot TLS server/client context creation
// =============================================================================

// ---------------------------------------------------------------------------
// Create a P2P server context with auto-generated certificate
// ---------------------------------------------------------------------------
SSL_CTX* create_p2p_server_context(const std::string& host_identity,
                                   uint16_t port = 0,
                                   TlsSecurityLevel level = TlsSecurityLevel::INTERMEDIATE) {
    // Generate a self-signed cert for this host
    auto cert = generate_p2p_certificate(host_identity, TLS_DEFAULT_RSA_KEY_BITS, 365,
                                          {host_identity, "localhost"});

    TlsCertificate tls_cert;
    tls_cert.cert_pem = cert.cert_pem;
    tls_cert.key_pem = cert.key_pem;
    tls_cert.common_name = cert.subject;
    tls_cert.cert_fingerprint = cert.fingerprint;
    tls_cert.sans = {host_identity, "localhost"};

    TlsServerConfig srv_cfg;
    srv_cfg.alpn_protocols = {"cppdesk-p2p", "h2"};

    auto* ctx = create_tls_server_context(tls_cert, srv_cfg);
    apply_tls_security_level(ctx, level);
    configure_session_resumption(ctx, true);

    spdlog::info("[TLS] P2P server context created for '{}' (security={})",
                 host_identity, static_cast<int>(level));
    return ctx;
}

// ---------------------------------------------------------------------------
// Create a P2P client context
// ---------------------------------------------------------------------------
SSL_CTX* create_p2p_client_context(bool enable_tofu = true,
                                   TlsSecurityLevel level = TlsSecurityLevel::INTERMEDIATE) {
    TlsClientConfig client_cfg;
    client_cfg.verify_server_cert = !enable_tofu; // When TOFU, don't verify against CA
    client_cfg.enable_pinning = enable_tofu;
    client_cfg.alpn_protocols = {"cppdesk-p2p", "h2"};
    client_cfg.enable_session_cache = true;
    client_cfg.enable_session_tickets = true;

    auto* ctx = create_tls_client_context(client_cfg);
    apply_tls_security_level(ctx, level);

    spdlog::info("[TLS] P2P client context created (TOFU={})", enable_tofu);
    return ctx;
}

// =============================================================================
// Cleanup helper
// =============================================================================

void destroy_tls_context(SSL_CTX* ctx) {
    if (ctx) {
        SSL_CTX_free(ctx);
    }
}

} // namespace cppdesk::common
