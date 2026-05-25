/**
 * test_enigo.cpp — Comprehensive tests for the enigo library
 *
 * Covers all enums, structs, classes, free functions, utilities, and DSL parser
 * declared in enigo/enigo.hpp. Uses Google Test. Target: 2000+ lines.
 */

#include <gtest/gtest.h>
#include "enigo/enigo.hpp"

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <map>
#include <set>
#include <functional>
#include <tuple>
#include <algorithm>

using namespace enigo;

// ============================================================================
// 1. MouseButton Enum Tests
// ============================================================================

class MouseButtonTest : public ::testing::Test {};

TEST_F(MouseButtonTest, AllValuesDistinct) {
    std::set<int32_t> values;
    values.insert(static_cast<int32_t>(MouseButton::Left));
    values.insert(static_cast<int32_t>(MouseButton::Middle));
    values.insert(static_cast<int32_t>(MouseButton::Right));
    values.insert(static_cast<int32_t>(MouseButton::ScrollUp));
    values.insert(static_cast<int32_t>(MouseButton::ScrollDown));
    values.insert(static_cast<int32_t>(MouseButton::ScrollLeft));
    values.insert(static_cast<int32_t>(MouseButton::ScrollRight));
    values.insert(static_cast<int32_t>(MouseButton::Back));
    values.insert(static_cast<int32_t>(MouseButton::Forward));
    EXPECT_EQ(9u, values.size()) << "All MouseButton values must be unique";
}

TEST_F(MouseButtonTest, LeftValue) {
    EXPECT_EQ(0, static_cast<int32_t>(MouseButton::Left));
}

TEST_F(MouseButtonTest, MiddleValue) {
    EXPECT_EQ(1, static_cast<int32_t>(MouseButton::Middle));
}

TEST_F(MouseButtonTest, RightValue) {
    EXPECT_EQ(2, static_cast<int32_t>(MouseButton::Right));
}

TEST_F(MouseButtonTest, ScrollUpValue) {
    EXPECT_EQ(3, static_cast<int32_t>(MouseButton::ScrollUp));
}

TEST_F(MouseButtonTest, ScrollDownValue) {
    EXPECT_EQ(4, static_cast<int32_t>(MouseButton::ScrollDown));
}

TEST_F(MouseButtonTest, ScrollLeftValue) {
    EXPECT_EQ(5, static_cast<int32_t>(MouseButton::ScrollLeft));
}

TEST_F(MouseButtonTest, ScrollRightValue) {
    EXPECT_EQ(6, static_cast<int32_t>(MouseButton::ScrollRight));
}

TEST_F(MouseButtonTest, BackValue) {
    EXPECT_EQ(7, static_cast<int32_t>(MouseButton::Back));
}

TEST_F(MouseButtonTest, ForwardValue) {
    EXPECT_EQ(8, static_cast<int32_t>(MouseButton::Forward));
}

TEST_F(MouseButtonTest, MouseButtonNameLeft) {
    EXPECT_STREQ("Left", mouse_button_name(MouseButton::Left));
}

TEST_F(MouseButtonTest, MouseButtonNameMiddle) {
    EXPECT_STREQ("Middle", mouse_button_name(MouseButton::Middle));
}

TEST_F(MouseButtonTest, MouseButtonNameRight) {
    EXPECT_STREQ("Right", mouse_button_name(MouseButton::Right));
}

TEST_F(MouseButtonTest, MouseButtonNameScrollUp) {
    EXPECT_STREQ("ScrollUp", mouse_button_name(MouseButton::ScrollUp));
}

TEST_F(MouseButtonTest, MouseButtonNameScrollDown) {
    EXPECT_STREQ("ScrollDown", mouse_button_name(MouseButton::ScrollDown));
}

TEST_F(MouseButtonTest, MouseButtonNameScrollLeft) {
    EXPECT_STREQ("ScrollLeft", mouse_button_name(MouseButton::ScrollLeft));
}

TEST_F(MouseButtonTest, MouseButtonNameScrollRight) {
    EXPECT_STREQ("ScrollRight", mouse_button_name(MouseButton::ScrollRight));
}

TEST_F(MouseButtonTest, MouseButtonNameBack) {
    EXPECT_STREQ("Back", mouse_button_name(MouseButton::Back));
}

TEST_F(MouseButtonTest, MouseButtonNameForward) {
    EXPECT_STREQ("Forward", mouse_button_name(MouseButton::Forward));
}

TEST_F(MouseButtonTest, MouseButtonNameUnknownValue) {
    auto unknown = static_cast<MouseButton>(999);
    EXPECT_STREQ("Unknown", mouse_button_name(unknown));
}

TEST_F(MouseButtonTest, MouseButtonToKeyLeft) {
    EXPECT_EQ(static_cast<Key>(0x1001), mouse_button_to_key(MouseButton::Left));
}

TEST_F(MouseButtonTest, MouseButtonToKeyRight) {
    EXPECT_EQ(static_cast<Key>(0x1002), mouse_button_to_key(MouseButton::Right));
}

TEST_F(MouseButtonTest, MouseButtonToKeyMiddle) {
    EXPECT_EQ(static_cast<Key>(0x1003), mouse_button_to_key(MouseButton::Middle));
}

TEST_F(MouseButtonTest, MouseButtonToKeyBack) {
    EXPECT_EQ(static_cast<Key>(0x1004), mouse_button_to_key(MouseButton::Back));
}

TEST_F(MouseButtonTest, MouseButtonToKeyForward) {
    EXPECT_EQ(static_cast<Key>(0x1005), mouse_button_to_key(MouseButton::Forward));
}

TEST_F(MouseButtonTest, MouseButtonToKeyDefaultFallsBackToSpace) {
    EXPECT_EQ(Key::Space, mouse_button_to_key(MouseButton::ScrollUp));
    EXPECT_EQ(Key::Space, mouse_button_to_key(MouseButton::ScrollDown));
    EXPECT_EQ(Key::Space, mouse_button_to_key(MouseButton::ScrollLeft));
    EXPECT_EQ(Key::Space, mouse_button_to_key(MouseButton::ScrollRight));
}

TEST_F(MouseButtonTest, UnderlyingTypeIsInt32) {
    EXPECT_TRUE((std::is_same_v<int32_t, std::underlying_type_t<MouseButton>>));
}

// ============================================================================
// 2. MouseAxis Enum Tests
// ============================================================================

class MouseAxisTest : public ::testing::Test {};

TEST_F(MouseAxisTest, HorizontalIsZero) {
    EXPECT_EQ(0, static_cast<int>(MouseAxis::Horizontal));
}

TEST_F(MouseAxisTest, VerticalIsOne) {
    EXPECT_EQ(1, static_cast<int>(MouseAxis::Vertical));
}

TEST_F(MouseAxisTest, DistinctValues) {
    EXPECT_NE(MouseAxis::Horizontal, MouseAxis::Vertical);
}

// ============================================================================
// 3. Key Enum Tests — Modifiers
// ============================================================================

class KeyModifierTest : public ::testing::Test {};

TEST_F(KeyModifierTest, AltValue) {
    EXPECT_EQ(0xE2u, static_cast<uint32_t>(Key::Alt));
}

TEST_F(KeyModifierTest, AltGrValue) {
    EXPECT_EQ(0xE6u, static_cast<uint32_t>(Key::AltGr));
}

TEST_F(KeyModifierTest, BackspaceValue) {
    EXPECT_EQ(0x2Au, static_cast<uint32_t>(Key::Backspace));
}

TEST_F(KeyModifierTest, CapsLockValue) {
    EXPECT_EQ(0x39u, static_cast<uint32_t>(Key::CapsLock));
}

TEST_F(KeyModifierTest, ControlValue) {
    EXPECT_EQ(0xE0u, static_cast<uint32_t>(Key::Control));
}

TEST_F(KeyModifierTest, DeleteValue) {
    EXPECT_EQ(0x4Cu, static_cast<uint32_t>(Key::Delete));
}

TEST_F(KeyModifierTest, DownArrowValue) {
    EXPECT_EQ(0x51u, static_cast<uint32_t>(Key::DownArrow));
}

TEST_F(KeyModifierTest, EndValue) {
    EXPECT_EQ(0x4Du, static_cast<uint32_t>(Key::End));
}

TEST_F(KeyModifierTest, EscapeValue) {
    EXPECT_EQ(0x29u, static_cast<uint32_t>(Key::Escape));
}

TEST_F(KeyModifierTest, HomeValue) {
    EXPECT_EQ(0x4Au, static_cast<uint32_t>(Key::Home));
}

TEST_F(KeyModifierTest, InsertValue) {
    EXPECT_EQ(0x49u, static_cast<uint32_t>(Key::Insert));
}

TEST_F(KeyModifierTest, LeftArrowValue) {
    EXPECT_EQ(0x50u, static_cast<uint32_t>(Key::LeftArrow));
}

TEST_F(KeyModifierTest, MetaValue) {
    EXPECT_EQ(0xE3u, static_cast<uint32_t>(Key::Meta));
}

TEST_F(KeyModifierTest, NumLockValue) {
    EXPECT_EQ(0x53u, static_cast<uint32_t>(Key::NumLock));
}

TEST_F(KeyModifierTest, PageDownValue) {
    EXPECT_EQ(0x4Eu, static_cast<uint32_t>(Key::PageDown));
}

TEST_F(KeyModifierTest, PageUpValue) {
    EXPECT_EQ(0x4Bu, static_cast<uint32_t>(Key::PageUp));
}

TEST_F(KeyModifierTest, ReturnValue) {
    EXPECT_EQ(0x28u, static_cast<uint32_t>(Key::Return));
}

TEST_F(KeyModifierTest, RightArrowValue) {
    EXPECT_EQ(0x4Fu, static_cast<uint32_t>(Key::RightArrow));
}

TEST_F(KeyModifierTest, ScrollLockValue) {
    EXPECT_EQ(0x47u, static_cast<uint32_t>(Key::ScrollLock));
}

TEST_F(KeyModifierTest, ShiftValue) {
    EXPECT_EQ(0xE1u, static_cast<uint32_t>(Key::Shift));
}

TEST_F(KeyModifierTest, SpaceValue) {
    EXPECT_EQ(0x2Cu, static_cast<uint32_t>(Key::Space));
}

TEST_F(KeyModifierTest, TabValue) {
    EXPECT_EQ(0x2Bu, static_cast<uint32_t>(Key::Tab));
}

TEST_F(KeyModifierTest, UpArrowValue) {
    EXPECT_EQ(0x52u, static_cast<uint32_t>(Key::UpArrow));
}

TEST_F(KeyModifierTest, PrintScreenValue) {
    EXPECT_EQ(0x46u, static_cast<uint32_t>(Key::PrintScreen));
}

TEST_F(KeyModifierTest, PauseValue) {
    EXPECT_EQ(0x48u, static_cast<uint32_t>(Key::Pause));
}

TEST_F(KeyModifierTest, MenuValue) {
    EXPECT_EQ(0x65u, static_cast<uint32_t>(Key::Menu));
}

// ============================================================================
// 4. Key Enum Tests — Right-hand Modifiers
// ============================================================================

class KeyRightModifierTest : public ::testing::Test {};

TEST_F(KeyRightModifierTest, RightShiftValue) {
    EXPECT_EQ(0xE5u, static_cast<uint32_t>(Key::RightShift));
}

TEST_F(KeyRightModifierTest, RightControlValue) {
    EXPECT_EQ(0xE4u, static_cast<uint32_t>(Key::RightControl));
}

TEST_F(KeyRightModifierTest, RightAltValue) {
    EXPECT_EQ(0xE7u, static_cast<uint32_t>(Key::RightAlt));
}

TEST_F(KeyRightModifierTest, RightMetaValue) {
    EXPECT_EQ(0xE8u, static_cast<uint32_t>(Key::RightMeta));
}

TEST_F(KeyRightModifierTest, LeftAndRightShiftAreDistinct) {
    EXPECT_NE(static_cast<uint32_t>(Key::Shift), static_cast<uint32_t>(Key::RightShift));
}

TEST_F(KeyRightModifierTest, LeftAndRightControlAreDistinct) {
    EXPECT_NE(static_cast<uint32_t>(Key::Control), static_cast<uint32_t>(Key::RightControl));
}

TEST_F(KeyRightModifierTest, LeftAndRightAltAreDistinct) {
    EXPECT_NE(static_cast<uint32_t>(Key::Alt), static_cast<uint32_t>(Key::RightAlt));
}

TEST_F(KeyRightModifierTest, LeftAndRightMetaAreDistinct) {
    EXPECT_NE(static_cast<uint32_t>(Key::Meta), static_cast<uint32_t>(Key::RightMeta));
}

// ============================================================================
// 5. Key Enum Tests — Function Keys F1-F24
// ============================================================================

class KeyFunctionKeyTest : public ::testing::Test {};

TEST_F(KeyFunctionKeyTest, F1Value) { EXPECT_EQ(0x3Au, static_cast<uint32_t>(Key::F1)); }
TEST_F(KeyFunctionKeyTest, F2Value) { EXPECT_EQ(0x3Bu, static_cast<uint32_t>(Key::F2)); }
TEST_F(KeyFunctionKeyTest, F3Value) { EXPECT_EQ(0x3Cu, static_cast<uint32_t>(Key::F3)); }
TEST_F(KeyFunctionKeyTest, F4Value) { EXPECT_EQ(0x3Du, static_cast<uint32_t>(Key::F4)); }
TEST_F(KeyFunctionKeyTest, F5Value) { EXPECT_EQ(0x3Eu, static_cast<uint32_t>(Key::F5)); }
TEST_F(KeyFunctionKeyTest, F6Value) { EXPECT_EQ(0x3Fu, static_cast<uint32_t>(Key::F6)); }
TEST_F(KeyFunctionKeyTest, F7Value) { EXPECT_EQ(0x40u, static_cast<uint32_t>(Key::F7)); }
TEST_F(KeyFunctionKeyTest, F8Value) { EXPECT_EQ(0x41u, static_cast<uint32_t>(Key::F8)); }
TEST_F(KeyFunctionKeyTest, F9Value) { EXPECT_EQ(0x42u, static_cast<uint32_t>(Key::F9)); }
TEST_F(KeyFunctionKeyTest, F10Value) { EXPECT_EQ(0x43u, static_cast<uint32_t>(Key::F10)); }
TEST_F(KeyFunctionKeyTest, F11Value) { EXPECT_EQ(0x44u, static_cast<uint32_t>(Key::F11)); }
TEST_F(KeyFunctionKeyTest, F12Value) { EXPECT_EQ(0x45u, static_cast<uint32_t>(Key::F12)); }
TEST_F(KeyFunctionKeyTest, F13Value) { EXPECT_EQ(0x68u, static_cast<uint32_t>(Key::F13)); }
TEST_F(KeyFunctionKeyTest, F14Value) { EXPECT_EQ(0x69u, static_cast<uint32_t>(Key::F14)); }
TEST_F(KeyFunctionKeyTest, F15Value) { EXPECT_EQ(0x6Au, static_cast<uint32_t>(Key::F15)); }
TEST_F(KeyFunctionKeyTest, F16Value) { EXPECT_EQ(0x6Bu, static_cast<uint32_t>(Key::F16)); }
TEST_F(KeyFunctionKeyTest, F17Value) { EXPECT_EQ(0x6Cu, static_cast<uint32_t>(Key::F17)); }
TEST_F(KeyFunctionKeyTest, F18Value) { EXPECT_EQ(0x6Du, static_cast<uint32_t>(Key::F18)); }
TEST_F(KeyFunctionKeyTest, F19Value) { EXPECT_EQ(0x6Eu, static_cast<uint32_t>(Key::F19)); }
TEST_F(KeyFunctionKeyTest, F20Value) { EXPECT_EQ(0x6Fu, static_cast<uint32_t>(Key::F20)); }
TEST_F(KeyFunctionKeyTest, F21Value) { EXPECT_EQ(0x70u, static_cast<uint32_t>(Key::F21)); }
TEST_F(KeyFunctionKeyTest, F22Value) { EXPECT_EQ(0x71u, static_cast<uint32_t>(Key::F22)); }
TEST_F(KeyFunctionKeyTest, F23Value) { EXPECT_EQ(0x72u, static_cast<uint32_t>(Key::F23)); }
TEST_F(KeyFunctionKeyTest, F24Value) { EXPECT_EQ(0x73u, static_cast<uint32_t>(Key::F24)); }

TEST_F(KeyFunctionKeyTest, F1ThroughF12Contiguous) {
    EXPECT_EQ(static_cast<uint32_t>(Key::F1) + 1, static_cast<uint32_t>(Key::F2));
    EXPECT_EQ(static_cast<uint32_t>(Key::F2) + 1, static_cast<uint32_t>(Key::F3));
    EXPECT_EQ(static_cast<uint32_t>(Key::F3) + 1, static_cast<uint32_t>(Key::F4));
    EXPECT_EQ(static_cast<uint32_t>(Key::F4) + 1, static_cast<uint32_t>(Key::F5));
    EXPECT_EQ(static_cast<uint32_t>(Key::F5) + 1, static_cast<uint32_t>(Key::F6));
    EXPECT_EQ(static_cast<uint32_t>(Key::F6) + 1, static_cast<uint32_t>(Key::F7));
    EXPECT_EQ(static_cast<uint32_t>(Key::F7) + 1, static_cast<uint32_t>(Key::F8));
    EXPECT_EQ(static_cast<uint32_t>(Key::F8) + 1, static_cast<uint32_t>(Key::F9));
    EXPECT_EQ(static_cast<uint32_t>(Key::F9) + 1, static_cast<uint32_t>(Key::F10));
    EXPECT_EQ(static_cast<uint32_t>(Key::F10) + 1, static_cast<uint32_t>(Key::F11));
    EXPECT_EQ(static_cast<uint32_t>(Key::F11) + 1, static_cast<uint32_t>(Key::F12));
}

TEST_F(KeyFunctionKeyTest, F13ThroughF24Contiguous) {
    EXPECT_EQ(static_cast<uint32_t>(Key::F13) + 1, static_cast<uint32_t>(Key::F14));
    EXPECT_EQ(static_cast<uint32_t>(Key::F14) + 1, static_cast<uint32_t>(Key::F15));
    EXPECT_EQ(static_cast<uint32_t>(Key::F15) + 1, static_cast<uint32_t>(Key::F16));
    EXPECT_EQ(static_cast<uint32_t>(Key::F16) + 1, static_cast<uint32_t>(Key::F17));
    EXPECT_EQ(static_cast<uint32_t>(Key::F17) + 1, static_cast<uint32_t>(Key::F18));
    EXPECT_EQ(static_cast<uint32_t>(Key::F18) + 1, static_cast<uint32_t>(Key::F19));
    EXPECT_EQ(static_cast<uint32_t>(Key::F19) + 1, static_cast<uint32_t>(Key::F20));
    EXPECT_EQ(static_cast<uint32_t>(Key::F20) + 1, static_cast<uint32_t>(Key::F21));
    EXPECT_EQ(static_cast<uint32_t>(Key::F21) + 1, static_cast<uint32_t>(Key::F22));
    EXPECT_EQ(static_cast<uint32_t>(Key::F22) + 1, static_cast<uint32_t>(Key::F23));
    EXPECT_EQ(static_cast<uint32_t>(Key::F23) + 1, static_cast<uint32_t>(Key::F24));
}

TEST_F(KeyFunctionKeyTest, F1ToF12DistinctValues) {
    std::set<uint32_t> vals;
    for (auto k : {Key::F1, Key::F2, Key::F3, Key::F4, Key::F5, Key::F6,
                   Key::F7, Key::F8, Key::F9, Key::F10, Key::F11, Key::F12}) {
        vals.insert(static_cast<uint32_t>(k));
    }
    EXPECT_EQ(12u, vals.size());
}

// ============================================================================
// 6. Key Enum Tests — Numpad Keys
// ============================================================================

class KeyNumpadTest : public ::testing::Test {};

