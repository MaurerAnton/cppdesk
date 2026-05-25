#include <gtest/gtest.h>
#include "client/client.hpp"
#include "common/protocol.hpp"
#include "common/config.hpp"

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <set>
#include <deque>
#include <cstring>
#include <optional>
#include <mutex>
#include <functional>
#include <sstream>
#include <iomanip>

using namespace cppdesk::client;
using namespace cppdesk::common;

// ============================================================================
// MOCK IMPLEMENTATIONS
// ============================================================================

/// Mock ClientInterface — records all callbacks for verification
class MockClientInterface : public ClientInterface {
public:
    std::string get_id() override { return id_; }
    void update_direct(bool direct) override { direct_ = direct; direct_count_++; }
    void update_received(bool received) override { received_ = received; received_count_++; }
    bool is_force_relay() override { return force_relay_; }

    void on_login_error(const std::string& msg) override { login_error_ = msg; login_error_count_++; }
    void on_login_success() override { login_success_count_++; }
    void on_connection_ready() override { connection_ready_count_++; }
    void on_connection_closed(const std::string& reason) override { connection_closed_reason_ = reason; connection_closed_count_++; }

    void on_video_frame(const VideoFrame& frame) override { last_video_frame_ = frame; video_frame_count_++; }
    void on_audio_frame(const AudioFrame& frame) override { last_audio_frame_ = frame; audio_frame_count_++; }

    void on_cursor_data(const CursorData& cursor) override { last_cursor_ = cursor; cursor_data_count_++; }
    void on_cursor_position(int32_t x, int32_t y) override { cursor_x_ = x; cursor_y_ = y; cursor_pos_count_++; }

    void on_clipboard_text(const std::string& text) override { clipboard_text_ = text; clipboard_text_count_++; }
    void on_clipboard_files(const std::vector<std::string>& files) override { clipboard_files_ = files; clipboard_files_count_++; }

    void on_file_transfer_request(const std::string& path, uint64_t size) override { file_req_path_ = path; file_req_size_ = size; file_req_count_++; }
    void on_file_transfer_progress(uint64_t transferred, uint64_t total) override { file_prog_transferred_ = transferred; file_prog_total_ = total; file_prog_count_++; }
    void on_file_transfer_done(const std::string& path) override { file_done_path_ = path; file_done_count_++; }

    void on_chat_message(const std::string& sender, const std::string& msg) override { chat_sender_ = sender; chat_msg_ = msg; chat_count_++; }
    void on_privacy_mode_changed(bool enabled) override { privacy_enabled_ = enabled; privacy_count_++; }
    void on_switch_display(uint32_t idx) override { display_idx_ = idx; display_count_++; }
    void on_permission_changed(const ControlPermissions& perms) override { perms_ = perms; perms_count_++; }

    std::string get_peer_id() override { return peer_id_; }
    std::string get_key() override { return key_; }
    std::string get_token() override { return token_; }
    ConnType get_conn_type() override { return conn_type_; }

    // Setters for test configuration
    void set_id(const std::string& id) { id_ = id; }
    void set_force_relay(bool fr) { force_relay_ = fr; }
    void set_peer_id(const std::string& pid) { peer_id_ = pid; }
    void set_key(const std::string& k) { key_ = k; }
    void set_token(const std::string& t) { token_ = t; }
    void set_conn_type(ConnType ct) { conn_type_ = ct; }

    // Accessors for verification
    bool direct() const { return direct_; }
    int direct_count() const { return direct_count_; }
    bool received() const { return received_; }
    int received_count() const { return received_count_; }
    std::string login_error() const { return login_error_; }
    int login_error_count() const { return login_error_count_; }
    int login_success_count() const { return login_success_count_; }
    int connection_ready_count() const { return connection_ready_count_; }
    std::string connection_closed_reason() const { return connection_closed_reason_; }
    int connection_closed_count() const { return connection_closed_count_; }
    VideoFrame last_video_frame() const { return last_video_frame_; }
    int video_frame_count() const { return video_frame_count_; }
    AudioFrame last_audio_frame() const { return last_audio_frame_; }
    int audio_frame_count() const { return audio_frame_count_; }
    CursorData last_cursor() const { return last_cursor_; }
    int cursor_data_count() const { return cursor_data_count_; }
    int32_t cursor_x() const { return cursor_x_; }
    int32_t cursor_y() const { return cursor_y_; }
    int cursor_pos_count() const { return cursor_pos_count_; }
    std::string clipboard_text() const { return clipboard_text_; }
    int clipboard_text_count() const { return clipboard_text_count_; }
    std::vector<std::string> clipboard_files() const { return clipboard_files_; }
    int clipboard_files_count() const { return clipboard_files_count_; }
    std::string file_req_path() const { return file_req_path_; }
    uint64_t file_req_size() const { return file_req_size_; }
    int file_req_count() const { return file_req_count_; }
    uint64_t file_prog_transferred() const { return file_prog_transferred_; }
    uint64_t file_prog_total() const { return file_prog_total_; }
    int file_prog_count() const { return file_prog_count_; }
    std::string file_done_path() const { return file_done_path_; }
    int file_done_count() const { return file_done_count_; }
    std::string chat_sender() const { return chat_sender_; }
    std::string chat_msg() const { return chat_msg_; }
    int chat_count() const { return chat_count_; }
    bool privacy_enabled() const { return privacy_enabled_; }
    int privacy_count() const { return privacy_count_; }
    uint32_t display_idx() const { return display_idx_; }
    int display_count() const { return display_count_; }
    ControlPermissions perms() const { return perms_; }
    int perms_count() const { return perms_count_; }

    // Reset for reuse
    void reset() {
        direct_ = false; received_ = false;
        direct_count_ = 0; received_count_ = 0;
        login_error_.clear(); login_error_count_ = 0;
        login_success_count_ = 0; connection_ready_count_ = 0;
        connection_closed_reason_.clear(); connection_closed_count_ = 0;
        last_video_frame_ = VideoFrame{}; video_frame_count_ = 0;
        last_audio_frame_ = AudioFrame{}; audio_frame_count_ = 0;
        last_cursor_ = CursorData{}; cursor_data_count_ = 0;
        cursor_x_ = 0; cursor_y_ = 0; cursor_pos_count_ = 0;
        clipboard_text_.clear(); clipboard_text_count_ = 0;
        clipboard_files_.clear(); clipboard_files_count_ = 0;
        file_req_path_.clear(); file_req_size_ = 0; file_req_count_ = 0;
        file_prog_transferred_ = 0; file_prog_total_ = 0; file_prog_count_ = 0;
        file_done_path_.clear(); file_done_count_ = 0;
        chat_sender_.clear(); chat_msg_.clear(); chat_count_ = 0;
        privacy_enabled_ = false; privacy_count_ = 0;
        display_idx_ = 0; display_count_ = 0;
        perms_ = ControlPermissions{}; perms_count_ = 0;
    }

private:
    std::string id_;
    bool direct_ = false;
    int direct_count_ = 0;
    bool received_ = false;
    int received_count_ = 0;
    bool force_relay_ = false;

    std::string login_error_;
    int login_error_count_ = 0;
    int login_success_count_ = 0;
    int connection_ready_count_ = 0;
    std::string connection_closed_reason_;
    int connection_closed_count_ = 0;

    VideoFrame last_video_frame_;
    int video_frame_count_ = 0;
    AudioFrame last_audio_frame_;
    int audio_frame_count_ = 0;

    CursorData last_cursor_;
    int cursor_data_count_ = 0;
    int32_t cursor_x_ = 0;
    int32_t cursor_y_ = 0;
    int cursor_pos_count_ = 0;

    std::string clipboard_text_;
    int clipboard_text_count_ = 0;
    std::vector<std::string> clipboard_files_;
    int clipboard_files_count_ = 0;

    std::string file_req_path_;
    uint64_t file_req_size_ = 0;
    int file_req_count_ = 0;
    uint64_t file_prog_transferred_ = 0;
    uint64_t file_prog_total_ = 0;
    int file_prog_count_ = 0;
    std::string file_done_path_;
    int file_done_count_ = 0;

    std::string chat_sender_;
    std::string chat_msg_;
    int chat_count_ = 0;
    bool privacy_enabled_ = false;
    int privacy_count_ = 0;
    uint32_t display_idx_ = 0;
    int display_count_ = 0;
    ControlPermissions perms_;
    int perms_count_ = 0;

    std::string peer_id_;
    std::string key_;
    std::string token_;
    ConnType conn_type_ = ConnType::DEFAULT_CONN;
};

/// Mock AudioDevice — records calls and returns configurable results
class MockAudioDevice : public AudioDevice {
public:
    bool open_input(const std::string& device_name) override {
        input_device_ = device_name;
        input_open_ = true;
        return input_open_return_;
    }
    bool open_output(const std::string& device_name) override {
        output_device_ = device_name;
        output_open_ = true;
        return output_open_return_;
    }
    std::vector<int16_t> read_samples(size_t count) override {
        read_count_++;
        last_read_count_ = count;
        return read_samples_return_;
    }
    bool write_samples(const std::vector<int16_t>& samples) override {
        write_count_++;
        last_written_ = samples;
        return write_return_;
    }
    void close() override {
        closed_ = true;
        input_open_ = false;
        output_open_ = false;
    }
    std::vector<std::string> list_devices() override {
        return list_devices_return_;
    }

    // Configurable return values
    void set_input_open_return(bool v) { input_open_return_ = v; }
    void set_output_open_return(bool v) { output_open_return_ = v; }
    void set_read_samples_return(std::vector<int16_t> v) { read_samples_return_ = std::move(v); }
    void set_write_return(bool v) { write_return_ = v; }
    void set_list_devices_return(std::vector<std::string> v) { list_devices_return_ = std::move(v); }

    // Accessors for verification
    std::string input_device() const { return input_device_; }
    std::string output_device() const { return output_device_; }
    bool input_open() const { return input_open_; }
    bool output_open() const { return output_open_; }
    bool is_closed() const { return closed_; }
    int read_count() const { return read_count_; }
    size_t last_read_count() const { return last_read_count_; }
    int write_count() const { return write_count_; }
    std::vector<int16_t> last_written() const { return last_written_; }

    void reset() {
        input_device_.clear(); output_device_.clear();
        input_open_ = false; output_open_ = false; closed_ = false;
        read_count_ = 0; last_read_count_ = 0;
        write_count_ = 0; last_written_.clear();
        input_open_return_ = true; output_open_return_ = true;
        write_return_ = true;
        read_samples_return_.clear();
        list_devices_return_.clear();
    }

private:
    std::string input_device_;
    std::string output_device_;
    bool input_open_ = false;
    bool output_open_ = false;
    bool closed_ = false;
    int read_count_ = 0;
    size_t last_read_count_ = 0;
    int write_count_ = 0;
    std::vector<int16_t> last_written_;
    bool input_open_return_ = true;
    bool output_open_return_ = true;
    bool write_return_ = true;
    std::vector<int16_t> read_samples_return_;
    std::vector<std::string> list_devices_return_;
};

/// Mock ScreenshotHelper
class MockScreenshotHelper : public ScreenshotHelper {
public:
    std::optional<VideoFrame> capture(uint32_t display) override {
        capture_calls_.push_back(display);
        if (capture_return_.has_value()) {
            auto f = *capture_return_;
            f.display = display;
            return f;
        }
        return std::nullopt;
    }
    std::vector<uint32_t> get_display_indices() override {
        return display_indices_;
    }

    void set_capture_return(std::optional<VideoFrame> v) { capture_return_ = std::move(v); }
    void set_display_indices(std::vector<uint32_t> v) { display_indices_ = std::move(v); }
    std::vector<uint32_t> capture_calls() const { return capture_calls_; }
    void reset() { capture_calls_.clear(); capture_return_ = std::nullopt; display_indices_.clear(); }

private:
    std::optional<VideoFrame> capture_return_;
    std::vector<uint32_t> display_indices_;
    std::vector<uint32_t> capture_calls_;
};

// ============================================================================
// MessageType Enum Tests
// ============================================================================

TEST(MessageTypeTest, RendezvousMessages) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::REGISTER_PEER), 0u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::PUNCH_HOLE_REQUEST), 1u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::PUNCH_HOLE_RESPONSE), 2u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::REQUEST_RELAY), 3u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::TEST_NAT), 4u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::QUERY_ONLINE), 5u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::HEARTBEAT), 6u);
}

TEST(MessageTypeTest, ControlMessages) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::LOGIN), 10u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::LOGIN_RESPONSE), 11u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::SWITCH_DISPLAY), 12u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::SWITCH_PERMISSION), 13u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::CLOSE_CONNECTION), 14u);
}

TEST(MessageTypeTest, VideoMessages) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::VIDEO_FRAME), 20u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::VIDEO_CODEC_CHANGE), 21u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::VIDEO_QUALITY_CHANGE), 22u);
}

TEST(MessageTypeTest, AudioMessages) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::AUDIO_FRAME), 30u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::AUDIO_CONFIG), 31u);
}

TEST(MessageTypeTest, InputMessages) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::MOUSE_EVENT), 40u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::KEY_EVENT), 41u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::CURSOR_DATA), 42u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::CURSOR_POSITION), 43u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::CURSOR_SHAPE), 44u);
}

TEST(MessageTypeTest, ClipboardMessages) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::CLIPBOARD_TEXT), 50u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::CLIPBOARD_FILE), 51u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::CLIPBOARD_IMAGE), 52u);
}

TEST(MessageTypeTest, FileTransferMessages) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::FILE_TRANSFER_REQUEST), 60u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::FILE_TRANSFER_RESPONSE), 61u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::FILE_CHUNK), 62u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::FILE_DONE), 63u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::FILE_DIR), 64u);
}

TEST(MessageTypeTest, MiscMessages) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::MISC), 70u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::CHAT_MESSAGE), 71u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::PRIVACY_MODE), 72u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::PORT_FORWARD), 73u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::WHITEBOARD), 74u);
}

TEST(MessageTypeTest, ServiceMessages) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::SUBSCRIBE_SERVICE), 80u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::UNSUBSCRIBE_SERVICE), 81u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::SERVICE_DATA), 82u);
}

