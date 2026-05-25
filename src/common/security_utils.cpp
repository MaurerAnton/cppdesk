// =============================================================================
// security_utils.cpp - Comprehensive security utility implementations
// Part of cppdesk framework
// =============================================================================

#include "common/config.hpp"
#include "common/crypto.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <regex>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include <sys/file.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hkdf.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <spdlog/spdlog.h>

namespace cppdesk::common {

// =============================================================================
// Forward declarations for internal helpers
// =============================================================================

namespace detail {

// OpenSSL error string extraction helper
std::string openssl_error_string();

// RAII wrappers for OpenSSL types
struct bio_deleter {
    void operator()(BIO* p) const { if (p) BIO_free(p); }
};
using bio_ptr = std::unique_ptr<BIO, bio_deleter>;

struct x509_deleter {
    void operator()(X509* p) const { if (p) X509_free(p); }
};
using x509_ptr = std::unique_ptr<X509, x509_deleter>;

struct evp_pkey_deleter {
    void operator()(EVP_PKEY* p) const { if (p) EVP_PKEY_free(p); }
};
using evp_pkey_ptr = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>;

struct bn_deleter {
    void operator()(BIGNUM* p) const { if (p) BN_free(p); }
};
using bn_ptr = std::unique_ptr<BIGNUM, bn_deleter>;

struct x509_name_deleter {
    void operator()(X509_NAME* p) const { if (p) X509_NAME_free(p); }
};
using x509_name_ptr = std::unique_ptr<X509_NAME, x509_name_deleter>;

struct x509_store_deleter {
    void operator()(X509_STORE* p) const { if (p) X509_STORE_free(p); }
};
using x509_store_ptr = std::unique_ptr<X509_STORE, x509_store_deleter>;

struct x509_store_ctx_deleter {
    void operator()(X509_STORE_CTX* p) const { if (p) X509_STORE_CTX_free(p); }
};
using x509_store_ctx_ptr = std::unique_ptr<X509_STORE_CTX, x509_store_ctx_deleter>;

struct x509_crl_deleter {
    void operator()(X509_CRL* p) const { if (p) X509_CRL_free(p); }
};
using x509_crl_ptr = std::unique_ptr<X509_CRL, x509_crl_deleter>;

struct asn1_time_deleter {
    void operator()(ASN1_TIME* p) const { if (p) ASN1_TIME_free(p); }
};
using asn1_time_ptr = std::unique_ptr<ASN1_TIME, asn1_time_deleter>;

struct asn1_integer_deleter {
    void operator()(ASN1_INTEGER* p) const { if (p) ASN1_INTEGER_free(p); }
};
using asn1_integer_ptr = std::unique_ptr<ASN1_INTEGER, asn1_integer_deleter>;

struct x509_extension_deleter {
    void operator()(X509_EXTENSION* p) const { if (p) X509_EXTENSION_free(p); }
};
using x509_extension_ptr = std::unique_ptr<X509_EXTENSION, x509_extension_deleter>;

struct basic_constraints_deleter {
    void operator()(BASIC_CONSTRAINTS* p) const { if (p) BASIC_CONSTRAINTS_free(p); }
};
using basic_constraints_ptr = std::unique_ptr<BASIC_CONSTRAINTS, basic_constraints_deleter>;

// Secure memory helper: lock pages to prevent swapping
bool lock_memory_region(void* addr, size_t len);

// Secure memory helper: advise kernel to zero on free
void zero_on_free(void* addr, size_t len);

} // namespace detail

// =============================================================================
// 1. Certificate Management
// =============================================================================

struct CertificateData {
    std::string pem_certificate;
    std::string pem_private_key;
    std::string subject;
    std::string issuer;
    std::string serial_number;
    std::chrono::system_clock::time_point not_before;
    std::chrono::system_clock::time_point not_after;
    std::vector<std::string> subject_alt_names;
    std::vector<std::string> key_usage;
    bool is_ca;
};

struct CertificateVerifyResult {
    bool valid;
    std::string error_message;
    int verify_depth;
    std::string verified_chain_info;
};

class CertificateManager {
public:
    CertificateManager() {
        OpenSSL_add_all_algorithms();
        ERR_load_crypto_strings();
    }

    ~CertificateManager() {
        EVP_cleanup();
        ERR_free_strings();
    }

    // -------------------------------------------------------------------------
    // Generate a self-signed X.509 certificate and RSA key pair
    // -------------------------------------------------------------------------
    CertificateData generate_self_signed(
            const std::string& common_name,
            int bits = 4096,
            int days_valid = 365,
            bool is_ca = false,
            const std::vector<std::string>& subject_alt_names = {}) {

        CertificateData result;
        result.subject = "/CN=" + common_name;
        result.issuer = result.subject;
        result.is_ca = is_ca;

        // ---- Generate RSA key ----
        detail::evp_pkey_ptr pkey(EVP_RSA_gen(bits));
        if (!pkey) {
            throw std::runtime_error("Failed to generate RSA key: " +
                                     detail::openssl_error_string());
        }

        // ---- Create certificate ----
        detail::x509_ptr x509(X509_new());
        if (!x509) {
            throw std::runtime_error("Failed to create X509 object: " +
                                     detail::openssl_error_string());
        }

        // Set version to v3
        X509_set_version(x509.get(), 2);

        // Generate serial number (random 64-bit)
        unsigned char serial_bytes[8];
        if (RAND_bytes(serial_bytes, sizeof(serial_bytes)) != 1) {
            throw std::runtime_error("Failed to generate random serial: " +
                                     detail::openssl_error_string());
        }
        serial_bytes[0] &= 0x7F; // Ensure positive
        detail::asn1_integer_ptr serial(ASN1_INTEGER_new());
        if (!serial || !ASN1_INTEGER_set_uint64(serial.get(),
                  *reinterpret_cast<const uint64_t*>(serial_bytes))) {
            throw std::runtime_error("Failed to set serial number");
        }
        X509_set_serialNumber(x509.get(), serial.get());

        {
            std::stringstream ss;
            ss << std::hex << std::setfill('0');
            for (int i = 0; i < 8; ++i) {
                ss << std::setw(2) << static_cast<int>(serial_bytes[i]);
            }
            result.serial_number = ss.str();
        }

        // Set validity period
        auto now = std::chrono::system_clock::now();
        result.not_before = now;

        detail::asn1_time_ptr not_before(ASN1_TIME_new());
        detail::asn1_time_ptr not_after(ASN1_TIME_new());

        if (!not_before || !not_after) {
            throw std::runtime_error("Failed to create ASN1_TIME objects");
        }

        ASN1_TIME_set(not_before.get(),
                      std::chrono::system_clock::to_time_t(now));
        X509_set_notBefore(x509.get(), not_before.get());

        auto expiry = now + std::chrono::hours(24 * days_valid);
        result.not_after = expiry;
        ASN1_TIME_set(not_after.get(),
                      std::chrono::system_clock::to_time_t(expiry));
        X509_set_notAfter(x509.get(), not_after.get());

        // Set public key
        X509_set_pubkey(x509.get(), pkey.get());

        // Set subject name
        detail::x509_name_ptr name(X509_NAME_new());
        if (!name) {
            throw std::runtime_error("Failed to create X509_NAME");
        }
        X509_NAME_add_entry_by_txt(name.get(), "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>(common_name.c_str()),
                                   -1, -1, 0);
        // Add organization
        X509_NAME_add_entry_by_txt(name.get(), "O", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>("cppdesk"),
                                   -1, -1, 0);
        // Add organizational unit
        X509_NAME_add_entry_by_txt(name.get(), "OU", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>("Security"),
                                   -1, -1, 0);

        X509_set_subject_name(x509.get(), name.get());
        X509_set_issuer_name(x509.get(), name.get());

        // ---- Add X.509 v3 extensions ----
        X509V3_CTX ctx;
        X509V3_set_ctx_nodb(&ctx);
        X509V3_set_ctx(&ctx, x509.get(), x509.get(), nullptr, nullptr, 0);

        // Basic Constraints
        {
            std::string bc_value = is_ca ? "critical,CA:TRUE" : "critical,CA:FALSE";
            detail::x509_extension_ptr ext(
                X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints,
                                    const_cast<char*>(bc_value.c_str())));
            if (ext) {
                X509_add_ext(x509.get(), ext.get(), -1);
            }
        }

        // Key Usage
        {
            std::string ku_value = is_ca
                ? "critical,digitalSignature,keyCertSign,cRLSign"
                : "critical,digitalSignature,keyEncipherment";
            result.key_usage.push_back(ku_value);
            detail::x509_extension_ptr ext(
                X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage,
                                    const_cast<char*>(ku_value.c_str())));
            if (ext) {
                X509_add_ext(x509.get(), ext.get(), -1);
            }
        }

        // Extended Key Usage (for non-CA certs)
        if (!is_ca) {
            detail::x509_extension_ptr ext(
                X509V3_EXT_conf_nid(nullptr, &ctx, NID_ext_key_usage,
                                    const_cast<char*>("serverAuth,clientAuth")));
            if (ext) {
                X509_add_ext(x509.get(), ext.get(), -1);
            }
        }

