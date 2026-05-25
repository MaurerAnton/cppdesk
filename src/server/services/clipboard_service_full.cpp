// clipboard_service_full.cpp — Comprehensive Clipboard Service Implementation
// Part of cppdesk remote desktop server
// C++20 | SPDLOG | namespace cppdesk::server
//
// Implements: bidirectional sync engine, format negotiation, deduplication,
// incremental file transfer, rate limiting, platform-specific integration,
// change monitoring, permission enforcement, session contexts, statistics.

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <bitset>
#include <charconv>
#include <chrono>
#include <compare>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <ranges>
#include <set>
#include <shared_mutex>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <syncstream>
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
#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/stopwatch.h>

// ============================================================================
// Platform detection and conditional includes
// ============================================================================
#if defined(_WIN32) || defined(_WIN64)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <ole2.h>
    #include <shlobj.h>
    #include <shlwapi.h>
    #define CLIPBOARD_PLATFORM_WINDOWS 1
#elif defined(__APPLE__) && defined(__MACH__)
    #include <objc/objc.h>
    #include <objc/runtime.h>
    #include <CoreFoundation/CoreFoundation.h>
    #include <ApplicationServices/ApplicationServices.h>
    #include <AppKit/AppKit.h>
    #define CLIPBOARD_PLATFORM_MACOS 1
#elif defined(__linux__) || defined(__unix__) || defined(__FreeBSD__)
    #include <X11/Xlib.h>
    #include <X11/Xatom.h>
    #include <X11/Xutil.h>
    #include <X11/extensions/Xfixes.h>
    #include <poll.h>
    #include <sys/inotify.h>
    #include <sys/timerfd.h>
    #include <unistd.h>
    #define CLIPBOARD_PLATFORM_X11 1
#else
    #define CLIPBOARD_PLATFORM_GENERIC 1
#endif

#if defined(__linux__)
    #include <linux/limits.h>
#endif

// ============================================================================
// Namespace declaration
// ============================================================================
namespace cppdesk::server {

// ============================================================================
// Forward declarations
// ============================================================================
class ClipboardService;
class ClipboardFormatNegotiator;
class ClipboardDedupEngine;
class ClipboardTransferEngine;
class ClipboardRateLimiter;
class ClipboardPlatformBridge;
class ClipboardMonitor;
class ClipboardPermissionManager;
class ClipboardSessionManager;
class ClipboardStatistics;

// ============================================================================
// Constants and types
// ============================================================================

// Version of the clipboard protocol
constexpr uint32_t kClipboardProtocolVersion = 3;

// Maximum clipboard data sizes in bytes
constexpr size_t kMaxTextSize       = 32 * 1024 * 1024;      // 32 MB
constexpr size_t kMaxImageSize      = 128 * 1024 * 1024;     // 128 MB
constexpr size_t kMaxFileSize       = 512 * 1024 * 1024;     // 512 MB
constexpr size_t kMaxTotalTransfer  = 2ULL * 1024 * 1024 * 1024; // 2 GB per session

// Chunked transfer parameters
constexpr size_t kChunkSize         = 64 * 1024;             // 64 KB chunks
constexpr size_t kMaxChunksInFlight = 16;                    // Pipeline depth
constexpr size_t kIncrementalThreshold = 256 * 1024;         // 256 KB — use incremental above

// Timing constants
constexpr auto kDefaultPollInterval    = std::chrono::milliseconds(100);
constexpr auto kMinPollInterval        = std::chrono::milliseconds(10);
constexpr auto kMaxPollInterval        = std::chrono::milliseconds(5000);
constexpr auto kDefaultSyncTimeout     = std::chrono::seconds(30);
constexpr auto kRateLimitWindow        = std::chrono::seconds(1);
constexpr auto kThrottleCooldown       = std::chrono::seconds(5);
constexpr auto kDedupEntryTtl          = std::chrono::hours(24);
constexpr auto kIdlePruneInterval      = std::chrono::minutes(10);
constexpr auto kStatisticsFlushInterval= std::chrono::minutes(1);

// Default rate limits
constexpr size_t kDefaultMaxSyncsPerSecond = 10;
constexpr size_t kDefaultMaxBytesPerSecond = 10 * 1024 * 1024; // 10 MB/s

// ============================================================================
// Clipboard format definitions
// ============================================================================

// Supported clipboard format identifiers
enum class ClipboardFormat : uint32_t {
    kUnknown        = 0,
    kUnicodeText    = 1,       // UTF-16 / UTF-8 text
    kRTFText        = 2,       // Rich Text Format
    kHTML           = 3,       // HTML clipboard format
    kBitmap         = 4,       // DIB / BMP
    kPNG            = 5,       // PNG image
    kJPEG           = 6,       // JPEG image
    kTIFF           = 7,       // TIFF image
    kGIF            = 8,       // GIF image
    kFileList       = 9,       // List of files (CF_HDROP / NSFilenamesPboardType)
    kFileContents   = 10,      // File contents for pasting
    kOEMText        = 11,      // OEM text encoding
    kMetafilePict   = 12,      // Windows metafile
    kEnhancedMetafile = 13,    // Enhanced metafile
    kPalette        = 14,      // Color palette
    kPenData        = 15,      // Pen input data
    kPrivateFormat  = 16,      // Private format base
    kCustomBinary   = 17,      // Custom binary blob

    // Internal synthesized formats
    kCompressedText = 100,
    kCompressedImage= 101,
    kIncrementalFile= 102,
    kDeltaUpdate    = 103,
    kFormatHint     = 104,
};

// Priority ordering for format negotiation (lower = preferred)
constexpr int kFormatPriorityOrder[] = {
    1,   // kUnicodeText
    5,   // kRTFText
    4,   // kHTML
    2,   // kPNG
    3,   // kBitmap
    8,   // kJPEG
    9,   // kTIFF
    10,  // kGIF
    6,   // kFileList
    7,   // kFileContents
    20,  // kOEMText
    21,  // kMetafilePict
    22,  // kEnhancedMetafile
    23,  // kPalette
    24,  // kPenData
    50,  // kPrivateFormat (base; dynamically adjusted)
    50,  // kCustomBinary
};

// Human-readable format names
constexpr std::string_view kFormatNames[] = {
    "Unknown", "UnicodeText", "RTFText", "HTML", "Bitmap",
    "PNG", "JPEG", "TIFF", "GIF", "FileList", "FileContents",
    "OEMText", "MetafilePict", "EnhancedMetafile", "Palette", "PenData",
    "PrivateFormat", "CustomBinary",
};

// Format categories for high-level handling
enum class FormatCategory : uint8_t {
    kText       = 0,
    kImage      = 1,
    kFile       = 2,
    kBinary     = 3,
    kMeta       = 4,
};

constexpr FormatCategory categorize(ClipboardFormat fmt) noexcept {
    switch (fmt) {
        case ClipboardFormat::kUnicodeText:
        case ClipboardFormat::kRTFText:
        case ClipboardFormat::kHTML:
        case ClipboardFormat::kOEMText:
            return FormatCategory::kText;
        case ClipboardFormat::kBitmap:
        case ClipboardFormat::kPNG:
        case ClipboardFormat::kJPEG:
        case ClipboardFormat::kTIFF:
        case ClipboardFormat::kGIF:
            return FormatCategory::kImage;
        case ClipboardFormat::kFileList:
        case ClipboardFormat::kFileContents:
            return FormatCategory::kFile;
        case ClipboardFormat::kPalette:
        case ClipboardFormat::kPenData:
        case ClipboardFormat::kMetafilePict:
        case ClipboardFormat::kEnhancedMetafile:
            return FormatCategory::kMeta;
        default:
            return FormatCategory::kBinary;
    }
}

// ============================================================================
// ClipboardContent — strongly-typed clipboard data holder
// ============================================================================

struct ClipboardContent {
    ClipboardFormat format{ClipboardFormat::kUnknown};
    FormatCategory category{FormatCategory::kBinary};
    std::vector<uint8_t> data;
    std::optional<std::string> mime_type;
    std::optional<std::string> source_display_name;
    std::chrono::steady_clock::time_point timestamp;
    uint64_t sequence_number{0};
    uint64_t content_hash{0};

    // File-specific metadata
    std::optional<std::vector<std::filesystem::path>> file_list;
    std::optional<std::string> suggested_filename;
    std::optional<uint64_t> total_file_size;

    // Image-specific metadata
    std::optional<uint32_t> image_width;
    std::optional<uint32_t> image_height;
    std::optional<uint32_t> image_bpp;

    // Text-specific metadata
    std::optional<std::string> text_encoding;
    std::optional<bool> has_unicode;

    // Delta information for incremental updates
    std::optional<uint64_t> base_sequence;
    std::optional<std::vector<uint8_t>> delta_payload;

    // Ownership tracking
    std::optional<std::string> session_id;
    std::optional<std::string> source_host;

    // -----------------------------------------------------------------------
    // Constructors
    // -----------------------------------------------------------------------
    ClipboardContent() : timestamp(std::chrono::steady_clock::now()) {}

    explicit ClipboardContent(ClipboardFormat fmt) noexcept
        : format(fmt), category(categorize(fmt)),
          timestamp(std::chrono::steady_clock::now()) {}

    ClipboardContent(ClipboardFormat fmt, std::vector<uint8_t> d) noexcept
        : format(fmt), category(categorize(fmt)),
          data(std::move(d)), timestamp(std::chrono::steady_clock::now()) {}

    // -----------------------------------------------------------------------
    // Queries
    // -----------------------------------------------------------------------
    [[nodiscard]] bool empty() const noexcept { return data.empty(); }
    [[nodiscard]] size_t size() const noexcept { return data.size(); }
    [[nodiscard]] bool is_text() const noexcept { return category == FormatCategory::kText; }
    [[nodiscard]] bool is_image() const noexcept { return category == FormatCategory::kImage; }
    [[nodiscard]] bool is_file() const noexcept { return category == FormatCategory::kFile; }

    [[nodiscard]] std::string_view text_utf8() const noexcept {
        if (is_text() && !data.empty()) {
            return {reinterpret_cast<const char*>(data.data()), data.size()};
        }
        return {};
    }

    [[nodiscard]] std::chrono::milliseconds age() const noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - timestamp);
    }

    // -----------------------------------------------------------------------
    // Serialization helpers
    // -----------------------------------------------------------------------
    [[nodiscard]] std::string to_hex_digest(size_t max_len = 64) const noexcept {
        std::string out;
        out.reserve(std::min(data.size(), max_len) * 2);
        for (size_t i = 0; i < std::min(data.size(), max_len); ++i) {
            out += "0123456789abcdef"[data[i] >> 4];
            out += "0123456789abcdef"[data[i] & 0xf];
        }
        return out;
    }
};

// ============================================================================
// ClipboardChangeEvent — emitted by the monitor
// ============================================================================

struct ClipboardChangeEvent {
    enum class Type : uint8_t {
        kContentChanged     = 0,
        kOwnerChanged       = 1,
        kFormatsChanged     = 2,
        kCleared            = 3,
        kUnknown            = 4,
    };

    Type type{Type::kUnknown};
    std::chrono::steady_clock::time_point timestamp;
    std::vector<ClipboardFormat> available_formats;
    std::optional<uint64_t> sequence_number;
    std::optional<std::string> session_id;
    bool from_network{false};

    ClipboardChangeEvent() : timestamp(std::chrono::steady_clock::now()) {}
    explicit ClipboardChangeEvent(Type t) : type(t), timestamp(std::chrono::steady_clock::now()) {}
};

// ============================================================================
// ClipboardSyncPolicy — per-session clipboard synchronization policy
// ============================================================================

struct ClipboardSyncPolicy {
    bool enable_text_sync{true};
    bool enable_image_sync{true};
    bool enable_file_sync{true};
    bool enable_format_conversion{true};
    bool enable_dedup{true};
    bool enable_incremental{true};
    bool enable_compression{false};
    size_t max_text_size{kMaxTextSize};
    size_t max_image_size{kMaxImageSize};
    size_t max_file_size{kMaxFileSize};
    size_t max_syncs_per_second{kDefaultMaxSyncsPerSecond};
    size_t max_bytes_per_second{kDefaultMaxBytesPerSecond};
    std::chrono::milliseconds poll_interval{kDefaultPollInterval};
    std::set<ClipboardFormat> allowed_formats;
    std::set<ClipboardFormat> blocked_formats;
    bool enable_statistics{true};

    /// Returns the effective poll interval, clamped within safe bounds.
    [[nodiscard]] std::chrono::milliseconds effective_poll_interval() const noexcept {
        return std::clamp(poll_interval, kMinPollInterval, kMaxPollInterval);
    }
};

// ============================================================================
// ClipboardSessionContext — isolated clipboard state for a session
// ============================================================================

class ClipboardSessionContext {
public:
    explicit ClipboardSessionContext(std::string session_id)
        : session_id_(std::move(session_id)),
          created_at_(std::chrono::steady_clock::now()) {
        spdlog::debug("[ClipboardSession] Created context for session '{}'", session_id_);
    }

    ~ClipboardSessionContext() {
        spdlog::debug("[ClipboardSession] Destroyed context for session '{}'", session_id_);
    }

    // --- Mutators ---

    void record_local_change(const ClipboardContent& content) {
        std::unique_lock lock(mutex_);
        last_local_content_ = content;
        last_local_content_->session_id = session_id_;
        last_local_content_->sequence_number = next_sequence_++;
        dirty_ = true;
    }

    void record_remote_change(const ClipboardContent& content) {
        std::unique_lock lock(mutex_);
        last_remote_content_ = content;
        last_remote_content_->session_id = session_id_;
        last_remote_content_->sequence_number = next_sequence_++;
    }

    void set_policy(const ClipboardSyncPolicy& policy) {
        std::unique_lock lock(mutex_);
        policy_ = policy;
    }

    void clear() {
        std::unique_lock lock(mutex_);
        last_local_content_.reset();
        last_remote_content_.reset();
        dirty_ = false;
    }

    void set_active(bool active) noexcept {
        active_.store(active, std::memory_order_release);
        spdlog::trace("[ClipboardSession] Session '{}' set active={}", session_id_, active);
    }

    // --- Accessors ---

    [[nodiscard]] const std::string& session_id() const noexcept { return session_id_; }

    [[nodiscard]] bool is_active() const noexcept {
        return active_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool is_dirty() const noexcept {
        std::shared_lock lock(mutex_);
        return dirty_;
    }

    void mark_clean() noexcept {
        std::unique_lock lock(mutex_);
        dirty_ = false;
    }

    [[nodiscard]] std::optional<ClipboardContent> last_local() const {
        std::shared_lock lock(mutex_);
        return last_local_content_;
    }

    [[nodiscard]] std::optional<ClipboardContent> last_remote() const {
        std::shared_lock lock(mutex_);
        return last_remote_content_;
    }

    [[nodiscard]] ClipboardSyncPolicy policy() const {
        std::shared_lock lock(mutex_);
        return policy_;
    }

    [[nodiscard]] uint64_t sequence() const noexcept {
        return next_sequence_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::chrono::steady_clock::time_point created_at() const noexcept {
        return created_at_;
    }

    [[nodiscard]] std::chrono::milliseconds age() const noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - created_at_);
    }

private:
    std::string session_id_;
    mutable std::shared_mutex mutex_;
    std::optional<ClipboardContent> last_local_content_;
    std::optional<ClipboardContent> last_remote_content_;
    ClipboardSyncPolicy policy_;
    std::atomic<uint64_t> next_sequence_{1};
    std::atomic<bool> active_{true};
    bool dirty_{false};
    std::chrono::steady_clock::time_point created_at_;
};

// ============================================================================
// ClipboardSessionManager — manages per-session clipboard contexts
// ============================================================================

class ClipboardSessionManager {
public:
    ClipboardSessionManager() {
        spdlog::info("[ClipboardSessionManager] Initialized");
    }

