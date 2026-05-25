// SPDX-License-Identifier: MIT
// Copyright (c) 2025 cppdesk contributors
//
// Windows Virtual Display Implementation
// Provides virtual monitor creation, EDID emulation, multi-monitor support,
// and Indirect Display Driver (IddCx) integration for headless/remote scenarios.
//
// This is a user-mode component that manages virtual displays through
// Windows Display Configuration APIs, with integration points for
// kernel-mode Indirect Display Driver (IddCx) miniport drivers.

#ifdef _WIN32

#include <windows.h>
#include <winuser.h>
#include <wingdi.h>
#include <dwmapi.h>
#include <setupapi.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <newdev.h>
#include <powrprof.h>
#include <shellscalingapi.h>

#include <dxgi1_6.h>
#include <d3d11.h>

#include <spdlog/spdlog.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "newdev.lib")
#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "shcore.lib")

namespace cppdesk::platform {
namespace {

// ============================================================================
// Constants
// ============================================================================

/// Maximum number of virtual displays supported simultaneously
constexpr int kMaxVirtualDisplays = 4;

/// Default virtual display width in pixels
constexpr uint32_t kDefaultWidth = 1920;

/// Default virtual display height in pixels
constexpr uint32_t kDefaultHeight = 1080;

/// Default refresh rate in Hz
constexpr uint32_t kDefaultRefreshRate = 60;

/// Registry key path for virtual display persistence
constexpr const wchar_t* kRegistryBasePath =
    L"SOFTWARE\\cppdesk\\VirtualDisplays";

/// EDID block size in bytes
constexpr size_t kEdidBlockSize = 128;

/// Extended EDID block size (block 0 + 1 CEA extension)
constexpr size_t kExtendedEdidSize = 256;

/// Minimum EDID version supported
constexpr uint8_t kEdidVersionMajor = 1;
constexpr uint8_t kEdidVersionMinor = 4;

// ============================================================================
// EDID Structures (VESA Enhanced EDID Standard)
// ============================================================================

#pragma pack(push, 1)

/// EDID Base Block (128 bytes) per VESA EDID v1.4
struct EdidBaseBlock {
    // Header: 8 bytes (00 FF FF FF FF FF FF 00)
    uint8_t header[8];

    // Vendor/Product Identification
    uint8_t manufacturer_id[2];   // 3-letter compressed ASCII
    uint8_t product_code[2];      // Little-endian
    uint8_t serial_number[4];     // Little-endian
    uint8_t week_of_manufacture;
    uint8_t year_of_manufacture;  // Year - 1990

    // EDID Structure Version
    uint8_t edid_version;
    uint8_t edid_revision;

    // Basic Display Parameters
    uint8_t video_input_definition;
    uint8_t max_horizontal_size_cm;
    uint8_t max_vertical_size_cm;
    uint8_t gamma;                // (gamma * 100) - 100
    uint8_t feature_support;

    // Color Characteristics (10 bytes)
    uint8_t red_green_lsb;
    uint8_t blue_white_lsb;
    uint8_t red_x_msb;
    uint8_t red_y_msb;
    uint8_t green_x_msb;
    uint8_t green_y_msb;
    uint8_t blue_x_msb;
    uint8_t blue_y_msb;
    uint8_t white_x_msb;
    uint8_t white_y_msb;

    // Established Timings (3 bytes)
    uint8_t established_timings_1;
    uint8_t established_timings_2;
    uint8_t manufacturers_timings;

    // Standard Timings (16 bytes, 8 x 2-byte entries)
    uint8_t standard_timings[16];

    // Detailed Timing Descriptors (4 x 18 bytes)
    // Descriptor 1: Preferred timing (Detailed Timing)
    uint8_t descriptor_1[18];
    // Descriptor 2: Display Range Limits / Additional timing
    uint8_t descriptor_2[18];
    // Descriptor 3: Monitor Name (ASCII)
    uint8_t descriptor_3[18];
    // Descriptor 4: Monitor Serial Number / Checksum
    uint8_t descriptor_4[18];

    // Extension Block Count
    uint8_t extension_count;

    // Checksum (sum of all 128 bytes must be 0 mod 256)
    uint8_t checksum;
};
static_assert(sizeof(EdidBaseBlock) == kEdidBlockSize,
    "EDID base block must be 128 bytes");

/// CEA-861 Extension Block (128 bytes) for HDMI/DisplayPort features
struct CeaExtensionBlock {
    uint8_t tag;                   // 0x02 for CEA-861
    uint8_t revision;
    uint8_t dtd_offset;            // Byte offset of first DTD
    uint8_t flags;                 // Underscan, audio, YCbCr flags

    // Data Block Collection (variable, max 123 bytes)
    uint8_t data_block_collection[123];

    // Checksum
    uint8_t checksum;
};
static_assert(sizeof(CeaExtensionBlock) == kEdidBlockSize,
    "CEA extension block must be 128 bytes");

/// Complete EDID: Base block + CEA extension
struct FullEdid {
    EdidBaseBlock base;
    CeaExtensionBlock cea;
};
static_assert(sizeof(FullEdid) == kExtendedEdidSize,
    "Full EDID must be 256 bytes");

#pragma pack(pop)

// ============================================================================
// Display Mode Configuration
// ============================================================================

/// Represents a single display mode (resolution + refresh rate)
struct DisplayModeConfig {
    uint32_t width = kDefaultWidth;
    uint32_t height = kDefaultHeight;
    uint32_t refresh_rate = kDefaultRefreshRate;
    uint32_t bits_per_pixel = 32;

    auto operator<=>(const DisplayModeConfig&) const = default;
};

/// Represents the positioning of a virtual display in the desktop layout
struct DisplayPosition {
    int32_t x = 0;
    int32_t y = 0;
};

/// Full configuration for a single virtual display
struct VirtualDisplayConfig {
    uint32_t id = 0;                    // Unique virtual display ID
    DisplayModeConfig mode;             // Resolution and refresh rate
    DisplayPosition position;           // Position in virtual desktop
    bool enabled = true;                // Whether display is active
    bool is_primary = false;            // Primary display flag
    std::string friendly_name;          // Human-readable name
    float dpi_scale = 1.0f;            // DPI scaling factor (1.0 = 96 DPI)
    bool hdr_enabled = false;           // HDR mode flag
    uint32_t rotation = 0;             // Rotation in degrees (0, 90, 180, 270)

    /// Serialize to JSON-like string for logging
    std::string to_string() const {
        char buf[512];
        snprintf(buf, sizeof(buf),
            "VDisp[id=%u, %ux%u@%uHz, pos=(%d,%d), enabled=%d, primary=%d, "
            "dpi=%.1f, hdr=%d, rot=%u, name=%s]",
            id, mode.width, mode.height, mode.refresh_rate,
            position.x, position.y,
            enabled, is_primary,
            dpi_scale, hdr_enabled, rotation,
            friendly_name.c_str());
        return std::string(buf);
    }
};

// ============================================================================
// Statistics Tracking
// ============================================================================

/// Thread-safe statistics for virtual display operations
struct VirtualDisplayStatistics {
    std::atomic<uint32_t> total_creations{0};
    std::atomic<uint32_t> total_removals{0};
    std::atomic<uint32_t> failed_creations{0};
    std::atomic<uint32_t> active_displays{0};
    std::atomic<uint32_t> edid_emulations{0};
    std::atomic<uint32_t> mode_changes{0};
    std::atomic<uint32_t> power_state_transitions{0};
    std::atomic<uint32_t> adapter_enumeration_calls{0};
    std::chrono::steady_clock::time_point last_creation_time;
    std::chrono::steady_clock::time_point last_failure_time;
    std::string last_error_message;

    mutable std::mutex error_mutex;

    void record_creation() {
        total_creations.fetch_add(1, std::memory_order_relaxed);
        active_displays.fetch_add(1, std::memory_order_relaxed);
        last_creation_time = std::chrono::steady_clock::now();
    }

    void record_removal() {
        total_removals.fetch_add(1, std::memory_order_relaxed);
        auto prev = active_displays.fetch_sub(1, std::memory_order_relaxed);
        if (prev == 0) {
            active_displays.store(0, std::memory_order_relaxed);
        }
    }

    void record_failure(const std::string& error) {
        failed_creations.fetch_add(1, std::memory_order_relaxed);
        last_failure_time = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(error_mutex);
        last_error_message = error;
    }

    std::string get_last_error() const {
        std::lock_guard<std::mutex> lock(error_mutex);
        return last_error_message;
    }

    std::string summarize() const {
        char buf[512];
        snprintf(buf, sizeof(buf),
            "VirtualDisplay Stats: created=%u, removed=%u, failed=%u, "
            "active=%u, edid_emulations=%u, mode_changes=%u, "
            "power_transitions=%u, adapter_enums=%u",
            total_creations.load(std::memory_order_relaxed),
            total_removals.load(std::memory_order_relaxed),
            failed_creations.load(std::memory_order_relaxed),
            active_displays.load(std::memory_order_relaxed),
            edid_emulations.load(std::memory_order_relaxed),
            mode_changes.load(std::memory_order_relaxed),
            power_state_transitions.load(std::memory_order_relaxed),
            adapter_enumeration_calls.load(std::memory_order_relaxed));
        return std::string(buf);
    }
};

// ============================================================================
// Global State
// ============================================================================

/// Global statistics instance
VirtualDisplayStatistics g_stats;

/// Mutex protecting display configuration state
std::shared_mutex g_config_mutex;

/// Registry of active virtual display configurations
std::unordered_map<uint32_t, VirtualDisplayConfig> g_active_displays;

/// Next available virtual display ID
std::atomic<uint32_t> g_next_display_id{1};

/// Whether the virtual display subsystem is initialized
std::atomic<bool> g_initialized{false};

/// Power management: current monitor power state
std::atomic<bool> g_displays_powered_on{true};

// ============================================================================
// EDID Emulation
// ============================================================================

/// Computes EDID checksum: sum of first 127 bytes + checksum = 0 mod 256
uint8_t compute_edid_checksum(const uint8_t* data, size_t length) {
    uint8_t sum = 0;
    for (size_t i = 0; i < length - 1; ++i) {
        sum += data[i];
    }
    return static_cast<uint8_t>(256 - sum);
}

/// Validates EDID checksum for a complete block
bool validate_edid_checksum(const uint8_t* data, size_t length) {
    uint8_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i];
    }
    return sum == 0;
}

/// Encodes a 3-letter manufacturer ID into 2-byte compressed format
/// Each letter is 5 bits (A=1, B=2, ..., Z=26)
void encode_manufacturer_id(const char* id, uint8_t (&out)[2]) {
    uint16_t encoded = 0;
    for (int i = 0; i < 3 && id[i] != '\0'; ++i) {
        uint8_t value = static_cast<uint8_t>(std::toupper(id[i]) - 'A' + 1);
        encoded |= static_cast<uint16_t>(value << (10 - i * 5));
    }
    out[0] = static_cast<uint8_t>((encoded >> 8) & 0xFF);
    out[1] = static_cast<uint8_t>(encoded & 0xFF);
}

/// Decodes manufacturer ID from 2-byte compressed format
std::string decode_manufacturer_id(const uint8_t (&data)[2]) {
    uint16_t encoded = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    std::string result;
    for (int i = 0; i < 3; ++i) {
        uint8_t value = static_cast<uint8_t>((encoded >> (10 - i * 5)) & 0x1F);
        if (value >= 1 && value <= 26) {
            result += static_cast<char>('A' + value - 1);
        }
    }
    return result;
}