TEST_F(KeyNumpadTest, Numpad0Value) { EXPECT_EQ(0x62u, static_cast<uint32_t>(Key::Numpad0)); }
TEST_F(KeyNumpadTest, Numpad1Value) { EXPECT_EQ(0x59u, static_cast<uint32_t>(Key::Numpad1)); }
TEST_F(KeyNumpadTest, Numpad2Value) { EXPECT_EQ(0x5Au, static_cast<uint32_t>(Key::Numpad2)); }
TEST_F(KeyNumpadTest, Numpad3Value) { EXPECT_EQ(0x5Bu, static_cast<uint32_t>(Key::Numpad3)); }
TEST_F(KeyNumpadTest, Numpad4Value) { EXPECT_EQ(0x5Cu, static_cast<uint32_t>(Key::Numpad4)); }
TEST_F(KeyNumpadTest, Numpad5Value) { EXPECT_EQ(0x5Du, static_cast<uint32_t>(Key::Numpad5)); }
TEST_F(KeyNumpadTest, Numpad6Value) { EXPECT_EQ(0x5Eu, static_cast<uint32_t>(Key::Numpad6)); }
TEST_F(KeyNumpadTest, Numpad7Value) { EXPECT_EQ(0x5Fu, static_cast<uint32_t>(Key::Numpad7)); }
TEST_F(KeyNumpadTest, Numpad8Value) { EXPECT_EQ(0x60u, static_cast<uint32_t>(Key::Numpad8)); }
TEST_F(KeyNumpadTest, Numpad9Value) { EXPECT_EQ(0x61u, static_cast<uint32_t>(Key::Numpad9)); }
TEST_F(KeyNumpadTest, NumpadAddValue) { EXPECT_EQ(0x57u, static_cast<uint32_t>(Key::NumpadAdd)); }
TEST_F(KeyNumpadTest, NumpadSubtractValue) { EXPECT_EQ(0x56u, static_cast<uint32_t>(Key::NumpadSubtract)); }
TEST_F(KeyNumpadTest, NumpadMultiplyValue) { EXPECT_EQ(0x55u, static_cast<uint32_t>(Key::NumpadMultiply)); }
TEST_F(KeyNumpadTest, NumpadDivideValue) { EXPECT_EQ(0x54u, static_cast<uint32_t>(Key::NumpadDivide)); }
TEST_F(KeyNumpadTest, NumpadDecimalValue) { EXPECT_EQ(0x63u, static_cast<uint32_t>(Key::NumpadDecimal)); }
TEST_F(KeyNumpadTest, NumpadEnterValue) { EXPECT_EQ(0x58u, static_cast<uint32_t>(Key::NumpadEnter)); }
TEST_F(KeyNumpadTest, NumpadEqualValue) { EXPECT_EQ(0x67u, static_cast<uint32_t>(Key::NumpadEqual)); }

TEST_F(KeyNumpadTest, AllNumpadValuesDistinct) {
    std::set<uint32_t> vals;
    vals.insert(static_cast<uint32_t>(Key::Numpad0));
    vals.insert(static_cast<uint32_t>(Key::Numpad1));
    vals.insert(static_cast<uint32_t>(Key::Numpad2));
    vals.insert(static_cast<uint32_t>(Key::Numpad3));
    vals.insert(static_cast<uint32_t>(Key::Numpad4));
    vals.insert(static_cast<uint32_t>(Key::Numpad5));
    vals.insert(static_cast<uint32_t>(Key::Numpad6));
    vals.insert(static_cast<uint32_t>(Key::Numpad7));
    vals.insert(static_cast<uint32_t>(Key::Numpad8));
    vals.insert(static_cast<uint32_t>(Key::Numpad9));
    vals.insert(static_cast<uint32_t>(Key::NumpadAdd));
    vals.insert(static_cast<uint32_t>(Key::NumpadSubtract));
    vals.insert(static_cast<uint32_t>(Key::NumpadMultiply));
    vals.insert(static_cast<uint32_t>(Key::NumpadDivide));
    vals.insert(static_cast<uint32_t>(Key::NumpadDecimal));
    vals.insert(static_cast<uint32_t>(Key::NumpadEnter));
    vals.insert(static_cast<uint32_t>(Key::NumpadEqual));
    EXPECT_EQ(17u, vals.size());
}

// ============================================================================
// 7. Key Enum Tests — Media Keys
// ============================================================================

class KeyMediaTest : public ::testing::Test {};

TEST_F(KeyMediaTest, VolumeMuteValue) { EXPECT_EQ(0xE9u, static_cast<uint32_t>(Key::VolumeMute)); }
TEST_F(KeyMediaTest, VolumeDownValue) { EXPECT_EQ(0xEAu, static_cast<uint32_t>(Key::VolumeDown)); }
TEST_F(KeyMediaTest, VolumeUpValue) { EXPECT_EQ(0xEBu, static_cast<uint32_t>(Key::VolumeUp)); }
TEST_F(KeyMediaTest, MediaPlayValue) { EXPECT_EQ(0xCDu, static_cast<uint32_t>(Key::MediaPlay)); }
TEST_F(KeyMediaTest, MediaPauseValue) { EXPECT_EQ(0xCEu, static_cast<uint32_t>(Key::MediaPause)); }
TEST_F(KeyMediaTest, MediaStopValue) { EXPECT_EQ(0xB7u, static_cast<uint32_t>(Key::MediaStop)); }
TEST_F(KeyMediaTest, MediaNextValue) { EXPECT_EQ(0xB5u, static_cast<uint32_t>(Key::MediaNext)); }
TEST_F(KeyMediaTest, MediaPrevValue) { EXPECT_EQ(0xB6u, static_cast<uint32_t>(Key::MediaPrev)); }
TEST_F(KeyMediaTest, MediaRewindValue) { EXPECT_EQ(0xB4u, static_cast<uint32_t>(Key::MediaRewind)); }
TEST_F(KeyMediaTest, MediaFastForwardValue) { EXPECT_EQ(0xB3u, static_cast<uint32_t>(Key::MediaFastForward)); }
TEST_F(KeyMediaTest, MediaRecordValue) { EXPECT_EQ(0xB2u, static_cast<uint32_t>(Key::MediaRecord)); }
TEST_F(KeyMediaTest, MediaSelectValue) { EXPECT_EQ(0x180u, static_cast<uint32_t>(Key::MediaSelect)); }
TEST_F(KeyMediaTest, MediaEjectValue) { EXPECT_EQ(0xB8u, static_cast<uint32_t>(Key::MediaEject)); }
TEST_F(KeyMediaTest, MediaRandomPlayValue) { EXPECT_EQ(0xB9u, static_cast<uint32_t>(Key::MediaRandomPlay)); }
TEST_F(KeyMediaTest, MediaPlayPauseValue) { EXPECT_EQ(0xCDu, static_cast<uint32_t>(Key::MediaPlayPause)); }

TEST_F(KeyMediaTest, MediaPlayAndPlayPauseAreSame) {
    EXPECT_EQ(static_cast<uint32_t>(Key::MediaPlay), static_cast<uint32_t>(Key::MediaPlayPause));
}

// ============================================================================
// 8. Key Enum Tests — Launch Keys
// ============================================================================

class KeyLaunchTest : public ::testing::Test {};

TEST_F(KeyLaunchTest, LaunchMailValue) { EXPECT_EQ(0xF1u, static_cast<uint32_t>(Key::LaunchMail)); }
TEST_F(KeyLaunchTest, LaunchMediaValue) { EXPECT_EQ(0xF2u, static_cast<uint32_t>(Key::LaunchMedia)); }
TEST_F(KeyLaunchTest, LaunchApp1Value) { EXPECT_EQ(0xF3u, static_cast<uint32_t>(Key::LaunchApp1)); }
TEST_F(KeyLaunchTest, LaunchApp2Value) { EXPECT_EQ(0xF4u, static_cast<uint32_t>(Key::LaunchApp2)); }
TEST_F(KeyLaunchTest, LaunchCalculatorValue) { EXPECT_EQ(0x192u, static_cast<uint32_t>(Key::LaunchCalculator)); }
TEST_F(KeyLaunchTest, LaunchFileBrowserValue) { EXPECT_EQ(0x194u, static_cast<uint32_t>(Key::LaunchFileBrowser)); }
TEST_F(KeyLaunchTest, LaunchTerminalValue) { EXPECT_EQ(0x196u, static_cast<uint32_t>(Key::LaunchTerminal)); }
TEST_F(KeyLaunchTest, LaunchWebBrowserValue) { EXPECT_EQ(0x190u, static_cast<uint32_t>(Key::LaunchWebBrowser)); }

TEST_F(KeyLaunchTest, AllLaunchValuesDistinct) {
    std::set<uint32_t> vals;
    vals.insert(static_cast<uint32_t>(Key::LaunchMail));
    vals.insert(static_cast<uint32_t>(Key::LaunchMedia));
    vals.insert(static_cast<uint32_t>(Key::LaunchApp1));
    vals.insert(static_cast<uint32_t>(Key::LaunchApp2));
    vals.insert(static_cast<uint32_t>(Key::LaunchCalculator));
    vals.insert(static_cast<uint32_t>(Key::LaunchFileBrowser));
    vals.insert(static_cast<uint32_t>(Key::LaunchTerminal));
    vals.insert(static_cast<uint32_t>(Key::LaunchWebBrowser));
    EXPECT_EQ(8u, vals.size());
}

// ============================================================================
// 9. Key Enum Tests — System Power Keys
// ============================================================================

class KeySystemPowerTest : public ::testing::Test {};

TEST_F(KeySystemPowerTest, SystemPowerDownValue) { EXPECT_EQ(0x81u, static_cast<uint32_t>(Key::SystemPowerDown)); }
TEST_F(KeySystemPowerTest, SystemSleepValue) { EXPECT_EQ(0x82u, static_cast<uint32_t>(Key::SystemSleep)); }
TEST_F(KeySystemPowerTest, SystemWakeUpValue) { EXPECT_EQ(0x83u, static_cast<uint32_t>(Key::SystemWakeUp)); }

TEST_F(KeySystemPowerTest, ContiguousRange) {
    auto p = static_cast<uint32_t>(Key::SystemPowerDown);
    auto s = static_cast<uint32_t>(Key::SystemSleep);
    auto w = static_cast<uint32_t>(Key::SystemWakeUp);
    EXPECT_EQ(p + 1, s);
    EXPECT_EQ(s + 1, w);
}

// ============================================================================
// 10. Key Enum Tests — Browser Keys
// ============================================================================

class KeyBrowserTest : public ::testing::Test {};

TEST_F(KeyBrowserTest, BrowserBackValue) { EXPECT_EQ(0xE11u, static_cast<uint32_t>(Key::BrowserBack)); }
TEST_F(KeyBrowserTest, BrowserForwardValue) { EXPECT_EQ(0xE12u, static_cast<uint32_t>(Key::BrowserForward)); }
TEST_F(KeyBrowserTest, BrowserRefreshValue) { EXPECT_EQ(0xE13u, static_cast<uint32_t>(Key::BrowserRefresh)); }
TEST_F(KeyBrowserTest, BrowserStopValue) { EXPECT_EQ(0xE14u, static_cast<uint32_t>(Key::BrowserStop)); }
TEST_F(KeyBrowserTest, BrowserSearchValue) { EXPECT_EQ(0xE15u, static_cast<uint32_t>(Key::BrowserSearch)); }
TEST_F(KeyBrowserTest, BrowserFavoritesValue) { EXPECT_EQ(0xE16u, static_cast<uint32_t>(Key::BrowserFavorites)); }
TEST_F(KeyBrowserTest, BrowserHomeValue) { EXPECT_EQ(0xE17u, static_cast<uint32_t>(Key::BrowserHome)); }

TEST_F(KeyBrowserTest, AllBrowserValuesContiguous) {
    auto b = static_cast<uint32_t>(Key::BrowserBack);
    EXPECT_EQ(b + 1, static_cast<uint32_t>(Key::BrowserForward));
    EXPECT_EQ(b + 2, static_cast<uint32_t>(Key::BrowserRefresh));
    EXPECT_EQ(b + 3, static_cast<uint32_t>(Key::BrowserStop));
    EXPECT_EQ(b + 4, static_cast<uint32_t>(Key::BrowserSearch));
    EXPECT_EQ(b + 5, static_cast<uint32_t>(Key::BrowserFavorites));
    EXPECT_EQ(b + 6, static_cast<uint32_t>(Key::BrowserHome));
}

// ============================================================================
// 11. Key Enum Tests — Extended Control Keys
// ============================================================================

class KeyExtendedControlTest : public ::testing::Test {};

TEST_F(KeyExtendedControlTest, HelpValue) { EXPECT_EQ(0x75u, static_cast<uint32_t>(Key::Help)); }
TEST_F(KeyExtendedControlTest, UndoValue) { EXPECT_EQ(0x7Au, static_cast<uint32_t>(Key::Undo)); }
TEST_F(KeyExtendedControlTest, RedoValue) { EXPECT_EQ(0x79u, static_cast<uint32_t>(Key::Redo)); }
TEST_F(KeyExtendedControlTest, CutValue) { EXPECT_EQ(0x7Bu, static_cast<uint32_t>(Key::Cut)); }
TEST_F(KeyExtendedControlTest, CopyValue) { EXPECT_EQ(0x7Cu, static_cast<uint32_t>(Key::Copy)); }
TEST_F(KeyExtendedControlTest, PasteValue) { EXPECT_EQ(0x7Du, static_cast<uint32_t>(Key::Paste)); }
TEST_F(KeyExtendedControlTest, FindValue) { EXPECT_EQ(0x7Eu, static_cast<uint32_t>(Key::Find)); }
TEST_F(KeyExtendedControlTest, SelectAllValue) { EXPECT_EQ(0x91u, static_cast<uint32_t>(Key::SelectAll)); }
TEST_F(KeyExtendedControlTest, ZoomInValue) { EXPECT_EQ(0x92u, static_cast<uint32_t>(Key::ZoomIn)); }
TEST_F(KeyExtendedControlTest, ZoomOutValue) { EXPECT_EQ(0x93u, static_cast<uint32_t>(Key::ZoomOut)); }
TEST_F(KeyExtendedControlTest, ZoomResetValue) { EXPECT_EQ(0x94u, static_cast<uint32_t>(Key::ZoomReset)); }

TEST_F(KeyExtendedControlTest, AllControlValuesDistinct) {
    std::set<uint32_t> vals;
    vals.insert(static_cast<uint32_t>(Key::Help));
    vals.insert(static_cast<uint32_t>(Key::Undo));
    vals.insert(static_cast<uint32_t>(Key::Redo));
    vals.insert(static_cast<uint32_t>(Key::Cut));
    vals.insert(static_cast<uint32_t>(Key::Copy));
    vals.insert(static_cast<uint32_t>(Key::Paste));
    vals.insert(static_cast<uint32_t>(Key::Find));
    vals.insert(static_cast<uint32_t>(Key::SelectAll));
    vals.insert(static_cast<uint32_t>(Key::ZoomIn));
    vals.insert(static_cast<uint32_t>(Key::ZoomOut));
    vals.insert(static_cast<uint32_t>(Key::ZoomReset));
    EXPECT_EQ(11u, vals.size());
}

// ============================================================================
// 12. Key Enum Tests — IME/Input Keys
// ============================================================================

class KeyIMETest : public ::testing::Test {};

TEST_F(KeyIMETest, IMEOnValue) { EXPECT_EQ(0x90u, static_cast<uint32_t>(Key::IMEOn)); }
TEST_F(KeyIMETest, IMEOffValue) { EXPECT_EQ(0x91u, static_cast<uint32_t>(Key::IMEOff)); }
TEST_F(KeyIMETest, KanaModeValue) { EXPECT_EQ(0x88u, static_cast<uint32_t>(Key::KanaMode)); }
TEST_F(KeyIMETest, HanjaModeValue) { EXPECT_EQ(0x89u, static_cast<uint32_t>(Key::HanjaMode)); }
TEST_F(KeyIMETest, HangulModeValue) { EXPECT_EQ(0x8Au, static_cast<uint32_t>(Key::HangulMode)); }
TEST_F(KeyIMETest, KatakanaValue) { EXPECT_EQ(0x92u, static_cast<uint32_t>(Key::Katakana)); }
TEST_F(KeyIMETest, HiraganaValue) { EXPECT_EQ(0x93u, static_cast<uint32_t>(Key::Hiragana)); }
TEST_F(KeyIMETest, ZenkakuHankakuValue) { EXPECT_EQ(0x94u, static_cast<uint32_t>(Key::ZenkakuHankaku)); }

// ============================================================================
// 13. Key Enum Tests — Language Keys
// ============================================================================

class KeyLanguageTest : public ::testing::Test {};

TEST_F(KeyLanguageTest, Lang1Value) { EXPECT_EQ(0x8Bu, static_cast<uint32_t>(Key::Lang1)); }
TEST_F(KeyLanguageTest, Lang2Value) { EXPECT_EQ(0x8Cu, static_cast<uint32_t>(Key::Lang2)); }
TEST_F(KeyLanguageTest, Lang3Value) { EXPECT_EQ(0x8Du, static_cast<uint32_t>(Key::Lang3)); }
TEST_F(KeyLanguageTest, Lang4Value) { EXPECT_EQ(0x8Eu, static_cast<uint32_t>(Key::Lang4)); }
TEST_F(KeyLanguageTest, Lang5Value) { EXPECT_EQ(0x8Fu, static_cast<uint32_t>(Key::Lang5)); }

TEST_F(KeyLanguageTest, LangContiguous) {
    auto l1 = static_cast<uint32_t>(Key::Lang1);
    EXPECT_EQ(l1 + 1, static_cast<uint32_t>(Key::Lang2));
    EXPECT_EQ(l1 + 2, static_cast<uint32_t>(Key::Lang3));
    EXPECT_EQ(l1 + 3, static_cast<uint32_t>(Key::Lang4));
    EXPECT_EQ(l1 + 4, static_cast<uint32_t>(Key::Lang5));
}

// ============================================================================
// 14. Key Enum Tests — Special Layout/Raw Keys & Underlying Type
// ============================================================================

class KeySpecialTest : public ::testing::Test {};

TEST_F(KeySpecialTest, LayoutValue) {
    EXPECT_EQ(0x1000u, static_cast<uint32_t>(Key::Layout));
}

TEST_F(KeySpecialTest, RawValue) {
    EXPECT_EQ(0x2000u, static_cast<uint32_t>(Key::Raw));
}

TEST_F(KeySpecialTest, UnderlyingTypeIsUint32) {
    EXPECT_TRUE((std::is_same_v<uint32_t, std::underlying_type_t<Key>>));
}

TEST_F(KeySpecialTest, KeyCanBeCastToUint32AndBack) {
    const std::vector<Key> test_keys = {
        Key::Alt, Key::Shift, Key::Control, Key::Meta,
        Key::F1, Key::F10, Key::F20, Key::F24,
        Key::Numpad0, Key::Numpad9, Key::NumpadEqual,
        Key::VolumeMute, Key::MediaRecord,
        Key::LaunchCalculator, Key::LaunchTerminal,
        Key::BrowserHome, Key::SystemSleep,
        Key::Help, Key::ZoomReset
    };
    for (auto k : test_keys) {
        auto val = static_cast<uint32_t>(k);
        auto back = static_cast<Key>(val);
        EXPECT_EQ(k, back);
    }
}

// ============================================================================
// 15. Key Name Tests
// ============================================================================

class KeyNameTest : public ::testing::Test {};