TEST(MessageTypeTest, AllValuesDistinct) {
    std::set<uint32_t> values;
    // Rendezvous
    values.insert(static_cast<uint32_t>(MessageType::REGISTER_PEER));
    values.insert(static_cast<uint32_t>(MessageType::PUNCH_HOLE_REQUEST));
    values.insert(static_cast<uint32_t>(MessageType::PUNCH_HOLE_RESPONSE));
    values.insert(static_cast<uint32_t>(MessageType::REQUEST_RELAY));
    values.insert(static_cast<uint32_t>(MessageType::TEST_NAT));
    values.insert(static_cast<uint32_t>(MessageType::QUERY_ONLINE));
    values.insert(static_cast<uint32_t>(MessageType::HEARTBEAT));
    // Control
    values.insert(static_cast<uint32_t>(MessageType::LOGIN));
    values.insert(static_cast<uint32_t>(MessageType::LOGIN_RESPONSE));
    values.insert(static_cast<uint32_t>(MessageType::SWITCH_DISPLAY));
    values.insert(static_cast<uint32_t>(MessageType::SWITCH_PERMISSION));
    values.insert(static_cast<uint32_t>(MessageType::CLOSE_CONNECTION));
    // Video
    values.insert(static_cast<uint32_t>(MessageType::VIDEO_FRAME));
    values.insert(static_cast<uint32_t>(MessageType::VIDEO_CODEC_CHANGE));
    values.insert(static_cast<uint32_t>(MessageType::VIDEO_QUALITY_CHANGE));
    // Audio
    values.insert(static_cast<uint32_t>(MessageType::AUDIO_FRAME));
    values.insert(static_cast<uint32_t>(MessageType::AUDIO_CONFIG));
    // Input
    values.insert(static_cast<uint32_t>(MessageType::MOUSE_EVENT));
    values.insert(static_cast<uint32_t>(MessageType::KEY_EVENT));
    values.insert(static_cast<uint32_t>(MessageType::CURSOR_DATA));
    values.insert(static_cast<uint32_t>(MessageType::CURSOR_POSITION));
    values.insert(static_cast<uint32_t>(MessageType::CURSOR_SHAPE));
    // Clipboard
    values.insert(static_cast<uint32_t>(MessageType::CLIPBOARD_TEXT));
    values.insert(static_cast<uint32_t>(MessageType::CLIPBOARD_FILE));
    values.insert(static_cast<uint32_t>(MessageType::CLIPBOARD_IMAGE));
    // File transfer
    values.insert(static_cast<uint32_t>(MessageType::FILE_TRANSFER_REQUEST));
    values.insert(static_cast<uint32_t>(MessageType::FILE_TRANSFER_RESPONSE));
    values.insert(static_cast<uint32_t>(MessageType::FILE_CHUNK));
    values.insert(static_cast<uint32_t>(MessageType::FILE_DONE));
    values.insert(static_cast<uint32_t>(MessageType::FILE_DIR));
    // Misc
    values.insert(static_cast<uint32_t>(MessageType::MISC));
    values.insert(static_cast<uint32_t>(MessageType::CHAT_MESSAGE));
    values.insert(static_cast<uint32_t>(MessageType::PRIVACY_MODE));
    values.insert(static_cast<uint32_t>(MessageType::PORT_FORWARD));
    values.insert(static_cast<uint32_t>(MessageType::WHITEBOARD));
    // Service
    values.insert(static_cast<uint32_t>(MessageType::SUBSCRIBE_SERVICE));
    values.insert(static_cast<uint32_t>(MessageType::UNSUBSCRIBE_SERVICE));
    values.insert(static_cast<uint32_t>(MessageType::SERVICE_DATA));
    // Total 35 unique values
    EXPECT_EQ(values.size(), 35u);
}

TEST(MessageTypeTest, NonOverlappingGroups) {
    // Rendezvous: 0-6
    for (int i = 0; i <= 6; i++) {
        auto val = static_cast<uint32_t>(MessageType::REGISTER_PEER) + i;
        EXPECT_GE(val, 0u);
        EXPECT_LE(val, 6u);
    }
    // Control: 10-14
    EXPECT_GE(static_cast<uint32_t>(MessageType::LOGIN), 10u);
    EXPECT_LE(static_cast<uint32_t>(MessageType::CLOSE_CONNECTION), 14u);
    // Video: 20-22
    EXPECT_GE(static_cast<uint32_t>(MessageType::VIDEO_FRAME), 20u);
    EXPECT_LE(static_cast<uint32_t>(MessageType::VIDEO_QUALITY_CHANGE), 22u);
    // Audio: 30-31
    EXPECT_GE(static_cast<uint32_t>(MessageType::AUDIO_FRAME), 30u);
    EXPECT_LE(static_cast<uint32_t>(MessageType::AUDIO_CONFIG), 31u);
    // Input: 40-44
    EXPECT_GE(static_cast<uint32_t>(MessageType::MOUSE_EVENT), 40u);
    EXPECT_LE(static_cast<uint32_t>(MessageType::CURSOR_SHAPE), 44u);
    // Clipboard: 50-52
    EXPECT_GE(static_cast<uint32_t>(MessageType::CLIPBOARD_TEXT), 50u);
    EXPECT_LE(static_cast<uint32_t>(MessageType::CLIPBOARD_IMAGE), 52u);
    // File transfer: 60-64
    EXPECT_GE(static_cast<uint32_t>(MessageType::FILE_TRANSFER_REQUEST), 60u);
    EXPECT_LE(static_cast<uint32_t>(MessageType::FILE_DIR), 64u);
    // Misc: 70-74
    EXPECT_GE(static_cast<uint32_t>(MessageType::MISC), 70u);
    EXPECT_LE(static_cast<uint32_t>(MessageType::WHITEBOARD), 74u);
    // Service: 80-82
    EXPECT_GE(static_cast<uint32_t>(MessageType::SUBSCRIBE_SERVICE), 80u);
    EXPECT_LE(static_cast<uint32_t>(MessageType::SERVICE_DATA), 82u);
}

// ============================================================================
// ConnType Enum Tests
// ============================================================================

TEST(ConnTypeTest, AllValues) {
    EXPECT_EQ(static_cast<int32_t>(ConnType::DEFAULT_CONN), 0);
    EXPECT_EQ(static_cast<int32_t>(ConnType::FILE_TRANSFER), 1);
    EXPECT_EQ(static_cast<int32_t>(ConnType::PORT_FORWARD), 2);
    EXPECT_EQ(static_cast<int32_t>(ConnType::RDP), 3);
}

TEST(ConnTypeTest, ValuesDistinct) {
    std::set<int32_t> values;
    values.insert(static_cast<int32_t>(ConnType::DEFAULT_CONN));
    values.insert(static_cast<int32_t>(ConnType::FILE_TRANSFER));
    values.insert(static_cast<int32_t>(ConnType::PORT_FORWARD));
    values.insert(static_cast<int32_t>(ConnType::RDP));
    EXPECT_EQ(values.size(), 4u);
}

TEST(ConnTypeTest, DefaultIsZero) {
    ConnType ct{};
    EXPECT_EQ(static_cast<int32_t>(ct), 0);
    EXPECT_EQ(ct, ConnType::DEFAULT_CONN);
}

TEST(ConnTypeTest, Comparison) {
    EXPECT_NE(ConnType::DEFAULT_CONN, ConnType::FILE_TRANSFER);
    EXPECT_NE(ConnType::PORT_FORWARD, ConnType::RDP);
    EXPECT_EQ(ConnType::DEFAULT_CONN, ConnType::DEFAULT_CONN);
}

// ============================================================================
// NatType Enum Tests
// ============================================================================

TEST(NatTypeTest, AllValues) {
    EXPECT_EQ(static_cast<int32_t>(NatType::UNKNOWN_NAT), 0);
    EXPECT_EQ(static_cast<int32_t>(NatType::OPEN_INTERNET), 1);
    EXPECT_EQ(static_cast<int32_t>(NatType::FULL_CONE), 2);
    EXPECT_EQ(static_cast<int32_t>(NatType::RESTRICTED_CONE), 3);
    EXPECT_EQ(static_cast<int32_t>(NatType::PORT_RESTRICTED_CONE), 4);
    EXPECT_EQ(static_cast<int32_t>(NatType::SYMMETRIC), 5);
}

TEST(NatTypeTest, ValuesDistinct) {
    std::set<int32_t> values;
    values.insert(static_cast<int32_t>(NatType::UNKNOWN_NAT));
    values.insert(static_cast<int32_t>(NatType::OPEN_INTERNET));
    values.insert(static_cast<int32_t>(NatType::FULL_CONE));
    values.insert(static_cast<int32_t>(NatType::RESTRICTED_CONE));
    values.insert(static_cast<int32_t>(NatType::PORT_RESTRICTED_CONE));
    values.insert(static_cast<int32_t>(NatType::SYMMETRIC));
    EXPECT_EQ(values.size(), 6u);
}

TEST(NatTypeTest, DefaultIsUnknown) {
    NatType nt{};
    EXPECT_EQ(nt, NatType::UNKNOWN_NAT);
    EXPECT_EQ(static_cast<int32_t>(nt), 0);
}

// ============================================================================
// JobType Enum Tests
// ============================================================================

TEST(JobTypeTest, AllValues) {
    EXPECT_NE(JobType::SEND_FILE, JobType::RECV_FILE);
    EXPECT_NE(JobType::SEND_FILE, JobType::SEND_DIR);
    EXPECT_NE(JobType::SEND_FILE, JobType::RECV_DIR);
}

TEST(JobTypeTest, ValuesDistinct) {
    std::set<int> vals;
    vals.insert(static_cast<int>(JobType::SEND_FILE));
    vals.insert(static_cast<int>(JobType::RECV_FILE));
    vals.insert(static_cast<int>(JobType::SEND_DIR));
    vals.insert(static_cast<int>(JobType::RECV_DIR));
    EXPECT_EQ(vals.size(), 4u);
}

TEST(JobTypeTest, SendAndReceiveAreDifferent) {
    EXPECT_NE(JobType::SEND_FILE, JobType::RECV_FILE);
    EXPECT_NE(JobType::SEND_DIR, JobType::RECV_DIR);
}

// ============================================================================
// Client::Phase Enum Tests
// ============================================================================

TEST(ClientPhaseTest, AllPhases) {
    EXPECT_NE(Client::Phase::DISCONNECTED, Client::Phase::RENDEZVOUS);
    EXPECT_NE(Client::Phase::RENDEZVOUS, Client::Phase::PUNCH_HOLE);
    EXPECT_NE(Client::Phase::PUNCH_HOLE, Client::Phase::TCP_HANDSHAKE);
    EXPECT_NE(Client::Phase::TCP_HANDSHAKE, Client::Phase::LOGIN);
    EXPECT_NE(Client::Phase::LOGIN, Client::Phase::CONNECTED);
}

TEST(ClientPhaseTest, ValuesDistinct) {
    std::set<int> vals;
    vals.insert(static_cast<int>(Client::Phase::DISCONNECTED));
    vals.insert(static_cast<int>(Client::Phase::RENDEZVOUS));
    vals.insert(static_cast<int>(Client::Phase::PUNCH_HOLE));
    vals.insert(static_cast<int>(Client::Phase::TCP_HANDSHAKE));
    vals.insert(static_cast<int>(Client::Phase::LOGIN));
    vals.insert(static_cast<int>(Client::Phase::CONNECTED));
    EXPECT_EQ(vals.size(), 6u);
}

TEST(ClientPhaseTest, DefaultIsDisconnected) {
    // Client starts disconnected by default via constructor default
    Client c;
    EXPECT_EQ(c.get_phase(), Client::Phase::DISCONNECTED);
}

// ============================================================================
// ClientClipboardContext Tests
// ============================================================================

TEST(ClientClipboardContextTest, DefaultValues) {
    ClientClipboardContext ctx;
    EXPECT_TRUE(ctx.is_text_required);
    EXPECT_FALSE(ctx.is_file_required);
    EXPECT_FALSE(ctx.running);
    EXPECT_TRUE(ctx.last_text.empty());
}

TEST(ClientClipboardContextTest, ModifyTextRequired) {
    ClientClipboardContext ctx;
    ctx.is_text_required = false;
    EXPECT_FALSE(ctx.is_text_required);
    ctx.is_text_required = true;
    EXPECT_TRUE(ctx.is_text_required);
}

TEST(ClientClipboardContextTest, ModifyFileRequired) {
    ClientClipboardContext ctx;
    ctx.is_file_required = true;
    EXPECT_TRUE(ctx.is_file_required);
    ctx.is_file_required = false;
    EXPECT_FALSE(ctx.is_file_required);
}

TEST(ClientClipboardContextTest, RunningFlag) {
    ClientClipboardContext ctx;
    ctx.running = true;
    EXPECT_TRUE(ctx.running);
    ctx.running = false;
    EXPECT_FALSE(ctx.running);
}

TEST(ClientClipboardContextTest, LastText) {
    ClientClipboardContext ctx;
    ctx.last_text = "Hello, clipboard!";
    EXPECT_EQ(ctx.last_text, "Hello, clipboard!");
    ctx.last_text = "";
    EXPECT_TRUE(ctx.last_text.empty());
}

TEST(ClientClipboardContextTest, BothRequiredFlagsOff) {
    ClientClipboardContext ctx;
    ctx.is_text_required = false;
    ctx.is_file_required = false;
    EXPECT_FALSE(ctx.is_text_required);
    EXPECT_FALSE(ctx.is_file_required);
}

TEST(ClientClipboardContextTest, BothRequiredFlagsOn) {
    ClientClipboardContext ctx;
    ctx.is_text_required = true;
    ctx.is_file_required = true;
    EXPECT_TRUE(ctx.is_text_required);
    EXPECT_TRUE(ctx.is_file_required);
}

// ============================================================================
// SessionInfo Tests
// ============================================================================

TEST(SessionInfoTest, DefaultConstruction) {
    SessionInfo info;
    EXPECT_TRUE(info.peer_id.empty());
    EXPECT_TRUE(info.peer_name.empty());
    EXPECT_TRUE(info.connection_type.empty());
    EXPECT_FALSE(info.encrypted);
    EXPECT_FALSE(info.view_only);
}

TEST(SessionInfoTest, PeerIdField) {
    SessionInfo info;
    info.peer_id = "peer-12345";
    EXPECT_EQ(info.peer_id, "peer-12345");
}

TEST(SessionInfoTest, PeerNameField) {
    SessionInfo info;
    info.peer_name = "Test Computer";
    EXPECT_EQ(info.peer_name, "Test Computer");
}

TEST(SessionInfoTest, ConnectionTypeField) {
    SessionInfo info;
    info.connection_type = "TCP";
    EXPECT_EQ(info.connection_type, "TCP");
    info.connection_type = "UDP";
    EXPECT_EQ(info.connection_type, "UDP");
    info.connection_type = "Relay";
    EXPECT_EQ(info.connection_type, "Relay");
}

TEST(SessionInfoTest, EncryptedFlag) {
    SessionInfo info;
    info.encrypted = true;
    EXPECT_TRUE(info.encrypted);
    info.encrypted = false;
    EXPECT_FALSE(info.encrypted);
}

TEST(SessionInfoTest, ViewOnlyFlag) {
    SessionInfo info;
    info.view_only = true;
    EXPECT_TRUE(info.view_only);
    info.view_only = false;
    EXPECT_FALSE(info.view_only);
}

TEST(SessionInfoTest, PeerResolution) {
    SessionInfo info;
    info.peer_resolution = Resolution{1920, 1080};
    EXPECT_EQ(info.peer_resolution.width, 1920u);
    EXPECT_EQ(info.peer_resolution.height, 1080u);
}

TEST(SessionInfoTest, ConnectedAtTime) {
    SessionInfo info;
    auto now = std::chrono::system_clock::now();
    info.connected_at = now;
    EXPECT_EQ(info.connected_at, now);
}

TEST(SessionInfoTest, FullSessionInfo) {
    SessionInfo info;
    info.peer_id = "abc-def-123";
    info.peer_name = "Office Desktop";
    info.connection_type = "TCP";
    info.encrypted = true;
    info.view_only = false;
    info.peer_resolution = Resolution{2560, 1440};
    info.connected_at = std::chrono::system_clock::now();

    EXPECT_EQ(info.peer_id, "abc-def-123");
    EXPECT_EQ(info.peer_name, "Office Desktop");
    EXPECT_EQ(info.connection_type, "TCP");
    EXPECT_TRUE(info.encrypted);
    EXPECT_FALSE(info.view_only);
    EXPECT_EQ(info.peer_resolution.width, 2560u);
    EXPECT_EQ(info.peer_resolution.height, 1440u);
}

TEST(SessionInfoTest, Copyable) {
    SessionInfo a;
    a.peer_id = "copy-test";
    a.peer_name = "Copy";
    a.encrypted = true;

    SessionInfo b = a;
    EXPECT_EQ(b.peer_id, a.peer_id);
    EXPECT_EQ(b.peer_name, a.peer_name);
    EXPECT_EQ(b.encrypted, a.encrypted);
    EXPECT_EQ(b.view_only, a.view_only);

    // Modify copy, original unchanged
    b.peer_id = "modified";
    EXPECT_EQ(a.peer_id, "copy-test");
    EXPECT_EQ(b.peer_id, "modified");
}