    ~ClipboardSessionManager() {
        spdlog::info("[ClipboardSessionManager] Shutting down, {} active sessions", 
                     session_count());
    }

    // --- Session lifecycle ---

    std::shared_ptr<ClipboardSessionContext> create_session(const std::string& session_id) {
        std::unique_lock lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            spdlog::warn("[ClipboardSessionManager] Session '{}' already exists, reusing", session_id);
            return it->second;
        }
        auto ctx = std::make_shared<ClipboardSessionContext>(session_id);
        sessions_.emplace(session_id, ctx);
        spdlog::info("[ClipboardSessionManager] Created session '{}' (total: {})",
                     session_id, sessions_.size());
        return ctx;
    }

    bool destroy_session(const std::string& session_id) {
        std::unique_lock lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return false;
        }
        it->second->set_active(false);
        sessions_.erase(it);
        spdlog::info("[ClipboardSessionManager] Destroyed session '{}' (remaining: {})",
                     session_id, sessions_.size());
        return true;
    }

    [[nodiscard]] std::shared_ptr<ClipboardSessionContext> get_session(
            const std::string& session_id) {
        std::shared_lock lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            return it->second;
        }
        return nullptr;
    }

    void set_active(const std::string& session_id, bool active) {
        std::shared_lock lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second->set_active(active);
        }
    }

    [[nodiscard]] size_t session_count() const noexcept {
        std::shared_lock lock(mutex_);
        return sessions_.size();
    }

    /// Returns all currently active sessions.
    [[nodiscard]] std::vector<std::shared_ptr<ClipboardSessionContext>> active_sessions() const {
        std::shared_lock lock(mutex_);
        std::vector<std::shared_ptr<ClipboardSessionContext>> result;
        result.reserve(sessions_.size());
        for (const auto& [id, ctx] : sessions_) {
            if (ctx->is_active()) {
                result.push_back(ctx);
            }
        }
        return result;
    }

    /// Prune sessions that have been idle beyond `max_idle`.
    size_t prune_idle_sessions(std::chrono::milliseconds max_idle) {
        std::unique_lock lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        size_t pruned = 0;
        for (auto it = sessions_.begin(); it != sessions_.end(); ) {
            if (!it->second->is_active() &&
                (now - it->second->created_at()) > max_idle) {
                spdlog::info("[ClipboardSessionManager] Pruning idle session '{}'", it->first);
                it = sessions_.erase(it);
                ++pruned;
            } else {
                ++it;
            }
        }
        return pruned;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<ClipboardSessionContext>> sessions_;
};

// ============================================================================
// ClipboardDedupEngine — content-hash-based deduplication
// ============================================================================

class ClipboardDedupEngine {
public:
    explicit ClipboardDedupEngine(size_t max_entries = 10000)
        : max_entries_(max_entries) {
        spdlog::info("[ClipboardDedup] Initialized with max_entries={}", max_entries_);
    }

    // --- Core dedup check ---

    /// Check if content is a duplicate. Returns true if already seen.
    /// If not a duplicate, records it for future checks.
    [[nodiscard]] bool check_and_record(const ClipboardContent& content,
                                         std::string_view session_id = {}) {
        uint64_t hash = compute_content_hash(content);
        std::unique_lock lock(mutex_);

        DedupEntry entry{hash, std::chrono::steady_clock::now(), std::string(session_id)};

        auto [it, inserted] = entries_.try_emplace(hash, entry);
        if (!inserted) {
            // Update timestamp for LRU tracking
            it->second.last_seen = std::chrono::steady_clock::now();
            ++dedup_hits_;
            ++total_dedup_hits_;
            spdlog::trace("[ClipboardDedup] Duplicate detected: hash={:016x}", hash);
            return true;  // Duplicate
        }

        ++total_checks_;
        spdlog::trace("[ClipboardDedup] New content: hash={:016x}", hash);

        // Evict oldest entries if over capacity
        if (entries_.size() > max_entries_) {
            evict_lru();
        }

        return false;  // Not a duplicate
    }

    /// Force-record a hash without checking duplication.
    void record_hash(uint64_t hash) {
        std::unique_lock lock(mutex_);
        DedupEntry entry{hash, std::chrono::steady_clock::now(), {}};
        entries_.insert_or_assign(hash, entry);
    }

    /// Check only (does not record).
    [[nodiscard]] bool is_duplicate(const ClipboardContent& content) const {
        uint64_t hash = compute_content_hash(content);
        std::shared_lock lock(mutex_);
        return entries_.contains(hash);
    }

    /// Explicitly invalidate a hash (e.g., after a clear).
    void invalidate(uint64_t hash) {
        std::unique_lock lock(mutex_);
        entries_.erase(hash);
    }

    /// Clear all entries.
    void reset() {
        std::unique_lock lock(mutex_);
        entries_.clear();
        dedup_hits_ = 0;
        total_checks_ = 0;
        spdlog::debug("[ClipboardDedup] Reset all entries");
    }

    /// Prune entries older than `max_age`.
    size_t prune_expired(std::chrono::milliseconds max_age = kDedupEntryTtl) {
        std::unique_lock lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        size_t removed = 0;
        for (auto it = entries_.begin(); it != entries_.end(); ) {
            if ((now - it->second.last_seen) > max_age) {
                it = entries_.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        if (removed > 0) {
            spdlog::debug("[ClipboardDedup] Pruned {} expired entries", removed);
        }
        return removed;
    }

    // --- Statistics ---

    [[nodiscard]] size_t entry_count() const noexcept {
        std::shared_lock lock(mutex_);
        return entries_.size();
    }

    [[nodiscard]] uint64_t dedup_hit_count() const noexcept {
        return total_dedup_hits_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t total_check_count() const noexcept {
        return total_checks_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] double dedup_ratio() const noexcept {
        uint64_t total = total_checks_.load(std::memory_order_relaxed);
        if (total == 0) return 0.0;
        uint64_t hits = total_dedup_hits_.load(std::memory_order_relaxed);
        return static_cast<double>(hits) / static_cast<double>(total);
    }

    // --- Periodic maintenance ---

    size_t run_maintenance() {
        size_t pruned = prune_expired();
        return pruned;
    }

private:
    struct DedupEntry {
        uint64_t hash;
        std::chrono::steady_clock::time_point last_seen;
        std::string source_session;
    };

    /// Fast, non-cryptographic content hash combining format + data.
    static uint64_t compute_content_hash(const ClipboardContent& content) noexcept {
        // Use FNV-1a 64-bit hash
        uint64_t hash = 14695981039346656037ULL;
        const uint8_t* data = content.data.data();
        size_t len = content.data.size();

        // Hash format identifier
        auto fmt_bytes = std::bit_cast<std::array<uint8_t, 4>>(
            static_cast<uint32_t>(content.format));
        for (auto b : fmt_bytes) {
            hash ^= b;
            hash *= 1099511628211ULL;
        }

        // Hash data
        for (size_t i = 0; i < len; ++i) {
            hash ^= data[i];
            hash *= 1099511628211ULL;
        }

        return hash;
    }

    void evict_lru() {
        if (entries_.empty()) return;

        auto oldest = entries_.begin();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.last_seen < oldest->second.last_seen) {
                oldest = it;
            }
        }
        entries_.erase(oldest);
    }

    mutable std::shared_mutex mutex_;
    std::unordered_map<uint64_t, DedupEntry> entries_;
    size_t max_entries_;
    std::atomic<uint64_t> total_dedup_hits_{0};
    std::atomic<uint64_t> total_checks_{0};
    size_t dedup_hits_{0};
};

// ============================================================================
// ClipboardRateLimiter — token-bucket based rate limiting
// ============================================================================

class ClipboardRateLimiter {
public:
    struct Config {
        size_t max_syncs_per_second{kDefaultMaxSyncsPerSecond};
        size_t max_bytes_per_second{kDefaultMaxBytesPerSecond};
        size_t burst_syncs{kDefaultMaxSyncsPerSecond * 2};
        size_t burst_bytes{kDefaultMaxBytesPerSecond * 2};
    };

    explicit ClipboardRateLimiter(Config cfg = {}) : config_(cfg) {
        sync_tokens_ = static_cast<double>(cfg.burst_syncs);
        bytes_tokens_ = static_cast<double>(cfg.burst_bytes);
        last_refill_ = std::chrono::steady_clock::now();
        spdlog::info("[ClipboardRateLimiter] Initialized: {} syncs/s, {} bytes/s",
                     cfg.max_syncs_per_second, cfg.max_bytes_per_second);
    }

    /// Attempt to consume one sync operation. Returns true if permitted.
    [[nodiscard]] bool try_consume_sync() {
        std::unique_lock lock(mutex_);
        refill_tokens();
        if (sync_tokens_ >= 1.0) {
            sync_tokens_ -= 1.0;
            ++total_syncs_allowed_;
            return true;
        }
        ++total_syncs_denied_;
        spdlog::debug("[ClipboardRateLimiter] Sync rate limited (tokens={:.2f})", sync_tokens_);
        return false;
    }

    /// Attempt to consume `bytes` bytes. Returns amount actually allowed.
    [[nodiscard]] size_t try_consume_bytes(size_t bytes) {
        std::unique_lock lock(mutex_);
        refill_tokens();
        double needed = static_cast<double>(bytes);
        if (bytes_tokens_ >= needed) {
            bytes_tokens_ -= needed;
            total_bytes_allowed_ += bytes;
            return bytes; // All allowed
        }
        size_t allowed = static_cast<size_t>(bytes_tokens_);
        if (allowed > 0) {
            bytes_tokens_ = 0.0;
            total_bytes_allowed_ += allowed;
        }
        total_bytes_denied_ += (bytes - allowed);
        spdlog::debug("[ClipboardRateLimiter] Bytes rate limited: requested={}, allowed={}",
                      bytes, allowed);
        return allowed;
    }

    /// Check if any sync is currently allowed.
    [[nodiscard]] bool is_sync_allowed() noexcept {
        std::unique_lock lock(mutex_);
        refill_tokens();
        return sync_tokens_ >= 1.0;
    }

    /// Check if `bytes` transfer is currently allowed.
    [[nodiscard]] bool is_bytes_allowed(size_t bytes) noexcept {
        std::unique_lock lock(mutex_);
        refill_tokens();
        return bytes_tokens_ >= static_cast<double>(bytes);
    }

    /// Enable or disable the rate limiter.
    void set_enabled(bool enabled) noexcept {
        enabled_.store(enabled, std::memory_order_release);
    }

    [[nodiscard]] bool is_enabled() const noexcept {
        return enabled_.load(std::memory_order_acquire);
    }

    /// Update configuration.
    void update_config(const Config& cfg) {
        std::unique_lock lock(mutex_);
        config_ = cfg;
        sync_tokens_ = std::min(sync_tokens_, static_cast<double>(cfg.burst_syncs));
        bytes_tokens_ = std::min(bytes_tokens_, static_cast<double>(cfg.burst_bytes));
    }

    // --- Statistics ---

    [[nodiscard]] uint64_t syncs_allowed() const noexcept { return total_syncs_allowed_; }
    [[nodiscard]] uint64_t syncs_denied() const noexcept { return total_syncs_denied_; }
    [[nodiscard]] uint64_t bytes_allowed() const noexcept { return total_bytes_allowed_; }
    [[nodiscard]] uint64_t bytes_denied() const noexcept { return total_bytes_denied_; }
    [[nodiscard]] double sync_tokens_available() const noexcept {
        std::shared_lock lock(mutex_);
        return sync_tokens_;
    }

private:
    void refill_tokens() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - last_refill_).count();
        if (elapsed <= 0.0) return;

        last_refill_ = now;
        sync_tokens_ += elapsed * static_cast<double>(config_.max_syncs_per_second);
        bytes_tokens_ += elapsed * static_cast<double>(config_.max_bytes_per_second);

        double max_sync = static_cast<double>(config_.burst_syncs);
        double max_bytes = static_cast<double>(config_.burst_bytes);
        sync_tokens_ = std::min(sync_tokens_, max_sync);
        bytes_tokens_ = std::min(bytes_tokens_, max_bytes);
    }

    mutable std::shared_mutex mutex_;
    Config config_;
    double sync_tokens_;
    double bytes_tokens_;
    std::chrono::steady_clock::time_point last_refill_;
    std::atomic<bool> enabled_{true};

    // Counters
    std::atomic<uint64_t> total_syncs_allowed_{0};
    std::atomic<uint64_t> total_syncs_denied_{0};
    std::atomic<uint64_t> total_bytes_allowed_{0};
    std::atomic<uint64_t> total_bytes_denied_{0};
};

// ============================================================================
// ClipboardStatistics — comprehensive clipboard operation statistics
// ============================================================================

class ClipboardStatistics {
public:
    ClipboardStatistics() { reset(); }

    // --- Record operations ---

    void record_sync_sent(size_t bytes, ClipboardFormat fmt) {
        ++total_syncs_sent_;
        total_bytes_sent_ += bytes;
        auto cat = categorize(fmt);
        switch (cat) {
            case FormatCategory::kText:  text_syncs_sent_++;  text_bytes_sent_ += bytes; break;
            case FormatCategory::kImage: image_syncs_sent_++; image_bytes_sent_ += bytes; break;
            case FormatCategory::kFile:  file_syncs_sent_++;  file_bytes_sent_ += bytes; break;
            default: break;
        }
    }

    void record_sync_recv(size_t bytes, ClipboardFormat fmt) {
        ++total_syncs_recv_;
        total_bytes_recv_ += bytes;
        auto cat = categorize(fmt);
        switch (cat) {
            case FormatCategory::kText:  text_syncs_recv_++;  text_bytes_recv_ += bytes; break;
            case FormatCategory::kImage: image_syncs_recv_++; image_bytes_recv_ += bytes; break;
            case FormatCategory::kFile:  file_syncs_recv_++;  file_bytes_recv_ += bytes; break;
            default: break;
        }
    }

    void record_dedup_hit() { ++dedup_hits_; }
    void record_dedup_miss() { ++dedup_misses_; }
    void record_format_negotiation() { ++format_negotiations_; }
    void record_format_conversion(ClipboardFormat from, ClipboardFormat to) {
        ++format_conversions_;
        last_conversion_ = {from, to};
    }
    void record_platform_read(std::chrono::microseconds duration) {
        ++platform_reads_;
        total_platform_read_us_ += duration.count();
    }
    void record_platform_write(std::chrono::microseconds duration) {
        ++platform_writes_;
        total_platform_write_us_ += duration.count();
    }
    void record_rate_limit_hit() { ++rate_limit_hits_; }
    void record_error() { ++errors_; }
    void record_chunk_sent() { ++chunks_sent_; }
    void record_chunk_received() { ++chunks_recv_; }

    // --- Accessors ---

    [[nodiscard]] uint64_t total_syncs() const noexcept {
        return total_syncs_sent_ + total_syncs_recv_;
    }
    [[nodiscard]] uint64_t total_bytes() const noexcept {
        return total_bytes_sent_ + total_bytes_recv_;
    }
    [[nodiscard]] uint64_t syncs_sent() const noexcept { return total_syncs_sent_; }
    [[nodiscard]] uint64_t syncs_recv() const noexcept { return total_syncs_recv_; }
    [[nodiscard]] uint64_t bytes_sent() const noexcept { return total_bytes_sent_; }
    [[nodiscard]] uint64_t bytes_recv() const noexcept { return total_bytes_recv_; }
    [[nodiscard]] uint64_t dedup_hits() const noexcept { return dedup_hits_; }
    [[nodiscard]] uint64_t dedup_misses() const noexcept { return dedup_misses_; }
    [[nodiscard]] double dedup_ratio() const noexcept {
        uint64_t total = dedup_hits_ + dedup_misses_;
        if (total == 0) return 0.0;
        return static_cast<double>(dedup_hits_) / static_cast<double>(total);
    }
    [[nodiscard]] uint64_t format_negotiations() const noexcept { return format_negotiations_; }
    [[nodiscard]] uint64_t format_conversions() const noexcept { return format_conversions_; }
    [[nodiscard]] uint64_t errors() const noexcept { return errors_; }
    [[nodiscard]] double avg_platform_read_us() const noexcept {
        if (platform_reads_ == 0) return 0.0;
        return static_cast<double>(total_platform_read_us_) / platform_reads_;
    }
    [[nodiscard]] double avg_platform_write_us() const noexcept {
        if (platform_writes_ == 0) return 0.0;
        return static_cast<double>(total_platform_write_us_) / platform_writes_;
    }

