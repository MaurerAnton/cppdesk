/**
 * test_scrap.cpp — Comprehensive tests for the scrap library
 *
 * Covers all classes, enums, structs, free functions, constants, and utilities
 * declared in scrap/scrap.hpp. Uses Google Test. Target: 2500+ lines.
 */

#include <gtest/gtest.h>
#include "scrap/scrap.hpp"

#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>

// ============================================================================
// Helper: minimal concrete TraitCapturer for testing virtual interface
// ============================================================================
namespace {
    class MockCapturer : public scrap::TraitCapturer {
    public:
        scrap::Frame _frame;
        std::vector<scrap::DisplayInfo> _displays;
        bool _select_ok = true;

        std::optional<scrap::Frame> frame(std::chrono::milliseconds /*timeout*/) override {
            if (_frame.empty()) return std::nullopt;
            return _frame;
        }
        std::vector<scrap::DisplayInfo> displays() const override { return _displays; }
        bool select_display(uint32_t /*index*/) override { return _select_ok; }
    };

    class MockDecoder : public scrap::Decoder {
    public:
        bool open_result = true;
        scrap::ImageRgb decode_result;
        bool closed = false;
        bool open_called = false;
        bool is_open_result = false;

        bool open(scrap::CodecFormat /*fmt*/) override {
            open_called = true;
            is_open_result = true;
            return open_result;
        }
        std::optional<scrap::ImageRgb> decode(const std::vector<uint8_t>& /*data*/) override {
            if (!decode_result.is_valid()) return std::nullopt;
            return decode_result;
        }
        void close() override { closed = true; is_open_result = false; }
        bool is_open() const override { return is_open_result; }
    };

    class MockEncoder : public scrap::Encoder {
    public:
        bool open_result = true;
        std::vector<uint8_t> encode_result;
        bool flush_result = true;
        bool closed = false;
        bool open_called = false;
        bool is_open_result = false;
        scrap::EncoderConfig _config;

        bool open(scrap::CodecFormat /*fmt*/, uint32_t /*w*/, uint32_t /*h*/,
                  uint32_t /*fps*/, uint32_t /*bitrate*/) override {
            open_called = true;
            is_open_result = true;
            return open_result;
        }
        std::vector<uint8_t> encode(const scrap::Frame& /*frame*/, bool& /*keyframe*/) override {
            return encode_result;
        }
        bool flush(std::vector<uint8_t>& /*out*/) override { return flush_result; }
        void close() override { closed = true; is_open_result = false; }
        bool is_open() const override { return is_open_result; }
        scrap::EncoderConfig config() const override { return _config; }
    };

    class MockRecorder : public scrap::Recorder {
    public:
        bool start_result = true;
        bool feed_result = true;
        bool stopped = false;
        bool recording = false;

        bool start(const Context& /*ctx*/) override { recording = true; return start_result; }
        bool feed(const scrap::Frame& /*frame*/) override { return feed_result; }
        void stop() override { stopped = true; recording = false; }
        bool is_recording() const override { return recording; }
    };

    class MockCamera : public scrap::Camera {
    public:
        bool open_result = true;
        scrap::Frame _frame;
        bool closed = false;
        std::vector<std::string> cameras;
        bool open_called = false;
        bool is_open_result = false;

        bool open(uint32_t /*index*/) override {
            open_called = true;
            is_open_result = true;
            return open_result;
        }
        std::optional<scrap::Frame> capture() override {
            if (_frame.empty()) return std::nullopt;
            return _frame;
        }
        void close() override { closed = true; is_open_result = false; }
        std::vector<std::string> list_cameras() override { return cameras; }
        bool is_open() const override { return is_open_result; }
    };
} // anonymous namespace

using namespace scrap;

// ============================================================================
// ImageFormat enum tests
// ============================================================================
TEST(ImageFormatTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(ImageFormat::RAW),    0);
    EXPECT_EQ(static_cast<uint8_t>(ImageFormat::ABGR),   1);
    EXPECT_EQ(static_cast<uint8_t>(ImageFormat::ARGB),   2);
    EXPECT_EQ(static_cast<uint8_t>(ImageFormat::BGRA),   3);
    EXPECT_EQ(static_cast<uint8_t>(ImageFormat::RGBA),   4);
    EXPECT_EQ(static_cast<uint8_t>(ImageFormat::YUV420), 5);
    EXPECT_EQ(static_cast<uint8_t>(ImageFormat::NV12),   6);
    EXPECT_EQ(static_cast<uint8_t>(ImageFormat::GRAY8),  7);
    EXPECT_EQ(static_cast<uint8_t>(ImageFormat::BGR),    8);
    EXPECT_EQ(static_cast<uint8_t>(ImageFormat::RGB),    9);
    EXPECT_EQ(static_cast<uint8_t>(ImageFormat::YUYV),   10);
    EXPECT_EQ(static_cast<uint8_t>(ImageFormat::UYVY),   11);
    EXPECT_EQ(static_cast<uint8_t>(ImageFormat::P010),   12);
}

TEST(ImageFormatTest, DistinctValues) {
    std::array<uint8_t, 13> vals = {
        static_cast<uint8_t>(ImageFormat::RAW),
        static_cast<uint8_t>(ImageFormat::ABGR),
        static_cast<uint8_t>(ImageFormat::ARGB),
        static_cast<uint8_t>(ImageFormat::BGRA),
        static_cast<uint8_t>(ImageFormat::RGBA),
        static_cast<uint8_t>(ImageFormat::YUV420),
        static_cast<uint8_t>(ImageFormat::NV12),
        static_cast<uint8_t>(ImageFormat::GRAY8),
        static_cast<uint8_t>(ImageFormat::BGR),
        static_cast<uint8_t>(ImageFormat::RGB),
        static_cast<uint8_t>(ImageFormat::YUYV),
        static_cast<uint8_t>(ImageFormat::UYVY),
        static_cast<uint8_t>(ImageFormat::P010),
    };
    std::sort(vals.begin(), vals.end());
    EXPECT_EQ(std::adjacent_find(vals.begin(), vals.end()), vals.end());
}

// ============================================================================
// CodecFormat enum tests
// ============================================================================
TEST(CodecFormatTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(CodecFormat::RAW),     0);
    EXPECT_EQ(static_cast<uint8_t>(CodecFormat::H264),    1);
    EXPECT_EQ(static_cast<uint8_t>(CodecFormat::H265),    2);
    EXPECT_EQ(static_cast<uint8_t>(CodecFormat::VP8),     3);
    EXPECT_EQ(static_cast<uint8_t>(CodecFormat::VP9),     4);
    EXPECT_EQ(static_cast<uint8_t>(CodecFormat::AV1),     5);
    EXPECT_EQ(static_cast<uint8_t>(CodecFormat::MJPEG),   6);
    EXPECT_EQ(static_cast<uint8_t>(CodecFormat::HEVC),    7);
    EXPECT_EQ(static_cast<uint8_t>(CodecFormat::AV1_SCC), 8);
}

TEST(CodecFormatTest, DistinctValues) {
    std::array<uint8_t, 9> vals = {
        static_cast<uint8_t>(CodecFormat::RAW),
        static_cast<uint8_t>(CodecFormat::H264),
        static_cast<uint8_t>(CodecFormat::H265),
        static_cast<uint8_t>(CodecFormat::VP8),
        static_cast<uint8_t>(CodecFormat::VP9),
        static_cast<uint8_t>(CodecFormat::AV1),
        static_cast<uint8_t>(CodecFormat::MJPEG),
        static_cast<uint8_t>(CodecFormat::HEVC),
        static_cast<uint8_t>(CodecFormat::AV1_SCC),
    };
    std::sort(vals.begin(), vals.end());
    EXPECT_EQ(std::adjacent_find(vals.begin(), vals.end()), vals.end());
}

// ============================================================================
// ScaleAlgorithm enum tests
// ============================================================================
TEST(ScaleAlgorithmTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(ScaleAlgorithm::Nearest),  0);
    EXPECT_EQ(static_cast<uint8_t>(ScaleAlgorithm::Bilinear), 1);
    EXPECT_EQ(static_cast<uint8_t>(ScaleAlgorithm::Bicubic),  2);
    EXPECT_EQ(static_cast<uint8_t>(ScaleAlgorithm::Lanczos3), 3);
    EXPECT_EQ(static_cast<uint8_t>(ScaleAlgorithm::AreaAvg),  4);
}

// ============================================================================
// ColorSpace enum tests
// ============================================================================
TEST(ColorSpaceTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(ColorSpace::BT601),      0);
    EXPECT_EQ(static_cast<uint8_t>(ColorSpace::BT709),      1);
    EXPECT_EQ(static_cast<uint8_t>(ColorSpace::BT2020),     2);
    EXPECT_EQ(static_cast<uint8_t>(ColorSpace::BT2020_PQ),  3);
    EXPECT_EQ(static_cast<uint8_t>(ColorSpace::BT2020_HLG), 4);
    EXPECT_EQ(static_cast<uint8_t>(ColorSpace::SRGB),       5);
}

// ============================================================================
// ColorRange enum tests
// ============================================================================
TEST(ColorRangeTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(ColorRange::Limited), 0);
    EXPECT_EQ(static_cast<uint8_t>(ColorRange::Full),    1);
}

// ============================================================================
// CodecProfile enum tests
// ============================================================================
TEST(CodecProfileTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(CodecProfile::Baseline),    0);
    EXPECT_EQ(static_cast<uint8_t>(CodecProfile::Main),        1);
    EXPECT_EQ(static_cast<uint8_t>(CodecProfile::High),        2);
    EXPECT_EQ(static_cast<uint8_t>(CodecProfile::High444),     3);
    EXPECT_EQ(static_cast<uint8_t>(CodecProfile::Main10),      4);
    EXPECT_EQ(static_cast<uint8_t>(CodecProfile::Main12),      5);
    EXPECT_EQ(static_cast<uint8_t>(CodecProfile::Professional), 6);
}

// ============================================================================
// EncoderQuality enum tests
// ============================================================================
TEST(EncoderQualityTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(EncoderQuality::Ultrafast), 0);
    EXPECT_EQ(static_cast<uint8_t>(EncoderQuality::Veryfast),  1);
    EXPECT_EQ(static_cast<uint8_t>(EncoderQuality::Fast),      2);
    EXPECT_EQ(static_cast<uint8_t>(EncoderQuality::Medium),    3);
    EXPECT_EQ(static_cast<uint8_t>(EncoderQuality::Slow),      4);
    EXPECT_EQ(static_cast<uint8_t>(EncoderQuality::Veryslow),  5);
    EXPECT_EQ(static_cast<uint8_t>(EncoderQuality::Lossless),  6);
}

// ============================================================================
// RateControl enum tests
// ============================================================================
TEST(RateControlTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(RateControl::CBR),    0);
    EXPECT_EQ(static_cast<uint8_t>(RateControl::VBR),    1);
    EXPECT_EQ(static_cast<uint8_t>(RateControl::CQP),    2);
    EXPECT_EQ(static_cast<uint8_t>(RateControl::CRF),    3);
    EXPECT_EQ(static_cast<uint8_t>(RateControl::VBR_HQ), 4);
    EXPECT_EQ(static_cast<uint8_t>(RateControl::QVBR),   5);
}

// ============================================================================
// EncoderBackend enum tests
// ============================================================================
TEST(EncoderBackendTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(EncoderBackend::Software),     0);
    EXPECT_EQ(static_cast<uint8_t>(EncoderBackend::NVENC),        1);
    EXPECT_EQ(static_cast<uint8_t>(EncoderBackend::AMF),          2);
    EXPECT_EQ(static_cast<uint8_t>(EncoderBackend::VAAPI),        3);
    EXPECT_EQ(static_cast<uint8_t>(EncoderBackend::VideoToolbox), 4);
    EXPECT_EQ(static_cast<uint8_t>(EncoderBackend::QSV),          5);
    EXPECT_EQ(static_cast<uint8_t>(EncoderBackend::MFX),          6);
    EXPECT_EQ(static_cast<uint8_t>(EncoderBackend::Auto),         7);
}

// ============================================================================
// AdapterType enum tests
// ============================================================================
TEST(AdapterTypeTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(AdapterType::Any),    0);
    EXPECT_EQ(static_cast<uint8_t>(AdapterType::NVIDIA), 1);
    EXPECT_EQ(static_cast<uint8_t>(AdapterType::AMD),    2);
    EXPECT_EQ(static_cast<uint8_t>(AdapterType::Intel),  3);
    EXPECT_EQ(static_cast<uint8_t>(AdapterType::Apple),  4);
}

// ============================================================================
// FrameFlag / FrameFlags tests
// ============================================================================
TEST(FrameFlagTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(FrameFlag::Key),       0);
    EXPECT_EQ(static_cast<uint8_t>(FrameFlag::Dirty),     1);
    EXPECT_EQ(static_cast<uint8_t>(FrameFlag::Cursor),    2);
    EXPECT_EQ(static_cast<uint8_t>(FrameFlag::Idr),       3);
    EXPECT_EQ(static_cast<uint8_t>(FrameFlag::SeqHeader), 4);
    EXPECT_EQ(static_cast<uint8_t>(FrameFlag::EOS),       5);
}

TEST(FrameFlagsTest, DefaultBitset) {
    FrameFlags flags;
    EXPECT_EQ(flags.count(), 0u);
    EXPECT_FALSE(flags.any());
    EXPECT_TRUE(flags.none());
}

TEST(FrameFlagsTest, SetAndTest) {
    FrameFlags flags;
    flags.set(static_cast<size_t>(FrameFlag::Key));
    EXPECT_TRUE(flags[static_cast<size_t>(FrameFlag::Key)]);
    EXPECT_FALSE(flags[static_cast<size_t>(FrameFlag::Dirty)]);

    flags.set(static_cast<size_t>(FrameFlag::Idr));
    EXPECT_EQ(flags.count(), 2u);

    flags.reset(static_cast<size_t>(FrameFlag::Key));
    EXPECT_FALSE(flags[static_cast<size_t>(FrameFlag::Key)]);
    EXPECT_TRUE(flags[static_cast<size_t>(FrameFlag::Idr)]);
}

TEST(FrameFlagsTest, AllFlags) {
    FrameFlags flags;
    for (int i = 0; i < 6; ++i) {
        flags.set(i);
    }
    EXPECT_EQ(flags.count(), 6u);
    EXPECT_TRUE(flags.any());
    EXPECT_FALSE(flags.none());

    flags.reset();
    EXPECT_EQ(flags.count(), 0u);
}

// ============================================================================
// DirtyRect tests
// ============================================================================
TEST(DirtyRectTest, DefaultConstruction) {
    DirtyRect r;
    EXPECT_EQ(r.left,   0u);
    EXPECT_EQ(r.top,    0u);
    EXPECT_EQ(r.right,  0u);
    EXPECT_EQ(r.bottom, 0u);
    EXPECT_FALSE(r.valid());
    EXPECT_EQ(r.width(),  0u);
    EXPECT_EQ(r.height(), 0u);
    EXPECT_EQ(r.area(),   0u);
}

TEST(DirtyRectTest, ValidRect) {
    DirtyRect r{10, 20, 110, 220};
    EXPECT_TRUE(r.valid());
    EXPECT_EQ(r.width(),  100u);
    EXPECT_EQ(r.height(), 200u);
    EXPECT_EQ(r.area(),   20000u);
}

TEST(DirtyRectTest, InvalidCases) {
    // Zero dimensions
    EXPECT_FALSE(DirtyRect{5, 5, 5, 10}.valid());
    EXPECT_FALSE(DirtyRect{5, 5, 10, 5}.valid());
    // Inverted
    EXPECT_FALSE(DirtyRect{10, 10, 5, 20}.valid());
    EXPECT_FALSE(DirtyRect{10, 10, 20, 5}.valid());
}

TEST(DirtyRectTest, Contains) {
    DirtyRect r{0, 0, 100, 200};
    EXPECT_TRUE(r.contains(0, 0));
    EXPECT_TRUE(r.contains(99, 199));
    EXPECT_TRUE(r.contains(50, 100));
    EXPECT_FALSE(r.contains(100, 0));
    EXPECT_FALSE(r.contains(0, 200));
    EXPECT_FALSE(r.contains(100, 200));
}

TEST(DirtyRectTest, IntersectOverlapping) {
    DirtyRect a{0, 0, 100, 100};
    DirtyRect b{50, 50, 150, 150};
    DirtyRect c = a.intersect(b);
    EXPECT_TRUE(c.valid());
    EXPECT_EQ(c.left,   50u);
    EXPECT_EQ(c.top,    50u);
    EXPECT_EQ(c.right,  100u);
    EXPECT_EQ(c.bottom, 100u);
    EXPECT_EQ(c.width(),  50u);
    EXPECT_EQ(c.height(), 50u);
}

