// updater_full.cpp — Comprehensive Auto-Update Implementation
// Part of cppdesk remote desktop update module
// C++20 | SPDLOG | namespace cppdesk::updater
//
// Implements: GitHub Releases API version checking with JSON parsing,
// download manager with resume support and progress callbacks, package
// verification (SHA256 checksum), platform-specific installation (MSI on
// Windows, DEB/RPM on Linux, DMG on macOS, APK on Android), silent update
// mode and user-prompted mode, rollback on installation failure, update
// scheduling (check interval, defer, auto-install window), release channel
// support (stable, beta, nightly), delta/binary diff updates, update
// history log, and self-restart after update.

#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <bitset>
#include <charconv>
#include <chrono>
#include <cmath>
#include <compare>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <ios>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <queue>
#include <random>
#include <ranges>
#include <regex>
#include <set>
#include <shared_mutex>
#include <span>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/fmt/fmt.h>

// Platform-specific includes
#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <shellapi.h>
  #include <shlobj.h>
  #include <shlwapi.h>
  #include <wincrypt.h>
  #include <wininet.h>
  #include <msi.h>
  #pragma comment(lib, "shell32.lib")
  #pragma comment(lib, "shlwapi.lib")
  #pragma comment(lib, "crypt32.lib")
  #pragma comment(lib, "wininet.lib")
  #pragma comment(lib, "msi.lib")
#elif defined(__APPLE__)
  #include <TargetConditionals.h>
  #if TARGET_OS_IPHONE
    // iOS imports, no auto-update for iOS generally
  #else
    #include <CoreFoundation/CoreFoundation.h>
    #include <CoreServices/CoreServices.h>
    #include <sys/stat.h>
    #include <sys/mount.h>
    #include <copyfile.h>
    #include <spawn.h>
    #include <unistd.h>
    extern char **environ;
  #endif
#elif defined(__ANDROID__)
  #include <jni.h>
  #include <android/log.h>
  #include <unistd.h>
#elif defined(__linux__)
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <sys/prctl.h>
  #include <unistd.h>
  #include <signal.h>
  #include <fcntl.h>
#endif

// For HTTP requests we use a minimal socket-based approach.
// In production this would use libcurl or a platform HTTP API.
#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <sys/socket.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netinet/tcp.h>
  #include <poll.h>
  #include <errno.h>
#endif

// ============================================================================
// Namespace
// ============================================================================

namespace cppdesk::updater {

// ============================================================================
// Forward declarations
// ============================================================================

class Version;
class UpdateChecker;
class DownloadManager;
class PackageVerifier;
class PlatformInstaller;
class UpdateScheduler;
class ReleaseChannel;
class DeltaUpdater;
class UpdateHistory;
class RestartManager;
class UpdateManager;
struct UpdateInfo;
struct DownloadProgress;
struct InstallResult;
struct UpdateConfig;
enum class UpdateMode;
enum class Channel;

// ============================================================================
// Constants
// ============================================================================

/// Current application version string.
static constexpr const char* kCurrentVersion   = "1.3.0";
static constexpr int         kCurrentVersionMajor = 1;
static constexpr int         kCurrentVersionMinor = 3;
static constexpr int         kCurrentVersionPatch = 0;

/// GitHub repository information.
static constexpr const char* kGithubOwner      = "MaurerAnton";
static constexpr const char* kGithubRepo       = "cppdesk";
static constexpr const char* kGithubApiBase    = "https://api.github.com";
static constexpr const char* kGithubReleasesApi = "https://api.github.com/repos/MaurerAnton/cppdesk/releases";

/// Default update check interval (4 hours in seconds).
static constexpr std::chrono::seconds kDefaultCheckInterval{4 * 3600};

/// Maximum retry attempts for download.
static constexpr int kMaxDownloadRetries = 5;

/// Buffer size for download chunks.
static constexpr size_t kDownloadBufferSize = 65536; // 64 KB

/// Maximum redirect depth for HTTP.
static constexpr int kMaxRedirectDepth = 10;

/// History log file name.
static constexpr const char* kUpdateHistoryFile = "update_history.json";

/// Temp download directory name.
static constexpr const char* kDownloadTempDir = "update_downloads";

/// Backup directory for rollback.
static constexpr const char* kBackupDir = "update_backups";

/// Default silent mode auto-install window (2 AM - 5 AM).
static constexpr int kDefaultSilentWindowStart = 2;
static constexpr int kDefaultSilentWindowEnd   = 5;

// ============================================================================
// Enumerations
// ============================================================================

/// Update mode: how the update is applied.
enum class UpdateMode : uint8_t {
    Silent = 0,       ///< Download and install without user interaction.
    Prompted = 1,     ///< Prompt user before downloading/installing.
    Manual = 2,       ///< Only check; user explicitly triggers install.
    Scheduled = 3,    ///< Install at a scheduled time.
};

/// Release channel for update selection.
enum class Channel : uint8_t {
    Stable = 0,       ///< Production-ready releases.
    Beta = 1,         ///< Pre-release, feature-complete testing.
    Nightly = 2,      ///< Cutting-edge, may be unstable.
};

/// Overall update status.
enum class UpdateStatus : uint8_t {
    Idle = 0,
    Checking = 1,
    UpdateAvailable = 2,
    Downloading = 3,
    Verifying = 4,
    Installing = 5,
    Success = 6,
    Failed = 7,
    Rollback = 8,
    UpToDate = 9,
    Deferred = 10,
};

/// Installation result detail.
enum class InstallError : uint8_t {
    None = 0,
    DownloadFailed = 1,
    ChecksumMismatch = 2,
    SignatureInvalid = 3,
    InstallerFailed = 4,
    PermissionDenied = 5,
    DiskSpaceInsufficient = 6,
    PlatformNotSupported = 7,
    PackageCorrupted = 8,
    RollbackFailed = 9,
    Timeout = 10,
    Unknown = 255,
};

/// Delta update method.
enum class DeltaMethod : uint8_t {
    FullDownload = 0,    ///< Download the entire new package.
    BinaryDiff = 1,       ///< Apply bsdiff/bspatch-style binary diff.
    FileLevel = 2,        ///< Only download changed files.
};

// ============================================================================
// Version class — semantic version parsing and comparison
// ============================================================================

/// Represents a semantic version (major.minor.patch[-prerelease][+build]).
class Version {
public:
    int         major = 0;
    int         minor = 0;
    int         patch = 0;
    std::string prerelease;   // e.g. "beta.1", "rc2"
    std::string build;        // build metadata

    constexpr Version() noexcept = default;

    constexpr Version(int ma, int mi, int pa) noexcept
        : major(ma), minor(mi), patch(pa) {}

    Version(int ma, int mi, int pa, std::string pre, std::string bld = {})
        : major(ma), minor(mi), patch(pa)
        , prerelease(std::move(pre)), build(std::move(bld)) {}

    /// Parse a version string like "2.1.0-beta.3+build42".
    static std::optional<Version> parse(std::string_view sv) noexcept {
        Version v;
        auto it = sv.begin();
        auto end = sv.end();

        // Optional leading 'v' or 'V'
        if (it != end && (*it == 'v' || *it == 'V')) ++it;

        // Parse major
        if (!parse_int(it, end, v.major)) return std::nullopt;
        if (it == end || *it != '.') return std::nullopt;
        ++it;

        // Parse minor
        if (!parse_int(it, end, v.minor)) return std::nullopt;
        if (it == end || *it != '.') {
            // tolerate "1.2" format
            v.patch = 0;
        } else {
            ++it;
            if (!parse_int(it, end, v.patch)) return std::nullopt;
        }

        // Parse prerelease if present
        if (it != end && *it == '-') {
            ++it;
            auto pre_start = it;
            while (it != end && *it != '+') ++it;
            v.prerelease.assign(pre_start, it);
        }

        // Parse build metadata if present
        if (it != end && *it == '+') {
            ++it;
            v.build.assign(it, end);
        }

        return v;
    }

    /// Compare versions semantically.
    auto operator<=>(Version const& o) const noexcept {
        if (auto c = major <=> o.major; c != 0) return c;
        if (auto c = minor <=> o.minor; c != 0) return c;
        if (auto c = patch <=> o.patch; c != 0) return c;

        // Prerelease versions are lower than release versions.
        bool a_has_pre = !prerelease.empty();
        bool b_has_pre = !o.prerelease.empty();
        if (!a_has_pre && b_has_pre) return std::strong_ordering::greater;
        if (a_has_pre && !b_has_pre) return std::strong_ordering::less;
        if (a_has_pre && b_has_pre) {
            // Lexicographic comparison of prerelease components
            return compare_prerelease(prerelease, o.prerelease);
        }
        return std::strong_ordering::equal;
    }

    bool operator==(Version const& o) const noexcept = default;

    /// Convert back to string.
    [[nodiscard]] std::string to_string() const {
        std::string s = fmt::format("{}.{}.{}", major, minor, patch);
        if (!prerelease.empty()) {
            s += '-';
            s += prerelease;
        }
        if (!build.empty()) {
            s += '+';
            s += build;
        }
        return s;
    }

  private:
    static bool parse_int(std::string_view::iterator& it,
                          std::string_view::iterator end, int& out) noexcept {
        if (it == end || !std::isdigit(static_cast<unsigned char>(*it)))
            return false;
        int val = 0;
        while (it != end && std::isdigit(static_cast<unsigned char>(*it))) {
            val = val * 10 + (*it - '0');
            if (val > 999999) return false; // sanity limit
            ++it;
        }
        out = val;
        return true;
    }

    static std::strong_ordering compare_prerelease(
        std::string_view a, std::string_view b) noexcept {
        auto ai = a.begin(), ae = a.end();
        auto bi = b.begin(), be = b.end();

        while (ai != ae && bi != be) {
            if (*ai == '.' && *bi == '.') { ++ai; ++bi; continue; }

            if (std::isdigit(static_cast<unsigned char>(*ai)) &&
                std::isdigit(static_cast<unsigned char>(*bi))) {
                // Numeric comparison
                int an = 0, bn = 0;
                while (ai != ae && std::isdigit(static_cast<unsigned char>(*ai)))
                    { an = an * 10 + (*ai - '0'); ++ai; }
                while (bi != be && std::isdigit(static_cast<unsigned char>(*bi)))
                    { bn = bn * 10 + (*bi - '0'); ++bi; }
                if (an < bn) return std::strong_ordering::less;
                if (an > bn) return std::strong_ordering::greater;
            } else {
                if (*ai < *bi) return std::strong_ordering::less;
                if (*ai > *bi) return std::strong_ordering::greater;
                ++ai; ++bi;
            }
        }

        if (ai == ae && bi != be) return std::strong_ordering::less;
        if (ai != ae && bi == be) return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }
};

// ============================================================================
// UpdateInfo — complete information about an available update
// ============================================================================

/// Holds all metadata about a discovered update.
struct UpdateInfo {
    Version     version;
    std::string tag_name;           // e.g. "v2.0.0"
    std::string release_name;       // e.g. "Version 2.0.0 — Major Release"
    std::string changelog;          // Release notes in markdown
    std::string download_url;       // Direct download URL for the package
    std::string checksum_url;       // URL to SHA256 checksum file
    std::string checksum;           // SHA256 hex digest if already fetched
    std::string signature_url;      // URL to GPG/authenticode signature
    std::string delta_url;          // URL for delta/binary diff (if available)
    uint64_t    package_size = 0;   // Size in bytes
    uint64_t    delta_size = 0;     // Size of delta diff in bytes
    bool        is_prerelease = false;
    bool        is_draft = false;
    Channel     channel = Channel::Stable;
    std::chrono::system_clock::time_point published_at;
    std::vector<std::string> assets;     // All asset download URLs
    std::map<std::string, std::string> asset_map; // name -> URL

    [[nodiscard]] bool is_valid() const noexcept {
        return !download_url.empty() && !version.to_string().empty();
    }

    [[nodiscard]] bool has_delta() const noexcept {
        return !delta_url.empty() && delta_size > 0;
    }

    [[nodiscard]] bool has_checksum() const noexcept {
        return !checksum.empty() || !checksum_url.empty();
    }
};

// ============================================================================
// DownloadProgress — callback data for download progress
// ============================================================================

/// Progress information passed to download callbacks.
struct DownloadProgress {
    uint64_t    bytes_downloaded = 0;
    uint64_t    total_bytes = 0;
    int         retry_count = 0;
    double      speed_bytes_per_sec = 0.0;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_update;
    bool        is_complete = false;
    bool        is_resumed = false;
    std::string stage; // "connecting", "downloading", "verifying", "done"

    [[nodiscard]] double percent() const noexcept {
        if (total_bytes == 0) return 0.0;
        return 100.0 * static_cast<double>(bytes_downloaded) /
                      static_cast<double>(total_bytes);
    }

    [[nodiscard]] std::chrono::seconds elapsed() const noexcept {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time);
    }

    [[nodiscard]] std::chrono::seconds eta() const noexcept {
        if (speed_bytes_per_sec <= 0.0 || total_bytes <= bytes_downloaded)
            return std::chrono::seconds{0};
        auto remaining = total_bytes - bytes_downloaded;
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::duration<double>(remaining / speed_bytes_per_sec));
    }
};

// ============================================================================
// InstallResult — outcome of an installation attempt
// ============================================================================

/// Result of an install operation.
struct InstallResult {
    bool            success = false;
    InstallError    error = InstallError::None;
    std::string     error_message;
    std::string     installed_version;
    std::string     previous_version;
    std::string     backup_path;    // Path to rollback backup
    int             exit_code = 0;
    bool            requires_restart = true;
    std::chrono::system_clock::time_point timestamp;

    [[nodiscard]] std::string summary() const {
        if (success) {
            return fmt::format("Successfully updated from {} to {}",
                               previous_version, installed_version);
        }
        return fmt::format("Update failed: {} (code {})",
                           error_message, static_cast<int>(error));
    }
};

// ============================================================================
// UpdateConfig — user-configurable update settings
// ============================================================================