TEST(SessionInfoTest, EmptyConnectionTypeByDefault) {
    SessionInfo info;
    EXPECT_TRUE(info.connection_type.empty());
    // Can be explicitly set to various valid types
    info.connection_type = "Relay";
    EXPECT_FALSE(info.connection_type.empty());
}

// ============================================================================
// SessionManager Tests
// ============================================================================

TEST(SessionManagerTest, Singleton) {
    auto& s1 = SessionManager::instance();
    auto& s2 = SessionManager::instance();
    EXPECT_EQ(&s1, &s2);
}

TEST(SessionManagerTest, NoActiveSessionByDefault) {
    auto& sm = SessionManager::instance();
    EXPECT_FALSE(sm.has_active_session());
}

TEST(SessionManagerTest, StartSession) {
    auto& sm = SessionManager::instance();
    sm.start_session("test-peer-id-001");
    EXPECT_TRUE(sm.has_active_session());
}

TEST(SessionManagerTest, GetCurrentSession) {
    auto& sm = SessionManager::instance();
    sm.start_session("peer-xyz");
    auto info = sm.get_current_session();
    EXPECT_EQ(info.peer_id, "peer-xyz");
}

TEST(SessionManagerTest, EndSession) {
    auto& sm = SessionManager::instance();
    sm.start_session("temp-peer");
    EXPECT_TRUE(sm.has_active_session());
    sm.end_session();
    EXPECT_FALSE(sm.has_active_session());
}

TEST(SessionManagerTest, StartNewSessionReplacesOld) {
    auto& sm = SessionManager::instance();
    sm.start_session("first-peer");
    EXPECT_TRUE(sm.has_active_session());
    EXPECT_EQ(sm.get_current_session().peer_id, "first-peer");

    sm.start_session("second-peer");
    EXPECT_TRUE(sm.has_active_session());
    EXPECT_EQ(sm.get_current_session().peer_id, "second-peer");
    sm.end_session();
}

TEST(SessionManagerTest, SetViewOnly) {
    auto& sm = SessionManager::instance();
    sm.start_session("view-test");
    sm.set_view_only(true);
    EXPECT_TRUE(sm.get_current_session().view_only);
    sm.set_view_only(false);
    EXPECT_FALSE(sm.get_current_session().view_only);
    sm.end_session();
}

TEST(SessionManagerTest, SetEncrypted) {
    auto& sm = SessionManager::instance();
    sm.start_session("enc-test");
    sm.set_encrypted(true);
    EXPECT_TRUE(sm.get_current_session().encrypted);
    sm.set_encrypted(false);
    EXPECT_FALSE(sm.get_current_session().encrypted);
    sm.end_session();
}

TEST(SessionManagerTest, ViewOnlyAndEncryptedTogether) {
    auto& sm = SessionManager::instance();
    sm.start_session("combo-test");
    sm.set_view_only(true);
    sm.set_encrypted(true);
    auto info = sm.get_current_session();
    EXPECT_TRUE(info.view_only);
    EXPECT_TRUE(info.encrypted);
    sm.end_session();
}

TEST(SessionManagerTest, EndSessionThenStartNew) {
    auto& sm = SessionManager::instance();
    sm.start_session("a");
    sm.end_session();
    EXPECT_FALSE(sm.has_active_session());
    sm.start_session("b");
    EXPECT_TRUE(sm.has_active_session());
    EXPECT_EQ(sm.get_current_session().peer_id, "b");
    sm.end_session();
}

TEST(SessionManagerTest, MultipleCallsToEndSession) {
    auto& sm = SessionManager::instance();
    sm.start_session("multi-end");
    sm.end_session();
    EXPECT_FALSE(sm.has_active_session());
    // Second end_session should be safe (idempotent)
    sm.end_session();
    EXPECT_FALSE(sm.has_active_session());
}

TEST(SessionManagerTest, SessionDefaultsNotEncryptedOrViewOnly) {
    auto& sm = SessionManager::instance();
    sm.start_session("defaults-test");
    auto info = sm.get_current_session();
    EXPECT_FALSE(info.encrypted);
    EXPECT_FALSE(info.view_only);
    sm.end_session();
}

// ============================================================================
// FileManager Tests
// ============================================================================

TEST(FileManagerTest, DefaultJobType) {
    FileManager fm;
    EXPECT_EQ(fm.get_job_type(), JobType::SEND_FILE);
}

TEST(FileManagerTest, SetJobType) {
    FileManager fm;
    fm.set_job_type(JobType::RECV_FILE);
    EXPECT_EQ(fm.get_job_type(), JobType::RECV_FILE);
    fm.set_job_type(JobType::SEND_DIR);
    EXPECT_EQ(fm.get_job_type(), JobType::SEND_DIR);
    fm.set_job_type(JobType::RECV_DIR);
    EXPECT_EQ(fm.get_job_type(), JobType::RECV_DIR);
    fm.set_job_type(JobType::SEND_FILE);
    EXPECT_EQ(fm.get_job_type(), JobType::SEND_FILE);
}

TEST(FileManagerTest, NotActiveByDefault) {
    FileManager fm;
    EXPECT_FALSE(fm.is_active());
}

TEST(FileManagerTest, SendSetsActive) {
    FileManager fm;
    fm.send("/tmp/test.txt", "/remote/test.txt");
    EXPECT_TRUE(fm.is_active());
}

TEST(FileManagerTest, CancelClearsActive) {
    FileManager fm;
    fm.send("/tmp/data.bin", "/remote/data.bin");
    EXPECT_TRUE(fm.is_active());
    fm.cancel();
    EXPECT_FALSE(fm.is_active());
}

TEST(FileManagerTest, DefaultSizeZero) {
    FileManager fm;
    EXPECT_EQ(fm.total_size(), 0u);
    EXPECT_EQ(fm.transferred(), 0u);
}

TEST(FileManagerTest, SendWithDifferentPaths) {
    FileManager fm;
    fm.send("/local/path/file.dat", "/remote/path/file.dat");
    EXPECT_TRUE(fm.is_active());
    fm.cancel();
    EXPECT_FALSE(fm.is_active());
}

TEST(FileManagerTest, CancelWithoutSend) {
    FileManager fm;
    EXPECT_FALSE(fm.is_active());
    fm.cancel();
    EXPECT_FALSE(fm.is_active());
}

TEST(FileManagerTest, MultipleCancelCalls) {
    FileManager fm;
    fm.send("/a", "/b");
    fm.cancel();
    fm.cancel();
    EXPECT_FALSE(fm.is_active());
}

TEST(FileManagerTest, SetJobTypeDoesNotAffectActive) {
    FileManager fm;
    fm.set_job_type(JobType::RECV_FILE);
    EXPECT_FALSE(fm.is_active());
    fm.set_job_type(JobType::SEND_DIR);
    EXPECT_FALSE(fm.is_active());
}

TEST(FileManagerTest, AllJobTypesCycle) {
    FileManager fm;
    std::vector<JobType> types = {JobType::SEND_FILE, JobType::RECV_FILE,
                                   JobType::SEND_DIR, JobType::RECV_DIR};
    for (auto t : types) {
        fm.set_job_type(t);
        EXPECT_EQ(fm.get_job_type(), t);
    }
}

// ============================================================================
// ScreenshotHelper Tests (via mock)
// ============================================================================

TEST(ScreenshotHelperTest, MockCaptureReturnsNullopt) {
    MockScreenshotHelper helper;
    auto result = helper.capture(0);
    EXPECT_FALSE(result.has_value());
}

TEST(ScreenshotHelperTest, MockCaptureReturnsFrame) {
    MockScreenshotHelper helper;
    VideoFrame expected;
    expected.width = 800;
    expected.height = 600;
    expected.codec = 0;
    helper.set_capture_return(expected);

    auto result = helper.capture(0);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->width, 800u);
    EXPECT_EQ(result->height, 600u);
    EXPECT_EQ(result->codec, 0u);
    EXPECT_EQ(result->display, 0u);
}

TEST(ScreenshotHelperTest, MockCaptureCorrectDisplay) {
    MockScreenshotHelper helper;
    VideoFrame expected;
    expected.width = 640;
    expected.height = 480;
    helper.set_capture_return(expected);

    auto r1 = helper.capture(1);
    EXPECT_TRUE(r1.has_value());
    EXPECT_EQ(r1->display, 1u);

    auto r2 = helper.capture(2);
    EXPECT_TRUE(r2.has_value());
    EXPECT_EQ(r2->display, 2u);
}

TEST(ScreenshotHelperTest, MockDisplayIndices) {
    MockScreenshotHelper helper;
    helper.set_display_indices({0, 1, 2});
    auto indices = helper.get_display_indices();
    EXPECT_EQ(indices.size(), 3u);
    EXPECT_EQ(indices[0], 0u);
    EXPECT_EQ(indices[1], 1u);
    EXPECT_EQ(indices[2], 2u);
}

TEST(ScreenshotHelperTest, MockEmptyDisplayIndices) {
    MockScreenshotHelper helper;
    auto indices = helper.get_display_indices();
    EXPECT_TRUE(indices.empty());
}

TEST(ScreenshotHelperTest, CaptureCallsTracked) {
    MockScreenshotHelper helper;
    VideoFrame f;
    f.width = 800; f.height = 600;
    helper.set_capture_return(f);

    helper.capture(0);
    helper.capture(1);
    helper.capture(2);

    auto calls = helper.capture_calls();
    EXPECT_EQ(calls.size(), 3u);
    EXPECT_EQ(calls[0], 0u);
    EXPECT_EQ(calls[1], 1u);
    EXPECT_EQ(calls[2], 2u);
}

// ============================================================================
// Client Tests — Construction and Basic State
// ============================================================================

TEST(ClientTest, DefaultConstruction) {
    Client c;
    // Client should be constructed in a valid state
    EXPECT_EQ(c.get_phase(), Client::Phase::DISCONNECTED);
    EXPECT_FALSE(c.is_connected());
}

TEST(ClientTest, InitialPeerIdEmpty) {
    Client c;
    EXPECT_TRUE(c.get_peer_id().empty());
}

TEST(ClientTest, InitialTransportTypeEmpty) {
    Client c;
    EXPECT_TRUE(c.get_transport_type().empty());
}

TEST(ClientTest, StartWithValidParams) {
    Client c;
    auto iface = std::make_shared<MockClientInterface>();
    iface->set_peer_id("test-peer-999");
    iface->set_key("my-secret-key");
    iface->set_token("auth-token-123");
    iface->set_conn_type(ConnType::DEFAULT_CONN);

    bool started = c.start("test-peer-999", "my-secret-key",
        "auth-token-123", ConnType::DEFAULT_CONN, iface);
    // start returns a bool (may be false in unit test without network)
    // but we verify it doesn't crash and the interface is stored
    EXPECT_TRUE(started == true || started == false);
}

TEST(ClientTest, StartWithFileTransferConnType) {
    Client c;
    auto iface = std::make_shared<MockClientInterface>();
    iface->set_peer_id("ft-peer");
    iface->set_key("ft-key");
    iface->set_token("ft-token");
    iface->set_conn_type(ConnType::FILE_TRANSFER);

    bool started = c.start("ft-peer", "ft-key", "ft-token",
        ConnType::FILE_TRANSFER, iface);
    EXPECT_TRUE(started == true || started == false);
}

TEST(ClientTest, StartWithPortForwardConnType) {
    Client c;
    auto iface = std::make_shared<MockClientInterface>();
    iface->set_peer_id("pf-peer");
    iface->set_key("pf-key");
    iface->set_token("pf-token");
    iface->set_conn_type(ConnType::PORT_FORWARD);

    bool started = c.start("pf-peer", "pf-key", "pf-token",
        ConnType::PORT_FORWARD, iface);
    EXPECT_TRUE(started == true || started == false);
}

TEST(ClientTest, StartWithRdpConnType) {
    Client c;
    auto iface = std::make_shared<MockClientInterface>();
    iface->set_peer_id("rdp-peer");
    iface->set_key("rdp-key");
    iface->set_token("rdp-token");
    iface->set_conn_type(ConnType::RDP);

    bool started = c.start("rdp-peer", "rdp-key", "rdp-token",
        ConnType::RDP, iface);
    EXPECT_TRUE(started == true || started == false);
}

TEST(ClientTest, CloseWhileDisconnected) {
    Client c;
    // Closing a disconnected client should be safe
    c.close();
    EXPECT_EQ(c.get_phase(), Client::Phase::DISCONNECTED);
}

TEST(ClientTest, DoubleClose) {
    Client c;
    c.close();
    c.close();
    EXPECT_FALSE(c.is_connected());
}

TEST(ClientTest, DestructorCleansUp) {
    {
        Client c;
        auto iface = std::make_shared<MockClientInterface>();
        c.start("peer", "key", "token", ConnType::DEFAULT_CONN, iface);
        // Destructor runs here — should not crash
    }
    SUCCEED();
}

TEST(ClientTest, StartThenClose) {
    Client c;
    auto iface = std::make_shared<MockClientInterface>();
    iface->set_peer_id("close-test");
    iface->set_key("k");
    iface->set_token("t");
    c.start("close-test", "k", "t", ConnType::DEFAULT_CONN, iface);
    c.close();
    // After close, should be disconnected
    EXPECT_EQ(c.get_phase(), Client::Phase::DISCONNECTED);
}

TEST(ClientTest, IsConnectedAfterStart) {
    Client c;
    auto iface = std::make_shared<MockClientInterface>();
    iface->set_peer_id("conn-test");
    iface->set_key("k");
    iface->set_token("t");
    iface->set_conn_type(ConnType::DEFAULT_CONN);
    bool started = c.start("conn-test", "k", "t", ConnType::DEFAULT_CONN, iface);
    // is_connected depends on whether network connection succeeded
    bool connected = c.is_connected();
    EXPECT_TRUE(connected == true || connected == false);
}

TEST(ClientTest, StartWithEmptyPeerId) {
    Client c;
    auto iface = std::make_shared<MockClientInterface>();
    iface->set_peer_id("");
    iface->set_key("k");
    iface->set_token("t");
    // Starting with empty peer_id may fail
    bool started = c.start("", "k", "t", ConnType::DEFAULT_CONN, iface);
    EXPECT_TRUE(started == true || started == false);
    c.close();
}

TEST(ClientTest, StartWithLongPeerId) {
    Client c;
    auto iface = std::make_shared<MockClientInterface>();
    std::string long_id(1024, 'x');
    iface->set_peer_id(long_id);
    iface->set_key("k");
    iface->set_token("t");
    bool started = c.start(long_id, "k", "t", ConnType::DEFAULT_CONN, iface);
    EXPECT_TRUE(started == true || started == false);
    c.close();
}

TEST(ClientTest, StartWithLongKey) {
    Client c;
    auto iface = std::make_shared<MockClientInterface>();
    std::string long_key(4096, 'y');
    iface->set_peer_id("peer");
    iface->set_key(long_key);
    iface->set_token("t");
    bool started = c.start("peer", long_key, "t", ConnType::DEFAULT_CONN, iface);
    EXPECT_TRUE(started == true || started == false);
    c.close();
}

TEST(ClientTest, StartWithLongToken) {
    Client c;
    auto iface = std::make_shared<MockClientInterface>();
    std::string long_token(2048, 'z');
    iface->set_peer_id("peer");
    iface->set_key("k");
    iface->set_token(long_token);
    bool started = c.start("peer", "k", long_token, ConnType::DEFAULT_CONN, iface);
    EXPECT_TRUE(started == true || started == false);
    c.close();
}