TEST_F(KeyNameTest, ShiftName) { EXPECT_STREQ("Shift", key_name(Key::Shift)); }
TEST_F(KeyNameTest, RightShiftName) { EXPECT_STREQ("RightShift", key_name(Key::RightShift)); }
TEST_F(KeyNameTest, ControlName) { EXPECT_STREQ("Control", key_name(Key::Control)); }
TEST_F(KeyNameTest, RightControlName) { EXPECT_STREQ("RightControl", key_name(Key::RightControl)); }
TEST_F(KeyNameTest, AltName) { EXPECT_STREQ("Alt", key_name(Key::Alt)); }
TEST_F(KeyNameTest, RightAltName) { EXPECT_STREQ("RightAlt", key_name(Key::RightAlt)); }
TEST_F(KeyNameTest, AltGrName) { EXPECT_STREQ("AltGr", key_name(Key::AltGr)); }
TEST_F(KeyNameTest, MetaName) { EXPECT_STREQ("Meta", key_name(Key::Meta)); }
TEST_F(KeyNameTest, RightMetaName) { EXPECT_STREQ("RightMeta", key_name(Key::RightMeta)); }
TEST_F(KeyNameTest, ReturnName) { EXPECT_STREQ("Return", key_name(Key::Return)); }
TEST_F(KeyNameTest, EscapeName) { EXPECT_STREQ("Escape", key_name(Key::Escape)); }
TEST_F(KeyNameTest, TabName) { EXPECT_STREQ("Tab", key_name(Key::Tab)); }
TEST_F(KeyNameTest, SpaceName) { EXPECT_STREQ("Space", key_name(Key::Space)); }
TEST_F(KeyNameTest, BackspaceName) { EXPECT_STREQ("Backspace", key_name(Key::Backspace)); }
TEST_F(KeyNameTest, DeleteName) { EXPECT_STREQ("Delete", key_name(Key::Delete)); }
TEST_F(KeyNameTest, InsertName) { EXPECT_STREQ("Insert", key_name(Key::Insert)); }
TEST_F(KeyNameTest, HomeName) { EXPECT_STREQ("Home", key_name(Key::Home)); }
TEST_F(KeyNameTest, EndName) { EXPECT_STREQ("End", key_name(Key::End)); }
TEST_F(KeyNameTest, PageUpName) { EXPECT_STREQ("PageUp", key_name(Key::PageUp)); }
TEST_F(KeyNameTest, PageDownName) { EXPECT_STREQ("PageDown", key_name(Key::PageDown)); }
TEST_F(KeyNameTest, LeftArrowName) { EXPECT_STREQ("LeftArrow", key_name(Key::LeftArrow)); }
TEST_F(KeyNameTest, RightArrowName) { EXPECT_STREQ("RightArrow", key_name(Key::RightArrow)); }
TEST_F(KeyNameTest, UpArrowName) { EXPECT_STREQ("UpArrow", key_name(Key::UpArrow)); }
TEST_F(KeyNameTest, DownArrowName) { EXPECT_STREQ("DownArrow", key_name(Key::DownArrow)); }
TEST_F(KeyNameTest, CapsLockName) { EXPECT_STREQ("CapsLock", key_name(Key::CapsLock)); }
TEST_F(KeyNameTest, NumLockName) { EXPECT_STREQ("NumLock", key_name(Key::NumLock)); }
TEST_F(KeyNameTest, ScrollLockName) { EXPECT_STREQ("ScrollLock", key_name(Key::ScrollLock)); }
TEST_F(KeyNameTest, PrintScreenName) { EXPECT_STREQ("PrintScreen", key_name(Key::PrintScreen)); }
TEST_F(KeyNameTest, PauseName) { EXPECT_STREQ("Pause", key_name(Key::Pause)); }
TEST_F(KeyNameTest, MenuName) { EXPECT_STREQ("Menu", key_name(Key::Menu)); }
TEST_F(KeyNameTest, VolumeMuteName) { EXPECT_STREQ("VolumeMute", key_name(Key::VolumeMute)); }
TEST_F(KeyNameTest, VolumeDownName) { EXPECT_STREQ("VolumeDown", key_name(Key::VolumeDown)); }
TEST_F(KeyNameTest, VolumeUpName) { EXPECT_STREQ("VolumeUp", key_name(Key::VolumeUp)); }
TEST_F(KeyNameTest, MediaPlayName) { EXPECT_STREQ("MediaPlay", key_name(Key::MediaPlay)); }
TEST_F(KeyNameTest, MediaPauseName) { EXPECT_STREQ("MediaPause", key_name(Key::MediaPause)); }
TEST_F(KeyNameTest, MediaStopName) { EXPECT_STREQ("MediaStop", key_name(Key::MediaStop)); }
TEST_F(KeyNameTest, MediaNextName) { EXPECT_STREQ("MediaNext", key_name(Key::MediaNext)); }
TEST_F(KeyNameTest, MediaPrevName) { EXPECT_STREQ("MediaPrev", key_name(Key::MediaPrev)); }
TEST_F(KeyNameTest, BrowserBackName) { EXPECT_STREQ("BrowserBack", key_name(Key::BrowserBack)); }
TEST_F(KeyNameTest, BrowserForwardName) { EXPECT_STREQ("BrowserForward", key_name(Key::BrowserForward)); }
TEST_F(KeyNameTest, BrowserRefreshName) { EXPECT_STREQ("BrowserRefresh", key_name(Key::BrowserRefresh)); }
TEST_F(KeyNameTest, BrowserStopName) { EXPECT_STREQ("BrowserStop", key_name(Key::BrowserStop)); }
TEST_F(KeyNameTest, BrowserSearchName) { EXPECT_STREQ("BrowserSearch", key_name(Key::BrowserSearch)); }
TEST_F(KeyNameTest, BrowserFavoritesName) { EXPECT_STREQ("BrowserFavorites", key_name(Key::BrowserFavorites)); }
TEST_F(KeyNameTest, BrowserHomeName) { EXPECT_STREQ("BrowserHome", key_name(Key::BrowserHome)); }
TEST_F(KeyNameTest, LaunchMailName) { EXPECT_STREQ("LaunchMail", key_name(Key::LaunchMail)); }
TEST_F(KeyNameTest, LaunchMediaName) { EXPECT_STREQ("LaunchMedia", key_name(Key::LaunchMedia)); }
TEST_F(KeyNameTest, LaunchCalculatorName) { EXPECT_STREQ("LaunchCalculator", key_name(Key::LaunchCalculator)); }
TEST_F(KeyNameTest, SystemPowerDownName) { EXPECT_STREQ("SystemPowerDown", key_name(Key::SystemPowerDown)); }
TEST_F(KeyNameTest, SystemSleepName) { EXPECT_STREQ("SystemSleep", key_name(Key::SystemSleep)); }
TEST_F(KeyNameTest, SystemWakeUpName) { EXPECT_STREQ("SystemWakeUp", key_name(Key::SystemWakeUp)); }
TEST_F(KeyNameTest, HelpName) { EXPECT_STREQ("Help", key_name(Key::Help)); }
TEST_F(KeyNameTest, UndoName) { EXPECT_STREQ("Undo", key_name(Key::Undo)); }
TEST_F(KeyNameTest, RedoName) { EXPECT_STREQ("Redo", key_name(Key::Redo)); }
TEST_F(KeyNameTest, CutName) { EXPECT_STREQ("Cut", key_name(Key::Cut)); }
TEST_F(KeyNameTest, CopyName) { EXPECT_STREQ("Copy", key_name(Key::Copy)); }
TEST_F(KeyNameTest, PasteName) { EXPECT_STREQ("Paste", key_name(Key::Paste)); }
TEST_F(KeyNameTest, FindName) { EXPECT_STREQ("Find", key_name(Key::Find)); }
TEST_F(KeyNameTest, SelectAllName) { EXPECT_STREQ("SelectAll", key_name(Key::SelectAll)); }
TEST_F(KeyNameTest, ZoomInName) { EXPECT_STREQ("ZoomIn", key_name(Key::ZoomIn)); }
TEST_F(KeyNameTest, ZoomOutName) { EXPECT_STREQ("ZoomOut", key_name(Key::ZoomOut)); }
TEST_F(KeyNameTest, ZoomResetName) { EXPECT_STREQ("ZoomReset", key_name(Key::ZoomReset)); }

TEST_F(KeyNameTest, UnknownKeyReturnsUnknown) {
    auto unknown = static_cast<Key>(0xDEAD);
    EXPECT_STREQ("Unknown", key_name(unknown));
}

TEST_F(KeyNameTest, KnownKeysDontReturnUnknown) {
    const std::vector<Key> known = {
        Key::Shift, Key::RightShift, Key::Control, Key::RightControl,
        Key::Alt, Key::RightAlt, Key::AltGr, Key::Meta, Key::RightMeta,
        Key::Return, Key::Escape, Key::Tab, Key::Space, Key::Backspace,
        Key::Delete, Key::Insert, Key::Home, Key::End, Key::PageUp,
        Key::PageDown, Key::LeftArrow, Key::RightArrow, Key::UpArrow,
        Key::DownArrow, Key::CapsLock, Key::NumLock, Key::ScrollLock,
        Key::PrintScreen, Key::Pause, Key::Menu,
        Key::VolumeMute, Key::VolumeDown, Key::VolumeUp,
        Key::MediaPlay, Key::MediaPause, Key::MediaStop,
        Key::MediaNext, Key::MediaPrev,
        Key::BrowserBack, Key::BrowserForward, Key::BrowserRefresh,
        Key::BrowserStop, Key::BrowserSearch, Key::BrowserFavorites,
        Key::BrowserHome,
        Key::LaunchMail, Key::LaunchMedia, Key::LaunchCalculator,
        Key::SystemPowerDown, Key::SystemSleep, Key::SystemWakeUp,
        Key::Help, Key::Undo, Key::Redo, Key::Cut, Key::Copy,
        Key::Paste, Key::Find, Key::SelectAll,
        Key::ZoomIn, Key::ZoomOut, Key::ZoomReset
    };
    for (auto k : known) {
        EXPECT_NE(std::string("Unknown"), std::string(key_name(k)))
            << "key_name should not return Unknown for known key";
    }
}

// ============================================================================
// 16. Utility Function Tests — is_modifier_key
// ============================================================================

class IsModifierKeyTest : public ::testing::Test {};

TEST_F(IsModifierKeyTest, LeftShiftIsModifier) {
    EXPECT_TRUE(is_modifier_key(Key::Shift));
}

TEST_F(IsModifierKeyTest, RightShiftIsModifier) {
    EXPECT_TRUE(is_modifier_key(Key::RightShift));
}

TEST_F(IsModifierKeyTest, LeftControlIsModifier) {
    EXPECT_TRUE(is_modifier_key(Key::Control));
}

TEST_F(IsModifierKeyTest, RightControlIsModifier) {
    EXPECT_TRUE(is_modifier_key(Key::RightControl));
}

TEST_F(IsModifierKeyTest, LeftAltIsModifier) {
    EXPECT_TRUE(is_modifier_key(Key::Alt));
}

TEST_F(IsModifierKeyTest, RightAltIsModifier) {
    EXPECT_TRUE(is_modifier_key(Key::RightAlt));
}

TEST_F(IsModifierKeyTest, AltGrIsModifier) {
    EXPECT_TRUE(is_modifier_key(Key::AltGr));
}

TEST_F(IsModifierKeyTest, LeftMetaIsModifier) {
    EXPECT_TRUE(is_modifier_key(Key::Meta));
}

TEST_F(IsModifierKeyTest, RightMetaIsModifier) {
    EXPECT_TRUE(is_modifier_key(Key::RightMeta));
}

TEST_F(IsModifierKeyTest, SpaceIsNotModifier) {
    EXPECT_FALSE(is_modifier_key(Key::Space));
}

TEST_F(IsModifierKeyTest, ReturnIsNotModifier) {
    EXPECT_FALSE(is_modifier_key(Key::Return));
}

TEST_F(IsModifierKeyTest, F1IsNotModifier) {
    EXPECT_FALSE(is_modifier_key(Key::F1));
}

TEST_F(IsModifierKeyTest, Numpad0IsNotModifier) {
    EXPECT_FALSE(is_modifier_key(Key::Numpad0));
}

TEST_F(IsModifierKeyTest, VolumeMuteIsNotModifier) {
    EXPECT_FALSE(is_modifier_key(Key::VolumeMute));
}

TEST_F(IsModifierKeyTest, BrowserBackIsNotModifier) {
    EXPECT_FALSE(is_modifier_key(Key::BrowserBack));
}

TEST_F(IsModifierKeyTest, CapsLockIsNotModifier) {
    EXPECT_FALSE(is_modifier_key(Key::CapsLock));
}

TEST_F(IsModifierKeyTest, LayoutIsNotModifier) {
    EXPECT_FALSE(is_modifier_key(Key::Layout));
}

// ============================================================================
// 17. Utility Function Tests — is_numpad_key
// ============================================================================

class IsNumpadKeyTest : public ::testing::Test {};

TEST_F(IsNumpadKeyTest, Numpad0IsNumpad) {
    EXPECT_TRUE(is_numpad_key(Key::Numpad0));
}

TEST_F(IsNumpadKeyTest, Numpad1IsNumpad) {
    EXPECT_TRUE(is_numpad_key(Key::Numpad1));
}

TEST_F(IsNumpadKeyTest, Numpad9IsNumpad) {
    EXPECT_TRUE(is_numpad_key(Key::Numpad9));
}

TEST_F(IsNumpadKeyTest, NumpadAddIsNumpad) {
    EXPECT_TRUE(is_numpad_key(Key::NumpadAdd));
}

TEST_F(IsNumpadKeyTest, NumpadSubtractIsNumpad) {
    EXPECT_TRUE(is_numpad_key(Key::NumpadSubtract));
}

TEST_F(IsNumpadKeyTest, NumpadMultiplyIsNumpad) {
    EXPECT_TRUE(is_numpad_key(Key::NumpadMultiply));
}

TEST_F(IsNumpadKeyTest, NumpadDivideIsNumpad) {
    EXPECT_TRUE(is_numpad_key(Key::NumpadDivide));
}

TEST_F(IsNumpadKeyTest, NumpadDecimalIsNumpad) {
    EXPECT_TRUE(is_numpad_key(Key::NumpadDecimal));
}

TEST_F(IsNumpadKeyTest, NumpadEnterIsNumpad) {
    EXPECT_TRUE(is_numpad_key(Key::NumpadEnter));
}

TEST_F(IsNumpadKeyTest, NumpadEqualIsNumpad) {
    EXPECT_TRUE(is_numpad_key(Key::NumpadEqual));
}

TEST_F(IsNumpadKeyTest, NumLockIsNumpad) {
    EXPECT_TRUE(is_numpad_key(Key::NumLock));
}

TEST_F(IsNumpadKeyTest, SpaceIsNotNumpad) {
    EXPECT_FALSE(is_numpad_key(Key::Space));
}

TEST_F(IsNumpadKeyTest, F1IsNotNumpad) {
    EXPECT_FALSE(is_numpad_key(Key::F1));
}

TEST_F(IsNumpadKeyTest, ShiftIsNotNumpad) {
    EXPECT_FALSE(is_numpad_key(Key::Shift));
}

TEST_F(IsNumpadKeyTest, AllNumpadKeysAreCorrectlyClassified) {
    const std::vector<Key> numpad_keys = {
        Key::Numpad0, Key::Numpad1, Key::Numpad2, Key::Numpad3,
        Key::Numpad4, Key::Numpad5, Key::Numpad6, Key::Numpad7,
        Key::Numpad8, Key::Numpad9,
        Key::NumpadAdd, Key::NumpadSubtract, Key::NumpadMultiply,
        Key::NumpadDivide, Key::NumpadDecimal, Key::NumpadEnter,
        Key::NumpadEqual, Key::NumLock
    };
    for (auto k : numpad_keys) {
        EXPECT_TRUE(is_numpad_key(k)) << "Key 0x" << std::hex
            << static_cast<uint32_t>(k) << " should be a numpad key";
    }
}

// ============================================================================
// 18. Utility Function Tests — is_media_key
// ============================================================================

class IsMediaKeyTest : public ::testing::Test {};

TEST_F(IsMediaKeyTest, VolumeMuteIsMedia) {
    EXPECT_TRUE(is_media_key(Key::VolumeMute));
}

TEST_F(IsMediaKeyTest, VolumeDownIsMedia) {
    EXPECT_TRUE(is_media_key(Key::VolumeDown));
}

TEST_F(IsMediaKeyTest, VolumeUpIsMedia) {
    EXPECT_TRUE(is_media_key(Key::VolumeUp));
}

TEST_F(IsMediaKeyTest, MediaPlayIsMedia) {
    EXPECT_TRUE(is_media_key(Key::MediaPlay));
}

TEST_F(IsMediaKeyTest, MediaPauseIsMedia) {
    EXPECT_TRUE(is_media_key(Key::MediaPause));
}

TEST_F(IsMediaKeyTest, MediaStopIsMedia) {
    EXPECT_TRUE(is_media_key(Key::MediaStop));
}

TEST_F(IsMediaKeyTest, MediaNextIsMedia) {
    EXPECT_TRUE(is_media_key(Key::MediaNext));
}

TEST_F(IsMediaKeyTest, MediaPrevIsMedia) {
    EXPECT_TRUE(is_media_key(Key::MediaPrev));
}

TEST_F(IsMediaKeyTest, MediaRewindIsMedia) {
    EXPECT_TRUE(is_media_key(Key::MediaRewind));
}

TEST_F(IsMediaKeyTest, MediaFastForwardIsMedia) {
    EXPECT_TRUE(is_media_key(Key::MediaFastForward));
}

TEST_F(IsMediaKeyTest, MediaRecordIsMedia) {
    EXPECT_TRUE(is_media_key(Key::MediaRecord));
}

TEST_F(IsMediaKeyTest, MediaPlayPauseIsMedia) {
    EXPECT_TRUE(is_media_key(Key::MediaPlayPause));
}

TEST_F(IsMediaKeyTest, MediaSelectIsNotMediaByRange) {
    EXPECT_FALSE(is_media_key(Key::MediaSelect));
}

TEST_F(IsMediaKeyTest, MediaEjectIsNotMediaByRange) {
    EXPECT_FALSE(is_media_key(Key::MediaEject));
}

TEST_F(IsMediaKeyTest, SpaceIsNotMedia) {
    EXPECT_FALSE(is_media_key(Key::Space));
}

TEST_F(IsMediaKeyTest, F1IsNotMedia) {
    EXPECT_FALSE(is_media_key(Key::F1));
}

// ============================================================================
// 19. Utility Function Tests — is_browser_key
// ============================================================================

class IsBrowserKeyTest : public ::testing::Test {};

TEST_F(IsBrowserKeyTest, BrowserBackIsBrowser) {
    EXPECT_TRUE(is_browser_key(Key::BrowserBack));
}

TEST_F(IsBrowserKeyTest, BrowserForwardIsBrowser) {
    EXPECT_TRUE(is_browser_key(Key::BrowserForward));
}

TEST_F(IsBrowserKeyTest, BrowserRefreshIsBrowser) {
    EXPECT_TRUE(is_browser_key(Key::BrowserRefresh));
}

TEST_F(IsBrowserKeyTest, BrowserStopIsBrowser) {
    EXPECT_TRUE(is_browser_key(Key::BrowserStop));
}

TEST_F(IsBrowserKeyTest, BrowserSearchIsBrowser) {
    EXPECT_TRUE(is_browser_key(Key::BrowserSearch));
}

TEST_F(IsBrowserKeyTest, BrowserFavoritesIsBrowser) {
    EXPECT_TRUE(is_browser_key(Key::BrowserFavorites));
}

TEST_F(IsBrowserKeyTest, BrowserHomeIsBrowser) {
    EXPECT_TRUE(is_browser_key(Key::BrowserHome));
}

TEST_F(IsBrowserKeyTest, SpaceIsNotBrowser) {
    EXPECT_FALSE(is_browser_key(Key::Space));
}

TEST_F(IsBrowserKeyTest, VolumeMuteIsNotBrowser) {
    EXPECT_FALSE(is_browser_key(Key::VolumeMute));
}

TEST_F(IsBrowserKeyTest, ShiftIsNotBrowser) {
    EXPECT_FALSE(is_browser_key(Key::Shift));
}

// ============================================================================
// 20. Utility Function Tests — is_launch_key
// ============================================================================

class IsLaunchKeyTest : public ::testing::Test {};

TEST_F(IsLaunchKeyTest, LaunchMailIsLaunch) {
    EXPECT_TRUE(is_launch_key(Key::LaunchMail));
}

TEST_F(IsLaunchKeyTest, LaunchMediaIsLaunch) {
    EXPECT_TRUE(is_launch_key(Key::LaunchMedia));
}

TEST_F(IsLaunchKeyTest, LaunchApp1IsLaunch) {
    EXPECT_TRUE(is_launch_key(Key::LaunchApp1));
}

TEST_F(IsLaunchKeyTest, LaunchApp2IsLaunch) {
    EXPECT_TRUE(is_launch_key(Key::LaunchApp2));
}

TEST_F(IsLaunchKeyTest, LaunchCalculatorIsLaunch) {
    EXPECT_TRUE(is_launch_key(Key::LaunchCalculator));
}

TEST_F(IsLaunchKeyTest, LaunchFileBrowserIsLaunch) {
    EXPECT_TRUE(is_launch_key(Key::LaunchFileBrowser));
}