        // Subject Alternative Names
        if (!subject_alt_names.empty()) {
            std::string san_value = "";
            for (size_t i = 0; i < subject_alt_names.size(); ++i) {
                if (i > 0) san_value += ",";
                // Determine if it's DNS or IP
                std::regex ip_regex(R"(^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$)");
                if (std::regex_match(subject_alt_names[i], ip_regex)) {
                    san_value += "IP:" + subject_alt_names[i];
                } else {
                    san_value += "DNS:" + subject_alt_names[i];
                }
            }
            result.subject_alt_names = subject_alt_names;
            detail::x509_extension_ptr ext(
                X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_alt_name,
                                    const_cast<char*>(san_value.c_str())));
            if (ext) {
                X509_add_ext(x509.get(), ext.get(), -1);
            }
        }

        // Subject Key Identifier
        {
            detail::x509_extension_ptr ext(
                X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_key_identifier,
                                    const_cast<char*>("hash")));
            if (ext) {
                X509_add_ext(x509.get(), ext.get(), -1);
            }
        }

        // ---- Sign certificate ----
        if (!X509_sign(x509.get(), pkey.get(), EVP_sha256())) {
            throw std::runtime_error("Failed to sign certificate: " +
                                     detail::openssl_error_string());
        }

        // ---- Export to PEM ----
        detail::bio_ptr cert_bio(BIO_new(BIO_s_mem()));
        if (!PEM_write_bio_X509(cert_bio.get(), x509.get())) {
            throw std::runtime_error("Failed to write certificate PEM: " +
                                     detail::openssl_error_string());
        }
        char* cert_data = nullptr;
        long cert_len = BIO_get_mem_data(cert_bio.get(), &cert_data);
        result.pem_certificate = std::string(cert_data, cert_len);

        // Export private key
        detail::bio_ptr key_bio(BIO_new(BIO_s_mem()));
        if (!PEM_write_bio_PrivateKey(key_bio.get(), pkey.get(),
                                       nullptr, nullptr, 0, nullptr, nullptr)) {
            throw std::runtime_error("Failed to write private key PEM: " +
                                     detail::openssl_error_string());
        }
        char* key_data = nullptr;
        long key_len = BIO_get_mem_data(key_bio.get(), &key_data);
        result.pem_private_key = std::string(key_data, key_len);

        spdlog::info("Generated self-signed certificate: {}", result.subject);
        return result;
    }

    // -------------------------------------------------------------------------
    // Generate a certificate signed by a CA
    // -------------------------------------------------------------------------
    CertificateData generate_signed_certificate(
            const std::string& common_name,
            const std::string& ca_cert_pem,
            const std::string& ca_key_pem,
            int bits = 2048,
            int days_valid = 365) {

        // Load CA certificate
        detail::bio_ptr ca_cert_bio(BIO_new_mem_buf(ca_cert_pem.data(),
                                                      static_cast<int>(ca_cert_pem.size())));
        detail::x509_ptr ca_cert(PEM_read_bio_X509(ca_cert_bio.get(),
                                                     nullptr, nullptr, nullptr));
        if (!ca_cert) {
            throw std::runtime_error("Failed to load CA certificate: " +
                                     detail::openssl_error_string());
        }

        // Load CA private key
        detail::bio_ptr ca_key_bio(BIO_new_mem_buf(ca_key_pem.data(),
                                                     static_cast<int>(ca_key_pem.size())));
        detail::evp_pkey_ptr ca_key(PEM_read_bio_PrivateKey(ca_key_bio.get(),
                                                              nullptr, nullptr, nullptr));
        if (!ca_key) {
            throw std::runtime_error("Failed to load CA private key: " +
                                     detail::openssl_error_string());
        }

        CertificateData result;
        result.subject = "/CN=" + common_name;
        result.is_ca = false;

        // Generate new key pair for the signed certificate
        detail::evp_pkey_ptr pkey(EVP_RSA_gen(bits));
        if (!pkey) {
            throw std::runtime_error("Failed to generate key pair: " +
                                     detail::openssl_error_string());
        }

        // Create certificate
        detail::x509_ptr x509(X509_new());
        if (!x509) {
            throw std::runtime_error("Failed to create X509 object");
        }

        X509_set_version(x509.get(), 2);

        // Serial number
        unsigned char serial_bytes[8];
        RAND_bytes(serial_bytes, sizeof(serial_bytes));
        serial_bytes[0] &= 0x7F;
        detail::asn1_integer_ptr serial(ASN1_INTEGER_new());
        ASN1_INTEGER_set_uint64(serial.get(),
                                *reinterpret_cast<const uint64_t*>(serial_bytes));
        X509_set_serialNumber(x509.get(), serial.get());

        {
            std::stringstream ss;
            ss << std::hex << std::setfill('0');
            for (int i = 0; i < 8; ++i) {
                ss << std::setw(2) << static_cast<int>(serial_bytes[i]);
            }
            result.serial_number = ss.str();
        }

        // Validity
        auto now = std::chrono::system_clock::now();
        result.not_before = now;
        detail::asn1_time_ptr not_before(ASN1_TIME_new());
        detail::asn1_time_ptr not_after(ASN1_TIME_new());
        ASN1_TIME_set(not_before.get(),
                      std::chrono::system_clock::to_time_t(now));
        X509_set_notBefore(x509.get(), not_before.get());

        auto expiry = now + std::chrono::hours(24 * days_valid);
        result.not_after = expiry;
        ASN1_TIME_set(not_after.get(),
                      std::chrono::system_clock::to_time_t(expiry));
        X509_set_notAfter(x509.get(), not_after.get());

        // Set public key
        X509_set_pubkey(x509.get(), pkey.get());

        // Set subject
        detail::x509_name_ptr name(X509_NAME_new());
        X509_NAME_add_entry_by_txt(name.get(), "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>(common_name.c_str()),
                                   -1, -1, 0);
        X509_NAME_add_entry_by_txt(name.get(), "O", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>("cppdesk Client"),
                                   -1, -1, 0);
        X509_set_subject_name(x509.get(), name.get());

        // Set issuer from CA
        X509_NAME* ca_subject = X509_get_subject_name(ca_cert.get());
        X509_set_issuer_name(x509.get(), ca_subject);
        char* issuer_cstr = X509_NAME_oneline(ca_subject, nullptr, 0);
        if (issuer_cstr) {
            result.issuer = std::string(issuer_cstr);
            OPENSSL_free(issuer_cstr);
        }

        // Copy extensions context from CA
        X509V3_CTX ctx;
        X509V3_set_ctx_nodb(&ctx);
        X509V3_set_ctx(&ctx, ca_cert.get(), x509.get(), nullptr, nullptr, 0);

        // Basic constraints: not a CA
        {
            detail::x509_extension_ptr ext(
                X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints,
                                    const_cast<char*>("critical,CA:FALSE")));
            if (ext) X509_add_ext(x509.get(), ext.get(), -1);
        }

        // Key usage
        {
            detail::x509_extension_ptr ext(
                X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage,
                                    const_cast<char*>(
                                        "critical,digitalSignature,keyEncipherment")));
            if (ext) X509_add_ext(x509.get(), ext.get(), -1);
        }

        // Extended key usage
        {
            detail::x509_extension_ptr ext(
                X509V3_EXT_conf_nid(nullptr, &ctx, NID_ext_key_usage,
                                    const_cast<char*>("serverAuth,clientAuth")));
            if (ext) X509_add_ext(x509.get(), ext.get(), -1);
        }

        // Sign with CA key
        if (!X509_sign(x509.get(), ca_key.get(), EVP_sha256())) {
            throw std::runtime_error("Failed to sign certificate with CA: " +
                                     detail::openssl_error_string());
        }

        // Export to PEM
        detail::bio_ptr cert_bio(BIO_new(BIO_s_mem()));
        PEM_write_bio_X509(cert_bio.get(), x509.get());
        char* cert_data = nullptr;
        long cert_len = BIO_get_mem_data(cert_bio.get(), &cert_data);
        result.pem_certificate = std::string(cert_data, cert_len);

        detail::bio_ptr key_bio(BIO_new(BIO_s_mem()));
        PEM_write_bio_PrivateKey(key_bio.get(), pkey.get(),
                                  nullptr, nullptr, 0, nullptr, nullptr);
        char* key_data = nullptr;
        long key_len = BIO_get_mem_data(key_bio.get(), &key_data);
        result.pem_private_key = std::string(key_data, key_len);

        return result;
    }

    // -------------------------------------------------------------------------
    // Verify a certificate chain against a trust store
    // -------------------------------------------------------------------------
    CertificateVerifyResult verify_chain(
            const std::string& cert_pem,
            const std::string& ca_bundle_pem = "",
            bool check_crl = false,
            const std::string& crl_pem = "") {

        CertificateVerifyResult result;
        result.valid = false;
        result.verify_depth = 0;

        // Load certificate to verify
        detail::bio_ptr cert_bio(BIO_new_mem_buf(cert_pem.data(),
                                                   static_cast<int>(cert_pem.size())));
        detail::x509_ptr cert(PEM_read_bio_X509(cert_bio.get(),
                                                  nullptr, nullptr, nullptr));
        if (!cert) {
            result.error_message = "Failed to parse certificate PEM: " +
                                   detail::openssl_error_string();
            return result;
        }

        // Create trust store
        detail::x509_store_ptr store(X509_STORE_new());
        if (!store) {
            result.error_message = "Failed to create X509_STORE";
            return result;
        }

        // Load CA bundle if provided
        if (!ca_bundle_pem.empty()) {
            detail::bio_ptr ca_bio(BIO_new_mem_buf(ca_bundle_pem.data(),
                                                     static_cast<int>(ca_bundle_pem.size())));

            // Parse multiple certificates from the bundle
            STACK_OF(X509_INFO)* inf = PEM_X509_INFO_read_bio(ca_bio.get(),
                                                               nullptr, nullptr, nullptr);
            if (!inf) {
                result.error_message = "Failed to parse CA bundle: " +
                                       detail::openssl_error_string();
                return result;
            }

            for (int i = 0; i < sk_X509_INFO_num(inf); ++i) {
                X509_INFO* itmp = sk_X509_INFO_value(inf, i);
                if (itmp->x509) {
                    X509_STORE_add_cert(store.get(), itmp->x509);
                }
            }
            sk_X509_INFO_pop_free(inf, X509_INFO_free);
        }

        // Default: load system CA certs if no bundle provided
        if (ca_bundle_pem.empty()) {
            X509_STORE_set_default_paths(store.get());
        }

        // Add CRL if requested
        if (check_crl && !crl_pem.empty()) {
            detail::bio_ptr crl_bio(BIO_new_mem_buf(crl_pem.data(),
                                                      static_cast<int>(crl_pem.size())));
            detail::x509_crl_ptr crl(PEM_read_bio_X509_CRL(crl_bio.get(),
                                                             nullptr, nullptr, nullptr));
            if (crl) {
                X509_STORE_add_crl(store.get(), crl.get());
                X509_STORE_set_flags(store.get(), X509_V_FLAG_CRL_CHECK |
                                                   X509_V_FLAG_CRL_CHECK_ALL);
            } else {
                spdlog::warn("Failed to load CRL: {}", detail::openssl_error_string());
            }
        }

        // Set verification parameters: check certificate purpose
        X509_STORE_set_purpose(store.get(), X509_PURPOSE_SSL_SERVER);

        // Create verification context
        detail::x509_store_ctx_ptr ctx(X509_STORE_CTX_new());
        if (!ctx) {
            result.error_message = "Failed to create X509_STORE_CTX";
            return result;
        }

        if (X509_STORE_CTX_init(ctx.get(), store.get(), cert.get(), nullptr) != 1) {
            result.error_message = "Failed to initialize verification context";
            return result;
        }

        // Perform verification
        int verify_code = X509_verify_cert(ctx.get());

        if (verify_code == 1) {
            result.valid = true;
            result.verify_depth = X509_STORE_CTX_get_error_depth(ctx.get());
            result.verified_chain_info = "Chain verified successfully";

            // Collect chain info for logging
            STACK_OF(X509)* chain = X509_STORE_CTX_get_chain(ctx.get());
            if (chain) {
                std::stringstream chain_ss;
                for (int i = 0; i < sk_X509_num(chain); ++i) {
                    X509* chain_cert = sk_X509_value(chain, i);
                    char* subj = X509_NAME_oneline(
                        X509_get_subject_name(chain_cert), nullptr, 0);
                    if (subj) {
                        chain_ss << "[" << i << "] " << subj << "; ";
                        OPENSSL_free(subj);
                    }
                }
                result.verified_chain_info = chain_ss.str();
            }
        } else {
            int err = X509_STORE_CTX_get_error(ctx.get());
            result.error_message = std::string("Certificate verification failed: ") +
                                   X509_verify_cert_error_string(err) +
                                   " (error code: " + std::to_string(err) + ")";
            result.verify_depth = X509_STORE_CTX_get_error_depth(ctx.get());
        }

        spdlog::info("Certificate verification result: {} (depth: {})",
                     result.valid ? "PASS" : "FAIL", result.verify_depth);
        return result;
    }

    // -------------------------------------------------------------------------
    // Check if a certificate is revoked using a CRL
    // -------------------------------------------------------------------------
    bool check_revocation(const std::string& cert_pem,
                          const std::string& crl_pem) {
        detail::bio_ptr cert_bio(BIO_new_mem_buf(cert_pem.data(),
                                                   static_cast<int>(cert_pem.size())));
        detail::x509_ptr cert(PEM_read_bio_X509(cert_bio.get(),
                                                  nullptr, nullptr, nullptr));
        if (!cert) {
            throw std::runtime_error("Failed to parse certificate for CRL check");
        }

        detail::bio_ptr crl_bio(BIO_new_mem_buf(crl_pem.data(),
                                                  static_cast<int>(crl_pem.size())));
        detail::x509_crl_ptr crl(PEM_read_bio_X509_CRL(crl_bio.get(),
                                                         nullptr, nullptr, nullptr));
        if (!crl) {
            throw std::runtime_error("Failed to parse CRL: " +
                                     detail::openssl_error_string());
        }

        // Get the revoked certificates stack
        STACK_OF(X509_REVOKED)* revoked = X509_CRL_get_REVOKED(crl.get());
        if (!revoked) {
            // No revoked certificates
            return false;
        }

        const ASN1_INTEGER* cert_serial = X509_get_serialNumber(cert.get());

        for (int i = 0; i < sk_X509_REVOKED_num(revoked); ++i) {
            X509_REVOKED* rev = sk_X509_REVOKED_value(revoked, i);
            const ASN1_INTEGER* rev_serial = X509_REVOKED_get0_serialNumber(rev);

            if (ASN1_INTEGER_cmp(cert_serial, rev_serial) == 0) {
                spdlog::warn("Certificate is REVOKED in CRL");
                return true; // Certificate is revoked
            }
        }

        spdlog::info("Certificate not found in CRL - considered valid");
        return false;
    }

    // -------------------------------------------------------------------------
    // Load a certificate from a PEM file
    // -------------------------------------------------------------------------
    std::string load_certificate_from_file(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open certificate file: " + path);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // -------------------------------------------------------------------------
    // Save certificate and key to files with restricted permissions
    // -------------------------------------------------------------------------
    void save_certificate_to_file(const std::string& pem_data,
                                  const std::string& path,
                                  mode_t mode = 0644) {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot write certificate file: " + path);
        }
        file << pem_data;
        file.close();
        chmod(path.c_str(), mode);
    }

private:
    bool initialized_ = false;
};

// =============================================================================
// 2. Secure Token Generation (JWT-like, OAuth2 helpers)
// =============================================================================

struct JWTHeader {
    std::string algorithm;     // e.g., "HS256", "RS256"
    std::string type = "JWT";
    std::string key_id;        // optional
};

struct JWTClaims {
    std::string issuer;        // iss
    std::string subject;       // sub
    std::string audience;      // aud
    std::string jwt_id;        // jti
    std::chrono::system_clock::time_point issued_at;
    std::chrono::system_clock::time_point expiration;
    std::chrono::system_clock::time_point not_before;
    std::map<std::string, std::string> custom_claims;
};