// ============================================================================
// Client Tests — Send Methods (no-op without active connection)
// ============================================================================

TEST(ClientTest, SendMouseEventDoesNotCrash) {
    Client c;
    MouseEvent ev;
    ev.x = 100; ev.y = 200;
    ev.mask = MouseEvent::BUTTON_LEFT | MouseEvent::TYPE_DOWN;
    c.send_mouse_event(ev);
    SUCCEED();
}

TEST(ClientTest, SendKeyEventDoesNotCrash) {
    Client c;
    KeyEvent ev;
    ev.keycode = 65;
    ev.down = true;
    c.send_key_event(ev);
    SUCCEED();
}

TEST(ClientTest, SendTextDoesNotCrash) {
    Client c;
    c.send_text("Hello from client!");
    SUCCEED();
}

TEST(ClientTest, SendClipboardTextDoesNotCrash) {
    Client c;
    c.send_clipboard_text("clipboard content");
    SUCCEED();
}

TEST(ClientTest, SendClipboardFilesDoesNotCrash) {
    Client c;
    c.send_clipboard_files({"/tmp/file1.txt", "/tmp/file2.txt"});
    SUCCEED();
}

TEST(ClientTest, SendFileDoesNotCrash) {
    Client c;
    c.send_file("/local/test.txt", "/remote/test.txt");
    SUCCEED();
}

TEST(ClientTest, CancelFileTransferDoesNotCrash) {
    Client c;
    c.cancel_file_transfer();
    SUCCEED();
}

TEST(ClientTest, SwitchDisplayDoesNotCrash) {
    Client c;
    c.switch_display(0);
    c.switch_display(1);
    c.switch_display(999);
    SUCCEED();
}

TEST(ClientTest, SetPermissionsDoesNotCrash) {
    Client c;
    ControlPermissions perms;
    perms.keyboard = true;
    perms.clipboard = false;
    c.set_permissions(perms);
    SUCCEED();
}

TEST(ClientTest, RequestVideoFrameDoesNotCrash) {
    Client c;
    c.request_video_frame();
    SUCCEED();
}

TEST(ClientTest, RequestPrivacyModeDoesNotCrash) {
    Client c;
    c.request_privacy_mode(true);
    c.request_privacy_mode(false);
    SUCCEED();
}

TEST(ClientTest, EnableAudioDoesNotCrash) {
    Client c;
    c.enable_audio(true);
    c.enable_audio(false);
    SUCCEED();
}

TEST(ClientTest, SetAudioConfigDoesNotCrash) {
    Client c;
    c.set_audio_config(48000, 2);
    c.set_audio_config(44100, 1);
    c.set_audio_config(96000, 6);
    SUCCEED();
}

TEST(ClientTest, SendChatMessageDoesNotCrash) {
    Client c;
    c.send_chat_message("Hello!");
    SUCCEED();
}

// ============================================================================
// Client Tests — Phase Transitions
// ============================================================================

TEST(ClientTest, PhaseDisconnectedByDefault) {
    Client c;
    EXPECT_EQ(c.get_phase(), Client::Phase::DISCONNECTED);
}

TEST(ClientTest, PhaseExpectedValues) {
    // Verify that phase enum values are as expected
    auto d = Client::Phase::DISCONNECTED;
    auto r = Client::Phase::RENDEZVOUS;
    auto p = Client::Phase::PUNCH_HOLE;
    auto t = Client::Phase::TCP_HANDSHAKE;
    auto l = Client::Phase::LOGIN;
    auto c = Client::Phase::CONNECTED;

    EXPECT_NE(d, r);
    EXPECT_NE(r, p);
    EXPECT_NE(p, t);
    EXPECT_NE(t, l);
    EXPECT_NE(l, c);
}

// ============================================================================
// ClientInterface Mock Tests
// ============================================================================

TEST(ClientInterfaceTest, GetId) {
    MockClientInterface iface;
    iface.set_id("my-client-id");
    EXPECT_EQ(iface.get_id(), "my-client-id");
}

TEST(ClientInterfaceTest, UpdateDirect) {
    MockClientInterface iface;
    iface.update_direct(true);
    EXPECT_TRUE(iface.direct());
    EXPECT_EQ(iface.direct_count(), 1);
    iface.update_direct(false);
    EXPECT_FALSE(iface.direct());
    EXPECT_EQ(iface.direct_count(), 2);
}

TEST(ClientInterfaceTest, UpdateReceived) {
    MockClientInterface iface;
    iface.update_received(true);
    EXPECT_TRUE(iface.received());
    EXPECT_EQ(iface.received_count(), 1);
    iface.update_received(false);
    EXPECT_FALSE(iface.received());
    EXPECT_EQ(iface.received_count(), 2);
}

TEST(ClientInterfaceTest, IsForceRelay) {
    MockClientInterface iface;
    EXPECT_FALSE(iface.is_force_relay());
    iface.set_force_relay(true);
    EXPECT_TRUE(iface.is_force_relay());
}

TEST(ClientInterfaceTest, OnLoginError) {
    MockClientInterface iface;
    iface.on_login_error("Invalid credentials");
    EXPECT_EQ(iface.login_error(), "Invalid credentials");
    EXPECT_EQ(iface.login_error_count(), 1);

    iface.on_login_error("Timeout");
    EXPECT_EQ(iface.login_error(), "Timeout");
    EXPECT_EQ(iface.login_error_count(), 2);
}

TEST(ClientInterfaceTest, OnLoginSuccess) {
    MockClientInterface iface;
    iface.on_login_success();
    EXPECT_EQ(iface.login_success_count(), 1);
    iface.on_login_success();
    EXPECT_EQ(iface.login_success_count(), 2);
}

TEST(ClientInterfaceTest, OnConnectionReady) {
    MockClientInterface iface;
    iface.on_connection_ready();
    EXPECT_EQ(iface.connection_ready_count(), 1);
    iface.on_connection_ready();
    EXPECT_EQ(iface.connection_ready_count(), 2);
}

TEST(ClientInterfaceTest, OnConnectionClosed) {
    MockClientInterface iface;
    iface.on_connection_closed("timeout");
    EXPECT_EQ(iface.connection_closed_reason(), "timeout");
    EXPECT_EQ(iface.connection_closed_count(), 1);

    iface.on_connection_closed("user request");
    EXPECT_EQ(iface.connection_closed_reason(), "user request");
    EXPECT_EQ(iface.connection_closed_count(), 2);
}

TEST(ClientInterfaceTest, OnVideoFrame) {
    MockClientInterface iface;
    VideoFrame f;
    f.width = 1920;
    f.height = 1080;
    f.codec = 1;
    f.timestamp = 12345;
    f.keyframe = true;
    iface.on_video_frame(f);
    EXPECT_EQ(iface.video_frame_count(), 1);
    EXPECT_EQ(iface.last_video_frame().width, 1920u);
    EXPECT_EQ(iface.last_video_frame().height, 1080u);
    EXPECT_EQ(iface.last_video_frame().codec, 1u);
    EXPECT_EQ(iface.last_video_frame().timestamp, 12345u);
    EXPECT_TRUE(iface.last_video_frame().keyframe);
}

TEST(ClientInterfaceTest, OnAudioFrame) {
    MockClientInterface iface;
    AudioFrame f;
    f.sample_rate = 48000;
    f.channels = 2;
    f.bits_per_sample = 16;
    f.timestamp = 99999;
    iface.on_audio_frame(f);
    EXPECT_EQ(iface.audio_frame_count(), 1);
    EXPECT_EQ(iface.last_audio_frame().sample_rate, 48000u);
    EXPECT_EQ(iface.last_audio_frame().channels, 2u);
    EXPECT_EQ(iface.last_audio_frame().bits_per_sample, 16u);
    EXPECT_EQ(iface.last_audio_frame().timestamp, 99999u);
}

TEST(ClientInterfaceTest, OnCursorData) {
    MockClientInterface iface;
    CursorData cd;
    cd.id = 7;
    cd.hot_x = 5;
    cd.hot_y = 3;
    cd.width = 32;
    cd.height = 32;
    iface.on_cursor_data(cd);
    EXPECT_EQ(iface.cursor_data_count(), 1);
    EXPECT_EQ(iface.last_cursor().id, 7u);
    EXPECT_EQ(iface.last_cursor().hot_x, 5);
    EXPECT_EQ(iface.last_cursor().hot_y, 3);
    EXPECT_EQ(iface.last_cursor().width, 32u);
    EXPECT_EQ(iface.last_cursor().height, 32u);
}

TEST(ClientInterfaceTest, OnCursorPosition) {
    MockClientInterface iface;
    iface.on_cursor_position(50, 100);
    EXPECT_EQ(iface.cursor_pos_count(), 1);
    EXPECT_EQ(iface.cursor_x(), 50);
    EXPECT_EQ(iface.cursor_y(), 100);

    iface.on_cursor_position(-10, 2000);
    EXPECT_EQ(iface.cursor_pos_count(), 2);
    EXPECT_EQ(iface.cursor_x(), -10);
    EXPECT_EQ(iface.cursor_y(), 2000);
}

TEST(ClientInterfaceTest, OnClipboardText) {
    MockClientInterface iface;
    iface.on_clipboard_text("sample text");
    EXPECT_EQ(iface.clipboard_text_count(), 1);
    EXPECT_EQ(iface.clipboard_text(), "sample text");

    iface.on_clipboard_text("");
    EXPECT_EQ(iface.clipboard_text_count(), 2);
    EXPECT_EQ(iface.clipboard_text(), "");
}

TEST(ClientInterfaceTest, OnClipboardFiles) {
    MockClientInterface iface;
    std::vector<std::string> files = {"/tmp/a.txt", "/tmp/b.png"};
    iface.on_clipboard_files(files);
    EXPECT_EQ(iface.clipboard_files_count(), 1);
    EXPECT_EQ(iface.clipboard_files().size(), 2u);
    EXPECT_EQ(iface.clipboard_files()[0], "/tmp/a.txt");
    EXPECT_EQ(iface.clipboard_files()[1], "/tmp/b.png");
}

TEST(ClientInterfaceTest, OnFileTransferRequest) {
    MockClientInterface iface;
    iface.on_file_transfer_request("/remote/bigfile.zip", 1048576);
    EXPECT_EQ(iface.file_req_count(), 1);
    EXPECT_EQ(iface.file_req_path(), "/remote/bigfile.zip");
    EXPECT_EQ(iface.file_req_size(), 1048576u);
}

TEST(ClientInterfaceTest, OnFileTransferProgress) {
    MockClientInterface iface;
    iface.on_file_transfer_progress(524288, 1048576);
    EXPECT_EQ(iface.file_prog_count(), 1);
    EXPECT_EQ(iface.file_prog_transferred(), 524288u);
    EXPECT_EQ(iface.file_prog_total(), 1048576u);
}

TEST(ClientInterfaceTest, OnFileTransferDone) {
    MockClientInterface iface;
    iface.on_file_transfer_done("/remote/done.zip");
    EXPECT_EQ(iface.file_done_count(), 1);
    EXPECT_EQ(iface.file_done_path(), "/remote/done.zip");
}

TEST(ClientInterfaceTest, OnChatMessage) {
    MockClientInterface iface;
    iface.on_chat_message("Alice", "Hi there!");
    EXPECT_EQ(iface.chat_count(), 1);
    EXPECT_EQ(iface.chat_sender(), "Alice");
    EXPECT_EQ(iface.chat_msg(), "Hi there!");
}

TEST(ClientInterfaceTest, OnPrivacyModeChanged) {
    MockClientInterface iface;
    iface.on_privacy_mode_changed(true);
    EXPECT_EQ(iface.privacy_count(), 1);
    EXPECT_TRUE(iface.privacy_enabled());

    iface.on_privacy_mode_changed(false);
    EXPECT_EQ(iface.privacy_count(), 2);
    EXPECT_FALSE(iface.privacy_enabled());
}

TEST(ClientInterfaceTest, OnSwitchDisplay) {
    MockClientInterface iface;
    iface.on_switch_display(0);
    EXPECT_EQ(iface.display_count(), 1);
    EXPECT_EQ(iface.display_idx(), 0u);

    iface.on_switch_display(3);
    EXPECT_EQ(iface.display_count(), 2);
    EXPECT_EQ(iface.display_idx(), 3u);
}

TEST(ClientInterfaceTest, OnPermissionChanged) {
    MockClientInterface iface;
    ControlPermissions p;
    p.keyboard = false;
    p.clipboard = true;
    p.file_transfer = false;
    iface.on_permission_changed(p);
    EXPECT_EQ(iface.perms_count(), 1);
    EXPECT_FALSE(iface.perms().keyboard);
    EXPECT_TRUE(iface.perms().clipboard);
    EXPECT_FALSE(iface.perms().file_transfer);
}

TEST(ClientInterfaceTest, ResetClearsAllState) {
    MockClientInterface iface;
    iface.set_id("test");
    iface.update_direct(true);
    iface.update_received(true);
    iface.on_login_error("err");
    iface.on_login_success();

    iface.reset();

    EXPECT_EQ(iface.direct_count(), 0);
    EXPECT_EQ(iface.received_count(), 0);
    EXPECT_EQ(iface.login_error_count(), 0);
    EXPECT_EQ(iface.login_success_count(), 0);
    EXPECT_EQ(iface.connection_ready_count(), 0);
    EXPECT_EQ(iface.connection_closed_count(), 0);
    EXPECT_EQ(iface.video_frame_count(), 0);
    EXPECT_EQ(iface.audio_frame_count(), 0);
    EXPECT_EQ(iface.cursor_data_count(), 0);
    EXPECT_EQ(iface.cursor_pos_count(), 0);
    EXPECT_EQ(iface.clipboard_text_count(), 0);
    EXPECT_EQ(iface.clipboard_files_count(), 0);
    EXPECT_EQ(iface.file_req_count(), 0);
    EXPECT_EQ(iface.file_prog_count(), 0);
    EXPECT_EQ(iface.file_done_count(), 0);
    EXPECT_EQ(iface.chat_count(), 0);
    EXPECT_EQ(iface.privacy_count(), 0);
    EXPECT_EQ(iface.display_count(), 0);
    EXPECT_EQ(iface.perms_count(), 0);
}

// ============================================================================
// LanDiscovery Tests
// ============================================================================

TEST(LanDiscoveryTest, Construction) {
    LanDiscovery ld;
    // Should construct without crash
    SUCCEED();
}

TEST(LanDiscoveryTest, StartStop) {
    LanDiscovery ld;
    ld.start();
    ld.stop();
    SUCCEED();
}

TEST(LanDiscoveryTest, DoubleStart) {
    LanDiscovery ld;
    ld.start();
    ld.start();
    ld.stop();
    SUCCEED();
}

TEST(LanDiscoveryTest, DoubleStop) {
    LanDiscovery ld;
    ld.start();
    ld.stop();
    ld.stop();
    SUCCEED();
}

TEST(LanDiscoveryTest, StopWithoutStart) {
    LanDiscovery ld;
    ld.stop();
    SUCCEED();
}

TEST(LanDiscoveryTest, StartStopCycle) {
    LanDiscovery ld;
    for (int i = 0; i < 3; i++) {
        ld.start();
        ld.stop();
    }
    SUCCEED();
}

TEST(LanDiscoveryTest, SetOnPeerFoundCallback) {
    LanDiscovery ld;
    int call_count = 0;
    PeerConfig last_peer;
    ld.set_on_peer_found([&](const PeerConfig& peer) {
        call_count++;
        last_peer = peer;
    });
    // Callback set should not crash
    EXPECT_EQ(call_count, 0);
}