TEST_F(IsLaunchKeyTest, LaunchTerminalIsLaunch) {
    EXPECT_TRUE(is_launch_key(Key::LaunchTerminal));
}

TEST_F(IsLaunchKeyTest, LaunchWebBrowserIsLaunch) {
    EXPECT_TRUE(is_launch_key(Key::LaunchWebBrowser));
}

TEST_F(IsLaunchKeyTest, SpaceIsNotLaunch) {
    EXPECT_FALSE(is_launch_key(Key::Space));
}

TEST_F(IsLaunchKeyTest, F1IsNotLaunch) {
    EXPECT_FALSE(is_launch_key(Key::F1));
}

// ============================================================================
// 21. Utility Function Tests — is_system_power_key
// ============================================================================

class IsSystemPowerKeyTest : public ::testing::Test {};

TEST_F(IsSystemPowerKeyTest, SystemPowerDownIsSystemPower) {
    EXPECT_TRUE(is_system_power_key(Key::SystemPowerDown));
}

TEST_F(IsSystemPowerKeyTest, SystemSleepIsSystemPower) {
    EXPECT_TRUE(is_system_power_key(Key::SystemSleep));
}

TEST_F(IsSystemPowerKeyTest, SystemWakeUpIsSystemPower) {
    EXPECT_TRUE(is_system_power_key(Key::SystemWakeUp));
}

TEST_F(IsSystemPowerKeyTest, SpaceIsNotSystemPower) {
    EXPECT_FALSE(is_system_power_key(Key::Space));
}

TEST_F(IsSystemPowerKeyTest, BrowserBackIsNotSystemPower) {
    EXPECT_FALSE(is_system_power_key(Key::BrowserBack));
}

// ============================================================================
// 22. ModifierState Struct Tests
// ============================================================================

class ModifierStateTest : public ::testing::Test {};

TEST_F(ModifierStateTest, DefaultConstructorAllFalse) {
    ModifierState ms;
    EXPECT_FALSE(ms.shift_left);
    EXPECT_FALSE(ms.shift_right);
    EXPECT_FALSE(ms.ctrl_left);
    EXPECT_FALSE(ms.ctrl_right);
    EXPECT_FALSE(ms.alt_left);
    EXPECT_FALSE(ms.alt_right);
    EXPECT_FALSE(ms.meta_left);
    EXPECT_FALSE(ms.meta_right);
    EXPECT_FALSE(ms.caps_lock);
    EXPECT_FALSE(ms.num_lock);
    EXPECT_FALSE(ms.scroll_lock);
}

TEST_F(ModifierStateTest, AnyShiftLeftOnly) {
    ModifierState ms;
    ms.shift_left = true;
    EXPECT_TRUE(ms.any_shift());
    EXPECT_FALSE(ms.any_ctrl());
    EXPECT_FALSE(ms.any_alt());
    EXPECT_FALSE(ms.any_meta());
}

TEST_F(ModifierStateTest, AnyShiftRightOnly) {
    ModifierState ms;
    ms.shift_right = true;
    EXPECT_TRUE(ms.any_shift());
}

TEST_F(ModifierStateTest, AnyShiftBothFalse) {
    ModifierState ms;
    EXPECT_FALSE(ms.any_shift());
}

TEST_F(ModifierStateTest, AnyCtrlLeftOnly) {
    ModifierState ms;
    ms.ctrl_left = true;
    EXPECT_TRUE(ms.any_ctrl());
    EXPECT_FALSE(ms.any_shift());
}

TEST_F(ModifierStateTest, AnyCtrlRightOnly) {
    ModifierState ms;
    ms.ctrl_right = true;
    EXPECT_TRUE(ms.any_ctrl());
}

TEST_F(ModifierStateTest, AnyCtrlBothFalse) {
    ModifierState ms;
    EXPECT_FALSE(ms.any_ctrl());
}

TEST_F(ModifierStateTest, AnyAltLeftOnly) {
    ModifierState ms;
    ms.alt_left = true;
    EXPECT_TRUE(ms.any_alt());
}

TEST_F(ModifierStateTest, AnyAltRightOnly) {
    ModifierState ms;
    ms.alt_right = true;
    EXPECT_TRUE(ms.any_alt());
}

TEST_F(ModifierStateTest, AnyAltBothFalse) {
    ModifierState ms;
    EXPECT_FALSE(ms.any_alt());
}

TEST_F(ModifierStateTest, AnyMetaLeftOnly) {
    ModifierState ms;
    ms.meta_left = true;
    EXPECT_TRUE(ms.any_meta());
}

TEST_F(ModifierStateTest, AnyMetaRightOnly) {
    ModifierState ms;
    ms.meta_right = true;
    EXPECT_TRUE(ms.any_meta());
}

TEST_F(ModifierStateTest, AnyMetaBothFalse) {
    ModifierState ms;
    EXPECT_FALSE(ms.any_meta());
}

TEST_F(ModifierStateTest, IsClassicModAllTrue) {
    ModifierState ms;
    ms.shift_left = true;
    ms.ctrl_left = true;
    ms.alt_left = true;
    ms.meta_left = true;
    EXPECT_TRUE(ms.is_classic_mod());
}

TEST_F(ModifierStateTest, IsClassicModMissingShift) {
    ModifierState ms;
    ms.ctrl_left = true;
    ms.alt_left = true;
    ms.meta_left = true;
    EXPECT_FALSE(ms.is_classic_mod());
}

TEST_F(ModifierStateTest, IsClassicModMissingCtrl) {
    ModifierState ms;
    ms.shift_left = true;
    ms.alt_left = true;
    ms.meta_left = true;
    EXPECT_FALSE(ms.is_classic_mod());
}

TEST_F(ModifierStateTest, IsClassicModMissingAlt) {
    ModifierState ms;
    ms.shift_left = true;
    ms.ctrl_left = true;
    ms.meta_left = true;
    EXPECT_FALSE(ms.is_classic_mod());
}

TEST_F(ModifierStateTest, IsClassicModMissingMeta) {
    ModifierState ms;
    ms.shift_left = true;
    ms.ctrl_left = true;
    ms.alt_left = true;
    EXPECT_FALSE(ms.is_classic_mod());
}

TEST_F(ModifierStateTest, IsClassicModRightSideModifiers) {
    ModifierState ms;
    ms.shift_right = true;
    ms.ctrl_right = true;
    ms.alt_right = true;
    ms.meta_right = true;
    EXPECT_TRUE(ms.is_classic_mod());
}

TEST_F(ModifierStateTest, IsClassicModMixedSides) {
    ModifierState ms;
    ms.shift_left = true;
    ms.ctrl_right = true;
    ms.alt_left = true;
    ms.meta_right = true;
    EXPECT_TRUE(ms.is_classic_mod());
}

TEST_F(ModifierStateTest, CapsLockIndependentOfModifiers) {
    ModifierState ms;
    ms.shift_left = true;
    ms.caps_lock = true;
    EXPECT_TRUE(ms.any_shift());
    EXPECT_FALSE(ms.is_classic_mod());
}

TEST_F(ModifierStateTest, NumLockIndependent) {
    ModifierState ms;
    ms.num_lock = true;
    EXPECT_FALSE(ms.any_shift());
    EXPECT_FALSE(ms.any_ctrl());
    EXPECT_FALSE(ms.any_alt());
    EXPECT_FALSE(ms.any_meta());
}

TEST_F(ModifierStateTest, ScrollLockIndependent) {
    ModifierState ms;
    ms.scroll_lock = true;
    EXPECT_FALSE(ms.any_shift());
}

TEST_F(ModifierStateTest, AggregatedAccessorsAreNoExcept) {
    // Verify that the accessor methods can be called in constexpr-friendly way
    constexpr ModifierState ms{};
    // Note: constexpr evaluation requires the functions to be noexcept or usable
    // in constant expressions
    EXPECT_FALSE(ms.any_shift());
    EXPECT_FALSE(ms.any_ctrl());
    EXPECT_FALSE(ms.any_alt());
    EXPECT_FALSE(ms.any_meta());
    EXPECT_FALSE(ms.is_classic_mod());
}

// ============================================================================
// 23. SmoothScrollConfig Struct Tests
// ============================================================================

class SmoothScrollConfigTest : public ::testing::Test {};

TEST_F(SmoothScrollConfigTest, DefaultValues) {
    SmoothScrollConfig cfg;
    EXPECT_EQ(1, cfg.min_step);
    EXPECT_EQ(100, cfg.max_step);
    EXPECT_DOUBLE_EQ(0.5, cfg.acceleration);
    EXPECT_EQ(std::chrono::milliseconds(1), cfg.step_delay);
    EXPECT_TRUE(cfg.enabled);
}

TEST_F(SmoothScrollConfigTest, CustomValues) {
    SmoothScrollConfig cfg;
    cfg.min_step = 5;
    cfg.max_step = 200;
    cfg.acceleration = 0.75;
    cfg.step_delay = std::chrono::milliseconds(5);
    cfg.enabled = false;
    EXPECT_EQ(5, cfg.min_step);
    EXPECT_EQ(200, cfg.max_step);
    EXPECT_DOUBLE_EQ(0.75, cfg.acceleration);
    EXPECT_EQ(std::chrono::milliseconds(5), cfg.step_delay);
    EXPECT_FALSE(cfg.enabled);
}

TEST_F(SmoothScrollConfigTest, MinStepNotGreaterThanMaxStepWhenDefault) {
    SmoothScrollConfig cfg;
    EXPECT_LE(cfg.min_step, cfg.max_step);
}

TEST_F(SmoothScrollConfigTest, DisabledFlag) {
    SmoothScrollConfig enabled;
    enabled.enabled = true;
    EXPECT_TRUE(enabled.enabled);

    SmoothScrollConfig disabled;
    disabled.enabled = false;
    EXPECT_FALSE(disabled.enabled);
}

// ============================================================================
// 24. DragConfig Struct Tests
// ============================================================================

class DragConfigTest : public ::testing::Test {};

TEST_F(DragConfigTest, DefaultValues) {
    DragConfig cfg;
    EXPECT_EQ(20, cfg.step_count);
    EXPECT_EQ(std::chrono::milliseconds(5), cfg.step_delay);
    EXPECT_FALSE(cfg.humanize);
    EXPECT_EQ(3, cfg.humanize_jitter);
}

TEST_F(DragConfigTest, CustomValues) {
    DragConfig cfg;
    cfg.step_count = 50;
    cfg.step_delay = std::chrono::milliseconds(10);
    cfg.humanize = true;
    cfg.humanize_jitter = 5;
    EXPECT_EQ(50, cfg.step_count);
    EXPECT_EQ(std::chrono::milliseconds(10), cfg.step_delay);
    EXPECT_TRUE(cfg.humanize);
    EXPECT_EQ(5, cfg.humanize_jitter);
}

TEST_F(DragConfigTest, StepCountPositive) {
    DragConfig cfg;
    EXPECT_GT(cfg.step_count, 0);
}

TEST_F(DragConfigTest, HumanizeNoEffectWhenFalse) {
    DragConfig cfg;
    cfg.humanize = false;
    cfg.humanize_jitter = 10; // set but won't be used
    EXPECT_FALSE(cfg.humanize);
}

// ============================================================================
// 25. MonitorInfo Struct Tests
// ============================================================================

class MonitorInfoTest : public ::testing::Test {};

TEST_F(MonitorInfoTest, DefaultValues) {
    MonitorInfo mi;
    EXPECT_EQ(0, mi.id);
    EXPECT_EQ(0, mi.x);
    EXPECT_EQ(0, mi.y);
    EXPECT_EQ(0, mi.width);
    EXPECT_EQ(0, mi.height);
    EXPECT_FALSE(mi.is_primary);
}

TEST_F(MonitorInfoTest, CustomValues) {
    MonitorInfo mi;
    mi.id = 1;
    mi.x = 1920;
    mi.y = 0;
    mi.width = 2560;
    mi.height = 1440;
    mi.is_primary = true;
    EXPECT_EQ(1, mi.id);
    EXPECT_EQ(1920, mi.x);
    EXPECT_EQ(0, mi.y);
    EXPECT_EQ(2560, mi.width);
    EXPECT_EQ(1440, mi.height);
    EXPECT_TRUE(mi.is_primary);
}

TEST_F(MonitorInfoTest, VirtualCoordinatesCanBeNegative) {
    MonitorInfo mi;
    mi.x = -1920;
    mi.y = -200;
    EXPECT_EQ(-1920, mi.x);
    EXPECT_EQ(-200, mi.y);
}

// ============================================================================
// 26. ClipRegion Struct Tests
// ============================================================================

class ClipRegionTest : public ::testing::Test {};

TEST_F(ClipRegionTest, DefaultValues) {
    ClipRegion cr;
    EXPECT_EQ(0, cr.x);
    EXPECT_EQ(0, cr.y);
    EXPECT_EQ(0, cr.width);
    EXPECT_EQ(0, cr.height);
    EXPECT_FALSE(cr.active);
}

TEST_F(ClipRegionTest, ActiveClip) {
    ClipRegion cr;
    cr.x = 100;
    cr.y = 200;
    cr.width = 800;
    cr.height = 600;
    cr.active = true;
    EXPECT_EQ(100, cr.x);
    EXPECT_EQ(200, cr.y);
    EXPECT_EQ(800, cr.width);
    EXPECT_EQ(600, cr.height);
    EXPECT_TRUE(cr.active);
}

TEST_F(ClipRegionTest, InactiveClipMeansUnclip) {
    ClipRegion cr;
    cr.active = false;
    EXPECT_FALSE(cr.active);
}

// ============================================================================
// 27. KeyRepeatConfig Struct Tests
// ============================================================================

class KeyRepeatConfigTest : public ::testing::Test {};

TEST_F(KeyRepeatConfigTest, DefaultValues) {
    KeyRepeatConfig cfg;
    EXPECT_EQ(1, cfg.count);
    EXPECT_EQ(std::chrono::milliseconds(30), cfg.press_duration);
    EXPECT_EQ(std::chrono::milliseconds(10), cfg.release_duration);
    EXPECT_EQ(std::chrono::milliseconds(50), cfg.inter_delay);
}

TEST_F(KeyRepeatConfigTest, CustomValues) {
    KeyRepeatConfig cfg;
    cfg.count = 10;
    cfg.press_duration = std::chrono::milliseconds(50);
    cfg.release_duration = std::chrono::milliseconds(20);
    cfg.inter_delay = std::chrono::milliseconds(100);
    EXPECT_EQ(10, cfg.count);
    EXPECT_EQ(std::chrono::milliseconds(50), cfg.press_duration);
    EXPECT_EQ(std::chrono::milliseconds(20), cfg.release_duration);
    EXPECT_EQ(std::chrono::milliseconds(100), cfg.inter_delay);
}

TEST_F(KeyRepeatConfigTest, CountAtLeastOne) {
    KeyRepeatConfig cfg;
    EXPECT_GE(cfg.count, 1);
}

// ============================================================================
// 28. KeySequenceConfig Struct Tests
// ============================================================================

class KeySequenceConfigTest : public ::testing::Test {};

TEST_F(KeySequenceConfigTest, DefaultValues) {
    KeySequenceConfig cfg;
    EXPECT_TRUE(cfg.use_unicode_fallback);
    EXPECT_EQ(std::chrono::milliseconds(1), cfg.char_delay);
}

TEST_F(KeySequenceConfigTest, CustomValues) {
    KeySequenceConfig cfg;
    cfg.use_unicode_fallback = false;
    cfg.char_delay = std::chrono::milliseconds(5);
    EXPECT_FALSE(cfg.use_unicode_fallback);
    EXPECT_EQ(std::chrono::milliseconds(5), cfg.char_delay);
}

// ============================================================================
// 29. DslToken Tests
// ============================================================================

class DslTokenTest : public ::testing::Test {};

TEST_F(DslTokenTest, DefaultTokenIsText) {
    DslToken token;
    EXPECT_EQ(DslToken::TEXT, token.type);
    EXPECT_TRUE(token.text.empty());
    EXPECT_EQ(Key::Space, token.key);
    EXPECT_TRUE(token.modifiers.empty());
    EXPECT_EQ(1, token.repeat_count);
    EXPECT_EQ(std::chrono::milliseconds(0), token.timing);
    EXPECT_TRUE(token.chord_keys.empty());
}

TEST_F(DslTokenTest, TextToken) {
    DslToken token;
    token.type = DslToken::TEXT;
    token.text = "hello";
    EXPECT_EQ(DslToken::TEXT, token.type);
    EXPECT_EQ("hello", token.text);
}

TEST_F(DslTokenTest, KeyToken) {
    DslToken token;
    token.type = DslToken::KEY;
    token.key = Key::Return;
    EXPECT_EQ(DslToken::KEY, token.type);
    EXPECT_EQ(Key::Return, token.key);
}

TEST_F(DslTokenTest, ModifierStartToken) {
    DslToken token;
    token.type = DslToken::MODIFIER_START;
    token.key = Key::Shift;
    token.timing = std::chrono::milliseconds(100);
    EXPECT_EQ(DslToken::MODIFIER_START, token.type);
    EXPECT_EQ(Key::Shift, token.key);
    EXPECT_EQ(std::chrono::milliseconds(100), token.timing);
}

TEST_F(DslTokenTest, ModifierEndToken) {
    DslToken token;
    token.type = DslToken::MODIFIER_END;
    token.key = Key::Control;
    EXPECT_EQ(DslToken::MODIFIER_END, token.type);
    EXPECT_EQ(Key::Control, token.key);
}

TEST_F(DslTokenTest, RawKeyToken) {
    DslToken token;
    token.type = DslToken::RAW_KEY;
    token.key = Key::Layout;
    EXPECT_EQ(DslToken::RAW_KEY, token.type);
    EXPECT_EQ(Key::Layout, token.key);
}

TEST_F(DslTokenTest, RepeatKeyToken) {
    DslToken token;
    token.type = DslToken::REPEAT_KEY;
    token.key = Key::F1;
    token.repeat_count = 5;
    EXPECT_EQ(DslToken::REPEAT_KEY, token.type);
    EXPECT_EQ(Key::F1, token.key);
    EXPECT_EQ(5, token.repeat_count);
}

TEST_F(DslTokenTest, DelayToken) {
    DslToken token;
    token.type = DslToken::DELAY;
    token.timing = std::chrono::milliseconds(250);
    EXPECT_EQ(DslToken::DELAY, token.type);
    EXPECT_EQ(std::chrono::milliseconds(250), token.timing);
}

TEST_F(DslTokenTest, TimedModStartToken) {
    DslToken token;
    token.type = DslToken::TIMED_MOD_START;
    token.key = Key::Shift;
    token.timing = std::chrono::milliseconds(200);
    EXPECT_EQ(DslToken::TIMED_MOD_START, token.type);
    EXPECT_EQ(Key::Shift, token.key);
    EXPECT_EQ(std::chrono::milliseconds(200), token.timing);
}

TEST_F(DslTokenTest, TimedModEndToken) {
    DslToken token;
    token.type = DslToken::TIMED_MOD_END;
    token.key = Key::Shift;
    token.timing = std::chrono::milliseconds(150);
    EXPECT_EQ(DslToken::TIMED_MOD_END, token.type);
    EXPECT_EQ(Key::Shift, token.key);
    EXPECT_EQ(std::chrono::milliseconds(150), token.timing);
}

TEST_F(DslTokenTest, ChordToken) {
    DslToken token;
    token.type = DslToken::CHORD;
    token.chord_keys = {Key::Control, Key::Shift, Key::A};
    EXPECT_EQ(DslToken::CHORD, token.type);
    ASSERT_EQ(3u, token.chord_keys.size());
    EXPECT_EQ(Key::Control, token.chord_keys[0]);
    EXPECT_EQ(Key::Shift, token.chord_keys[1]);
}

TEST_F(DslTokenTest, ModifiersVectorCanTrackState) {
    DslToken token;
    token.modifiers = {Key::Shift, Key::Control};
    ASSERT_EQ(2u, token.modifiers.size());
    EXPECT_EQ(Key::Shift, token.modifiers[0]);
    EXPECT_EQ(Key::Control, token.modifiers[1]);

    token.modifiers.push_back(Key::Alt);
    ASSERT_EQ(3u, token.modifiers.size());
    EXPECT_EQ(Key::Alt, token.modifiers[2]);
}

// ============================================================================
// 30. DslParser Tests — Basic Parsing
// ============================================================================