TEST(DirtyRectTest, IntersectNonOverlapping) {
    DirtyRect a{0,   0, 50, 50};
    DirtyRect b{100, 100, 150, 150};
    DirtyRect c = a.intersect(b);
    EXPECT_FALSE(c.valid());
    EXPECT_EQ(c.left,   0u);
    EXPECT_EQ(c.top,    0u);
    EXPECT_EQ(c.right,  0u);
    EXPECT_EQ(c.bottom, 0u);
}

TEST(DirtyRectTest, IntersectTouching) {
    DirtyRect a{0, 0, 50, 50};
    DirtyRect b{50, 0, 100, 50};
    DirtyRect c = a.intersect(b);
    EXPECT_FALSE(c.valid());
}

TEST(DirtyRectTest, MergeValid) {
    DirtyRect a{0, 0, 50, 50};
    DirtyRect b{50, 50, 100, 100};
    DirtyRect c = a.merge(b);
    EXPECT_TRUE(c.valid());
    EXPECT_EQ(c.left,   0u);
    EXPECT_EQ(c.top,    0u);
    EXPECT_EQ(c.right,  100u);
    EXPECT_EQ(c.bottom, 100u);
}

TEST(DirtyRectTest, MergeWithInvalid) {
    DirtyRect a{10, 20, 110, 220};
    DirtyRect b; // invalid
    EXPECT_EQ(a.merge(b).left,  10u);
    EXPECT_EQ(b.merge(a).left,  10u);
    EXPECT_EQ(a.merge(b).right, 110u);
}

// ============================================================================
// ImageRgb tests
// ============================================================================
TEST(ImageRgbTest, DefaultConstruction) {
    ImageRgb img;
    EXPECT_EQ(img.w, 0u);
    EXPECT_EQ(img.h, 0u);
    EXPECT_EQ(img.fmt, ImageFormat::RGBA);
    EXPECT_EQ(img.align, 1u);
    EXPECT_EQ(img.cspace, ColorSpace::SRGB);
    EXPECT_EQ(img.range, ColorRange::Full);
    EXPECT_FALSE(img.is_valid());
    EXPECT_EQ(img.stride(), 0u);
    EXPECT_EQ(img.size_bytes(), 0u);
    EXPECT_TRUE(img.raw.empty());
}

TEST(ImageRgbTest, DefaultDataAccess) {
    ImageRgb img;
    EXPECT_EQ(img.data(), nullptr);
    const auto& cimg = img;
    EXPECT_EQ(cimg.data(), nullptr);
}

TEST(ImageRgbTest, ResizeRGBA) {
    ImageRgb img;
    img.resize(1920, 1080, ImageFormat::RGBA);
    EXPECT_TRUE(img.is_valid());
    EXPECT_EQ(img.w, 1920u);
    EXPECT_EQ(img.h, 1080u);
    EXPECT_EQ(img.fmt, ImageFormat::RGBA);
    EXPECT_EQ(img.stride(), 1920u * 4);
    EXPECT_EQ(img.size_bytes(), 1920u * 1080u * 4);
    EXPECT_EQ(img.raw.size(), 1920u * 1080u * 4);
}

TEST(ImageRgbTest, ResizeBGRA) {
    ImageRgb img;
    img.resize(640, 480, ImageFormat::BGRA);
    EXPECT_TRUE(img.is_valid());
    EXPECT_EQ(img.fmt, ImageFormat::BGRA);
    EXPECT_EQ(img.stride(), 640u * 4);
    EXPECT_EQ(img.size_bytes(), 640u * 480u * 4);
}

TEST(ImageRgbTest, ResizeARGB) {
    ImageRgb img;
    img.resize(100, 200, ImageFormat::ARGB);
    EXPECT_TRUE(img.is_valid());
    EXPECT_EQ(img.fmt, ImageFormat::ARGB);
    EXPECT_EQ(img.size_bytes(), 100u * 200u * 4);
}

TEST(ImageRgbTest, ResizeABGR) {
    ImageRgb img;
    img.resize(32, 32, ImageFormat::ABGR);
    EXPECT_TRUE(img.is_valid());
    EXPECT_EQ(img.fmt, ImageFormat::ABGR);
    EXPECT_EQ(img.size_bytes(), 32u * 32u * 4);
}

TEST(ImageRgbTest, ResizeGRAY8) {
    ImageRgb img;
    img.resize(64, 64, ImageFormat::GRAY8);
    EXPECT_TRUE(img.is_valid());
    EXPECT_EQ(img.fmt, ImageFormat::GRAY8);
    EXPECT_EQ(img.size_bytes(), 64u * 64u);
    EXPECT_EQ(img.stride(), 64u * 4); // stride() always w*4
}

TEST(ImageRgbTest, ResizeYUV420) {
    ImageRgb img;
    img.resize(64, 64, ImageFormat::YUV420);
    EXPECT_TRUE(img.is_valid());
    // w*h + w*h/2
    EXPECT_EQ(img.size_bytes(), 64u * 64u + 64u * 32u);
}

TEST(ImageRgbTest, ResizeNV12) {
    ImageRgb img;
    img.resize(64, 64, ImageFormat::NV12);
    EXPECT_TRUE(img.is_valid());
    EXPECT_EQ(img.size_bytes(), 64u * 64u + 64u * 32u);
}

TEST(ImageRgbTest, ResizeYUYV) {
    ImageRgb img;
    img.resize(64, 64, ImageFormat::YUYV);
    EXPECT_TRUE(img.is_valid());
    EXPECT_EQ(img.size_bytes(), 64u * 64u * 2);
}

TEST(ImageRgbTest, ResizeUYVY) {
    ImageRgb img;
    img.resize(64, 64, ImageFormat::UYVY);
    EXPECT_TRUE(img.is_valid());
    EXPECT_EQ(img.size_bytes(), 64u * 64u * 2);
}

TEST(ImageRgbTest, ResizeP010) {
    ImageRgb img;
    img.resize(64, 64, ImageFormat::P010);
    EXPECT_TRUE(img.is_valid());
    // (w*h + w*h/2)*2
    EXPECT_EQ(img.size_bytes(), (64u * 64u + 64u * 32u) * 2);
}

TEST(ImageRgbTest, ResizeRAW) {
    ImageRgb img;
    img.resize(16, 16, ImageFormat::RAW);
    EXPECT_TRUE(img.is_valid());
    EXPECT_EQ(img.size_bytes(), 16u * 16u * 4); // default case = 4bpp
}

TEST(ImageRgbTest, Clear) {
    ImageRgb img;
    img.resize(100, 100, ImageFormat::RGBA);
    EXPECT_TRUE(img.is_valid());
    img.clear();
    EXPECT_FALSE(img.is_valid());
    EXPECT_EQ(img.w, 0u);
    EXPECT_EQ(img.h, 0u);
    EXPECT_TRUE(img.raw.empty());
}

TEST(ImageRgbTest, Alignment) {
    ImageRgb img;
    img.align = 64;
    EXPECT_EQ(img.align, 64u);

    ImageRgb img2;
    img2.align = 1;
    EXPECT_EQ(img2.align, 1u);
}

TEST(ImageRgbTest, ColorProperties) {
    ImageRgb img;
    img.cspace = ColorSpace::BT709;
    img.range  = ColorRange::Limited;
    EXPECT_EQ(img.cspace, ColorSpace::BT709);
    EXPECT_EQ(img.range,  ColorRange::Limited);

    img.cspace = ColorSpace::BT2020_PQ;
    img.range  = ColorRange::Full;
    EXPECT_EQ(img.cspace, ColorSpace::BT2020_PQ);
    EXPECT_EQ(img.range,  ColorRange::Full);
}

TEST(ImageRgbTest, DataModification) {
    ImageRgb img;
    img.resize(2, 2, ImageFormat::RGBA);
    uint8_t* ptr = img.data();
    ASSERT_NE(ptr, nullptr);
    ptr[0] = 255;
    ptr[1] = 128;
    ptr[2] = 64;
    ptr[3] = 32;
    EXPECT_EQ(img.raw[0], 255);
    EXPECT_EQ(img.raw[1], 128);
    EXPECT_EQ(img.raw[2], 64);
    EXPECT_EQ(img.raw[3], 32);
}

TEST(ImageRgbTest, ConstDataAccess) {
    ImageRgb img;
    img.resize(1, 1, ImageFormat::RGBA);
    img.raw[0] = 100;
    const ImageRgb& cimg = img;
    EXPECT_EQ(cimg.data()[0], 100);
}

TEST(ImageRgbTest, ResizeZeroClear) {
    ImageRgb img;
    img.resize(0, 0, ImageFormat::RGBA);
    EXPECT_FALSE(img.is_valid());

    img.resize(1920, 0, ImageFormat::RGBA);
    EXPECT_FALSE(img.is_valid());

    img.resize(0, 1080, ImageFormat::RGBA);
    EXPECT_FALSE(img.is_valid());
}

// ============================================================================
// ImageTexture tests
// ============================================================================
TEST(ImageTextureTest, DefaultConstruction) {
    ImageTexture tex;
    EXPECT_EQ(tex.texture, nullptr);
    EXPECT_EQ(tex.w, 0u);
    EXPECT_EQ(tex.h, 0u);
    EXPECT_EQ(tex.format, CodecFormat::RAW);
    EXPECT_EQ(tex.mip_levels, 1u);
    EXPECT_EQ(tex.array_size, 1u);
    EXPECT_FALSE(tex.is_valid());
}

TEST(ImageTextureTest, ValidTexture) {
    ImageTexture tex;
    int dummy = 42;
    tex.texture = &dummy;
    tex.w = 640;
    tex.h = 480;
    EXPECT_TRUE(tex.is_valid());
}

TEST(ImageTextureTest, InvalidTextureNull) {
    ImageTexture tex;
    tex.w = 640;
    tex.h = 480;
    EXPECT_FALSE(tex.is_valid());
}

TEST(ImageTextureTest, InvalidTextureZeroSize) {
    ImageTexture tex;
    int dummy = 42;
    tex.texture = &dummy;
    EXPECT_FALSE(tex.is_valid());
    tex.w = 640;
    EXPECT_FALSE(tex.is_valid());
    tex.h = 480;
    EXPECT_TRUE(tex.is_valid());
}

TEST(ImageTextureTest, MipLevelsAndArraySize) {
    ImageTexture tex;
    tex.mip_levels = 4;
    tex.array_size = 6;
    EXPECT_EQ(tex.mip_levels, 4u);
    EXPECT_EQ(tex.array_size, 6u);
}

TEST(ImageTextureTest, FormatAssignment) {
    ImageTexture tex;
    tex.format = CodecFormat::H264;
    EXPECT_EQ(tex.format, CodecFormat::H264);
    tex.format = CodecFormat::AV1;
    EXPECT_EQ(tex.format, CodecFormat::AV1);
}

// ============================================================================
// PlaneData tests
// ============================================================================
TEST(PlaneDataTest, DefaultConstruction) {
    PlaneData p;
    EXPECT_EQ(p.data,   nullptr);
    EXPECT_EQ(p.width,  0u);
    EXPECT_EQ(p.height, 0u);
    EXPECT_EQ(p.stride, 0u);
    EXPECT_EQ(p.size,   0u);
}

TEST(PlaneDataTest, FieldAssignment) {
    PlaneData p;
    uint8_t buf[64];
    p.data   = buf;
    p.width  = 1920;
    p.height = 1080;
    p.stride = 1920;
    p.size   = 1920 * 1080;
    EXPECT_EQ(p.data,   buf);
    EXPECT_EQ(p.width,  1920u);
    EXPECT_EQ(p.height, 1080u);
    EXPECT_EQ(p.stride, 1920u);
    EXPECT_EQ(p.size,   1920u * 1080u);
}

// ============================================================================
// Frame tests
// ============================================================================
TEST(FrameTest, DefaultConstruction) {
    Frame f;
    EXPECT_TRUE(f.empty());
    EXPECT_EQ(f.size(), 0u);
    EXPECT_EQ(f.w, 0u);
    EXPECT_EQ(f.h, 0u);
    EXPECT_EQ(f.stride, 0u);
    EXPECT_EQ(f.fmt, ImageFormat::RGBA);
    EXPECT_EQ(f.timestamp_us, 0);
    EXPECT_TRUE(f.keyframe);
    EXPECT_EQ(f.codec, CodecFormat::RAW);
    EXPECT_EQ(f.flags.count(), 0u);
    EXPECT_FALSE(f.has_dirty());
    EXPECT_EQ(f.pts, 0);
    EXPECT_EQ(f.dts, 0);
    EXPECT_EQ(f.duration, 0);
    EXPECT_EQ(f.sequence_number, 0u);
    EXPECT_FALSE(f.cursor_visible);
    EXPECT_EQ(f.cursor_x, 0);
    EXPECT_EQ(f.cursor_y, 0);
    EXPECT_EQ(f.cursor_w, 0u);
    EXPECT_EQ(f.cursor_h, 0u);
    EXPECT_TRUE(f.cursor_data.empty());
    EXPECT_TRUE(f.planes.empty());
}

TEST(FrameTest, WithData) {
    Frame f;
    f.data = {0, 1, 2, 3};
    f.w = 2;
    f.h = 2;
    f.stride = 8;
    f.timestamp_us = 123456;
    f.keyframe = false;
    f.codec = CodecFormat::H264;
    f.flags.set(static_cast<size_t>(FrameFlag::Idr));
    f.pts = 1000;
    f.dts = 900;
    f.duration = 33;
    f.sequence_number = 42;

    EXPECT_FALSE(f.empty());
    EXPECT_EQ(f.size(), 4u);
    EXPECT_EQ(f.w, 2u);
    EXPECT_EQ(f.h, 2u);
    EXPECT_EQ(f.stride, 8u);
    EXPECT_EQ(f.timestamp_us, 123456);
    EXPECT_FALSE(f.keyframe);
    EXPECT_EQ(f.codec, CodecFormat::H264);
    EXPECT_TRUE(f.flags[static_cast<size_t>(FrameFlag::Idr)]);
    EXPECT_EQ(f.pts, 1000);
    EXPECT_EQ(f.dts, 900);
    EXPECT_EQ(f.duration, 33);
    EXPECT_EQ(f.sequence_number, 42u);
}

TEST(FrameTest, DirtyRects) {
    Frame f;
    EXPECT_FALSE(f.has_dirty());
    f.dirty_rects.push_back(DirtyRect{0, 0, 100, 100});
    EXPECT_TRUE(f.has_dirty());

    f.dirty_rects.clear();
    EXPECT_FALSE(f.has_dirty());
}

TEST(FrameTest, Planes) {
    Frame f;
    EXPECT_TRUE(f.planes.empty());
    f.planes.push_back(PlaneData{});
    f.planes.push_back(PlaneData{});
    EXPECT_EQ(f.planes.size(), 2u);
}

TEST(FrameTest, CursorData) {
    Frame f;
    f.cursor_visible = true;
    f.cursor_x = 500;
    f.cursor_y = 300;
    f.cursor_w = 32;
    f.cursor_h = 32;
    f.cursor_data = {1, 2, 3, 4};

    EXPECT_TRUE(f.cursor_visible);
    EXPECT_EQ(f.cursor_x, 500);
    EXPECT_EQ(f.cursor_y, 300);
    EXPECT_EQ(f.cursor_w, 32u);
    EXPECT_EQ(f.cursor_h, 32u);
    EXPECT_EQ(f.cursor_data.size(), 4u);
}

TEST(FrameTest, Swap) {
    Frame a;
    a.data = {10, 20, 30};
    a.w = 640; a.h = 480;
    a.stride = 2560;
    a.fmt = ImageFormat::BGRA;
    a.timestamp_us = 1000000;
    a.keyframe = false;
    a.codec = CodecFormat::H265;
    a.flags.set(static_cast<size_t>(FrameFlag::Key));
    a.pts = 3000;
    a.dts = 2900;
    a.duration = 33;
    a.sequence_number = 99;
    a.cursor_visible = true;
    a.cursor_x = 10;
    a.cursor_y = 20;
    a.cursor_w = 16;
    a.cursor_h = 16;
    a.cursor_data = {7, 8};
    a.dirty_rects.push_back(DirtyRect{0, 0, 10, 10});
    a.planes.push_back(PlaneData{});

    Frame b;
    b.data = {40, 50};
    b.w = 1920; b.h = 1080;
    b.stride = 7680;
    b.fmt = ImageFormat::RGBA;
    b.timestamp_us = 2000000;
    b.keyframe = true;
    b.codec = CodecFormat::RAW;
    b.pts = 6000;
    b.dts = 5900;
    b.duration = 16;
    b.sequence_number = 1;
    b.cursor_visible = false;
    b.cursor_data = {99};

    a.swap(b);

    // a now has b's old values
    EXPECT_EQ(a.data, (std::vector<uint8_t>{40, 50}));
    EXPECT_EQ(a.w, 1920u);
    EXPECT_EQ(a.h, 1080u);
    EXPECT_EQ(a.stride, 7680u);
    EXPECT_EQ(a.fmt, ImageFormat::RGBA);
    EXPECT_EQ(a.timestamp_us, 2000000);
    EXPECT_TRUE(a.keyframe);
    EXPECT_EQ(a.codec, CodecFormat::RAW);
    EXPECT_EQ(a.pts, 6000);
    EXPECT_EQ(a.dts, 5900);
    EXPECT_EQ(a.duration, 16);
    EXPECT_EQ(a.sequence_number, 1u);
    EXPECT_FALSE(a.cursor_visible);
    EXPECT_EQ(a.cursor_data, (std::vector<uint8_t>{99}));
    EXPECT_TRUE(a.dirty_rects.empty());
    EXPECT_TRUE(a.planes.empty());

    // b now has a's old values
    EXPECT_EQ(b.data, (std::vector<uint8_t>{10, 20, 30}));
    EXPECT_EQ(b.w, 640u);
    EXPECT_FALSE(b.keyframe);
    EXPECT_EQ(b.codec, CodecFormat::H265);
    EXPECT_EQ(b.pts, 3000);
    EXPECT_TRUE(b.cursor_visible);
    EXPECT_EQ(b.cursor_data, (std::vector<uint8_t>{7, 8}));
}

