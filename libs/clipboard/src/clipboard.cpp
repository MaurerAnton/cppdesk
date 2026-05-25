#include "clipboard/clipboard.hpp"
#include <spdlog/spdlog.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <cstring>
#include <algorithm>
#include <map>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <array>
#include <filesystem>
#include <chrono>
#include <shared_mutex>
#include <cassert>
#include <random>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <ole2.h>
#include <shlwapi.h>
#include <gdiplus.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")
#elif defined(__APPLE__)
#import <AppKit/NSPasteboard.h>
#import <AppKit/NSImage.h>
#import <AppKit/NSBitmapImageRep.h>
#import <Foundation/Foundation.h>
#include <objc/runtime.h>
#include <objc/message.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace fs = std::filesystem;

namespace clipboard {

// ====== Version and constants ======
static constexpr const char* LIBRARY_VERSION = "2.0.0";
static constexpr size_t MAX_CLIPBOARD_SIZE_BYTES = 512 * 1024 * 1024; // 512 MB
static constexpr size_t CHUNK_SIZE = 65536; // 64 KB
static constexpr int INCR_CHUNK_MAX = 262144; // 256 KB max INCR chunk

// ====== ClipboardContentDescriptor::describe ======
std::string ClipboardContentDescriptor::describe() const {
    std::ostringstream oss;
    oss << "ClipboardContent[seq=" << sequence_number
        << " formats=";
    bool first = true;
    if (available_formats & ContentFormat::TEXT) {
        oss << (first ? "" : "|") << "TEXT"; first = false;
    }
    if (available_formats & ContentFormat::HTML) {
        oss << (first ? "" : "|") << "HTML"; first = false;
    }
    if (available_formats & ContentFormat::RTF) {
        oss << (first ? "" : "|") << "RTF"; first = false;
    }
    if (available_formats & ContentFormat::IMAGE_PNG) {
        oss << (first ? "" : "|") << "PNG"; first = false;
    }
    if (available_formats & ContentFormat::IMAGE_TIFF) {
        oss << (first ? "" : "|") << "TIFF"; first = false;
    }
    if (available_formats & ContentFormat::IMAGE_BMP) {
        oss << (first ? "" : "|") << "BMP"; first = false;
    }
    if (available_formats & ContentFormat::IMAGE_DIB) {
        oss << (first ? "" : "|") << "DIB"; first = false;
    }
    if (available_formats & ContentFormat::IMAGE_DIBV5) {
        oss << (first ? "" : "|") << "DIBV5"; first = false;
    }
    if (available_formats & ContentFormat::FILE_LIST) {
        oss << (first ? "" : "|") << "FILES(" << file_list.size() << ")"; first = false;
    }
    if (available_formats & ContentFormat::URI_LIST) {
        oss << (first ? "" : "|") << "URIS(" << uri_list.size() << ")"; first = false;
    }
    oss << "]";
    return oss.str();
}

// ====== Error String ======
const char* cliprdr_error_string(CliprdrError err) {
    switch (err) {
        case CliprdrError::None: return "No error";
        case CliprdrError::CliprdrName: return "Clipboard name error";
        case CliprdrError::CliprdrInit: return "Clipboard initialization error";
        case CliprdrError::CliprdrOutOfMemory: return "Out of memory";
        case CliprdrError::ClipboardInternalError: return "Internal clipboard error";
        case CliprdrError::ClipboardOccupied: return "Clipboard occupied by another process";
        case CliprdrError::ConversionFailure: return "Format conversion failure";
        case CliprdrError::OpenClipboard: return "Failed to open clipboard";
        case CliprdrError::FileError: return "File operation error";
        case CliprdrError::InvalidRequest: return "Invalid request";
        case CliprdrError::CommonError: return "Common error";
        case CliprdrError::FormatNotAvailable: return "Requested format not available";
        case CliprdrError::RenderingFailed: return "Delayed rendering failed";
        case CliprdrError::TransferInProgress: return "Transfer already in progress";
        case CliprdrError::Timeout: return "Operation timed out";
        default: return "Unknown error";
    }
}

// ====== ContentDeduplicator ======
struct ContentDeduplicator::Impl {
    std::unordered_map<std::string, CacheEntry> entries; // hex hash -> entry
    mutable std::shared_mutex mutex;
    size_t max_entries = 256;
};

ContentDeduplicator::ContentDeduplicator(size_t max)
    : impl_(std::make_unique<Impl>()) {
    impl_->max_entries = max;
}

std::optional<ClipboardContentDescriptor> ContentDeduplicator::find_by_hash(const ContentHash& hash) {
    std::shared_lock lock(impl_->mutex);
    auto hex = hash_to_hex(hash);
    auto it = impl_->entries.find(std::string(hex));
    if (it != impl_->entries.end()) {
        it->second.access_count++;
        return it->second.content;
    }
    return std::nullopt;
}

void ContentDeduplicator::store(const ContentHash& hash, const ClipboardContentDescriptor& content) {
    std::unique_lock lock(impl_->mutex);
    auto hex = hash_to_hex(hash);
    std::string key(hex);
    // Evict oldest if at capacity
    if (impl_->entries.size() >= impl_->max_entries && !impl_->entries.count(key)) {
        auto oldest = impl_->entries.begin();
        for (auto it = impl_->entries.begin(); it != impl_->entries.end(); ++it) {
            if (it->second.timestamp < oldest->second.timestamp) {
                oldest = it;
            }
        }
        impl_->entries.erase(oldest);
    }
    CacheEntry entry;
    entry.hash = hash;
    entry.content = content;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.access_count = 0;
    impl_->entries[key] = std::move(entry);
}

void ContentDeduplicator::invalidate(const ContentHash& hash) {
    std::unique_lock lock(impl_->mutex);
    impl_->entries.erase(std::string(hash_to_hex(hash)));
}

void ContentDeduplicator::clear() {
    std::unique_lock lock(impl_->mutex);
    impl_->entries.clear();
}

size_t ContentDeduplicator::size() const {
    std::shared_lock lock(impl_->mutex);
    return impl_->entries.size();
}

void ContentDeduplicator::set_max_entries(size_t max) {
    std::unique_lock lock(impl_->mutex);
    impl_->max_entries = max;
    while (impl_->entries.size() > max) {
        auto oldest = impl_->entries.begin();
        for (auto it = impl_->entries.begin(); it != impl_->entries.end(); ++it) {
            if (it->second.timestamp < oldest->second.timestamp) oldest = it;
        }
        impl_->entries.erase(oldest);
    }
}

ContentHash ContentDeduplicator::compute_text_hash(const std::string& text) {
    ContentHash hash{};
    uint64_t h = 0xcbf29ce484222325ULL;
    for (char c : text) {
        h ^= static_cast<uint8_t>(c);
        h *= 0x100000001b3ULL;
    }
    // Fill hash array with FNV-1a bits + some extra bytes
    for (size_t i = 0; i < 8 && i < 32; i++) {
        hash[i] = static_cast<uint8_t>(h >> (i * 8));
    }
    // Mix in length
    uint64_t len = text.size();
    for (size_t i = 0; i < 8 && (i + 8) < 32; i++) {
        hash[i + 8] = static_cast<uint8_t>(len >> (i * 8));
    }
    // Second hash for remaining bytes
    uint64_t h2 = 0x6c62272e07bb0142ULL;
    for (size_t i = text.size(); i > 0; i--) {
        h2 ^= static_cast<uint8_t>(text[i - 1]);
        h2 *= 0x100000001b3ULL;
    }
    for (size_t i = 0; i < 8 && (i + 16) < 32; i++) {
        hash[i + 16] = static_cast<uint8_t>(h2 >> (i * 8));
    }
    // Fill remainder with SHA-256-like padding
    for (size_t i = 24; i < 32; i++) {
        hash[i] = 0;
    }
    return hash;
}

ContentHash ContentDeduplicator::compute_data_hash(const std::vector<uint8_t>& data) {
    ContentHash hash{};
    // Simple double-hash approach: FNV-1a + DJB2
    uint64_t h1 = 0xcbf29ce484222325ULL;
    uint64_t h2 = 5381;
    for (uint8_t b : data) {
        h1 ^= b;
        h1 *= 0x100000001b3ULL;
        h2 = ((h2 << 5) + h2) + b;
    }
    for (size_t i = 0; i < 8; i++) hash[i] = static_cast<uint8_t>(h1 >> (i * 8));
    for (size_t i = 0; i < 8; i++) hash[i + 8] = static_cast<uint8_t>(h2 >> (i * 8));
    // Mix in length
    uint64_t len = data.size();
    for (size_t i = 0; i < 8; i++) hash[i + 16] = static_cast<uint8_t>(len >> (i * 8));
    // Fill remainder
    for (size_t i = 24; i < 32; i++) hash[i] = 0;
    return hash;
}

ContentHash ContentDeduplicator::compute_file_hash(const std::vector<std::string>& files) {
    ContentHash hash{};
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const auto& f : files) {
        for (char c : f) {
            h ^= static_cast<uint8_t>(c);
            h *= 0x100000001b3ULL;
        }
        h ^= 0xFF; // separator
    }
    for (size_t i = 0; i < 8; i++) hash[i] = static_cast<uint8_t>(h >> (i * 8));
    uint64_t count = files.size();
    for (size_t i = 0; i < 8 && (i + 8) < 32; i++) {
        hash[i + 8] = static_cast<uint8_t>(count >> (i * 8));
    }
    return hash;
}

std::string ContentDeduplicator::hash_to_hex(const ContentHash& hash) {
    std::ostringstream oss;
    for (uint8_t b : hash) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

// ====== Utility Functions ======
namespace util {

std::vector<uint8_t> convert_image_format(const std::vector<uint8_t>& src,
    const std::string& src_format, const std::string& dst_format) {
    if (src.empty()) return {};
    if (src_format == dst_format) return src;

#ifdef _WIN32
    // Use GDI+ for conversion
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartupInput gdiSI;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiSI, nullptr);

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, src.size());
    if (!hMem) { Gdiplus::GdiplusShutdown(gdiToken); return {}; }
    void* pMem = GlobalLock(hMem);
    memcpy(pMem, src.data(), src.size());
    GlobalUnlock(hMem);

    IStream* pStream = nullptr;
    CreateStreamOnHGlobal(hMem, TRUE, &pStream);

    Gdiplus::Image* img = Gdiplus::Image::FromStream(pStream);
    pStream->Release();
    if (!img) { Gdiplus::GdiplusShutdown(gdiToken); return {}; }

    std::vector<uint8_t> result;
    CLSID clsid = {};
    if (dst_format == "png") {
        CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &clsid);
    } else if (dst_format == "bmp") {
        CLSIDFromString(L"{557cf400-1a04-11d3-9a73-0000f81ef32e}", &clsid);
    } else if (dst_format == "jpeg" || dst_format == "jpg") {
        CLSIDFromString(L"{557cf401-1a04-11d3-9a73-0000f81ef32e}", &clsid);
    } else if (dst_format == "tiff") {
        CLSIDFromString(L"{557cf405-1a04-11d3-9a73-0000f81ef32e}", &clsid);
    }

    if (clsid.Data1 == 0) {
        delete img;
        Gdiplus::GdiplusShutdown(gdiToken);
        return src;
    }

    IStream* outStream = nullptr;
    CreateStreamOnHGlobal(nullptr, TRUE, &outStream);
    Gdiplus::Status st = img->Save(outStream, &clsid, nullptr);
    if (st == Gdiplus::Ok) {
        STATSTG stat;
        outStream->Stat(&stat, STATFLAG_NONAME);
        result.resize(static_cast<size_t>(stat.cbSize.QuadPart));
        LARGE_INTEGER li{};
        outStream->Seek(li, STREAM_SEEK_SET, nullptr);
        ULONG read = 0;
        outStream->Read(result.data(), static_cast<ULONG>(result.size()), &read);
    }
    outStream->Release();
    delete img;
    Gdiplus::GdiplusShutdown(gdiToken);
    return result;

#elif defined(__APPLE__)
    // Use NSImage for conversion
    @autoreleasepool {
        NSData* srcData = [NSData dataWithBytes:src.data() length:src.size()];
        NSImage* image = [[NSImage alloc] initWithData:srcData];
        if (!image) return {};

        NSBitmapImageFileType fileType = NSPNGFileType;
        if (dst_format == "tiff") fileType = NSTIFFFileType;
        else if (dst_format == "bmp") fileType = NSBMPFileType;
        else if (dst_format == "jpeg" || dst_format == "jpg") fileType = NSJPEGFileType;
        else fileType = NSPNGFileType;

        NSData* outData = nil;
        if ([image.representations count] > 0) {
            NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
                initWithData:[image TIFFRepresentation]];
            outData = [rep representationUsingType:fileType properties:@{}];
            [rep release];
        }
        [image release];

        if (!outData) return {};
        std::vector<uint8_t> result([outData length]);
        memcpy(result.data(), [outData bytes], [outData length]);
        return result;
    }
#else
    spdlog::warn("convert_image_format: not fully supported on this platform");
    return src;
#endif
}

std::string detect_image_format(const std::vector<uint8_t>& data) {
    if (data.size() < 8) return "unknown";

    // PNG magic: 89 50 4E 47 0D 0A 1A 0A
    if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47)
        return "png";
    // JPEG magic: FF D8 FF
    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
        return "jpeg";
    // BMP magic: 42 4D (BM)
    if (data[0] == 0x42 && data[1] == 0x4D)
        return "bmp";
    // TIFF magic: 49 49 2A 00 (little-endian) or 4D 4D 00 2A (big-endian)
    if (data[0] == 0x49 && data[1] == 0x49 && data[2] == 0x2A && data[3] == 0x00)
        return "tiff";
    if (data[0] == 0x4D && data[1] == 0x4D && data[2] == 0x00 && data[3] == 0x2A)
        return "tiff";
    // GIF magic: 47 49 46 38
    if (data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x38)
        return "gif";
    // WebP magic: 52 49 46 46 ... 57 45 42 50
    if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46 &&
        data.size() >= 12 && data[8] == 0x57 && data[9] == 0x45 && data[10] == 0x42 && data[11] == 0x50)
        return "webp";
    return "unknown";
}

std::string parse_cf_html(const std::string& raw) {
    if (raw.empty()) return "";

    // CF_HTML format starts with Version:...\r\n header
    // Format: Version:XX\r\nStartHTML:NNNN\r\nEndHTML:NNNN\r\n...\r\n\r\n<html>...</html>
    size_t start = 0;
    size_t end = raw.size();

    auto header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        header_end = raw.find("\n\n");
    }
    if (header_end == std::string::npos) {
        // No header found, assume entire content is HTML
        return raw;
    }

    // Parse StartHTML and EndHTML from header
    std::string header = raw.substr(0, header_end);
    auto start_pos = header.find("StartHTML:");
    auto end_pos = header.find("EndHTML:");

    if (start_pos != std::string::npos) {
        start_pos += 10; // length of "StartHTML:"
        auto end_line = header.find("\r\n", start_pos);
        if (end_line == std::string::npos) end_line = header.find("\n", start_pos);
        if (end_line != std::string::npos) {
            try {
                start = static_cast<size_t>(std::stoull(header.substr(start_pos, end_line - start_pos)));
            } catch (...) { start = header_end + 4; }
        }
    } else {
        start = header_end + 4;
    }

    if (end_pos != std::string::npos) {
        end_pos += 8;
        auto end_line = header.find("\r\n", end_pos);
        if (end_line == std::string::npos) end_line = header.find("\n", end_pos);
        if (end_line != std::string::npos) {
            try {
                end = static_cast<size_t>(std::stoull(header.substr(end_pos, end_line - end_pos)));
            } catch (...) { end = raw.size(); }
        }
    }

    if (start >= raw.size()) return "";
    if (end > raw.size()) end = raw.size();
    if (start >= end) return "";

    return raw.substr(start, end - start);
}

std::string generate_cf_html(const std::string& html, const std::string& source_url) {
    // CF_HTML Format:
    // Version:0.9\r\n
    // StartHTML:XXXXXX\r\n
    // EndHTML:XXXXXX\r\n
    // StartFragment:XXXXXX\r\n
    // EndFragment:XXXXXX\r\n
    // ...optional headers...\r\n
    // \r\n
    // <!--StartFragment-->\r\n
    // ...html fragment...\r\n
    // <!--EndFragment-->\r\n

    std::ostringstream header;
    header << "Version:0.9\r\n";
    if (!source_url.empty()) {
        header << "SourceURL:" << source_url << "\r\n";
    }

    // Build the fragment
    std::string fragment = "<!--StartFragment-->\r\n" + html + "\r\n<!--EndFragment-->\r\n";

    // We need placeholder positions
    std::string startHTML_marker = "StartHTML:0000000000";
    std::string endHTML_marker = "EndHTML:0000000000";
    std::string startFrag_marker = "StartFragment:0000000000";
    std::string endFrag_marker = "EndFragment:0000000000";

    // Calculate positions starting after full header + \r\n\r\n
    std::string full_header = "Version:0.9\r\n";
    if (!source_url.empty()) {
        full_header += "SourceURL:" + source_url + "\r\n";
    }
    full_header += startHTML_marker + "\r\n";
    full_header += endHTML_marker + "\r\n";
    full_header += startFrag_marker + "\r\n";
    full_header += endFrag_marker + "\r\n";
    full_header += "\r\n";

    size_t header_nl_pos = full_header.size();
    size_t startHTML = header_nl_pos;
    size_t startFrag = header_nl_pos + fragment.find("<!--StartFragment-->");
    size_t endFrag = header_nl_pos + fragment.find("<!--EndFragment-->") + 17; // "<!--EndFragment-->"
    size_t endHTML = header_nl_pos + fragment.size();

    // Rebuild header with correct offsets
    std::ostringstream result;
    result << "Version:0.9\r\n";
    if (!source_url.empty()) {
        result << "SourceURL:" << source_url << "\r\n";
    }
    result << "StartHTML:" << std::setw(10) << std::setfill('0') << startHTML << "\r\n";
    result << "EndHTML:" << std::setw(10) << std::setfill('0') << endHTML << "\r\n";
    result << "StartFragment:" << std::setw(10) << std::setfill('0') << startFrag << "\r\n";
    result << "EndFragment:" << std::setw(10) << std::setfill('0') << endFrag << "\r\n";
    result << "\r\n";
    result << fragment;

    return result.str();
}

std::string format_to_mime(ContentFormat fmt) {
    switch (fmt) {
        case ContentFormat::TEXT: return "text/plain";
        case ContentFormat::HTML: return "text/html";
        case ContentFormat::RTF: return "text/rtf";
        case ContentFormat::IMAGE_PNG: return "image/png";
        case ContentFormat::IMAGE_TIFF: return "image/tiff";
        case ContentFormat::IMAGE_BMP: return "image/bmp";
        case ContentFormat::IMAGE_DIB: return "image/bmp";
        case ContentFormat::IMAGE_DIBV5: return "image/bmp";
        case ContentFormat::FILE_LIST: return "text/uri-list";
        case ContentFormat::URI_LIST: return "text/uri-list";
        default: return "application/octet-stream";
    }
}

std::string mime_to_extension(const std::string& mime) {
    static const std::unordered_map<std::string, std::string> map = {
        {"text/plain", ".txt"},
        {"text/html", ".html"},
        {"text/css", ".css"},
        {"text/rtf", ".rtf"},
        {"image/png", ".png"},
        {"image/jpeg", ".jpg"},
        {"image/gif", ".gif"},
        {"image/bmp", ".bmp"},
        {"image/tiff", ".tiff"},
        {"image/webp", ".webp"},
        {"image/svg+xml", ".svg"},
        {"application/pdf", ".pdf"},
        {"application/rtf", ".rtf"},
        {"text/uri-list", ".uri"},
    };
    auto it = map.find(mime);
    return it != map.end() ? it->second : ".bin";
}

std::string encode_uri_list(const std::vector<std::string>& uris) {
    std::ostringstream oss;
    for (size_t i = 0; i < uris.size(); i++) {
        if (i > 0) oss << "\r\n";
        oss << uris[i];
        if (i == uris.size() - 1) oss << "\r\n";
    }
    return oss.str();
}

std::vector<std::string> decode_uri_list(const std::string& raw) {
    std::vector<std::string> result;
    std::istringstream iss(raw);
    std::string line;
    while (std::getline(iss, line)) {
        // Remove trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] != '#') {
            result.push_back(line);
        }
    }
    return result;
}

std::string platform_format_name(int format_id) {
#ifdef _WIN32
    switch (format_id) {
        case CF_TEXT: return "CF_TEXT";
        case CF_BITMAP: return "CF_BITMAP";
        case CF_METAFILEPICT: return "CF_METAFILEPICT";
        case CF_SYLK: return "CF_SYLK";
        case CF_DIF: return "CF_DIF";
        case CF_TIFF: return "CF_TIFF";
        case CF_OEMTEXT: return "CF_OEMTEXT";
        case CF_DIB: return "CF_DIB";
        case CF_PALETTE: return "CF_PALETTE";
        case CF_PENDATA: return "CF_PENDATA";
        case CF_RIFF: return "CF_RIFF";
        case CF_WAVE: return "CF_WAVE";
        case CF_UNICODETEXT: return "CF_UNICODETEXT";
        case CF_ENHMETAFILE: return "CF_ENHMETAFILE";
        case CF_HDROP: return "CF_HDROP";
        case CF_LOCALE: return "CF_LOCALE";
        case CF_DIBV5: return "CF_DIBV5";
        default: {
            char name[256];
            if (GetClipboardFormatNameA(format_id, name, 256) > 0)
                return std::string(name);
            return "CF_PRIVATE(" + std::to_string(format_id) + ")";
        }
    }
#else
    (void)format_id;
    return "unknown";
#endif
}

