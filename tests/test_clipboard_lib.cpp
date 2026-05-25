#include <gtest/gtest.h>
#include "clipboard/clipboard.hpp"

#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <filesystem>
#include <array>
#include <cstring>
#include <mutex>
#include <memory>
#include <functional>
#include <optional>
#include <algorithm>
#include <set>
#include <map>

using namespace clipboard;

// =============================================================================
// Mock / Stub Implementations for Abstract Classes
// =============================================================================

// Mock for DelayedRenderer
class MockDelayedRenderer : public DelayedRenderer {
public:
    std::vector<uint8_t> render(ContentFormat format) override {
        render_calls++;
        last_format = format;
        if (render_data.empty()) {
            return {1, 2, 3, 4};
        }
        return render_data;
    }
    bool can_render(ContentFormat format) const override {
        can_render_calls++;
        return (format & supported_formats) != static_cast<ContentFormat>(0);
    }
    void release() override {
        released = true;
    }

    int render_calls = 0;
    mutable int can_render_calls = 0;
    ContentFormat last_format = static_cast<ContentFormat>(0);
    std::vector<uint8_t> render_data;
    ContentFormat supported_formats = ContentFormat::TEXT;
    bool released = false;
};

// Mock for CliprdrServiceContext
class MockCliprdrServiceContext : public CliprdrServiceContext {
public:
    bool set_stopped() override {
        stopped = true;
        return true;
    }
    bool empty_clipboard(int32_t conn_id) override {
        empty_clipboard_calls++;
        last_conn_id = conn_id;
        return empty_clipboard_result;
    }
    bool server_clip_file(int32_t conn_id, const ClipboardFile& msg) override {
        server_clip_file_calls++;
        last_conn_id = conn_id;
        last_file_msg = msg;
        return server_clip_file_result;
    }
    std::optional<ProgressPercent> get_progress() override {
        if (return_progress) {
            return ProgressPercent{};
        }
        return std::nullopt;
    }
    void cancel() override { canceled = true; }
    bool is_connected() const override { return connected; }
    int32_t connection_count() const override { return conn_count; }

    bool stopped = false;
    int empty_clipboard_calls = 0;
    int server_clip_file_calls = 0;
    int32_t last_conn_id = 0;
    ClipboardFile last_file_msg;
    bool empty_clipboard_result = true;
    bool server_clip_file_result = true;
    bool return_progress = false;
    bool canceled = false;
    bool connected = true;
    int32_t conn_count = 1;
};

// Mock for PlatformClipboard
class MockPlatformClipboard : public PlatformClipboard {
public:
    std::string get_text() override {
        text_get_calls++;
        return text_value;
    }
    bool set_text(const std::string& text) override {
        text_set_calls++;
        text_value = text;
        return text_set_result;
    }
    bool has_text() override {
        has_text_calls++;
        return has_text_result;
    }

    std::vector<std::string> get_file_list() override {
        file_get_calls++;
        return file_list_value;
    }
    bool set_file_list(const std::vector<std::string>& paths) override {
        file_set_calls++;
        file_list_value = paths;
        return file_set_result;
    }
    bool has_file_list() override {
        has_file_list_calls++;
        return has_file_list_result;
    }

    std::vector<uint8_t> get_image(const std::string& format) override {
        image_get_calls++;
        last_image_format = format;
        return image_data;
    }
    bool set_image(const std::vector<uint8_t>& data,
        const std::string& format) override {
        image_set_calls++;
        image_data = data;
        last_image_format = format;
        return image_set_result;
    }

    std::string get_html() override {
        html_get_calls++;
        return html_value;
    }
    bool set_html(const std::string& html) override {
        html_set_calls++;
        html_value = html;
        return html_set_result;
    }

    std::string get_rtf() override {
        rtf_get_calls++;
        return rtf_value;
    }
    bool set_rtf(const std::string& rtf) override {
        rtf_set_calls++;
        rtf_value = rtf;
        return rtf_set_result;
    }

    std::vector<std::string> get_uri_list() override {
        uri_get_calls++;
        return uri_list_value;
    }
    bool set_uri_list(const std::vector<std::string>& uris) override {
        uri_set_calls++;
        uri_list_value = uris;
        return uri_set_result;
    }

    bool clear() override {
        clear_calls++;
        text_value.clear();
        html_value.clear();
        rtf_value.clear();
        image_data.clear();
        file_list_value.clear();
        uri_list_value.clear();
        return clear_result;
    }

    bool owns_clipboard() override {
        owns_calls++;
        return owns_result;
    }

    void set_on_change(OnChange cb) override {
        change_cb = cb;
    }

    bool enable_delayed_rendering(std::shared_ptr<DelayedRenderer> renderer) override {
        delayed_renderer = renderer;
        return enable_delayed_rendering_result;
    }
    void disable_delayed_rendering() override {
        delayed_renderer.reset();
    }

    ContentFormat available_formats() override {
        available_formats_calls++;
        return available_formats_value;
    }

    ClipboardContentDescriptor get_content_descriptor() override {
        desc_get_calls++;
        return descriptor_value;
    }

    bool set_content(const ClipboardContentDescriptor& content) override {
        content_set_calls++;
        last_content_set = content;
        return content_set_result;
    }

    int64_t get_change_count() override {
        change_count_calls++;
        return change_count_value;
    }

    // Test control fields
    int text_get_calls = 0;
    int text_set_calls = 0;
    int has_text_calls = 0;
    int file_get_calls = 0;
    int file_set_calls = 0;
    int has_file_list_calls = 0;
    int image_get_calls = 0;
    int image_set_calls = 0;
    int html_get_calls = 0;
    int html_set_calls = 0;
    int rtf_get_calls = 0;
    int rtf_set_calls = 0;
    int uri_get_calls = 0;
    int uri_set_calls = 0;
    int clear_calls = 0;
    int owns_calls = 0;
    int available_formats_calls = 0;
    int desc_get_calls = 0;
    int content_set_calls = 0;
    int change_count_calls = 0;

    std::string text_value;
    std::vector<std::string> file_list_value;
    std::vector<uint8_t> image_data;
    std::string html_value;
    std::string rtf_value;
    std::vector<std::string> uri_list_value;
    std::string last_image_format;

    ClipboardContentDescriptor descriptor_value;
    ClipboardContentDescriptor last_content_set;
    ContentFormat available_formats_value = static_cast<ContentFormat>(0);
    int64_t change_count_value = 0;

    bool text_set_result = true;
    bool has_text_result = false;
    bool file_set_result = true;
    bool has_file_list_result = false;
    bool image_set_result = true;
    bool html_set_result = true;
    bool rtf_set_result = true;
    bool uri_set_result = true;
    bool clear_result = true;
    bool owns_result = true;
    bool content_set_result = true;
    bool enable_delayed_rendering_result = true;

    OnChange change_cb;
    std::shared_ptr<DelayedRenderer> delayed_renderer;
};

// =============================================================================
// ContentFormat Enum Tests
// =============================================================================

TEST(ContentFormatTest, EnumValues) {
    EXPECT_NE(ContentFormat::TEXT, ContentFormat::HTML);
    EXPECT_NE(ContentFormat::TEXT, ContentFormat::RTF);
    EXPECT_NE(ContentFormat::TEXT, ContentFormat::IMAGE_PNG);
    EXPECT_NE(ContentFormat::TEXT, ContentFormat::IMAGE_TIFF);
    EXPECT_NE(ContentFormat::TEXT, ContentFormat::IMAGE_BMP);
    EXPECT_NE(ContentFormat::TEXT, ContentFormat::IMAGE_DIB);
    EXPECT_NE(ContentFormat::TEXT, ContentFormat::IMAGE_DIBV5);
    EXPECT_NE(ContentFormat::TEXT, ContentFormat::FILE_LIST);
    EXPECT_NE(ContentFormat::TEXT, ContentFormat::URI_LIST);
}

TEST(ContentFormatTest, BitwiseValues) {
    EXPECT_EQ(static_cast<uint32_t>(ContentFormat::TEXT), 1);
    EXPECT_EQ(static_cast<uint32_t>(ContentFormat::HTML), 2);
    EXPECT_EQ(static_cast<uint32_t>(ContentFormat::RTF), 4);
    EXPECT_EQ(static_cast<uint32_t>(ContentFormat::IMAGE_PNG), 8);
    EXPECT_EQ(static_cast<uint32_t>(ContentFormat::IMAGE_TIFF), 16);
    EXPECT_EQ(static_cast<uint32_t>(ContentFormat::IMAGE_BMP), 32);
    EXPECT_EQ(static_cast<uint32_t>(ContentFormat::IMAGE_DIB), 64);
    EXPECT_EQ(static_cast<uint32_t>(ContentFormat::IMAGE_DIBV5), 128);
    EXPECT_EQ(static_cast<uint32_t>(ContentFormat::FILE_LIST), 256);
    EXPECT_EQ(static_cast<uint32_t>(ContentFormat::URI_LIST), 512);
}

TEST(ContentFormatTest, OrOperator) {
    ContentFormat tf = ContentFormat::TEXT | ContentFormat::HTML;
    EXPECT_EQ(static_cast<uint32_t>(tf), 3);

    ContentFormat all_img = ContentFormat::IMAGE_PNG | ContentFormat::IMAGE_TIFF |
        ContentFormat::IMAGE_BMP | ContentFormat::IMAGE_DIB | ContentFormat::IMAGE_DIBV5;
    EXPECT_EQ(static_cast<uint32_t>(all_img),
        static_cast<uint32_t>(ContentFormat::ALL_IMAGE));
}

TEST(ContentFormatTest, AndOperator) {
    ContentFormat tf = ContentFormat::TEXT | ContentFormat::HTML;
    ContentFormat result = tf & ContentFormat::TEXT;
    EXPECT_NE(static_cast<uint32_t>(result), 0u);

    result = tf & ContentFormat::RTF;
    EXPECT_EQ(static_cast<uint32_t>(result), 0u);
}

TEST(ContentFormatTest, NotOperator) {
    ContentFormat none = static_cast<ContentFormat>(0);
    EXPECT_TRUE(!none);

    EXPECT_FALSE(!ContentFormat::TEXT);
    EXPECT_FALSE(!ContentFormat::ALL);
}

TEST(ContentFormatTest, CombinedEnums) {
    uint32_t all_text = static_cast<uint32_t>(ContentFormat::ALL_TEXT);
    EXPECT_EQ(all_text, 1 | 2 | 4);

    uint32_t all_image = static_cast<uint32_t>(ContentFormat::ALL_IMAGE);
    EXPECT_EQ(all_image, 8 | 16 | 32 | 64 | 128);

    uint32_t all = static_cast<uint32_t>(ContentFormat::ALL);
    EXPECT_EQ(all, all_text | all_image | 256 | 512);
}

TEST(ContentFormatTest, ChainedOperators) {
    ContentFormat fmt = ContentFormat::TEXT | ContentFormat::HTML | ContentFormat::FILE_LIST;
    EXPECT_NE(static_cast<uint32_t>(fmt), 0u);

    ContentFormat filtered = fmt & ContentFormat::ALL_TEXT;
    EXPECT_NE(static_cast<uint32_t>(filtered), 0u);
    EXPECT_EQ(static_cast<uint32_t>(filtered),
        static_cast<uint32_t>(ContentFormat::TEXT | ContentFormat::HTML));
}

TEST(ContentFormatTest, OperatorWithAll) {
    ContentFormat all = ContentFormat::ALL;
    ContentFormat result = all & ContentFormat::TEXT;
    EXPECT_EQ(static_cast<uint32_t>(result), static_cast<uint32_t>(ContentFormat::TEXT));

    result = ContentFormat::TEXT & ContentFormat::ALL;
    EXPECT_EQ(static_cast<uint32_t>(result), static_cast<uint32_t>(ContentFormat::TEXT));
}

TEST(ContentFormatTest, OrAssignment) {
    ContentFormat fmt = ContentFormat::TEXT;
    fmt = fmt | ContentFormat::HTML;
    EXPECT_EQ(static_cast<uint32_t>(fmt), 3);
    fmt = fmt | ContentFormat::FILE_LIST;
    EXPECT_EQ(static_cast<uint32_t>(fmt), 3 | 256);
}

// =============================================================================
// ContentHash Tests
// =============================================================================

TEST(ContentHashTest, DefaultZeroInitialized) {
    ContentHash hash{};
    for (auto byte : hash) {
        EXPECT_EQ(byte, 0);
    }
}

TEST(ContentHashTest, SizeIs32) {
    ContentHash hash{};
    EXPECT_EQ(hash.size(), 32u);
}

TEST(ContentHashTest, HashToStringNonEmpty) {
    ContentHash hash{};
    hash[0] = 0xAA;
    hash[1] = 0xBB;
    hash[31] = 0xFF;
    auto sv = hash_to_string(hash);
    EXPECT_EQ(sv.size(), 32u);
    // First two and last byte should match
    EXPECT_EQ(static_cast<uint8_t>(sv[0]), 0xAA);
    EXPECT_EQ(static_cast<uint8_t>(sv[1]), 0xBB);
    EXPECT_EQ(static_cast<uint8_t>(sv[31]), 0xFF);
}

TEST(ContentHashTest, HashToStringConstexpr) {
    ContentHash hash{};
    auto sv = hash_to_string(hash);
    EXPECT_EQ(sv.size(), 32u);
    for (size_t i = 0; i < sv.size(); i++) {
        EXPECT_EQ(static_cast<uint8_t>(sv[i]), 0);
    }
}

TEST(ContentHashTest, Comparison) {
    ContentHash a{};
    ContentHash b{};
    EXPECT_EQ(a, b);

    a[0] = 1;
    b[0] = 1;
    EXPECT_EQ(a, b);

    b[0] = 2;
    EXPECT_NE(a, b);
}

TEST(ContentHashTest, Assignment) {
    ContentHash a{};
    a[0] = 42;
    a[15] = 128;
    a[31] = 255;

    ContentHash b = a;
    EXPECT_EQ(b, a);
    EXPECT_EQ(b[0], 42);
    EXPECT_EQ(b[15], 128);
    EXPECT_EQ(b[31], 255);
}

TEST(ContentHashTest, FullRange) {
    ContentHash hash{};
    for (size_t i = 0; i < 32; i++) {
        hash[i] = static_cast<uint8_t>(i * 7 + 13);
    }
    auto sv = hash_to_string(hash);
    for (size_t i = 0; i < 32; i++) {
        EXPECT_EQ(static_cast<uint8_t>(sv[i]), static_cast<uint8_t>(i * 7 + 13));
    }
}

// =============================================================================
// ClipboardContentDescriptor Tests
// =============================================================================

TEST(ClipboardContentDescriptorTest, DefaultConstruction) {
    ClipboardContentDescriptor desc;
    EXPECT_EQ(desc.available_formats, static_cast<ContentFormat>(0));
    EXPECT_TRUE(desc.text.empty());
    EXPECT_TRUE(desc.html.empty());
    EXPECT_TRUE(desc.rtf.empty());
    EXPECT_TRUE(desc.image_data.empty());
    EXPECT_TRUE(desc.image_format.empty());
    EXPECT_TRUE(desc.file_list.empty());
    EXPECT_TRUE(desc.uri_list.empty());
    EXPECT_EQ(desc.timestamp_ms, 0);
    EXPECT_EQ(desc.sequence_number, 0u);
    EXPECT_FALSE(desc.is_delayed_rendered);
    EXPECT_TRUE(desc.source_application.empty());
}