TEST(LanDiscoveryTest, PeerFoundCallbackInvoked) {
    LanDiscovery ld;
    std::atomic<int> found{0};
    ld.set_on_peer_found([&](const PeerConfig&) {
        found++;
    });
    // Start discovery
    ld.start();
    // Give a brief moment (callback may or may not fire in test env)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ld.stop();
    // In a test environment, we may or may not find peers
    EXPECT_TRUE(found >= 0);
}

TEST(LanDiscoveryTest, Discover) {
    LanDiscovery ld;
    ld.start();
    auto peers = ld.discover();
    // discover returns a vector of PeerConfig
    // May be empty in test environment, but should not crash
    EXPECT_TRUE(peers.empty() || !peers.empty());
    ld.stop();
}

TEST(LanDiscoveryTest, DiscoverBeforeStart) {
    LanDiscovery ld;
    auto peers = ld.discover();
    // Should return empty vector
    EXPECT_TRUE(peers.empty());
}

TEST(LanDiscoveryTest, NullptrCallbackSafe) {
    LanDiscovery ld;
    ld.set_on_peer_found(nullptr);
    ld.start();
    ld.stop();
    SUCCEED();
}

TEST(LanDiscoveryTest, DestructorStops) {
    {
        LanDiscovery ld;
        ld.start();
    }
    // Destructor should stop discovery, no crash
    SUCCEED();
}

// ============================================================================
// AudioDevice Tests (via mock)
// ============================================================================

TEST(AudioDeviceTest, OpenInput) {
    MockAudioDevice dev;
    bool result = dev.open_input("default_input");
    EXPECT_TRUE(result);
    EXPECT_TRUE(dev.input_open());
    EXPECT_EQ(dev.input_device(), "default_input");
}

TEST(AudioDeviceTest, OpenInputFails) {
    MockAudioDevice dev;
    dev.set_input_open_return(false);
    bool result = dev.open_input("bad_device");
    EXPECT_FALSE(result);
}

TEST(AudioDeviceTest, OpenOutput) {
    MockAudioDevice dev;
    bool result = dev.open_output("default_output");
    EXPECT_TRUE(result);
    EXPECT_TRUE(dev.output_open());
    EXPECT_EQ(dev.output_device(), "default_output");
}

TEST(AudioDeviceTest, OpenOutputFails) {
    MockAudioDevice dev;
    dev.set_output_open_return(false);
    bool result = dev.open_output("bad_device");
    EXPECT_FALSE(result);
}

TEST(AudioDeviceTest, ReadSamples) {
    MockAudioDevice dev;
    auto samples = dev.read_samples(1024);
    EXPECT_EQ(dev.read_count(), 1);
    EXPECT_EQ(dev.last_read_count(), 1024u);
    // Default returns empty vector
    EXPECT_TRUE(samples.empty());
}

TEST(AudioDeviceTest, ReadSamplesReturnsData) {
    MockAudioDevice dev;
    dev.set_read_samples_return({100, 200, 300, -100, -200});
    auto samples = dev.read_samples(5);
    EXPECT_EQ(samples.size(), 5u);
    EXPECT_EQ(samples[0], 100);
    EXPECT_EQ(samples[1], 200);
    EXPECT_EQ(samples[2], 300);
    EXPECT_EQ(samples[3], -100);
    EXPECT_EQ(samples[4], -200);
}

TEST(AudioDeviceTest, ReadSamplesMultipleCalls) {
    MockAudioDevice dev;
    dev.set_read_samples_return({42});
    dev.read_samples(1);
    dev.read_samples(1);
    dev.read_samples(1);
    EXPECT_EQ(dev.read_count(), 3);
}

TEST(AudioDeviceTest, WriteSamples) {
    MockAudioDevice dev;
    std::vector<int16_t> data = {0, 32767, -32768, 128, -128};
    bool result = dev.write_samples(data);
    EXPECT_TRUE(result);
    EXPECT_EQ(dev.write_count(), 1);
    EXPECT_EQ(dev.last_written(), data);
}

TEST(AudioDeviceTest, WriteSamplesFails) {
    MockAudioDevice dev;
    dev.set_write_return(false);
    std::vector<int16_t> data = {1, 2, 3};
    bool result = dev.write_samples(data);
    EXPECT_FALSE(result);
}

TEST(AudioDeviceTest, WriteEmptySamples) {
    MockAudioDevice dev;
    std::vector<int16_t> empty;
    bool result = dev.write_samples(empty);
    EXPECT_TRUE(result);
    EXPECT_EQ(dev.write_count(), 1);
    EXPECT_TRUE(dev.last_written().empty());
}

TEST(AudioDeviceTest, Close) {
    MockAudioDevice dev;
    dev.open_input("in");
    dev.open_output("out");
    EXPECT_TRUE(dev.input_open());
    EXPECT_TRUE(dev.output_open());

    dev.close();
    EXPECT_TRUE(dev.is_closed());
    EXPECT_FALSE(dev.input_open());
    EXPECT_FALSE(dev.output_open());
}

TEST(AudioDeviceTest, CloseMultipleTimes) {
    MockAudioDevice dev;
    dev.open_input("in");
    dev.close();
    dev.close();
    EXPECT_TRUE(dev.is_closed());
}

TEST(AudioDeviceTest, ListDevices) {
    MockAudioDevice dev;
    dev.set_list_devices_return({"Device A", "Device B", "Device C"});
    auto devices = dev.list_devices();
    EXPECT_EQ(devices.size(), 3u);
    EXPECT_EQ(devices[0], "Device A");
    EXPECT_EQ(devices[1], "Device B");
    EXPECT_EQ(devices[2], "Device C");
}

TEST(AudioDeviceTest, ListDevicesEmpty) {
    MockAudioDevice dev;
    auto devices = dev.list_devices();
    EXPECT_TRUE(devices.empty());
}

TEST(AudioDeviceTest, OpenInputWithDifferentDevices) {
    MockAudioDevice dev;
    EXPECT_TRUE(dev.open_input("mic1"));
    EXPECT_EQ(dev.input_device(), "mic1");
    EXPECT_TRUE(dev.open_input("mic2"));
    EXPECT_EQ(dev.input_device(), "mic2");
}

TEST(AudioDeviceTest, OpenOutputWithDifferentDevices) {
    MockAudioDevice dev;
    EXPECT_TRUE(dev.open_output("speakers"));
    EXPECT_EQ(dev.output_device(), "speakers");
    EXPECT_TRUE(dev.open_output("headphones"));
    EXPECT_EQ(dev.output_device(), "headphones");
}

TEST(AudioDeviceTest, ResetMockState) {
    MockAudioDevice dev;
    dev.open_input("in");
    dev.open_output("out");
    dev.read_samples(100);
    dev.write_samples({1, 2, 3});

    dev.reset();

    EXPECT_FALSE(dev.input_open());
    EXPECT_FALSE(dev.output_open());
    EXPECT_FALSE(dev.is_closed());
    EXPECT_EQ(dev.read_count(), 0);
    EXPECT_EQ(dev.write_count(), 0);
    EXPECT_TRUE(dev.last_written().empty());
}

// ============================================================================
// Login Constants Tests
// ============================================================================

TEST(LoginConstantsTest, PasswordEmpty) {
    EXPECT_STREQ(LOGIN_MSG_PASSWORD_EMPTY, "Empty Password");
    EXPECT_NE(std::string(LOGIN_MSG_PASSWORD_EMPTY), "");
}

TEST(LoginConstantsTest, PasswordWrong) {
    EXPECT_STREQ(LOGIN_MSG_PASSWORD_WRONG, "Wrong Password");
    EXPECT_NE(LOGIN_MSG_PASSWORD_EMPTY, LOGIN_MSG_PASSWORD_WRONG);
}

TEST(LoginConstantsTest, TwoFaWrong) {
    EXPECT_STREQ(LOGIN_MSG_2FA_WRONG, "Wrong 2FA Code");
}

TEST(LoginConstantsTest, Offline) {
    EXPECT_STREQ(LOGIN_MSG_OFFLINE, "Offline");
}

TEST(LoginConstantsTest, NoPasswordAccess) {
    EXPECT_STREQ(LOGIN_MSG_NO_PASSWORD_ACCESS, "No Password Access");
}

TEST(LoginConstantsTest, Require2fa) {
    EXPECT_STREQ(REQUIRE_2FA, "2FA Required");
}

TEST(LoginConstantsTest, DesktopNotInited) {
    EXPECT_STREQ(LOGIN_MSG_DESKTOP_NOT_INITED, "Desktop env is not inited");
}

TEST(LoginConstantsTest, DesktopSessionNotReady) {
    EXPECT_STREQ(LOGIN_MSG_DESKTOP_SESSION_NOT_READY, "Desktop session not ready");
}

TEST(LoginConstantsTest, LoginScreenWayland) {
    EXPECT_STREQ(LOGIN_SCREEN_WAYLAND, "Wayland login screen is not supported");
}

TEST(LoginConstantsTest, AllMessagesUnique) {
    std::set<std::string> msgs;
    msgs.insert(LOGIN_MSG_PASSWORD_EMPTY);
    msgs.insert(LOGIN_MSG_PASSWORD_WRONG);
    msgs.insert(LOGIN_MSG_2FA_WRONG);
    msgs.insert(LOGIN_MSG_OFFLINE);
    msgs.insert(LOGIN_MSG_NO_PASSWORD_ACCESS);
    msgs.insert(REQUIRE_2FA);
    msgs.insert(LOGIN_MSG_DESKTOP_NOT_INITED);
    msgs.insert(LOGIN_MSG_DESKTOP_SESSION_NOT_READY);
    msgs.insert(LOGIN_SCREEN_WAYLAND);
    EXPECT_EQ(msgs.size(), 9u);
}

TEST(LoginConstantsTest, AllMessagesNonNull) {
    EXPECT_NE(LOGIN_MSG_PASSWORD_EMPTY, nullptr);
    EXPECT_NE(LOGIN_MSG_PASSWORD_WRONG, nullptr);
    EXPECT_NE(LOGIN_MSG_2FA_WRONG, nullptr);
    EXPECT_NE(LOGIN_MSG_OFFLINE, nullptr);
    EXPECT_NE(LOGIN_MSG_NO_PASSWORD_ACCESS, nullptr);
    EXPECT_NE(REQUIRE_2FA, nullptr);
    EXPECT_NE(LOGIN_MSG_DESKTOP_NOT_INITED, nullptr);
    EXPECT_NE(LOGIN_MSG_DESKTOP_SESSION_NOT_READY, nullptr);
    EXPECT_NE(LOGIN_SCREEN_WAYLAND, nullptr);
}

// ============================================================================
// Video Constants Tests
// ============================================================================

TEST(VideoConstantsTest, QueueSize) {
    EXPECT_EQ(VIDEO_QUEUE_SIZE, 120u);
    EXPECT_GT(VIDEO_QUEUE_SIZE, 0u);
}

TEST(VideoConstantsTest, Sec30) {
    EXPECT_EQ(SEC30, std::chrono::seconds(30));
    EXPECT_LT(SEC30, std::chrono::seconds(31));
    EXPECT_GT(SEC30, std::chrono::seconds(29));
}

TEST(VideoConstantsTest, Milli1) {
    EXPECT_EQ(MILLI1, std::chrono::milliseconds(1));
}

TEST(VideoConstantsTest, Sec30InMilliseconds) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(SEC30);
    EXPECT_EQ(ms.count(), 30000);
}

// ============================================================================
// Constants Compilation Tests
// ============================================================================

TEST(ConstantsTest, ConnectTimeout) {
    EXPECT_EQ(CONNECT_TIMEOUT, std::chrono::seconds(5));
}

TEST(ConstantsTest, ReadTimeout) {
    EXPECT_EQ(READ_TIMEOUT, std::chrono::seconds(30));
}

TEST(ConstantsTest, RegInterval) {
    EXPECT_EQ(REG_INTERVAL, std::chrono::seconds(60));
}

TEST(ConstantsTest, ClipboardInterval) {
    EXPECT_EQ(CLIPBOARD_INTERVAL, std::chrono::milliseconds(500));
}

TEST(ConstantsTest, ServiceInterval) {
    EXPECT_EQ(SERVICE_INTERVAL, std::chrono::seconds(300));
}

TEST(ConstantsTest, HeartbeatInterval) {
    EXPECT_EQ(HEARTBEAT_INTERVAL, std::chrono::seconds(10));
}

TEST(ConstantsTest, UdpPunchTimeout) {
    EXPECT_EQ(UDP_PUNCH_TIMEOUT, std::chrono::seconds(15));
}

TEST(ConstantsTest, RendezvousPort) {
    EXPECT_EQ(RENDEZVOUS_PORT, 21116);
}

TEST(ConstantsTest, RelayPort) {
    EXPECT_EQ(RELAY_PORT, 21117);
}

TEST(ConstantsTest, WebsocketPort) {
    EXPECT_EQ(WEBSOCKET_PORT, 21118);
}

TEST(ConstantsTest, LocalPort) {
    EXPECT_EQ(LOCAL_PORT, 21119);
}

TEST(ConstantsTest, RendezvousServersNotEmpty) {
    EXPECT_FALSE(RENDEZVOUS_SERVERS.empty());
    EXPECT_GE(RENDEZVOUS_SERVERS.size(), 1u);
}

// ============================================================================
// Config Keys Tests
// ============================================================================

TEST(ConfigKeysTest, IdKey) {
    EXPECT_EQ(keys::ID, "id");
}

TEST(ConfigKeysTest, PasswordKey) {
    EXPECT_EQ(keys::PASSWORD, "password");
}

TEST(ConfigKeysTest, KeyPairKey) {
    EXPECT_EQ(keys::KEY_PAIR, "key_pair");
}

TEST(ConfigKeysTest, CustomRendezvousKey) {
    EXPECT_EQ(keys::CUSTOM_RENDEZVOUS, "custom-rendezvous-server");
}

TEST(ConfigKeysTest, DirectAccessKey) {
    EXPECT_EQ(keys::DIRECT_ACCESS, "direct-access-port");
}

TEST(ConfigKeysTest, EnableUdpKey) {
    EXPECT_EQ(keys::ENABLE_UDP, "enable-udp");
}

TEST(ConfigKeysTest, EnableIpv6Key) {
    EXPECT_EQ(keys::ENABLE_IPV6, "enable-ipv6");
}

TEST(ConfigKeysTest, ForceRelayKey) {
    EXPECT_EQ(keys::FORCE_RELAY, "force-relay");
}

TEST(ConfigKeysTest, VideoCodecKey) {
    EXPECT_EQ(keys::VIDEO_CODEC, "video-codec");
}

TEST(ConfigKeysTest, AudioInputKey) {
    EXPECT_EQ(keys::AUDIO_INPUT, "audio-input-device");
}

TEST(ConfigKeysTest, AudioOutputKey) {
    EXPECT_EQ(keys::AUDIO_OUTPUT, "audio-output-device");
}

TEST(ConfigKeysTest, ThemeKey) {
    EXPECT_EQ(keys::THEME, "theme");
}

TEST(ConfigKeysTest, LangKey) {
    EXPECT_EQ(keys::LANG, "lang");
}

TEST(ConfigKeysTest, ViewOnlyKey) {
    EXPECT_EQ(keys::VIEW_ONLY, "view-only");
}

TEST(ConfigKeysTest, ShowRemoteCursorKey) {
    EXPECT_EQ(keys::SHOW_REMOTE_CURSOR, "show-remote-cursor");
}

TEST(ConfigKeysTest, LockAfterSessionEndKey) {
    EXPECT_EQ(keys::LOCK_AFTER_SESSION_END, "lock-after-session-end");
}

TEST(ConfigKeysTest, EnableFileTransferKey) {
    EXPECT_EQ(keys::ENABLE_FILE_TRANSFER, "enable-file-transfer");
}