    /// Generate a human-readable summary string.
    [[nodiscard]] std::string summary() const {
        return std::format(
            "ClipboardStatistics[syncs:{} sent/{} recv, bytes:{} sent/{} recv, "
            "dedup:{:.1%}, negot:{} conv:{}, errors:{}, r/w avg:{:.0f}/{:.0f}us]",
            total_syncs_sent_.load(), total_syncs_recv_.load(),
            total_bytes_sent_.load(), total_bytes_recv_.load(),
            dedup_ratio(), format_negotiations_.load(), format_conversions_.load(),
            errors_.load(), avg_platform_read_us(), avg_platform_write_us()
        );
    }

    /// Reset all counters.
    void reset() {
        total_syncs_sent_ = 0;
        total_syncs_recv_ = 0;
        total_bytes_sent_ = 0;
        total_bytes_recv_ = 0;
        text_syncs_sent_ = 0;  text_bytes_sent_ = 0;
        text_syncs_recv_ = 0;  text_bytes_recv_ = 0;
        image_syncs_sent_ = 0; image_bytes_sent_ = 0;
        image_syncs_recv_ = 0; image_bytes_recv_ = 0;
        file_syncs_sent_ = 0;  file_bytes_sent_ = 0;
        file_syncs_recv_ = 0;  file_bytes_recv_ = 0;
        dedup_hits_ = 0; dedup_misses_ = 0;
        format_negotiations_ = 0; format_conversions_ = 0;
        platform_reads_ = 0; platform_writes_ = 0;
        total_platform_read_us_ = 0; total_platform_write_us_ = 0;
        rate_limit_hits_ = 0; errors_ = 0;
        chunks_sent_ = 0; chunks_recv_ = 0;
    }

private:
    // Sync counts
    std::atomic<uint64_t> total_syncs_sent_{0};
    std::atomic<uint64_t> total_syncs_recv_{0};
    std::atomic<uint64_t> total_bytes_sent_{0};
    std::atomic<uint64_t> total_bytes_recv_{0};

    // Per-category breakdowns
    std::atomic<uint64_t> text_syncs_sent_{0};
    std::atomic<uint64_t> text_syncs_recv_{0};
    std::atomic<uint64_t> text_bytes_sent_{0};
    std::atomic<uint64_t> text_bytes_recv_{0};
    std::atomic<uint64_t> image_syncs_sent_{0};
    std::atomic<uint64_t> image_syncs_recv_{0};
    std::atomic<uint64_t> image_bytes_sent_{0};
    std::atomic<uint64_t> image_bytes_recv_{0};
    std::atomic<uint64_t> file_syncs_sent_{0};
    std::atomic<uint64_t> file_syncs_recv_{0};
    std::atomic<uint64_t> file_bytes_sent_{0};
    std::atomic<uint64_t> file_bytes_recv_{0};

    // Deduplication
    std::atomic<uint64_t> dedup_hits_{0};
    std::atomic<uint64_t> dedup_misses_{0};

    // Negotiation and conversion
    std::atomic<uint64_t> format_negotiations_{0};
    std::atomic<uint64_t> format_conversions_{0};
    struct ConvPair {
        ClipboardFormat from;
        ClipboardFormat to;
    };
    std::optional<ConvPair> last_conversion_;

    // Platform I/O timings
    std::atomic<uint64_t> platform_reads_{0};
    std::atomic<uint64_t> platform_writes_{0};
    std::atomic<uint64_t> total_platform_read_us_{0};
    std::atomic<uint64_t> total_platform_write_us_{0};

    // Misc
    std::atomic<uint64_t> rate_limit_hits_{0};
    std::atomic<uint64_t> errors_{0};
    std::atomic<uint64_t> chunks_sent_{0};
    std::atomic<uint64_t> chunks_recv_{0};
};

// ============================================================================
// ClipboardFormatNegotiator — format negotiation and conversion
// ============================================================================

class ClipboardFormatNegotiator {
public:
    ClipboardFormatNegotiator() {
        spdlog::info("[ClipboardFormatNegotiator] Initialized");
        initialize_conversion_graph();
    }

    // --- Primary negotiation interface ---

    /// Negotiate the best common format between local available formats and
    /// remote desired formats. Returns the negotiated format, or nullopt if
    /// no compatible format is available.
    [[nodiscard]] std::optional<ClipboardFormat> negotiate(
            const std::vector<ClipboardFormat>& local_formats,
            const std::vector<ClipboardFormat>& remote_formats,
            const ClipboardSyncPolicy& policy = {}) const {

        spdlog::debug("[ClipboardFormatNegotiator] Negotiating: local={} remote={} formats",
                      local_formats.size(), remote_formats.size());

        // Build sets for O(1) lookup
        std::unordered_set<ClipboardFormat> local_set;
        for (auto& f : local_formats) {
            if (is_format_allowed(f, policy)) {
                local_set.insert(f);
            }
        }
        std::unordered_set<ClipboardFormat> remote_set(
            remote_formats.begin(), remote_formats.end());

        // First pass: direct matches, ordered by priority
        for (auto& entry : priority_order_) {
            if (local_set.contains(entry) && remote_set.contains(entry)) {
                spdlog::debug("[ClipboardFormatNegotiator] Direct match: {}", 
                              format_name(entry));
                return entry;
            }
        }

        // Second pass: convertible matches
        if (policy.enable_format_conversion) {
            for (auto& entry : priority_order_) {
                if (!remote_set.contains(entry)) continue;

                // Check if any local format can be converted to this remote format
                for (auto& local_fmt : local_set) {
                    if (can_convert(local_fmt, entry)) {
                        spdlog::debug("[ClipboardFormatNegotiator] Convertible match: "
                                      "local {} -> remote {}",
                                      format_name(local_fmt), format_name(entry));
                        return entry;
                    }
                }
            }
        }

        spdlog::debug("[ClipboardFormatNegotiator] No compatible format found");
        return std::nullopt;
    }

    /// Determine which local format should be read to serve a negotiated format.
    [[nodiscard]] std::optional<ClipboardFormat> resolve_source_format(
            const std::vector<ClipboardFormat>& available_formats,
            ClipboardFormat target) const {

        // Direct match
        if (std::ranges::find(available_formats, target) != available_formats.end()) {
            return target;
        }

        // Find best convertible source
        for (auto& entry : priority_order_) {
            if (std::ranges::find(available_formats, entry) != available_formats.end() &&
                can_convert(entry, target)) {
                return entry;
            }
        }

        return std::nullopt;
    }

    // --- Format conversion ---

    /// Convert content from source format to target format.
    [[nodiscard]] std::optional<ClipboardContent> convert(
            const ClipboardContent& source, ClipboardFormat target) const {

        if (source.format == target) {
            return source; // No conversion needed
        }

        spdlog::debug("[ClipboardFormatNegotiator] Converting {} -> {}",
                      format_name(source.format), format_name(target));

        auto it = converters_.find({source.format, target});
        if (it == converters_.end()) {
            spdlog::warn("[ClipboardFormatNegotiator] No converter for {} -> {}",
                         format_name(source.format), format_name(target));
            return std::nullopt;
        }

        return it->second(source);
    }

    /// Check if conversion is possible between two formats.
    [[nodiscard]] bool can_convert(ClipboardFormat from, ClipboardFormat to) const noexcept {
        return converters_.contains({from, to});
    }

    /// Get all formats that can be produced from a source format.
    [[nodiscard]] std::vector<ClipboardFormat> convert_targets(
            ClipboardFormat from) const noexcept {
        std::vector<ClipboardFormat> result;
        for (const auto& [key, _] : converters_) {
            if (key.first == from) {
                result.push_back(key.second);
            }
        }
        return result;
    }

    // --- Format listing ---

    [[nodiscard]] static std::string_view format_name(ClipboardFormat fmt) noexcept {
        auto idx = static_cast<size_t>(fmt);
        if (idx < std::size(kFormatNames)) {
            return kFormatNames[idx];
        }
        return "Unknown";
    }

    [[nodiscard]] static constexpr int format_priority(ClipboardFormat fmt) noexcept {
        auto idx = static_cast<size_t>(fmt);
        if (idx < std::size(kFormatPriorityOrder)) {
            return kFormatPriorityOrder[idx];
        }
        return 100;
    }

private:
    using ConversionKey = std::pair<ClipboardFormat, ClipboardFormat>;
    using ConverterFunc = std::function<std::optional<ClipboardContent>(const ClipboardContent&)>;

    // Ordered by priority (lowest = best)
    std::vector<ClipboardFormat> priority_order_{
        ClipboardFormat::kUnicodeText,
        ClipboardFormat::kPNG,
        ClipboardFormat::kBitmap,
        ClipboardFormat::kHTML,
        ClipboardFormat::kRTFText,
        ClipboardFormat::kFileList,
        ClipboardFormat::kFileContents,
        ClipboardFormat::kJPEG,
        ClipboardFormat::kTIFF,
        ClipboardFormat::kGIF,
        ClipboardFormat::kOEMText,
        ClipboardFormat::kMetafilePict,
        ClipboardFormat::kEnhancedMetafile,
        ClipboardFormat::kPalette,
        ClipboardFormat::kPenData,
        ClipboardFormat::kCustomBinary,
    };

    std::unordered_map<ConversionKey, ConverterFunc, 
        decltype([](const ConversionKey& k) { 
            return (static_cast<uint64_t>(k.first) << 32) | static_cast<uint64_t>(k.second); 
        }),
        decltype([](const ConversionKey& a, const ConversionKey& b) { 
            auto ha = (static_cast<uint64_t>(a.first) << 32) | static_cast<uint64_t>(a.second);
            auto hb = (static_cast<uint64_t>(b.first) << 32) | static_cast<uint64_t>(b.second);
            return ha == hb; 
        })> converters_;

    void initialize_conversion_graph() {
        // Text conversions
        add_converter(ClipboardFormat::kUnicodeText, ClipboardFormat::kOEMText,
            [](const ClipboardContent& src) -> std::optional<ClipboardContent> {
                // UTF-8 to OEM (simplified: strip high bytes)
                ClipboardContent result(ClipboardFormat::kOEMText);
                result.data.reserve(src.data.size());
                for (auto c : src.data) {
                    result.data.push_back(c < 128 ? c : '?');
                }
                result.text_encoding = "OEM";
                return result;
            });

        add_converter(ClipboardFormat::kOEMText, ClipboardFormat::kUnicodeText,
            [](const ClipboardContent& src) -> std::optional<ClipboardContent> {
                ClipboardContent result(ClipboardFormat::kUnicodeText);
                result.data = src.data; // OEM fits in Unicode range 0-255
                result.text_encoding = "UTF-8";
                return result;
            });

        // HTML to plain text (simple tag stripping)
        add_converter(ClipboardFormat::kHTML, ClipboardFormat::kUnicodeText,
            [](const ClipboardContent& src) -> std::optional<ClipboardContent> {
                ClipboardContent result(ClipboardFormat::kUnicodeText);
                std::string_view html(reinterpret_cast<const char*>(src.data.data()), 
                                      src.data.size());
                result.data.reserve(html.size());
                bool in_tag = false;
                for (char c : html) {
                    if (c == '<') in_tag = true;
                    else if (c == '>') in_tag = false;
                    else if (!in_tag) result.data.push_back(static_cast<uint8_t>(c));
                }
                result.text_encoding = "UTF-8";
                return result;
            });

        // RTF to plain text (simplified)
        add_converter(ClipboardFormat::kRTFText, ClipboardFormat::kUnicodeText,
            [](const ClipboardContent& src) -> std::optional<ClipboardContent> {
                ClipboardContent result(ClipboardFormat::kUnicodeText);
                // Basic RTF stripping: skip control words
                std::string_view rtf(reinterpret_cast<const char*>(src.data.data()),
                                     src.data.size());
                result.data.reserve(rtf.size());
                bool in_control = false;
                for (size_t i = 0; i < rtf.size(); ++i) {
                    char c = rtf[i];
                    if (c == '\\' && !in_control) {
                        in_control = true;
                        continue;
                    }
                    if (in_control) {
                        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                            in_control = false;
                        }
                        continue;
                    }
                    if (c == '{' || c == '}') continue;
                    result.data.push_back(static_cast<uint8_t>(c));
                }
                result.text_encoding = "UTF-8";
                return result;
            });

        // PNG to Bitmap (raw pixel extraction; placeholder)
        add_converter(ClipboardFormat::kPNG, ClipboardFormat::kBitmap,
            [](const ClipboardContent& src) -> std::optional<ClipboardContent> {
                // In a real implementation, decode PNG to BMP DIB
                // For now, wrap with a minimal BMP header if possible
                ClipboardContent result(ClipboardFormat::kBitmap);
                result.data = src.data; // Passthrough with format change
                result.mime_type = "image/bmp";
                return result;
            });

        // Bitmap to PNG
        add_converter(ClipboardFormat::kBitmap, ClipboardFormat::kPNG,
            [](const ClipboardContent& src) -> std::optional<ClipboardContent> {
                ClipboardContent result(ClipboardFormat::kPNG);
                result.data = src.data; // Placeholder
                result.mime_type = "image/png";
                return result;
            });

        // PNG to JPEG
        add_converter(ClipboardFormat::kPNG, ClipboardFormat::kJPEG,
            [](const ClipboardContent& src) -> std::optional<ClipboardContent> {
                ClipboardContent result(ClipboardFormat::kJPEG);
                result.data = src.data; // Placeholder
                result.mime_type = "image/jpeg";
                return result;
            });

        // FileList to FileContents (create bundle)
        add_converter(ClipboardFormat::kFileList, ClipboardFormat::kFileContents,
            [](const ClipboardContent& src) -> std::optional<ClipboardContent> {
                ClipboardContent result(ClipboardFormat::kFileContents);
                result.data = src.data;
                result.file_list = src.file_list;
                return result;
            });
    }

    void add_converter(ClipboardFormat from, ClipboardFormat to, ConverterFunc fn) {
        converters_[{from, to}] = std::move(fn);
        spdlog::trace("[ClipboardFormatNegotiator] Registered converter: {} -> {}",
                      format_name(from), format_name(to));
    }

    static bool is_format_allowed(ClipboardFormat fmt,
                                   const ClipboardSyncPolicy& policy) noexcept {
        if (!policy.allowed_formats.empty() &&
            !policy.allowed_formats.contains(fmt)) {
            return false;
        }
        if (policy.blocked_formats.contains(fmt)) {
            return false;
        }
        return true;
    }
};

// ============================================================================
// ClipboardTransferEngine — chunked/incremental file transfer
// ============================================================================

class ClipboardTransferEngine {
public:
    ClipboardTransferEngine() {
        spdlog::info("[ClipboardTransferEngine] Initialized with chunk_size={}", kChunkSize);
    }

    // --- Chunked transfer ---