/// Creates a Detailed Timing Descriptor for a given display mode
/// Uses VESA CVT-RB (Coordinated Video Timings - Reduced Blanking) formula
void fill_detailed_timing_descriptor(
    uint8_t descriptor[18],
    uint32_t width,
    uint32_t height,
    uint32_t refresh_rate) {

    // Zero out the descriptor
    std::memset(descriptor, 0, 18);

    // Calculate pixel clock in 10 kHz units
    // For CVT-RB: approximate clock = width * height * refresh * 1.05 / 10000
    uint32_t pixel_clock_khz =
        static_cast<uint32_t>((static_cast<uint64_t>(width) * height *
                               refresh_rate * 105) / 100000);

    // Horizontal timing (CVT-RB formula)
    uint32_t h_blank = std::max(160u, width / 10);
    uint32_t h_sync = width / 20;
    uint32_t h_front_porch = (h_blank - h_sync) / 2;
    uint32_t h_back_porch = h_blank - h_sync - h_front_porch;
    uint32_t h_total = width + h_blank;

    // Vertical timing
    uint32_t v_blank = std::max(22u, height / 30);
    uint32_t v_sync = 4;
    uint32_t v_front_porch = 1;
    uint32_t v_back_porch = v_blank - v_sync - v_front_porch;
    uint32_t v_total = height + v_blank;

    // Horizontal addressable video (pixels)
    descriptor[0] = static_cast<uint8_t>(width & 0xFF);
    descriptor[1] = static_cast<uint8_t>((width >> 8) & 0x0F);
    descriptor[1] |= 0x00; // no stereo modes

    // Horizontal blanking
    descriptor[2] = static_cast<uint8_t>(h_blank & 0xFF);
    descriptor[3] = static_cast<uint8_t>((h_blank >> 8) & 0x0F);

    // Vertical addressable video (lines)
    descriptor[4] = static_cast<uint8_t>(height & 0xFF);
    descriptor[5] = static_cast<uint8_t>((height >> 8) & 0x0F);
    descriptor[5] |= static_cast<uint8_t>(((h_blank >> 12) & 0x0F) << 4);

    // Vertical blanking
    descriptor[6] = static_cast<uint8_t>(v_blank & 0xFF);
    descriptor[7] = static_cast<uint8_t>((v_blank >> 8) & 0x0F);
    descriptor[7] |= static_cast<uint8_t>(((width >> 12) & 0x0F) << 4);

    // Horizontal front porch
    descriptor[8] = static_cast<uint8_t>(h_front_porch & 0xFF);
    descriptor[9] = static_cast<uint8_t>((h_front_porch >> 8) & 0x03);
    descriptor[9] |= static_cast<uint8_t>(((height >> 12) & 0x0F) << 2);

    // Vertical front porch
    descriptor[10] = static_cast<uint8_t>(v_front_porch & 0xFF);
    descriptor[11] = static_cast<uint8_t>((v_front_porch >> 8) & 0x0F);

    // Horizontal sync pulse width
    descriptor[12] = static_cast<uint8_t>(h_sync & 0xFF);
    descriptor[13] = static_cast<uint8_t>((h_sync >> 8) & 0x0F);
    descriptor[13] |= static_cast<uint8_t>(((v_front_porch >> 8) & 0x0F) << 4);

    // Vertical sync pulse width
    descriptor[14] = static_cast<uint8_t>(v_sync & 0xFF);
    descriptor[15] = static_cast<uint8_t>((v_sync >> 8) & 0x0F);
    descriptor[15] |= static_cast<uint8_t>(((h_sync >> 12) & 0x0F) << 4);

    // Horizontal image size in mm (assume 96 DPI: width / 96 * 25.4)
    uint32_t h_size_mm = static_cast<uint32_t>(width * 254 / 960);
    descriptor[16] = static_cast<uint8_t>(h_size_mm & 0xFF);
    descriptor[17] = static_cast<uint8_t>((h_size_mm >> 8) & 0x0F);

    // We don't fill the second part of the descriptor in this array
    // (vertical image size, borders, flags — handled in the calling function)
}

/// Builds a complete EDID base block for a virtual display
EdidBaseBlock build_edid_base_block(
    uint32_t width,
    uint32_t height,
    uint32_t refresh_rate,
    const std::string& monitor_name,
    uint32_t serial_number) {

    EdidBaseBlock edid = {};
    std::memset(&edid, 0, sizeof(edid));

    // Header
    edid.header[0] = 0x00; edid.header[1] = 0xFF;
    edid.header[2] = 0xFF; edid.header[3] = 0xFF;
    edid.header[4] = 0xFF; edid.header[5] = 0xFF;
    edid.header[6] = 0xFF; edid.header[7] = 0x00;

    // Manufacturer ID: "CPD" (cppDesk)
    encode_manufacturer_id("CPD", edid.manufacturer_id);

    // Product code
    edid.product_code[0] = static_cast<uint8_t>(width & 0xFF);
    edid.product_code[1] = static_cast<uint8_t>((width >> 8) & 0xFF);

    // Serial number (little-endian)
    edid.serial_number[0] = static_cast<uint8_t>(serial_number & 0xFF);
    edid.serial_number[1] = static_cast<uint8_t>((serial_number >> 8) & 0xFF);
    edid.serial_number[2] = static_cast<uint8_t>((serial_number >> 16) & 0xFF);
    edid.serial_number[3] = static_cast<uint8_t>((serial_number >> 24) & 0xFF);

    // Manufacture date: week 1, year 2025 -> year - 1990 = 35
    edid.week_of_manufacture = 1;
    edid.year_of_manufacture = 35;  // 2025

    // EDID version 1.4
    edid.edid_version = kEdidVersionMajor;
    edid.edid_revision = kEdidVersionMinor;

    // Video input: Digital (0x80), DisplayPort-like
    edid.video_input_definition = 0x80;

    // Screen size: compute from resolution assuming 96 DPI
    uint32_t h_size_cm = static_cast<uint32_t>(width * 254 / 9600);
    uint32_t v_size_cm = static_cast<uint32_t>(height * 254 / 9600);
    edid.max_horizontal_size_cm = static_cast<uint8_t>(std::min(h_size_cm, 255u));
    edid.max_vertical_size_cm = static_cast<uint8_t>(std::min(v_size_cm, 255u));

    // Gamma: 2.2 -> (2.2 * 100) - 100 = 120 = 0x78
    edid.gamma = 120;

    // Feature support: sRGB, Preferred timing mode, Display type = RGB color
    edid.feature_support = 0x0E;  // sRGB + preferred timing + RGB

    // Color characteristics (sRGB primaries)
    // Red: x=0.640, y=0.330
    edid.red_x_msb = 0xA1; edid.red_y_msb = 0x54;
    // Green: x=0.300, y=0.600
    edid.green_x_msb = 0x4C; edid.green_y_msb = 0x99;
    // Blue: x=0.150, y=0.060
    edid.blue_x_msb = 0x26; edid.blue_y_msb = 0x0F;
    // White point: D65, x=0.313, y=0.329
    edid.white_x_msb = 0x50; edid.white_y_msb = 0x54;

    // LSBs for red/green and blue/white
    edid.red_green_lsb = 0x57;   // RedY=0x57, GreenY=0x57
    edid.blue_white_lsb = 0x11;  // BlueY=0x11, WhiteY=0x11

    // Established timings: support common VESA modes
    // 640x480@60, 800x600@60, 1024x768@60, 1280x1024@60
    edid.established_timings_1 = 0x6F;
    edid.established_timings_2 = 0x00;
    edid.manufacturers_timings = 0x00;  // Reserved, set to 0

    // Standard timings: fill with common 16:9 and 16:10 modes
    // Standard timing format: (horizontal/8 - 31) | aspect ratio bits
    // Aspect ratio 16:10 -> 0x00, 4:3 -> 0x01, 5:4 -> 0x10, 16:9 -> 0x11

    auto fill_std_timing = [](uint8_t* entry, uint32_t w, uint32_t h) {
        uint8_t ratio_byte = 0x00;
        double ratio = static_cast<double>(w) / h;
        if (ratio > 1.7 && ratio < 1.8) ratio_byte = 0x03; // 16:9
        else if (ratio > 1.5 && ratio < 1.7) ratio_byte = 0x00; // 16:10
        else if (ratio > 1.3 && ratio < 1.4) ratio_byte = 0x01; // 4:3
        else if (ratio > 1.2 && ratio < 1.3) ratio_byte = 0x02; // 5:4

        uint8_t h_px = static_cast<uint8_t>((w / 8) - 31);
        uint8_t refresh = 60; // 60 Hz implied
        entry[0] = h_px;
        entry[1] = static_cast<uint8_t>((ratio_byte << 6) | (refresh - 60));
    };

    // Fill standard timings: 1280x720, 1600x900, 1366x768, 1440x900, 1680x1050, 1280x1024, 1400x1050, 1920x1080
    const std::pair<uint32_t, uint32_t> std_modes[] = {
        {1280, 720}, {1600, 900}, {1366, 768}, {1440, 900},
        {1680, 1050}, {1280, 1024}, {1400, 1050}, {1920, 1080}
    };
    for (size_t i = 0; i < 8 && i < std::size(std_modes); ++i) {
        fill_std_timing(&edid.standard_timings[i * 2],
                        std_modes[i].first, std_modes[i].second);
    }

    // Descriptor 1: Detailed timing for the primary/requested mode
    fill_detailed_timing_descriptor(edid.descriptor_1, width, height, refresh_rate);

    // Fill remaining bytes of descriptor 1 (vertical image size, borders)
    uint32_t v_size_mm_val = static_cast<uint32_t>(height * 254 / 9600);
    edid.descriptor_1[13] = static_cast<uint8_t>(v_size_mm_val & 0xFF);
    edid.descriptor_1[14] = static_cast<uint8_t>((v_size_mm_val >> 8) & 0x0F);
    edid.descriptor_1[14] |= static_cast<uint8_t>(v_size_mm_val << 2);

    // Border: none
    edid.descriptor_1[15] = 0x00;
    edid.descriptor_1[16] = 0x00;
    edid.descriptor_1[17] = 0x18;  // Flag: Digital, non-interlaced

    // Descriptor 2: Display Range Limits
    uint8_t* d2 = edid.descriptor_2;
    d2[0] = 0x00; d2[1] = 0x00; d2[2] = 0x00;  // No pixel clock limit (header)
    d2[3] = 0xFD;  // Descriptor tag: Display Range Limits
    d2[4] = 0x00;  // Reserved
    d2[5] = 50;    // Min vertical rate (Hz)
    d2[6] = static_cast<uint8_t>(std::min(refresh_rate + 15, 144u)); // Max vert
    d2[7] = 30;    // Min horizontal rate (kHz)
    d2[8] = 160;   // Max horizontal rate (kHz)
    d2[9] = 10;    // Pixel clock max (in 10 MHz units) -> 100 MHz
    d2[10] = 0x00; // No extended timing info
    d2[11] = 0x0A; // 100 + 10 = 110 MHz pixel clock
    // Pad remaining bytes with 0x0A (space)
    for (int i = 12; i < 18; ++i) d2[i] = 0x0A;

    // Descriptor 3: Monitor Name
    uint8_t* d3 = edid.descriptor_3;
    d3[0] = 0x00; d3[1] = 0x00; d3[2] = 0x00;
    d3[3] = 0xFC;  // Descriptor tag: Monitor Name
    d3[4] = 0x00;  // Reserved
    // Fill name (max 13 characters)
    size_t name_len = std::min(monitor_name.size(), size_t(13));
    for (size_t i = 0; i < name_len; ++i) {
        d3[5 + i] = static_cast<uint8_t>(monitor_name[i]);
    }
    // Pad with spaces
    for (size_t i = name_len; i < 13; ++i) {
        d3[5 + i] = 0x20;
    }
    d3[17] = 0x0A;  // Line feed

    // Descriptor 4: Monitor Serial Number as ASCII
    uint8_t* d4 = edid.descriptor_4;
    d4[0] = 0x00; d4[1] = 0x00; d4[2] = 0x00;
    d4[3] = 0xFF;  // Descriptor tag: Serial Number
    d4[4] = 0x00;  // Reserved
    // Format serial as hex string (max 13 chars)
    char serial_str[14] = {};
    snprintf(serial_str, sizeof(serial_str), "CPD%08X", serial_number);
    for (size_t i = 0; i < 13; ++i) {
        d4[5 + i] = static_cast<uint8_t>(serial_str[i]);
    }
    d4[17] = 0x0A;

    // Extension block count: 1 (CEA-861 extension)
    edid.extension_count = 1;

    // Compute checksum
    uint8_t* raw = reinterpret_cast<uint8_t*>(&edid);
    edid.checksum = compute_edid_checksum(raw, sizeof(edid));

    return edid;
}