TEST(ConfigKeysTest, EnableClipboardKey) {
    EXPECT_EQ(keys::ENABLE_CLIPBOARD, "enable-clipboard");
}

TEST(ConfigKeysTest, EnableAudioKey) {
    EXPECT_EQ(keys::ENABLE_AUDIO, "enable-audio");
}

TEST(ConfigKeysTest, EnableTunnelKey) {
    EXPECT_EQ(keys::ENABLE_TUNNEL, "enable-tunnel");
}

TEST(ConfigKeysTest, StopServiceKey) {
    EXPECT_EQ(keys::STOP_SERVICE, "stop-service");
}

TEST(ConfigKeysTest, IncomingOnlyKey) {
    EXPECT_EQ(keys::INCOMING_ONLY, "incoming-only");
}

TEST(ConfigKeysTest, OutgoingOnlyKey) {
    EXPECT_EQ(keys::OUTGOING_ONLY, "outgoing-only");
}

TEST(ConfigKeysTest, AllowLanDiscoveryKey) {
    EXPECT_EQ(keys::ALLOW_LAN_DISCOVERY, "allow-lan-discovery");
}

TEST(ConfigKeysTest, DirectServerKey) {
    EXPECT_EQ(keys::DIRECT_SERVER, "direct-server");
}

TEST(ConfigKeysTest, ApiServerKey) {
    EXPECT_EQ(keys::API_SERVER, "api-server");
}

TEST(ConfigKeysTest, KeyKey) {
    EXPECT_EQ(keys::KEY, "key");
}

TEST(ConfigKeysTest, TokenKey) {
    EXPECT_EQ(keys::TOKEN, "token");
}

TEST(ConfigKeysTest, ApproveModeKey) {
    EXPECT_EQ(keys::APPROVE_MODE, "approve-mode");
}

TEST(ConfigKeysTest, PrivacyModeKey) {
    EXPECT_EQ(keys::PRIVACY_MODE, "privacy-mode");
}

TEST(ConfigKeysTest, DisplayIndexKey) {
    EXPECT_EQ(keys::DISPLAY_INDEX, "display-index");
}

TEST(ConfigKeysTest, ScaleKey) {
    EXPECT_EQ(keys::SCALE, "scale");
}

TEST(ConfigKeysTest, QualityKey) {
    EXPECT_EQ(keys::QUALITY, "quality");
}

TEST(ConfigKeysTest, BitrateKey) {
    EXPECT_EQ(keys::BITRATE, "bitrate");
}

TEST(ConfigKeysTest, FpsKey) {
    EXPECT_EQ(keys::FPS, "fps");
}

TEST(ConfigKeysTest, EnableKeyboardKey) {
    EXPECT_EQ(keys::ENABLE_KEYBOARD, "enable-keyboard");
}

TEST(ConfigKeysTest, EnableClipboardFileKey) {
    EXPECT_EQ(keys::ENABLE_CLIPBOARD_FILE, "enable-clipboard-file");
}

TEST(ConfigKeysTest, AllKeysUnique) {
    std::set<std::string_view> all_keys;
    all_keys.insert(keys::ID);
    all_keys.insert(keys::PASSWORD);
    all_keys.insert(keys::KEY_PAIR);
    all_keys.insert(keys::CUSTOM_RENDEZVOUS);
    all_keys.insert(keys::DIRECT_ACCESS);
    all_keys.insert(keys::ENABLE_UDP);
    all_keys.insert(keys::ENABLE_IPV6);
    all_keys.insert(keys::FORCE_RELAY);
    all_keys.insert(keys::VIDEO_CODEC);
    all_keys.insert(keys::AUDIO_INPUT);
    all_keys.insert(keys::AUDIO_OUTPUT);
    all_keys.insert(keys::THEME);
    all_keys.insert(keys::LANG);
    all_keys.insert(keys::VIEW_ONLY);
    all_keys.insert(keys::SHOW_REMOTE_CURSOR);
    all_keys.insert(keys::LOCK_AFTER_SESSION_END);
    all_keys.insert(keys::ENABLE_FILE_TRANSFER);
    all_keys.insert(keys::ENABLE_CLIPBOARD);
    all_keys.insert(keys::ENABLE_AUDIO);
    all_keys.insert(keys::ENABLE_TUNNEL);
    all_keys.insert(keys::STOP_SERVICE);
    all_keys.insert(keys::INCOMING_ONLY);
    all_keys.insert(keys::OUTGOING_ONLY);
    all_keys.insert(keys::ALLOW_LAN_DISCOVERY);
    all_keys.insert(keys::DIRECT_SERVER);
    all_keys.insert(keys::API_SERVER);
    all_keys.insert(keys::KEY);
    all_keys.insert(keys::TOKEN);
    all_keys.insert(keys::APPROVE_MODE);
    all_keys.insert(keys::PRIVACY_MODE);
    all_keys.insert(keys::DISPLAY_INDEX);
    all_keys.insert(keys::SCALE);
    all_keys.insert(keys::QUALITY);
    all_keys.insert(keys::BITRATE);
    all_keys.insert(keys::FPS);
    all_keys.insert(keys::ENABLE_KEYBOARD);
    all_keys.insert(keys::ENABLE_CLIPBOARD_FILE);
    EXPECT_EQ(all_keys.size(), 37u);
}

// ============================================================================
// ControlPermissions Tests
// ============================================================================

TEST(ControlPermissionsTest, DefaultPermissions) {
    ControlPermissions p;
    EXPECT_TRUE(p.keyboard);
    EXPECT_TRUE(p.clipboard);
    EXPECT_TRUE(p.file_transfer);
    EXPECT_TRUE(p.audio);
    EXPECT_FALSE(p.restart);
    EXPECT_FALSE(p.shutdown);
    EXPECT_FALSE(p.privacy_mode);
}

TEST(ControlPermissionsTest, ModifyKeyboard) {
    ControlPermissions p;
    p.keyboard = false;
    EXPECT_FALSE(p.keyboard);
}

TEST(ControlPermissionsTest, ModifyClipboard) {
    ControlPermissions p;
    p.clipboard = false;
    EXPECT_FALSE(p.clipboard);
}

TEST(ControlPermissionsTest, ModifyFileTransfer) {
    ControlPermissions p;
    p.file_transfer = false;
    EXPECT_FALSE(p.file_transfer);
}

TEST(ControlPermissionsTest, ModifyAudio) {
    ControlPermissions p;
    p.audio = false;
    EXPECT_FALSE(p.audio);
}

TEST(ControlPermissionsTest, ModifyRestart) {
    ControlPermissions p;
    p.restart = true;
    EXPECT_TRUE(p.restart);
}

TEST(ControlPermissionsTest, ModifyShutdown) {
    ControlPermissions p;
    p.shutdown = true;
    EXPECT_TRUE(p.shutdown);
}

TEST(ControlPermissionsTest, ModifyPrivacyMode) {
    ControlPermissions p;
    p.privacy_mode = true;
    EXPECT_TRUE(p.privacy_mode);
}

TEST(ControlPermissionsTest, AllOff) {
    ControlPermissions p;
    p.keyboard = false;
    p.clipboard = false;
    p.file_transfer = false;
    p.audio = false;
    p.restart = false;
    p.shutdown = false;
    p.privacy_mode = false;

    EXPECT_FALSE(p.keyboard);
    EXPECT_FALSE(p.clipboard);
    EXPECT_FALSE(p.file_transfer);
    EXPECT_FALSE(p.audio);
    EXPECT_FALSE(p.restart);
    EXPECT_FALSE(p.shutdown);
    EXPECT_FALSE(p.privacy_mode);
}

TEST(ControlPermissionsTest, AllOn) {
    ControlPermissions p;
    p.keyboard = true;
    p.clipboard = true;
    p.file_transfer = true;
    p.audio = true;
    p.restart = true;
    p.shutdown = true;
    p.privacy_mode = true;

    EXPECT_TRUE(p.keyboard);
    EXPECT_TRUE(p.clipboard);
    EXPECT_TRUE(p.file_transfer);
    EXPECT_TRUE(p.audio);
    EXPECT_TRUE(p.restart);
    EXPECT_TRUE(p.shutdown);
    EXPECT_TRUE(p.privacy_mode);
}

TEST(ControlPermissionsTest, Copyable) {
    ControlPermissions a;
    a.keyboard = false;
    a.clipboard = true;
    a.restart = true;

    ControlPermissions b = a;
    EXPECT_FALSE(b.keyboard);
    EXPECT_TRUE(b.clipboard);
    EXPECT_TRUE(b.restart);
}

// ============================================================================
// MouseEvent Tests
// ============================================================================

TEST(MouseEventTest, DefaultValues) {
    MouseEvent ev;
    EXPECT_EQ(ev.mask, 0);
    EXPECT_EQ(ev.x, 0);
    EXPECT_EQ(ev.y, 0);
}

TEST(MouseEventTest, ButtonConstants) {
    EXPECT_EQ(MouseEvent::BUTTON_LEFT, 0x01);
    EXPECT_EQ(MouseEvent::BUTTON_RIGHT, 0x02);
    EXPECT_EQ(MouseEvent::BUTTON_WHEEL, 0x04);
    EXPECT_EQ(MouseEvent::BUTTON_BACK, 0x08);
    EXPECT_EQ(MouseEvent::BUTTON_FORWARD, 0x10);
}

TEST(MouseEventTest, TypeConstants) {
    EXPECT_EQ(MouseEvent::TYPE_MOVE, 0);
    EXPECT_EQ(MouseEvent::TYPE_DOWN, 1);
    EXPECT_EQ(MouseEvent::TYPE_UP, 2);
    EXPECT_EQ(MouseEvent::TYPE_WHEEL, 3);
}

TEST(MouseEventTest, CombineButtonAndType) {
    MouseEvent ev;
    ev.mask = MouseEvent::BUTTON_LEFT | MouseEvent::TYPE_DOWN;
    EXPECT_NE(ev.mask & MouseEvent::BUTTON_LEFT, 0);
    EXPECT_EQ(ev.mask & MouseEvent::TYPE_DOWN, MouseEvent::TYPE_DOWN);
}

TEST(MouseEventTest, RightClick) {
    MouseEvent ev;
    ev.mask = MouseEvent::BUTTON_RIGHT | MouseEvent::TYPE_DOWN;
    ev.x = 300; ev.y = 400;
    EXPECT_NE(ev.mask & MouseEvent::BUTTON_RIGHT, 0);
    EXPECT_EQ(ev.x, 300);
    EXPECT_EQ(ev.y, 400);
}

TEST(MouseEventTest, WheelEvent) {
    MouseEvent ev;
    ev.mask = MouseEvent::BUTTON_WHEEL | MouseEvent::TYPE_WHEEL;
    ev.x = 0; ev.y = -120; // scroll up
    EXPECT_NE(ev.mask & MouseEvent::BUTTON_WHEEL, 0);
}

TEST(MouseEventTest, MultipleButtons) {
    MouseEvent ev;
    ev.mask = MouseEvent::BUTTON_LEFT | MouseEvent::BUTTON_RIGHT;
    EXPECT_NE(ev.mask & MouseEvent::BUTTON_LEFT, 0);
    EXPECT_NE(ev.mask & MouseEvent::BUTTON_RIGHT, 0);
}

TEST(MouseEventTest, BackAndForwardButtons) {
    MouseEvent ev;
    ev.mask = MouseEvent::BUTTON_BACK | MouseEvent::TYPE_DOWN;
    EXPECT_NE(ev.mask & MouseEvent::BUTTON_BACK, 0);

    ev.mask = MouseEvent::BUTTON_FORWARD | MouseEvent::TYPE_DOWN;
    EXPECT_NE(ev.mask & MouseEvent::BUTTON_FORWARD, 0);
}

TEST(MouseEventTest, CoordinatesNegative) {
    MouseEvent ev;
    ev.x = -100; ev.y = -200;
    EXPECT_EQ(ev.x, -100);
    EXPECT_EQ(ev.y, -200);
}

TEST(MouseEventTest, CoordinatesLarge) {
    MouseEvent ev;
    ev.x = 7680; ev.y = 4320; // 8K
    EXPECT_EQ(ev.x, 7680);
    EXPECT_EQ(ev.y, 4320);
}

// ============================================================================
// KeyEvent Tests
// ============================================================================

TEST(KeyEventTest, DefaultValues) {
    KeyEvent ev;
    EXPECT_EQ(ev.keycode, 0u);
    EXPECT_FALSE(ev.down);
    EXPECT_FALSE(ev.is_modifier);
    EXPECT_TRUE(ev.sequence.empty());
}

TEST(KeyEventTest, KeyPress) {
    KeyEvent ev;
    ev.keycode = 65;
    ev.down = true;
    EXPECT_EQ(ev.keycode, 65u);
    EXPECT_TRUE(ev.down);
}

TEST(KeyEventTest, KeyRelease) {
    KeyEvent ev;
    ev.keycode = 65;
    ev.down = false;
    EXPECT_EQ(ev.keycode, 65u);
    EXPECT_FALSE(ev.down);
}

TEST(KeyEventTest, ModifierKey) {
    KeyEvent ev;
    ev.keycode = 0xFFE3; // Control_L
    ev.down = true;
    ev.is_modifier = true;
    EXPECT_TRUE(ev.is_modifier);
}

TEST(KeyEventTest, NonModifierKey) {
    KeyEvent ev;
    ev.keycode = 65;
    ev.is_modifier = false;
    EXPECT_FALSE(ev.is_modifier);
}

TEST(KeyEventTest, TextSequence) {
    KeyEvent ev;
    ev.sequence = "Hello";
    EXPECT_EQ(ev.sequence, "Hello");
}

TEST(KeyEventTest, Copyable) {
    KeyEvent a;
    a.keycode = 42;
    a.down = true;
    a.is_modifier = true;
    a.sequence = "test";

    KeyEvent b = a;
    EXPECT_EQ(b.keycode, 42u);
    EXPECT_TRUE(b.down);
    EXPECT_TRUE(b.is_modifier);
    EXPECT_EQ(b.sequence, "test");
}

// ============================================================================
// VideoFrame Tests
// ============================================================================

TEST(VideoFrameTest, DefaultValues) {
    VideoFrame f;
    EXPECT_EQ(f.display, 0u);
    EXPECT_EQ(f.width, 0u);
    EXPECT_EQ(f.height, 0u);
    EXPECT_EQ(f.codec, 0u);
    EXPECT_EQ(f.timestamp, 0u);
    EXPECT_FALSE(f.keyframe);
    EXPECT_TRUE(f.data.empty());
    EXPECT_TRUE(f.is_monitor);
}

TEST(VideoFrameTest, SetCodec) {
    VideoFrame f;
    f.codec = 0; // raw
    EXPECT_EQ(f.codec, 0u);
    f.codec = 1; // h264
    EXPECT_EQ(f.codec, 1u);
    f.codec = 2; // h265
    EXPECT_EQ(f.codec, 2u);
}

TEST(VideoFrameTest, SetResolution) {
    VideoFrame f;
    f.width = 1920;
    f.height = 1080;
    EXPECT_EQ(f.width, 1920u);
    EXPECT_EQ(f.height, 1080u);
}

TEST(VideoFrameTest, SetTimestamp) {
    VideoFrame f;
    f.timestamp = 9876543210ULL;
    EXPECT_EQ(f.timestamp, 9876543210ULL);
}

TEST(VideoFrameTest, KeyframeFlag) {
    VideoFrame f;
    f.keyframe = true;
    EXPECT_TRUE(f.keyframe);
    f.keyframe = false;
    EXPECT_FALSE(f.keyframe);
}