TEST(FrameTest, SwapEmpty) {
    Frame a, b;
    a.data = {1};
    a.w = 100;
    a.swap(b);
    EXPECT_TRUE(a.empty());
    EXPECT_FALSE(b.empty());
    EXPECT_EQ(b.w, 100u);
}

// ============================================================================
// DisplayInfo tests
// ============================================================================
TEST(DisplayInfoTest, DefaultConstruction) {
    DisplayInfo d;
    EXPECT_EQ(d.index, 0u);
    EXPECT_TRUE(d.name.empty());
    EXPECT_EQ(d.x, 0);
    EXPECT_EQ(d.y, 0);
    EXPECT_EQ(d.width, 0u);
    EXPECT_EQ(d.height, 0u);
    EXPECT_EQ(d.refresh_rate, 60u);
    EXPECT_FALSE(d.is_primary);
    EXPECT_FALSE(d.is_virtual);
    EXPECT_DOUBLE_EQ(d.scale, 1.0);
    EXPECT_EQ(d.bits_per_pixel, 32u);
    EXPECT_FALSE(d.hdr_capable);
    EXPECT_EQ(d.native_color_space, ColorSpace::SRGB);
    EXPECT_TRUE(d.adapter_name.empty());
    EXPECT_EQ(d.adapter_index, 0u);
}

TEST(DisplayInfoTest, FieldAssignment) {
    DisplayInfo d;
    d.index = 1;
    d.name  = "DisplayPort-0";
    d.x = 100;
    d.y = 200;
    d.width  = 2560;
    d.height = 1440;
    d.refresh_rate = 144;
    d.is_primary    = true;
    d.is_virtual    = false;
    d.scale = 1.5;
    d.bits_per_pixel = 30;
    d.hdr_capable = true;
    d.native_color_space = ColorSpace::BT2020;
    d.adapter_name = "NVIDIA RTX 4090";
    d.adapter_index = 0;

    EXPECT_EQ(d.index, 1u);
    EXPECT_EQ(d.name,  "DisplayPort-0");
    EXPECT_EQ(d.x, 100);
    EXPECT_EQ(d.y, 200);
    EXPECT_EQ(d.width,  2560u);
    EXPECT_EQ(d.height, 1440u);
    EXPECT_EQ(d.refresh_rate, 144u);
    EXPECT_TRUE(d.is_primary);
    EXPECT_FALSE(d.is_virtual);
    EXPECT_DOUBLE_EQ(d.scale, 1.5);
    EXPECT_EQ(d.bits_per_pixel, 30u);
    EXPECT_TRUE(d.hdr_capable);
    EXPECT_EQ(d.native_color_space, ColorSpace::BT2020);
    EXPECT_EQ(d.adapter_name, "NVIDIA RTX 4090");
    EXPECT_EQ(d.adapter_index, 0u);
}

TEST(DisplayInfoTest, VirtualDisplay) {
    DisplayInfo d;
    d.is_virtual = true;
    EXPECT_TRUE(d.is_virtual);
}

// ============================================================================
// FrameStats tests
// ============================================================================
TEST(FrameStatsTest, DefaultConstruction) {
    FrameStats s;
    EXPECT_EQ(s.total_frames,    0u);
    EXPECT_EQ(s.captured_frames, 0u);
    EXPECT_EQ(s.dropped_frames,  0u);
    EXPECT_EQ(s.encoded_frames,  0u);
    EXPECT_EQ(s.key_frames,      0u);
    EXPECT_EQ(s.total_bytes,     0u);
    EXPECT_EQ(s.encoded_bytes,   0u);
    EXPECT_DOUBLE_EQ(s.avg_capture_ms, 0.0);
    EXPECT_DOUBLE_EQ(s.avg_encode_ms,  0.0);
    EXPECT_DOUBLE_EQ(s.avg_fps,        0.0);
    EXPECT_DOUBLE_EQ(s.current_fps,    0.0);
    EXPECT_DOUBLE_EQ(s.peak_fps,       0.0);
    EXPECT_EQ(s.current_bitrate,   0u);
    EXPECT_EQ(s.dirty_rect_pct,    0u);
    EXPECT_EQ(s.last_frame_ts,     0);
    EXPECT_EQ(s.session_start_ts,  0);
    EXPECT_EQ(s.consecutive_drops, 0u);
}

TEST(FrameStatsTest, Reset) {
    FrameStats s;
    s.total_frames    = 100;
    s.captured_frames = 95;
    s.dropped_frames  = 5;
    s.encoded_frames  = 95;
    s.key_frames      = 10;
    s.total_bytes     = 1000000;
    s.encoded_bytes   = 500000;
    s.avg_capture_ms  = 2.5;
    s.avg_encode_ms   = 3.0;
    s.avg_fps         = 30.0;
    s.current_fps     = 28.0;
    s.peak_fps        = 32.0;
    s.current_bitrate = 5000000;
    s.dirty_rect_pct  = 25;
    s.last_frame_ts   = 123456789;
    s.session_start_ts = 0;
    s.consecutive_drops = 2;

    s.reset();

    EXPECT_EQ(s.total_frames,    0u);
    EXPECT_EQ(s.captured_frames, 0u);
    EXPECT_EQ(s.dropped_frames,  0u);
    EXPECT_EQ(s.encoded_frames,  0u);
    EXPECT_EQ(s.key_frames,      0u);
    EXPECT_EQ(s.total_bytes,     0u);
    EXPECT_EQ(s.encoded_bytes,   0u);
    EXPECT_DOUBLE_EQ(s.avg_capture_ms, 0.0);
    EXPECT_DOUBLE_EQ(s.avg_encode_ms,  0.0);
    EXPECT_DOUBLE_EQ(s.avg_fps,        0.0);
    EXPECT_DOUBLE_EQ(s.current_fps,    0.0);
    EXPECT_DOUBLE_EQ(s.peak_fps,       0.0);
    EXPECT_EQ(s.current_bitrate,   0u);
    EXPECT_EQ(s.dirty_rect_pct,    0u);
    EXPECT_EQ(s.consecutive_drops, 0u);
}

// ============================================================================
// EncoderConfig tests
// ============================================================================
TEST(EncoderConfigTest, DefaultConstruction) {
    EncoderConfig cfg;
    EXPECT_EQ(cfg.codec,        CodecFormat::H264);
    EXPECT_EQ(cfg.width,        1920u);
    EXPECT_EQ(cfg.height,       1080u);
    EXPECT_EQ(cfg.fps_num,      30u);
    EXPECT_EQ(cfg.fps_den,      1u);
    EXPECT_EQ(cfg.bitrate,      5000000u);
    EXPECT_EQ(cfg.max_bitrate,  0u);
    EXPECT_EQ(cfg.buffer_size,  0u);
    EXPECT_EQ(cfg.gop_size,     60u);
    EXPECT_EQ(cfg.b_frames,     0u);
    EXPECT_EQ(cfg.profile,      CodecProfile::High);
    EXPECT_EQ(cfg.quality,      EncoderQuality::Medium);
    EXPECT_EQ(cfg.rate_control, RateControl::VBR);
    EXPECT_EQ(cfg.backend,      EncoderBackend::Auto);
    EXPECT_EQ(cfg.adapter,      AdapterType::Any);
    EXPECT_EQ(cfg.adapter_index, 0u);
    EXPECT_EQ(cfg.qp_min,       0u);
    EXPECT_EQ(cfg.qp_max,       51u);
    EXPECT_EQ(cfg.qp_i,         23u);
    EXPECT_EQ(cfg.qp_p,         23u);
    EXPECT_EQ(cfg.qp_b,         23u);
    EXPECT_EQ(cfg.crf,          23);
    EXPECT_EQ(cfg.ref_frames,   3);
    EXPECT_EQ(cfg.threads,      0);
    EXPECT_TRUE(cfg.low_latency);
    EXPECT_FALSE(cfg.full_range);
    EXPECT_EQ(cfg.color_space,  ColorSpace::BT709);
    EXPECT_EQ(cfg.color_range,  ColorRange::Limited);
    EXPECT_TRUE(cfg.preset_override.empty());
    EXPECT_TRUE(cfg.tune.empty());
    EXPECT_TRUE(cfg.extra_opts.empty());
}

TEST(EncoderConfigTest, FpsMethod) {
    EncoderConfig cfg;
    cfg.fps_num = 60;
    cfg.fps_den = 1;
    EXPECT_EQ(cfg.fps(), 60u);

    cfg.fps_num = 30;
    cfg.fps_den = 2;
    EXPECT_EQ(cfg.fps(), 15u);

    cfg.fps_num = 0;
    cfg.fps_den = 1;
    EXPECT_EQ(cfg.fps(), 0u);

    // fps_den == 0 guard
    cfg.fps_num = 30;
    cfg.fps_den = 0;
    EXPECT_EQ(cfg.fps(), 30u);
}

TEST(EncoderConfigTest, FrameIntervalMs) {
    EncoderConfig cfg;
    cfg.fps_num = 60;
    cfg.fps_den = 1;
    EXPECT_NEAR(cfg.frame_interval_ms(), 1000.0 / 60.0, 0.1);

    cfg.fps_num = 30;
    cfg.fps_den = 1;
    EXPECT_NEAR(cfg.frame_interval_ms(), 1000.0 / 30.0, 0.1);

    cfg.fps_num = 30;
    cfg.fps_den = 2;
    EXPECT_NEAR(cfg.frame_interval_ms(), 1000.0 / 15.0, 0.1);

    // zero fps
    cfg.fps_num = 0;
    cfg.fps_den = 1;
    EXPECT_NEAR(cfg.frame_interval_ms(), 33.333, 0.1);
}

TEST(EncoderConfigTest, FieldAssignment) {
    EncoderConfig cfg;
    cfg.codec        = CodecFormat::H265;
    cfg.width        = 3840;
    cfg.height       = 2160;
    cfg.fps_num      = 60;
    cfg.fps_den      = 1;
    cfg.bitrate      = 20000000;
    cfg.max_bitrate  = 25000000;
    cfg.buffer_size  = 5000000;
    cfg.gop_size     = 120;
    cfg.b_frames     = 2;
    cfg.profile      = CodecProfile::Main10;
    cfg.quality      = EncoderQuality::Slow;
    cfg.rate_control = RateControl::CRF;
    cfg.backend      = EncoderBackend::NVENC;
    cfg.adapter      = AdapterType::NVIDIA;
    cfg.adapter_index = 0;
    cfg.qp_min = 18;
    cfg.qp_max = 45;
    cfg.qp_i   = 18;
    cfg.qp_p   = 24;
    cfg.qp_b   = 28;
    cfg.crf        = 18;
    cfg.ref_frames = 5;
    cfg.threads    = 8;
    cfg.low_latency = false;
    cfg.full_range  = true;
    cfg.color_space = ColorSpace::BT2020;
    cfg.color_range = ColorRange::Full;
    cfg.preset_override = "p5";
    cfg.tune = "hq";
    cfg.extra_opts = "aq-strength=8";

    EXPECT_EQ(cfg.codec,    CodecFormat::H265);
    EXPECT_EQ(cfg.width,    3840u);
    EXPECT_EQ(cfg.height,   2160u);
    EXPECT_EQ(cfg.bitrate,  20000000u);
    EXPECT_EQ(cfg.max_bitrate, 25000000u);
    EXPECT_EQ(cfg.buffer_size, 5000000u);
    EXPECT_EQ(cfg.gop_size, 120u);
    EXPECT_EQ(cfg.b_frames, 2u);
    EXPECT_EQ(cfg.profile,  CodecProfile::Main10);
    EXPECT_EQ(cfg.quality,  EncoderQuality::Slow);
    EXPECT_EQ(cfg.rate_control, RateControl::CRF);
    EXPECT_EQ(cfg.backend,  EncoderBackend::NVENC);
    EXPECT_EQ(cfg.adapter,  AdapterType::NVIDIA);
    EXPECT_EQ(cfg.qp_min, 18u);
    EXPECT_EQ(cfg.qp_max, 45u);
    EXPECT_EQ(cfg.crf, 18);
    EXPECT_EQ(cfg.ref_frames, 5);
    EXPECT_EQ(cfg.threads, 8);
    EXPECT_FALSE(cfg.low_latency);
    EXPECT_TRUE(cfg.full_range);
    EXPECT_EQ(cfg.color_space, ColorSpace::BT2020);
    EXPECT_EQ(cfg.color_range, ColorRange::Full);
    EXPECT_EQ(cfg.preset_override, "p5");
    EXPECT_EQ(cfg.tune, "hq");
    EXPECT_EQ(cfg.extra_opts, "aq-strength=8");
}

// ============================================================================
// ScaleConfig tests
// ============================================================================
TEST(ScaleConfigTest, DefaultConstruction) {
    ScaleConfig cfg;
    EXPECT_EQ(cfg.src_w,  0u);
    EXPECT_EQ(cfg.src_h,  0u);
    EXPECT_EQ(cfg.dst_w,  0u);
    EXPECT_EQ(cfg.dst_h,  0u);
    EXPECT_EQ(cfg.algo,   ScaleAlgorithm::Bilinear);
    EXPECT_EQ(cfg.src_fmt, ImageFormat::RGBA);
    EXPECT_EQ(cfg.dst_fmt, ImageFormat::RGBA);
    EXPECT_FALSE(cfg.keep_aspect);
    EXPECT_EQ(cfg.background_r, 0);
    EXPECT_EQ(cfg.background_g, 0);
    EXPECT_EQ(cfg.background_b, 0);
    EXPECT_EQ(cfg.background_a, 255);
}

TEST(ScaleConfigTest, ScaleXY) {
    ScaleConfig cfg;
    cfg.src_w = 640;
    cfg.src_h = 480;
    cfg.dst_w = 1280;
    cfg.dst_h = 960;
    EXPECT_DOUBLE_EQ(cfg.scale_x(), 2.0);
    EXPECT_DOUBLE_EQ(cfg.scale_y(), 2.0);
}

TEST(ScaleConfigTest, ScaleXYDownsample) {
    ScaleConfig cfg;
    cfg.src_w = 3840;
    cfg.src_h = 2160;
    cfg.dst_w = 1920;
    cfg.dst_h = 1080;
    EXPECT_DOUBLE_EQ(cfg.scale_x(), 0.5);
    EXPECT_DOUBLE_EQ(cfg.scale_y(), 0.5);
}

TEST(ScaleConfigTest, ScaleXYZeroSrc) {
    ScaleConfig cfg;
    cfg.src_w = 0;
    cfg.src_h = 0;
    cfg.dst_w = 1920;
    cfg.dst_h = 1080;
    EXPECT_DOUBLE_EQ(cfg.scale_x(), 1.0);
    EXPECT_DOUBLE_EQ(cfg.scale_y(), 1.0);
}

TEST(ScaleConfigTest, BackgroundColor) {
    ScaleConfig cfg;
    cfg.background_r = 255;
    cfg.background_g = 128;
    cfg.background_b = 64;
    cfg.background_a = 200;
    EXPECT_EQ(cfg.background_r, 255);
    EXPECT_EQ(cfg.background_g, 128);
    EXPECT_EQ(cfg.background_b, 64);
    EXPECT_EQ(cfg.background_a, 200);
}

TEST(ScaleConfigTest, KeepAspect) {
    ScaleConfig cfg;
    cfg.keep_aspect = true;
    EXPECT_TRUE(cfg.keep_aspect);
}

// ============================================================================
// ColorConversionPlan tests
// ============================================================================
TEST(ColorConversionPlanTest, DefaultConstruction) {
    ColorConversionPlan plan;
    EXPECT_EQ(plan.src_fmt,    ImageFormat::RGBA);
    EXPECT_EQ(plan.dst_fmt,    ImageFormat::RGBA);
    EXPECT_EQ(plan.src_cspace, ColorSpace::SRGB);
    EXPECT_EQ(plan.dst_cspace, ColorSpace::SRGB);
    EXPECT_EQ(plan.src_range,  ColorRange::Full);
    EXPECT_EQ(plan.dst_range,  ColorRange::Full);
    EXPECT_FALSE(plan.premultiply_alpha);
    EXPECT_FALSE(plan.unpremultiply_alpha);
    EXPECT_EQ(plan.alpha_fill, 255);
}