std::string sanitize_html_for_clipboard(const std::string& html) {
    // Basic sanitization: remove script tags, event handlers, etc.
    std::string result = html;

    // Remove script blocks
    size_t pos = 0;
    while ((pos = result.find("<script", pos)) != std::string::npos) {
        size_t end = result.find("</script>", pos);
        size_t end2 = result.find("/>", pos);
        size_t end3 = result.find(">", pos);
        if (end != std::string::npos) {
            size_t real_end = end + 9; // </script>
            // Find minimum closing
            if (end2 != std::string::npos && end2 < end) real_end = end2 + 2;
            else if (end3 != std::string::npos && end3 < end) real_end = end3 + 1;
            result.erase(pos, real_end - pos);
        } else if (end2 != std::string::npos) {
            result.erase(pos, end2 + 2 - pos);
        } else if (end3 != std::string::npos) {
            result.erase(pos, end3 + 1 - pos);
        } else {
            break;
        }
    }

    // Remove on* event attributes (simplified)
    pos = 0;
    while ((pos = result.find(" on", pos)) != std::string::npos) {
        // Check it's an attribute
        if (pos > 0 && (result[pos-1] == ' ' || result[pos-1] == '\t' || result[pos-1] == '\r' || result[pos-1] == '\n')) {
            size_t eq = result.find('=', pos);
            if (eq != std::string::npos && eq < result.find('>', pos)) {
                size_t q_start = eq + 1;
                if (q_start < result.size() && (result[q_start] == '"' || result[q_start] == '\'')) {
                    char quote = result[q_start];
                    size_t q_end = result.find(quote, q_start + 1);
                    if (q_end != std::string::npos) {
                        result.erase(pos, q_end - pos + 1);
                        continue;
                    }
                }
            }
        }
        pos++;
    }

    return result;
}

} // namespace util

// ====== Platform-Specific Clipboard ======

#ifdef _WIN32

// ====== Windows Clipboard Formats ======
// We detect/cache these format IDs at init time
static UINT CF_HTML_FORMAT = 0;
static UINT CF_RTF_FORMAT = 0;
static UINT CF_PNG = 0;
static UINT CF_URILIST = 0;

static void init_windows_formats() {
    static std::once_flag once;
    std::call_once(once, []() {
        CF_HTML_FORMAT = RegisterClipboardFormatW(L"HTML Format");
        CF_RTF_FORMAT = RegisterClipboardFormatW(L"Rich Text Format");
        CF_PNG = RegisterClipboardFormatW(L"PNG");
        CF_URILIST = RegisterClipboardFormatW(L"text/uri-list");
        spdlog::info("Windows clipboard formats registered: HTML={}, RTF={}, PNG={}, URILIST={}",
            CF_HTML_FORMAT, CF_RTF_FORMAT, CF_PNG, CF_URILIST);
    });
}

// ====== Delayed Rendering Data ======
struct DelayedRenderData {
    std::shared_ptr<DelayedRenderer> renderer;
    ClipboardContentDescriptor descriptor;
    HWND owner_window = nullptr;
    std::atomic<bool> rendering{false};
};

// Thread-local delayed render data
static thread_local DelayedRenderData* g_delayed_data = nullptr;

// ====== Windows Clipboard Viewer Chain ======
class WindowsClipboardViewerImpl {
public:
    HWND viewer_window = nullptr;
    HWND next_viewer = nullptr;
    HWND parent_window = nullptr;
    ClipboardViewer::ChangeCallback callback;
    std::atomic<bool> active{false};
    std::thread message_thread;
    std::atomic<bool> thread_running{false};

    static constexpr const wchar_t* WINDOW_CLASS = L"ClipboardViewerWindow";

    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<WindowsClipboardViewerImpl*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (msg) {
            case WM_CREATE: {
                auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
                self = static_cast<WindowsClipboardViewerImpl*>(cs->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                if (self) {
                    self->next_viewer = SetClipboardViewer(hwnd);
                    self->active = true;
                }
                return 0;
            }

            case WM_CHANGECBCHAIN:
                if (self && reinterpret_cast<HWND>(wp) == self->next_viewer) {
                    self->next_viewer = reinterpret_cast<HWND>(lp);
                } else if (self && self->next_viewer) {
                    SendMessageW(self->next_viewer, msg, wp, lp);
                }
                return 0;

            case WM_DRAWCLIPBOARD:
                if (self && self->callback) {
                    self->callback();
                }
                if (self && self->next_viewer) {
                    SendMessageW(self->next_viewer, msg, wp, lp);
                }
                return 0;

            case WM_DESTROY:
                if (self) {
                    ChangeClipboardChain(hwnd, self->next_viewer);
                    self->next_viewer = nullptr;
                }
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    bool create_window(HWND parent) {
        parent_window = parent;

        // Register window class
        WNDCLASSW wc = {};
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = WINDOW_CLASS;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            spdlog::error("ClipboardViewer: Failed to register window class");
            return false;
        }

        viewer_window = CreateWindowExW(
            0, WINDOW_CLASS, L"Clipboard Viewer",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
            parent_window, nullptr,
            GetModuleHandleW(nullptr), this);

        if (!viewer_window) {
            spdlog::error("ClipboardViewer: Failed to create viewer window, error={}",
                GetLastError());
            return false;
        }

        // Start message pump in background thread
        thread_running = true;
        message_thread = std::thread([this]() {
            spdlog::info("ClipboardViewer: message pump started");
            MSG msg;
            while (thread_running && GetMessageW(&msg, nullptr, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            spdlog::info("ClipboardViewer: message pump stopped");
        });

        return true;
    }

    void destroy_window() {
        thread_running = false;
        if (viewer_window) {
            PostMessageW(viewer_window, WM_QUIT, 0, 0);
        }
        if (message_thread.joinable()) {
            message_thread.join();
        }
        if (viewer_window) {
            DestroyWindow(viewer_window);
            viewer_window = nullptr;
        }
        UnregisterClassW(WINDOW_CLASS, GetModuleHandleW(nullptr));
        active = false;
    }
};

static LRESULT CALLBACK delayed_render_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* data = reinterpret_cast<DelayedRenderData*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_RENDERFORMAT: {
            if (!data || !data->renderer) break;
            UINT fmt = static_cast<UINT>(wp);
            ContentFormat cfmt;

            // Map Windows format to our ContentFormat
            if (fmt == CF_UNICODETEXT || fmt == CF_TEXT) {
                cfmt = ContentFormat::TEXT;
            } else if (fmt == CF_HTML_FORMAT) {
                cfmt = ContentFormat::HTML;
            } else if (fmt == CF_RTF_FORMAT) {
                cfmt = ContentFormat::RTF;
            } else if (fmt == CF_DIB || fmt == CF_DIBV5) {
                cfmt = (fmt == CF_DIBV5) ? ContentFormat::IMAGE_DIBV5 : ContentFormat::IMAGE_DIB;
            } else if (fmt == CF_HDROP) {
                cfmt = ContentFormat::FILE_LIST;
            } else if (fmt == CF_PNG) {
                cfmt = ContentFormat::IMAGE_PNG;
            } else if (fmt == CF_TIFF) {
                cfmt = ContentFormat::IMAGE_TIFF;
            } else {
                spdlog::warn("WM_RENDERFORMAT: unknown format {}", fmt);
                break;
            }

            if (!data->renderer->can_render(cfmt)) {
                spdlog::warn("WM_RENDERFORMAT: renderer cannot render format {}", static_cast<uint32_t>(cfmt));
                SetClipboardData(fmt, nullptr);
                break;
            }

            auto rendered = data->renderer->render(cfmt);
            if (rendered.empty()) {
                SetClipboardData(fmt, nullptr);
                break;
            }

            // Allocate global memory and copy data
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, rendered.size());
            if (hMem) {
                void* pMem = GlobalLock(hMem);
                memcpy(pMem, rendered.data(), rendered.size());
                GlobalUnlock(hMem);
                SetClipboardData(fmt, hMem);
                spdlog::debug("WM_RENDERFORMAT: rendered format {}, {} bytes", fmt, rendered.size());
            }
            break;
        }

        case WM_RENDERALLFORMATS: {
            if (!data || !data->renderer) break;
            if (!OpenClipboard(hwnd)) break;

            // Render all formats the renderer supports
            for (int fmt_idx = 0; fmt_idx < 16; fmt_idx++) {
                ContentFormat cfmt = static_cast<ContentFormat>(1 << fmt_idx);
                if (!data->renderer->can_render(cfmt)) continue;

                UINT wfmt = 0;
                if (cfmt == ContentFormat::TEXT) wfmt = CF_UNICODETEXT;
                else if (cfmt == ContentFormat::HTML) wfmt = CF_HTML_FORMAT;
                else if (cfmt == ContentFormat::RTF) wfmt = CF_RTF_FORMAT;
                else if (cfmt == ContentFormat::IMAGE_DIB) wfmt = CF_DIB;
                else if (cfmt == ContentFormat::IMAGE_DIBV5) wfmt = CF_DIBV5;
                else if (cfmt == ContentFormat::FILE_LIST) wfmt = CF_HDROP;
                else if (cfmt == ContentFormat::IMAGE_PNG) wfmt = CF_PNG;

                if (wfmt == 0) continue;

                auto rendered = data->renderer->render(cfmt);
                if (!rendered.empty()) {
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, rendered.size());
                    if (hMem) {
                        void* pMem = GlobalLock(hMem);
                        memcpy(pMem, rendered.data(), rendered.size());
                        GlobalUnlock(hMem);
                        SetClipboardData(wfmt, hMem);
                    }
                }
            }
            CloseClipboard();
            break;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ====== OLE IDataObject Implementation ======
class OleClipboardDataObject : public IDataObject {
private:
    LONG ref_count_ = 1;
    ClipboardContentDescriptor content_;
    std::unordered_map<UINT, std::vector<uint8_t>> format_data_;
    mutable std::mutex mutex_;

    void build_format_data() {
        format_data_.clear();

        if (content_.has_format(ContentFormat::TEXT) && !content_.text.empty()) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, content_.text.c_str(), -1, nullptr, 0);
            std::vector<uint8_t> wdata(wlen * sizeof(wchar_t));
            MultiByteToWideChar(CP_UTF8, 0, content_.text.c_str(), -1,
                reinterpret_cast<wchar_t*>(wdata.data()), wlen);
            format_data_[CF_UNICODETEXT] = std::move(wdata);
        }

        if (content_.has_format(ContentFormat::HTML) && !content_.html.empty()) {
            std::string cf_html = util::generate_cf_html(content_.html);
            format_data_[CF_HTML_FORMAT] = std::vector<uint8_t>(cf_html.begin(), cf_html.end());
        }

        if (content_.has_format(ContentFormat::RTF) && !content_.rtf.empty()) {
            format_data_[CF_RTF_FORMAT] = std::vector<uint8_t>(content_.rtf.begin(), content_.rtf.end());
        }

        if (content_.has_format(ContentFormat::IMAGE_PNG) && !content_.image_data.empty()) {
            format_data_[CF_PNG] = content_.image_data;
        }

        if (content_.has_format(ContentFormat::IMAGE_DIB) && !content_.image_data.empty()) {
            format_data_[CF_DIB] = content_.image_data;
        }

        if (content_.has_format(ContentFormat::IMAGE_DIBV5) && !content_.image_data.empty()) {
            format_data_[CF_DIBV5] = content_.image_data;
        }

        if (content_.has_format(ContentFormat::FILE_LIST) && !content_.file_list.empty()) {
            // Build DROPFILES structure
            size_t total = 0;
            for (auto& p : content_.file_list) total += p.size() + 1;
            total += 1;
            size_t alloc_size = sizeof(DROPFILES) + total * sizeof(wchar_t);
            std::vector<uint8_t> drop_data(alloc_size, 0);

            auto* pdf = reinterpret_cast<DROPFILES*>(drop_data.data());
            pdf->pFiles = sizeof(DROPFILES);
            pdf->fWide = TRUE;

            wchar_t* wptr = reinterpret_cast<wchar_t*>(pdf + 1);
            for (const auto& p : content_.file_list) {
                MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, wptr,
                    static_cast<int>(p.size() + 1));
                wptr += p.size() + 1;
            }
            *wptr = 0;
            format_data_[CF_HDROP] = std::move(drop_data);
        }

        if (content_.has_format(ContentFormat::URI_LIST) && !content_.uri_list.empty()) {
            std::string uris = util::encode_uri_list(content_.uri_list);
            format_data_[CF_URILIST] = std::vector<uint8_t>(uris.begin(), uris.end());
        }
    }

public:
    explicit OleClipboardDataObject(const ClipboardContentDescriptor& content)
        : content_(content) {
        build_format_data();
        spdlog::debug("OleClipboardDataObject: created with {} formats", format_data_.size());
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&ref_count_);
    }

    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&ref_count_);
        if (r == 0) delete this;
        return r;
    }

    // IDataObject
    STDMETHODIMP GetData(FORMATETC* pformatetc, STGMEDIUM* pmedium) override {
        if (!pformatetc || !pmedium) return E_INVALIDARG;

        std::lock_guard lk(mutex_);
        auto it = format_data_.find(pformatetc->cfFormat);
        if (it == format_data_.end()) {
            spdlog::debug("OleClipboardDataObject::GetData: format {} not available",
                util::platform_format_name(pformatetc->cfFormat));
            return DV_E_FORMATETC;
        }

        const auto& data = it->second;
        pmedium->tymed = TYMED_HGLOBAL;
        pmedium->hGlobal = GlobalAlloc(GMEM_MOVEABLE, data.size());
        if (!pmedium->hGlobal) return E_OUTOFMEMORY;

        void* pMem = GlobalLock(pmedium->hGlobal);
        memcpy(pMem, data.data(), data.size());
        GlobalUnlock(pmedium->hGlobal);
        pmedium->pUnkForRelease = nullptr;

        spdlog::debug("OleClipboardDataObject::GetData: delivered format {}, {} bytes",
            util::platform_format_name(pformatetc->cfFormat), data.size());
        return S_OK;
    }

    STDMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override {
        return E_NOTIMPL;
    }

    STDMETHODIMP QueryGetData(FORMATETC* pformatetc) override {
        if (!pformatetc) return E_INVALIDARG;
        std::lock_guard lk(mutex_);
        if (format_data_.count(pformatetc->cfFormat)) return S_OK;
        return DV_E_FORMATETC;
    }

    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override {
        return E_NOTIMPL;
    }

    STDMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override {
        return E_NOTIMPL;
    }

    STDMETHODIMP EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc) override {
        if (!ppenumFormatEtc) return E_INVALIDARG;
        if (dwDirection != DATADIR_GET) return E_NOTIMPL;

        std::lock_guard lk(mutex_);
        std::vector<FORMATETC> formats;
        formats.reserve(format_data_.size());
        for (auto& [fmt, data] : format_data_) {
            FORMATETC fe{};
            fe.cfFormat = fmt;
            fe.dwAspect = DVASPECT_CONTENT;
            fe.lindex = -1;
            fe.tymed = TYMED_HGLOBAL;
            formats.push_back(fe);
        }

        // Create a simple enumerator
        *ppenumFormatEtc = nullptr; // Simplified: real impl would create IEnumFORMATETC
        return E_NOTIMPL; // Simplified - would need full IEnumFORMATETC implementation
    }

    STDMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    STDMETHODIMP DUnadvise(DWORD) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    STDMETHODIMP EnumDAdvise(IEnumSTATDATA**) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
};

// ====== Full Windows Clipboard Implementation ======
class WindowsClipboard : public PlatformClipboard {
    HWND helper_window_ = nullptr;
    OnChange onChange_;
    std::shared_ptr<DelayedRenderer> delayed_renderer_;
    std::atomic<uint64_t> change_count_{0};
    std::atomic<bool> owns_clipboard_{false};

    bool open_and_clear() {
        if (!OpenClipboard(helper_window_)) {
            spdlog::warn("WindowsClipboard: OpenClipboard failed, error={}", GetLastError());
            return false;
        }
        EmptyClipboard();
        owns_clipboard_ = true;
        return true;
    }