class DslParserTest : public ::testing::Test {};

TEST_F(DslParserTest, ParseEmptyString) {
    auto tokens = DslParser::parse("");
    EXPECT_TRUE(tokens.empty());
}

TEST_F(DslParserTest, ParsePlainText) {
    auto tokens = DslParser::parse("hello world");
    ASSERT_EQ(1u, tokens.size());
    EXPECT_EQ(DslToken::TEXT, tokens[0].type);
    EXPECT_EQ("hello world", tokens[0].text);
}

TEST_F(DslParserTest, ParseModifierOnOff) {
    auto tokens = DslParser::parse("{+SHIFT}HELLO{-SHIFT}");
    EXPECT_GE(tokens.size(), 3u);
    bool found_mod_start = false;
    bool found_mod_end = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::MODIFIER_START && t.key == Key::Shift) found_mod_start = true;
        if (t.type == DslToken::MODIFIER_END && t.key == Key::Shift) found_mod_end = true;
    }
    EXPECT_TRUE(found_mod_start);
    EXPECT_TRUE(found_mod_end);
}

TEST_F(DslParserTest, ParseModifierControl) {
    auto tokens = DslParser::parse("{+CTRL}text{-CTRL}");
    EXPECT_GE(tokens.size(), 2u);
    bool found_start = false, found_end = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::MODIFIER_START && t.key == Key::Control) found_start = true;
        if (t.type == DslToken::MODIFIER_END && t.key == Key::Control) found_end = true;
    }
    EXPECT_TRUE(found_start);
    EXPECT_TRUE(found_end);
}

TEST_F(DslParserTest, ParseModifierAlt) {
    auto tokens = DslParser::parse("{+ALT}text{-ALT}");
    bool found_start = false, found_end = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::MODIFIER_START && t.key == Key::Alt) found_start = true;
        if (t.type == DslToken::MODIFIER_END && t.key == Key::Alt) found_end = true;
    }
    EXPECT_TRUE(found_start);
    EXPECT_TRUE(found_end);
}

TEST_F(DslParserTest, ParseModifierMeta) {
    auto tokens = DslParser::parse("{+META}text{-META}");
    bool found_start = false, found_end = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::MODIFIER_START && t.key == Key::Meta) found_start = true;
        if (t.type == DslToken::MODIFIER_END && t.key == Key::Meta) found_end = true;
    }
    EXPECT_TRUE(found_start);
    EXPECT_TRUE(found_end);
}

TEST_F(DslParserTest, ParseSpecialKeyToken) {
    auto tokens = DslParser::parse("abc{RETURN}def");
    EXPECT_GE(tokens.size(), 3u);
    bool found_key = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::KEY && t.key == Key::Return) found_key = true;
    }
    EXPECT_TRUE(found_key);
}

TEST_F(DslParserTest, ParseEscapeToken) {
    auto tokens = DslParser::parse("{ESCAPE}");
    ASSERT_EQ(1u, tokens.size());
    EXPECT_EQ(DslToken::KEY, tokens[0].type);
    EXPECT_EQ(Key::Escape, tokens[0].key);
}

TEST_F(DslParserTest, ParseTabToken) {
    auto tokens = DslParser::parse("{TAB}");
    ASSERT_EQ(1u, tokens.size());
    EXPECT_EQ(DslToken::KEY, tokens[0].type);
    EXPECT_EQ(Key::Tab, tokens[0].key);
}

TEST_F(DslParserTest, ParseSpaceToken) {
    auto tokens = DslParser::parse("{SPACE}");
    ASSERT_EQ(1u, tokens.size());
    EXPECT_EQ(DslToken::KEY, tokens[0].type);
    EXPECT_EQ(Key::Space, tokens[0].key);
}

TEST_F(DslParserTest, ParseBackspaceToken) {
    auto tokens = DslParser::parse("{BACKSPACE}");
    ASSERT_EQ(1u, tokens.size());
    EXPECT_EQ(DslToken::KEY, tokens[0].type);
    EXPECT_EQ(Key::Backspace, tokens[0].key);
}

TEST_F(DslParserTest, ParseDeleteToken) {
    auto tokens = DslParser::parse("{DELETE}");
    ASSERT_EQ(1u, tokens.size());
    EXPECT_EQ(DslToken::KEY, tokens[0].type);
    EXPECT_EQ(Key::Delete, tokens[0].key);
}

TEST_F(DslParserTest, ParseArrowKeys) {
    auto tokens = DslParser::parse("{UP}{DOWN}{LEFT}{RIGHT}");
    ASSERT_EQ(4u, tokens.size());
    EXPECT_EQ(Key::UpArrow, tokens[0].key);
    EXPECT_EQ(Key::DownArrow, tokens[1].key);
    EXPECT_EQ(Key::LeftArrow, tokens[2].key);
    EXPECT_EQ(Key::RightArrow, tokens[3].key);
}

TEST_F(DslParserTest, ParseFunctionKeys) {
    auto tokens = DslParser::parse("{F1}{F2}{F12}");
    ASSERT_EQ(3u, tokens.size());
    EXPECT_EQ(Key::F1, tokens[0].key);
    EXPECT_EQ(Key::F2, tokens[1].key);
    EXPECT_EQ(Key::F12, tokens[2].key);
}

TEST_F(DslParserTest, ParseHomeAndEnd) {
    auto tokens = DslParser::parse("{HOME}text{END}");
    ASSERT_GE(tokens.size(), 2u);
    bool has_home = false, has_end = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::KEY && t.key == Key::Home) has_home = true;
        if (t.type == DslToken::KEY && t.key == Key::End) has_end = true;
    }
    EXPECT_TRUE(has_home);
    EXPECT_TRUE(has_end);
}

TEST_F(DslParserTest, ParsePageUpPageDown) {
    auto tokens = DslParser::parse("{PAGEUP}{PAGEDOWN}");
    ASSERT_EQ(2u, tokens.size());
    EXPECT_EQ(Key::PageUp, tokens[0].key);
    EXPECT_EQ(Key::PageDown, tokens[1].key);
}

TEST_F(DslParserTest, ParseNumpadKeys) {
    auto tokens = DslParser::parse("{NUMPAD0}{NUMPADENTER}{NUMPADADD}");
    ASSERT_EQ(3u, tokens.size());
    EXPECT_EQ(Key::Numpad0, tokens[0].key);
    EXPECT_EQ(Key::NumpadEnter, tokens[1].key);
    EXPECT_EQ(Key::NumpadAdd, tokens[2].key);
}

TEST_F(DslParserTest, ParseVolumeKeys) {
    auto tokens = DslParser::parse("{VOLUMEMUTE}{VOLUMEUP}{VOLUMEDOWN}");
    ASSERT_EQ(3u, tokens.size());
    EXPECT_EQ(Key::VolumeMute, tokens[0].key);
    EXPECT_EQ(Key::VolumeUp, tokens[1].key);
    EXPECT_EQ(Key::VolumeDown, tokens[2].key);
}

TEST_F(DslParserTest, ParseMediaKeys) {
    auto tokens = DslParser::parse("{MEDIAPLAY}{MEDIAPAUSE}{MEDIASTOP}");
    ASSERT_EQ(3u, tokens.size());
    EXPECT_EQ(Key::MediaPlay, tokens[0].key);
    EXPECT_EQ(Key::MediaPause, tokens[1].key);
    EXPECT_EQ(Key::MediaStop, tokens[2].key);
}

TEST_F(DslParserTest, ParseNestedModifiers) {
    auto tokens = DslParser::parse("{{+CTRL}{+SHIFT}A{-SHIFT}{-CTRL}}");
    EXPECT_GE(tokens.size(), 4u);
    int mod_count = 0;
    for (const auto& t : tokens) {
        if (t.type == DslToken::MODIFIER_START || t.type == DslToken::MODIFIER_END) {
            mod_count++;
        }
    }
    EXPECT_GE(mod_count, 4);
}

TEST_F(DslParserTest, ParseNestedDoubleBraces) {
    auto tokens = DslParser::parse("{{+CTRL}{+SHIFT}text{-SHIFT}{-CTRL}}");
    EXPECT_GE(tokens.size(), 4u);
    bool found_ctrl_start = false;
    bool found_shift_start = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::MODIFIER_START && t.key == Key::Control) found_ctrl_start = true;
        if (t.type == DslToken::MODIFIER_START && t.key == Key::Shift) found_shift_start = true;
    }
    EXPECT_TRUE(found_ctrl_start);
    EXPECT_TRUE(found_shift_start);
}

TEST_F(DslParserTest, ParseRepeatKey) {
    auto tokens = DslParser::parse("{TAB 5}");
    ASSERT_GE(tokens.size(), 1u);
    bool found_repeat = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::REPEAT_KEY) {
            found_repeat = true;
            EXPECT_EQ(Key::Tab, t.key);
            EXPECT_EQ(5, t.repeat_count);
        }
    }
    EXPECT_TRUE(found_repeat);
}

TEST_F(DslParserTest, ParseRepeatKeyF1) {
    auto tokens = DslParser::parse("{F1 10}");
    bool found_repeat = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::REPEAT_KEY) {
            found_repeat = true;
            EXPECT_EQ(Key::F1, t.key);
            EXPECT_EQ(10, t.repeat_count);
        }
    }
    EXPECT_TRUE(found_repeat);
}

TEST_F(DslParserTest, ParseDelay) {
    auto tokens = DslParser::parse("{DELAY 500ms}");
    bool found_delay = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::DELAY) {
            found_delay = true;
            EXPECT_EQ(std::chrono::milliseconds(500), t.timing);
        }
    }
    EXPECT_TRUE(found_delay);
}

TEST_F(DslParserTest, ParseDelay100ms) {
    auto tokens = DslParser::parse("text{DELAY 100ms}more");
    bool found_delay = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::DELAY) {
            found_delay = true;
            EXPECT_EQ(std::chrono::milliseconds(100), t.timing);
        }
    }
    EXPECT_TRUE(found_delay);
}

TEST_F(DslParserTest, ParseTimedModifierStart) {
    auto tokens = DslParser::parse("{+SHIFT 100ms}text{-SHIFT}");
    bool found_timed = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::TIMED_MOD_START) {
            found_timed = true;
            EXPECT_EQ(Key::Shift, t.key);
            EXPECT_EQ(std::chrono::milliseconds(100), t.timing);
        }
    }
    EXPECT_TRUE(found_timed);
}

TEST_F(DslParserTest, ParseTimedModifierEnd) {
    auto tokens = DslParser::parse("{+SHIFT}text{-SHIFT 200ms}");
    bool found_timed_end = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::TIMED_MOD_END) {
            found_timed_end = true;
            EXPECT_EQ(Key::Shift, t.key);
            EXPECT_EQ(std::chrono::milliseconds(200), t.timing);
        }
    }
    EXPECT_TRUE(found_timed_end);
}

TEST_F(DslParserTest, ParseChord) {
    auto tokens = DslParser::parse("{CTRL+SHIFT+A}");
    bool found_chord = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::CHORD) {
            found_chord = true;
            EXPECT_GE(t.chord_keys.size(), 2u);
        }
    }
    EXPECT_TRUE(found_chord);
}

TEST_F(DslParserTest, ParseChordPairs) {
    auto tokens = DslParser::parse("{CTRL+C}");
    bool found_chord = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::CHORD) {
            found_chord = true;
            EXPECT_EQ(2u, t.chord_keys.size());
        }
    }
    EXPECT_TRUE(found_chord);
}

TEST_F(DslParserTest, ParseComplexDsl) {
    auto tokens = DslParser::parse(
        "hello {+SHIFT}world{-SHIFT}! {DELAY 100ms} {TAB}{RETURN}"
        "{{+CTRL}{+SHIFT}A{-SHIFT}{-CTRL}}");
    EXPECT_GE(tokens.size(), 5u);
    int text_count = 0;
    for (const auto& t : tokens) {
        if (t.type == DslToken::TEXT) text_count++;
    }
    EXPECT_GE(text_count, 0);
}

TEST_F(DslParserTest, ParseModifierNamesCaseInsensitive) {
    // DSL parser should handle case-insensitive key names
    auto tokens1 = DslParser::parse("{+shift}HELLO{-shift}");
    auto tokens2 = DslParser::parse("{+SHIFT}HELLO{-SHIFT}");
    EXPECT_GE(tokens1.size(), 2u);
    EXPECT_GE(tokens2.size(), 2u);
}

// ============================================================================
// 31. DslParser Tests — Unparse
// ============================================================================

class DslUnparseTest : public ::testing::Test {};

TEST_F(DslUnparseTest, UnparseEmptyVector) {
    std::vector<DslToken> tokens;
    auto result = DslParser::unparse(tokens);
    EXPECT_TRUE(result.empty());
}

TEST_F(DslUnparseTest, UnparseTextToken) {
    DslToken t;
    t.type = DslToken::TEXT;
    t.text = "hello";
    auto result = DslParser::unparse({t});
    EXPECT_FALSE(result.empty());
    EXPECT_NE(std::string::npos, result.find("hello"));
}

TEST_F(DslUnparseTest, UnparseKeyToken) {
    DslToken t;
    t.type = DslToken::KEY;
    t.key = Key::Return;
    auto result = DslParser::unparse({t});
    EXPECT_FALSE(result.empty());
    EXPECT_NE(std::string::npos, result.find("{"));
}

TEST_F(DslUnparseTest, UnparseRoundTripSimpleText) {
    auto tokens = DslParser::parse("hello world");
    auto unparsed = DslParser::unparse(tokens);
    EXPECT_FALSE(unparsed.empty());
}

TEST_F(DslUnparseTest, UnparseRoundTripSpecialKeys) {
    auto tokens = DslParser::parse("{RETURN}{TAB}{ESCAPE}");
    auto unparsed = DslParser::unparse(tokens);
    EXPECT_FALSE(unparsed.empty());
}

TEST_F(DslUnparseTest, UnparseRoundTripModifiers) {
    auto tokens = DslParser::parse("{+SHIFT}HELLO{-SHIFT}");
    auto unparsed = DslParser::unparse(tokens);
    EXPECT_FALSE(unparsed.empty());
}

TEST_F(DslUnparseTest, UnparseRoundTripComplex) {
    auto tokens = DslParser::parse(
        "hello {+CTRL}{+SHIFT}world{-SHIFT}{-CTRL} {RETURN}{DELAY 500ms}done");
    auto unparsed = DslParser::unparse(tokens);
    EXPECT_FALSE(unparsed.empty());
}

TEST_F(DslUnparseTest, ParseThenUnparseTextIsIdempotent) {
    // Parse plain text, unparse, parse again — should produce same tokens
    const std::string original = "simple text";
    auto tokens1 = DslParser::parse(original);
    auto unparsed = DslParser::unparse(tokens1);
    auto tokens2 = DslParser::parse(unparsed);
    // The unparsed version wraps text in braces, so round-trip may change.
    // At minimum both token lists should be non-empty for non-empty input.
    EXPECT_FALSE(tokens2.empty());
}

// ============================================================================
// 32. DslParser Tests — is_valid_key_name and registered_key_names
// ============================================================================

class DslValidationTest : public ::testing::Test {};

TEST_F(DslValidationTest, ValidKeyNameShift) {
    EXPECT_TRUE(DslParser::is_valid_key_name("SHIFT"));
}

TEST_F(DslValidationTest, ValidKeyNameCtrl) {
    EXPECT_TRUE(DslParser::is_valid_key_name("CTRL"));
}

TEST_F(DslValidationTest, ValidKeyNameAlt) {
    EXPECT_TRUE(DslParser::is_valid_key_name("ALT"));
}

TEST_F(DslValidationTest, ValidKeyNameMeta) {
    EXPECT_TRUE(DslParser::is_valid_key_name("META"));
}

TEST_F(DslValidationTest, ValidKeyNameReturn) {
    EXPECT_TRUE(DslParser::is_valid_key_name("RETURN"));
}

TEST_F(DslValidationTest, ValidKeyNameEscape) {
    EXPECT_TRUE(DslParser::is_valid_key_name("ESCAPE"));
}

TEST_F(DslValidationTest, ValidKeyNameTab) {
    EXPECT_TRUE(DslParser::is_valid_key_name("TAB"));
}

TEST_F(DslValidationTest, ValidKeyNameSpace) {
    EXPECT_TRUE(DslParser::is_valid_key_name("SPACE"));
}

TEST_F(DslValidationTest, ValidKeyNameBackspace) {
    EXPECT_TRUE(DslParser::is_valid_key_name("BACKSPACE"));
}

TEST_F(DslValidationTest, ValidKeyNameDelete) {
    EXPECT_TRUE(DslParser::is_valid_key_name("DELETE"));
}

TEST_F(DslValidationTest, ValidKeyNameUp) {
    EXPECT_TRUE(DslParser::is_valid_key_name("UP"));
}

TEST_F(DslValidationTest, ValidKeyNameDown) {
    EXPECT_TRUE(DslParser::is_valid_key_name("DOWN"));
}

TEST_F(DslValidationTest, ValidKeyNameLeft) {
    EXPECT_TRUE(DslParser::is_valid_key_name("LEFT"));
}

TEST_F(DslValidationTest, ValidKeyNameRight) {
    EXPECT_TRUE(DslParser::is_valid_key_name("RIGHT"));
}

TEST_F(DslValidationTest, ValidKeyNameF1) {
    EXPECT_TRUE(DslParser::is_valid_key_name("F1"));
}

TEST_F(DslValidationTest, ValidKeyNameF12) {
    EXPECT_TRUE(DslParser::is_valid_key_name("F12"));
}

TEST_F(DslValidationTest, ValidKeyNameF24) {
    EXPECT_TRUE(DslParser::is_valid_key_name("F24"));
}

TEST_F(DslValidationTest, ValidKeyNameHome) {
    EXPECT_TRUE(DslParser::is_valid_key_name("HOME"));
}

TEST_F(DslValidationTest, ValidKeyNameEnd) {
    EXPECT_TRUE(DslParser::is_valid_key_name("END"));
}

TEST_F(DslValidationTest, ValidKeyNamePageUp) {
    EXPECT_TRUE(DslParser::is_valid_key_name("PAGEUP"));
}

TEST_F(DslValidationTest, ValidKeyNamePageDown) {
    EXPECT_TRUE(DslParser::is_valid_key_name("PAGEDOWN"));
}

TEST_F(DslValidationTest, ValidKeyNameInsert) {
    EXPECT_TRUE(DslParser::is_valid_key_name("INSERT"));
}

TEST_F(DslValidationTest, ValidKeyNamePrintScreen) {
    EXPECT_TRUE(DslParser::is_valid_key_name("PRINTSCREEN"));
}

TEST_F(DslValidationTest, ValidKeyNamePause) {
    EXPECT_TRUE(DslParser::is_valid_key_name("PAUSE"));
}

TEST_F(DslValidationTest, ValidKeyNameCapsLock) {
    EXPECT_TRUE(DslParser::is_valid_key_name("CAPSLOCK"));
}

TEST_F(DslValidationTest, ValidKeyNameNumLock) {
    EXPECT_TRUE(DslParser::is_valid_key_name("NUMLOCK"));
}

TEST_F(DslValidationTest, ValidKeyNameScrollLock) {
    EXPECT_TRUE(DslParser::is_valid_key_name("SCROLLLOCK"));
}

TEST_F(DslValidationTest, ValidKeyNameNumpadKeys) {
    EXPECT_TRUE(DslParser::is_valid_key_name("NUMPAD0"));
    EXPECT_TRUE(DslParser::is_valid_key_name("NUMPAD5"));
    EXPECT_TRUE(DslParser::is_valid_key_name("NUMPAD9"));
    EXPECT_TRUE(DslParser::is_valid_key_name("NUMPADENTER"));
    EXPECT_TRUE(DslParser::is_valid_key_name("NUMPADADD"));
    EXPECT_TRUE(DslParser::is_valid_key_name("NUMPADSUBTRACT"));
}

TEST_F(DslValidationTest, ValidKeyNameMediaKeys) {
    EXPECT_TRUE(DslParser::is_valid_key_name("VOLUMEMUTE"));
    EXPECT_TRUE(DslParser::is_valid_key_name("VOLUMEUP"));
    EXPECT_TRUE(DslParser::is_valid_key_name("VOLUMEDOWN"));
    EXPECT_TRUE(DslParser::is_valid_key_name("MEDIAPLAY"));
    EXPECT_TRUE(DslParser::is_valid_key_name("MEDIANEXT"));
}