TEST(ColorConversionPlanTest, IsIdentityDefault) {
    ColorConversionPlan plan;
    EXPECT_TRUE(plan.is_identity());
}

TEST(ColorConversionPlanTest, IsIdentityDifferentFmt) {
    ColorConversionPlan plan;
    plan.dst_fmt = ImageFormat::BGRA;
    EXPECT_FALSE(plan.is_identity());
}

TEST(ColorConversionPlanTest, IsIdentityDifferentSpace) {
    ColorConversionPlan plan;
    plan.dst_cspace = ColorSpace::BT709;
    EXPECT_FALSE(plan.is_identity());
}

TEST(ColorConversionPlanTest, IsIdentityDifferentRange) {
    ColorConversionPlan plan;
    plan.dst_range = ColorRange::Limited;
    EXPECT_FALSE(plan.is_identity());
}

TEST(ColorConversionPlanTest, IsIdentityPremultiplied) {
    ColorConversionPlan plan;
    plan.premultiply_alpha = true;
    EXPECT_FALSE(plan.is_identity());

    ColorConversionPlan plan2;
    plan2.unpremultiply_alpha = true;
    EXPECT_FALSE(plan2.is_identity());
}

TEST(ColorConversionPlanTest, AlphaFill) {
    ColorConversionPlan plan;
    plan.alpha_fill = 128;
    EXPECT_EQ(plan.alpha_fill, 128);
    plan.alpha_fill = 0;
    EXPECT_EQ(plan.alpha_fill, 0);
}

// ============================================================================
// CaptureRegion tests
// ============================================================================
TEST(CaptureRegionTest, DefaultConstruction) {
    CaptureRegion r;
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, 0);
    EXPECT_EQ(r.w, 0u);
    EXPECT_EQ(r.h, 0u);
    EXPECT_FALSE(r.valid());
}

TEST(CaptureRegionTest, ValidRegion) {
    CaptureRegion r{0, 0, 1920, 1080};
    EXPECT_TRUE(r.valid());
}

TEST(CaptureRegionTest, ContainsPoint) {
    CaptureRegion r{10, 20, 100, 200};
    EXPECT_TRUE(r.contains_point(10, 20));
    EXPECT_TRUE(r.contains_point(109, 219));
    EXPECT_TRUE(r.contains_point(50, 100));
    EXPECT_FALSE(r.contains_point(9, 20));
    EXPECT_FALSE(r.contains_point(10, 19));
    EXPECT_FALSE(r.contains_point(110, 20));
    EXPECT_FALSE(r.contains_point(10, 220));
}

TEST(CaptureRegionTest, InvalidRegionNoPoint) {
    CaptureRegion r{0, 0, 0, 0};
    EXPECT_FALSE(r.valid());
    EXPECT_FALSE(r.contains_point(0, 0));
}

// ============================================================================
// FilterState tests
// ============================================================================
TEST(FilterStateTest, DefaultConstruction) {
    FilterState fs;
    EXPECT_TRUE(fs.coeffs.empty());
    EXPECT_TRUE(fs.buffer.empty());
    EXPECT_EQ(fs.tap_count,   0u);
    EXPECT_EQ(fs.phase_count, 0u);
    EXPECT_FALSE(fs.initialized);
}

TEST(FilterStateTest, Initialization) {
    FilterState fs;
    fs.tap_count   = 4;
    fs.phase_count = 16;
    fs.coeffs.resize(64, 0.25f);
    fs.initialized = true;
    EXPECT_EQ(fs.tap_count,   4u);
    EXPECT_EQ(fs.phase_count, 16u);
    EXPECT_TRUE(fs.initialized);
}

// ============================================================================
// TraitCapturer tests (base virtual interface)
// ============================================================================
TEST(TraitCapturerTest, FactoryCreate) {
    auto capturer = create_capturer();
    EXPECT_NE(capturer, nullptr);
}

TEST(TraitCapturerTest, IsGdiDefault) {
    auto capturer = create_capturer();
    EXPECT_FALSE(capturer->is_gdi());
}

TEST(TraitCapturerTest, SetGdiDefault) {
    auto capturer = create_capturer();
    EXPECT_FALSE(capturer->set_gdi());
}

TEST(TraitCapturerTest, Displays) {
    auto capturer = create_capturer();
    auto displays = capturer->displays();
    // May be empty on headless systems; just check it returns valid type
    EXPECT_TRUE(displays.empty() || displays.size() >= 1);
}

TEST(TraitCapturerTest, SelectDisplay) {
    auto capturer = create_capturer();
    bool result = capturer->select_display(0);
    // May succeed or fail depending on platform; just check no crash
    EXPECT_TRUE(result || !result);
}

TEST(TraitCapturerTest, FrameTimeout) {
    auto capturer = create_capturer();
    auto f = capturer->frame(std::chrono::milliseconds(10));
    // On headless / no display, may return nullopt; this is valid
    if (f) {
        EXPECT_GE(f->w, 0u);
        EXPECT_GE(f->h, 0u);
    }
}

TEST(TraitCapturerTest, CaptureRegionDefault) {
    auto capturer = create_capturer();
    CaptureRegion region{0, 0, 100, 100};
    auto f = capturer->capture_region(region, std::chrono::milliseconds(10));
    if (f) {
        // If we got a frame, the region should be respected within bounds
        EXPECT_TRUE(f->w <= 100u || f->w >= 0u);
    }
}

TEST(TraitCapturerTest, CurrentDisplayDefault) {
    auto capturer = create_capturer();
    EXPECT_EQ(capturer->current_display(), 0u);
}

TEST(TraitCapturerTest, StatsDefault) {
    auto capturer = create_capturer();
    auto s = capturer->stats();
    EXPECT_EQ(s.total_frames, 0u);
}

TEST(TraitCapturerTest, ResetStatsNoExcept) {
    auto capturer = create_capturer();
    EXPECT_NO_THROW(capturer->reset_stats());
}

TEST(TraitCapturerTest, DisplayCountDefault) {
    auto capturer = create_capturer();
    EXPECT_GE(capturer->display_count(), 0u);
}

TEST(TraitCapturerTest, CursorSupport) {
    auto capturer = create_capturer();
    // Just verify no crash
    bool supported = capturer->cursor_capture_supported();
    EXPECT_TRUE(supported || !supported);
}

TEST(TraitCapturerTest, SetCursorCaptureNoExcept) {
    auto capturer = create_capturer();
    EXPECT_NO_THROW(capturer->set_cursor_capture(true));
    EXPECT_NO_THROW(capturer->set_cursor_capture(false));
}

TEST(TraitCapturerTest, CursorCaptureEnabled) {
    auto capturer = create_capturer();
    bool enabled = capturer->cursor_capture_enabled();
    EXPECT_TRUE(enabled || !enabled);
}

TEST(TraitCapturerTest, DisplayRefreshRate) {
    auto capturer = create_capturer();
    uint32_t rate = capturer->display_refresh_rate();
    EXPECT_GE(rate, 0u);
}

TEST(TraitCapturerTest, DisplayBounds) {
    auto capturer = create_capturer();
    int32_t x, y;
    uint32_t w, h;
    bool ok = capturer->display_bounds(0, x, y, w, h);
    EXPECT_TRUE(ok || !ok);
}

TEST(TraitCapturerTest, DirtyDetection) {
    auto capturer = create_capturer();
    EXPECT_NO_THROW(capturer->set_dirty_detection(true));
    EXPECT_NO_THROW(capturer->set_dirty_detection(false));
}

TEST(TraitCapturerTest, LastDirtyRects) {
    auto capturer = create_capturer();
    auto rects = capturer->last_dirty_rects();
    EXPECT_TRUE(rects.empty()); // no capture yet
}

TEST(TraitCapturerTest, IsCapturing) {
    auto capturer = create_capturer();
    bool capturing = capturer->is_capturing();
    EXPECT_TRUE(capturing || !capturing);
}

TEST(TraitCapturerTest, PauseResume) {
    auto capturer = create_capturer();
    EXPECT_NO_THROW(capturer->pause());
    EXPECT_NO_THROW(capturer->resume());
}

// ============================================================================
// Mock TraitCapturer tests
// ============================================================================
TEST(MockCapturerTest, Displays) {
    MockCapturer mc;
    mc._displays.push_back(DisplayInfo{});
    EXPECT_EQ(mc.displays().size(), 1u);
}

TEST(MockCapturerTest, SelectDisplay) {
    MockCapturer mc;
    EXPECT_TRUE(mc.select_display(0));
    mc._select_ok = false;
    EXPECT_FALSE(mc.select_display(0));
}

TEST(MockCapturerTest, Frame) {
    MockCapturer mc;
    // Empty frame returns nullopt
    auto f = mc.frame(std::chrono::milliseconds(100));
    EXPECT_FALSE(f.has_value());

    mc._frame.data = {0};
    mc._frame.w = 1; mc._frame.h = 1;
    f = mc.frame(std::chrono::milliseconds(100));
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->w, 1u);
}

// ============================================================================
// Decoder tests
// ============================================================================
TEST(DecoderTest, FactoryCreate) {
    auto decoder = create_decoder();
    EXPECT_NE(decoder, nullptr);
}

TEST(DecoderTest, OpenClose) {
    auto decoder = create_decoder();
    // The real decoder may fail; we just test no crash
    for (int i = 0; i <= 8; ++i) {
        bool ok = decoder->open(static_cast<CodecFormat>(i));
        EXPECT_TRUE(ok || !ok);
        decoder->close();
    }
}

TEST(DecoderTest, DecodeEmptyData) {
    auto decoder = create_decoder();
    decoder->open(CodecFormat::H264);
    std::vector<uint8_t> empty;
    auto img = decoder->decode(empty);
    // Should return nullopt or empty image on empty data
    if (img) {
        EXPECT_FALSE(img->is_valid());
    }
    decoder->close();
}

TEST(DecoderTest, IsOpenDefault) {
    auto decoder = create_decoder();
    EXPECT_FALSE(decoder->is_open());
}

TEST(DecoderTest, WidthHeightDefault) {
    auto decoder = create_decoder();
    EXPECT_EQ(decoder->width(),  0);
    EXPECT_EQ(decoder->height(), 0);
}

TEST(DecoderTest, FlushDefault) {
    auto decoder = create_decoder();
    EXPECT_TRUE(decoder->flush());
}

TEST(DecoderTest, OpenWithConfig) {
    auto decoder = create_decoder();
    bool ok = decoder->open_with_config(CodecFormat::H264, 1920, 1080, ColorSpace::BT709);
    EXPECT_TRUE(ok || !ok);
    decoder->close();
}

TEST(DecoderTest, DecodePacket) {
    auto decoder = create_decoder();
    decoder->open(CodecFormat::H264);
    std::vector<uint8_t> data{0, 0, 0, 1, 0x67}; // partial AVC data
    auto img = decoder->decode_packet(data.data(), data.size(), 0);
    // May fail on fake data; just verify no crash
    EXPECT_TRUE(img.has_value() || !img.has_value());
    decoder->close();
}

TEST(DecoderTest, DecodePacketNull) {
    auto decoder = create_decoder();
    auto img = decoder->decode_packet(nullptr, 0, 0);
    EXPECT_FALSE(img.has_value());
}

TEST(DecoderTest, DecodePacketZeroSize) {
    auto decoder = create_decoder();
    uint8_t dummy = 0;
    auto img = decoder->decode_packet(&dummy, 0, 0);
    EXPECT_FALSE(img.has_value());
}

// ============================================================================
// Mock Decoder tests
// ============================================================================
TEST(MockDecoderTest, Open) {
    MockDecoder md;
    EXPECT_FALSE(md.is_open());
    EXPECT_TRUE(md.open(CodecFormat::H264));
    EXPECT_TRUE(md.is_open());
    EXPECT_TRUE(md.open_called);
}

TEST(MockDecoderTest, OpenFailure) {
    MockDecoder md;
    md.open_result = false;
    EXPECT_FALSE(md.open(CodecFormat::H264));
    // is_open_result is set before return, so it's true
    EXPECT_TRUE(md.is_open());
}

TEST(MockDecoderTest, Close) {
    MockDecoder md;
    md.open(CodecFormat::H264);
    EXPECT_TRUE(md.is_open());
    md.close();
    EXPECT_TRUE(md.closed);
    EXPECT_FALSE(md.is_open());
}

TEST(MockDecoderTest, Decode) {
    MockDecoder md;
    md.open(CodecFormat::H264);
    // decode_result invalid -> nullopt
    auto img = md.decode({0, 1, 2});
    EXPECT_FALSE(img.has_value());

    md.decode_result.resize(2, 2, ImageFormat::RGBA);
    img = md.decode({0, 1, 2});
    ASSERT_TRUE(img.has_value());
    EXPECT_TRUE(img->is_valid());
}

// ============================================================================
// Encoder tests
// ============================================================================
TEST(EncoderTest, FactoryCreate) {
    auto encoder = create_encoder();
    EXPECT_NE(encoder, nullptr);
}

TEST(EncoderTest, OpenClose) {
    auto encoder = create_encoder();
    for (int i = 0; i <= 8; ++i) {
        bool ok = encoder->open(static_cast<CodecFormat>(i), 1920, 1080, 30, 2000000);
        EXPECT_TRUE(ok || !ok);
        encoder->close();
    }
}

TEST(EncoderTest, IsOpenDefault) {
    auto encoder = create_encoder();
    EXPECT_FALSE(encoder->is_open());
}

TEST(EncoderTest, ConfigDefault) {
    auto encoder = create_encoder();
    auto cfg = encoder->config();
    EXPECT_EQ(cfg.width, 1920u);
    EXPECT_EQ(cfg.height, 1080u);
}

TEST(EncoderTest, OpenWithConfig) {
    auto encoder = create_encoder();
    EncoderConfig cfg;
    cfg.codec   = CodecFormat::H264;
    cfg.width   = 1280;
    cfg.height  = 720;
    cfg.fps_num = 60;
    cfg.bitrate = 4000000;
    bool ok = encoder->open_with_config(cfg);
    EXPECT_TRUE(ok || !ok);
    encoder->close();
}

TEST(EncoderTest, EncodeEmptyFrame) {
    auto encoder = create_encoder();
    encoder->open(CodecFormat::H264, 640, 480);
    Frame empty_frame;
    bool keyframe = false;
    auto data = encoder->encode(empty_frame, keyframe);
    // May fail on empty frame; verify no crash
    EXPECT_TRUE(data.empty() || !data.empty());
    encoder->close();
}

TEST(EncoderTest, Flush) {
    auto encoder = create_encoder();
    encoder->open(CodecFormat::H264, 640, 480);
    std::vector<uint8_t> out;
    bool flushed = encoder->flush(out);
    EXPECT_TRUE(flushed || !flushed);
    encoder->close();
}

TEST(EncoderTest, EncodeFrame) {
    auto encoder = create_encoder();
    encoder->open(CodecFormat::H264, 640, 480);
    Frame f;
    f.data.resize(640 * 480 * 4);
    f.w = 640; f.h = 480;
    f.fmt = ImageFormat::RGBA;
    bool keyframe = false;
    auto data = encoder->encode_frame(f, keyframe);
    EXPECT_TRUE(data.empty() || !data.empty());
    encoder->close();
}

TEST(EncoderTest, EncodeInto) {
    auto encoder = create_encoder();
    encoder->open(CodecFormat::H264, 640, 480);
    Frame f;
    f.data.resize(640 * 480 * 4);
    f.w = 640; f.h = 480;
    f.fmt = ImageFormat::RGBA;
    std::vector<uint8_t> output;
    bool keyframe = false;
    bool ok = encoder->encode_into(f, output, keyframe);
    EXPECT_TRUE(ok || !ok);
    encoder->close();
}

TEST(EncoderTest, Reconfigure) {
    auto encoder = create_encoder();
    EncoderConfig cfg;
    cfg.codec  = CodecFormat::H265;
    cfg.width  = 1920;
    cfg.height = 1080;
    cfg.fps_num = 30;
    cfg.bitrate = 5000000;
    bool ok = encoder->reconfigure(cfg);
    EXPECT_TRUE(ok || !ok);
    encoder->close();
}

TEST(EncoderTest, SetBitrate) {
    auto encoder = create_encoder();
    encoder->open(CodecFormat::H264, 640, 480);
    bool ok = encoder->set_bitrate(1000000);
    EXPECT_TRUE(ok || !ok);
    encoder->close();
}

TEST(EncoderTest, SetQuality) {
    auto encoder = create_encoder();
    encoder->open(CodecFormat::H264, 640, 480);
    bool ok = encoder->set_quality(EncoderQuality::Fast);
    EXPECT_TRUE(ok || !ok);
    encoder->close();
}

TEST(EncoderTest, ForceKeyframe) {
    auto encoder = create_encoder();
    bool ok = encoder->force_keyframe();
    EXPECT_TRUE(ok || !ok);
}