    std::string wchar_to_utf8(const wchar_t* wstr) {
        if (!wstr) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return "";
        std::string result(len - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], len, nullptr, nullptr);
        return result;
    }

    std::wstring utf8_to_wchar(const std::string& str) {
        int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        if (len <= 0) return L"";
        std::wstring result(len - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], len);
        return result;
    }

    // Helper window for delayed rendering and OLE operations
    static LRESULT CALLBACK helper_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<WindowsClipboard*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (msg) {
            case WM_CREATE:
                return 0;
            case WM_CLIPBOARDUPDATE:
                if (self) {
                    self->change_count_.fetch_add(1);
                    self->owns_clipboard_ = false;
                    if (self->onChange_) self->onChange_();
                }
                return 0;
            case WM_DESTROY:
                if (self) RemoveClipboardFormatListener(hwnd);
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void init_helper_window() {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = helper_wnd_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"ClipboardHelperWindow";
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassW(&wc);

        helper_window_ = CreateWindowExW(
            0, L"ClipboardHelperWindow", L"Clipboard Helper",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
            nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);

        if (helper_window_) {
            SetWindowLongPtrW(helper_window_, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(this));
            AddClipboardFormatListener(helper_window_);
            spdlog::info("WindowsClipboard: helper window created with format listener");
        }
    }

public:
    WindowsClipboard() {
        init_windows_formats();
        init_helper_window();
    }

    ~WindowsClipboard() override {
        if (helper_window_) {
            DestroyWindow(helper_window_);
        }
    }

    // ====== Text Operations ======
    std::string get_text() override {
        if (!OpenClipboard(nullptr)) return "";
        std::string result;

        // Try CF_UNICODETEXT first
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            wchar_t* wstr = static_cast<wchar_t*>(GlobalLock(h));
            if (wstr) {
                result = wchar_to_utf8(wstr);
                GlobalUnlock(h);
            }
        } else {
            // Fallback to CF_TEXT
            h = GetClipboardData(CF_TEXT);
            if (h) {
                char* str = static_cast<char*>(GlobalLock(h));
                if (str) {
                    result = str;
                    GlobalUnlock(h);
                }
            }
        }
        CloseClipboard();
        return result;
    }

    bool set_text(const std::string& text) override {
        if (!open_and_clear()) return false;
        change_count_.fetch_add(1);

        std::wstring wstr = utf8_to_wchar(text);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (wstr.size() + 1) * sizeof(wchar_t));
        if (!h) { CloseClipboard(); return false; }
        wchar_t* wptr = static_cast<wchar_t*>(GlobalLock(h));
        wcscpy_s(wptr, wstr.size() + 1, wstr.c_str());
        GlobalUnlock(h);

        SetClipboardData(CF_UNICODETEXT, h);
        CloseClipboard();
        spdlog::debug("WindowsClipboard: set_text {} chars", text.size());
        return true;
    }

    bool has_text() override {
        if (!OpenClipboard(nullptr)) return false;
        bool r = IsClipboardFormatAvailable(CF_UNICODETEXT) ||
                 IsClipboardFormatAvailable(CF_TEXT);
        CloseClipboard();
        return r;
    }

    // ====== File Operations ======
    std::vector<std::string> get_file_list() override {
        std::vector<std::string> files;
        if (!OpenClipboard(nullptr)) return files;

        HDROP hdrop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
        if (hdrop) {
            UINT count = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; i++) {
                wchar_t path[MAX_PATH * 2]; // Allow long paths
                if (DragQueryFileW(hdrop, i, path, MAX_PATH * 2) > 0) {
                    files.push_back(wchar_to_utf8(path));
                }
            }
            spdlog::debug("WindowsClipboard: get_file_list found {} files", files.size());
        }
        CloseClipboard();
        return files;
    }

    bool set_file_list(const std::vector<std::string>& paths) override {
        if (!open_and_clear()) return false;
        change_count_.fetch_add(1);

        size_t total = 0;
        for (auto& p : paths) total += p.size() + 1;
        total += 1;

        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
            sizeof(DROPFILES) + total * sizeof(wchar_t));
        if (!h) { CloseClipboard(); return false; }

        auto* pdf = static_cast<DROPFILES*>(GlobalLock(h));
        pdf->pFiles = sizeof(DROPFILES);
        pdf->fWide = TRUE;

        wchar_t* wptr = reinterpret_cast<wchar_t*>(pdf + 1);
        for (const auto& p : paths) {
            std::wstring wpath = utf8_to_wchar(p);
            wcscpy_s(wptr, wpath.size() + 1, wpath.c_str());
            wptr += wpath.size() + 1;
        }
        *wptr = 0;
        GlobalUnlock(h);

        SetClipboardData(CF_HDROP, h);
        CloseClipboard();
        spdlog::debug("WindowsClipboard: set_file_list {} files", paths.size());
        return true;
    }

    bool has_file_list() override {
        if (!OpenClipboard(nullptr)) return false;
        bool r = IsClipboardFormatAvailable(CF_HDROP);
        CloseClipboard();
        return r;
    }

    // ====== Image Operations ======
    std::vector<uint8_t> get_image(const std::string& format) override {
        std::vector<uint8_t> result;
        if (!OpenClipboard(nullptr)) return result;

        if (format == "png" || format == "PNG") {
            HANDLE h = GetClipboardData(CF_PNG);
            if (h) {
                size_t sz = GlobalSize(h);
                void* ptr = GlobalLock(h);
                if (ptr && sz > 0) {
                    result.assign(static_cast<uint8_t*>(ptr),
                        static_cast<uint8_t*>(ptr) + sz);
                    GlobalUnlock(h);
                }
            }
        }

        if (result.empty() && (format == "tiff" || format == "TIFF")) {
            HANDLE h = GetClipboardData(CF_TIFF);
            if (h) {
                size_t sz = GlobalSize(h);
                void* ptr = GlobalLock(h);
                if (ptr && sz > 0) {
                    result.assign(static_cast<uint8_t*>(ptr),
                        static_cast<uint8_t*>(ptr) + sz);
                    GlobalUnlock(h);
                }
            }
        }

        // Try DIB/DIBV5 and convert to requested format
        if (result.empty()) {
            UINT dib_fmt = (format == "dibv5") ? CF_DIBV5 : CF_DIB;
            HANDLE h = GetClipboardData(dib_fmt);
            if (!h) {
                // Try the other DIB format
                dib_fmt = (dib_fmt == CF_DIB) ? CF_DIBV5 : CF_DIB;
                h = GetClipboardData(dib_fmt);
            }
            if (h) {
                void* ptr = GlobalLock(h);
                if (ptr) {
                    BITMAPINFOHEADER* bmi = static_cast<BITMAPINFOHEADER*>(ptr);
                    size_t sz = bmi->biSizeImage;
                    if (sz == 0) {
                        int bytes_per_line = ((bmi->biWidth * bmi->biBitCount + 31) / 32) * 4;
                        sz = bytes_per_line * abs(bmi->biHeight);
                    }
                    sz += bmi->biSize;
                    result.assign(static_cast<uint8_t*>(ptr),
                        static_cast<uint8_t*>(ptr) + sz);
                    GlobalUnlock(h);
                }
            }
        }

        // Try BITMAP format as last resort
        if (result.empty()) {
            HANDLE h = GetClipboardData(CF_BITMAP);
            if (h) {
                HBITMAP hbmp = static_cast<HBITMAP>(h);
                BITMAP bmp;
                if (GetObjectW(hbmp, sizeof(BITMAP), &bmp)) {
                    HDC hdc = GetDC(nullptr);
                    HDC memDC = CreateCompatibleDC(hdc);
                    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, hbmp));

                    BITMAPINFO bi = {};
                    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bi.bmiHeader.biWidth = bmp.bmWidth;
                    bi.bmiHeader.biHeight = bmp.bmHeight;
                    bi.bmiHeader.biPlanes = 1;
                    bi.bmiHeader.biBitCount = 32;
                    bi.bmiHeader.biCompression = BI_RGB;

                    int stride = ((bmp.bmWidth * 32 + 31) / 32) * 4;
                    size_t data_size = stride * bmp.bmHeight;
                    result.resize(sizeof(BITMAPINFOHEADER) + data_size);
                    memcpy(result.data(), &bi.bmiHeader, sizeof(BITMAPINFOHEADER));

                    GetDIBits(memDC, hbmp, 0, bmp.bmHeight,
                        result.data() + sizeof(BITMAPINFOHEADER),
                        &bi, DIB_RGB_COLORS);

                    SelectObject(memDC, oldBmp);
                    DeleteDC(memDC);
                    ReleaseDC(nullptr, hdc);
                }
            }
        }

        CloseClipboard();

        // Convert if needed
        if (!result.empty() && format != "dib" && format != "dibv5") {
            result = util::convert_image_format(result, "bmp", format);
        }

        return result;
    }

    bool set_image(const std::vector<uint8_t>& data, const std::string& format) override {
        if (data.empty()) return false;
        if (!open_and_clear()) return false;
        change_count_.fetch_add(1);

        std::vector<uint8_t> to_set = data;

        // If not already in a Windows clipboard format, convert to DIB
        UINT cf = 0;
        if (format == "png") {
            cf = CF_PNG;
        } else if (format == "tiff") {
            cf = CF_TIFF;
        } else if (format == "dib") {
            cf = CF_DIB;
        } else if (format == "dibv5") {
            cf = CF_DIBV5;
        } else if (format == "bmp") {
            cf = CF_DIB;
            // Convert BMP to DIB if needed
            to_set = util::convert_image_format(data, "bmp", "dib");
        } else {
            cf = CF_PNG;
        }

        if (cf == 0) {
            CloseClipboard();
            return false;
        }

        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, to_set.size());
        if (!h) { CloseClipboard(); return false; }
        void* ptr = GlobalLock(h);
        memcpy(ptr, to_set.data(), to_set.size());
        GlobalUnlock(h);
        SetClipboardData(cf, h);

        // Also set DIB if PNG was set (for compatibility)
        if (cf == CF_PNG) {
            auto dib_data = util::convert_image_format(data, "png", "bmp");
            if (!dib_data.empty()) {
                HGLOBAL hDib = GlobalAlloc(GMEM_MOVEABLE, dib_data.size());
                if (hDib) {
                    void* pDib = GlobalLock(hDib);
                    memcpy(pDib, dib_data.data(), dib_data.size());
                    GlobalUnlock(hDib);
                    SetClipboardData(CF_DIB, hDib);
                }
            }
        }

        CloseClipboard();
        spdlog::debug("WindowsClipboard: set_image format={} size={}", format, data.size());
        return true;
    }

    // ====== HTML Operations ======
    std::string get_html() override {
        if (!OpenClipboard(nullptr)) return "";
        std::string result;

        HANDLE h = GetClipboardData(CF_HTML_FORMAT);
        if (h) {
            size_t sz = GlobalSize(h);
            char* ptr = static_cast<char*>(GlobalLock(h));
            if (ptr && sz > 0) {
                std::string raw(ptr, sz);
                GlobalUnlock(h);
                result = util::parse_cf_html(raw);
            }
        }
        CloseClipboard();
        return result;
    }

    bool set_html(const std::string& html) override {
        if (!open_and_clear()) return false;
        change_count_.fetch_add(1);

        std::string cf_html = util::generate_cf_html(html);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, cf_html.size() + 1);
        if (!h) { CloseClipboard(); return false; }
        char* ptr = static_cast<char*>(GlobalLock(h));
        memcpy(ptr, cf_html.data(), cf_html.size());
        ptr[cf_html.size()] = 0;
        GlobalUnlock(h);
        SetClipboardData(CF_HTML_FORMAT, h);

        // Also set plain text version for apps that don't support HTML
        // Strip HTML tags (simple approach)
        std::string plain_text;
        bool in_tag = false;
        for (char c : html) {
            if (c == '<') in_tag = true;
            else if (c == '>') in_tag = false;
            else if (!in_tag) plain_text += c;
        }
        if (!plain_text.empty()) {
            std::wstring wplain = utf8_to_wchar(plain_text);
            HGLOBAL ht = GlobalAlloc(GMEM_MOVEABLE, (wplain.size() + 1) * sizeof(wchar_t));
            if (ht) {
                wchar_t* wptr = static_cast<wchar_t*>(GlobalLock(ht));
                wcscpy_s(wptr, wplain.size() + 1, wplain.c_str());
                GlobalUnlock(ht);
                SetClipboardData(CF_UNICODETEXT, ht);
            }
        }

        CloseClipboard();
        spdlog::debug("WindowsClipboard: set_html {} chars, CF_HTML {} bytes", html.size(), cf_html.size());
        return true;
    }

    // ====== RTF Operations ======
    std::string get_rtf() override {
        if (!OpenClipboard(nullptr)) return "";
        std::string result;

        HANDLE h = GetClipboardData(CF_RTF_FORMAT);
        if (h) {
            char* ptr = static_cast<char*>(GlobalLock(h));
            if (ptr) {
                result = ptr;
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
        return result;
    }

    bool set_rtf(const std::string& rtf) override {
        if (!open_and_clear()) return false;
        change_count_.fetch_add(1);

        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, rtf.size() + 1);
        if (!h) { CloseClipboard(); return false; }
        char* ptr = static_cast<char*>(GlobalLock(h));
        memcpy(ptr, rtf.data(), rtf.size());
        ptr[rtf.size()] = 0;
        GlobalUnlock(h);
        SetClipboardData(CF_RTF_FORMAT, h);
        CloseClipboard();
        spdlog::debug("WindowsClipboard: set_rtf {} bytes", rtf.size());
        return true;
    }

    // ====== URI List Operations ======
    std::vector<std::string> get_uri_list() override {
        std::vector<std::string> uris;
        if (!OpenClipboard(nullptr)) return uris;

        HANDLE h = GetClipboardData(CF_URILIST);
        if (h) {
            char* ptr = static_cast<char*>(GlobalLock(h));
            if (ptr) {
                uris = util::decode_uri_list(ptr);
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
        return uris;
    }

    bool set_uri_list(const std::vector<std::string>& uris) override {
        if (!open_and_clear()) return false;
        change_count_.fetch_add(1);

        std::string uri_str = util::encode_uri_list(uris);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, uri_str.size() + 1);
        if (!h) { CloseClipboard(); return false; }
        char* ptr = static_cast<char*>(GlobalLock(h));
        memcpy(ptr, uri_str.data(), uri_str.size());
        ptr[uri_str.size()] = 0;
        GlobalUnlock(h);
        SetClipboardData(CF_URILIST, h);
        CloseClipboard();
        return true;
    }

    // ====== Clear and Ownership ======
    bool clear() override {
        if (!OpenClipboard(nullptr)) return false;
        EmptyClipboard();
        CloseClipboard();
        change_count_.fetch_add(1);
        owns_clipboard_ = false;
        return true;
    }

    bool owns_clipboard() override {
        return owns_clipboard_.load();
    }

    // ====== Change Notification ======
    void set_on_change(OnChange cb) override {
        onChange_ = std::move(cb);
    }

    // ====== Delayed Rendering ======
    bool enable_delayed_rendering(std::shared_ptr<DelayedRenderer> renderer) override {
        if (!renderer) return false;
        delayed_renderer_ = renderer;

        if (!OpenClipboard(helper_window_)) return false;
        EmptyClipboard();

        // Enumerate formats the renderer supports and set NULL data
        static const ContentFormat formats[] = {
            ContentFormat::TEXT, ContentFormat::HTML, ContentFormat::RTF,
            ContentFormat::IMAGE_PNG, ContentFormat::IMAGE_DIB, ContentFormat::IMAGE_DIBV5,
            ContentFormat::FILE_LIST, ContentFormat::URI_LIST
        };

        for (auto fmt : formats) {
            if (!renderer->can_render(fmt)) continue;

            UINT cf = 0;
            switch (fmt) {
                case ContentFormat::TEXT: cf = CF_UNICODETEXT; break;
                case ContentFormat::HTML: cf = CF_HTML_FORMAT; break;
                case ContentFormat::RTF: cf = CF_RTF_FORMAT; break;
                case ContentFormat::IMAGE_PNG: cf = CF_PNG; break;
                case ContentFormat::IMAGE_DIB: cf = CF_DIB; break;
                case ContentFormat::IMAGE_DIBV5: cf = CF_DIBV5; break;
                case ContentFormat::FILE_LIST: cf = CF_HDROP; break;
                default: continue;
            }

            SetClipboardData(cf, nullptr); // NULL = delayed rendering
        }

        CloseClipboard();
        owns_clipboard_ = true;
        spdlog::info("WindowsClipboard: delayed rendering enabled");
        return true;
    }

    void disable_delayed_rendering() override {
        delayed_renderer_.reset();
        spdlog::info("WindowsClipboard: delayed rendering disabled");
    }

    // ====== Available Formats ======
    ContentFormat available_formats() override {
        ContentFormat result = static_cast<ContentFormat>(0);

        if (!OpenClipboard(nullptr)) return result;

        if (IsClipboardFormatAvailable(CF_UNICODETEXT) || IsClipboardFormatAvailable(CF_TEXT))
            result = result | ContentFormat::TEXT;
        if (IsClipboardFormatAvailable(CF_HTML_FORMAT))
            result = result | ContentFormat::HTML;
        if (IsClipboardFormatAvailable(CF_RTF_FORMAT))
            result = result | ContentFormat::RTF;
        if (IsClipboardFormatAvailable(CF_PNG))
            result = result | ContentFormat::IMAGE_PNG;
        if (IsClipboardFormatAvailable(CF_TIFF))
            result = result | ContentFormat::IMAGE_TIFF;
        if (IsClipboardFormatAvailable(CF_DIB))
            result = result | ContentFormat::IMAGE_DIB;
        if (IsClipboardFormatAvailable(CF_DIBV5))
            result = result | ContentFormat::IMAGE_DIBV5;
        if (IsClipboardFormatAvailable(CF_HDROP))
            result = result | ContentFormat::FILE_LIST;
        if (IsClipboardFormatAvailable(CF_URILIST))
            result = result | ContentFormat::URI_LIST;

        CloseClipboard();
        return result;
    }

    ClipboardContentDescriptor get_content_descriptor() override {
        ClipboardContentDescriptor desc;
        desc.available_formats = available_formats();
        desc.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        desc.sequence_number = static_cast<uint32_t>(change_count_.load());
        desc.source_application = "cppdesk (Windows)";

        if (desc.has_format(ContentFormat::TEXT)) desc.text = get_text();
        if (desc.has_format(ContentFormat::HTML)) desc.html = get_html();
        if (desc.has_format(ContentFormat::RTF)) desc.rtf = get_rtf();
        if (desc.has_format(ContentFormat::IMAGE_PNG)) desc.image_data = get_image("png");
        if (desc.has_format(ContentFormat::FILE_LIST)) desc.file_list = get_file_list();
        if (desc.has_format(ContentFormat::URI_LIST)) desc.uri_list = get_uri_list();

        if (!desc.text.empty()) desc.text_hash = ContentDeduplicator::compute_text_hash(desc.text);
        if (!desc.image_data.empty()) desc.image_hash = ContentDeduplicator::compute_data_hash(desc.image_data);
        if (!desc.file_list.empty()) desc.file_hash = ContentDeduplicator::compute_file_hash(desc.file_list);

        return desc;
    }

    bool set_content(const ClipboardContentDescriptor& content) override {
        if (!open_and_clear()) return false;
        change_count_.fetch_add(1);

        bool ok = true;

        if (content.has_format(ContentFormat::TEXT) && !content.text.empty()) {
            if (!set_text_internal(content.text)) ok = false;
        }
        if (content.has_format(ContentFormat::HTML) && !content.html.empty()) {
            if (!set_html_internal(content.html)) ok = false;
        }
        if (content.has_format(ContentFormat::RTF) && !content.rtf.empty()) {
            if (!set_rtf_internal(content.rtf)) ok = false;
        }
        if (content.has_format(ContentFormat::IMAGE_PNG) && !content.image_data.empty()) {
            if (!set_image_internal(content.image_data, content.image_format.empty() ? "png" : content.image_format)) ok = false;
        }
        if (content.has_format(ContentFormat::FILE_LIST) && !content.file_list.empty()) {
            if (!set_file_list_internal(content.file_list)) ok = false;
        }

        CloseClipboard();
        return ok;
    }

    int64_t get_change_count() override {
        return static_cast<int64_t>(change_count_.load());
    }

private:
    // Internal methods that assume clipboard is open
    bool set_text_internal(const std::string& text) {
        std::wstring wstr = utf8_to_wchar(text);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (wstr.size() + 1) * sizeof(wchar_t));
        if (!h) return false;
        wchar_t* wptr = static_cast<wchar_t*>(GlobalLock(h));
        wcscpy_s(wptr, wstr.size() + 1, wstr.c_str());
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
        return true;
    }

    bool set_html_internal(const std::string& html) {
        std::string cf_html = util::generate_cf_html(html);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, cf_html.size() + 1);
        if (!h) return false;
        char* ptr = static_cast<char*>(GlobalLock(h));
        memcpy(ptr, cf_html.data(), cf_html.size());
        ptr[cf_html.size()] = 0;
        GlobalUnlock(h);
        SetClipboardData(CF_HTML_FORMAT, h);
        return true;
    }

    bool set_rtf_internal(const std::string& rtf) {
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, rtf.size() + 1);
        if (!h) return false;
        char* ptr = static_cast<char*>(GlobalLock(h));
        memcpy(ptr, rtf.data(), rtf.size());
        ptr[rtf.size()] = 0;
        GlobalUnlock(h);
        SetClipboardData(CF_RTF_FORMAT, h);
        return true;
    }

    bool set_image_internal(const std::vector<uint8_t>& data, const std::string& format) {
        UINT cf = CF_PNG;
        if (format == "tiff") cf = CF_TIFF;
        else if (format == "dib") cf = CF_DIB;
        else if (format == "dibv5") cf = CF_DIBV5;

        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, data.size());
        if (!h) return false;
        void* ptr = GlobalLock(h);
        memcpy(ptr, data.data(), data.size());
        GlobalUnlock(h);
        SetClipboardData(cf, h);
        return true;
    }

    bool set_file_list_internal(const std::vector<std::string>& paths) {
        size_t total = 0;
        for (auto& p : paths) total += p.size() + 1;
        total += 1;
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
            sizeof(DROPFILES) + total * sizeof(wchar_t));
        if (!h) return false;
        auto* pdf = static_cast<DROPFILES*>(GlobalLock(h));
        pdf->pFiles = sizeof(DROPFILES);
        pdf->fWide = TRUE;
        wchar_t* wptr = reinterpret_cast<wchar_t*>(pdf + 1);
        for (const auto& p : paths) {
            std::wstring wpath = utf8_to_wchar(p);
            wcscpy_s(wptr, wpath.size() + 1, wpath.c_str());
            wptr += wpath.size() + 1;
        }
        *wptr = 0;
        GlobalUnlock(h);
        SetClipboardData(CF_HDROP, h);
        return true;
    }
};

// ====== ClipboardViewer Implementation ======
ClipboardViewer::ClipboardViewer() : impl_(std::make_unique<Impl>()) {}
ClipboardViewer::~ClipboardViewer() { shutdown(); }

void ClipboardViewer::set_change_callback(ChangeCallback cb) {
    impl_->callback = std::move(cb);
}

bool ClipboardViewer::initialize(void* parent_window_handle) {
    return impl_->create_window(static_cast<HWND>(parent_window_handle));
}

void ClipboardViewer::shutdown() {
    impl_->destroy_window();
}

bool ClipboardViewer::is_active() const {
    return impl_->active.load();
}

#elif defined(__APPLE__)

// ====== macOS NSPasteboard Implementation ======
class MacClipboard : public PlatformClipboard {
private:
    OnChange onChange_;
    std::shared_ptr<DelayedRenderer> delayed_renderer_;
    std::atomic<int64_t> last_change_count_{0};
    std::atomic<uint64_t> sequence_number_{0};

    int64_t get_nspasteboard_change_count() const {
        @autoreleasepool {
            return [[NSPasteboard generalPasteboard] changeCount];
        }
    }

    // Helper to convert NSString to std::string
    static std::string nsstring_to_std(NSString* str) {
        if (!str) return "";
        const char* utf8 = [str UTF8String];
        return utf8 ? std::string(utf8) : "";
    }

    // Helper to convert std::string to NSString
    static NSString* std_to_nsstring(const std::string& str) {
        return [NSString stringWithUTF8String:str.c_str()];
    }

public:
    MacClipboard() {
        last_change_count_ = get_nspasteboard_change_count();
        spdlog::info("MacClipboard: initialized, change count = {}", last_change_count_.load());
    }

    ~MacClipboard() override = default;

    // ====== Text Operations ======
    std::string get_text() override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            // Try public.utf8-plain-text first (modern), fall back to NSStringPboardType
            NSArray* types = @[NSPasteboardTypeString, @"public.utf8-plain-text"];
            NSString* best = [pb availableTypeFromArray:types];
            if (!best) return "";