TEST(ClipboardContentDescriptorTest, ClearMethod) {
    ClipboardContentDescriptor desc;
    desc.text = "Hello";
    desc.html = "<html>";
    desc.rtf = "{\\rtf1";
    desc.image_data = {0x89, 0x50, 0x4E, 0x47};
    desc.image_format = "png";
    desc.file_list = {"file1.txt"};
    desc.uri_list = {"http://example.com"};
    desc.timestamp_ms = 123456;
    desc.sequence_number = 5;
    desc.is_delayed_rendered = true;
    desc.source_application = "TestApp";
    desc.available_formats = ContentFormat::TEXT | ContentFormat::HTML;

    desc.clear();

    EXPECT_EQ(desc.available_formats, static_cast<ContentFormat>(0));
    EXPECT_TRUE(desc.text.empty());
    EXPECT_TRUE(desc.html.empty());
    EXPECT_TRUE(desc.rtf.empty());
    EXPECT_TRUE(desc.image_data.empty());
    EXPECT_TRUE(desc.image_format.empty());
    EXPECT_TRUE(desc.file_list.empty());
    EXPECT_TRUE(desc.uri_list.empty());
    EXPECT_EQ(desc.timestamp_ms, 0);
    EXPECT_EQ(desc.sequence_number, 0u);
    EXPECT_FALSE(desc.is_delayed_rendered);
    EXPECT_TRUE(desc.source_application.empty());
}

TEST(ClipboardContentDescriptorTest, HasFormatBasic) {
    ClipboardContentDescriptor desc;
    EXPECT_FALSE(desc.has_format(ContentFormat::TEXT));
    EXPECT_FALSE(desc.has_format(ContentFormat::HTML));
    EXPECT_FALSE(desc.has_format(ContentFormat::FILE_LIST));

    desc.available_formats = ContentFormat::TEXT;
    EXPECT_TRUE(desc.has_format(ContentFormat::TEXT));
    EXPECT_FALSE(desc.has_format(ContentFormat::HTML));
    EXPECT_FALSE(desc.has_format(ContentFormat::FILE_LIST));
}

TEST(ClipboardContentDescriptorTest, HasFormatMultiple) {
    ClipboardContentDescriptor desc;
    desc.available_formats = ContentFormat::TEXT | ContentFormat::HTML | ContentFormat::FILE_LIST;
    EXPECT_TRUE(desc.has_format(ContentFormat::TEXT));
    EXPECT_TRUE(desc.has_format(ContentFormat::HTML));
    EXPECT_TRUE(desc.has_format(ContentFormat::FILE_LIST));
    EXPECT_FALSE(desc.has_format(ContentFormat::RTF));
    EXPECT_FALSE(desc.has_format(ContentFormat::IMAGE_PNG));
    EXPECT_FALSE(desc.has_format(ContentFormat::URI_LIST));
}

TEST(ClipboardContentDescriptorTest, HasFormatAllFormats) {
    ClipboardContentDescriptor desc;
    desc.available_formats = ContentFormat::ALL;
    EXPECT_TRUE(desc.has_format(ContentFormat::TEXT));
    EXPECT_TRUE(desc.has_format(ContentFormat::HTML));
    EXPECT_TRUE(desc.has_format(ContentFormat::RTF));
    EXPECT_TRUE(desc.has_format(ContentFormat::IMAGE_PNG));
    EXPECT_TRUE(desc.has_format(ContentFormat::IMAGE_TIFF));
    EXPECT_TRUE(desc.has_format(ContentFormat::IMAGE_BMP));
    EXPECT_TRUE(desc.has_format(ContentFormat::IMAGE_DIB));
    EXPECT_TRUE(desc.has_format(ContentFormat::IMAGE_DIBV5));
    EXPECT_TRUE(desc.has_format(ContentFormat::FILE_LIST));
    EXPECT_TRUE(desc.has_format(ContentFormat::URI_LIST));
}

TEST(ClipboardContentDescriptorTest, DescribeEmpty) {
    ClipboardContentDescriptor desc;
    std::string d = desc.describe();
    EXPECT_FALSE(d.empty());
    EXPECT_NE(d.find("ClipboardContent"), std::string::npos);
}

TEST(ClipboardContentDescriptorTest, DescribeWithText) {
    ClipboardContentDescriptor desc;
    desc.available_formats = ContentFormat::TEXT;
    desc.sequence_number = 42;
    std::string d = desc.describe();
    EXPECT_NE(d.find("TEXT"), std::string::npos);
    EXPECT_NE(d.find("seq=42"), std::string::npos);
}

TEST(ClipboardContentDescriptorTest, DescribeWithMultipleFormats) {
    ClipboardContentDescriptor desc;
    desc.available_formats = ContentFormat::TEXT | ContentFormat::HTML | ContentFormat::FILE_LIST;
    desc.file_list = {"a.txt", "b.txt"};
    std::string d = desc.describe();
    EXPECT_NE(d.find("TEXT"), std::string::npos);
    EXPECT_NE(d.find("HTML"), std::string::npos);
    EXPECT_NE(d.find("FILES"), std::string::npos);
}

TEST(ClipboardContentDescriptorTest, DescribeWithImage) {
    ClipboardContentDescriptor desc;
    desc.available_formats = ContentFormat::IMAGE_PNG;
    std::string d = desc.describe();
    EXPECT_NE(d.find("PNG"), std::string::npos);
}

TEST(ClipboardContentDescriptorTest, DescribeWithUris) {
    ClipboardContentDescriptor desc;
    desc.available_formats = ContentFormat::URI_LIST;
    desc.uri_list = {"http://a", "http://b", "http://c"};
    std::string d = desc.describe();
    EXPECT_NE(d.find("URIS"), std::string::npos);
    EXPECT_NE(d.find("3"), std::string::npos);
}

TEST(ClipboardContentDescriptorTest, DescribeAllFormats) {
    ClipboardContentDescriptor desc;
    desc.available_formats = ContentFormat::ALL;
    std::string d = desc.describe();
    EXPECT_NE(d.find("TEXT"), std::string::npos);
    EXPECT_NE(d.find("HTML"), std::string::npos);
    EXPECT_NE(d.find("RTF"), std::string::npos);
    EXPECT_NE(d.find("PNG"), std::string::npos);
    EXPECT_NE(d.find("TIFF"), std::string::npos);
    EXPECT_NE(d.find("BMP"), std::string::npos);
    EXPECT_NE(d.find("DIB"), std::string::npos);
    EXPECT_NE(d.find("DIBV5"), std::string::npos);
}

TEST(ClipboardContentDescriptorTest, CopyAndAssignment) {
    ClipboardContentDescriptor a;
    a.text = "copy test";
    a.html = "<p>copy</p>";
    a.available_formats = ContentFormat::TEXT | ContentFormat::HTML;
    a.sequence_number = 7;

    ClipboardContentDescriptor b = a;
    EXPECT_EQ(b.text, "copy test");
    EXPECT_EQ(b.html, "<p>copy</p>");
    EXPECT_EQ(b.available_formats, a.available_formats);
    EXPECT_EQ(b.sequence_number, 7u);

    ClipboardContentDescriptor c;
    c = a;
    EXPECT_EQ(c.text, "copy test");
    EXPECT_EQ(c.html, "<p>copy</p>");
}

TEST(ClipboardContentDescriptorTest, HashFieldsClearOnClear) {
    ClipboardContentDescriptor desc;
    desc.text_hash[0] = 0xFF;
    desc.image_hash[0] = 0xEE;
    desc.file_hash[0] = 0xDD;
    desc.clear();

    for (size_t i = 0; i < 32; i++) {
        EXPECT_EQ(desc.text_hash[i], 0);
        EXPECT_EQ(desc.image_hash[i], 0);
        EXPECT_EQ(desc.file_hash[i], 0);
    }
}

TEST(ClipboardContentDescriptorTest, LargeDataInit) {
    ClipboardContentDescriptor desc;
    desc.text = std::string(10000, 'A');
    desc.html = std::string(5000, '<');
    desc.image_data.resize(1000000, 0x42);
    desc.file_list.resize(500, "test_file.txt");

    EXPECT_EQ(desc.text.size(), 10000u);
    EXPECT_EQ(desc.html.size(), 5000u);
    EXPECT_EQ(desc.image_data.size(), 1000000u);
    EXPECT_EQ(desc.file_list.size(), 500u);

    desc.clear();
    EXPECT_TRUE(desc.text.empty());
    EXPECT_TRUE(desc.html.empty());
    EXPECT_TRUE(desc.image_data.empty());
    EXPECT_TRUE(desc.file_list.empty());
}

// =============================================================================
// CliprdrError Tests
// =============================================================================

TEST(CliprdrErrorTest, AllValuesDefined) {
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::None), 0u);
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::CliprdrName), 1u);
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::CliprdrInit), 2u);
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::CliprdrOutOfMemory), 3u);
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::ClipboardInternalError), 4u);
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::ClipboardOccupied), 5u);
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::ConversionFailure), 6u);
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::OpenClipboard), 7u);
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::FileError), 8u);
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::InvalidRequest), 9u);
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::CommonError), 10u);
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::FormatNotAvailable), 11u);
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::RenderingFailed), 12u);
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::TransferInProgress), 13u);
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::Timeout), 14u);
    EXPECT_EQ(static_cast<uint32_t>(CliprdrError::Unknown), 99u);
}

TEST(CliprdrErrorTest, ErrorStringNotEmpty) {
    EXPECT_NE(cliprdr_error_string(CliprdrError::None), nullptr);
    EXPECT_NE(cliprdr_error_string(CliprdrError::CliprdrName), nullptr);
    EXPECT_NE(cliprdr_error_string(CliprdrError::CliprdrInit), nullptr);
    EXPECT_NE(cliprdr_error_string(CliprdrError::CliprdrOutOfMemory), nullptr);
    EXPECT_NE(cliprdr_error_string(CliprdrError::ClipboardInternalError), nullptr);
    EXPECT_NE(cliprdr_error_string(CliprdrError::ClipboardOccupied), nullptr);
    EXPECT_NE(cliprdr_error_string(CliprdrError::ConversionFailure), nullptr);
    EXPECT_NE(cliprdr_error_string(CliprdrError::OpenClipboard), nullptr);
    EXPECT_NE(cliprdr_error_string(CliprdrError::FileError), nullptr);
    EXPECT_NE(cliprdr_error_string(CliprdrError::InvalidRequest), nullptr);
    EXPECT_NE(cliprdr_error_string(CliprdrError::CommonError), nullptr);
    EXPECT_NE(cliprdr_error_string(CliprdrError::FormatNotAvailable), nullptr);
    EXPECT_NE(cliprdr_error_string(CliprdrError::RenderingFailed), nullptr);
    EXPECT_NE(cliprdr_error_string(CliprdrError::TransferInProgress), nullptr);
    EXPECT_NE(cliprdr_error_string(CliprdrError::Timeout), nullptr);
    EXPECT_NE(cliprdr_error_string(CliprdrError::Unknown), nullptr);
}

TEST(CliprdrErrorTest, ErrorStringNonEmptyCString) {
    for (int i = 0; i <= 14; i++) {
        auto err = static_cast<CliprdrError>(i);
        const char* str = cliprdr_error_string(err);
        ASSERT_NE(str, nullptr);
        EXPECT_GT(strlen(str), 0u) << "Error value " << i << " has empty string";
    }
    EXPECT_GT(strlen(cliprdr_error_string(CliprdrError::Unknown)), 0u);
}

TEST(CliprdrErrorTest, ErrorStringDistinct) {
    std::set<const char*> strings;
    strings.insert(cliprdr_error_string(CliprdrError::None));
    strings.insert(cliprdr_error_string(CliprdrError::CliprdrName));
    strings.insert(cliprdr_error_string(CliprdrError::CommonError));
    strings.insert(cliprdr_error_string(CliprdrError::Timeout));
    strings.insert(cliprdr_error_string(CliprdrError::Unknown));
    // At least some should be distinct
    EXPECT_GE(strings.size(), 3u);
}

TEST(CliprdrErrorTest, CastRoundTrip) {
    CliprdrError err = CliprdrError::FileError;
    auto val = static_cast<uint32_t>(err);
    auto back = static_cast<CliprdrError>(val);
    EXPECT_EQ(err, back);
}

// =============================================================================
// ClipboardFile Tests
// =============================================================================

TEST(ClipboardFileTest, TypeEnumValues) {
    EXPECT_EQ(ClipboardFile::NOTIFY, 0);
    EXPECT_EQ(ClipboardFile::REQUEST, 1);
    EXPECT_NE(ClipboardFile::DATA, ClipboardFile::NOTIFY);
    EXPECT_NE(ClipboardFile::DONE, ClipboardFile::CANCEL);
    EXPECT_NE(ClipboardFile::PROGRESS, ClipboardFile::ERROR);
}

TEST(ClipboardFileTest, DefaultConstruction) {
    ClipboardFile file;
    EXPECT_EQ(file.type, ClipboardFile::NOTIFY);
    EXPECT_TRUE(file.path.empty());
    EXPECT_EQ(file.size, 0u);
    EXPECT_TRUE(file.data.empty());
    EXPECT_FALSE(file.is_dir);
    EXPECT_EQ(file.conn_id, 0);
    EXPECT_EQ(file.offset, 0u);
    EXPECT_EQ(file.bytes_transferred, 0u);
}

TEST(ClipboardFileTest, SetAllFields) {
    ClipboardFile file;
    file.type = ClipboardFile::DATA;
    file.path = "/tmp/test.dat";
    file.size = 1024;
    file.data = {0x00, 0x01, 0x02, 0x03};
    file.is_dir = false;
    file.conn_id = 42;
    file.offset = 512;
    file.bytes_transferred = 128;

    EXPECT_EQ(file.type, ClipboardFile::DATA);
    EXPECT_EQ(file.path, "/tmp/test.dat");
    EXPECT_EQ(file.size, 1024u);
    EXPECT_EQ(file.data.size(), 4u);
    EXPECT_FALSE(file.is_dir);
    EXPECT_EQ(file.conn_id, 42);
    EXPECT_EQ(file.offset, 512u);
    EXPECT_EQ(file.bytes_transferred, 128u);
}

TEST(ClipboardFileTest, DirectoryFile) {
    ClipboardFile file;
    file.is_dir = true;
    file.path = "/home/user/docs";
    EXPECT_TRUE(file.is_dir);
    EXPECT_EQ(file.path, "/home/user/docs");
}

TEST(ClipboardFileTest, CancelType) {
    ClipboardFile file;
    file.type = ClipboardFile::CANCEL;
    file.path = "/tmp/stale_transfer";
    EXPECT_EQ(file.type, ClipboardFile::CANCEL);
}

TEST(ClipboardFileTest, DoneType) {
    ClipboardFile file;
    file.type = ClipboardFile::DONE;
    EXPECT_EQ(file.type, ClipboardFile::DONE);
}