/// Configuration for the update system.
struct UpdateConfig {
    UpdateMode      mode = UpdateMode::Prompted;
    Channel         channel = Channel::Stable;
    std::chrono::seconds check_interval = kDefaultCheckInterval;
    bool            auto_download = false;      ///< Download automatically when available.
    bool            auto_install = false;       ///< Install automatically (within window).
    bool            allow_downgrade = false;    ///< Allow installing older versions.
    bool            use_delta = true;           ///< Prefer delta updates when available.
    bool            verify_checksum = true;     ///< Verify SHA256 checksum.
    bool            verify_signature = false;   ///< Verify GPG/Authenticode signature.
    int             silent_window_start = kDefaultSilentWindowStart;
    int             silent_window_end = kDefaultSilentWindowEnd;
    int             max_defer_count = 5;        ///< How many times user can defer.
    std::string     temp_directory;             ///< Custom temp dir for downloads.
    std::string     install_directory;          ///< Target install directory.
    std::string     proxy_url;                  ///< HTTP proxy for downloads.
    std::string     custom_api_url;             ///< Override GitHub API URL.
    bool            include_beta_in_stable = false; ///< Show beta releases on stable channel.
    std::function<void(DownloadProgress const&)> progress_callback;
    std::function<void(std::string_view)> status_callback;
    std::function<bool(UpdateInfo const&)> prompt_callback; ///< Returns true if user accepts.
};

// ============================================================================
// HTTP utilities — minimal HTTP/HTTPS client
// ============================================================================

namespace http {

/// Simple HTTP response.
struct Response {
    int         status_code = 0;
    std::string status_message;
    std::string body;
    std::map<std::string, std::string> headers;
    bool        success = false;
};

/// URL components.
struct UrlParts {
    std::string scheme;     // "http" or "https"
    std::string host;
    int         port = 0;
    std::string path;       // includes query string
};

/// Parse a URL into components.
[[nodiscard]] inline std::optional<UrlParts> parse_url(std::string_view url) {
    UrlParts parts;

    // Find scheme
    auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) return std::nullopt;
    parts.scheme = std::string(url.substr(0, scheme_end));
    auto rest = url.substr(scheme_end + 3);

    // Split host and path
    auto path_start = rest.find('/');
    if (path_start == std::string_view::npos) {
        parts.host = std::string(rest);
        parts.path = "/";
    } else {
        parts.host = std::string(rest.substr(0, path_start));
        parts.path = std::string(rest.substr(path_start));
    }

    // Extract port from host if present
    auto colon = parts.host.rfind(':');
    if (colon != std::string::npos) {
        auto port_str = parts.host.substr(colon + 1);
        parts.port = std::stoi(std::string(port_str));
        parts.host = parts.host.substr(0, colon);
    } else {
        parts.port = (parts.scheme == "https") ? 443 : 80;
    }

    return parts;
}

/// Base64 encode (for basic auth).
[[nodiscard]] inline std::string base64_encode(std::string_view input) {
    static constexpr const char* kBase64Chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((input.size() + 2) / 3) * 4);

    size_t i = 0;
    unsigned char c3[3];
    auto it = input.begin();
    auto end = input.end();

    while (it != end) {
        c3[0] = static_cast<unsigned char>(*it++);
        c3[1] = (it != end) ? static_cast<unsigned char>(*it++) : 0;
        c3[2] = (it != end) ? static_cast<unsigned char>(*it++) : 0;

        result.push_back(kBase64Chars[(c3[0] & 0xFC) >> 2]);
        result.push_back(kBase64Chars[((c3[0] & 0x03) << 4) | ((c3[1] & 0xF0) >> 4)]);
        result.push_back((it > end - 1) ? '=' : kBase64Chars[((c3[1] & 0x0F) << 2) | ((c3[2] & 0xC0) >> 6)]);
        result.push_back((it > end) ? '=' : kBase64Chars[c3[2] & 0x3F]);
    }

    return result;
}

/// URL-encode a string.
[[nodiscard]] inline std::string url_encode(std::string_view input) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (unsigned char c : input) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(c);
        }
    }
    return escaped.str();
}

/// Build an HTTP request string.
[[nodiscard]] inline std::string build_request(
    std::string_view method, std::string_view host,
    std::string_view path, std::map<std::string, std::string> const& extra_headers = {},
    std::string_view body = {}) {
    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n";
    req << "Host: " << host << "\r\n";
    req << "User-Agent: cppdesk-updater/2.0\r\n";
    req << "Accept: application/json, application/octet-stream, */*\r\n";
    req << "Connection: close\r\n";
    for (auto const& [k, v] : extra_headers) {
        req << k << ": " << v << "\r\n";
    }
    if (!body.empty()) {
        req << "Content-Length: " << body.size() << "\r\n";
    }
    req << "\r\n";
    if (!body.empty()) {
        req << body;
    }
    return req.str();
}

/// Parse HTTP response headers.
inline void parse_response_headers(std::string_view header_block,
                                   Response& resp) {
    std::istringstream stream(std::string(header_block));
    std::string line;
    bool first = true;

    while (std::getline(stream, line) && !line.empty() && line != "\r") {
        if (line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (first) {
            // Status line: "HTTP/1.1 200 OK"
            auto sp1 = line.find(' ');
            if (sp1 != std::string::npos) {
                auto sp2 = line.find(' ', sp1 + 1);
                auto code_str = line.substr(sp1 + 1, sp2 - sp1 - 1);
                resp.status_code = std::stoi(code_str);
                resp.status_message = (sp2 != std::string::npos)
                    ? line.substr(sp2 + 1) : "";
            }
            first = false;
        } else {
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                auto key = line.substr(0, colon);
                auto value = line.substr(colon + 1);
                // Trim leading space
                if (!value.empty() && value[0] == ' ') value = value.substr(1);
                resp.headers[key] = value;
            }
        }
    }
}

/// Check if a socket is ready for reading with a timeout.
[[nodiscard]] inline bool socket_ready(int fd, int timeout_ms) {
#if defined(_WIN32)
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    return ::select(0, &fds, nullptr, nullptr, &tv) > 0;
#else
    pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    return ::poll(&pfd, 1, timeout_ms) > 0;
#endif
}

/// Perform a blocking HTTP GET request.
/// Returns the full response including body.
[[nodiscard]] inline Response get(std::string_view url,
                                  std::map<std::string, std::string> headers = {},
                                  int timeout_ms = 30000,
                                  int redirect_depth = 0) {
    Response resp;
    if (redirect_depth > kMaxRedirectDepth) {
        resp.success = false;
        resp.status_code = 310; // Too many redirects
        return resp;
    }

    auto parts = parse_url(url);
    if (!parts) {
        resp.success = false;
        resp.status_code = -1;
        resp.status_message = "Invalid URL";
        return resp;
    }

    // Resolve hostname
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;

    auto port_str = std::to_string(parts->port);
    int gai_err = getaddrinfo(parts->host.c_str(), port_str.c_str(), &hints, &result);
    if (gai_err != 0) {
        resp.success = false;
        resp.status_code = -2;
        resp.status_message = fmt::format("DNS resolution failed: {}", gai_strerror(gai_err));
        return resp;
    }

    // Create socket and connect
    int sock = -1;
    for (auto* rp = result; rp != nullptr; rp = rp->ai_next) {
#if defined(_WIN32)
        sock = static_cast<int>(::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol));
#else
        sock = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
#endif
        if (sock < 0) continue;

        // Set timeout
#if defined(_WIN32)
        DWORD to = static_cast<DWORD>(timeout_ms);
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&to), sizeof(to));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char*>(&to), sizeof(to));
#else
        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

        if (::connect(sock, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0) {
            break; // Connected
        }

#if defined(_WIN32)
        ::closesocket(sock);
#else
        ::close(sock);
#endif
        sock = -1;
    }

    freeaddrinfo(result);

    if (sock < 0) {
        resp.success = false;
        resp.status_code = -3;
        resp.status_message = "Connection failed";
        return resp;
    }

    // Build and send request
    headers.insert({"Host", parts->host});
    auto req = build_request("GET", parts->host, parts->path, headers);
#if defined(_WIN32)
    ::send(sock, req.c_str(), static_cast<int>(req.size()), 0);
#else
    ::send(sock, req.c_str(), req.size(), 0);
#endif

    // Read response
    std::vector<char> buffer(kDownloadBufferSize);
    std::string raw_response;
    bool headers_parsed = false;
    std::string header_block;
    size_t content_length = 0;
    size_t body_bytes_read = 0;

    while (true) {
        if (!socket_ready(sock, timeout_ms)) {
            resp.success = false;
            resp.status_code = -4;
            resp.status_message = "Timeout reading response";
#if defined(_WIN32)
            ::closesocket(sock);
#else
            ::close(sock);
#endif
            return resp;
        }

#if defined(_WIN32)
        int bytes = ::recv(sock, buffer.data(),
                           static_cast<int>(buffer.size()), 0);
#else
        ssize_t bytes = ::recv(sock, buffer.data(), buffer.size(), 0);
#endif
        if (bytes <= 0) break;

        raw_response.append(buffer.data(), static_cast<size_t>(bytes));

        if (!headers_parsed) {
            auto header_end = raw_response.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                header_block = raw_response.substr(0, header_end);
                parse_response_headers(header_block, resp);

                // Determine content length
                auto cl_it = resp.headers.find("Content-Length");
                if (cl_it != resp.headers.end()) {
                    content_length = std::stoull(cl_it->second);
                }

                // Move body data
                auto body_start = header_end + 4;
                resp.body = raw_response.substr(body_start);
                body_bytes_read = resp.body.size();

                headers_parsed = true;

                // Handle redirects
                if (resp.status_code >= 301 && resp.status_code <= 308) {
                    auto loc = resp.headers.find("Location");
#if defined(_WIN32)
                    ::closesocket(sock);
#else
                    ::close(sock);
#endif
                    if (loc != resp.headers.end()) {
                        return get(loc->second, headers, timeout_ms,
                                   redirect_depth + 1);
                    }
                    resp.success = false;
                    return resp;
                }
            }
        } else {
            body_bytes_read += static_cast<size_t>(bytes);
        }

        // If we have Content-Length and we've read all the body, stop
        if (headers_parsed && content_length > 0 &&
            body_bytes_read >= content_length) {
            // Append remaining data that arrived with headers
            auto remaining = raw_response.find("\r\n\r\n");
            if (remaining != std::string::npos) {
                resp.body = raw_response.substr(remaining + 4);
            }
            break;
        }
    }

#if defined(_WIN32)
    ::closesocket(sock);
#else
    ::close(sock);
#endif

    resp.success = (resp.status_code >= 200 && resp.status_code < 300);
    return resp;
}

/// Perform a HEAD request to get file size and support for resume.
[[nodiscard]] inline Response head(std::string_view url,
                                   int timeout_ms = 15000) {
    auto parts = parse_url(url);
    if (!parts) return {};

    int sock = -1;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    auto port_str = std::to_string(parts->port);
    getaddrinfo(parts->host.c_str(), port_str.c_str(), &hints, &result);

    for (auto* rp = result; rp != nullptr; rp = rp->ai_next) {
#if defined(_WIN32)
        sock = static_cast<int>(::socket(rp->ai_family, rp->ai_socktype,
                                         rp->ai_protocol));
#else
        sock = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
#endif
        if (sock < 0) continue;
        if (::connect(sock, rp->ai_addr,
                      static_cast<int>(rp->ai_addrlen)) == 0) break;
#if defined(_WIN32)
        ::closesocket(sock);
#else
        ::close(sock);
#endif
        sock = -1;
    }
    freeaddrinfo(result);

    if (sock < 0) return {};

    auto req = build_request("HEAD", parts->host, parts->path);
#if defined(_WIN32)
    ::send(sock, req.c_str(), static_cast<int>(req.size()), 0);
#else
    ::send(sock, req.c_str(), req.size(), 0);
#endif

    std::vector<char> buf(4096);
    std::string raw;
    int total = 0;
    while (total < 8192) {
#if defined(_WIN32)
        int n = ::recv(sock, buf.data(), static_cast<int>(buf.size()), 0);
#else
        ssize_t n = ::recv(sock, buf.data(), buf.size(), 0);
#endif

        if (n <= 0) break;
        raw.append(buf.data(), static_cast<size_t>(n));
        total += n;
        if (raw.find("\r\n\r\n") != std::string::npos) break;
    }

#if defined(_WIN32)
    ::closesocket(sock);
#else
    ::close(sock);
#endif

    Response resp;
    auto hdr_end = raw.find("\r\n\r\n");
    if (hdr_end != std::string::npos) {
        parse_response_headers(raw.substr(0, hdr_end), resp);
        resp.success = (resp.status_code >= 200 && resp.status_code < 300);
    }
    return resp;
}

/// Check if server supports Range requests (for resume).
[[nodiscard]] inline bool supports_ranges(std::string_view url) {
    auto resp = head(url);
    auto it = resp.headers.find("Accept-Ranges");
    return (it != resp.headers.end() && it->second.find("bytes") != std::string::npos);
}

/// Get remote file size via HEAD request.
[[nodiscard]] inline uint64_t remote_file_size(std::string_view url) {
    auto resp = head(url);
    auto it = resp.headers.find("Content-Length");
    if (it != resp.headers.end()) {
        return std::stoull(it->second);
    }
    return 0;
}

} // namespace http

// ============================================================================
// JSON parsing — minimal JSON parser for GitHub API responses
// ============================================================================

namespace json {

/// JSON value types.
enum class Type : uint8_t {
    Null, Bool, Number, String, Array, Object
};

/// Forward declaration.
class Value;

using Array  = std::vector<Value>;
using Object = std::map<std::string, Value>;

/// A JSON value.
class Value {
public:
    Type type = Type::Null;

    // Typed accessors
    bool                bool_val = false;
    double              num_val  = 0.0;
    std::string         str_val;
    std::vector<Value>  arr_val;
    std::map<std::string, Value> obj_val;

    Value() noexcept = default;

    explicit Value(bool v) noexcept : type(Type::Bool), bool_val(v) {}
    explicit Value(double v) noexcept : type(Type::Number), num_val(v) {}
    explicit Value(int64_t v) noexcept : type(Type::Number), num_val(static_cast<double>(v)) {}
    explicit Value(std::string v) noexcept : type(Type::String), str_val(std::move(v)) {}
    explicit Value(const char* v) : type(Type::String), str_val(v) {}
    explicit Value(Array v) noexcept : type(Type::Array), arr_val(std::move(v)) {}
    explicit Value(Object v) noexcept : type(Type::Object), obj_val(std::move(v)) {}