            NSString* str = [pb stringForType:best];
            if (!str) {
                // Try reading as data and converting
                NSData* data = [pb dataForType:best];
                if (data) {
                    str = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
                    [str autorelease];
                }
            }
            spdlog::debug("MacClipboard: get_text {} chars", (unsigned long)[str length]);
            return nsstring_to_std(str);
        }
    }

    bool set_text(const std::string& text) override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];

            NSString* nsStr = std_to_nsstring(text);
            BOOL ok = [pb setString:nsStr forType:NSPasteboardTypeString];
            if (ok) {
                sequence_number_.fetch_add(1);
                last_change_count_ = [pb changeCount];
                spdlog::debug("MacClipboard: set_text {} chars OK", text.size());
            } else {
                spdlog::error("MacClipboard: set_text failed");
            }
            return ok == YES;
        }
    }

    bool has_text() override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            NSArray* types = @[NSPasteboardTypeString, @"public.utf8-plain-text", @"NSStringPboardType"];
            return [pb availableTypeFromArray:types] != nil;
        }
    }

    // ====== File List Operations (NSFilenamesPboardType / NSPasteboardTypeFileURL) ======
    std::vector<std::string> get_file_list() override {
        @autoreleasepool {
            std::vector<std::string> files;
            NSPasteboard* pb = [NSPasteboard generalPasteboard];

            // Modern approach: NSPasteboardTypeFileURL (since 10.13)
            NSArray* classes = @[[NSURL class]];
            NSDictionary* options = @{
                NSPasteboardURLReadingFileURLsOnlyKey: @YES
            };
            NSArray* urls = [pb readObjectsForClasses:classes options:options];
            if (urls && [urls count] > 0) {
                for (NSURL* url in urls) {
                    if ([url isFileURL]) {
                        files.push_back(nsstring_to_std([url path]));
                    }
                }
            }

            // Fallback: NSFilenamesPboardType (deprecated but widely used)
            if (files.empty()) {
                NSArray* filenames = [pb propertyListForType:NSFilenamesPboardType];
                if (filenames && [filenames isKindOfClass:[NSArray class]]) {
                    for (NSString* fname in filenames) {
                        files.push_back(nsstring_to_std(fname));
                    }
                }
            }

            spdlog::debug("MacClipboard: get_file_list found {} files", files.size());
            return files;
        }
    }

    bool set_file_list(const std::vector<std::string>& paths) override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];

            // Build array of file URLs
            NSMutableArray* urlArray = [NSMutableArray arrayWithCapacity:paths.size()];
            for (const auto& p : paths) {
                NSString* pathStr = std_to_nsstring(p);
                NSURL* url = [NSURL fileURLWithPath:pathStr];
                if (url) [urlArray addObject:url];
            }

            // Modern approach: write file URLs
            if ([urlArray count] > 0) {
                BOOL ok = [pb writeObjects:urlArray];
                if (!ok) {
                    // Fallback: use NSFilenamesPboardType
                    NSMutableArray* strArray = [NSMutableArray arrayWithCapacity:paths.size()];
                    for (const auto& p : paths) {
                        [strArray addObject:std_to_nsstring(p)];
                    }
                    ok = [pb setPropertyList:strArray forType:NSFilenamesPboardType];
                }
                if (ok) {
                    sequence_number_.fetch_add(1);
                    last_change_count_ = [pb changeCount];
                }
                return ok == YES;
            }
            return false;
        }
    }

    bool has_file_list() override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            // Check modern format
            if ([pb availableTypeFromArray:@[NSPasteboardTypeFileURL, @"public.file-url"]] != nil)
                return true;
            // Check legacy format
            if ([pb availableTypeFromArray:@[NSFilenamesPboardType]] != nil)
                return true;
            return false;
        }
    }

    // ====== Image Operations ======
    std::vector<uint8_t> get_image(const std::string& format) override {
        @autoreleasepool {
            std::vector<uint8_t> result;
            NSPasteboard* pb = [NSPasteboard generalPasteboard];

            NSImage* image = nil;

            // Try reading as NSImage directly
            NSArray* classes = @[[NSImage class]];
            NSDictionary* options = @{};
            NSArray* images = [pb readObjectsForClasses:classes options:options];
            if (images && [images count] > 0) {
                image = images[0];
            }

            // Try reading PNG or TIFF data and creating NSImage
            if (!image) {
                NSArray* imgTypes = @[NSPasteboardTypePNG, NSPasteboardTypeTIFF];
                NSString* avType = [pb availableTypeFromArray:imgTypes];
                if (avType) {
                    NSData* imgData = [pb dataForType:avType];
                    if (imgData) {
                        image = [[NSImage alloc] initWithData:imgData];
                        [image autorelease];
                    }
                }
            }

            if (image) {
                NSBitmapImageFileType fileType;
                if (format == "tiff" || format == "TIFF") {
                    fileType = NSTIFFFileType;
                } else if (format == "bmp" || format == "BMP") {
                    fileType = NSBMPFileType;
                } else if (format == "jpeg" || format == "jpg" || format == "JPEG") {
                    fileType = NSJPEGFileType;
                } else if (format == "gif" || format == "GIF") {
                    fileType = NSGIFFileType;
                } else {
                    fileType = NSPNGFileType;
                }

                // Convert to requested format
                NSData* tiffData = [image TIFFRepresentation];
                if (tiffData) {
                    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithData:tiffData];
                    if (rep) {
                        NSDictionary* props = @{};
                        NSData* outData = [rep representationUsingType:fileType properties:props];
                        if (outData) {
                            result.assign((const uint8_t*)[outData bytes],
                                        (const uint8_t*)[outData bytes] + [outData length]);
                        }
                        [rep release];
                    }
                }
                spdlog::debug("MacClipboard: get_image format={}, size={}", format, result.size());
            }

            return result;
        }
    }

    bool set_image(const std::vector<uint8_t>& data, const std::string& format) override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];

            NSData* nsData = [NSData dataWithBytes:data.data() length:data.size()];
            NSImage* image = [[NSImage alloc] initWithData:nsData];
            if (!image) {
                spdlog::error("MacClipboard: set_image failed to create NSImage from data");
                return false;
            }

            // Write image to pasteboard
            BOOL ok = [pb writeObjects:@[image]];
            [image release];

            // Also write in the requested format for better compatibility
            if (format == "png" || format.empty()) {
                [pb setData:nsData forType:NSPasteboardTypePNG];
            } else if (format == "tiff") {
                [pb setData:nsData forType:NSPasteboardTypeTIFF];
            }

            if (ok) {
                sequence_number_.fetch_add(1);
                last_change_count_ = [pb changeCount];
                spdlog::debug("MacClipboard: set_image format={}, size={}", format, data.size());
            }
            return ok == YES;
        }
    }

    // ====== HTML Operations ======
    std::string get_html() override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            NSArray* types = @[NSPasteboardTypeHTML, @"NSHTMLPboardType"];
            NSString* avType = [pb availableTypeFromArray:types];
            if (!avType) return "";

            NSData* data = [pb dataForType:avType];
            if (!data) return "";

            NSString* str = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
            [str autorelease];
            return nsstring_to_std(str);
        }
    }

    bool set_html(const std::string& html) override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];

            NSString* nsHtml = std_to_nsstring(html);
            NSData* data = [nsHtml dataUsingEncoding:NSUTF8StringEncoding];
            BOOL ok = [pb setData:data forType:NSPasteboardTypeHTML];

            // Also set plain text
            NSString* plainText = [NSString stringWithUTF8String:html.c_str()];
            // Strip HTML tags for plain text version
            NSRange range = NSMakeRange(0, [plainText length]);
            NSMutableString* stripped = [NSMutableString stringWithCapacity:[plainText length]];
            BOOL inTag = NO;
            for (NSUInteger i = 0; i < [plainText length]; i++) {
                unichar c = [plainText characterAtIndex:i];
                if (c == '<') inTag = YES;
                else if (c == '>') inTag = NO;
                else if (!inTag) [stripped appendFormat:@"%C", c];
            }
            if ([stripped length] > 0) {
                [pb setString:stripped forType:NSPasteboardTypeString];
            }

            if (ok) {
                sequence_number_.fetch_add(1);
                last_change_count_ = [pb changeCount];
                spdlog::debug("MacClipboard: set_html {} chars", html.size());
            }
            return ok == YES;
        }
    }

    // ====== RTF Operations ======
    std::string get_rtf() override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            NSArray* types = @[NSPasteboardTypeRTF, @"NSRTFPboardType"];
            NSString* avType = [pb availableTypeFromArray:types];
            if (!avType) return "";

            NSData* data = [pb dataForType:avType];
            if (!data) return "";

            // RTF data is NSData (not string)
            std::string result((const char*)[data bytes], [data length]);
            return result;
        }
    }

    bool set_rtf(const std::string& rtf) override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];

            NSData* data = [NSData dataWithBytes:rtf.data() length:rtf.size()];
            BOOL ok = [pb setData:data forType:NSPasteboardTypeRTF];

            if (ok) {
                sequence_number_.fetch_add(1);
                last_change_count_ = [pb changeCount];
                spdlog::debug("MacClipboard: set_rtf {} bytes", rtf.size());
            }
            return ok == YES;
        }
    }

    // ====== URI List Operations ======
    std::vector<std::string> get_uri_list() override {
        @autoreleasepool {
            std::vector<std::string> uris;
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            NSString* t = [pb availableTypeFromArray:@[@"public.url", @"public.utf8-plain-text"]];

            if (t) {
                if ([t isEqualToString:@"public.url"]) {
                    NSURL* url = [NSURL URLFromPasteboard:pb];
                    if (url) uris.push_back(nsstring_to_std([url absoluteString]));
                } else {
                    NSString* str = [pb stringForType:t];
                    if (str) uris = util::decode_uri_list(nsstring_to_std(str));
                }
            }
            return uris;
        }
    }

    bool set_uri_list(const std::vector<std::string>& uris) override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            if (uris.empty()) return true;

            // Set first URI as URL
            NSURL* url = [NSURL URLWithString:std_to_nsstring(uris[0])];
            if (url) {
                [url writeToPasteboard:pb];
            }

            // Also set as plain text
            std::string all_uris = util::encode_uri_list(uris);
            NSString* nsStr = std_to_nsstring(all_uris);
            [pb setString:nsStr forType:NSPasteboardTypeString];

            sequence_number_.fetch_add(1);
            last_change_count_ = [pb changeCount];
            return true;
        }
    }

    // ====== Clear and Ownership ======
    bool clear() override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            sequence_number_.fetch_add(1);
            last_change_count_ = [pb changeCount];
            return true;
        }
    }

    bool owns_clipboard() override {
        // macOS doesn't really have clipboard ownership concept via NSPasteboard
        // We track based on our last write
        return true; // Always able to write
    }

    // ====== Change Notification ======
    void set_on_change(OnChange cb) override {
        onChange_ = std::move(cb);
    }

    // ====== Delayed Rendering ======
    bool enable_delayed_rendering(std::shared_ptr<DelayedRenderer> renderer) override {
        delayed_renderer_ = renderer;
        spdlog::info("MacClipboard: delayed rendering enabled");
        return true;
    }

    void disable_delayed_rendering() override {
        delayed_renderer_.reset();
        spdlog::info("MacClipboard: delayed rendering disabled");
    }

    // ====== Format Detection ======
    ContentFormat available_formats() override {
        ContentFormat result = static_cast<ContentFormat>(0);
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];

            if ([pb availableTypeFromArray:@[NSPasteboardTypeString, @"public.utf8-plain-text"]] != nil)
                result = result | ContentFormat::TEXT;
            if ([pb availableTypeFromArray:@[NSPasteboardTypeHTML, @"NSHTMLPboardType"]] != nil)
                result = result | ContentFormat::HTML;
            if ([pb availableTypeFromArray:@[NSPasteboardTypeRTF, @"NSRTFPboardType"]] != nil)
                result = result | ContentFormat::RTF;
            if ([pb availableTypeFromArray:@[NSPasteboardTypePNG]] != nil)
                result = result | ContentFormat::IMAGE_PNG;
            if ([pb availableTypeFromArray:@[NSPasteboardTypeTIFF]] != nil)
                result = result | ContentFormat::IMAGE_TIFF;
            if ([pb availableTypeFromArray:@[NSPasteboardTypeFileURL, NSFilenamesPboardType, @"public.file-url"]] != nil)
                result = result | ContentFormat::FILE_LIST;
            if ([pb availableTypeFromArray:@[@"public.url"]] != nil)
                result = result | ContentFormat::URI_LIST;
        }
        return result;
    }

    ClipboardContentDescriptor get_content_descriptor() override {
        @autoreleasepool {
            ClipboardContentDescriptor desc;
            desc.available_formats = available_formats();
            desc.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            desc.sequence_number = static_cast<uint32_t>(sequence_number_.load());
            desc.source_application = "cppdesk (macOS)";

            if (desc.has_format(ContentFormat::TEXT)) desc.text = get_text();
            if (desc.has_format(ContentFormat::HTML)) desc.html = get_html();
            if (desc.has_format(ContentFormat::RTF)) desc.rtf = get_rtf();
            if (desc.has_format(ContentFormat::IMAGE_PNG)) desc.image_data = get_image("png");
            if (desc.has_format(ContentFormat::FILE_LIST)) desc.file_list = get_file_list();
            if (desc.has_format(ContentFormat::URI_LIST)) desc.uri_list = get_uri_list();

            if (!desc.text.empty()) desc.text_hash = ContentDeduplicator::compute_text_hash(desc.text);
            if (!desc.image_data.empty()) desc.image_hash = ContentDeduplicator::compute_data_hash(desc.image_data);
            if (!desc.file_list.empty()) desc.file_hash = ContentDeduplicator::compute_file_hash(desc.file_list);

            return desc;
        }
    }

    bool set_content(const ClipboardContentDescriptor& content) override {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            bool ok = true;

            NSMutableArray* writeObjects = [NSMutableArray array];

            if (content.has_format(ContentFormat::TEXT) && !content.text.empty()) {
                [pb setString:std_to_nsstring(content.text) forType:NSPasteboardTypeString];
            }
            if (content.has_format(ContentFormat::HTML) && !content.html.empty()) {
                [pb setData:[std_to_nsstring(content.html) dataUsingEncoding:NSUTF8StringEncoding]
                    forType:NSPasteboardTypeHTML];
            }
            if (content.has_format(ContentFormat::RTF) && !content.rtf.empty()) {
                [pb setData:[NSData dataWithBytes:content.rtf.data() length:content.rtf.size()]
                    forType:NSPasteboardTypeRTF];
            }
            if (content.has_format(ContentFormat::IMAGE_PNG) && !content.image_data.empty()) {
                NSData* imgData = [NSData dataWithBytes:content.image_data.data()
                    length:content.image_data.size()];
                [pb setData:imgData forType:NSPasteboardTypePNG];

                NSImage* image = [[NSImage alloc] initWithData:imgData];
                if (image) {
                    [writeObjects addObject:image];
                    [image release];
                }
            }
            if (content.has_format(ContentFormat::FILE_LIST) && !content.file_list.empty()) {
                NSMutableArray* urls = [NSMutableArray array];
                for (const auto& f : content.file_list) {
                    NSURL* url = [NSURL fileURLWithPath:std_to_nsstring(f)];
                    if (url) [urls addObject:url];
                }
                if ([urls count] > 0) {
                    [pb writeObjects:urls];
                }
            }

            if ([writeObjects count] > 0) {
                [pb writeObjects:writeObjects];
            }

            sequence_number_.fetch_add(1);
            last_change_count_ = [pb changeCount];
            return ok;
        }
    }

    int64_t get_change_count() override {
        int64_t cc = get_nspasteboard_change_count();
        if (cc != last_change_count_.load()) {
            int64_t prev = last_change_count_.exchange(cc);
            if (prev != cc && onChange_) {
                // Schedule callback (in real impl would be on main thread)
                onChange_();
            }
        }
        return cc;
    }
};

#elif defined(__linux__)

// ====== X11 Selection Atoms ======
static Atom XA_CLIPBOARD = 0;
static Atom XA_PRIMARY = 0;
static Atom XA_TARGETS = 0;
static Atom XA_UTF8_STRING = 0;
static Atom XA_TEXT = 0;
static Atom XA_STRING = 0;
static Atom XA_ATOM = 0;
static Atom XA_TIMESTAMP = 0;
static Atom XA_MULTIPLE = 0;
static Atom XA_INCR = 0;
static Atom XA_TEXT_URI_LIST = 0;
static Atom XA_IMAGE_PNG = 0;
static Atom XA_IMAGE_BMP = 0;
static Atom XA_IMAGE_TIFF = 0;
static Atom XA_TEXT_HTML = 0;
static Atom XA_TEXT_RTF = 0;
static Atom XA_FILE_NAME = 0;
static Atom XA_NETSCAPE_URL = 0;

static void init_x11_atoms(Display* display) {
    XA_CLIPBOARD = XInternAtom(display, "CLIPBOARD", False);
    XA_PRIMARY = XInternAtom(display, "PRIMARY", False);
    XA_TARGETS = XInternAtom(display, "TARGETS", False);
    XA_UTF8_STRING = XInternAtom(display, "UTF8_STRING", False);
    XA_TEXT = XInternAtom(display, "TEXT", False);
    XA_STRING = XInternAtom(display, "STRING", False);
    XA_ATOM = XInternAtom(display, "ATOM", False);
    XA_TIMESTAMP = XInternAtom(display, "TIMESTAMP", False);
    XA_MULTIPLE = XInternAtom(display, "MULTIPLE", False);
    XA_INCR = XInternAtom(display, "INCR", False);
    XA_TEXT_URI_LIST = XInternAtom(display, "text/uri-list", False);
    XA_IMAGE_PNG = XInternAtom(display, "image/png", False);
    XA_IMAGE_BMP = XInternAtom(display, "image/bmp", False);
    XA_IMAGE_TIFF = XInternAtom(display, "image/tiff", False);
    XA_TEXT_HTML = XInternAtom(display, "text/html", False);
    XA_TEXT_RTF = XInternAtom(display, "text/rtf", False);
    XA_FILE_NAME = XInternAtom(display, "FILE_NAME", False);
    XA_NETSCAPE_URL = XInternAtom(display, "_NETSCAPE_URL", False);
}

// ====== Full X11 Clipboard Implementation ======
class X11Clipboard : public PlatformClipboard {
    Display* display_ = nullptr;
    Window window_ = 0;
    int screen_ = 0;
    OnChange onChange_;
    std::shared_ptr<DelayedRenderer> delayed_renderer_;
    std::atomic<uint64_t> change_count_{0};

    // Stored data for serving to other apps
    std::string stored_text_;
    std::string stored_html_;
    std::string stored_rtf_;
    std::vector<uint8_t> stored_image_png_;
    std::vector<uint8_t> stored_image_tiff_;
    std::string stored_uri_list_;
    std::vector<std::string> stored_file_list_;
    mutable std::mutex store_mutex_;

    // Map selection atom to clipboard type (for dual CLIPBOARD + PRIMARY support)
    Atom active_selection_ = 0;

    // Timestamp for selection ownership
    Time selection_timestamp_ = 0;

    bool init_display() {
        display_ = XOpenDisplay(nullptr);
        if (!display_) {
            spdlog::error("X11Clipboard: Failed to open X display");
            return false;
        }
        screen_ = DefaultScreen(display_);
        window_ = XCreateSimpleWindow(display_, RootWindow(display_, screen_),
            0, 0, 1, 1, 0,
            BlackPixel(display_, screen_),
            WhitePixel(display_, screen_));

        init_x11_atoms(display_);

        // Listen for SelectionRequest events on our window
        XSelectInput(display_, window_, PropertyChangeMask);

        spdlog::info("X11Clipboard: display opened, window=0x{:x}", static_cast<unsigned long>(window_));
        return true;
    }

    // Read a property using INCR (incremental transfer) if needed
    std::vector<uint8_t> read_property_incr(Window win, Atom property, Atom type) {
        std::vector<uint8_t> result;
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char* data = nullptr;

        XGetWindowProperty(display_, win, property, 0, 0, False,
            AnyPropertyType, &actual_type, &actual_format, &nitems, &bytes_after, &data);
        if (data) XFree(data);

        if (bytes_after == 0) {
            // Small enough to read in one shot
            XGetWindowProperty(display_, win, property, 0, bytes_after + (nitems * (actual_format / 8)),
                False, type, &actual_type, &actual_format, &nitems, &bytes_after, &data);
            if (data) {
                size_t sz = nitems * (actual_format / 8);
                result.assign(data, data + sz);
                XFree(data);
            }
        } else {
            // INCR transfer
            spdlog::debug("X11Clipboard: INCR transfer initiated, remaining={}", bytes_after);
            // Delete the property so we get PropertyNotify
            XDeleteProperty(display_, win, property);

            // Wait for PropertyNotify events until property has 0 length
            bool done = false;
            while (!done) {
                XEvent ev;
                XNextEvent(display_, &ev);
                if (ev.type == PropertyNotify &&
                    ev.xproperty.state == PropertyNewValue &&
                    ev.xproperty.atom == property) {

                    // Read the chunk
                    XGetWindowProperty(display_, win, property, 0, 0, False,
                        AnyPropertyType, &actual_type, &actual_format, &nitems, &bytes_after, &data);
                    if (data) XFree(data);

                    if (bytes_after == 0) {
                        // Last chunk (or all chunks done)
                        XGetWindowProperty(display_, win, property, 0,
                            bytes_after + (nitems * (actual_format / 8)),
                            False, type, &actual_type, &actual_format, &nitems, &bytes_after, &data);
                        if (data) {
                            if (nitems == 0) {
                                XFree(data);
                                done = true;
                            } else {
                                size_t sz = nitems * (actual_format / 8);
                                result.insert(result.end(), data, data + sz);
                                XFree(data);
                                XDeleteProperty(display_, win, property);
                            }
                        } else {
                            done = true;
                        }
                    } else {
                        // More chunks coming
                        XGetWindowProperty(display_, win, property, 0,
                            bytes_after + (nitems * (actual_format / 8)),
                            False, type, &actual_type, &actual_format, &nitems, &bytes_after, &data);
                        if (data) {
                            size_t sz = nitems * (actual_format / 8);
                            result.insert(result.end(), data, data + sz);
                            XFree(data);
                            XDeleteProperty(display_, win, property);
                        }
                    }
                }
            }
        }

        XDeleteProperty(display_, win, property);
        spdlog::debug("X11Clipboard: INCR read complete, total={} bytes", result.size());
        return result;
    }