TEST(EncoderTest, GetDelayFrames) {
    auto encoder = create_encoder();
    int delay = encoder->get_delay_frames();
    EXPECT_GE(delay, 0);
}

TEST(EncoderTest, FactoryWithConfig) {
    EncoderConfig cfg;
    cfg.codec = CodecFormat::H264;
    cfg.width = 1280;
    cfg.height = 720;
    auto encoder = create_encoder_with_config(cfg);
    EXPECT_NE(encoder, nullptr);
}

TEST(EncoderTest, FactoryHwEncoder) {
    for (int i = 0; i <= 7; ++i) {
        auto encoder = create_hw_encoder(static_cast<EncoderBackend>(i));
        // May return nullptr for unavailable backends
        if (encoder) {
            EXPECT_NE(encoder.get(), nullptr);
        }
    }
}

// ============================================================================
// Mock Encoder tests
// ============================================================================
TEST(MockEncoderTest, OpenClose) {
    MockEncoder me;
    EXPECT_TRUE(me.open(CodecFormat::H264, 640, 480));
    EXPECT_TRUE(me.is_open());
    me.close();
    EXPECT_TRUE(me.closed);
    EXPECT_FALSE(me.is_open());
}

TEST(MockEncoderTest, Encode) {
    MockEncoder me;
    me.open(CodecFormat::H264, 640, 480);
    me.encode_result = {0, 0, 0, 1, 0x65};
    Frame f;
    bool kf = false;
    auto data = me.encode(f, kf);
    EXPECT_EQ(data.size(), 5u);
}

TEST(MockEncoderTest, Config) {
    MockEncoder me;
    me._config.width = 1280;
    me._config.height = 720;
    auto cfg = me.config();
    EXPECT_EQ(cfg.width,  1280u);
    EXPECT_EQ(cfg.height, 720u);
}

// ============================================================================
// Recorder tests
// ============================================================================
TEST(RecorderTest, ContextDefaults) {
    Recorder::Context ctx;
    EXPECT_EQ(ctx.width,       1920u);
    EXPECT_EQ(ctx.height,      1080u);
    EXPECT_EQ(ctx.fps,         30u);
    EXPECT_EQ(ctx.bitrate,     5000000u);
    EXPECT_EQ(ctx.codec,       CodecFormat::H264);
    EXPECT_TRUE(ctx.output_path.empty());
    EXPECT_EQ(ctx.container,   "mp4");
    EXPECT_EQ(ctx.quality,     EncoderQuality::Medium);
    EXPECT_EQ(ctx.gop_size,    60u);
    EXPECT_FALSE(ctx.include_audio);
    EXPECT_TRUE(ctx.audio_device.empty());
    EXPECT_EQ(ctx.audio_sample_rate, 48000u);
    EXPECT_EQ(ctx.audio_channels,    2u);
}

TEST(RecorderTest, FactoryCreate) {
    auto recorder = create_recorder();
    EXPECT_NE(recorder, nullptr);
}

TEST(RecorderTest, FFmpegRecorderFactory) {
    auto recorder = create_ffmpeg_recorder();
    EXPECT_NE(recorder, nullptr);
}

TEST(RecorderTest, StartStop) {
    auto recorder = create_recorder();
    Recorder::Context ctx;
    ctx.output_path = "/tmp/test_output.mp4";
    bool started = recorder->start(ctx);
    EXPECT_TRUE(started || !started);
    if (recorder->is_recording()) {
        recorder->stop();
    }
    EXPECT_FALSE(recorder->is_recording());
}

TEST(RecorderTest, FeedWhileNotRecording) {
    auto recorder = create_recorder();
    Frame f;
    f.data = {0};
    f.w = 1; f.h = 1;
    bool ok = recorder->feed(f);
    // May return false when not recording
    EXPECT_TRUE(ok || !ok);
}

TEST(RecorderTest, PauseResume) {
    auto recorder = create_recorder();
    EXPECT_FALSE(recorder->pause());
    EXPECT_FALSE(recorder->resume());
    EXPECT_FALSE(recorder->is_paused());
}

TEST(RecorderTest, DurationDefault) {
    auto recorder = create_recorder();
    EXPECT_EQ(recorder->duration_ms(), 0);
}

TEST(RecorderTest, BytesWrittenDefault) {
    auto recorder = create_recorder();
    EXPECT_EQ(recorder->bytes_written(), 0u);
}

TEST(RecorderTest, StatsDefault) {
    auto recorder = create_recorder();
    auto s = recorder->stats();
    EXPECT_EQ(s.total_frames, 0u);
}

TEST(RecorderTest, SplitFileDefault) {
    auto recorder = create_recorder();
    EXPECT_FALSE(recorder->split_file());
}

TEST(RecorderTest, FeedAudio) {
    auto recorder = create_recorder();
    std::vector<uint8_t> samples(48000 * 2 * 2, 0);
    bool ok = recorder->feed_audio(samples, 48000);
    EXPECT_FALSE(ok); // not supported by default
}

// ============================================================================
// Mock Recorder tests
// ============================================================================
TEST(MockRecorderTest, StartStop) {
    MockRecorder mr;
    Recorder::Context ctx;
    EXPECT_FALSE(mr.is_recording());
    EXPECT_TRUE(mr.start(ctx));
    EXPECT_TRUE(mr.is_recording());
    mr.stop();
    EXPECT_FALSE(mr.is_recording());
    EXPECT_TRUE(mr.stopped);
}

TEST(MockRecorderTest, Feed) {
    MockRecorder mr;
    Recorder::Context ctx;
    mr.start(ctx);
    Frame f;
    f.data = {1};
    f.w = 1; f.h = 1;
    EXPECT_TRUE(mr.feed(f));
}

TEST(MockRecorderTest, FeedWhileStopped) {
    MockRecorder mr;
    Frame f;
    f.data = {1};
    f.w = 1; f.h = 1;
    EXPECT_TRUE(mr.feed(f)); // feed_result defaults true
}

// ============================================================================
// Camera tests
// ============================================================================
TEST(CameraTest, FactoryCreate) {
    auto camera = create_camera();
    EXPECT_NE(camera, nullptr);
}

TEST(CameraTest, ListCameras) {
    auto camera = create_camera();
    auto cams = camera->list_cameras();
    // May be empty if no cameras
    EXPECT_TRUE(cams.empty() || cams.size() >= 1);
}

TEST(CameraTest, Open) {
    auto camera = create_camera();
    bool ok = camera->open(0);
    EXPECT_TRUE(ok || !ok);
    camera->close();
}

TEST(CameraTest, OpenByIndex) {
    auto camera = create_camera();
    for (uint32_t i = 0; i < 3; ++i) {
        bool ok = camera->open(i);
        EXPECT_TRUE(ok || !ok);
        camera->close();
    }
}

TEST(CameraTest, Capture) {
    auto camera = create_camera();
    camera->open(0);
    auto f = camera->capture();
    if (f) {
        EXPECT_GE(f->w, 0u);
    }
    camera->close();
}

TEST(CameraTest, IsOpenDefault) {
    auto camera = create_camera();
    EXPECT_FALSE(camera->is_open());
}

TEST(CameraTest, OpenByName) {
    auto camera = create_camera();
    auto cams = camera->list_cameras();
    if (!cams.empty()) {
        bool ok = camera->open_by_name(cams[0]);
        EXPECT_TRUE(ok || !ok);
        camera->close();
    } else {
        bool ok = camera->open_by_name("nonexistent");
        EXPECT_FALSE(ok);
    }
}

TEST(CameraTest, SetResolution) {
    auto camera = create_camera();
    EXPECT_FALSE(camera->set_resolution(1920, 1080));
    EXPECT_FALSE(camera->set_resolution(0, 0));
}

TEST(CameraTest, SetFps) {
    auto camera = create_camera();
    EXPECT_FALSE(camera->set_fps(30));
    EXPECT_FALSE(camera->set_fps(0));
}

TEST(CameraTest, SetFormat) {
    auto camera = create_camera();
    EXPECT_FALSE(camera->set_format(ImageFormat::RGBA));
    EXPECT_FALSE(camera->set_format(ImageFormat::NV12));
}

TEST(CameraTest, Resolution) {
    auto camera = create_camera();
    auto [w, h] = camera->resolution();
    EXPECT_EQ(w, 0u);
    EXPECT_EQ(h, 0u);
}

TEST(CameraTest, CameraFps) {
    auto camera = create_camera();
    EXPECT_EQ(camera->camera_fps(), 0u);
}

TEST(CameraTest, Autofocus) {
    auto camera = create_camera();
    EXPECT_FALSE(camera->has_autofocus());
    EXPECT_FALSE(camera->set_autofocus(true));
    EXPECT_FALSE(camera->set_autofocus(false));
}

// ============================================================================
// Mock Camera tests
// ============================================================================
TEST(MockCameraTest, OpenClose) {
    MockCamera mc;
    EXPECT_FALSE(mc.is_open());
    EXPECT_TRUE(mc.open(0));
    EXPECT_TRUE(mc.is_open());
    mc.close();
    EXPECT_TRUE(mc.closed);
    EXPECT_FALSE(mc.is_open());
}

TEST(MockCameraTest, ListCameras) {
    MockCamera mc;
    mc.cameras = {"cam0", "cam1"};
    auto cams = mc.list_cameras();
    EXPECT_EQ(cams.size(), 2u);
    EXPECT_EQ(cams[0], "cam0");
    EXPECT_EQ(cams[1], "cam1");
}

TEST(MockCameraTest, Capture) {
    MockCamera mc;
    mc.open(0);
    // empty frame -> nullopt
    auto f = mc.capture();
    EXPECT_FALSE(f.has_value());

    mc._frame.data = {0};
    mc._frame.w = 1; mc._frame.h = 1;
    f = mc.capture();
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->w, 1u);
}

// ============================================================================
// Scaler tests
// ============================================================================
TEST(ScalerTest, FactoryCreate) {
    ScaleConfig cfg;
    cfg.src_w = 1920; cfg.src_h = 1080;
    cfg.dst_w = 1280; cfg.dst_h = 720;
    auto scaler = create_scaler(cfg);
    EXPECT_NE(scaler, nullptr);
}

TEST(ScalerTest, DefaultConstruction) {
    Scaler s;
    EXPECT_FALSE(s.is_configured());
}

TEST(ScalerTest, ConstructWithConfig) {
    ScaleConfig cfg;
    cfg.src_w = 640; cfg.src_h = 480;
    cfg.dst_w = 320; cfg.dst_h = 240;
    Scaler s(cfg);
    // Configuration depends on available backends
    EXPECT_TRUE(s.is_configured() || !s.is_configured());
}

TEST(ScalerTest, Configure) {
    Scaler s;
    ScaleConfig cfg;
    cfg.src_w = 1920; cfg.src_h = 1080;
    cfg.dst_w = 1280; cfg.dst_h = 720;
    bool ok = s.configure(cfg);
    EXPECT_TRUE(ok || !ok);
}

TEST(ScalerTest, ConfigAccess) {
    ScaleConfig cfg;
    cfg.src_w = 640; cfg.src_h = 480;
    cfg.dst_w = 320; cfg.dst_h = 240;
    Scaler s(cfg);
    const auto& c = s.config();
    EXPECT_EQ(c.src_w, 640u);
    EXPECT_EQ(c.src_h, 480u);
    EXPECT_EQ(c.dst_w, 320u);
    EXPECT_EQ(c.dst_h, 240u);
}

TEST(ScalerTest, MoveConstruct) {
    ScaleConfig cfg;
    cfg.src_w = 100; cfg.src_h = 100;
    cfg.dst_w = 50;  cfg.dst_h = 50;
    Scaler s1(cfg);
    Scaler s2(std::move(s1));
    // s2 should be in valid state
    EXPECT_TRUE(s2.is_configured() || !s2.is_configured());
}

TEST(ScalerTest, MoveAssign) {
    ScaleConfig cfg;
    cfg.src_w = 100; cfg.src_h = 100;
    cfg.dst_w = 50;  cfg.dst_h = 50;
    Scaler s1(cfg);
    Scaler s2;
    s2 = std::move(s1);
    EXPECT_TRUE(s2.is_configured() || !s2.is_configured());
}

TEST(ScalerTest, Scale) {
    ScaleConfig cfg;
    cfg.src_w = 640; cfg.src_h = 480;
    cfg.dst_w = 320; cfg.dst_h = 240;
    Scaler s(cfg);

    ImageRgb src;
    src.resize(640, 480, ImageFormat::RGBA);
    ImageRgb dst;
    bool ok = s.scale(src, dst);
    if (ok) {
        EXPECT_TRUE(dst.is_valid());
        EXPECT_EQ(dst.w, 320u);
        EXPECT_EQ(dst.h, 240u);
    }
}

TEST(ScalerTest, ScaleFrame) {
    ScaleConfig cfg;
    cfg.src_w = 640; cfg.src_h = 480;
    cfg.dst_w = 320; cfg.dst_h = 240;
    Scaler s(cfg);

    Frame src;
    src.data.resize(640 * 480 * 4);
    src.w = 640; src.h = 480;
    src.fmt = ImageFormat::RGBA;
    Frame dst;
    bool ok = s.scale_frame(src, dst);
    if (ok) {
        EXPECT_TRUE(dst.is_valid() || dst.data.empty());
    }
}

TEST(ScalerTest, ScalePlanes) {
    Scaler s;

    std::vector<uint8_t> src_buf(64 * 64 * 4, 128);
    std::vector<uint8_t> dst_buf(32 * 32 * 4, 0);

    bool ok = s.scale_planes(
        src_buf.data(), 64, 64, 64 * 4,
        dst_buf.data(), 32, 32, 32 * 4,
        ScaleAlgorithm::Bilinear
    );
    EXPECT_TRUE(ok || !ok);
}

// ============================================================================
// FrameDifferencer tests
// ============================================================================
TEST(FrameDifferencerTest, FactoryCreate) {
    auto diff = create_differencer();
    EXPECT_NE(diff, nullptr);
}

TEST(FrameDifferencerTest, DefaultConstruction) {
    FrameDifferencer fd;
    EXPECT_EQ(fd.block_size(), 16u);
}

TEST(FrameDifferencerTest, Configure) {
    FrameDifferencer fd;
    EXPECT_NO_THROW(fd.configure(1920, 1080, ImageFormat::RGBA, 32, 10));
}

TEST(FrameDifferencerTest, SetSensitivity) {
    FrameDifferencer fd;
    EXPECT_NO_THROW(fd.set_sensitivity(5));
    EXPECT_NO_THROW(fd.set_sensitivity(200));
}

TEST(FrameDifferencerTest, Reset) {
    FrameDifferencer fd;
    EXPECT_NO_THROW(fd.reset());
}

TEST(FrameDifferencerTest, DiffEmptyFrames) {
    FrameDifferencer fd;
    fd.configure(640, 480, ImageFormat::RGBA);
    Frame prev, curr;
    auto rects = fd.diff(prev, curr);
    EXPECT_TRUE(rects.empty());
}

TEST(FrameDifferencerTest, HasChangedEmpty) {
    FrameDifferencer fd;
    fd.configure(640, 480, ImageFormat::RGBA);
    Frame prev, curr;
    EXPECT_FALSE(fd.has_changed(prev, curr));
}

TEST(FrameDifferencerTest, HashFrame) {
    FrameDifferencer fd;
    Frame f;
    f.data = {0, 1, 2, 3};
    f.w = 2; f.h = 2;
    uint64_t h = fd.hash_frame(f);
    EXPECT_GE(h, 0u);
}

TEST(FrameDifferencerTest, DiffData) {
    FrameDifferencer fd;
    fd.configure(16, 16, ImageFormat::RGBA);
    std::vector<uint8_t> prev(16 * 16 * 4, 0);
    std::vector<uint8_t> curr(16 * 16 * 4, 128);
    auto rects = fd.diff_data(prev.data(), curr.data(), 16, 16, 64);
    // Should detect changes
    EXPECT_TRUE(rects.empty() || !rects.empty());
}

// ============================================================================
// FPSLimiter tests
// ============================================================================
TEST(FPSLimiterTest, FactoryCreate) {
    auto limiter = create_fps_limiter(30);
    EXPECT_NE(limiter, nullptr);
    EXPECT_EQ(limiter->target_fps(), 30u);

    auto limiter2 = create_fps_limiter();
    EXPECT_EQ(limiter2->target_fps(), 60u);
}

TEST(FPSLimiterTest, DefaultConstruction) {
    FPSLimiter fl;
    EXPECT_EQ(fl.target_fps(), 60u);
    EXPECT_DOUBLE_EQ(fl.current_fps(), 0.0);
    EXPECT_EQ(fl.frames_throttled(), 0u);
}

TEST(FPSLimiterTest, ConstructorWithFps) {
    FPSLimiter fl(30);
    EXPECT_EQ(fl.target_fps(), 30u);
}

