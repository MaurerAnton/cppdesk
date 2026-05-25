// flutter_bridge.cpp - C++ FFI bridge for Flutter/Dart
#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <functional>
#include <mutex>
#include <spdlog/spdlog.h>

namespace cppdesk::ffi {

// ====== FFI Types (Dart-compatible) ======
extern "C" {
    typedef void (*DartCallback)(const char* event, const char* data);
}

static DartCallback g_dart_callback = nullptr;
static std::mutex g_callback_mutex;

extern "C" {

// ====== Initialization ======
void cppdesk_init(DartCallback callback) {
    std::lock_guard lk(g_callback_mutex);
    g_dart_callback = callback;
    spdlog::info("cppdesk FFI initialized");
}

void cppdesk_shutdown() {
    std::lock_guard lk(g_callback_mutex);
    g_dart_callback = nullptr;
    spdlog::info("cppdesk FFI shutdown");
}

// ====== Device Info ======
const char* cppdesk_get_device_id() {
    static std::string id;
    id = common::Config::get_id();
    return id.c_str();
}

const char* cppdesk_get_version() {
    return "1.3.0-cpp";
}

const char* cppdesk_get_platform() {
#ifdef _WIN32
    return "Windows";
#elif defined(__APPLE__)
#if TARGET_OS_IOS
    return "iOS";
#else
    return "macOS";
#endif
#elif defined(__ANDROID__)
    return "Android";
#else
    return "Linux";
#endif
}

// ====== Connection ======
int32_t cppdesk_connect(const char* peer_id, const char* password, int view_only) {
    spdlog::info("FFI connect: peer={} view_only={}", peer_id, view_only);
    // TODO: actual connection
    return 0; // conn_id
}

void cppdesk_disconnect(int32_t conn_id) {
    spdlog::info("FFI disconnect: conn_id={}", conn_id);
}

int cppdesk_is_connected(int32_t conn_id) {
    return 0;
}

// ====== Video ======
void cppdesk_start_video(int32_t conn_id, int display_idx) {
    spdlog::info("FFI start video: conn={} display={}", conn_id, display_idx);
}

void cppdesk_stop_video(int32_t conn_id) {
    spdlog::info("FFI stop video: conn={}", conn_id);
}

// Called from C++ to Dart with video frame data
void cppdesk_send_video_frame_to_dart(int32_t conn_id, const uint8_t* data,
    int32_t len, int32_t w, int32_t h, int32_t codec) {
    if (!g_dart_callback) return;
    // Serialize frame info as JSON and call Dart callback
    std::string json = "{\"type\":\"video_frame\",\"conn_id\":" +
        std::to_string(conn_id) + ",\"w\":" + std::to_string(w) +
        ",\"h\":" + std::to_string(h) + ",\"codec\":" +
        std::to_string(codec) + ",\"size\":" + std::to_string(len) + "}";
    g_dart_callback(json.c_str(), reinterpret_cast<const char*>(data));
}

// ====== Audio ======
void cppdesk_start_audio(int32_t conn_id) {
    spdlog::info("FFI start audio: conn={}", conn_id);
}

void cppdesk_stop_audio(int32_t conn_id) {
    spdlog::info("FFI stop audio: conn={}", conn_id);
}

// ====== Input ======
void cppdesk_send_mouse(int32_t conn_id, int32_t mask, int32_t x, int32_t y) {
    // Forward mouse event
}

void cppdesk_send_key(int32_t conn_id, int32_t keycode, int down) {
    // Forward key event
}

void cppdesk_send_text(int32_t conn_id, const char* text) {
    // Forward text input
}

// ====== Clipboard ======
void cppdesk_send_clipboard_text(int32_t conn_id, const char* text) {
    // Forward clipboard text
}

const char* cppdesk_get_clipboard_text() {
    static std::string text;
    text = platform::get_clipboard_text();
    return text.c_str();
}

// ====== File Transfer ======
void cppdesk_send_file(int32_t conn_id, const char* local_path, const char* remote_path) {
    spdlog::info("FFI send file: {} -> {}", local_path, remote_path);
}

void cppdesk_cancel_file_transfer(int32_t conn_id) {
    // Cancel
}

// ====== Settings ======
void cppdesk_set_option(const char* key, const char* value) {
    common::Config::set_option(key, value);
}

const char* cppdesk_get_option(const char* key) {
    static std::string val;
    val = common::Config::get_option(key);
    return val.c_str();
}

// ====== Service ======
void cppdesk_start_service() {
    platform::start_os_service();
}

void cppdesk_stop_service() {
    platform::stop_os_service();
}

int cppdesk_is_service_running() {
    return platform::is_service_running() ? 1 : 0;
}

// ====== UI Callbacks (from Dart to C++) ======
void cppdesk_on_settings_changed(const char* settings_json) {
    spdlog::debug("Settings changed: {}", settings_json);
    // Parse JSON and apply settings
}

void cppdesk_on_permission_changed(int32_t conn_id, int keyboard, int clipboard,
    int file_transfer, int audio) {
    spdlog::debug("Permissions changed for conn {}: k={} c={} f={} a={}",
        conn_id, keyboard, clipboard, file_transfer, audio);
}

void cppdesk_on_display_switched(int32_t conn_id, int display_idx) {
    spdlog::info("Display switched: conn={} display={}", conn_id, display_idx);
}

// ====== Chat ======
void cppdesk_send_chat(int32_t conn_id, const char* message) {
    spdlog::info("Chat: conn={} msg={}", conn_id, message);
}

// ====== Native Memory ======
uint8_t* cppdesk_alloc(int32_t size) {
    return new uint8_t[size];
}

void cppdesk_free(uint8_t* ptr) {
    delete[] ptr;
}

} // extern "C"

} // namespace cppdesk::ffi