TEST(ClipboardFileTest, ProgressType) {
    ClipboardFile file;
    file.type = ClipboardFile::PROGRESS;
    file.bytes_transferred = 500;
    file.size = 1000;
    EXPECT_EQ(file.type, ClipboardFile::PROGRESS);
    EXPECT_EQ(file.bytes_transferred, 500u);
    EXPECT_EQ(file.size, 1000u);
}

TEST(ClipboardFileTest, ErrorType) {
    ClipboardFile file;
    file.type = ClipboardFile::ERROR;
    file.path = "Transfer failed";
    EXPECT_EQ(file.type, ClipboardFile::ERROR);
}

TEST(ClipboardFileTest, CopyConstruction) {
    ClipboardFile original;
    original.type = ClipboardFile::REQUEST;
    original.path = "/home/copy_test";
    original.size = 999;
    original.conn_id = 7;

    ClipboardFile copy = original;
    EXPECT_EQ(copy.type, ClipboardFile::REQUEST);
    EXPECT_EQ(copy.path, "/home/copy_test");
    EXPECT_EQ(copy.size, 999u);
    EXPECT_EQ(copy.conn_id, 7);

    // Modify copy, ensure original unchanged
    copy.path = "/tmp/modified";
    EXPECT_EQ(original.path, "/home/copy_test");
}

TEST(ClipboardFileTest, LargeDataBuffer) {
    ClipboardFile file;
    file.data.resize(1024 * 1024, 0xAB); // 1 MB
    EXPECT_EQ(file.data.size(), 1024u * 1024u);
    EXPECT_EQ(file.data[0], 0xAB);
    EXPECT_EQ(file.data[file.data.size() - 1], 0xAB);
}

// =============================================================================
// ProgressPercent Tests
// =============================================================================

TEST(ProgressPercentTest, DefaultValues) {
    ProgressPercent pp;
    EXPECT_DOUBLE_EQ(pp.percent, 0.0);
    EXPECT_FALSE(pp.is_canceled);
    EXPECT_FALSE(pp.is_failed);
    EXPECT_TRUE(pp.error_message.empty());
    EXPECT_EQ(pp.bytes_processed, 0u);
    EXPECT_EQ(pp.total_bytes, 0u);
}

TEST(ProgressPercentTest, HalfComplete) {
    ProgressPercent pp;
    pp.percent = 50.0;
    pp.bytes_processed = 500;
    pp.total_bytes = 1000;
    EXPECT_DOUBLE_EQ(pp.percent, 50.0);
    EXPECT_EQ(pp.bytes_processed, 500u);
    EXPECT_EQ(pp.total_bytes, 1000u);
}

TEST(ProgressPercentTest, FullComplete) {
    ProgressPercent pp;
    pp.percent = 100.0;
    pp.bytes_processed = 1000;
    pp.total_bytes = 1000;
    EXPECT_DOUBLE_EQ(pp.percent, 100.0);
}

TEST(ProgressPercentTest, Canceled) {
    ProgressPercent pp;
    pp.is_canceled = true;
    pp.percent = 30.0;
    EXPECT_TRUE(pp.is_canceled);
    EXPECT_DOUBLE_EQ(pp.percent, 30.0);
    EXPECT_FALSE(pp.is_failed);
}

TEST(ProgressPercentTest, Failed) {
    ProgressPercent pp;
    pp.is_failed = true;
    pp.error_message = "Transfer error: disconnected";
    EXPECT_TRUE(pp.is_failed);
    EXPECT_FALSE(pp.is_canceled);
    EXPECT_EQ(pp.error_message, "Transfer error: disconnected");
}

TEST(ProgressPercentTest, CanceledAndFailedMutuallyExclusive) {
    ProgressPercent pp;
    pp.is_canceled = true;
    pp.is_failed = true;
    // Both can be true (edge case), struct allows it
    EXPECT_TRUE(pp.is_canceled);
    EXPECT_TRUE(pp.is_failed);
}

TEST(ProgressPercentTest, ZeroTotalBytes) {
    ProgressPercent pp;
    pp.percent = 0.0;
    pp.bytes_processed = 0;
    pp.total_bytes = 0;
    EXPECT_DOUBLE_EQ(pp.percent, 0.0);
    EXPECT_EQ(pp.total_bytes, 0u);
}

TEST(ProgressPercentTest, LargeByteCounts) {
    ProgressPercent pp;
    pp.bytes_processed = UINT64_C(5000000000);
    pp.total_bytes = UINT64_C(10000000000);
    pp.percent = 50.0;
    EXPECT_EQ(pp.bytes_processed, UINT64_C(5000000000));
    EXPECT_EQ(pp.total_bytes, UINT64_C(10000000000));
}

TEST(ProgressPercentTest, CopyAndCompare) {
    ProgressPercent a;
    a.percent = 25.0;
    a.is_failed = true;
    a.error_message = "disk full";

    ProgressPercent b = a;
    EXPECT_DOUBLE_EQ(b.percent, 25.0);
    EXPECT_TRUE(b.is_failed);
    EXPECT_EQ(b.error_message, "disk full");

    b.percent = 100.0;
    EXPECT_NE(a.percent, b.percent);
}

// =============================================================================
// DelayedRenderer Mock Tests
// =============================================================================

TEST(DelayedRendererTest, BasicRender) {
    MockDelayedRenderer renderer;
    auto result = renderer.render(ContentFormat::TEXT);
    EXPECT_FALSE(result.empty());
    EXPECT_EQ(renderer.render_calls, 1);
    EXPECT_EQ(renderer.last_format, ContentFormat::TEXT);
}

TEST(DelayedRendererTest, CustomRenderData) {
    MockDelayedRenderer renderer;
    renderer.render_data = {0x10, 0x20, 0x30, 0x40, 0x50};
    auto result = renderer.render(ContentFormat::IMAGE_PNG);
    EXPECT_EQ(result, renderer.render_data);
}

TEST(DelayedRendererTest, CanRender) {
    MockDelayedRenderer renderer;
    renderer.supported_formats = ContentFormat::TEXT | ContentFormat::HTML;
    EXPECT_TRUE(renderer.can_render(ContentFormat::TEXT));
    EXPECT_TRUE(renderer.can_render(ContentFormat::HTML));
    EXPECT_FALSE(renderer.can_render(ContentFormat::IMAGE_PNG));
    EXPECT_FALSE(renderer.can_render(ContentFormat::FILE_LIST));
    EXPECT_EQ(renderer.can_render_calls, 4);
}

TEST(DelayedRendererTest, CanRenderAll) {
    MockDelayedRenderer renderer;
    renderer.supported_formats = ContentFormat::ALL;
    EXPECT_TRUE(renderer.can_render(ContentFormat::TEXT));
    EXPECT_TRUE(renderer.can_render(ContentFormat::HTML));
    EXPECT_TRUE(renderer.can_render(ContentFormat::RTF));
    EXPECT_TRUE(renderer.can_render(ContentFormat::IMAGE_PNG));
    EXPECT_TRUE(renderer.can_render(ContentFormat::IMAGE_TIFF));
    EXPECT_TRUE(renderer.can_render(ContentFormat::IMAGE_BMP));
    EXPECT_TRUE(renderer.can_render(ContentFormat::IMAGE_DIB));
    EXPECT_TRUE(renderer.can_render(ContentFormat::IMAGE_DIBV5));
    EXPECT_TRUE(renderer.can_render(ContentFormat::FILE_LIST));
    EXPECT_TRUE(renderer.can_render(ContentFormat::URI_LIST));
}

TEST(DelayedRendererTest, CanRenderNone) {
    MockDelayedRenderer renderer;
    renderer.supported_formats = static_cast<ContentFormat>(0);
    EXPECT_FALSE(renderer.can_render(ContentFormat::TEXT));
    EXPECT_FALSE(renderer.can_render(ContentFormat::HTML));
    EXPECT_FALSE(renderer.can_render(ContentFormat::FILE_LIST));
}

TEST(DelayedRendererTest, Release) {
    MockDelayedRenderer renderer;
    EXPECT_FALSE(renderer.released);
    renderer.release();
    EXPECT_TRUE(renderer.released);
}

TEST(DelayedRendererTest, MultipleRendersIncrementCounter) {
    MockDelayedRenderer renderer;
    renderer.render(ContentFormat::TEXT);
    renderer.render(ContentFormat::HTML);
    renderer.render(ContentFormat::RTF);
    EXPECT_EQ(renderer.render_calls, 3);
}

TEST(DelayedRendererTest, PolymorphicUsage) {
    auto renderer = std::make_shared<MockDelayedRenderer>();
    DelayedRenderer* base = renderer.get();
    EXPECT_TRUE(base->can_render(ContentFormat::TEXT));
    auto data = base->render(ContentFormat::TEXT);
    EXPECT_FALSE(data.empty());
    base->release();
    EXPECT_TRUE(renderer->released);
}

// =============================================================================
// CliprdrServiceContext Mock Tests
// =============================================================================

TEST(CliprdrServiceContextTest, SetStopped) {
    MockCliprdrServiceContext ctx;
    EXPECT_FALSE(ctx.stopped);
    bool result = ctx.set_stopped();
    EXPECT_TRUE(result);
    EXPECT_TRUE(ctx.stopped);
}

TEST(CliprdrServiceContextTest, EmptyClipboard) {
    MockCliprdrServiceContext ctx;
    bool result = ctx.empty_clipboard(42);
    EXPECT_TRUE(result);
    EXPECT_EQ(ctx.empty_clipboard_calls, 1);
    EXPECT_EQ(ctx.last_conn_id, 42);
}

TEST(CliprdrServiceContextTest, EmptyClipboardNegativeConnId) {
    MockCliprdrServiceContext ctx;
    bool result = ctx.empty_clipboard(-1);
    EXPECT_TRUE(result);
    EXPECT_EQ(ctx.last_conn_id, -1);
}

TEST(CliprdrServiceContextTest, EmptyClipboardFail) {
    MockCliprdrServiceContext ctx;
    ctx.empty_clipboard_result = false;
    EXPECT_FALSE(ctx.empty_clipboard(10));
}

TEST(CliprdrServiceContextTest, ServerClipFile) {
    MockCliprdrServiceContext ctx;
    ClipboardFile file;
    file.type = ClipboardFile::NOTIFY;
    file.path = "/tmp/test.file";
    file.size = 100;
    file.conn_id = 5;

    bool result = ctx.server_clip_file(5, file);
    EXPECT_TRUE(result);
    EXPECT_EQ(ctx.server_clip_file_calls, 1);
    EXPECT_EQ(ctx.last_conn_id, 5);
    EXPECT_EQ(ctx.last_file_msg.path, "/tmp/test.file");
    EXPECT_EQ(ctx.last_file_msg.size, 100u);
}

TEST(CliprdrServiceContextTest, ServerClipFileFail) {
    MockCliprdrServiceContext ctx;
    ctx.server_clip_file_result = false;
    ClipboardFile file;
    EXPECT_FALSE(ctx.server_clip_file(0, file));
}

TEST(CliprdrServiceContextTest, GetProgressNone) {
    MockCliprdrServiceContext ctx;
    ctx.return_progress = false;
    auto progress = ctx.get_progress();
    EXPECT_FALSE(progress.has_value());
}

TEST(CliprdrServiceContextTest, GetProgressSome) {
    MockCliprdrServiceContext ctx;
    ctx.return_progress = true;
    auto progress = ctx.get_progress();
    EXPECT_TRUE(progress.has_value());
}

TEST(CliprdrServiceContextTest, Cancel) {
    MockCliprdrServiceContext ctx;
    EXPECT_FALSE(ctx.canceled);
    ctx.cancel();
    EXPECT_TRUE(ctx.canceled);
}

TEST(CliprdrServiceContextTest, IsConnected) {
    MockCliprdrServiceContext ctx;
    EXPECT_TRUE(ctx.is_connected());
    ctx.connected = false;
    EXPECT_FALSE(ctx.is_connected());
}

TEST(CliprdrServiceContextTest, ConnectionCount) {
    MockCliprdrServiceContext ctx;
    EXPECT_EQ(ctx.connection_count(), 1);
    ctx.conn_count = 5;
    EXPECT_EQ(ctx.connection_count(), 5);
    ctx.conn_count = 0;
    EXPECT_EQ(ctx.connection_count(), 0);
}

TEST(CliprdrServiceContextTest, MultipleEmptyClipboard) {
    MockCliprdrServiceContext ctx;
    ctx.empty_clipboard(1);
    ctx.empty_clipboard(2);
    ctx.empty_clipboard(3);
    EXPECT_EQ(ctx.empty_clipboard_calls, 3);
    EXPECT_EQ(ctx.last_conn_id, 3);
}

// =============================================================================
// PlatformClipboard Mock Tests
// =============================================================================

TEST(PlatformClipboardTest, FactoryCreate) {
    auto cb = PlatformClipboard::create();
    EXPECT_NE(cb, nullptr);
}

TEST(PlatformClipboardTest, FactoryCreatesUnique) {
    auto cb1 = PlatformClipboard::create();
    auto cb2 = PlatformClipboard::create();
    EXPECT_NE(cb1.get(), cb2.get());
}

// --- Text Operations ---

TEST(PlatformClipboardTest, MockSetGetText) {
    MockPlatformClipboard cb;
    EXPECT_TRUE(cb.set_text("Hello, World!"));
    EXPECT_EQ(cb.get_text(), "Hello, World!");
    EXPECT_EQ(cb.text_set_calls, 1);
    EXPECT_EQ(cb.text_get_calls, 1);
}

TEST(PlatformClipboardTest, MockHasText) {
    MockPlatformClipboard cb;
    cb.has_text_result = true;
    EXPECT_TRUE(cb.has_text());

    cb.has_text_result = false;
    EXPECT_FALSE(cb.has_text());
}

TEST(PlatformClipboardTest, MockSetTextEmpty) {
    MockPlatformClipboard cb;
    EXPECT_TRUE(cb.set_text(""));
    EXPECT_EQ(cb.get_text(), "");
    EXPECT_TRUE(cb.text_value.empty());
}

TEST(PlatformClipboardTest, MockSetTextLong) {
    MockPlatformClipboard cb;
    std::string long_text(10000, 'X');
    EXPECT_TRUE(cb.set_text(long_text));
    EXPECT_EQ(cb.get_text().size(), 10000u);
}

TEST(PlatformClipboardTest, MockSetTextUnicode) {
    MockPlatformClipboard cb;
    std::string unicode = u8"Hello \u00e9\u00e0\u00fc\u00f1 World \u4e16\u754c";
    EXPECT_TRUE(cb.set_text(unicode));
    EXPECT_EQ(cb.get_text(), unicode);
}

// --- File Operations ---

TEST(PlatformClipboardTest, MockSetGetFileList) {
    MockPlatformClipboard cb;
    std::vector<std::string> files = {"/tmp/a.txt", "/tmp/b.txt"};
    EXPECT_TRUE(cb.set_file_list(files));
    EXPECT_EQ(cb.get_file_list(), files);
    EXPECT_EQ(cb.file_set_calls, 1);
    EXPECT_EQ(cb.file_get_calls, 1);
}