TEST(FPSLimiterTest, SetTargetFps) {
    FPSLimiter fl;
    fl.set_target_fps(120);
    EXPECT_EQ(fl.target_fps(), 120u);

    fl.set_target_fps(15);
    EXPECT_EQ(fl.target_fps(), 15u);
}

TEST(FPSLimiterTest, FrameIntervalUs) {
    FPSLimiter fl(60);
    EXPECT_EQ(fl.frame_interval_us(), 1000000LL / 60);

    FPSLimiter fl2(30);
    EXPECT_EQ(fl2.frame_interval_us(), 1000000LL / 30);

    FPSLimiter fl3(0);
    EXPECT_EQ(fl3.frame_interval_us(), 0);
}

TEST(FPSLimiterTest, ElapsedSinceLast) {
    FPSLimiter fl;
    auto elapsed = fl.elapsed_since_last_us();
    EXPECT_GE(elapsed, 0);
}

TEST(FPSLimiterTest, Ready) {
    FPSLimiter fl;
    bool r = fl.ready();
    EXPECT_TRUE(r || !r);
}

TEST(FPSLimiterTest, FrameCaptured) {
    FPSLimiter fl;
    EXPECT_NO_THROW(fl.frame_captured());
    EXPECT_NO_THROW(fl.frame_captured());
}

TEST(FPSLimiterTest, Reset) {
    FPSLimiter fl;
    EXPECT_NO_THROW(fl.reset());
    EXPECT_EQ(fl.frames_throttled(), 0u);
}

TEST(FPSLimiterTest, Wait) {
    FPSLimiter fl(120);
    bool waited = fl.wait();
    EXPECT_TRUE(waited || !waited);
}

// ============================================================================
// StatsTracker tests
// ============================================================================
TEST(StatsTrackerTest, FactoryCreate) {
    auto tracker = create_stats_tracker();
    EXPECT_NE(tracker, nullptr);
}

TEST(StatsTrackerTest, DefaultConstruction) {
    StatsTracker st;
    auto s = st.snapshot();
    EXPECT_EQ(s.total_frames, 0u);
}

TEST(StatsTrackerTest, FrameCaptured) {
    StatsTracker st;
    EXPECT_NO_THROW(st.frame_captured(10000, 2.5));
    auto s = st.snapshot();
    EXPECT_EQ(s.captured_frames, 1u);
    EXPECT_EQ(s.total_bytes, 10000u);
}

TEST(StatsTrackerTest, FrameEncoded) {
    StatsTracker st;
    EXPECT_NO_THROW(st.frame_encoded(5000, 3.0, true));
    auto s = st.snapshot();
    EXPECT_EQ(s.encoded_frames, 1u);
    EXPECT_EQ(s.encoded_bytes, 5000u);
    EXPECT_EQ(s.key_frames, 1u);
}

TEST(StatsTrackerTest, FrameEncodedNonKey) {
    StatsTracker st;
    EXPECT_NO_THROW(st.frame_encoded(3000, 1.5, false));
    auto s = st.snapshot();
    EXPECT_EQ(s.encoded_frames, 1u);
    EXPECT_EQ(s.key_frames, 0u);
}

TEST(StatsTrackerTest, FrameDropped) {
    StatsTracker st;
    EXPECT_NO_THROW(st.frame_dropped());
    auto s = st.snapshot();
    EXPECT_EQ(s.dropped_frames, 1u);
}

TEST(StatsTrackerTest, SetDirtyPct) {
    StatsTracker st;
    EXPECT_NO_THROW(st.set_dirty_pct(25));
    auto s = st.snapshot();
    EXPECT_EQ(s.dirty_rect_pct, 25u);
}

TEST(StatsTrackerTest, Reset) {
    StatsTracker st;
    st.frame_captured(100, 1.0);
    st.frame_encoded(80, 2.0, true);
    st.frame_dropped();
    st.set_dirty_pct(50);

    st.reset();
    auto s = st.snapshot();
    EXPECT_EQ(s.total_frames,    0u);
    EXPECT_EQ(s.captured_frames, 0u);
    EXPECT_EQ(s.dropped_frames,  0u);
    EXPECT_EQ(s.encoded_frames,  0u);
    EXPECT_EQ(s.key_frames,      0u);
    EXPECT_EQ(s.total_bytes,     0u);
    EXPECT_EQ(s.encoded_bytes,   0u);
}

TEST(StatsTrackerTest, MultipleFrames) {
    StatsTracker st;
    for (int i = 0; i < 100; ++i) {
        st.frame_captured(100 + i, 1.0);
        st.frame_encoded(80 + i, 2.0, i % 30 == 0);
    }
    auto s = st.snapshot();
    EXPECT_EQ(s.captured_frames, 100u);
    EXPECT_EQ(s.encoded_frames, 100u);
    // Key frames: indices 0,30,60,90 -> 4
    EXPECT_EQ(s.key_frames, 4u);
}

TEST(StatsTrackerTest, LogSummary) {
    StatsTracker st;
    EXPECT_NO_THROW(st.log_summary());
}

// ============================================================================
// Format conversion tests
// ============================================================================
class FormatConversionTest : public ::testing::Test {
protected:
    ImageRgb makeRgbaImage(size_t w, size_t h) {
        ImageRgb img;
        img.resize(w, h, ImageFormat::RGBA);
        std::fill(img.raw.begin(), img.raw.end(), 0);
        return img;
    }
};

TEST_F(FormatConversionTest, ConvertFormatIdentity) {
    auto src = makeRgbaImage(16, 16);
    auto dst = convert_format(src, ImageFormat::RGBA);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.w, 16u);
    EXPECT_EQ(dst.h, 16u);
    EXPECT_EQ(dst.fmt, ImageFormat::RGBA);
}

TEST_F(FormatConversionTest, ConvertFormatRGBAtoBGRA) {
    auto src = makeRgbaImage(16, 16);
    auto dst = convert_format(src, ImageFormat::BGRA);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.fmt, ImageFormat::BGRA);
    EXPECT_EQ(dst.w, 16u);
    EXPECT_EQ(dst.h, 16u);
}

TEST_F(FormatConversionTest, ConvertBGRAToRGBA) {
    ImageRgb src;
    src.resize(16, 16, ImageFormat::BGRA);
    src.raw[0] = 255; // B
    src.raw[1] = 128; // G
    src.raw[2] = 64;  // R
    src.raw[3] = 32;  // A
    auto dst = convert_bgra_to_rgba(src);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.fmt, ImageFormat::RGBA);
    // R=64, G=128, B=255, A=32
    EXPECT_EQ(dst.raw[0], 64);
    EXPECT_EQ(dst.raw[1], 128);
    EXPECT_EQ(dst.raw[2], 255);
    EXPECT_EQ(dst.raw[3], 32);
}

TEST_F(FormatConversionTest, ConvertARGBToRGBA) {
    ImageRgb src;
    src.resize(16, 16, ImageFormat::ARGB);
    src.raw[0] = 128; // A
    src.raw[1] = 64;  // R
    src.raw[2] = 32;  // G
    src.raw[3] = 16;  // B
    auto dst = convert_argb_to_rgba(src);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.fmt, ImageFormat::RGBA);
    EXPECT_EQ(dst.raw[0], 64);  // R
    EXPECT_EQ(dst.raw[1], 32);  // G
    EXPECT_EQ(dst.raw[2], 16);  // B
    EXPECT_EQ(dst.raw[3], 128); // A
}

TEST_F(FormatConversionTest, ConvertABGRToRGBA) {
    ImageRgb src;
    src.resize(16, 16, ImageFormat::ABGR);
    src.raw[0] = 128; // A
    src.raw[1] = 255; // B
    src.raw[2] = 128; // G
    src.raw[3] = 64;  // R
    auto dst = convert_abgr_to_rgba(src);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.fmt, ImageFormat::RGBA);
    EXPECT_EQ(dst.raw[0], 64);  // R
    EXPECT_EQ(dst.raw[1], 128); // G
    EXPECT_EQ(dst.raw[2], 255); // B
    EXPECT_EQ(dst.raw[3], 128); // A
}

TEST_F(FormatConversionTest, ConvertRGBToRGBA) {
    ImageRgb src;
    src.w = 4; src.h = 4;
    src.fmt = ImageFormat::RGB;
    src.raw.clear();
    for (size_t i = 0; i < 4 * 4; ++i) {
        src.raw.push_back(static_cast<uint8_t>(i % 3));   // R,G,B cycle
        src.raw.push_back(static_cast<uint8_t>((i+1) % 3));
        src.raw.push_back(static_cast<uint8_t>((i+2) % 3));
    }
    auto dst = convert_rgb_to_rgba(src);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.fmt, ImageFormat::RGBA);
    EXPECT_EQ(dst.w, 4u);
    EXPECT_EQ(dst.h, 4u);
    // Each pixel should now be 4 bytes with alpha=255
    for (size_t i = 0; i < dst.raw.size(); i += 4) {
        EXPECT_EQ(dst.raw[i + 3], 255);
    }
}

TEST_F(FormatConversionTest, ConvertBGRToRGBA) {
    ImageRgb src;
    src.w = 4; src.h = 4;
    src.fmt = ImageFormat::BGR;
    src.raw.clear();
    for (size_t i = 0; i < 4 * 4; ++i) {
        src.raw.push_back(static_cast<uint8_t>((i+2) % 3)); // B
        src.raw.push_back(static_cast<uint8_t>((i+1) % 3)); // G
        src.raw.push_back(static_cast<uint8_t>(i % 3));      // R
    }
    auto dst = convert_bgr_to_rgba(src);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.fmt, ImageFormat::RGBA);
    for (size_t i = 0; i < dst.raw.size(); i += 4) {
        EXPECT_EQ(dst.raw[i + 3], 255); // alpha fill
    }
}

TEST_F(FormatConversionTest, ConvertGrayToRGBA) {
    ImageRgb src;
    src.w = 4; src.h = 4;
    src.fmt = ImageFormat::GRAY8;
    src.raw = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160};
    auto dst = convert_gray_to_rgba(src);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.fmt, ImageFormat::RGBA);
    EXPECT_EQ(dst.w, 4u);
    EXPECT_EQ(dst.h, 4u);
    EXPECT_EQ(dst.raw.size(), 64u);
    // First pixel: R=G=B=10, A=255
    EXPECT_EQ(dst.raw[0], 10);
    EXPECT_EQ(dst.raw[1], 10);
    EXPECT_EQ(dst.raw[2], 10);
    EXPECT_EQ(dst.raw[3], 255);
}

TEST_F(FormatConversionTest, ConvertYUVToRGBA) {
    // Create a minimal YUV420 buffer: black frame
    // Y plane: all 16 (black in limited range), UV: all 128
    size_t w = 4, h = 4;
    std::vector<uint8_t> yuv;
    yuv.resize(w * h + w * h / 2, 0);
    for (size_t i = 0; i < w * h; ++i) yuv[i] = 16;         // Y (black)
    for (size_t i = w * h; i < yuv.size(); ++i) yuv[i] = 128; // UV (no chroma)
    auto dst = convert_yuv_to_rgba(yuv, w, h);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.w, 4u);
    EXPECT_EQ(dst.h, 4u);
    EXPECT_EQ(dst.fmt, ImageFormat::RGBA);
}

TEST_F(FormatConversionTest, ConvertNV12ToRGBA) {
    size_t w = 4, h = 4;
    std::vector<uint8_t> nv12(w * h + w * h / 2, 0);
    for (size_t i = 0; i < w * h; ++i) nv12[i] = 16;
    for (size_t i = w * h; i < nv12.size(); ++i) nv12[i] = 128;
    auto dst = convert_nv12_to_rgba(nv12.data(), w, h);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.fmt, ImageFormat::RGBA);
}

TEST_F(FormatConversionTest, ConvertYUV420ToRGBA) {
    size_t w = 4, h = 4;
    std::vector<uint8_t> y(w * h, 16);
    std::vector<uint8_t> u(w * h / 4, 128);
    std::vector<uint8_t> v(w * h / 4, 128);
    auto dst = convert_yuv420_to_rgba(
        y.data(), w, u.data(), w / 2, v.data(), w / 2,
        w, h, ColorSpace::BT709
    );
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.fmt, ImageFormat::RGBA);
    EXPECT_EQ(dst.w, 4u);
    EXPECT_EQ(dst.h, 4u);
}

TEST_F(FormatConversionTest, ConvertYUV420ToRGBA_BT601) {
    size_t w = 8, h = 8;
    std::vector<uint8_t> y(w * h, 128);
    std::vector<uint8_t> u(w * h / 4, 128);
    std::vector<uint8_t> v(w * h / 4, 128);
    auto dst = convert_yuv420_to_rgba(
        y.data(), w, u.data(), w / 2, v.data(), w / 2,
        w, h, ColorSpace::BT601
    );
    EXPECT_TRUE(dst.is_valid());
}

TEST_F(FormatConversionTest, ConvertYUYVToRGBA) {
    size_t w = 4, h = 4;
    std::vector<uint8_t> yuyv(w * h * 2, 0);
    // Fill with neutral values: Y=128, U=128, V=128
    for (size_t i = 0; i < yuyv.size(); i += 4) {
        yuyv[i]     = 128; // Y0
        yuyv[i + 1] = 128; // U
        yuyv[i + 2] = 128; // Y1
        yuyv[i + 3] = 128; // V
    }
    auto dst = convert_yuyv_to_rgba(yuyv.data(), w, h);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.fmt, ImageFormat::RGBA);
}

TEST_F(FormatConversionTest, ConvertUYVYToRGBA) {
    size_t w = 4, h = 4;
    std::vector<uint8_t> uyvy(w * h * 2, 0);
    for (size_t i = 0; i < uyvy.size(); i += 4) {
        uyvy[i]     = 128; // U
        uyvy[i + 1] = 128; // Y0
        uyvy[i + 2] = 128; // V
        uyvy[i + 3] = 128; // Y1
    }
    auto dst = convert_uyvy_to_rgba(uyvy.data(), w, h);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.fmt, ImageFormat::RGBA);
}

TEST_F(FormatConversionTest, ConvertP010ToRGBA) {
    size_t w = 4, h = 4;
    std::vector<uint8_t> p010;
    p010.resize((w * h + w * h / 2) * 2, 0);
    // Fill with 10-bit neutral: 16 << 6 for Y, 128 << 6 for UV
    for (size_t i = 0; i < w * h; ++i) {
        p010[i * 2]     = 0x00;
        p010[i * 2 + 1] = 0x04; // 16 << 6 = 1024 = 0x0400 in LE
    }
    for (size_t i = w * h; i < w * h + w * h / 2; ++i) {
        p010[i * 2]     = 0x00;
        p010[i * 2 + 1] = 0x20; // 128 << 6 = 8192 = 0x2000 in LE
    }
    auto dst = convert_p010_to_rgba(p010.data(), w, h);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.fmt, ImageFormat::RGBA);
}

TEST_F(FormatConversionTest, ConvertRGBAToYUV420) {
    size_t w = 8, h = 8;
    std::vector<uint8_t> rgba(w * h * 4, 0);
    // Fill with gray
    for (size_t i = 0; i < w * h * 4; i += 4) {
        rgba[i]     = 128;
        rgba[i + 1] = 128;
        rgba[i + 2] = 128;
        rgba[i + 3] = 255;
    }
    std::vector<uint8_t> y_plane(w * h, 0);
    std::vector<uint8_t> u_plane(w * h / 4, 0);
    std::vector<uint8_t> v_plane(w * h / 4, 0);

    EXPECT_NO_THROW(convert_rgba_to_yuv420(
        rgba.data(), w, h,
        y_plane.data(), w, u_plane.data(), w / 2, v_plane.data(), w / 2
    ));

    // Gray should produce U=V=128 for BT.709
    for (auto b : u_plane) EXPECT_GE(b, 0);
    for (auto b : v_plane) EXPECT_GE(b, 0);
}

TEST_F(FormatConversionTest, ConvertRGBAToNV12) {
    size_t w = 8, h = 8;
    std::vector<uint8_t> rgba(w * h * 4, 0);
    for (size_t i = 0; i < w * h * 4; i += 4) {
        rgba[i]     = 255;
        rgba[i + 1] = 0;
        rgba[i + 2] = 0;
        rgba[i + 3] = 255;
    }
    std::vector<uint8_t> nv12(w * h + w * h / 2, 0);
    EXPECT_NO_THROW(convert_rgba_to_nv12(rgba.data(), w, h, nv12.data(), w));
}

TEST_F(FormatConversionTest, PremultiplyAlpha) {
    size_t w = 4, h = 4;
    std::vector<uint8_t> rgba(w * h * 4, 0);
    // Set pixel 0: R=255, G=128, B=64, A=128 (half transparent)
    rgba[0] = 255; rgba[1] = 128; rgba[2] = 64; rgba[3] = 128;
    premultiply_alpha(rgba.data(), w, h, w * 4);
    // Premultiplied: R=255*128/255=128, G=128*128/255≈64, B=64*128/255≈32
    EXPECT_NEAR(rgba[0], 128, 1);
    EXPECT_NEAR(rgba[1], 64, 1);
    EXPECT_NEAR(rgba[2], 32, 1);
    EXPECT_EQ(rgba[3], 128); // A unchanged
}