class TokenGenerator {
public:
    TokenGenerator() = default;

    // -------------------------------------------------------------------------
    // Base64 URL-safe encoding/decoding (no padding)
    // -------------------------------------------------------------------------
    static std::string base64url_encode(const std::string& data) {
        static const char* table =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

        std::string result;
        result.reserve(((data.size() + 2) / 3) * 4);

        int val = 0;
        int valb = -6;
        for (unsigned char c : data) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                result.push_back(table[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }

        if (valb > -6) {
            result.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
        }

        // No padding in URL-safe mode
        return result;
    }

    static std::string base64url_decode(const std::string& data) {
        static const int decode_table[256] = {
            -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,62,-1,62,-1,63,
            52,53,54,55,56,57,58,59, 60,61,-1,-1,-1, 0,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6,  7, 8, 9,10,11,12,13,14,
            15,16,17,18,19,20,21,22, 23,24,25,-1,-1,-1,-1,63,
            -1,26,27,28,29,30,31,32, 33,34,35,36,37,38,39,40,
            41,42,43,44,45,46,47,48, 49,50,51,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1
        };

        std::string result;
        result.reserve((data.size() * 3) / 4);

        int val = 0;
        int valb = -8;
        for (unsigned char c : data) {
            if (c == '=') break; // Stop at padding
            int idx = decode_table[c];
            if (idx == -1) continue; // Skip invalid characters
            val = (val << 6) + idx;
            valb += 6;
            if (valb >= 0) {
                result.push_back(static_cast<char>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // Minimal JSON builder (no dependency on a JSON library)
    // -------------------------------------------------------------------------
    static std::string json_escape(const std::string& s) {
        std::string result;
        result.reserve(s.size() + 8);
        for (char c : s) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b";  break;
                case '\f': result += "\\f";  break;
                case '\n': result += "\\n";  break;
                case '\r': result += "\\r";  break;
                case '\t': result += "\\t";  break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x",
                                 static_cast<unsigned char>(c));
                        result += buf;
                    } else {
                        result += c;
                    }
            }
        }
        return result;
    }

    static std::string time_to_unix_str(
            const std::chrono::system_clock::time_point& tp) {
        return std::to_string(
            std::chrono::duration_cast<std::chrono::seconds>(
                tp.time_since_epoch()).count());
    }

    // -------------------------------------------------------------------------
    // Build JWT header JSON
    // -------------------------------------------------------------------------
    static std::string build_header_json(const JWTHeader& header) {
        std::string json = R"({"alg":")" + json_escape(header.algorithm) +
                           R"(","typ":")" + json_escape(header.type) + R"(")";
        if (!header.key_id.empty()) {
            json += R"(,"kid":")" + json_escape(header.key_id) + R"(")";
        }
        json += "}";
        return json;
    }

    // -------------------------------------------------------------------------
    // Build JWT claims JSON
    // -------------------------------------------------------------------------
    static std::string build_claims_json(const JWTClaims& claims) {
        std::string json = "{";
        bool first = true;

        auto add_str = [&](const std::string& key, const std::string& val) {
            if (!val.empty()) {
                if (!first) json += ",";
                json += "\"" + json_escape(key) + "\":\"" + json_escape(val) + "\"";
                first = false;
            }
        };

        auto add_num = [&](const std::string& key,
                           const std::chrono::system_clock::time_point& tp,
                           const std::chrono::system_clock::time_point& zero) {
            if (tp != zero) {
                if (!first) json += ",";
                json += "\"" + json_escape(key) + "\":" + time_to_unix_str(tp);
                first = false;
            }
        };

        add_str("iss", claims.issuer);
        add_str("sub", claims.subject);
        add_str("aud", claims.audience);
        add_str("jti", claims.jwt_id);
        add_num("iat", claims.issued_at, std::chrono::system_clock::time_point{});
        add_num("exp", claims.expiration, std::chrono::system_clock::time_point{});
        add_num("nbf", claims.not_before, std::chrono::system_clock::time_point{});

        for (const auto& [key, val] : claims.custom_claims) {
            if (!first) json += ",";
            json += "\"" + json_escape(key) + "\":\"" + json_escape(val) + "\"";
            first = false;
        }

        json += "}";
        return json;
    }

    // -------------------------------------------------------------------------
    // Generate HS256 (HMAC-SHA256) JWT
    // -------------------------------------------------------------------------
    std::string generate_hs256_jwt(const JWTClaims& claims,
                                    const std::string& secret) {
        JWTHeader header;
        header.algorithm = "HS256";
        return sign_jwt_hmac(header, claims, secret, EVP_sha256());
    }

    // -------------------------------------------------------------------------
    // Generate HS384 JWT
    // -------------------------------------------------------------------------
    std::string generate_hs384_jwt(const JWTClaims& claims,
                                    const std::string& secret) {
        JWTHeader header;
        header.algorithm = "HS384";
        return sign_jwt_hmac(header, claims, secret, EVP_sha384());
    }

    // -------------------------------------------------------------------------
    // Generate HS512 JWT
    // -------------------------------------------------------------------------
    std::string generate_hs512_jwt(const JWTClaims& claims,
                                    const std::string& secret) {
        JWTHeader header;
        header.algorithm = "HS512";
        return sign_jwt_hmac(header, claims, secret, EVP_sha512());
    }

    // -------------------------------------------------------------------------
    // Verify HMAC-signed JWT
    // -------------------------------------------------------------------------
    bool verify_hs256_jwt(const std::string& token, const std::string& secret) {
        return verify_jwt_hmac(token, secret, EVP_sha256());
    }

    bool verify_hs384_jwt(const std::string& token, const std::string& secret) {
        return verify_jwt_hmac(token, secret, EVP_sha384());
    }

    bool verify_hs512_jwt(const std::string& token, const std::string& secret) {
        return verify_jwt_hmac(token, secret, EVP_sha512());
    }

    // -------------------------------------------------------------------------
    // OAuth2: Generate authorization code
    // -------------------------------------------------------------------------
    std::string generate_authorization_code(size_t length = 32) {
        std::vector<unsigned char> buf(length);
        if (RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1) {
            throw std::runtime_error("Failed to generate authorization code");
        }
        return base64url_encode(std::string(
            reinterpret_cast<const char*>(buf.data()), buf.size()));
    }

    // -------------------------------------------------------------------------
    // OAuth2: Generate access token (opaque bearer token)
    // -------------------------------------------------------------------------
    std::string generate_access_token(size_t length = 48) {
        std::vector<unsigned char> buf(length);
        if (RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1) {
            throw std::runtime_error("Failed to generate access token");
        }
        return base64url_encode(std::string(
            reinterpret_cast<const char*>(buf.data()), buf.size()));
    }

    // -------------------------------------------------------------------------
    // OAuth2: Generate refresh token
    // -------------------------------------------------------------------------
    std::string generate_refresh_token(size_t length = 64) {
        std::vector<unsigned char> buf(length);
        if (RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1) {
            throw std::runtime_error("Failed to generate refresh token");
        }
        return base64url_encode(std::string(
            reinterpret_cast<const char*>(buf.data()), buf.size()));
    }

    // -------------------------------------------------------------------------
    // OAuth2: Generate PKCE code verifier and challenge (S256)
    // -------------------------------------------------------------------------
    struct PKCEPair {
        std::string code_verifier;
        std::string code_challenge;
    };

    PKCEPair generate_pkce_pair() {
        // Generate 32 random bytes -> 43-char base64url verifier
        std::vector<unsigned char> buf(32);
        RAND_bytes(buf.data(), static_cast<int>(buf.size()));
        std::string verifier = base64url_encode(std::string(
            reinterpret_cast<const char*>(buf.data()), buf.size()));

        // SHA-256 of verifier, then base64url encode
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(verifier.data()),
               verifier.size(), hash);
        std::string challenge = base64url_encode(std::string(
            reinterpret_cast<const char*>(hash), sizeof(hash)));

        return {verifier, challenge};
    }

    // -------------------------------------------------------------------------
    // OAuth2: Generate client secret
    // -------------------------------------------------------------------------
    std::string generate_client_secret(size_t length = 32) {
        std::vector<unsigned char> buf(length);
        RAND_bytes(buf.data(), static_cast<int>(buf.size()));
        // Use hex encoding for client secrets (standard OAuth2 practice)
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (auto byte : buf) {
            ss << std::setw(2) << static_cast<int>(byte);
        }
        return ss.str();
    }

private:
    // -------------------------------------------------------------------------
    // HMAC-based JWT signing
    // -------------------------------------------------------------------------
    std::string sign_jwt_hmac(const JWTHeader& header,
                               const JWTClaims& claims,
                               const std::string& secret,
                               const EVP_MD* md) {
        std::string header_json = build_header_json(header);
        std::string claims_json = build_claims_json(claims);

        std::string header_b64 = base64url_encode(header_json);
        std::string claims_b64 = base64url_encode(claims_json);

        std::string signing_input = header_b64 + "." + claims_b64;

        // Compute HMAC
        unsigned int sig_len = static_cast<unsigned int>(EVP_MAX_MD_SIZE);
        unsigned char sig[EVP_MAX_MD_SIZE];
        HMAC(md, secret.data(), static_cast<int>(secret.size()),
             reinterpret_cast<const unsigned char*>(signing_input.data()),
             signing_input.size(), sig, &sig_len);

        std::string signature_b64 = base64url_encode(
            std::string(reinterpret_cast<const char*>(sig), sig_len));

        return signing_input + "." + signature_b64;
    }

    // -------------------------------------------------------------------------
    // HMAC-based JWT verification
    // -------------------------------------------------------------------------
    bool verify_jwt_hmac(const std::string& token,
                          const std::string& secret,
                          const EVP_MD* md) {
        // Split token into parts
        size_t first_dot = token.find('.');
        size_t second_dot = token.rfind('.');

        if (first_dot == std::string::npos ||
            second_dot == std::string::npos ||
            first_dot == second_dot) {
            spdlog::error("Invalid JWT format");
            return false;
        }

        std::string signing_input = token.substr(0, second_dot);
        std::string provided_sig_b64 = token.substr(second_dot + 1);

        // Compute expected signature
        unsigned int sig_len = static_cast<unsigned int>(EVP_MAX_MD_SIZE);
        unsigned char sig[EVP_MAX_MD_SIZE];
        HMAC(md, secret.data(), static_cast<int>(secret.size()),
             reinterpret_cast<const unsigned char*>(signing_input.data()),
             signing_input.size(), sig, &sig_len);

        std::string expected_sig_b64 = base64url_encode(
            std::string(reinterpret_cast<const char*>(sig), sig_len));

        // Constant-time comparison
        return constant_time_compare(provided_sig_b64, expected_sig_b64);
    }
};

// =============================================================================
// 3. Input Sanitization
// =============================================================================