    /// Split content into chunks for incremental transfer.
    [[nodiscard]] std::vector<std::vector<uint8_t>> chunkify(
            const ClipboardContent& content) const {
        std::vector<std::vector<uint8_t>> chunks;
        const auto& data = content.data;
        if (data.size() <= kChunkSize) {
            chunks.push_back(data); // Single chunk
            return chunks;
        }

        size_t num_chunks = (data.size() + kChunkSize - 1) / kChunkSize;
        chunks.reserve(num_chunks);
        for (size_t offset = 0; offset < data.size(); offset += kChunkSize) {
            size_t chunk_len = std::min(kChunkSize, data.size() - offset);
            chunks.emplace_back(data.begin() + offset, 
                                data.begin() + offset + chunk_len);
        }

        spdlog::debug("[ClipboardTransferEngine] Content chunkified: {} bytes -> {} chunks",
                      data.size(), chunks.size());
        return chunks;
    }

    /// Reassemble chunks into full content.
    [[nodiscard]] ClipboardContent assemble_chunks(
            ClipboardFormat fmt,
            const std::vector<std::vector<uint8_t>>& chunks,
            uint64_t expected_size) const {

        ClipboardContent result(fmt);
        result.data.reserve(expected_size);
        for (const auto& chunk : chunks) {
            result.data.insert(result.data.end(), chunk.begin(), chunk.end());
        }
        spdlog::debug("[ClipboardTransferEngine] Assembled {} chunks into {} bytes",
                      chunks.size(), result.data.size());
        return result;
    }

    // --- Incremental file transfer ---

    /// Compute delta between two versions of content for incremental transfer.
    [[nodiscard]] std::optional<std::vector<uint8_t>> compute_delta(
            const ClipboardContent& old_content,
            const ClipboardContent& new_content) const {

        if (old_content.format != new_content.format) {
            return std::nullopt; // Format changed; full transfer needed
        }

        if (new_content.data.size() > kIncrementalThreshold) {
            // For large data, compute a simple diff
            return compute_simple_delta(old_content.data, new_content.data);
        }

        // Small data: delta not worth it
        return std::nullopt;
    }

    /// Apply delta to base content to produce new content.
    [[nodiscard]] std::optional<ClipboardContent> apply_delta(
            const ClipboardContent& base,
            const std::vector<uint8_t>& delta) const {

        if (delta.size() < sizeof(uint32_t) * 2) {
            return std::nullopt; // Invalid delta header
        }

        ClipboardContent result(base);
        apply_simple_delta(result.data, delta);
        result.timestamp = std::chrono::steady_clock::now();
        return result;
    }

    /// Determine whether incremental transfer should be used.
    [[nodiscard]] bool should_use_incremental(
            const ClipboardContent& current,
            const std::optional<ClipboardContent>& previous) const {
        if (!previous.has_value()) return false;
        if (current.format != previous->format) return false;
        if (current.data.size() < kIncrementalThreshold) return false;
        if (previous->data.size() < kIncrementalThreshold) return false;

        // Use incremental if the change is less than 30% of total size
        size_t diff_size = current.data.size() > previous->data.size() ?
            current.data.size() - previous->data.size() :
            previous->data.size() - current.data.size();
        return (static_cast<double>(diff_size) / 
                static_cast<double>(std::max(current.data.size(), previous->data.size()))) < 0.30;
    }

    // --- File list handling ---

    /// Read a list of files into clipboard content.
    [[nodiscard]] ClipboardContent create_file_transfer(
            const std::vector<std::filesystem::path>& paths) {

        ClipboardContent content(ClipboardFormat::kFileList);
        content.file_list = paths;

        // Compute total file size
        uint64_t total_size = 0;
        std::vector<std::string> path_strings;
        path_strings.reserve(paths.size());
        for (const auto& p : paths) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(p, ec)) {
                total_size += std::filesystem::file_size(p, ec);
            }
            path_strings.push_back(p.string());
        }
        content.total_file_size = total_size;

        // Serialize file paths into data
        std::ostringstream oss;
        for (const auto& ps : path_strings) {
            oss << ps << '\n';
        }
        std::string serialized = oss.str();
        content.data.assign(serialized.begin(), serialized.end());

        spdlog::debug("[ClipboardTransferEngine] Created file transfer: {} files, {} bytes",
                      paths.size(), total_size);
        return content;
    }

    /// Deserialize file list from clipboard content.
    [[nodiscard]] std::vector<std::filesystem::path> parse_file_list(
            const ClipboardContent& content) const {
        std::vector<std::filesystem::path> paths;
        std::string_view data(reinterpret_cast<const char*>(content.data.data()),
                              content.data.size());
        size_t start = 0;
        for (size_t i = 0; i <= data.size(); ++i) {
            if (i == data.size() || data[i] == '\n' || data[i] == '\0') {
                if (i > start) {
                    paths.emplace_back(data.substr(start, i - start));
                }
                start = i + 1;
            }
        }
        return paths;
    }

private:
    // Simple delta: record changed byte ranges (offset, length, data)
    [[nodiscard]] std::vector<uint8_t> compute_simple_delta(
            const std::vector<uint8_t>& old_data,
            const std::vector<uint8_t>& new_data) const {

        std::vector<uint8_t> delta;
        // Header: old_size (4 bytes) + new_size (4 bytes) + num_ranges (4 bytes)
        auto old_size = static_cast<uint32_t>(old_data.size());
        auto new_size = static_cast<uint32_t>(new_data.size());

        // Reserve space for header
        delta.resize(12, 0);
        std::memcpy(delta.data(), &old_size, 4);
        std::memcpy(delta.data() + 4, &new_size, 4);

        // Find changed ranges
        struct Range { uint32_t offset; uint32_t length; };
        std::vector<Range> ranges;
        std::vector<uint8_t> range_data;

        size_t max_len = std::max(old_data.size(), new_data.size());
        size_t range_start = 0;
        bool in_range = false;

        for (size_t i = 0; i <= max_len; ++i) {
            bool diff = false;
            if (i < old_data.size() && i < new_data.size()) {
                diff = (old_data[i] != new_data[i]);
            } else if (i < old_data.size() || i < new_data.size()) {
                diff = true; // Length mismatch
            }

            if (diff && !in_range) {
                range_start = i;
                in_range = true;
            } else if (!diff && in_range) {
                size_t range_len = i - range_start;
                ranges.push_back({static_cast<uint32_t>(range_start),
                                  static_cast<uint32_t>(range_len)});
                range_data.insert(range_data.end(),
                                  new_data.begin() + range_start,
                                  new_data.begin() + i);
                in_range = false;
            }
        }
        if (in_range) {
            size_t range_len = max_len - range_start;
            ranges.push_back({static_cast<uint32_t>(range_start),
                              static_cast<uint32_t>(range_len)});
            for (size_t j = range_start; j < new_data.size(); ++j) {
                range_data.push_back(new_data[j]);
            }
        }

        auto num_ranges = static_cast<uint32_t>(ranges.size());
        std::memcpy(delta.data() + 8, &num_ranges, 4);

        // Range descriptors: each [offset(4) + length(4)]
        for (const auto& r : ranges) {
            uint8_t buf[8];
            std::memcpy(buf, &r.offset, 4);
            std::memcpy(buf + 4, &r.length, 4);
            delta.insert(delta.end(), buf, buf + 8);
        }

        // Range data
        delta.insert(delta.end(), range_data.begin(), range_data.end());

        return delta;
    }

    void apply_simple_delta(std::vector<uint8_t>& base,
                            const std::vector<uint8_t>& delta) const {
        if (delta.size() < 12) return;

        uint32_t old_size, new_size, num_ranges;
        std::memcpy(&old_size, delta.data(), 4);
        std::memcpy(&new_size, delta.data() + 4, 4);
        std::memcpy(&num_ranges, delta.data() + 8, 4);

        base.resize(new_size, 0);

        size_t offset = 12;
        for (uint32_t i = 0; i < num_ranges && offset + 8 <= delta.size(); ++i) {
            uint32_t range_offset, range_len;
            std::memcpy(&range_offset, delta.data() + offset, 4);
            std::memcpy(&range_len, delta.data() + offset + 4, 4);
            offset += 8;

            if (offset + range_len <= delta.size()) {
                std::memcpy(base.data() + range_offset,
                            delta.data() + offset, range_len);
                offset += range_len;
            }
        }
    }
};

// ============================================================================
// ClipboardPermissionManager — access control for clipboard operations
// ============================================================================

class ClipboardPermissionManager {
public:
    enum class PermissionLevel : uint8_t {
        kDenied     = 0,
        kReadOnly   = 1,
        kWriteOnly  = 2,
        kReadWrite  = 3,
        kFullAccess = 4,
    };

    struct PermissionRule {
        PermissionLevel level{PermissionLevel::kDenied};
        std::optional<std::string> session_pattern;
        std::optional<ClipboardFormat> format_restriction;
        bool can_read{false};
        bool can_write{false};
        std::chrono::steady_clock::time_point created_at;
        std::optional<std::chrono::steady_clock::time_point> expires_at;
        std::string description;
    };

    ClipboardPermissionManager() {
        spdlog::info("[ClipboardPermission] Initialized");
        // Default: allow read/write for all sessions
        default_rule_.level = PermissionLevel::kReadWrite;
        default_rule_.can_read = true;
        default_rule_.can_write = true;
        default_rule_.description = "Default allow-all";
    }

    // --- Permission checks ---

    [[nodiscard]] bool can_read(const std::string& session_id,
                                 ClipboardFormat fmt = ClipboardFormat::kUnknown) const {
        std::shared_lock lock(mutex_);
        auto rule = find_applicable_rule(session_id, fmt);
        return rule && rule->can_read;
    }

    [[nodiscard]] bool can_write(const std::string& session_id,
                                  ClipboardFormat fmt = ClipboardFormat::kUnknown) const {
        std::shared_lock lock(mutex_);
        auto rule = find_applicable_rule(session_id, fmt);
        return rule && rule->can_write;
    }

    [[nodiscard]] PermissionLevel get_level(const std::string& session_id,
                                              ClipboardFormat fmt = ClipboardFormat::kUnknown) const {
        std::shared_lock lock(mutex_);
        auto rule = find_applicable_rule(session_id, fmt);
        return rule ? rule->level : PermissionLevel::kDenied;
    }

    // --- Rule management ---

    void add_rule(const std::string& session_pattern,
                  PermissionLevel level,
                  const std::string& description = {}) {
        std::unique_lock lock(mutex_);
        PermissionRule rule;
        rule.level = level;
        rule.session_pattern = session_pattern;
        rule.can_read = (level == PermissionLevel::kReadOnly ||
                         level == PermissionLevel::kReadWrite ||
                         level == PermissionLevel::kFullAccess);
        rule.can_write = (level == PermissionLevel::kWriteOnly ||
                          level == PermissionLevel::kReadWrite ||
                          level == PermissionLevel::kFullAccess);
        rule.created_at = std::chrono::steady_clock::now();
        rule.description = description;
        rules_.push_back(std::move(rule));
        spdlog::info("[ClipboardPermission] Added rule: pattern='{}', level={}, desc='{}'",
                     session_pattern, static_cast<int>(level), description);
    }

    void remove_rule(const std::string& session_pattern) {
        std::unique_lock lock(mutex_);
        std::erase_if(rules_, [&](const PermissionRule& r) {
            return r.session_pattern == session_pattern;
        });
    }

    void set_default_level(PermissionLevel level) {
        std::unique_lock lock(mutex_);
        default_rule_.level = level;
        default_rule_.can_read = (level == PermissionLevel::kReadOnly ||
                                  level == PermissionLevel::kReadWrite ||
                                  level == PermissionLevel::kFullAccess);
        default_rule_.can_write = (level == PermissionLevel::kWriteOnly ||
                                   level == PermissionLevel::kReadWrite ||
                                   level == PermissionLevel::kFullAccess);
        spdlog::info("[ClipboardPermission] Default level set to {}", static_cast<int>(level));
    }

    void set_format_policy(const std::string& session_pattern,
                            const std::set<ClipboardFormat>& allowed,
                            const std::set<ClipboardFormat>& blocked) {
        std::unique_lock lock(mutex_);
        session_format_policies_[session_pattern] = {allowed, blocked};
    }

    /// Check if a specific format is permitted for a session.
    [[nodiscard]] bool is_format_permitted(const std::string& session_id,
                                             ClipboardFormat fmt) const {
        std::shared_lock lock(mutex_);
        auto it = session_format_policies_.find(session_id);
        if (it == session_format_policies_.end()) return true;
        const auto& [allowed, blocked] = it->second;
        if (blocked.contains(fmt)) return false;
        if (!allowed.empty() && !allowed.contains(fmt)) return false;
        return true;
    }

    /// Check all preconditions for a clipboard operation.
    [[nodiscard]] bool check_operation(const std::string& session_id,
                                         ClipboardFormat fmt,
                                         bool is_read) const {
        if (is_read) return can_read(session_id, fmt) && is_format_permitted(session_id, fmt);
        return can_write(session_id, fmt) && is_format_permitted(session_id, fmt);
    }

    // --- Maintenance ---

    void purge_expired_rules() {
        std::unique_lock lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        std::erase_if(rules_, [&](const PermissionRule& r) {
            return r.expires_at.has_value() && *r.expires_at < now;
        });
    }

    [[nodiscard]] size_t rule_count() const noexcept {
        std::shared_lock lock(mutex_);
        return rules_.size();
    }

private:
    [[nodiscard]] const PermissionRule* find_applicable_rule(
            const std::string& session_id, ClipboardFormat fmt) const {
        // Check session-specific rules (most specific first)
        for (auto it = rules_.rbegin(); it != rules_.rend(); ++it) {
            if (!it->session_pattern.has_value()) continue;
            if (session_matches(session_id, *it->session_pattern)) {
                if (!it->format_restriction.has_value() ||
                    it->format_restriction == fmt) {
                    return &(*it);
                }
            }
        }
        return &default_rule_;
    }

    static bool session_matches(const std::string& session_id,
                                const std::string& pattern) noexcept {
        // Simple glob-like matching: supports * and ?
        size_t si = 0, pi = 0;
        size_t star_idx = std::string::npos;
        size_t match_idx = 0;

        while (si < session_id.size()) {
            if (pi < pattern.size() && (pattern[pi] == '?' ||
                pattern[pi] == session_id[si])) {
                ++si; ++pi;
            } else if (pi < pattern.size() && pattern[pi] == '*') {
                star_idx = pi;
                match_idx = si;
                ++pi;
            } else if (star_idx != std::string::npos) {
                pi = star_idx + 1;
                match_idx++;
                si = match_idx;
            } else {
                return false;
            }
        }

        while (pi < pattern.size() && pattern[pi] == '*') ++pi;
        return pi == pattern.size();
    }

    mutable std::shared_mutex mutex_;
    PermissionRule default_rule_;
    std::vector<PermissionRule> rules_;
    std::unordered_map<std::string,
        std::pair<std::set<ClipboardFormat>, std::set<ClipboardFormat>>>
        session_format_policies_;
};

// ============================================================================
// ClipboardPlatformBridge — platform-specific clipboard I/O
// ============================================================================

class ClipboardPlatformBridge {
public:
    ClipboardPlatformBridge() {
        spdlog::info("[ClipboardPlatformBridge] Initializing platform bridge");
        initialize_platform();
    }

    ~ClipboardPlatformBridge() {
        shutdown_platform();
        spdlog::info("[ClipboardPlatformBridge] Shutdown");
    }

    // --- Lifecycle ---

    bool is_available() const noexcept {
        return platform_available_.load(std::memory_order_acquire);
    }

    // --- Read operations ---

    /// Get list of currently available clipboard formats.
    [[nodiscard]] std::vector<ClipboardFormat> get_available_formats() const {
        if (!is_available()) return {};
        auto stopwatch = spdlog::stopwatch{};
        auto result = platform_get_formats();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(stopwatch.elapsed());
        spdlog::trace("[ClipboardPlatformBridge] get_available_formats: {} formats, {}us",
                      result.size(), elapsed.count());
        return result;
    }