    // Array access
    Value& operator[](size_t i) { return arr_val.at(i); }
    Value const& operator[](size_t i) const { return arr_val.at(i); }

    // Object access
    Value& operator[](std::string const& k) { return obj_val[k]; }
    bool has(std::string const& k) const {
        return obj_val.find(k) != obj_val.end();
    }

    // Convenience getters
    [[nodiscard]] std::string get_string(std::string const& key,
                                         std::string const& def = "") const {
        auto it = obj_val.find(key);
        if (it != obj_val.end() && it->second.type == Type::String)
            return it->second.str_val;
        return def;
    }

    [[nodiscard]] int64_t get_int(std::string const& key,
                                  int64_t def = 0) const {
        auto it = obj_val.find(key);
        if (it != obj_val.end() && it->second.type == Type::Number)
            return static_cast<int64_t>(it->second.num_val);
        return def;
    }

    [[nodiscard]] bool get_bool(std::string const& key,
                                bool def = false) const {
        auto it = obj_val.find(key);
        if (it != obj_val.end() && it->second.type == Type::Bool)
            return it->second.bool_val;
        return def;
    }

    [[nodiscard]] size_t size() const {
        if (type == Type::Array)  return arr_val.size();
        if (type == Type::Object) return obj_val.size();
        return 0;
    }
};

/// Skip whitespace.
constexpr void skip_ws(std::string_view& sv) noexcept {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' ||
           sv.front() == '\n' || sv.front() == '\r')) {
        sv.remove_prefix(1);
    }
}

/// Parse a JSON string (expects opening quote already consumed or at start).
[[nodiscard]] inline std::string parse_json_string(std::string_view& sv) {
    if (sv.empty() || sv.front() != '"') return {};
    sv.remove_prefix(1); // skip opening quote
    std::string result;
    while (!sv.empty() && sv.front() != '"') {
        if (sv.front() == '\\') {
            sv.remove_prefix(1);
            if (sv.empty()) break;
            switch (sv.front()) {
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                case '/':  result += '/';  break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                case 'u': {
                    // \uXXXX — 4 hex digits
                    sv.remove_prefix(1);
                    if (sv.size() < 4) break;
                    std::string hex(sv.substr(0, 4));
                    sv.remove_prefix(4);
                    auto cp = std::stoi(hex, nullptr, 16);
                    if (cp <= 0x7F) {
                        result += static_cast<char>(cp);
                    } else if (cp <= 0x7FF) {
                        result += static_cast<char>(0xC0 | (cp >> 6));
                        result += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        result += static_cast<char>(0xE0 | (cp >> 12));
                        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        result += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    continue; // already consumed
                }
                default: result += sv.front(); break;
            }
        } else {
            result += sv.front();
        }
        sv.remove_prefix(1);
    }
    if (!sv.empty()) sv.remove_prefix(1); // closing quote
    return result;
}

// Forward
[[nodiscard]] Value parse_value(std::string_view& sv);

/// Parse a JSON array.
[[nodiscard]] inline Value parse_json_array(std::string_view& sv) {
    sv.remove_prefix(1); // skip '['
    Array arr;
    skip_ws(sv);
    while (!sv.empty() && sv.front() != ']') {
        arr.push_back(parse_value(sv));
        skip_ws(sv);
        if (!sv.empty() && sv.front() == ',') {
            sv.remove_prefix(1);
            skip_ws(sv);
        }
    }
    if (!sv.empty()) sv.remove_prefix(1); // skip ']'
    return Value(std::move(arr));
}

/// Parse a JSON object.
[[nodiscard]] inline Value parse_json_object(std::string_view& sv) {
    sv.remove_prefix(1); // skip '{'
    Object obj;
    skip_ws(sv);
    while (!sv.empty() && sv.front() != '}') {
        skip_ws(sv);
        auto key = parse_json_string(sv);
        skip_ws(sv);
        if (!sv.empty() && sv.front() == ':') {
            sv.remove_prefix(1);
            obj[key] = parse_value(sv);
        }
        skip_ws(sv);
        if (!sv.empty() && sv.front() == ',') {
            sv.remove_prefix(1);
        }
    }
    if (!sv.empty()) sv.remove_prefix(1); // skip '}'
    return Value(std::move(obj));
}

/// Parse any JSON value.
[[nodiscard]] inline Value parse_value(std::string_view& sv) {
    skip_ws(sv);
    if (sv.empty()) return Value{};

    switch (sv.front()) {
        case '"': return Value{parse_json_string(sv)};
        case '[': return parse_json_array(sv);
        case '{': return parse_json_object(sv);
        case 't':
            if (sv.substr(0, 4) == "true") {
                sv.remove_prefix(4);
                return Value{true};
            }
            break;
        case 'f':
            if (sv.substr(0, 5) == "false") {
                sv.remove_prefix(5);
                return Value{false};
            }
            break;
        case 'n':
            if (sv.substr(0, 4) == "null") {
                sv.remove_prefix(4);
                return Value{};
            }
            break;
        default: {
            // Number
            auto start = sv.data();
            char* end = nullptr;
            double num = std::strtod(start, &end);
            if (end != start) {
                sv.remove_prefix(static_cast<size_t>(end - start));
                return Value{num};
            }
            break;
        }
    }
    // Unrecognized — consume one char to avoid infinite loop
    sv.remove_prefix(1);
    return Value{};
}

/// Parse a complete JSON string.
[[nodiscard]] inline Value parse(std::string_view input) {
    auto sv = input;
    return parse_value(sv);
}

} // namespace json

// ============================================================================
// SHA256 — minimal SHA-256 implementation for checksum verification
// ============================================================================

namespace crypto {

/// SHA-256 context.
class Sha256 {
public:
    Sha256() noexcept { reset(); }

    void update(const void* data, size_t len) noexcept {
        auto const* bytes = static_cast<uint8_t const*>(data);
        for (size_t i = 0; i < len; ++i) {
            buffer_[buf_len_++] = bytes[i];
            if (buf_len_ == 64) {
                transform(buffer_);
                bit_len_ += 512;
                buf_len_ = 0;
            }
        }
    }

    void update(std::string_view sv) noexcept {
        update(sv.data(), sv.size());
    }

    [[nodiscard]] std::string finalize() noexcept {
        // Padding
        size_t i = buf_len_;
        buffer_[i++] = 0x80;
        if (i > 56) {
            while (i < 64) buffer_[i++] = 0x00;
            transform(buffer_);
            i = 0;
            while (i < 56) buffer_[i++] = 0x00;
        } else {
            while (i < 56) buffer_[i++] = 0x00;
        }

        // Append total bit length in big-endian
        uint64_t total_bits = bit_len_ + buf_len_ * 8;
        buffer_[56] = static_cast<uint8_t>(total_bits >> 56);
        buffer_[57] = static_cast<uint8_t>(total_bits >> 48);
        buffer_[58] = static_cast<uint8_t>(total_bits >> 40);
        buffer_[59] = static_cast<uint8_t>(total_bits >> 32);
        buffer_[60] = static_cast<uint8_t>(total_bits >> 24);
        buffer_[61] = static_cast<uint8_t>(total_bits >> 16);
        buffer_[62] = static_cast<uint8_t>(total_bits >> 8);
        buffer_[63] = static_cast<uint8_t>(total_bits);
        transform(buffer_);

        // Output as hex
        std::string hex;
        hex.reserve(64);
        for (uint32_t h : hash_) {
            hex += fmt::format("{:08x}", swap_endian(h));
        }
        return hex;
    }

    void reset() noexcept {
        hash_[0] = 0x6a09e667;
        hash_[1] = 0xbb67ae85;
        hash_[2] = 0x3c6ef372;
        hash_[3] = 0xa54ff53a;
        hash_[4] = 0x510e527f;
        hash_[5] = 0x9b05688c;
        hash_[6] = 0x1f83d9ab;
        hash_[7] = 0x5be0cd19;
        bit_len_ = 0;
        buf_len_ = 0;
        buffer_.fill(0);
    }

private:
    uint32_t hash_[8];
    uint64_t bit_len_;
    size_t   buf_len_;
    std::array<uint8_t, 64> buffer_;

    static constexpr std::array<uint32_t, 64> kK = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    static constexpr uint32_t ror(uint32_t v, uint32_t n) noexcept {
        return (v >> n) | (v << (32 - n));
    }

    static constexpr uint32_t swap_endian(uint32_t v) noexcept {
        return ((v & 0xFF000000) >> 24) | ((v & 0x00FF0000) >> 8) |
               ((v & 0x0000FF00) << 8)  | ((v & 0x000000FF) << 24);
    }

    void transform(uint8_t const block[64]) noexcept {
        uint32_t w[64];

        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(block[i * 4 + 3]));
        }

        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = hash_[0], b = hash_[1], c = hash_[2], d = hash_[3];
        uint32_t e = hash_[4], f = hash_[5], g = hash_[6], h = hash_[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + kK[i] + w[i];
            uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }

        hash_[0] += a; hash_[1] += b; hash_[2] += c; hash_[3] += d;
        hash_[4] += e; hash_[5] += f; hash_[6] += g; hash_[7] += h;
    }
};

/// Compute SHA256 of a file.
[[nodiscard]] inline std::string sha256_file(std::filesystem::path const& path) {
    Sha256 sha;
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::array<char, kDownloadBufferSize> buf;
    while (file.read(buf.data(), static_cast<std::streamsize>(buf.size())) ||
           file.gcount() > 0) {
        sha.update(buf.data(), static_cast<size_t>(file.gcount()));
    }
    return sha.finalize();
}

/// Compute SHA256 of a string.
[[nodiscard]] inline std::string sha256_string(std::string_view sv) {
    Sha256 sha;
    sha.update(sv);
    return sha.finalize();
}

} // namespace crypto

// ============================================================================
// Update history log — persistent record of updates
// ============================================================================

namespace history {

/// Single update history entry.
struct Entry {
    Version     version;
    Version     previous_version;
    Channel     channel = Channel::Stable;
    bool        success = false;
    std::string error_message;
    std::chrono::system_clock::time_point timestamp;
    InstallError error_code = InstallError::None;
    uint64_t    download_size = 0;
    std::chrono::seconds download_duration{0};
    std::chrono::seconds install_duration{0};
};

/// Serialize a history entry to JSON.
[[nodiscard]] inline std::string entry_to_json(Entry const& e) {
    return fmt::format(
        R"({{"version":"{}","previous":"{}","channel":{},"success":{},"error":"{}","error_code":{},"timestamp":{},"size":{},"download_s":{},"install_s":{}}})",
        e.version.to_string(),
        e.previous_version.to_string(),
        static_cast<int>(e.channel),
        e.success ? "true" : "false",
        e.error_message,
        static_cast<int>(e.error_code),
        std::chrono::duration_cast<std::chrono::seconds>(
            e.timestamp.time_since_epoch()).count(),
        e.download_size,
        e.download_duration.count(),
        e.install_duration.count()
    );
}

/// Parse a history entry from JSON.
[[nodiscard]] inline std::optional<Entry> entry_from_json(json::Value const& v) {
    if (v.type != json::Type::Object) return std::nullopt;
    Entry e;
    auto ver_str = v.get_string("version");
    if (auto ver = Version::parse(ver_str)) e.version = *ver;
    auto prev_str = v.get_string("previous");
    if (auto ver = Version::parse(prev_str)) e.previous_version = *ver;
    e.channel = static_cast<Channel>(v.get_int("channel", 0));
    e.success = v.get_bool("success", false);
    e.error_message = v.get_string("error");
    e.error_code = static_cast<InstallError>(v.get_int("error_code", 0));
    auto ts = v.get_int("timestamp", 0);
    e.timestamp = std::chrono::system_clock::from_time_t(
        static_cast<std::time_t>(ts));
    e.download_size = static_cast<uint64_t>(v.get_int("size", 0));
    e.download_duration = std::chrono::seconds(v.get_int("download_s", 0));
    e.install_duration = std::chrono::seconds(v.get_int("install_s", 0));
    return e;
}

/// Update history manager.
class UpdateHistory {
public:
    explicit UpdateHistory(std::filesystem::path file = {})
        : file_path_(std::move(file)) {
        if (file_path_.empty()) {
            file_path_ = data_dir() / kUpdateHistoryFile;
        }
        load();
    }

    /// Add a new entry.
    void add(Entry entry) {
        std::lock_guard lock(mutex_);
        entries_.push_back(std::move(entry));
        // Keep at most 100 entries
        while (entries_.size() > 100) {
            entries_.pop_front();
        }
        save();
    }

    /// Get all entries.
    [[nodiscard]] std::vector<Entry> entries() const {
        std::shared_lock lock(mutex_);
        return {entries_.begin(), entries_.end()};
    }

    /// Get the last successful update version.
    [[nodiscard]] std::optional<Version> last_successful() const {
        std::shared_lock lock(mutex_);
        for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
            if (it->success) return it->version;
        }
        return std::nullopt;
    }

    /// Check if a version was previously attempted and failed.
    [[nodiscard]] bool has_failed(Version const& v) const {
        std::shared_lock lock(mutex_);
        for (auto const& e : entries_) {
            if (e.version == v && !e.success) return true;
        }
        return false;
    }

    /// Get failure count for a version.
    [[nodiscard]] int failure_count(Version const& v) const {
        std::shared_lock lock(mutex_);
        int count = 0;
        for (auto const& e : entries_) {
            if (e.version == v && !e.success) ++count;
        }
        return count;
    }

    /// Clear all history.
    void clear() {
        std::lock_guard lock(mutex_);
        entries_.clear();
        save();
    }

private:
    std::filesystem::path file_path_;
    std::deque<Entry> entries_;
    mutable std::shared_mutex mutex_;

    void load() {
        std::ifstream file(file_path_);
        if (!file) {
            spdlog::info("No existing update history at {}", file_path_.string());
            return;
        }
        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        auto parsed = json::parse(content);
        if (parsed.type == json::Type::Array) {
            std::lock_guard lock(mutex_);
            for (size_t i = 0; i < parsed.size(); ++i) {
                if (auto e = entry_from_json(parsed[i])) {
                    entries_.push_back(std::move(*e));
                }
            }
        }
        spdlog::info("Loaded {} update history entries", entries_.size());
    }

