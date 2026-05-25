#include "clipboard/clipboard.hpp"
#include <spdlog/spdlog.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#import <AppKit/NSPasteboard.h>
#import <AppKit/NSImage.h>
#import <Foundation/Foundation.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

namespace clipboard {

// ====== Platform-Specific Clipboard ======

#ifdef _WIN32
class WindowsClipboard : public PlatformClipboard {
public:
    std::string get_text() override {
        if (!OpenClipboard(nullptr)) return "";
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (!h) { CloseClipboard(); return ""; }
        wchar_t* wstr = static_cast<wchar_t*>(GlobalLock(h));
        if (!wstr) { CloseClipboard(); return ""; }
        int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
        std::string result(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
        while (!result.empty() && result.back() == 0) result.pop_back();
        GlobalUnlock(h);
        CloseClipboard();
        return result;
    }

    bool set_text(const std::string& text) override {
        if (!OpenClipboard(nullptr)) return false;
        EmptyClipboard();
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
        if (!h) { CloseClipboard(); return false; }
        wchar_t* wstr = static_cast<wchar_t*>(GlobalLock(h));
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wstr, wlen);
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
        CloseClipboard();
        return true;
    }

    bool has_text() override {
        if (!OpenClipboard(nullptr)) return false;
        bool r = IsClipboardFormatAvailable(CF_UNICODETEXT) || IsClipboardFormatAvailable(CF_TEXT);
        CloseClipboard();
        return r;
    }

    std::vector<std::string> get_file_list() override {
        std::vector<std::string> files;
        if (!OpenClipboard(nullptr)) return files;
        HDROP hdrop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
        if (hdrop) {
            UINT count = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; i++) {
                wchar_t path[MAX_PATH];
                DragQueryFileW(hdrop, i, path, MAX_PATH);
                int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                std::string spath(len, 0);
                WideCharToMultiByte(CP_UTF8, 0, path, -1, spath.data(), len, nullptr, nullptr);
                while (!spath.empty() && spath.back() == 0) spath.pop_back();
                files.push_back(spath);
            }
        }
        CloseClipboard();
        return files;
    }

    bool set_file_list(const std::vector<std::string>& paths) override {
        if (!OpenClipboard(nullptr)) return false;
        EmptyClipboard();
        DROPFILES df = {sizeof(DROPFILES), {0, 0}, 0, TRUE};
        // Build wide string list
        size_t total = 0;
        for (auto& p : paths) total += p.size() + 1;
        total += 1; // double null terminator
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
            sizeof(DROPFILES) + total * sizeof(wchar_t));
        if (!h) { CloseClipboard(); return false; }
        auto* pdf = static_cast<DROPFILES*>(GlobalLock(h));
        *pdf = df;
        wchar_t* wptr = reinterpret_cast<wchar_t*>(pdf + 1);
        for (auto& p : paths) {
            MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, wptr,
                static_cast<int>(p.size() + 1));
            wptr += p.size() + 1;
        }
        *wptr = 0;
        GlobalUnlock(h);
        SetClipboardData(CF_HDROP, h);
        CloseClipboard();
        return true;
    }

    bool has_file_list() override {
        if (!OpenClipboard(nullptr)) return false;
        bool r = IsClipboardFormatAvailable(CF_HDROP);
        CloseClipboard();
        return r;
    }

    std::vector<uint8_t> get_image(const std::string& format) override { return {}; }
    bool set_image(const std::vector<uint8_t>&, const std::string&) override { return false; }
    std::string get_html() override { return ""; }
    bool set_html(const std::string&) override { return false; }

    bool clear() override {
        if (!OpenClipboard(nullptr)) return false;
        EmptyClipboard();
        CloseClipboard();
        return true;
    }

    bool owns_clipboard() override { return false; }

    void set_on_change(OnChange cb) override {
        onChange_ = std::move(cb);
    }

private:
    OnChange onChange_;
    static LRESULT CALLBACK clip_wnd_proc(HWND, UINT, WPARAM, LPARAM) { return 0; }
};