    // Get selection content as data
    std::vector<uint8_t> get_selection_data(Atom selection, Atom target) {
        std::vector<uint8_t> result;

        Atom prop = XInternAtom(display_, "CPPDESK_CLIP", False);
        XConvertSelection(display_, selection, target, prop, window_, CurrentTime);
        XFlush(display_);

        // Wait for SelectionNotify
        bool got_it = false;
        for (int attempts = 0; attempts < 50 && !got_it; attempts++) {
            XEvent ev;
            if (XCheckTypedWindowEvent(display_, window_, SelectionNotify, &ev)) {
                if (ev.xselection.property == None) {
                    spdlog::warn("X11Clipboard: selection conversion refused by owner");
                    return result;
                }
                got_it = true;
                prop = ev.xselection.property;
            } else {
                usleep(10000); // 10ms
            }
        }

        if (!got_it) {
            spdlog::warn("X11Clipboard: selection request timed out");
            return result;
        }

        result = read_property_incr(window_, prop, target);
        return result;
    }

    // Send selection data to requestor (INCR if needed)
    void send_selection_data(XSelectionRequestEvent& req, const std::vector<uint8_t>& data,
                              Atom type, int format) {
        if (data.size() <= static_cast<size_t>(INCR_CHUNK_MAX)) {
            // Short data - send directly
            XChangeProperty(display_, req.requestor, req.property, type,
                format, PropModeReplace, data.data(), static_cast<int>(data.size()));
        } else {
            // INCR transfer
            spdlog::debug("X11Clipboard: sending via INCR, total={} bytes", data.size());
            // Tell requestor we'll use INCR
            XChangeProperty(display_, req.requestor, req.property,
                XA_INCR, 32, PropModeReplace,
                reinterpret_cast<const unsigned char*>(&data), 0);

            XFlush(display_);

            // Send chunks as PropertyNotify events come in
            size_t offset = 0;
            size_t remaining = data.size();
            bool done = false;

            while (!done) {
                XEvent ev;
                XNextEvent(display_, &ev);

                if (ev.type == PropertyNotify &&
                    ev.xproperty.state == PropertyDelete &&
                    ev.xproperty.atom == req.property) {

                    size_t chunk_size = std::min(remaining, static_cast<size_t>(INCR_CHUNK_MAX));
                    if (chunk_size == 0) {
                        // Zero-length chunk signals end
                        XChangeProperty(display_, req.requestor, req.property,
                            type, format, PropModeReplace, data.data(), 0);
                        done = true;
                    } else {
                        XChangeProperty(display_, req.requestor, req.property,
                            type, format, PropModeReplace,
                            data.data() + offset, static_cast<int>(chunk_size));
                        offset += chunk_size;
                        remaining -= chunk_size;
                    }
                    XFlush(display_);
                }
            }
        }
    }

    // Handle SelectionRequest event
    void handle_selection_request(const XSelectionRequestEvent& req) {
        XEvent respond;
        respond.xselection.type = SelectionNotify;
        respond.xselection.requestor = req.requestor;
        respond.xselection.selection = req.selection;
        respond.xselection.target = req.target;
        respond.xselection.property = req.property;
        respond.xselection.time = req.time;

        std::lock_guard lk(store_mutex_);

        if (req.target == XA_TARGETS) {
            // List of supported targets
            std::vector<Atom> targets = {
                XA_TARGETS, XA_TIMESTAMP, XA_MULTIPLE,
                XA_UTF8_STRING, XA_TEXT, XA_STRING
            };

            if (!stored_text_.empty()) targets.push_back(XA_UTF8_STRING);
            if (!stored_html_.empty()) targets.push_back(XA_TEXT_HTML);
            if (!stored_rtf_.empty()) targets.push_back(XA_TEXT_RTF);
            if (!stored_image_png_.empty()) targets.push_back(XA_IMAGE_PNG);
            if (!stored_image_tiff_.empty()) targets.push_back(XA_IMAGE_TIFF);
            if (!stored_uri_list_.empty()) targets.push_back(XA_TEXT_URI_LIST);
            if (!stored_file_list_.empty()) targets.push_back(XA_FILE_NAME);

            XChangeProperty(display_, req.requestor, req.property,
                XA_ATOM, 32, PropModeReplace,
                reinterpret_cast<const unsigned char*>(targets.data()),
                static_cast<int>(targets.size()));

            respond.xselection.property = req.property;

        } else if (req.target == XA_TIMESTAMP) {
            Time ts = selection_timestamp_;
            XChangeProperty(display_, req.requestor, req.property,
                XA_TIMESTAMP, 32, PropModeReplace,
                reinterpret_cast<const unsigned char*>(&ts), 1);
            respond.xselection.property = req.property;

        } else if (req.target == XA_UTF8_STRING || req.target == XA_TEXT || req.target == XA_STRING) {
            if (!stored_text_.empty()) {
                std::vector<uint8_t> data(stored_text_.begin(), stored_text_.end());
                if (req.target == XA_STRING) {
                    // Convert to Latin-1 (simple, just use ASCII subset)
                    std::string ascii;
                    for (char c : stored_text_) ascii += (c >= 0 ? c : '?');
                    data.assign(ascii.begin(), ascii.end());
                }
                send_selection_data(const_cast<XSelectionRequestEvent&>(req), data,
                    req.target, 8);
                respond.xselection.property = req.property;
                return; // Already sent response implicitly via send_selection_data
            } else {
                respond.xselection.property = None;
            }

        } else if (req.target == XA_TEXT_HTML && !stored_html_.empty()) {
            std::vector<uint8_t> data(stored_html_.begin(), stored_html_.end());
            send_selection_data(const_cast<XSelectionRequestEvent&>(req), data, XA_TEXT_HTML, 8);
            return;

        } else if (req.target == XA_TEXT_RTF && !stored_rtf_.empty()) {
            std::vector<uint8_t> data(stored_rtf_.begin(), stored_rtf_.end());
            send_selection_data(const_cast<XSelectionRequestEvent&>(req), data, XA_TEXT_RTF, 8);
            return;

        } else if (req.target == XA_IMAGE_PNG && !stored_image_png_.empty()) {
            send_selection_data(const_cast<XSelectionRequestEvent&>(req), stored_image_png_, XA_IMAGE_PNG, 8);
            return;

        } else if (req.target == XA_IMAGE_TIFF && !stored_image_tiff_.empty()) {
            send_selection_data(const_cast<XSelectionRequestEvent&>(req), stored_image_tiff_, XA_IMAGE_TIFF, 8);
            return;

        } else if (req.target == XA_TEXT_URI_LIST && !stored_uri_list_.empty()) {
            std::vector<uint8_t> data(stored_uri_list_.begin(), stored_uri_list_.end());
            send_selection_data(const_cast<XSelectionRequestEvent&>(req), data, XA_TEXT_URI_LIST, 8);
            return;

        } else if (req.target == XA_FILE_NAME && !stored_file_list_.empty()) {
            // Send file list as text/uri-list
            std::string uri_list = util::encode_uri_list(stored_file_list_);
            std::vector<uint8_t> data(uri_list.begin(), uri_list.end());
            send_selection_data(const_cast<XSelectionRequestEvent&>(req), data, XA_TEXT_URI_LIST, 8);
            return;

        } else {
            respond.xselection.property = None;
        }

        XSendEvent(display_, req.requestor, True, NoEventMask, &respond);
        XFlush(display_);
    }

public:
    X11Clipboard() {
        if (init_display()) {
            active_selection_ = XA_CLIPBOARD;
            selection_timestamp_ = CurrentTime;
        }
    }

    ~X11Clipboard() {
        if (display_) {
            XDestroyWindow(display_, window_);
            XCloseDisplay(display_);
            spdlog::info("X11Clipboard: destroyed");
        }
    }

    // ====== Internal: Process events ======
    void process_events() {
        if (!display_) return;
        while (XPending(display_)) {
            XEvent ev;
            XNextEvent(display_, &ev);

            if (ev.type == SelectionRequest) {
                handle_selection_request(ev.xselectionrequest);
            } else if (ev.type == SelectionClear) {
                // Lost selection ownership
                std::lock_guard lk(store_mutex_);
                stored_text_.clear();
                stored_html_.clear();
                stored_rtf_.clear();
                stored_image_png_.clear();
                stored_image_tiff_.clear();
                stored_uri_list_.clear();
                stored_file_list_.clear();
                active_selection_ = 0;
                spdlog::info("X11Clipboard: lost selection ownership");
            }
        }
    }

    // ====== Text Operations ======
    std::string get_text() override {
        if (!display_) return "";
        process_events();

        auto data = get_selection_data(active_selection_ ? active_selection_ : XA_CLIPBOARD, XA_UTF8_STRING);
        if (!data.empty()) {
            // Remove null terminator if present
            while (!data.empty() && data.back() == 0) data.pop_back();
            return std::string(reinterpret_cast<char*>(data.data()), data.size());
        }

        // Try TEXT and STRING as fallback
        data = get_selection_data(active_selection_ ? active_selection_ : XA_CLIPBOARD, XA_TEXT);
        if (data.empty()) {
            data = get_selection_data(active_selection_ ? active_selection_ : XA_CLIPBOARD, XA_STRING);
        }

        while (!data.empty() && data.back() == 0) data.pop_back();
        return std::string(reinterpret_cast<char*>(data.data()), data.size());
    }

    bool set_text(const std::string& text) override {
        if (!display_) return false;
        process_events();

        {
            std::lock_guard lk(store_mutex_);
            stored_text_ = text;
        }

        active_selection_ = XA_CLIPBOARD;
        selection_timestamp_ = CurrentTime;
        XSetSelectionOwner(display_, XA_CLIPBOARD, window_, CurrentTime);

        // Also set PRIMARY for convenience
        XSetSelectionOwner(display_, XA_PRIMARY, window_, CurrentTime);

        change_count_.fetch_add(1);
        spdlog::debug("X11Clipboard: set_text {} chars, claimed CLIPBOARD+PRIMARY", text.size());
        return true;
    }

    bool has_text() override {
        if (!display_) return false;
        process_events();
        // Check if any text target is available
        auto owner = XGetSelectionOwner(display_, XA_CLIPBOARD);
        if (owner == None) owner = XGetSelectionOwner(display_, XA_PRIMARY);
        return owner != None;
    }

    // ====== File Operations ======
    std::vector<std::string> get_file_list() override {
        if (!display_) return {};
        process_events();

        auto data = get_selection_data(active_selection_ ? active_selection_ : XA_CLIPBOARD, XA_TEXT_URI_LIST);
        if (data.empty()) return {};

        while (!data.empty() && data.back() == 0) data.pop_back();
        std::string uri_str(reinterpret_cast<char*>(data.data()), data.size());
        return util::decode_uri_list(uri_str);
    }

    bool set_file_list(const std::vector<std::string>& paths) override {
        if (!display_) return false;
        process_events();

        std::string uri_list = util::encode_uri_list(paths);

        {
            std::lock_guard lk(store_mutex_);
            stored_file_list_ = paths;
            stored_uri_list_ = uri_list;
        }

        active_selection_ = XA_CLIPBOARD;
        selection_timestamp_ = CurrentTime;
        XSetSelectionOwner(display_, XA_CLIPBOARD, window_, CurrentTime);
        XSetSelectionOwner(display_, XA_PRIMARY, window_, CurrentTime);

        change_count_.fetch_add(1);
        spdlog::debug("X11Clipboard: set_file_list {} files", paths.size());
        return true;
    }

    bool has_file_list() override {
        if (!display_) return false;
        process_events();
        // X11 doesn't have native file list query; we check text/uri-list
        return XGetSelectionOwner(display_, XA_CLIPBOARD) != None;
    }

    // ====== Image Operations ======
    std::vector<uint8_t> get_image(const std::string& format) override {
        if (!display_) return {};
        process_events();

        Atom target = XA_IMAGE_PNG;
        if (format == "tiff" || format == "TIFF") target = XA_IMAGE_TIFF;
        else if (format == "bmp" || format == "BMP") target = XA_IMAGE_BMP;

        auto data = get_selection_data(active_selection_ ? active_selection_ : XA_CLIPBOARD, target);
        return data;
    }

    bool set_image(const std::vector<uint8_t>& data, const std::string& format) override {
        if (!display_ || data.empty()) return false;
        process_events();

        {
            std::lock_guard lk(store_mutex_);
            if (format == "png" || format.empty()) {
                stored_image_png_ = data;
            } else if (format == "tiff") {
                stored_image_tiff_ = data;
            }
        }

        active_selection_ = XA_CLIPBOARD;
        selection_timestamp_ = CurrentTime;
        XSetSelectionOwner(display_, XA_CLIPBOARD, window_, CurrentTime);

        change_count_.fetch_add(1);
        spdlog::debug("X11Clipboard: set_image format={}, {} bytes", format, data.size());
        return true;
    }

    // ====== HTML Operations ======
    std::string get_html() override {
        if (!display_) return "";
        process_events();

        auto data = get_selection_data(active_selection_ ? active_selection_ : XA_CLIPBOARD, XA_TEXT_HTML);
        while (!data.empty() && data.back() == 0) data.pop_back();
        return std::string(reinterpret_cast<char*>(data.data()), data.size());
    }

    bool set_html(const std::string& html) override {
        if (!display_) return false;
        process_events();

        {
            std::lock_guard lk(store_mutex_);
            stored_html_ = html;
        }

        active_selection_ = XA_CLIPBOARD;
        selection_timestamp_ = CurrentTime;
        XSetSelectionOwner(display_, XA_CLIPBOARD, window_, CurrentTime);

        change_count_.fetch_add(1);
        spdlog::debug("X11Clipboard: set_html {} chars", html.size());
        return true;
    }

    // ====== RTF Operations ======
    std::string get_rtf() override {
        if (!display_) return "";
        process_events();

        auto data = get_selection_data(active_selection_ ? active_selection_ : XA_CLIPBOARD, XA_TEXT_RTF);
        while (!data.empty() && data.back() == 0) data.pop_back();
        return std::string(reinterpret_cast<char*>(data.data()), data.size());
    }

    bool set_rtf(const std::string& rtf) override {
        if (!display_) return false;
        process_events();

        {
            std::lock_guard lk(store_mutex_);
            stored_rtf_ = rtf;
        }

        active_selection_ = XA_CLIPBOARD;
        selection_timestamp_ = CurrentTime;
        XSetSelectionOwner(display_, XA_CLIPBOARD, window_, CurrentTime);

        change_count_.fetch_add(1);
        spdlog::debug("X11Clipboard: set_rtf {} bytes", rtf.size());
        return true;
    }

    // ====== URI List ======
    std::vector<std::string> get_uri_list() override {
        return get_file_list(); // Same mechanism via text/uri-list
    }

    bool set_uri_list(const std::vector<std::string>& uris) override {
        if (!display_) return false;
        process_events();

        std::string encoded = util::encode_uri_list(uris);
        {
            std::lock_guard lk(store_mutex_);
            stored_uri_list_ = encoded;
        }

        active_selection_ = XA_CLIPBOARD;
        selection_timestamp_ = CurrentTime;
        XSetSelectionOwner(display_, XA_CLIPBOARD, window_, CurrentTime);

        change_count_.fetch_add(1);
        return true;
    }

    // ====== Clear and Ownership ======
    bool clear() override {
        return set_text(""); // Clear by setting empty text
    }

    bool owns_clipboard() override {
        if (!display_) return false;
        return XGetSelectionOwner(display_, XA_CLIPBOARD) == window_;
    }

    // ====== Change Notification ======
    void set_on_change(OnChange cb) override {
        onChange_ = std::move(cb);
    }

    // ====== Delayed Rendering ======
    bool enable_delayed_rendering(std::shared_ptr<DelayedRenderer> renderer) override {
        delayed_renderer_ = renderer;
        spdlog::info("X11Clipboard: delayed rendering enabled (via selection ownership)");
        return true;
    }

    void disable_delayed_rendering() override {
        delayed_renderer_.reset();
    }

    // ====== Format Detection ======
    ContentFormat available_formats() override {
        ContentFormat result = static_cast<ContentFormat>(0);
        if (!display_) return result;
        process_events();

        Atom sel = active_selection_ ? active_selection_ : XA_CLIPBOARD;
        if (XGetSelectionOwner(display_, sel) != None) {
            // We can't easily enumerate all formats; check by requesting TARGETS
            auto targets_data = get_selection_data(sel, XA_TARGETS);
            if (!targets_data.empty()) {
                auto* atoms = reinterpret_cast<Atom*>(targets_data.data());
                size_t natoms = targets_data.size() / sizeof(Atom);

                for (size_t i = 0; i < natoms; i++) {
                    if (atoms[i] == XA_UTF8_STRING || atoms[i] == XA_TEXT || atoms[i] == XA_STRING)
                        result = result | ContentFormat::TEXT;
                    else if (atoms[i] == XA_TEXT_HTML)
                        result = result | ContentFormat::HTML;
                    else if (atoms[i] == XA_TEXT_RTF)
                        result = result | ContentFormat::RTF;
                    else if (atoms[i] == XA_IMAGE_PNG)
                        result = result | ContentFormat::IMAGE_PNG;
                    else if (atoms[i] == XA_IMAGE_TIFF)
                        result = result | ContentFormat::IMAGE_TIFF;
                    else if (atoms[i] == XA_IMAGE_BMP)
                        result = result | ContentFormat::IMAGE_BMP;
                    else if (atoms[i] == XA_TEXT_URI_LIST || atoms[i] == XA_FILE_NAME) {
                        result = result | ContentFormat::FILE_LIST;
                        result = result | ContentFormat::URI_LIST;
                    }
                }
            } else {
                // If TARGETS inquiry fails, assume basic text
                result = ContentFormat::TEXT;
            }
        }
        return result;
    }

    ClipboardContentDescriptor get_content_descriptor() override {
        ClipboardContentDescriptor desc;
        desc.available_formats = available_formats();
        desc.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        desc.sequence_number = static_cast<uint32_t>(change_count_.load());
        desc.source_application = "cppdesk (X11)";

        if (desc.has_format(ContentFormat::TEXT)) desc.text = get_text();
        if (desc.has_format(ContentFormat::HTML)) desc.html = get_html();
        if (desc.has_format(ContentFormat::RTF)) desc.rtf = get_rtf();
        if (desc.has_format(ContentFormat::IMAGE_PNG)) desc.image_data = get_image("png");
        if (desc.has_format(ContentFormat::FILE_LIST)) desc.file_list = get_file_list();
        if (desc.has_format(ContentFormat::URI_LIST)) desc.uri_list = get_uri_list();

        if (!desc.text.empty()) desc.text_hash = ContentDeduplicator::compute_text_hash(desc.text);
        if (!desc.image_data.empty()) desc.image_hash = ContentDeduplicator::compute_data_hash(desc.image_data);
        if (!desc.file_list.empty()) desc.file_hash = ContentDeduplicator::compute_file_hash(desc.file_list);

        return desc;
    }

    bool set_content(const ClipboardContentDescriptor& content) override {
        if (!display_) return false;
        process_events();

        {
            std::lock_guard lk(store_mutex_);
            stored_text_ = content.text;
            stored_html_ = content.html;
            stored_rtf_ = content.rtf;
            stored_image_png_.clear();
            stored_image_tiff_.clear();

            if (!content.image_data.empty()) {
                if (content.image_format == "tiff")
                    stored_image_tiff_ = content.image_data;
                else
                    stored_image_png_ = content.image_data;
            }

            stored_file_list_ = content.file_list;
            stored_uri_list_ = util::encode_uri_list(
                content.file_list.empty() ? content.uri_list : content.file_list);
        }

        active_selection_ = XA_CLIPBOARD;
        selection_timestamp_ = CurrentTime;
        XSetSelectionOwner(display_, XA_CLIPBOARD, window_, CurrentTime);
        XSetSelectionOwner(display_, XA_PRIMARY, window_, CurrentTime);

        change_count_.fetch_add(1);
        return true;
    }

    int64_t get_change_count() override {
        return static_cast<int64_t>(change_count_.load());
    }
};

#endif // Platform

// ====== Factory ======
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
    std::string last_html;
    std::string last_rtf;
    std::vector<std::string> last_files;
    ClipboardContentDescriptor last_descriptor;
    OnTextChange on_text;
    OnFileChange on_files;
    OnImageChange on_image;
    OnHtmlChange on_html;
    OnRtfChange on_rtf;
    OnAnyChange on_any;
    mutable std::mutex mutex;
    int64_t last_change_count = -1;
};

ClipboardMonitor::ClipboardMonitor() : impl_(std::make_unique<Impl>()) {
    impl_->clipboard = PlatformClipboard::create();
}

ClipboardMonitor::~ClipboardMonitor() { stop(); }