    void save() {
        std::ostringstream json_out;
        json_out << "[\n";
        for (size_t i = 0; i < entries_.size(); ++i) {
            json_out << "  " << entry_to_json(entries_[i]);
            if (i + 1 < entries_.size()) json_out << ',';
            json_out << '\n';
        }
        json_out << "]\n";

        // Atomic write: write to temp, then rename
        auto tmp = file_path_;
        tmp += ".tmp";
        {
            std::ofstream file(tmp);
            if (!file) {
                spdlog::error("Failed to write update history to {}", tmp.string());
                return;
            }
            file << json_out.str();
        }
        std::error_code ec;
        std::filesystem::rename(tmp, file_path_, ec);
        if (ec) {
            spdlog::warn("Failed to rename history file: {}", ec.message());
        }
    }

    static std::filesystem::path data_dir() {
#if defined(_WIN32)
        wchar_t path[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA,
                                       nullptr, 0, path))) {
            auto p = std::filesystem::path(path) / "cppdesk";
            std::filesystem::create_directories(p);
            return p;
        }
#elif defined(__APPLE__) && !TARGET_OS_IPHONE
        auto home = std::filesystem::path(getenv("HOME") ? getenv("HOME") : ".");
        auto p = home / "Library" / "Application Support" / "cppdesk";
        std::filesystem::create_directories(p);
        return p;
#elif defined(__ANDROID__)
        return std::filesystem::path("/data/data/com.cppdesk.app/files");
#else
        auto home = std::filesystem::path(getenv("HOME") ? getenv("HOME") : ".");
        auto p = home / ".local" / "share" / "cppdesk";
        std::filesystem::create_directories(p);
        return p;
#endif
    }
};

} // namespace history

// ============================================================================
// Release channel — filter releases by channel
// ============================================================================

namespace channel {

/// Determine which channel a release belongs to.
[[nodiscard]] inline Channel classify_release(json::Value const& release_json) {
    bool is_prerelease = release_json.get_bool("prerelease", false);
    bool is_draft = release_json.get_bool("draft", false);

    if (is_draft) return Channel::Nightly;

    std::string tag = release_json.get_string("tag_name");
    std::string name = release_json.get_string("name");

    // Look for channel hints in tag/name
    auto lower_tag = tag;
    std::transform(lower_tag.begin(), lower_tag.end(), lower_tag.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower_tag.find("nightly") != std::string::npos ||
        lower_name.find("nightly") != std::string::npos) {
        return Channel::Nightly;
    }
    if (lower_tag.find("beta") != std::string::npos ||
        lower_name.find("beta") != std::string::npos) {
        return Channel::Beta;
    }
    if (lower_tag.find("alpha") != std::string::npos ||
        lower_name.find("alpha") != std::string::npos ||
        lower_tag.find("rc") != std::string::npos ||
        lower_name.find("rc") != std::string::npos ||
        lower_tag.find("dev") != std::string::npos ||
        lower_name.find("dev") != std::string::npos) {
        return Channel::Beta; // Treat alpha/rc/dev as beta
    }

    // If marked as prerelease but no specific channel tag, treat as beta
    if (is_prerelease) return Channel::Beta;

    return Channel::Stable;
}

/// Check if a release is eligible for the given channel.
[[nodiscard]] inline bool is_eligible(Channel desired, Channel release_channel) {
    switch (desired) {
        case Channel::Nightly: return true; // Nightly gets everything
        case Channel::Beta:   return release_channel != Channel::Stable;
        case Channel::Stable: return release_channel == Channel::Stable;
    }
    return false;
}

/// Get the API endpoint for a given channel.
[[nodiscard]] inline std::string api_url(Channel ch) {
    switch (ch) {
        case Channel::Stable:
            return std::string(kGithubReleasesApi) + "/latest";
        case Channel::Beta:
            // Get all releases and client-side filter beta
            return std::string(kGithubReleasesApi) + "?per_page=20";
        case Channel::Nightly:
            return std::string(kGithubReleasesApi) + "?per_page=5";
    }
    return kGithubReleasesApi;
}

} // namespace channel

// ============================================================================
// UpdateChecker — GitHub Releases API integration
// ============================================================================

namespace checker {

/// Parse a GitHub release JSON object into UpdateInfo.
[[nodiscard]] inline UpdateInfo parse_release(json::Value const& release_json) {
    UpdateInfo info;
    info.tag_name     = release_json.get_string("tag_name");
    info.release_name = release_json.get_string("name");
    info.changelog    = release_json.get_string("body");
    info.is_prerelease = release_json.get_bool("prerelease", false);
    info.is_draft     = release_json.get_bool("draft", false);
    info.channel      = channel::classify_release(release_json);

    // Parse version from tag name
    if (auto ver = Version::parse(info.tag_name)) {
        info.version = *ver;
    } else {
        // Fallback: use name
        if (auto ver = Version::parse(info.release_name)) {
            info.version = *ver;
        }
    }

    // Parse published_at
    std::string pub_str = release_json.get_string("published_at");
    if (!pub_str.empty()) {
        // ISO 8601: "2024-01-15T10:30:00Z"
        std::tm tm = {};
        std::istringstream ss(pub_str);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (!ss.fail()) {
            info.published_at = std::chrono::system_clock::from_time_t(
                timegm(&tm));
        }
    }

    // Parse assets
    auto& assets_json = release_json.obj_val;
    auto assets_it = assets_json.find("assets");
    if (assets_it != assets_json.end() &&
        assets_it->second.type == json::Type::Array) {
        auto& assets_arr = assets_it->second.arr_val;

        for (auto const& asset_val : assets_arr) {
            if (asset_val.type != json::Type::Object) continue;

            std::string name = asset_val.get_string("name");
            std::string url  = asset_val.get_string("browser_download_url");
            uint64_t size    = static_cast<uint64_t>(asset_val.get_int("size", 0));

            if (name.empty() || url.empty()) continue;

            info.assets.push_back(url);
            info.asset_map[name] = url;

            // Identify platform-specific asset
            if (is_platform_asset(name)) {
                info.download_url = url;
                info.package_size = size;
            }

            // Look for checksum file
            if (name.find("sha256") != std::string::npos ||
                name.find("SHA256") != std::string::npos ||
                name.find("checksum") != std::string::npos) {
                info.checksum_url = url;
            }

            // Look for delta/diff asset
            if (name.find("delta") != std::string::npos ||
                name.find("diff") != std::string::npos ||
                name.find("patch") != std::string::npos) {
                info.delta_url = url;
                info.delta_size = size;
            }

            // Look for signature
            if (name.find(".sig") != std::string::npos ||
                name.find(".asc") != std::string::npos ||
                name.find("signature") != std::string::npos) {
                info.signature_url = url;
            }
        }
    }

    return info;
}

/// Determine if an asset name matches the current platform.
[[nodiscard]] inline bool is_platform_asset(std::string_view name) {
    auto lower = std::string(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

#if defined(_WIN32)
    return lower.find(".msi") != std::string::npos ||
           lower.find("windows") != std::string::npos ||
           lower.find("win64") != std::string::npos ||
           lower.find("win32") != std::string::npos ||
           lower.find(".exe") != std::string::npos;
#elif defined(__APPLE__)
  #if TARGET_OS_IPHONE
    return lower.find("ios") != std::string::npos ||
           lower.find(".ipa") != std::string::npos;
  #else
    return lower.find("mac") != std::string::npos ||
           lower.find("macos") != std::string::npos ||
           lower.find("darwin") != std::string::npos ||
           lower.find(".dmg") != std::string::npos ||
           lower.find(".pkg") != std::string::npos;
  #endif
#elif defined(__ANDROID__)
    return lower.find("android") != std::string::npos ||
           lower.find(".apk") != std::string::npos ||
           lower.find(".aab") != std::string::npos;
#elif defined(__linux__)
    return lower.find("linux") != std::string::npos ||
           lower.find(".deb") != std::string::npos ||
           lower.find(".rpm") != std::string::npos ||
           lower.find(".appimage") != std::string::npos ||
           lower.find(".tar.gz") != std::string::npos ||
           lower.find(".tgz") != std::string::npos;
#else
    return true; // Unknown platform, accept any
#endif
}

/// Fetch and parse releases from GitHub API.
[[nodiscard]] inline std::vector<UpdateInfo> fetch_releases(
    Channel desired_channel,
    UpdateConfig const& config) {
    std::vector<UpdateInfo> results;

    std::string url = config.custom_api_url.empty()
        ? channel::api_url(desired_channel)
        : config.custom_api_url;

    spdlog::info("Fetching releases from: {}", url);

    std::map<std::string, std::string> headers;
    headers["Accept"] = "application/vnd.github.v3+json";
    // GitHub API token can be provided via environment
    const char* token = std::getenv("GITHUB_TOKEN");
    if (token) {
        headers["Authorization"] = std::string("token ") + token;
    }

    auto resp = http::get(url, headers, 30000);
    if (!resp.success) {
        spdlog::error("Failed to fetch releases: HTTP {} {}",
                      resp.status_code, resp.status_message);
        return results;
    }

    spdlog::debug("GitHub API response size: {} bytes", resp.body.size());

    // Parse JSON response
    auto parsed = json::parse(resp.body);

    // Handle both single release (latest) and array of releases
    std::vector<json::Value> releases;
    if (parsed.type == json::Type::Array) {
        releases = std::move(parsed.arr_val);
    } else if (parsed.type == json::Type::Object) {
        releases.push_back(std::move(parsed));
    }

    for (auto& rel : releases) {
        auto info = parse_release(rel);
        if (!info.is_valid()) continue;
        if (!channel::is_eligible(desired_channel, info.channel)) continue;

        results.push_back(std::move(info));
    }

    // Sort by version (descending)
    std::sort(results.begin(), results.end(),
              [](UpdateInfo const& a, UpdateInfo const& b) {
                  return a.version > b.version;
              });

    spdlog::info("Found {} eligible releases", results.size());
    return results;
}

/// Check if an update is available.
[[nodiscard]] inline std::optional<UpdateInfo> check_for_update(
    UpdateConfig const& config,
    Version const& current_version) {
    auto releases = fetch_releases(config.channel, config);

    if (releases.empty()) {
        spdlog::info("No releases found for channel {}",
                     static_cast<int>(config.channel));
        return std::nullopt;
    }

    auto const& latest = releases.front();

    if (!config.allow_downgrade && latest.version <= current_version) {
        spdlog::info("Current version {} is up to date (latest: {})",
                     current_version.to_string(),
                     latest.version.to_string());
        return std::nullopt;
    }

    if (latest.version > current_version) {
        spdlog::info("New version available: {} (current: {})",
                     latest.version.to_string(),
                     current_version.to_string());
        return latest;
    }

    return std::nullopt;
}

} // namespace checker

// ============================================================================
// Download manager — resume support and progress callbacks
// ============================================================================

namespace download {

/// Download a file with resume support and progress callbacks.
[[nodiscard]] inline bool download_file(
    std::string_view url,
    std::filesystem::path const& destination,
    DownloadProgress& progress,
    std::function<void(DownloadProgress const&)> progress_cb = {},
    int max_retries = kMaxDownloadRetries) {

    auto parts = http::parse_url(url);
    if (!parts) {
        spdlog::error("Invalid download URL: {}", url);
        return false;
    }

    progress.stage = "connecting";
    progress.start_time = std::chrono::steady_clock::now();
    progress.last_update = progress.start_time;
    progress.total_bytes = http::remote_file_size(url);

    // Check if we can resume
    uint64_t existing_size = 0;
    if (std::filesystem::exists(destination)) {
        existing_size = std::filesystem::file_size(destination);
        if (progress.total_bytes > 0 && existing_size >= progress.total_bytes) {
            // File already complete
            spdlog::info("File already fully downloaded: {}", destination.string());
            progress.bytes_downloaded = progress.total_bytes;
            progress.is_complete = true;
            progress.stage = "done";
            if (progress_cb) progress_cb(progress);
            return true;
        }
        if (existing_size > 0) {
            progress.is_resumed = true;
            spdlog::info("Resuming download from byte {}", existing_size);
        }
    }

    int retry = 0;
    while (retry <= max_retries) {
        progress.retry_count = retry;

        if (retry > 0) {
            spdlog::warn("Download retry {}/{}", retry, max_retries);
            // Exponential backoff
            std::this_thread::sleep_for(
                std::chrono::seconds(1 << std::min(retry, 5)));
        }

        bool ok = download_attempt(url, destination, progress,
                                   progress_cb, existing_size);
        if (ok) return true;

        ++retry;
    }

    spdlog::error("Download failed after {} retries", max_retries);
    progress.stage = "failed";
    return false;
}

/// Perform a single download attempt.
[[nodiscard]] inline bool download_attempt(
    std::string_view url,
    std::filesystem::path const& destination,
    DownloadProgress& progress,
    std::function<void(DownloadProgress const&)> const& progress_cb,
    uint64_t resume_offset) {

    auto parts = http::parse_url(url);
    if (!parts) return false;

    // Resolve hostname
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    auto port_str = std::to_string(parts->port);

    int gai_err = getaddrinfo(parts->host.c_str(), port_str.c_str(),
                              &hints, &result);
    if (gai_err != 0) {
        spdlog::error("DNS resolution failed: {}", gai_strerror(gai_err));
        return false;
    }

    int sock = -1;
    for (auto* rp = result; rp != nullptr; rp = rp->ai_next) {
#if defined(_WIN32)
        sock = static_cast<int>(::socket(rp->ai_family, rp->ai_socktype,
                                         rp->ai_protocol));
#else
        sock = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
#endif
        if (sock < 0) continue;

        // Set TCP_NODELAY for faster downloads
        int flag = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
#if defined(_WIN32)
                   reinterpret_cast<const char*>(&flag), sizeof(flag));
#else
                   &flag, sizeof(flag));
#endif

        int timeout_ms = 30000;
#if defined(_WIN32)
        DWORD to = static_cast<DWORD>(timeout_ms);
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&to), sizeof(to));
#else
        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

        if (::connect(sock, rp->ai_addr,
                      static_cast<int>(rp->ai_addrlen)) == 0) {
            break;
        }

#if defined(_WIN32)
        ::closesocket(sock);
#else
        ::close(sock);