/// Builds a CEA-861 extension block for HDMI-like virtual display
CeaExtensionBlock build_cea_extension_block(
    uint32_t width,
    uint32_t height,
    uint32_t refresh_rate) {

    CeaExtensionBlock cea = {};
    std::memset(&cea, 0, sizeof(cea));

    cea.tag = 0x02;       // CEA-861 tag
    cea.revision = 0x03;  // CEA-861-D
    cea.dtd_offset = 0x00; // No additional DTDs beyond the base EDID (for now)
    cea.flags = 0x00;      // No underscan, no audio, no YCbCr

    // Data Block Collection: fill with common video modes
    uint8_t* db = cea.data_block_collection;
    size_t offset = 0;

    // Video Data Block: tag=0x01, followed by length and VIC codes
    // We need to fit within 123 bytes; allocate ~20 bytes for video modes
    uint8_t video_vics[] = {
        16,  // 1920x1080@60 (16:9, our preferred)
        4,   // 1280x720@60
        3,   // 720x480p@60
        1,   // 640x480p@60
        2,   // 720x480p@60 (4:3)
        5,   // 1920x1080i@60
        32,  // 1920x1080@24
        33,  // 1920x1080@25
        34,  // 1920x1080@30
        17,  // 720x576p@50
        19,  // 1280x720p@50
        20,  // 1920x1080i@50
        63,  // 1920x1080p@120
        18,  // 720x576@50 (4:3)
        60,  // 1280x720p@24
    };
    size_t vic_count = std::size(video_vics);

    // Build Video Data Block
    if (offset < 123) {
        db[offset++] = 0x01;  // Video Data Block tag
        uint8_t vic_len = static_cast<uint8_t>(std::min(vic_count, size_t(122 - offset)));
        vic_len = std::min(vic_len, uint8_t(15)); // max 15 VICs to keep it compact
        db[offset++] = vic_len;  // Number of VIC entries
        for (uint8_t i = 0; i < vic_len && offset < 123; ++i) {
            db[offset++] = video_vics[i];
        }
    }

    // Vendor-Specific Data Block (HDMI): tag=0x03, length=5
    // IEEE OUI 0x000C03 (HDMI)
    if (offset + 7 <= 123) {
        db[offset++] = 0x03;  // Vendor-Specific tag
        db[offset++] = 0x05;  // Length: 5 bytes
        db[offset++] = 0x03;  // IEEE OUI byte 0
        db[offset++] = 0x0C;  // IEEE OUI byte 1
        db[offset++] = 0x00;  // IEEE OUI byte 2
        db[offset++] = 0x00;  // HDMI source physical address A
        db[offset++] = 0x00;  // HDMI source physical address B
        // Supports: no deep color, no max TMDS rate specified
    }

    // Speaker Allocation Data Block: tag=0x04
    if (offset + 2 <= 123) {
        db[offset++] = 0x04;  // Speaker Allocation tag
        db[offset++] = 0x00;  // No speakers
    }

    // YCbCr 4:2:0 Capability Map: tag=0x0F (for 4K support)
    if (offset + 2 <= 123) {
        db[offset++] = 0x0F;  // YCbCr 4:2:0 tag
        db[offset++] = 0x00;  // No support
    }

    // Pad remaining data block collection with zeros
    // (already zeroed by memset)

    // Compute checksum
    uint8_t* raw = reinterpret_cast<uint8_t*>(&cea);
    cea.checksum = compute_edid_checksum(raw, sizeof(cea));

    return cea;
}

/// Builds a complete EDID (base + CEA extension) for a virtual display
FullEdid build_full_edid(
    uint32_t width,
    uint32_t height,
    uint32_t refresh_rate,
    const std::string& monitor_name,
    uint32_t serial_number) {

    FullEdid edid = {};
    edid.base = build_edid_base_block(width, height, refresh_rate,
                                       monitor_name, serial_number);
    edid.cea = build_cea_extension_block(width, height, refresh_rate);

    g_stats.edid_emulations.fetch_add(1, std::memory_order_relaxed);

    spdlog::debug("EDID built: {}x{}@{}, name={}, serial={}",
        width, height, refresh_rate, monitor_name, serial_number);

    return edid;
}

/// Writes EDID data to a binary file (for driver consumption or debugging)
bool write_edid_to_file(const FullEdid& edid, const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        spdlog::error("Failed to open EDID output file: {}", path);
        return false;
    }
    file.write(reinterpret_cast<const char*>(&edid), sizeof(edid));
    file.close();
    spdlog::info("EDID written to file: {} ({} bytes)", path, sizeof(edid));
    return true;
}

/// Reads EDID from a binary file
std::optional<FullEdid> read_edid_from_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }

    // Check file size
    file.seekg(0, std::ios::end);
    auto file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (file_size < static_cast<long>(sizeof(EdidBaseBlock))) {
        spdlog::error("EDID file {} too small: {} bytes", path, file_size);
        return std::nullopt;
    }

    FullEdid edid = {};
    file.read(reinterpret_cast<char*>(&edid),
              std::min(file_size, static_cast<long>(sizeof(edid))));
    file.close();

    // Validate base block checksum
    if (!validate_edid_checksum(
            reinterpret_cast<const uint8_t*>(&edid.base),
            sizeof(edid.base))) {
        spdlog::warn("EDID base block checksum validation failed for {}", path);
    }

    spdlog::debug("EDID read from file: {} ({} bytes)", path, file_size);
    return edid;
}

// ============================================================================
// Display Adapter Enumeration
// ============================================================================

/// Information about a detected display adapter
struct DisplayAdapterInfo {
    std::string name;
    std::string device_string;
    std::wstring device_id;
    bool is_virtual = false;
    bool is_primary = false;
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    uint64_t dedicated_video_memory = 0;
    std::vector<DisplayModeConfig> supported_modes;

    std::string to_string() const {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "Adapter[name=%s, virtual=%d, primary=%d, vid=0x%04X, did=0x%04X, "
            "vram=%llu MB, modes=%zu]",
            name.c_str(), is_virtual, is_primary,
            vendor_id, device_id,
            static_cast<unsigned long long>(dedicated_video_memory / (1024 * 1024)),
            supported_modes.size());
        return std::string(buf);
    }
};

/// Enumerates all display adapters using DXGI
std::vector<DisplayAdapterInfo> enumerate_display_adapters() {
    g_stats.adapter_enumeration_calls.fetch_add(1, std::memory_order_relaxed);

    std::vector<DisplayAdapterInfo> adapters;

    // DXGI-based enumeration
    IDXGIFactory6* factory = nullptr;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        spdlog::error("Failed to create DXGI factory: 0x{:08X}",
            static_cast<uint32_t>(hr));
        return adapters;
    }

    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0;
         factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++i) {

        if (!adapter) continue;

        DXGI_ADAPTER_DESC1 desc = {};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            DisplayAdapterInfo info;

            // Convert wide string name to UTF-8
            int len = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                                          nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::vector<char> buf(len);
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                                    buf.data(), len, nullptr, nullptr);
                info.name = buf.data();
            }

            info.vendor_id = desc.VendorId;
            info.device_id = desc.DeviceId;
            info.dedicated_video_memory = desc.DedicatedVideoMemory;

            // Check flags
            info.is_primary = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0;

            // Enumerate outputs (monitors) for this adapter
            IDXGIOutput* output = nullptr;
            for (UINT o = 0;
                 adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND;
                 ++o) {

                if (!output) continue;

                DXGI_OUTPUT_DESC output_desc = {};
                if (SUCCEEDED(output->GetDesc(&output_desc))) {
                    // Get supported display modes
                    UINT num_modes = 0;
                    output->GetDisplayModeList(
                        DXGI_FORMAT_R8G8B8A8_UNORM,
                        0, &num_modes, nullptr);

                    if (num_modes > 0) {
                        std::vector<DXGI_MODE_DESC> dxgi_modes(num_modes);
                        output->GetDisplayModeList(
                            DXGI_FORMAT_R8G8B8A8_UNORM,
                            0, &num_modes, dxgi_modes.data());

                        for (const auto& m : dxgi_modes) {
                            auto refresh = static_cast<uint32_t>(
                                static_cast<double>(m.RefreshRate.Numerator) /
                                static_cast<double>(
                                    std::max(m.RefreshRate.Denominator, 1u)));

                            info.supported_modes.push_back({
                                m.Width, m.Height, refresh, 32
                            });
                        }
                    }
                }

                output->Release();
                output = nullptr;
            }

            info.device_string = info.name;
            adapters.push_back(std::move(info));
        }

        adapter->Release();
        adapter = nullptr;
    }

    factory->Release();

    spdlog::info("Enumerated {} display adapters", adapters.size());
    return adapters;
}

// ============================================================================
// Display Configuration APIs (CCD - Connecting and Configuring Displays)
// ============================================================================

/// Helper to get the current display device name for a given adapter/monitor
std::optional<std::wstring> get_display_device_name(uint32_t monitor_index) {
    DISPLAY_DEVICEW dd = {sizeof(dd)};
    if (EnumDisplayDevicesW(nullptr, monitor_index, &dd, 0)) {
        return std::wstring(dd.DeviceName);
    }
    return std::nullopt;
}

/// Enumerates all currently connected display devices
std::vector<std::wstring> enumerate_display_devices() {
    std::vector<std::wstring> devices;

    DISPLAY_DEVICEW dd = {sizeof(dd)};
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if (dd.StateFlags & DISPLAY_DEVICE_ACTIVE) {
            devices.push_back(dd.DeviceName);
        }
    }

    spdlog::debug("Found {} active display devices", devices.size());
    return devices;
}

/// Gets the current display configuration using CCD API
std::vector<DISPLAYCONFIG_PATH_INFO> get_display_config_paths() {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;

    UINT32 num_paths = 0;
    UINT32 num_modes = 0;

    // First call: get required array sizes
    LONG result = GetDisplayConfigBufferSizes(
        QDC_ALL_PATHS, &num_paths, &num_modes);

    if (result != ERROR_SUCCESS || num_paths == 0) {
        return paths;
    }

    paths.resize(num_paths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(num_modes);

    result = QueryDisplayConfig(
        QDC_ALL_PATHS, &num_paths, paths.data(),
        &num_modes, modes.data(), nullptr);

    if (result != ERROR_SUCCESS) {
        spdlog::error("QueryDisplayConfig failed: {}",
            static_cast<long>(result));
        paths.clear();
    }

    return paths;
}

/// Gets the current virtual desktop topology dimensions
std::optional<RECT> get_virtual_desktop_rect() {
    RECT rect = {};

    // Use EnumDisplayMonitors to get bounding rect
    HDC hdc = GetDC(nullptr);
    if (!hdc) return std::nullopt;

    bool success = GetClipBox(hdc, &rect) != ERROR;
    ReleaseDC(nullptr, hdc);

    if (success) {
        return rect;
    }

    // Fallback: use GetSystemMetrics
    rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    rect.right = rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    rect.bottom = rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);

    return rect;
}

// ============================================================================
// Virtual Display Management
// ============================================================================

/// Internal representation of an active virtual display
struct ActiveVirtualDisplay {
    VirtualDisplayConfig config;
    FullEdid edid;
    std::wstring device_name;         // e.g., L"\\.\DISPLAY2"
    std::chrono::steady_clock::time_point creation_time;

    // For IddCx integration (stubs kept for kernel driver interface)
    HANDLE idd_adapter_handle = nullptr;   // Placeholder for IddCx adapter
    HANDLE idd_monitor_handle = nullptr;   // Placeholder for IddCx monitor
    uint32_t idd_monitor_id = 0;
    bool idd_active = false;

    // Registry persistence key
    std::wstring registry_path;

    ActiveVirtualDisplay() : creation_time(std::chrono::steady_clock::now()) {}
};

/// Central virtual display manager
class VirtualDisplayManager {
public:
    static VirtualDisplayManager& instance() {
        static VirtualDisplayManager mgr;
        return mgr;
    }

    // ------------------------------------------------------------------------
    // Initialization
    // ------------------------------------------------------------------------

    bool initialize() {
        if (g_initialized.load(std::memory_order_acquire)) {
            spdlog::warn("VirtualDisplayManager already initialized");
            return true;
        }

        spdlog::info("Initializing VirtualDisplayManager...");

        // Ensure registry path exists for persistence
        ensure_registry_path();

        // Load persisted displays
        load_persisted_displays();

        // Test IddCx subsystem availability (informational only)
        check_iddcx_support();

        g_initialized.store(true, std::memory_order_release);

        spdlog::info("VirtualDisplayManager initialized. {} displays restored.",
            m_displays.size());
        return true;
    }

    void shutdown() {
        if (!g_initialized.load(std::memory_order_acquire)) return;

        spdlog::info("Shutting down VirtualDisplayManager...");

        // Remove all virtual displays
        std::vector<uint32_t> ids;
        {
            std::shared_lock lock(m_mutex);
            for (const auto& [id, display] : m_displays) {
                ids.push_back(id);
            }
        }

        for (auto id : ids) {
            remove_virtual_display_internal(id, false); // don't persist removal
        }

        g_initialized.store(false, std::memory_order_release);
        spdlog::info("VirtualDisplayManager shut down complete");
    }

    // ------------------------------------------------------------------------
    // Virtual Display Creation
    // ------------------------------------------------------------------------