TEST(PlatformClipboardTest, MockHasFileList) {
    MockPlatformClipboard cb;
    cb.has_file_list_result = true;
    EXPECT_TRUE(cb.has_file_list());

    cb.has_file_list_result = false;
    EXPECT_FALSE(cb.has_file_list());
}

TEST(PlatformClipboardTest, MockSetFileListEmpty) {
    MockPlatformClipboard cb;
    EXPECT_TRUE(cb.set_file_list({}));
    EXPECT_TRUE(cb.get_file_list().empty());
}

TEST(PlatformClipboardTest, MockSetFileListSingle) {
    MockPlatformClipboard cb;
    EXPECT_TRUE(cb.set_file_list({"/home/user/readme.md"}));
    auto result = cb.get_file_list();
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "/home/user/readme.md");
}

// --- Image Operations ---

TEST(PlatformClipboardTest, MockSetGetImage) {
    MockPlatformClipboard cb;
    std::vector<uint8_t> img = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A};
    EXPECT_TRUE(cb.set_image(img, "png"));
    EXPECT_EQ(cb.get_image("png"), img);
    EXPECT_EQ(cb.last_image_format, "png");
}

TEST(PlatformClipboardTest, MockGetImageDefaultFormat) {
    MockPlatformClipboard cb;
    cb.image_data = {0xFF, 0xD8, 0xFF};
    auto result = cb.get_image();
    EXPECT_EQ(cb.last_image_format, "png");
    EXPECT_EQ(result, cb.image_data);
}

TEST(PlatformClipboardTest, MockSetImageFail) {
    MockPlatformClipboard cb;
    cb.image_set_result = false;
    EXPECT_FALSE(cb.set_image({0x00}, "bmp"));
}

// --- HTML Operations ---

TEST(PlatformClipboardTest, MockSetGetHtml) {
    MockPlatformClipboard cb;
    EXPECT_TRUE(cb.set_html("<b>Bold</b>"));
    EXPECT_EQ(cb.get_html(), "<b>Bold</b>");
}

TEST(PlatformClipboardTest, MockSetHtmlEmpty) {
    MockPlatformClipboard cb;
    EXPECT_TRUE(cb.set_html(""));
    EXPECT_EQ(cb.get_html(), "");
}

TEST(PlatformClipboardTest, MockSetHtmlComplex) {
    MockPlatformClipboard cb;
    std::string html = "<html><body><p>Hello</p><script>alert('x')</script></body></html>";
    EXPECT_TRUE(cb.set_html(html));
    EXPECT_EQ(cb.get_html(), html);
}

// --- RTF Operations ---

TEST(PlatformClipboardTest, MockSetGetRtf) {
    MockPlatformClipboard cb;
    EXPECT_TRUE(cb.set_rtf("{\\rtf1\\ansi\\b Bold\\b0}"));
    EXPECT_EQ(cb.get_rtf(), "{\\rtf1\\ansi\\b Bold\\b0}");
}

TEST(PlatformClipboardTest, MockSetRtfFail) {
    MockPlatformClipboard cb;
    cb.rtf_set_result = false;
    EXPECT_FALSE(cb.set_rtf("{\\rtf1}"));
}

// --- URI List Operations ---

TEST(PlatformClipboardTest, MockSetGetUriList) {
    MockPlatformClipboard cb;
    std::vector<std::string> uris = {"http://example.com", "https://test.org"};
    EXPECT_TRUE(cb.set_uri_list(uris));
    EXPECT_EQ(cb.get_uri_list(), uris);
}

TEST(PlatformClipboardTest, MockSetUriListEmpty) {
    MockPlatformClipboard cb;
    EXPECT_TRUE(cb.set_uri_list({}));
    EXPECT_TRUE(cb.get_uri_list().empty());
}

// --- Clear ---

TEST(PlatformClipboardTest, MockClear) {
    MockPlatformClipboard cb;
    cb.set_text("something");
    cb.set_file_list({"file.txt"});
    cb.set_html("<p>html</p>");
    EXPECT_TRUE(cb.clear());
    EXPECT_EQ(cb.clear_calls, 1);
}

TEST(PlatformClipboardTest, MockClearFail) {
    MockPlatformClipboard cb;
    cb.clear_result = false;
    EXPECT_FALSE(cb.clear());
}

// --- Ownership ---

TEST(PlatformClipboardTest, MockOwnsClipboard) {
    MockPlatformClipboard cb;
    EXPECT_TRUE(cb.owns_clipboard());
    cb.owns_result = false;
    EXPECT_FALSE(cb.owns_clipboard());
}

// --- Change Callback ---

TEST(PlatformClipboardTest, MockSetOnChange) {
    MockPlatformClipboard cb;
    int call_count = 0;
    cb.set_on_change([&call_count]() { call_count++; });
    EXPECT_TRUE(cb.change_cb != nullptr);
    cb.change_cb();
    EXPECT_EQ(call_count, 1);
    cb.change_cb();
    EXPECT_EQ(call_count, 2);
}

// --- Delayed Rendering ---

TEST(PlatformClipboardTest, MockEnableDelayedRendering) {
    MockPlatformClipboard cb;
    auto renderer = std::make_shared<MockDelayedRenderer>();
    EXPECT_TRUE(cb.enable_delayed_rendering(renderer));
    EXPECT_NE(cb.delayed_renderer, nullptr);
}

TEST(PlatformClipboardTest, MockEnableDelayedRenderingFail) {
    MockPlatformClipboard cb;
    cb.enable_delayed_rendering_result = false;
    EXPECT_FALSE(cb.enable_delayed_rendering(std::make_shared<MockDelayedRenderer>()));
}

TEST(PlatformClipboardTest, MockDisableDelayedRendering) {
    MockPlatformClipboard cb;
    cb.enable_delayed_rendering(std::make_shared<MockDelayedRenderer>());
    EXPECT_NE(cb.delayed_renderer, nullptr);
    cb.disable_delayed_rendering();
    EXPECT_EQ(cb.delayed_renderer, nullptr);
}

// --- Available Formats ---

TEST(PlatformClipboardTest, MockAvailableFormats) {
    MockPlatformClipboard cb;
    EXPECT_EQ(cb.available_formats(), static_cast<ContentFormat>(0));
    cb.available_formats_value = ContentFormat::TEXT | ContentFormat::HTML;
    EXPECT_NE(cb.available_formats(), static_cast<ContentFormat>(0));
}

// --- Content Descriptor ---

TEST(PlatformClipboardTest, MockGetContentDescriptor) {
    MockPlatformClipboard cb;
    cb.descriptor_value.text = "desc text";
    cb.descriptor_value.available_formats = ContentFormat::TEXT;
    auto desc = cb.get_content_descriptor();
    EXPECT_EQ(desc.text, "desc text");
    EXPECT_NE(desc.available_formats, static_cast<ContentFormat>(0));
    EXPECT_EQ(cb.desc_get_calls, 1);
}

TEST(PlatformClipboardTest, MockSetContent) {
    MockPlatformClipboard cb;
    ClipboardContentDescriptor content;
    content.text = "set via content";
    content.available_formats = ContentFormat::TEXT;
    EXPECT_TRUE(cb.set_content(content));
    EXPECT_EQ(cb.last_content_set.text, "set via content");
    EXPECT_EQ(cb.content_set_calls, 1);
}

TEST(PlatformClipboardTest, MockSetContentFail) {
    MockPlatformClipboard cb;
    cb.content_set_result = false;
    ClipboardContentDescriptor content;
    EXPECT_FALSE(cb.set_content(content));
}

TEST(PlatformClipboardTest, MockSetContentMultipleFormats) {
    MockPlatformClipboard cb;
    ClipboardContentDescriptor content;
    content.text = "multi";
    content.html = "<multi>";
    content.file_list = {"a", "b", "c"};
    content.available_formats = ContentFormat::TEXT | ContentFormat::HTML | ContentFormat::FILE_LIST;
    EXPECT_TRUE(cb.set_content(content));
    EXPECT_EQ(cb.last_content_set.text, "multi");
    EXPECT_EQ(cb.last_content_set.html, "<multi>");
    EXPECT_EQ(cb.last_content_set.file_list.size(), 3u);
}

// --- Change Count ---

TEST(PlatformClipboardTest, MockGetChangeCount) {
    MockPlatformClipboard cb;
    EXPECT_EQ(cb.get_change_count(), 0);
    cb.change_count_value = 42;
    EXPECT_EQ(cb.get_change_count(), 42);
    EXPECT_EQ(cb.change_count_calls, 2);
}

// --- Polymorphic usage ---

TEST(PlatformClipboardTest, PolymorphicSetGetText) {
    std::unique_ptr<PlatformClipboard> cb = std::make_unique<MockPlatformClipboard>();
    EXPECT_TRUE(cb->set_text("polymorphic"));
    EXPECT_EQ(cb->get_text(), "polymorphic");
}

TEST(PlatformClipboardTest, PolymorphicFileList) {
    std::unique_ptr<PlatformClipboard> cb = std::make_unique<MockPlatformClipboard>();
    std::vector<std::string> files = {"/a", "/b", "/c"};
    EXPECT_TRUE(cb->set_file_list(files));
    EXPECT_EQ(cb->get_file_list().size(), 3u);
}

// =============================================================================
// ContentDeduplicator Tests
// =============================================================================

TEST(ContentDeduplicatorTest, DefaultConstruction) {
    ContentDeduplicator dedup;
    EXPECT_EQ(dedup.size(), 0u);
}

TEST(ContentDeduplicatorTest, ConstructionWithCustomSize) {
    ContentDeduplicator dedup(128);
    EXPECT_EQ(dedup.size(), 0u);
}

TEST(ContentDeduplicatorTest, ConstructionWithLargeMax) {
    ContentDeduplicator dedup(4096);
    EXPECT_EQ(dedup.size(), 0u);
}

TEST(ContentDeduplicatorTest, ConstructionWithSmallMax) {
    ContentDeduplicator dedup(1);
    EXPECT_EQ(dedup.size(), 0u);
}

TEST(ContentDeduplicatorTest, ComputeTextHash) {
    ContentHash h1 = ContentDeduplicator::compute_text_hash("hello");
    ContentHash h2 = ContentDeduplicator::compute_text_hash("hello");
    ContentHash h3 = ContentDeduplicator::compute_text_hash("world");

    EXPECT_EQ(h1, h2);
    EXPECT_NE(h1, h3);
}

TEST(ContentDeduplicatorTest, ComputeTextHashEmpty) {
    ContentHash empty = ContentDeduplicator::compute_text_hash("");
    ContentHash empty2 = ContentDeduplicator::compute_text_hash("");
    EXPECT_EQ(empty, empty2);
}

TEST(ContentDeduplicatorTest, ComputeDataHash) {
    std::vector<uint8_t> data1 = {1, 2, 3, 4, 5};
    std::vector<uint8_t> data2 = {1, 2, 3, 4, 5};
    std::vector<uint8_t> data3 = {1, 2, 3, 4, 6};

    ContentHash h1 = ContentDeduplicator::compute_data_hash(data1);
    ContentHash h2 = ContentDeduplicator::compute_data_hash(data2);
    ContentHash h3 = ContentDeduplicator::compute_data_hash(data3);

    EXPECT_EQ(h1, h2);
    EXPECT_NE(h1, h3);
}

TEST(ContentDeduplicatorTest, ComputeDataHashEmpty) {
    std::vector<uint8_t> empty;
    ContentHash h = ContentDeduplicator::compute_data_hash(empty);
    EXPECT_EQ(h.size(), 32u);
}

TEST(ContentDeduplicatorTest, ComputeDataHashLarge) {
    std::vector<uint8_t> large(100000, 0x42);
    ContentHash h = ContentDeduplicator::compute_data_hash(large);
    EXPECT_EQ(h.size(), 32u);
}

TEST(ContentDeduplicatorTest, ComputeFileHash) {
    std::vector<std::string> files1 = {"/a.txt", "/b.txt"};
    std::vector<std::string> files2 = {"/a.txt", "/b.txt"};
    std::vector<std::string> files3 = {"/a.txt", "/c.txt"};

    ContentHash h1 = ContentDeduplicator::compute_file_hash(files1);
    ContentHash h2 = ContentDeduplicator::compute_file_hash(files2);
    ContentHash h3 = ContentDeduplicator::compute_file_hash(files3);

    EXPECT_EQ(h1, h2);
    EXPECT_NE(h1, h3);
}

TEST(ContentDeduplicatorTest, ComputeFileHashEmpty) {
    std::vector<std::string> empty;
    ContentHash h = ContentDeduplicator::compute_file_hash(empty);
    EXPECT_EQ(h.size(), 32u);
}

TEST(ContentDeduplicatorTest, HashToHex) {
    ContentHash hash{};
    hash[0] = 0xAB;
    hash[1] = 0xCD;
    hash[31] = 0xEF;
    std::string hex = ContentDeduplicator::hash_to_hex(hash);
    EXPECT_EQ(hex.size(), 64u);
    EXPECT_EQ(hex[0], 'a');
    EXPECT_EQ(hex[1], 'b');
    EXPECT_EQ(hex[2], 'c');
    EXPECT_EQ(hex[3], 'd');
    EXPECT_EQ(hex[62], 'e');
    EXPECT_EQ(hex[63], 'f');
}

TEST(ContentDeduplicatorTest, HashToHexAllZeros) {
    ContentHash hash{};
    std::string hex = ContentDeduplicator::hash_to_hex(hash);
    EXPECT_EQ(hex, std::string(64, '0'));
}

TEST(ContentDeduplicatorTest, HashToHexAllFF) {
    ContentHash hash{};
    hash.fill(0xFF);
    std::string hex = ContentDeduplicator::hash_to_hex(hash);
    EXPECT_EQ(hex, std::string(64, 'f'));
}

TEST(ContentDeduplicatorTest, StoreAndFind) {
    ContentDeduplicator dedup;
    ContentHash hash = ContentDeduplicator::compute_text_hash("test content");

    ClipboardContentDescriptor desc;
    desc.text = "test content";
    desc.available_formats = ContentFormat::TEXT;

    dedup.store(hash, desc);
    EXPECT_EQ(dedup.size(), 1u);

    auto found = dedup.find_by_hash(hash);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->text, "test content");
}

TEST(ContentDeduplicatorTest, FindMissing) {
    ContentDeduplicator dedup;
    ContentHash hash = ContentDeduplicator::compute_text_hash("missing");
    auto found = dedup.find_by_hash(hash);
    EXPECT_FALSE(found.has_value());
}

TEST(ContentDeduplicatorTest, StoreMultiple) {
    ContentDeduplicator dedup;
    for (int i = 0; i < 10; i++) {
        ContentHash hash = ContentDeduplicator::compute_text_hash("content_" + std::to_string(i));
        ClipboardContentDescriptor desc;
        desc.text = "content_" + std::to_string(i);
        dedup.store(hash, desc);
    }
    EXPECT_EQ(dedup.size(), 10u);

    // Find one
    ContentHash hash5 = ContentDeduplicator::compute_text_hash("content_5");
    auto found = dedup.find_by_hash(hash5);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->text, "content_5");
}