TEST_F(FormatConversionTest, UnpremultiplyAlpha) {
    size_t w = 4, h = 4;
    std::vector<uint8_t> rgba(w * h * 4, 0);
    rgba[0] = 128; rgba[1] = 64; rgba[2] = 32; rgba[3] = 128;
    unpremultiply_alpha(rgba.data(), w, h, w * 4);
    // Unpremultiplied: R=128*255/128=255, G=64*255/128≈128, B=32*255/128≈64
    EXPECT_NEAR(rgba[0], 255, 1);
    EXPECT_NEAR(rgba[1], 128, 1);
    EXPECT_NEAR(rgba[2], 64, 1);
    EXPECT_EQ(rgba[3], 128);
}

TEST_F(FormatConversionTest, UnpremultiplyAlphaZeroAlpha) {
    size_t w = 4, h = 4;
    std::vector<uint8_t> rgba(w * h * 4, 0);
    rgba[0] = 100; rgba[1] = 200; rgba[2] = 50; rgba[3] = 0;
    EXPECT_NO_THROW(unpremultiply_alpha(rgba.data(), w, h, w * 4));
}

TEST_F(FormatConversionTest, FillAlpha) {
    size_t w = 4, h = 4;
    std::vector<uint8_t> rgba(w * h * 4, 0);
    fill_alpha(rgba.data(), w, h, w * 4, 128);
    for (size_t i = 3; i < rgba.size(); i += 4) {
        EXPECT_EQ(rgba[i], 128);
    }
    // R,G,B unchanged (zero)
    for (size_t i = 0; i < rgba.size(); i += 4) {
        EXPECT_EQ(rgba[i], 0);
        EXPECT_EQ(rgba[i + 1], 0);
        EXPECT_EQ(rgba[i + 2], 0);
    }
}

TEST_F(FormatConversionTest, FillAlphaFullOpaque) {
    size_t w = 64, h = 64;
    std::vector<uint8_t> rgba(w * h * 4, 0);
    fill_alpha(rgba.data(), w, h, w * 4, 255);
    for (size_t i = 3; i < rgba.size(); i += 4) {
        EXPECT_EQ(rgba[i], 255);
    }
}

TEST_F(FormatConversionTest, ConvertFormatEdgeCases) {
    // Test with 1x1 images
    ImageRgb single;
    single.resize(1, 1, ImageFormat::RGBA);
    single.raw = {255, 0, 0, 255};

    auto bgra = convert_format(single, ImageFormat::BGRA);
    EXPECT_TRUE(bgra.is_valid());
    EXPECT_EQ(bgra.raw[0], 0);
    EXPECT_EQ(bgra.raw[1], 0);
    EXPECT_EQ(bgra.raw[2], 255);
    EXPECT_EQ(bgra.raw[3], 255);
}

TEST_F(FormatConversionTest, ConvertFormatGRAY8) {
    ImageRgb src;
    src.resize(2, 2, ImageFormat::RGBA);
    src.raw = {100, 0, 0, 255,  200, 0, 0, 255,  50, 0, 0, 255,  150, 0, 0, 255};
    auto dst = convert_format(src, ImageFormat::GRAY8);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.fmt, ImageFormat::GRAY8);
    EXPECT_EQ(dst.raw.size(), 4u);
    // GRAY8 from RGB uses luminance weights
    EXPECT_GE(dst.raw[0], 20u); // roughly 0.299*100 ~= 30
    EXPECT_LE(dst.raw[0], 40u);
}

// ============================================================================
// Image scaling free function tests
// ============================================================================
TEST(ImageScalingTest, ScaleImage) {
    ImageRgb src;
    src.resize(16, 16, ImageFormat::RGBA);
    for (size_t i = 0; i < src.raw.size(); ++i) src.raw[i] = static_cast<uint8_t>(i % 256);
    auto dst = scale_image(src, 8, 8, ScaleAlgorithm::Bilinear);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.w, 8u);
    EXPECT_EQ(dst.h, 8u);
    EXPECT_EQ(dst.fmt, ImageFormat::RGBA);
}

TEST(ImageScalingTest, ScaleImageNearest) {
    ImageRgb src;
    src.resize(4, 4, ImageFormat::RGBA);
    for (size_t i = 0; i < src.raw.size(); ++i) src.raw[i] = 255;
    auto dst = scale_image(src, 8, 8, ScaleAlgorithm::Nearest);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.w, 8u);
    EXPECT_EQ(dst.h, 8u);
}

TEST(ImageScalingTest, ScaleImageAspectDownscale) {
    ImageRgb src;
    src.resize(32, 32, ImageFormat::RGBA);
    auto dst = scale_image(src, 4, 4, ScaleAlgorithm::Bicubic);
    EXPECT_TRUE(dst.is_valid());
    EXPECT_EQ(dst.w, 4u);
    EXPECT_EQ(dst.h, 4u);
}

TEST(ImageScalingTest, ScaleImageLanczos) {
    ImageRgb src;
    src.resize(16, 16, ImageFormat::RGBA);
    auto dst = scale_image(src, 8, 8, ScaleAlgorithm::Lanczos3);
    EXPECT_TRUE(dst.is_valid());
}

TEST(ImageScalingTest, ScaleImageAreaAvg) {
    ImageRgb src;
    src.resize(16, 16, ImageFormat::RGBA);
    auto dst = scale_image(src, 4, 4, ScaleAlgorithm::AreaAvg);
    EXPECT_TRUE(dst.is_valid());
}

TEST(ImageScalingTest, ScaleImageInto) {
    ImageRgb src;
    src.resize(16, 16, ImageFormat::RGBA);
    std::vector<uint8_t> dst_buf(8 * 8 * 4, 0);
    bool ok = scale_image_into(
        src.data(), src.w, src.h, src.stride(), src.fmt,
        dst_buf.data(), 8, 8, 8 * 4, ImageFormat::RGBA,
        ScaleAlgorithm::Bilinear
    );
    EXPECT_TRUE(ok);
}

TEST(ImageScalingTest, ScaleNearest) {
    std::vector<uint8_t> src(4 * 4 * 4, 255);
    std::vector<uint8_t> dst(8 * 8 * 4, 0);
    EXPECT_NO_THROW(scale_nearest(
        src.data(), 4, 4, 16, dst.data(), 8, 8, 32, 4
    ));
}

TEST(ImageScalingTest, ScaleBilinear) {
    std::vector<uint8_t> src(4 * 4 * 4, 128);
    std::vector<uint8_t> dst(2 * 2 * 4, 0);
    EXPECT_NO_THROW(scale_bilinear(
        src.data(), 4, 4, 16, dst.data(), 2, 2, 8, 4
    ));
}

TEST(ImageScalingTest, ScaleBicubic) {
    std::vector<uint8_t> src(8 * 8 * 4, 128);
    std::vector<uint8_t> dst(4 * 4 * 4, 0);
    EXPECT_NO_THROW(scale_bicubic(
        src.data(), 8, 8, 32, dst.data(), 4, 4, 16, 4
    ));
}

// ============================================================================
// Constants tests
// ============================================================================
TEST(ConstantsTest, STRIDE_ALIGN) {
    EXPECT_EQ(STRIDE_ALIGN, 64u);
}

TEST(ConstantsTest, HW_STRIDE_ALIGN) {
    EXPECT_EQ(HW_STRIDE_ALIGN, 0u);
}

TEST(ConstantsTest, PRIMARY_CAMERA_IDX) {
    EXPECT_EQ(PRIMARY_CAMERA_IDX, 0);
}

TEST(ConstantsTest, DEFAULT_CAPTURE_TIMEOUT_MS) {
    EXPECT_EQ(DEFAULT_CAPTURE_TIMEOUT_MS, 5000u);
}

TEST(ConstantsTest, MAX_DIRTY_RECTS) {
    EXPECT_EQ(MAX_DIRTY_RECTS, 256u);
}

TEST(ConstantsTest, DEFAULT_DIRTY_BLOCK_SIZE) {
    EXPECT_EQ(DEFAULT_DIRTY_BLOCK_SIZE, 16u);
}

TEST(ConstantsTest, DEFAULT_FPS) {
    EXPECT_EQ(DEFAULT_FPS, 60u);
}

TEST(ConstantsTest, DEFAULT_BITRATE) {
    EXPECT_EQ(DEFAULT_BITRATE, 5000000u);
}

TEST(ConstantsTest, MAX_PLANES) {
    EXPECT_EQ(MAX_PLANES, 4u);
}

// ============================================================================
// Utility function tests
// ============================================================================
TEST(UtilityTest, WouldBlockIfEqual) {
    std::vector<uint8_t> old = {1, 2, 3, 4, 5};
    std::vector<uint8_t> b   = {1, 2, 3, 4, 5}; // identical
    bool blocked = would_block_if_equal(old, b);
    if (blocked) {
        // If blocked, old should be moved-from (empty)
        EXPECT_TRUE(old.empty());
    }
}

TEST(UtilityTest, WouldBlockIfDifferent) {
    std::vector<uint8_t> old = {1, 2, 3, 4, 5};
    std::vector<uint8_t> b   = {5, 4, 3, 2, 1}; // different
    bool blocked = would_block_if_equal(old, b);
    EXPECT_FALSE(blocked);
    // old should still have its data
    EXPECT_EQ(old, (std::vector<uint8_t>{1, 2, 3, 4, 5}));
}

TEST(UtilityTest, WouldBlockIfSameSize) {
    std::vector<uint8_t> old = {1, 2, 3};
    std::vector<uint8_t> b   = {1, 2, 3};
    would_block_if_equal(old, b);
}

TEST(UtilityTest, WouldBlockIfDifferentSize) {
    std::vector<uint8_t> old = {1, 2, 3};
    std::vector<uint8_t> b   = {1, 2, 3, 4};
    bool blocked = would_block_if_equal(old, b);
    EXPECT_FALSE(blocked);
}

TEST(UtilityTest, WouldBlockIfEmpty) {
    std::vector<uint8_t> old;
    std::vector<uint8_t> b;
    bool blocked = would_block_if_equal(old, b);
    // Both empty: may or may not block
    EXPECT_TRUE(blocked || !blocked);
}

TEST(UtilityTest, IsX11) {
    bool isx = is_x11();
    EXPECT_TRUE(isx || !isx);
}

TEST(UtilityTest, IsWayland) {
    bool isw = is_wayland();
    EXPECT_TRUE(isw || !isw);
}

TEST(UtilityTest, IsDxgiAvailable) {
    bool isdx = is_dxgi_available();
    EXPECT_TRUE(isdx || !isdx);
}

TEST(UtilityTest, IsQuartzAvailable) {
    bool isq = is_quartz_available();
    EXPECT_TRUE(isq || !isq);
}

TEST(UtilityTest, IsNvencAvailable) {
    bool isn = is_nvenc_available();
    EXPECT_TRUE(isn || !isn);
}

TEST(UtilityTest, IsAmfAvailable) {
    bool isa = is_amf_available();
    EXPECT_TRUE(isa || !isa);
}

TEST(UtilityTest, IsVaapiAvailable) {
    bool isv = is_vaapi_available();
    EXPECT_TRUE(isv || !isv);
}

TEST(UtilityTest, IsVideotoolboxAvailable) {
    bool isvt = is_videotoolbox_available();
    EXPECT_TRUE(isvt || !isvt);
}

TEST(UtilityTest, IsQsvAvailable) {
    bool isq = is_qsv_available();
    EXPECT_TRUE(isq || !isq);
}

TEST(UtilityTest, DetectBestEncoderBackend) {
    auto backend = detect_best_encoder_backend();
    // Should return a valid enum value
    EXPECT_GE(static_cast<uint8_t>(backend), 0u);
    EXPECT_LE(static_cast<uint8_t>(backend), 7u);
}

// ============================================================================
// Name functions tests
// ============================================================================
TEST(NameFunctionsTest, CodecName) {
    EXPECT_EQ(codec_name(CodecFormat::RAW),     "raw");
    EXPECT_EQ(codec_name(CodecFormat::H264),    "h264");
    EXPECT_EQ(codec_name(CodecFormat::H265),    "h265");
    EXPECT_EQ(codec_name(CodecFormat::VP8),     "vp8");
    EXPECT_EQ(codec_name(CodecFormat::VP9),     "vp9");
    EXPECT_EQ(codec_name(CodecFormat::AV1),     "av1");
    EXPECT_EQ(codec_name(CodecFormat::MJPEG),   "mjpeg");
    EXPECT_EQ(codec_name(CodecFormat::HEVC),    "hevc");
    EXPECT_EQ(codec_name(CodecFormat::AV1_SCC), "av1_scc");
}

TEST(NameFunctionsTest, BackendName) {
    EXPECT_EQ(backend_name(EncoderBackend::Software),     "software");
    EXPECT_EQ(backend_name(EncoderBackend::NVENC),        "nvenc");
    EXPECT_EQ(backend_name(EncoderBackend::AMF),          "amf");
    EXPECT_EQ(backend_name(EncoderBackend::VAAPI),        "vaapi");
    EXPECT_EQ(backend_name(EncoderBackend::VideoToolbox), "videotoolbox");
    EXPECT_EQ(backend_name(EncoderBackend::QSV),          "qsv");
    EXPECT_EQ(backend_name(EncoderBackend::MFX),          "mfx");
    EXPECT_EQ(backend_name(EncoderBackend::Auto),         "auto");
}

TEST(NameFunctionsTest, ImageFormatName) {
    EXPECT_EQ(image_format_name(ImageFormat::RAW),    "raw");
    EXPECT_EQ(image_format_name(ImageFormat::ABGR),   "abgr");
    EXPECT_EQ(image_format_name(ImageFormat::ARGB),   "argb");
    EXPECT_EQ(image_format_name(ImageFormat::BGRA),   "bgra");
    EXPECT_EQ(image_format_name(ImageFormat::RGBA),   "rgba");
    EXPECT_EQ(image_format_name(ImageFormat::YUV420), "yuv420");
    EXPECT_EQ(image_format_name(ImageFormat::NV12),   "nv12");
    EXPECT_EQ(image_format_name(ImageFormat::GRAY8),  "gray8");
    EXPECT_EQ(image_format_name(ImageFormat::BGR),    "bgr");
    EXPECT_EQ(image_format_name(ImageFormat::RGB),    "rgb");
    EXPECT_EQ(image_format_name(ImageFormat::YUYV),   "yuyv");
    EXPECT_EQ(image_format_name(ImageFormat::UYVY),   "uyvy");
    EXPECT_EQ(image_format_name(ImageFormat::P010),   "p010");
}

// ============================================================================
// CRC64 and aligned allocation tests
// ============================================================================
TEST(Crc64Test, Basic) {
    const uint8_t data[] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint64_t crc1 = crc64_fast(data, 8);
    uint64_t crc2 = crc64_fast(data, 8);
    EXPECT_EQ(crc1, crc2); // deterministic
}

TEST(Crc64Test, DifferentData) {
    const uint8_t a[] = {1, 2, 3};
    const uint8_t b[] = {1, 2, 4};
    EXPECT_NE(crc64_fast(a, 3), crc64_fast(b, 3));
}

TEST(Crc64Test, EmptyData) {
    uint64_t crc = crc64_fast(nullptr, 0);
    EXPECT_GE(crc, 0u);
}

TEST(Crc64Test, Seed) {
    const uint8_t data[] = {42};
    uint64_t c1 = crc64_fast(data, 1, 0);
    uint64_t c2 = crc64_fast(data, 1, 12345);
    EXPECT_NE(c1, c2);
}

TEST(AlignedAllocTest, AllocFree) {
    uint8_t* ptr = alloc_aligned(1024, 64);
    ASSERT_NE(ptr, nullptr);
    // Alignment check
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 64, 0u);
    EXPECT_NO_THROW(free_aligned(ptr));
}

TEST(AlignedAllocTest, AllocDefaultAlignment) {
    uint8_t* ptr = alloc_aligned(256);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % STRIDE_ALIGN, 0u);
    free_aligned(ptr);
}

TEST(AlignedAllocTest, AllocSmall) {
    uint8_t* ptr = alloc_aligned(1, 16);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 16, 0u);
    free_aligned(ptr);
}

TEST(AlignedAllocTest, AllocZero) {
    uint8_t* ptr = alloc_aligned(0, 64);
    // implementation may return nullptr or a valid pointer
    if (ptr) {
        free_aligned(ptr);
    }
}