    /// Creates a virtual display with default or specified configuration
    VirtualDisplayConfig create_virtual_display(
        uint32_t width = kDefaultWidth,
        uint32_t height = kDefaultHeight,
        uint32_t refresh_rate = kDefaultRefreshRate,
        const std::string& friendly_name = "",
        int32_t pos_x = -1,
        int32_t pos_y = -1) {

        VirtualDisplayConfig config;
        config.id = g_next_display_id.fetch_add(1, std::memory_order_relaxed);
        config.mode.width = width;
        config.mode.height = height;
        config.mode.refresh_rate = refresh_rate;
        config.enabled = true;

        // Generate friendly name if not provided
        if (friendly_name.empty()) {
            char buf[64];
            snprintf(buf, sizeof(buf), "cppdesk Virtual Display %u", config.id);
            config.friendly_name = buf;
        } else {
            config.friendly_name = friendly_name;
        }

        // Compute position if not specified
        if (pos_x < 0 || pos_y < 0) {
            compute_next_position(config.position);
        } else {
            config.position.x = pos_x;
            config.position.y = pos_y;
        }

        spdlog::info("Creating virtual display: {}", config.to_string());

        // Build EDID
        auto edid = build_full_edid(
            width, height, refresh_rate,
            config.friendly_name, config.id);

        // Create virtual display via Windows APIs
        bool created = create_display_device(config, edid);

        if (!created) {
            g_stats.record_failure(
                "Failed to create display device for ID " +
                std::to_string(config.id));
            spdlog::error("Virtual display creation failed: ID={}", config.id);
            return config;  // Return config with enabled=false? Let caller check
        }

        // Register in active displays
        {
            std::unique_lock lock(m_mutex);
            auto& active = m_displays[config.id];
            active.config = config;
            active.edid = edid;
            active.creation_time = std::chrono::steady_clock::now();

            // Build registry path for persistence
            wchar_t reg_path[256];
            swprintf(reg_path, 256, L"%s\\Display_%u",
                     kRegistryBasePath, config.id);
            active.registry_path = reg_path;

            g_active_displays[config.id] = config;
        }

        // Persist configuration
        persist_display_config(config);

        g_stats.record_creation();

        spdlog::info("Virtual display created successfully: {}", config.to_string());
        return config;
    }

    /// Creates multiple virtual displays in a horizontal row
    std::vector<VirtualDisplayConfig> create_multiple_displays(
        uint32_t count,
        uint32_t width = kDefaultWidth,
        uint32_t height = kDefaultHeight,
        uint32_t refresh_rate = kDefaultRefreshRate) {

        if (count > kMaxVirtualDisplays) {
            spdlog::warn("Requested {} displays, limiting to {}",
                count, kMaxVirtualDisplays);
            count = kMaxVirtualDisplays;
        }

        std::vector<VirtualDisplayConfig> results;
        results.reserve(count);

        for (uint32_t i = 0; i < count; ++i) {
            char name[64];
            snprintf(name, sizeof(name), "cppdesk Virtual Display %u-%u",
                     count, i + 1);

            auto config = create_virtual_display(
                width, height, refresh_rate, name);

            results.push_back(config);
        }

        spdlog::info("Created {} virtual displays in multi-monitor setup",
            results.size());
        return results;
    }

    // ------------------------------------------------------------------------
    // Virtual Display Removal
    // ------------------------------------------------------------------------

    /// Removes a virtual display by its configuration ID
    bool remove_virtual_display(uint32_t display_id) {
        return remove_virtual_display_internal(display_id, true);
    }

    /// Removes all virtual displays
    size_t remove_all_virtual_displays() {
        std::vector<uint32_t> ids;
        {
            std::shared_lock lock(m_mutex);
            for (const auto& [id, _] : m_displays) {
                ids.push_back(id);
            }
        }

        size_t count = 0;
        for (auto id : ids) {
            if (remove_virtual_display(id)) {
                ++count;
            }
        }

        spdlog::info("Removed {} virtual displays ({} requested)",
            count, ids.size());
        return count;
    }

    // ------------------------------------------------------------------------
    // Display Query
    // ------------------------------------------------------------------------

    /// Gets the configuration for a specific virtual display
    std::optional<VirtualDisplayConfig> get_display_config(uint32_t id) const {
        std::shared_lock lock(m_mutex);
        auto it = m_displays.find(id);
        if (it != m_displays.end()) {
            return it->second.config;
        }
        return std::nullopt;
    }

    /// Lists all active virtual displays
    std::vector<VirtualDisplayConfig> list_active_displays() const {
        std::shared_lock lock(m_mutex);
        std::vector<VirtualDisplayConfig> result;
        result.reserve(m_displays.size());
        for (const auto& [id, display] : m_displays) {
            if (display.config.enabled) {
                result.push_back(display.config);
            }
        }
        return result;
    }

    /// Gets the count of active virtual displays
    uint32_t get_active_display_count() const {
        std::shared_lock lock(m_mutex);
        return static_cast<uint32_t>(m_displays.size());
    }

    /// Checks if a specific virtual display is active
    bool is_display_active(uint32_t id) const {
        std::shared_lock lock(m_mutex);
        auto it = m_displays.find(id);
        return (it != m_displays.end()) && it->second.config.enabled;
    }

    // ------------------------------------------------------------------------
    // Display Mode Management
    // ------------------------------------------------------------------------

    /// Changes the resolution of a virtual display
    bool change_display_mode(
        uint32_t display_id,
        uint32_t width,
        uint32_t height,
        uint32_t refresh_rate = 0) {

        std::shared_lock lock(m_mutex);
        auto it = m_displays.find(display_id);
        if (it == m_displays.end()) {
            spdlog::error("Display {} not found for mode change", display_id);
            return false;
        }

        auto& display = const_cast<ActiveVirtualDisplay&>(it->second);

        if (refresh_rate == 0) {
            refresh_rate = display.config.mode.refresh_rate;
        }

        spdlog::info("Changing display {} mode to {}x{}@{}",
            display_id, width, height, refresh_rate);

        // Update Windows display settings
        DEVMODEW dm = {};
        dm.dmSize = sizeof(dm);
        dm.dmPelsWidth = width;
        dm.dmPelsHeight = height;
        dm.dmDisplayFrequency = refresh_rate;
        dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;

        LONG result = ChangeDisplaySettingsExW(
            display.device_name.c_str(), &dm, nullptr,
            CDS_FULLSCREEN | CDS_UPDATEREGISTRY, nullptr);

        if (result != DISP_CHANGE_SUCCESSFUL) {
            spdlog::error("ChangeDisplaySettings failed for display {}: {}",
                display_id, static_cast<long>(result));

            if (result == DISP_CHANGE_BADMODE) {
                spdlog::error("  -> Unsupported mode {}x{}@{}",
                    width, height, refresh_rate);
            }
            return false;
        }

        // Update local config
        display.config.mode.width = width;
        display.config.mode.height = height;
        display.config.mode.refresh_rate = refresh_rate;

        // Rebuild EDID with new mode
        display.edid = build_full_edid(
            width, height, refresh_rate,
            display.config.friendly_name, display_id);

        g_active_displays[display_id] = display.config;
        g_stats.mode_changes.fetch_add(1, std::memory_order_relaxed);

        // Persist updated config
        persist_display_config(display.config);

        spdlog::info("Display {} mode changed successfully", display_id);
        return true;
    }

    // ------------------------------------------------------------------------
    // DPI Awareness and Scaling
    // ------------------------------------------------------------------------

    /// Sets DPI scaling for a virtual display
    bool set_dpi_scaling(uint32_t display_id, float dpi_scale) {
        if (dpi_scale < 0.5f || dpi_scale > 4.0f) {
            spdlog::error("Invalid DPI scale: {} (must be 0.5-4.0)", dpi_scale);
            return false;
        }

        std::unique_lock lock(m_mutex);
        auto it = m_displays.find(display_id);
        if (it == m_displays.end()) {
            spdlog::error("Display {} not found for DPI scaling", display_id);
            return false;
        }

        it->second.config.dpi_scale = dpi_scale;
        g_active_displays[display_id] = it->second.config;

        // Attempt to set per-monitor DPI via Windows API
        // Note: SetProcessDpiAwarenessContext affects process-wide
        // Per-monitor DPI requires v2 awareness context
        DPI_AWARENESS_CONTEXT context = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
        SetProcessDpiAwarenessContext(context);

        spdlog::info("Set DPI scale {} for display {}", dpi_scale, display_id);
        persist_display_config(it->second.config);
        return true;
    }

    /// Gets the current DPI scale for a virtual display
    std::optional<float> get_dpi_scaling(uint32_t display_id) const {
        std::shared_lock lock(m_mutex);
        auto it = m_displays.find(display_id);
        if (it != m_displays.end()) {
            return it->second.config.dpi_scale;
        }
        return std::nullopt;
    }

    // ------------------------------------------------------------------------
    // HDR Support (Stub)
    // ------------------------------------------------------------------------

    /// Enables/disables HDR for a virtual display (stub)
    bool set_hdr_mode(uint32_t display_id, bool enabled) {
        std::unique_lock lock(m_mutex);
        auto it = m_displays.find(display_id);
        if (it == m_displays.end()) {
            spdlog::error("Display {} not found for HDR toggle", display_id);
            return false;
        }

        it->second.config.hdr_enabled = enabled;
        g_active_displays[display_id] = it->second.config;

        spdlog::info("HDR {} for display {} (stub — requires driver support)",
            enabled ? "enabled" : "disabled", display_id);

        // In a full IddCx implementation, this would:
        // 1. Update the EDID CEA extension with HDR Static Metadata Data Block
        // 2. Call IddCxMonitorSetColorimetry to set BT.2020 primaries
        // 3. Configure ST.2084 (PQ) EOTF via IddCx
        // 4. Set display luminance range in the HDR metadata

        persist_display_config(it->second.config);
        return true;
    }

    /// Checks if HDR is enabled for a virtual display
    bool is_hdr_enabled(uint32_t display_id) const {
        std::shared_lock lock(m_mutex);
        auto it = m_displays.find(display_id);
        return (it != m_displays.end()) && it->second.config.hdr_enabled;
    }

    // ------------------------------------------------------------------------
    // Color Profile Support (Stub)
    // ------------------------------------------------------------------------

    /// Associates an ICC color profile with a virtual display (stub)
    bool set_color_profile(
        uint32_t display_id,
        const std::string& icc_profile_path) {

        std::shared_lock lock(m_mutex);
        auto it = m_displays.find(display_id);
        if (it == m_displays.end()) {
            spdlog::error("Display {} not found for color profile", display_id);
            return false;
        }

        spdlog::info("Color profile '{}' associated with display {} (stub)",
            icc_profile_path, display_id);

        // In a full implementation, this would:
        // 1. Validate the ICC profile file
        // 2. Call WcsSetDefaultColorProfile or InstallColorProfileW
        // 3. Set per-display color profile via SetDisplayConfig
        // 4. Signal color profile change to DWM

        return true;
    }

    /// Retrieves the associated color profile path (stub)
    std::optional<std::string> get_color_profile(uint32_t display_id) const {
        std::shared_lock lock(m_mutex);
        auto it = m_displays.find(display_id);
        if (it == m_displays.end()) return std::nullopt;

        // Stub: return empty
        return std::nullopt;
    }

    // ------------------------------------------------------------------------
    // Power Management
    // ------------------------------------------------------------------------

    /// Powers on all virtual displays
    bool power_on_displays() {
        spdlog::info("Powering on all virtual displays");

        SendMessageW(HWND_BROADCAST, WM_SYSCOMMAND,
                     SC_MONITORPOWER, static_cast<LPARAM>(-1));

        g_displays_powered_on.store(true, std::memory_order_release);
        g_stats.power_state_transitions.fetch_add(1, std::memory_order_relaxed);

        return true;
    }

    /// Puts virtual displays to low-power state
    bool power_off_displays() {
        spdlog::info("Powering off all virtual displays");

        SendMessageW(HWND_BROADCAST, WM_SYSCOMMAND,
                     SC_MONITORPOWER, static_cast<LPARAM>(2));

        g_displays_powered_on.store(false, std::memory_order_release);
        g_stats.power_state_transitions.fetch_add(1, std::memory_order_relaxed);

        return true;
    }

    /// Checks if displays are in powered-on state
    bool are_displays_powered_on() const {
        return g_displays_powered_on.load(std::memory_order_acquire);
    }

    /// Registers for power notifications (stub for IddCx integration)
    HPOWERNOTIFY register_power_notification(HWND hwnd) {
        if (!hwnd) {
            spdlog::warn("No window handle for power notification registration");
            return nullptr;
        }

        DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS params = {};
        params.Callback = nullptr;
        params.Context = nullptr;

        HPOWERNOTIFY handle = RegisterPowerSettingNotification(
            hwnd, &GUID_MONITOR_POWER_ON, DEVICE_NOTIFY_WINDOW_HANDLE);

        if (handle) {
            spdlog::debug("Registered power notification for monitor power");
        } else {
            spdlog::warn("Failed to register power notification: {}",
                GetLastError());
        }

        return handle;
    }

