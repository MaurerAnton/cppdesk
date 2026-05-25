
// common utilities expansion
#include "common/config.hpp"
#include "common/crypto.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>
#include <random>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <regex>

namespace cppdesk::common {

// ====== Timestamp utilities ======
std::string format_timestamp(int64_t us) {
    auto ms = us / 1000;
    auto sec = ms / 1000;
    auto min = sec / 60;
    auto hour = min / 60;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << (hour % 24) << ":"
        << std::setw(2) << (min % 60) << ":"
        << std::setw(2) << (sec % 60) << "."
        << std::setw(3) << (ms % 1000);
    return oss.str();
}

int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int64_t now_ms() { return now_us() / 1000; }

// ====== String utilities ======
std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::istringstream iss(s);
    std::string part;
    while (std::getline(iss, part, delim)) {
        if (!part.empty()) parts.push_back(part);
    }
    return parts;
}

std::string join(const std::vector<std::string>& parts, const std::string& delim) {
    if (parts.empty()) return "";
    std::ostringstream oss;
    oss << parts[0];
    for (size_t i = 1; i < parts.size(); i++) oss << delim << parts[i];
    return oss.str();
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
        s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string to_lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

std::string to_upper(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::toupper);
    return r;
}

std::string replace_all(const std::string& s, const std::string& from,
    const std::string& to) {
    if (from.empty()) return s;
    std::string r = s;
    size_t pos = 0;
    while ((pos = r.find(from, pos)) != std::string::npos) {
        r.replace(pos, from.size(), to);
        pos += to.size();
    }
    return r;
}

// ====== File system utilities ======
bool file_exists(const std::string& path) {
    return std::filesystem::exists(path);
}

std::string file_extension(const std::string& path) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    return path.substr(pos);
}

std::string file_name(const std::string& path) {
    auto pos = path.rfind('/');
#ifdef _WIN32
    auto pos2 = path.rfind('\\');
    if (pos2 != std::string::npos && (pos == std::string::npos || pos2 > pos))
        pos = pos2;
#endif
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

std::string dir_name(const std::string& path) {
    auto pos = path.rfind('/');
#ifdef _WIN32
    auto pos2 = path.rfind('\\');
    if (pos2 != std::string::npos && (pos == std::string::npos || pos2 > pos))
        pos = pos2;
#endif
    if (pos == std::string::npos) return ".";
    return path.substr(0, pos);
}

std::string temp_file_path(const std::string& prefix, const std::string& ext) {
    auto tmp = std::filesystem::temp_directory_path();
    auto name = prefix + "_" + std::to_string(random_u64()) + ext;
    return (tmp / name).string();
}

std::vector<uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

bool write_file_bytes(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    return f.good();
}

// ====== Rate limiter ======
class TokenBucket {
    double tokens_;
    double max_tokens_;
    double refill_rate_;
    std::chrono::steady_clock::time_point last_refill_;

public:
    TokenBucket(double rate_per_sec, double burst = 0)
        : tokens_(burst > 0 ? burst : rate_per_sec),
          max_tokens_(burst > 0 ? burst : rate_per_sec),
          refill_rate_(rate_per_sec),
          last_refill_(std::chrono::steady_clock::now()) {}

    bool try_consume(double tokens = 1.0) {
        refill();
        if (tokens_ >= tokens) { tokens_ -= tokens; return true; }
        return false;
    }

    void refill() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - last_refill_).count();
        tokens_ = std::min(max_tokens_, tokens_ + elapsed * refill_rate_);
        last_refill_ = now;
    }

    double available() const { return tokens_; }
};

// ====== Exponential backoff ======
class ExponentialBackoff {
    std::chrono::milliseconds initial_;
    std::chrono::milliseconds max_;
    double multiplier_;
    int attempt_ = 0;

public:
    ExponentialBackoff(std::chrono::milliseconds initial = std::chrono::seconds(1),
        std::chrono::milliseconds max = std::chrono::minutes(5),
        double multiplier = 2.0)
        : initial_(initial), max_(max), multiplier_(multiplier) {}

    std::chrono::milliseconds next_delay() {
        double ms = initial_.count() * std::pow(multiplier_, attempt_);
        attempt_++;
        auto delay = std::chrono::milliseconds(static_cast<long long>(ms));
        return std::min(delay, max_);
    }

    void reset() { attempt_ = 0; }
    int attempt() const { return attempt_; }
};

// ====== URL encoding ======
std::string url_encode(const std::string& s) {
    std::ostringstream oss;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        } else {
            oss << '%' << std::hex << std::uppercase << std::setw(2)
                << std::setfill('0') << static_cast<int>(c);
        }
    }
    return oss.str();
}

std::string url_decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int val;
            std::istringstream iss(s.substr(i + 1, 2));
            iss >> std::hex >> val;
            out += static_cast<char>(val);
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

// ====== Safe string conversion ======
std::optional<int> safe_stoi(const std::string& s) {
    try { return std::stoi(s); } catch (...) { return std::nullopt; }
}

std::optional<int64_t> safe_stoll(const std::string& s) {
    try { return std::stoll(s); } catch (...) { return std::nullopt; }
}

std::optional<double> safe_stod(const std::string& s) {
    try { return std::stod(s); } catch (...) { return std::nullopt; }
}

} // namespace cppdesk::common