#elif defined(__linux__)
class X11Clipboard : public PlatformClipboard {
    Display* display_ = nullptr;
    Window window_;
    Atom clipboard_atom_, utf8_atom_, targets_atom_;
    OnChange onChange_;

public:
    X11Clipboard() {
        display_ = XOpenDisplay(nullptr);
        if (!display_) {
            spdlog::error("X11Clipboard: Failed to open display");
            return;
        }
        int screen = DefaultScreen(display_);
        window_ = XCreateSimpleWindow(display_, RootWindow(display_, screen),
            0, 0, 1, 1, 0, 0, 0);
        clipboard_atom_ = XInternAtom(display_, "CLIPBOARD", False);
        utf8_atom_ = XInternAtom(display_, "UTF8_STRING", False);
        targets_atom_ = XInternAtom(display_, "TARGETS", False);
    }
    ~X11Clipboard() {
        if (display_) {
            XDestroyWindow(display_, window_);
            XCloseDisplay(display_);
        }
    }

    std::string get_text() override {
        if (!display_) return "";
        XConvertSelection(display_, clipboard_atom_, utf8_atom_,
            utf8_atom_, window_, CurrentTime);
        XFlush(display_);
        return ""; // Async, simplified
    }

    bool set_text(const std::string& text) override {
        if (!display_) return false;
        XSetSelectionOwner(display_, clipboard_atom_, window_, CurrentTime);
        // Store text for later retrieval
        return true;
    }

    bool has_text() override {
        if (!display_) return false;
        return XGetSelectionOwner(display_, clipboard_atom_) != None;
    }

    std::vector<std::string> get_file_list() override { return {}; }
    bool set_file_list(const std::vector<std::string>&) override { return false; }
    bool has_file_list() override { return false; }
    std::vector<uint8_t> get_image(const std::string&) override { return {}; }
    bool set_image(const std::vector<uint8_t>&, const std::string&) override { return false; }
    std::string get_html() override { return ""; }
    bool set_html(const std::string&) override { return false; }
    bool clear() override { return set_text(""); }
    bool owns_clipboard() override {
        if (!display_) return false;
        return XGetSelectionOwner(display_, clipboard_atom_) == window_;
    }
    void set_on_change(OnChange cb) override { onChange_ = std::move(cb); }
};

#elif defined(__APPLE__)
class MacClipboard : public PlatformClipboard {
public:
    std::string get_text() override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            NSString* str = [pb stringForType:NSPasteboardTypeString];
            return str ? [str UTF8String] : "";
        }
    }

    bool set_text(const std::string& text) override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            return [pb setString:[NSString stringWithUTF8String:text.c_str()]
                forType:NSPasteboardTypeString];
        }
    }

    bool has_text() override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            return [pb availableTypeFromArray:@[NSPasteboardTypeString]] != nil;
        }
    }

    std::vector<std::string> get_file_list() override { return {}; }
    bool set_file_list(const std::vector<std::string>&) override { return false; }
    bool has_file_list() override { return false; }
    std::vector<uint8_t> get_image(const std::string&) override { return {}; }
    bool set_image(const std::vector<uint8_t>&, const std::string&) override { return false; }
    std::string get_html() override { return ""; }
    bool set_html(const std::string&) override { return false; }
    bool clear() override { return set_text(""); }
    bool owns_clipboard() override { return false; }
    void set_on_change(OnChange cb) override {}
};
#endif

std::unique_ptr<PlatformClipboard> PlatformClipboard::create() {
#ifdef _WIN32
    return std::make_unique<WindowsClipboard>();
#elif defined(__linux__)
    return std::make_unique<X11Clipboard>();
#elif defined(__APPLE__)
    return std::make_unique<MacClipboard>();
#else
    return nullptr;
#endif
}

// ====== Clipboard Monitor ======
struct ClipboardMonitor::Impl {
    std::unique_ptr<PlatformClipboard> clipboard;
    std::atomic<bool> running{false};
    std::thread worker;
    std::string last_text;
    std::vector<std::string> last_files;
    OnTextChange on_text;
    OnFileChange on_files;
    OnImageChange on_image;
    mutable std::mutex mutex;
};