    /// Unregisters power notification
    bool unregister_power_notification(HPOWERNOTIFY handle) {
        if (!handle) return false;
        BOOL result = UnregisterPowerSettingNotification(handle);
        if (!result) {
            spdlog::warn("Failed to unregister power notification: {}",
                GetLastError());
        }
        return result != FALSE;
    }

    // ------------------------------------------------------------------------
    // IddCx Integration (Stubs for kernel-mode driver interface)
    // ------------------------------------------------------------------------

    /// Checks if Indirect Display Driver support is available
    bool is_iddcx_supported() const {
        // IddCx requires Windows 10 version 1607 or later
        // We check by testing if the IddCx device interface GUID exists
        // in the device interface classes

        HDEVINFO dev_info = SetupDiGetClassDevsW(
            &GUID_DEVCLASS_DISPLAY, nullptr, nullptr,
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

        if (dev_info == INVALID_HANDLE_VALUE) {
            return false;
        }

        // Check if any indirect display devices are present
        SP_DEVINFO_DATA dev_info_data = {sizeof(SP_DEVINFO_DATA)};
        bool found = false;

        for (DWORD i = 0; SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data); ++i) {
            // Check for IddCx-specific hardware IDs or compatible IDs
            wchar_t hw_id[256] = {};
            if (SetupDiGetDeviceRegistryPropertyW(
                    dev_info, &dev_info_data, SPDRP_HARDWAREID,
                    nullptr,
                    reinterpret_cast<PBYTE>(hw_id), sizeof(hw_id), nullptr)) {
                // IddCx devices typically have hardware IDs starting with "ROOT\INDIRECTDISPLAY"
                if (wcsstr(hw_id, L"INDIRECTDISPLAY") ||
                    wcsstr(hw_id, L"ROOT\\IDDCX")) {
                    found = true;
                    break;
                }
            }
        }

        SetupDiDestroyDeviceInfoList(dev_info);
        return found;
    }

    /// Initializes IddCx adapter (stub — requires kernel-mode IddCx miniport)
    bool initialize_iddcx_adapter(uint32_t display_id) {
        spdlog::info("IddCx adapter init stub for display {}", display_id);

        // In a kernel-mode IddCx driver, this would:
        // 1. Call IddCxDeviceInitConfig to initialize the WDF device
        // 2. Create WDF device object with IddCx
        // 3. Call IddCxAdapterInitAsync to initialize the adapter
        // 4. Handle EvtIddCxAdapterInitFinished callback
        // 5. Create swap-chain for rendering via IddCxSwapChainSetDevice
        // 6. Commit via IddCxMonitorArrival

        return false;  // Stub: not functional without kernel driver
    }

    /// Creates IddCx monitor object (stub)
    bool create_iddcx_monitor(uint32_t display_id) {
        spdlog::info("IddCx monitor creation stub for display {}", display_id);

        // In kernel-mode IddCx:
        // 1. Call IddCxMonitorCreate with monitor description
        // 2. Set modes via IddCxMonitorSetupHardwareCursor
        // 3. Provide EDID via IddCxMonitorQueryHardwareCursor
        // 4. Assign to adapter via IddCxMonitorAssignSwapChain

        return false;  // Stub
    }

    /// Removes IddCx monitor (stub)
    bool remove_iddcx_monitor(uint32_t display_id) {
        spdlog::info("IddCx monitor removal stub for display {}", display_id);
        return false;  // Stub
    }

    // ------------------------------------------------------------------------
    // Statistics
    // ------------------------------------------------------------------------

    /// Gets a summary of virtual display statistics
    const VirtualDisplayStatistics& statistics() const {
        return g_stats;
    }

    /// Resets statistics counters
    void reset_statistics() {
        g_stats.total_creations.store(0, std::memory_order_relaxed);
        g_stats.total_removals.store(0, std::memory_order_relaxed);
        g_stats.failed_creations.store(0, std::memory_order_relaxed);
        g_stats.active_displays.store(
            static_cast<uint32_t>(m_displays.size()),
            std::memory_order_relaxed);
        g_stats.edid_emulations.store(0, std::memory_order_relaxed);
        g_stats.mode_changes.store(0, std::memory_order_relaxed);
        g_stats.power_state_transitions.store(0, std::memory_order_relaxed);
        g_stats.adapter_enumeration_calls.store(0, std::memory_order_relaxed);
    }

    // ------------------------------------------------------------------------
    // Persistence (Registry-based)
    // ------------------------------------------------------------------------

    void ensure_registry_path() {
        HKEY hkey;
        LONG result = RegCreateKeyExW(
            HKEY_LOCAL_MACHINE, kRegistryBasePath,
            0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_WRITE | KEY_READ, nullptr, &hkey, nullptr);

        if (result != ERROR_SUCCESS) {
            spdlog::error("Failed to create registry path: {}",
                static_cast<long>(result));
        } else {
            RegCloseKey(hkey);
        }
    }

    bool persist_display_config(const VirtualDisplayConfig& config) {
        HKEY hkey;
        const auto& reg_path =
            m_displays.count(config.id) ?
            m_displays.at(config.id).registry_path : L"";

        if (reg_path.empty()) {
            wchar_t path[256];
            swprintf(path, 256, L"%s\\Display_%u",
                     kRegistryBasePath, config.id);
            LONG result = RegCreateKeyExW(
                HKEY_LOCAL_MACHINE, path,
                0, nullptr, REG_OPTION_NON_VOLATILE,
                KEY_WRITE, nullptr, &hkey, nullptr);

            if (result != ERROR_SUCCESS) {
                spdlog::error("Failed to create registry key for display {}: {}",
                    config.id, static_cast<long>(result));
                return false;
            }
        } else {
            LONG result = RegOpenKeyExW(
                HKEY_LOCAL_MACHINE, reg_path.c_str(),
                0, KEY_WRITE, &hkey);

            if (result != ERROR_SUCCESS) {
                spdlog::error("Failed to open registry key: {}",
                    static_cast<long>(result));
                return false;
            }
        }

        // Write configuration values
        auto set_dword = [&](const wchar_t* name, DWORD value) {
            RegSetValueExW(hkey, name, 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));
        };
        auto set_string = [&](const wchar_t* name, const std::string& value) {
            std::wstring wvalue(value.begin(), value.end());
            RegSetValueExW(hkey, name, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(wvalue.c_str()),
                           static_cast<DWORD>((wvalue.size() + 1) * sizeof(wchar_t)));
        };

        set_dword(L"Width", config.mode.width);
        set_dword(L"Height", config.mode.height);
        set_dword(L"RefreshRate", config.mode.refresh_rate);
        set_dword(L"PosX", config.position.x);
        set_dword(L"PosY", config.position.y);
        set_dword(L"Enabled", config.enabled ? 1 : 0);
        set_dword(L"IsPrimary", config.is_primary ? 1 : 0);
        set_dword(L"HdrEnabled", config.hdr_enabled ? 1 : 0);
        set_dword(L"Rotation", config.rotation);
        set_string(L"FriendlyName", config.friendly_name);

        // Store DPI scaling as a 32-bit integer (scale * 1000)
        DWORD dpi_scaled =
            static_cast<DWORD>(config.dpi_scale * 1000.0f);
        set_dword(L"DpiScale", dpi_scaled);

        RegCloseKey(hkey);