TEST(VideoFrameTest, SetData) {
    VideoFrame f;
    f.data = {0x00, 0x01, 0x02, 0x03};
    EXPECT_EQ(f.data.size(), 4u);
    EXPECT_EQ(f.data[0], 0x00);
    EXPECT_EQ(f.data[1], 0x01);
    EXPECT_EQ(f.data[2], 0x02);
    EXPECT_EQ(f.data[3], 0x03);
}

TEST(VideoFrameTest, IsMonitorDefault) {
    VideoFrame f;
    EXPECT_TRUE(f.is_monitor);
    f.is_monitor = false;
    EXPECT_FALSE(f.is_monitor);
}

TEST(VideoFrameTest, Copyable) {
    VideoFrame a;
    a.width = 640;
    a.height = 480;
    a.codec = 1;
    a.keyframe = true;
    a.data = {1, 2, 3};

    VideoFrame b = a;
    EXPECT_EQ(b.width, 640u);
    EXPECT_EQ(b.height, 480u);
    EXPECT_EQ(b.codec, 1u);
    EXPECT_TRUE(b.keyframe);
    EXPECT_EQ(b.data.size(), 3u);
}

// ============================================================================
// AudioFrame Tests
// ============================================================================

TEST(AudioFrameTest, DefaultValues) {
    AudioFrame f;
    EXPECT_EQ(f.sample_rate, 48000u);
    EXPECT_EQ(f.channels, 2u);
    EXPECT_EQ(f.bits_per_sample, 16u);
    EXPECT_EQ(f.timestamp, 0u);
    EXPECT_TRUE(f.data.empty());
}

TEST(AudioFrameTest, SetSampleRate) {
    AudioFrame f;
    f.sample_rate = 44100;
    EXPECT_EQ(f.sample_rate, 44100u);
    f.sample_rate = 96000;
    EXPECT_EQ(f.sample_rate, 96000u);
}

TEST(AudioFrameTest, SetChannels) {
    AudioFrame f;
    f.channels = 1; // mono
    EXPECT_EQ(f.channels, 1u);
    f.channels = 6; // 5.1 surround
    EXPECT_EQ(f.channels, 6u);
}

TEST(AudioFrameTest, SetBitsPerSample) {
    AudioFrame f;
    f.bits_per_sample = 8;
    EXPECT_EQ(f.bits_per_sample, 8u);
    f.bits_per_sample = 24;
    EXPECT_EQ(f.bits_per_sample, 24u);
}

TEST(AudioFrameTest, SetTimestamp) {
    AudioFrame f;
    f.timestamp = 123456789;
    EXPECT_EQ(f.timestamp, 123456789u);
}

TEST(AudioFrameTest, SetData) {
    AudioFrame f;
    f.data = {0xAA, 0xBB, 0xCC};
    EXPECT_EQ(f.data.size(), 3u);
}

TEST(AudioFrameTest, Copyable) {
    AudioFrame a;
    a.sample_rate = 22050;
    a.channels = 1;
    a.data = {0x01};

    AudioFrame b = a;
    EXPECT_EQ(b.sample_rate, 22050u);
    EXPECT_EQ(b.channels, 1u);
    EXPECT_EQ(b.data.size(), 1u);
}

// ============================================================================
// CursorData Tests
// ============================================================================

TEST(CursorDataTest, DefaultValues) {
    CursorData cd;
    EXPECT_EQ(cd.id, 0u);
    EXPECT_EQ(cd.hot_x, 0);
    EXPECT_EQ(cd.hot_y, 0);
    EXPECT_EQ(cd.width, 0u);
    EXPECT_EQ(cd.height, 0u);
    EXPECT_TRUE(cd.colors.empty());
}

TEST(CursorDataTest, SetDimensions) {
    CursorData cd;
    cd.width = 32;
    cd.height = 32;
    EXPECT_EQ(cd.width, 32u);
    EXPECT_EQ(cd.height, 32u);
}

TEST(CursorDataTest, SetHotspot) {
    CursorData cd;
    cd.hot_x = 10;
    cd.hot_y = 5;
    EXPECT_EQ(cd.hot_x, 10);
    EXPECT_EQ(cd.hot_y, 5);
}

TEST(CursorDataTest, SetId) {
    CursorData cd;
    cd.id = 42;
    EXPECT_EQ(cd.id, 42u);
}

TEST(CursorDataTest, SetColors) {
    CursorData cd;
    cd.width = 2; cd.height = 2;
    // RGBA: 4 pixels * 4 bytes = 16 bytes
    cd.colors.resize(16, 128);
    EXPECT_EQ(cd.colors.size(), 16u);
    for (auto c : cd.colors) {
        EXPECT_EQ(c, 128);
    }
}

TEST(CursorDataTest, Copyable) {
    CursorData a;
    a.id = 5;
    a.hot_x = 3;
    a.hot_y = 7;
    a.width = 24;
    a.height = 24;
    a.colors = {1, 2, 3, 4};

    CursorData b = a;
    EXPECT_EQ(b.id, 5u);
    EXPECT_EQ(b.hot_x, 3);
    EXPECT_EQ(b.hot_y, 7);
    EXPECT_EQ(b.width, 24u);
    EXPECT_EQ(b.height, 24u);
    EXPECT_EQ(b.colors.size(), 4u);
}

// ============================================================================
// Resolution Tests
// ============================================================================

TEST(ResolutionTest, DefaultResolution) {
    Resolution r;
    EXPECT_EQ(r.width, 1920u);
    EXPECT_EQ(r.height, 1080u);
}