void ClipboardMonitor::start(std::chrono::milliseconds interval) {
    if (!impl_->clipboard) return;
    impl_->running = true;
    impl_->last_change_count = impl_->clipboard->get_change_count();

    impl_->worker = std::thread([this, interval]() {
        spdlog::info("ClipboardMonitor: started with {}ms interval", interval.count());
        while (impl_->running) {
            try {
                int64_t cc = impl_->clipboard->get_change_count();
                bool changed = (cc != impl_->last_change_count && cc != -1);

                if (changed) {
                    impl_->last_change_count = cc;
                    auto desc = impl_->clipboard->get_content_descriptor();

                    std::lock_guard lk(impl_->mutex);

                    if (!desc.text.empty() && desc.text != impl_->last_text) {
                        impl_->last_text = desc.text;
                        if (impl_->on_text) impl_->on_text(desc.text);
                    }

                    if (!desc.html.empty() && desc.html != impl_->last_html) {
                        impl_->last_html = desc.html;
                        if (impl_->on_html) impl_->on_html(desc.html);
                    }

                    if (!desc.rtf.empty() && desc.rtf != impl_->last_rtf) {
                        impl_->last_rtf = desc.rtf;
                        if (impl_->on_rtf) impl_->on_rtf(desc.rtf);
                    }

                    if (desc.file_list != impl_->last_files && !desc.file_list.empty()) {
                        impl_->last_files = desc.file_list;
                        if (impl_->on_files) impl_->on_files(desc.file_list);
                    }

                    if (!desc.image_data.empty() && impl_->on_image) {
                        impl_->on_image(desc.image_data, desc.image_format);
                    }

                    impl_->last_descriptor = desc;

                    if (impl_->on_any) impl_->on_any(desc);
                }
            } catch (const std::exception& e) {
                spdlog::error("ClipboardMonitor: exception in poll loop: {}", e.what());
            }

            std::this_thread::sleep_for(interval);
        }
        spdlog::info("ClipboardMonitor: stopped");
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
void ClipboardMonitor::set_on_html_change(OnHtmlChange cb) { impl_->on_html = std::move(cb); }
void ClipboardMonitor::set_on_rtf_change(OnRtfChange cb) { impl_->on_rtf = std::move(cb); }
void ClipboardMonitor::set_on_any_change(OnAnyChange cb) { impl_->on_any = std::move(cb); }

std::string ClipboardMonitor::last_text() const {
    std::lock_guard lk(impl_->mutex);
    return impl_->last_text;
}

std::vector<std::string> ClipboardMonitor::last_files() const {
    std::lock_guard lk(impl_->mutex);
    return impl_->last_files;
}

std::string ClipboardMonitor::last_html() const {
    std::lock_guard lk(impl_->mutex);
    return impl_->last_html;
}

ClipboardContentDescriptor ClipboardMonitor::last_descriptor() const {
    std::lock_guard lk(impl_->mutex);
    return impl_->last_descriptor;
}

// ====== Clipboard Synchronizer ======
struct ClipboardSynchronizer::Impl {
    ClipboardSynchronizer::Mode mode = Mode::DISABLED;
    std::unique_ptr<ClipboardMonitor> monitor;
    std::unique_ptr<PlatformClipboard> clipboard;
    CliprdrServiceContext* context = nullptr;
    ContentDeduplicator deduplicator;
    ContentHash last_text_hash{};
    ContentHash last_file_hash{};
    ContentHash last_image_hash{};
    std::mutex hash_mutex;
    ProgressPercent file_progress;
    std::atomic<uint32_t> change_seq{0};
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
        spdlog::info("ClipboardSynchronizer: mode DISABLED");
    } else {
        const char* mode_str = "UNKNOWN";
        switch (mode) {
            case Mode::LOCAL_ONLY: mode_str = "LOCAL_ONLY"; break;
            case Mode::REMOTE_ONLY: mode_str = "REMOTE_ONLY"; break;
            case Mode::BIDIRECTIONAL: mode_str = "BIDIRECTIONAL"; break;
            default: break;
        }
        spdlog::info("ClipboardSynchronizer: mode {}", mode_str);
        impl_->monitor->start();
    }
}

ClipboardSynchronizer::Mode ClipboardSynchronizer::get_mode() const {
    return impl_->mode;
}

std::string ClipboardSynchronizer::poll_text_change() {
    return impl_->monitor->last_text();
}

std::vector<std::string> ClipboardSynchronizer::poll_file_change() {
    return impl_->monitor->last_files();
}

ClipboardContentDescriptor ClipboardSynchronizer::poll_content_change() {
    return impl_->monitor->last_descriptor();
}

std::optional<ClipboardContentDescriptor> ClipboardSynchronizer::poll_deduplicated_change() {
    auto desc = impl_->monitor->last_descriptor();
    if (!desc.available_formats) return std::nullopt;

    // Simple hash-based dedup: if content hash matches last seen, skip
    bool is_new = false;

    if (desc.has_format(ContentFormat::TEXT) && !desc.text.empty()) {
        auto h = ContentDeduplicator::compute_text_hash(desc.text);
        std::lock_guard lk(impl_->hash_mutex);
        if (h != impl_->last_text_hash) {
            impl_->last_text_hash = h;
            is_new = true;
        }
    }
    if (desc.has_format(ContentFormat::FILE_LIST) && !desc.file_list.empty()) {
        auto h = ContentDeduplicator::compute_file_hash(desc.file_list);
        std::lock_guard lk(impl_->hash_mutex);
        if (h != impl_->last_file_hash) {
            impl_->last_file_hash = h;
            is_new = true;
        }
    }
    if (desc.has_format(ContentFormat::IMAGE_PNG) && !desc.image_data.empty()) {
        auto h = ContentDeduplicator::compute_data_hash(desc.image_data);
        std::lock_guard lk(impl_->hash_mutex);
        if (h != impl_->last_image_hash) {
            impl_->last_image_hash = h;
            is_new = true;
        }
    }

    if (is_new) {
        impl_->deduplicator.store(ContentDeduplicator::compute_text_hash(desc.describe()), desc);
        return desc;
    }

    return std::nullopt;
}

void ClipboardSynchronizer::apply_remote_text(const std::string& text) {
    if (impl_->mode == Mode::LOCAL_ONLY) return;
    if (impl_->clipboard) {
        impl_->clipboard->set_text(text);
        spdlog::debug("ClipboardSynchronizer: applied remote text ({} chars)", text.size());
    }
}

void ClipboardSynchronizer::apply_remote_files(const std::vector<std::string>& files) {
    if (impl_->mode == Mode::LOCAL_ONLY) return;
    if (impl_->clipboard) {
        impl_->clipboard->set_file_list(files);
        spdlog::debug("ClipboardSynchronizer: applied remote files ({} files)", files.size());
    }
}

void ClipboardSynchronizer::apply_remote_image(const std::vector<uint8_t>& data) {
    if (impl_->mode == Mode::LOCAL_ONLY) return;
    if (impl_->clipboard) {
        impl_->clipboard->set_image(data, "png");
        spdlog::debug("ClipboardSynchronizer: applied remote image ({} bytes)", data.size());
    }
}

void ClipboardSynchronizer::apply_remote_html(const std::string& html) {
    if (impl_->mode == Mode::LOCAL_ONLY) return;
    if (impl_->clipboard) {
        impl_->clipboard->set_html(html);
        spdlog::debug("ClipboardSynchronizer: applied remote HTML ({} chars)", html.size());
    }
}

void ClipboardSynchronizer::apply_remote_rtf(const std::string& rtf) {
    if (impl_->mode == Mode::LOCAL_ONLY) return;
    if (impl_->clipboard) {
        impl_->clipboard->set_rtf(rtf);
        spdlog::debug("ClipboardSynchronizer: applied remote RTF ({} bytes)", rtf.size());
    }
}

void ClipboardSynchronizer::apply_remote_content(const ClipboardContentDescriptor& desc) {
    if (impl_->mode == Mode::LOCAL_ONLY) return;
    if (impl_->clipboard) {
        impl_->clipboard->set_content(desc);
        spdlog::debug("ClipboardSynchronizer: applied remote content {}",
            desc.describe());
    }
}

void ClipboardSynchronizer::begin_file_copy(int32_t conn_id) {
    impl_->file_progress = {};
    impl_->file_progress.bytes_processed = 0;
    impl_->file_progress.total_bytes = 0;
    spdlog::info("ClipboardSynchronizer: begin file copy, conn={}", conn_id);
}

void ClipboardSynchronizer::end_file_copy(int32_t conn_id) {
    impl_->file_progress = {100.0, false, false, "", 0, 0};
    impl_->file_progress.bytes_processed = impl_->file_progress.total_bytes;
    spdlog::info("ClipboardSynchronizer: end file copy, conn={}", conn_id);
}

ProgressPercent ClipboardSynchronizer::get_file_progress() const {
    return impl_->file_progress;
}

// ====== Clipboard File Server ======
struct ClipboardFileServer::Impl {
    std::unordered_map<std::string, FileEntry> files;
    mutable std::shared_mutex mutex;
    std::atomic<uint64_t> total_bytes{0};
};

ClipboardFileServer::ClipboardFileServer() : impl_(std::make_unique<Impl>()) {}

ClipboardFileServer::~ClipboardFileServer() = default;

void ClipboardFileServer::add_file(const FileEntry& entry) {
    std::unique_lock lock(impl_->mutex);
    impl_->files[entry.path] = entry;
    impl_->total_bytes = 0;
    for (const auto& [_, f] : impl_->files) impl_->total_bytes += f.size;
    spdlog::debug("ClipboardFileServer: added file '{}', size={}", entry.path, entry.size);
}

void ClipboardFileServer::add_files(const std::vector<FileEntry>& entries) {
    std::unique_lock lock(impl_->mutex);
    for (const auto& e : entries) {
        impl_->files[e.path] = e;
    }
    impl_->total_bytes = 0;
    for (const auto& [_, f] : impl_->files) impl_->total_bytes += f.size;
    spdlog::debug("ClipboardFileServer: added {} files", entries.size());
}

void ClipboardFileServer::remove_file(const std::string& path) {
    std::unique_lock lock(impl_->mutex);
    auto it = impl_->files.find(path);
    if (it != impl_->files.end()) {
        impl_->total_bytes -= it->second.size;
        impl_->files.erase(it);
    }
}

void ClipboardFileServer::clear_files() {
    std::unique_lock lock(impl_->mutex);
    impl_->files.clear();
    impl_->total_bytes = 0;
}

std::vector<ClipboardFileServer::FileEntry> ClipboardFileServer::list_files() const {
    std::shared_lock lock(impl_->mutex);
    std::vector<FileEntry> result;
    result.reserve(impl_->files.size());
    for (const auto& [_, f] : impl_->files) {
        result.push_back(f);
    }
    return result;
}

std::optional<ClipboardFileServer::FileEntry> ClipboardFileServer::get_file(const std::string& path) const {
    std::shared_lock lock(impl_->mutex);
    auto it = impl_->files.find(path);
    if (it != impl_->files.end()) return it->second;
    return std::nullopt;
}

size_t ClipboardFileServer::file_count() const {
    std::shared_lock lock(impl_->mutex);
    return impl_->files.size();
}

uint64_t ClipboardFileServer::total_size() const {
    return impl_->total_bytes.load();
}

std::vector<uint8_t> ClipboardFileServer::read_chunk(const std::string& path,
    uint64_t offset, uint64_t size) {
    std::shared_lock lock(impl_->mutex);
    auto it = impl_->files.find(path);
    if (it == impl_->files.end()) return {};

    const auto& data = it->second.cached_data;
    if (offset >= data.size()) return {};

    uint64_t actual_size = std::min(size, static_cast<uint64_t>(data.size() - offset));
    return std::vector<uint8_t>(data.begin() + static_cast<ptrdiff_t>(offset),
        data.begin() + static_cast<ptrdiff_t>(offset + actual_size));
}

ProgressPercent ClipboardFileServer::get_serve_progress(const std::string& path) const {
    ProgressPercent pp;
    std::shared_lock lock(impl_->mutex);
    auto it = impl_->files.find(path);
    if (it != impl_->files.end()) {
        pp.total_bytes = it->second.size;
        pp.bytes_processed = it->second.cached_data.size();
        if (pp.total_bytes > 0) {
            pp.percent = (static_cast<double>(pp.bytes_processed) / pp.total_bytes) * 100.0;
        }
    }
    return pp;
}

// ====== Clipboard FUSE Filesystem (Stub) ======
struct ClipboardFuseFs::Impl {
    MountOptions options;
    std::shared_ptr<ClipboardFileServer> file_server;
    std::atomic<bool> mounted{false};
    std::vector<ClipboardFileServer::FileEntry> current_entries;
    mutable std::shared_mutex entries_mutex;
};

ClipboardFuseFs::ClipboardFuseFs() : impl_(std::make_unique<Impl>()) {}

ClipboardFuseFs::~ClipboardFuseFs() {
    if (impl_->mounted) unmount();
}

bool ClipboardFuseFs::mount(const MountOptions& options) {
    if (impl_->mounted) {
        spdlog::warn("ClipboardFuseFs: already mounted at {}", impl_->options.mount_point);
        return false;
    }
    impl_->options = options;

    // Ensure mount point exists
    std::error_code ec;
    fs::create_directories(options.mount_point, ec);
    if (ec) {
        spdlog::error("ClipboardFuseFs: failed to create mount point '{}': {}",
            options.mount_point, ec.message());
        return false;
    }

    // STUB: In a real implementation, this would call fuse_main or fuse_mount
    // For now, we just mark as mounted and operate as a virtual filesystem
    impl_->mounted = true;
    spdlog::info("ClipboardFuseFs: mounted (stub) at '{}', readonly={}, max_file_size={}MB",
        options.mount_point, options.read_only, options.max_file_size_mb);
    return true;
}

void ClipboardFuseFs::unmount() {
    if (!impl_->mounted) return;
    // STUB: In real implementation, this would call fuse_unmount
    impl_->mounted = false;
    spdlog::info("ClipboardFuseFs: unmounted from '{}'", impl_->options.mount_point);
}

bool ClipboardFuseFs::is_mounted() const {
    return impl_->mounted.load();
}

std::string ClipboardFuseFs::mount_point() const {
    return impl_->options.mount_point;
}

void ClipboardFuseFs::set_file_server(std::shared_ptr<ClipboardFileServer> server) {
    impl_->file_server = std::move(server);
}

void ClipboardFuseFs::update_contents(const std::vector<ClipboardFileServer::FileEntry>& entries) {
    std::unique_lock lock(impl_->entries_mutex);
    impl_->current_entries = entries;
    spdlog::debug("ClipboardFuseFs: contents updated, {} entries", entries.size());
}

int ClipboardFuseFs::fuse_getattr(const char* path, void* statbuf) {
    // STUB: Fill statbuf with file metadata
    // struct stat* st = static_cast<struct stat*>(statbuf);
    // memset(st, 0, sizeof(struct stat));

    if (!impl_->mounted) return -1;

    std::string path_str(path);
    std::shared_lock lock(impl_->entries_mutex);

    if (path_str == "/") {
        // Root directory
        // st->st_mode = S_IFDIR | 0555;
        // st->st_nlink = 2;
        return 0;
    }

    // Look up file in entries
    auto it = std::find_if(impl_->current_entries.begin(), impl_->current_entries.end(),
        [&path_str](const ClipboardFileServer::FileEntry& e) {
            return e.path == path_str ||
                   ("/" + fs::path(e.path).filename().string()) == path_str;
        });

    if (it != impl_->current_entries.end()) {
        if (it->is_directory) {
            // st->st_mode = S_IFDIR | 0555;
            // st->st_nlink = 2;
        } else {
            // st->st_mode = S_IFREG | 0444;
            // st->st_nlink = 1;
            // st->st_size = static_cast<off_t>(it->size);
        }
        return 0;
    }

    spdlog::debug("ClipboardFuseFs::getattr: not found '{}'", path);
    return -2; // -ENOENT
}

int ClipboardFuseFs::fuse_readdir(const char* path, void* buf, void* filler,
                                   uint64_t offset, void* fi) {
    // STUB: Fill directory entries
    if (!impl_->mounted) return -1;

    std::string path_str(path);
    std::shared_lock lock(impl_->entries_mutex);

    if (path_str == "/") {
        // Add . and ..
        // filler(buf, ".", nullptr, 0);
        // filler(buf, "..", nullptr, 0);

        // Add file entries
        std::set<std::string> added;
        for (const auto& entry : impl_->current_entries) {
            std::string name = fs::path(entry.path).filename().string();
            if (added.insert(name).second) {
                // filler(buf, name.c_str(), nullptr, 0);
            }
        }
        return 0;
    }

    return -2; // -ENOENT
}

int ClipboardFuseFs::fuse_open(const char* path, void* fi) {
    // STUB: Open file for reading
    if (!impl_->mounted) return -1;

    std::string path_str(path);
    std::shared_lock lock(impl_->entries_mutex);

    auto it = std::find_if(impl_->current_entries.begin(), impl_->current_entries.end(),
        [&path_str](const ClipboardFileServer::FileEntry& e) {
            return e.path == path_str ||
                   ("/" + fs::path(e.path).filename().string()) == path_str;
        });

    if (it != impl_->current_entries.end() && !it->is_directory) {
        return 0;
    }

    return -2; // -ENOENT
}

int ClipboardFuseFs::fuse_read(const char* path, char* buf, size_t size,
                                uint64_t offset, void* fi) {
    // STUB: Read file data from file server
    if (!impl_->mounted || !impl_->file_server) return -1;

    std::string path_str(path);
    std::shared_lock lock(impl_->entries_mutex);

    auto it = std::find_if(impl_->current_entries.begin(), impl_->current_entries.end(),
        [&path_str](const ClipboardFileServer::FileEntry& e) {
            return e.path == path_str ||
                   ("/" + fs::path(e.path).filename().string()) == path_str;
        });

    if (it == impl_->current_entries.end()) return -2;

    std::string actual_path = it->path;
    auto chunk = impl_->file_server->read_chunk(actual_path, offset,
        static_cast<uint64_t>(size));

    if (!chunk.empty()) {
        memcpy(buf, chunk.data(), chunk.size());
        return static_cast<int>(chunk.size());
    }

    return 0;
}

int ClipboardFuseFs::fuse_release(const char* path, void* fi) {
    // STUB: Close file
    return 0;
}

// ====== IEnumFORMATETC Implementation ======
// Full COM enumerator for FORMATETC structures used by OLE clipboard
class FormatEnumerator : public IEnumFORMATETC {
private:
    LONG ref_count_ = 1;
    std::vector<FORMATETC> formats_;
    size_t cursor_ = 0;
    mutable std::mutex mutex_;

public:
    explicit FormatEnumerator(std::vector<FORMATETC> formats)
        : formats_(std::move(formats)), cursor_(0) {}

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
            *ppv = static_cast<IEnumFORMATETC*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&ref_count_);
    }

    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&ref_count_);
        if (r == 0) delete this;
        return r;
    }

    // IEnumFORMATETC
    STDMETHODIMP Next(ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched) override {
        if (!rgelt) return E_INVALIDARG;
        if (pceltFetched) *pceltFetched = 0;

        std::lock_guard lk(mutex_);
        ULONG fetched = 0;
        while (cursor_ < formats_.size() && fetched < celt) {
            rgelt[fetched] = formats_[cursor_];
            cursor_++;
            fetched++;
        }

        if (pceltFetched) *pceltFetched = fetched;
        return (fetched == celt) ? S_OK : S_FALSE;
    }

    STDMETHODIMP Skip(ULONG celt) override {
        std::lock_guard lk(mutex_);
        cursor_ = std::min(cursor_ + static_cast<size_t>(celt), formats_.size());
        return (cursor_ < formats_.size()) ? S_OK : S_FALSE;
    }

    STDMETHODIMP Reset() override {
        std::lock_guard lk(mutex_);
        cursor_ = 0;
        return S_OK;
    }

    STDMETHODIMP Clone(IEnumFORMATETC** ppenum) override {
        if (!ppenum) return E_INVALIDARG;
        std::lock_guard lk(mutex_);
        auto* clone = new FormatEnumerator(formats_);
        clone->cursor_ = this->cursor_;
        *ppenum = static_cast<IEnumFORMATETC*>(clone);
        return S_OK;
    }
};

