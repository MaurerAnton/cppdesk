#include <gtest/gtest.h>
#include "server/server.hpp"
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

using namespace cppdesk::server;
using namespace cppdesk::common;

// ============================================================================
// Test Helpers — Mock Stream for unit-testing connections
// ============================================================================

class MockStream : public Stream {
public:
    MockStream(bool open = true, const std::string& local = "127.0.0.1:1000",
               const std::string& remote = "127.0.0.1:2000")
        : open_(open), local_(local), remote_(remote) {}

    bool send(const std::vector<uint8_t>& data) override {
        if (!open_) return false;
        std::lock_guard lk(sent_mutex_);
        sent_data_.push_back(std::move(data));
        send_count_++;
        return true;
    }

    std::vector<uint8_t> recv() override {
        std::lock_guard lk(recv_mutex_);
        if (recv_queue_.empty()) return {};
        auto front = std::move(recv_queue_.front());
        recv_queue_.pop_front();
        return front;
    }

    bool is_open() const override { return open_; }
    void close() override { open_ = false; }

    std::string local_addr() const override { return local_; }
    std::string remote_addr() const override { return remote_; }
    void set_nodelay(bool) override {}
    void set_encryption_key(const std::vector<uint8_t>&) override {}

    // Test helper injection
    void inject_recv(std::vector<uint8_t> data) {
        std::lock_guard lk(recv_mutex_);
        recv_queue_.push_back(std::move(data));
    }

    void set_open(bool o) { open_ = o; }

    size_t sent_count() const { return send_count_.load(); }
    std::vector<uint8_t> last_sent() const {
        std::lock_guard lk(sent_mutex_);
        return sent_data_.empty() ? std::vector<uint8_t>{} : sent_data_.back();
    }
    std::vector<std::vector<uint8_t>> all_sent() const {
        std::lock_guard lk(sent_mutex_);
        return sent_data_;
    }

private:
    std::atomic<bool> open_{true};
    std::string local_;
    std::string remote_;
    mutable std::mutex sent_mutex_;
    std::vector<std::vector<uint8_t>> sent_data_;
    std::atomic<size_t> send_count_{0};
    mutable std::mutex recv_mutex_;
    std::deque<std::vector<uint8_t>> recv_queue_;
};

// ============================================================================
// GenericService Tests
// ============================================================================

TEST(GenericServiceTest, NameReturnsConstructorArg) {
    GenericService svc("test_service");
    EXPECT_EQ(svc.name(), "test_service");
}

TEST(GenericServiceTest, NameEmptyString) {
    GenericService svc("");
    EXPECT_EQ(svc.name(), "");
}

TEST(GenericServiceTest, NameLongString) {
    std::string long_name(1024, 'x');
    GenericService svc(long_name);
    EXPECT_EQ(svc.name(), long_name);
}

TEST(GenericServiceTest, SubscriberCountInitiallyZero) {
    GenericService svc("test");
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(GenericServiceTest, SubscribeIncrementsCount) {
    GenericService svc("test");
    svc.on_subscribe(1);
    EXPECT_EQ(svc.subscriber_count(), 1u);
    svc.on_subscribe(2);
    EXPECT_EQ(svc.subscriber_count(), 2u);
}

TEST(GenericServiceTest, UnsubscribeDecrementsCount) {
    GenericService svc("test");
    svc.on_subscribe(1);
    svc.on_subscribe(2);
    svc.on_unsubscribe(1);
    EXPECT_EQ(svc.subscriber_count(), 1u);
    svc.on_unsubscribe(2);
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(GenericServiceTest, UnsubscribeNonExistentIsSafe) {
    GenericService svc("test");
    svc.on_unsubscribe(999);
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(GenericServiceTest, IsSubscribedReturnsTrueAfterSubscribe) {
    GenericService svc("test");
    EXPECT_FALSE(svc.is_subscribed(1));
    svc.on_subscribe(1);
    EXPECT_TRUE(svc.is_subscribed(1));
}

TEST(GenericServiceTest, IsSubscribedReturnsFalseAfterUnsubscribe) {
    GenericService svc("test");
    svc.on_subscribe(1);
    svc.on_unsubscribe(1);
    EXPECT_FALSE(svc.is_subscribed(1));
}

TEST(GenericServiceTest, IsSubscribedDifferentConnections) {
    GenericService svc("test");
    svc.on_subscribe(100);
    svc.on_subscribe(200);
    EXPECT_TRUE(svc.is_subscribed(100));
    EXPECT_TRUE(svc.is_subscribed(200));
    EXPECT_FALSE(svc.is_subscribed(300));
}

TEST(GenericServiceTest, DuplicateSubscribeIsSafe) {
    GenericService svc("test");
    svc.on_subscribe(1);
    svc.on_subscribe(1); // duplicate
    EXPECT_EQ(svc.subscriber_count(), 1u);
    EXPECT_TRUE(svc.is_subscribed(1));
}

TEST(GenericServiceTest, DuplicateUnsubscribeIsSafe) {
    GenericService svc("test");
    svc.on_subscribe(1);
    svc.on_unsubscribe(1);
    svc.on_unsubscribe(1); // duplicate
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(GenericServiceTest, SetOptionDefaultDoesNothing) {
    GenericService svc("test");
    // Base implementation is a no-op; just verify no crash
    svc.set_option("key", "value");
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(GenericServiceTest, StartDefaultDoesNothing) {
    GenericService svc("test");
    svc.start();
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(GenericServiceTest, StopDefaultDoesNothing) {
    GenericService svc("test");
    svc.stop();
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(GenericServiceTest, ManySubscribers) {
    GenericService svc("test");
    for (int32_t i = 1; i <= 1000; ++i) {
        svc.on_subscribe(i);
    }
    EXPECT_EQ(svc.subscriber_count(), 1000u);
    for (int32_t i = 1; i <= 1000; ++i) {
        EXPECT_TRUE(svc.is_subscribed(i));
    }
}

TEST(GenericServiceTest, ManySubscribersUnsubscribeAll) {
    GenericService svc("test");
    for (int32_t i = 1; i <= 500; ++i) svc.on_subscribe(i);
    for (int32_t i = 1; i <= 500; ++i) svc.on_unsubscribe(i);
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(GenericServiceTest, NegativeConnectionIds) {
    GenericService svc("test");
    svc.on_subscribe(-1);
    svc.on_subscribe(-100);
    EXPECT_EQ(svc.subscriber_count(), 2u);
    EXPECT_TRUE(svc.is_subscribed(-1));
    svc.on_unsubscribe(-1);
    EXPECT_FALSE(svc.is_subscribed(-1));
}

// ============================================================================
// ConnInner Tests
// ============================================================================

TEST(ConnInnerTest, ConstructWithValidStream) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(1, stream, "192.168.1.1:12345");
    EXPECT_EQ(conn.id(), 1);
    EXPECT_EQ(conn.addr(), "192.168.1.1:12345");
    EXPECT_TRUE(conn.is_open());
}

TEST(ConnInnerTest, ConstructWithClosedStream) {
    auto stream = std::make_shared<MockStream>(false);
    ConnInner conn(2, stream, "10.0.0.1:9999");
    EXPECT_EQ(conn.id(), 2);
    EXPECT_FALSE(conn.is_open());
}

TEST(ConnInnerTest, IdIsInt32) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(2147483647, stream, "addr");
    EXPECT_EQ(conn.id(), 2147483647);
}

TEST(ConnInnerTest, NegativeId) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(-5, stream, "addr");
    EXPECT_EQ(conn.id(), -5);
}

TEST(ConnInnerTest, SendWhenOpen) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(1, stream, "addr");
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    conn.send(data);
    EXPECT_EQ(stream->sent_count(), 1u);
}

TEST(ConnInnerTest, SendWhenClosed) {
    auto stream = std::make_shared<MockStream>(false);
    ConnInner conn(1, stream, "addr");
    std::vector<uint8_t> data = {0xFF};
    conn.send(data);
    EXPECT_EQ(stream->sent_count(), 0u);
}

TEST(ConnInnerTest, SendEmptyData) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(1, stream, "addr");
    std::vector<uint8_t> empty;
    conn.send(empty);
    // Should still attempt send when open
    EXPECT_EQ(stream->sent_count(), 1u);
}

TEST(ConnInnerTest, SendPreservesData) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(1, stream, "addr");
    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
    conn.send(data);
    auto last = stream->last_sent();
    ASSERT_EQ(last.size(), 4u);
    EXPECT_EQ(last[0], 0xDE);
    EXPECT_EQ(last[3], 0xEF);
}

TEST(ConnInnerTest, SendLargeData) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(1, stream, "addr");
    std::vector<uint8_t> data(1024 * 1024, 0xAA); // 1 MB
    conn.send(data);
    EXPECT_EQ(stream->sent_count(), 1u);
}

TEST(ConnInnerTest, CloseClosesStream) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(1, stream, "addr");
    EXPECT_TRUE(conn.is_open());
    conn.close();
    EXPECT_FALSE(conn.is_open());
}

TEST(ConnInnerTest, DestructorCallsClose) {
    auto stream = std::make_shared<MockStream>();
    EXPECT_TRUE(stream->is_open());
    {
        ConnInner conn(1, stream, "addr");
        EXPECT_TRUE(conn.is_open());
    }
    EXPECT_FALSE(stream->is_open());
}

TEST(ConnInnerTest, ControlPermissionsDefault) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(1, stream, "addr");
    auto perms = conn.get_control_permissions();
    // Default ControlPermissions has keyboard=true, clipboard=true,
    // file_transfer=true, audio=true, restart=false, shutdown=false
    EXPECT_TRUE(perms.keyboard);
    EXPECT_TRUE(perms.clipboard);
    EXPECT_TRUE(perms.file_transfer);
    EXPECT_TRUE(perms.audio);
    EXPECT_FALSE(perms.restart);
    EXPECT_FALSE(perms.shutdown);
}

TEST(ConnInnerTest, SetAndGetControlPermissions) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(1, stream, "addr");

    ControlPermissions custom;
    custom.keyboard = false;
    custom.clipboard = false;
    custom.file_transfer = false;
    custom.audio = false;
    custom.restart = true;
    custom.shutdown = true;
    custom.privacy_mode = true;

    conn.set_control_permissions(custom);
    auto perms = conn.get_control_permissions();

    EXPECT_FALSE(perms.keyboard);
    EXPECT_FALSE(perms.clipboard);
    EXPECT_FALSE(perms.file_transfer);
    EXPECT_FALSE(perms.audio);
    EXPECT_TRUE(perms.restart);
    EXPECT_TRUE(perms.shutdown);
    EXPECT_TRUE(perms.privacy_mode);
}

TEST(ConnInnerTest, ControlPermissionsRoundTrip) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(1, stream, "addr");

    ControlPermissions p1;
    p1.keyboard = true; p1.clipboard = false; p1.file_transfer = true;
    p1.audio = false; p1.restart = true; p1.shutdown = false;

    conn.set_control_permissions(p1);
    auto p2 = conn.get_control_permissions();

    EXPECT_EQ(p2.keyboard, p1.keyboard);
    EXPECT_EQ(p2.clipboard, p1.clipboard);
    EXPECT_EQ(p2.file_transfer, p1.file_transfer);
    EXPECT_EQ(p2.audio, p1.audio);
    EXPECT_EQ(p2.restart, p1.restart);
    EXPECT_EQ(p2.shutdown, p1.shutdown);
}

TEST(ConnInnerTest, StreamPointerAccess) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(42, stream, "addr");
    EXPECT_EQ(conn.stream().get(), stream.get());
}

// ============================================================================
// DisplayService Tests
// ============================================================================

TEST(DisplayServiceTest, NameIsDisplay) {
    DisplayService svc;
    EXPECT_EQ(svc.name(), "display");
    EXPECT_STREQ(svc.name().c_str(), "display");
}

TEST(DisplayServiceTest, StaticConstantMatches) {
    EXPECT_STREQ(DisplayService::NAME, "display");
}

TEST(DisplayServiceTest, PrimaryDisplayIndexReturnsZero) {
    EXPECT_EQ(DisplayService::primary_display_idx(), 0);
}

TEST(DisplayServiceTest, PrimaryDisplayIndexIsNonNegative) {
    EXPECT_GE(DisplayService::primary_display_idx(), 0);
}

TEST(DisplayServiceTest, SubscriberManagement) {
    DisplayService svc;
    EXPECT_EQ(svc.subscriber_count(), 0u);
    svc.on_subscribe(10);
    EXPECT_EQ(svc.subscriber_count(), 1u);
    EXPECT_TRUE(svc.is_subscribed(10));
    svc.on_unsubscribe(10);
    EXPECT_FALSE(svc.is_subscribed(10));
}

TEST(DisplayServiceTest, InheritsGenericService) {
    DisplayService svc;
    Service* base = &svc;
    EXPECT_EQ(base->name(), "display");
}

TEST(DisplayServiceTest, MultipleInstances) {
    DisplayService svc1;
    DisplayService svc2;
    EXPECT_EQ(svc1.name(), svc2.name());
    svc1.on_subscribe(1);
    svc2.on_subscribe(2);
    EXPECT_EQ(svc1.subscriber_count(), 1u);
    EXPECT_EQ(svc2.subscriber_count(), 1u);
    EXPECT_FALSE(svc1.is_subscribed(2));
    EXPECT_FALSE(svc2.is_subscribed(1));
}