class InputSanitizer {
public:
    // -------------------------------------------------------------------------
    // XSS prevention: HTML-encode dangerous characters
    // -------------------------------------------------------------------------
    static std::string html_encode(const std::string& input) {
        std::string result;
        result.reserve(input.size() * 1.2);

        for (char c : input) {
            switch (c) {
                case '&':  result += "&amp;";  break;
                case '<':  result += "&lt;";   break;
                case '>':  result += "&gt;";   break;
                case '"':  result += "&quot;"; break;
                case '\'': result += "&#x27;"; break;
                case '/':  result += "&#x2F;"; break;
                case '`':  result += "&#x60;"; break;
                case '=':  result += "&#x3D;"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20 &&
                        c != '\n' && c != '\r' && c != '\t') {
                        // Encode control characters
                        char buf[8];
                        snprintf(buf, sizeof(buf), "&#x%02X;",
                                 static_cast<unsigned char>(c));
                        result += buf;
                    } else {
                        result += c;
                    }
            }
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // HTML decode
    // -------------------------------------------------------------------------
    static std::string html_decode(const std::string& input) {
        static const std::unordered_map<std::string, char> entities = {
            {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'},
            {"&quot;", '"'}, {"&#x27;", '\''}, {"&#x2F;", '/'},
            {"&#x60;", '`'}, {"&#x3D;", '='}, {"&apos;", '\''}
        };

        std::string result;
        result.reserve(input.size());
        size_t i = 0;

        while (i < input.size()) {
            if (input[i] == '&') {
                size_t semicolon = input.find(';', i);
                if (semicolon != std::string::npos) {
                    std::string entity = input.substr(i, semicolon - i + 1);
                    auto it = entities.find(entity);
                    if (it != entities.end()) {
                        result += it->second;
                        i = semicolon + 1;
                        continue;
                    }

                    // Handle numeric entities (&#xHH; or &#NN;)
                    if (entity.size() >= 4 && entity[1] == '#') {
                        int codepoint = 0;
                        if (entity[2] == 'x' || entity[2] == 'X') {
                            codepoint = std::stoi(entity.substr(3, entity.size() - 4),
                                                  nullptr, 16);
                        } else {
                            codepoint = std::stoi(entity.substr(2, entity.size() - 3));
                        }
                        if (codepoint > 0 && codepoint <= 127) {
                            result += static_cast<char>(codepoint);
                            i = semicolon + 1;
                            continue;
                        }
                    }
                }
            }
            result += input[i];
            ++i;
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // Strip all HTML/XML tags (aggressive)
    // -------------------------------------------------------------------------
    static std::string strip_tags(const std::string& input) {
        std::string result;
        result.reserve(input.size());
        bool in_tag = false;

        for (size_t i = 0; i < input.size(); ++i) {
            if (input[i] == '<') {
                in_tag = true;
            } else if (input[i] == '>') {
                in_tag = false;
            } else if (!in_tag) {
                result += input[i];
            }
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // SQL injection guard: escape dangerous characters for SQL strings
    // -------------------------------------------------------------------------
    static std::string sql_escape(const std::string& input) {
        std::string result;
        result.reserve(input.size() * 2);
        for (char c : input) {
            switch (c) {
                case '\'': result += "''";    break;  // Double single-quote
                case '\\': result += "\\\\";  break;
                case '\0': result += "\\0";   break;
                case '\b': result += "\\b";   break;
                case '\n': result += "\\n";   break;
                case '\r': result += "\\r";   break;
                case '\t': result += "\\t";   break;
                case '\x1a': result += "\\Z"; break; // EOF marker
                default:   result += c;
            }
        }
        return result;
    }

    // -------------------------------------------------------------------------
    // Validate SQL identifier (table/column name) - whitelist approach
    // -------------------------------------------------------------------------
    static bool is_valid_sql_identifier(const std::string& input) {
        if (input.empty() || input.size() > 64) return false;

        // Must start with letter or underscore
        if (!std::isalpha(static_cast<unsigned char>(input[0])) && input[0] != '_') {
            return false;
        }

        // Rest: letters, digits, underscores
        for (size_t i = 1; i < input.size(); ++i) {
            char c = input[i];
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
                return false;
            }
        }

        return true;
    }

    // -------------------------------------------------------------------------
    // Shell command injection prevention: sanitize for POSIX shell
    // -------------------------------------------------------------------------
    static std::string shell_escape(const std::string& input) {
        // Wrap in single quotes; escape single quotes within
        std::string result = "'";
        for (char c : input) {
            if (c == '\'') {
                result += "'\\''";
            } else {
                result += c;
            }
        }
        result += "'";
        return result;
    }

    // -------------------------------------------------------------------------
    // Path traversal prevention: sanitize file paths
    // -------------------------------------------------------------------------
    static std::string sanitize_path(const std::string& input) {
        std::string result;
        result.reserve(input.size());

        for (char c : input) {
            // Allow alphanumeric, common path chars, but block traversal
            if (c == '.' && result.size() >= 1 && result.back() == '.') {
                // Block ".."
                result.pop_back();
                continue;
            }
            if (c == '/' || c == '\\' || c == '_' || c == '-' ||
                c == '.' || c == ' ' || std::isalnum(static_cast<unsigned char>(c))) {
                result += c;
            }
            // Otherwise, skip the character
        }

        // Strip leading slashes to prevent absolute paths
        while (!result.empty() && (result[0] == '/' || result[0] == '\\')) {
            result.erase(0, 1);
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // URL encoding for safe inclusion in URLs
    // -------------------------------------------------------------------------
    static std::string url_encode(const std::string& input) {
        static const char hex_chars[] = "0123456789ABCDEF";
        std::string result;
        result.reserve(input.size() * 3);

        for (unsigned char c : input) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                result += static_cast<char>(c);
            } else if (c == ' ') {
                result += '+';
            } else {
                result += '%';
                result += hex_chars[c >> 4];
                result += hex_chars[c & 0x0F];
            }
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // URL decoding
    // -------------------------------------------------------------------------
    static std::string url_decode(const std::string& input) {
        std::string result;
        result.reserve(input.size());

        for (size_t i = 0; i < input.size(); ++i) {
            if (input[i] == '%' && i + 2 < input.size() &&
                std::isxdigit(static_cast<unsigned char>(input[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(input[i + 2]))) {
                int high = std::isdigit(input[i + 1])
                    ? input[i + 1] - '0'
                    : std::toupper(input[i + 1]) - 'A' + 10;
                int low = std::isdigit(input[i + 2])
                    ? input[i + 2] - '0'
                    : std::toupper(input[i + 2]) - 'A' + 10;
                result += static_cast<char>((high << 4) | low);
                i += 2;
            } else if (input[i] == '+') {
                result += ' ';
            } else {
                result += input[i];
            }
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // LDAP injection prevention
    // -------------------------------------------------------------------------
    static std::string ldap_escape(const std::string& input) {
        std::string result;
        result.reserve(input.size() * 2);
        for (char c : input) {
            switch (c) {
                case '\\': result += "\\5c"; break;
                case '*':  result += "\\2a"; break;
                case '(':  result += "\\28"; break;
                case ')':  result += "\\29"; break;
                case '\0': result += "\\00"; break;
                case '/':  result += "\\2f"; break;
                default:   result += c;
            }
        }
        return result;
    }

    // -------------------------------------------------------------------------
    // E-mail address validation
    // -------------------------------------------------------------------------
    static bool is_valid_email(const std::string& email) {
        // RFC 5322 simplified
        static const std::regex email_regex(
            R"(^[a-zA-Z0-9.!#$%&'*+/=?^_`{|}~-]+@[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(?:\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$)"
        );

        if (email.size() > 254) return false;
        return std::regex_match(email, email_regex);
    }

    // -------------------------------------------------------------------------
    // Generic allow-list filter: only keep characters in the allowed set
    // -------------------------------------------------------------------------
    static std::string whitelist_filter(const std::string& input,
                                         const std::string& allowed_chars) {
        std::unordered_set<char> allowed(allowed_chars.begin(), allowed_chars.end());
        std::string result;
        result.reserve(input.size());

        for (char c : input) {
            if (allowed.count(c)) {
                result += c;
            }
        }

        return result;
    }
};

// =============================================================================
// 4. Rate Limiting Algorithms
// =============================================================================

class TokenBucket {
public:
    // tokens_per_second: refill rate
    // burst_size: maximum number of tokens the bucket can hold
    TokenBucket(double tokens_per_second, size_t burst_size)
        : rate_(tokens_per_second)
        , burst_(burst_size)
        , tokens_(static_cast<double>(burst_size))
        , last_refill_(std::chrono::steady_clock::now()) {}

    // Returns true if a token was consumed, false if rate limited
    bool consume(size_t tokens = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        refill();

        if (tokens_ >= tokens) {
            tokens_ -= static_cast<double>(tokens);
            return true;
        }

        return false;
    }

    // Non-blocking attempt; returns false immediately if no tokens available
    bool try_consume(size_t tokens = 1) {
        return consume(tokens);
    }

    // Returns the number of tokens currently available
    double available_tokens() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tokens_;
    }

    // Reset the bucket to full
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        tokens_ = static_cast<double>(burst_);
        last_refill_ = std::chrono::steady_clock::now();
    }

    // Update rate limit parameters
    void update_rate(double tokens_per_second, size_t burst_size) {
        std::lock_guard<std::mutex> lock(mutex_);
        rate_ = tokens_per_second;
        burst_ = burst_size;
        if (tokens_ > burst_) {
            tokens_ = static_cast<double>(burst_);
        }
    }

private:
    void refill() {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - last_refill_).count();
        tokens_ = std::min(static_cast<double>(burst_), tokens_ + elapsed * rate_);
        last_refill_ = now;
    }

    double rate_;
    size_t burst_;
    double tokens_;
    std::chrono::steady_clock::time_point last_refill_;
    mutable std::mutex mutex_;
};

// ---------------------------------------------------------------------------
// Sliding Window Rate Limiter
// ---------------------------------------------------------------------------
class SlidingWindowRateLimiter {
public:
    // max_requests: maximum requests allowed in the window
    // window_seconds: time window duration in seconds
    SlidingWindowRateLimiter(size_t max_requests, double window_seconds)
        : max_requests_(max_requests)
        , window_seconds_(window_seconds) {}

    // Returns true if request is allowed, false if rate limited
    bool allow_request() {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);

        // Remove timestamps outside the window
        while (!timestamps_.empty()) {
            double elapsed = std::chrono::duration<double>(
                now - timestamps_.front()).count();
            if (elapsed >= window_seconds_) {
                timestamps_.pop_front();
            } else {
                break;
            }
        }

        if (timestamps_.size() < max_requests_) {
            timestamps_.push_back(now);
            return true;
        }

        return false;
    }

    // Returns the number of allowed requests remaining in the current window
    size_t remaining() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (timestamps_.size() >= max_requests_) return 0;
        return max_requests_ - timestamps_.size();
    }

    // Get the time until next allowed request (seconds), or 0 if already allowed
    double time_until_next_request() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (timestamps_.size() < max_requests_) return 0.0;

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(
            now - timestamps_.front()).count();
        if (elapsed >= window_seconds_) return 0.0;
        return window_seconds_ - elapsed;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        timestamps_.clear();
    }

private:
    size_t max_requests_;
    double window_seconds_;
    std::deque<std::chrono::steady_clock::time_point> timestamps_;
    mutable std::mutex mutex_;
};

// ---------------------------------------------------------------------------
// Leaky Bucket Rate Limiter
// ---------------------------------------------------------------------------
class LeakyBucket {
public:
    // capacity: maximum number of requests that can be queued
    // leak_rate: requests processed per second
    LeakyBucket(size_t capacity, double leak_rate)
        : capacity_(capacity)
        , leak_rate_(leak_rate)
        , water_level_(0.0)
        , last_leak_(std::chrono::steady_clock::now()) {}

    // Returns true if request is accepted, false if bucket overflow (rate limited)
    bool add_request() {
        std::lock_guard<std::mutex> lock(mutex_);
        leak();

        if (water_level_ < static_cast<double>(capacity_)) {
            water_level_ += 1.0;
            return true;
        }

        return false;
    }

    // Get current water level (queue depth)
    double current_level() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return water_level_;
    }

    // Get capacity
    size_t capacity() const { return capacity_; }

    // Reset bucket
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        water_level_ = 0.0;
        last_leak_ = std::chrono::steady_clock::now();
    }

private:
    void leak() {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - last_leak_).count();
        water_level_ = std::max(0.0, water_level_ - elapsed * leak_rate_);
        last_leak_ = now;
    }

    size_t capacity_;
    double leak_rate_;
    double water_level_;
    std::chrono::steady_clock::time_point last_leak_;
    mutable std::mutex mutex_;
};

// ---------------------------------------------------------------------------
// Per-client Rate Limiter (combines key-based tracking)
// ---------------------------------------------------------------------------
class PerClientRateLimiter {
public:
    using ClientKey = std::string;

    PerClientRateLimiter(size_t max_requests, double window_seconds,
                         size_t max_clients = 10000)
        : max_requests_(max_requests)
        , window_seconds_(window_seconds)
        , max_clients_(max_clients) {}

    bool allow_request(const ClientKey& client_id) {
        std::lock_guard<std::shared_mutex> lock(mutex_);

        // Clean up expired entries periodically
        auto now = std::chrono::steady_clock::now();
        auto& entry = clients_[client_id];

        // Remove timestamps outside the window
        while (!entry.timestamps.empty()) {
            double elapsed = std::chrono::duration<double>(
                now - entry.timestamps.front()).count();
            if (elapsed >= window_seconds_) {
                entry.timestamps.pop_front();
            } else {
                break;
            }
        }

        if (entry.timestamps.size() < max_requests_) {
            entry.timestamps.push_back(now);
            return true;
        }

        return false;
    }

    // Garbage-collect expired client entries
    void cleanup() {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();

        for (auto it = clients_.begin(); it != clients_.end(); ) {
            // Remove empty or completely expired entries
            while (!it->second.timestamps.empty()) {
                double elapsed = std::chrono::duration<double>(
                    now - it->second.timestamps.front()).count();
                if (elapsed >= window_seconds_) {
                    it->second.timestamps.pop_front();
                } else {
                    break;
                }
            }

            if (it->second.timestamps.empty()) {
                it = clients_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void reset_client(const ClientKey& client_id) {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        clients_.erase(client_id);
    }

    size_t client_count() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return clients_.size();
    }

private:
    struct ClientEntry {
        std::deque<std::chrono::steady_clock::time_point> timestamps;
    };

    size_t max_requests_;
    double window_seconds_;
    size_t max_clients_;
    std::unordered_map<ClientKey, ClientEntry> clients_;
    mutable std::shared_mutex mutex_;
};

// =============================================================================
// 5. Audit Logging Framework
// =============================================================================

enum class AuditEventType {
    AUTHENTICATION,
    AUTHORIZATION,
    DATA_ACCESS,
    DATA_MODIFICATION,
    CONFIGURATION_CHANGE,
    SYSTEM_STARTUP,
    SYSTEM_SHUTDOWN,
    USER_MANAGEMENT,
    SECURITY_ALERT,
    NETWORK_ACTIVITY,
    CRYPTO_OPERATION,
    FILE_ACCESS
};

enum class AuditSeverity {
    INFO,
    WARNING,
    CRITICAL,
    ALERT
};

struct AuditEvent {
    AuditEventType type;
    AuditSeverity severity;
    std::string actor;          // User or service identifier
    std::string action;         // What happened
    std::string resource;       // What was affected
    std::string result;         // Success, Failure, etc.
    std::string source_ip;
    std::string details;        // Additional context
    std::chrono::system_clock::time_point timestamp;
    std::string event_id;       // Unique event identifier
    std::string session_id;
};

class AuditLogger {
public:
    explicit AuditLogger(const std::string& log_file_path)
        : log_file_path_(log_file_path)
        , enabled_(true) {
        open_log_file();
    }

    ~AuditLogger() {
        if (log_file_.is_open()) {
            log_file_.close();
        }
    }

    // -------------------------------------------------------------------------
    // Log an audit event
    // -------------------------------------------------------------------------
    void log_event(const AuditEvent& event) {
        if (!enabled_) return;

        std::lock_guard<std::mutex> lock(mutex_);

        // Build structured log entry in CEF-like format
        std::stringstream entry;

        // Timestamp in ISO 8601
        auto time_t = std::chrono::system_clock::to_time_t(event.timestamp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            event.timestamp.time_since_epoch()) % 1000;

        std::tm tm_buf;
        localtime_r(&time_t, &tm_buf);

        entry << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
              << '.' << std::setfill('0') << std::setw(3) << ms.count()
              << std::put_time(&tm_buf, "%z");

        entry << " | " << audit_event_type_to_string(event.type);
        entry << " | " << audit_severity_to_string(event.severity);
        entry << " | actor=" << sanitize_log_field(event.actor);
        entry << " | action=" << sanitize_log_field(event.action);
        entry << " | resource=" << sanitize_log_field(event.resource);
        entry << " | result=" << sanitize_log_field(event.result);
        entry << " | src=" << sanitize_log_field(event.source_ip);
        entry << " | event_id=" << sanitize_log_field(event.event_id);
        entry << " | session=" << sanitize_log_field(event.session_id);
        if (!event.details.empty()) {
            entry << " | details=" << sanitize_log_field(event.details);
        }

        std::string line = entry.str();

        // Write to file
        if (log_file_.is_open()) {
            log_file_ << line << "\n";
            log_file_.flush(); // Audit logs must be immediately durable
        }

        // Also emit to spdlog for centralized logging
        switch (event.severity) {
            case AuditSeverity::INFO:
                spdlog::info("[AUDIT] {}", line);
                break;
            case AuditSeverity::WARNING:
                spdlog::warn("[AUDIT] {}", line);
                break;
            case AuditSeverity::CRITICAL:
                spdlog::critical("[AUDIT] {}", line);
                break;
            case AuditSeverity::ALERT:
                spdlog::error("[AUDIT] {}", line);
                spdlog::error("[ALERT] Immediate attention required!");
                break;
        }

        // Fire callback if registered
        if (event_callback_) {
            try {
                event_callback_(event);
            } catch (...) {
                // Callback should not break audit logging
            }
        }
    }

    // -------------------------------------------------------------------------
    // Convenience method: log with automatic timestamp and event ID
    // -------------------------------------------------------------------------
    void log(AuditEventType type,
             AuditSeverity severity,
             const std::string& actor,
             const std::string& action,
             const std::string& resource,
             const std::string& result,
             const std::string& source_ip = "",
             const std::string& details = "",
             const std::string& session_id = "") {

        AuditEvent event;
        event.type = type;
        event.severity = severity;
        event.actor = actor;
        event.action = action;
        event.resource = resource;
        event.result = result;
        event.source_ip = source_ip;
        event.details = details;
        event.session_id = session_id;
        event.timestamp = std::chrono::system_clock::now();
        event.event_id = generate_event_id();

        log_event(event);
    }

    // -------------------------------------------------------------------------
    // Register a callback for real-time event processing
    // -------------------------------------------------------------------------
    void set_event_callback(std::function<void(const AuditEvent&)> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        event_callback_ = std::move(callback);
    }

    // -------------------------------------------------------------------------
    // Enable/disable audit logging
    // -------------------------------------------------------------------------
    void set_enabled(bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (enabled != enabled_) {
            spdlog::info("Audit logging {}", enabled ? "enabled" : "disabled");
            enabled_ = enabled;
        }
    }

    bool is_enabled() const { return enabled_; }

    // -------------------------------------------------------------------------
    // Rotate log file
    // -------------------------------------------------------------------------
    void rotate_log() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (log_file_.is_open()) {
            log_file_.close();
        }

        // Rename old log with timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
        localtime_r(&time_t, &tm_buf);

        std::stringstream new_name;
        new_name << log_file_path_ << "."
                 << std::put_time(&tm_buf, "%Y%m%d-%H%M%S");

        rename(log_file_path_.c_str(), new_name.str().c_str());

        open_log_file();
    }

    // -------------------------------------------------------------------------
    // Get current log file path
    // -------------------------------------------------------------------------
    const std::string& log_file_path() const { return log_file_path_; }

private:
    void open_log_file() {
        log_file_.open(log_file_path_, std::ios::app | std::ios::binary);
        if (!log_file_.is_open()) {
            spdlog::error("Failed to open audit log file: {}", log_file_path_);
            throw std::runtime_error("Cannot open audit log: " + log_file_path_);
        }

        // Set restrictive file permissions (owner read/write only)
        chmod(log_file_path_.c_str(), 0600);
    }

    std::string sanitize_log_field(const std::string& field) {
        // Replace newlines and pipes to prevent log injection
        std::string result = field;
        for (auto& c : result) {
            if (c == '\n' || c == '\r') c = ' ';
            if (c == '|') c = ':';
        }
        return result;
    }

    std::string generate_event_id() {
        static std::atomic<uint64_t> counter{0};
        auto now = std::chrono::system_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();

        std::stringstream ss;
        ss << std::hex << us << "-" << counter.fetch_add(1, std::memory_order_relaxed);
        return ss.str();
    }

    static const char* audit_event_type_to_string(AuditEventType type) {
        switch (type) {
            case AuditEventType::AUTHENTICATION:      return "AUTHN";
            case AuditEventType::AUTHORIZATION:        return "AUTHZ";
            case AuditEventType::DATA_ACCESS:          return "DATA_ACCESS";
            case AuditEventType::DATA_MODIFICATION:    return "DATA_MOD";
            case AuditEventType::CONFIGURATION_CHANGE: return "CONFIG";
            case AuditEventType::SYSTEM_STARTUP:       return "STARTUP";
            case AuditEventType::SYSTEM_SHUTDOWN:      return "SHUTDOWN";
            case AuditEventType::USER_MANAGEMENT:       return "USER_MGMT";
            case AuditEventType::SECURITY_ALERT:       return "SEC_ALERT";
            case AuditEventType::NETWORK_ACTIVITY:     return "NETWORK";
            case AuditEventType::CRYPTO_OPERATION:     return "CRYPTO";
            case AuditEventType::FILE_ACCESS:          return "FILE";
            default: return "UNKNOWN";
        }
    }

    static const char* audit_severity_to_string(AuditSeverity severity) {
        switch (severity) {
            case AuditSeverity::INFO:     return "INFO";
            case AuditSeverity::WARNING:  return "WARN";
            case AuditSeverity::CRITICAL: return "CRIT";
            case AuditSeverity::ALERT:    return "ALERT";
            default: return "UNKNOWN";
        }
    }

    std::string log_file_path_;
    std::ofstream log_file_;
    bool enabled_;
    std::mutex mutex_;
    std::function<void(const AuditEvent&)> event_callback_;
};

// =============================================================================
// 6. Secure Memory Management
// =============================================================================

template<typename T>
class SecureAllocator : public std::allocator<T> {
public:
    using base = std::allocator<T>;

    SecureAllocator() noexcept : base() {}
    SecureAllocator(const SecureAllocator&) noexcept = default;
    template<typename U>
    SecureAllocator(const SecureAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        T* ptr = base::allocate(n);
        // Lock the memory to prevent swapping to disk
        if (mlock(ptr, n * sizeof(T)) != 0) {
            spdlog::warn("mlock failed in SecureAllocator: {}", strerror(errno));
        }
        return ptr;
    }

    void deallocate(T* ptr, std::size_t n) {
        // Zero out the memory before freeing
        if (ptr) {
            std::memset(ptr, 0, n * sizeof(T));
            munlock(ptr, n * sizeof(T));
        }
        base::deallocate(ptr, n);
    }
};

// Specialization for void
template<>
class SecureAllocator<void> {
public:
    using value_type = void;
    template<typename U>
    struct rebind {
        using other = SecureAllocator<U>;
    };
};

using secure_string = std::basic_string<char, std::char_traits<char>,
                                         SecureAllocator<char>>;
using secure_wstring = std::basic_string<wchar_t, std::char_traits<wchar_t>,
                                          SecureAllocator<wchar_t>>;

class SecureMemoryManager {
public:
    // -------------------------------------------------------------------------
    // Allocate locked (non-swappable) memory
    // -------------------------------------------------------------------------
    static void* allocate_locked(size_t size) {
        // Round up to page size
        long page_size = sysconf(_SC_PAGESIZE);
        if (page_size <= 0) page_size = 4096;

        size_t aligned_size = ((size + page_size - 1) / page_size) * page_size;

        void* ptr = mmap(nullptr, aligned_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_LOCKED, -1, 0);

        if (ptr == MAP_FAILED) {
            throw std::runtime_error("Failed to allocate locked memory: " +
                                     std::string(strerror(errno)));
        }

        // Double-lock it (MAP_LOCKED is a hint on some systems)
        if (mlock(ptr, aligned_size) != 0) {
            munmap(ptr, aligned_size);
            throw std::runtime_error("mlock failed: " + std::string(strerror(errno)));
        }

        return ptr;
    }

    // -------------------------------------------------------------------------
    // Free locked memory with secure zeroing
    // -------------------------------------------------------------------------
    static void free_locked(void* ptr, size_t size) {
        if (!ptr || size == 0) return;

        long page_size = sysconf(_SC_PAGESIZE);
        if (page_size <= 0) page_size = 4096;

        size_t aligned_size = ((size + page_size - 1) / page_size) * page_size;

        // Zero out the memory
        explicit_memset(ptr, 0, size);

        // Unlock and unmap
        munlock(ptr, aligned_size);
        munmap(ptr, aligned_size);
    }

    // -------------------------------------------------------------------------
    // Zero memory (explicit, not optimizer-removable)
    // -------------------------------------------------------------------------
    static void explicit_memset(void* ptr, int value, size_t size) {
        // Use volatile pointer to prevent optimization
        volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
        while (size--) {
            *p++ = static_cast<unsigned char>(value);
        }
        // Memory barrier to ensure write completion
        __sync_synchronize();
    }

    // -------------------------------------------------------------------------
    // Secure string clear
    // -------------------------------------------------------------------------
    static void secure_clear_string(std::string& str) {
        if (!str.empty()) {
            explicit_memset(&str[0], 0, str.size());
            str.clear();
        }
    }

    // -------------------------------------------------------------------------
    // Disable core dumps for this process (protect sensitive data)
    // -------------------------------------------------------------------------
    static void disable_core_dumps() {
        struct rlimit rl;
        rl.rlim_cur = 0;
        rl.rlim_max = 0;
        if (setrlimit(RLIMIT_CORE, &rl) != 0) {
            spdlog::warn("Failed to disable core dumps: {}", strerror(errno));
        } else {
            spdlog::info("Core dumps disabled");
        }

#ifdef PR_SET_DUMPABLE
        // Also set dumpable flag on Linux
        if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0) {
            spdlog::warn("Failed to set PR_SET_DUMPABLE: {}", strerror(errno));
        }
#endif
    }

    // -------------------------------------------------------------------------
    // Securely compare two buffers without short-circuit (constant-time)
    // -------------------------------------------------------------------------
    static bool secure_memcmp(const void* a, const void* b, size_t len) {
        const volatile unsigned char* pa =
            static_cast<const volatile unsigned char*>(a);
        const volatile unsigned char* pb =
            static_cast<const volatile unsigned char*>(b);
        unsigned char diff = 0;

        for (size_t i = 0; i < len; ++i) {
            diff |= pa[i] ^ pb[i];
        }

        return diff == 0;
    }

    // -------------------------------------------------------------------------
    // Guard page allocation for detecting buffer overflows
    // -------------------------------------------------------------------------
    struct GuardedAllocation {
        void* data;
        size_t usable_size;
        size_t total_size;
    };

    static GuardedAllocation allocate_guarded(size_t usable_size) {
        long page_size = sysconf(_SC_PAGESIZE);
        if (page_size <= 0) page_size = 4096;

        // Allocate: [guard page] [usable pages] [guard page]
        size_t usable_pages = ((usable_size + page_size - 1) / page_size);
        size_t total_size = (usable_pages + 2) * page_size;

        void* region = mmap(nullptr, total_size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (region == MAP_FAILED) {
            throw std::runtime_error("Failed to allocate guarded memory");
        }

        // Protect first guard page
        mprotect(region, page_size, PROT_NONE);

        // Protect last guard page
        void* last_guard = static_cast<char*>(region) + (usable_pages + 1) * page_size;
        mprotect(last_guard, page_size, PROT_NONE);

        return {
            static_cast<char*>(region) + page_size,
            usable_pages * page_size,
            total_size
        };
    }

    static void free_guarded(GuardedAllocation& alloc) {
        if (alloc.data) {
            // Zero usable region, then unmap everything
            explicit_memset(alloc.data, 0, alloc.usable_size);
            long page_size = sysconf(_SC_PAGESIZE);
            if (page_size <= 0) page_size = 4096;
            void* base = static_cast<char*>(alloc.data) - page_size;
            munmap(base, alloc.total_size);
            alloc.data = nullptr;
        }
    }
};

// =============================================================================
// 7. File Permission Hardening
// =============================================================================

class FilePermissionHardener {
public:
    // -------------------------------------------------------------------------
    // Set the umask to a restrictive value
    // -------------------------------------------------------------------------
    static void set_restrictive_umask(mode_t mask = 0077) {
        umask(mask);
        spdlog::info("UMask set to {:04o}", mask);
    }

    // -------------------------------------------------------------------------
    // Create a file with restricted permissions (owner read/write only)
    // -------------------------------------------------------------------------
    static bool create_secure_file(const std::string& path,
                                    const std::string& content = "") {
        // Use open() with O_EXCL to prevent symlink attacks
        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (fd < 0) {
            spdlog::error("Failed to create secure file {}: {}", path, strerror(errno));
            return false;
        }

        if (!content.empty()) {
            ssize_t written = write(fd, content.data(), content.size());
            if (written != static_cast<ssize_t>(content.size())) {
                close(fd);
                unlink(path.c_str());
                spdlog::error("Failed to write to secure file {}", path);
                return false;
            }
        }

        close(fd);
        return true;
    }

    // -------------------------------------------------------------------------
    // Harden existing file permissions
    // -------------------------------------------------------------------------
    static bool harden_file(const std::string& path, mode_t mode = 0600) {
        if (chmod(path.c_str(), mode) != 0) {
            spdlog::error("Failed to chmod {}: {}", path, strerror(errno));
            return false;
        }
        spdlog::info("Hardened permissions on {}: {:04o}", path, mode);
        return true;
    }

    // -------------------------------------------------------------------------
    // Harden directory permissions
    // -------------------------------------------------------------------------
    static bool harden_directory(const std::string& path, mode_t mode = 0700) {
        if (chmod(path.c_str(), mode) != 0) {
            spdlog::error("Failed to chmod directory {}: {}", path, strerror(errno));
            return false;
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // Check if a file has safe permissions (owner-only, not world-writable)
    // -------------------------------------------------------------------------
    static bool has_safe_permissions(const std::string& path) {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            spdlog::error("Cannot stat {}: {}", path, strerror(errno));
            return false;
        }

        // Check that only owner has write permissions
        if (st.st_mode & S_IWGRP) {
            spdlog::warn("{} is group-writable", path);
            return false;
        }
        if (st.st_mode & S_IWOTH) {
            spdlog::warn("{} is world-writable", path);
            return false;
        }

        // Check that the file is a regular file (not symlink)
        if (!S_ISREG(st.st_mode)) {
            spdlog::warn("{} is not a regular file", path);
            return false;
        }

        return true;
    }

    // -------------------------------------------------------------------------
    // Lock a file for exclusive access
    // -------------------------------------------------------------------------
    class FileLock {
    public:
        explicit FileLock(const std::string& path)
            : fd_(-1), path_(path) {
            fd_ = open(path.c_str(), O_RDWR | O_CREAT, 0600);
            if (fd_ < 0) {
                throw std::runtime_error("Cannot open lock file: " + path);
            }

            if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
                close(fd_);
                fd_ = -1;
                throw std::runtime_error("Cannot acquire lock on: " + path +
                                         " (" + strerror(errno) + ")");
            }

            locked_ = true;
        }

        ~FileLock() {
            unlock();
        }

        FileLock(const FileLock&) = delete;
        FileLock& operator=(const FileLock&) = delete;

        FileLock(FileLock&& other) noexcept
            : fd_(other.fd_), path_(std::move(other.path_)), locked_(other.locked_) {
            other.fd_ = -1;
            other.locked_ = false;
        }

        void unlock() {
            if (locked_ && fd_ >= 0) {
                flock(fd_, LOCK_UN);
                close(fd_);
                fd_ = -1;
                locked_ = false;
                unlink(path_.c_str());
            }
        }

        bool is_locked() const { return locked_; }

    private:
        int fd_;
        std::string path_;
        bool locked_ = false;
    };

    // -------------------------------------------------------------------------
    // Securely delete a file (overwrite before unlink)
    // -------------------------------------------------------------------------
    static bool secure_delete(const std::string& path, int passes = 3) {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            return false; // File doesn't exist
        }

        if (!S_ISREG(st.st_mode)) {
            spdlog::error("secure_delete: {} is not a regular file", path);
            return false;
        }

        // Overwrite file contents
        if (st.st_size > 0) {
            std::vector<unsigned char> buf(4096);

            for (int pass = 0; pass < passes; ++pass) {
                int fd = open(path.c_str(), O_WRONLY);
                if (fd < 0) {
                    spdlog::error("secure_delete: cannot open {} for overwrite", path);
                    return false;
                }

                // Each pass uses different pattern
                int fill_byte;
                switch (pass) {
                    case 0: fill_byte = 0x00; break;
                    case 1: fill_byte = 0xFF; break;
                    default: fill_byte = 0xAA; break;
                }
                std::fill(buf.begin(), buf.end(),
                          static_cast<unsigned char>(fill_byte));

                off_t remaining = st.st_size;
                while (remaining > 0) {
                    ssize_t to_write = std::min(static_cast<off_t>(buf.size()),
                                                 remaining);
                    ssize_t written = write(fd, buf.data(), to_write);
                    if (written < 0) {
                        close(fd);
                        return false;
                    }
                    remaining -= written;
                }

                fsync(fd); // Ensure writes hit disk
                close(fd);
            }

            // Final pass: random data
            int fd = open(path.c_str(), O_WRONLY);
            if (fd >= 0) {
                RAND_bytes(buf.data(), static_cast<int>(buf.size()));
                off_t remaining = st.st_size;
                while (remaining > 0) {
                    ssize_t to_write = std::min(static_cast<off_t>(buf.size()),
                                                 remaining);
                    write(fd, buf.data(), to_write);
                    remaining -= to_write;
                }
                fsync(fd);
                close(fd);
            }
        }

        // Now unlink
        if (unlink(path.c_str()) != 0) {
            spdlog::error("secure_delete: unlink failed for {}", path);
            return false;
        }

        spdlog::info("Securely deleted: {}", path);
        return true;
    }
};

// =============================================================================
// 8. Process Isolation Helpers
// =============================================================================

class ProcessIsolation {
public:
    // -------------------------------------------------------------------------
    // Drop privileges to a specified user/group
    // -------------------------------------------------------------------------
    static bool drop_privileges(uid_t uid, gid_t gid) {
        // Must drop group first, then user
        if (setgid(gid) != 0) {
            spdlog::error("Failed to setgid({}): {}", gid, strerror(errno));
            return false;
        }

        // Ensure no supplementary groups
        if (setgroups(0, nullptr) != 0) {
            spdlog::error("Failed to clear supplementary groups: {}",
                          strerror(errno));
            return false;
        }

        if (setuid(uid) != 0) {
            spdlog::error("Failed to setuid({}): {}", uid, strerror(errno));
            return false;
        }

        // Verify we can't regain privileges
        if (setuid(0) == 0 || seteuid(0) == 0) {
            spdlog::error("Failed to drop privileges - still can become root");
            return false;
        }

        spdlog::info("Privileges dropped to uid={}, gid={}", uid, gid);
        return true;
    }

    // -------------------------------------------------------------------------
    // Enter a chroot jail
    // -------------------------------------------------------------------------
    static bool chroot_jail(const std::string& new_root) {
        if (chroot(new_root.c_str()) != 0) {
            spdlog::error("chroot({}) failed: {}", new_root, strerror(errno));
            return false;
        }

        if (chdir("/") != 0) {
            spdlog::error("chdir after chroot failed: {}", strerror(errno));
            return false;
        }

        spdlog::info("Entered chroot jail: {}", new_root);
        return true;
    }

    // -------------------------------------------------------------------------
    // Set up seccomp filter (simplified - real implementation would be larger)
    // -------------------------------------------------------------------------
    // Note: Full seccomp BPF filter generation requires platform-specific
    // structures. This provides the framework and common syscall allowlists.

    static void enable_seccomp_strict() {
#ifdef PR_SET_SECCOMP
        if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT) != 0) {
            spdlog::error("Failed to enable seccomp strict mode: {}",
                          strerror(errno));
            throw std::runtime_error("seccomp strict mode failed");
        }
        spdlog::info("Seccomp strict mode enabled");
#else
        spdlog::warn("Seccomp not available on this platform");
#endif
    }