TEST(ContentDeduplicatorTest, Invalidate) {
    ContentDeduplicator dedup;
    ContentHash hash = ContentDeduplicator::compute_text_hash("invalidate me");

    ClipboardContentDescriptor desc;
    desc.text = "invalidate me";
    dedup.store(hash, desc);
    EXPECT_EQ(dedup.size(), 1u);

    dedup.invalidate(hash);
    EXPECT_EQ(dedup.size(), 0u);

    auto found = dedup.find_by_hash(hash);
    EXPECT_FALSE(found.has_value());
}

TEST(ContentDeduplicatorTest, InvalidateNonexistent) {
    ContentDeduplicator dedup;
    ContentHash hash = ContentDeduplicator::compute_text_hash("no such");
    // Should not crash
    dedup.invalidate(hash);
    EXPECT_EQ(dedup.size(), 0u);
}

TEST(ContentDeduplicatorTest, Clear) {
    ContentDeduplicator dedup;
    for (int i = 0; i < 5; i++) {
        ContentHash hash = ContentDeduplicator::compute_text_hash("clear_" + std::to_string(i));
        ClipboardContentDescriptor desc;
        desc.text = "clear_" + std::to_string(i);
        dedup.store(hash, desc);
    }
    EXPECT_EQ(dedup.size(), 5u);
    dedup.clear();
    EXPECT_EQ(dedup.size(), 0u);
}

TEST(ContentDeduplicatorTest, SetMaxEntries) {
    ContentDeduplicator dedup(100);
    dedup.set_max_entries(200);
    // No crash
    dedup.set_max_entries(1);
    // No crash with very small limit
}

TEST(ContentDeduplicatorTest, StoreOverwrite) {
    ContentDeduplicator dedup;
    ContentHash hash = ContentDeduplicator::compute_text_hash("original");

    ClipboardContentDescriptor desc1;
    desc1.text = "original";
    dedup.store(hash, desc1);

    ClipboardContentDescriptor desc2;
    desc2.text = "updated";
    dedup.store(hash, desc2);

    EXPECT_EQ(dedup.size(), 1u);
    auto found = dedup.find_by_hash(hash);
    EXPECT_TRUE(found.has_value());
    // Last store should win
    EXPECT_EQ(found->text, "updated");
}

TEST(ContentDeduplicatorTest, HashConsistencyAcrossInstances) {
    ContentHash h1 = ContentDeduplicator::compute_text_hash("same");
    ContentHash h2 = ContentDeduplicator::compute_text_hash("same");
    EXPECT_EQ(h1, h2);

    std::string hex1 = ContentDeduplicator::hash_to_hex(h1);
    std::string hex2 = ContentDeduplicator::hash_to_hex(h2);
    EXPECT_EQ(hex1, hex2);
}

// =============================================================================
// ClipboardMonitor Tests
// =============================================================================

TEST(ClipboardMonitorTest, DefaultConstruction) {
    ClipboardMonitor monitor;
    EXPECT_FALSE(monitor.is_running());
}

TEST(ClipboardMonitorTest, StartAndIsRunning) {
    ClipboardMonitor monitor;
    monitor.start(std::chrono::milliseconds(100));
    EXPECT_TRUE(monitor.is_running());
    monitor.stop();
}

TEST(ClipboardMonitorTest, StopAfterStart) {
    ClipboardMonitor monitor;
    monitor.start(std::chrono::milliseconds(100));
    EXPECT_TRUE(monitor.is_running());
    monitor.stop();
    // Note: stopping is asynchronous, give it time
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(monitor.is_running());
}

TEST(ClipboardMonitorTest, DoubleStop) {
    ClipboardMonitor monitor;
    monitor.start(std::chrono::milliseconds(100));
    monitor.stop();
    // Second stop should be safe
    monitor.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(monitor.is_running());
}

TEST(ClipboardMonitorTest, StartWithoutStop) {
    ClipboardMonitor monitor;
    monitor.start(std::chrono::milliseconds(100));
    EXPECT_TRUE(monitor.is_running());
    // Destructor will handle stopping
}

TEST(ClipboardMonitorTest, MultipleStarts) {
    ClipboardMonitor monitor;
    monitor.start(std::chrono::milliseconds(50));
    EXPECT_TRUE(monitor.is_running());
    monitor.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    monitor.start(std::chrono::milliseconds(50));
    EXPECT_TRUE(monitor.is_running());
    monitor.stop();
}

TEST(ClipboardMonitorTest, SetOnTextChange) {
    ClipboardMonitor monitor;
    bool called = false;
    monitor.set_on_text_change([&called](const std::string& text) {
        called = true;
    });
    // Just verify setter doesn't crash
    EXPECT_FALSE(called);
}

TEST(ClipboardMonitorTest, SetOnFileChange) {
    ClipboardMonitor monitor;
    bool called = false;
    monitor.set_on_file_change([&called](const std::vector<std::string>& files) {
        called = true;
    });
    EXPECT_FALSE(called);
}

TEST(ClipboardMonitorTest, SetOnImageChange) {
    ClipboardMonitor monitor;
    bool called = false;
    monitor.set_on_image_change([&called](const std::vector<uint8_t>& data, const std::string& fmt) {
        called = true;
    });
    EXPECT_FALSE(called);
}

TEST(ClipboardMonitorTest, SetOnHtmlChange) {
    ClipboardMonitor monitor;
    bool called = false;
    monitor.set_on_html_change([&called](const std::string& html) {
        called = true;
    });
    EXPECT_FALSE(called);
}

TEST(ClipboardMonitorTest, SetOnRtfChange) {
    ClipboardMonitor monitor;
    bool called = false;
    monitor.set_on_rtf_change([&called](const std::string& rtf) {
        called = true;
    });
    EXPECT_FALSE(called);
}

TEST(ClipboardMonitorTest, SetOnAnyChange) {
    ClipboardMonitor monitor;
    bool called = false;
    monitor.set_on_any_change([&called](const ClipboardContentDescriptor& desc) {
        called = true;
    });
    EXPECT_FALSE(called);
}

TEST(ClipboardMonitorTest, LastTextInitiallyEmpty) {
    ClipboardMonitor monitor;
    EXPECT_TRUE(monitor.last_text().empty());
}

TEST(ClipboardMonitorTest, LastFilesInitiallyEmpty) {
    ClipboardMonitor monitor;
    EXPECT_TRUE(monitor.last_files().empty());
}

TEST(ClipboardMonitorTest, LastHtmlInitiallyEmpty) {
    ClipboardMonitor monitor;
    EXPECT_TRUE(monitor.last_html().empty());
}

TEST(ClipboardMonitorTest, LastDescriptorInitiallyEmpty) {
    ClipboardMonitor monitor;
    auto desc = monitor.last_descriptor();
    EXPECT_EQ(desc.available_formats, static_cast<ContentFormat>(0));
}

TEST(ClipboardMonitorTest, AllCallbacksSet) {
    ClipboardMonitor monitor;
    int text_calls = 0, file_calls = 0, image_calls = 0;
    int html_calls = 0, rtf_calls = 0, any_calls = 0;

    monitor.set_on_text_change([&](const std::string&) { text_calls++; });
    monitor.set_on_file_change([&](const std::vector<std::string>&) { file_calls++; });
    monitor.set_on_image_change([&](const std::vector<uint8_t>&, const std::string&) { image_calls++; });
    monitor.set_on_html_change([&](const std::string&) { html_calls++; });
    monitor.set_on_rtf_change([&](const std::string&) { rtf_calls++; });
    monitor.set_on_any_change([&](const ClipboardContentDescriptor&) { any_calls++; });

    EXPECT_EQ(text_calls, 0);
    EXPECT_EQ(file_calls, 0);
    EXPECT_EQ(image_calls, 0);
    EXPECT_EQ(html_calls, 0);
    EXPECT_EQ(rtf_calls, 0);
    EXPECT_EQ(any_calls, 0);
}

TEST(ClipboardMonitorTest, StartWithDefaultInterval) {
    ClipboardMonitor monitor;
    monitor.start(); // Default 500ms
    EXPECT_TRUE(monitor.is_running());
    monitor.stop();
}

TEST(ClipboardMonitorTest, StartWithFastInterval) {
    ClipboardMonitor monitor;
    monitor.start(std::chrono::milliseconds(10));
    EXPECT_TRUE(monitor.is_running());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(monitor.is_running());
    monitor.stop();
}

// =============================================================================
// ClipboardSynchronizer Tests
// =============================================================================

TEST(ClipboardSynchronizerTest, DefaultConstruction) {
    ClipboardSynchronizer sync;
    EXPECT_EQ(sync.get_mode(), ClipboardSynchronizer::Mode::DISABLED);
}

TEST(ClipboardSynchronizerTest, SetModeDisabled) {
    ClipboardSynchronizer sync;
    sync.set_mode(ClipboardSynchronizer::Mode::DISABLED);
    EXPECT_EQ(sync.get_mode(), ClipboardSynchronizer::Mode::DISABLED);
}

TEST(ClipboardSynchronizerTest, SetModeLocalOnly) {
    ClipboardSynchronizer sync;
    sync.set_mode(ClipboardSynchronizer::Mode::LOCAL_ONLY);
    EXPECT_EQ(sync.get_mode(), ClipboardSynchronizer::Mode::LOCAL_ONLY);
}

TEST(ClipboardSynchronizerTest, SetModeRemoteOnly) {
    ClipboardSynchronizer sync;
    sync.set_mode(ClipboardSynchronizer::Mode::REMOTE_ONLY);
    EXPECT_EQ(sync.get_mode(), ClipboardSynchronizer::Mode::REMOTE_ONLY);
}

TEST(ClipboardSynchronizerTest, SetModeBidirectional) {
    ClipboardSynchronizer sync;
    sync.set_mode(ClipboardSynchronizer::Mode::BIDIRECTIONAL);
    EXPECT_EQ(sync.get_mode(), ClipboardSynchronizer::Mode::BIDIRECTIONAL);
}

TEST(ClipboardSynchronizerTest, AllModesTransition) {
    ClipboardSynchronizer sync;

    sync.set_mode(ClipboardSynchronizer::Mode::LOCAL_ONLY);
    EXPECT_EQ(sync.get_mode(), ClipboardSynchronizer::Mode::LOCAL_ONLY);

    sync.set_mode(ClipboardSynchronizer::Mode::REMOTE_ONLY);
    EXPECT_EQ(sync.get_mode(), ClipboardSynchronizer::Mode::REMOTE_ONLY);

    sync.set_mode(ClipboardSynchronizer::Mode::BIDIRECTIONAL);
    EXPECT_EQ(sync.get_mode(), ClipboardSynchronizer::Mode::BIDIRECTIONAL);

    sync.set_mode(ClipboardSynchronizer::Mode::DISABLED);
    EXPECT_EQ(sync.get_mode(), ClipboardSynchronizer::Mode::DISABLED);
}

TEST(ClipboardSynchronizerTest, QuickModeToggle) {
    ClipboardSynchronizer sync;
    for (int i = 0; i < 10; i++) {
        sync.set_mode(ClipboardSynchronizer::Mode::LOCAL_ONLY);
        sync.set_mode(ClipboardSynchronizer::Mode::REMOTE_ONLY);
    }
    EXPECT_EQ(sync.get_mode(), ClipboardSynchronizer::Mode::REMOTE_ONLY);
}

TEST(ClipboardSynchronizerTest, PollTextChangeInitiallyEmpty) {
    ClipboardSynchronizer sync;
    sync.set_mode(ClipboardSynchronizer::Mode::LOCAL_ONLY);
    std::string result = sync.poll_text_change();
    // Should be empty when no change
    EXPECT_TRUE(result.empty());
}

TEST(ClipboardSynchronizerTest, PollFileChangeInitiallyEmpty) {
    ClipboardSynchronizer sync;
    sync.set_mode(ClipboardSynchronizer::Mode::LOCAL_ONLY);
    auto result = sync.poll_file_change();
    EXPECT_TRUE(result.empty());
}

TEST(ClipboardSynchronizerTest, PollContentChangeInitiallyEmpty) {
    ClipboardSynchronizer sync;
    sync.set_mode(ClipboardSynchronizer::Mode::LOCAL_ONLY);
    auto result = sync.poll_content_change();
    EXPECT_EQ(result.available_formats, static_cast<ContentFormat>(0));
}

TEST(ClipboardSynchronizerTest, PollDeduplicatedChangeInitiallyNull) {
    ClipboardSynchronizer sync;
    sync.set_mode(ClipboardSynchronizer::Mode::LOCAL_ONLY);
    auto result = sync.poll_deduplicated_change();
    EXPECT_FALSE(result.has_value());
}

TEST(ClipboardSynchronizerTest, ApplyRemoteText) {
    ClipboardSynchronizer sync;
    sync.set_mode(ClipboardSynchronizer::Mode::REMOTE_ONLY);
    // Should not crash
    sync.apply_remote_text("remote text content");
}

TEST(ClipboardSynchronizerTest, ApplyRemoteEmptyText) {
    ClipboardSynchronizer sync;
    sync.apply_remote_text("");
}

TEST(ClipboardSynchronizerTest, ApplyRemoteLongText) {
    ClipboardSynchronizer sync;
    std::string long_text(50000, 'R');
    sync.apply_remote_text(long_text);
}

TEST(ClipboardSynchronizerTest, ApplyRemoteFiles) {
    ClipboardSynchronizer sync;
    std::vector<std::string> files = {"/remote/file1.txt", "/remote/file2.txt"};
    sync.apply_remote_files(files);
}

TEST(ClipboardSynchronizerTest, ApplyRemoteEmptyFiles) {
    ClipboardSynchronizer sync;
    sync.apply_remote_files({});
}

TEST(ClipboardSynchronizerTest, ApplyRemoteImage) {
    ClipboardSynchronizer sync;
    std::vector<uint8_t> img = {0x89, 0x50, 0x4E, 0x47};
    sync.apply_remote_image(img);
}

TEST(ClipboardSynchronizerTest, ApplyRemoteHtml) {
    ClipboardSynchronizer sync;
    sync.apply_remote_html("<html><body>Remote HTML</body></html>");
}

TEST(ClipboardSynchronizerTest, ApplyRemoteRtf) {
    ClipboardSynchronizer sync;
    sync.apply_remote_rtf("{\\rtf1\\ansi Remote RTF}");
}

TEST(ClipboardSynchronizerTest, ApplyRemoteContent) {
    ClipboardSynchronizer sync;
    ClipboardContentDescriptor desc;
    desc.text = "remote desc";
    desc.html = "<remote>";
    desc.available_formats = ContentFormat::TEXT | ContentFormat::HTML;
    sync.apply_remote_content(desc);
}

TEST(ClipboardSynchronizerTest, ApplyRemoteContentEmpty) {
    ClipboardSynchronizer sync;
    ClipboardContentDescriptor desc;
    sync.apply_remote_content(desc);
}

TEST(ClipboardSynchronizerTest, BeginEndFileCopy) {
    ClipboardSynchronizer sync;
    sync.begin_file_copy(1);
    sync.end_file_copy(1);
}

TEST(ClipboardSynchronizerTest, BeginEndFileCopyMultipleConns) {
    ClipboardSynchronizer sync;
    sync.begin_file_copy(1);
    sync.begin_file_copy(2);
    sync.end_file_copy(1);
    sync.end_file_copy(2);
}