ClipboardMonitor::ClipboardMonitor() : impl_(std::make_unique<Impl>()) {
    impl_->clipboard = PlatformClipboard::create();
}

ClipboardMonitor::~ClipboardMonitor() { stop(); }

void ClipboardMonitor::start(std::chrono::milliseconds interval) {
    if (!impl_->clipboard) return;
    impl_->running = true;
    impl_->worker = std::thread([this, interval]() {
        while (impl_->running) {
            auto text = impl_->clipboard->get_text();
            if (text != impl_->last_text && !text.empty()) {
                std::lock_guard lk(impl_->mutex);
                impl_->last_text = text;
                if (impl_->on_text) impl_->on_text(text);
            }
            auto files = impl_->clipboard->get_file_list();
            if (files != impl_->last_files && !files.empty()) {
                std::lock_guard lk(impl_->mutex);
                impl_->last_files = files;
                if (impl_->on_files) impl_->on_files(files);
            }
            std::this_thread::sleep_for(interval);
        }
    });
}

void ClipboardMonitor::stop() {
    impl_->running = false;
    if (impl_->worker.joinable()) impl_->worker.join();
}

bool ClipboardMonitor::is_running() const { return impl_->running; }

void ClipboardMonitor::set_on_text_change(OnTextChange cb) { impl_->on_text = std::move(cb); }
void ClipboardMonitor::set_on_file_change(OnFileChange cb) { impl_->on_files = std::move(cb); }
void ClipboardMonitor::set_on_image_change(OnImageChange cb) { impl_->on_image = std::move(cb); }

std::string ClipboardMonitor::last_text() const {
    std::lock_guard lk(impl_->mutex);
    return impl_->last_text;
}

std::vector<std::string> ClipboardMonitor::last_files() const {
    std::lock_guard lk(impl_->mutex);
    return impl_->last_files;
}

// ====== Clipboard Synchronizer ======
struct ClipboardSynchronizer::Impl {
    ClipboardSynchronizer::Mode mode = Mode::DISABLED;
    std::unique_ptr<ClipboardMonitor> monitor;
    std::unique_ptr<PlatformClipboard> clipboard;
    CliprdrServiceContext* context = nullptr;
    std::string last_hash;
    ProgressPercent file_progress;
};

ClipboardSynchronizer::ClipboardSynchronizer()
    : impl_(std::make_unique<Impl>()) {
    impl_->clipboard = PlatformClipboard::create();
    impl_->monitor = std::make_unique<ClipboardMonitor>();
}

ClipboardSynchronizer::~ClipboardSynchronizer() = default;

void ClipboardSynchronizer::set_mode(Mode mode) {
    impl_->mode = mode;
    if (mode == Mode::DISABLED) {
        impl_->monitor->stop();
    } else {
        impl_->monitor->start();
    }
}

ClipboardSynchronizer::Mode ClipboardSynchronizer::get_mode() const {
    return impl_->mode;
}

std::string ClipboardSynchronizer::poll_text_change() {
    auto text = impl_->monitor->last_text();
    return text;
}

std::vector<std::string> ClipboardSynchronizer::poll_file_change() {
    return impl_->monitor->last_files();
}

void ClipboardSynchronizer::apply_remote_text(const std::string& text) {
    if (impl_->mode == Mode::LOCAL_ONLY) return;
    if (impl_->clipboard) {
        impl_->clipboard->set_text(text);
    }
}

void ClipboardSynchronizer::apply_remote_files(const std::vector<std::string>& files) {
    if (impl_->mode == Mode::LOCAL_ONLY) return;
    if (impl_->clipboard) {
        impl_->clipboard->set_file_list(files);
    }
}

void ClipboardSynchronizer::apply_remote_image(const std::vector<uint8_t>&) {}

void ClipboardSynchronizer::begin_file_copy(int32_t) {
    impl_->file_progress = {};
}

void ClipboardSynchronizer::end_file_copy(int32_t) {
    impl_->file_progress = {100.0, false, false};
}

ProgressPercent ClipboardSynchronizer::get_file_progress() const {
    return impl_->file_progress;
}

} // namespace clipboard