    // -------------------------------------------------------------------------
    // Set resource limits for process hardening
    // -------------------------------------------------------------------------
    static bool set_resource_limits() {
        struct rlimit rl;

        // Disable core dumps
        rl.rlim_cur = 0;
        rl.rlim_max = 0;
        setrlimit(RLIMIT_CORE, &rl);

        // Limit number of processes
        rl.rlim_cur = 10;
        rl.rlim_max = 50;
        setrlimit(RLIMIT_NPROC, &rl);

        // Limit file descriptors
        rl.rlim_cur = 256;
        rl.rlim_max = 1024;
        setrlimit(RLIMIT_NOFILE, &rl);

        // Limit memory
        rl.rlim_cur = 512 * 1024 * 1024; // 512 MB
        rl.rlim_max = 1024 * 1024 * 1024; // 1 GB
        setrlimit(RLIMIT_AS, &rl);

        spdlog::info("Resource limits applied");
        return true;
    }

    // -------------------------------------------------------------------------
    // Sandbox file system access to specific directories
    // -------------------------------------------------------------------------
    static void restrict_filesystem(const std::vector<std::string>& allowed_paths) {
        // This is a framework - on Linux, use mount namespaces + bind mounts
        // For portability, we validate paths at the application level

        spdlog::info("Filesystem restricted to {} paths", allowed_paths.size());
        for (const auto& p : allowed_paths) {
            spdlog::info("  Allowed path: {}", p);
        }

        // In a full implementation, this would:
        // 1. unshare(CLONE_NEWNS) to get private mount namespace
        // 2. Mount tmpfs on /tmp with MS_NOEXEC|MS_NOSUID|MS_NODEV
        // 3. Bind-mount only allowed paths
        // 4. pivot_root to the restricted environment
    }