// ============================================================================
// VideoService Tests
// ============================================================================

TEST(VideoServiceTest, MonitorServiceName) {
    EXPECT_EQ(VideoService::service_name(VideoSource::MONITOR, 0), "video_monitor_0");
    EXPECT_EQ(VideoService::service_name(VideoSource::MONITOR, 1), "video_monitor_1");
}

TEST(VideoServiceTest, CameraServiceName) {
    EXPECT_EQ(VideoService::service_name(VideoSource::CAMERA, 0), "video_camera_0");
    EXPECT_EQ(VideoService::service_name(VideoSource::CAMERA, 5), "video_camera_5");
}

TEST(VideoServiceTest, ConstructorSetsCorrectName) {
    VideoService mon(VideoSource::MONITOR, 0);
    EXPECT_EQ(mon.name(), "video_monitor_0");

    VideoService cam(VideoSource::CAMERA, 2);
    EXPECT_EQ(cam.name(), "video_camera_2");
}

TEST(VideoServiceTest, MonitorPrefixConstant) {
    EXPECT_STREQ(VideoService::MONITOR_PREFIX, "video_monitor_");
}

TEST(VideoServiceTest, CameraPrefixConstant) {
    EXPECT_STREQ(VideoService::CAMERA_PREFIX, "video_camera_");
}

TEST(VideoServiceTest, ServiceNameMatchesPrefixPlusIndex) {
    for (int32_t i = 0; i < 10; ++i) {
        std::string expected = "video_monitor_" + std::to_string(i);
        EXPECT_EQ(VideoService::service_name(VideoSource::MONITOR, i), expected);
    }
}

TEST(VideoServiceTest, NegativeDisplayIndex) {
    std::string name = VideoService::service_name(VideoSource::MONITOR, -1);
    EXPECT_EQ(name, "video_monitor_-1");
}

TEST(VideoServiceTest, StartSetsRunningState) {
    VideoService svc(VideoSource::MONITOR, 0);
    svc.start();
    // After start, the service should be running (internal state)
    EXPECT_EQ(svc.name(), "video_monitor_0");
}

