#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
namespace scrap {
struct ImageRgb { uint32_t w, h; std::vector<uint8_t> data; };
struct ImageTexture { uint32_t id, w, h; };
enum class CodecFormat { RAW, H264, H265, VP8, VP9, AV1 };
enum class ImageFormat { RGB, RGBA, BGRA, YUV420 };
class Decoder { public: bool decode(const uint8_t* d, size_t len, ImageRgb& out) { return false; } };
class Recorder { public: struct Context { int w, h, fps, bitrate; }; bool start(const Context&) { return true; } void stop() {} };
bool is_x11() { return false; }
namespace camera { bool primary_camera_exists() { return false; } static constexpr int PRIMARY_CAMERA_IDX = 0; }
}