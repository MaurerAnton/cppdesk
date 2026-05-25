#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <cstdint>
#include <chrono>
#include <map>
#include <variant>
#include <array>
#include <filesystem>
#include <string_view>
#include <mutex>
#include <set>

namespace clipboard {

// ====== Content Formats ======
enum class ContentFormat : uint32_t {
    TEXT            = 1 << 0,
    HTML            = 1 << 1,
    RTF             = 1 << 2,
    IMAGE_PNG       = 1 << 3,
    IMAGE_TIFF      = 1 << 4,
    IMAGE_BMP       = 1 << 5,
    IMAGE_DIB       = 1 << 6,
    IMAGE_DIBV5     = 1 << 7,
    FILE_LIST       = 1 << 8,
    URI_LIST        = 1 << 9,
    ALL_TEXT        = TEXT | HTML | RTF,
    ALL_IMAGE       = IMAGE_PNG | IMAGE_TIFF | IMAGE_BMP | IMAGE_DIB | IMAGE_DIBV5,
    ALL             = ALL_TEXT | ALL_IMAGE | FILE_LIST | URI_LIST,
};

inline constexpr ContentFormat operator|(ContentFormat a, ContentFormat b) {
    return static_cast<ContentFormat>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline constexpr ContentFormat operator&(ContentFormat a, ContentFormat b) {
    return static_cast<ContentFormat>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline constexpr bool operator!(ContentFormat f) {
    return static_cast<uint32_t>(f) == 0;
}

// ====== Content Hash ======
using ContentHash = std::array<uint8_t, 32>;

inline constexpr std::string_view hash_to_string(const ContentHash& hash) {
    return {reinterpret_cast<const char*>(hash.data()), hash.size()};
}

// ====== Clipboard Content Descriptor ======
struct ClipboardContentDescriptor {
    ContentFormat available_formats = static_cast<ContentFormat>(0);
    std::string text;
    std::string html;
    std::string rtf;
    std::vector<uint8_t> image_data;
    std::string image_format; // "png", "tiff", "bmp", "dib", "dibv5"
    std::vector<std::string> file_list;
    std::vector<std::string> uri_list;
    ContentHash text_hash{};
    ContentHash image_hash{};
    ContentHash file_hash{};
    int64_t timestamp_ms = 0;
    uint32_t sequence_number = 0;
    bool is_delayed_rendered = false;
    std::string source_application;

    void clear() {
        available_formats = static_cast<ContentFormat>(0);
        text.clear(); html.clear(); rtf.clear();
        image_data.clear(); image_format.clear();
        file_list.clear(); uri_list.clear();
        text_hash = {}; image_hash = {}; file_hash = {};
        timestamp_ms = 0; sequence_number = 0;
        is_delayed_rendered = false;
        source_application.clear();
    }

    bool has_format(ContentFormat fmt) const {
        return !!(available_formats & fmt);
    }

    std::string describe() const;
};

// ====== Error Types ======
enum class CliprdrError : uint32_t {
    None = 0,
    CliprdrName = 1,
    CliprdrInit = 2,
    CliprdrOutOfMemory = 3,
    ClipboardInternalError = 4,
    ClipboardOccupied = 5,
    ConversionFailure = 6,
    OpenClipboard = 7,
    FileError = 8,
    InvalidRequest = 9,
    CommonError = 10,
    FormatNotAvailable = 11,
    RenderingFailed = 12,
    TransferInProgress = 13,
    Timeout = 14,
    Unknown = 99,
};

const char* cliprdr_error_string(CliprdrError err);

// ====== Clipboard File Message ======
struct ClipboardFile {
    enum Type { NOTIFY, REQUEST, DATA, DONE, CANCEL, PROGRESS, ERROR };
    Type type = NOTIFY;
    std::string path;
    uint64_t size = 0;
    std::vector<uint8_t> data;
    bool is_dir = false;
    int32_t conn_id = 0;
    uint64_t offset = 0;
    uint64_t bytes_transferred = 0;
};

// ====== Progress ======
struct ProgressPercent {
    double percent = 0.0;
    bool is_canceled = false;
    bool is_failed = false;
    std::string error_message;
    uint64_t bytes_processed = 0;
    uint64_t total_bytes = 0;
};

// ====== Delayed Renderer Interface ======
class DelayedRenderer {
public:
    virtual ~DelayedRenderer() = default;
    virtual std::vector<uint8_t> render(ContentFormat format) = 0;
    virtual bool can_render(ContentFormat format) const = 0;
    virtual void release() = 0;
};

// ====== Service Context (RPC server for clipboard) ======
class CliprdrServiceContext {
public:
    virtual ~CliprdrServiceContext() = default;
    virtual bool set_stopped() = 0;
    virtual bool empty_clipboard(int32_t conn_id) = 0;
    virtual bool server_clip_file(int32_t conn_id, const ClipboardFile& msg) = 0;
    virtual std::optional<ProgressPercent> get_progress() = 0;
    virtual void cancel() = 0;
    virtual bool is_connected() const = 0;
    virtual int32_t connection_count() const = 0;
};

// ====== Platform Clipboard ======
class PlatformClipboard {
public:
    virtual ~PlatformClipboard() = default;

    // Text
    virtual std::string get_text() = 0;
    virtual bool set_text(const std::string& text) = 0;
    virtual bool has_text() = 0;

    // Files
    virtual std::vector<std::string> get_file_list() = 0;
    virtual bool set_file_list(const std::vector<std::string>& paths) = 0;
    virtual bool has_file_list() = 0;

    // Images
    virtual std::vector<uint8_t> get_image(const std::string& format = "png") = 0;
    virtual bool set_image(const std::vector<uint8_t>& data,
        const std::string& format = "png") = 0;

    // HTML
    virtual std::string get_html() = 0;
    virtual bool set_html(const std::string& html) = 0;

    // RTF (Rich Text Format)
    virtual std::string get_rtf() = 0;
    virtual bool set_rtf(const std::string& rtf) = 0;

    // URI list
    virtual std::vector<std::string> get_uri_list() = 0;
    virtual bool set_uri_list(const std::vector<std::string>& uris) = 0;

    // Clear
    virtual bool clear() = 0;

    // Ownership
    virtual bool owns_clipboard() = 0;

    // Monitoring callback
    using OnChange = std::function<void()>;
    virtual void set_on_change(OnChange cb) = 0;

    // Delayed rendering support
    virtual bool enable_delayed_rendering(std::shared_ptr<DelayedRenderer> renderer) = 0;
    virtual void disable_delayed_rendering() = 0;

    // Get all available formats
    virtual ContentFormat available_formats() = 0;

    // Get full descriptor
    virtual ClipboardContentDescriptor get_content_descriptor() = 0;

    // Set multiple formats at once
    virtual bool set_content(const ClipboardContentDescriptor& content) = 0;

    // Change count / sequence tracking
    virtual int64_t get_change_count() = 0;

    // Factory
    static std::unique_ptr<PlatformClipboard> create();
};

// ====== Hash-Based Content Deduplication ======
class ContentDeduplicator {
public:
    struct CacheEntry {
        ContentHash hash;
        ClipboardContentDescriptor content;
        std::chrono::steady_clock::time_point timestamp;
        uint32_t access_count = 0;
    };

    ContentDeduplicator(size_t max_entries = 256);

    std::optional<ClipboardContentDescriptor> find_by_hash(const ContentHash& hash);
    void store(const ContentHash& hash, const ClipboardContentDescriptor& content);
    void invalidate(const ContentHash& hash);
    void clear();
    size_t size() const;
    void set_max_entries(size_t max);

    static ContentHash compute_text_hash(const std::string& text);
    static ContentHash compute_data_hash(const std::vector<uint8_t>& data);
    static ContentHash compute_file_hash(const std::vector<std::string>& files);
    static std::string hash_to_hex(const ContentHash& hash);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ====== Clipboard Change Monitor ======
class ClipboardMonitor {
public:
    ClipboardMonitor();
    ~ClipboardMonitor();

    void start(std::chrono::milliseconds interval = std::chrono::milliseconds(500));
    void stop();
    bool is_running() const;

    using OnTextChange = std::function<void(const std::string& text)>;
    using OnFileChange = std::function<void(const std::vector<std::string>& files)>;
    using OnImageChange = std::function<void(const std::vector<uint8_t>& data, const std::string& fmt)>;
    using OnHtmlChange = std::function<void(const std::string& html)>;
    using OnRtfChange = std::function<void(const std::string& rtf)>;
    using OnAnyChange = std::function<void(const ClipboardContentDescriptor& desc)>;

    void set_on_text_change(OnTextChange cb);
    void set_on_file_change(OnFileChange cb);
    void set_on_image_change(OnImageChange cb);
    void set_on_html_change(OnHtmlChange cb);
    void set_on_rtf_change(OnRtfChange cb);
    void set_on_any_change(OnAnyChange cb);

    std::string last_text() const;
    std::vector<std::string> last_files() const;
    std::string last_html() const;
    ClipboardContentDescriptor last_descriptor() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ====== Clipboard Synchronizer ======
class ClipboardSynchronizer {
public:
    ClipboardSynchronizer();
    ~ClipboardSynchronizer();

    enum class Mode { DISABLED, LOCAL_ONLY, REMOTE_ONLY, BIDIRECTIONAL };
    void set_mode(Mode mode);
    Mode get_mode() const;

    // Local -> Remote
    std::string poll_text_change();
    std::vector<std::string> poll_file_change();
    ClipboardContentDescriptor poll_content_change();
    std::optional<ClipboardContentDescriptor> poll_deduplicated_change();

    // Remote -> Local
    void apply_remote_text(const std::string& text);
    void apply_remote_files(const std::vector<std::string>& files);
    void apply_remote_image(const std::vector<uint8_t>& data);
    void apply_remote_html(const std::string& html);
    void apply_remote_rtf(const std::string& rtf);
    void apply_remote_content(const ClipboardContentDescriptor& desc);

    // File transfer
    void begin_file_copy(int32_t conn_id);
    void end_file_copy(int32_t conn_id);
    ProgressPercent get_file_progress() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ====== Clipboard File Server ======
class ClipboardFileServer {
public:
    ClipboardFileServer();
    ~ClipboardFileServer();

    struct FileEntry {
        std::string path;
        uint64_t size = 0;
        bool is_directory = false;
        std::vector<uint8_t> cached_data;
        ContentHash content_hash;
    };

    void add_file(const FileEntry& entry);
    void add_files(const std::vector<FileEntry>& entries);
    void remove_file(const std::string& path);
    void clear_files();
    std::vector<FileEntry> list_files() const;
    std::optional<FileEntry> get_file(const std::string& path) const;
    size_t file_count() const;
    uint64_t total_size() const;

    // Chunked serving
    std::vector<uint8_t> read_chunk(const std::string& path, uint64_t offset, uint64_t size);
    ProgressPercent get_serve_progress(const std::string& path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ====== Clipboard FUSE Filesystem (Stub) ======
class ClipboardFuseFs {
public:
    struct MountOptions {
        std::string mount_point = "/tmp/clipboard-fuse";
        bool read_only = true;
        bool allow_other = false;
        int32_t max_file_size_mb = 100;
    };

    ClipboardFuseFs();
    ~ClipboardFuseFs();

    bool mount(const MountOptions& options);
    void unmount();
    bool is_mounted() const;
    std::string mount_point() const;
    void set_file_server(std::shared_ptr<ClipboardFileServer> server);
    void update_contents(const std::vector<ClipboardFileServer::FileEntry>& entries);

    // Low-level FUSE operations (stubs)
    int fuse_getattr(const char* path, void* statbuf);
    int fuse_readdir(const char* path, void* buf, void* filler,
                     uint64_t offset, void* fi);
    int fuse_open(const char* path, void* fi);
    int fuse_read(const char* path, char* buf, size_t size,
                  uint64_t offset, void* fi);
    int fuse_release(const char* path, void* fi);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ====== Clipboard Viewer Support (Windows) ======
class ClipboardViewer {
public:
    ClipboardViewer();
    ~ClipboardViewer();

    using ChangeCallback = std::function<void()>;
    void set_change_callback(ChangeCallback cb);
    bool initialize(void* parent_window_handle = nullptr);
    void shutdown();
    bool is_active() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ====== Utility Functions ======
namespace util {

// Convert between image formats
std::vector<uint8_t> convert_image_format(const std::vector<uint8_t>& src,
    const std::string& src_format, const std::string& dst_format);

// Detect image format from magic bytes
std::string detect_image_format(const std::vector<uint8_t>& data);

// Parse Windows HTML Clipboard Format (CF_HTML)
std::string parse_cf_html(const std::string& raw);

// Generate Windows HTML Clipboard Format
std::string generate_cf_html(const std::string& html, const std::string& source_url = "");

// Get MIME type for clipboard format
std::string format_to_mime(ContentFormat fmt);

// Get file extension from MIME type
std::string mime_to_extension(const std::string& mime);

// Encode URI list for clipboard
std::string encode_uri_list(const std::vector<std::string>& uris);
std::vector<std::string> decode_uri_list(const std::string& raw);

// Platform-specific format name lookup
std::string platform_format_name(int format_id);

// Sanitize HTML for clipboard safety
std::string sanitize_html_for_clipboard(const std::string& html);

} // namespace util

} // namespace clipboard