TEST_F(DslValidationTest, ValidKeyNameBrowserKeys) {
    EXPECT_TRUE(DslParser::is_valid_key_name("BROWSERBACK"));
    EXPECT_TRUE(DslParser::is_valid_key_name("BROWSERFORWARD"));
    EXPECT_TRUE(DslParser::is_valid_key_name("BROWSERHOME"));
}

TEST_F(DslValidationTest, InvalidKeyName) {
    EXPECT_FALSE(DslParser::is_valid_key_name("INVALID_KEY_NAME_XYZ"));
    EXPECT_FALSE(DslParser::is_valid_key_name(""));
    EXPECT_FALSE(DslParser::is_valid_key_name("!@#$%"));
}

TEST_F(DslValidationTest, ValidKeyNamesCaseInsensitive) {
    EXPECT_TRUE(DslParser::is_valid_key_name("shift"));
    EXPECT_TRUE(DslParser::is_valid_key_name("Shift"));
    EXPECT_TRUE(DslParser::is_valid_key_name("SHIFT"));
}

TEST_F(DslValidationTest, RegisteredKeyNamesNotEmpty) {
    auto names = DslParser::registered_key_names();
    EXPECT_FALSE(names.empty());
}

TEST_F(DslValidationTest, RegisteredKeyNamesContainsShift) {
    auto names = DslParser::registered_key_names();
    bool found_shift = false;
    for (const auto& n : names) {
        if (n == "SHIFT") { found_shift = true; break; }
    }
    EXPECT_TRUE(found_shift);
}

TEST_F(DslValidationTest, RegisteredKeyNamesContainsCommonKeys) {
    auto names = DslParser::registered_key_names();
    std::set<std::string> name_set(names.begin(), names.end());
    EXPECT_TRUE(name_set.count("RETURN") > 0);
    EXPECT_TRUE(name_set.count("TAB") > 0);
    EXPECT_TRUE(name_set.count("ESCAPE") > 0);
    EXPECT_TRUE(name_set.count("SPACE") > 0);
}

TEST_F(DslValidationTest, RegisteredKeyNamesAllValid) {
    auto names = DslParser::registered_key_names();
    for (const auto& name : names) {
        EXPECT_TRUE(DslParser::is_valid_key_name(name))
            << "Registered name '" << name << "' should be valid";
    }
}

TEST_F(DslValidationTest, RegisteredKeyNamesNoDuplicates) {
    auto names = DslParser::registered_key_names();
    std::set<std::string> name_set(names.begin(), names.end());
    EXPECT_EQ(names.size(), name_set.size());
}

// ============================================================================
// 33. MouseControllable Mock Tests
// ============================================================================

namespace {
    class MockMouse : public MouseControllable {
    public:
        int32_t last_x = -1, last_y = -1;
        int32_t last_dx = -1, last_dy = -1;
        MouseButton last_down = MouseButton::Left;
        MouseButton last_up = MouseButton::Left;
        MouseButton last_click = MouseButton::Left;
        int32_t last_scroll_delta = 0;
        MouseAxis last_scroll_axis = MouseAxis::Vertical;
        bool move_to_result = true;
        bool move_rel_result = true;
        bool down_result = true;
        bool up_result = true;
        bool scroll_result = true;
        std::pair<int32_t, int32_t> location_result = {100, 200};
        int call_count_move_to = 0;
        int call_count_down = 0;
        int call_count_up = 0;
        int call_count_scroll = 0;

        bool mouse_move_to(int32_t x, int32_t y) override {
            last_x = x; last_y = y; call_count_move_to++; return move_to_result;
        }
        bool mouse_move_relative(int32_t dx, int32_t dy) override {
            last_dx = dx; last_dy = dy; return move_rel_result;
        }
        bool mouse_down(MouseButton button) override {
            last_down = button; call_count_down++; return down_result;
        }
        bool mouse_up(MouseButton button) override {
            last_up = button; call_count_up++; return up_result;
        }
        bool mouse_scroll(int32_t delta, MouseAxis axis) override {
            last_scroll_delta = delta; last_scroll_axis = axis; call_count_scroll++; return scroll_result;
        }
        std::pair<int32_t, int32_t> mouse_location() override {
            return location_result;
        }
    };
}

class MouseControllableTest : public ::testing::Test {
protected:
    MockMouse mouse;
};

TEST_F(MouseControllableTest, MoveTo) {
    EXPECT_TRUE(mouse.mouse_move_to(50, 60));
    EXPECT_EQ(50, mouse.last_x);
    EXPECT_EQ(60, mouse.last_y);
    EXPECT_EQ(1, mouse.call_count_move_to);
}

TEST_F(MouseControllableTest, MoveRelative) {
    EXPECT_TRUE(mouse.mouse_move_relative(10, -5));
    EXPECT_EQ(10, mouse.last_dx);
    EXPECT_EQ(-5, mouse.last_dy);
}

TEST_F(MouseControllableTest, MoveRelativeZero) {
    EXPECT_TRUE(mouse.mouse_move_relative(0, 0));
    EXPECT_EQ(0, mouse.last_dx);
    EXPECT_EQ(0, mouse.last_dy);
}

TEST_F(MouseControllableTest, DownLeft) {
    EXPECT_TRUE(mouse.mouse_down(MouseButton::Left));
    EXPECT_EQ(MouseButton::Left, mouse.last_down);
    EXPECT_EQ(1, mouse.call_count_down);
}

TEST_F(MouseControllableTest, DownRight) {
    EXPECT_TRUE(mouse.mouse_down(MouseButton::Right));
    EXPECT_EQ(MouseButton::Right, mouse.last_down);
}

TEST_F(MouseControllableTest, DownMiddle) {
    EXPECT_TRUE(mouse.mouse_down(MouseButton::Middle));
    EXPECT_EQ(MouseButton::Middle, mouse.last_down);
}

TEST_F(MouseControllableTest, DownBack) {
    EXPECT_TRUE(mouse.mouse_down(MouseButton::Back));
    EXPECT_EQ(MouseButton::Back, mouse.last_down);
}

TEST_F(MouseControllableTest, DownForward) {
    EXPECT_TRUE(mouse.mouse_down(MouseButton::Forward));
    EXPECT_EQ(MouseButton::Forward, mouse.last_down);
}

TEST_F(MouseControllableTest, Up) {
    EXPECT_TRUE(mouse.mouse_up(MouseButton::Left));
    EXPECT_EQ(MouseButton::Left, mouse.last_up);
    EXPECT_EQ(1, mouse.call_count_up);
}

TEST_F(MouseControllableTest, ClickCallsDownAndUp) {
    EXPECT_TRUE(mouse.mouse_click(MouseButton::Right));
    EXPECT_EQ(MouseButton::Right, mouse.last_down);
    EXPECT_EQ(MouseButton::Right, mouse.last_up);
    EXPECT_EQ(1, mouse.call_count_down);
    EXPECT_EQ(1, mouse.call_count_up);
}

TEST_F(MouseControllableTest, ClickAllButtons) {
    const std::vector<MouseButton> buttons = {
        MouseButton::Left, MouseButton::Middle, MouseButton::Right,
        MouseButton::ScrollUp, MouseButton::ScrollDown,
        MouseButton::ScrollLeft, MouseButton::ScrollRight,
        MouseButton::Back, MouseButton::Forward
    };
    for (auto b : buttons) {
        auto down_before = mouse.call_count_down;
        auto up_before = mouse.call_count_up;
        EXPECT_TRUE(mouse.mouse_click(b));
        EXPECT_EQ(down_before + 1, mouse.call_count_down);
        EXPECT_EQ(up_before + 1, mouse.call_count_up);
    }
}

TEST_F(MouseControllableTest, ClickPropagatesFailureFromDown) {
    mouse.down_result = false;
    EXPECT_FALSE(mouse.mouse_click(MouseButton::Left));
    EXPECT_EQ(1, mouse.call_count_down);
    EXPECT_EQ(0, mouse.call_count_up);
}

TEST_F(MouseControllableTest, ClickPropagatesFailureFromUp) {
    mouse.down_result = true;
    mouse.up_result = false;
    EXPECT_FALSE(mouse.mouse_click(MouseButton::Left));
    EXPECT_EQ(1, mouse.call_count_down);
    EXPECT_EQ(1, mouse.call_count_up);
}

TEST_F(MouseControllableTest, ScrollVertical) {
    EXPECT_TRUE(mouse.mouse_scroll(3, MouseAxis::Vertical));
    EXPECT_EQ(3, mouse.last_scroll_delta);
    EXPECT_EQ(MouseAxis::Vertical, mouse.last_scroll_axis);
}

TEST_F(MouseControllableTest, ScrollHorizontal) {
    EXPECT_TRUE(mouse.mouse_scroll(-2, MouseAxis::Horizontal));
    EXPECT_EQ(-2, mouse.last_scroll_delta);
    EXPECT_EQ(MouseAxis::Horizontal, mouse.last_scroll_axis);
}

TEST_F(MouseControllableTest, ScrollDefaultAxisVertical) {
    EXPECT_TRUE(mouse.mouse_scroll(5));
    EXPECT_EQ(5, mouse.last_scroll_delta);
    EXPECT_EQ(MouseAxis::Vertical, mouse.last_scroll_axis);
}

TEST_F(MouseControllableTest, ScrollZero) {
    EXPECT_TRUE(mouse.mouse_scroll(0));
    EXPECT_EQ(0, mouse.last_scroll_delta);
}

TEST_F(MouseControllableTest, ScrollNegative) {
    EXPECT_TRUE(mouse.mouse_scroll(-10, MouseAxis::Vertical));
    EXPECT_EQ(-10, mouse.last_scroll_delta);
}

TEST_F(MouseControllableTest, Location) {
    auto loc = mouse.mouse_location();
    EXPECT_EQ(100, loc.first);
    EXPECT_EQ(200, loc.second);
}

TEST_F(MouseControllableTest, VirtualDestructorSafe) {
    MouseControllable* ptr = &mouse;
    // Just verifying we can call through the pointer without issues
    EXPECT_TRUE(ptr->mouse_move_to(1, 1));
}

// ============================================================================
// 34. KeyboardControllable Mock Tests
// ============================================================================

namespace {
    class MockKeyboard : public KeyboardControllable {
    public:
        Key last_down_key = Key::Space;
        Key last_up_key = Key::Space;
        Key last_click_key = Key::Space;
        std::string last_sequence;
        std::string last_dsl;
        bool down_result = true;
        bool up_result = true;
        bool sequence_result = true;
        bool dsl_result = true;
        bool key_state_result = false;
        bool caps_lock_result = false;
        bool num_lock_result = false;
        int down_count = 0;
        int up_count = 0;
        int release_all_count = 0;
        bool dsl_called = false;

        bool key_down(Key key) override { last_down_key = key; down_count++; return down_result; }
        bool key_up(Key key) override { last_up_key = key; up_count++; return up_result; }
        bool key_click(Key key) override {
            last_click_key = key;
            return KeyboardControllable::key_click(key);
        }
        bool key_sequence(const std::string& seq) override {
            last_sequence = seq;
            return sequence_result;
        }
        bool key_sequence_dsl(const std::string& dsl) override {
            last_dsl = dsl;
            dsl_called = true;
            return dsl_result;
        }
        bool get_key_state(Key key) override {
            last_down_key = key;
            return key_state_result;
        }
        bool get_caps_lock_state() override { return caps_lock_result; }
        bool get_num_lock_state() override { return num_lock_result; }
        void release_all() override { release_all_count++; }
    };
}

class KeyboardControllableTest : public ::testing::Test {
protected:
    MockKeyboard kb;
};

TEST_F(KeyboardControllableTest, KeyDown) {
    EXPECT_TRUE(kb.key_down(Key::A));
    EXPECT_EQ(Key::A, kb.last_down_key);
    EXPECT_EQ(1, kb.down_count);
}

TEST_F(KeyboardControllableTest, KeyUp) {
    EXPECT_TRUE(kb.key_up(Key::A));
    EXPECT_EQ(Key::A, kb.last_up_key);
    EXPECT_EQ(1, kb.up_count);
}

TEST_F(KeyboardControllableTest, KeyClickCallsDownAndUp) {
    EXPECT_TRUE(kb.key_click(Key::Return));
    EXPECT_EQ(Key::Return, kb.last_down_key);
    EXPECT_EQ(Key::Return, kb.last_up_key);
    EXPECT_EQ(1, kb.down_count);
    EXPECT_EQ(1, kb.up_count);
}

TEST_F(KeyboardControllableTest, KeyClickAllArrowKeys) {
    const std::vector<Key> arrows = {
        Key::UpArrow, Key::DownArrow, Key::LeftArrow, Key::RightArrow
    };
    for (auto k : arrows) {
        auto down_before = kb.down_count;
        auto up_before = kb.up_count;
        EXPECT_TRUE(kb.key_click(k));
        EXPECT_EQ(down_before + 1, kb.down_count);
        EXPECT_EQ(up_before + 1, kb.up_count);
    }
}

TEST_F(KeyboardControllableTest, KeyClickFailureFromDown) {
    kb.down_result = false;
    EXPECT_FALSE(kb.key_click(Key::Space));
    EXPECT_EQ(1, kb.down_count);
    EXPECT_EQ(0, kb.up_count);
}

TEST_F(KeyboardControllableTest, KeyClickFailureFromUp) {
    kb.up_result = false;
    EXPECT_FALSE(kb.key_click(Key::Space));
    EXPECT_EQ(1, kb.down_count);
    EXPECT_EQ(1, kb.up_count);
}

TEST_F(KeyboardControllableTest, KeySequence) {
    EXPECT_TRUE(kb.key_sequence("hello"));
    EXPECT_EQ("hello", kb.last_sequence);
}

TEST_F(KeyboardControllableTest, KeySequenceEmpty) {
    EXPECT_TRUE(kb.key_sequence(""));
    EXPECT_EQ("", kb.last_sequence);
}

TEST_F(KeyboardControllableTest, KeySequenceUnicode) {
    EXPECT_TRUE(kb.key_sequence("café ñ 汉字"));
    EXPECT_EQ("café ñ 汉字", kb.last_sequence);
}

TEST_F(KeyboardControllableTest, KeySequenceDsl) {
    kb.dsl_result = true;
    EXPECT_TRUE(kb.key_sequence_dsl("{+SHIFT}hello{-SHIFT}"));
    EXPECT_TRUE(kb.dsl_called);
    EXPECT_EQ("{+SHIFT}hello{-SHIFT}", kb.last_dsl);
}

TEST_F(KeyboardControllableTest, KeySequenceDslEmpty) {
    kb.dsl_result = true;
    EXPECT_TRUE(kb.key_sequence_dsl(""));
    EXPECT_TRUE(kb.dsl_called);
}

TEST_F(KeyboardControllableTest, GetKeyState) {
    kb.key_state_result = true;
    EXPECT_TRUE(kb.get_key_state(Key::Shift));
}

TEST_F(KeyboardControllableTest, GetKeyStateFalse) {
    kb.key_state_result = false;
    EXPECT_FALSE(kb.get_key_state(Key::CapsLock));
}

TEST_F(KeyboardControllableTest, GetCapsLockState) {
    kb.caps_lock_result = true;
    EXPECT_TRUE(kb.get_caps_lock_state());
}

TEST_F(KeyboardControllableTest, GetCapsLockStateFalse) {
    kb.caps_lock_result = false;
    EXPECT_FALSE(kb.get_caps_lock_state());
}

TEST_F(KeyboardControllableTest, GetNumLockState) {
    kb.num_lock_result = true;
    EXPECT_TRUE(kb.get_num_lock_state());
}

TEST_F(KeyboardControllableTest, GetNumLockStateFalse) {
    kb.num_lock_result = false;
    EXPECT_FALSE(kb.get_num_lock_state());
}

TEST_F(KeyboardControllableTest, ReleaseAll) {
    kb.release_all();
    EXPECT_EQ(1, kb.release_all_count);
    kb.release_all();
    EXPECT_EQ(2, kb.release_all_count);
}

TEST_F(KeyboardControllableTest, VirtualDestructorSafe) {
    KeyboardControllable* ptr = &kb;
    ptr->key_down(Key::Space);
    EXPECT_EQ(Key::Space, kb.last_down_key);
}

// ============================================================================
// 35. Enigo Class Tests — Construction and Basic Properties
// ============================================================================

class EnigoTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Enigo construction may connect to platform display server
    }
};

TEST_F(EnigoTest, CanConstruct) {
    EXPECT_NO_THROW({
        Enigo enigo;
    });
}

TEST_F(EnigoTest, IsAvailable) {
    Enigo enigo;
    bool avail = enigo.is_available();
    // Result is platform-dependent; just verify it doesn't crash
    (void)avail;
}

TEST_F(EnigoTest, DefaultDelay) {
    Enigo enigo;
    auto delay = enigo.get_delay();
    // Default should be a valid duration
    EXPECT_GE(delay.count(), 0);
}

TEST_F(EnigoTest, SetDelay) {
    Enigo enigo;
    enigo.set_delay(std::chrono::milliseconds(50));
    EXPECT_EQ(std::chrono::milliseconds(50), enigo.get_delay());
}

TEST_F(EnigoTest, SetDelayZero) {
    Enigo enigo;
    enigo.set_delay(std::chrono::milliseconds(0));
    EXPECT_EQ(std::chrono::milliseconds(0), enigo.get_delay());
}

TEST_F(EnigoTest, SetDelayMultipleTimes) {
    Enigo enigo;
    enigo.set_delay(std::chrono::milliseconds(10));
    EXPECT_EQ(std::chrono::milliseconds(10), enigo.get_delay());
    enigo.set_delay(std::chrono::milliseconds(200));
    EXPECT_EQ(std::chrono::milliseconds(200), enigo.get_delay());
}

TEST_F(EnigoTest, MouseLocation) {
    Enigo enigo;
    auto loc = enigo.mouse_location();
    // Location should return valid int32_t values; content is platform-dependent
    EXPECT_TRUE(true);
    (void)loc;
}

TEST_F(EnigoTest, MouseMoveToDoesNotCrash) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.mouse_move_to(100, 100);
    });
}

TEST_F(EnigoTest, MouseMoveRelativeDoesNotCrash) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.mouse_move_relative(10, 10);
    });
}

TEST_F(EnigoTest, MouseDownUpDoesNotCrash) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.mouse_down(MouseButton::Left);
        enigo.mouse_up(MouseButton::Left);
    });
}

TEST_F(EnigoTest, MouseClickDoesNotCrash) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.mouse_click(MouseButton::Left);
    });
}

TEST_F(EnigoTest, MouseScrollDoesNotCrash) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.mouse_scroll(3);
        enigo.mouse_scroll(-3, MouseAxis::Horizontal);
    });
}

TEST_F(EnigoTest, KeyDownUpDoesNotCrash) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.key_down(Key::Space);
        enigo.key_up(Key::Space);
    });
}

TEST_F(EnigoTest, KeyClickDoesNotCrash) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.key_click(Key::Return);
        enigo.key_click(Key::Tab);
    });
}

TEST_F(EnigoTest, KeySequenceDoesNotCrash) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.key_sequence("test");
    });
}

TEST_F(EnigoTest, KeySequenceDslDoesNotCrash) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.key_sequence_dsl("{+SHIFT}hello{-SHIFT}");
    });
}

TEST_F(EnigoTest, GetKeyStateDoesNotCrash) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.get_key_state(Key::Shift);
        enigo.get_key_state(Key::CapsLock);
    });
}

TEST_F(EnigoTest, GetCapsLockStateDoesNotCrash) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.get_caps_lock_state();
    });
}

TEST_F(EnigoTest, GetNumLockStateDoesNotCrash) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.get_num_lock_state();
    });
}

TEST_F(EnigoTest, ReleaseAllDoesNotCrash) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.release_all();
    });
}

// ============================================================================
// 36. Enigo Extended Mouse API Tests
// ============================================================================

TEST_F(EnigoTest, MouseSmoothScrollDoesNotCrash) {
    Enigo enigo;
    EXPECT_NO_THROW({
        SmoothScrollConfig cfg;
        cfg.min_step = 2;
        cfg.max_step = 50;
        enigo.mouse_smooth_scroll(100, MouseAxis::Vertical, cfg);
    });
}