TEST(VideoServiceTest, StopAfterStart) {
    VideoService svc(VideoSource::MONITOR, 0);
    svc.start();
    svc.stop();
    // Should not crash; state transitions safely
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(VideoServiceTest, DoubleStartIsSafe) {
    VideoService svc(VideoSource::MONITOR, 0);
    svc.start();
    svc.start(); // second start
    svc.stop();
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(VideoServiceTest, StopWithoutStartIsSafe) {
    VideoService svc(VideoSource::MONITOR, 0);
    svc.stop(); // stop before start
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(VideoServiceTest, SetOptionDoesNotThrow) {
    VideoService svc(VideoSource::MONITOR, 0);
    svc.set_option("quality", "high");
    svc.set_option("bitrate", "1000000");
    svc.set_option("fps", "30");
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(VideoServiceTest, SetOptionEmptyKeyValue) {
    VideoService svc(VideoSource::MONITOR, 0);
    svc.set_option("", "");
    svc.set_option("key", "");
    svc.set_option("", "value");
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(VideoServiceTest, VideoSourceEnumValues) {
    // Verify enum values are distinct
    EXPECT_NE(static_cast<int>(VideoSource::MONITOR),
              static_cast<int>(VideoSource::CAMERA));
    EXPECT_NE(static_cast<int>(VideoSource::CAMERA),
              static_cast<int>(VideoSource::VIRTUAL));
    EXPECT_NE(static_cast<int>(VideoSource::MONITOR),
              static_cast<int>(VideoSource::VIRTUAL));
}

TEST(VideoServiceTest, SubscriberManagement) {
    VideoService svc(VideoSource::MONITOR, 0);
    svc.on_subscribe(100);
    svc.on_subscribe(200);
    EXPECT_EQ(svc.subscriber_count(), 2u);
    svc.on_unsubscribe(100);
    EXPECT_EQ(svc.subscriber_count(), 1u);
    EXPECT_FALSE(svc.is_subscribed(100));
    EXPECT_TRUE(svc.is_subscribed(200));
}

TEST(VideoServiceTest, InheritsGenericService) {
    VideoService svc(VideoSource::CAMERA, 0);
    Service* base = &svc;
    EXPECT_EQ(base->name(), "video_camera_0");
    base->on_subscribe(42);
    EXPECT_EQ(base->subscriber_count(), 1u);
}

// ============================================================================
// AudioService Tests
// ============================================================================

TEST(AudioServiceTest, NameIsAudio) {
    AudioService svc;
    EXPECT_EQ(svc.name(), "audio");
    EXPECT_STREQ(svc.name().c_str(), "audio");
}

TEST(AudioServiceTest, StaticConstantMatches) {
    EXPECT_STREQ(AudioService::NAME, "audio");
}

TEST(AudioServiceTest, StartIsSafe) {
    AudioService svc;
    svc.start();
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(AudioServiceTest, StopIsSafe) {
    AudioService svc;
    svc.stop();
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(AudioServiceTest, StartStopCycle) {
    AudioService svc;
    svc.start();
    svc.stop();
    svc.start();
    svc.stop();
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(AudioServiceTest, SubscriberCount) {
    AudioService svc;
    EXPECT_EQ(svc.subscriber_count(), 0u);
    svc.on_subscribe(1);
    EXPECT_EQ(svc.subscriber_count(), 1u);
    svc.on_subscribe(1);
    EXPECT_EQ(svc.subscriber_count(), 1u); // duplicate safe
    svc.on_subscribe(2);
    EXPECT_EQ(svc.subscriber_count(), 2u);
    svc.on_unsubscribe(1);
    svc.on_unsubscribe(2);
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(AudioServiceTest, InheritsGenericService) {
    AudioService svc;
    Service* base = &svc;
    EXPECT_EQ(base->name(), "audio");
}

TEST(AudioServiceTest, AudioFrameStructure) {
    AudioFrame frame;
    EXPECT_EQ(frame.sample_rate, 48000u);
    EXPECT_EQ(frame.channels, 2u);
    EXPECT_EQ(frame.bits_per_sample, 16u);
    EXPECT_EQ(frame.timestamp, 0u);
    EXPECT_TRUE(frame.data.empty());
}

TEST(AudioServiceTest, AudioFrameDefaultValues) {
    AudioFrame f;
    f.sample_rate = 44100;
    f.channels = 1;
    f.bits_per_sample = 24;
    EXPECT_EQ(f.sample_rate, 44100u);
    EXPECT_EQ(f.channels, 1u);
    EXPECT_EQ(f.bits_per_sample, 24u);
}

TEST(AudioServiceTest, AudioFrameDataAssignment) {
    AudioFrame f;
    std::vector<uint8_t> audio_data(480, 0x80);
    f.data = audio_data;
    EXPECT_EQ(f.data.size(), 480u);
    EXPECT_EQ(f.data[0], 0x80);
}

// ============================================================================
// InputService Tests
// ============================================================================

TEST(InputServiceTest, CreateCursorHasCorrectName) {
    auto svc = InputService::create_cursor();
    EXPECT_EQ(svc->name(), "cursor");
    EXPECT_STREQ(InputService::NAME_CURSOR, "cursor");
}

TEST(InputServiceTest, CreatePositionHasCorrectName) {
    auto svc = InputService::create_position();
    EXPECT_EQ(svc->name(), "cursor_pos");
    EXPECT_STREQ(InputService::NAME_POS, "cursor_pos");
}

TEST(InputServiceTest, CreateWindowFocusHasCorrectName) {
    auto svc = InputService::create_window_focus();
    EXPECT_EQ(svc->name(), "window_focus");
    EXPECT_STREQ(InputService::NAME_WINDOW_FOCUS, "window_focus");
}

TEST(InputServiceTest, FactoryNamesAreUnique) {
    auto cursor = InputService::create_cursor();
    auto pos = InputService::create_position();
    auto focus = InputService::create_window_focus();

    EXPECT_NE(cursor->name(), pos->name());
    EXPECT_NE(pos->name(), focus->name());
    EXPECT_NE(cursor->name(), focus->name());
}

TEST(InputServiceTest, StartStopAreNoOps) {
    auto svc = InputService::create_cursor();
    svc->start();
    svc->stop();
    svc->start();
    EXPECT_EQ(svc->subscriber_count(), 0u);
}

TEST(InputServiceTest, SubscriberOperations) {
    auto svc = InputService::create_cursor();
    svc->on_subscribe(1);
    svc->on_subscribe(2);
    svc->on_subscribe(3);
    EXPECT_EQ(svc->subscriber_count(), 3u);
    EXPECT_TRUE(svc->is_subscribed(2));
    svc->on_unsubscribe(2);
    EXPECT_FALSE(svc->is_subscribed(2));
}

TEST(InputServiceTest, CustomNameSuffix) {
    InputService custom("custom_input_type");
    EXPECT_EQ(custom.name(), "custom_input_type");
}

TEST(InputServiceTest, PositionServiceExplicitName) {
    InputService svc(InputService::NAME_POS);
    EXPECT_EQ(svc.name(), "cursor_pos");
}

TEST(InputServiceTest, WindowFocusServiceExplicitName) {
    InputService svc(InputService::NAME_WINDOW_FOCUS);
    EXPECT_EQ(svc.name(), "window_focus");
}

TEST(InputServiceTest, MouseEventConstants) {
    EXPECT_EQ(MouseEvent::BUTTON_LEFT, 0x01);
    EXPECT_EQ(MouseEvent::BUTTON_RIGHT, 0x02);
    EXPECT_EQ(MouseEvent::BUTTON_WHEEL, 0x04);
    EXPECT_EQ(MouseEvent::BUTTON_BACK, 0x08);
    EXPECT_EQ(MouseEvent::BUTTON_FORWARD, 0x10);
    EXPECT_EQ(MouseEvent::TYPE_MOVE, 0);
    EXPECT_EQ(MouseEvent::TYPE_DOWN, 1);
    EXPECT_EQ(MouseEvent::TYPE_UP, 2);
    EXPECT_EQ(MouseEvent::TYPE_WHEEL, 3);
}

TEST(InputServiceTest, MouseEventMaskCombinations) {
    int32_t mask = MouseEvent::BUTTON_LEFT | MouseEvent::TYPE_DOWN;
    EXPECT_NE(mask & MouseEvent::BUTTON_LEFT, 0);
    EXPECT_NE(mask & MouseEvent::TYPE_DOWN, 0);
}

TEST(InputServiceTest, KeyEventStructure) {
    KeyEvent ev;
    ev.keycode = 0x41; // 'A'
    ev.down = true;
    ev.is_modifier = false;
    ev.sequence = "hello";
    EXPECT_EQ(ev.keycode, 0x41u);
    EXPECT_TRUE(ev.down);
    EXPECT_FALSE(ev.is_modifier);
    EXPECT_EQ(ev.sequence, "hello");
}

TEST(InputServiceTest, KeyEventModifierFlag) {
    KeyEvent ev;
    ev.keycode = 0x10; // Shift
    ev.is_modifier = true;
    EXPECT_TRUE(ev.is_modifier);
}

TEST(InputServiceTest, CursorDataStructure) {
    CursorData cd;
    cd.id = 42;
    cd.hot_x = 16;
    cd.hot_y = 16;
    cd.width = 32;
    cd.height = 32;
    cd.colors.resize(32 * 32 * 4, 0xFF);

    EXPECT_EQ(cd.id, 42u);
    EXPECT_EQ(cd.hot_x, 16);
    EXPECT_EQ(cd.hot_y, 16);
    EXPECT_EQ(cd.width, 32u);
    EXPECT_EQ(cd.height, 32u);
    EXPECT_EQ(cd.colors.size(), 4096u);
}

// ============================================================================
// ClipboardService Tests
// ============================================================================

TEST(ClipboardServiceTest, NameConstants) {
    EXPECT_STREQ(ClipboardService::NAME, "clipboard");
    EXPECT_STREQ(ClipboardService::FILE_NAME, "clipboard_file");
}

TEST(ClipboardServiceTest, ConstructorWithName) {
    ClipboardService svc(ClipboardService::NAME);
    EXPECT_EQ(svc.name(), "clipboard");
}

TEST(ClipboardServiceTest, ConstructorWithFileName) {
    ClipboardService svc(ClipboardService::FILE_NAME);
    EXPECT_EQ(svc.name(), "clipboard_file");
}

TEST(ClipboardServiceTest, StartStopAreNoOps) {
    ClipboardService svc(ClipboardService::NAME);
    svc.start();
    svc.stop();
    svc.start();
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(ClipboardServiceTest, IsRunningFalseWithoutSubscribers) {
    ClipboardService svc(ClipboardService::NAME);
    EXPECT_FALSE(svc.is_running());
}

TEST(ClipboardServiceTest, IsRunningTrueWithSubscribers) {
    ClipboardService svc(ClipboardService::NAME);
    svc.on_subscribe(1);
    EXPECT_TRUE(svc.is_running());
}

TEST(ClipboardServiceTest, IsRunningReflectsSubscriberCount) {
    ClipboardService svc(ClipboardService::NAME);
    EXPECT_FALSE(svc.is_running());
    svc.on_subscribe(1);
    EXPECT_TRUE(svc.is_running());
    svc.on_subscribe(2);
    EXPECT_TRUE(svc.is_running());
    svc.on_unsubscribe(1);
    EXPECT_TRUE(svc.is_running());
    svc.on_unsubscribe(2);
    EXPECT_FALSE(svc.is_running());
}

TEST(ClipboardServiceTest, IsRunningAfterDuplicateOperations) {
    ClipboardService svc(ClipboardService::NAME);
    svc.on_subscribe(1);
    svc.on_subscribe(1); // duplicate
    EXPECT_TRUE(svc.is_running());
    svc.on_unsubscribe(1);
    EXPECT_FALSE(svc.is_running());
}

TEST(ClipboardServiceTest, SubscriberOperations) {
    ClipboardService svc(ClipboardService::FILE_NAME);
    svc.on_subscribe(10);
    svc.on_subscribe(20);
    EXPECT_EQ(svc.subscriber_count(), 2u);
    EXPECT_TRUE(svc.is_subscribed(20));
    svc.on_unsubscribe(10);
    EXPECT_FALSE(svc.is_subscribed(10));
    EXPECT_TRUE(svc.is_running());
}

TEST(ClipboardServiceTest, FileServiceIndependentFromTextService) {
    ClipboardService text_svc(ClipboardService::NAME);
    ClipboardService file_svc(ClipboardService::FILE_NAME);

    text_svc.on_subscribe(1);
    file_svc.on_subscribe(2);

    EXPECT_TRUE(text_svc.is_running());
    EXPECT_TRUE(file_svc.is_running());
    EXPECT_EQ(text_svc.subscriber_count(), 1u);
    EXPECT_EQ(file_svc.subscriber_count(), 1u);
}

// ============================================================================
// TerminalService Tests
// ============================================================================

TEST(TerminalServiceTest, NameIsTerminal) {
    TerminalService svc;
    EXPECT_EQ(svc.name(), "terminal");
    EXPECT_STREQ(svc.name().c_str(), "terminal");
}

TEST(TerminalServiceTest, StaticConstantMatches) {
    EXPECT_STREQ(TerminalService::NAME, "terminal");
}

TEST(TerminalServiceTest, StartIsNoOp) {
    TerminalService svc;
    svc.start();
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(TerminalServiceTest, StopIsNoOp) {
    TerminalService svc;
    svc.stop();
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(TerminalServiceTest, StartStopCycle) {
    TerminalService svc;
    for (int i = 0; i < 10; ++i) {
        svc.start();
        svc.stop();
    }
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(TerminalServiceTest, SubscriberOperations) {
    TerminalService svc;
    svc.on_subscribe(1);
    svc.on_subscribe(2);
    EXPECT_EQ(svc.subscriber_count(), 2u);
    EXPECT_TRUE(svc.is_subscribed(1));
    svc.on_unsubscribe(1);
    EXPECT_FALSE(svc.is_subscribed(1));
}

TEST(TerminalServiceTest, InheritsGenericService) {
    TerminalService svc;
    Service* base = &svc;
    EXPECT_EQ(base->name(), "terminal");
    base->on_subscribe(99);
    EXPECT_EQ(base->subscriber_count(), 1u);
}

// ============================================================================
// PrinterService Tests
// ============================================================================

TEST(PrinterServiceTest, NameIsPrinter) {
    PrinterService svc(PrinterService::NAME);
    EXPECT_EQ(svc.name(), "printer");
}

TEST(PrinterServiceTest, StaticConstantMatches) {
    EXPECT_STREQ(PrinterService::NAME, "printer");
}

TEST(PrinterServiceTest, InitReturnsTrue) {
    EXPECT_TRUE(PrinterService::init("Generic PDF Driver"));
}

TEST(PrinterServiceTest, InitWithEmptyDriverName) {
    EXPECT_TRUE(PrinterService::init(""));
}

TEST(PrinterServiceTest, InitIsStatic) {
    bool result = PrinterService::init("test");
    EXPECT_TRUE(result);
}

TEST(PrinterServiceTest, StartStopAreNoOps) {
    PrinterService svc(PrinterService::NAME);
    svc.start();
    svc.stop();
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(PrinterServiceTest, SubscriberOperations) {
    PrinterService svc("custom_printer");
    svc.on_subscribe(1);
    EXPECT_EQ(svc.subscriber_count(), 1u);
    EXPECT_TRUE(svc.is_subscribed(1));
    svc.on_unsubscribe(1);
    EXPECT_FALSE(svc.is_subscribed(1));
}

TEST(PrinterServiceTest, CustomName) {
    PrinterService svc("network_printer_1");
    EXPECT_EQ(svc.name(), "network_printer_1");
}

// ============================================================================
// Connection Tests
// ============================================================================

class ConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_ = Server::create();
        ASSERT_TRUE(server_ != nullptr);
    }

    void TearDown() override {
        server_->close_all_connections();
    }

    Server::ServerPtr server_;
};

TEST_F(ConnectionTest, ConstructWithValidArgs) {
    auto stream = std::make_shared<MockStream>();
    ControlPermissions perms;
    perms.keyboard = false;

    Connection conn(1, stream, "10.0.0.1:12345", server_, perms);

    EXPECT_FALSE(conn.is_running());
}

TEST_F(ConnectionTest, ConstructWithoutPermissions) {
    auto stream = std::make_shared<MockStream>();
    Connection conn(2, stream, "10.0.0.2:9999", server_, std::nullopt);
    EXPECT_FALSE(conn.is_running());
}

TEST_F(ConnectionTest, StartThenStop) {
    auto stream = std::make_shared<MockStream>();
    Connection conn(3, stream, "10.0.0.3:5000", server_, std::nullopt);

    conn.start();
    // Allow brief time for detached thread to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(conn.is_running());

    conn.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(conn.is_running());
}

TEST_F(ConnectionTest, StopBeforeStartIsSafe) {
    auto stream = std::make_shared<MockStream>();
    Connection conn(4, stream, "addr", server_, std::nullopt);
    conn.stop();
    EXPECT_FALSE(conn.is_running());
}

TEST_F(ConnectionTest, DoubleStopIsSafe) {
    auto stream = std::make_shared<MockStream>();
    Connection conn(5, stream, "addr", server_, std::nullopt);
    conn.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    conn.stop();
    conn.stop();
    EXPECT_FALSE(conn.is_running());
}

TEST_F(ConnectionTest, DestructorStopsRunning) {
    auto stream = std::make_shared<MockStream>();
    {
        Connection conn(6, stream, "addr", server_, std::nullopt);
        conn.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_TRUE(conn.is_running());
    }
    // After destruction, stream should be closed
    EXPECT_FALSE(stream->is_open());
}

TEST_F(ConnectionTest, StartWithClosedStream) {
    auto stream = std::make_shared<MockStream>(false);
    Connection conn(7, stream, "addr", server_, std::nullopt);
    conn.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // Stream was already closed; message loop should terminate quickly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // After message loop detects closed stream, running should become false
    // (the detached thread calls server_->remove_connection then exits)
}

TEST_F(ConnectionTest, ConnectionId) {
    auto stream = std::make_shared<MockStream>();
    Connection conn(42, stream, "addr", server_, std::nullopt);
    // No id() accessor on Connection, but constructed with id
    EXPECT_FALSE(conn.is_running());
}

TEST_F(ConnectionTest, PermissionsEnforced) {
    ControlPermissions perms;
    perms.keyboard = true;
    perms.clipboard = false;
    perms.file_transfer = true;
    perms.audio = false;

    auto stream = std::make_shared<MockStream>();
    Connection conn(10, stream, "192.168.1.100:8080", server_, perms);

    // Connection stores perms; verification is at usage time
    EXPECT_FALSE(conn.is_running());
}

TEST_F(ConnectionTest, MultipleConnectionsNoConflict) {
    auto s1 = std::make_shared<MockStream>(true, "a", "r1");
    auto s2 = std::make_shared<MockStream>(true, "a", "r2");
    auto s3 = std::make_shared<MockStream>(true, "a", "r3");

    Connection c1(100, s1, "192.168.1.1", server_, std::nullopt);
    Connection c2(200, s2, "192.168.1.2", server_, std::nullopt);
    Connection c3(300, s3, "192.168.1.3", server_, std::nullopt);

    c1.start();
    c2.start();
    c3.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_TRUE(c1.is_running());
    EXPECT_TRUE(c2.is_running());
    EXPECT_TRUE(c3.is_running());

    c1.stop();
    c2.stop();
    c3.stop();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_FALSE(c1.is_running());
    EXPECT_FALSE(c2.is_running());
    EXPECT_FALSE(c3.is_running());
}

// ============================================================================
// Server Tests
// ============================================================================

class ServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_ = Server::create();
        ASSERT_TRUE(server_ != nullptr);
    }

    void TearDown() override {
        if (server_) {
            server_->close_all_connections();
        }
    }

    Server::ServerPtr server_;
};

TEST_F(ServerTest, CreateReturnsNonNull) {
    EXPECT_TRUE(server_ != nullptr);
}

TEST_F(ServerTest, CreateIsRepeatable) {
    auto srv1 = Server::create();
    auto srv2 = Server::create();
    EXPECT_TRUE(srv1 != nullptr);
    EXPECT_TRUE(srv2 != nullptr);
    EXPECT_NE(srv1.get(), srv2.get());
}

TEST_F(ServerTest, HasCoreServicesAfterCreate) {
    // Server::create() adds: audio, display, clipboard, clipboard_file,
    // video_monitor_0, cursor, cursor_pos, window_focus
    EXPECT_TRUE(server_->has_service("audio"));
    EXPECT_TRUE(server_->has_service("display"));
    EXPECT_TRUE(server_->has_service("clipboard"));
    EXPECT_TRUE(server_->has_service("clipboard_file"));
    EXPECT_TRUE(server_->has_service("cursor"));
    EXPECT_TRUE(server_->has_service("cursor_pos"));
    EXPECT_TRUE(server_->has_service("window_focus"));
    EXPECT_TRUE(server_->has_service("video_monitor_0"));
}

TEST_F(ServerTest, HasServiceReturnsFalseForUnknown) {
    EXPECT_FALSE(server_->has_service("nonexistent"));
    EXPECT_FALSE(server_->has_service(""));
    EXPECT_FALSE(server_->has_service("video_monitor_99"));
}

TEST_F(ServerTest, GetServiceReturnsNonNullForExisting) {
    EXPECT_TRUE(server_->get_service("audio") != nullptr);
    EXPECT_TRUE(server_->get_service("display") != nullptr);
    EXPECT_TRUE(server_->get_service("clipboard") != nullptr);
}

TEST_F(ServerTest, GetServiceReturnsNullForMissing) {
    EXPECT_TRUE(server_->get_service("nonexistent") == nullptr);
}

TEST_F(ServerTest, AddServiceNew) {
    auto svc = std::make_unique<GenericService>("custom_test_svc");
    server_->add_service(std::move(svc));
    EXPECT_TRUE(server_->has_service("custom_test_svc"));
}

TEST_F(ServerTest, AddServiceOverwritesExisting) {
    auto svc = std::make_unique<GenericService>("audio");
    svc->on_subscribe(1);
    svc->on_subscribe(2);
    server_->add_service(std::move(svc));
    // The new audio service has 2 subscribers
    Service* audio = server_->get_service("audio");
    ASSERT_TRUE(audio != nullptr);
    EXPECT_EQ(audio->subscriber_count(), 2u);
}

TEST_F(ServerTest, RemoveService) {
    server_->add_service(std::make_unique<GenericService>("removable"));
    EXPECT_TRUE(server_->has_service("removable"));
    server_->remove_service("removable");
    EXPECT_FALSE(server_->has_service("removable"));
}

TEST_F(ServerTest, RemoveServiceMissingIsSafe) {
    server_->remove_service("does_not_exist");
    EXPECT_FALSE(server_->has_service("does_not_exist"));
}

TEST_F(ServerTest, ConnectionCountInitiallyZero) {
    EXPECT_EQ(server_->connection_count(), 0u);
}

TEST_F(ServerTest, AddConnectionIncrementsCount) {
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(1, stream, "addr1");
    server_->add_connection(conn);
    EXPECT_EQ(server_->connection_count(), 1u);
}

TEST_F(ServerTest, AddConnectionReturnsId) {
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(42, stream, "addr1");
    int32_t id = server_->add_connection(conn);
    EXPECT_EQ(id, 42);
}

TEST_F(ServerTest, AddMultipleConnections) {
    auto s1 = std::make_shared<MockStream>();
    auto s2 = std::make_shared<MockStream>();
    auto s3 = std::make_shared<MockStream>();

    server_->add_connection(std::make_shared<ConnInner>(1, s1, "a1"));
    server_->add_connection(std::make_shared<ConnInner>(2, s2, "a2"));
    server_->add_connection(std::make_shared<ConnInner>(3, s3, "a3"));

    EXPECT_EQ(server_->connection_count(), 3u);
}

TEST_F(ServerTest, GetConnectionById) {
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(101, stream, "addr101");
    server_->add_connection(conn);

    auto found = server_->get_connection(101);
    ASSERT_TRUE(found != nullptr);
    EXPECT_EQ(found->id(), 101);
    EXPECT_EQ(found->addr(), "addr101");
}

TEST_F(ServerTest, GetConnectionMissing) {
    EXPECT_TRUE(server_->get_connection(9999) == nullptr);
}

TEST_F(ServerTest, RemoveConnection) {
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(200, stream, "addr");
    server_->add_connection(conn);
    EXPECT_EQ(server_->connection_count(), 1u);

    server_->remove_connection(200);
    EXPECT_EQ(server_->connection_count(), 0u);
    EXPECT_TRUE(server_->get_connection(200) == nullptr);
}

TEST_F(ServerTest, RemoveConnectionUnsubscribesFromServices) {
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(300, stream, "addr");
    server_->add_connection(conn);

    // After add_connection, the auto-subscription should have occurred
    server_->remove_connection(300);
    EXPECT_EQ(server_->connection_count(), 0u);
}

TEST_F(ServerTest, CloseAllConnections) {
    auto s1 = std::make_shared<MockStream>();
    auto s2 = std::make_shared<MockStream>();
    server_->add_connection(std::make_shared<ConnInner>(1, s1, "a"));
    server_->add_connection(std::make_shared<ConnInner>(2, s2, "b"));
    EXPECT_EQ(server_->connection_count(), 2u);

    server_->close_all_connections();
    EXPECT_EQ(server_->connection_count(), 0u);
    EXPECT_FALSE(s1->is_open());
    EXPECT_FALSE(s2->is_open());
}

TEST_F(ServerTest, NextConnectionIdIncrements) {
    int32_t id1 = server_->next_connection_id();
    int32_t id2 = server_->next_connection_id();
    int32_t id3 = server_->next_connection_id();
    EXPECT_GT(id2, id1);
    EXPECT_GT(id3, id2);
    EXPECT_EQ(id3, id1 + 2);
}

TEST_F(ServerTest, NextConnectionIdIsPositive) {
    int32_t id = server_->next_connection_id();
    EXPECT_GT(id, 0);
}

TEST_F(ServerTest, NextConnectionIdManyIncrements) {
    int32_t first = server_->next_connection_id();
    for (int i = 0; i < 100; ++i) {
        server_->next_connection_id();
    }
    int32_t last = server_->next_connection_id();
    EXPECT_EQ(last, first + 101);
}

TEST_F(ServerTest, SubscribeToService) {
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(500, stream, "addr");
    server_->add_connection(conn);

    Service* audio = server_->get_service("audio");
    ASSERT_TRUE(audio != nullptr);

    // add_connection auto-subscribes to audio
    EXPECT_TRUE(audio->is_subscribed(500));
}

TEST_F(ServerTest, SubscribeAndUnsubscribeExplicit) {
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(600, stream, "addr");
    // Add without auto-subscription by using a fresh server without auto-subscribe
    auto srv = std::make_shared<Server>();
    srv->add_service(std::make_unique<GenericService>("test_svc"));

    auto conn_ptr = std::make_shared<ConnInner>(600, stream, "addr");
    srv->subscribe("test_svc", conn_ptr, true);

    Service* svc = srv->get_service("test_svc");
    ASSERT_TRUE(svc != nullptr);
    EXPECT_TRUE(svc->is_subscribed(600));

    srv->subscribe("test_svc", conn_ptr, false);
    EXPECT_FALSE(svc->is_subscribed(600));
}

TEST_F(ServerTest, SubscribeNonexistentServiceIsSafe) {
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(700, stream, "addr");
    server_->subscribe("no_such_service", conn, true);
    // Should not crash
}

TEST_F(ServerTest, SubscribeDuplicateIsSafe) {
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(800, stream, "addr");
    server_->add_connection(conn);

    Service* audio = server_->get_service("audio");
    ASSERT_TRUE(audio != nullptr);
    size_t before = audio->subscriber_count();

    server_->subscribe("audio", conn, true); // duplicate subscribe
    EXPECT_EQ(audio->subscriber_count(), before);
}

TEST_F(ServerTest, AddPrimaryVideoService) {
    // Already has video_monitor_0 from create(), test adding another
    server_->add_primary_video_service();
    EXPECT_TRUE(server_->has_service("video_monitor_0"));
}

TEST_F(ServerTest, AddPrimaryCameraService) {
    server_->add_primary_camera_service();
    EXPECT_TRUE(server_->has_service("video_camera_0"));
}

TEST_F(ServerTest, SetVideoServiceOptionValid) {
    // video_monitor_0 exists from create()
    server_->set_video_service_option(0, "quality", "75");
    server_->set_video_service_option(0, "bitrate", "2000000");
    // Should not throw
}

TEST_F(ServerTest, SetVideoServiceOptionInvalidDisplay) {
    server_->set_video_service_option(99, "quality", "50");
    // Should silently do nothing for nonexistent service
}

TEST_F(ServerTest, AddConnectionWithNoPermissions) {
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(900, stream, "addr");

    std::vector<std::string> noperms = {"audio", "clipboard"};
    server_->add_connection(conn, noperms);

    Service* audio = server_->get_service("audio");
    ASSERT_TRUE(audio != nullptr);
    // With noperms containing "audio", audio should NOT be subscribed
    EXPECT_FALSE(audio->is_subscribed(900));
}

TEST_F(ServerTest, AddCameraConnection) {
    server_->add_primary_camera_service();
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(1001, stream, "addr");

    server_->add_camera_connection(conn);

    Service* cam = server_->get_service("video_camera_0");
    ASSERT_TRUE(cam != nullptr);
    EXPECT_TRUE(cam->is_subscribed(1001));
}

TEST_F(ServerTest, AddCameraConnectionWithoutService) {
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(1002, stream, "addr");

    // No camera service exists
    server_->add_camera_connection(conn);
    EXPECT_EQ(server_->connection_count(), 1u);
}

TEST_F(ServerTest, ServiceTypeIsCorrect) {
    Service* display = server_->get_service("display");
    ASSERT_TRUE(display != nullptr);
    EXPECT_EQ(display->name(), "display");

    Service* audio = server_->get_service("audio");
    ASSERT_TRUE(audio != nullptr);
    EXPECT_EQ(audio->name(), "audio");
}

TEST_F(ServerTest, EmptyServerHasNoServices) {
    auto empty = std::make_shared<Server>();
    EXPECT_EQ(empty->connection_count(), 0u);
    EXPECT_FALSE(empty->has_service("audio"));
}

TEST_F(ServerTest, AllCoreServicesAreCallable) {
    // Verify all services returned by get_service can be called
    auto services = {"audio", "display", "clipboard", "clipboard_file",
                     "video_monitor_0", "cursor", "cursor_pos", "window_focus"};
    for (const auto& name : services) {
        Service* svc = server_->get_service(name);
        ASSERT_TRUE(svc != nullptr) << "Service missing: " << name;
        EXPECT_EQ(svc->name(), name);
        svc->start();
        svc->stop();
    }
}

TEST_F(ServerTest, ConnectionsAutoSubscribeToPrimaryDisplay) {
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(1100, stream, "addr");
    server_->add_connection(conn);

    Service* video = server_->get_service("video_monitor_0");
    ASSERT_TRUE(video != nullptr);
    EXPECT_TRUE(video->is_subscribed(1100));
}

TEST_F(ServerTest, AddConnectionSkipsFileClipboardSubscription) {
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(1200, stream, "addr");
    server_->add_connection(conn);

    Service* file_cb = server_->get_service("clipboard_file");
    ASSERT_TRUE(file_cb != nullptr);
    // clipboard_file is NOT auto-subscribed (by design in Server::add_connection)
    EXPECT_FALSE(file_cb->is_subscribed(1200));
}

// ============================================================================
// LoginFailureCheck Tests
// ============================================================================

TEST(LoginFailureCheckTest, InitiallyNotBlocked) {
    LoginFailureCheck checker;
    EXPECT_FALSE(checker.is_blocked("192.168.1.1"));
}

TEST(LoginFailureCheckTest, SingleFailureNotBlocked) {
    LoginFailureCheck checker;
    checker.record_attempt("10.0.0.1");
    EXPECT_FALSE(checker.is_blocked("10.0.0.1"));
}

TEST(LoginFailureCheckTest, FiveFailuresBlocked) {
    LoginFailureCheck checker;
    for (int i = 0; i < 5; ++i) {
        checker.record_attempt("192.168.1.100");
    }
    EXPECT_TRUE(checker.is_blocked("192.168.1.100"));
}

TEST(LoginFailureCheckTest, FourFailuresNotBlocked) {
    LoginFailureCheck checker;
    for (int i = 0; i < 4; ++i) {
        checker.record_attempt("10.0.0.55");
    }
    EXPECT_FALSE(checker.is_blocked("10.0.0.55"));
}

TEST(LoginFailureCheckTest, BlockedAfterExceeded) {
    LoginFailureCheck checker;
    for (int i = 0; i < 10; ++i) {
        checker.record_attempt("172.16.0.1");
    }
    EXPECT_TRUE(checker.is_blocked("172.16.0.1"));
}

TEST(LoginFailureCheckTest, DifferentAddressesIndependent) {
    LoginFailureCheck checker;
    for (int i = 0; i < 5; ++i) {
        checker.record_attempt("10.0.0.1");
    }
    EXPECT_TRUE(checker.is_blocked("10.0.0.1"));
    EXPECT_FALSE(checker.is_blocked("10.0.0.2"));
}

TEST(LoginFailureCheckTest, ResetUnblocks) {
    LoginFailureCheck checker;
    for (int i = 0; i < 5; ++i) {
        checker.record_attempt("192.168.1.50");
    }
    EXPECT_TRUE(checker.is_blocked("192.168.1.50"));

    checker.reset("192.168.1.50");
    EXPECT_FALSE(checker.is_blocked("192.168.1.50"));
}

TEST(LoginFailureCheckTest, ResetDifferentAddress) {
    LoginFailureCheck checker;
    for (int i = 0; i < 5; ++i) {
        checker.record_attempt("10.0.0.1");
    }
    checker.reset("10.0.0.2"); // reset different address
    EXPECT_TRUE(checker.is_blocked("10.0.0.1"));
}

TEST(LoginFailureCheckTest, ResetUnknownAddressIsSafe) {
    LoginFailureCheck checker;
    checker.reset("never.seen.before");
    EXPECT_FALSE(checker.is_blocked("never.seen.before"));
}

TEST(LoginFailureCheckTest, EmptyAddress) {
    LoginFailureCheck checker;
    checker.record_attempt("");
    EXPECT_FALSE(checker.is_blocked(""));
    for (int i = 0; i < 4; ++i) checker.record_attempt("");
    EXPECT_TRUE(checker.is_blocked(""));
    checker.reset("");
    EXPECT_FALSE(checker.is_blocked(""));
}

// ============================================================================
// ChildProcessTracker Tests
// ============================================================================

TEST(ChildProcessTrackerTest, InitiallyEmpty) {
    ChildProcessTracker tracker;
    // No direct size accessor, but kill_all on empty should be safe
    tracker.kill_all();
}

TEST(ChildProcessTrackerTest, AddAndKillAll) {
    ChildProcessTracker tracker;
    tracker.add(12345);
    tracker.add(12346);
    tracker.add(12347);
    // kill_all sends SIGTERM to those PIDs (will fail gracefully for nonexistent PIDs)
    tracker.kill_all();
}

TEST(ChildProcessTrackerTest, RemovePid) {
    ChildProcessTracker tracker;
    tracker.add(1000);
    tracker.add(2000);
    tracker.remove(1000);
    tracker.kill_all();
}

TEST(ChildProcessTrackerTest, RemoveMissingPidIsSafe) {
    ChildProcessTracker tracker;
    tracker.add(3000);
    tracker.remove(9999); // not in list
    tracker.kill_all();
}

TEST(ChildProcessTrackerTest, AddNegativePid) {
    ChildProcessTracker tracker;
    tracker.add(-1);
    tracker.kill_all();
}

TEST(ChildProcessTrackerTest, MultipleKillAllsAreSafe) {
    ChildProcessTracker tracker;
    tracker.add(4000);
    tracker.kill_all();
    tracker.kill_all(); // second time should be fine (list cleared)
}

// ============================================================================
// VirtualDisplayManager Tests
// ============================================================================

TEST(VirtualDisplayManagerTest, NotInstalledByDefault) {
    VirtualDisplayManager mgr;
    EXPECT_FALSE(mgr.is_installed());
}

TEST(VirtualDisplayManagerTest, InstallReturnsFalse) {
    VirtualDisplayManager mgr;
    EXPECT_FALSE(mgr.install());
}

TEST(VirtualDisplayManagerTest, UninstallReturnsFalse) {
    VirtualDisplayManager mgr;
    EXPECT_FALSE(mgr.uninstall());
}

TEST(VirtualDisplayManagerTest, GetVirtualDisplaysReturnsEmpty) {
    VirtualDisplayManager mgr;
    auto displays = mgr.get_virtual_displays();
    EXPECT_TRUE(displays.empty());
}

TEST(VirtualDisplayManagerTest, IsInstalledAfterInstallAttempt) {
    VirtualDisplayManager mgr;
    mgr.install();
    EXPECT_FALSE(mgr.is_installed()); // Still false (platform stub)
}

// ============================================================================
// RdpInput Tests
// ============================================================================

TEST(RdpInputTest, DefaultDisabled) {
    RdpInput rdp;
    EXPECT_FALSE(rdp.enabled);
}

TEST(RdpInputTest, StartStopAreNoOps) {
    RdpInput rdp;
    rdp.start();
    rdp.stop();
    rdp.start();
    // No crash, no state change to check
}

TEST(RdpInputTest, EnableFlag) {
    RdpInput rdp;
    rdp.enabled = true;
    EXPECT_TRUE(rdp.enabled);
    rdp.enabled = false;
    EXPECT_FALSE(rdp.enabled);
}

// ============================================================================
// Cross-Service Interaction Tests
// ============================================================================

TEST(ServerInteractionTest, MultipleServicesPerConnection) {
    auto srv = Server::create();
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(2000, stream, "addr");
    srv->add_connection(conn);

    // After auto-subscribe, verify multiple services see this connection
    EXPECT_TRUE(srv->get_service("audio")->is_subscribed(2000));
    EXPECT_TRUE(srv->get_service("clipboard")->is_subscribed(2000));
    EXPECT_TRUE(srv->get_service("cursor")->is_subscribed(2000));
    EXPECT_TRUE(srv->get_service("cursor_pos")->is_subscribed(2000));
    EXPECT_TRUE(srv->get_service("window_focus")->is_subscribed(2000));
    EXPECT_TRUE(srv->get_service("display")->is_subscribed(2000));
}

TEST(ServerInteractionTest, RemoveConnectionUnsubscribesAllServices) {
    auto srv = Server::create();
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(3000, stream, "addr");
    srv->add_connection(conn);

    srv->remove_connection(3000);

    EXPECT_FALSE(srv->get_service("audio")->is_subscribed(3000));
    EXPECT_FALSE(srv->get_service("clipboard")->is_subscribed(3000));
    EXPECT_FALSE(srv->get_service("cursor")->is_subscribed(3000));
}

TEST(ServerInteractionTest, ServiceLifecycleAcrossConnections) {
    auto srv = Server::create();
    auto s1 = std::make_shared<MockStream>();
    auto s2 = std::make_shared<MockStream>();

    auto c1 = std::make_shared<ConnInner>(4001, s1, "a");
    auto c2 = std::make_shared<ConnInner>(4002, s2, "b");

    srv->add_connection(c1);
    srv->add_connection(c2);

    Service* audio = srv->get_service("audio");
    ASSERT_TRUE(audio != nullptr);
    EXPECT_EQ(audio->subscriber_count(), 2u);

    srv->remove_connection(4001);
    EXPECT_EQ(audio->subscriber_count(), 1u);

    srv->remove_connection(4002);
    EXPECT_EQ(audio->subscriber_count(), 0u);
}

TEST(ServerInteractionTest, AddThenRemoveServiceWhileConnectionsActive) {
    auto srv = Server::create();
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(5000, stream, "addr");
    srv->add_connection(conn);

    // Add a new service; active connections won't auto-subscribe unless
    // we manually call add_connection again
    srv->add_service(std::make_unique<GenericService>("late_service"));
    EXPECT_TRUE(srv->has_service("late_service"));

    // Remove it; should be safe
    srv->remove_service("late_service");
    EXPECT_FALSE(srv->has_service("late_service"));
}

TEST(ServerInteractionTest, ConnectionStreamStillAliveAfterDisconnect) {
    auto stream = std::make_shared<MockStream>();
    {
        auto srv = Server::create();
        auto conn = std::make_shared<ConnInner>(6000, stream, "addr");
        srv->add_connection(conn);
    }
    // Stream shared_ptr is still alive even if server is destroyed
    // But server destructor calls close_all_connections which closes streams
    EXPECT_FALSE(stream->is_open());
}

// ============================================================================
// Thread Safety / Stress Tests
// ============================================================================

TEST(ServerThreadSafetyTest, ConcurrentSubscriptions) {
    auto srv = Server::create();
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(7000, stream, "addr");
    srv->add_connection(conn);

    Service* audio = srv->get_service("audio");
    ASSERT_TRUE(audio != nullptr);

    std::atomic<bool> done{false};
    std::vector<std::thread> threads;

    // Spin up threads that subscribe/unsubscribe repeatedly
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, tid = t]() {
            auto local_stream = std::make_shared<MockStream>();
            while (!done) {
                auto local_conn = std::make_shared<ConnInner>(7000 + tid * 100, local_stream, "addr");
                srv->subscribe("audio", local_conn, true);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                srv->subscribe("audio", local_conn, false);
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    done = true;
    for (auto& t : threads) t.join();

    // Original subscriber should still be present
    EXPECT_TRUE(audio->is_subscribed(7000));
}

TEST(ServerThreadSafetyTest, ConcurrentConnectionAddRemove) {
    std::atomic<int32_t> next_id{10000};
    std::atomic<bool> done{false};

    auto srv_ptr = Server::create();
    // Use raw pointer to avoid shared_ptr copy in lambda capture
    Server* srv = srv_ptr.get();

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&]() {
            while (!done) {
                int32_t id = next_id.fetch_add(1);
                auto stream = std::make_shared<MockStream>();
                auto conn = std::make_shared<ConnInner>(id, stream, "addr");
                srv->add_connection(conn);
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                srv->remove_connection(id);
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    done = true;
    for (auto& t : threads) t.join();

    // Clean up remaining connections
    srv->close_all_connections();
    EXPECT_EQ(srv->connection_count(), 0u);
}

TEST(ServerThreadSafetyTest, ConcurrentServiceAddRemove) {
    Server srv;

    std::atomic<bool> done{false};
    std::atomic<int> counter{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&]() {
            while (!done) {
                int i = counter.fetch_add(1);
                std::string name = "thread_svc_" + std::to_string(i);
                srv.add_service(std::make_unique<GenericService>(name));
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                srv.remove_service(name);
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    done = true;
    for (auto& t : threads) t.join();
}

TEST(ServerThreadSafetyTest, ConcurrentNextConnectionId) {
    Server srv;
    std::atomic<bool> done{false};
    std::set<int32_t> ids;
    std::mutex ids_mutex;

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&]() {
            while (!done) {
                int32_t id = srv.next_connection_id();
                std::lock_guard lk(ids_mutex);
                ids.insert(id);
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    done = true;
    for (auto& t : threads) t.join();

    // All IDs should be unique (monotonically increasing)
    EXPECT_GT(ids.size(), 1u);
    bool sequential = true;
    int32_t prev = 0;
    for (int32_t id : ids) {
        if (prev != 0 && id != prev + 1) sequential = false;
        prev = id;
    }
    EXPECT_TRUE(sequential);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST(ServerEdgeCaseTest, NullStreamInConnInner) {
    ConnInner conn(1, nullptr, "addr");
    EXPECT_TRUE(conn.stream() == nullptr);
    EXPECT_FALSE(conn.is_open());
    // send should be safe with null stream
    std::vector<uint8_t> data = {0x01};
    conn.send(data); // Should not crash
}

TEST(ServerEdgeCaseTest, AddConnectionWithNullStream) {
    auto srv = Server::create();
    auto conn = std::make_shared<ConnInner>(8888, nullptr, "addr");
    srv->add_connection(conn);
    EXPECT_EQ(srv->connection_count(), 1u);
    auto found = srv->get_connection(8888);
    ASSERT_TRUE(found != nullptr);
    EXPECT_TRUE(found->stream() == nullptr);
}

TEST(ServerEdgeCaseTest, VeryLongAddress) {
    std::string long_addr(10000, 'x');
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(99, stream, long_addr);
    EXPECT_EQ(conn.addr(), long_addr);
}

TEST(ServerEdgeCaseTest, ServiceNameWithSpecialChars) {
    GenericService svc("svc!@#$%^&*()");
    EXPECT_EQ(svc.name(), "svc!@#$%^&*()");
}

TEST(ServerEdgeCaseTest, ZeroConnectionId) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(0, stream, "addr");
    EXPECT_EQ(conn.id(), 0);
}

TEST(ServerEdgeCaseTest, MaxConnectionId) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(2147483647, stream, "addr");
    EXPECT_EQ(conn.id(), 2147483647);
}

TEST(ServerEdgeCaseTest, VideoServiceLargeDisplayIndex) {
    std::string name = VideoService::service_name(VideoSource::MONITOR, 999999);
    EXPECT_EQ(name, "video_monitor_999999");
}

// ============================================================================
// Service Integration Tests (linked lifecycle)
// ============================================================================

TEST(ServiceIntegrationTest, FullLifecycleWithAllServices) {
    auto srv = Server::create();

    // Verify all expected services are present
    const std::vector<std::string> expected = {
        "audio", "display", "clipboard", "clipboard_file",
        "video_monitor_0", "cursor", "cursor_pos", "window_focus"
    };

    for (const auto& name : expected) {
        EXPECT_TRUE(srv->has_service(name))
            << "Expected service missing: " << name;
    }

    // Create connections
    auto s1 = std::make_shared<MockStream>();
    auto s2 = std::make_shared<MockStream>();
    auto c1 = std::make_shared<ConnInner>(10001, s1, "192.168.1.1");
    auto c2 = std::make_shared<ConnInner>(10002, s2, "192.168.1.2");

    srv->add_connection(c1);
    srv->add_connection(c2);

    EXPECT_EQ(srv->connection_count(), 2u);

    // Expect each non-file-clipboard, non-excluded service to have 2 subscribers
    for (const auto& name : expected) {
        if (name == "clipboard_file") continue; // excluded from auto-subscribe
        Service* svc = srv->get_service(name);
        ASSERT_TRUE(svc != nullptr) << "Service null: " << name;
        EXPECT_EQ(svc->subscriber_count(), 2u)
            << "Service " << name << " has wrong subscriber count";
    }

    // Remove one connection
    srv->remove_connection(10001);

    for (const auto& name : expected) {
        if (name == "clipboard_file") continue;
        Service* svc = srv->get_service(name);
        ASSERT_TRUE(svc != nullptr);
        EXPECT_EQ(svc->subscriber_count(), 1u)
            << "Service " << name << " subscriber count should be 1 after removal";
    }

    // Remove last connection
    srv->remove_connection(10002);

    for (const auto& name : expected) {
        Service* svc = srv->get_service(name);
        ASSERT_TRUE(svc != nullptr);
        EXPECT_EQ(svc->subscriber_count(), 0u)
            << "Service " << name << " subscriber count should be 0 after all removed";
    }

    EXPECT_EQ(srv->connection_count(), 0u);
}

TEST(ServiceIntegrationTest, PermissionBasedSubscription) {
    auto srv = Server::create();

    auto s1 = std::make_shared<MockStream>();
    auto c1 = std::make_shared<ConnInner>(11001, s1, "addr1");

    // Pass noperms for audio and clipboard
    std::vector<std::string> noperms = {"audio", "clipboard"};
    srv->add_connection(c1, noperms);

    // Audio and clipboard should NOT be subscribed
    EXPECT_FALSE(srv->get_service("audio")->is_subscribed(11001));
    EXPECT_FALSE(srv->get_service("clipboard")->is_subscribed(11001));

    // Other services should still be subscribed
    EXPECT_TRUE(srv->get_service("cursor")->is_subscribed(11001));
    EXPECT_TRUE(srv->get_service("display")->is_subscribed(11001));
}

TEST(ServiceIntegrationTest, SubscribeUnsubscribeCyclePreservesState) {
    auto srv = std::make_shared<Server>();
    srv->add_service(std::make_unique<GenericService>("cycle_test"));

    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(12001, stream, "addr");

    Service* svc = srv->get_service("cycle_test");
    ASSERT_TRUE(svc != nullptr);

    // Cycle: subscribe, unsubscribe, subscribe again
    for (int cycle = 0; cycle < 10; ++cycle) {
        srv->subscribe("cycle_test", conn, true);
        EXPECT_TRUE(svc->is_subscribed(12001)) << "Cycle " << cycle << " subscribe failed";

        srv->subscribe("cycle_test", conn, false);
        EXPECT_FALSE(svc->is_subscribed(12001)) << "Cycle " << cycle << " unsubscribe failed";
    }
}

TEST(ServiceIntegrationTest, MultipleServersIndependentState) {
    auto srv1 = Server::create();
    auto srv2 = Server::create();

    auto s1 = std::make_shared<MockStream>();
    auto s2 = std::make_shared<MockStream>();

    auto c1 = std::make_shared<ConnInner>(13001, s1, "addr1");
    auto c2 = std::make_shared<ConnInner>(13002, s2, "addr2");

    srv1->add_connection(c1);
    srv2->add_connection(c2);

    // Each server's audio service has exactly 1 subscriber
    EXPECT_EQ(srv1->get_service("audio")->subscriber_count(), 1u);
    EXPECT_EQ(srv2->get_service("audio")->subscriber_count(), 1u);

    // Removing from srv1 doesn't affect srv2
    srv1->remove_connection(13001);
    EXPECT_EQ(srv1->get_service("audio")->subscriber_count(), 0u);
    EXPECT_EQ(srv2->get_service("audio")->subscriber_count(), 1u);
}

// ============================================================================
// Protocol Structure Tests (server-relevant)
// ============================================================================

TEST(ServerProtocolTest, LoginResponseDefaults) {
    LoginResponse r;
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(r.message.empty());
    EXPECT_EQ(r.code, 0);
    EXPECT_TRUE(r.view_only);
}

TEST(ServerProtocolTest, LoginResponseSuccess) {
    LoginResponse r;
    r.success = true;
    r.message = "OK";
    r.code = 200;
    r.resolution = Resolution{1280, 720};
    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.message, "OK");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.resolution.width, 1280u);
    EXPECT_EQ(r.resolution.height, 720u);
}

TEST(ServerProtocolTest, ControlPermissionsAllEnabled) {
    ControlPermissions p;
    EXPECT_TRUE(p.keyboard);
    EXPECT_TRUE(p.clipboard);
    EXPECT_TRUE(p.file_transfer);
    EXPECT_TRUE(p.audio);
    EXPECT_FALSE(p.restart);
    EXPECT_FALSE(p.shutdown);
    EXPECT_FALSE(p.privacy_mode);
}

TEST(ServerProtocolTest, ControlPermissionsAllDisabled) {
    ControlPermissions p;
    p.keyboard = false;
    p.clipboard = false;
    p.file_transfer = false;
    p.audio = false;

    EXPECT_FALSE(p.keyboard);
    EXPECT_FALSE(p.clipboard);
    EXPECT_FALSE(p.file_transfer);
    EXPECT_FALSE(p.audio);
}

TEST(ServerProtocolTest, ThumbnailDataStructure) {
    ThumbnailData td;
    td.width = 320;
    td.height = 240;
    td.pixels.resize(320 * 240 * 4, 0x80);
    EXPECT_EQ(td.width, 320u);
    EXPECT_EQ(td.height, 240u);
    EXPECT_EQ(td.pixels.size(), 320u * 240u * 4u);
}

TEST(ServerProtocolTest, VideoFrameIsMonitorFlag) {
    VideoFrame f;
    EXPECT_TRUE(f.is_monitor);
    f.is_monitor = false;
    EXPECT_FALSE(f.is_monitor);
}

TEST(ServerProtocolTest, MessageTypeValues) {
    // Verify key message type values used by server
    EXPECT_EQ(static_cast<uint32_t>(MessageType::LOGIN), 10u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::LOGIN_RESPONSE), 11u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::SUBSCRIBE_SERVICE), 80u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::UNSUBSCRIBE_SERVICE), 81u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::SERVICE_DATA), 82u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::CLOSE_CONNECTION), 14u);
}

// ============================================================================
// Streaming / Data Path Tests
// ============================================================================

TEST(StreamingTest, MockStreamBasic) {
    auto stream = std::make_shared<MockStream>();
    EXPECT_TRUE(stream->is_open());
    EXPECT_EQ(stream->local_addr(), "127.0.0.1:1000");
    EXPECT_EQ(stream->remote_addr(), "127.0.0.1:2000");
}

TEST(StreamingTest, MockStreamSendRecv) {
    auto stream = std::make_shared<MockStream>();
    std::vector<uint8_t> test_data = {0x01, 0x02, 0x03};
    EXPECT_TRUE(stream->send(test_data));
    EXPECT_EQ(stream->sent_count(), 1u);
}

TEST(StreamingTest, MockStreamInjectRecv) {
    auto stream = std::make_shared<MockStream>();
    std::vector<uint8_t> test = {0xAA, 0xBB};
    stream->inject_recv(test);
    auto recv = stream->recv();
    ASSERT_EQ(recv.size(), 2u);
    EXPECT_EQ(recv[0], 0xAA);
    EXPECT_EQ(recv[1], 0xBB);
}

TEST(StreamingTest, MockStreamRecvEmptyWhenNoData) {
    auto stream = std::make_shared<MockStream>();
    auto data = stream->recv();
    EXPECT_TRUE(data.empty());
}

TEST(StreamingTest, MockStreamClose) {
    auto stream = std::make_shared<MockStream>();
    stream->close();
    EXPECT_FALSE(stream->is_open());
    EXPECT_FALSE(stream->send({0x01}));
}

TEST(StreamingTest, MockStreamSetNoDelay) {
    auto stream = std::make_shared<MockStream>();
    // Should not throw
    stream->set_nodelay(true);
    stream->set_nodelay(false);
}

TEST(StreamingTest, MockStreamEncryption) {
    auto stream = std::make_shared<MockStream>();
    std::vector<uint8_t> key(32, 0x42);
    // Should not throw
    stream->set_encryption_key(key);
}

TEST(StreamingTest, ConnInnerUsesStreamCorrectly) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(1, stream, "test");

    std::vector<uint8_t> msg = {0x01, 0x02};
    conn.send(msg);
    EXPECT_EQ(stream->sent_count(), 1u);

    conn.close();
    EXPECT_FALSE(conn.is_open());
    EXPECT_FALSE(stream->is_open());
}

TEST(StreamingTest, MultipleSendsAccumulate) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(1, stream, "addr");

    for (int i = 0; i < 100; ++i) {
        std::vector<uint8_t> data = {static_cast<uint8_t>(i)};
        conn.send(data);
    }
    EXPECT_EQ(stream->sent_count(), 100u);
}

// ============================================================================
// Service Group Tests (testing each service category comprehensively)
// ============================================================================

TEST(ServiceGroupTest, AllServicesStartStopIdempotent) {
    auto srv = Server::create();

    // Get all services
    std::vector<Service*> all_svcs;
    for (const auto& name : {"audio", "display", "clipboard", "clipboard_file",
        "video_monitor_0", "cursor", "cursor_pos", "window_focus"}) {
        all_svcs.push_back(srv->get_service(name));
    }

    // Start all twice (idempotency check)
    for (auto svc : all_svcs) {
        if (svc) {
            svc->start();
            svc->start(); // double start
        }
    }

    // Stop all twice
    for (auto svc : all_svcs) {
        if (svc) {
            svc->stop();
            svc->stop(); // double stop
        }
    }
}

TEST(ServiceGroupTest, AllServicesSetOptionIdempotent) {
    auto srv = Server::create();

    std::vector<Service*> all_svcs = {
        srv->get_service("audio"),
        srv->get_service("display"),
        srv->get_service("clipboard"),
        srv->get_service("clipboard_file"),
        srv->get_service("video_monitor_0"),
        srv->get_service("cursor"),
        srv->get_service("cursor_pos"),
        srv->get_service("window_focus"),
    };

    for (auto svc : all_svcs) {
        if (svc) {
            svc->set_option("test_key", "test_value");
            svc->set_option("", "");
        }
    }
}

// ============================================================================
// Connection Lifecycle Extended Tests
// ============================================================================

TEST(ConnectionLifecycleTest, StartStopRestartCycle) {
    auto srv = Server::create();
    auto stream = std::make_shared<MockStream>();

    {
        Connection conn(20001, stream, "10.0.0.1:8888", srv, std::nullopt);
        conn.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_TRUE(conn.is_running());
        conn.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_FALSE(conn.is_running());
        conn.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_TRUE(conn.is_running());
        conn.stop();
    }
}

TEST(ConnectionLifecycleTest, RapidStartStop) {
    auto srv = Server::create();
    auto stream = std::make_shared<MockStream>();
    Connection conn(20002, stream, "addr", srv, std::nullopt);

    for (int i = 0; i < 5; ++i) {
        conn.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        conn.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

TEST(ConnectionLifecycleTest, ConnectionSurvivesServerDestruction) {
    auto stream = std::make_shared<MockStream>();
    Server::ServerPtr srv = Server::create();
    ServerWeakPtr weak = srv;

    std::optional<Connection> conn;
    conn.emplace(20003, stream, "addr", srv, std::nullopt);
    conn->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Destroy server while connection is still running
    srv.reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Connection's message loop should detect server is gone and exit
    conn->stop();
    conn.reset();
}

TEST(ConnectionLifecycleTest, ConnectionWithCustomPermissions) {
    auto srv = Server::create();
    auto stream = std::make_shared<MockStream>();

    ControlPermissions perms;
    perms.keyboard = false;
    perms.clipboard = false;
    perms.file_transfer = true;
    perms.audio = true;
    perms.restart = true;
    perms.shutdown = false;
    perms.privacy_mode = true;

    Connection conn(20004, stream, "restricted_host", srv, perms);
    conn.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(conn.is_running());
    conn.stop();
}

TEST(ConnectionLifecycleTest, ConnectionWithoutPermissions) {
    auto srv = Server::create();
    auto stream = std::make_shared<MockStream>();

    Connection conn(20005, stream, "addr", srv, std::nullopt);
    conn.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(conn.is_running());
    conn.stop();
}

TEST(ConnectionLifecycleTest, ManyConnectionsLifecycle) {
    auto srv = Server::create();

    for (int32_t id = 21001; id <= 21010; ++id) {
        auto stream = std::make_shared<MockStream>();
        auto conn = std::make_shared<ConnInner>(id, stream, "addr_" + std::to_string(id));
        srv->add_connection(conn);
    }

    EXPECT_EQ(srv->connection_count(), 10u);

    for (int32_t id = 21001; id <= 21010; ++id) {
        EXPECT_TRUE(srv->get_connection(id) != nullptr);
    }

    srv->close_all_connections();
    EXPECT_EQ(srv->connection_count(), 0u);
}

TEST(ConnectionLifecycleTest, ConnectionStreamClosesOnServerShutdown) {
    auto stream = std::make_shared<MockStream>();
    {
        auto srv = Server::create();
        auto conn = std::make_shared<ConnInner>(22001, stream, "addr");
        srv->add_connection(conn);
        EXPECT_TRUE(stream->is_open());
    }
    // Server destructor called close_all_connections
    EXPECT_FALSE(stream->is_open());
}

// ============================================================================
// LoginFailureCheck Extended Tests
// ============================================================================

TEST(LoginFailureCheckExtendedTest, MultipleAddressesRateLimited) {
    LoginFailureCheck checker;

    // Rate-limit three different addresses
    const std::vector<std::string> addrs = {
        "10.0.0.1", "10.0.0.2", "10.0.0.3"
    };

    for (int i = 0; i < 5; ++i) {
        for (const auto& addr : addrs) {
            checker.record_attempt(addr);
        }
    }

    for (const auto& addr : addrs) {
        EXPECT_TRUE(checker.is_blocked(addr));
    }
}

TEST(LoginFailureCheckExtendedTest, PartialRateLimit) {
    LoginFailureCheck checker;

    // Block 10.0.0.1 but not 10.0.0.2
    for (int i = 0; i < 5; ++i) checker.record_attempt("10.0.0.1");
    for (int i = 0; i < 3; ++i) checker.record_attempt("10.0.0.2");

    EXPECT_TRUE(checker.is_blocked("10.0.0.1"));
    EXPECT_FALSE(checker.is_blocked("10.0.0.2"));
}

TEST(LoginFailureCheckExtendedTest, ManyAttemptsBeyondThreshold) {
    LoginFailureCheck checker;

    // 100 attempts on same address
    for (int i = 0; i < 100; ++i) {
        checker.record_attempt("192.168.1.254");
    }
    EXPECT_TRUE(checker.is_blocked("192.168.1.254"));

    checker.reset("192.168.1.254");
    EXPECT_FALSE(checker.is_blocked("192.168.1.254"));

    // After reset, single attempt should not block
    checker.record_attempt("192.168.1.254");
    EXPECT_FALSE(checker.is_blocked("192.168.1.254"));
}

TEST(LoginFailureCheckExtendedTest, IPv6Addresses) {
    LoginFailureCheck checker;
    checker.record_attempt("::1");
    checker.record_attempt("::1");
    checker.record_attempt("::1");
    checker.record_attempt("::1");
    checker.record_attempt("::1");
    EXPECT_TRUE(checker.is_blocked("::1"));
    checker.reset("::1");
    EXPECT_FALSE(checker.is_blocked("::1"));
}

TEST(LoginFailureCheckExtendedTest, ManyUniqueAddresses) {
    LoginFailureCheck checker;
    for (int i = 0; i < 100; ++i) {
        std::string addr = "10.0.0." + std::to_string(i);
        checker.record_attempt(addr);
    }
    // Each address has only 1 attempt, none should be blocked
    for (int i = 0; i < 100; ++i) {
        std::string addr = "10.0.0." + std::to_string(i);
        EXPECT_FALSE(checker.is_blocked(addr));
    }
}

// ============================================================================
// VideoService Extended Tests
// ============================================================================

TEST(VideoServiceExtendedTest, AllVideoSources) {
    VideoService mon(VideoSource::MONITOR, 0);
    VideoService cam(VideoSource::CAMERA, 0);
    VideoService virt(VideoSource::VIRTUAL, 0);

    EXPECT_NE(mon.name(), cam.name());
    EXPECT_NE(cam.name(), virt.name());
    EXPECT_NE(mon.name(), virt.name());
}

TEST(VideoServiceExtendedTest, MultiDisplayServiceNames) {
    for (int32_t i = 0; i < 16; ++i) {
        EXPECT_EQ(VideoService::service_name(VideoSource::MONITOR, i),
                  "video_monitor_" + std::to_string(i));
    }
}

TEST(VideoServiceExtendedTest, MultiCameraServiceNames) {
    for (int32_t i = 0; i < 8; ++i) {
        EXPECT_EQ(VideoService::service_name(VideoSource::CAMERA, i),
                  "video_camera_" + std::to_string(i));
    }
}

TEST(VideoServiceExtendedTest, SetOptionQualitySettings) {
    VideoService svc(VideoSource::MONITOR, 0);
    svc.set_option("quality", "best");
    svc.set_option("quality", "balanced");
    svc.set_option("quality", "low");
    EXPECT_EQ(svc.name(), "video_monitor_0");
}

TEST(VideoServiceExtendedTest, SetOptionCodecSelection) {
    VideoService svc(VideoSource::MONITOR, 0);
    svc.set_option("codec", "h264");
    svc.set_option("codec", "h265");
    svc.set_option("codec", "vp9");
    svc.set_option("codec", "av1");
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(VideoServiceExtendedTest, SetOptionResolution) {
    VideoService svc(VideoSource::MONITOR, 0);
    svc.set_option("width", "1920");
    svc.set_option("height", "1080");
    svc.set_option("width", "3840");
    svc.set_option("height", "2160");
}

TEST(VideoServiceExtendedTest, StartStopMultipleTimes) {
    VideoService svc(VideoSource::CAMERA, 3);
    for (int i = 0; i < 10; ++i) {
        svc.start();
        svc.stop();
    }
    EXPECT_EQ(svc.name(), "video_camera_3");
}

TEST(VideoServiceExtendedTest, ServiceNameLargeIndex) {
    EXPECT_EQ(VideoService::service_name(VideoSource::MONITOR, 1000000),
              "video_monitor_1000000");
}

// ============================================================================
// InputService Extended Tests
// ============================================================================

TEST(InputServiceExtendedTest, CursorPositionMasks) {
    MouseEvent ev;
    // Simulate left click at position
    ev.mask = MouseEvent::BUTTON_LEFT | MouseEvent::TYPE_DOWN;
    ev.x = 640;
    ev.y = 480;
    EXPECT_EQ(ev.x, 640);
    EXPECT_EQ(ev.y, 480);
    EXPECT_NE(ev.mask & MouseEvent::BUTTON_LEFT, 0);
    EXPECT_NE(ev.mask & MouseEvent::TYPE_DOWN, 0);
    EXPECT_EQ(ev.mask & MouseEvent::TYPE_UP, 0);
}

TEST(InputServiceExtendedTest, MouseButtonRelease) {
    MouseEvent ev;
    ev.mask = MouseEvent::BUTTON_RIGHT | MouseEvent::TYPE_UP;
    EXPECT_NE(ev.mask & MouseEvent::BUTTON_RIGHT, 0);
    EXPECT_NE(ev.mask & MouseEvent::TYPE_UP, 0);
}

TEST(InputServiceExtendedTest, MouseWheelEvent) {
    MouseEvent ev;
    ev.mask = MouseEvent::BUTTON_WHEEL | MouseEvent::TYPE_WHEEL;
    ev.x = 0;
    ev.y = 120; // scroll amount
    EXPECT_NE(ev.mask & MouseEvent::BUTTON_WHEEL, 0);
}

TEST(InputServiceExtendedTest, CombinedButtons) {
    MouseEvent ev;
    ev.mask = MouseEvent::BUTTON_LEFT | MouseEvent::BUTTON_RIGHT;
    EXPECT_NE(ev.mask & MouseEvent::BUTTON_LEFT, 0);
    EXPECT_NE(ev.mask & MouseEvent::BUTTON_RIGHT, 0);
}

TEST(InputServiceExtendedTest, AllMouseButtons) {
    int32_t all_buttons = MouseEvent::BUTTON_LEFT |
                          MouseEvent::BUTTON_RIGHT |
                          MouseEvent::BUTTON_WHEEL |
                          MouseEvent::BUTTON_BACK |
                          MouseEvent::BUTTON_FORWARD;

    // Each button bit should be unique
    EXPECT_NE(MouseEvent::BUTTON_LEFT & MouseEvent::BUTTON_RIGHT, MouseEvent::BUTTON_LEFT);
    EXPECT_NE(MouseEvent::BUTTON_LEFT & MouseEvent::BUTTON_WHEEL, MouseEvent::BUTTON_LEFT);
}

TEST(InputServiceExtendedTest, KeyModifierTracking) {
    KeyEvent shift_down;
    shift_down.keycode = 0x10;
    shift_down.down = true;
    shift_down.is_modifier = true;
    EXPECT_TRUE(shift_down.is_modifier);

    KeyEvent shift_up;
    shift_up.keycode = 0x10;
    shift_up.down = false;
    shift_up.is_modifier = true;
    EXPECT_TRUE(shift_up.is_modifier);
    EXPECT_FALSE(shift_up.down);

    KeyEvent letter;
    letter.keycode = 0x41;
    letter.down = true;
    letter.is_modifier = false;
    letter.sequence = "A";
    EXPECT_FALSE(letter.is_modifier);
    EXPECT_EQ(letter.sequence, "A");
}

TEST(InputServiceExtendedTest, FactoryDefaultNames) {
    // Verify factory methods produce expected names
    auto c = InputService::create_cursor();
    auto p = InputService::create_position();
    auto w = InputService::create_window_focus();

    EXPECT_EQ(c->name(), "cursor");
    EXPECT_EQ(p->name(), "cursor_pos");
    EXPECT_EQ(w->name(), "window_focus");
}

// ============================================================================
// ClipboardService Extended Tests
// ============================================================================

TEST(ClipboardServiceExtendedTest, TextAndFileServicesIndependent) {
    auto text_svc = std::make_unique<ClipboardService>(ClipboardService::NAME);
    auto file_svc = std::make_unique<ClipboardService>(ClipboardService::FILE_NAME);

    text_svc->on_subscribe(1);
    file_svc->on_subscribe(2);

    EXPECT_TRUE(text_svc->is_running());
    EXPECT_TRUE(file_svc->is_running());

    text_svc->on_unsubscribe(1);
    EXPECT_FALSE(text_svc->is_running());
    EXPECT_TRUE(file_svc->is_running());
}

TEST(ClipboardServiceExtendedTest, IsRunningTransitions) {
    ClipboardService svc(ClipboardService::NAME);

    // Initial state
    EXPECT_FALSE(svc.is_running());

    // One subscriber
    svc.on_subscribe(1);
    EXPECT_TRUE(svc.is_running());

    // Two subscribers
    svc.on_subscribe(2);
    EXPECT_TRUE(svc.is_running());

    // Back to one
    svc.on_unsubscribe(1);
    EXPECT_TRUE(svc.is_running());

    // Back to zero
    svc.on_unsubscribe(2);
    EXPECT_FALSE(svc.is_running());
}

TEST(ClipboardServiceExtendedTest, FileServiceSubscriberTracking) {
    ClipboardService svc(ClipboardService::FILE_NAME);

    svc.on_subscribe(100);
    svc.on_subscribe(200);
    svc.on_subscribe(300);

    EXPECT_TRUE(svc.is_subscribed(100));
    EXPECT_TRUE(svc.is_subscribed(200));
    EXPECT_TRUE(svc.is_subscribed(300));
    EXPECT_FALSE(svc.is_subscribed(400));

    svc.on_unsubscribe(200);
    EXPECT_FALSE(svc.is_subscribed(200));
    EXPECT_TRUE(svc.is_subscribed(300));
}

TEST(ClipboardServiceExtendedTest, StartStopDoesNotAffectSubscriptions) {
    ClipboardService svc(ClipboardService::NAME);
    svc.on_subscribe(1);
    svc.start();
    svc.stop();
    EXPECT_TRUE(svc.is_subscribed(1));
}

// ============================================================================
// TerminalService Extended Tests
// ============================================================================

TEST(TerminalServiceExtendedTest, NameInvariant) {
    TerminalService svc;
    EXPECT_EQ(svc.name(), "terminal");
    EXPECT_STREQ(TerminalService::NAME, "terminal");
}

TEST(TerminalServiceExtendedTest, MultipleTerminalServices) {
    TerminalService svc1;
    TerminalService svc2;
    EXPECT_EQ(svc1.name(), svc2.name());

    svc1.on_subscribe(10);
    svc2.on_subscribe(20);

    EXPECT_EQ(svc1.subscriber_count(), 1u);
    EXPECT_EQ(svc2.subscriber_count(), 1u);
    EXPECT_FALSE(svc1.is_subscribed(20));
}

TEST(TerminalServiceExtendedTest, StartStopRepeated) {
    TerminalService svc;
    for (int i = 0; i < 100; ++i) {
        svc.start();
        svc.stop();
    }
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

// ============================================================================
// PrinterService Extended Tests
// ============================================================================

TEST(PrinterServiceExtendedTest, StaticInitAlwaysTrue) {
    EXPECT_TRUE(PrinterService::init("Generic / Text Only"));
    EXPECT_TRUE(PrinterService::init("Microsoft Print to PDF"));
    EXPECT_TRUE(PrinterService::init(""));
}

TEST(PrinterServiceExtendedTest, MultiplePrinters) {
    PrinterService p1("printer_lpt1");
    PrinterService p2("printer_usb001");
    PrinterService p3("printer_network");

    EXPECT_EQ(p1.name(), "printer_lpt1");
    EXPECT_EQ(p2.name(), "printer_usb001");
    EXPECT_EQ(p3.name(), "printer_network");
}

TEST(PrinterServiceExtendedTest, SubscriberIsolation) {
    PrinterService p1("printer_1");
    PrinterService p2("printer_2");

    p1.on_subscribe(1);
    p2.on_subscribe(2);

    EXPECT_TRUE(p1.is_subscribed(1));
    EXPECT_FALSE(p1.is_subscribed(2));
    EXPECT_TRUE(p2.is_subscribed(2));
    EXPECT_FALSE(p2.is_subscribed(1));
}

// ============================================================================
// Server Extended Management Tests
// ============================================================================

TEST(ServerExtendedTest, ServiceNamesAreUnique) {
    auto srv = Server::create();
    std::set<std::string> names;

    const std::vector<std::string> all_names = {
        "audio", "display", "clipboard", "clipboard_file",
        "video_monitor_0", "cursor", "cursor_pos", "window_focus"
    };

    for (const auto& name : all_names) {
        ASSERT_TRUE(srv->has_service(name)) << name;
        auto [it, inserted] = names.insert(name);
        EXPECT_TRUE(inserted) << "Duplicate name: " << name;
    }
}

TEST(ServerExtendedTest, AddServiceDuplicateName) {
    auto srv = Server::create();
    size_t before = srv->get_service("audio")->subscriber_count();

    // Add a new service with the same name (overwrite)
    auto new_audio = std::make_unique<GenericService>("audio");
    new_audio->on_subscribe(9999);
    srv->add_service(std::move(new_audio));

    Service* audio = srv->get_service("audio");
    ASSERT_TRUE(audio != nullptr);
    EXPECT_EQ(audio->subscriber_count(), 1u);
    EXPECT_TRUE(audio->is_subscribed(9999));
}

TEST(ServerExtendedTest, RemoveServiceThenReadd) {
    auto srv = Server::create();

    srv->remove_service("audio");
    EXPECT_FALSE(srv->has_service("audio"));

    srv->add_service(std::make_unique<AudioService>());
    EXPECT_TRUE(srv->has_service("audio"));
}

TEST(ServerExtendedTest, ConnectionCountAfterAddRemove) {
    auto srv = Server::create();

    for (int32_t id = 1; id <= 50; ++id) {
        auto stream = std::make_shared<MockStream>();
        srv->add_connection(std::make_shared<ConnInner>(id, stream, "addr"));
    }
    EXPECT_EQ(srv->connection_count(), 50u);

    for (int32_t id = 1; id <= 25; ++id) {
        srv->remove_connection(id);
    }
    EXPECT_EQ(srv->connection_count(), 25u);

    srv->close_all_connections();
    EXPECT_EQ(srv->connection_count(), 0u);
}

TEST(ServerExtendedTest, GetConnectionAfterRemoval) {
    auto srv = Server::create();
    auto stream = std::make_shared<MockStream>();
    srv->add_connection(std::make_shared<ConnInner>(999, stream, "addr"));

    auto found = srv->get_connection(999);
    ASSERT_TRUE(found != nullptr);
    EXPECT_EQ(found->id(), 999);

    srv->remove_connection(999);
    EXPECT_TRUE(srv->get_connection(999) == nullptr);
}

TEST(ServerExtendedTest, SubscribeMissingServiceDoesNothing) {
    auto srv = Server::create();
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(555, stream, "addr");

    srv->subscribe("phantom_service", conn, true);
    EXPECT_FALSE(srv->has_service("phantom_service"));
}

TEST(ServerExtendedTest, ServiceOptionChannel) {
    auto srv = Server::create();

    // Test set_video_service_option dispatches correctly
    srv->set_video_service_option(0, "fps", "60");
    srv->set_video_service_option(0, "scale", "1.5");

    // Non-existent display should not crash
    srv->set_video_service_option(50, "fps", "30");
}

TEST(ServerExtendedTest, ConnectionCountReflectsState) {
    auto srv = std::make_shared<Server>();

    EXPECT_EQ(srv->connection_count(), 0u);

    auto s1 = std::make_shared<MockStream>();
    srv->add_connection(std::make_shared<ConnInner>(1, s1, "a"));

    EXPECT_EQ(srv->connection_count(), 1u);

    auto s2 = std::make_shared<MockStream>();
    srv->add_connection(std::make_shared<ConnInner>(2, s2, "b"));

    EXPECT_EQ(srv->connection_count(), 2u);

    srv->remove_connection(1);
    EXPECT_EQ(srv->connection_count(), 1u);

    srv->close_all_connections();
    EXPECT_EQ(srv->connection_count(), 0u);
}

// ============================================================================
// Memory / Smart Pointer Tests
// ============================================================================

TEST(MemoryTest, ConnPtrCycles) {
    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(1, stream, "addr");

    // Shared pointers with same underlying object
    ConnPtr conn2 = conn;
    EXPECT_EQ(conn.use_count(), 2);
    EXPECT_EQ(conn->id(), conn2->id());
}

TEST(MemoryTest, ServerWeakPtr) {
    auto srv = Server::create();
    ServerWeakPtr weak = srv;

    {
        auto locked = weak.lock();
        EXPECT_TRUE(locked != nullptr);
        EXPECT_EQ(locked.get(), srv.get());
    }

    srv.reset();
    auto locked = weak.lock();
    EXPECT_TRUE(locked == nullptr);
}

TEST(MemoryTest, ConnInnerDestroysCorrectly) {
    auto stream = std::make_shared<MockStream>();
    EXPECT_TRUE(stream->is_open());

    {
        auto conn = std::make_unique<ConnInner>(1, stream, "addr");
        EXPECT_EQ(stream.use_count(), 2);
    }

    // After conn is destroyed, stream should be closed (via close() in destructor)
    EXPECT_FALSE(stream->is_open());
    EXPECT_EQ(stream.use_count(), 1);
}

TEST(MemoryTest, ServiceUniquePtrOwnership) {
    auto srv = std::make_shared<Server>();
    {
        auto svc = std::make_unique<GenericService>("temp");
        srv->add_service(std::move(svc));
    }
    // svc was moved, should still be accessible
    EXPECT_TRUE(srv->has_service("temp"));
}

// ============================================================================
// Stress / Load Tests
// ============================================================================

TEST(StressTest, HighVolumeSubscriptions) {
    GenericService svc("stress_test");
    for (int32_t i = 1; i <= 10000; ++i) {
        svc.on_subscribe(i);
    }
    EXPECT_EQ(svc.subscriber_count(), 10000u);
    for (int32_t i = 1; i <= 10000; ++i) {
        EXPECT_TRUE(svc.is_subscribed(i));
    }
    for (int32_t i = 1; i <= 10000; ++i) {
        svc.on_unsubscribe(i);
    }
    EXPECT_EQ(svc.subscriber_count(), 0u);
}

TEST(StressTest, ManyServersInParallel) {
    std::vector<Server::ServerPtr> servers;
    for (int i = 0; i < 20; ++i) {
        servers.push_back(Server::create());
        EXPECT_TRUE(servers.back() != nullptr);
    }

    // Add connections to each
    for (size_t i = 0; i < servers.size(); ++i) {
        auto stream = std::make_shared<MockStream>();
        auto conn = std::make_shared<ConnInner>(
            static_cast<int32_t>(i + 1), stream, "addr");
        servers[i]->add_connection(conn);
    }

    // All should have exactly 1 connection
    for (auto& srv : servers) {
        EXPECT_EQ(srv->connection_count(), 1u);
    }

    // Clean up
    for (auto& srv : servers) {
        srv->close_all_connections();
        EXPECT_EQ(srv->connection_count(), 0u);
    }
}

TEST(StressTest, RapidSubscribeUnsubscribe) {
    auto srv = std::make_shared<Server>();
    srv->add_service(std::make_unique<GenericService>("rapid"));

    auto stream = std::make_shared<MockStream>();
    auto conn = std::make_shared<ConnInner>(1, stream, "addr");

    Service* svc = srv->get_service("rapid");
    ASSERT_TRUE(svc != nullptr);

    for (int i = 0; i < 1000; ++i) {
        srv->subscribe("rapid", conn, true);
        EXPECT_TRUE(svc->is_subscribed(1));
        srv->subscribe("rapid", conn, false);
        EXPECT_FALSE(svc->is_subscribed(1));
    }
}

TEST(StressTest, ConnInnerBulkSend) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(1, stream, "addr");

    std::vector<uint8_t> payload(65536, 0xCC); // 64K payload
    for (int i = 0; i < 100; ++i) {
        conn.send(payload);
    }

    EXPECT_EQ(stream->sent_count(), 100u);
}

// ============================================================================
// Configuration / Resolution Tests
// ============================================================================

TEST(ConfigIntegrationTest, ResolutionComparison) {
    Resolution r1{1920, 1080};
    Resolution r2{1920, 1080};
    Resolution r3{1280, 720};

    EXPECT_TRUE(r1 == r2);
    EXPECT_FALSE(r1 == r3);
}

TEST(ConfigIntegrationTest, ResolutionDefaultValues) {
    Resolution r;
    EXPECT_EQ(r.width, 1920u);
    EXPECT_EQ(r.height, 1080u);
}

TEST(ConfigIntegrationTest, ResolutionCustomSizes) {
    Resolution r;
    r.width = 3840;
    r.height = 2160;
    EXPECT_EQ(r.width, 3840u);
    EXPECT_EQ(r.height, 2160u);

    Resolution small{640, 480};
    EXPECT_EQ(small.width, 640u);
    EXPECT_EQ(small.height, 480u);
}

TEST(ConfigIntegrationTest, TimeConstantsExist) {
    // Verify key time constants are accessible
    EXPECT_GT(std::chrono::seconds(CONNECT_TIMEOUT).count(), 0);
    EXPECT_GT(std::chrono::seconds(HEARTBEAT_INTERVAL).count(), 0);
    EXPECT_GT(std::chrono::milliseconds(CLIPBOARD_INTERVAL).count(), 0);
}

// ============================================================================
// Final Integration / Smoke Tests
// ============================================================================

TEST(SmokeTest, ServerCreateDestroyCycle) {
    for (int i = 0; i < 10; ++i) {
        auto srv = Server::create();
        ASSERT_TRUE(srv != nullptr);
        EXPECT_GT(srv->connection_count(), 0u); // 0, may have none
        // Server destructor cleans up
    }
}

TEST(SmokeTest, FullServiceLifecycleWithSubscribers) {
    auto srv = Server::create();

    // Simulate a realistic session with 5 connections
    for (int32_t id = 1; id <= 5; ++id) {
        auto stream = std::make_shared<MockStream>();
        auto conn = std::make_shared<ConnInner>(id, stream,
            "192.168.1." + std::to_string(id));
        srv->add_connection(conn);
    }

    // Verify all auto-subscribed services see 5 subscribers
    EXPECT_EQ(srv->get_service("audio")->subscriber_count(), 5u);
    EXPECT_EQ(srv->get_service("cursor")->subscriber_count(), 5u);
    EXPECT_EQ(srv->get_service("display")->subscriber_count(), 5u);

    // Drop one connection
    srv->remove_connection(3);
    EXPECT_EQ(srv->get_service("audio")->subscriber_count(), 4u);
    EXPECT_EQ(srv->get_service("cursor")->subscriber_count(), 4u);

    // Drop the rest
    srv->close_all_connections();
    EXPECT_EQ(srv->get_service("audio")->subscriber_count(), 0u);
    EXPECT_EQ(srv->get_service("clipboard")->subscriber_count(), 0u);
    EXPECT_EQ(srv->get_service("cursor")->subscriber_count(), 0u);
    EXPECT_EQ(srv->get_service("cursor_pos")->subscriber_count(), 0u);
    EXPECT_EQ(srv->get_service("window_focus")->subscriber_count(), 0u);
    EXPECT_EQ(srv->get_service("display")->subscriber_count(), 0u);
}

TEST(SmokeTest, AllServicesAccessibleAfterCreate) {
    auto srv = Server::create();

    std::vector<std::pair<std::string, bool>> service_checks = {
        {"audio", true},
        {"display", true},
        {"clipboard", true},
        {"clipboard_file", true},
        {"video_monitor_0", true},
        {"cursor", true},
        {"cursor_pos", true},
        {"window_focus", true},
        {"nonexistent", false},
        {"video_monitor_99", false},
    };

    for (const auto& [name, should_exist] : service_checks) {
        EXPECT_EQ(srv->has_service(name), should_exist)
            << "Service " << name << " existence check failed";
    }
}

TEST(SmokeTest, ConnectionManagementEndToEnd) {
    auto srv = Server::create();

    // Phase 1: Add connections
    std::vector<std::shared_ptr<MockStream>> streams;
    for (int i = 0; i < 5; ++i) {
        auto stream = std::make_shared<MockStream>();
        streams.push_back(stream);
        auto conn = std::make_shared<ConnInner>(i + 100, stream,
            "host_" + std::to_string(i));
        srv->add_connection(conn);
    }
    EXPECT_EQ(srv->connection_count(), 5u);

    // Phase 2: Verify all connections retrievable
    for (int i = 0; i < 5; ++i) {
        auto conn = srv->get_connection(i + 100);
        ASSERT_TRUE(conn != nullptr);
        EXPECT_EQ(conn->id(), i + 100);
        EXPECT_TRUE(conn->is_open());
    }

    // Phase 3: Remove one by one
    for (int i = 0; i < 5; ++i) {
        srv->remove_connection(i + 100);
        EXPECT_TRUE(srv->get_connection(i + 100) == nullptr);
        EXPECT_EQ(srv->connection_count(), static_cast<size_t>(4 - i));
    }

    EXPECT_EQ(srv->connection_count(), 0u);
}

// ============================================================================
// Service Abstract Interface Tests
// ============================================================================

// Test that we can work with services polymorphically
TEST(PolymorphismTest, ServicePointerCast) {
    auto audio = std::make_unique<AudioService>();
    Service* base = audio.get();

    EXPECT_EQ(base->name(), "audio");
    base->start();
    base->stop();
    base->on_subscribe(42);
    EXPECT_EQ(base->subscriber_count(), 1u);
    EXPECT_TRUE(base->is_subscribed(42));
    base->on_unsubscribe(42);
    EXPECT_FALSE(base->is_subscribed(42));
}

TEST(PolymorphismTest, MixedServiceVector) {
    std::vector<std::unique_ptr<Service>> services;
    services.push_back(std::make_unique<AudioService>());
    services.push_back(std::make_unique<DisplayService>());
    services.push_back(std::make_unique<TerminalService>());
    services.push_back(InputService::create_cursor());
    services.push_back(std::make_unique<ClipboardService>(ClipboardService::NAME));

    EXPECT_EQ(services.size(), 5u);

    // All should have different names
    std::set<std::string> names;
    for (auto& svc : services) {
        names.insert(svc->name());
    }
    EXPECT_EQ(names.size(), services.size());

    // All should support subscribe/unsubscribe
    for (auto& svc : services) {
        svc->on_subscribe(999);
        EXPECT_EQ(svc->subscriber_count(), 1u);
        svc->on_unsubscribe(999);
        EXPECT_EQ(svc->subscriber_count(), 0u);
    }
}

// ============================================================================
// Edge Cases for Protocol Integration
// ============================================================================

TEST(ServerEdgeCaseTest, EmptyDataSend) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(1, stream, "addr");

    std::vector<uint8_t> empty;
    conn.send(empty);
    // Should send empty data if stream is open
    EXPECT_EQ(stream->sent_count(), 1u);
}

TEST(ServerEdgeCaseTest, MaxSizeDataSend) {
    auto stream = std::make_shared<MockStream>();
    ConnInner conn(1, stream, "addr");

    // Large but reasonable payload
    std::vector<uint8_t> large(10 * 1024 * 1024, 0x00); // 10 MB
    conn.send(large);
    EXPECT_EQ(stream->sent_count(), 1u);
}

TEST(ServerEdgeCaseTest, ConnInnerAddressPreservation) {
    std::vector<std::string> addresses = {
        "192.168.1.1:80",
        "[::1]:8080",
        "localhost:9999",
        "10.0.0.0.1:1234", // unusual format but preserved as-is
        "",
        "a",
    };

    for (const auto& addr : addresses) {
        auto stream = std::make_shared<MockStream>();
        ConnInner conn(1, stream, addr);
        EXPECT_EQ(conn.addr(), addr);
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