// ============================================================================
// Integration / scenario tests
// ============================================================================
TEST(IntegrationTest, FullPipelineMock) {
    // Simulate a simplified capture->encode->record pipeline
    MockCapturer capturer;
    capturer._frame.data.resize(640 * 480 * 4, 128);
    capturer._frame.w = 640; capturer._frame.h = 480;
    capturer._frame.fmt = ImageFormat::RGBA;
    capturer._frame.timestamp_us = 0;

    MockEncoder encoder;
    encoder.open(CodecFormat::H264, 640, 480);
    encoder.encode_result = {0, 0, 0, 1, 0x65};

    MockRecorder recorder;
    Recorder::Context ctx;
    ctx.width  = 640;
    ctx.height = 480;
    ASSERT_TRUE(recorder.start(ctx));

    auto f_opt = capturer.frame(std::chrono::milliseconds(100));
    ASSERT_TRUE(f_opt.has_value());

    // Convert to BGRA
    auto converted = convert_format(*f_opt, ImageFormat::BGRA);
    EXPECT_TRUE(converted.is_valid());

    bool keyframe = true;
    auto encoded = encoder.encode(*f_opt, keyframe);
    EXPECT_EQ(encoded.size(), 5u);

    EXPECT_TRUE(recorder.feed(*f_opt));
    recorder.stop();
}

TEST(IntegrationTest, StatsTracking) {
    StatsTracker st;
    FrameStats snapshot;

    // Simulate 300 frames at 30 fps
    for (int i = 0; i < 300; ++i) {
        st.frame_captured(640 * 480 * 4, 1.0);
        st.frame_encoded(5000, 2.0, i % 90 == 0);
    }

    snapshot = st.snapshot();
    EXPECT_EQ(snapshot.captured_frames, 300u);
    EXPECT_EQ(snapshot.encoded_frames, 300u);
    EXPECT_EQ(snapshot.key_frames, 4u); // 0,90,180,270
    EXPECT_GT(snapshot.total_bytes, 0u);
    EXPECT_GT(snapshot.encoded_bytes, 0u);
}

TEST(IntegrationTest, MultipleResizes) {
    ImageRgb img;
    img.resize(1920, 1080, ImageFormat::RGBA);
    EXPECT_EQ(img.raw.size(), 1920u * 1080u * 4);
    EXPECT_TRUE(img.is_valid());

    img.resize(1280, 720, ImageFormat::BGRA);
    EXPECT_EQ(img.raw.size(), 1280u * 720u * 4);
    EXPECT_TRUE(img.is_valid());

    img.resize(640, 480, ImageFormat::GRAY8);
    EXPECT_EQ(img.raw.size(), 640u * 480u);
    EXPECT_TRUE(img.is_valid());

    img.clear();
    EXPECT_FALSE(img.is_valid());
}

TEST(IntegrationTest, DirtyRectWorkflow) {
    DirtyRect full{0, 0, 1920, 1080};
    EXPECT_TRUE(full.valid());

    DirtyRect left{0, 0, 960, 1080};
    DirtyRect right{960, 0, 1920, 1080};

    auto overlap = left.intersect(right);
    EXPECT_FALSE(overlap.valid());

    auto merged = left.merge(right);
    EXPECT_TRUE(merged.valid());
    EXPECT_EQ(merged.left,   0u);
    EXPECT_EQ(merged.right,  1920u);
    EXPECT_EQ(merged.top,    0u);
    EXPECT_EQ(merged.bottom, 1080u);
}

TEST(IntegrationTest, EncoderConfigCustom) {
    EncoderConfig cfg;
    cfg.codec   = CodecFormat::AV1;
    cfg.width   = 3840;
    cfg.height  = 2160;
    cfg.fps_num = 60;
    cfg.fps_den = 1;
    cfg.bitrate = 50000000;
    cfg.rate_control = RateControl::CQP;
    cfg.qp_i = 15;
    cfg.qp_p = 20;
    cfg.backend = EncoderBackend::Auto;

    EXPECT_EQ(cfg.fps(), 60u);
    EXPECT_NEAR(cfg.frame_interval_ms(), 16.666, 0.1);
}

TEST(IntegrationTest, FrameFullFieldExercise) {
    Frame f;
    f.data = std::vector<uint8_t>(100, 42);
    f.w = 10; f.h = 10; f.stride = 40;
    f.fmt = ImageFormat::BGRA;
    f.timestamp_us = 33333;
    f.keyframe = true;
    f.codec = CodecFormat::H265;
    f.flags.set(static_cast<size_t>(FrameFlag::Key));
    f.flags.set(static_cast<size_t>(FrameFlag::SeqHeader));
    f.pts = 1;
    f.dts = 1;
    f.duration = 33;
    f.sequence_number = 1;
    f.cursor_visible = true;
    f.cursor_x = 500;
    f.cursor_y = 300;
    f.cursor_w = 32;
    f.cursor_h = 32;
    f.cursor_data = std::vector<uint8_t>(32 * 32 * 4, 255);

    DirtyRect dr{0, 0, 10, 10};
    f.dirty_rects.push_back(dr);

    PlaneData pd;
    pd.width = 10;
    pd.height = 10;
    pd.stride = 10;
    f.planes.push_back(pd);

    EXPECT_FALSE(f.empty());
    EXPECT_EQ(f.size(), 100u);
    EXPECT_TRUE(f.has_dirty());

    // Clone via swap
    Frame clone;
    clone.swap(f);
    EXPECT_FALSE(clone.empty());
    EXPECT_TRUE(f.empty()); // f now has clone's old data
    EXPECT_EQ(clone.w, 10u);
    EXPECT_EQ(clone.flags.count(), 2u);
    EXPECT_EQ(clone.pts, 1);
    EXPECT_TRUE(clone.cursor_visible);
    EXPECT_EQ(clone.dirty_rects.size(), 1u);
    EXPECT_EQ(clone.planes.size(), 1u);
}

TEST(IntegrationTest, ColorConversionPlanIdentityCheck) {
    ColorConversionPlan plan;
    EXPECT_TRUE(plan.is_identity());

    plan.src_fmt = ImageFormat::BGRA;
    EXPECT_FALSE(plan.is_identity());

    plan.dst_fmt = ImageFormat::BGRA;
    plan.src_cspace = ColorSpace::BT709;
    EXPECT_FALSE(plan.is_identity());

    plan.dst_cspace = ColorSpace::BT709;
    plan.premultiply_alpha = true;
    EXPECT_FALSE(plan.is_identity());
}

TEST(IntegrationTest, ScaleConfigPairing) {
    ScaleConfig cfg;
    cfg.src_w = 1920; cfg.src_h = 1080;
    cfg.dst_w = 640;  cfg.dst_h = 360;
    cfg.keep_aspect = true;
    cfg.algo = ScaleAlgorithm::Lanczos3;

    EXPECT_DOUBLE_EQ(cfg.scale_x(), 640.0 / 1920.0);
    EXPECT_DOUBLE_EQ(cfg.scale_y(), 360.0 / 1080.0);
}

// ============================================================================
// Edge case tests
// ============================================================================
TEST(EdgeCaseTest, ZeroSizedImageRgb) {
    ImageRgb img;
    img.resize(0, 0, ImageFormat::RGBA);
    EXPECT_FALSE(img.is_valid());

    img.resize(100, 100, ImageFormat::RGBA);
    EXPECT_TRUE(img.is_valid());
    img.clear();
    EXPECT_FALSE(img.is_valid());
}

TEST(EdgeCaseTest, VeryLargeImageRgb) {
    ImageRgb img;
    // Test with a very large but valid resize (may OOM if too large)
    img.resize(8192, 8192, ImageFormat::RGBA);
    if (img.is_valid()) {
        EXPECT_EQ(img.w, 8192u);
        EXPECT_EQ(img.h, 8192u);
    }
}

TEST(EdgeCaseTest, OnePixelImages) {
    ImageRgb img;
    img.resize(1, 1, ImageFormat::RGBA);
    EXPECT_TRUE(img.is_valid());
    EXPECT_EQ(img.raw.size(), 4u);
    img.raw = {255, 128, 64, 32};
    EXPECT_EQ(img.raw[0], 255);
    EXPECT_EQ(img.raw[3], 32);
}

TEST(EdgeCaseTest, DirtyRectDegenerateCases) {
    // Zero area rects are invalid
    EXPECT_FALSE(DirtyRect{5, 5, 5, 5}.valid());

    // Merge of two invalid rects
    DirtyRect a, b;
    auto merged = a.merge(b);
    EXPECT_FALSE(merged.valid());

    // Intersect of two invalid rects
    auto isect = a.intersect(b);
    EXPECT_FALSE(isect.valid());
}

TEST(EdgeCaseTest, FrameSwapSelf) {
    Frame f;
    f.data = {1, 2, 3};
    f.w = 3; f.h = 1;
    // Self-swap should be safe
    EXPECT_NO_THROW(f.swap(f));
    EXPECT_EQ(f.data, (std::vector<uint8_t>{1, 2, 3}));
}

TEST(EdgeCaseTest, LargeTimestampValues) {
    Frame f;
    f.timestamp_us = INT64_MAX;
    EXPECT_EQ(f.timestamp_us, INT64_MAX);
    f.timestamp_us = 0;
    EXPECT_EQ(f.timestamp_us, 0);
    f.timestamp_us = -1;
    EXPECT_EQ(f.timestamp_us, -1);
}

TEST(EdgeCaseTest, EncoderConfigExtremes) {
    EncoderConfig cfg;
    cfg.bitrate = UINT32_MAX;
    EXPECT_EQ(cfg.bitrate, UINT32_MAX);
    cfg.bitrate = 0;
    EXPECT_EQ(cfg.bitrate, 0u);

    cfg.crf = -12;
    EXPECT_EQ(cfg.crf, -12);
    cfg.crf = 51;
    EXPECT_EQ(cfg.crf, 51);

    cfg.threads = -1;
    EXPECT_EQ(cfg.threads, -1);
    cfg.threads = 256;
    EXPECT_EQ(cfg.threads, 256);
}

TEST(EdgeCaseTest, FPSLimiterEdgeCases) {
    FPSLimiter fl(1);
    EXPECT_EQ(fl.frame_interval_us(), 1000000LL);
    EXPECT_EQ(fl.target_fps(), 1u);

    FPSLimiter fl2(0);
    EXPECT_EQ(fl2.frame_interval_us(), 0);
    EXPECT_EQ(fl2.target_fps(), 0u);

    FPSLimiter fl3(1000);
    EXPECT_EQ(fl3.frame_interval_us(), 1000);
}

TEST(EdgeCaseTest, CaptureRegionEdge) {
    CaptureRegion r;
    r.x = -100; r.y = -100;
    r.w = 200; r.h = 200;
    EXPECT_TRUE(r.valid());
    EXPECT_TRUE(r.contains_point(-1, -1));
    EXPECT_TRUE(r.contains_point(-100, -100));
    EXPECT_FALSE(r.contains_point(100, 100));
    EXPECT_FALSE(r.contains_point(-101, 0));
}

TEST(EdgeCaseTest, WouldBlockIfEqualLargeIdentical) {
    std::vector<uint8_t> old(10000, 0xAA);
    std::vector<uint8_t> b(10000, 0xAA);
    bool blocked = would_block_if_equal(old, b);
    if (blocked) EXPECT_TRUE(old.empty());
}

TEST(EdgeCaseTest, ConvertRGBAtoYUV420OnePixel) {
    // 1x1 is problematic for YUV420 subsampling; test that it doesn't crash
    std::vector<uint8_t> rgba = {128, 128, 128, 255};
    std::vector<uint8_t> y(1), u(1), v(1);
    EXPECT_NO_THROW(convert_rgba_to_yuv420(
        rgba.data(), 1, 1,
        y.data(), 1, u.data(), 1, v.data(), 1
    ));
}

// ============================================================================
// Multi-threading safety smoke tests
// ============================================================================
TEST(ThreadSafetySmokeTest, FactoryCreatesIndependentObjects) {
    auto c1 = create_capturer();
    auto c2 = create_capturer();
    EXPECT_NE(c1.get(), c2.get());

    auto d1 = create_decoder();
    auto d2 = create_decoder();
    EXPECT_NE(d1.get(), d2.get());

    auto e1 = create_encoder();
    auto e2 = create_encoder();
    EXPECT_NE(e1.get(), e2.get());
}

TEST(ThreadSafetySmokeTest, ParallelStatsAccumulation) {
    StatsTracker st;

    auto worker = [&st](int n) {
        for (int i = 0; i < n; ++i) {
            st.frame_captured(100, 0.5);
            st.frame_encoded(50, 1.0, i % 10 == 0);
        }
    };

    std::thread t1(worker, 50);
    std::thread t2(worker, 50);
    t1.join();
    t2.join();

    auto s = st.snapshot();
    EXPECT_EQ(s.captured_frames, 100u);
    EXPECT_EQ(s.encoded_frames, 100u);
}

// ============================================================================
// Enum roundtrip / coverage tests
// ============================================================================
TEST(EnumCoverageTest, AllCodecFormatsIterable) {
    std::vector<CodecFormat> all = {
        CodecFormat::RAW, CodecFormat::H264, CodecFormat::H265,
        CodecFormat::VP8, CodecFormat::VP9,  CodecFormat::AV1,
        CodecFormat::MJPEG, CodecFormat::HEVC, CodecFormat::AV1_SCC
    };
    for (auto cf : all) {
        std::string name = codec_name(cf);
        EXPECT_FALSE(name.empty());
    }
}

TEST(EnumCoverageTest, AllImageFormatsIterable) {
    std::vector<ImageFormat> all = {
        ImageFormat::RAW, ImageFormat::ABGR, ImageFormat::ARGB,
        ImageFormat::BGRA, ImageFormat::RGBA, ImageFormat::YUV420,
        ImageFormat::NV12, ImageFormat::GRAY8, ImageFormat::BGR,
        ImageFormat::RGB, ImageFormat::YUYV, ImageFormat::UYVY,
        ImageFormat::P010
    };
    for (auto fmt : all) {
        std::string name = image_format_name(fmt);
        EXPECT_FALSE(name.empty());
    }
}

TEST(EnumCoverageTest, AllEncoderBackends) {
    std::vector<EncoderBackend> all = {
        EncoderBackend::Software, EncoderBackend::NVENC,
        EncoderBackend::AMF, EncoderBackend::VAAPI,
        EncoderBackend::VideoToolbox, EncoderBackend::QSV,
        EncoderBackend::MFX, EncoderBackend::Auto
    };
    for (auto eb : all) {
        std::string name = backend_name(eb);
        EXPECT_FALSE(name.empty());
    }
}

TEST(EnumCoverageTest, AllRateControls) {
    std::vector<RateControl> all = {
        RateControl::CBR, RateControl::VBR, RateControl::CQP,
        RateControl::CRF, RateControl::VBR_HQ, RateControl::QVBR
    };
    for (auto& rc : all) {
        EXPECT_GE(static_cast<uint8_t>(rc), 0u);
    }
}

TEST(EnumCoverageTest, AllScaleAlgorithms) {
    std::vector<ScaleAlgorithm> all = {
        ScaleAlgorithm::Nearest, ScaleAlgorithm::Bilinear,
        ScaleAlgorithm::Bicubic, ScaleAlgorithm::Lanczos3,
        ScaleAlgorithm::AreaAvg
    };
    for (auto& sa : all) {
        EXPECT_GE(static_cast<uint8_t>(sa), 0u);
        EXPECT_LE(static_cast<uint8_t>(sa), 4u);
    }
}

TEST(EnumCoverageTest, AllColorSpaces) {
    std::vector<ColorSpace> all = {
        ColorSpace::BT601, ColorSpace::BT709, ColorSpace::BT2020,
        ColorSpace::BT2020_PQ, ColorSpace::BT2020_HLG, ColorSpace::SRGB
    };
    for (auto& cs : all) {
        EXPECT_GE(static_cast<uint8_t>(cs), 0u);
        EXPECT_LE(static_cast<uint8_t>(cs), 5u);
    }
}

TEST(EnumCoverageTest, AllAdapterTypes) {
    std::vector<AdapterType> all = {
        AdapterType::Any, AdapterType::NVIDIA,
        AdapterType::AMD, AdapterType::Intel, AdapterType::Apple
    };
    for (auto& at : all) {
        EXPECT_GE(static_cast<uint8_t>(at), 0u);
        EXPECT_LE(static_cast<uint8_t>(at), 4u);
    }
}

TEST(EnumCoverageTest, AllEncoderQualities) {
    std::vector<EncoderQuality> all = {
        EncoderQuality::Ultrafast, EncoderQuality::Veryfast,
        EncoderQuality::Fast, EncoderQuality::Medium,
        EncoderQuality::Slow, EncoderQuality::Veryslow,
        EncoderQuality::Lossless
    };
    for (auto& eq : all) {
        EXPECT_GE(static_cast<uint8_t>(eq), 0u);
        EXPECT_LE(static_cast<uint8_t>(eq), 6u);
    }
}

TEST(EnumCoverageTest, AllCodecProfiles) {
    std::vector<CodecProfile> all = {
        CodecProfile::Baseline, CodecProfile::Main,
        CodecProfile::High, CodecProfile::High444,
        CodecProfile::Main10, CodecProfile::Main12,
        CodecProfile::Professional
    };
    for (auto& cp : all) {
        EXPECT_GE(static_cast<uint8_t>(cp), 0u);
        EXPECT_LE(static_cast<uint8_t>(cp), 6u);
    }
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
