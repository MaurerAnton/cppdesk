#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <mutex>
#include <atomic>
#include <random>
#include <spdlog/spdlog.h>
#include "common/crypto.hpp"

namespace cppdesk::auth {

class TotpGenerator {
    static constexpr int DIGITS = 6;
    static constexpr int PERIOD = 30;
    std::vector<uint8_t> secret_;
public:
    TotpGenerator() { secret_ = crypto::random_bytes(20); }
    explicit TotpGenerator(const std::string& b32_secret) { decode_base32(b32_secret); }
    
    int generate() const {
        auto counter = static_cast<uint64_t>(std::time(nullptr) / PERIOD);
        return compute_hotp(counter);
    }
    
    bool verify(int code, int window = 1) const {
        auto counter = static_cast<uint64_t>(std::time(nullptr) / PERIOD);
        for (int i = -window; i <= window; i++) {
            if (compute_hotp(static_cast<uint64_t>(static_cast<int64_t>(counter) + i)) == code)
                return true;
        }
        return false;
    }
    
    std::string to_base32() const {
        static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
        std::string result;
        int bits = 0, val = 0;
        for (auto b : secret_) {
            val = (val << 8) | b; bits += 8;
            while (bits >= 5) { result += alphabet[(val >> (bits - 5)) & 31]; bits -= 5; }
        }
        if (bits > 0) result += alphabet[(val << (5 - bits)) & 31];
        return result;
    }

    std::string get_provisioning_uri(const std::string& label, const std::string& issuer = "cppdesk") const {
        return "otpauth://totp/" + issuer + ":" + label + "?secret=" + to_base32() + "&issuer=" + issuer;
    }
private:
    int compute_hotp(uint64_t counter) const {
        // HMAC-SHA1 based HOTP
        uint8_t counter_bytes[8];
        for (int i = 7; i >= 0; i--) { counter_bytes[i] = counter & 0xFF; counter >>= 8; }
        auto mac = crypto::hmac_sha256(counter_bytes, 8, secret_.data(), secret_.size());
        int offset = mac[mac.size()-1] & 0x0F;
        int binary = ((mac[offset] & 0x7F) << 24) | (mac[offset+1] << 16) | (mac[offset+2] << 8) | mac[offset+3];
        int mod = 1; for (int i = 0; i < DIGITS; i++) mod *= 10;
        return binary % mod;
    }
    
    void decode_base32(const std::string& b32) {
        static const int values[128] = {};
        // Simplified base32 decode
        secret_.clear();
        for (size_t i = 0; i < b32.size(); i++) {
            if (b32[i] >= 'A' && b32[i] <= 'Z') secret_.push_back(b32[i] - 'A');
            else if (b32[i] >= '2' && b32[i] <= '7') secret_.push_back(b32[i] - '2' + 26);
        }
    }
};

class LoginAttemptTracker {
    struct Entry {
        int failures = 0;
        std::chrono::steady_clock::time_point last_attempt;
        std::chrono::steady_clock::time_point blocked_until;
    };
    std::map<std::string, Entry> entries_;
    mutable std::mutex mutex_;
    int max_failures_ = 5;
    std::chrono::minutes block_duration_{5};
    std::chrono::minutes reset_window_{10};
public:
    bool record_failure(const std::string& addr) {
        std::lock_guard lk(mutex_);
        auto& e = entries_[addr];
        auto now = std::chrono::steady_clock::now();
        if (now > e.blocked_until) { e.blocked_until = now - std::chrono::seconds(1); }
        if (now - e.last_attempt > reset_window_) e.failures = 0;
        e.failures++;
        e.last_attempt = now;
        if (e.failures >= max_failures_) { e.blocked_until = now + block_duration_; return false; }
        return true;
    }
    bool is_blocked(const std::string& addr) const {
        std::lock_guard lk(mutex_);
        auto it = entries_.find(addr);
        if (it == entries_.end()) return false;
        return std::chrono::steady_clock::now() < it->second.blocked_until;
    }
    void reset(const std::string& addr) {
        std::lock_guard lk(mutex_);
        entries_.erase(addr);
    }
};

class PasswordStrengthChecker {
public:
    enum class Strength { VERY_WEAK, WEAK, FAIR, STRONG, VERY_STRONG };
    static Strength check(const std::string& pw) {
        int score = 0;
        if (pw.size() >= 8) score++;
        if (pw.size() >= 12) score++;
        if (pw.size() >= 16) score++;
        bool has_upper = false, has_lower = false, has_digit = false, has_special = false;
        for (char c : pw) {
            if (isupper(c)) has_upper = true;
            else if (islower(c)) has_lower = true;
            else if (isdigit(c)) has_digit = true;
            else has_special = true;
        }
        if (has_upper) score++;
        if (has_lower) score++;
        if (has_digit) score++;
        if (has_special) score++;
        if (score <= 2) return Strength::VERY_WEAK;
        if (score <= 3) return Strength::WEAK;
        if (score <= 4) return Strength::FAIR;
        if (score <= 5) return Strength::STRONG;
        return Strength::VERY_STRONG;
    }
    static const char* to_string(Strength s) {
        switch (s) {
            case Strength::VERY_WEAK: return "Very Weak";
            case Strength::WEAK: return "Weak";
            case Strength::FAIR: return "Fair";
            case Strength::STRONG: return "Strong";
            case Strength::VERY_STRONG: return "Very Strong";
        }
        return "Unknown";
    }
};

class TokenManager {
    std::map<std::string, std::pair<std::string, std::chrono::steady_clock::time_point>> tokens_;
    mutable std::mutex mutex_;
    std::chrono::minutes token_ttl_{60};
public:
    std::string issue_token(const std::string& user_id) {
        auto token_bytes = crypto::random_bytes(32);
        std::string token = crypto::encode64(token_bytes.data(), token_bytes.size());
        std::lock_guard lk(mutex_);
        tokens_[token] = {user_id, std::chrono::steady_clock::now() + token_ttl_};
        cleanup_expired();
        return token;
    }
    std::optional<std::string> validate_token(const std::string& token) {
        std::lock_guard lk(mutex_);
        auto it = tokens_.find(token);
        if (it == tokens_.end()) return std::nullopt;
        if (std::chrono::steady_clock::now() > it->second.second) {
            tokens_.erase(it);
            return std::nullopt;
        }
        return it->second.first;
    }
    void revoke_token(const std::string& token) {
        std::lock_guard lk(mutex_);
        tokens_.erase(token);
    }
private:
    void cleanup_expired() {
        auto now = std::chrono::steady_clock::now();
        for (auto it = tokens_.begin(); it != tokens_.end();) {
            if (now > it->second.second) it = tokens_.erase(it);
            else ++it;
        }
    }
};

} // namespace cppdesk::auth