#endif
        sock = -1;
    }
    freeaddrinfo(result);

    if (sock < 0) {
        spdlog::error("Connection failed");
        return false;
    }

    // Build request with optional Range header
    std::map<std::string, std::string> headers;
    headers.insert({"Host", parts->host});
    if (resume_offset > 0) {
        headers.insert({"Range",
                        fmt::format("bytes={}-", resume_offset)});
    }

    auto req = http::build_request("GET", parts->host, parts->path, headers);
#if defined(_WIN32)
    ::send(sock, req.c_str(), static_cast<int>(req.size()), 0);
#else
    ::send(sock, req.c_str(), req.size(), 0);
#endif

    // Read response headers
    std::vector<char> buf(kDownloadBufferSize);
    std::string header_data;
    bool headers_parsed = false;
    int http_status = 0;
    uint64_t content_length = 0;

    while (!headers_parsed) {
#if defined(_WIN32)
        int n = ::recv(sock, buf.data(),
                       static_cast<int>(buf.size()), 0);
#else
        ssize_t n = ::recv(sock, buf.data(), buf.size(), 0);
#endif
        if (n <= 0) {
#if defined(_WIN32)
            ::closesocket(sock);
#else
            ::close(sock);
#endif
            spdlog::error("Failed to receive response headers");
            return false;
        }
        header_data.append(buf.data(), static_cast<size_t>(n));
        auto hdr_end = header_data.find("\r\n\r\n");
        if (hdr_end != std::string::npos) {
            // Parse status line
            auto first_line_end = header_data.find("\r\n");
            std::string status_line = header_data.substr(0, first_line_end);
            auto sp1 = status_line.find(' ');
            if (sp1 != std::string::npos) {
                auto sp2 = status_line.find(' ', sp1 + 1);
                http_status = std::stoi(status_line.substr(sp1 + 1, sp2 - sp1 - 1));
            }

            // Handle redirect
            if (http_status >= 301 && http_status <= 308) {
#if defined(_WIN32)
                ::closesocket(sock);
#else
                ::close(sock);
#endif
                auto hdr_block = header_data.substr(0, hdr_end);
                auto loc_pos = hdr_block.find("Location: ");
                if (loc_pos != std::string::npos) {
                    auto loc_start = loc_pos + 10;
                    auto loc_end = hdr_block.find("\r\n", loc_start);
                    auto redirect_url = hdr_block.substr(loc_start, loc_end - loc_start);
                    return download_attempt(redirect_url, destination,
                                            progress, progress_cb, 0);
                }
                return false;
            }

            // Parse Content-Length if present
            auto cl_pos = header_data.find("Content-Length: ");
            if (cl_pos != std::string::npos) {
                auto num_start = cl_pos + 16;
                auto num_end = header_data.find("\r\n", num_start);
                auto num_str = header_data.substr(num_start, num_end - num_start);
                content_length = std::stoull(num_str);
            }

            // Extract body remainder
            auto body_start = hdr_end + 4;
            auto body_remainder = header_data.substr(body_start);

            // Open file for writing (append mode if resuming)
            auto open_mode = (resume_offset > 0)
                ? (std::ios::binary | std::ios::app)
                : (std::ios::binary | std::ios::trunc);

            // Ensure parent directory exists
            std::filesystem::create_directories(destination.parent_path());

            std::ofstream file(destination, open_mode);
            if (!file) {
#if defined(_WIN32)
                ::closesocket(sock);
#else
                ::close(sock);
#endif
                spdlog::error("Cannot open file for writing: {}",
                              destination.string());
                return false;
            }

            // Write body remainder
            if (!body_remainder.empty()) {
                file.write(body_remainder.data(),
                           static_cast<std::streamsize>(body_remainder.size()));
                progress.bytes_downloaded = resume_offset + body_remainder.size();
            } else {
                progress.bytes_downloaded = resume_offset;
            }

            headers_parsed = true;

            if (http_status != 200 && http_status != 206) {
                spdlog::error("HTTP status {} — download failed", http_status);
                file.close();
                return false;
            }

            // Download body
            progress.stage = "downloading";
            auto last_progress_update = std::chrono::steady_clock::now();

            while (true) {
#if defined(_WIN32)
                int n = ::recv(sock, buf.data(),
                               static_cast<int>(buf.size()), 0);
#else
                ssize_t n = ::recv(sock, buf.data(), buf.size(), 0);
#endif
                if (n <= 0) {
                    if (n < 0) {
#if defined(_WIN32)
                        int err = WSAGetLastError();
                        if (err == WSAETIMEDOUT) {
                            // Retryable timeout
                            continue;
                        }
#endif
                    }
                    break; // Connection closed or error
                }

                file.write(buf.data(), n);
                progress.bytes_downloaded += static_cast<uint64_t>(n);

                // Update speed calculation
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - progress.last_update).count();
                if (elapsed >= 500) { // Update every 500ms
                    auto dt = std::chrono::duration<double>(now - progress.last_update).count();
                    auto bytes_in_period = static_cast<uint64_t>(n); // approximate
                    // Smoothed speed (EMA)
                    double instant_speed = static_cast<double>(n) / dt;
                    double alpha = 0.3;
                    progress.speed_bytes_per_sec = alpha * instant_speed +
                        (1.0 - alpha) * progress.speed_bytes_per_sec;
                    progress.last_update = now;

                    if (progress_cb) progress_cb(progress);
                }

                // Check for completion
                if (progress.total_bytes > 0 &&
                    progress.bytes_downloaded >= progress.total_bytes) {
                    break;
                }
            }

            file.close();
#if defined(_WIN32)
            ::closesocket(sock);
#else
            ::close(sock);
#endif

            // Verify completion
            if (progress.total_bytes > 0) {
                auto actual_size = std::filesystem::file_size(destination);
                if (actual_size < progress.total_bytes) {
                    spdlog::warn("Download incomplete: {}/{} bytes",
                                actual_size, progress.total_bytes);
                    progress.bytes_downloaded = actual_size;
                    return false;
                }
            }

            progress.is_complete = true;
            progress.stage = "done";
            if (progress_cb) progress_cb(progress);
            spdlog::info("Download complete: {} ({} bytes)",
                         destination.string(), progress.bytes_downloaded);
            return true;
        }
    }

#if defined(_WIN32)
    ::closesocket(sock);
#else
    ::close(sock);
#endif
    return false;
}

} // namespace download

// ============================================================================
// Package verification — SHA256 checksum
// ============================================================================

namespace verify {

/// Fetch the checksum file from the remote URL.
[[nodiscard]] inline std::optional<std::string> fetch_checksum(
    std::string_view checksum_url) {
    if (checksum_url.empty()) return std::nullopt;

    auto resp = http::get(checksum_url, {}, 15000);
    if (!resp.success) {
        spdlog::error("Failed to fetch checksum from {}", checksum_url);
        return std::nullopt;
    }
    return resp.body;
}

/// Extract the expected hash for a given filename from a checksum file.
[[nodiscard]] inline std::optional<std::string> parse_checksum_for_file(
    std::string_view checksum_content,
    std::string_view filename) {
    // Typical format: "abcdef1234567890...  filename.ext"
    std::istringstream stream(std::string(checksum_content));
    std::string line;
    while (std::getline(stream, line)) {
        // Remove trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Split by whitespace
        auto space1 = line.find(' ');
        auto space2 = line.find('\t');

        auto delim = (space1 != std::string::npos && space2 != std::string::npos)
            ? std::min(space1, space2)
            : std::max(space1, space2);

        if (delim == std::string::npos) continue;

        std::string hash = line.substr(0, delim);
        // Trim hash
        while (!hash.empty() && hash.back() == ' ') hash.pop_back();

        std::string name = line.substr(delim + 1);
        // Trim name, handle " *filename" or "  filename" formats
        auto name_start = name.find_first_not_of(" *");
        if (name_start != std::string::npos) {
            name = name.substr(name_start);
        }

        // Extract just the basename for comparison
        auto fn_base = std::filesystem::path(filename).filename().string();
        auto hash_fn_base = std::filesystem::path(name).filename().string();

        if (hash_fn_base == fn_base || name == filename) {
            return hash;
        }
    }
    return std::nullopt;
}

/// Verify a downloaded package against its SHA256 checksum.
[[nodiscard]] inline bool verify_package(
    std::filesystem::path const& package_path,
    UpdateInfo const& info,
    UpdateConfig const& config) {

    if (!config.verify_checksum) {
        spdlog::info("Checksum verification disabled");
        return true;
    }

    spdlog::info("Verifying package: {}", package_path.string());

    // Compute local SHA256
    auto computed = crypto::sha256_file(package_path);
    if (computed.empty()) {
        spdlog::error("Failed to compute SHA256 for {}", package_path.string());
        return false;
    }

    spdlog::debug("Computed SHA256: {}", computed);

    // If checksum is already in UpdateInfo, compare directly
    if (!info.checksum.empty()) {
        bool match = (computed == info.checksum);
        if (!match) {
            spdlog::error("Checksum mismatch!\n  Expected: {}\n  Got:      {}",
                          info.checksum, computed);
        }
        return match;
    }

    // Otherwise fetch checksum file and parse
    auto checksum_content = fetch_checksum(info.checksum_url);
    if (!checksum_content) {
        spdlog::warn("Could not fetch checksum file; skipping verification");
        return true; // Graceful degradation
    }

    auto expected = parse_checksum_for_file(
        *checksum_content,
        package_path.filename().string());

    if (!expected) {
        spdlog::warn("Could not find checksum for {} in checksum file",
                     package_path.filename().string());
        return true; // Graceful degradation
    }

    bool match = (computed == *expected);
    if (!match) {
        spdlog::error("Checksum mismatch!\n  Expected: {}\n  Got:      {}",
                      *expected, computed);
    } else {
        spdlog::info("Checksum verified OK");
    }

    return match;
}

} // namespace verify

// ============================================================================
// Delta/binary diff updates
// ============================================================================

namespace delta {

/// Apply a delta update.
/// In a real implementation, this would use bsdiff/bspatch or courgette.
/// Here we implement a simple file-level diff strategy.
[[nodiscard]] inline bool apply_delta(
    std::filesystem::path const& current_install_dir,
    std::filesystem::path const& delta_package_path,
    std::filesystem::path const& output_dir) {

    spdlog::info("Applying delta update...");

    // Ensure output directory exists
    std::filesystem::create_directories(output_dir);

    // Read delta manifest (JSON file listing changed files)
    std::ifstream manifest(delta_package_path, std::ios::binary);
    if (!manifest) {
        spdlog::error("Cannot open delta package: {}",
                      delta_package_path.string());
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(manifest)),
                         std::istreambuf_iterator<char>());

    auto delta_data = json::parse(content);

    if (delta_data.type != json::Type::Object) {
        spdlog::error("Invalid delta package format");
        return false;
    }

    // Process delta operations
    // Expected format: {"version": "2.0.0", "operations": [...]}
    auto ops_it = delta_data.obj_val.find("operations");
    if (ops_it == delta_data.obj_val.end() ||
        ops_it->second.type != json::Type::Array) {
        spdlog::error("No operations found in delta package");
        return false;
    }

    auto const& delta_dir = delta_package_path.parent_path();

    size_t applied = 0;
    size_t failed = 0;

    for (size_t i = 0; i < ops_it->second.size(); ++i) {
        auto& op = ops_it->second[i];
        if (op.type != json::Type::Object) continue;

        std::string op_type   = op.get_string("type");
        std::string file_path = op.get_string("path");

        if (op_type == "add" || op_type == "replace") {
            // Copy the new file from delta package
            auto src = delta_dir / file_path;
            auto dst = output_dir / file_path;

            std::filesystem::create_directories(dst.parent_path());
            std::error_code ec;
            std::filesystem::copy_file(src, dst,
                std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                spdlog::warn("Failed to {} file {}: {}",
                             op_type, file_path, ec.message());
                ++failed;
            } else {
                ++applied;
            }
        } else if (op_type == "delete") {
            // Remove the file from output
            auto dst = output_dir / file_path;
            std::error_code ec;
            std::filesystem::remove(dst, ec);
            ++applied;
        } else if (op_type == "patch") {
            // Apply binary patch
            auto src_patch = delta_dir / (file_path + ".patch");
            auto src_orig  = current_install_dir / file_path;
            auto dst       = output_dir / file_path;

            if (!apply_binary_patch(src_orig, src_patch, dst)) {
                ++failed;
            } else {
                ++applied;
            }
        }
    }

    spdlog::info("Delta applied: {} operations, {} succeeded, {} failed",
                 ops_it->second.size(), applied, failed);
    return failed == 0;
}

/// Apply a simple binary patch (XOR difference + size header).
/// This is a simplified version; production would use bsdiff.
[[nodiscard]] inline bool apply_binary_patch(
    std::filesystem::path const& original,
    std::filesystem::path const& patch_file,
    std::filesystem::path const& output) {

    if (!std::filesystem::exists(original)) {
        spdlog::warn("Original file not found for patching: {}",
                     original.string());
        return false;
    }
    if (!std::filesystem::exists(patch_file)) {
        spdlog::warn("Patch file not found: {}", patch_file.string());
        return false;
    }

    // Read original
    std::ifstream orig(original, std::ios::binary | std::ios::ate);
    auto orig_size = static_cast<size_t>(orig.tellg());
    orig.seekg(0);
    std::vector<uint8_t> orig_data(orig_size);
    orig.read(reinterpret_cast<char*>(orig_data.data()),
              static_cast<std::streamsize>(orig_size));

    // Read patch
    std::ifstream patch(patch_file, std::ios::binary | std::ios::ate);
    auto patch_size = static_cast<size_t>(patch.tellg());
    patch.seekg(0);
    std::vector<uint8_t> patch_data(patch_size);
    patch.read(reinterpret_cast<char*>(patch_data.data()),
               static_cast<std::streamsize>(patch_size));

    // Simple format: 4-byte new size (big-endian), then XOR diff
    if (patch_size < 4) return false;

    uint32_t new_size = (static_cast<uint32_t>(patch_data[0]) << 24) |
                        (static_cast<uint32_t>(patch_data[1]) << 16) |
                        (static_cast<uint32_t>(patch_data[2]) << 8)  |
                        (static_cast<uint32_t>(patch_data[3]));

    std::vector<uint8_t> output_data(new_size, 0);
    size_t copy_len = std::min(orig_size, static_cast<size_t>(new_size));
    for (size_t i = 0; i < copy_len; ++i) {
        if (i + 4 < patch_size) {
            output_data[i] = orig_data[i] ^ patch_data[i + 4];
        } else {
            output_data[i] = orig_data[i];
        }
    }

    // Write output
    std::filesystem::create_directories(output.parent_path());
    std::ofstream out(output, std::ios::binary);
    out.write(reinterpret_cast<const char*>(output_data.data()),
              static_cast<std::streamsize>(new_size));
    out.close();

    return out.good();
}