TEST(ClipboardSynchronizerTest, GetFileProgressDefault) {
    ClipboardSynchronizer sync;
    ProgressPercent pp = sync.get_file_progress();
    EXPECT_DOUBLE_EQ(pp.percent, 0.0);
}

TEST(ClipboardSynchronizerTest, ModeEnumDistinctValues) {
    EXPECT_NE(ClipboardSynchronizer::Mode::DISABLED,
        ClipboardSynchronizer::Mode::LOCAL_ONLY);
    EXPECT_NE(ClipboardSynchronizer::Mode::LOCAL_ONLY,
        ClipboardSynchronizer::Mode::REMOTE_ONLY);
    EXPECT_NE(ClipboardSynchronizer::Mode::REMOTE_ONLY,
        ClipboardSynchronizer::Mode::BIDIRECTIONAL);
    EXPECT_NE(ClipboardSynchronizer::Mode::BIDIRECTIONAL,
        ClipboardSynchronizer::Mode::DISABLED);
}

TEST(ClipboardSynchronizerTest, RemoteOnlyIgnoresLocalPoll) {
    ClipboardSynchronizer sync;
    sync.set_mode(ClipboardSynchronizer::Mode::REMOTE_ONLY);
    auto result = sync.poll_text_change();
    EXPECT_TRUE(result.empty());
}

// =============================================================================
// ClipboardFileServer Tests
// =============================================================================

TEST(ClipboardFileServerTest, DefaultConstruction) {
    ClipboardFileServer server;
    EXPECT_EQ(server.file_count(), 0u);
    EXPECT_EQ(server.total_size(), 0u);
}

TEST(ClipboardFileServerTest, AddFile) {
    ClipboardFileServer server;
    ClipboardFileServer::FileEntry entry;
    entry.path = "/tmp/test.txt";
    entry.size = 100;
    entry.is_directory = false;
    server.add_file(entry);
    EXPECT_EQ(server.file_count(), 1u);
    EXPECT_EQ(server.total_size(), 100u);
}

TEST(ClipboardFileServerTest, AddMultipleFiles) {
    ClipboardFileServer server;
    std::vector<ClipboardFileServer::FileEntry> entries;
    for (int i = 0; i < 10; i++) {
        ClipboardFileServer::FileEntry entry;
        entry.path = "/tmp/file_" + std::to_string(i) + ".txt";
        entry.size = 100 * (i + 1);
        entries.push_back(entry);
    }
    server.add_files(entries);
    EXPECT_EQ(server.file_count(), 10u);
    EXPECT_GT(server.total_size(), 0u);
}

TEST(ClipboardFileServerTest, ListFiles) {
    ClipboardFileServer server;
    ClipboardFileServer::FileEntry entry;
    entry.path = "/tmp/list_test.txt";
    entry.size = 42;
    server.add_file(entry);

    auto files = server.list_files();
    EXPECT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].path, "/tmp/list_test.txt");
    EXPECT_EQ(files[0].size, 42u);
}

TEST(ClipboardFileServerTest, GetFile) {
    ClipboardFileServer server;
    ClipboardFileServer::FileEntry entry;
    entry.path = "/tmp/get_test.txt";
    entry.size = 200;
    server.add_file(entry);

    auto found = server.get_file("/tmp/get_test.txt");
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->path, "/tmp/get_test.txt");
    EXPECT_EQ(found->size, 200u);
}

TEST(ClipboardFileServerTest, GetFileMissing) {
    ClipboardFileServer server;
    auto found = server.get_file("/nonexistent");
    EXPECT_FALSE(found.has_value());
}

TEST(ClipboardFileServerTest, RemoveFile) {
    ClipboardFileServer server;
    ClipboardFileServer::FileEntry entry;
    entry.path = "/tmp/remove_test.txt";
    server.add_file(entry);
    EXPECT_EQ(server.file_count(), 1u);

    server.remove_file("/tmp/remove_test.txt");
    EXPECT_EQ(server.file_count(), 0u);
}

TEST(ClipboardFileServerTest, RemoveNonexistentFile) {
    ClipboardFileServer server;
    server.remove_file("/no/such/file");
    EXPECT_EQ(server.file_count(), 0u);
}

TEST(ClipboardFileServerTest, ClearFiles) {
    ClipboardFileServer server;
    for (int i = 0; i < 5; i++) {
        ClipboardFileServer::FileEntry entry;
        entry.path = "/tmp/clear_" + std::to_string(i);
        entry.size = i * 10;
        server.add_file(entry);
    }
    EXPECT_EQ(server.file_count(), 5u);

    server.clear_files();
    EXPECT_EQ(server.file_count(), 0u);
    EXPECT_EQ(server.total_size(), 0u);
}

TEST(ClipboardFileServerTest, ReadChunk) {
    ClipboardFileServer server;
    ClipboardFileServer::FileEntry entry;
    entry.path = "/tmp/chunks.txt";
    entry.size = 200;
    entry.cached_data = std::vector<uint8_t>(200, 'D');
    server.add_file(entry);

    auto chunk = server.read_chunk("/tmp/chunks.txt", 0, 50);
    EXPECT_EQ(chunk.size(), 50u);
    EXPECT_EQ(chunk[0], 'D');

    auto chunk2 = server.read_chunk("/tmp/chunks.txt", 50, 50);
    EXPECT_EQ(chunk2.size(), 50u);
}

TEST(ClipboardFileServerTest, ReadChunkPastEnd) {
    ClipboardFileServer server;
    ClipboardFileServer::FileEntry entry;
    entry.path = "/tmp/short.txt";
    entry.size = 100;
    entry.cached_data = std::vector<uint8_t>(100, 'S');
    server.add_file(entry);

    auto chunk = server.read_chunk("/tmp/short.txt", 90, 50);
    // Should return truncated chunk
    EXPECT_EQ(chunk.size(), 10u);
}

TEST(ClipboardFileServerTest, ReadChunkMissingFile) {
    ClipboardFileServer server;
    auto chunk = server.read_chunk("/missing", 0, 100);
    EXPECT_TRUE(chunk.empty());
}

TEST(ClipboardFileServerTest, ServeProgress) {
    ClipboardFileServer server;
    ClipboardFileServer::FileEntry entry;
    entry.path = "/tmp/progress.txt";
    entry.size = 100;
    server.add_file(entry);

    auto progress = server.get_serve_progress("/tmp/progress.txt");
    // Should return valid progress
    EXPECT_DOUBLE_EQ(progress.percent, 0.0);
}

TEST(ClipboardFileServerTest, ServeProgressMissingFile) {
    ClipboardFileServer server;
    auto progress = server.get_serve_progress("/missing");
    EXPECT_DOUBLE_EQ(progress.percent, 0.0);
}

TEST(ClipboardFileServerTest, FileEntryProperties) {
    ClipboardFileServer::FileEntry entry;
    EXPECT_TRUE(entry.path.empty());
    EXPECT_EQ(entry.size, 0u);
    EXPECT_FALSE(entry.is_directory);
    EXPECT_TRUE(entry.cached_data.empty());

    entry.path = "/test/dir";
    entry.is_directory = true;
    EXPECT_TRUE(entry.is_directory);
    EXPECT_EQ(entry.path, "/test/dir");
}

TEST(ClipboardFileServerTest, FileEntryContentHash) {
    ClipboardFileServer::FileEntry entry;
    ContentHash hash{};
    hash[0] = 0xAB;
    entry.content_hash = hash;
    EXPECT_EQ(entry.content_hash[0], 0xAB);
}

TEST(ClipboardFileServerTest, LargeCachedData) {
    ClipboardFileServer server;
    ClipboardFileServer::FileEntry entry;
    entry.path = "/tmp/large.bin";
    entry.size = 1024 * 1024;
    entry.cached_data = std::vector<uint8_t>(entry.size, 0xCC);
    server.add_file(entry);
    EXPECT_EQ(server.total_size(), 1024u * 1024u);

    auto chunk = server.read_chunk("/tmp/large.bin", 0, 4096);
    EXPECT_EQ(chunk.size(), 4096u);
}

TEST(ClipboardFileServerTest, MultipleChunksRead) {
    ClipboardFileServer server;
    ClipboardFileServer::FileEntry entry;
    entry.path = "/tmp/multi_chunk.dat";
    entry.size = 256;
    entry.cached_data.resize(256);
    for (size_t i = 0; i < 256; i++) {
        entry.cached_data[i] = static_cast<uint8_t>(i);
    }
    server.add_file(entry);

    for (uint64_t offset = 0; offset < 256; offset += 64) {
        auto chunk = server.read_chunk("/tmp/multi_chunk.dat", offset, 64);
        EXPECT_EQ(chunk.size(), 64u);
        EXPECT_EQ(chunk[0], static_cast<uint8_t>(offset));
    }
}

// =============================================================================
// ClipboardFuseFs Tests
// =============================================================================

TEST(ClipboardFuseFsTest, DefaultConstruction) {
    ClipboardFuseFs fs;
    EXPECT_FALSE(fs.is_mounted());
    EXPECT_EQ(fs.mount_point(), "");
}

TEST(ClipboardFuseFsTest, MountOptionsDefault) {
    ClipboardFuseFs::MountOptions opts;
    EXPECT_EQ(opts.mount_point, "/tmp/clipboard-fuse");
    EXPECT_TRUE(opts.read_only);
    EXPECT_FALSE(opts.allow_other);
    EXPECT_EQ(opts.max_file_size_mb, 100);
}

TEST(ClipboardFuseFsTest, MountOptionsCustom) {
    ClipboardFuseFs::MountOptions opts;
    opts.mount_point = "/mnt/clipboard";
    opts.read_only = false;
    opts.allow_other = true;
    opts.max_file_size_mb = 200;

    EXPECT_EQ(opts.mount_point, "/mnt/clipboard");
    EXPECT_FALSE(opts.read_only);
    EXPECT_TRUE(opts.allow_other);
    EXPECT_EQ(opts.max_file_size_mb, 200);
}

TEST(ClipboardFuseFsTest, Mount) {
    ClipboardFuseFs fs;
    ClipboardFuseFs::MountOptions opts;
    opts.mount_point = "/tmp/test-clipboard-fuse-" + std::to_string(std::rand());
    // Mount may fail on CI (requires FUSE), but should not crash
    bool result = fs.mount(opts);
    if (result) {
        EXPECT_TRUE(fs.is_mounted());
        EXPECT_EQ(fs.mount_point(), opts.mount_point);
        fs.unmount();
    }
    // Either way, no crash
}

TEST(ClipboardFuseFsTest, UnmountWhenNotMounted) {
    ClipboardFuseFs fs;
    // Should be safe
    fs.unmount();
    EXPECT_FALSE(fs.is_mounted());
}

TEST(ClipboardFuseFsTest, SetFileServer) {
    ClipboardFuseFs fs;
    auto server = std::make_shared<ClipboardFileServer>();
    fs.set_file_server(server);
    // Should not crash
}

TEST(ClipboardFuseFsTest, SetFileServerNullptr) {
    ClipboardFuseFs fs;
    fs.set_file_server(nullptr);
    // Should not crash
}

TEST(ClipboardFuseFsTest, UpdateContents) {
    ClipboardFuseFs fs;
    std::vector<ClipboardFileServer::FileEntry> entries;
    ClipboardFileServer::FileEntry entry;
    entry.path = "/fuse/test.txt";
    entry.size = 100;
    entries.push_back(entry);
    fs.update_contents(entries);
    // Should not crash
}

TEST(ClipboardFuseFsTest, UpdateContentsEmpty) {
    ClipboardFuseFs fs;
    std::vector<ClipboardFileServer::FileEntry> empty;
    fs.update_contents(empty);
    // Should not crash
}

TEST(ClipboardFuseFsTest, FuseGetattrStub) {
    ClipboardFuseFs fs;
    // Call the stub directly — should not crash
    int result = fs.fuse_getattr("/", nullptr);
    // Result is implementation-defined, but should not crash
    (void)result;
}

TEST(ClipboardFuseFsTest, FuseReaddirStub) {
    ClipboardFuseFs fs;
    int result = fs.fuse_readdir("/", nullptr, nullptr, 0, nullptr);
    (void)result;
}

TEST(ClipboardFuseFsTest, FuseOpenStub) {
    ClipboardFuseFs fs;
    int result = fs.fuse_open("/test", nullptr);
    (void)result;
}

TEST(ClipboardFuseFsTest, FuseReadStub) {
    ClipboardFuseFs fs;
    char buf[1024] = {};
    int result = fs.fuse_read("/test", buf, 1024, 0, nullptr);
    (void)result;
}

TEST(ClipboardFuseFsTest, FuseReleaseStub) {
    ClipboardFuseFs fs;
    int result = fs.fuse_release("/test", nullptr);
    (void)result;
}

TEST(ClipboardFuseFsTest, MountUnmountCycle) {
    ClipboardFuseFs fs;
    ClipboardFuseFs::MountOptions opts;
    opts.mount_point = "/tmp/clipboard-fuse-cycle-" + std::to_string(std::rand());
    bool mounted = fs.mount(opts);
    if (mounted) {
        EXPECT_TRUE(fs.is_mounted());
        fs.unmount();
        EXPECT_FALSE(fs.is_mounted());
    }
}

// =============================================================================
// ClipboardViewer Tests
// =============================================================================

TEST(ClipboardViewerTest, DefaultConstruction) {
    ClipboardViewer viewer;
    EXPECT_FALSE(viewer.is_active());
}

TEST(ClipboardViewerTest, SetChangeCallback) {
    ClipboardViewer viewer;
    bool called = false;
    viewer.set_change_callback([&called]() { called = true; });
    // Just verify setter doesn't crash
}

TEST(ClipboardViewerTest, Initialize) {
    ClipboardViewer viewer;
    bool result = viewer.initialize();
    if (result) {
        EXPECT_TRUE(viewer.is_active());
        viewer.shutdown();
        EXPECT_FALSE(viewer.is_active());
    }
    // Either way, no crash
}

TEST(ClipboardViewerTest, InitializeWithNullHandle) {
    ClipboardViewer viewer;
    bool result = viewer.initialize(nullptr);
    (void)result; // May fail on non-Windows
}

TEST(ClipboardViewerTest, ShutdownWhenNotActive) {
    ClipboardViewer viewer;
    viewer.shutdown();
    EXPECT_FALSE(viewer.is_active());
}

TEST(ClipboardViewerTest, DoubleShutdown) {
    ClipboardViewer viewer;
    viewer.initialize();
    viewer.shutdown();
    viewer.shutdown(); // Should be safe
    EXPECT_FALSE(viewer.is_active());
}

TEST(ClipboardViewerTest, ActiveAfterInitShutdownCycle) {
    ClipboardViewer viewer;
    EXPECT_FALSE(viewer.is_active());

    bool init_ok = viewer.initialize();
    if (init_ok) {
        EXPECT_TRUE(viewer.is_active());
        viewer.shutdown();
    }
    EXPECT_FALSE(viewer.is_active());
}

// =============================================================================
// Utility Functions Tests
// =============================================================================

TEST(UtilityTest, DetectImageFormatPNG) {
    std::vector<uint8_t> png = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    std::string fmt = util::detect_image_format(png);
    EXPECT_EQ(fmt, "png");
}