    /// Read clipboard content in the specified format.
    [[nodiscard]] std::optional<ClipboardContent> read_clipboard(
            ClipboardFormat fmt) const {
        if (!is_available()) return std::nullopt;
        auto stopwatch = spdlog::stopwatch{};
        auto result = platform_read(fmt);
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(stopwatch.elapsed());
        if (result) {
            spdlog::debug("[ClipboardPlatformBridge] Read {}: {} bytes, {}us",
                         ClipboardFormatNegotiator::format_name(fmt),
                         result->data.size(), elapsed.count());
        }
        return result;
    }

    /// Read clipboard content in the best available format from a list.
    [[nodiscard]] std::optional<ClipboardContent> read_clipboard_best(
            const std::vector<ClipboardFormat>& preferred_formats) const {
        if (!is_available()) return std::nullopt;

        auto available = get_available_formats();
        std::unordered_set<ClipboardFormat> avail_set(available.begin(), available.end());

        for (auto fmt : preferred_formats) {
            if (avail_set.contains(fmt)) {
                return read_clipboard(fmt);
            }
        }

        // Fallback: read the first available format
        if (!available.empty()) {
            return read_clipboard(available.front());
        }

        return std::nullopt;
    }

    // --- Write operations ---

    /// Write content to the system clipboard.
    bool write_clipboard(const ClipboardContent& content) {
        if (!is_available()) return false;
        auto stopwatch = spdlog::stopwatch{};
        bool ok = platform_write(content);
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(stopwatch.elapsed());
        spdlog::debug("[ClipboardPlatformBridge] Write {}: {} bytes, {}us, success={}",
                     ClipboardFormatNegotiator::format_name(content.format),
                     content.data.size(), elapsed.count(), ok);
        return ok;
    }

    /// Clear the system clipboard.
    bool clear_clipboard() {
        if (!is_available()) return false;
        spdlog::debug("[ClipboardPlatformBridge] Clearing clipboard");
        return platform_clear();
    }

    /// Check if clipboard has changed since last check (optimized).
    [[nodiscard]] bool has_changed() const noexcept {
        if (!is_available()) return false;
        uint64_t current_seq = platform_get_sequence_number();
        bool changed = (current_seq != last_sequence_number_);
        if (changed) {
            last_sequence_number_ = current_seq;
        }
        return changed;
    }

    [[nodiscard]] uint64_t sequence_number() const noexcept {
        if (!is_available()) return 0;
        return platform_get_sequence_number();
    }

    // --- Platform information ---

    [[nodiscard]] static constexpr std::string_view platform_name() noexcept {
#if CLIPBOARD_PLATFORM_WINDOWS
        return "Windows OLE";
#elif CLIPBOARD_PLATFORM_MACOS
        return "macOS NSPasteboard";
#elif CLIPBOARD_PLATFORM_X11
        return "X11 Selections";
#else
        return "Generic";
#endif
    }

    [[nodiscard]] static std::string platform_info() {
        return std::format("ClipboardPlatformBridge[{}]", platform_name());
    }

private:
    // -----------------------------------------------------------------------
    // Platform initialization
    // -----------------------------------------------------------------------

    void initialize_platform() {
        try {
#if CLIPBOARD_PLATFORM_WINDOWS
            initialize_windows();
#elif CLIPBOARD_PLATFORM_MACOS
            initialize_macos();
#elif CLIPBOARD_PLATFORM_X11
            initialize_x11();
#else
            spdlog::warn("[ClipboardPlatformBridge] No platform-specific clipboard support");
            platform_available_.store(false, std::memory_order_release);
            return;
#endif
            platform_available_.store(true, std::memory_order_release);
            spdlog::info("[ClipboardPlatformBridge] Platform initialized: {}", platform_name());
        } catch (const std::exception& e) {
            spdlog::error("[ClipboardPlatformBridge] Platform init failed: {}", e.what());
            platform_available_.store(false, std::memory_order_release);
        }
    }

    void shutdown_platform() {
#if CLIPBOARD_PLATFORM_WINDOWS
        shutdown_windows();
#elif CLIPBOARD_PLATFORM_MACOS
        shutdown_macos();
#elif CLIPBOARD_PLATFORM_X11
        shutdown_x11();
#endif
    }

    // -----------------------------------------------------------------------
    // Platform-specific read
    // -----------------------------------------------------------------------

    std::optional<ClipboardContent> platform_read(ClipboardFormat fmt) const {
#if CLIPBOARD_PLATFORM_WINDOWS
        return platform_read_windows(fmt);
#elif CLIPBOARD_PLATFORM_MACOS
        return platform_read_macos(fmt);
#elif CLIPBOARD_PLATFORM_X11
        return platform_read_x11(fmt);
#else
        (void)fmt;
        return std::nullopt;
#endif
    }

    // -----------------------------------------------------------------------
    // Platform-specific write
    // -----------------------------------------------------------------------

    bool platform_write(const ClipboardContent& content) {
#if CLIPBOARD_PLATFORM_WINDOWS
        return platform_write_windows(content);
#elif CLIPBOARD_PLATFORM_MACOS
        return platform_write_macos(content);
#elif CLIPBOARD_PLATFORM_X11
        return platform_write_x11(content);
#else
        (void)content;
        return false;
#endif
    }

    // -----------------------------------------------------------------------
    // Platform-specific format enumeration
    // -----------------------------------------------------------------------

    std::vector<ClipboardFormat> platform_get_formats() const {
#if CLIPBOARD_PLATFORM_WINDOWS
        return platform_get_formats_windows();
#elif CLIPBOARD_PLATFORM_MACOS
        return platform_get_formats_macos();
#elif CLIPBOARD_PLATFORM_X11
        return platform_get_formats_x11();
#else
        return {};
#endif
    }

    // -----------------------------------------------------------------------
    // Platform-specific clear
    // -----------------------------------------------------------------------

    bool platform_clear() {
#if CLIPBOARD_PLATFORM_WINDOWS
        return platform_clear_windows();
#elif CLIPBOARD_PLATFORM_MACOS
        return platform_clear_macos();
#elif CLIPBOARD_PLATFORM_X11
        return platform_clear_x11();
#else
        return false;
#endif
    }

    // -----------------------------------------------------------------------
    // Platform-specific sequence number
    // -----------------------------------------------------------------------

    uint64_t platform_get_sequence_number() const {
#if CLIPBOARD_PLATFORM_WINDOWS
        return platform_seqnum_windows();
#elif CLIPBOARD_PLATFORM_MACOS
        return platform_seqnum_macos();
#elif CLIPBOARD_PLATFORM_X11
        return platform_seqnum_x11();
#else
        return 0;
#endif
    }

    // ====================================================================
    // WINDOWS IMPLEMENTATION
    // ====================================================================
#if CLIPBOARD_PLATFORM_WINDOWS

    struct WindowsClipState {
        HWND hwnd{nullptr};
        uint32_t next_clipboard_viewer{0};
        bool initialized{false};
        std::mutex mutex;
    };

    mutable WindowsClipState win_state_;

    // Map Windows CF_* to our ClipboardFormat enum
    static ClipboardFormat map_windows_format(UINT cf) noexcept {
        switch (cf) {
            case CF_UNICODETEXT: return ClipboardFormat::kUnicodeText;
            case CF_TEXT:        return ClipboardFormat::kOEMText;
            case CF_BITMAP:      return ClipboardFormat::kBitmap;
            case CF_DIB:         return ClipboardFormat::kBitmap;
            case CF_DIBV5:       return ClipboardFormat::kBitmap;
            case CF_METAFILEPICT:return ClipboardFormat::kMetafilePict;
            case CF_ENHMETAFILE: return ClipboardFormat::kEnhancedMetafile;
            case CF_PALETTE:     return ClipboardFormat::kPalette;
            case CF_HDROP:       return ClipboardFormat::kFileList;
            default: {
                // Check for registered formats
                char name[256] = {};
                if (GetClipboardFormatNameA(cf, name, sizeof(name)) > 0) {
                    std::string_view n(name);
                    if (n.find("HTML") != std::string_view::npos) return ClipboardFormat::kHTML;
                    if (n.find("RTF") != std::string_view::npos) return ClipboardFormat::kRTFText;
                    if (n.find("PNG") != std::string_view::npos) return ClipboardFormat::kPNG;
                    if (n.find("JFIF") != std::string_view::npos) return ClipboardFormat::kJPEG;
                    if (n.find("GIF") != std::string_view::npos) return ClipboardFormat::kGIF;
                    if (n.find("Rich Text") != std::string_view::npos) return ClipboardFormat::kRTFText;
                }
                return ClipboardFormat::kCustomBinary;
            }
        }
    }

    static UINT map_to_windows_format(ClipboardFormat fmt) noexcept {
        switch (fmt) {
            case ClipboardFormat::kUnicodeText: return CF_UNICODETEXT;
            case ClipboardFormat::kOEMText:     return CF_TEXT;
            case ClipboardFormat::kBitmap:      return CF_DIB;
            case ClipboardFormat::kFileList:    return CF_HDROP;
            case ClipboardFormat::kMetafilePict:return CF_METAFILEPICT;
            case ClipboardFormat::kEnhancedMetafile: return CF_ENHMETAFILE;
            case ClipboardFormat::kPalette:     return CF_PALETTE;
            default: {
                static std::unordered_map<ClipboardFormat, UINT> registered;
                auto it = registered.find(fmt);
                if (it != registered.end()) return it->second;
                std::string name = std::format("cppdesk_format_{}", static_cast<uint32_t>(fmt));
                UINT id = RegisterClipboardFormatA(name.c_str());
                registered[fmt] = id;
                return id;
            }
        }
    }

    void initialize_windows() {
        // Register a dummy window class for clipboard operations
        HINSTANCE hinst = GetModuleHandleA(nullptr);
        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof(WNDCLASSEXA);
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = hinst;
        wc.lpszClassName = "cppdesk_ClipboardBridge";
        RegisterClassExA(&wc);

        win_state_.hwnd = CreateWindowExA(
            0, "cppdesk_ClipboardBridge", "ClipboardBridge",
            0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hinst, nullptr);

        if (win_state_.hwnd) {
            win_state_.next_clipboard_viewer = 
                SetClipboardViewer(win_state_.hwnd);
            win_state_.initialized = true;
        }

        OleInitialize(nullptr);
    }

    void shutdown_windows() {
        if (win_state_.hwnd) {
            ChangeClipboardChain(win_state_.hwnd, win_state_.next_clipboard_viewer);
            DestroyWindow(win_state_.hwnd);
            win_state_.hwnd = nullptr;
        }
        win_state_.initialized = false;
        OleUninitialize();
    }

    std::optional<ClipboardContent> platform_read_windows(ClipboardFormat fmt) const {
        if (!OpenClipboard(win_state_.hwnd)) {
            spdlog::warn("[ClipboardPlatformBridge] OpenClipboard failed");
            return std::nullopt;
        }

        std::optional<ClipboardContent> result;
        UINT cf = map_to_windows_format(fmt);

        HGLOBAL hmem = GetClipboardData(cf);
        if (hmem) {
            void* data = GlobalLock(hmem);
            if (data) {
                size_t size = GlobalSize(hmem);
                ClipboardContent content(fmt);
                content.data.resize(size);
                std::memcpy(content.data.data(), data, size);
                result = std::move(content);
                GlobalUnlock(hmem);
            }
        }

        CloseClipboard();
        return result;
    }

    bool platform_write_windows(const ClipboardContent& content) {
        if (!OpenClipboard(win_state_.hwnd)) {
            spdlog::warn("[ClipboardPlatformBridge] OpenClipboard for write failed");
            return false;
        }

        EmptyClipboard();

        UINT cf = map_to_windows_format(content.format);
        HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, content.data.size());
        if (hmem) {
            void* data = GlobalLock(hmem);
            if (data) {
                std::memcpy(data, content.data.data(), content.data.size());
                GlobalUnlock(hmem);
            }
            SetClipboardData(cf, hmem);
        }

        CloseClipboard();
        return true;
    }

    std::vector<ClipboardFormat> platform_get_formats_windows() const {
        std::vector<ClipboardFormat> formats;
        if (!OpenClipboard(win_state_.hwnd)) return formats;

        UINT format = 0;
        while ((format = EnumClipboardFormats(format)) != 0) {
            ClipboardFormat cf = map_windows_format(format);
            if (cf != ClipboardFormat::kUnknown) {
                formats.push_back(cf);
            }
        }

        CloseClipboard();
        return formats;
    }

    bool platform_clear_windows() {
        if (!OpenClipboard(win_state_.hwnd)) return false;
        EmptyClipboard();
        CloseClipboard();
        return true;
    }

    uint64_t platform_seqnum_windows() const {
        return static_cast<uint64_t>(GetClipboardSequenceNumber());
    }

    // ====================================================================
    // MACOS IMPLEMENTATION
    // ====================================================================
#elif CLIPBOARD_PLATFORM_MACOS

    struct MacClipState {
        NSPasteboard* pasteboard{nullptr};
        NSInteger last_change_count{0};
    };

    mutable MacClipState mac_state_;

    static ClipboardFormat map_macos_type(NSString* type) noexcept {
        if ([type isEqualToString:NSPasteboardTypeString]) return ClipboardFormat::kUnicodeText;
        if ([type isEqualToString:NSPasteboardTypeRTF]) return ClipboardFormat::kRTFText;
        if ([type isEqualToString:NSPasteboardTypeHTML]) return ClipboardFormat::kHTML;
        if ([type isEqualToString:NSPasteboardTypePNG]) return ClipboardFormat::kPNG;
        if ([type isEqualToString:NSPasteboardTypeTIFF]) return ClipboardFormat::kTIFF;
        if ([type isEqualToString:(NSString*)kUTTypeFileURL]) return ClipboardFormat::kFileList;
        return ClipboardFormat::kCustomBinary;
    }

    static NSString* map_to_macos_type(ClipboardFormat fmt) noexcept {
        switch (fmt) {
            case ClipboardFormat::kUnicodeText: return NSPasteboardTypeString;
            case ClipboardFormat::kRTFText:     return NSPasteboardTypeRTF;
            case ClipboardFormat::kHTML:        return NSPasteboardTypeHTML;
            case ClipboardFormat::kPNG:         return NSPasteboardTypePNG;
            case ClipboardFormat::kTIFF:        return NSPasteboardTypeTIFF;
            default: {
                return [NSString stringWithFormat:@"com.cppdesk.format.%u",
                        static_cast<uint32_t>(fmt)];
            }
        }
    }

    void initialize_macos() {
        mac_state_.pasteboard = [NSPasteboard generalPasteboard];
        mac_state_.last_change_count = [mac_state_.pasteboard changeCount];
    }

    void shutdown_macos() {
        mac_state_.pasteboard = nullptr;
    }

    std::optional<ClipboardContent> platform_read_macos(ClipboardFormat fmt) const {
        NSPasteboard* pb = mac_state_.pasteboard;
        if (!pb) return std::nullopt;

        NSString* nstype = map_to_macos_type(fmt);
        NSData* nsdata = [pb dataForType:nstype];
        if (!nsdata) return std::nullopt;

        ClipboardContent content(fmt);
        content.data.resize([nsdata length]);
        std::memcpy(content.data.data(), [nsdata bytes], [nsdata length]);
        return content;
    }

    bool platform_write_macos(const ClipboardContent& content) {
        NSPasteboard* pb = mac_state_.pasteboard;
        if (!pb) return false;

        [pb clearContents];
        NSString* nstype = map_to_macos_type(content.format);
        NSData* nsdata = [NSData dataWithBytes:content.data.data()
                                        length:content.data.size()];
        return [pb setData:nsdata forType:nstype];
    }

    std::vector<ClipboardFormat> platform_get_formats_macos() const {
        std::vector<ClipboardFormat> formats;
        NSPasteboard* pb = mac_state_.pasteboard;
        if (!pb) return formats;

        NSArray* types = [pb types];
        for (NSString* type in types) {
            ClipboardFormat cf = map_macos_type(type);
            if (cf != ClipboardFormat::kUnknown) {
                formats.push_back(cf);
            }
        }
        return formats;
    }

    bool platform_clear_macos() {
        NSPasteboard* pb = mac_state_.pasteboard;
        if (!pb) return false;
        [pb clearContents];
        return true;
    }

    uint64_t platform_seqnum_macos() const {
        return static_cast<uint64_t>([mac_state_.pasteboard changeCount]);
    }

    // ====================================================================
    // X11 IMPLEMENTATION
    // ====================================================================