/// Check if a delta update is available and applicable.
[[nodiscard]] inline bool can_use_delta(
    UpdateInfo const& info,
    Version const& current_version) {
    return info.has_delta() &&
           std::filesystem::exists(
               std::filesystem::path(kDownloadTempDir) / "delta.json");
}

} // namespace delta

// ============================================================================
// Platform-specific installation
// ============================================================================

namespace install {

/// Get temp download directory.
[[nodiscard]] inline std::filesystem::path download_dir(UpdateConfig const& config) {
    if (!config.temp_directory.empty()) return config.temp_directory;
    auto base = std::filesystem::temp_directory_path() / kDownloadTempDir;
    std::filesystem::create_directories(base);
    return base;
}

/// Get backup directory for rollback.
[[nodiscard]] inline std::filesystem::path backup_dir() {
    auto base = std::filesystem::temp_directory_path() / kBackupDir;
    std::filesystem::create_directories(base);
    return base;
}

// ============================================================================
// Windows installation
// ============================================================================
#if defined(_WIN32)

/// Install MSI package on Windows.
[[nodiscard]] inline InstallResult install_msi(
    std::filesystem::path const& msi_path,
    UpdateConfig const& config,
    UpdateMode mode) {

    InstallResult result;
    result.timestamp = std::chrono::system_clock::now();

    spdlog::info("Installing MSI: {}", msi_path.string());

    // Build msiexec command line
    std::wstring msi_path_w = msi_path.wstring();
    std::wstring cmd_line;

    if (mode == UpdateMode::Silent || mode == UpdateMode::Scheduled) {
        cmd_line = L"msiexec /i \"" + msi_path_w +
                   L"\" /quiet /norestart /log \"";
        auto log_path = download_dir(config) / "msi_install.log";
        cmd_line += log_path.wstring() + L"\"";
    } else {
        cmd_line = L"msiexec /i \"" + msi_path_w + L"\"";
    }

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(SHELLEXECUTEINFOW);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"runas"; // Request elevation
    sei.lpFile = L"msiexec.exe";
    sei.lpParameters = cmd_line.c_str() + std::wstring(L"msiexec ").size();
    sei.nShow = SW_HIDE;

    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) {
            WaitForSingleObject(sei.hProcess, INFINITE);
            DWORD exit_code = 0;
            GetExitCodeProcess(sei.hProcess, &exit_code);
            CloseHandle(sei.hProcess);

            result.exit_code = static_cast<int>(exit_code);
            result.success = (exit_code == 0 || exit_code == ERROR_SUCCESS_REBOOT_REQUIRED);
            result.requires_restart = (exit_code == ERROR_SUCCESS_REBOOT_REQUIRED);

            if (!result.success) {
                result.error = InstallError::InstallerFailed;
                result.error_message = fmt::format("MSI installer exited with code {}",
                                                   exit_code);
            }
        }
    } else {
        DWORD err = GetLastError();
        result.error = InstallError::PermissionDenied;
        result.error_message = fmt::format("ShellExecuteEx failed: {}", err);
    }

    return result;
}

/// Check available disk space for installation.
[[nodiscard]] inline bool check_disk_space(uint64_t required_bytes,
                                           std::filesystem::path const& path) {
    ULARGE_INTEGER free_bytes_available;
    ULARGE_INTEGER total_bytes;
    ULARGE_INTEGER total_free_bytes;

    auto path_str = path.wstring();
    if (GetDiskFreeSpaceExW(path_str.c_str(),
                            &free_bytes_available,
                            &total_bytes,
                            &total_free_bytes)) {
        return free_bytes_available.QuadPart >=
               static_cast<LONGLONG>(required_bytes * 2); // 2x safety
    }
    return true; // If we can't check, assume OK
}

/// Create a backup for rollback on Windows.
[[nodiscard]] inline std::optional<std::string> create_backup(
    std::filesystem::path const& install_dir) {

    auto bk_dir = backup_dir();
    auto timestamp_str = std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    auto backup = bk_dir / ("backup_" + timestamp_str);
    std::filesystem::create_directories(backup);

    // Copy current installation files
    std::error_code ec;
    if (std::filesystem::exists(install_dir)) {
        for (auto const& entry :
             std::filesystem::recursive_directory_iterator(install_dir, ec)) {
            auto rel = std::filesystem::relative(entry.path(), install_dir, ec);
            if (ec) continue;
            auto dst = backup / rel;
            if (entry.is_directory()) {
                std::filesystem::create_directories(dst, ec);
            } else {
                std::filesystem::copy_file(entry.path(), dst,
                    std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
    }

    spdlog::info("Backup created at: {}", backup.string());
    return backup.string();
}

/// Perform rollback on Windows.
[[nodiscard]] inline bool rollback(std::filesystem::path const& install_dir,
                                   std::string const& backup_path) {
    spdlog::info("Rolling back from backup: {}", backup_path);

    if (!std::filesystem::exists(backup_path)) {
        spdlog::error("Backup not found at {}", backup_path);
        return false;
    }

    std::error_code ec;
    // Remove current installation
    std::filesystem::remove_all(install_dir, ec);
    std::filesystem::create_directories(install_dir, ec);

    // Restore from backup
    for (auto const& entry :
         std::filesystem::recursive_directory_iterator(backup_path, ec)) {
        auto rel = std::filesystem::relative(entry.path(), backup_path, ec);
        if (ec) continue;
        auto dst = install_dir / rel;
        if (entry.is_directory()) {
            std::filesystem::create_directories(dst, ec);
        } else {
            std::filesystem::copy_file(entry.path(), dst,
                std::filesystem::copy_options::overwrite_existing, ec);
        }
    }

    return !ec;
}

#elif defined(__APPLE__) && !TARGET_OS_IPHONE
// ============================================================================
// macOS installation
// ============================================================================

/// Install DMG package on macOS.
[[nodiscard]] inline InstallResult install_dmg(
    std::filesystem::path const& dmg_path,
    UpdateConfig const& config,
    UpdateMode mode) {

    InstallResult result;
    result.timestamp = std::chrono::system_clock::now();

    spdlog::info("Installing DMG: {}", dmg_path.string());

    // Mount the DMG
    auto mount_point = std::filesystem::temp_directory_path() / "cppdesk_mount";
    std::filesystem::create_directories(mount_point);

    std::string mount_cmd = fmt::format(
        "hdiutil attach '{}' -mountpoint '{}' -nobrowse -quiet",
        dmg_path.string(), mount_point.string());
    int mount_result = std::system(mount_cmd.c_str());

    if (mount_result != 0) {
        result.error = InstallError::InstallerFailed;
        result.error_message = "Failed to mount DMG";
        return result;
    }

    // Find .app bundle in mounted DMG
    std::filesystem::path app_bundle;
    std::error_code ec;
    for (auto const& entry :
         std::filesystem::directory_iterator(mount_point, ec)) {
        if (entry.path().extension() == ".app") {
            app_bundle = entry.path();
            break;
        }
    }

    if (app_bundle.empty()) {
        // Try PKG installer
        for (auto const& entry :
             std::filesystem::directory_iterator(mount_point, ec)) {
            if (entry.path().extension() == ".pkg") {
                return install_pkg(entry.path(), config, mode);
            }
        }
        result.error = InstallError::PackageCorrupted;
        result.error_message = "No .app or .pkg found in DMG";
        // Unmount
        std::system(fmt::format("hdiutil detach '{}' -quiet",
                                mount_point.string()).c_str());
        return result;
    }

    // Determine target
    auto target_dir = config.install_directory.empty()
        ? std::filesystem::path("/Applications")
        : std::filesystem::path(config.install_directory);
    auto target = target_dir / app_bundle.filename();

    // Backup existing if present
    if (std::filesystem::exists(target)) {
        auto backup = target_dir / (app_bundle.filename().string() + ".backup");
        if (std::filesystem::exists(backup)) {
            std::filesystem::remove_all(backup, ec);
        }
        std::filesystem::rename(target, backup, ec);
        result.backup_path = backup.string();
    }

    // Copy new app bundle
    std::filesystem::copy(app_bundle, target,
        std::filesystem::copy_options::recursive |
        std::filesystem::copy_options::overwrite_existing, ec);

    if (ec) {
        result.error = InstallError::InstallerFailed;
        result.error_message = fmt::format("Failed to copy app: {}", ec.message());
    } else {
        result.success = true;
        result.requires_restart = true;
    }

    // Unmount DMG
    std::system(fmt::format("hdiutil detach '{}' -quiet",
                            mount_point.string()).c_str());

    return result;
}

/// Install PKG on macOS.
[[nodiscard]] inline InstallResult install_pkg(
    std::filesystem::path const& pkg_path,
    UpdateConfig const& config,
    UpdateMode mode) {

    InstallResult result;
    result.timestamp = std::chrono::system_clock::now();

    std::string cmd;
    if (mode == UpdateMode::Silent || mode == UpdateMode::Scheduled) {
        cmd = fmt::format("sudo installer -pkg '{}' -target / -verboseR",
                          pkg_path.string());
    } else {
        cmd = fmt::format("open '{}'", pkg_path.string());
    }

    int rc = std::system(cmd.c_str());
    result.exit_code = rc;
    result.success = (rc == 0);
    result.requires_restart = true;

    if (!result.success) {
        result.error = InstallError::InstallerFailed;
        result.error_message = fmt::format("Installer exited with code {}", rc);
    }

    return result;
}

/// Check available disk space.
[[nodiscard]] inline bool check_disk_space(uint64_t required_bytes,
                                           std::filesystem::path const& dir) {
    struct statfs stats;
    if (statfs(dir.c_str(), &stats) == 0) {
        uint64_t free = static_cast<uint64_t>(stats.f_bsize) *
                        static_cast<uint64_t>(stats.f_bavail);
        return free >= required_bytes * 2;
    }
    return true;
}

/// Create backup.
[[nodiscard]] inline std::optional<std::string> create_backup(
    std::filesystem::path const& install_dir) {
    auto bk_dir = backup_dir();
    auto ts = std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    auto backup = bk_dir / ("backup_" + ts);
    std::filesystem::create_directories(backup);

    std::string cmd = fmt::format("cp -a '{}' '{}'",
                                  install_dir.string(), backup.string());
    std::system(cmd.c_str());

    return backup.string();
}

/// Rollback on macOS.
[[nodiscard]] inline bool rollback(std::filesystem::path const& install_dir,
                                   std::string const& backup_path) {
    std::error_code ec;
    std::filesystem::remove_all(install_dir, ec);

    std::string cmd = fmt::format("cp -a '{}' '{}'",
                                  backup_path, install_dir.string());
    return std::system(cmd.c_str()) == 0;
}

#elif defined(__ANDROID__)
// ============================================================================
// Android installation
// ============================================================================

/// Install APK on Android (requires system permissions or root).
[[nodiscard]] inline InstallResult install_apk(
    std::filesystem::path const& apk_path,
    UpdateConfig const& config,
    UpdateMode mode) {

    InstallResult result;
    result.timestamp = std::chrono::system_clock::now();

    spdlog::info("Installing APK: {}", apk_path.string());

    // On Android, we use PackageInstaller API via JNI.
    // For a command-line approach (requires root or system app):
    std::string cmd = fmt::format(
        "pm install -r {}", apk_path.string());

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.error = InstallError::InstallerFailed;
        result.error_message = "Failed to execute pm install";
        return result;
    }

    char buf[256];
    std::string output;
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }
    int rc = pclose(pipe);

    result.exit_code = rc;
    result.success = (rc == 0);
    result.requires_restart = true;

    if (!result.success) {
        result.error = InstallError::InstallerFailed;
        result.error_message = output;
    }

    return result;
}

/// Check disk space on Android.
[[nodiscard]] inline bool check_disk_space(uint64_t required_bytes,
                                           std::filesystem::path const&) {
    struct statfs stats;
    if (statfs("/data", &stats) == 0) {
        uint64_t free = static_cast<uint64_t>(stats.f_bsize) *
                        static_cast<uint64_t>(stats.f_bavail);
        return free >= required_bytes * 2;
    }
    return true;
}

/// Create backup on Android.
[[nodiscard]] inline std::optional<std::string> create_backup(
    std::filesystem::path const& install_dir) {
    auto bk_dir = backup_dir();
    auto ts = std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    auto backup = bk_dir / ("backup_" + ts);
    std::filesystem::create_directories(backup);

    std::string cmd = fmt::format("cp -a '{}' '{}'",
                                  install_dir.string(), backup.string());
    std::system(cmd.c_str());
    return backup.string();
}

/// Rollback on Android.
[[nodiscard]] inline bool rollback(std::filesystem::path const& install_dir,
                                   std::string const& backup_path) {
    std::error_code ec;
    std::filesystem::remove_all(install_dir, ec);

    std::string cmd = fmt::format("cp -a '{}' '{}'",
                                  backup_path, install_dir.string());
    return std::system(cmd.c_str()) == 0;
}

#else
// ============================================================================
// Linux installation
// ============================================================================

/// Install DEB package on Linux.
[[nodiscard]] inline InstallResult install_deb(
    std::filesystem::path const& deb_path,
    UpdateConfig const& config,
    UpdateMode mode) {

    InstallResult result;
    result.timestamp = std::chrono::system_clock::now();

    spdlog::info("Installing DEB: {}", deb_path.string());

    std::string cmd;
    if (mode == UpdateMode::Silent || mode == UpdateMode::Scheduled) {
        // Non-interactive DEB install
        cmd = fmt::format(
            "DEBIAN_FRONTEND=noninteractive dpkg -i '{}' 2>&1",
            deb_path.string());
    } else {
        cmd = fmt::format("pkexec dpkg -i '{}' 2>&1", deb_path.string());
    }

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.error = InstallError::InstallerFailed;
        result.error_message = "Failed to execute dpkg";
        return result;
    }

    char buf[512];
    std::string output;
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }
    int rc = pclose(pipe);

    result.exit_code = rc;
    result.success = (rc == 0);
    result.requires_restart = true;

    // Fix broken dependencies if any
    if (rc != 0 && output.find("dependency") != std::string::npos) {
        std::system("apt-get install -f -y 2>&1");
    }

    if (!result.success) {
        result.error = InstallError::InstallerFailed;
        result.error_message = output;
    }

    return result;
}