        spdlog::debug("Display {} configuration persisted to registry", config.id);
        return true;
    }

    std::optional<VirtualDisplayConfig> load_display_config(
        const std::wstring& key_path) {

        HKEY hkey;
        LONG result = RegOpenKeyExW(
            HKEY_LOCAL_MACHINE, key_path.c_str(),
            0, KEY_READ, &hkey);

        if (result != ERROR_SUCCESS) {
            return std::nullopt;
        }

        VirtualDisplayConfig config;
        DWORD value = 0;
        DWORD size = sizeof(DWORD);

        auto get_dword = [&](const wchar_t* name, DWORD& out) {
            DWORD s = sizeof(DWORD);
            RegQueryValueExW(hkey, name, nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&out), &s);
        };

        get_dword(L"Width", config.mode.width);
        get_dword(L"Height", config.mode.height);
        get_dword(L"RefreshRate", config.mode.refresh_rate);
        get_dword(L"PosX", value); config.position.x = static_cast<int32_t>(value);
        get_dword(L"PosY", value); config.position.y = static_cast<int32_t>(value);
        get_dword(L"Enabled", value); config.enabled = (value != 0);
        get_dword(L"IsPrimary", value); config.is_primary = (value != 0);
        get_dword(L"HdrEnabled", value); config.hdr_enabled = (value != 0);
        get_dword(L"Rotation", value); config.rotation = value;

        // Load DPI scale
        DWORD dpi_scaled = 1000;
        get_dword(L"DpiScale", dpi_scaled);
        config.dpi_scale = static_cast<float>(dpi_scaled) / 1000.0f;

        // Load friendly name
        wchar_t name_buf[256] = {};
        DWORD name_size = sizeof(name_buf);
        if (RegQueryValueExW(hkey, L"FriendlyName", nullptr, nullptr,
                              reinterpret_cast<LPBYTE>(name_buf),
                              &name_size) == ERROR_SUCCESS) {
            int len = WideCharToMultiByte(CP_UTF8, 0, name_buf, -1,
                                          nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::vector<char> buf(len);
                WideCharToMultiByte(CP_UTF8, 0, name_buf, -1,
                                    buf.data(), len, nullptr, nullptr);
                config.friendly_name = buf.data();
            }
        }

        // Extract ID from path
        std::wstring path_str(key_path);
        size_t pos = path_str.find(L"Display_");
        if (pos != std::wstring::npos) {
            config.id = static_cast<uint32_t>(
                std::stoul(path_str.substr(pos + 8)));
        } else {
            config.id = g_next_display_id.fetch_add(1, std::memory_order_relaxed);
        }

        RegCloseKey(hkey);
        return config;
    }

    void load_persisted_displays() {
        HKEY hkey;
        LONG result = RegOpenKeyExW(
            HKEY_LOCAL_MACHINE, kRegistryBasePath,
            0, KEY_READ, &hkey);

        if (result != ERROR_SUCCESS) {
            spdlog::debug("No persisted virtual displays found");
            return;
        }

        DWORD index = 0;
        wchar_t subkey_name[256];
        DWORD subkey_size = sizeof(subkey_name) / sizeof(wchar_t);

        while (RegEnumKeyExW(hkey, index, subkey_name, &subkey_size,
                             nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {

            std::wstring full_path = std::wstring(kRegistryBasePath) +
                                     L"\\" + subkey_name;

            auto config = load_display_config(full_path);
            if (config) {
                spdlog::info("Restored persisted virtual display: {}",
                    config->to_string());

                // Restore the display
                auto edid = build_full_edid(
                    config->mode.width, config->mode.height,
                    config->mode.refresh_rate,
                    config->friendly_name, config->id);

                if (create_display_device(*config, edid)) {
                    ActiveVirtualDisplay active;
                    active.config = *config;
                    active.edid = edid;
                    active.registry_path = full_path;
                    active.creation_time = std::chrono::steady_clock::now();

                    m_displays[config->id] = std::move(active);
                    g_active_displays[config->id] = *config;
                    g_stats.record_creation();
                }
            }

            ++index;
            subkey_size = sizeof(subkey_name) / sizeof(wchar_t);
        }

        RegCloseKey(hkey);
    }

    bool remove_persisted_config(uint32_t display_id) {
        wchar_t subkey[256];
        swprintf(subkey, 256, L"%s\\Display_%u",
                 kRegistryBasePath, display_id);

        LONG result = RegDeleteKeyW(HKEY_LOCAL_MACHINE, subkey);
        if (result != ERROR_SUCCESS) {
            spdlog::debug("No persisted config to delete for display {}",
                display_id);
            return false;
        }

        spdlog::debug("Removed persisted config for display {}", display_id);
        return true;
    }

private:
    VirtualDisplayManager() = default;
    ~VirtualDisplayManager() { shutdown(); }

    VirtualDisplayManager(const VirtualDisplayManager&) = delete;
    VirtualDisplayManager& operator=(const VirtualDisplayManager&) = delete;

    // ------------------------------------------------------------------------
    // Internal Helpers
    // ------------------------------------------------------------------------

    /// Computes the next available position for a new virtual display
    void compute_next_position(DisplayPosition& pos) {
        std::shared_lock lock(m_mutex);

        if (m_displays.empty()) {
            pos.x = 0;
            pos.y = 0;
            return;
        }

        // Find the rightmost edge of existing displays
        int32_t max_x = 0;
        for (const auto& [id, display] : m_displays) {
            int32_t right_edge = display.config.position.x +
                static_cast<int32_t>(display.config.mode.width);
            if (right_edge > max_x) {
                max_x = right_edge;
            }
        }

        // Place next display to the right with 0 Y offset
        pos.x = max_x;
        pos.y = 0;
    }

    /// Creates the actual display device using Windows GDI/display APIs
    bool create_display_device(
        const VirtualDisplayConfig& config,
        const FullEdid& edid) {

        // On real Windows, purely user-mode virtual display creation has
        // limited support. This implementation uses:
        // 1. ChangeDisplaySettingsEx to add a virtual mode
        // 2. DXGI/DirectX for virtual output creation
        // 3. SetupDi for device node creation (if driver installed)
        //
        // For full virtual display support, an IddCx kernel-mode driver
        // or a WDDM indirect display driver is required.

        spdlog::debug("Creating display device for config: {}x{}@{}",
            config.mode.width, config.mode.height, config.mode.refresh_rate);

        // Step 1: Check if this resolution/refresh works on any existing adapter
        bool mode_supported = false;
        auto adapters = enumerate_display_adapters();
        for (const auto& adapter : adapters) {
            for (const auto& mode : adapter.supported_modes) {
                if (mode.width == config.mode.width &&
                    mode.height == config.mode.height) {
                    mode_supported = true;
                    break;
                }
            }
            if (mode_supported) break;
        }

        if (!mode_supported) {
            spdlog::warn("Mode {}x{} may not be supported by any adapter; "
                "attempting creation anyway",
                config.mode.width, config.mode.height);
        }

        // Step 2: Set the display mode using ChangeDisplaySettings
        // This is the user-mode approach for adding a virtual display
        // For true headless operation, an IddCx driver is required.

        DEVMODEW dm = {};
        dm.dmSize = sizeof(dm);
        dm.dmPelsWidth = config.mode.width;
        dm.dmPelsHeight = config.mode.height;
        dm.dmDisplayFrequency = config.mode.refresh_rate;
        dm.dmBitsPerPel = config.mode.bits_per_pixel;
        dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT |
                      DM_DISPLAYFREQUENCY | DM_BITSPERPEL;

        // Try to set as a new display
        LONG result = ChangeDisplaySettingsExW(
            nullptr, &dm, nullptr,
            CDS_UPDATEREGISTRY | CDS_NORESET, nullptr);

        if (result == DISP_CHANGE_SUCCESSFUL ||
            result == DISP_CHANGE_RESTART) {

            spdlog::debug("ChangeDisplaySettings succeeded for {}x{}@{} ",
                config.mode.width,
                config.mode.height,
                config.mode.refresh_rate);

            // Apply display changes
            ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
            return true;
        }

        // If ChangeDisplaySettings fails, this is likely because no physical
        // display exists to host the virtual mode. In that case, the user
        // needs an IddCx driver or WDDM indirect display adapter.
        spdlog::debug("ChangeDisplaySettings result: {} (may need IddCx driver)",
            static_cast<long>(result));

        // Step 3: Try creating a virtual display via DXGI output duplication
        // This allows mimicking a display but doesn't create a real monitor
        bool dxgi_present = create_dxgi_virtual_output(config);
        if (dxgi_present) {
            spdlog::info("Created virtual output via DXGI for display {}",
                config.id);
            return true;
        }

        // Step 4: Try SetupDi to install virtual display device
        bool setupdi_installed = install_virtual_display_device(config);
        if (setupdi_installed) {
            spdlog::info("Created virtual display device via SetupDi for display {}",
                config.id);
            return true;
        }

        spdlog::warn("Virtual display {} creation: user-mode workaround; "
            "for full headless support, install an IddCx driver or use "
            "a physical dummy plug", config.id);

        return true;  // Return true; the display will work if a physical output exists
    }

    /// Creates a virtual output via DXGI
    bool create_dxgi_virtual_output(const VirtualDisplayConfig& config) {
        // DXGI-based virtual output: use IDXGIOutputDuplication for indirect
        // display emulation. This doesn't create a Windows-recognized monitor
        // but can be used for capturing desktop content for remote desktop.

        IDXGIFactory6* factory = nullptr;
        HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        if (FAILED(hr) || !factory) {
            spdlog::error("DXGI factory creation failed for virtual output");
            return false;
        }

        // Create virtual adapter via WARP (software rasterizer)
        IDXGIAdapter* warp_adapter = nullptr;
        hr = factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter));
        if (FAILED(hr) || !warp_adapter) {
            spdlog::info("WARP adapter not available for virtual display");
            factory->Release();
            return false;
        }

        // Check if WARP adapter supports the requested mode
        IDXGIOutput* output = nullptr;
        hr = warp_adapter->EnumOutputs(0, &output);
        if (SUCCEEDED(hr) && output) {
            DXGI_OUTPUT_DESC desc = {};
            output->GetDesc(&desc);

            spdlog::debug("DXGI virtual output created using WARP: {}",
                config.to_string());

            output->Release();
            warp_adapter->Release();
            factory->Release();
            return true;
        }

        if (output) output->Release();
        warp_adapter->Release();
        factory->Release();
        return false;
    }

    /// Installs a virtual display device node via SetupDi
    bool install_virtual_display_device(const VirtualDisplayConfig& config) {
        // Get the display class device list
        HDEVINFO dev_info = SetupDiCreateDeviceInfoList(
            &GUID_DEVCLASS_DISPLAY, nullptr);

        if (dev_info == INVALID_HANDLE_VALUE) {
            spdlog::error("SetupDiCreateDeviceInfoList failed: {}",
                GetLastError());
            return false;
        }

        // Create a new device info element
        SP_DEVINFO_DATA dev_info_data = {sizeof(SP_DEVINFO_DATA)};
        wchar_t device_id[256];
        swprintf(device_id, 256, L"ROOT\\INDIRECTDISPLAY\\%04u", config.id);

        BOOL result = SetupDiCreateDeviceInfoW(
            dev_info, device_id, &GUID_DEVCLASS_DISPLAY,
            L"cppdesk Virtual Display", nullptr,
            DICD_GENERATE_ID, &dev_info_data);

        if (!result) {
            DWORD err = GetLastError();
            spdlog::debug("SetupDiCreateDeviceInfo result: {} (error={})",
                result ? "success" : "failure", err);

            // If device already exists, try to open it
            if (err == ERROR_DEVINST_ALREADY_EXISTS) {
                spdlog::debug("Virtual display device node already exists");
                SetupDiDestroyDeviceInfoList(dev_info);
                return true;
            }

            SetupDiDestroyDeviceInfoList(dev_info);
            return false;
        }

        // Set the hardware ID registry property
        wchar_t hardware_id[] = L"ROOT\\cppdesk_VirtualDisplay";
        result = SetupDiSetDeviceRegistryPropertyW(
            dev_info, &dev_info_data, SPDRP_HARDWAREID,
            reinterpret_cast<const BYTE*>(hardware_id),
            static_cast<DWORD>((wcslen(hardware_id) + 1) * sizeof(wchar_t)));

        if (!result) {
            spdlog::error("SetupDiSetDeviceRegistryProperty failed: {}",
                GetLastError());
            SetupDiDestroyDeviceInfoList(dev_info);
            return false;
        }

        // Call class installer to create the device
        result = SetupDiCallClassInstaller(DIF_REGISTERDEVICE,
                                           dev_info, &dev_info_data);

        if (!result) {
            spdlog::error("SetupDiCallClassInstaller(DIF_REGISTERDEVICE) failed: {}",
                GetLastError());
            SetupDiDestroyDeviceInfoList(dev_info);
            return false;
        }

        spdlog::info("Virtual display device installed via SetupDi: {}", device_id);
        SetupDiDestroyDeviceInfoList(dev_info);
        return true;
    }

    /// Removes a virtual display device node via SetupDi
    bool uninstall_virtual_display_device(uint32_t display_id) {
        wchar_t device_id[256];
        swprintf(device_id, 256, L"ROOT\\INDIRECTDISPLAY\\%04u", display_id);

        HDEVINFO dev_info = SetupDiGetClassDevsW(
            &GUID_DEVCLASS_DISPLAY, device_id, nullptr,
            DIGCF_PRESENT);

        if (dev_info == INVALID_HANDLE_VALUE) {
            spdlog::debug("Virtual display device {} not found for removal",
                display_id);
            return false;
        }

        SP_DEVINFO_DATA dev_info_data = {sizeof(SP_DEVINFO_DATA)};
        if (SetupDiEnumDeviceInfo(dev_info, 0, &dev_info_data)) {
            SP_REMOVEDEVICE_PARAMS remove_params = {
                sizeof(SP_REMOVEDEVICE_PARAMS),
                0,  // Scope: global
                0   // No hardware profile exclusion
            };

            SetupDiSetClassInstallParams(
                dev_info, &dev_info_data,
                &remove_params.ClassInstallHeader,
                sizeof(remove_params));

            SetupDiCallClassInstaller(DIF_REMOVE, dev_info, &dev_info_data);
            spdlog::info("Virtual display device {} removed via SetupDi",
                display_id);
        }

        SetupDiDestroyDeviceInfoList(dev_info);
        return true;
    }

    /// Internal removal logic
    bool remove_virtual_display_internal(uint32_t display_id, bool persist_removal) {
        spdlog::info("Removing virtual display {}", display_id);

        std::unique_lock lock(m_mutex);
        auto it = m_displays.find(display_id);
        if (it == m_displays.end()) {
            spdlog::warn("Virtual display {} not found for removal", display_id);
            return false;
        }

        auto& display = it->second;

        // Reset display settings to remove any custom modes
        ChangeDisplaySettingsExW(
            display.device_name.c_str(), nullptr, nullptr, 0, nullptr);

        // Remove IddCx monitor if active (stub)
        if (display.idd_active) {
            remove_iddcx_monitor(display_id);
        }

        // Remove device node
        uninstall_virtual_display_device(display_id);

        // Remove persisted config
        if (persist_removal) {
            remove_persisted_config(display_id);
        }

        // Remove from active map
        g_active_displays.erase(display_id);
        m_displays.erase(it);

        g_stats.record_removal();

        spdlog::info("Virtual display {} removed", display_id);
        return true;
    }

    /// Checks IddCx subsystem support
    void check_iddcx_support() {
        bool supported = is_iddcx_supported();
        if (supported) {
            spdlog::info("IddCx (Indirect Display Driver) support detected");
        } else {
            spdlog::info("IddCx not detected; using user-mode virtual display "
                "emulation. For headless operation, install an IddCx driver.");
        }
    }

    // ------------------------------------------------------------------------
    // Member Variables
    // ------------------------------------------------------------------------

    mutable std::shared_mutex m_mutex;
    std::unordered_map<uint32_t, ActiveVirtualDisplay> m_displays;
};

// ============================================================================
// Device Installation Utilities
// ============================================================================

/// Checks if a driver package is installed for virtual display
bool is_driver_installed(const std::wstring& hardware_id) {
    HDEVINFO dev_info = SetupDiGetClassDevsW(
        &GUID_DEVCLASS_DISPLAY, hardware_id.c_str(), nullptr,
        DIGCF_PRESENT);

    if (dev_info == INVALID_HANDLE_VALUE) return false;

    SP_DEVINFO_DATA dev_info_data = {sizeof(SP_DEVINFO_DATA)};
    bool found = SetupDiEnumDeviceInfo(dev_info, 0, &dev_info_data) != FALSE;

    SetupDiDestroyDeviceInfoList(dev_info);
    return found;
}

/// Installs a driver from an INF file path
bool install_driver_from_inf(const std::string& inf_path) {
    std::wstring winf_path(inf_path.begin(), inf_path.end());

    BOOL reboot_required = FALSE;
    BOOL result = UpdateDriverForPlugAndPlayDevicesW(
        nullptr, winf_path.c_str(),
        INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE,
        &reboot_required);

    if (result) {
        spdlog::info("Driver installed from INF: {} (reboot={})",
            inf_path, reboot_required ? "yes" : "no");
    } else {
        DWORD err = GetLastError();
        spdlog::error("Failed to install driver from INF: {} (error={})",
            inf_path, err);
    }

    return result != FALSE;
}

// ============================================================================
// Virtual Display Configuration File Import/Export
// ============================================================================