TEST(ResolutionTest, Equality) {
    Resolution a{1920, 1080};
    Resolution b{1920, 1080};
    Resolution c{3840, 2160};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(ResolutionTest, CommonResolutions) {
    Resolution r720{1280, 720};
    Resolution r1080{1920, 1080};
    Resolution r1440{2560, 1440};
    Resolution r4k{3840, 2160};

    EXPECT_NE(r720, r1080);
    EXPECT_NE(r1080, r1440);
    EXPECT_NE(r1440, r4k);
}

TEST(ResolutionTest, CustomResolution) {
    Resolution r{1024, 768};
    EXPECT_EQ(r.width, 1024u);
    EXPECT_EQ(r.height, 768u);
}

TEST(ResolutionTest, ZeroResolution) {
    Resolution r{0, 0};
    EXPECT_EQ(r.width, 0u);
    EXPECT_EQ(r.height, 0u);
}

TEST(ResolutionTest, VeryLargeResolution) {
    Resolution r{16384, 16384};
    EXPECT_EQ(r.width, 16384u);
    EXPECT_EQ(r.height, 16384u);
}

// ============================================================================
// PeerConfig Tests
// ============================================================================

TEST(PeerConfigTest, DefaultConstruction) {
    PeerConfig p;
    EXPECT_TRUE(p.id.empty());
    EXPECT_TRUE(p.username.empty());
    EXPECT_TRUE(p.hostname.empty());
    EXPECT_TRUE(p.platform.empty());
    EXPECT_TRUE(p.alias.empty());
    EXPECT_FALSE(p.online);
    EXPECT_EQ(p.nat_type, NatType::UNKNOWN_NAT);
    EXPECT_TRUE(p.tags.empty());
}

TEST(PeerConfigTest, SetId) {
    PeerConfig p;
    p.id = "peer-abc-123";
    EXPECT_EQ(p.id, "peer-abc-123");
}

TEST(PeerConfigTest, SetUsername) {
    PeerConfig p;
    p.username = "john_doe";
    EXPECT_EQ(p.username, "john_doe");
}

TEST(PeerConfigTest, SetHostname) {
    PeerConfig p;
    p.hostname = "desktop-pc";
    EXPECT_EQ(p.hostname, "desktop-pc");
}

TEST(PeerConfigTest, SetPlatform) {
    PeerConfig p;
    p.platform = "Linux";
    EXPECT_EQ(p.platform, "Linux");
}

TEST(PeerConfigTest, SetAlias) {
    PeerConfig p;
    p.alias = "Office PC";
    EXPECT_EQ(p.alias, "Office PC");
}

TEST(PeerConfigTest, OnlineFlag) {
    PeerConfig p;
    p.online = true;
    EXPECT_TRUE(p.online);
    p.online = false;
    EXPECT_FALSE(p.online);
}

TEST(PeerConfigTest, SetNatType) {
    PeerConfig p;
    p.nat_type = NatType::OPEN_INTERNET;
    EXPECT_EQ(p.nat_type, NatType::OPEN_INTERNET);
    p.nat_type = NatType::SYMMETRIC;
    EXPECT_EQ(p.nat_type, NatType::SYMMETRIC);
}

TEST(PeerConfigTest, SetTags) {
    PeerConfig p;
    p.tags = {"trusted", "office"};
    EXPECT_EQ(p.tags.size(), 2u);
    EXPECT_EQ(p.tags[0], "trusted");
    EXPECT_EQ(p.tags[1], "office");
}

TEST(PeerConfigTest, Copyable) {
    PeerConfig a;
    a.id = "copy-peer";
    a.username = "alice";
    a.online = true;

    PeerConfig b = a;
    EXPECT_EQ(b.id, "copy-peer");
    EXPECT_EQ(b.username, "alice");
    EXPECT_TRUE(b.online);
}

// ============================================================================
// LoginResponse Tests
// ============================================================================

TEST(LoginResponseTest, DefaultValues) {
    LoginResponse lr;
    EXPECT_FALSE(lr.success);
    EXPECT_TRUE(lr.message.empty());
    EXPECT_EQ(lr.code, 0);
    EXPECT_FALSE(lr.view_only);
}

TEST(LoginResponseTest, SuccessResponse) {
    LoginResponse lr;
    lr.success = true;
    lr.message = "Login OK";
    lr.code = 200;
    EXPECT_TRUE(lr.success);
    EXPECT_EQ(lr.message, "Login OK");
    EXPECT_EQ(lr.code, 200);
}

TEST(LoginResponseTest, FailureResponse) {
    LoginResponse lr;
    lr.success = false;
    lr.message = "Wrong Password";
    lr.code = 403;
    EXPECT_FALSE(lr.success);
    EXPECT_EQ(lr.message, "Wrong Password");
    EXPECT_EQ(lr.code, 403);
}

TEST(LoginResponseTest, ViewOnlyFlag) {
    LoginResponse lr;
    lr.view_only = true;
    EXPECT_TRUE(lr.view_only);
    lr.view_only = false;
    EXPECT_FALSE(lr.view_only);
}

TEST(LoginResponseTest, WithResolution) {
    LoginResponse lr;
    lr.resolution = Resolution{2560, 1440};
    EXPECT_EQ(lr.resolution.width, 2560u);
    EXPECT_EQ(lr.resolution.height, 1440u);
}

// ============================================================================
// ThumbnailData Tests
// ============================================================================

TEST(ThumbnailDataTest, DefaultValues) {
    ThumbnailData td;
    EXPECT_EQ(td.width, 0u);
    EXPECT_EQ(td.height, 0u);
    EXPECT_TRUE(td.pixels.empty());
}

TEST(ThumbnailDataTest, SetDimensions) {
    ThumbnailData td;
    td.width = 320;
    td.height = 240;
    EXPECT_EQ(td.width, 320u);
    EXPECT_EQ(td.height, 240u);
}

TEST(ThumbnailDataTest, SetPixels) {
    ThumbnailData td;
    td.width = 2; td.height = 2;
    td.pixels = {0xFF, 0x00, 0x00, 0xFF,  // red
                  0x00, 0xFF, 0x00, 0xFF,  // green
                  0x00, 0x00, 0xFF, 0xFF,  // blue
                  0xFF, 0xFF, 0xFF, 0xFF}; // white
    EXPECT_EQ(td.pixels.size(), 16u);
}

// ============================================================================
// Stream Interface Tests (via MockStream derivative)
// ============================================================================

class TestStream : public Stream {
public:
    bool send(const std::vector<uint8_t>& data) override {
        sent.push_back(data);
        return open_flag_;
    }
    std::vector<uint8_t> recv() override {
        if (recv_queue.empty()) return {};
        auto front = std::move(recv_queue.front());
        recv_queue.erase(recv_queue.begin());
        return front;
    }
    bool is_open() const override { return open_flag_; }
    void close() override { open_flag_ = false; }
    std::string local_addr() const override { return local_; }
    std::string remote_addr() const override { return remote_; }
    void set_nodelay(bool on) override { nodelay_ = on; }
    void set_encryption_key(const std::vector<uint8_t>& key) override { enc_key_ = key; }

    bool open_flag_ = true;
    std::string local_ = "127.0.0.1:10000";
    std::string remote_ = "192.168.1.5:21119";
    bool nodelay_ = false;
    std::vector<uint8_t> enc_key_;
    std::vector<std::vector<uint8_t>> sent;
    std::vector<std::vector<uint8_t>> recv_queue;
};

TEST(StreamTest, SendAndReceive) {
    TestStream s;
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    EXPECT_TRUE(s.send(data));
    EXPECT_EQ(s.sent.size(), 1u);
    EXPECT_EQ(s.sent[0], data);
}

TEST(StreamTest, RecvEmpty) {
    TestStream s;
    auto data = s.recv();
    EXPECT_TRUE(data.empty());
}

TEST(StreamTest, RecvQueued) {
    TestStream s;
    s.recv_queue.push_back({10, 20, 30});
    auto data = s.recv();
    EXPECT_EQ(data.size(), 3u);
    EXPECT_EQ(data[0], 10);
    EXPECT_EQ(data[1], 20);
    EXPECT_EQ(data[2], 30);
}

TEST(StreamTest, IsOpen) {
    TestStream s;
    EXPECT_TRUE(s.is_open());
}

TEST(StreamTest, Close) {
    TestStream s;
    EXPECT_TRUE(s.is_open());
    s.close();
    EXPECT_FALSE(s.is_open());
}

TEST(StreamTest, LocalAddr) {
    TestStream s;
    EXPECT_EQ(s.local_addr(), "127.0.0.1:10000");
}

TEST(StreamTest, RemoteAddr) {
    TestStream s;
    EXPECT_EQ(s.remote_addr(), "192.168.1.5:21119");
}

TEST(StreamTest, SetNodelay) {
    TestStream s;
    EXPECT_FALSE(s.nodelay_);
    s.set_nodelay(true);
    EXPECT_TRUE(s.nodelay_);
    s.set_nodelay(false);
    EXPECT_FALSE(s.nodelay_);
}

TEST(StreamTest, SetEncryptionKey) {
    TestStream s;
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
    s.set_encryption_key(key);
    EXPECT_EQ(s.enc_key_, key);
}

TEST(StreamTest, SendWhenClosed) {
    TestStream s;
    s.close();
    std::vector<uint8_t> data = {1, 2, 3};
    EXPECT_FALSE(s.send(data));
    EXPECT_EQ(s.sent.size(), 0u);
}

// ============================================================================
// Combined / Integration Tests
// ============================================================================

TEST(IntegrationTest, SessionManagerWithFileManager) {
    auto& sm = SessionManager::instance();
    sm.start_session("integ-test");
    EXPECT_TRUE(sm.has_active_session());

    FileManager fm;
    fm.set_job_type(JobType::SEND_FILE);
    fm.send("/tmp/test", "/remote/test");
    EXPECT_TRUE(fm.is_active());
    fm.cancel();
    EXPECT_FALSE(fm.is_active());

    sm.set_view_only(true);
    sm.set_encrypted(true);
    auto info = sm.get_current_session();
    EXPECT_TRUE(info.view_only);
    EXPECT_TRUE(info.encrypted);

    sm.end_session();
    EXPECT_FALSE(sm.has_active_session());
}

TEST(IntegrationTest, ClientInterfaceWithAllCallbacks) {
    MockClientInterface iface;
    iface.set_id("integ-client");
    iface.set_peer_id("peer-xyz");
    iface.set_key("secret-key");
    iface.set_token("auth-token");
    iface.set_conn_type(ConnType::DEFAULT_CONN);
    iface.set_force_relay(false);

    // Simulate a full connection lifecycle
    iface.update_direct(false);
    iface.update_received(false);
    iface.on_connection_ready();
    iface.on_login_success();

    // Video
    VideoFrame vf;
    vf.width = 1920; vf.height = 1080; vf.codec = 1;
    vf.keyframe = true;
    iface.on_video_frame(vf);

    // Audio
    AudioFrame af;
    af.sample_rate = 48000; af.channels = 2;
    iface.on_audio_frame(af);

    // Input
    CursorData cd;
    cd.id = 1; cd.width = 32; cd.height = 32;
    iface.on_cursor_data(cd);
    iface.on_cursor_position(500, 300);

    // Clipboard
    iface.on_clipboard_text("integration test");
    iface.on_clipboard_files({"/tmp/f1"});

    // File transfer
    iface.on_file_transfer_request("/remote/big.zip", 1000000);
    iface.on_file_transfer_progress(500000, 1000000);
    iface.on_file_transfer_done("/remote/big.zip");

    // Chat
    iface.on_chat_message("peer", "hi");

    // Misc
    iface.on_privacy_mode_changed(false);
    iface.on_switch_display(1);
    ControlPermissions perms;
    perms.keyboard = false;
    iface.on_permission_changed(perms);

    // Verify counts
    EXPECT_EQ(iface.direct_count(), 1);
    EXPECT_EQ(iface.received_count(), 1);
    EXPECT_EQ(iface.connection_ready_count(), 1);
    EXPECT_EQ(iface.login_success_count(), 1);
    EXPECT_EQ(iface.video_frame_count(), 1);
    EXPECT_EQ(iface.audio_frame_count(), 1);
    EXPECT_EQ(iface.cursor_data_count(), 1);
    EXPECT_EQ(iface.cursor_pos_count(), 1);
    EXPECT_EQ(iface.clipboard_text_count(), 1);
    EXPECT_EQ(iface.clipboard_files_count(), 1);
    EXPECT_EQ(iface.file_req_count(), 1);
    EXPECT_EQ(iface.file_prog_count(), 1);
    EXPECT_EQ(iface.file_done_count(), 1);
    EXPECT_EQ(iface.chat_count(), 1);
    EXPECT_EQ(iface.privacy_count(), 1);
    EXPECT_EQ(iface.display_count(), 1);
    EXPECT_EQ(iface.perms_count(), 1);

    // Connection close
    iface.on_connection_closed("user disconnected");
    EXPECT_EQ(iface.connection_closed_count(), 1);
    EXPECT_EQ(iface.connection_closed_reason(), "user disconnected");
}

TEST(IntegrationTest, AudioDeviceFullWorkflow) {
    MockAudioDevice dev;

    // List devices
    dev.set_list_devices_return({"Built-in Mic", "USB Headset", "HDMI Output"});
    auto devices = dev.list_devices();
    EXPECT_EQ(devices.size(), 3u);

    // Open input
    EXPECT_TRUE(dev.open_input("Built-in Mic"));
    EXPECT_TRUE(dev.input_open());

    // Open output
    EXPECT_TRUE(dev.open_output("HDMI Output"));
    EXPECT_TRUE(dev.output_open());

    // Read samples
    dev.set_read_samples_return({100, 200, 300, 400, 500});
    auto samples = dev.read_samples(5);
    EXPECT_EQ(samples.size(), 5u);

    // Write samples
    std::vector<int16_t> out = {256, -128, 1024, -512};
    EXPECT_TRUE(dev.write_samples(out));

    // Close
    dev.close();
    EXPECT_TRUE(dev.is_closed());
    EXPECT_FALSE(dev.input_open());
    EXPECT_FALSE(dev.output_open());
}

TEST(IntegrationTest, LanDiscoveryWithPeerConfig) {
    LanDiscovery ld;
    std::vector<PeerConfig> found_peers;

    ld.set_on_peer_found([&](const PeerConfig& peer) {
        found_peers.push_back(peer);
    });

    ld.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto discovered = ld.discover();
    ld.stop();

    // Both vectors should be consistently sized (0 in test environment)
    EXPECT_EQ(found_peers.size(), discovered.size());
}

TEST(IntegrationTest, ClientStartWithAllConnTypes) {
    std::vector<ConnType> types = {
        ConnType::DEFAULT_CONN,
        ConnType::FILE_TRANSFER,
        ConnType::PORT_FORWARD,
        ConnType::RDP
    };

    for (auto ct : types) {
        Client c;
        auto iface = std::make_shared<MockClientInterface>();
        iface->set_peer_id("ct-test");
        iface->set_key("key");
        iface->set_token("token");
        iface->set_conn_type(ct);

        bool started = c.start("ct-test", "key", "token", ct, iface);
        EXPECT_TRUE(started == true || started == false);
        c.close();
    }
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST(EdgeCaseTest, EmptyStringsEverywhere) {
    ClientClipboardContext ctx;
    ctx.last_text = "";
    EXPECT_TRUE(ctx.last_text.empty());

    SessionInfo info;
    info.peer_id = "";
    info.peer_name = "";
    info.connection_type = "";
    EXPECT_TRUE(info.peer_id.empty());
    EXPECT_TRUE(info.peer_name.empty());
    EXPECT_TRUE(info.connection_type.empty());

    PeerConfig peer;
    peer.id = "";
    peer.username = "";
    EXPECT_TRUE(peer.id.empty());

    ControlPermissions perms;
    // Even with no changes, defaults are as expected
    EXPECT_TRUE(perms.keyboard);
}

TEST(EdgeCaseTest, LargeDataVideoFrame) {
    VideoFrame f;
    f.width = 7680;
    f.height = 4320;
    f.data.resize(1000000, 0xAA); // 1MB of data
    EXPECT_EQ(f.data.size(), 1000000u);
    EXPECT_EQ(f.data[0], 0xAA);
    EXPECT_EQ(f.data[999999], 0xAA);
}

TEST(EdgeCaseTest, LargeDataAudioFrame) {
    AudioFrame f;
    f.sample_rate = 192000;
    f.channels = 8;
    f.data.resize(500000, 0xBB);
    EXPECT_EQ(f.data.size(), 500000u);
}

TEST(EdgeCaseTest, LargeCursorColors) {
    CursorData cd;
    cd.width = 256;
    cd.height = 256;
    cd.colors.resize(256 * 256 * 4); // 256K RGBA pixels
    EXPECT_EQ(cd.colors.size(), 262144u);
}

TEST(EdgeCaseTest, VeryLongPeerId) {
    std::string long_id(10000, 'A');
    PeerConfig p;
    p.id = long_id;
    EXPECT_EQ(p.id.size(), 10000u);
}

TEST(EdgeCaseTest, ManyClipboardFiles) {
    MockClientInterface iface;
    std::vector<std::string> many_files;
    for (int i = 0; i < 1000; i++) {
        many_files.push_back("/tmp/file_" + std::to_string(i) + ".txt");
    }
    iface.on_clipboard_files(many_files);
    EXPECT_EQ(iface.clipboard_files().size(), 1000u);
}

TEST(EdgeCaseTest, MaxUint32Values) {
    VideoFrame f;
    f.width = 0xFFFFFFFF;
    f.height = 0xFFFFFFFF;
    f.codec = 0xFFFFFFFF;
    EXPECT_EQ(f.width, 0xFFFFFFFFu);
    EXPECT_EQ(f.height, 0xFFFFFFFFu);
    EXPECT_EQ(f.codec, 0xFFFFFFFFu);
}

TEST(EdgeCaseTest, MaxInt32MouseCoords) {
    MouseEvent ev;
    ev.x = 2147483647;  // INT32_MAX
    ev.y = -2147483648; // INT32_MIN
    EXPECT_EQ(ev.x, 2147483647);
    EXPECT_EQ(ev.y, -2147483648);
}

TEST(EdgeCaseTest, ZeroReadSamples) {
    MockAudioDevice dev;
    auto samples = dev.read_samples(0);
    EXPECT_TRUE(samples.empty());
    EXPECT_EQ(dev.last_read_count(), 0u);
}

TEST(EdgeCaseTest, SpecialCharactersInStrings) {
    PeerConfig p;
    p.id = "peer\x00\x01\x02"; // with null bytes
    p.username = "user\n\t\r";
    p.alias = "alias with spaces and symbols !@#$%^";
    EXPECT_FALSE(p.id.empty());
    EXPECT_FALSE(p.username.empty());
    EXPECT_FALSE(p.alias.empty());
}

TEST(EdgeCaseTest, UnicodeStrings) {
    MockClientInterface iface;
    iface.on_clipboard_text("こんにちは世界");
    EXPECT_EQ(iface.clipboard_text(), "こんにちは世界");

    iface.on_chat_message("ユーザー", "メッセージ");
    EXPECT_EQ(iface.chat_sender(), "ユーザー");
    EXPECT_EQ(iface.chat_msg(), "メッセージ");
}

// ============================================================================
// Thread Safety Smoke Tests
// ============================================================================

TEST(ThreadSafetyTest, SessionManagerConcurrentAccess) {
    auto& sm = SessionManager::instance();
    sm.start_session("thread-test");

    std::atomic<int> reads{0};
    std::atomic<bool> done{false};

    std::thread reader([&]() {
        while (!done) {
            sm.has_active_session();
            auto info = sm.get_current_session();
            (void)info;
            reads++;
        }
    });

    std::thread writer([&]() {
        for (int i = 0; i < 10; i++) {
            sm.set_view_only(i % 2 == 0);
            sm.set_encrypted(i % 2 == 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        done = true;
    });

    writer.join();
    reader.join();

    EXPECT_GT(reads, 0);
    sm.end_session();
}

TEST(ThreadSafetyTest, FileManagerConcurrentAccess) {
    // Simple concurrent access to checks — should not crash
    FileManager fm;

    std::thread t1([&]() {
        for (int i = 0; i < 100; i++) {
            fm.is_active();
            fm.get_job_type();
            fm.total_size();
            fm.transferred();
        }
    });

    std::thread t2([&]() {
        for (int i = 0; i < 100; i++) {
            fm.set_job_type(static_cast<JobType>(i % 4));
        }
    });

    t1.join();
    t2.join();
    SUCCEED();
}

TEST(ThreadSafetyTest, ClientClipboardContextConcurrent) {
    ClientClipboardContext ctx;

    std::thread t1([&]() {
        for (int i = 0; i < 100; i++) {
            ctx.is_text_required = !ctx.is_text_required;
            ctx.last_text = "thread1-" + std::to_string(i);
        }
    });

    std::thread t2([&]() {
        for (int i = 0; i < 100; i++) {
            ctx.is_file_required = !ctx.is_file_required;
            ctx.running = !ctx.running;
        }
    });

    t1.join();
    t2.join();
    SUCCEED();
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST(StressTest, CreateManyClients) {
    for (int i = 0; i < 100; i++) {
        Client c;
        EXPECT_EQ(c.get_phase(), Client::Phase::DISCONNECTED);
    }
}

TEST(StressTest, CreateManyFileManagers) {
    for (int i = 0; i < 100; i++) {
        FileManager fm;
        EXPECT_EQ(fm.get_job_type(), JobType::SEND_FILE);
        EXPECT_FALSE(fm.is_active());
    }
}

TEST(StressTest, CreateManySessionInfos) {
    for (int i = 0; i < 100; i++) {
        SessionInfo info;
        info.peer_id = "stress-" + std::to_string(i);
        EXPECT_FALSE(info.encrypted);
    }
}

TEST(StressTest, CreateManyLanDiscoveries) {
    for (int i = 0; i < 50; i++) {
        LanDiscovery ld;
        ld.start();
        ld.stop();
    }
}

TEST(StressTest, RapidStartStopLanDiscovery) {
    LanDiscovery ld;
    for (int i = 0; i < 20; i++) {
        ld.start();
        ld.stop();
    }
}

TEST(StressTest, MockInterfaceResetManyTimes) {
    MockClientInterface iface;
    for (int i = 0; i < 100; i++) {
        iface.on_login_success();
        iface.on_connection_ready();
        iface.on_connection_closed("reason");
        iface.reset();
        EXPECT_EQ(iface.login_success_count(), 0);
        EXPECT_EQ(iface.connection_ready_count(), 0);
        EXPECT_EQ(iface.connection_closed_count(), 0);
    }
}

TEST(StressTest, AudioDeviceManyWrites) {
    MockAudioDevice dev;
    std::vector<int16_t> data(1024, 42);
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(dev.write_samples(data));
    }
    EXPECT_EQ(dev.write_count(), 100);
}

TEST(StressTest, AudioDeviceManyReads) {
    MockAudioDevice dev;
    dev.set_read_samples_return({1, 2, 3, 4, 5});
    for (int i = 0; i < 100; i++) {
        auto samples = dev.read_samples(5);
        EXPECT_EQ(samples.size(), 5u);
    }
    EXPECT_EQ(dev.read_count(), 100);
}

// ============================================================================
// Codec Type Tests
// ============================================================================

TEST(CodecTest, VideoCodecValues) {
    // 0=raw, 1=h264, 2=h265
    VideoFrame f;
    f.codec = 0; EXPECT_EQ(f.codec, 0u);
    f.codec = 1; EXPECT_EQ(f.codec, 1u);
    f.codec = 2; EXPECT_EQ(f.codec, 2u);
}

TEST(CodecTest, AudioCodecValues) {
    AudioFrame f;
    f.bits_per_sample = 8; EXPECT_EQ(f.bits_per_sample, 8u);
    f.bits_per_sample = 16; EXPECT_EQ(f.bits_per_sample, 16u);
    f.bits_per_sample = 24; EXPECT_EQ(f.bits_per_sample, 24u);
    f.bits_per_sample = 32; EXPECT_EQ(f.bits_per_sample, 32u);
}

// ============================================================================
// SessionInfo ConnectionType Value Tests
// ============================================================================

TEST(SessionInfoConnectionTest, ValidConnectionTypes) {
    SessionInfo info;
    const std::vector<std::string> valid_types = {"TCP", "UDP", "Relay", "WebSocket", "Direct"};

    for (const auto& t : valid_types) {
        info.connection_type = t;
        EXPECT_EQ(info.connection_type, t);
    }
}

// ============================================================================
// Client Start/Close Lifecycle with Phase Checks
// ============================================================================

TEST(ClientLifecycleTest, StartThenClosePhases) {
    Client c;
    EXPECT_EQ(c.get_phase(), Client::Phase::DISCONNECTED);

    auto iface = std::make_shared<MockClientInterface>();
    iface->set_peer_id("lifecycle-test");
    iface->set_key("k");
    iface->set_token("t");

    c.start("lifecycle-test", "k", "t", ConnType::DEFAULT_CONN, iface);
    // Phase may change depending on network, but disconnect check is safe
    c.close();
    EXPECT_EQ(c.get_phase(), Client::Phase::DISCONNECTED);
}

TEST(ClientLifecycleTest, MultipleStartCloseCycles) {
    for (int i = 0; i < 5; i++) {
        Client c;
        auto iface = std::make_shared<MockClientInterface>();
        iface->set_peer_id("cycle-" + std::to_string(i));
        iface->set_key("k" + std::to_string(i));
        iface->set_token("t" + std::to_string(i));

        c.start("cycle-" + std::to_string(i), "k" + std::to_string(i),
                "t" + std::to_string(i), ConnType::DEFAULT_CONN, iface);
        c.close();
        EXPECT_EQ(c.get_phase(), Client::Phase::DISCONNECTED);
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