/// Install RPM package on Linux.
[[nodiscard]] inline InstallResult install_rpm(
    std::filesystem::path const& rpm_path,
    UpdateConfig const& config,
    UpdateMode mode) {

    InstallResult result;
    result.timestamp = std::chrono::system_clock::now();

    spdlog::info("Installing RPM: {}", rpm_path.string());

    std::string cmd;
    if (mode == UpdateMode::Silent || mode == UpdateMode::Scheduled) {
        cmd = fmt::format("rpm -Uvh --quiet '{}' 2>&1", rpm_path.string());
    } else {
        cmd = fmt::format("pkexec rpm -Uvh '{}' 2>&1", rpm_path.string());
    }

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.error = InstallError::InstallerFailed;
        result.error_message = "Failed to execute rpm";
        return result;
    }

    char buf[512];
    std::string output;
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }
    int rc = pclose(pipe);

    result.exit_code = rc;
    result.success = (rc == 0);
    result.requires_restart = true;

    if (!result.success) {
        result.error = InstallError::InstallerFailed;
        result.error_message = output;
    }

    return result;
}

/// Install AppImage on Linux (copy and set executable).
[[nodiscard]] inline InstallResult install_appimage(
    std::filesystem::path const& appimage_path,
    UpdateConfig const& config,
    UpdateMode mode) {

    InstallResult result;
    result.timestamp = std::chrono::system_clock::now();

    spdlog::info("Installing AppImage: {}", appimage_path.string());

    auto target_dir = config.install_directory.empty()
        ? std::filesystem::path(
              std::string(getenv("HOME") ? getenv("HOME") : ".")) / "Applications"
        : std::filesystem::path(config.install_directory);

    std::filesystem::create_directories(target_dir);

    auto target = target_dir / "cppdesk.AppImage";

    // Backup existing
    if (std::filesystem::exists(target)) {
        auto bk = target_dir / "cppdesk.AppImage.backup";
        std::error_code ec;
        std::filesystem::rename(target, bk, ec);
        result.backup_path = bk.string();
    }

    std::error_code ec;
    std::filesystem::copy_file(appimage_path, target,
        std::filesystem::copy_options::overwrite_existing, ec);

    if (ec) {
        result.error = InstallError::InstallerFailed;
        result.error_message = fmt::format("Failed to copy AppImage: {}",
                                           ec.message());
        return result;
    }

    // Make executable
    std::filesystem::permissions(target,
        std::filesystem::perms::owner_exec |
        std::filesystem::perms::group_exec |
        std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add, ec);

    if (ec) {
        spdlog::warn("Failed to set executable permission: {}", ec.message());
    }

    result.success = true;
    result.requires_restart = true;
    return result;
}

/// Install tarball on Linux (extract to install directory).
[[nodiscard]] inline InstallResult install_tarball(
    std::filesystem::path const& tar_path,
    UpdateConfig const& config,
    UpdateMode mode) {

    InstallResult result;
    result.timestamp = std::chrono::system_clock::now();

    spdlog::info("Installing tarball: {}", tar_path.string());

    auto target_dir = config.install_directory.empty()
        ? std::filesystem::path(
              std::string(getenv("HOME") ? getenv("HOME") : ".")) / ".local" / "cppdesk"
        : std::filesystem::path(config.install_directory);

    // Create backup
    result.backup_path = create_backup(target_dir).value_or("");

    // Remove old installation
    std::error_code ec;
    std::filesystem::remove_all(target_dir, ec);
    std::filesystem::create_directories(target_dir, ec);

    // Extract
    std::string cmd = fmt::format(
        "tar -xzf '{}' -C '{}' 2>&1",
        tar_path.string(), target_dir.string());

    int rc = std::system(cmd.c_str());
    result.exit_code = rc;
    result.success = (rc == 0);
    result.requires_restart = true;

    if (!result.success) {
        result.error = InstallError::InstallerFailed;
        result.error_message = fmt::format("tar extract failed with code {}", rc);
    }

    return result;
}

/// Determine package type and install on Linux.
[[nodiscard]] inline InstallResult install_linux(
    std::filesystem::path const& package_path,
    UpdateConfig const& config,
    UpdateMode mode) {

    auto ext = package_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    auto filename = package_path.filename().string();
    std::transform(filename.begin(), filename.end(), filename.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext == ".deb") {
        // Wait for any other dpkg/apt processes
        wait_for_lock("/var/lib/dpkg/lock-frontend");
        return install_deb(package_path, config, mode);
    } else if (ext == ".rpm") {
        return install_rpm(package_path, config, mode);
    } else if (filename.find("appimage") != std::string::npos ||
               ext == ".appimage") {
        return install_appimage(package_path, config, mode);
    } else if (ext == ".gz" || ext == ".tgz" || ext == ".xz" ||
               filename.find(".tar.") != std::string::npos) {
        return install_tarball(package_path, config, mode);
    }

    InstallResult result;
    result.error = InstallError::PlatformNotSupported;
    result.error_message = fmt::format("Unknown package type: {}",
                                       package_path.string());
    return result;
}

/// Wait for dpkg/apt lock to be released.
inline void wait_for_lock(std::string_view lock_file, int timeout_sec = 30) {
    auto start = std::chrono::steady_clock::now();
    while (std::filesystem::exists(lock_file)) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_sec) {
            spdlog::warn("Timed out waiting for lock: {}", lock_file);
            break;
        }
        spdlog::debug("Waiting for lock: {} ({}s)", lock_file, elapsed);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

/// Check available disk space.
[[nodiscard]] inline bool check_disk_space(uint64_t required_bytes,
                                           std::filesystem::path const& dir) {
    struct statfs stats;
    if (statfs(dir.c_str(), &stats) == 0) {
        uint64_t free = static_cast<uint64_t>(stats.f_bsize) *
                        static_cast<uint64_t>(stats.f_bavail);
        return free >= required_bytes * 2;
    }
    return true;
}

/// Create backup.
[[nodiscard]] inline std::optional<std::string> create_backup(
    std::filesystem::path const& install_dir) {

    if (!std::filesystem::exists(install_dir)) return {};

    auto bk_dir = backup_dir();
    auto ts = std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    auto backup = bk_dir / ("backup_" + ts);
    std::filesystem::create_directories(backup);

    std::string cmd = fmt::format("cp -a '{}' '{}' 2>&1",
                                  install_dir.string(), backup.string());
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        spdlog::error("Backup failed: cp returned {}", rc);
        return std::nullopt;
    }

    return backup.string();
}

/// Rollback on Linux.
[[nodiscard]] inline bool rollback(std::filesystem::path const& install_dir,
                                   std::string const& backup_path) {
    if (backup_path.empty() || !std::filesystem::exists(backup_path)) {
        spdlog::error("No backup to rollback from");
        return false;
    }

    std::error_code ec;
    std::filesystem::remove_all(install_dir, ec);
    std::filesystem::create_directories(install_dir, ec);

    std::string cmd = fmt::format("cp -a '{}'/ '{}' 2>&1",
                                  backup_path, install_dir.string());
    int rc = std::system(cmd.c_str());
    return rc == 0;
}

#endif

// ============================================================================
// Platform-specific install dispatcher
// ============================================================================

/// Install a downloaded package for the current platform.
[[nodiscard]] inline InstallResult install_package(
    std::filesystem::path const& package_path,
    UpdateConfig const& config,
    UpdateMode mode) {

    spdlog::info("Installing package: {} (mode: {}, channel: {})",
                 package_path.string(),
                 static_cast<int>(mode),
                 static_cast<int>(config.channel));

#if defined(_WIN32)
    return install_msi(package_path, config, mode);
#elif defined(__APPLE__) && !TARGET_OS_IPHONE
    auto ext = package_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".pkg") {
        return install_pkg(package_path, config, mode);
    }
    return install_dmg(package_path, config, mode);
#elif defined(__ANDROID__)
    return install_apk(package_path, config, mode);
#else
    return install_linux(package_path, config, mode);
#endif
}

/// Check if there's enough disk space.
[[nodiscard]] inline bool has_disk_space(uint64_t required_bytes,
                                         std::filesystem::path const& path) {
    return check_disk_space(required_bytes, path);
}

/// Create a backup for rollback.
[[nodiscard]] inline std::optional<std::string> make_backup(
    std::filesystem::path const& install_dir) {
    return create_backup(install_dir);
}

/// Perform rollback.
[[nodiscard]] inline bool do_rollback(
    std::filesystem::path const& install_dir,
    std::string const& backup_path) {
    return rollback(install_dir, backup_path);
}

} // namespace install

// ============================================================================
// Self-restart after update
// ============================================================================

namespace restart {

/// Restart the application after a successful update.
void restart_application() {
    spdlog::info("Preparing to restart application...");

    // Allow pending log messages to flush
    spdlog::default_logger()->flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

#if defined(_WIN32)
    // On Windows, create a small helper batch script to restart
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);

    auto temp_dir = std::filesystem::temp_directory_path();
    auto batch_file = temp_dir / "cppdesk_restart.bat";

    std::wofstream batch(batch_file);
    batch << L"@echo off\r\n";
    batch << L"timeout /t 2 /nobreak > NUL\r\n";  // Wait for old process to exit
    batch << L"start \"\" \"" << exe_path << L"\"\r\n";
    batch << L"del \"%~f0\"\r\n";  // Self-delete
    batch.close();

    // Launch the batch script
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpFile = batch_file.c_str();
    sei.nShow = SW_HIDE;

    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) CloseHandle(sei.hProcess);
    }

    // Exit current process
    ExitProcess(0);

#elif defined(__APPLE__) && !TARGET_OS_IPHONE
    // On macOS, use a shell command to relaunch
    char exe_path[PATH_MAX];
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) == 0) {
        std::string cmd = fmt::format(
            "sleep 1 && open '{}' &", exe_path);
        std::system(cmd.c_str());
    }
    std::exit(0);

#elif defined(__ANDROID__)
    // On Android, trigger activity restart via am command
    std::system("am start -n com.cppdesk.app/.MainActivity");
    std::exit(0);

#else
    // On Linux, fork and exec
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';

        pid_t pid = fork();
        if (pid == 0) {
            // Child: wait a bit, then restart
            // Detach from parent process group
            setsid();
            std::this_thread::sleep_for(std::chrono::seconds(1));

            // Close all file descriptors
            for (int fd = 3; fd < 256; ++fd) close(fd);

            // Re-launch
            execl(exe_path, exe_path, nullptr);
            // If execl fails
            _exit(1);
        } else if (pid > 0) {
            // Parent exits
            _exit(0);
        }
    }
    std::exit(0);
#endif
}

/// Request restart after update (delayed).
void request_restart(std::chrono::seconds delay = std::chrono::seconds(3)) {
    spdlog::info("Restart scheduled in {} seconds", delay.count());

    std::thread([delay]() {
        std::this_thread::sleep_for(delay);
        restart_application();
    }).detach();
}

} // namespace restart

// ============================================================================
// UpdateScheduler — check interval, defer, auto-install window
// ============================================================================

namespace scheduler {

/// Check if current time is within the auto-install window.
[[nodiscard]] inline bool is_in_install_window(int window_start, int window_end) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
#if defined(_WIN32)
    localtime_s(&local_tm, &time_t_now);
#else
    localtime_r(&time_t_now, &local_tm);
#endif
    int hour = local_tm.tm_hour;

    if (window_start <= window_end) {
        return hour >= window_start && hour < window_end;
    } else {
        // Overnight window (e.g., 22:00 to 05:00)
        return hour >= window_start || hour < window_end;
    }
}

/// Calculate next check time.
[[nodiscard]] inline std::chrono::system_clock::time_point next_check_time(
    std::chrono::seconds interval) {
    return std::chrono::system_clock::now() + interval;
}

/// Determine if an auto-install should proceed.
[[nodiscard]] inline bool should_auto_install(UpdateConfig const& config) {
    if (!config.auto_install) return false;

    // Check if we're in the install window
    if (!is_in_install_window(config.silent_window_start,
                               config.silent_window_end)) {
        spdlog::debug("Not in auto-install window ({}:00 - {}:00)",
                      config.silent_window_start, config.silent_window_end);
        return false;
    }
    return true;
}

/// Update scheduler: manages periodic checks and deferred updates.
class UpdateScheduler {
public:
    UpdateScheduler() = default;

    explicit UpdateScheduler(UpdateConfig config)
        : config_(std::move(config)) {}

    /// Start periodic checking.
    void start(std::function<void()> check_callback) {
        if (running_) return;
        running_ = true;
        check_cb_ = std::move(check_callback);

        worker_ = std::jthread([this](std::stop_token stop_token) {
            while (!stop_token.stop_requested()) {
                auto now = std::chrono::system_clock::now();

                // Check if it's time
                {
                    std::shared_lock lock(mutex_);
                    if (now >= next_check_ && check_cb_) {
                        lock.unlock(); // Release before callback
                        spdlog::debug("Scheduler: triggering update check");
                        check_cb_();
                        std::lock_guard wlock(mutex_);
                        next_check_ = next_check_time(config_.check_interval);
                        spdlog::debug("Next check at: {}",
                                      std::chrono::duration_cast<std::chrono::seconds>(
                                          next_check_.time_since_epoch()).count());
                    }
                }

                // Also check if a deferred update is ready
                check_deferred();

                // Sleep for a bit, checking stop_token
                auto sleep_end = std::chrono::steady_clock::now() +
                                 std::chrono::seconds(10);
                while (!stop_token.stop_requested() &&
                       std::chrono::steady_clock::now() < sleep_end) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            }
        });

        spdlog::info("Update scheduler started (interval: {}s)",
                     config_.check_interval.count());
    }

    /// Stop periodic checking.
    void stop() {
        running_ = false;
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
        spdlog::info("Update scheduler stopped");
    }