TEST(UtilityTest, DetectImageFormatJPEG) {
    std::vector<uint8_t> jpg = {0xFF, 0xD8, 0xFF, 0xE0};
    std::string fmt = util::detect_image_format(jpg);
    EXPECT_EQ(fmt, "jpeg");
}

TEST(UtilityTest, DetectImageFormatBMP) {
    std::vector<uint8_t> bmp = {0x42, 0x4D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::string fmt = util::detect_image_format(bmp);
    EXPECT_EQ(fmt, "bmp");
}

TEST(UtilityTest, DetectImageFormatTIFF_LE) {
    std::vector<uint8_t> tiff = {0x49, 0x49, 0x2A, 0x00};
    std::string fmt = util::detect_image_format(tiff);
    EXPECT_EQ(fmt, "tiff");
}

TEST(UtilityTest, DetectImageFormatTIFF_BE) {
    std::vector<uint8_t> tiff = {0x4D, 0x4D, 0x00, 0x2A};
    std::string fmt = util::detect_image_format(tiff);
    EXPECT_EQ(fmt, "tiff");
}

TEST(UtilityTest, DetectImageFormatGIF) {
    std::vector<uint8_t> gif = {0x47, 0x49, 0x46, 0x38, 0x39, 0x61};
    std::string fmt = util::detect_image_format(gif);
    EXPECT_EQ(fmt, "gif");
}

TEST(UtilityTest, DetectImageFormatICO) {
    std::vector<uint8_t> ico = {0x00, 0x00, 0x01, 0x00};
    std::string fmt = util::detect_image_format(ico);
    EXPECT_EQ(fmt, "ico");
}

TEST(UtilityTest, DetectImageFormatWebP) {
    std::vector<uint8_t> webp = {0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00,
                                  0x57, 0x45, 0x42, 0x50};
    std::string fmt = util::detect_image_format(webp);
    EXPECT_EQ(fmt, "webp");
}

TEST(UtilityTest, DetectImageFormatEmpty) {
    std::string fmt = util::detect_image_format({});
    EXPECT_TRUE(fmt.empty() || fmt == "unknown");
}

TEST(UtilityTest, DetectImageFormatShort) {
    std::string fmt = util::detect_image_format({0x00});
    EXPECT_TRUE(fmt.empty() || fmt == "unknown");
}

TEST(UtilityTest, DetectImageFormatUnknown) {
    std::vector<uint8_t> unknown = {0xAB, 0xCD, 0xEF, 0x01, 0x02, 0x03, 0x04, 0x05,
                                      0x06, 0x07, 0x08, 0x09};
    std::string fmt = util::detect_image_format(unknown);
    EXPECT_TRUE(fmt.empty() || fmt == "unknown");
}

TEST(UtilityTest, ConvertImageFormatSame) {
    std::vector<uint8_t> data = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A};
    // Converting to same format should work or return empty
    auto result = util::convert_image_format(data, "png", "png");
    // Result could be same or empty depending on implementation
    if (!result.empty()) {
        EXPECT_EQ(result, data);
    }
}

TEST(UtilityTest, ConvertImageFormatEmpty) {
    auto result = util::convert_image_format({}, "png", "bmp");
    EXPECT_TRUE(result.empty());
}

TEST(UtilityTest, ParseCfHtml) {
    std::string raw = "Version:0.9\r\nStartHTML:71\r\nEndHTML:170\r\n"
                       "StartFragment:140\r\nEndFragment:160\r\n"
                       "<html><body><!--StartFragment-->Hello<!--EndFragment--></body></html>";
    std::string parsed = util::parse_cf_html(raw);
    EXPECT_FALSE(parsed.empty());
}

TEST(UtilityTest, ParseCfHtmlMinimal) {
    std::string raw = "Version:1.0\r\nStartHTML:00000097\r\nEndHTML:00000134\r\n"
                       "StartFragment:00000113\r\nEndFragment:00000124\r\n"
                       "<html><body><!--StartFragment-->Hi<!--EndFragment--></body></html>";
    std::string parsed = util::parse_cf_html(raw);
    EXPECT_FALSE(parsed.empty());
}

TEST(UtilityTest, ParseCfHtmlEmpty) {
    std::string parsed = util::parse_cf_html("");
    EXPECT_TRUE(parsed.empty() || parsed == "");
}

TEST(UtilityTest, ParseCfHtmlNoFragment) {
    std::string raw = "<html><body>No fragment markers</body></html>";
    std::string parsed = util::parse_cf_html(raw);
    // Should return something even without markers
    (void)parsed;
}

TEST(UtilityTest, GenerateCfHtml) {
    std::string html = "<p>Hello World</p>";
    std::string result = util::generate_cf_html(html);
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result.find("StartHTML"), std::string::npos);
    EXPECT_NE(result.find("StartFragment"), std::string::npos);
    EXPECT_NE(result.find("EndFragment"), std::string::npos);
    EXPECT_NE(result.find("EndHTML"), std::string::npos);
}

TEST(UtilityTest, GenerateCfHtmlEmpty) {
    std::string result = util::generate_cf_html("");
    EXPECT_FALSE(result.empty());
}

TEST(UtilityTest, GenerateCfHtmlWithSourceUrl) {
    std::string html = "<b>bold</b>";
    std::string result = util::generate_cf_html(html, "https://example.com");
    EXPECT_NE(result.find("SourceURL"), std::string::npos);
    EXPECT_NE(result.find("https://example.com"), std::string::npos);
}

TEST(UtilityTest, GenerateCfHtmlNoSourceUrl) {
    std::string html = "<b>bold</b>";
    std::string result = util::generate_cf_html(html);
    // Without source URL, we should still get valid CF_HTML
    EXPECT_NE(result.find("StartHTML"), std::string::npos);
}

TEST(UtilityTest, FormatToMimeText) {
    std::string mime = util::format_to_mime(ContentFormat::TEXT);
    EXPECT_EQ(mime, "text/plain");
}

TEST(UtilityTest, FormatToMimeHtml) {
    std::string mime = util::format_to_mime(ContentFormat::HTML);
    EXPECT_EQ(mime, "text/html");
}

TEST(UtilityTest, FormatToMimeRtf) {
    std::string mime = util::format_to_mime(ContentFormat::RTF);
    EXPECT_EQ(mime, "text/rtf");
}

TEST(UtilityTest, FormatToMimeImagePng) {
    std::string mime = util::format_to_mime(ContentFormat::IMAGE_PNG);
    EXPECT_EQ(mime, "image/png");
}

TEST(UtilityTest, FormatToMimeImageTiff) {
    std::string mime = util::format_to_mime(ContentFormat::IMAGE_TIFF);
    EXPECT_EQ(mime, "image/tiff");
}

TEST(UtilityTest, FormatToMimeImageBmp) {
    std::string mime = util::format_to_mime(ContentFormat::IMAGE_BMP);
    EXPECT_EQ(mime, "image/bmp");
}

TEST(UtilityTest, FormatToMimeFileList) {
    std::string mime = util::format_to_mime(ContentFormat::FILE_LIST);
    EXPECT_EQ(mime, "text/uri-list");
}

TEST(UtilityTest, FormatToMimeUriList) {
    std::string mime = util::format_to_mime(ContentFormat::URI_LIST);
    EXPECT_EQ(mime, "text/uri-list");
}

TEST(UtilityTest, FormatToMimeInvalid) {
    std::string mime = util::format_to_mime(static_cast<ContentFormat>(0));
    EXPECT_TRUE(mime.empty() || mime == "application/octet-stream");
}

TEST(UtilityTest, MimeToExtensionPng) {
    std::string ext = util::mime_to_extension("image/png");
    EXPECT_EQ(ext, ".png") << "Expected .png but got: " << ext;
}

TEST(UtilityTest, MimeToExtensionJpeg) {
    std::string ext = util::mime_to_extension("image/jpeg");
    EXPECT_EQ(ext, ".jpg") << "Expected .jpg but got: " << ext;
}

TEST(UtilityTest, MimeToExtensionBmp) {
    std::string ext = util::mime_to_extension("image/bmp");
    EXPECT_EQ(ext, ".bmp");
}

TEST(UtilityTest, MimeToExtensionHtml) {
    std::string ext = util::mime_to_extension("text/html");
    EXPECT_EQ(ext, ".html");
}

TEST(UtilityTest, MimeToExtensionPlain) {
    std::string ext = util::mime_to_extension("text/plain");
    EXPECT_EQ(ext, ".txt");
}

TEST(UtilityTest, MimeToExtensionUnknown) {
    std::string ext = util::mime_to_extension("application/octet-stream");
    // Should return some default
    EXPECT_FALSE(ext.empty());
}

TEST(UtilityTest, MimeToExtensionEmpty) {
    std::string ext = util::mime_to_extension("");
    EXPECT_FALSE(ext.empty());
}

TEST(UtilityTest, EncodeUriList) {
    std::vector<std::string> uris = {"http://example.com", "https://test.org"};
    std::string encoded = util::encode_uri_list(uris);
    EXPECT_NE(encoded.find("http://example.com"), std::string::npos);
    EXPECT_NE(encoded.find("https://test.org"), std::string::npos);
}

TEST(UtilityTest, EncodeUriListSingle) {
    std::string encoded = util::encode_uri_list({"file:///tmp/test.txt"});
    EXPECT_NE(encoded.find("file:///tmp/test.txt"), std::string::npos);
}

TEST(UtilityTest, EncodeUriListEmpty) {
    std::string encoded = util::encode_uri_list({});
    EXPECT_TRUE(encoded.empty());
}

TEST(UtilityTest, DecodeUriList) {
    std::string raw = "http://example.com\r\nhttps://test.org\r\n";
    auto uris = util::decode_uri_list(raw);
    EXPECT_EQ(uris.size(), 2u);
    EXPECT_EQ(uris[0], "http://example.com");
    EXPECT_EQ(uris[1], "https://test.org");
}

TEST(UtilityTest, DecodeUriListSingle) {
    std::string raw = "file:///tmp/test.txt\r\n";
    auto uris = util::decode_uri_list(raw);
    EXPECT_EQ(uris.size(), 1u);
    EXPECT_EQ(uris[0], "file:///tmp/test.txt");
}

TEST(UtilityTest, DecodeUriListEmpty) {
    auto uris = util::decode_uri_list("");
    EXPECT_TRUE(uris.empty());
}

TEST(UtilityTest, DecodeUriListNewline) {
    std::string raw = "http://a.com\nhttp://b.com\n";
    auto uris = util::decode_uri_list(raw);
    EXPECT_EQ(uris.size(), 2u);
}

TEST(UtilityTest, EncodeDecodeRoundTrip) {
    std::vector<std::string> original = {
        "http://example.com",
        "https://test.org/path",
        "file:///home/user/document.txt"
    };
    std::string encoded = util::encode_uri_list(original);
    auto decoded = util::decode_uri_list(encoded);
    EXPECT_EQ(decoded.size(), original.size());
    for (size_t i = 0; i < original.size(); i++) {
        EXPECT_EQ(decoded[i], original[i]);
    }
}

TEST(UtilityTest, PlatformFormatName) {
    // Test various format IDs — should not crash
    std::string name = util::platform_format_name(1);  // CF_TEXT
    EXPECT_FALSE(name.empty());

    name = util::platform_format_name(13); // CF_UNICODETEXT
    EXPECT_FALSE(name.empty());

    name = util::platform_format_name(0);
    EXPECT_FALSE(name.empty());
}

TEST(UtilityTest, PlatformFormatNameInvalid) {
    std::string name = util::platform_format_name(99999);
    EXPECT_FALSE(name.empty());
    EXPECT_NE(name.find("unknown"), std::string::npos);
}

TEST(UtilityTest, SanitizeHtmlBasic) {
    std::string result = util::sanitize_html_for_clipboard("<b>Hello</b>");
    EXPECT_FALSE(result.empty());
}

TEST(UtilityTest, SanitizeHtmlScriptTag) {
    std::string html = "<script>alert('xss')</script><p>safe</p>";
    std::string result = util::sanitize_html_for_clipboard(html);
    EXPECT_EQ(result.find("<script"), std::string::npos);
    EXPECT_NE(result.find("safe"), std::string::npos);
}

TEST(UtilityTest, SanitizeHtmlJavaScriptUri) {
    std::string html = "<a href=\"javascript:alert(1)\">click</a>";
    std::string result = util::sanitize_html_for_clipboard(html);
    EXPECT_EQ(result.find("javascript:"), std::string::npos);
}

TEST(UtilityTest, SanitizeHtmlOnClick) {
    std::string html = "<div onclick=\"alert(1)\">click</div>";
    std::string result = util::sanitize_html_for_clipboard(html);
    EXPECT_EQ(result.find("onclick"), std::string::npos);
}

TEST(UtilityTest, SanitizeHtmlEmpty) {
    std::string result = util::sanitize_html_for_clipboard("");
    EXPECT_TRUE(result.empty());
}

TEST(UtilityTest, SanitizeHtmlComplex) {
    std::string html = R"(<html>
      <head><script>evil()</script></head>
      <body>
        <h1 onmouseover="bad()">Title</h1>
        <p>Safe <b>content</b></p>
        <a href="javascript:void(0)" onclick="hack()">link</a>
      </body>
    </html>)";
    std::string result = util::sanitize_html_for_clipboard(html);
    EXPECT_EQ(result.find("<script"), std::string::npos);
    EXPECT_EQ(result.find("onmouseover"), std::string::npos);
    EXPECT_EQ(result.find("javascript:"), std::string::npos);
    EXPECT_EQ(result.find("onclick"), std::string::npos);
    EXPECT_NE(result.find("Safe"), std::string::npos);
    EXPECT_NE(result.find("content"), std::string::npos);
}

// =============================================================================
// Integration Tests
// =============================================================================

TEST(IntegrationTest, ContentFlowText) {
    // Simulate: platform clipboard -> content descriptor -> remote -> apply
    MockPlatformClipboard cb;
    cb.has_text_result = true;
    cb.text_value = "integration test";

    // Extract content as descriptor
    ClipboardContentDescriptor desc;
    desc.text = cb.get_text();
    desc.available_formats = ContentFormat::TEXT;

    // Apply to synchronizer (simulating remote end)
    ClipboardSynchronizer sync;
    sync.set_mode(ClipboardSynchronizer::Mode::BIDIRECTIONAL);
    sync.apply_remote_content(desc);

    // Verify
    EXPECT_EQ(desc.text, "integration test");
}

TEST(IntegrationTest, DedupIntegration) {
    ContentDeduplicator dedup;

    ClipboardContentDescriptor desc1;
    desc1.text = "dedup test";
    desc1.available_formats = ContentFormat::TEXT;
    ContentHash h1 = ContentDeduplicator::compute_text_hash("dedup test");

    dedup.store(h1, desc1);

    // Simulate same content arriving again
    auto found = dedup.find_by_hash(h1);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->text, "dedup test");
}

