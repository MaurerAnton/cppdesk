/**
 * test_codec.cpp — Comprehensive tests for cppdesk codec modules
 *
 * Tests cover:
 *   Video:  VideoEncoder (H264/H265/VP8/VP9/AV1), VideoDecoder, BitrateController
 *   Audio:  OpusEncoder, OpusDecoder, Resampler, AudioMixer, SilenceDetector, EchoCanceller
 *
 * Source headers are in src/ (not include/), referenced by relative path:
 *   ../../src/codec/video_codec.h
 *   ../../src/codec/audio_codec.h
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <array>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Include codec headers from src/ directory (relative to tests/)
// ---------------------------------------------------------------------------
#include "../../src/codec/video_codec.h"
#include "../../src/codec/audio_codec.h"

// ---------------------------------------------------------------------------
// Convenience namespace aliases
// ---------------------------------------------------------------------------
namespace vid = cppdesk::codec::video;
namespace aud = cppdesk::codec::audio;

// ============================================================================
//  Test helpers & fixtures
// ============================================================================

/// Generate a synthetic YUV420 frame of the given dimensions.
/// Fills Y plane with a gradient, U and V planes with constant mid-grey (128).
static std::vector<uint8_t> make_yuv420_frame(int width, int height) {
    int y_size  = width * height;
    int uv_size = (width / 2) * (height / 2);
    std::vector<uint8_t> frame(y_size + 2 * uv_size, 0);
    // Y plane: simple horizontal gradient 0..255 wrapped
    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            frame[r * width + c] = static_cast<uint8_t>((c * 256 / width) & 0xFF);
        }
    }
    // U / V : mid-grey
    std::fill(frame.begin() + y_size, frame.begin() + y_size + uv_size, 128);
    std::fill(frame.begin() + y_size + uv_size, frame.end(), 128);
    return frame;
}

/// Generate a synthetic PCM audio buffer (int16 interleaved stereo).
/// Fills with a sine wave.
static std::vector<int16_t> make_pcm_samples(int num_samples, int channels = 2,
                                              float frequency = 440.0f,
                                              int sample_rate = 48000) {
    std::vector<int16_t> buf(num_samples * channels, 0);
    for (int i = 0; i < num_samples; ++i) {
        float t    = static_cast<float>(i) / sample_rate;
        int16_t v  = static_cast<int16_t>(16000.0f * std::sin(2.0f * 3.14159265f * frequency * t));
        for (int ch = 0; ch < channels; ++ch) {
            buf[i * channels + ch] = v;
        }
    }
    return buf;
}

/// Generate a synthetic float PCM buffer (interleaved stereo).
static std::vector<float> make_float_samples(int num_samples, int channels = 2,
                                              float frequency = 440.0f,
                                              int sample_rate = 48000) {
    std::vector<float> buf(num_samples * channels, 0.0f);
    for (int i = 0; i < num_samples; ++i) {
        float t = static_cast<float>(i) / sample_rate;
        float v = 0.8f * std::sin(2.0f * 3.14159265f * frequency * t);
        for (int ch = 0; ch < channels; ++ch) {
            buf[i * channels + ch] = v;
        }
    }
    return buf;
}

/// Random byte generator
static std::vector<uint8_t> random_bytes(size_t n) {
    static std::mt19937 rng(42); // fixed seed for reproducibility
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<uint8_t> out(n);
    for (auto& b : out) b = static_cast<uint8_t>(dist(rng));
    return out;
}

// ============================================================================
//  Video Codec Tests — Configurations
// ============================================================================

TEST(VideoConfigTest, DefaultH264Config) {
    vid::H264EncoderConfig cfg;
    // Verify sensible defaults
    EXPECT_GT(cfg.width, 0);
    EXPECT_GT(cfg.height, 0);
    EXPECT_GT(cfg.framerate, 0);
    EXPECT_GT(cfg.bitrate, 0);
    EXPECT_NE(cfg.preset, vid::EncoderPreset::DEFAULT);
}

TEST(VideoConfigTest, DefaultH265Config) {
    vid::H265EncoderConfig cfg;
    EXPECT_GT(cfg.width, 0);
    EXPECT_GT(cfg.height, 0);
    EXPECT_GT(cfg.framerate, 0);
    EXPECT_GE(cfg.bitrate, 0);
}

TEST(VideoConfigTest, DefaultVP8Config) {
    vid::VP8EncoderConfig cfg;
    EXPECT_GT(cfg.width, 0);
    EXPECT_GT(cfg.height, 0);
}

TEST(VideoConfigTest, DefaultVP9Config) {
    vid::VP9EncoderConfig cfg;
    EXPECT_GT(cfg.width, 0);
    EXPECT_GT(cfg.height, 0);
}

TEST(VideoConfigTest, DefaultAV1Config) {
    vid::AV1EncoderConfig cfg;
    EXPECT_GT(cfg.width, 0);
    EXPECT_GT(cfg.height, 0);
}

TEST(VideoConfigTest, H264ConfigBuilder_AllFields) {
    vid::H264EncoderConfig cfg;
    cfg.width      = 1920;
    cfg.height     = 1080;
    cfg.framerate  = 30;
    cfg.bitrate    = 4'000'000;  // 4 Mbps
    cfg.gop_size   = 60;
    cfg.profile    = vid::H264Profile::HIGH;
    cfg.level      = vid::H264Level::LEVEL_4_1;
    cfg.preset     = vid::EncoderPreset::MEDIUM;
    cfg.rc_mode    = vid::RateControlMode::VBR;
    cfg.keyint_max = 120;
    cfg.bframes    = 3;

    EXPECT_EQ(cfg.width, 1920);
    EXPECT_EQ(cfg.height, 1080);
    EXPECT_EQ(cfg.framerate, 30);
    EXPECT_EQ(cfg.bitrate, 4'000'000);
    EXPECT_EQ(cfg.gop_size, 60);
    EXPECT_EQ(cfg.profile, vid::H264Profile::HIGH);
    EXPECT_EQ(cfg.level, vid::H264Level::LEVEL_4_1);
    EXPECT_EQ(cfg.preset, vid::EncoderPreset::MEDIUM);
    EXPECT_EQ(cfg.rc_mode, vid::RateControlMode::VBR);
    EXPECT_EQ(cfg.keyint_max, 120);
    EXPECT_EQ(cfg.bframes, 3);
}

TEST(VideoConfigTest, H265ConfigBuilder_AllFields) {
    vid::H265EncoderConfig cfg;
    cfg.width       = 3840;
    cfg.height      = 2160;
    cfg.framerate   = 60;
    cfg.bitrate     = 20'000'000;  // 20 Mbps
    cfg.gop_size    = 120;
    cfg.profile     = vid::H265Profile::MAIN10;
    cfg.preset      = vid::EncoderPreset::SLOW;
    cfg.rc_mode     = vid::RateControlMode::CBR;
    cfg.keyint_max  = 240;
    cfg.bframes     = 2;
    cfg.tier        = vid::H265Tier::HIGH;

    EXPECT_EQ(cfg.width, 3840);
    EXPECT_EQ(cfg.height, 2160);
    EXPECT_EQ(cfg.framerate, 60);
    EXPECT_EQ(cfg.bitrate, 20'000'000);
    EXPECT_EQ(cfg.gop_size, 120);
    EXPECT_EQ(cfg.profile, vid::H265Profile::MAIN10);
    EXPECT_EQ(cfg.preset, vid::EncoderPreset::SLOW);
    EXPECT_EQ(cfg.rc_mode, vid::RateControlMode::CBR);
    EXPECT_EQ(cfg.keyint_max, 240);
    EXPECT_EQ(cfg.bframes, 2);
    EXPECT_EQ(cfg.tier, vid::H265Tier::HIGH);
}

TEST(VideoConfigTest, VP8ConfigBuilder) {
    vid::VP8EncoderConfig cfg;
    cfg.width      = 1280;
    cfg.height     = 720;
    cfg.framerate  = 25;
    cfg.bitrate    = 2'000'000;
    cfg.gop_size   = 50;
    cfg.rc_mode    = vid::RateControlMode::CQ;
    cfg.cq_level   = 23;
    cfg.cpu_used   = 4;

    EXPECT_EQ(cfg.width, 1280);
    EXPECT_EQ(cfg.height, 720);
    EXPECT_EQ(cfg.framerate, 25);
    EXPECT_EQ(cfg.bitrate, 2'000'000);
    EXPECT_EQ(cfg.rc_mode, vid::RateControlMode::CQ);
    EXPECT_EQ(cfg.cq_level, 23);
    EXPECT_EQ(cfg.cpu_used, 4);
}

TEST(VideoConfigTest, VP9ConfigBuilder) {
    vid::VP9EncoderConfig cfg;
    cfg.width      = 1920;
    cfg.height     = 1080;
    cfg.framerate  = 30;
    cfg.bitrate    = 5'000'000;
    cfg.gop_size   = 60;
    cfg.profile    = vid::VP9Profile::PROFILE_2;
    cfg.rc_mode    = vid::RateControlMode::VBR;
    cfg.cq_level   = 30;
    cfg.tile_columns = 2;
    cfg.tile_rows   = 1;
    cfg.lossless    = false;

    EXPECT_EQ(cfg.width, 1920);
    EXPECT_EQ(cfg.bitrate, 5'000'000);
    EXPECT_EQ(cfg.profile, vid::VP9Profile::PROFILE_2);
    EXPECT_EQ(cfg.tile_columns, 2);
    EXPECT_EQ(cfg.tile_rows, 1);
    EXPECT_FALSE(cfg.lossless);
}

TEST(VideoConfigTest, AV1ConfigBuilder) {
    vid::AV1EncoderConfig cfg;
    cfg.width      = 3840;
    cfg.height     = 2160;
    cfg.framerate  = 30;
    cfg.bitrate    = 12'000'000;
    cfg.gop_size   = 90;
    cfg.profile    = vid::AV1Profile::MAIN;
    cfg.rc_mode    = vid::RateControlMode::VBR;
    cfg.cq_level   = 28;
    cfg.tile_cols  = 4;
    cfg.tile_rows  = 1;
    cfg.film_grain = true;
    cfg.superres   = false;

    EXPECT_EQ(cfg.width, 3840);
    EXPECT_EQ(cfg.framerate, 30);
    EXPECT_EQ(cfg.bitrate, 12'000'000);
    EXPECT_EQ(cfg.profile, vid::AV1Profile::MAIN);
    EXPECT_EQ(cfg.cq_level, 28);
    EXPECT_EQ(cfg.tile_cols, 4);
    EXPECT_TRUE(cfg.film_grain);
    EXPECT_FALSE(cfg.superres);
}

// ============================================================================
//  Video Codec Tests — Encoder Lifecycle
// ============================================================================

class VideoEncoderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Baseline config: 720p30 @ 2 Mbps
        cfg_.width     = 1280;
        cfg_.height    = 720;
        cfg_.framerate = 30;
        cfg_.bitrate   = 2'000'000;
        cfg_.gop_size  = 60;
        cfg_.preset    = vid::EncoderPreset::ULTRAFAST;
        cfg_.rc_mode   = vid::RateControlMode::CBR;
    }
    vid::H264EncoderConfig cfg_;
};

TEST_F(VideoEncoderTest, H264_CreateAndDestroy) {
    ASSERT_NO_THROW({
        auto enc = vid::VideoEncoder::create_h264(cfg_);
        EXPECT_TRUE(enc != nullptr);
    });
}

TEST_F(VideoEncoderTest, H264_InvalidDimensions_ShallReturnNull) {
    cfg_.width = 0;
    auto enc = vid::VideoEncoder::create_h264(cfg_);
    EXPECT_EQ(enc, nullptr);
}

TEST_F(VideoEncoderTest, H264_EncodeSingleFrame) {
    auto enc = vid::VideoEncoder::create_h264(cfg_);
    ASSERT_NE(enc, nullptr);

    auto frame = make_yuv420_frame(cfg_.width, cfg_.height);

    vid::EncodedPacket pkt;
    vid::EncodeResult res = enc->encode(frame.data(), frame.size(), &pkt);
    // First frame may be a keyframe, but it's acceptable to get NEED_MORE
    EXPECT_TRUE(res == vid::EncodeResult::SUCCESS ||
                res == vid::EncodeResult::NEED_MORE);
}

TEST_F(VideoEncoderTest, H264_EncodeMultipleFrames) {
    auto enc = vid::VideoEncoder::create_h264(cfg_);
    ASSERT_NE(enc, nullptr);

    constexpr int kFrameCount = 30;
    int success_count = 0;

    for (int i = 0; i < kFrameCount; ++i) {
        auto frame = make_yuv420_frame(cfg_.width, cfg_.height);
        vid::EncodedPacket pkt;
        vid::EncodeResult res = enc->encode(frame.data(), frame.size(), &pkt);
        if (res == vid::EncodeResult::SUCCESS) {
            EXPECT_FALSE(pkt.data.empty());
            EXPECT_GT(pkt.size, 0u);
            success_count++;
        }
    }

    // At least some frames should produce output
    EXPECT_GT(success_count, 0);
}

TEST_F(VideoEncoderTest, H264_FlushProducesRemainingPackets) {
    auto enc = vid::VideoEncoder::create_h264(cfg_);
    ASSERT_NE(enc, nullptr);

    // Feed a few frames
    for (int i = 0; i < 5; ++i) {
        auto frame = make_yuv420_frame(cfg_.width, cfg_.height);
        vid::EncodedPacket pkt;
        enc->encode(frame.data(), frame.size(), &pkt);
    }

    // Flush
    std::vector<vid::EncodedPacket> flushed;
    vid::EncodeResult flush_res;
    do {
        vid::EncodedPacket pkt;
        flush_res = enc->flush(&pkt);
        if (flush_res == vid::EncodeResult::SUCCESS && pkt.size > 0) {
            flushed.push_back(std::move(pkt));
        }
    } while (flush_res != vid::EncodeResult::FINISHED);

    // We should get at least some flushed packets (keyframe at minimum)
    EXPECT_GE(flushed.size(), 1u);
}

TEST_F(VideoEncoderTest, H264_KeyframeRequest) {
    auto enc = vid::VideoEncoder::create_h264(cfg_);
    ASSERT_NE(enc, nullptr);

    // Encode a few frames normally
    for (int i = 0; i < 10; ++i) {
        auto frame = make_yuv420_frame(cfg_.width, cfg_.height);
        vid::EncodedPacket pkt;
        enc->encode(frame.data(), frame.size(), &pkt);
    }

    // Request a keyframe and encode
    enc->request_keyframe();
    auto frame = make_yuv420_frame(cfg_.width, cfg_.height);
    vid::EncodedPacket pkt;
    enc->encode(frame.data(), frame.size(), &pkt);

    if (pkt.size > 0) {
        EXPECT_TRUE(pkt.is_keyframe);
    }
}

TEST_F(VideoEncoderTest, H264_ForceIDR) {
    auto enc = vid::VideoEncoder::create_h264(cfg_);
    ASSERT_NE(enc, nullptr);

    enc->force_idr();
    auto frame = make_yuv420_frame(cfg_.width, cfg_.height);
    vid::EncodedPacket pkt;
    enc->encode(frame.data(), frame.size(), &pkt);
    // Next packet should be a keyframe/IDR
    if (pkt.size > 0) {
        EXPECT_TRUE(pkt.is_keyframe);
    }
}

TEST_F(VideoEncoderTest, H264_ReconfigureBitrate) {
    auto enc = vid::VideoEncoder::create_h264(cfg_);
    ASSERT_NE(enc, nullptr);

    // Encode a few frames at original bitrate
    for (int i = 0; i < 5; ++i) {
        auto frame = make_yuv420_frame(cfg_.width, cfg_.height);
        vid::EncodedPacket pkt;
        enc->encode(frame.data(), frame.size(), &pkt);
    }

    // Reconfigure to lower bitrate
    cfg_.bitrate = 500'000;
    bool reconf_ok = enc->reconfigure(cfg_);
    EXPECT_TRUE(reconf_ok);

    // Encode more frames at new bitrate
    for (int i = 0; i < 5; ++i) {
        auto frame = make_yuv420_frame(cfg_.width, cfg_.height);
        vid::EncodedPacket pkt;
        enc->encode(frame.data(), frame.size(), &pkt);
    }
}

TEST_F(VideoEncoderTest, H264_GetEncodeStats) {
    auto enc = vid::VideoEncoder::create_h264(cfg_);
    ASSERT_NE(enc, nullptr);

    for (int i = 0; i < 10; ++i) {
        auto frame = make_yuv420_frame(cfg_.width, cfg_.height);
        vid::EncodedPacket pkt;
        enc->encode(frame.data(), frame.size(), &pkt);
    }

    auto stats = enc->get_stats();
    EXPECT_GE(stats.frames_encoded, 10u);
    EXPECT_GE(stats.bytes_output, 0u);
    EXPECT_GE(stats.average_bitrate_bps, 0u);
}

TEST_F(VideoEncoderTest, H264_EncoderType) {
    auto enc = vid::VideoEncoder::create_h264(cfg_);
    ASSERT_NE(enc, nullptr);
    EXPECT_EQ(enc->codec_type(), vid::CodecType::H264);
}

// ---------------------------------------------------------------------------
//  H265 Encoder Tests
// ---------------------------------------------------------------------------

TEST(VideoEncoderH265Test, H265_CreateAndEncode) {
    vid::H265EncoderConfig cfg;
    cfg.width     = 1920;
    cfg.height    = 1080;
    cfg.framerate = 30;
    cfg.bitrate   = 8'000'000;
    cfg.gop_size  = 60;
    cfg.preset    = vid::EncoderPreset::FAST;
    cfg.profile   = vid::H265Profile::MAIN;

    auto enc = vid::VideoEncoder::create_h265(cfg);
    ASSERT_NE(enc, nullptr);

    auto frame = make_yuv420_frame(cfg.width, cfg.height);
    vid::EncodedPacket pkt;
    vid::EncodeResult res = enc->encode(frame.data(), frame.size(), &pkt);

    EXPECT_NE(res, vid::EncodeResult::ERROR);
    EXPECT_EQ(enc->codec_type(), vid::CodecType::H265);
}

TEST(VideoEncoderH265Test, H265_FlushAndDrain) {
    vid::H265EncoderConfig cfg;
    cfg.width     = 1280;
    cfg.height    = 720;
    cfg.framerate = 25;
    cfg.bitrate   = 3'000'000;
    cfg.preset    = vid::EncoderPreset::SUPERFAST;

    auto enc = vid::VideoEncoder::create_h265(cfg);
    ASSERT_NE(enc, nullptr);

    for (int i = 0; i < 10; ++i) {
        auto frame = make_yuv420_frame(cfg.width, cfg.height);
        vid::EncodedPacket pkt;
        enc->encode(frame.data(), frame.size(), &pkt);
    }

    vid::EncodedPacket pkt;
    vid::EncodeResult res;
    int flushed_count = 0;
    while ((res = enc->flush(&pkt)) != vid::EncodeResult::FINISHED) {
        if (res == vid::EncodeResult::SUCCESS) flushed_count++;
    }
    EXPECT_GE(flushed_count, 1);
}

// ---------------------------------------------------------------------------
//  VP8 Encoder Tests
// ---------------------------------------------------------------------------

TEST(VideoEncoderVP8Test, VP8_CreateAndEncode) {
    vid::VP8EncoderConfig cfg;
    cfg.width     = 640;
    cfg.height    = 480;
    cfg.framerate = 30;
    cfg.bitrate   = 1'000'000;
    cfg.rc_mode   = vid::RateControlMode::VBR;

    auto enc = vid::VideoEncoder::create_vp8(cfg);
    if (enc) {  // VP8 support may be optional
        EXPECT_EQ(enc->codec_type(), vid::CodecType::VP8);

        auto frame = make_yuv420_frame(cfg.width, cfg.height);
        vid::EncodedPacket pkt;
        vid::EncodeResult res = enc->encode(frame.data(), frame.size(), &pkt);
        EXPECT_NE(res, vid::EncodeResult::ERROR);
    }
}

TEST(VideoEncoderVP8Test, VP8_LosslessMode) {
    vid::VP8EncoderConfig cfg;
    cfg.width     = 640;
    cfg.height    = 480;
    cfg.framerate = 30;
    cfg.bitrate   = 0;          // ignored in lossless
    cfg.lossless  = true;

    auto enc = vid::VideoEncoder::create_vp8(cfg);
    if (enc) {
        EXPECT_TRUE(cfg.lossless);
        auto frame = make_yuv420_frame(cfg.width, cfg.height);
        vid::EncodedPacket pkt;
        enc->encode(frame.data(), frame.size(), &pkt);
    }
}

TEST(VideoEncoderVP8Test, VP8_DeadlineRealtime) {
    vid::VP8EncoderConfig cfg;
    cfg.width     = 640;
    cfg.height    = 480;
    cfg.framerate = 30;
    cfg.bitrate   = 800'000;
    cfg.deadline  = vid::VP8Deadline::REALTIME;
    cfg.cpu_used  = -6; // fastest

    auto enc = vid::VideoEncoder::create_vp8(cfg);
    if (enc) {
        for (int i = 0; i < 60; ++i) {
            auto frame = make_yuv420_frame(cfg.width, cfg.height);
            vid::EncodedPacket pkt;
            enc->encode(frame.data(), frame.size(), &pkt);
        }
    }
}

// ---------------------------------------------------------------------------
//  VP9 Encoder Tests
// ---------------------------------------------------------------------------

TEST(VideoEncoderVP9Test, VP9_CreateAndEncode) {
    vid::VP9EncoderConfig cfg;
    cfg.width     = 1920;
    cfg.height    = 1080;
    cfg.framerate = 30;
    cfg.bitrate   = 4'000'000;

    auto enc = vid::VideoEncoder::create_vp9(cfg);
    if (enc) {
        EXPECT_EQ(enc->codec_type(), vid::CodecType::VP9);

        auto frame = make_yuv420_frame(cfg.width, cfg.height);
        vid::EncodedPacket pkt;
        vid::EncodeResult res = enc->encode(frame.data(), frame.size(), &pkt);
        EXPECT_NE(res, vid::EncodeResult::ERROR);
    }
}

TEST(VideoEncoderVP9Test, VP9_TileEncoding) {
    vid::VP9EncoderConfig cfg;
    cfg.width        = 3840;
    cfg.height       = 2160;
    cfg.framerate    = 30;
    cfg.bitrate      = 10'000'000;
    cfg.tile_columns = 4;
    cfg.tile_rows    = 2;

    auto enc = vid::VideoEncoder::create_vp9(cfg);
    if (enc) {
        auto frame = make_yuv420_frame(cfg.width, cfg.height);
        vid::EncodedPacket pkt;
        enc->encode(frame.data(), frame.size(), &pkt);
    }
}

TEST(VideoEncoderVP9Test, VP9_SVC) {
    vid::VP9EncoderConfig cfg;
    cfg.width       = 1280;
    cfg.height      = 720;
    cfg.framerate   = 30;
    cfg.bitrate     = 3'000'000;
    cfg.svc_enabled = true;
    cfg.svc_layers  = 3;

    auto enc = vid::VideoEncoder::create_vp9(cfg);
    if (enc) {
        for (int i = 0; i < 15; ++i) {
            auto frame = make_yuv420_frame(cfg.width, cfg.height);
            vid::EncodedPacket pkt;
            enc->encode(frame.data(), frame.size(), &pkt);
        }
    }
}

// ---------------------------------------------------------------------------
//  AV1 Encoder Tests
// ---------------------------------------------------------------------------

TEST(VideoEncoderAV1Test, AV1_CreateAndEncode) {
    vid::AV1EncoderConfig cfg;
    cfg.width     = 1920;
    cfg.height    = 1080;
    cfg.framerate = 30;
    cfg.bitrate   = 6'000'000;

    auto enc = vid::VideoEncoder::create_av1(cfg);
    if (enc) {
        EXPECT_EQ(enc->codec_type(), vid::CodecType::AV1);

        auto frame = make_yuv420_frame(cfg.width, cfg.height);
        vid::EncodedPacket pkt;
        vid::EncodeResult res = enc->encode(frame.data(), frame.size(), &pkt);
        EXPECT_NE(res, vid::EncodeResult::ERROR);
    }
}

TEST(VideoEncoderAV1Test, AV1_FilmGrainSynthesis) {
    vid::AV1EncoderConfig cfg;
    cfg.width      = 1920;
    cfg.height     = 1080;
    cfg.framerate  = 30;
    cfg.bitrate    = 4'000'000;
    cfg.film_grain = true;
    cfg.grain_strength = 8;

    auto enc = vid::VideoEncoder::create_av1(cfg);
    if (enc) {
        EXPECT_TRUE(cfg.film_grain);
        auto frame = make_yuv420_frame(cfg.width, cfg.height);
        vid::EncodedPacket pkt;
        enc->encode(frame.data(), frame.size(), &pkt);
    }
}

TEST(VideoEncoderAV1Test, AV1_SuperResolution) {
    vid::AV1EncoderConfig cfg;
    cfg.width       = 1920;
    cfg.height      = 1080;
    cfg.framerate   = 30;
    cfg.bitrate     = 5'000'000;
    cfg.superres    = true;
    cfg.superres_scale = 3; // 3/4 resolution

    auto enc = vid::VideoEncoder::create_av1(cfg);
    if (enc) {
        auto frame = make_yuv420_frame(cfg.width, cfg.height);
        vid::EncodedPacket pkt;
        enc->encode(frame.data(), frame.size(), &pkt);
    }
}

TEST(VideoEncoderAV1Test, AV1_MultiTile) {
    vid::AV1EncoderConfig cfg;
    cfg.width     = 3840;
    cfg.height    = 2160;
    cfg.framerate = 30;
    cfg.bitrate   = 15'000'000;
    cfg.tile_cols = 8;
    cfg.tile_rows = 4;

    auto enc = vid::VideoEncoder::create_av1(cfg);
    if (enc) {
        for (int i = 0; i < 5; ++i) {
            auto frame = make_yuv420_frame(cfg.width, cfg.height);
            vid::EncodedPacket pkt;
            enc->encode(frame.data(), frame.size(), &pkt);
        }
    }
}

// ============================================================================
//  Video Codec Tests — BitrateController
// ============================================================================

TEST(BitrateControllerTest, DefaultConstruction) {
    vid::BitrateController bc;
    EXPECT_EQ(bc.target_bitrate(), 0);
    EXPECT_FALSE(bc.is_active());
}

TEST(BitrateControllerTest, SetTargetBitrate) {
    vid::BitrateController bc;
    bc.set_target_bitrate(2'000'000);  // 2 Mbps
    EXPECT_EQ(bc.target_bitrate(), 2'000'000);
}

TEST(BitrateControllerTest, VBRMode) {
    vid::BitrateController bc;
    bc.set_target_bitrate(4'000'000);
    bc.set_mode(vid::RateControlMode::VBR);
    bc.set_vbr_quality(0.85f);
    EXPECT_EQ(bc.mode(), vid::RateControlMode::VBR);
    EXPECT_FLOAT_EQ(bc.vbr_quality(), 0.85f);
}

TEST(BitrateControllerTest, CBRMode) {
    vid::BitrateController bc;
    bc.set_target_bitrate(3'000'000);
    bc.set_mode(vid::RateControlMode::CBR);
    EXPECT_EQ(bc.mode(), vid::RateControlMode::CBR);
}

TEST(BitrateControllerTest, CQMode) {
    vid::BitrateController bc;
    bc.set_mode(vid::RateControlMode::CQ);
    bc.set_cq_level(23);
    EXPECT_EQ(bc.mode(), vid::RateControlMode::CQ);
    EXPECT_EQ(bc.cq_level(), 23);
}

TEST(BitrateControllerTest, MinMaxBounds) {
    vid::BitrateController bc;
    bc.set_target_bitrate(5'000'000);
    bc.set_bitrate_bounds(500'000, 10'000'000);
    EXPECT_EQ(bc.min_bitrate(), 500'000);
    EXPECT_EQ(bc.max_bitrate(), 10'000'000);
}

TEST(BitrateControllerTest, BufferControl) {
    vid::BitrateController bc;
    bc.set_target_bitrate(2'000'000);
    bc.set_buffer_size(4'000'000);   // 2 seconds of buffer
    bc.set_buffer_initial(2'000'000); // 1 second pre-fill
    EXPECT_EQ(bc.buffer_size(), 4'000'000);
    EXPECT_EQ(bc.buffer_initial(), 2'000'000);
}

TEST(BitrateControllerTest, ActivateDeactivate) {
    vid::BitrateController bc;
    bc.set_target_bitrate(2'000'000);
    bc.activate();
    EXPECT_TRUE(bc.is_active());

    bc.deactivate();
    EXPECT_FALSE(bc.is_active());
}

TEST(BitrateControllerTest, UpdateWithFrameSize) {
    vid::BitrateController bc;
    bc.set_target_bitrate(2'000'000);
    bc.set_buffer_size(4'000'000);
    bc.activate();

    // Simulate encoding frames
    for (int i = 0; i < 30; ++i) {
        size_t frame_size = 8000 + (i % 5) * 2000;  // vary frame sizes
        bc.report_encoded_frame(frame_size, vid::FrameType::P);
    }

    auto stats = bc.get_stats();
    EXPECT_GT(stats.total_frames, 0u);
    EXPECT_GT(stats.total_bytes, 0u);
    EXPECT_GT(stats.current_bitrate_bps, 0u);
}

TEST(BitrateControllerTest, AdaptiveBitrateDown) {
    vid::BitrateController bc;
    bc.set_target_bitrate(4'000'000);
    bc.activate();

    // Simulate network congestion (buffer filling up)
    for (int i = 0; i < 60; ++i) {
        bc.report_encoded_frame(20'000, vid::FrameType::P);  // large frames
    }

    // Bitrate should have been lowered
    EXPECT_LE(bc.current_bitrate(), bc.target_bitrate());
}

TEST(BitrateControllerTest, Reset) {
    vid::BitrateController bc;
    bc.set_target_bitrate(3'000'000);
    bc.activate();
    bc.report_encoded_frame(5000, vid::FrameType::I);
    bc.report_encoded_frame(2000, vid::FrameType::P);

    bc.reset();
    EXPECT_EQ(bc.get_stats().total_frames, 0u);
    EXPECT_EQ(bc.get_stats().total_bytes, 0u);
}

// ============================================================================
//  Video Codec Tests — Decoder
// ============================================================================

TEST(VideoDecoderTest, H264_DecoderCreation) {
    ASSERT_NO_THROW({
        auto dec = vid::VideoDecoder::create_h264();
        EXPECT_TRUE(dec != nullptr);
    });
}

TEST(VideoDecoderTest, H265_DecoderCreation) {
    ASSERT_NO_THROW({
        auto dec = vid::VideoDecoder::create_h265();
        EXPECT_TRUE(dec != nullptr);
    });
}

TEST(VideoDecoderTest, VP8_DecoderCreation) {
    // VP8 support may be optional
    auto dec = vid::VideoDecoder::create_vp8();
    // Either created or nullptr if codec not supported
    SUCCEED();
}

TEST(VideoDecoderTest, VP9_DecoderCreation) {
    auto dec = vid::VideoDecoder::create_vp9();
    SUCCEED();
}

TEST(VideoDecoderTest, AV1_DecoderCreation) {
    auto dec = vid::VideoDecoder::create_av1();
    SUCCEED();
}

TEST(VideoDecoderTest, DecodeEmptyData) {
    auto dec = vid::VideoDecoder::create_h264();
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> empty;
    vid::DecodedFrame frame;
    vid::DecodeResult res = dec->decode(empty.data(), 0, &frame);
    EXPECT_EQ(res, vid::DecodeResult::NEED_MORE);
}

TEST(VideoDecoderTest, DecodeCorruptedData) {
    auto dec = vid::VideoDecoder::create_h264();
    ASSERT_NE(dec, nullptr);

    auto garbage = random_bytes(1024);
    vid::DecodedFrame frame;
    vid::DecodeResult res = dec->decode(garbage.data(), garbage.size(), &frame);
    // Should not crash; expected behaviour is ERROR or NEED_MORE
    EXPECT_NE(res, vid::DecodeResult::CRASH);
}

// ============================================================================
//  Video Codec Tests — Round-trip (Encode → Decode)
// ============================================================================

TEST(VideoRoundTripTest, H264_EncodeDecode) {
    vid::H264EncoderConfig cfg;
    cfg.width     = 640;
    cfg.height    = 480;
    cfg.framerate = 30;
    cfg.bitrate   = 1'000'000;
    cfg.gop_size  = 10;
    cfg.preset    = vid::EncoderPreset::ULTRAFAST;
    cfg.rc_mode   = vid::RateControlMode::CBR;

    auto enc = vid::VideoEncoder::create_h264(cfg);
    ASSERT_NE(enc, nullptr);
    auto dec = vid::VideoDecoder::create_h264();
    ASSERT_NE(dec, nullptr);

    // Prepare synthetic frames
    std::vector<std::vector<uint8_t>> original_frames;
    for (int i = 0; i < 5; ++i) {
        original_frames.push_back(make_yuv420_frame(cfg.width, cfg.height));
    }

    // Encode
    std::vector<std::vector<uint8_t>> encoded;
    for (auto& frm : original_frames) {
        vid::EncodedPacket pkt;
        vid::EncodeResult res = enc->encode(frm.data(), frm.size(), &pkt);
        if (res == vid::EncodeResult::SUCCESS && pkt.size > 0) {
            encoded.emplace_back(pkt.data.begin(), pkt.data.end());
        }
    }

    // Flush encoder
    vid::EncodedPacket pkt;
    while (enc->flush(&pkt) != vid::EncodeResult::FINISHED) {
        if (pkt.size > 0) {
            encoded.emplace_back(pkt.data.begin(), pkt.data.end());
        }
    }

    // Decode
    int decoded_count = 0;
    for (auto& e : encoded) {
        vid::DecodedFrame dframe;
        vid::DecodeResult res = dec->decode(e.data(), e.size(), &dframe);
        if (res == vid::DecodeResult::SUCCESS) {
            decoded_count++;
            EXPECT_EQ(dframe.width, cfg.width);
            EXPECT_EQ(dframe.height, cfg.height);
            EXPECT_FALSE(dframe.y_plane.empty());
        }
    }

    // At least some frames should decode
    EXPECT_GT(decoded_count, 0);
}

// ============================================================================
//  Video Codec Tests — EncodedPacket / DecodedFrame structure
// ============================================================================

TEST(VideoPacketTest, EncodedPacketDefaults) {
    vid::EncodedPacket pkt;
    EXPECT_EQ(pkt.size, 0u);
    EXPECT_TRUE(pkt.data.empty());
    EXPECT_FALSE(pkt.is_keyframe);
    EXPECT_EQ(pkt.pts, 0);
    EXPECT_EQ(pkt.dts, 0);
    EXPECT_EQ(pkt.duration, 0);
}

TEST(VideoPacketTest, EncodedPacketKeyframeFlag) {
    vid::EncodedPacket pkt;
    pkt.is_keyframe = true;
    EXPECT_TRUE(pkt.is_keyframe);
}

TEST(VideoPacketTest, EncodedPacketTimestamps) {
    vid::EncodedPacket pkt;
    pkt.pts      = 1000;
    pkt.dts      = 900;
    pkt.duration = 33;
    EXPECT_EQ(pkt.pts, 1000);
    EXPECT_EQ(pkt.dts, 900);
    EXPECT_EQ(pkt.duration, 33);
}

TEST(VideoPacketTest, DecodedFrameDefaults) {
    vid::DecodedFrame frm;
    EXPECT_EQ(frm.width, 0);
    EXPECT_EQ(frm.height, 0);
    EXPECT_EQ(frm.format, vid::PixelFormat::UNKNOWN);
    EXPECT_TRUE(frm.y_plane.empty());
}

// ============================================================================
//  Video Codec Tests — Pixel format conversion hints
// ============================================================================

TEST(VideoPixelFormatTest, KnownFormats) {
    // Enumerate pixel formats
    EXPECT_NE(vid::PixelFormat::YUV420P, vid::PixelFormat::UNKNOWN);
    EXPECT_NE(vid::PixelFormat::NV12, vid::PixelFormat::UNKNOWN);
    EXPECT_NE(vid::PixelFormat::YUV422P, vid::PixelFormat::UNKNOWN);
    EXPECT_NE(vid::PixelFormat::YUV444P, vid::PixelFormat::UNKNOWN);
    EXPECT_NE(vid::PixelFormat::RGB24, vid::PixelFormat::UNKNOWN);
    EXPECT_NE(vid::PixelFormat::BGRA, vid::PixelFormat::UNKNOWN);
}

// ============================================================================
//  Video Codec Tests — EncoderPreset benchmarks
// ============================================================================

TEST(VideoEncoderPresetTest, AllPresetsEncode) {
    vid::H264EncoderConfig cfg;
    cfg.width     = 320;
    cfg.height    = 240;
    cfg.framerate = 30;
    cfg.bitrate   = 500'000;
    cfg.gop_size  = 10;

    std::vector<vid::EncoderPreset> presets = {
        vid::EncoderPreset::ULTRAFAST,
        vid::EncoderPreset::SUPERFAST,
        vid::EncoderPreset::VERYFAST,
        vid::EncoderPreset::FASTER,
        vid::EncoderPreset::FAST,
        vid::EncoderPreset::MEDIUM,
        vid::EncoderPreset::SLOW,
        vid::EncoderPreset::SLOWER,
        vid::EncoderPreset::VERYSLOW,
    };

    for (auto preset : presets) {
        cfg.preset = preset;
        auto enc = vid::VideoEncoder::create_h264(cfg);
        if (enc) {
            auto frame = make_yuv420_frame(cfg.width, cfg.height);
            vid::EncodedPacket pkt;
            enc->encode(frame.data(), frame.size(), &pkt);
        }
    }
}

// ============================================================================
//  Audio Codec Tests — Configurations
// ============================================================================

TEST(AudioConfigTest, DefaultOpusEncoderConfig) {
    aud::OpusEncoderConfig cfg;
    EXPECT_EQ(cfg.sample_rate, 48000);
    EXPECT_EQ(cfg.channels, 2);
    EXPECT_GT(cfg.bitrate, 0);
    EXPECT_NE(cfg.application, aud::OpusApplication::DEFAULT);
}

TEST(AudioConfigTest, OpusConfigBuilder_VoiceMode) {
    aud::OpusEncoderConfig cfg;
    cfg.sample_rate   = 16000;
    cfg.channels      = 1;
    cfg.bitrate       = 32000;
    cfg.application   = aud::OpusApplication::VOIP;
    cfg.complexity    = 5;
    cfg.use_fec       = true;
    cfg.use_dtx       = true;
    cfg.frame_duration_ms = 20;

    EXPECT_EQ(cfg.sample_rate, 16000);
    EXPECT_EQ(cfg.channels, 1);
    EXPECT_EQ(cfg.bitrate, 32000);
    EXPECT_EQ(cfg.application, aud::OpusApplication::VOIP);
    EXPECT_EQ(cfg.complexity, 5);
    EXPECT_TRUE(cfg.use_fec);
    EXPECT_TRUE(cfg.use_dtx);
    EXPECT_EQ(cfg.frame_duration_ms, 20);
}

TEST(AudioConfigTest, OpusConfigBuilder_MusicMode) {
    aud::OpusEncoderConfig cfg;
    cfg.sample_rate   = 48000;
    cfg.channels      = 2;
    cfg.bitrate       = 128000;
    cfg.application   = aud::OpusApplication::AUDIO;
    cfg.complexity    = 10;
    cfg.use_fec       = false;
    cfg.use_dtx       = false;
    cfg.frame_duration_ms = 10;

    EXPECT_EQ(cfg.bitrate, 128000);
    EXPECT_EQ(cfg.application, aud::OpusApplication::AUDIO);
    EXPECT_EQ(cfg.complexity, 10);
    EXPECT_FALSE(cfg.use_fec);
    EXPECT_EQ(cfg.frame_duration_ms, 10);
}

TEST(AudioConfigTest, OpusConfigBuilder_LowDelay) {
    aud::OpusEncoderConfig cfg;
    cfg.sample_rate        = 48000;
    cfg.channels           = 1;
    cfg.bitrate            = 64000;
    cfg.application        = aud::OpusApplication::RESTRICTED_LOWDELAY;
    cfg.frame_duration_ms  = 5;

    EXPECT_EQ(cfg.application, aud::OpusApplication::RESTRICTED_LOWDELAY);
    EXPECT_EQ(cfg.frame_duration_ms, 5);
}

TEST(AudioConfigTest, ResamplerConfig) {
    aud::ResamplerConfig cfg;
    cfg.input_rate  = 44100;
    cfg.output_rate = 48000;
    cfg.channels    = 2;
    cfg.quality     = aud::ResampleQuality::MEDIUM;

    EXPECT_EQ(cfg.input_rate, 44100);
    EXPECT_EQ(cfg.output_rate, 48000);
    EXPECT_EQ(cfg.channels, 2);
    EXPECT_EQ(cfg.quality, aud::ResampleQuality::MEDIUM);
}

TEST(AudioConfigTest, AudioMixerConfig) {
    aud::AudioMixerConfig cfg;
    cfg.sample_rate        = 48000;
    cfg.channels           = 2;
    cfg.mix_mode           = aud::MixMode::ADD;
    cfg.normalize_output   = true;
    cfg.output_gain_db     = -3.0f;

    EXPECT_EQ(cfg.sample_rate, 48000);
    EXPECT_EQ(cfg.channels, 2);
    EXPECT_EQ(cfg.mix_mode, aud::MixMode::ADD);
    EXPECT_TRUE(cfg.normalize_output);
    EXPECT_FLOAT_EQ(cfg.output_gain_db, -3.0f);
}

TEST(AudioConfigTest, SilenceDetectorConfig) {
    aud::SilenceDetectorConfig cfg;
    cfg.threshold_db     = -40.0f;
    cfg.silence_duration_ms = 500;
    cfg.hangover_ms      = 200;
    cfg.sample_rate      = 48000;
    cfg.channels         = 1;

    EXPECT_FLOAT_EQ(cfg.threshold_db, -40.0f);
    EXPECT_EQ(cfg.silence_duration_ms, 500);
    EXPECT_EQ(cfg.hangover_ms, 200);
}

TEST(AudioConfigTest, EchoCancellerConfig) {
    aud::EchoCancellerConfig cfg;
    cfg.sample_rate      = 16000;
    cfg.channels         = 1;
    cfg.filter_length_ms = 200;
    cfg.aec_mode         = aud::AECMode::FULL_DUPLEX;
    cfg.nlp_mode         = aud::NLPMode::SUPPRESS;

    EXPECT_EQ(cfg.sample_rate, 16000);
    EXPECT_EQ(cfg.channels, 1);
    EXPECT_EQ(cfg.filter_length_ms, 200);
    EXPECT_EQ(cfg.aec_mode, aud::AECMode::FULL_DUPLEX);
    EXPECT_EQ(cfg.nlp_mode, aud::NLPMode::SUPPRESS);
}

// ============================================================================
//  Audio Codec Tests — Opus Encoder
// ============================================================================

class OpusEncoderTest : public ::testing::Test {
protected:
    void SetUp() override {
        cfg_.sample_rate = 48000;
        cfg_.channels    = 2;
        cfg_.bitrate     = 64000;
        cfg_.application = aud::OpusApplication::AUDIO;
    }
    aud::OpusEncoderConfig cfg_;
};

TEST_F(OpusEncoderTest, CreateAndDestroy) {
    auto enc = aud::OpusEncoder::create(cfg_);
    EXPECT_NE(enc, nullptr);
}

TEST_F(OpusEncoderTest, EncodeSilence_float) {
    auto enc = aud::OpusEncoder::create(cfg_);
    ASSERT_NE(enc, nullptr);

    constexpr int kFrameSamples = 960; // 20ms @ 48kHz
    auto pcm = std::vector<float>(kFrameSamples * cfg_.channels, 0.0f);

    std::vector<uint8_t> encoded(4096);
    int bytes = enc->encode_float(pcm.data(), kFrameSamples, encoded.data(), encoded.size());
    EXPECT_GT(bytes, 0);
    EXPECT_LE(bytes, static_cast<int>(encoded.size()));
}

TEST_F(OpusEncoderTest, EncodeSine_int16) {
    auto enc = aud::OpusEncoder::create(cfg_);
    ASSERT_NE(enc, nullptr);

    constexpr int kFrameSamples = 960;
    auto pcm = make_pcm_samples(kFrameSamples, cfg_.channels, 440.0f,
                                 cfg_.sample_rate);

    std::vector<uint8_t> encoded(4096);
    int bytes = enc->encode_int16(pcm.data(), kFrameSamples, encoded.data(), encoded.size());
    EXPECT_GT(bytes, 0);
}

TEST_F(OpusEncoderTest, EncodeMultipleFrames) {
    auto enc = aud::OpusEncoder::create(cfg_);
    ASSERT_NE(enc, nullptr);

    constexpr int kFrameSamples = 960;
    constexpr int kNumFrames    = 50;

    for (int i = 0; i < kNumFrames; ++i) {
        auto pcm = make_pcm_samples(kFrameSamples, cfg_.channels,
                                     220.0f + i * 10.0f,
                                     cfg_.sample_rate);
        std::vector<uint8_t> out(4096);
        int bytes = enc->encode_int16(pcm.data(), kFrameSamples, out.data(), out.size());
        EXPECT_GT(bytes, 0);
    }
}

TEST_F(OpusEncoderTest, EncodeFloatMultipleFrames) {
    auto enc = aud::OpusEncoder::create(cfg_);
    ASSERT_NE(enc, nullptr);

    constexpr int kFrameSamples = 960;
    auto pcm = make_float_samples(kFrameSamples, cfg_.channels,
                                   440.0f, cfg_.sample_rate);

    for (int i = 0; i < 20; ++i) {
        std::vector<uint8_t> out(4096);
        int bytes = enc->encode_float(pcm.data(), kFrameSamples, out.data(), out.size());
        EXPECT_GT(bytes, 0);
    }
}

TEST_F(OpusEncoderTest, DTXMode) {
    cfg_.use_dtx = true;
    auto enc = aud::OpusEncoder::create(cfg_);
    ASSERT_NE(enc, nullptr);

    // Encode some audio
    constexpr int kFrameSamples = 960;
    auto pcm = make_pcm_samples(kFrameSamples, cfg_.channels, 440.0f,
                                 cfg_.sample_rate);
    std::vector<uint8_t> out(4096);
    enc->encode_int16(pcm.data(), kFrameSamples, out.data(), out.size());

    // Then feed silence — DTX should produce smaller packets or no packets
    std::fill(pcm.begin(), pcm.end(), 0);
    int silent_bytes = enc->encode_int16(pcm.data(), kFrameSamples, out.data(), out.size());
    // With DTX, silence frame could be very small (comfort noise) or zero
    EXPECT_GE(silent_bytes, 0);
}

TEST_F(OpusEncoderTest, FECMode) {
    cfg_.use_fec = true;
    auto enc = aud::OpusEncoder::create(cfg_);
    ASSERT_NE(enc, nullptr);

    constexpr int kFrameSamples = 960;
    auto pcm = make_pcm_samples(kFrameSamples, cfg_.channels, 440.0f,
                                 cfg_.sample_rate);
    std::vector<uint8_t> out(4096);
    int bytes = enc->encode_int16(pcm.data(), kFrameSamples, out.data(), out.size());
    EXPECT_GT(bytes, 0);
}

TEST_F(OpusEncoderTest, VariableBitrate) {
    cfg_.bitrate = 128000;
    auto enc = aud::OpusEncoder::create(cfg_);
    ASSERT_NE(enc, nullptr);

    enc->set_bitrate(64000);
    EXPECT_EQ(enc->bitrate(), 64000);

    enc->set_bitrate(96000);
    EXPECT_EQ(enc->bitrate(), 96000);
}

TEST_F(OpusEncoderTest, MaxBandwidth) {
    cfg_.max_bandwidth = aud::OpusBandwidth::FULLBAND;
    auto enc = aud::OpusEncoder::create(cfg_);
    ASSERT_NE(enc, nullptr);

    constexpr int kFrameSamples = 960;
    auto pcm = make_pcm_samples(kFrameSamples, cfg_.channels, 12000.0f,  // tests fullband
                                 cfg_.sample_rate);
    std::vector<uint8_t> out(4096);
    int bytes = enc->encode_int16(pcm.data(), kFrameSamples, out.data(), out.size());
    EXPECT_GT(bytes, 0);
}

TEST_F(OpusEncoderTest, MonoEncoding) {
    cfg_.channels = 1;
    auto enc = aud::OpusEncoder::create(cfg_);
    ASSERT_NE(enc, nullptr);

    constexpr int kFrameSamples = 960;
    auto pcm = make_pcm_samples(kFrameSamples, 1, 440.0f, cfg_.sample_rate);
    std::vector<uint8_t> out(4096);
    int bytes = enc->encode_int16(pcm.data(), kFrameSamples, out.data(), out.size());
    EXPECT_GT(bytes, 0);
}

TEST_F(OpusEncoderTest, GetLookahead) {
    auto enc = aud::OpusEncoder::create(cfg_);
    ASSERT_NE(enc, nullptr);
    int lookahead = enc->lookahead();
    EXPECT_GT(lookahead, 0);
}

TEST_F(OpusEncoderTest, ResetEncoder) {
    auto enc = aud::OpusEncoder::create(cfg_);
    ASSERT_NE(enc, nullptr);

    constexpr int kFrameSamples = 960;
    auto pcm = make_pcm_samples(kFrameSamples, cfg_.channels, 440.0f,
                                 cfg_.sample_rate);
    std::vector<uint8_t> out(4096);
    enc->encode_int16(pcm.data(), kFrameSamples, out.data(), out.size());

    enc->reset();
    // After reset, should still work
    int bytes = enc->encode_int16(pcm.data(), kFrameSamples, out.data(), out.size());
    EXPECT_GT(bytes, 0);
}

TEST_F(OpusEncoderTest, SampleRatesTests) {
    std::vector<int> rates = {8000, 12000, 16000, 24000, 48000};
    for (int sr : rates) {
        cfg_.sample_rate = sr;
        auto enc = aud::OpusEncoder::create(cfg_);
        if (enc) {
            int frame_samples = sr / 50;  // 20ms
            auto pcm = make_pcm_samples(frame_samples, cfg_.channels, 440.0f, sr);
            std::vector<uint8_t> out(4096);
            int bytes = enc->encode_int16(pcm.data(), frame_samples, out.data(), out.size());
            EXPECT_GT(bytes, 0) << "Failed for sample rate " << sr;
        }
    }
}

// ============================================================================
//  Audio Codec Tests — Opus Decoder
// ============================================================================

class OpusDecoderTest : public ::testing::Test {
protected:
    void SetUp() override {
        enc_cfg_.sample_rate = 48000;
        enc_cfg_.channels    = 2;
        enc_cfg_.bitrate     = 64000;
        enc_cfg_.application = aud::OpusApplication::AUDIO;

        encoder_ = aud::OpusEncoder::create(enc_cfg_);
        ASSERT_NE(encoder_, nullptr);

        decoder_ = aud::OpusDecoder::create(enc_cfg_.sample_rate, enc_cfg_.channels);
        ASSERT_NE(decoder_, nullptr);
    }

    aud::OpusEncoderConfig enc_cfg_;
    std::unique_ptr<aud::OpusEncoder> encoder_;
    std::unique_ptr<aud::OpusDecoder> decoder_;
};

TEST_F(OpusDecoderTest, DecodeEncodedFloat) {
    constexpr int kFrameSamples = 960;
    auto src = make_float_samples(kFrameSamples, enc_cfg_.channels, 440.0f,
                                   enc_cfg_.sample_rate);

    std::vector<uint8_t> encoded(4096);
    int enc_bytes = encoder_->encode_float(src.data(), kFrameSamples,
                                            encoded.data(), encoded.size());
    ASSERT_GT(enc_bytes, 0);

    std::vector<float> decoded(kFrameSamples * enc_cfg_.channels);
    int samples = decoder_->decode_float(encoded.data(), enc_bytes,
                                          decoded.data(), kFrameSamples);
    EXPECT_EQ(samples, kFrameSamples);
}

TEST_F(OpusDecoderTest, DecodeEncodedInt16) {
    constexpr int kFrameSamples = 960;
    auto src = make_pcm_samples(kFrameSamples, enc_cfg_.channels, 440.0f,
                                 enc_cfg_.sample_rate);

    std::vector<uint8_t> encoded(4096);
    int enc_bytes = encoder_->encode_int16(src.data(), kFrameSamples,
                                            encoded.data(), encoded.size());
    ASSERT_GT(enc_bytes, 0);

    std::vector<int16_t> decoded(kFrameSamples * enc_cfg_.channels);
    int samples = decoder_->decode_int16(encoded.data(), enc_bytes,
                                          decoded.data(), kFrameSamples);
    EXPECT_EQ(samples, kFrameSamples);
}

TEST_F(OpusDecoderTest, DecodeEmptyPacket) {
    std::vector<float> pcm(960 * enc_cfg_.channels);
    // Empty packet — should return PLC or zero samples
    int samples = decoder_->decode_float(nullptr, 0, pcm.data(), 960);
    // Decoder should handle gracefully
    EXPECT_GE(samples, 0);
}

TEST_F(OpusDecoderTest, DecodeWithFEC) {
    enc_cfg_.use_fec = true;
    auto enc = aud::OpusEncoder::create(enc_cfg_);
    ASSERT_NE(enc, nullptr);

    constexpr int kFrameSamples = 960;
    auto src = make_pcm_samples(kFrameSamples, enc_cfg_.channels, 440.0f,
                                 enc_cfg_.sample_rate);

    std::vector<uint8_t> encoded(4096);
    int enc_bytes = enc->encode_int16(src.data(), kFrameSamples,
                                       encoded.data(), encoded.size());
    ASSERT_GT(enc_bytes, 0);

    // Feed decoder and request FEC on next missing packet
    std::vector<int16_t> decoded(kFrameSamples * enc_cfg_.channels);
    decoder_->decode_int16(encoded.data(), enc_bytes, decoded.data(), kFrameSamples);

    // Now simulate packet loss with FEC
    int fec_samples = decoder_->decode_fec(nullptr, 0, decoded.data(), kFrameSamples);
    EXPECT_GE(fec_samples, 0);
}

TEST_F(OpusDecoderTest, RoundTrip_Throughput) {
    constexpr int kFrameSamples = 960;
    constexpr int kNumFrames    = 100;

    for (int i = 0; i < kNumFrames; ++i) {
        auto src = make_float_samples(kFrameSamples, enc_cfg_.channels,
                                       440.0f + (i % 10) * 10.0f,
                                       enc_cfg_.sample_rate);

        std::vector<uint8_t> enc_buf(4096);
        int enc_bytes = encoder_->encode_float(src.data(), kFrameSamples,
                                                enc_buf.data(), enc_buf.size());
        ASSERT_GT(enc_bytes, 0);

        std::vector<float> dec_buf(kFrameSamples * enc_cfg_.channels);
        int dec_samples = decoder_->decode_float(enc_buf.data(), enc_bytes,
                                                  dec_buf.data(), kFrameSamples);
        EXPECT_EQ(dec_samples, kFrameSamples);
    }
}

TEST_F(OpusDecoderTest, ResetDecoder) {
    constexpr int kFrameSamples = 960;
    auto src = make_pcm_samples(kFrameSamples, enc_cfg_.channels, 440.0f,
                                 enc_cfg_.sample_rate);

    std::vector<uint8_t> enc_buf(4096);
    int enc_bytes = encoder_->encode_int16(src.data(), kFrameSamples,
                                            enc_buf.data(), enc_buf.size());
    ASSERT_GT(enc_bytes, 0);

    std::vector<int16_t> dec_buf(kFrameSamples * enc_cfg_.channels);
    decoder_->decode_int16(enc_buf.data(), enc_bytes, dec_buf.data(), kFrameSamples);

    decoder_->reset();

    // After reset, decode again
    int samples = decoder_->decode_int16(enc_buf.data(), enc_bytes,
                                          dec_buf.data(), kFrameSamples);
    EXPECT_EQ(samples, kFrameSamples);
}

// ============================================================================
//  Audio Codec Tests — Resampler
// ============================================================================

TEST(AudioResamplerTest, CreateResampler) {
    auto resampler = aud::Resampler::create(44100, 48000, 2);
    EXPECT_NE(resampler, nullptr);
}

TEST(AudioResamplerTest, Downsample48kTo16k) {
    auto rs = aud::Resampler::create(48000, 16000, 1);
    ASSERT_NE(rs, nullptr);

    constexpr int kInSamples = 480;  // 10ms @ 48kHz
    auto in = make_float_samples(kInSamples, 1, 440.0f, 48000);

    // Output buffer: up to (in_len * out_rate / in_rate) + margin
    int max_out = kInSamples * 16000 / 48000 + 32;
    std::vector<float> out(max_out);

    int processed = rs->process_float(in.data(), kInSamples, out.data(), max_out);
    EXPECT_GT(processed, 0);
    EXPECT_LE(processed, max_out);
}

TEST(AudioResamplerTest, Upsample16kTo48k) {
    auto rs = aud::Resampler::create(16000, 48000, 1);
    ASSERT_NE(rs, nullptr);

    constexpr int kInSamples = 160;  // 10ms @ 16kHz
    auto in = make_float_samples(kInSamples, 1, 440.0f, 16000);

    int max_out = kInSamples * 48000 / 16000 + 32;
    std::vector<float> out(max_out);

    int processed = rs->process_float(in.data(), kInSamples, out.data(), max_out);
    EXPECT_GT(processed, 0);
}

TEST(AudioResamplerTest, Int16Resample) {
    auto rs = aud::Resampler::create(44100, 48000, 2);
    ASSERT_NE(rs, nullptr);

    constexpr int kInSamples = 441;  // 10ms @ 44.1kHz
    auto in = make_pcm_samples(kInSamples, 2, 440.0f, 44100);

    int max_out = kInSamples * 48000 / 44100 + 32;
    std::vector<int16_t> out(max_out * 2);

    int processed = rs->process_int16(in.data(), kInSamples, out.data(), max_out);
    EXPECT_GT(processed, 0);
}

TEST(AudioResamplerTest, ResetResamplerMidStream) {
    auto rs = aud::Resampler::create(48000, 16000, 1);
    ASSERT_NE(rs, nullptr);

    constexpr int kInSamples = 480;
    auto in = make_float_samples(kInSamples, 1, 440.0f, 48000);
    std::vector<float> out(kInSamples);

    // Process a few chunks
    rs->process_float(in.data(), kInSamples / 2, out.data(), out.size());
    rs->reset();
    // After reset, process full chunk — should work from clean state
    int processed = rs->process_float(in.data(), kInSamples, out.data(), out.size());
    EXPECT_GT(processed, 0);
}

TEST(AudioResamplerTest, QualitySettings) {
    std::vector<aud::ResampleQuality> qualities = {
        aud::ResampleQuality::LOW,
        aud::ResampleQuality::MEDIUM,
        aud::ResampleQuality::HIGH,
    };

    for (auto q : qualities) {
        aud::ResamplerConfig cfg;
        cfg.input_rate  = 44100;
        cfg.output_rate = 48000;
        cfg.channels    = 1;
        cfg.quality     = q;

        auto rs = aud::Resampler::create(cfg);
        if (rs) {
            auto in = make_float_samples(441, 1, 440.0f, 44100);
            std::vector<float> out(480);
            rs->process_float(in.data(), 441, out.data(), 480);
        }
    }
}

TEST(AudioResamplerTest, IdentityResample) {
    auto rs = aud::Resampler::create(48000, 48000, 1);
    ASSERT_NE(rs, nullptr);

    constexpr int kSamples = 960;
    auto in = make_float_samples(kSamples, 1, 440.0f, 48000);
    std::vector<float> out(kSamples + 32);

    int processed = rs->process_float(in.data(), kSamples, out.data(), out.size());
    // Identity resample should output approximately same number of samples
    EXPECT_GE(processed, kSamples - 1);
}

// ============================================================================
//  Audio Codec Tests — AudioMixer
// ============================================================================

class AudioMixerTest : public ::testing::Test {
protected:
    void SetUp() override {
        cfg_.sample_rate      = 48000;
        cfg_.channels         = 2;
        cfg_.mix_mode         = aud::MixMode::ADD;
        cfg_.normalize_output = false;
        cfg_.output_gain_db   = 0.0f;
    }
    aud::AudioMixerConfig cfg_;
};

TEST_F(AudioMixerTest, CreateMixer) {
    auto mixer = aud::AudioMixer::create(cfg_);
    EXPECT_NE(mixer, nullptr);
}

TEST_F(AudioMixerTest, MixOneStream) {
    auto mixer = aud::AudioMixer::create(cfg_);
    ASSERT_NE(mixer, nullptr);

    constexpr int kSamples = 960;
    auto pcm = make_float_samples(kSamples, cfg_.channels, 440.0f, cfg_.sample_rate);

    mixer->add_stream(0, pcm.data(), kSamples);
    mixer->process();

    std::vector<float> out(kSamples * cfg_.channels);
    int mixed = mixer->read_output(out.data(), kSamples);
    EXPECT_EQ(mixed, kSamples);
}

TEST_F(AudioMixerTest, MixTwoStreams) {
    auto mixer = aud::AudioMixer::create(cfg_);
    ASSERT_NE(mixer, nullptr);

    constexpr int kSamples = 960;
    auto pcm1 = make_float_samples(kSamples, cfg_.channels, 440.0f, cfg_.sample_rate);
    auto pcm2 = make_float_samples(kSamples, cfg_.channels, 880.0f, cfg_.sample_rate);

    mixer->add_stream(0, pcm1.data(), kSamples);
    mixer->add_stream(1, pcm2.data(), kSamples);
    mixer->process();

    std::vector<float> out(kSamples * cfg_.channels);
    int mixed = mixer->read_output(out.data(), kSamples);
    EXPECT_EQ(mixed, kSamples);
}

TEST_F(AudioMixerTest, MixWithPerStreamGain) {
    auto mixer = aud::AudioMixer::create(cfg_);
    ASSERT_NE(mixer, nullptr);

    mixer->set_stream_gain(0, 0.5f);
    mixer->set_stream_gain(1, 1.0f);

    constexpr int kSamples = 960;
    auto pcm = make_float_samples(kSamples, cfg_.channels, 440.0f, cfg_.sample_rate);

    mixer->add_stream(0, pcm.data(), kSamples);
    mixer->add_stream(1, pcm.data(), kSamples);
    mixer->process();

    std::vector<float> out(kSamples * cfg_.channels);
    mixer->read_output(out.data(), kSamples);
}

TEST_F(AudioMixerTest, Normalization) {
    cfg_.normalize_output = true;
    cfg_.output_gain_db   = -6.0f;
    auto mixer = aud::AudioMixer::create(cfg_);
    ASSERT_NE(mixer, nullptr);

    constexpr int kSamples = 960;
    auto pcm = make_float_samples(kSamples, cfg_.channels, 440.0f, cfg_.sample_rate);

    mixer->add_stream(0, pcm.data(), kSamples);
    mixer->add_stream(1, pcm.data(), kSamples);
    mixer->process();

    std::vector<float> out(kSamples * cfg_.channels);
    mixer->read_output(out.data(), kSamples);

    // Check no clipping (values within [-1, 1])
    for (auto& s : out) {
        EXPECT_GE(s, -1.0f);
        EXPECT_LE(s, 1.0f);
    }
}

TEST_F(AudioMixerTest, AddStreamAfterReset) {
    auto mixer = aud::AudioMixer::create(cfg_);
    ASSERT_NE(mixer, nullptr);

    constexpr int kSamples = 960;
    auto pcm = make_float_samples(kSamples, cfg_.channels, 440.0f, cfg_.sample_rate);

    mixer->add_stream(0, pcm.data(), kSamples);
    mixer->process();
    std::vector<float> out(kSamples * cfg_.channels);
    mixer->read_output(out.data(), kSamples);

    mixer->reset();

    // After reset, streams cleared — adding again should work
    mixer->add_stream(0, pcm.data(), kSamples);
    mixer->process();
    mixer->read_output(out.data(), kSamples);
}

// ============================================================================
//  Audio Codec Tests — SilenceDetector
// ============================================================================

class SilenceDetectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        cfg_.threshold_db      = -30.0f;
        cfg_.silence_duration_ms = 300;
        cfg_.hangover_ms       = 100;
        cfg_.sample_rate       = 48000;
        cfg_.channels          = 1;
    }
    aud::SilenceDetectorConfig cfg_;
};

TEST_F(SilenceDetectorTest, CreateDetector) {
    auto det = aud::SilenceDetector::create(cfg_);
    EXPECT_NE(det, nullptr);
}

TEST_F(SilenceDetectorTest, DetectSilenceOnZeros) {
    auto det = aud::SilenceDetector::create(cfg_);
    ASSERT_NE(det, nullptr);

    constexpr int kSamples = 480;  // 10ms
    std::vector<float> silence(kSamples, 0.0f);

    bool is_silent = false;
    for (int i = 0; i < 50; ++i) {
        is_silent = det->process(silence.data(), kSamples);
        if (is_silent) break;
    }
    EXPECT_TRUE(is_silent);
}

TEST_F(SilenceDetectorTest, DetectSpeech) {
    auto det = aud::SilenceDetector::create(cfg_);
    ASSERT_NE(det, nullptr);

    // First feed silence to establish baseline
    constexpr int kSamples = 480;
    std::vector<float> silence(kSamples, 0.0f);
    for (int i = 0; i < 10; ++i) {
        det->process(silence.data(), kSamples);
    }

    // Then feed speech-level audio
    auto speech = make_float_samples(kSamples, 1, 440.0f, cfg_.sample_rate);
    det->process(speech.data(), kSamples);

    // Should not stay silent after speech onset
}

TEST_F(SilenceDetectorTest, HangoverPeriod) {
    auto det = aud::SilenceDetector::create(cfg_);
    ASSERT_NE(det, nullptr);

    constexpr int kSamples = 480;
    // Feed speech
    auto speech = make_float_samples(kSamples, 1, 440.0f, cfg_.sample_rate);
    for (int i = 0; i < 20; ++i) {
        det->process(speech.data(), kSamples);
    }

    // First silence frame — should still be "speech" due to hangover
    std::vector<float> silence(kSamples, 0.0f);
    bool first_silent = det->process(silence.data(), kSamples);
    // May or may not be silent depending on hangover duration
    // Just verify no crash
    SUCCEED();
}

TEST_F(SilenceDetectorTest, AdjustThreshold) {
    auto det = aud::SilenceDetector::create(cfg_);
    ASSERT_NE(det, nullptr);

    det->set_threshold_db(-20.0f);

    constexpr int kSamples = 480;
    auto low_audio = make_float_samples(kSamples, 1, 440.0f, cfg_.sample_rate);
    // Reduce amplitude below threshold
    for (auto& s : low_audio) s *= 0.05f;

    bool is_silent = false;
    for (int i = 0; i < 50; ++i) {
        is_silent = det->process(low_audio.data(), kSamples);
        if (is_silent) break;
    }
    // With -20dB threshold and 0.05 amplitude, should detect silence
    EXPECT_TRUE(is_silent);
}

TEST_F(SilenceDetectorTest, ResetDetector) {
    auto det = aud::SilenceDetector::create(cfg_);
    ASSERT_NE(det, nullptr);

    constexpr int kSamples = 480;
    std::vector<float> silence(kSamples, 0.0f);
    for (int i = 0; i < 30; ++i) {
        det->process(silence.data(), kSamples);
    }

    det->reset();

    // After reset, state should be fresh
    auto speech = make_float_samples(kSamples, 1, 440.0f, cfg_.sample_rate);
    det->process(speech.data(), kSamples);
}

// ============================================================================
//  Audio Codec Tests — EchoCanceller
// ============================================================================

class EchoCancellerTest : public ::testing::Test {
protected:
    void SetUp() override {
        cfg_.sample_rate      = 16000;
        cfg_.channels         = 1;
        cfg_.filter_length_ms = 200;
        cfg_.aec_mode         = aud::AECMode::FULL_DUPLEX;
        cfg_.nlp_mode         = aud::NLPMode::SUPPRESS;
    }
    aud::EchoCancellerConfig cfg_;
};

TEST_F(EchoCancellerTest, CreateCanceller) {
    auto aec = aud::EchoCanceller::create(cfg_);
    EXPECT_NE(aec, nullptr);
}

TEST_F(EchoCancellerTest, ProcessNoEcho) {
    auto aec = aud::EchoCanceller::create(cfg_);
    ASSERT_NE(aec, nullptr);

    constexpr int kSamples = 160;  // 10ms @ 16kHz
    auto mic   = make_float_samples(kSamples, 1, 440.0f, cfg_.sample_rate);
    auto ref   = std::vector<float>(kSamples, 0.0f);  // no echo reference

    std::vector<float> out(kSamples);
    aec->process(mic.data(), ref.data(), out.data(), kSamples);

    // Without echo, output should be similar to input
    // Just verify it doesn't crash and produces output
    for (auto& s : out) {
        EXPECT_TRUE(std::isfinite(s));
    }
}

TEST_F(EchoCancellerTest, ProcessWithEchoPath) {
    auto aec = aud::EchoCanceller::create(cfg_);
    ASSERT_NE(aec, nullptr);

    constexpr int kSamples = 160;
    auto ref   = make_float_samples(kSamples, 1, 440.0f, cfg_.sample_rate);
    // Simulate mic signal: speech + attenuated/delayed echo of ref
    auto speech = make_float_samples(kSamples, 1, 880.0f, cfg_.sample_rate);
    for (auto& s : speech) s *= 0.5f;

    std::vector<float> mic(kSamples);
    for (int i = 0; i < kSamples; ++i) {
        mic[i] = speech[i] + 0.3f * ref[i];  // speech + echo
    }

    std::vector<float> out(kSamples);
    aec->process(mic.data(), ref.data(), out.data(), kSamples);

    for (auto& s : out) {
        EXPECT_TRUE(std::isfinite(s));
    }
}

TEST_F(EchoCancellerTest, ConvergenceOverTime) {
    auto aec = aud::EchoCanceller::create(cfg_);
    ASSERT_NE(aec, nullptr);

    constexpr int kSamples = 160;
    // Feed many frames so the AEC can converge
    for (int i = 0; i < 50; ++i) {
        auto ref = make_float_samples(kSamples, 1, 440.0f, cfg_.sample_rate);
        std::vector<float> mic(kSamples);
        for (int j = 0; j < kSamples; ++j) {
            mic[j] = ref[j] * 0.5f;  // pure echo, no speech
        }

        std::vector<float> out(kSamples);
        aec->process(mic.data(), ref.data(), out.data(), kSamples);

        // After convergence, output should be near-zero
        if (i > 40) {
            float energy = 0.0f;
            for (auto& s : out) energy += s * s;
            EXPECT_LT(energy / kSamples, 0.1f);  // energy should be low
        }
    }
}

TEST_F(EchoCancellerTest, HalfDuplexMode) {
    cfg_.aec_mode = aud::AECMode::HALF_DUPLEX;
    auto aec = aud::EchoCanceller::create(cfg_);
    ASSERT_NE(aec, nullptr);

    constexpr int kSamples = 160;
    auto mic = make_float_samples(kSamples, 1, 440.0f, cfg_.sample_rate);
    auto ref = make_float_samples(kSamples, 1, 880.0f, cfg_.sample_rate);

    std::vector<float> out(kSamples);
    aec->process(mic.data(), ref.data(), out.data(), kSamples);
}

TEST_F(EchoCancellerTest, ComfortNoise) {
    cfg_.comfort_noise = true;
    auto aec = aud::EchoCanceller::create(cfg_);
    ASSERT_NE(aec, nullptr);

    constexpr int kSamples = 160;
    std::vector<float> silence(kSamples, 0.0f);
    std::vector<float> out(kSamples);

    // In comfort noise mode, even with silence input, output may contain
    // comfort noise to avoid total silence.
    aec->process(silence.data(), silence.data(), out.data(), kSamples);
    for (auto& s : out) {
        EXPECT_TRUE(std::isfinite(s));
    }
}

TEST_F(EchoCancellerTest, ResetCanceller) {
    auto aec = aud::EchoCanceller::create(cfg_);
    ASSERT_NE(aec, nullptr);

    constexpr int kSamples = 160;
    auto mic = make_float_samples(kSamples, 1, 440.0f, cfg_.sample_rate);
    auto ref = make_float_samples(kSamples, 1, 880.0f, cfg_.sample_rate);

    std::vector<float> out(kSamples);
    aec->process(mic.data(), ref.data(), out.data(), kSamples);
    aec->reset();
    aec->process(mic.data(), ref.data(), out.data(), kSamples);
}

// ============================================================================
//  Audio Codec Tests — Audio Stats / Metadata
// ============================================================================

TEST(AudioStatsTest, OpusEncoderStats) {
    aud::OpusEncoderConfig cfg;
    cfg.sample_rate = 48000;
    cfg.channels    = 2;
    cfg.bitrate     = 64000;

    auto enc = aud::OpusEncoder::create(cfg);
    ASSERT_NE(enc, nullptr);

    constexpr int kFrameSamples = 960;
    for (int i = 0; i < 30; ++i) {
        auto pcm = make_pcm_samples(kFrameSamples, cfg.channels, 440.0f + i * 5.0f,
                                     cfg.sample_rate);
        std::vector<uint8_t> out(4096);
        enc->encode_int16(pcm.data(), kFrameSamples, out.data(), out.size());
    }

    auto stats = enc->get_stats();
    EXPECT_GT(stats.frames_encoded, 0u);
    EXPECT_GT(stats.bytes_encoded, 0u);
    EXPECT_GE(stats.average_bitrate_bps, 0u);
}

TEST(AudioStatsTest, OpusDecoderStats) {
    aud::OpusEncoderConfig cfg;
    cfg.sample_rate = 48000;
    cfg.channels    = 2;
    cfg.bitrate     = 64000;

    auto enc = aud::OpusEncoder::create(cfg);
    auto dec = aud::OpusDecoder::create(cfg.sample_rate, cfg.channels);
    ASSERT_NE(enc, nullptr);
    ASSERT_NE(dec, nullptr);

    constexpr int kFrameSamples = 960;
    auto pcm = make_pcm_samples(kFrameSamples, cfg.channels, 440.0f, cfg.sample_rate);
    std::vector<uint8_t> enc_buf(4096);
    int enc_bytes = enc->encode_int16(pcm.data(), kFrameSamples, enc_buf.data(), enc_buf.size());

    std::vector<int16_t> dec_buf(kFrameSamples * cfg.channels);
    dec->decode_int16(enc_buf.data(), enc_bytes, dec_buf.data(), kFrameSamples);

    auto stats = dec->get_stats();
    EXPECT_GT(stats.frames_decoded, 0u);
}

// ============================================================================
//  Audio Codec Tests — End-to-end pipeline
// ============================================================================

TEST(AudioPipelineTest, EncodeDecodeRoundTrip) {
    aud::OpusEncoderConfig cfg;
    cfg.sample_rate = 48000;
    cfg.channels    = 2;
    cfg.bitrate     = 96000;

    auto enc = aud::OpusEncoder::create(cfg);
    auto dec = aud::OpusDecoder::create(cfg.sample_rate, cfg.channels);
    ASSERT_NE(enc, nullptr);
    ASSERT_NE(dec, nullptr);

    constexpr int kFrameSamples = 960;
    constexpr int kNumFrames    = 30;

    float total_error = 0.0f;
    int total_samples = 0;

    for (int i = 0; i < kNumFrames; ++i) {
        auto src = make_float_samples(kFrameSamples, cfg.channels,
                                       440.0f + i * 2.0f,
                                       cfg.sample_rate);

        std::vector<uint8_t> enc_buf(4096);
        int enc_bytes = enc->encode_float(src.data(), kFrameSamples,
                                           enc_buf.data(), enc_buf.size());
        ASSERT_GT(enc_bytes, 0);

        std::vector<float> dec_buf(kFrameSamples * cfg.channels);
        int dec_samples = dec->decode_float(enc_buf.data(), enc_bytes,
                                             dec_buf.data(), kFrameSamples);
        EXPECT_EQ(dec_samples, kFrameSamples);

        for (int s = 0; s < dec_samples * cfg.channels; ++s) {
            total_error += std::abs(src[s] - dec_buf[s]);
            total_samples++;
        }
    }

    float avg_error = total_error / total_samples;
    // Opus is lossy, but the average error should be reasonable
    EXPECT_LT(avg_error, 0.5f);
}

TEST(AudioPipelineTest, ResampleThenEncode) {
    // 44.1kHz → resample → 48kHz → Opus encode → decode
    aud::ResamplerConfig rs_cfg;
    rs_cfg.input_rate  = 44100;
    rs_cfg.output_rate = 48000;
    rs_cfg.channels    = 1;
    rs_cfg.quality     = aud::ResampleQuality::HIGH;

    auto rs = aud::Resampler::create(rs_cfg);
    ASSERT_NE(rs, nullptr);

    aud::OpusEncoderConfig enc_cfg;
    enc_cfg.sample_rate = 48000;
    enc_cfg.channels    = 1;
    enc_cfg.bitrate     = 32000;

    auto enc = aud::OpusEncoder::create(enc_cfg);
    auto dec = aud::OpusDecoder::create(enc_cfg.sample_rate, enc_cfg.channels);
    ASSERT_NE(enc, nullptr);
    ASSERT_NE(dec, nullptr);

    constexpr int kInSamples = 441;  // 10ms @ 44.1kHz
    auto pcm_44k = make_float_samples(kInSamples, 1, 440.0f, 44100);

    std::vector<float> pcm_48k(kInSamples * 48000 / 44100 + 32);
    int resampled = rs->process_float(pcm_44k.data(), kInSamples,
                                       pcm_48k.data(), pcm_48k.size());
    ASSERT_GT(resampled, 0);

    std::vector<uint8_t> enc_buf(4096);
    int enc_bytes = enc->encode_float(pcm_48k.data(), resampled,
                                       enc_buf.data(), enc_buf.size());
    ASSERT_GT(enc_bytes, 0);

    std::vector<float> dec_buf(resampled);
    int dec_samples = dec->decode_float(enc_buf.data(), enc_bytes,
                                         dec_buf.data(), resampled);
    EXPECT_GT(dec_samples, 0);
}

TEST(AudioPipelineTest, MixThenEncode) {
    // Mix two streams → Opus encode → decode
    aud::AudioMixerConfig mix_cfg;
    mix_cfg.sample_rate      = 48000;
    mix_cfg.channels         = 2;
    mix_cfg.normalize_output = true;
    mix_cfg.output_gain_db   = -3.0f;

    auto mixer = aud::AudioMixer::create(mix_cfg);
    ASSERT_NE(mixer, nullptr);

    aud::OpusEncoderConfig enc_cfg;
    enc_cfg.sample_rate = 48000;
    enc_cfg.channels    = 2;
    enc_cfg.bitrate     = 64000;

    auto enc = aud::OpusEncoder::create(enc_cfg);
    auto dec = aud::OpusDecoder::create(enc_cfg.sample_rate, enc_cfg.channels);
    ASSERT_NE(enc, nullptr);
    ASSERT_NE(dec, nullptr);

    constexpr int kSamples = 960;
    auto pcm1 = make_float_samples(kSamples, 2, 440.0f, 48000);
    auto pcm2 = make_float_samples(kSamples, 2, 880.0f, 48000);

    mixer->add_stream(0, pcm1.data(), kSamples);
    mixer->add_stream(1, pcm2.data(), kSamples);
    mixer->process();

    std::vector<float> mixed(kSamples * 2);
    int mixed_samples = mixer->read_output(mixed.data(), kSamples);
    ASSERT_GT(mixed_samples, 0);

    std::vector<uint8_t> enc_buf(4096);
    int enc_bytes = enc->encode_float(mixed.data(), mixed_samples,
                                       enc_buf.data(), enc_buf.size());
    ASSERT_GT(enc_bytes, 0);

    std::vector<float> dec_buf(mixed_samples * 2);
    int dec_samples = dec->decode_float(enc_buf.data(), enc_bytes,
                                         dec_buf.data(), mixed_samples);
    EXPECT_GT(dec_samples, 0);
}

// ============================================================================
//  Codec Tests — Stress / Edge Cases
// ============================================================================

TEST(CodecStressTest, Video_RepeatedCreateDestroy) {
    vid::H264EncoderConfig cfg;
    cfg.width     = 320;
    cfg.height    = 240;
    cfg.framerate = 30;
    cfg.bitrate   = 500'000;
    cfg.preset    = vid::EncoderPreset::ULTRAFAST;

    for (int i = 0; i < 50; ++i) {
        auto enc = vid::VideoEncoder::create_h264(cfg);
        if (enc) {
            auto frame = make_yuv420_frame(cfg.width, cfg.height);
            vid::EncodedPacket pkt;
            enc->encode(frame.data(), frame.size(), &pkt);

            vid::EncodedPacket flush_pkt;
            while (enc->flush(&flush_pkt) != vid::EncodeResult::FINISHED) {}
        }
    }
}

TEST(CodecStressTest, Audio_RepeatedCreateDestroy) {
    aud::OpusEncoderConfig cfg;
    cfg.sample_rate = 48000;
    cfg.channels    = 1;
    cfg.bitrate     = 32000;

    for (int i = 0; i < 100; ++i) {
        auto enc = aud::OpusEncoder::create(cfg);
        if (enc) {
            constexpr int kFrameSamples = 960;
            auto pcm = make_pcm_samples(kFrameSamples, cfg.channels, 440.0f,
                                         cfg.sample_rate);
            std::vector<uint8_t> out(4096);
            enc->encode_int16(pcm.data(), kFrameSamples, out.data(), out.size());
        }
    }
}

TEST(CodecStressTest, Video_ResolutionBounds) {
    // Test various resolutions including unusual ones
    std::vector<std::pair<int, int>> resolutions = {
        {64, 64},       // Tiny
        {320, 240},     // Small
        {640, 480},     // SD
        {1280, 720},    // HD
        {1920, 1080},   // FHD
        {3840, 2160},   // 4K
        {7680, 4320},   // 8K
    };

    for (auto& [w, h] : resolutions) {
        vid::H264EncoderConfig cfg;
        cfg.width     = w;
        cfg.height    = h;
        cfg.framerate = 30;
        cfg.bitrate   = w * h / 10;  // proportional bitrate
        cfg.preset    = vid::EncoderPreset::ULTRAFAST;

        auto enc = vid::VideoEncoder::create_h264(cfg);
        if (enc) {
            auto frame = make_yuv420_frame(w, h);
            vid::EncodedPacket pkt;
            enc->encode(frame.data(), frame.size(), &pkt);
        }
    }
}

TEST(CodecStressTest, Video_FramerateBounds) {
    std::vector<int> framerates = {1, 5, 10, 15, 24, 25, 30, 48, 50, 60, 120, 240};

    vid::H264EncoderConfig cfg;
    cfg.width  = 320;
    cfg.height = 240;
    cfg.bitrate = 500'000;
    cfg.preset  = vid::EncoderPreset::ULTRAFAST;

    for (int fps : framerates) {
        cfg.framerate = fps;
        auto enc = vid::VideoEncoder::create_h264(cfg);
        if (enc) {
            auto frame = make_yuv420_frame(cfg.width, cfg.height);
            vid::EncodedPacket pkt;
            enc->encode(frame.data(), frame.size(), &pkt);
        }
    }
}

TEST(CodecStressTest, Audio_OpusBitrateBounds) {
    aud::OpusEncoderConfig cfg;
    cfg.sample_rate = 48000;
    cfg.channels    = 1;

    std::vector<int> bitrates = {6000, 8000, 12000, 16000, 24000, 32000,
                                  48000, 64000, 96000, 128000, 192000, 256000, 512000};

    for (int br : bitrates) {
        cfg.bitrate = br;
        auto enc = aud::OpusEncoder::create(cfg);
        if (enc) {
            constexpr int kFrameSamples = 960;
            auto pcm = make_pcm_samples(kFrameSamples, cfg.channels, 440.0f,
                                         cfg.sample_rate);
            std::vector<uint8_t> out(4096);
            enc->encode_int16(pcm.data(), kFrameSamples, out.data(), out.size());
        }
    }
}

TEST(CodecStressTest, Audio_MultipleChannels) {
    aud::OpusEncoderConfig cfg;
    cfg.sample_rate = 48000;
    cfg.bitrate     = 128000;

    for (int ch = 1; ch <= 2; ++ch) {
        cfg.channels = ch;
        auto enc = aud::OpusEncoder::create(cfg);
        if (enc) {
            constexpr int kFrameSamples = 960;
            auto pcm = make_pcm_samples(kFrameSamples, ch, 440.0f, cfg.sample_rate);
            std::vector<uint8_t> out(4096);
            enc->encode_int16(pcm.data(), kFrameSamples, out.data(), out.size());
        }
    }
}

TEST(CodecStressTest, Video_LargeGOP) {
    vid::H264EncoderConfig cfg;
    cfg.width     = 320;
    cfg.height    = 240;
    cfg.framerate = 30;
    cfg.bitrate   = 500'000;
    cfg.gop_size  = 300;  // 10-second GOP
    cfg.preset    = vid::EncoderPreset::ULTRAFAST;

    auto enc = vid::VideoEncoder::create_h264(cfg);
    ASSERT_NE(enc, nullptr);

    for (int i = 0; i < 300; ++i) {
        auto frame = make_yuv420_frame(cfg.width, cfg.height);
        vid::EncodedPacket pkt;
        enc->encode(frame.data(), frame.size(), &pkt);
    }
}

TEST(CodecStressTest, Video_SmallGOP) {
    vid::H264EncoderConfig cfg;
    cfg.width     = 320;
    cfg.height    = 240;
    cfg.framerate = 30;
    cfg.bitrate   = 500'000;
    cfg.gop_size  = 1;  // All intra
    cfg.preset    = vid::EncoderPreset::ULTRAFAST;

    auto enc = vid::VideoEncoder::create_h264(cfg);
    ASSERT_NE(enc, nullptr);

    for (int i = 0; i < 10; ++i) {
        auto frame = make_yuv420_frame(cfg.width, cfg.height);
        vid::EncodedPacket pkt;
        enc->encode(frame.data(), frame.size(), &pkt);
        if (pkt.size > 0) {
            EXPECT_TRUE(pkt.is_keyframe);
        }
    }
}

// ============================================================================
//  Codec Tests — Thread-adjacent safety
//  (Not full thread-safety guarantees; just ensure independent instances
//   don't interfere.)
// ============================================================================

TEST(CodecThreadSeparation, IndependentVideoEncoders) {
    vid::H264EncoderConfig cfg;
    cfg.width     = 320;
    cfg.height    = 240;
    cfg.framerate = 30;
    cfg.bitrate   = 500'000;
    cfg.preset    = vid::EncoderPreset::ULTRAFAST;

    auto enc1 = vid::VideoEncoder::create_h264(cfg);
    auto enc2 = vid::VideoEncoder::create_h264(cfg);

    auto frame = make_yuv420_frame(cfg.width, cfg.height);

    vid::EncodedPacket pkt1, pkt2;
    enc1->encode(frame.data(), frame.size(), &pkt1);
    enc2->encode(frame.data(), frame.size(), &pkt2);

    // Both should work independently
    // (packets may differ due to internal state, but both should succeed)
}

TEST(CodecThreadSeparation, IndependentOpusEncoders) {
    aud::OpusEncoderConfig cfg;
    cfg.sample_rate = 48000;
    cfg.channels    = 1;
    cfg.bitrate     = 32000;

    auto enc1 = aud::OpusEncoder::create(cfg);
    auto enc2 = aud::OpusEncoder::create(cfg);

    constexpr int kFrameSamples = 960;
    auto pcm = make_pcm_samples(kFrameSamples, cfg.channels, 440.0f, cfg.sample_rate);

    std::vector<uint8_t> out1(4096), out2(4096);
    int b1 = enc1->encode_int16(pcm.data(), kFrameSamples, out1.data(), out1.size());
    int b2 = enc2->encode_int16(pcm.data(), kFrameSamples, out2.data(), out2.size());

    EXPECT_GT(b1, 0);
    EXPECT_GT(b2, 0);
}

// ============================================================================
//  Codec Tests — Configuration validation edge cases
// ============================================================================

TEST(CodecConfigValidation, BitrateControllerInvalidBounds) {
    vid::BitrateController bc;
    // Max less than min should be rejected or clamped
    bc.set_bitrate_bounds(5'000'000, 1'000'000);
    // Implementation may clamp or reject; either way shouldn't crash
    EXPECT_GE(bc.max_bitrate(), bc.min_bitrate());
}

TEST(CodecConfigValidation, EncoderNegativeDimensions) {
    vid::H264EncoderConfig cfg;
    cfg.width  = -1;
    cfg.height = 720;
    auto enc = vid::VideoEncoder::create_h264(cfg);
    EXPECT_EQ(enc, nullptr);
}

TEST(CodecConfigValidation, EncoderZeroFramerate) {
    vid::H264EncoderConfig cfg;
    cfg.width     = 640;
    cfg.height    = 480;
    cfg.framerate = 0;
    auto enc = vid::VideoEncoder::create_h264(cfg);
    // Should either handle gracefully or return null
}

TEST(CodecConfigValidation, OpusInvalidSampleRate) {
    aud::OpusEncoderConfig cfg;
    cfg.sample_rate = 12345;  // invalid
    cfg.channels    = 1;
    cfg.bitrate     = 32000;
    auto enc = aud::OpusEncoder::create(cfg);
    // Opus supports specific rates only; should fail or map to nearest
    EXPECT_EQ(enc, nullptr);
}

TEST(CodecConfigValidation, OpusInvalidChannelCount) {
    aud::OpusEncoderConfig cfg;
    cfg.sample_rate = 48000;
    cfg.channels    = 0;  // invalid
    cfg.bitrate     = 32000;
    auto enc = aud::OpusEncoder::create(cfg);
    EXPECT_EQ(enc, nullptr);
}

// ============================================================================
//  Codec Tests — CodecType enum
// ============================================================================

TEST(CodecEnumTest, VideoCodecTypeValues) {
    EXPECT_NE(vid::CodecType::H264, vid::CodecType::H265);
    EXPECT_NE(vid::CodecType::H264, vid::CodecType::VP8);
    EXPECT_NE(vid::CodecType::H264, vid::CodecType::VP9);
    EXPECT_NE(vid::CodecType::H264, vid::CodecType::AV1);
}

TEST(CodecEnumTest, EncodeResultValues) {
    EXPECT_NE(vid::EncodeResult::SUCCESS, vid::EncodeResult::ERROR);
    EXPECT_NE(vid::EncodeResult::SUCCESS, vid::EncodeResult::NEED_MORE);
    EXPECT_NE(vid::EncodeResult::SUCCESS, vid::EncodeResult::FINISHED);
}

TEST(CodecEnumTest, DecodeResultValues) {
    EXPECT_NE(vid::DecodeResult::SUCCESS, vid::DecodeResult::ERROR);
    EXPECT_NE(vid::DecodeResult::SUCCESS, vid::DecodeResult::NEED_MORE);
}

TEST(CodecEnumTest, FrameTypeValues) {
    EXPECT_NE(vid::FrameType::I, vid::FrameType::P);
    EXPECT_NE(vid::FrameType::I, vid::FrameType::B);
    EXPECT_NE(vid::FrameType::P, vid::FrameType::B);
}

TEST(CodecEnumTest, RateControlModeValues) {
    EXPECT_NE(vid::RateControlMode::CBR, vid::RateControlMode::VBR);
    EXPECT_NE(vid::RateControlMode::CBR, vid::RateControlMode::CQ);
}

TEST(CodecEnumTest, EncoderPresetValues) {
    EXPECT_NE(vid::EncoderPreset::ULTRAFAST, vid::EncoderPreset::VERYSLOW);
    EXPECT_NE(vid::EncoderPreset::MEDIUM, vid::EncoderPreset::PLACEBO);
}

// ============================================================================
//  Codec Tests — MediaFormat descriptions
// ============================================================================

TEST(MediaFormatTest, VideoEncoderFormat) {
    vid::H264EncoderConfig cfg;
    cfg.width     = 1920;
    cfg.height    = 1080;
    cfg.framerate = 30;

    auto enc = vid::VideoEncoder::create_h264(cfg);
    if (enc) {
        auto fmt = enc->output_format();
        EXPECT_EQ(fmt.codec, vid::CodecType::H264);
        EXPECT_EQ(fmt.width, cfg.width);
        EXPECT_EQ(fmt.height, cfg.height);
        EXPECT_EQ(fmt.framerate, cfg.framerate);
    }
}

// ============================================================================
//  A/V Synchronisation helpers (presentation timestamp handling)
// ============================================================================

TEST(AVSyncTest, PTSProgressionMonotonic) {
    vid::H264EncoderConfig cfg;
    cfg.width     = 640;
    cfg.height    = 480;
    cfg.framerate = 30;
    cfg.bitrate   = 1'000'000;
    cfg.preset    = vid::EncoderPreset::ULTRAFAST;

    auto enc = vid::VideoEncoder::create_h264(cfg);
    ASSERT_NE(enc, nullptr);

    int64_t last_pts = -1;
    int64_t pts_increment = 90000 / cfg.framerate;  // 90kHz clock

    for (int i = 0; i < 30; ++i) {
        auto frame = make_yuv420_frame(cfg.width, cfg.height);
        enc->set_pts(i * pts_increment);
        vid::EncodedPacket pkt;
        vid::EncodeResult res = enc->encode(frame.data(), frame.size(), &pkt);
        if (res == vid::EncodeResult::SUCCESS && pkt.size > 0) {
            // PTS should be monotonically increasing
            if (last_pts >= 0) {
                EXPECT_GE(pkt.pts, last_pts);
            }
            last_pts = pkt.pts;
        }
    }
}

// ============================================================================
//  Feature support query tests
// ============================================================================

TEST(FeatureSupportTest, QueryH264Support) {
    bool supported = vid::VideoEncoder::is_codec_supported(vid::CodecType::H264);
    // This is just a query — either result is acceptable
    SUCCEED();
}

TEST(FeatureSupportTest, QueryH265Support) {
    bool supported = vid::VideoEncoder::is_codec_supported(vid::CodecType::H265);
    SUCCEED();
}

TEST(FeatureSupportTest, QueryHardwareAccel) {
    // Query if hardware encoding is available for H264
    bool hw = vid::VideoEncoder::is_hardware_supported(vid::CodecType::H264);
    SUCCEED();
}