    /// Defer an update to a later time.
    void defer(std::chrono::seconds duration) {
        std::lock_guard lock(mutex_);
        deferred_until_ = std::chrono::system_clock::now() + duration;
        defer_count_++;
        spdlog::info("Update deferred for {} seconds (defer count: {}/{})",
                     duration.count(), defer_count_, config_.max_defer_count);
    }

    /// Check if update is currently deferred.
    [[nodiscard]] bool is_deferred() const {
        std::shared_lock lock(mutex_);
        return std::chrono::system_clock::now() < deferred_until_;
    }

    /// Get remaining defer time.
    [[nodiscard]] std::chrono::seconds remaining_defer() const {
        std::shared_lock lock(mutex_);
        auto now = std::chrono::system_clock::now();
        if (now >= deferred_until_) return std::chrono::seconds{0};
        return std::chrono::duration_cast<std::chrono::seconds>(
            deferred_until_ - now);
    }

    /// Can the user defer again?
    [[nodiscard]] bool can_defer() const {
        std::shared_lock lock(mutex_);
        return defer_count_ < config_.max_defer_count;
    }

    /// Force check now.
    void check_now() {
        std::lock_guard lock(mutex_);
        next_check_ = std::chrono::system_clock::now();
    }

    /// Reset defer count.
    void reset_defer() {
        std::lock_guard lock(mutex_);
        defer_count_ = 0;
        deferred_until_ = std::chrono::system_clock::now();
    }

    /// Get next scheduled check time.
    [[nodiscard]] std::chrono::system_clock::time_point next_check() const {
        std::shared_lock lock(mutex_);
        return next_check_;
    }

private:
    UpdateConfig config_;
    mutable std::shared_mutex mutex_;
    std::atomic<bool> running_{false};
    std::jthread worker_;
    std::function<void()> check_cb_;
    std::chrono::system_clock::time_point next_check_{
        std::chrono::system_clock::now()};
    std::chrono::system_clock::time_point deferred_until_{
        std::chrono::system_clock::now()};
    int defer_count_ = 0;

    void check_deferred() {
        std::shared_lock lock(mutex_);
        if (deferred_until_ > std::chrono::system_clock::time_point{} &&
            std::chrono::system_clock::now() >= deferred_until_) {
            spdlog::info("Defer period ended, update can now proceed");
        }
    }
};

} // namespace scheduler

// ============================================================================
// UpdateManager — main orchestrator class
// ============================================================================

/// Central update manager: ties together all components.
class UpdateManager {
public:
    UpdateManager() : history_() {
        current_version_ = Version::parse(kCurrentVersion).value_or(Version{});
        spdlog::info("UpdateManager initialized (version: {})",
                     current_version_.to_string());
    }

    explicit UpdateManager(UpdateConfig config)
        : config_(std::move(config))
        , scheduler_(config_)
        , history_() {
        current_version_ = Version::parse(kCurrentVersion).value_or(Version{});
        spdlog::info("UpdateManager initialized (version: {}, channel: {})",
                     current_version_.to_string(),
                     static_cast<int>(config_.channel));
    }

    ~UpdateManager() {
        stop_auto_check();
    }

    // ---- Configuration ----

    void set_config(UpdateConfig config) {
        std::lock_guard lock(mutex_);
        config_ = std::move(config);
        scheduler_ = scheduler::UpdateScheduler(config_);
        if (auto_check_active_) {
            start_auto_check(); // Restart with new config
        }
    }

    [[nodiscard]] UpdateConfig const& config() const {
        std::shared_lock lock(mutex_);
        return config_;
    }

    [[nodiscard]] Version current_version() const { return current_version_; }

    // ---- Status ----

    [[nodiscard]] UpdateStatus status() const {
        return status_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::optional<UpdateInfo> latest_update() const {
        std::shared_lock lock(mutex_);
        if (latest_update_) return *latest_update_;
        return std::nullopt;
    }

    // ---- Update Check ----

    /// Check for available updates (one-shot).
    [[nodiscard]] std::optional<UpdateInfo> check_now() {
        set_status(UpdateStatus::Checking);

        if (config_.status_callback) {
            config_.status_callback("Checking for updates...");
        }

        auto update = checker::check_for_update(config_, current_version_);

        {
            std::lock_guard lock(mutex_);
            latest_update_ = update;
        }

        if (update) {
            set_status(UpdateStatus::UpdateAvailable);
            spdlog::info("Update available: {} ({})",
                         update->version.to_string(),
                         update->tag_name);

            // Auto-download if configured
            if (config_.auto_download) {
                download_and_install(*update);
            }
        } else {
            set_status(UpdateStatus::UpToDate);
            spdlog::info("Already up to date");
        }

        return update;
    }

    /// Start automatic periodic checks.
    void start_auto_check() {
        auto_check_active_ = true;

        scheduler_.start([this]() {
            this->check_now();
        });

        if (config_.status_callback) {
            config_.status_callback("Auto-update check started");
        }
    }

    /// Stop automatic periodic checks.
    void stop_auto_check() {
        auto_check_active_ = false;
        scheduler_.stop();
    }

    // ---- Download & Install ----

    /// Download and install an update.
    [[nodiscard]] InstallResult download_and_install(UpdateInfo const& info) {
        InstallResult result;
        result.previous_version = current_version_.to_string();
        result.timestamp = std::chrono::system_clock::now();

        auto start_time = std::chrono::steady_clock::now();

        // Determine mode
        UpdateMode mode = determine_mode();

        // Prompt user if needed
        if (mode == UpdateMode::Prompted && config_.prompt_callback) {
            if (!config_.prompt_callback(info)) {
                spdlog::info("User declined update");
                result.error = InstallError::PermissionDenied;
                result.error_message = "User declined";
                return result;
            }
        }

        // Check disk space
        if (info.package_size > 0) {
            auto dl_dir = install::download_dir(config_);
            if (!install::has_disk_space(info.package_size, dl_dir)) {
                result.error = InstallError::DiskSpaceInsufficient;
                result.error_message = fmt::format(
                    "Insufficient disk space for {} byte download",
                    info.package_size);
                spdlog::error(result.error_message);
                return result;
            }
        }

        // Determine download destination
        auto dl_dir = install::download_dir(config_);
        auto filename = extract_filename(info.download_url);
        auto package_path = dl_dir / filename;

        // Download
        set_status(UpdateStatus::Downloading);
        if (config_.status_callback) {
            config_.status_callback(
                fmt::format("Downloading {}...", info.version.to_string()));
        }

        DownloadProgress progress;
        bool download_ok = download::download_file(
            info.download_url, package_path, progress,
            config_.progress_callback);

        if (!download_ok) {
            result.error = InstallError::DownloadFailed;
            result.error_message = "Download failed";
            spdlog::error(result.error_message);
            log_history(info, result,
                        std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - start_time));
            set_status(UpdateStatus::Failed);
            return result;
        }

        auto download_time = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time);

        // Verify
        set_status(UpdateStatus::Verifying);
        if (config_.status_callback) {
            config_.status_callback("Verifying package...");
        }

        if (!verify::verify_package(package_path, info, config_)) {
            result.error = InstallError::ChecksumMismatch;
            result.error_message = "Package checksum verification failed";
            spdlog::error(result.error_message);
            log_history(info, result, download_time);
            set_status(UpdateStatus::Failed);
            return result;
        }

        // Create backup for rollback
        if (config_.status_callback) {
            config_.status_callback("Creating backup...");
        }
        auto target_dir = config_.install_directory.empty()
            ? get_default_install_dir()
            : std::filesystem::path(config_.install_directory);
        auto backup = install::make_backup(target_dir);
        if (backup) {
            result.backup_path = *backup;
        }

        // Install
        set_status(UpdateStatus::Installing);
        if (config_.status_callback) {
            config_.status_callback("Installing update...");
        }

        auto install_start = std::chrono::steady_clock::now();
        auto install_result = install::install_package(
            package_path, config_, mode);

        auto install_time = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - install_start);

        if (!install_result.success) {
            spdlog::error("Installation failed: {}", install_result.error_message);

            // Attempt rollback
            if (!result.backup_path.empty()) {
                spdlog::info("Rolling back...");
                set_status(UpdateStatus::Rollback);
                if (config_.status_callback) {
                    config_.status_callback("Rolling back...");
                }

                bool rollback_ok = install::do_rollback(target_dir,
                                                        result.backup_path);
                if (!rollback_ok) {
                    spdlog::error("Rollback failed!");
                    result.error = InstallError::RollbackFailed;
                }
            }

            result.error = install_result.error;
            result.error_message = install_result.error_message;
            result.requires_restart = false;
            log_history(info, result, download_time);
            set_status(UpdateStatus::Failed);
            return result;
        }

        // Success
        result.success = true;
        result.installed_version = info.version.to_string();
        result.requires_restart = install_result.requires_restart;
        result.exit_code = install_result.exit_code;

        // Update current version tracking
        current_version_ = info.version;

        set_status(UpdateStatus::Success);
        spdlog::info("Update to {} installed successfully!", info.version.to_string());

        if (config_.status_callback) {
            config_.status_callback(
                fmt::format("Update to {} complete!", info.version.to_string()));
        }

        log_history(info, result, download_time);

        // Schedule restart if needed
        if (result.requires_restart) {
            spdlog::info("Scheduling application restart...");
            if (config_.status_callback) {
                config_.status_callback("Restarting application...");
            }
            restart::request_restart(std::chrono::seconds(3));
        }

        // Clean up downloaded file (optional — keep for delta reuse)
        // std::error_code ec;
        // std::filesystem::remove(package_path, ec);

        return result;
    }

    // ---- Defer ----

    /// Defer the current update.
    void defer_update(std::chrono::seconds duration = std::chrono::hours(4)) {
        if (scheduler_.can_defer()) {
            scheduler_.defer(duration);
            set_status(UpdateStatus::Deferred);
            spdlog::info("Update deferred for {} seconds", duration.count());
        } else {
            spdlog::warn("Maximum defer count reached, cannot defer further");
        }
    }

    // ---- History ----

    /// Get update history.
    [[nodiscard]] std::vector<history::Entry> get_history() const {
        return history_.entries();
    }

private:
    UpdateConfig config_;
    scheduler::UpdateScheduler scheduler_;
    history::UpdateHistory history_;
    Version current_version_;
    std::optional<UpdateInfo> latest_update_;
    mutable std::shared_mutex mutex_;
    std::atomic<UpdateStatus> status_{UpdateStatus::Idle};
    bool auto_check_active_ = false;

    void set_status(UpdateStatus s) {
        status_.store(s, std::memory_order_release);
    }

    /// Determine the effective update mode.
    [[nodiscard]] UpdateMode determine_mode() const {
        if (config_.mode == UpdateMode::Scheduled ||
            config_.mode == UpdateMode::Silent) {
            if (scheduler::should_auto_install(config_)) {
                return UpdateMode::Silent;
            }
            return config_.mode;
        }
        return config_.mode;
    }

    /// Extract filename from URL.
    [[nodiscard]] static std::string extract_filename(
        std::string_view url) {
        auto pos = url.rfind('/');
        if (pos == std::string_view::npos) return "update_package";
        auto fn = std::string(url.substr(pos + 1));
        // Strip query parameters
        auto qpos = fn.find('?');
        if (qpos != std::string::npos) {
            fn = fn.substr(0, qpos);
        }
        if (fn.empty()) return "update_package";
        return fn;
    }

    /// Get the default installation directory for the platform.
    [[nodiscard]] static std::filesystem::path get_default_install_dir() {
#if defined(_WIN32)
        wchar_t prog_files[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES,
                                       nullptr, 0, prog_files))) {
            return std::filesystem::path(prog_files) / "cppdesk";
        }
        return "C:\\Program Files\\cppdesk";
#elif defined(__APPLE__) && !TARGET_OS_IPHONE
        return "/Applications/cppdesk.app";
#elif defined(__ANDROID__)
        return "/data/app/com.cppdesk.app";
#else
        auto home = getenv("HOME") ? getenv("HOME") : ".";
        return std::filesystem::path(home) / ".local" / "lib" / "cppdesk";
#endif
    }

    /// Log update attempt to history.
    void log_history(UpdateInfo const& info,
                     InstallResult const& result,
                     std::chrono::seconds download_time) {
        history::Entry entry;
        entry.version = info.version;
        entry.previous_version = Version::parse(
            result.previous_version).value_or(Version{});
        entry.channel = info.channel;
        entry.success = result.success;
        entry.error_message = result.error_message;
        entry.error_code = result.error;
        entry.timestamp = result.timestamp;
        entry.download_size = info.package_size;
        entry.download_duration = download_time;

        if (result.success) {
            auto install_dur = std::chrono::duration_cast<std::chrono::seconds>(
                result.timestamp.time_since_epoch()) - download_time;
            entry.install_duration = install_dur;
        }

        history_.add(std::move(entry));
    }
};

} // namespace cppdesk::updater

// ============================================================================
// Convenience free functions
// ============================================================================

namespace cppdesk::updater {

/// Global update manager instance (singleton pattern, use with care).
static std::unique_ptr<UpdateManager> g_update_manager;
static std::once_flag g_init_flag;

/// Initialize the global update manager.
inline void init_updater(UpdateConfig config = {}) {
    std::call_once(g_init_flag, [&]() {
        g_update_manager = std::make_unique<UpdateManager>(std::move(config));
    });
}

/// Get the global update manager.
inline UpdateManager& updater() {
    if (!g_update_manager) {
        init_updater();
    }
    return *g_update_manager;
}

/// Convenience: check for updates synchronously.
[[nodiscard]] inline std::optional<UpdateInfo> check_for_updates() {
    return updater().check_now();
}

/// Convenience: start automatic periodic checks.
inline void start_auto_updates() {
    updater().start_auto_check();
}

/// Convenience: stop automatic checks.
inline void stop_auto_updates() {
    updater().stop_auto_check();
}

/// Convenience: get current update status.
[[nodiscard]] inline UpdateStatus get_update_status() {
    return updater().status();
}

/// Convenience: defer current update.
inline void defer_current_update(std::chrono::seconds duration = std::chrono::hours(4)) {
    updater().defer_update(duration);
}

/// Convenience: get update history.
[[nodiscard]] inline std::vector<history::Entry> get_update_history() {
    return updater().get_history();
}

/// Convenience: get the current application version.
[[nodiscard]] inline std::string get_app_version() {
    return updater().current_version().to_string();
}

} // namespace cppdesk::updater