// ====== SHA-256 Implementation for Content Deduplication ======
// Proper SHA-256 for robust content hashing
namespace sha256 {

static constexpr uint32_t K[64] = {
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

static inline uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

static void transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

std::array<uint8_t, 32> hash(const void* data_ptr, size_t len) {
    const auto* data = static_cast<const uint8_t*>(data_ptr);
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    uint8_t block[64] = {};
    size_t offset = 0;

    while (offset + 64 <= len) {
        transform(state, data + offset);
        offset += 64;
    }

    size_t remaining = len - offset;
    memcpy(block, data + offset, remaining);
    block[remaining] = 0x80;

    if (remaining >= 56) {
        transform(state, block);
        memset(block, 0, 64);
    }

    uint64_t bits = len * 8;
    for (int i = 0; i < 8; i++) {
        block[56 + i] = static_cast<uint8_t>(bits >> (56 - i * 8));
    }
    transform(state, block);

    std::array<uint8_t, 32> result;
    for (int i = 0; i < 8; i++) {
        result[i * 4]     = static_cast<uint8_t>(state[i] >> 24);
        result[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
        result[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
        result[i * 4 + 3] = static_cast<uint8_t>(state[i]);
    }
    return result;
}

std::array<uint8_t, 32> hash_string(const std::string& s) {
    return hash(s.data(), s.size());
}

std::array<uint8_t, 32> hash_bytes(const std::vector<uint8_t>& v) {
    return hash(v.data(), v.size());
}

} // namespace sha256

// ====== SHA-256 Content Deduplicator ======
class SHA256Deduplicator {
    std::unordered_map<std::string, uint64_t> hashes_; // hex hash -> timestamp
    mutable std::shared_mutex mutex_;
    size_t max_entries_ = 4096;

    static std::string hex(const std::array<uint8_t, 32>& h) {
        static const char* digits = "0123456789abcdef";
        std::string s(64, '0');
        for (size_t i = 0; i < 32; i++) {
            s[i * 2]     = digits[h[i] >> 4];
            s[i * 2 + 1] = digits[h[i] & 0xF];
        }
        return s;
    }

public:
    explicit SHA256Deduplicator(size_t max = 4096) : max_entries_(max) {}

    bool is_duplicate(const std::string& text) {
        auto h = sha256::hash_string(text);
        std::string key = hex(h);
        std::unique_lock lock(mutex_);
        auto it = hashes_.find(key);
        if (it != hashes_.end()) {
            it->second = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            return true;
        }
        hashes_[key] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        evict_oldest();
        return false;
    }

    bool is_duplicate_bytes(const std::vector<uint8_t>& data) {
        auto h = sha256::hash_bytes(data);
        std::string key = hex(h);
        std::unique_lock lock(mutex_);
        auto it = hashes_.find(key);
        if (it != hashes_.end()) {
            it->second = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            return true;
        }
        hashes_[key] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        evict_oldest();
        return false;
    }

    void clear() {
        std::unique_lock lock(mutex_);
        hashes_.clear();
    }

    size_t size() const {
        std::shared_lock lock(mutex_);
        return hashes_.size();
    }

private:
    void evict_oldest() {
        while (hashes_.size() > max_entries_) {
            auto oldest = hashes_.begin();
            for (auto it = hashes_.begin(); it != hashes_.end(); ++it) {
                if (it->second < oldest->second) oldest = it;
            }
            hashes_.erase(oldest);
        }
    }
};

// ====== Clipboard Rate Limiter ======
// Token bucket algorithm to prevent clipboard flooding during sync
class ClipboardRateLimiter {
    double max_tokens_;
    double tokens_;
    double refill_rate_; // tokens per second
    std::chrono::steady_clock::time_point last_refill_;
    std::mutex mutex_;

public:
    ClipboardRateLimiter(double max_tokens = 10.0, double refill_rate = 2.0)
        : max_tokens_(max_tokens), tokens_(max_tokens),
          refill_rate_(refill_rate),
          last_refill_(std::chrono::steady_clock::now()) {}

    bool try_consume(double tokens = 1.0) {
        std::lock_guard lk(mutex_);
        refill();
        if (tokens_ >= tokens) {
            tokens_ -= tokens;
            return true;
        }
        return false;
    }

    // Block until tokens are available, returns false on timeout
    bool wait_and_consume(double tokens = 1.0,
                          std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard lk(mutex_);
                refill();
                if (tokens_ >= tokens) {
                    tokens_ -= tokens;
                    return true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        spdlog::warn("ClipboardRateLimiter: timed out waiting for {} tokens", tokens);
        return false;
    }

    void set_rate(double tokens_per_second) {
        std::lock_guard lk(mutex_);
        refill_rate_ = tokens_per_second;
        if (tokens_ > max_tokens_) tokens_ = max_tokens_;
    }

    double available_tokens() {
        std::lock_guard lk(mutex_);
        refill();
        return tokens_;
    }

    void reset() {
        std::lock_guard lk(mutex_);
        tokens_ = max_tokens_;
        last_refill_ = std::chrono::steady_clock::now();
    }

private:
    void refill() {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - last_refill_).count();
        tokens_ = std::min(max_tokens_, tokens_ + elapsed * refill_rate_);
        last_refill_ = now;
    }
};

// ====== Rate-Limited Clipboard Synchronizer ======
class RateLimitedClipboardSync {
    ClipboardRateLimiter limiter_;
    SHA256Deduplicator dedup_;
    std::atomic<size_t> sync_count_{0};
    std::atomic<size_t> drop_count_{0};
    std::atomic<uint64_t> last_sync_ms_{0};

public:
    RateLimitedClipboardSync(double max_rate = 5.0, size_t dedup_cache = 2048)
        : limiter_(max_rate, max_rate), dedup_(dedup_cache) {}

    enum class SyncResult {
        ALLOWED,
        RATE_LIMITED,
        DUPLICATE,
        TOO_LARGE
    };

    SyncResult check_text(const std::string& text, size_t max_size = 1024 * 1024) {
        if (text.size() > max_size) return SyncResult::TOO_LARGE;
        if (dedup_.is_duplicate(text)) {
            drop_count_.fetch_add(1);
            return SyncResult::DUPLICATE;
        }
        if (!limiter_.try_consume(1.0)) {
            drop_count_.fetch_add(1);
            return SyncResult::RATE_LIMITED;
        }
        sync_count_.fetch_add(1);
        last_sync_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        return SyncResult::ALLOWED;
    }

    SyncResult check_image(const std::vector<uint8_t>& data,
                           size_t max_size = 50 * 1024 * 1024) {
        if (data.size() > max_size) return SyncResult::TOO_LARGE;
        if (dedup_.is_duplicate_bytes(data)) {
            drop_count_.fetch_add(1);
            return SyncResult::DUPLICATE;
        }
        // Images cost more tokens
        double cost = 1.0 + (static_cast<double>(data.size()) / (1024.0 * 1024.0));
        if (!limiter_.try_consume(cost)) {
            drop_count_.fetch_add(1);
            return SyncResult::RATE_LIMITED;
        }
        sync_count_.fetch_add(1);
        last_sync_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        return SyncResult::ALLOWED;
    }

    size_t sync_count() const { return sync_count_.load(); }
    size_t drop_count() const { return drop_count_.load(); }
    uint64_t last_sync_ms() const { return last_sync_ms_.load(); }

    void reset_stats() {
        sync_count_ = 0;
        drop_count_ = 0;
        last_sync_ms_ = 0;
    }

    void clear_dedup() { dedup_.clear(); }
};

// ====== x-special/gnome-copied-files Format Support ======
// GNOME and KDE file managers use this special format for cut/copy operations
namespace gnome_copied_files {

// Parse "x-special/gnome-copied-files" or "x-special/nautilus-clipboard" format
// Format: "copy\nfile:///path1\nfile:///path2\n" or "cut\nfile:///path1\n"
struct GnomeFileList {
    enum Action { COPY, CUT, UNKNOWN };
    Action action = COPY;
    std::vector<std::string> uris;
};

GnomeFileList parse(const std::string& raw) {
    GnomeFileList result;
    if (raw.empty()) return result;

    std::istringstream iss(raw);
    std::string first_line;
    if (!std::getline(iss, first_line)) return result;

    // First line is the action: "copy" or "cut"
    if (first_line == "cut" || first_line == "Cut") {
        result.action = GnomeFileList::CUT;
    } else {
        result.action = GnomeFileList::COPY;
    }

    // Remaining lines are file:// URIs
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            result.uris.push_back(line);
        }
    }

    spdlog::debug("gnome_copied_files::parse: action={}, {} URIs",
        result.action == GnomeFileList::CUT ? "cut" : "copy",
        result.uris.size());
    return result;
}

std::string generate(const std::vector<std::string>& uris, bool is_cut) {
    std::ostringstream oss;
    oss << (is_cut ? "cut" : "copy") << "\n";
    for (size_t i = 0; i < uris.size(); i++) {
        oss << uris[i];
        if (i < uris.size() - 1) oss << "\n";
    }
    return oss.str();
}

// Convert file:// URIs to local paths
std::vector<std::string> uris_to_paths(const std::vector<std::string>& uris) {
    std::vector<std::string> paths;
    for (const auto& uri : uris) {
        // Remove file:// prefix
        std::string path;
        if (uri.find("file://") == 0) {
            path = uri.substr(7);
        } else if (uri.find("file:") == 0) {
            path = uri.substr(5);
        } else {
            path = uri;
        }

        // URL-decode the path (basic decoding of common chars)
        std::string decoded;
        for (size_t i = 0; i < path.size(); i++) {
            if (path[i] == '%' && i + 2 < path.size()) {
                char hex[3] = {path[i + 1], path[i + 2], 0};
                char* end = nullptr;
                long val = strtol(hex, &end, 16);
                if (end && *end == 0 && val > 0 && val <= 255) {
                    decoded += static_cast<char>(val);
                    i += 2;
                    continue;
                }
            }
            decoded += path[i];
        }

        // On Linux, strip the hostname prefix if present (file://hostname/path)
#ifdef __linux__
        if (!decoded.empty() && decoded[0] != '/') {
            auto slash_pos = decoded.find('/');
            if (slash_pos != std::string::npos) {
                decoded = decoded.substr(slash_pos);
            }
        }
#endif

        if (!decoded.empty()) {
            paths.push_back(decoded);
        }
    }
    return paths;
}

// Convert local paths to file:// URIs
std::vector<std::string> paths_to_uris(const std::vector<std::string>& paths) {
    std::vector<std::string> uris;
    for (const auto& p : paths) {
        // Simple URL encoding for the path
        std::ostringstream oss;
        oss << "file://";
        for (char c : p) {
            if (c == ' ') {
                oss << "%20";
            } else if (c == '#') {
                oss << "%23";
            } else if (c == '%') {
                oss << "%25";
            } else if (c == '?') {
                oss << "%3F";
            } else if (c == '&') {
                oss << "%26";
            } else if (c == '=') {
                oss << "%3D";
            } else if (static_cast<unsigned char>(c) > 127) {
                oss << '%' << std::hex << std::uppercase
                    << std::setw(2) << std::setfill('0')
                    << static_cast<int>(static_cast<unsigned char>(c));
            } else {
                oss << c;
            }
        }
        uris.push_back(oss.str());
    }
    return uris;
}

} // namespace gnome_copied_files

// ====== macOS Pasteboard Observer with Timer-based Polling ======
#ifdef __APPLE__
class MacPasteboardObserver {
    void* timer_ = nullptr;  // NSTimer*
    std::function<void()> callback_;
    std::chrono::milliseconds interval_;
    std::atomic<bool> running_{false};
    std::atomic<int64_t> last_change_count_{0};

public:
    MacPasteboardObserver(std::chrono::milliseconds interval = std::chrono::milliseconds(500))
        : interval_(interval) {}

    ~MacPasteboardObserver() { stop(); }

    void set_callback(std::function<void()> cb) {
        callback_ = std::move(cb);
    }

    bool start() {
        if (running_) return true;

        running_ = true;
        last_change_count_ = get_current_change_count();

        // Use a dedicated thread with CFRunLoop for timer-based polling
        std::thread([this]() {
            spdlog::info("MacPasteboardObserver: polling started at {}ms interval",
                interval_.count());

            @autoreleasepool {
                // Create a CFRunLoop-based timer for reliable macOS polling
                CFRunLoopTimerContext ctx = {0, this, nullptr, nullptr, nullptr};
                CFRunLoopTimerRef timer = CFRunLoopTimerCreate(
                    kCFAllocatorDefault,
                    CFAbsoluteTimeGetCurrent() + 0.1,
                    interval_.count() / 1000.0,
                    0, 0,
                    [](CFRunLoopTimerRef, void* info) {
                        auto* self = static_cast<MacPasteboardObserver*>(info);
                        int64_t cc = self->get_current_change_count();
                        int64_t prev = self->last_change_count_.exchange(cc);
                        if (prev != cc && cc != -1 && self->callback_) {
                            spdlog::debug("MacPasteboardObserver: change detected, "
                                "old={}, new={}", prev, cc);
                            self->callback_();
                        }
                    },
                    &ctx);

                if (timer) {
                    CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer,
                        kCFRunLoopCommonModes);
                    CFRunLoopRun();

                    CFRunLoopTimerInvalidate(timer);
                    CFRelease(timer);
                }
            }

            spdlog::info("MacPasteboardObserver: polling stopped");
        }).detach();

        return true;
    }

    void stop() {
        running_ = false;
        // The CFRunLoop will stop when running_ is checked (in real impl)
    }

    bool is_running() const { return running_.load(); }

    void set_interval(std::chrono::milliseconds interval) {
        interval_ = interval;
    }

private:
    int64_t get_current_change_count() const {
        @autoreleasepool {
            return [[NSPasteboard generalPasteboard] changeCount];
        }
        return -1;
    }
};
#endif // __APPLE__

// ====== Wayland Clipboard Stub ======
// Placeholder for future Wayland data-device protocol support
#ifdef __linux__
class WaylandClipboardStub {
    std::atomic<bool> initialized_{false};
    std::string error_msg_;

public:
    WaylandClipboardStub() {
        // Check if running under Wayland
        const char* xdg_session_type = std::getenv("XDG_SESSION_TYPE");
        const char* wayland_display = std::getenv("WAYLAND_DISPLAY");

        if (xdg_session_type && strcmp(xdg_session_type, "wayland") == 0) {
            spdlog::info("WaylandClipboardStub: Wayland session detected (display={})",
                wayland_display ? wayland_display : "unset");
        } else if (wayland_display) {
            spdlog::info("WaylandClipboardStub: WAYLAND_DISPLAY={} set but XDG_SESSION_TYPE={}",
                wayland_display, xdg_session_type ? xdg_session_type : "unset");
        }
    }

    bool is_wayland_session() const {
        const char* st = std::getenv("XDG_SESSION_TYPE");
        return st && strcmp(st, "wayland") == 0;
    }

    bool initialize() {
        if (initialized_) return true;

        if (!is_wayland_session()) {
            error_msg_ = "Not a Wayland session";
            return false;
        }

        // In a full implementation, this would:
        // 1. Connect to wl_display
        // 2. Bind wl_data_device_manager
        // 3. Create wl_data_device for the seat
        // 4. Set up wl_data_offer / wl_data_source handlers
        // 5. Handle data-device protocol for copy/paste

        initialized_ = true;
        spdlog::info("WaylandClipboardStub: initialized (stub - full impl requires "
            "libwayland-client and wl_data_device_manager protocol)");
        return true;
    }

    // Stub methods matching PlatformClipboard interface
    bool has_text() {
        // In real impl: check wl_data_offer for text/plain MIME type
        return false;
    }

    std::string get_text() {
        // In real impl: read fd from wl_data_offer and read text/plain data
        return "";
    }

    bool set_text(const std::string& text) {
        // In real impl: create wl_data_source, offer text/plain, set selection
        (void)text;
        spdlog::warn("WaylandClipboardStub::set_text: not implemented (stub)");
        return false;
    }

    std::string status() const {
        return initialized_ ? "stub_initialized" : "not_initialized";
    }

    std::string last_error() const {
        return error_msg_;
    }
};
#endif // __linux__

// ====== Clipboard Compression Utilities ======
// Compress/decompress clipboard data for network transfer
namespace clipboard_compression {

// Simple zlib-like deflate stub with size reporting
struct CompressionStats {
    size_t original_size = 0;
    size_t compressed_size = 0;
    double ratio = 0.0;
    uint64_t elapsed_us = 0;
    bool was_compressed = false;
};

// Stub: in real implementation this would use zlib/miniz
// For now provides the framework with size-based heuristics
CompressionStats compress_if_beneficial(const std::vector<uint8_t>& data,
                                         size_t min_size = 1024) {
    CompressionStats stats;
    stats.original_size = data.size();

    if (data.size() < min_size) {
        stats.compressed_size = data.size();
        stats.ratio = 1.0;
        stats.was_compressed = false;
        return stats;
    }

    auto start = std::chrono::steady_clock::now();

    // Heuristic: text data compresses well, already-compressed data (PNG, etc.) doesn't
    // Check if data looks like text (high proportion of printable ASCII)
    size_t printable = 0;
    for (size_t i = 0; i < std::min(data.size(), size_t(4096)); i++) {
        uint8_t b = data[i];
        if ((b >= 32 && b <= 126) || b == '\n' || b == '\r' || b == '\t') {
            printable++;
        }
    }

    bool looks_text = (printable > std::min(data.size(), size_t(4096)) * 7 / 10);

    if (looks_text) {
        // Placeholder: would call zlib deflate here
        // For now, estimate ~50% compression for text
        stats.compressed_size = data.size() * 55 / 100;
        stats.ratio = static_cast<double>(stats.compressed_size) / stats.original_size;
        stats.was_compressed = true;
    } else {
        stats.compressed_size = data.size();
        stats.ratio = 1.0;
        stats.was_compressed = false;
    }

    auto end = std::chrono::steady_clock::now();
    stats.elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        end - start).count();

    spdlog::debug("clipboard_compression: orig={}, comp={}, ratio={:.3f}, text_like={}",
        stats.original_size, stats.compressed_size, stats.ratio, looks_text);
    return stats;
}

// Stub decompression
std::vector<uint8_t> decompress(const std::vector<uint8_t>& data,
                                 size_t expected_original_size) {
    // In real impl: call zlib inflate
    // For now, return as-is (data was never actually compressed in stub)
    (void)expected_original_size;
    return data;
}

// Check if data should be compressed based on type
bool should_compress(const std::string& content_type) {
    static const std::vector<std::string> compressible = {
        "text/plain", "text/html", "text/rtf", "text/xml",
        "text/css", "text/javascript", "application/json",
        "application/rtf"
    };
    static const std::vector<std::string> incompressible = {
        "image/png", "image/jpeg", "image/gif", "image/webp",
        "image/tiff", "application/zip", "application/gzip",
        "video/", "audio/"
    };

    for (const auto& c : compressible) {
        if (content_type.find(c) != std::string::npos) return true;
    }
    for (const auto& ic : incompressible) {
        if (content_type.find(ic) != std::string::npos) return false;
    }
    // Default: try compression for unknown types
    return true;
}

} // namespace clipboard_compression

// ====== Clipboard Statistics Tracker ======
class ClipboardStats {
    std::atomic<uint64_t> total_ops_{0};
    std::atomic<uint64_t> text_reads_{0};
    std::atomic<uint64_t> text_writes_{0};
    std::atomic<uint64_t> image_reads_{0};
    std::atomic<uint64_t> image_writes_{0};
    std::atomic<uint64_t> file_reads_{0};
    std::atomic<uint64_t> file_writes_{0};
    std::atomic<uint64_t> errors_{0};
    std::atomic<uint64_t> bytes_read_{0};
    std::atomic<uint64_t> bytes_written_{0};
    std::chrono::steady_clock::time_point start_time_;

public:
    ClipboardStats() : start_time_(std::chrono::steady_clock::now()) {}

    void record_text_read(size_t bytes) {
        text_reads_.fetch_add(1);
        bytes_read_.fetch_add(bytes);
        total_ops_.fetch_add(1);
    }

    void record_text_write(size_t bytes) {
        text_writes_.fetch_add(1);
        bytes_written_.fetch_add(bytes);
        total_ops_.fetch_add(1);
    }

    void record_image_read(size_t bytes) {
        image_reads_.fetch_add(1);
        bytes_read_.fetch_add(bytes);
        total_ops_.fetch_add(1);
    }

    void record_image_write(size_t bytes) {
        image_writes_.fetch_add(1);
        bytes_written_.fetch_add(bytes);
        total_ops_.fetch_add(1);
    }

    void record_file_read(uint32_t count, uint64_t bytes) {
        file_reads_.fetch_add(1);
        bytes_read_.fetch_add(bytes);
        total_ops_.fetch_add(1);
        (void)count;
    }

    void record_file_write(uint32_t count, uint64_t bytes) {
        file_writes_.fetch_add(1);
        bytes_written_.fetch_add(bytes);
        total_ops_.fetch_add(1);
        (void)count;
    }

    void record_error() { errors_.fetch_add(1); }

    uint64_t uptime_seconds() const {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time_).count();
    }

    // Generate a human-readable stats report
    std::string report() const {
        std::ostringstream oss;
        auto uptime = uptime_seconds();
        oss << "Clipboard Stats (uptime: " << uptime << "s)\n";
        oss << "  Total operations: " << total_ops_.load() << "\n";
        oss << "  Text:  R=" << text_reads_.load() << " W=" << text_writes_.load() << "\n";
        oss << "  Image: R=" << image_reads_.load() << " W=" << image_writes_.load() << "\n";
        oss << "  Files: R=" << file_reads_.load() << " W=" << file_writes_.load() << "\n";
        oss << "  Errors: " << errors_.load() << "\n";
        oss << "  Bytes read: " << bytes_read_.load() << " ("
            << format_bytes(bytes_read_.load()) << ")\n";
        oss << "  Bytes written: " << bytes_written_.load() << " ("
            << format_bytes(bytes_written_.load()) << ")\n";
        if (uptime > 0) {
            oss << "  Throughput: " << (bytes_read_.load() + bytes_written_.load()) / uptime
                << " bytes/sec\n";
        }
        return oss.str();
    }

    void reset() {
        total_ops_ = 0;
        text_reads_ = 0; text_writes_ = 0;
        image_reads_ = 0; image_writes_ = 0;
        file_reads_ = 0; file_writes_ = 0;
        errors_ = 0;
        bytes_read_ = 0; bytes_written_ = 0;
        start_time_ = std::chrono::steady_clock::now();
    }