TEST(IntegrationTest, FileServerAndFuse) {
    ClipboardFileServer server;
    ClipboardFuseFs fuse;

    ClipboardFileServer::FileEntry entry;
    entry.path = "/virtual/file.txt";
    entry.size = 100;
    entry.cached_data = std::vector<uint8_t>(100, 'F');
    server.add_file(entry);

    fuse.set_file_server(std::make_shared<ClipboardFileServer>(server));
    // Update FUSE contents
    fuse.update_contents(server.list_files());

    // Read via file server
    auto chunk = server.read_chunk("/virtual/file.txt", 0, 50);
    EXPECT_EQ(chunk.size(), 50u);
    EXPECT_EQ(chunk[0], 'F');
}

TEST(IntegrationTest, MonitorSynchronizerInterop) {
    ClipboardMonitor monitor;
    ClipboardSynchronizer sync;
    sync.set_mode(ClipboardSynchronizer::Mode::LOCAL_ONLY);

    int callback_count = 0;
    monitor.set_on_any_change([&](const ClipboardContentDescriptor& desc) {
        callback_count++;
        if (!desc.text.empty()) {
            sync.apply_remote_text(desc.text);
        }
    });

    // No change yet
    EXPECT_EQ(callback_count, 0);
}

TEST(IntegrationTest, ErrorHandlingFlow) {
    MockCliprdrServiceContext ctx;
    ClipboardFile file;
    file.type = ClipboardFile::ERROR;
    file.path = "transfer_failed";

    bool result = ctx.server_clip_file(0, file);
    EXPECT_TRUE(result);
}

TEST(IntegrationTest, ProgressTrackingFlow) {
    MockCliprdrServiceContext ctx;
    ctx.return_progress = true;
    auto progress = ctx.get_progress();
    EXPECT_TRUE(progress.has_value());

    ProgressPercent pp;
    pp.percent = 75.0;
    pp.bytes_processed = 750;
    pp.total_bytes = 1000;
    EXPECT_DOUBLE_EQ(pp.percent, 75.0);
}

// =============================================================================
// Edge Case Tests
// =============================================================================

TEST(EdgeCaseTest, EmptyContentHashAllZeros) {
    ContentHash hash{};
    for (size_t i = 0; i < 32; i++) {
        EXPECT_EQ(hash[i], 0);
    }
}

TEST(EdgeCaseTest, MaxSeqNumber) {
    ClipboardContentDescriptor desc;
    desc.sequence_number = UINT32_MAX;
    EXPECT_EQ(desc.sequence_number, UINT32_MAX);
}

TEST(EdgeCaseTest, NegativeTimestamp) {
    ClipboardContentDescriptor desc;
    desc.timestamp_ms = -1;
    EXPECT_EQ(desc.timestamp_ms, -1);
}

TEST(EdgeCaseTest, VeryLongText) {
    std::string long_str(100000, 'L');
    MockPlatformClipboard cb;
    cb.set_text(long_str);
    EXPECT_EQ(cb.get_text().size(), 100000u);
}

TEST(EdgeCaseTest, ZeroSizeFile) {
    ClipboardFile file;
    file.type = ClipboardFile::DATA;
    file.size = 0;
    file.path = "/dev/null";
    EXPECT_EQ(file.size, 0u);
}

TEST(EdgeCaseTest, EmptyFileList) {
    MockPlatformClipboard cb;
    cb.set_file_list({});
    EXPECT_TRUE(cb.get_file_list().empty());

    ClipboardContentDescriptor desc;
    desc.file_list.clear();
    EXPECT_TRUE(desc.file_list.empty());
}

TEST(EdgeCaseTest, ConcurrentConnIds) {
    MockCliprdrServiceContext ctx;
    bool r1 = ctx.server_clip_file(1, ClipboardFile{});
    bool r2 = ctx.server_clip_file(2, ClipboardFile{});
    bool r3 = ctx.server_clip_file(3, ClipboardFile{});
    EXPECT_TRUE(r1 && r2 && r3);
    EXPECT_EQ(ctx.server_clip_file_calls, 3);
}

TEST(EdgeCaseTest, DelayedRenderingAfterRelease) {
    auto renderer = std::make_shared<MockDelayedRenderer>();
    renderer->release();
    EXPECT_TRUE(renderer->released);
    // Render after release should still work (mock)
    auto data = renderer->render(ContentFormat::TEXT);
    EXPECT_FALSE(data.empty());
}

TEST(EdgeCaseTest, SanitizeHtmlNullBytes) {
    std::string html = "<html>\0<body>\0</body></html>";
    html = std::string(html.c_str()); // trim at null
    std::string result = util::sanitize_html_for_clipboard(html);
    (void)result;
}

TEST(EdgeCaseTest, UnicodeInUrls) {
    std::vector<std::string> uris = {
        "https://example.com/查询",
        "https://例子.测试"
    };
    std::string encoded = util::encode_uri_list(uris);
    auto decoded = util::decode_uri_list(encoded);
    EXPECT_EQ(decoded.size(), uris.size());
}

TEST(EdgeCaseTest, EmptySourceApplication) {
    ClipboardContentDescriptor desc;
    desc.source_application = "";
    EXPECT_TRUE(desc.source_application.empty());
}

// =============================================================================
// Thread Safety Tests
// =============================================================================

TEST(ThreadSafetyTest, ConcurrentDedupStore) {
    ContentDeduplicator dedup;
    std::atomic<int> ops{0};

    auto worker = [&](int start, int count) {
        for (int i = start; i < start + count; i++) {
            ContentHash hash = ContentDeduplicator::compute_text_hash("ts_" + std::to_string(i));
            ClipboardContentDescriptor desc;
            desc.text = "ts_" + std::to_string(i);
            dedup.store(hash, desc);
            ops++;
        }
    };

    std::thread t1(worker, 0, 50);
    std::thread t2(worker, 50, 50);
    std::thread t3(worker, 100, 50);

    t1.join();
    t2.join();
    t3.join();

    EXPECT_EQ(ops.load(), 150);
    EXPECT_LE(dedup.size(), 150u);
}

TEST(ThreadSafetyTest, ConcurrentDedupFind) {
    ContentDeduplicator dedup;
    ContentHash hash = ContentDeduplicator::compute_text_hash("ts_find");
    ClipboardContentDescriptor desc;
    desc.text = "ts_find";
    dedup.store(hash, desc);

    std::atomic<int> found{0};
    auto worker = [&]() {
        for (int i = 0; i < 100; i++) {
            auto result = dedup.find_by_hash(hash);
            if (result.has_value()) found++;
        }
    };

    std::thread t1(worker);
    std::thread t2(worker);
    std::thread t3(worker);

    t1.join();
    t2.join();
    t3.join();

    EXPECT_EQ(found.load(), 300);
}

TEST(ThreadSafetyTest, ConcurrentMonitorStartStop) {
    ClipboardMonitor monitor;
    std::atomic<int> starts{0};
    std::atomic<int> stops{0};

    auto worker = [&]() {
        for (int i = 0; i < 20; i++) {
            monitor.start(std::chrono::milliseconds(10));
            starts++;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            monitor.stop();
            stops++;
        }
    };

    std::thread t1(worker);
    std::thread t2(worker);

    t1.join();
    t2.join();

    EXPECT_EQ(starts.load(), 40);
    EXPECT_EQ(stops.load(), 40);
}

TEST(ThreadSafetyTest, ConcurrentHashComputation) {
    std::atomic<int> done{0};

    auto worker = [&]() {
        for (int i = 0; i < 500; i++) {
            ContentDeduplicator::compute_text_hash("thread_safe_" + std::to_string(i));
        }
        done++;
    };

    std::thread t1(worker);
    std::thread t2(worker);
    std::thread t3(worker);
    std::thread t4(worker);

    t1.join();
    t2.join();
    t3.join();
    t4.join();

    EXPECT_EQ(done.load(), 4);
}

TEST(ThreadSafetyTest, ConcurrentSynchronizerModeChanges) {
    ClipboardSynchronizer sync;
    std::atomic<int> ops{0};

    auto worker = [&](ClipboardSynchronizer::Mode mode) {
        for (int i = 0; i < 100; i++) {
            sync.set_mode(mode);
            ops++;
        }
    };

    std::thread t1(worker, ClipboardSynchronizer::Mode::LOCAL_ONLY);
    std::thread t2(worker, ClipboardSynchronizer::Mode::REMOTE_ONLY);

    t1.join();
    t2.join();

    EXPECT_EQ(ops.load(), 200);
}

// =============================================================================
// Performance / Stress Tests
// =============================================================================

TEST(PerformanceTest, ManyDedupStores) {
    ContentDeduplicator dedup(10000);
    for (int i = 0; i < 1000; i++) {
        ContentHash hash = ContentDeduplicator::compute_text_hash("perf_" + std::to_string(i));
        ClipboardContentDescriptor desc;
        desc.text = "perf_" + std::to_string(i);
        dedup.store(hash, desc);
    }
    EXPECT_GT(dedup.size(), 0u);
}

TEST(PerformanceTest, ManyHashComputations) {
    for (int i = 0; i < 500; i++) {
        ContentDeduplicator::compute_text_hash(std::to_string(i));
    }
}

TEST(PerformanceTest, ManyHashToHexConversions) {
    ContentHash hash{};
    for (int i = 0; i < 500; i++) {
        hash[0] = static_cast<uint8_t>(i);
        ContentDeduplicator::hash_to_hex(hash);
    }
}

TEST(PerformanceTest, ManyFileServerReads) {
    ClipboardFileServer server;
    ClipboardFileServer::FileEntry entry;
    entry.path = "/perf/data.bin";
    entry.size = 10240;
    entry.cached_data = std::vector<uint8_t>(10240, 'P');
    server.add_file(entry);

    for (int i = 0; i < 200; i++) {
        uint64_t offset = (i * 37) % 10240;
        server.read_chunk("/perf/data.bin", offset, 64);
    }
}

TEST(PerformanceTest, ManyDescriptorDescribes) {
    ClipboardContentDescriptor desc;
    desc.available_formats = ContentFormat::ALL;
    desc.sequence_number = 123;
    for (int i = 0; i < 500; i++) {
        desc.describe();
    }
}

TEST(PerformanceTest, ManySanitizeHtmlCalls) {
    std::string html = "<div><b>Test</b><script>alert(1)</script></div>";
    for (int i = 0; i < 100; i++) {
        util::sanitize_html_for_clipboard(html);
    }
}

TEST(PerformanceTest, ManyCfHtmlRoundTrips) {
    std::string html = "<p>Performance test HTML content</p>";
    for (int i = 0; i < 100; i++) {
        std::string cf = util::generate_cf_html(html);
        util::parse_cf_html(cf);
    }
}

TEST(PerformanceTest, ManyProgressPercentOps) {
    ProgressPercent pp;
    for (uint64_t i = 0; i < 10000; i++) {
        pp.percent = static_cast<double>(i % 101);
        pp.bytes_processed = i;
        pp.total_bytes = 10000;
    }
}

TEST(PerformanceTest, ManySetGetTextOperations) {
    MockPlatformClipboard cb;
    for (int i = 0; i < 200; i++) {
        cb.set_text("performance_text_" + std::to_string(i));
        EXPECT_EQ(cb.get_text(), "performance_text_" + std::to_string(i));
    }
    EXPECT_EQ(cb.text_set_calls, 200);
    EXPECT_EQ(cb.text_get_calls, 200);
}

// =============================================================================
// Constructor / Destructor Lifetime Tests
// =============================================================================

TEST(LifetimeTest, MultipleMonitorCreateDestroy) {
    for (int i = 0; i < 5; i++) {
        ClipboardMonitor monitor;
        EXPECT_FALSE(monitor.is_running());
    }
}

TEST(LifetimeTest, MultipleSynchronizerCreateDestroy) {
    for (int i = 0; i < 5; i++) {
        ClipboardSynchronizer sync;
        EXPECT_EQ(sync.get_mode(), ClipboardSynchronizer::Mode::DISABLED);
    }
}

TEST(LifetimeTest, MultipleFileServerCreateDestroy) {
    for (int i = 0; i < 5; i++) {
        ClipboardFileServer server;
        EXPECT_EQ(server.file_count(), 0u);
    }
}

TEST(LifetimeTest, MultipleFuseFsCreateDestroy) {
    for (int i = 0; i < 5; i++) {
        ClipboardFuseFs fs;
        EXPECT_FALSE(fs.is_mounted());
    }
}

TEST(LifetimeTest, MultipleViewerCreateDestroy) {
    for (int i = 0; i < 5; i++) {
        ClipboardViewer viewer;
        EXPECT_FALSE(viewer.is_active());
    }
}

TEST(LifetimeTest, MoveSemanticsPlatformClipboard) {
    auto cb1 = PlatformClipboard::create();
    auto cb2 = PlatformClipboard::create();
    cb1 = std::move(cb2);
    // cb1 should own, cb2 should be nullptr
    EXPECT_NE(cb1, nullptr);
}

TEST(LifetimeTest, MoveSemanticsDedup) {
    ContentDeduplicator d1(100);
    ContentHash hash = ContentDeduplicator::compute_text_hash("move");
    ClipboardContentDescriptor desc;
    desc.text = "move";
    d1.store(hash, desc);
    EXPECT_EQ(d1.size(), 1u);

    ContentDeduplicator d2 = std::move(d1);
    // d2 should have the stored entry
    auto found = d2.find_by_hash(hash);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->text, "move");
}

// =============================================================================
// ContentFormat exhaustive tests
// =============================================================================

TEST(ContentFormatExhaustiveTest, AllValuesUnique) {
    std::set<uint32_t> values;
    values.insert(static_cast<uint32_t>(ContentFormat::TEXT));
    values.insert(static_cast<uint32_t>(ContentFormat::HTML));
    values.insert(static_cast<uint32_t>(ContentFormat::RTF));
    values.insert(static_cast<uint32_t>(ContentFormat::IMAGE_PNG));
    values.insert(static_cast<uint32_t>(ContentFormat::IMAGE_TIFF));
    values.insert(static_cast<uint32_t>(ContentFormat::IMAGE_BMP));
    values.insert(static_cast<uint32_t>(ContentFormat::IMAGE_DIB));
    values.insert(static_cast<uint32_t>(ContentFormat::IMAGE_DIBV5));
    values.insert(static_cast<uint32_t>(ContentFormat::FILE_LIST));
    values.insert(static_cast<uint32_t>(ContentFormat::URI_LIST));
    EXPECT_EQ(values.size(), 10u);
}

TEST(ContentFormatExhaustiveTest, AllTextSubsetOfAll) {
    uint32_t all_text = static_cast<uint32_t>(ContentFormat::ALL_TEXT);
    uint32_t all = static_cast<uint32_t>(ContentFormat::ALL);
    EXPECT_EQ(all_text & ~all, 0u);
}

TEST(ContentFormatExhaustiveTest, AllImageSubsetOfAll) {
    uint32_t all_image = static_cast<uint32_t>(ContentFormat::ALL_IMAGE);
    uint32_t all = static_cast<uint32_t>(ContentFormat::ALL);
    EXPECT_EQ(all_image & ~all, 0u);
}

TEST(ContentFormatExhaustiveTest, AnyTextNotInImage) {
    uint32_t all_text = static_cast<uint32_t>(ContentFormat::ALL_TEXT);
    uint32_t all_image = static_cast<uint32_t>(ContentFormat::ALL_IMAGE);
    EXPECT_EQ(all_text & all_image, 0u);
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