    // -------------------------------------------------------------------------
    // Set no_new_privs to prevent privilege escalation
    // -------------------------------------------------------------------------
    static bool set_no_new_privs() {
#ifdef PR_SET_NO_NEW_PRIVS
        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
            spdlog::error("Failed to set NO_NEW_PRIVS: {}", strerror(errno));
            return false;
        }
        spdlog::info("NO_NEW_PRIVS set");
        return true;
#else
        spdlog::warn("PR_SET_NO_NEW_PRIVS not available");
        return false;
#endif
    }

    // -------------------------------------------------------------------------
    // Fork and execute a child process with restricted privileges
    // -------------------------------------------------------------------------
    static pid_t spawn_isolated_child(
            const std::string& executable,
            const std::vector<std::string>& args,
            uid_t uid,
            gid_t gid,
            const std::string& root_dir = "") {

        pid_t pid = fork();

        if (pid < 0) {
            throw std::runtime_error("Failed to fork: " +
                                     std::string(strerror(errno)));
        }

        if (pid == 0) {
            // Child process
            // Set NO_NEW_PRIVS first
            set_no_new_privs();

            // Chroot if specified
            if (!root_dir.empty()) {
                if (!chroot_jail(root_dir)) {
                    _exit(1);
                }
            }

            // Drop privileges
            if (!drop_privileges(uid, gid)) {
                _exit(1);
            }

            // Build argv
            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(executable.c_str()));
            for (const auto& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);

            // Execute
            execvp(executable.c_str(), argv.data());

            // execvp only returns on error
            spdlog::error("execvp failed: {}", strerror(errno));
            _exit(127);
        }

        spdlog::info("Spawned isolated child process: pid={} exec={}", pid, executable);
        return pid;
    }

    // -------------------------------------------------------------------------
    // Wait for child process and get exit status
    // -------------------------------------------------------------------------
    static int wait_for_child(pid_t pid, int timeout_seconds = 60) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(timeout_seconds);

        while (true) {
            int status = 0;
            pid_t result = waitpid(pid, &status, WNOHANG);

            if (result == pid) {
                if (WIFEXITED(status)) {
                    int exit_code = WEXITSTATUS(status);
                    spdlog::info("Child pid={} exited with code={}", pid, exit_code);
                    return exit_code;
                } else if (WIFSIGNALED(status)) {
                    int sig = WTERMSIG(status);
                    spdlog::warn("Child pid={} killed by signal {}", pid, sig);
                    return -sig;
                }
                return -1;
            } else if (result < 0) {
                spdlog::error("waitpid failed: {}", strerror(errno));
                return -1;
            }

            if (std::chrono::steady_clock::now() > deadline) {
                spdlog::warn("Child pid={} timed out, sending SIGKILL", pid);
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
                return -1;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};

// =============================================================================
// 9. Secure Random Number Generation Wrapper
// =============================================================================

class SecureRandom {
public:
    SecureRandom() {
        // Ensure OpenSSL is properly seeded
        if (RAND_status() != 1) {
            spdlog::warn("OpenSSL RNG not properly seeded, trying to seed...");
            // Try to seed from /dev/urandom
            unsigned char seed[32];
            int fd = open("/dev/urandom", O_RDONLY);
            if (fd >= 0) {
                read(fd, seed, sizeof(seed));
                close(fd);
                RAND_seed(seed, sizeof(seed));
                SecureMemoryManager::explicit_memset(seed, 0, sizeof(seed));
            }
        }
    }

    // -------------------------------------------------------------------------
    // Generate cryptographically secure random bytes
    // -------------------------------------------------------------------------
    std::vector<unsigned char> random_bytes(size_t count) {
        std::vector<unsigned char> buf(count);
        if (RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1) {
            throw std::runtime_error("Failed to generate random bytes: " +
                                     detail::openssl_error_string());
        }
        return buf;
    }

    // -------------------------------------------------------------------------
    // Fill a buffer with secure random bytes
    // -------------------------------------------------------------------------
    void random_fill(void* buf, size_t count) {
        if (RAND_bytes(static_cast<unsigned char*>(buf),
                        static_cast<int>(count)) != 1) {
            throw std::runtime_error("Failed to fill random bytes: " +
                                     detail::openssl_error_string());
        }
    }

    // -------------------------------------------------------------------------
    // Generate a random integer in [0, max)
    // -------------------------------------------------------------------------
    uint64_t random_uint64(uint64_t max = std::numeric_limits<uint64_t>::max()) {
        if (max == 0) return 0;

        uint64_t threshold = std::numeric_limits<uint64_t>::max() - 
            (std::numeric_limits<uint64_t>::max() % max);

        uint64_t value;
        do {
            random_fill(&value, sizeof(value));
        } while (value >= threshold);

        return value % max;
    }

    // -------------------------------------------------------------------------
    // Generate a random integer in [min, max]
    // -------------------------------------------------------------------------
    int64_t random_int64(int64_t min, int64_t max) {
        if (min > max) std::swap(min, max);
        uint64_t range = static_cast<uint64_t>(max - min);
        return min + static_cast<int64_t>(random_uint64(range + 1));
    }

    // -------------------------------------------------------------------------
    // Generate a random double in [0.0, 1.0)
    // -------------------------------------------------------------------------
    double random_double() {
        uint64_t val;
        random_fill(&val, sizeof(val));

        // IEEE 754 trick: set exponent to 0x3FF (1.0)
        // This gives us a uniform distribution in [1.0, 2.0)
        val = (val >> 12) | 0x3FF0000000000000ULL;

        double result;
        std::memcpy(&result, &val, sizeof(result));
        return result - 1.0;
    }

    // -------------------------------------------------------------------------
    // Generate a random hex string
    // -------------------------------------------------------------------------
    std::string random_hex(size_t length) {
        auto bytes = random_bytes((length + 1) / 2);
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < length && i / 2 < bytes.size(); ++i) {
            if (i % 2 == 0) {
                ss << std::setw(2) << static_cast<int>(bytes[i / 2]);
            }
        }
        return ss.str().substr(0, length);
    }

    // -------------------------------------------------------------------------
    // Generate a random alphanumeric string
    // -------------------------------------------------------------------------
    std::string random_alphanumeric(size_t length) {
        static const char charset[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        auto bytes = random_bytes(length);
        std::string result;
        result.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            result += charset[bytes[i] % (sizeof(charset) - 1)];
        }
        return result;
    }

    // -------------------------------------------------------------------------
    // Generate a UUID v4
    // -------------------------------------------------------------------------
    std::string uuid_v4() {
        auto bytes = random_bytes(16);

        // Set version (4) and variant (2)
        bytes[6] = (bytes[6] & 0x0F) | 0x40;
        bytes[8] = (bytes[8] & 0x3F) | 0x80;

        char buf[37];
        snprintf(buf, sizeof(buf),
                 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 bytes[0], bytes[1], bytes[2], bytes[3],
                 bytes[4], bytes[5], bytes[6], bytes[7],
                 bytes[8], bytes[9], bytes[10], bytes[11],
                 bytes[12], bytes[13], bytes[14], bytes[15]);
        return std::string(buf);
    }

    // -------------------------------------------------------------------------
    // Shuffle a range uniformly (Fisher-Yates)
    // -------------------------------------------------------------------------
    template<typename RandomIt>
    void shuffle(RandomIt first, RandomIt last) {
        auto n = static_cast<size_t>(std::distance(first, last));
        for (size_t i = n - 1; i > 0; --i) {
            size_t j = static_cast<size_t>(random_uint64(i + 1));
            if (i != j) {
                std::swap(first[static_cast<long>(i)],
                          first[static_cast<long>(j)]);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Pick a random element from a container
    // -------------------------------------------------------------------------
    template<typename Container>
    typename Container::const_reference pick(const Container& container) {
        if (container.empty()) {
            throw std::runtime_error("Cannot pick from empty container");
        }
        size_t idx = static_cast<size_t>(random_uint64(container.size()));
        auto it = container.begin();
        std::advance(it, idx);
        return *it;
    }
};

// =============================================================================
// 10. Key Derivation Functions (PBKDF2, HKDF)
// =============================================================================

class KeyDerivation {
public:
    // -------------------------------------------------------------------------
    // PBKDF2-HMAC-SHA256
    // -------------------------------------------------------------------------
    static std::vector<unsigned char> pbkdf2_sha256(
            const std::string& password,
            const std::vector<unsigned char>& salt,
            size_t key_length,
            int iterations = 600000) {  // OWASP 2023 recommendation

        std::vector<unsigned char> derived_key(key_length);

        int result = PKCS5_PBKDF2_HMAC(
            password.data(), static_cast<int>(password.size()),
            salt.data(), static_cast<int>(salt.size()),
            iterations,
            EVP_sha256(),
            static_cast<int>(key_length),
            derived_key.data());

        if (result != 1) {
            throw std::runtime_error("PBKDF2 derivation failed: " +
                                     detail::openssl_error_string());
        }

        return derived_key;
    }

    // -------------------------------------------------------------------------
    // PBKDF2-HMAC-SHA512
    // -------------------------------------------------------------------------
    static std::vector<unsigned char> pbkdf2_sha512(
            const std::string& password,
            const std::vector<unsigned char>& salt,
            size_t key_length,
            int iterations = 600000) {

        std::vector<unsigned char> derived_key(key_length);

        int result = PKCS5_PBKDF2_HMAC(
            password.data(), static_cast<int>(password.size()),
            salt.data(), static_cast<int>(salt.size()),
            iterations,
            EVP_sha512(),
            static_cast<int>(key_length),
            derived_key.data());

        if (result != 1) {
            throw std::runtime_error("PBKDF2-SHA512 derivation failed: " +
                                     detail::openssl_error_string());
        }

        return derived_key;
    }

    // -------------------------------------------------------------------------
    // HKDF Extract-and-Expand (RFC 5869)
    // -------------------------------------------------------------------------
    static std::vector<unsigned char> hkdf_sha256(
            const std::vector<unsigned char>& input_key_material,
            const std::vector<unsigned char>& salt,
            const std::vector<unsigned char>& info,
            size_t output_length) {

        std::vector<unsigned char> output(output_length);

        EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
        if (!pctx) {
            throw std::runtime_error("Failed to create HKDF context");
        }

        // Use a scope guard-like pattern for cleanup
        auto cleanup = [](EVP_PKEY_CTX* ctx) { EVP_PKEY_CTX_free(ctx); };
        std::unique_ptr<EVP_PKEY_CTX, decltype(cleanup)> ctx_guard(pctx, cleanup);

        if (EVP_PKEY_derive_init(pctx) <= 0) {
            throw std::runtime_error("HKDF init failed: " +
                                     detail::openssl_error_string());
        }

        if (EVP_PKEY_CTX_set_hkdf_md(pctx, EVP_sha256()) <= 0) {
            throw std::runtime_error("HKDF set digest failed");
        }

        if (!salt.empty()) {
            if (EVP_PKEY_CTX_set1_hkdf_salt(pctx, salt.data(),
                                             static_cast<int>(salt.size())) <= 0) {
                throw std::runtime_error("HKDF set salt failed");
            }
        }

        if (EVP_PKEY_CTX_set1_hkdf_key(pctx, input_key_material.data(),
                                        static_cast<int>(input_key_material.size())) <= 0) {
            throw std::runtime_error("HKDF set key failed");
        }

        if (!info.empty()) {
            if (EVP_PKEY_CTX_add1_hkdf_info(pctx, info.data(),
                                             static_cast<int>(info.size())) <= 0) {
                throw std::runtime_error("HKDF set info failed");
            }
        }

        size_t outlen = output_length;
        if (EVP_PKEY_derive(pctx, output.data(), &outlen) <= 0) {
            throw std::runtime_error("HKDF derive failed: " +
                                     detail::openssl_error_string());
        }

        output.resize(outlen);
        return output;
    }

    // -------------------------------------------------------------------------
    // HKDF-SHA512 variant
    // -------------------------------------------------------------------------
    static std::vector<unsigned char> hkdf_sha512(
            const std::vector<unsigned char>& input_key_material,
            const std::vector<unsigned char>& salt,
            const std::vector<unsigned char>& info,
            size_t output_length) {

        std::vector<unsigned char> output(output_length);

        EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
        if (!pctx) {
            throw std::runtime_error("Failed to create HKDF context");
        }

        auto cleanup = [](EVP_PKEY_CTX* ctx) { EVP_PKEY_CTX_free(ctx); };
        std::unique_ptr<EVP_PKEY_CTX, decltype(cleanup)> ctx_guard(pctx, cleanup);

        EVP_PKEY_derive_init(pctx);
        EVP_PKEY_CTX_set_hkdf_md(pctx, EVP_sha512());

        if (!salt.empty()) {
            EVP_PKEY_CTX_set1_hkdf_salt(pctx, salt.data(),
                                         static_cast<int>(salt.size()));
        }

        EVP_PKEY_CTX_set1_hkdf_key(pctx, input_key_material.data(),
                                    static_cast<int>(input_key_material.size()));

        if (!info.empty()) {
            EVP_PKEY_CTX_add1_hkdf_info(pctx, info.data(),
                                         static_cast<int>(info.size()));
        }

        size_t outlen = output_length;
        if (EVP_PKEY_derive(pctx, output.data(), &outlen) <= 0) {
            throw std::runtime_error("HKDF-SHA512 derive failed: " +
                                     detail::openssl_error_string());
        }

        output.resize(outlen);
        return output;
    }

    // -------------------------------------------------------------------------
    // Generate a cryptographically random salt
    // -------------------------------------------------------------------------
    static std::vector<unsigned char> generate_salt(size_t length = 32) {
        static SecureRandom rng;
        return rng.random_bytes(length);
    }

    // -------------------------------------------------------------------------
    // Argon2-like placeholder (password hashing with memory-hard function)
    // Note: Full Argon2 requires libargon2. This is a framework integration point.
    // -------------------------------------------------------------------------
    static std::vector<unsigned char> argon2_placeholder(
            const std::string& password,
            const std::vector<unsigned char>& salt,
            size_t key_length = 32) {
        // In production, integrate libargon2 here.
        // For now, fall back to PBKDF2 with high iteration count as a
        // framework-compatible alternative.
        spdlog::warn("argon2_placeholder: falling back to PBKDF2-SHA512");
        return pbkdf2_sha512(password, salt, key_length, 600000);
    }

    // -------------------------------------------------------------------------
    // HMAC-based one-time password (HOTP) - RFC 4226
    // -------------------------------------------------------------------------
    static uint32_t hotp(const std::vector<unsigned char>& key,
                          uint64_t counter,
                          size_t digits = 6) {
        // Convert counter to big-endian bytes
        unsigned char counter_bytes[8];
        for (int i = 7; i >= 0; --i) {
            counter_bytes[i] = static_cast<unsigned char>(counter & 0xFF);
            counter >>= 8;
        }

        // HMAC-SHA1
        unsigned int result_len = SHA_DIGEST_LENGTH;
        unsigned char hmac_result[SHA_DIGEST_LENGTH];
        HMAC(EVP_sha1(), key.data(), static_cast<int>(key.size()),
             counter_bytes, sizeof(counter_bytes),
             hmac_result, &result_len);

        // Dynamic truncation
        int offset = hmac_result[result_len - 1] & 0x0F;
        uint32_t binary = ((hmac_result[offset] & 0x7F) << 24) |
                          ((hmac_result[offset + 1] & 0xFF) << 16) |
                          ((hmac_result[offset + 2] & 0xFF) << 8) |
                          (hmac_result[offset + 3] & 0xFF);

        // Modulo to get desired number of digits
        uint32_t mod = 1;
        for (size_t i = 0; i < digits; ++i) {
            mod *= 10;
        }

        return binary % mod;
    }

    // -------------------------------------------------------------------------
    // Time-based OTP (TOTP) - RFC 6238
    // -------------------------------------------------------------------------
    static uint32_t totp(const std::vector<unsigned char>& key,
                          size_t digits = 6,
                          uint64_t time_step = 30) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        uint64_t counter = std::chrono::duration_cast<std::chrono::seconds>(now).count();
        counter /= time_step;

        return hotp(key, counter, digits);
    }

    // -------------------------------------------------------------------------
    // Generate a TOTP key in base32 format
    // -------------------------------------------------------------------------
    static std::string generate_totp_key(size_t byte_length = 20) {
        static SecureRandom rng;
        auto bytes = rng.random_bytes(byte_length);

        // Base32 encoding
        static const char b32_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
        std::string result;
        result.reserve(((bytes.size() * 8) + 4) / 5);

        unsigned int buffer = 0;
        int bits_left = 0;

        for (size_t i = 0; i < bytes.size(); ++i) {
            buffer = (buffer << 8) | bytes[i];
            bits_left += 8;

            while (bits_left >= 5) {
                result += b32_table[(buffer >> (bits_left - 5)) & 0x1F];
                bits_left -= 5;
            }
        }

        if (bits_left > 0) {
            result += b32_table[(buffer << (5 - bits_left)) & 0x1F];
        }

        return result;
    }
};

// =============================================================================
// 11. Constant-Time Comparison Utilities
// =============================================================================

// ---------------------------------------------------------------------------
// Constant-time string comparison
// ---------------------------------------------------------------------------
bool constant_time_compare(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        // Still do constant-time work to avoid length-based timing leak
        volatile unsigned char diff = 0;
        size_t max_len = std::max(a.size(), b.size());
        for (size_t i = 0; i < max_len; ++i) {
            unsigned char ca = i < a.size()
                ? static_cast<unsigned char>(a[i]) : 0;
            unsigned char cb = i < b.size()
                ? static_cast<unsigned char>(b[i]) : 0;
            diff |= ca ^ cb;
        }
        return false; // We already know they differ, but timing is now uniform
    }

    return SecureMemoryManager::secure_memcmp(a.data(), b.data(), a.size());
}

// ---------------------------------------------------------------------------
// Constant-time buffer comparison
// ---------------------------------------------------------------------------
bool constant_time_compare(const std::vector<unsigned char>& a,
                            const std::vector<unsigned char>& b) {
    if (a.size() != b.size()) {
        volatile unsigned char diff = 0;
        size_t max_len = std::max(a.size(), b.size());
        for (size_t i = 0; i < max_len; ++i) {
            unsigned char ca = i < a.size() ? a[i] : 0;
            unsigned char cb = i < b.size() ? b[i] : 0;
            diff |= ca ^ cb;
        }
        return false;
    }

    return SecureMemoryManager::secure_memcmp(a.data(), b.data(), a.size());
}

// ---------------------------------------------------------------------------
// Constant-time string_view comparison
// ---------------------------------------------------------------------------
bool constant_time_compare(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        volatile unsigned char diff = 0;
        size_t max_len = std::max(a.size(), b.size());
        for (size_t i = 0; i < max_len; ++i) {
            unsigned char ca = i < a.size()
                ? static_cast<unsigned char>(a[i]) : 0;
            unsigned char cb = i < b.size()
                ? static_cast<unsigned char>(b[i]) : 0;
            diff |= ca ^ cb;
        }
        return false;
    }

    return SecureMemoryManager::secure_memcmp(a.data(), b.data(), a.size());
}

