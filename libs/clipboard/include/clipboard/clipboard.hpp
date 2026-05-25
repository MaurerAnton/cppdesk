#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <cstdint>
#include <chrono>

namespace clipboard {

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
    Unknown = 99,
};

// ====== Clipboard File Message ======
struct ClipboardFile {
    enum Type { NOTIFY, REQUEST, DATA, DONE, CANCEL };
    Type type = NOTIFY;
    std::string path;
    uint64_t size = 0;
    std::vector<uint8_t> data;
    bool is_dir = false;
    int32_t conn_id = 0;
};

// ====== Progress ======
struct ProgressPercent {
    double percent = 0.0;
    bool is_canceled = false;
    bool is_failed = false;
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

    // Clear
    virtual bool clear() = 0;

    // Ownership
    virtual bool owns_clipboard() = 0;

    // Monitoring callback
    using OnChange = std::function<void()>;
    virtual void set_on_change(OnChange cb) = 0;

    // Factory
    static std::unique_ptr<PlatformClipboard> create();
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

    void set_on_text_change(OnTextChange cb);
    void set_on_file_change(OnFileChange cb);
    void set_on_image_change(OnImageChange cb);

    std::string last_text() const;
    std::vector<std::string> last_files() const;

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

    // Remote -> Local
    void apply_remote_text(const std::string& text);
    void apply_remote_files(const std::vector<std::string>& files);
    void apply_remote_image(const std::vector<uint8_t>& data);

    // File transfer
    void begin_file_copy(int32_t conn_id);
    void end_file_copy(int32_t conn_id);
    ProgressPercent get_file_progress() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace clipboard