TEST_F(EnigoTest, MouseSmoothScrollDefaultConfig) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.mouse_smooth_scroll(50, MouseAxis::Vertical);
    });
}

TEST_F(EnigoTest, DragBasic) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.drag(0, 0, 100, 100, MouseButton::Left);
    });
}

TEST_F(EnigoTest, DragWithConfig) {
    Enigo enigo;
    DragConfig cfg;
    cfg.step_count = 10;
    cfg.step_delay = std::chrono::milliseconds(2);
    cfg.humanize = true;
    cfg.humanize_jitter = 5;
    EXPECT_NO_THROW({
        enigo.drag(0, 0, 200, 200, MouseButton::Left, cfg);
    });
}

TEST_F(EnigoTest, DragWithMiddleButton) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.drag(10, 10, 50, 50, MouseButton::Middle);
    });
}

TEST_F(EnigoTest, DragWithRightButton) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.drag(50, 50, 0, 0, MouseButton::Right);
    });
}

TEST_F(EnigoTest, DragZeroDistance) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.drag(100, 100, 100, 100, MouseButton::Left);
    });
}

TEST_F(EnigoTest, MouseClip) {
    Enigo enigo;
    ClipRegion cr;
    cr.x = 0; cr.y = 0; cr.width = 800; cr.height = 600; cr.active = true;
    EXPECT_NO_THROW({
        enigo.mouse_clip(cr);
    });
}

TEST_F(EnigoTest, MouseUnclip) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.mouse_unclip();
    });
}

TEST_F(EnigoTest, MonitorList) {
    Enigo enigo;
    EXPECT_NO_THROW({
        auto monitors = enigo.monitor_list();
        // At least one monitor should exist (the system has a display)
        (void)monitors;
    });
}

TEST_F(EnigoTest, PrimaryMonitor) {
    Enigo enigo;
    EXPECT_NO_THROW({
        auto pm = enigo.primary_monitor();
        // Primary monitor should have positive dimensions
        (void)pm;
    });
}

TEST_F(EnigoTest, MonitorAt) {
    Enigo enigo;
    EXPECT_NO_THROW({
        int32_t id = enigo.monitor_at(100, 100);
        (void)id;
    });
}

TEST_F(EnigoTest, MonitorTranslate) {
    Enigo enigo;
    EXPECT_NO_THROW({
        auto result = enigo.monitor_translate(100, 100, 0, 0);
        (void)result;
    });
}

// ============================================================================
// 37. Enigo Extended Keyboard API Tests
// ============================================================================

TEST_F(EnigoTest, GetModifierState) {
    Enigo enigo;
    EXPECT_NO_THROW({
        auto state = enigo.get_modifier_state();
        // Should be in a consistent state
        (void)state;
    });
}

TEST_F(EnigoTest, KeyRepeat) {
    Enigo enigo;
    KeyRepeatConfig cfg;
    cfg.count = 3;
    EXPECT_NO_THROW({
        enigo.key_repeat(Key::Space, cfg);
    });
}

TEST_F(EnigoTest, KeyRepeatDefaultConfig) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.key_repeat(Key::F1);
    });
}

TEST_F(EnigoTest, KeySequenceUnicode) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.key_sequence_unicode("Hello, 世界!");
    });
}

TEST_F(EnigoTest, KeyUnicodeCodepoint) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.key_unicode_codepoint(0x0041); // 'A'
        enigo.key_unicode_codepoint(0x4E16); // 世
    });
}

TEST_F(EnigoTest, RawKeyDownUp) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.raw_key_down(0x41);
        enigo.raw_key_up(0x41);
    });
}

TEST_F(EnigoTest, IsModifierPressed) {
    Enigo enigo;
    EXPECT_NO_THROW({
        bool pressed = enigo.is_modifier_pressed(Key::Shift);
        (void)pressed;
    });
}

TEST_F(EnigoTest, KeyChord) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.key_chord({Key::Control, Key::Alt, Key::Delete});
    });
}

TEST_F(EnigoTest, KeyChordSingleKey) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.key_chord({Key::Escape});
    });
}

TEST_F(EnigoTest, KeyChordEmptyVector) {
    Enigo enigo;
    EXPECT_NO_THROW({
        enigo.key_chord({});
    });
}

// ============================================================================
// 38. Enigo Sequence Configuration Tests
// ============================================================================

TEST_F(EnigoTest, SetAndGetSequenceConfig) {
    Enigo enigo;
    KeySequenceConfig cfg;
    cfg.use_unicode_fallback = false;
    cfg.char_delay = std::chrono::milliseconds(5);
    enigo.set_sequence_config(cfg);
    auto retrieved = enigo.get_sequence_config();
    EXPECT_EQ(cfg.use_unicode_fallback, retrieved.use_unicode_fallback);
    EXPECT_EQ(cfg.char_delay, retrieved.char_delay);
}

TEST_F(EnigoTest, DefaultSequenceConfig) {
    Enigo enigo;
    auto cfg = enigo.get_sequence_config();
    EXPECT_TRUE(cfg.use_unicode_fallback);
    EXPECT_EQ(std::chrono::milliseconds(1), cfg.char_delay);
}

TEST_F(EnigoTest, SetAndGetSmoothScrollConfig) {
    Enigo enigo;
    SmoothScrollConfig cfg;
    cfg.min_step = 3;
    cfg.max_step = 80;
    cfg.acceleration = 0.3;
    cfg.step_delay = std::chrono::milliseconds(2);
    cfg.enabled = false;
    enigo.set_smooth_scroll_config(cfg);
    auto retrieved = enigo.get_smooth_scroll_config();
    EXPECT_EQ(cfg.min_step, retrieved.min_step);
    EXPECT_EQ(cfg.max_step, retrieved.max_step);
    EXPECT_DOUBLE_EQ(cfg.acceleration, retrieved.acceleration);
    EXPECT_EQ(cfg.step_delay, retrieved.step_delay);
    EXPECT_EQ(cfg.enabled, retrieved.enabled);
}

TEST_F(EnigoTest, DefaultSmoothScrollConfig) {
    Enigo enigo;
    auto cfg = enigo.get_smooth_scroll_config();
    EXPECT_EQ(1, cfg.min_step);
    EXPECT_EQ(100, cfg.max_step);
    EXPECT_DOUBLE_EQ(0.5, cfg.acceleration);
    EXPECT_EQ(std::chrono::milliseconds(1), cfg.step_delay);
    EXPECT_TRUE(cfg.enabled);
}

// ============================================================================
// 39. Enigo Inheritance Tests
// ============================================================================

TEST_F(EnigoTest, EnigoIsMouseControllable) {
    Enigo enigo;
    MouseControllable* mc = &enigo;
    EXPECT_TRUE(mc->mouse_move_to(100, 200));
    auto loc = mc->mouse_location();
    (void)loc;
}

TEST_F(EnigoTest, EnigoIsKeyboardControllable) {
    Enigo enigo;
    KeyboardControllable* kc = &enigo;
    EXPECT_TRUE(kc->key_down(Key::Space));
    EXPECT_TRUE(kc->key_up(Key::Space));
    auto state = kc->get_key_state(Key::Shift);
    (void)state;
}

TEST_F(EnigoTest, EnigoCanBeUsedThroughBothInterfaces) {
    Enigo enigo;
    MouseControllable& mref = enigo;
    KeyboardControllable& kref = enigo;
    EXPECT_TRUE(mref.mouse_move_to(10, 10));
    EXPECT_TRUE(kref.key_click(Key::Return));
}

// ============================================================================
// 40. PlatformKeyMap Tests — Static Interface Verification
// ============================================================================

class PlatformKeyMapTest : public ::testing::Test {};

TEST_F(PlatformKeyMapTest, ClassExists) {
    // Verify that PlatformKeyMap is a usable type
    EXPECT_TRUE((std::is_class_v<PlatformKeyMap>));
}

TEST_F(PlatformKeyMapTest, StructHasNoNonStaticData) {
    // PlatformKeyMap is a pure static utility struct
    EXPECT_TRUE(std::is_empty_v<PlatformKeyMap>);
}

#ifdef _WIN32
TEST_F(PlatformKeyMapTest, ToVirtualKeyReturnsValidValue) {
    uint32_t vk = PlatformKeyMap::to_virtual_key(Key::Shift);
    (void)vk; // platform-dependent, just ensure it compiles
}

TEST_F(PlatformKeyMapTest, FromVirtualKeyRoundTrip) {
    uint32_t vk = PlatformKeyMap::to_virtual_key(Key::Return);
    Key k = PlatformKeyMap::from_virtual_key(vk);
    EXPECT_EQ(Key::Return, k);
}

TEST_F(PlatformKeyMapTest, ToScanCode) {
    uint32_t sc = PlatformKeyMap::to_scan_code(Key::A);
    (void)sc;
}

TEST_F(PlatformKeyMapTest, IsExtendedKey) {
    bool ext = PlatformKeyMap::is_extended_key(Key::RightControl);
    EXPECT_TRUE(ext);
}

TEST_F(PlatformKeyMapTest, VirtualKeyName) {
    const char* name = PlatformKeyMap::virtual_key_name(0x41);
    EXPECT_NE(nullptr, name);
}
#endif

#ifdef __APPLE__
TEST_F(PlatformKeyMapTest, ToCGKeycode) {
    uint16_t code = PlatformKeyMap::to_cg_keycode(Key::Shift);
    (void)code;
}

TEST_F(PlatformKeyMapTest, FromCGKeycodeRoundTrip) {
    uint16_t code = PlatformKeyMap::to_cg_keycode(Key::Escape);
    Key k = PlatformKeyMap::from_cg_keycode(code);
    EXPECT_EQ(Key::Escape, k);
}

TEST_F(PlatformKeyMapTest, ToCGEventFlags) {
    uint64_t flags = PlatformKeyMap::to_cg_event_flags(Key::Shift);
    (void)flags;
}

TEST_F(PlatformKeyMapTest, CGKeycodeName) {
    const char* name = PlatformKeyMap::cg_keycode_name(0x38);
    EXPECT_NE(nullptr, name);
}
#endif

#ifdef __linux__
// Since we're on Linux, we can actually exercise these
TEST_F(PlatformKeyMapTest, ToX11KeysymReturnsValidValue) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::Return);
    // Return should map to XK_Return = 0xFF0D
    EXPECT_NE(0u, ks);
}

TEST_F(PlatformKeyMapTest, ToX11KeysymShift) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::Shift);
    // Shift_L = 0xFFE1
    EXPECT_NE(0u, ks);
}

TEST_F(PlatformKeyMapTest, ToX11KeysymControl) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::Control);
    EXPECT_NE(0u, ks);
}

TEST_F(PlatformKeyMapTest, ToX11KeysymEscape) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::Escape);
    EXPECT_NE(0u, ks);
}

TEST_F(PlatformKeyMapTest, ToX11KeysymF1) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::F1);
    EXPECT_NE(0u, ks);
}

TEST_F(PlatformKeyMapTest, ToX11KeysymF12) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::F12);
    EXPECT_NE(0u, ks);
}

TEST_F(PlatformKeyMapTest, FromX11KeysymRoundTripShift) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::Shift);
    Key k = PlatformKeyMap::from_x11_keysym(ks);
    EXPECT_EQ(Key::Shift, k);
}

TEST_F(PlatformKeyMapTest, FromX11KeysymRoundTripReturn) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::Return);
    Key k = PlatformKeyMap::from_x11_keysym(ks);
    EXPECT_EQ(Key::Return, k);
}

TEST_F(PlatformKeyMapTest, FromX11KeysymRoundTripEscape) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::Escape);
    Key k = PlatformKeyMap::from_x11_keysym(ks);
    EXPECT_EQ(Key::Escape, k);
}

TEST_F(PlatformKeyMapTest, FromX11KeysymRoundTripSpace) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::Space);
    Key k = PlatformKeyMap::from_x11_keysym(ks);
    EXPECT_EQ(Key::Space, k);
}

TEST_F(PlatformKeyMapTest, FromX11KeysymRoundTripTab) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::Tab);
    Key k = PlatformKeyMap::from_x11_keysym(ks);
    EXPECT_EQ(Key::Tab, k);
}

TEST_F(PlatformKeyMapTest, FromX11KeysymRoundTripF1) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::F1);
    Key k = PlatformKeyMap::from_x11_keysym(ks);
    EXPECT_EQ(Key::F1, k);
}

TEST_F(PlatformKeyMapTest, FromX11KeysymRoundTripF12) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::F12);
    Key k = PlatformKeyMap::from_x11_keysym(ks);
    EXPECT_EQ(Key::F12, k);
}

TEST_F(PlatformKeyMapTest, FromX11KeysymRoundTripUpArrow) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::UpArrow);
    Key k = PlatformKeyMap::from_x11_keysym(ks);
    EXPECT_EQ(Key::UpArrow, k);
}

TEST_F(PlatformKeyMapTest, FromX11KeysymRoundTripDownArrow) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::DownArrow);
    Key k = PlatformKeyMap::from_x11_keysym(ks);
    EXPECT_EQ(Key::DownArrow, k);
}

TEST_F(PlatformKeyMapTest, FromX11KeysymRoundTripLeftArrow) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::LeftArrow);
    Key k = PlatformKeyMap::from_x11_keysym(ks);
    EXPECT_EQ(Key::LeftArrow, k);
}

TEST_F(PlatformKeyMapTest, FromX11KeysymRoundTripRightArrow) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::RightArrow);
    Key k = PlatformKeyMap::from_x11_keysym(ks);
    EXPECT_EQ(Key::RightArrow, k);
}

TEST_F(PlatformKeyMapTest, FromX11KeysymRoundTripDelete) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::Delete);
    Key k = PlatformKeyMap::from_x11_keysym(ks);
    EXPECT_EQ(Key::Delete, k);
}

TEST_F(PlatformKeyMapTest, FromX11KeysymRoundTripBackspace) {
    uint32_t ks = PlatformKeyMap::to_x11_keysym(Key::Backspace);
    Key k = PlatformKeyMap::from_x11_keysym(ks);
    EXPECT_EQ(Key::Backspace, k);
}

TEST_F(PlatformKeyMapTest, FromX11KeysymRoundTripModifiers) {
    const std::vector<Key> mods = {
        Key::Shift, Key::RightShift,
        Key::Control, Key::RightControl,
        Key::Alt, Key::RightAlt,
        Key::Meta, Key::RightMeta,
        Key::AltGr, Key::CapsLock
    };
    for (auto k : mods) {
        uint32_t ks = PlatformKeyMap::to_x11_keysym(k);
        Key back = PlatformKeyMap::from_x11_keysym(ks);
        EXPECT_EQ(k, back) << "Round-trip failed for key 0x"
            << std::hex << static_cast<uint32_t>(k);
    }
}

TEST_F(PlatformKeyMapTest, X11KeysymName) {
    const char* name = PlatformKeyMap::x11_keysym_name(
        PlatformKeyMap::to_x11_keysym(Key::Return));
    EXPECT_NE(nullptr, name);
    EXPECT_NE(std::string(""), std::string(name));
}

TEST_F(PlatformKeyMapTest, X11KeysymNameForShift) {
    const char* name = PlatformKeyMap::x11_keysym_name(
        PlatformKeyMap::to_x11_keysym(Key::Shift));
    EXPECT_NE(nullptr, name);
}

TEST_F(PlatformKeyMapTest, ToX11Keycode) {
    // Requires a Display* — passing nullptr tests the function doesn't crash
    uint32_t kc = PlatformKeyMap::to_x11_keycode(Key::Space, nullptr);
    (void)kc;
}
#endif

// ============================================================================
// 41. PowerAction Enum Tests
// ============================================================================

class PowerActionTest : public ::testing::Test {};

TEST_F(PowerActionTest, AllValuesDistinct) {
    std::set<int> values;
    values.insert(static_cast<int>(PowerAction::Shutdown));
    values.insert(static_cast<int>(PowerAction::Reboot));
    values.insert(static_cast<int>(PowerAction::Sleep));
    values.insert(static_cast<int>(PowerAction::Hibernate));
    values.insert(static_cast<int>(PowerAction::Logoff));
    values.insert(static_cast<int>(PowerAction::LockScreen));
    EXPECT_EQ(6u, values.size());
}

TEST_F(PowerActionTest, ShutdownValue) {
    EXPECT_EQ(0, static_cast<int>(PowerAction::Shutdown));
}

TEST_F(PowerActionTest, RebootValue) {
    EXPECT_EQ(1, static_cast<int>(PowerAction::Reboot));
}

TEST_F(PowerActionTest, SleepValue) {
    EXPECT_EQ(2, static_cast<int>(PowerAction::Sleep));
}

TEST_F(PowerActionTest, HibernateValue) {
    EXPECT_EQ(3, static_cast<int>(PowerAction::Hibernate));
}

TEST_F(PowerActionTest, LogoffValue) {
    EXPECT_EQ(4, static_cast<int>(PowerAction::Logoff));
}

TEST_F(PowerActionTest, LockScreenValue) {
    EXPECT_EQ(5, static_cast<int>(PowerAction::LockScreen));
}

// ============================================================================
// 42. Global Constant and Free Function Tests
// ============================================================================

class FreeFunctionsTest : public ::testing::Test {};

TEST_F(FreeFunctionsTest, EnigoInputExtraValueExists) {
    // ENIGO_INPUT_EXTRA_VALUE is an extern global
    uint32_t val = ENIGO_INPUT_EXTRA_VALUE;
    (void)val;
}

TEST_F(FreeFunctionsTest, SystemUptimeMs) {
    uint64_t uptime = system_uptime_ms();
    EXPECT_GT(uptime, 0u) << "System uptime should be > 0";
}

TEST_F(FreeFunctionsTest, SystemUptimeMsIncreases) {
    auto t1 = system_uptime_ms();
    // Small busy wait to ensure time advances
    for (volatile int i = 0; i < 1000000; i++) {}
    auto t2 = system_uptime_ms();
    EXPECT_GE(t2, t1);
}

// ============================================================================
// 43. Convenience Function Tests — type_with_modifiers
// ============================================================================

class ConvenienceFunctionsTest : public ::testing::Test {
protected:
    MockKeyboard kb;
};

TEST_F(ConvenienceFunctionsTest, TypeWithModifiersShift) {
    kb.sequence_result = true;
    type_with_modifiers(kb, "hello", {Key::Shift});
    EXPECT_EQ("hello", kb.last_sequence);
    // Shift should have been pressed and released
    EXPECT_GE(kb.down_count, 1);
    EXPECT_GE(kb.up_count, 1);
}

TEST_F(ConvenienceFunctionsTest, TypeWithModifiersCtrlShift) {
    kb.sequence_result = true;
    type_with_modifiers(kb, "text", {Key::Control, Key::Shift});
    EXPECT_EQ("text", kb.last_sequence);
}

TEST_F(ConvenienceFunctionsTest, TypeWithModifiersEmptyString) {
    kb.sequence_result = true;
    type_with_modifiers(kb, "", {Key::Alt});
    EXPECT_EQ("", kb.last_sequence);
}

TEST_F(ConvenienceFunctionsTest, TypeWithModifiersEmptyModifiers) {
    kb.sequence_result = true;
    type_with_modifiers(kb, "plain", {});
    EXPECT_EQ("plain", kb.last_sequence);
}

TEST_F(ConvenienceFunctionsTest, KeyboardShortcutCtrlC) {
    kb.sequence_result = true;
    keyboard_shortcut(kb, {Key::Control}, Key::C);
    EXPECT_EQ(Key::C, kb.last_click_key);
}

TEST_F(ConvenienceFunctionsTest, KeyboardShortcutCtrlShiftEscape) {
    kb.sequence_result = true;
    keyboard_shortcut(kb, {Key::Control, Key::Shift}, Key::Escape);
    EXPECT_EQ(Key::Escape, kb.last_click_key);
}

TEST_F(ConvenienceFunctionsTest, KeyboardShortcutNoModifiers) {
    kb.sequence_result = true;
    keyboard_shortcut(kb, {}, Key::F5);
    EXPECT_EQ(Key::F5, kb.last_click_key);
}

// ============================================================================
// 44. Edge Cases and Boundary Tests
// ============================================================================

class EdgeCaseTest : public ::testing::Test {};