#elif CLIPBOARD_PLATFORM_X11

    struct X11ClipState {
        Display* display{nullptr};
        Window window{0};
        Atom atom_clipboard{0};
        Atom atom_primary{0};
        Atom atom_secondary{0};
        Atom atom_targets{0};
        Atom atom_utf8_string{0};
        Atom atom_image_png{0};
        Atom atom_text_uri_list{0};
        Atom atom_incr{0};
        Atom atom_multiple{0};
        Atom atom_timestamp{0};
        int xfixes_event_base{0};
        int xfixes_error_base{0};
        bool has_xfixes{false};
        mutable std::mutex mutex;
        std::atomic<uint64_t> seqnum{0};
        bool initialized{false};
    };

    mutable X11ClipState x11_state_;

    // Atom cache
    struct AtomCache {
        Atom atom_clipboard_format{0};
        std::unordered_map<ClipboardFormat, Atom> format_to_atom;
        std::unordered_map<Atom, ClipboardFormat> atom_to_format;
    };
    mutable AtomCache atom_cache_;

    void initialize_x11() {
        x11_state_.display = XOpenDisplay(nullptr);
        if (!x11_state_.display) {
            throw std::runtime_error("Cannot open X11 display");
        }

        auto* dpy = x11_state_.display;
        int screen = DefaultScreen(dpy);
        x11_state_.window = XCreateSimpleWindow(
            dpy, RootWindow(dpy, screen), 0, 0, 1, 1, 0, 0, 0);

        // Intern standard atoms
        x11_state_.atom_clipboard = XInternAtom(dpy, "CLIPBOARD", False);
        x11_state_.atom_primary   = XInternAtom(dpy, "PRIMARY", False);
        x11_state_.atom_secondary = XInternAtom(dpy, "SECONDARY", False);
        x11_state_.atom_targets   = XInternAtom(dpy, "TARGETS", False);
        x11_state_.atom_utf8_string = XInternAtom(dpy, "UTF8_STRING", False);
        x11_state_.atom_image_png = XInternAtom(dpy, "image/png", False);
        x11_state_.atom_text_uri_list = XInternAtom(dpy, "text/uri-list", False);
        x11_state_.atom_incr      = XInternAtom(dpy, "INCR", False);
        x11_state_.atom_multiple  = XInternAtom(dpy, "MULTIPLE", False);
        x11_state_.atom_timestamp = XInternAtom(dpy, "TIMESTAMP", False);

        // Register custom formats
        register_x11_format(ClipboardFormat::kRTFText, "text/rtf");
        register_x11_format(ClipboardFormat::kHTML, "text/html");
        register_x11_format(ClipboardFormat::kJPEG, "image/jpeg");
        register_x11_format(ClipboardFormat::kGIF, "image/gif");
        register_x11_format(ClipboardFormat::kTIFF, "image/tiff");
        register_x11_format(ClipboardFormat::kBitmap, "image/bmp");

        // Try XFixes for event-based monitoring
        int event_base, error_base;
        if (XFixesQueryExtension(dpy, &event_base, &error_base)) {
            x11_state_.has_xfixes = true;
            x11_state_.xfixes_event_base = event_base;
            x11_state_.xfixes_error_base = error_base;
            XFixesSelectSelectionInput(dpy, x11_state_.window,
                                       x11_state_.atom_clipboard,
                                       XFixesSetSelectionOwnerNotifyMask |
                                       XFixesSelectionWindowDestroyNotifyMask |
                                       XFixesSelectionClientCloseNotifyMask);
            spdlog::info("[ClipboardPlatformBridge] XFixes enabled for event-based monitoring");
        } else {
            spdlog::info("[ClipboardPlatformBridge] XFixes not available, using polling fallback");
        }

        x11_state_.initialized = true;
        XFlush(dpy);
    }

    void shutdown_x11() {
        if (x11_state_.display && x11_state_.window) {
            XDestroyWindow(x11_state_.display, x11_state_.window);
            XCloseDisplay(x11_state_.display);
        }
        x11_state_.initialized = false;
    }

    void register_x11_format(ClipboardFormat fmt, const char* mime) {
        Atom atom = XInternAtom(x11_state_.display, mime, False);
        atom_cache_.format_to_atom[fmt] = atom;
        atom_cache_.atom_to_format[atom] = fmt;
    }

    Atom get_atom_for_format(ClipboardFormat fmt) const {
        switch (fmt) {
            case ClipboardFormat::kUnicodeText:
                return XInternAtom(x11_state_.display, "UTF8_STRING", False);
            case ClipboardFormat::kPNG:
                return x11_state_.atom_image_png;
            case ClipboardFormat::kFileList:
                return x11_state_.atom_text_uri_list;
            default: {
                auto it = atom_cache_.format_to_atom.find(fmt);
                if (it != atom_cache_.format_to_atom.end()) return it->second;
                std::string name = std::format("application/x-cppdesk-{}",
                                               static_cast<uint32_t>(fmt));
                Atom atom = XInternAtom(x11_state_.display, name.c_str(), False);
                atom_cache_.format_to_atom[fmt] = atom;
                atom_cache_.atom_to_format[atom] = fmt;
                return atom;
            }
        }
    }

    std::optional<ClipboardContent> platform_read_x11(ClipboardFormat fmt) const {
        auto* dpy = x11_state_.display;
        if (!dpy) return std::nullopt;

        Atom selection = x11_state_.atom_clipboard;
        Atom target = get_atom_for_format(fmt);

        // Request the selection content
        XConvertSelection(dpy, selection, target, selection,
                          x11_state_.window, CurrentTime);
        XFlush(dpy);

        // Wait for SelectionNotify
        XEvent event;
        for (int retry = 0; retry < 20; ++retry) {
            if (XCheckTypedWindowEvent(dpy, x11_state_.window,
                                        SelectionNotify, &event)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        if (event.type != SelectionNotify) {
            spdlog::warn("[ClipboardPlatformBridge] X11 SelectionNotify timeout");
            return std::nullopt;
        }

        if (event.xselection.property == None) {
            // Selection owner cannot provide the requested format
            return std::nullopt;
        }

        // Read the property
        return read_x11_property(event.xselection.property, fmt);
    }

    std::optional<ClipboardContent> read_x11_property(Atom property,
                                                        ClipboardFormat fmt) const {
        auto* dpy = x11_state_.display;
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char* prop_data = nullptr;

        // INCR (incremental) transfer for large data
        if (XGetWindowProperty(dpy, x11_state_.window, property,
                                0, 0, False, AnyPropertyType,
                                &actual_type, &actual_format,
                                &nitems, &bytes_after,
                                &prop_data) == Success) {
            if (prop_data) XFree(prop_data);

            if (actual_type == x11_state_.atom_incr) {
                return read_x11_incremental(fmt);
            }
        }

        // Normal transfer
        if (XGetWindowProperty(dpy, x11_state_.window, property,
                                0, kMaxImageSize / 4, False, AnyPropertyType,
                                &actual_type, &actual_format,
                                &nitems, &bytes_after,
                                &prop_data) != Success || !prop_data) {
            return std::nullopt;
        }

        ClipboardContent content(fmt);
        size_t bytes = nitems * (actual_format / 8);
        content.data.resize(bytes);
        std::memcpy(content.data.data(), prop_data, bytes);
        XFree(prop_data);

        // Delete the property
        XDeleteProperty(dpy, x11_state_.window, property);
        return content;
    }

    std::optional<ClipboardContent> read_x11_incremental(ClipboardFormat fmt) const {
        auto* dpy = x11_state_.display;
        ClipboardContent content(fmt);

        Atom property = XInternAtom(dpy, "CPPDESK_INCR_PROP", False);
        XSelectInput(dpy, x11_state_.window, PropertyChangeMask);

        bool done = false;
        while (!done) {
            XEvent event;
            XWindowEvent(dpy, x11_state_.window, PropertyChangeMask, &event);

            if (event.type == PropertyNotify &&
                event.xproperty.state == PropertyNewValue) {

                Atom actual_type;
                int actual_format;
                unsigned long nitems, bytes_after;
                unsigned char* prop_data = nullptr;

                XGetWindowProperty(dpy, x11_state_.window, property,
                                    0, kMaxImageSize / 4, True,
                                    AnyPropertyType,
                                    &actual_type, &actual_format,
                                    &nitems, &bytes_after,
                                    &prop_data);

                if (prop_data) {
                    if (nitems == 0) {
                        done = true; // Zero-length property signals end
                    } else {
                        size_t bytes = nitems * (actual_format / 8);
                        size_t old_size = content.data.size();
                        content.data.resize(old_size + bytes);
                        std::memcpy(content.data.data() + old_size, prop_data, bytes);
                    }
                    XFree(prop_data);
                } else {
                    done = true; // Error or empty
                }
            }
        }

        return content.data.empty() ? std::nullopt : std::optional(content);
    }

    bool platform_write_x11(const ClipboardContent& content) {
        auto* dpy = x11_state_.display;
        if (!dpy) return false;

        Atom selection = x11_state_.atom_clipboard;
        Atom target = get_atom_for_format(content.format);

        // Acquire selection ownership
        XSetSelectionOwner(dpy, selection, x11_state_.window, CurrentTime);
        if (XGetSelectionOwner(dpy, selection) != x11_state_.window) {
            spdlog::warn("[ClipboardPlatformBridge] X11 could not acquire selection ownership");
            return false;
        }

        // Store the data for later retrieval via SelectionRequest
        // In a full implementation, this would be stored in a per-format buffer
        // and served via a SelectionRequest handler. For this implementation,
        // we write directly to the CLIPBOARD manager.

        // Store property for the clipboard manager
        Atom property = XInternAtom(dpy, "CPPDESK_CLIP_PROP", False);
        XChangeProperty(dpy, x11_state_.window, property,
                        target, 8, PropModeReplace,
                        content.data.data(),
                        static_cast<int>(content.data.size()));

        XFlush(dpy);
        x11_state_.seqnum.fetch_add(1, std::memory_order_release);
        return true;
    }

    std::vector<ClipboardFormat> platform_get_formats_x11() const {
        std::vector<ClipboardFormat> formats;
        auto* dpy = x11_state_.display;
        if (!dpy) return formats;

        Atom selection = x11_state_.atom_clipboard;
        Atom targets = x11_state_.atom_targets;

        XConvertSelection(dpy, selection, targets, selection,
                          x11_state_.window, CurrentTime);
        XFlush(dpy);

        XEvent event;
        for (int retry = 0; retry < 10; ++retry) {
            if (XCheckTypedWindowEvent(dpy, x11_state_.window,
                                        SelectionNotify, &event)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (event.type != SelectionNotify || event.xselection.property == None) {
            return formats;
        }

        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char* prop_data = nullptr;

        XGetWindowProperty(dpy, x11_state_.window,
                           event.xselection.property,
                           0, 1024, False, AnyPropertyType,
                           &actual_type, &actual_format,
                           &nitems, &bytes_after,
                           &prop_data);

        if (prop_data && actual_type == XA_ATOM && actual_format == 32) {
            Atom* atoms = reinterpret_cast<Atom*>(prop_data);
            for (unsigned long i = 0; i < nitems; ++i) {
                if (atoms[i] == x11_state_.atom_utf8_string) {
                    formats.push_back(ClipboardFormat::kUnicodeText);
                } else if (atoms[i] == x11_state_.atom_image_png) {
                    formats.push_back(ClipboardFormat::kPNG);
                } else if (atoms[i] == x11_state_.atom_text_uri_list) {
                    formats.push_back(ClipboardFormat::kFileList);
                } else {
                    auto it = atom_cache_.atom_to_format.find(atoms[i]);
                    if (it != atom_cache_.atom_to_format.end()) {
                        formats.push_back(it->second);
                    }
                }
            }
            XFree(prop_data);
        }

        return formats;
    }

    bool platform_clear_x11() {
        auto* dpy = x11_state_.display;
        if (!dpy) return false;

        // Clear CLIPBOARD and PRIMARY selections
        XSetSelectionOwner(dpy, x11_state_.atom_clipboard, None, CurrentTime);
        XSetSelectionOwner(dpy, x11_state_.atom_primary, None, CurrentTime);
        XFlush(dpy);
        return true;
    }

    uint64_t platform_seqnum_x11() const {
        return x11_state_.seqnum.load(std::memory_order_acquire);
    }

    // ====================================================================
    // GENERIC / STUB
    // ====================================================================
#else
    // No-op stubs for unsupported platforms
    bool platform_write_generic(const ClipboardContent&) { return false; }
    std::optional<ClipboardContent> platform_read_generic(ClipboardFormat) { return std::nullopt; }
    std::vector<ClipboardFormat> platform_get_formats_generic() const { return {}; }
    bool platform_clear_generic() { return false; }
    uint64_t platform_seqnum_generic() const { return 0; }
#endif

    std::atomic<bool> platform_available_{false};
    mutable uint64_t last_sequence_number_{0};
};

// ============================================================================
// ClipboardMonitor — clipboard change detection (polling + events)
// ============================================================================

class ClipboardMonitor {
public:
    using ChangeCallback = std::function<void(const ClipboardChangeEvent&)>;

    ClipboardMonitor(ClipboardPlatformBridge& bridge)
        : bridge_(bridge) {
        spdlog::info("[ClipboardMonitor] Initialized");
    }

    ~ClipboardMonitor() {
        stop();
        spdlog::info("[ClipboardMonitor] Shutdown");
    }

    // --- Lifecycle ---

    void start(std::chrono::milliseconds poll_interval = kDefaultPollInterval) {
        if (running_.exchange(true, std::memory_order_acq_rel)) {
            spdlog::warn("[ClipboardMonitor] Already running");
            return;
        }

        poll_interval_ = std::clamp(poll_interval, kMinPollInterval, kMaxPollInterval);
        last_sequence_ = bridge_.sequence_number();

        poll_thread_ = std::thread([this] { poll_loop(); });

        spdlog::info("[ClipboardMonitor] Started with poll_interval={}ms",
                     poll_interval_.count());
    }

    void stop() {
        if (!running_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        {
            std::lock_guard lock(mutex_);
            stop_requested_ = true;
        }
        cv_.notify_all();

        if (poll_thread_.joinable()) {
            poll_thread_.join();
        }

        spdlog::info("[ClipboardMonitor] Stopped");
    }

    [[nodiscard]] bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    // --- Callback registration ---

    void set_callback(ChangeCallback cb) {
        std::lock_guard lock(mutex_);
        callback_ = std::move(cb);
    }

    void clear_callback() {
        std::lock_guard lock(mutex_);
        callback_ = nullptr;
    }

    // --- Adaptive polling ---

    void set_poll_interval(std::chrono::milliseconds interval) {
        std::lock_guard lock(mutex_);
        poll_interval_ = std::clamp(interval, kMinPollInterval, kMaxPollInterval);
    }

    [[nodiscard]] std::chrono::milliseconds poll_interval() const noexcept {
        std::lock_guard lock(mutex_);
        return poll_interval_;
    }

    void enable_adaptive_polling(bool enable) {
        adaptive_polling_.store(enable, std::memory_order_release);
    }

    // --- Statistics ---

    [[nodiscard]] uint64_t changes_detected() const noexcept { return changes_detected_; }
    [[nodiscard]] uint64_t polls_performed() const noexcept { return polls_performed_; }

private:
    void poll_loop() {
        spdlog::debug("[ClipboardMonitor] Poll loop started");

        while (!stop_requested_) {
            auto interval = poll_interval_;

            // Perform poll
            ++polls_performed_;
            bool changed = bridge_.has_changed();

            if (changed) {
                process_change();
            }

            // Adaptive polling: slow down after prolonged idle, speed up after changes
            if (adaptive_polling_.load(std::memory_order_acquire)) {
                if (changed) {
                    // Burst mode after a change: poll faster
                    rapid_poll_count_ = 5;
                } else if (rapid_poll_count_ > 0) {
                    --rapid_poll_count_;
                } else {
                    // Slow down gradually
                    interval = std::min(interval * 2, kMaxPollInterval);
                }
            }

            // Wait with timeout
            std::unique_lock lock(mutex_);
            cv_.wait_for(lock, interval, [this] { return stop_requested_; });
        }

        spdlog::debug("[ClipboardMonitor] Poll loop stopped");
    }

    void process_change() {
        ++changes_detected_;

        // Read available formats
        auto formats = bridge_.get_available_formats();

        ClipboardChangeEvent event(ClipboardChangeEvent::Type::kContentChanged);
        event.available_formats = std::move(formats);
        event.sequence_number = bridge_.sequence_number();
        event.timestamp = std::chrono::steady_clock::now();

        spdlog::debug("[ClipboardMonitor] Change detected: seq={}, formats={}",
                      *event.sequence_number, event.available_formats.size());

        // Invoke callback
        ChangeCallback cb;
        {
            std::lock_guard lock(mutex_);
            cb = callback_;
        }
        if (cb) {
            try {
                cb(event);
            } catch (const std::exception& e) {
                spdlog::error("[ClipboardMonitor] Callback exception: {}", e.what());
            }
        }
    }

    ClipboardPlatformBridge& bridge_;
    std::atomic<bool> running_{false};
    bool stop_requested_{false};
    std::thread poll_thread_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    ChangeCallback callback_;
    std::chrono::milliseconds poll_interval_{kDefaultPollInterval};
    std::atomic<bool> adaptive_polling_{true};
    int rapid_poll_count_{0};

    uint64_t last_sequence_{0};
    std::atomic<uint64_t> changes_detected_{0};
    std::atomic<uint64_t> polls_performed_{0};
};

// ============================================================================
// ClipboardSyncEngine — the main bidirectional sync orchestrator
// ============================================================================

class ClipboardSyncEngine {
public:
    ClipboardSyncEngine(std::shared_ptr<ClipboardSessionManager> sessions,
                        std::shared_ptr<ClipboardFormatNegotiator> negotiator,
                        std::shared_ptr<ClipboardDedupEngine> dedup,
                        std::shared_ptr<ClipboardTransferEngine> transfer,
                        std::shared_ptr<ClipboardRateLimiter> limiter,
                        std::shared_ptr<ClipboardPlatformBridge> bridge,
                        std::shared_ptr<ClipboardPermissionManager> permissions,
                        std::shared_ptr<ClipboardStatistics> stats)
        : sessions_(std::move(sessions)),
          negotiator_(std::move(negotiator)),
          dedup_(std::move(dedup)),
          transfer_(std::move(transfer)),
          limiter_(std::move(limiter)),
          bridge_(std::move(bridge)),
          permissions_(std::move(permissions)),
          stats_(std::move(stats)) {
        spdlog::info("[ClipboardSyncEngine] Initialized");
    }

    // ====================================================================
    // OUTBOUND SYNC: Local clipboard -> Remote sessions
    // ====================================================================

    /// Sync local clipboard content to all active remote sessions.
    /// Returns number of sessions synced.
    size_t sync_local_to_remote() {
        spdlog::trace("[ClipboardSyncEngine] sync_local_to_remote triggered");

        if (!limiter_->try_consume_sync()) {
            stats_->record_rate_limit_hit();
            spdlog::debug("[ClipboardSyncEngine] Rate limit hit on sync_local_to_remote");
            return 0;
        }

        // Read local clipboard
        auto local_formats = bridge_->get_available_formats();
        if (local_formats.empty()) {
            spdlog::trace("[ClipboardSyncEngine] No local clipboard formats available");
            return 0;
        }

        // Read content in the best available format
        auto content_opt = bridge_->read_clipboard_best(local_formats);
        if (!content_opt) {
            spdlog::debug("[ClipboardSyncEngine] Failed to read local clipboard");
            return 0;
        }
        auto& content = *content_opt;

        // Check bytes rate limit
        size_t allowed_bytes = limiter_->try_consume_bytes(content.size());
        if (allowed_bytes < content.size()) {
            stats_->record_rate_limit_hit();
            spdlog::debug("[ClipboardSyncEngine] Byte rate limit: {}, allowed {}",
                         content.size(), allowed_bytes);
            if (allowed_bytes == 0) return 0;
        }

        // Deduplication check
        if (dedup_->check_and_record(content)) {
            stats_->record_dedup_hit();
            spdlog::trace("[ClipboardSyncEngine] Local content deduplicated (no change)");
            return 0;
        }
        stats_->record_dedup_miss();

        // Broadcast to all active sessions
        auto active = sessions_->active_sessions();
        size_t synced = 0;

        for (auto& session : active) {
            const auto& sid = session->session_id();

            // Permission check
            if (!permissions_->check_operation(sid, content.format, false)) {
                spdlog::debug("[ClipboardSyncEngine] Read permission denied for session '{}'", sid);
                continue;
            }

            // Policy check
            auto policy = session->policy();
            if (!is_category_enabled(content.category, policy)) {
                continue;
            }

            // Format negotiation: find format the remote side can accept
            // (In full implementation, we'd query remote capabilities)
            auto negotiated = content.format; // Simplified: use same format

            // Record in session context
            session->record_local_change(content);

            // Log the sync
            stats_->record_sync_sent(content.size(), content.format);
            spdlog::info("[ClipboardSyncEngine] Synced local->remote session '{}': "
                         "{} bytes, format {}",
                         sid, content.size(),
                         ClipboardFormatNegotiator::format_name(content.format));

            ++synced;
        }

        return synced;
    }

    // ====================================================================
    // INBOUND SYNC: Remote session -> Local clipboard
    // ====================================================================

    /// Sync remote clipboard content to local clipboard for a specific session.
    bool sync_remote_to_local(const std::string& session_id,
                               const ClipboardContent& remote_content) {
        spdlog::trace("[ClipboardSyncEngine] sync_remote_to_local from '{}'", session_id);

        if (!limiter_->try_consume_sync()) {
            stats_->record_rate_limit_hit();
            spdlog::debug("[ClipboardSyncEngine] Rate limit hit on sync_remote_to_local");
            return false;
        }

        // Permission check
        if (!permissions_->check_operation(session_id, remote_content.format, true)) {
            spdlog::warn("[ClipboardSyncEngine] Write permission denied for session '{}'",
                         session_id);
            return false;
        }

        // Deduplication check
        if (dedup_->check_and_record(remote_content, session_id)) {
            stats_->record_dedup_hit();
            spdlog::trace("[ClipboardSyncEngine] Remote content deduplicated");
            return false;
        }
        stats_->record_dedup_miss();

        // Check byte rate limit
        size_t allowed = limiter_->try_consume_bytes(remote_content.size());
        if (allowed < remote_content.size()) {
            stats_->record_rate_limit_hit();
            if (allowed == 0) return false;
        }

        // Write to local clipboard
        bool ok = bridge_->write_clipboard(remote_content);
        if (!ok) {
            stats_->record_error();
            spdlog::error("[ClipboardSyncEngine] Failed to write remote content to local clipboard");
            return false;
        }

        // Record in session context
        auto session = sessions_->get_session(session_id);
        if (session) {
            session->record_remote_change(remote_content);
        }

        stats_->record_sync_recv(remote_content.size(), remote_content.format);
        spdlog::info("[ClipboardSyncEngine] Synced remote->local session '{}': "
                     "{} bytes, format {}",
                     session_id, remote_content.size(),
                     ClipboardFormatNegotiator::format_name(remote_content.format));

        return true;
    }

    // ====================================================================
    // BIDIRECTIONAL SYNC (full cycle)
    // ====================================================================

    /// Perform a full bidirectional sync cycle:
    /// 1. Check local changes, push to remotes
    /// 2. Check pending remote changes, pull to local
    size_t perform_sync_cycle() {
        spdlog::trace("[ClipboardSyncEngine] Performing full sync cycle");

        size_t outbound = sync_local_to_remote();

        // Inbound changes are event-driven; this is a proactive check
        // for any pending remote content that needs to be applied.
        size_t inbound = process_pending_inbound();

        spdlog::debug("[ClipboardSyncEngine] Sync cycle complete: outbound={}, inbound={}",
                      outbound, inbound);
        return outbound + inbound;
    }

    // ====================================================================
    // INCREMENTAL SYNC
    // ====================================================================

    /// Attempt incremental sync for a session with previous content reference.
    bool sync_incremental(const std::string& session_id,
                           const ClipboardContent& new_content,
                           const ClipboardContent& old_content) {
        if (!transfer_->should_use_incremental(new_content, old_content)) {
            return false; // Not suitable for incremental
        }

        auto delta = transfer_->compute_delta(old_content, new_content);
        if (!delta) {
            return false;
        }

        // Create delta content envelope
        ClipboardContent delta_content(ClipboardFormat::kDeltaUpdate);
        delta_content.delta_payload = std::move(delta);
        delta_content.base_sequence = old_content.sequence_number;
        delta_content.session_id = session_id;

        spdlog::info("[ClipboardSyncEngine] Incremental sync: session='{}', "
                     "delta_size={} (saved {:.1f}%)",
                     session_id, delta_content.delta_payload->size(),
                     (1.0 - static_cast<double>(delta_content.delta_payload->size()) /
                      std::max(new_content.size(), size_t(1))) * 100.0);

        // The delta is then transmitted instead of full content
        return bridge_->write_clipboard(new_content); // Apply full content locally
    }

    // ====================================================================
    // FORMAT NEGOTIATION HOOKS
    // ====================================================================

    /// Negotiate best format between local and a remote session.
    [[nodiscard]] std::optional<ClipboardFormat> negotiate_format(
            const std::string& session_id) const {
        auto local_formats = bridge_->get_available_formats();
        if (local_formats.empty()) return std::nullopt;

        // In full implementation, we'd query remote session's supported formats
        // For now, return the best local format
        return local_formats.front();
    }

private:
    // --- Helpers ---

    static bool is_category_enabled(FormatCategory cat,
                                     const ClipboardSyncPolicy& policy) noexcept {
        switch (cat) {
            case FormatCategory::kText:  return policy.enable_text_sync;
            case FormatCategory::kImage: return policy.enable_image_sync;
            case FormatCategory::kFile:  return policy.enable_file_sync;
            default: return true;
        }
    }

    size_t process_pending_inbound() {
        // Check sessions for incoming remote changes that need to be applied
        size_t applied = 0;
        auto active = sessions_->active_sessions();
        for (auto& session : active) {
            auto remote = session->last_remote();
            if (remote && session->is_dirty()) {
                if (bridge_->write_clipboard(*remote)) {
                    session->mark_clean();
                    ++applied;
                }
            }
        }
        return applied;
    }

    std::shared_ptr<ClipboardSessionManager> sessions_;
    std::shared_ptr<ClipboardFormatNegotiator> negotiator_;
    std::shared_ptr<ClipboardDedupEngine> dedup_;
    std::shared_ptr<ClipboardTransferEngine> transfer_;
    std::shared_ptr<ClipboardRateLimiter> limiter_;
    std::shared_ptr<ClipboardPlatformBridge> bridge_;
    std::shared_ptr<ClipboardPermissionManager> permissions_;
    std::shared_ptr<ClipboardStatistics> stats_;
};

// ============================================================================
// ClipboardService — the top-level service facade
// ============================================================================

class ClipboardService {
public:
    struct Config {
        ClipboardSyncPolicy default_policy;
        ClipboardRateLimiter::Config rate_limit;
        size_t dedup_max_entries{10000};
        std::chrono::milliseconds poll_interval{kDefaultPollInterval};
        bool enable_monitoring{true};
        bool enable_statistics{true};
        std::chrono::milliseconds maintenance_interval{kIdlePruneInterval};
        size_t max_sessions{100};
    };

    explicit ClipboardService(Config cfg = {}) : config_(std::move(cfg)) {
        spdlog::info("[ClipboardService] Initializing comprehensive clipboard service");
        spdlog::info("[ClipboardService] Platform: {}", 
                     ClipboardPlatformBridge::platform_name());
        spdlog::info("[ClipboardService] C++20, protocol version {}", kClipboardProtocolVersion);

        // Initialize all subsystems
        sessions_ = std::make_shared<ClipboardSessionManager>();
        negotiator_ = std::make_shared<ClipboardFormatNegotiator>();
        dedup_ = std::make_shared<ClipboardDedupEngine>(config_.dedup_max_entries);
        transfer_ = std::make_shared<ClipboardTransferEngine>();
        limiter_ = std::make_shared<ClipboardRateLimiter>(config_.rate_limit);
        bridge_ = std::make_shared<ClipboardPlatformBridge>();
        permissions_ = std::make_shared<ClipboardPermissionManager>();
        stats_ = std::make_shared<ClipboardStatistics>();

        engine_ = std::make_shared<ClipboardSyncEngine>(
            sessions_, negotiator_, dedup_, transfer_,
            limiter_, bridge_, permissions_, stats_);

        monitor_ = std::make_shared<ClipboardMonitor>(*bridge_);

        spdlog::info("[ClipboardService] All subsystems initialized");
    }

    ~ClipboardService() {
        shutdown();
        spdlog::info("[ClipboardService] Destroyed");
    }

    // ===================================================================
    // Lifecycle
    // ===================================================================

    /// Start the clipboard service. Begins monitoring and sync loop.
    bool start() {
        if (started_.exchange(true, std::memory_order_acq_rel)) {
            spdlog::warn("[ClipboardService] Already started");
            return false;
        }

        spdlog::info("[ClipboardService] Starting...");

        if (!bridge_->is_available()) {
            spdlog::warn("[ClipboardService] Platform clipboard bridge is not available");
            // Continue anyway — the service degrades gracefully
        }

        // Start clipboard monitor
        if (config_.enable_monitoring) {
            monitor_->set_callback([this](const ClipboardChangeEvent& event) {
                on_clipboard_change(event);
            });
            monitor_->start(config_.poll_interval);
        }

        // Start maintenance thread
        maintenance_running_.store(true, std::memory_order_release);
        maintenance_thread_ = std::thread([this] { maintenance_loop(); });

        // Start stats reporting timer
        if (config_.enable_statistics) {
            stats_timer_running_.store(true, std::memory_order_release);
            stats_thread_ = std::thread([this] { stats_reporting_loop(); });
        }

        spdlog::info("[ClipboardService] Started successfully");
        return true;
    }

    /// Graceful shutdown.
    void shutdown() {
        if (!started_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        spdlog::info("[ClipboardService] Shutting down...");

        // Stop monitor
        if (monitor_) {
            monitor_->stop();
        }

        // Stop maintenance
        maintenance_running_.store(false, std::memory_order_release);
        stats_timer_running_.store(false, std::memory_order_release);
        {
            std::lock_guard lock(maintenance_mutex_);
            maintenance_cv_.notify_all();
        }
        {
            std::lock_guard lock(stats_mutex_);
            stats_cv_.notify_all();
        }
        if (maintenance_thread_.joinable()) maintenance_thread_.join();
        if (stats_thread_.joinable()) stats_thread_.join();

        // Final stats dump
        spdlog::info("[ClipboardService] Final statistics:\n{}", get_statistics_summary());

        spdlog::info("[ClipboardService] Shutdown complete");
    }

    [[nodiscard]] bool is_started() const noexcept {
        return started_.load(std::memory_order_acquire);
    }

    // ===================================================================
    // Session management
    // ===================================================================

    /// Create a new clipboard session for a remote client.
    [[nodiscard]] std::shared_ptr<ClipboardSessionContext> create_session(
            const std::string& session_id) {
        if (sessions_->session_count() >= config_.max_sessions) {
            spdlog::warn("[ClipboardService] Max sessions reached ({})", config_.max_sessions);
            return nullptr;
        }
        return sessions_->create_session(session_id);
    }

    /// Destroy a clipboard session.
    bool destroy_session(const std::string& session_id) {
        return sessions_->destroy_session(session_id);
    }

    /// Get a clipboard session context.
    [[nodiscard]] std::shared_ptr<ClipboardSessionContext> get_session(
            const std::string& session_id) {
        return sessions_->get_session(session_id);
    }

    /// Set session active/inactive.
    void set_session_active(const std::string& session_id, bool active) {
        sessions_->set_active(session_id, active);
    }

    /// Get active session count.
    [[nodiscard]] size_t session_count() const noexcept {
        return sessions_->session_count();
    }

    // ===================================================================
    // Clipboard operations
    // ===================================================================

    /// Read current local clipboard content.
    [[nodiscard]] std::optional<ClipboardContent> read_local_clipboard(
            ClipboardFormat fmt = ClipboardFormat::kUnicodeText) {
        if (!permissions_->can_read("*", fmt)) {
            spdlog::warn("[ClipboardService] Read denied by permissions");
            return std::nullopt;
        }
        return bridge_->read_clipboard(fmt);
    }

    /// Read local clipboard in the best available format.
    [[nodiscard]] std::optional<ClipboardContent> read_local_clipboard_best(
            const std::vector<ClipboardFormat>& preferred = {}) {
        if (preferred.empty()) {
            return read_local_clipboard();
        }
        return bridge_->read_clipboard_best(preferred);
    }

    /// Write content to the local clipboard.
    bool write_local_clipboard(const std::string& session_id,
                                const ClipboardContent& content) {
        if (!permissions_->check_operation(session_id, content.format, true)) {
            spdlog::warn("[ClipboardService] Write denied for session '{}'", session_id);
            return false;
        }
        return bridge_->write_clipboard(content);
    }

    /// Get available local clipboard formats.
    [[nodiscard]] std::vector<ClipboardFormat> get_local_formats() const {
        return bridge_->get_available_formats();
    }

    /// Clear the local clipboard.
    bool clear_local_clipboard() {
        return bridge_->clear_clipboard();
    }

    // ===================================================================
    // Remote sync (called from network layer)
    // ===================================================================

    /// Handle incoming clipboard content from a remote session.
    bool handle_remote_clipboard(const std::string& session_id,
                                  const ClipboardContent& content) {
        auto session = sessions_->get_session(session_id);
        if (!session) {
            spdlog::warn("[ClipboardService] Unknown session '{}' for remote clipboard",
                         session_id);
            return false;
        }

        // Store the remote content
        session->record_remote_change(content);

        // Sync to local clipboard
        return engine_->sync_remote_to_local(session_id, content);
    }

    /// Handle incoming delta (incremental) update.
    bool handle_remote_delta(const std::string& session_id,
                              const ClipboardContent& delta_content) {
        auto session = sessions_->get_session(session_id);
        if (!session) {
            spdlog::warn("[ClipboardService] Unknown session '{}' for delta update", session_id);
            return false;
        }

        if (!delta_content.base_sequence || !delta_content.delta_payload) {
            spdlog::warn("[ClipboardService] Invalid delta content from session '{}'", session_id);
            return false;
        }

        // Find base content in session history
        auto last_local = session->last_local();
        if (!last_local || last_local->sequence_number != *delta_content.base_sequence) {
            spdlog::warn("[ClipboardService] Delta base sequence mismatch for session '{}'",
                         session_id);
            return false;
        }

        auto new_content = transfer_->apply_delta(*last_local, *delta_content.delta_payload);
        if (!new_content) {
            spdlog::error("[ClipboardService] Failed to apply delta for session '{}'", session_id);
            return false;
        }

        return engine_->sync_remote_to_local(session_id, *new_content);
    }

    // ===================================================================
    // Sync control
    // ===================================================================

    /// Trigger a proactive sync cycle.
    size_t trigger_sync_cycle() {
        if (!started_.load(std::memory_order_acquire)) {
            spdlog::debug("[ClipboardService] Sync cycle ignored: service not started");
            return 0;
        }
        return engine_->perform_sync_cycle();
    }

    /// Set the sync policy for a session.
    void set_session_policy(const std::string& session_id,
                             const ClipboardSyncPolicy& policy) {
        auto session = sessions_->get_session(session_id);
        if (session) {
            session->set_policy(policy);
        }
    }

    /// Enable/disable specific sync categories globally.
    void set_sync_categories(bool text, bool image, bool file) {
        auto policy = config_.default_policy;
        policy.enable_text_sync = text;
        policy.enable_image_sync = image;
        policy.enable_file_sync = file;
        config_.default_policy = policy;
    }

    // ===================================================================
    // Permission management
    // ===================================================================

    void set_permission(const std::string& session_pattern,
                         ClipboardPermissionManager::PermissionLevel level,
                         const std::string& description = {}) {
        permissions_->add_rule(session_pattern, level, description);
    }

    void block_format_for_session(const std::string& session_id,
                                   ClipboardFormat fmt) {
        std::set<ClipboardFormat> blocked{fmt};
        permissions_->set_format_policy(session_id, {}, blocked);
    }

    void allow_only_formats(const std::string& session_id,
                             const std::set<ClipboardFormat>& allowed) {
        permissions_->set_format_policy(session_id, allowed, {});
    }

    // ===================================================================
    // Format negotiation
    // ===================================================================

    [[nodiscard]] std::optional<ClipboardFormat> negotiate_format(
            const std::string& session_id) const {
        return engine_->negotiate_format(session_id);
    }

    [[nodiscard]] std::optional<ClipboardContent> convert_format(
            const ClipboardContent& source, ClipboardFormat target) const {
        auto result = negotiator_->convert(source, target);
        if (result) {
            stats_->record_format_conversion(source.format, target);
        }
        return result;
    }

    // ===================================================================
    // Statistics
    // ===================================================================

    [[nodiscard]] std::shared_ptr<ClipboardStatistics> statistics() const {
        return stats_;
    }

    [[nodiscard]] std::string get_statistics_summary() const {
        auto s = stats_->summary();
        auto d = std::format(
            " Sessions: {} active | "
            "Dedup: {} entries, {} hits, {:.1%} ratio | "
            "Rate: {} syncs allowed / {} denied | "
            "Platform: {}",
            sessions_->session_count(),
            dedup_->entry_count(), dedup_->dedup_hit_count(),
            dedup_->dedup_ratio(),
            limiter_->syncs_allowed(), limiter_->syncs_denied(),
            ClipboardPlatformBridge::platform_name()
        );
        return s + "\n" + d;
    }

    void reset_statistics() {
        stats_->reset();
    }

    // ===================================================================
    // Maintenance
    // ===================================================================

    size_t run_maintenance_now() {
        spdlog::debug("[ClipboardService] Running maintenance");
        size_t dedup_pruned = dedup_->run_maintenance();
        size_t sessions_pruned = sessions_->prune_idle_sessions(
            std::chrono::hours(1));
        permissions_->purge_expired_rules();
        spdlog::debug("[ClipboardService] Maintenance: dedup_pruned={}, sessions_pruned={}",
                      dedup_pruned, sessions_pruned);
        return dedup_pruned + sessions_pruned;
    }

private:
    // ===================================================================
    // Internal: Clipboard change handler
    // ===================================================================

    void on_clipboard_change(const ClipboardChangeEvent& event) {
        spdlog::debug("[ClipboardService] Clipboard change: type={}, formats={}",
                      static_cast<int>(event.type), event.available_formats.size());

        // Push local changes to all remote sessions
        try {
            engine_->sync_local_to_remote();
        } catch (const std::exception& e) {
            spdlog::error("[ClipboardService] Error in sync_local_to_remote: {}", e.what());
            stats_->record_error();
        }
    }

    // ===================================================================
    // Internal: Maintenance thread
    // ===================================================================

    void maintenance_loop() {
        spdlog::debug("[ClipboardService] Maintenance thread started");
        while (maintenance_running_.load(std::memory_order_acquire)) {
            {
                std::unique_lock lock(maintenance_mutex_);
                maintenance_cv_.wait_for(lock, config_.maintenance_interval,
                    [this] { return !maintenance_running_.load(std::memory_order_acquire); });
            }
            if (!maintenance_running_.load(std::memory_order_acquire)) break;
            run_maintenance_now();
        }
        spdlog::debug("[ClipboardService] Maintenance thread stopped");
    }

    // ===================================================================
    // Internal: Stats reporting thread
    // ===================================================================

    void stats_reporting_loop() {
        spdlog::debug("[ClipboardService] Stats reporting thread started");
        while (stats_timer_running_.load(std::memory_order_acquire)) {
            {
                std::unique_lock lock(stats_mutex_);
                stats_cv_.wait_for(lock, kStatisticsFlushInterval,
                    [this] { return !stats_timer_running_.load(std::memory_order_acquire); });
            }
            if (!stats_timer_running_.load(std::memory_order_acquire)) break;
            spdlog::info("[ClipboardService] Periodic statistics:\n{}",
                         get_statistics_summary());
        }
        spdlog::debug("[ClipboardService] Stats reporting thread stopped");
    }

    // ===================================================================
    // Configuration
    // ===================================================================

    Config config_;
    std::atomic<bool> started_{false};

    // Subsystems
    std::shared_ptr<ClipboardSessionManager> sessions_;
    std::shared_ptr<ClipboardFormatNegotiator> negotiator_;
    std::shared_ptr<ClipboardDedupEngine> dedup_;
    std::shared_ptr<ClipboardTransferEngine> transfer_;
    std::shared_ptr<ClipboardRateLimiter> limiter_;
    std::shared_ptr<ClipboardPlatformBridge> bridge_;
    std::shared_ptr<ClipboardPermissionManager> permissions_;
    std::shared_ptr<ClipboardStatistics> stats_;
    std::shared_ptr<ClipboardSyncEngine> engine_;
    std::shared_ptr<ClipboardMonitor> monitor_;

    // Threads
    std::thread maintenance_thread_;
    std::thread stats_thread_;
    std::atomic<bool> maintenance_running_{false};
    std::atomic<bool> stats_timer_running_{false};
    std::mutex maintenance_mutex_;
    std::condition_variable maintenance_cv_;
    std::mutex stats_mutex_;
    std::condition_variable stats_cv_;
};

// ============================================================================
// Free functions — ClipboardService factory and helpers
// ============================================================================

/// Create a default-configured clipboard service.
inline std::shared_ptr<ClipboardService> create_default_clipboard_service() {
    ClipboardService::Config cfg;
    cfg.default_policy = ClipboardSyncPolicy{};
    cfg.rate_limit = ClipboardRateLimiter::Config{};
    cfg.dedup_max_entries = 10000;
    cfg.poll_interval = kDefaultPollInterval;
    cfg.enable_monitoring = true;
    cfg.enable_statistics = true;
    return std::make_shared<ClipboardService>(std::move(cfg));
}

/// Create a clipboard service with custom sync policy.
inline std::shared_ptr<ClipboardService> create_clipboard_service(
        const ClipboardSyncPolicy& policy) {
    ClipboardService::Config cfg;
    cfg.default_policy = policy;
    return std::make_shared<ClipboardService>(std::move(cfg));
}

/// Convert a platform-specific clipboard format to a MIME string.
[[nodiscard]] inline std::string format_to_mime(ClipboardFormat fmt) noexcept {
    switch (fmt) {
        case ClipboardFormat::kUnicodeText: return "text/plain; charset=utf-8";
        case ClipboardFormat::kRTFText:     return "text/rtf";
        case ClipboardFormat::kHTML:        return "text/html";
        case ClipboardFormat::kBitmap:      return "image/bmp";
        case ClipboardFormat::kPNG:         return "image/png";
        case ClipboardFormat::kJPEG:        return "image/jpeg";
        case ClipboardFormat::kTIFF:        return "image/tiff";
        case ClipboardFormat::kGIF:         return "image/gif";
        case ClipboardFormat::kFileList:    return "text/uri-list";
        case ClipboardFormat::kOEMText:     return "text/plain; charset=oem";
        case ClipboardFormat::kMetafilePict: return "image/wmf";
        case ClipboardFormat::kEnhancedMetafile: return "image/emf";
        default:                            return "application/octet-stream";
    }
}

/// Parse a MIME type string into a ClipboardFormat.
[[nodiscard]] inline std::optional<ClipboardFormat> mime_to_format(
        std::string_view mime) noexcept {
    if (mime.starts_with("text/plain"))         return ClipboardFormat::kUnicodeText;
    if (mime.starts_with("text/rtf"))           return ClipboardFormat::kRTFText;
    if (mime.starts_with("text/html"))          return ClipboardFormat::kHTML;
    if (mime.starts_with("image/png"))          return ClipboardFormat::kPNG;
    if (mime.starts_with("image/jpeg"))         return ClipboardFormat::kJPEG;
    if (mime.starts_with("image/tiff"))         return ClipboardFormat::kTIFF;
    if (mime.starts_with("image/gif"))          return ClipboardFormat::kGIF;
    if (mime.starts_with("image/bmp"))          return ClipboardFormat::kBitmap;
    if (mime.starts_with("image/x-bmp"))        return ClipboardFormat::kBitmap;
    if (mime.starts_with("text/uri-list"))      return ClipboardFormat::kFileList;
    return std::nullopt;
}

/// Get all registered private clipboard format names.
[[nodiscard]] inline std::vector<std::string> registered_private_formats(
        const ClipboardService& service) {
    std::vector<std::string> names;
    // Iterate all known formats
    for (uint32_t i = 0; i <= static_cast<uint32_t>(ClipboardFormat::kCustomBinary); ++i) {
        auto fmt = static_cast<ClipboardFormat>(i);
        names.emplace_back(ClipboardFormatNegotiator::format_name(fmt));
    }
    return names;
}

} // namespace cppdesk::server

// ============================================================================
// Optional: Main entry point for standalone testing
// ============================================================================
#ifdef CLIPBOARD_SERVICE_STANDALONE_TEST

#include <iostream>
#include <csignal>

namespace {
    std::atomic<bool> g_running{true};
    void signal_handler(int) { g_running.store(false); }
}

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");

    spdlog::info("=== Clipboard Service Full Standalone Test ===");
    spdlog::info("Platform: {}", cppdesk::server::ClipboardPlatformBridge::platform_name());

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Create and start the service
    auto service = cppdesk::server::create_default_clipboard_service();

    if (!service->start()) {
        spdlog::error("Failed to start clipboard service");
        return 1;
    }

    // Create a test session
    auto session = service->create_session("test_session_1");
    if (!session) {
        spdlog::error("Failed to create test session");
        return 1;
    }
    spdlog::info("Created test session '{}'", session->session_id());

    // Main loop
    spdlog::info("Monitoring clipboard changes. Press Ctrl+C to exit.");
    size_t iterations = 0;
    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Periodic sync
        if (++iterations % 5 == 0) {
            service->trigger_sync_cycle();
        }

        // Periodic stats dump
        if (iterations % 30 == 0) {
            spdlog::info("Stats ({}s): {}", iterations,
                         service->get_statistics_summary());
        }
    }

    spdlog::info("Shutting down...");
    service->shutdown();
    spdlog::info("=== Test complete ===");

    return 0;
}

#endif // CLIPBOARD_SERVICE_STANDALONE_TEST