private:
    static std::string format_bytes(uint64_t bytes) {
        const char* suffixes[] = {"B", "KB", "MB", "GB"};
        int idx = 0;
        double b = static_cast<double>(bytes);
        while (b >= 1024.0 && idx < 3) {
            b /= 1024.0;
            idx++;
        }
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << b << " " << suffixes[idx];
        return oss.str();
    }
};

// ====== Global Clipboard Stats Instance ======
static ClipboardStats g_clipboard_stats;

// ====== Clipboard File Server with Compression ======
class CompressedClipboardFileServer {
    ClipboardFileServer base_server_;
    mutable std::shared_mutex mutex_;

public:
    void add_compressed_file(const std::string& path,
                              const std::vector<uint8_t>& data,
                              bool auto_compress = true) {
        auto stats = clipboard_compression::compress_if_beneficial(data, 256);
        ClipboardFileServer::FileEntry entry;
        entry.path = path;
        entry.size = stats.compressed_size;
        entry.is_directory = false;
        entry.mime_type = "application/octet-stream";
        entry.local_path = "";
        // In full impl: entry.cached_data = compressed data
        (void)auto_compress;

        std::unique_lock lock(mutex_);
        base_server_.add_file(entry);

        spdlog::debug("CompressedClipboardFileServer: added '{}', {} -> {} bytes (ratio {:.2f})",
            path, stats.original_size, stats.compressed_size, stats.ratio);
    }

    void add_file(const ClipboardFileServer::FileEntry& entry) {
        std::unique_lock lock(mutex_);
        base_server_.add_file(entry);
    }

    std::vector<ClipboardFileServer::FileEntry> list_files() const {
        std::shared_lock lock(mutex_);
        return base_server_.list_files();
    }

    size_t file_count() const {
        std::shared_lock lock(mutex_);
        return base_server_.file_count();
    }

    uint64_t total_size() const {
        std::shared_lock lock(mutex_);
        return base_server_.total_size();
    }

    void clear() {
        std::unique_lock lock(mutex_);
        base_server_.clear_files();
    }
};

// ====== Clipboard Security Helpers ======
// Validate and sanitize clipboard content for safety
namespace clipboard_security {

// Maximum lengths for safety
static constexpr size_t MAX_TEXT_LENGTH = 10 * 1024 * 1024;     // 10 MB
static constexpr size_t MAX_HTML_LENGTH = 50 * 1024 * 1024;     // 50 MB
static constexpr size_t MAX_RTF_LENGTH = 50 * 1024 * 1024;      // 50 MB
static constexpr size_t MAX_IMAGE_SIZE = 100 * 1024 * 1024;     // 100 MB
static constexpr size_t MAX_FILE_COUNT = 10000;
static constexpr size_t MAX_PATH_LENGTH = 4096;

// Validate a single file path
bool is_valid_file_path(const std::string& path) {
    if (path.empty()) return false;
    if (path.size() > MAX_PATH_LENGTH) return false;

    // Reject paths with null bytes
    if (path.find('\0') != std::string::npos) return false;

    // Basic path traversal check
    if (path.find("..") != std::string::npos) {
        // Allow .. if it's part of a valid path, but flag it
        spdlog::warn("clipboard_security: path contains '..': {}", path);
    }

    return true;
}

// Validate file list
bool validate_file_list(const std::vector<std::string>& paths) {
    if (paths.size() > MAX_FILE_COUNT) {
        spdlog::warn("clipboard_security: too many files ({})", paths.size());
        return false;
    }
    for (const auto& p : paths) {
        if (!is_valid_file_path(p)) return false;
    }
    return true;
}

// Truncate text if too long
std::string safe_text(const std::string& text) {
    if (text.size() <= MAX_TEXT_LENGTH) return text;
    spdlog::warn("clipboard_security: truncating text from {} to {} bytes",
        text.size(), MAX_TEXT_LENGTH);
    return text.substr(0, MAX_TEXT_LENGTH);
}

// Check image data for basic integrity
bool validate_image_data(const std::vector<uint8_t>& data) {
    if (data.size() > MAX_IMAGE_SIZE) {
        spdlog::warn("clipboard_security: image too large ({})", data.size());
        return false;
    }
    if (data.size() < 4) return false;

    // Basic magic byte check for common formats
    // PNG: 89 50 4E 47
    // JPEG: FF D8 FF
    // GIF: 47 49 46
    // BMP: 42 4D
    // TIFF: 49 49 or 4D 4D
    bool valid_magic =
        (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) ||  // PNG
        (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) ||                       // JPEG
        (data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46) ||                       // GIF
        (data[0] == 0x42 && data[1] == 0x4D) ||                                          // BMP
        (data[0] == 0x49 && data[1] == 0x49) ||                                          // TIFF LE
        (data[0] == 0x4D && data[1] == 0x4D) ||                                          // TIFF BE
        (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46);     // RIFF (WebP)

    if (!valid_magic) {
        spdlog::warn("clipboard_security: unrecognized image format");
        return false;
    }

    return true;
}

// Sanitize HTML for safe clipboard use
std::string sanitize_html(const std::string& html) {
    // Use existing sanitizer
    return util::sanitize_html_for_clipboard(html);
}

} // namespace clipboard_security

// ====== Clipboard Format Registry ======
// Registry for mapping between platform format IDs and ContentFormat
class ClipboardFormatRegistry {
    struct FormatEntry {
        ContentFormat content_format;
        std::string mime_type;
        std::string platform_name;
        int priority = 0; // Higher = preferred
    };

    std::unordered_map<int, FormatEntry> id_to_format_;
    std::unordered_map<std::string, int> mime_to_id_;
    mutable std::shared_mutex mutex_;

public:
    void register_format(int platform_id, ContentFormat cf,
                         const std::string& mime, const std::string& name,
                         int priority = 0) {
        std::unique_lock lock(mutex_);
        FormatEntry entry{cf, mime, name, priority};
        id_to_format_[platform_id] = entry;
        mime_to_id_[mime] = platform_id;
        spdlog::debug("FormatRegistry: registered '{}' (MIME: {}, id: {}, prio: {})",
            name, mime, platform_id, priority);
    }

    std::optional<ContentFormat> lookup(int platform_id) const {
        std::shared_lock lock(mutex_);
        auto it = id_to_format_.find(platform_id);
        if (it != id_to_format_.end()) return it->second.content_format;
        return std::nullopt;
    }

    std::optional<int> lookup_by_mime(const std::string& mime) const {
        std::shared_lock lock(mutex_);
        auto it = mime_to_id_.find(mime);
        if (it != mime_to_id_.end()) return it->second;
        return std::nullopt;
    }

    std::string get_mime(int platform_id) const {
        std::shared_lock lock(mutex_);
        auto it = id_to_format_.find(platform_id);
        if (it != id_to_format_.end()) return it->second.mime_type;
        return "application/octet-stream";
    }

    std::vector<int> get_all_ids() const {
        std::shared_lock lock(mutex_);
        std::vector<int> ids;
        ids.reserve(id_to_format_.size());
        for (const auto& [id, _] : id_to_format_) ids.push_back(id);
        return ids;
    }

    void init_defaults() {
        // Register common platform formats
        register_format(1,  ContentFormat::TEXT,       "text/plain",     "CF_TEXT/STRING",         10);
        register_format(13, ContentFormat::TEXT,       "text/plain",     "CF_UNICODETEXT",         20);
        register_format(2,  ContentFormat::IMAGE_BMP,  "image/bmp",      "CF_BITMAP",               5);
        register_format(8,  ContentFormat::IMAGE_DIB,  "image/bmp",      "CF_DIB",                 10);
        register_format(17, ContentFormat::IMAGE_DIBV5,"image/bmp",      "CF_DIBV5",               15);
        register_format(15, ContentFormat::FILE_LIST,  "text/uri-list",  "CF_HDROP",               10);
        register_format(6,  ContentFormat::IMAGE_TIFF, "image/tiff",     "CF_TIFF",                 5);
    }
};

// ====== Clipboard Data Integrity Verifier ======
class ClipboardDataVerifier {
    std::map<std::string, std::array<uint8_t, 32>> checksums_;
    mutable std::mutex mutex_;

public:
    // Store checksum for a key
    void store_checksum(const std::string& key, const std::vector<uint8_t>& data) {
        auto h = sha256::hash_bytes(data);
        std::lock_guard lk(mutex_);
        checksums_[key] = h;
    }

    // Verify data against stored checksum
    bool verify(const std::string& key, const std::vector<uint8_t>& data) const {
        std::lock_guard lk(mutex_);
        auto it = checksums_.find(key);
        if (it == checksums_.end()) {
            spdlog::warn("ClipboardDataVerifier: no checksum for '{}'", key);
            return true; // No checksum = trust
        }
        auto h = sha256::hash_bytes(data);
        bool match = (h == it->second);
        if (!match) {
            spdlog::error("ClipboardDataVerifier: checksum mismatch for '{}'", key);
        }
        return match;
    }

    // Verify text data
    bool verify_text(const std::string& key, const std::string& text) const {
        std::vector<uint8_t> data(text.begin(), text.end());
        return verify(key, data);
    }

    void remove(const std::string& key) {
        std::lock_guard lk(mutex_);
        checksums_.erase(key);
    }

    void clear() {
        std::lock_guard lk(mutex_);
        checksums_.clear();
    }

    size_t size() const {
        std::lock_guard lk(mutex_);
        return checksums_.size();
    }
};

// ====== MIME Type Sniffer ======
namespace mime_sniffer {

struct SniffResult {
    std::string mime_type;
    std::string extension;
    int confidence = 0; // 0-100
};

SniffResult sniff(const std::vector<uint8_t>& data) {
    if (data.empty()) return {"application/octet-stream", ".bin", 0};

    const auto& d = data;

    // Check magic bytes
    if (d.size() >= 8 &&
        d[0] == 0x89 && d[1] == 0x50 && d[2] == 0x4E && d[3] == 0x47 &&
        d[4] == 0x0D && d[5] == 0x0A && d[6] == 0x1A && d[7] == 0x0A) {
        return {"image/png", ".png", 100};
    }

    if (d.size() >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) {
        return {"image/jpeg", ".jpg", 100};
    }

    if (d.size() >= 6 && d[0] == 0x47 && d[1] == 0x49 && d[2] == 0x46 &&
        d[3] == 0x38 && (d[4] == 0x37 || d[4] == 0x39) && d[5] == 0x61) {
        return {"image/gif", ".gif", 100};
    }

    if (d.size() >= 2 && d[0] == 0x42 && d[1] == 0x4D) {
        return {"image/bmp", ".bmp", 100};
    }

    if (d.size() >= 4 && d[0] == 0x49 && d[1] == 0x49 && d[2] == 0x2A && d[3] == 0x00) {
        return {"image/tiff", ".tiff", 100};
    }
    if (d.size() >= 4 && d[0] == 0x4D && d[1] == 0x4D && d[2] == 0x00 && d[3] == 0x2A) {
        return {"image/tiff", ".tiff", 100};
    }

    if (d.size() >= 12 &&
        d[0] == 0x52 && d[1] == 0x49 && d[2] == 0x46 && d[3] == 0x46 &&
        d[8] == 0x57 && d[9] == 0x45 && d[10] == 0x42 && d[11] == 0x50) {
        return {"image/webp", ".webp", 100};
    }

    if (d.size() >= 4 && d[0] == 0x50 && d[1] == 0x4B &&
        d[2] == 0x03 && d[3] == 0x04) {
        return {"application/zip", ".zip", 100};
    }

    // Check for text content
    bool is_text = true;
    size_t printable = 0;
    size_t sample = std::min(d.size(), size_t(1024));
    for (size_t i = 0; i < sample; i++) {
        if ((d[i] >= 32 && d[i] <= 126) || d[i] == '\n' || d[i] == '\r' || d[i] == '\t') {
            printable++;
        }
    }
    if (printable > sample * 9 / 10) {
        // Check for HTML signature
        std::string head(reinterpret_cast<const char*>(d.data()),
            std::min(sample, size_t(100)));
        // Convert to lowercase for comparison
        std::string lower = head;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return std::tolower(c); });

        if (lower.find("<!doctype html") != std::string::npos ||
            lower.find("<html") != std::string::npos ||
            lower.find("<head") != std::string::npos) {
            return {"text/html", ".html", 90};
        }

        if (lower.find("{\\rtf") != std::string::npos) {
            return {"text/rtf", ".rtf", 90};
        }

        return {"text/plain", ".txt", 70};
    }

    return {"application/octet-stream", ".bin", 10};
}

} // namespace mime_sniffer

// ====== File URI Helpers ======
namespace file_uri {

// Comprehensive file:// URI to local path conversion
std::optional<std::string> uri_to_path(const std::string& uri) {
    if (uri.empty()) return std::nullopt;

    std::string s = uri;

    // Strip file:// prefix
    if (s.find("file://") == 0) {
        s = s.substr(7);
    } else if (s.find("file:") == 0) {
        s = s.substr(5);
    } else {
        // Not a file URI
        return std::nullopt;
    }

    // Handle Windows paths: file:///C:/path
#ifdef _WIN32
    if (s.size() >= 3 && s[0] == '/' && std::isalpha(s[1]) && s[2] == ':') {
        s = s.substr(1); // Remove leading /
    }
#else
    // Handle hostname: file://hostname/path
    if (!s.empty() && s[0] == '/') {
        // Check if second component looks like a hostname (contains no / after it)
        auto second_slash = s.find('/', 1);
        if (second_slash == std::string::npos) {
            // Just "//hostname" — no path
            return std::nullopt;
        }
        std::string host_part = s.substr(0, second_slash);
        // If hostname is not "localhost" or empty, strip it
        if (!host_part.empty() && host_part != "/localhost") {
            s = s.substr(second_slash);
        }
    }
#endif

    // URL decode (comprehensive)
    std::string decoded;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = {s[i + 1], s[i + 2], 0};
            char* end = nullptr;
            long val = strtol(hex, &end, 16);
            if (end && *end == 0 && val > 0) {
                decoded += static_cast<char>(val);
                i += 2;
                continue;
            }
        }
        decoded += s[i];
    }

    if (decoded.empty()) return std::nullopt;
    return decoded;
}

// Local path to file:// URI
std::string path_to_uri(const std::string& path) {
    std::ostringstream oss;
    oss << "file://";
#ifdef _WIN32
    oss << "/"; // Windows: file:///C:/...
#endif
    for (char c : path) {
        if (c == ' ') oss << "%20";
        else if (c == '#') oss << "%23";
        else if (c == '%') oss << "%25";
        else if (c == '?') oss << "%3F";
        else if (c == '&') oss << "%26";
        else if (c == '=') oss << "%3D";
        else if (c == '+') oss << "%2B";
        else if (static_cast<unsigned char>(c) < 32 ||
                 static_cast<unsigned char>(c) > 126) {
            oss << '%' << std::hex << std::uppercase
                << std::setw(2) << std::setfill('0')
                << static_cast<int>(static_cast<unsigned char>(c));
        } else {
            oss << c;
        }
    }
    return oss.str();
}

// Convert a list of file URIs to local paths
std::vector<std::string> uris_to_paths(const std::vector<std::string>& uris) {
    std::vector<std::string> paths;
    for (const auto& uri : uris) {
        auto path = uri_to_path(uri);
        if (path.has_value()) {
            paths.push_back(path.value());
        } else {
            // Try as-is (might already be a path)
            paths.push_back(uri);
        }
    }
    return paths;
}

// Convert local paths to file URIs
std::vector<std::string> paths_to_uris(const std::vector<std::string>& paths) {
    std::vector<std::string> uris;
    for (const auto& p : paths) {
        uris.push_back(path_to_uri(p));
    }
    return uris;
}

} // namespace file_uri

// ====== Clipboard Content Builder ======
// Fluent builder for ClipboardContentDescriptor
class ClipboardContentBuilder {
    ClipboardContentDescriptor desc_;

public:
    ClipboardContentBuilder& with_text(const std::string& text) {
        desc_.text = text;
        desc_.available_formats = desc_.available_formats | ContentFormat::TEXT;
        desc_.text_hash = ContentDeduplicator::compute_text_hash(text);
        return *this;
    }

    ClipboardContentBuilder& with_html(const std::string& html) {
        desc_.html = html;
        desc_.available_formats = desc_.available_formats | ContentFormat::HTML;
        return *this;
    }

    ClipboardContentBuilder& with_rtf(const std::string& rtf) {
        desc_.rtf = rtf;
        desc_.available_formats = desc_.available_formats | ContentFormat::RTF;
        return *this;
    }

    ClipboardContentBuilder& with_image(const std::vector<uint8_t>& data,
                                         const std::string& format = "png") {
        desc_.image_data = data;
        desc_.image_format = format;
        if (format == "png" || format.empty())
            desc_.available_formats = desc_.available_formats | ContentFormat::IMAGE_PNG;
        else if (format == "tiff")
            desc_.available_formats = desc_.available_formats | ContentFormat::IMAGE_TIFF;
        else if (format == "bmp" || format == "dib")
            desc_.available_formats = desc_.available_formats | ContentFormat::IMAGE_DIB;
        desc_.image_hash = ContentDeduplicator::compute_data_hash(data);
        return *this;
    }

    ClipboardContentBuilder& with_files(const std::vector<std::string>& files) {
        desc_.file_list = files;
        desc_.available_formats = desc_.available_formats | ContentFormat::FILE_LIST;
        desc_.file_hash = ContentDeduplicator::compute_file_hash(files);
        return *this;
    }

    ClipboardContentBuilder& with_uris(const std::vector<std::string>& uris) {
        desc_.uri_list = uris;
        desc_.available_formats = desc_.available_formats | ContentFormat::URI_LIST;
        return *this;
    }

    ClipboardContentBuilder& with_sequence(uint32_t seq) {
        desc_.sequence_number = seq;
        return *this;
    }

    ClipboardContentBuilder& with_source(const std::string& source) {
        desc_.source_application = source;
        return *this;
    }

    ClipboardContentDescriptor build() {
        desc_.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return desc_;
    }

    static ClipboardContentBuilder create() {
        return ClipboardContentBuilder();
    }
};

// ====== Clipboard Batch Operations ======
// Set multiple formats atomically on the clipboard
class ClipboardBatchWriter {
    std::vector<std::pair<std::string, std::function<bool()>>> operations_;
    PlatformClipboard* clipboard_;

public:
    explicit ClipboardBatchWriter(PlatformClipboard* cb) : clipboard_(cb) {}

    void add_text(const std::string& text) {
        operations_.emplace_back("text", [this, text]() {
            return clipboard_->set_text(text);
        });
    }

    void add_html(const std::string& html) {
        operations_.emplace_back("html", [this, html]() {
            return clipboard_->set_html(html);
        });
    }

    void add_image(const std::vector<uint8_t>& data, const std::string& fmt = "png") {
        operations_.emplace_back("image/" + fmt, [this, data, fmt]() {
            return clipboard_->set_image(data, fmt);
        });
    }

    void add_file_list(const std::vector<std::string>& files) {
        operations_.emplace_back("files", [this, files]() {
            return clipboard_->set_file_list(files);
        });
    }

    // Execute all operations, returns count of successful operations
    size_t commit() {
        size_t successes = 0;
        for (auto& [name, op] : operations_) {
            try {
                if (op()) {
                    successes++;
                    spdlog::debug("ClipboardBatchWriter: '{}' committed OK", name);
                } else {
                    spdlog::warn("ClipboardBatchWriter: '{}' failed", name);
                }
            } catch (const std::exception& e) {
                spdlog::error("ClipboardBatchWriter: '{}' threw: {}", name, e.what());
            }
        }
        operations_.clear();
        return successes;
    }

    void clear() { operations_.clear(); }
    size_t pending() const { return operations_.size(); }
};

// ====== Clipboard Transfer Metrics ======
struct ClipboardTransferMetrics {
    uint64_t transfer_count = 0;
    uint64_t total_bytes_transferred = 0;
    uint64_t peak_bytes_per_second = 0;
    uint64_t total_errors = 0;
    std::chrono::steady_clock::time_point last_transfer;

    void record_transfer(uint64_t bytes, bool success) {
        transfer_count++;
        if (success) {
            total_bytes_transferred += bytes;
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration<double>(now - last_transfer).count();
            if (elapsed > 0.001) {
                uint64_t bps = static_cast<uint64_t>(bytes / elapsed);
                if (bps > peak_bytes_per_second) peak_bytes_per_second = bps;
            }
            last_transfer = now;
        } else {
            total_errors++;
        }
    }

    double average_bytes_per_transfer() const {
        if (transfer_count == 0) return 0.0;
        return static_cast<double>(total_bytes_transferred) / transfer_count;
    }
};

} // namespace clipboard