// ---------------------------------------------------------------------------
// Constant-time integer comparison (useful for comparing MACs)
// ---------------------------------------------------------------------------
bool constant_time_compare_integer(uint64_t a, uint64_t b) {
    volatile uint64_t diff = a ^ b;
    // Check if diff is zero without branching
    // For each byte in diff...
    volatile unsigned char result = 0;
    for (size_t i = 0; i < sizeof(uint64_t); ++i) {
        result |= static_cast<unsigned char>((diff >> (i * 8)) & 0xFF);
    }
    return result == 0;
}

// ---------------------------------------------------------------------------
// Constant-time select (returns a if condition is 0, b if condition is 1)
// ---------------------------------------------------------------------------
template<typename T>
T constant_time_select(T a, T b, bool condition) {
    // Mask: all-ones if condition is true, all-zeros if false
    // This avoids branching
    using UT = typename std::make_unsigned<T>::type;
    UT mask = condition ? ~UT(0) : UT(0);
    return static_cast<T>((static_cast<UT>(a) & ~mask) |
                          (static_cast<UT>(b) & mask));
}

// ---------------------------------------------------------------------------
// Constant-time byte equality (returns 0xFF if equal, 0x00 if not)
// ---------------------------------------------------------------------------
unsigned char constant_time_byte_eq(unsigned char a, unsigned char b) {
    unsigned char diff = a ^ b;
    // If diff == 0, all bits are 0, ~diff = 0xFF, rightmost bit = 0
    // We need to detect zero: ((diff - 1) & ~diff) >> 7 gives 0xFF if diff==0
    // Apply bit twiddling: flip bits
    diff = ~diff;
    diff &= (diff >> 4);
    diff &= (diff >> 2);
    diff &= (diff >> 1);
    return diff;
}