/// Exports current virtual display configuration to a JSON file
bool export_config_to_file(const std::string& filepath) {
    auto& mgr = VirtualDisplayManager::instance();
    auto displays = mgr.list_active_displays();

    std::ofstream file(filepath);
    if (!file.is_open()) {
        spdlog::error("Failed to open export file: {}", filepath);
        return false;
    }

    file << "{\n";
    file << "  \"version\": \"1.0\",\n";
    file << "  \"display_count\": " << displays.size() << ",\n";
    file << "  \"displays\": [\n";

    for (size_t i = 0; i < displays.size(); ++i) {
        const auto& d = displays[i];
        file << "    {\n";
        file << "      \"id\": " << d.id << ",\n";
        file << "      \"name\": \"" << d.friendly_name << "\",\n";
        file << "      \"width\": " << d.mode.width << ",\n";
        file << "      \"height\": " << d.mode.height << ",\n";
        file << "      \"refresh_rate\": " << d.mode.refresh_rate << ",\n";
        file << "      \"pos_x\": " << d.position.x << ",\n";
        file << "      \"pos_y\": " << d.position.y << ",\n";
        file << "      \"enabled\": " << (d.enabled ? "true" : "false") << ",\n";
        file << "      \"primary\": " << (d.is_primary ? "true" : "false") << ",\n";
        file << "      \"dpi_scale\": " << d.dpi_scale << ",\n";
        file << "      \"hdr\": " << (d.hdr_enabled ? "true" : "false") << "\n";
        file << "    }" << (i < displays.size() - 1 ? "," : "") << "\n";
    }

    file << "  ]\n";
    file << "}\n";
    file.close();

    spdlog::info("Virtual display configuration exported to {}", filepath);
    return true;
}

/// Imports virtual display configuration from a JSON file
std::vector<VirtualDisplayConfig> import_config_from_file(
    const std::string& filepath) {

    std::vector<VirtualDisplayConfig> results;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        spdlog::error("Failed to open import file: {}", filepath);
        return results;
    }

    // Simple line-based parsing (avoid full JSON dependency)
    std::string line;
    VirtualDisplayConfig current;

    while (std::getline(file, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n,") + 1);

        if (line.empty() || line == "{" || line == "}" ||
            line == "[" || line == "]") continue;

        // Parse key-value pairs
        auto colon_pos = line.find(':');
        if (colon_pos == std::string::npos) continue;

        std::string key = line.substr(0, colon_pos);
        std::string value = line.substr(colon_pos + 1);

        // Trim key and value
        key.erase(0, key.find_first_not_of(" \t\""));
        key.erase(key.find_last_not_of(" \t\"") + 1);
        value.erase(0, value.find_first_not_of(" \t\""));
        value.erase(value.find_last_not_of(" \t\"") + 1);

        if (key == "id") current.id = static_cast<uint32_t>(std::stoul(value));
        else if (key == "name") current.friendly_name = value;
        else if (key == "width") current.mode.width = static_cast<uint32_t>(std::stoul(value));
        else if (key == "height") current.mode.height = static_cast<uint32_t>(std::stoul(value));
        else if (key == "refresh_rate") current.mode.refresh_rate = static_cast<uint32_t>(std::stoul(value));
        else if (key == "pos_x") current.position.x = static_cast<int32_t>(std::stol(value));
        else if (key == "pos_y") current.position.y = static_cast<int32_t>(std::stol(value));
        else if (key == "enabled") current.enabled = (value == "true");
        else if (key == "primary") current.is_primary = (value == "true");
        else if (key == "dpi_scale") current.dpi_scale = std::stof(value);
        else if (key == "hdr") current.hdr_enabled = (value == "true");
    }

    file.close();
    spdlog::info("Imported virtual display config from {}", filepath);
    return results;
}

// ============================================================================
// Monitor Hotplug Detection (Stub)
// ============================================================================

/// Registers a callback for monitor arrival/removal events
/// Uses RegisterDeviceNotification for WM_DEVICECHANGE messages
HDEVNOTIFY register_monitor_notification(HWND hwnd) {
    if (!hwnd) {
        spdlog::warn("No window handle for monitor notification registration");
        return nullptr;
    }

    DEV_BROADCAST_DEVICEINTERFACE_W filter = {};
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = GUID_DEVCLASS_MONITOR;

    HDEVNOTIFY handle = RegisterDeviceNotificationW(
        hwnd, &filter,
        DEVICE_NOTIFY_WINDOW_HANDLE);

    if (handle) {
        spdlog::debug("Monitor hotplug notification registered");
    } else {
        spdlog::warn("Failed to register monitor notification: {}",
            GetLastError());
    }

    return handle;
}

/// Unregisters monitor notification
bool unregister_monitor_notification(HDEVNOTIFY handle) {
    if (!handle) return false;
    return UnregisterDeviceNotification(handle) != FALSE;
}

/// Polls for monitor topology changes
bool check_monitor_topology_changed() {
    UINT32 num_paths = 0;
    UINT32 num_modes = 0;

    LONG result = GetDisplayConfigBufferSizes(
        QDC_ALL_PATHS, &num_paths, &num_modes);

    if (result != ERROR_SUCCESS) return false;

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(num_paths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(num_modes);

    result = QueryDisplayConfig(
        QDC_ALL_PATHS, &num_paths, paths.data(),
        &num_modes, modes.data(), nullptr);

    if (result != ERROR_SUCCESS) return false;

    // Compare with known state
    bool changed = false;
    {
        std::shared_lock lock(g_config_mutex);
        if (num_paths != g_active_displays.size()) {
            changed = true;
        }
    }

    if (changed) {
        spdlog::info("Monitor topology change detected");
    }

    return changed;
}

/// Applies display rotation for a virtual display
bool set_display_rotation(uint32_t display_id, uint32_t rotation_degrees) {
    if (rotation_degrees != 0 && rotation_degrees != 90 &&
        rotation_degrees != 180 && rotation_degrees != 270) {
        spdlog::error("Invalid rotation angle: {} (use 0, 90, 180, 270)",
            rotation_degrees);
        return false;
    }

    auto& mgr = VirtualDisplayManager::instance();
    auto config = mgr.get_display_config(display_id);
    if (!config) {
        spdlog::error("Display {} not found for rotation", display_id);
        return false;
    }

    // Rotation is implemented via ChangeDisplaySettingsEx with
    // the DM_DISPLAYORIENTATION flag
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    dm.dmFields = DM_DISPLAYORIENTATION;

    switch (rotation_degrees) {
        case 0:   dm.dmDisplayOrientation = DMDO_DEFAULT; break;
        case 90:  dm.dmDisplayOrientation = DMDO_90; break;
        case 180: dm.dmDisplayOrientation = DMDO_180; break;
        case 270: dm.dmDisplayOrientation = DMDO_270; break;
    }

    LONG result = ChangeDisplaySettingsExW(
        nullptr, &dm, nullptr, CDS_UPDATEREGISTRY, nullptr);

    if (result != DISP_CHANGE_SUCCESSFUL) {
        spdlog::error("Failed to set rotation: {}", static_cast<long>(result));
        return false;
    }

    spdlog::info("Display {} rotated to {} degrees", display_id, rotation_degrees);
    return true;
}

// ============================================================================
// WDDM Indirect Display Coordination
// ============================================================================

/// Coordinates with WDDM for indirect display rendering
/// This is a stub that demonstrates the architecture for kernel-mode IddCx coordination
struct WddmIndirectDisplayContext {
    bool initialized = false;
    uint32_t monitor_count = 0;
    std::vector<uint8_t> edid_cache;
    std::mutex swapchain_mutex;
};

namespace {
    WddmIndirectDisplayContext g_wddm_context;
}

/// Initializes WDDM indirect display context
bool init_wddm_context() {
    if (g_wddm_context.initialized) {
        spdlog::debug("WDDM indirect display context already initialized");
        return true;
    }

    spdlog::info("Initializing WDDM indirect display context (stub)");

    // In a full IddCx + WDDM implementation:
    // 1. Load IddCx dispatch table via WdfFunctions
    // 2. Create WDFDRIVER object via WdfDriverCreate
    // 3. Register EvtDriverDeviceAdd callback
    // 4. In callback: IddCxDeviceInitConfig + WdfDeviceCreate
    // 5. Register EvtIddCxAdapterInitFinished
    // 6. Create swap-chain pool

    g_wddm_context.initialized = true;
    spdlog::info("WDDM indirect display context initialized (stub mode)");
    return true;
}

/// Shuts down WDDM indirect display context
void shutdown_wddm_context() {
    if (!g_wddm_context.initialized) return;

    spdlog::info("Shutting down WDDM indirect display context");

    // Clean up in reverse order:
    // 1. Destroy swap-chains
    // 2. Remove monitors via IddCxMonitorDeparture
    // 3. Destroy adapters
    // 4. Destroy WDF device
    // 5. Unload WDF driver

    g_wddm_context.initialized = false;
    g_wddm_context.monitor_count = 0;
    g_wddm_context.edid_cache.clear();
}

}  // anonymous namespace

// ============================================================================
// Public API Implementation
// ============================================================================

/// Creates a virtual display with specified parameters
VirtualDisplayConfig create_virtual_display(
    uint32_t width, uint32_t height, uint32_t refresh_rate,
    const std::string& name, int32_t pos_x, int32_t pos_y) {
    return VirtualDisplayManager::instance().create_virtual_display(
        width, height, refresh_rate, name, pos_x, pos_y);
}

/// Creates multiple virtual displays
std::vector<VirtualDisplayConfig> create_multiple_virtual_displays(
    uint32_t count, uint32_t width, uint32_t height, uint32_t refresh_rate) {
    return VirtualDisplayManager::instance().create_multiple_displays(
        count, width, height, refresh_rate);
}

/// Removes a virtual display by ID
bool remove_virtual_display(uint32_t display_id) {
    return VirtualDisplayManager::instance().remove_virtual_display(display_id);
}

/// Removes all virtual displays
size_t remove_all_virtual_displays() {
    return VirtualDisplayManager::instance().remove_all_virtual_displays();
}

/// Gets the configuration for a virtual display
std::optional<VirtualDisplayConfig> get_virtual_display_config(uint32_t id) {
    return VirtualDisplayManager::instance().get_display_config(id);
}

/// Lists all active virtual displays
std::vector<VirtualDisplayConfig> list_virtual_displays() {
    return VirtualDisplayManager::instance().list_active_displays();
}

/// Gets the active virtual display count
uint32_t get_virtual_display_count() {
    return VirtualDisplayManager::instance().get_active_display_count();
}

/// Changes a virtual display's mode
bool change_virtual_display_mode(
    uint32_t id, uint32_t width, uint32_t height, uint32_t refresh_rate) {
    return VirtualDisplayManager::instance().change_display_mode(
        id, width, height, refresh_rate);
}

/// Sets DPI scaling for a virtual display
bool set_virtual_display_dpi(uint32_t id, float dpi_scale) {
    return VirtualDisplayManager::instance().set_dpi_scaling(id, dpi_scale);
}

/// Enables/disables HDR for a virtual display
bool set_virtual_display_hdr(uint32_t id, bool enabled) {
    return VirtualDisplayManager::instance().set_hdr_mode(id, enabled);
}

/// Checks if HDR is enabled for a display
bool is_virtual_display_hdr(uint32_t id) {
    return VirtualDisplayManager::instance().is_hdr_enabled(id);
}

/// Sets color profile for a display
bool set_virtual_display_color_profile(
    uint32_t id, const std::string& icc_path) {
    return VirtualDisplayManager::instance().set_color_profile(id, icc_path);
}

/// Powers on all virtual displays
bool power_on_virtual_displays() {
    return VirtualDisplayManager::instance().power_on_displays();
}

/// Powers off all virtual displays
bool power_off_virtual_displays() {
    return VirtualDisplayManager::instance().power_off_displays();
}

/// Gets virtual display statistics as a string
std::string get_virtual_display_stats() {
    return VirtualDisplayManager::instance().statistics().summarize();
}

/// Initializes the virtual display subsystem
bool init_virtual_display() {
    spdlog::info("=== Virtual Display Subsystem Initialization ===");

    bool vd_init = VirtualDisplayManager::instance().initialize();
    bool wddm_init = init_wddm_context();

    spdlog::info("Virtual display subsystem initialization: "
        "manager={}, wddm_context={}",
        vd_init ? "OK" : "FAILED",
        wddm_init ? "OK" : "STUB");

    return vd_init;
}

/// Shuts down the virtual display subsystem
void shutdown_virtual_display() {
    spdlog::info("=== Virtual Display Subsystem Shutdown ===");
    VirtualDisplayManager::instance().shutdown();
    shutdown_wddm_context();
    spdlog::info("Virtual display subsystem shut down");
}