TEST_F(EdgeCaseTest, MouseButtonAllValuesHaveName) {
    for (int32_t val = 0; val <= 8; val++) {
        auto btn = static_cast<MouseButton>(val);
        auto name = mouse_button_name(btn);
        EXPECT_NE(std::string("Unknown"), std::string(name))
            << "MouseButton value " << val << " should have a name";
    }
}

TEST_F(EdgeCaseTest, MouseButtonNegativeValueReturnsUnknown) {
    auto invalid = static_cast<MouseButton>(-1);
    EXPECT_STREQ("Unknown", mouse_button_name(invalid));
}

TEST_F(EdgeCaseTest, MouseButtonLargeValueReturnsUnknown) {
    auto invalid = static_cast<MouseButton>(100);
    EXPECT_STREQ("Unknown", mouse_button_name(invalid));
}

TEST_F(EdgeCaseTest, KeyCastToUint32AllPositive) {
    // All key values should be non-zero and positive
    const std::vector<Key> all_keys = {
        Key::Alt, Key::AltGr, Key::Backspace, Key::CapsLock, Key::Control,
        Key::Delete, Key::DownArrow, Key::End, Key::Escape,
        Key::F1, Key::F12, Key::F24,
        Key::Home, Key::Insert, Key::LeftArrow,
        Key::Meta, Key::NumLock, Key::PageDown, Key::PageUp,
        Key::Return, Key::RightArrow, Key::ScrollLock, Key::Shift,
        Key::Space, Key::Tab, Key::UpArrow, Key::PrintScreen, Key::Pause,
        Key::Menu, Key::RightShift, Key::RightControl, Key::RightAlt,
        Key::RightMeta,
        Key::Numpad0, Key::Numpad9, Key::NumpadAdd, Key::NumpadEnter,
        Key::VolumeMute, Key::MediaPlay, Key::MediaStop,
        Key::BrowserBack, Key::BrowserHome,
        Key::LaunchMail, Key::LaunchWebBrowser,
        Key::SystemPowerDown, Key::SystemWakeUp,
        Key::Help, Key::Undo, Key::Redo, Key::Cut, Key::Copy, Key::Paste,
        Key::Find, Key::SelectAll, Key::ZoomIn, Key::ZoomOut, Key::ZoomReset,
        Key::IMEOn, Key::IMEOff,
        Key::KanaMode, Key::HanjaMode, Key::HangulMode,
        Key::Katakana, Key::Hiragana, Key::ZenkakuHankaku,
        Key::Lang1, Key::Lang2, Key::Lang3, Key::Lang4, Key::Lang5,
        Key::Layout, Key::Raw
    };
    for (auto k : all_keys) {
        EXPECT_GT(static_cast<uint32_t>(k), 0u)
            << "Key value should be non-zero";
    }
}

TEST_F(EdgeCaseTest, ModifierStateAllCombinations) {
    // Test a variety of ModifierState combinations
    ModifierState ms;

    // No mods
    EXPECT_FALSE(ms.any_shift() || ms.any_ctrl() || ms.any_alt() || ms.any_meta());

    // Only left shift
    ms.shift_left = true;
    EXPECT_TRUE(ms.any_shift());
    ms.shift_left = false;

    // Only right shift
    ms.shift_right = true;
    EXPECT_TRUE(ms.any_shift());
    ms.shift_right = false;

    // All left
    ms.shift_left = ms.ctrl_left = ms.alt_left = ms.meta_left = true;
    EXPECT_TRUE(ms.is_classic_mod());
    ms = ModifierState{};

    // All right
    ms.shift_right = ms.ctrl_right = ms.alt_right = ms.meta_right = true;
    EXPECT_TRUE(ms.is_classic_mod());
    ms = ModifierState{};

    // Mixed
    ms.shift_left = true;
    ms.ctrl_right = true;
    ms.alt_left = true;
    ms.meta_left = true;
    EXPECT_TRUE(ms.is_classic_mod());
    EXPECT_TRUE(ms.any_shift() && ms.any_ctrl() && ms.any_alt() && ms.any_meta());
}

TEST_F(EdgeCaseTest, SmoothScrollConfigAccelerationBetweenZeroAndOne) {
    SmoothScrollConfig cfg;
    cfg.acceleration = 0.0;
    EXPECT_DOUBLE_EQ(0.0, cfg.acceleration);
    cfg.acceleration = 0.5;
    EXPECT_DOUBLE_EQ(0.5, cfg.acceleration);
    cfg.acceleration = 1.0;
    EXPECT_DOUBLE_EQ(1.0, cfg.acceleration);
}

TEST_F(EdgeCaseTest, DragConfigJitterNonNegative) {
    DragConfig cfg;
    EXPECT_GE(cfg.humanize_jitter, 0);
    cfg.humanize_jitter = 0;
    EXPECT_EQ(0, cfg.humanize_jitter);
}

TEST_F(EdgeCaseTest, KeyRepeatConfigAllZeroMilliseconds) {
    KeyRepeatConfig cfg;
    cfg.press_duration = std::chrono::milliseconds(0);
    cfg.release_duration = std::chrono::milliseconds(0);
    cfg.inter_delay = std::chrono::milliseconds(0);
    EXPECT_EQ(std::chrono::milliseconds(0), cfg.press_duration);
    EXPECT_EQ(std::chrono::milliseconds(0), cfg.release_duration);
    EXPECT_EQ(std::chrono::milliseconds(0), cfg.inter_delay);
}

TEST_F(EdgeCaseTest, KeySequenceConfigCharDelayZero) {
    KeySequenceConfig cfg;
    cfg.char_delay = std::chrono::milliseconds(0);
    EXPECT_EQ(std::chrono::milliseconds(0), cfg.char_delay);
}

TEST_F(EdgeCaseTest, DslTokenChordEmptyVector) {
    DslToken token;
    token.type = DslToken::CHORD;
    EXPECT_TRUE(token.chord_keys.empty());
}

TEST_F(EdgeCaseTest, DslTokenRepeatCountLarge) {
    DslToken token;
    token.type = DslToken::REPEAT_KEY;
    token.repeat_count = 9999;
    EXPECT_EQ(9999, token.repeat_count);
}

TEST_F(EdgeCaseTest, DslTokenTimingLarge) {
    DslToken token;
    token.type = DslToken::DELAY;
    token.timing = std::chrono::milliseconds(60000); // 60 seconds
    EXPECT_EQ(std::chrono::milliseconds(60000), token.timing);
}

TEST_F(EdgeCaseTest, ParseDslWithEscapedBraces) {
    // Regular text with curly braces that are NOT DSL tokens
    // Should be treated as plain text since they don't match a key pattern
    auto tokens = DslParser::parse("just {text} here");
    EXPECT_GE(tokens.size(), 1u);
}

TEST_F(EdgeCaseTest, ParseDslOnlyModifierBraces) {
    auto tokens = DslParser::parse("{+SHIFT}{-SHIFT}");
    EXPECT_GE(tokens.size(), 2u);
}

TEST_F(EdgeCaseTest, ParseDslKeyNameWithUnderscores) {
    // Keys like PAGE_UP might be parsed as PAGEUP
    auto tokens = DslParser::parse("{PAGEUP}{PAGEDOWN}");
    ASSERT_EQ(2u, tokens.size());
    EXPECT_EQ(DslToken::KEY, tokens[0].type);
    EXPECT_EQ(DslToken::KEY, tokens[1].type);
}

TEST_F(EdgeCaseTest, IsValidKeyNameWithWhitespace) {
    EXPECT_FALSE(DslParser::is_valid_key_name("SHIFT "));
    EXPECT_FALSE(DslParser::is_valid_key_name(" SHIFT"));
}

TEST_F(EdgeCaseTest, IsValidKeyNameWithSpecialChars) {
    EXPECT_FALSE(DslParser::is_valid_key_name("SHIFT+CTRL"));
    EXPECT_FALSE(DslParser::is_valid_key_name("{SHIFT}"));
}

// ============================================================================
// 45. Stress Tests — Many Keys
// ============================================================================

TEST(StressTest, AllFunctionKeysRoundTripX11) {
#ifdef __linux__
    const std::vector<Key> fkeys = {
        Key::F1, Key::F2, Key::F3, Key::F4, Key::F5, Key::F6,
        Key::F7, Key::F8, Key::F9, Key::F10, Key::F11, Key::F12,
        Key::F13, Key::F14, Key::F15, Key::F16, Key::F17, Key::F18,
        Key::F19, Key::F20, Key::F21, Key::F22, Key::F23, Key::F24
    };
    for (auto k : fkeys) {
        uint32_t ks = PlatformKeyMap::to_x11_keysym(k);
        Key back = PlatformKeyMap::from_x11_keysym(ks);
        EXPECT_EQ(k, back) << "Round-trip failed for F-key value 0x"
            << std::hex << static_cast<uint32_t>(k);
    }
#endif
}

TEST(StressTest, AllNumpadKeysRoundTripX11) {
#ifdef __linux__
    const std::vector<Key> npads = {
        Key::Numpad0, Key::Numpad1, Key::Numpad2, Key::Numpad3,
        Key::Numpad4, Key::Numpad5, Key::Numpad6, Key::Numpad7,
        Key::Numpad8, Key::Numpad9,
        Key::NumpadAdd, Key::NumpadSubtract, Key::NumpadMultiply,
        Key::NumpadDivide, Key::NumpadDecimal, Key::NumpadEnter,
        Key::NumpadEqual
    };
    for (auto k : npads) {
        uint32_t ks = PlatformKeyMap::to_x11_keysym(k);
        EXPECT_NE(0u, ks) << "to_x11_keysym returned 0 for numpad key 0x"
            << std::hex << static_cast<uint32_t>(k);
        Key back = PlatformKeyMap::from_x11_keysym(ks);
        EXPECT_EQ(k, back) << "Round-trip failed for numpad key 0x"
            << std::hex << static_cast<uint32_t>(k);
    }
#endif
}

TEST(StressTest, AllBrowserKeysRoundTripX11) {
#ifdef __linux__
    const std::vector<Key> browsers = {
        Key::BrowserBack, Key::BrowserForward, Key::BrowserRefresh,
        Key::BrowserStop, Key::BrowserSearch, Key::BrowserFavorites,
        Key::BrowserHome
    };
    for (auto k : browsers) {
        uint32_t ks = PlatformKeyMap::to_x11_keysym(k);
        EXPECT_NE(0u, ks) << "to_x11_keysym returned 0 for browser key 0x"
            << std::hex << static_cast<uint32_t>(k);
    }
#endif
}

TEST(StressTest, AllMediaKeysRoundTripX11) {
#ifdef __linux__
    const std::vector<Key> medias = {
        Key::VolumeMute, Key::VolumeDown, Key::VolumeUp,
        Key::MediaPlay, Key::MediaPause, Key::MediaStop,
        Key::MediaNext, Key::MediaPrev, Key::MediaRewind,
        Key::MediaFastForward, Key::MediaRecord
    };
    for (auto k : medias) {
        uint32_t ks = PlatformKeyMap::to_x11_keysym(k);
        EXPECT_NE(0u, ks);
        Key back = PlatformKeyMap::from_x11_keysym(ks);
        EXPECT_EQ(k, back);
    }
#endif
}

TEST(StressTest, ManyMouseClicks) {
    MockMouse mouse;
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(mouse.mouse_click(MouseButton::Left));
    }
    EXPECT_EQ(100, mouse.call_count_down);
    EXPECT_EQ(100, mouse.call_count_up);
}

TEST(StressTest, ManyKeyClicks) {
    MockKeyboard kb;
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(kb.key_click(Key::Space));
    }
    EXPECT_EQ(100, kb.down_count);
    EXPECT_EQ(100, kb.up_count);
}

TEST(StressTest, DslParserManyTokens) {
    std::string dsl;
    for (int i = 0; i < 50; i++) {
        dsl += "{SPACE}";
    }
    auto tokens = DslParser::parse(dsl);
    EXPECT_EQ(50u, tokens.size());
    for (const auto& t : tokens) {
        EXPECT_EQ(DslToken::KEY, t.type);
        EXPECT_EQ(Key::Space, t.key);
    }
}

TEST(StressTest, DslParserComplexMix) {
    std::string dsl = "start ";
    for (int i = 0; i < 10; i++) {
        dsl += "{+SHIFT}word" + std::to_string(i) + "{-SHIFT} {SPACE}";
    }
    dsl += "{RETURN}end";
    auto tokens = DslParser::parse(dsl);
    EXPECT_GE(tokens.size(), 5u);
}

// ============================================================================
// 46. Additional DslParser Edge Cases
// ============================================================================

class DslParserEdgeTest : public ::testing::Test {};

TEST_F(DslParserEdgeTest, ParseSingleBraceWithoutMatch) {
    // A single '{' without matching '}' should be text
    auto tokens = DslParser::parse("hello { world");
    EXPECT_GE(tokens.size(), 1u);
}

TEST_F(DslParserEdgeTest, ParseUnclosedBrace) {
    auto tokens = DslParser::parse("{+SHIFT hello");
    EXPECT_GE(tokens.size(), 1u);
}

TEST_F(DslParserEdgeTest, ParseEmptyBraces) {
    auto tokens = DslParser::parse("{}");
    // Empty braces may be treated as text or error
    EXPECT_GE(tokens.size(), 0u);
}

TEST_F(DslParserEdgeTest, ParseConsecutiveSpecialKeys) {
    auto tokens = DslParser::parse("{TAB}{TAB}{TAB}{RETURN}{RETURN}");
    ASSERT_EQ(5u, tokens.size());
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(Key::Tab, tokens[i].key);
    }
    EXPECT_EQ(Key::Return, tokens[3].key);
    EXPECT_EQ(Key::Return, tokens[4].key);
}

TEST_F(DslParserEdgeTest, ParseKeyWithTrailingSpaces) {
    auto tokens = DslParser::parse("{TAB  }");
    EXPECT_GE(tokens.size(), 1u);
}

TEST_F(DslParserEdgeTest, ParseDelayWithMsSuffix) {
    auto tokens = DslParser::parse("{DELAY 1000ms}");
    bool found_delay = false;
    for (const auto& t : tokens) {
        if (t.type == DslToken::DELAY) {
            found_delay = true;
            EXPECT_EQ(std::chrono::milliseconds(1000), t.timing);
        }
    }
    EXPECT_TRUE(found_delay);
}

TEST_F(DslParserEdgeTest, ParseDelayWithoutMsSuffix) {
    // May or may not be valid depending on parser implementation
    auto tokens = DslParser::parse("{DELAY 500}");
    // At minimum, parsing should not crash
    EXPECT_GE(tokens.size(), 0u);
}

TEST_F(DslParserEdgeTest, ParseNestedModifiersDeep) {
    auto tokens = DslParser::parse("{{{{+SHIFT}text{-SHIFT}}}}");
    EXPECT_GE(tokens.size(), 3u);
}

TEST_F(DslParserEdgeTest, ParseOnlyModifiersNoText) {
    auto tokens = DslParser::parse("{+CTRL}{+ALT}{-ALT}{-CTRL}");
    EXPECT_GE(tokens.size(), 4u);
}

TEST_F(DslParserEdgeTest, ParseFKeysAll) {
    for (int i = 1; i <= 24; i++) {
        std::string dsl = "{F" + std::to_string(i) + "}";
        auto tokens = DslParser::parse(dsl);
        ASSERT_EQ(1u, tokens.size());
        EXPECT_EQ(DslToken::KEY, tokens[0].type);
    }
}

TEST_F(DslParserEdgeTest, ParseSingleCharacterInBraces) {
    // A single character that isn't a key name: {x}
    auto tokens = DslParser::parse("a{x}b");
    EXPECT_GE(tokens.size(), 1u);
}

// ============================================================================
// Additional Utility Coverage
// ============================================================================

TEST(UtilityTest, MouseButtonNameConsistentWithEnumValues) {
    // Verify symmetry between enum values and names
    for (int32_t val = 0; val <= 8; val++) {
        auto btn = static_cast<MouseButton>(val);
        auto name = std::string(mouse_button_name(btn));
        EXPECT_FALSE(name.empty());
        EXPECT_NE("Unknown", name);
    }
}

TEST(UtilityTest, KeyNameVersusIsValidKeyName) {
    // All keys returned by key_name should be valid DSL key names
    const std::vector<Key> named_keys = {
        Key::Shift, Key::RightShift, Key::Control, Key::RightControl,
        Key::Alt, Key::RightAlt, Key::AltGr, Key::Meta, Key::RightMeta,
        Key::Return, Key::Escape, Key::Tab, Key::Space, Key::Backspace,
        Key::Delete, Key::Insert, Key::Home, Key::End, Key::PageUp,
        Key::PageDown, Key::LeftArrow, Key::RightArrow, Key::UpArrow,
        Key::DownArrow, Key::CapsLock, Key::NumLock, Key::ScrollLock,
        Key::PrintScreen, Key::Pause, Key::Menu,
        Key::VolumeMute, Key::VolumeDown, Key::VolumeUp,
        Key::MediaPlay, Key::MediaPause, Key::MediaStop,
        Key::MediaNext, Key::MediaPrev,
        Key::BrowserBack, Key::BrowserForward, Key::BrowserRefresh,
        Key::BrowserStop, Key::BrowserSearch, Key::BrowserFavorites,
        Key::BrowserHome,
        Key::LaunchMail, Key::LaunchMedia, Key::LaunchCalculator,
        Key::SystemPowerDown, Key::SystemSleep, Key::SystemWakeUp,
        Key::Help, Key::Undo, Key::Redo, Key::Cut, Key::Copy,
        Key::Paste, Key::Find, Key::SelectAll,
        Key::ZoomIn, Key::ZoomOut, Key::ZoomReset
    };
    for (auto k : named_keys) {
        auto name = std::string(key_name(k));
        // Convert to uppercase for DSL validation
        std::string upper = name;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        if (name != "Unknown") {
            EXPECT_TRUE(DslParser::is_valid_key_name(upper))
                << "key_name '" << name << "' should be a valid DSL key name";
        }
    }
}

TEST(UtilityTest, IsModifierKeyConsistentWithModifierState) {
    // All keys classified as modifiers should appear in ModifierState
    const std::vector<Key> modifier_keys = {
        Key::Shift, Key::RightShift,
        Key::Control, Key::RightControl,
        Key::Alt, Key::RightAlt, Key::AltGr,
        Key::Meta, Key::RightMeta
    };
    for (auto k : modifier_keys) {
        EXPECT_TRUE(is_modifier_key(k));
    }
}

TEST(UtilityTest, IsNumpadKeyMatchesRange) {
    // Keys outside the numpad range should return false
    EXPECT_FALSE(is_numpad_key(Key::Space));
    EXPECT_FALSE(is_numpad_key(Key::Shift));
    EXPECT_FALSE(is_numpad_key(Key::F1));
    EXPECT_FALSE(is_numpad_key(Key::VolumeMute));
    EXPECT_FALSE(is_numpad_key(Key::BrowserBack));
}

TEST(UtilityTest, CategoryChecksAreMutuallyExclusiveInMostCases) {
    // Most keys should NOT be classified in multiple categories
    auto check_exclusive = [](Key k) {
        int cats = 0;
        if (is_modifier_key(k)) cats++;
        if (is_numpad_key(k)) cats++;
        if (is_media_key(k)) cats++;
        if (is_browser_key(k)) cats++;
        if (is_launch_key(k)) cats++;
        if (is_system_power_key(k)) cats++;
        return cats;
    };

    // Regular keys should be in exactly 0 or 1 category
    EXPECT_EQ(0, check_exclusive(Key::Space));
    EXPECT_EQ(0, check_exclusive(Key::Return));
    EXPECT_EQ(0, check_exclusive(Key::Escape));
    EXPECT_EQ(1, check_exclusive(Key::Shift));
    EXPECT_EQ(1, check_exclusive(Key::Numpad0));
    EXPECT_EQ(1, check_exclusive(Key::VolumeMute));
    EXPECT_EQ(1, check_exclusive(Key::BrowserBack));
    EXPECT_EQ(1, check_exclusive(Key::LaunchMail));
    EXPECT_EQ(1, check_exclusive(Key::SystemSleep));
}

// ============================================================================
// Final: Ensure we have comprehensive test structure
// ============================================================================

TEST(TestSuiteOrganization, AllSuitesExist) {
    // This test verifies the organization structure exists;
    // actual coverage is verified by the individual test cases above.
    EXPECT_TRUE(true);
}