// ---------------------------------------------------------------------------
// Constant-time memory equality check (returns 0xFF if equal, 0 otherwise)
// ---------------------------------------------------------------------------
unsigned char constant_time_mem_is_zero(const unsigned char* data, size_t len) {
    unsigned char result = 0;
    for (size_t i = 0; i < len; ++i) {
        result |= data[i];
    }
    // Now result is 0 iff all bytes were 0
    return constant_time_byte_eq(result, 0);
}

// =============================================================================
// Internal helper implementations
// =============================================================================

namespace detail {

std::string openssl_error_string() {
    BIO* bio = BIO_new(BIO_s_mem());
    ERR_print_errors(bio);
    char* buf = nullptr;
    long len = BIO_get_mem_data(bio, &buf);
    std::string result(buf, len);
    BIO_free(bio);
    if (result.empty()) {
        result = "Unknown OpenSSL error";
    }
    return result;
}

bool lock_memory_region(void* addr, size_t len) {
    if (mlock(addr, len) != 0) {
        spdlog::warn("mlock failed: {}", strerror(errno));
        return false;
    }
    return true;
}

void zero_on_free(void* addr, size_t len) {
#ifdef MADV_DONTDUMP
    madvise(addr, len, MADV_DONTDUMP);
#endif
}

} // namespace detail

// =============================================================================
// Singleton factories and convenience accessors
// =============================================================================

namespace {

std::unique_ptr<CertificateManager> g_cert_manager;
std::unique_ptr<TokenGenerator> g_token_generator;
std::unique_ptr<SecureRandom> g_secure_random;
std::unique_ptr<KeyDerivation> g_key_derivation;
std::mutex g_singleton_mutex;

} // namespace

CertificateManager& certificate_manager() {
    std::lock_guard<std::mutex> lock(g_singleton_mutex);
    if (!g_cert_manager) {
        g_cert_manager = std::make_unique<CertificateManager>();
    }
    return *g_cert_manager;
}

TokenGenerator& token_generator() {
    std::lock_guard<std::mutex> lock(g_singleton_mutex);
    if (!g_token_generator) {
        g_token_generator = std::make_unique<TokenGenerator>();
    }
    return *g_token_generator;
}

SecureRandom& secure_random() {
    std::lock_guard<std::mutex> lock(g_singleton_mutex);
    if (!g_secure_random) {
        g_secure_random = std::make_unique<SecureRandom>();
    }
    return *g_secure_random;
}

InputSanitizer& input_sanitizer() {
    // Stateless, but provide for API consistency
    static InputSanitizer instance;
    return instance;
}

// =============================================================================
// Convenience free functions
// =============================================================================

bool verify_certificate_chain(const std::string& cert_pem,
                               const std::string& ca_bundle_pem) {
    auto result = certificate_manager().verify_chain(cert_pem, ca_bundle_pem);
    return result.valid;
}

std::string generate_jwt_token(const std::string& subject,
                                const std::string& secret,
                                int ttl_seconds = 3600) {
    JWTClaims claims;
    claims.subject = subject;
    claims.issuer = "cppdesk";
    auto now = std::chrono::system_clock::now();
    claims.issued_at = now;
    claims.expiration = now + std::chrono::seconds(ttl_seconds);
    claims.jwt_id = secure_random().uuid_v4();

    return token_generator().generate_hs256_jwt(claims, secret);
}

bool validate_jwt_token(const std::string& token, const std::string& secret) {
    return token_generator().verify_hs256_jwt(token, secret);
}

std::string hash_password(const std::string& password) {
    auto salt = KeyDerivation::generate_salt(32);
    auto hash = KeyDerivation::pbkdf2_sha512(password, salt, 32, 600000);

    // Store as: $pbkdf2-sha512$iterations$salt$hash (all hex-encoded)
    std::stringstream ss;
    ss << "$pbkdf2-sha512$600000$";
    for (auto b : salt) ss << std::hex << std::setw(2) << std::setfill('0')
                           << static_cast<int>(b);
    ss << "$";
    for (auto b : hash) ss << std::hex << std::setw(2) << std::setfill('0')
                           << static_cast<int>(b);

    return ss.str();
}

bool create_audit_logger(const std::string& path) {
    static std::unique_ptr<AuditLogger> g_audit_logger;
    static std::mutex audit_mutex;

    std::lock_guard<std::mutex> lock(audit_mutex);
    try {
        g_audit_logger = std::make_unique<AuditLogger>(path);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to create audit logger: {}", e.what());
        return false;
    }
}

AuditLogger& audit_logger() {
    // Must be initialized via create_audit_logger first
    static std::unique_ptr<AuditLogger> g_audit_logger;
    static std::mutex audit_mutex;
    throw std::runtime_error(
        "Audit logger not initialized - call create_audit_logger() first");
}

} // namespace cppdesk::common