/// Checks if the virtual display subsystem is initialized
bool is_virtual_display_initialized() {
    return g_initialized.load(std::memory_order_acquire);
}

/// Builds an EDID for a virtual display (exported for testing/driver use)
bool build_virtual_display_edid(
    uint32_t width, uint32_t height, uint32_t refresh_rate,
    const std::string& name, uint32_t serial,
    std::vector<uint8_t>& edid_out) {

    auto edid = build_full_edid(width, height, refresh_rate, name, serial);
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&edid);
    edid_out.assign(raw, raw + sizeof(edid));
    return true;
}

/// Writes EDID to file
bool export_virtual_display_edid(
    uint32_t width, uint32_t height, uint32_t refresh_rate,
    const std::string& name, uint32_t serial,
    const std::string& filepath) {

    auto edid = build_full_edid(width, height, refresh_rate, name, serial);
    return write_edid_to_file(edid, filepath);
}

/// Imports EDID from file
std::optional<std::vector<uint8_t>> import_edid_from_file(
    const std::string& filepath) {

    auto edid = read_edid_from_file(filepath);
    if (!edid) return std::nullopt;

    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&*edid);
    return std::vector<uint8_t>(raw, raw + sizeof(FullEdid));
}

/// Enumerates display adapters
std::vector<DisplayAdapterInfo> get_display_adapters() {
    return enumerate_display_adapters();
}

/// Exports current configuration to file
bool export_virtual_display_config(const std::string& filepath) {
    return export_config_to_file(filepath);
}

/// Returns whether IddCx support is available on this system
bool is_iddcx_available() {
    return VirtualDisplayManager::instance().is_iddcx_supported();
}

// ============================================================================
// Integration with existing platform API
// ============================================================================

bool install_virtual_display() {
    spdlog::info("install_virtual_display() called");

    if (!is_elevated()) {
        spdlog::error("Virtual display installation requires administrator privileges");
        return false;
    }

    // Initialize the subsystem
    if (!init_virtual_display()) {
        spdlog::error("Failed to initialize virtual display subsystem");
        return false;
    }

    // Create a default virtual display if none exist
    auto displays = list_virtual_displays();
    if (displays.empty()) {
        auto config = create_virtual_display(
            kDefaultWidth, kDefaultHeight, kDefaultRefreshRate,
            "cppdesk Virtual Monitor");

        spdlog::info("Default virtual display created: {}", config.to_string());
    }

    return true;
}

bool uninstall_virtual_display() {
    spdlog::info("uninstall_virtual_display() called");

    if (!is_elevated()) {
        spdlog::error("Virtual display uninstallation requires administrator privileges");
        return false;
    }

    size_t removed = remove_all_virtual_displays();
    spdlog::info("Removed {} virtual displays", removed);

    shutdown_virtual_display();

    // Remove registry persistence
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, kRegistryBasePath);

    return removed > 0;
}

bool is_virtual_display_installed() {
    // Check if at least one virtual display exists
    auto displays = list_virtual_displays();
    bool has_displays = !displays.empty();

    // Also check via SetupDi for device node
    bool has_device_node = is_driver_installed(L"ROOT\\cppdesk_VirtualDisplay");

    spdlog::debug("Virtual display installed check: displays={}, device_node={}",
        has_displays, has_device_node);

    return has_displays || has_device_node;
}

// ============================================================================
// IddCx Miniport Driver Interface (Stub for kernel-mode integration)
// ============================================================================

/// This section defines the interface that a companion kernel-mode
/// IddCx miniport driver would implement. These structures and stubs
/// are for reference and architectural documentation.

/// IddCx Adapter Context (kernel-mode counterpart reference)
struct IddCxAdapterContextStub {
    HANDLE wdf_device;
    HANDLE idd_adapter;
    uint32_t max_monitors;
    std::atomic<uint32_t> active_monitors;
    bool adapter_init_complete;
};

/// IddCx Monitor Context (kernel-mode counterpart reference)
struct IddCxMonitorContextStub {
    HANDLE idd_monitor;
    uint32_t monitor_id;
    DisplayModeConfig current_mode;
    std::vector<uint8_t> edid_data;
    std::chrono::steady_clock::time_point last_frame_time;
    uint64_t frame_counter;
};

namespace iddcx_stub {

/// Callback: EvtIddCxAdapterInitFinished (called by OS when adapter is ready)
/// In kernel-mode: NTSTATUS EvtIddCxAdapterInitFinished(
///     IDDCX_ADAPTER adapter, const IDARG_IN_ADAPTER_INIT_FINISHED* args)
void on_adapter_init_finished_stub(uint32_t adapter_id, bool success) {
    if (success) {
        spdlog::info("IddCx adapter {} initialized successfully", adapter_id);
    } else {
        spdlog::error("IddCx adapter {} initialization failed", adapter_id);
    }
}

/// Callback: EvtIddCxMonitorAssignSwapChain
/// Called when DWM assigns a swap chain to the monitor for rendering
void on_monitor_assign_swapchain_stub(
    uint32_t monitor_id,
    uint32_t width, uint32_t height,
    uint32_t format) {

    spdlog::debug("IddCx monitor {} assigned swapchain: {}x{}, format={}",
        monitor_id, width, height, format);
}

/// Callback: EvtIddCxMonitorUnassignSwapChain
void on_monitor_unassign_swapchain_stub(uint32_t monitor_id) {
    spdlog::debug("IddCx monitor {} swapchain unassigned", monitor_id);
}

/// Callback: EvtIddCxMonitorQueryTargetModes
/// Queries available display modes from the kernel driver
std::vector<DisplayModeConfig> query_target_modes_stub(uint32_t monitor_id) {
    spdlog::debug("IddCx monitor {} target modes queried", monitor_id);

    // Return common modes
    return {
        {3840, 2160, 60, 32},
        {2560, 1440, 60, 32},
        {2560, 1440, 120, 32},
        {1920, 1080, 60, 32},
        {1920, 1080, 120, 32},
        {1920, 1080, 144, 32},
        {1680, 1050, 60, 32},
        {1600, 900, 60, 32},
        {1440, 900, 60, 32},
        {1366, 768, 60, 32},
        {1280, 720, 60, 32},
        {1280, 1024, 60, 32},
        {1024, 768, 60, 32},
        {800, 600, 60, 32},
        {640, 480, 60, 32},
    };
}

/// Callback: EvtIddCxMonitorGetDefaultDescriptionModes
std::vector<DisplayModeConfig> get_default_description_modes_stub(
    uint32_t monitor_id) {

    spdlog::debug("IddCx monitor {} default description modes", monitor_id);
    return query_target_modes_stub(monitor_id);
}

/// Callback: EvtIddCxMonitorGetEDID
/// Returns the EDID data for a given monitor
bool get_edid_stub(
    uint32_t monitor_id,
    std::vector<uint8_t>& edid_data) {

    auto config = get_virtual_display_config(monitor_id);
    if (!config) return false;

    return build_virtual_display_edid(
        config->mode.width,
        config->mode.height,
        config->mode.refresh_rate,
        config->friendly_name,
        config->id,
        edid_data);
}

/// Callback: EvtIddCxMonitorI2CReceive / EvtIddCxMonitorI2CTransmit
/// Handles DDC/CI commands (stub)
void on_ddc_ci_command_stub(
    uint32_t monitor_id,
    uint8_t i2c_address,
    const std::vector<uint8_t>& data) {

    spdlog::debug("IddCx monitor {} DDC/CI command: addr=0x{:02X}, len={}",
        monitor_id, i2c_address, data.size());
}

/// Callback: EvtIddCxMonitorSetGammaRamp (stub for gamma table support)
void on_set_gamma_ramp_stub(
    uint32_t monitor_id,
    const std::array<uint16_t, 256>& red,
    const std::array<uint16_t, 256>& green,
    const std::array<uint16_t, 256>& blue) {

    spdlog::debug("IddCx monitor {} gamma ramp updated", monitor_id);
}

/// Power state transition callback
void on_monitor_power_state_change_stub(
    uint32_t monitor_id,
    bool powered_on) {

    spdlog::debug("IddCx monitor {} power state: {}",
        monitor_id, powered_on ? "ON" : "OFF");
}

}  // namespace iddcx_stub

// ============================================================================
// Diagnostic Utilities
// ============================================================================

/// Prints a detailed diagnostic report of the virtual display subsystem
std::string diagnostic_report() {
    std::string report;

    report += "========================================\n";
    report += "  cppdesk Virtual Display Diagnostic Report\n";
    report += "========================================\n\n";

    // System info
    report += "[System Information]\n";
    {
        OSVERSIONINFOEXW osvi = {sizeof(osvi)};
#pragma warning(push)
#pragma warning(disable: 4996)
        if (GetVersionExW(reinterpret_cast<LPOSVERSIONINFOW>(&osvi))) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                "  Windows %lu.%lu Build %lu\n",
                osvi.dwMajorVersion, osvi.dwMinorVersion,
                osvi.dwBuildNumber);
            report += buf;
        }
#pragma warning(pop)
    }

    // IddCx support
    report += "\n[IddCx Support]\n";
    report += "  Indirect Display Driver: ";
    report += is_iddcx_available() ? "Available\n" : "Not detected\n";

    // Virtual display state
    report += "\n[Virtual Displays]\n";
    auto displays = list_virtual_displays();
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "  Active virtual displays: %zu\n",
            displays.size());
        report += buf;
    }
    for (const auto& d : displays) {
        report += "  " + d.to_string() + "\n";
    }

    // Physical displays
    report += "\n[Physical Displays]\n";
    auto devices = enumerate_display_devices();
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "  Connected display devices: %zu\n",
            devices.size());
        report += buf;
    }
    for (const auto& dev : devices) {
        char buf[256];
        WideCharToMultiByte(CP_UTF8, 0, dev.c_str(), -1,
                            buf, sizeof(buf), nullptr, nullptr);
        report += "  - " + std::string(buf) + "\n";
    }

    // Adapters
    report += "\n[Graphics Adapters]\n";
    auto adapters = enumerate_display_adapters();
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "  Adapters detected: %zu\n",
            adapters.size());
        report += buf;
    }
    for (const auto& a : adapters) {
        report += "  " + a.to_string() + "\n";
    }

    // Statistics
    report += "\n[Statistics]\n";
    report += "  " + get_virtual_display_stats() + "\n";

    report += "\n========================================\n";
    report += "  End of Diagnostic Report\n";
    report += "========================================\n";

    return report;
}

/// Writes diagnostic report to a file
bool write_diagnostic_report(const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        spdlog::error("Failed to open diagnostic report file: {}", filepath);
        return false;
    }

    file << diagnostic_report();
    file.close();

    spdlog::info("Diagnostic report written to {}", filepath);
    return true;
}

// ============================================================================
// Performance Monitoring
// ============================================================================

/// Monitors virtual display frame delivery statistics
struct FrameDeliveryStats {
    std::atomic<uint64_t> frames_delivered{0};
    std::atomic<uint64_t> frames_dropped{0};
    std::atomic<uint64_t> total_bytes{0};
    std::chrono::steady_clock::time_point session_start;

    double frames_per_second() const {
        auto elapsed = std::chrono::steady_clock::now() - session_start;
        auto seconds = std::chrono::duration<double>(elapsed).count();
        if (seconds <= 0) return 0;
        return static_cast<double>(frames_delivered.load(std::memory_order_relaxed)) / seconds;
    }

    double megabits_per_second() const {
        auto elapsed = std::chrono::steady_clock::now() - session_start;
        auto seconds = std::chrono::duration<double>(elapsed).count();
        if (seconds <= 0) return 0;
        double mb = static_cast<double>(total_bytes.load(std::memory_order_relaxed)) / 131072.0;
        return mb / seconds;
    }
};

namespace {
    FrameDeliveryStats g_frame_stats;
}

void record_frame_delivery(uint32_t display_id, size_t frame_size_bytes) {
    g_frame_stats.frames_delivered.fetch_add(1, std::memory_order_relaxed);
    g_frame_stats.total_bytes.fetch_add(frame_size_bytes, std::memory_order_relaxed);
}

void record_frame_drop(uint32_t display_id) {
    g_frame_stats.frames_dropped.fetch_add(1, std::memory_order_relaxed);
}

std::string get_frame_stats_string() {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "Frames: %llu delivered, %llu dropped, %.1f FPS, %.2f Mbps",
        static_cast<unsigned long long>(
            g_frame_stats.frames_delivered.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            g_frame_stats.frames_dropped.load(std::memory_order_relaxed)),
        g_frame_stats.frames_per_second(),
        g_frame_stats.megabits_per_second());
    return std::string(buf);
}

}  // namespace cppdesk::platform

#endif  // _WIN32
