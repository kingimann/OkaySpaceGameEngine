#pragma once
// The macOS backend's pure logic. NO Objective-C is written here or anywhere else: there is no macOS
// toolchain on the machine this was written on, so only what can be tested on Linux exists. When the
// Cocoa backend is written it consumes these tables (NSEvent.keyCode) and the UTF-16 helpers in
// PlatformText.hpp (NSEvent.characters is UTF-16).
#include "okay/Platform/Platform.hpp"
#include <cstdint>

namespace okay::platform::mac {

// kVK_* virtual key codes (Carbon HIToolbox/Events.h) -> Key. 0 / Key::Unknown = not mapped. The codes are
// physical-position codes (ANSI layout names), which is what Key (USB usage numbering) means too.
inline Key KeyFromVirtualKey(unsigned vk) {
    switch (vk) {
    case 0x00: return Key::A; case 0x01: return Key::S; case 0x02: return Key::D; case 0x03: return Key::F;
    case 0x04: return Key::H; case 0x05: return Key::G; case 0x06: return Key::Z; case 0x07: return Key::X;
    case 0x08: return Key::C; case 0x09: return Key::V; case 0x0B: return Key::B; case 0x0C: return Key::Q;
    case 0x0D: return Key::W; case 0x0E: return Key::E; case 0x0F: return Key::R; case 0x10: return Key::Y;
    case 0x11: return Key::T; case 0x12: return Key::N1; case 0x13: return Key::N2; case 0x14: return Key::N3;
    case 0x15: return Key::N4; case 0x16: return Key::N6; case 0x17: return Key::N5; case 0x18: return Key::Equals;
    case 0x19: return Key::N9; case 0x1A: return Key::N7; case 0x1B: return Key::Minus; case 0x1C: return Key::N8;
    case 0x1D: return Key::N0; case 0x1E: return Key::RightBracket; case 0x1F: return Key::O; case 0x20: return Key::U;
    case 0x21: return Key::LeftBracket; case 0x22: return Key::I; case 0x23: return Key::P; case 0x24: return Key::Return;
    case 0x25: return Key::L; case 0x26: return Key::J; case 0x27: return Key::Apostrophe; case 0x28: return Key::K;
    case 0x29: return Key::Semicolon; case 0x2A: return Key::Backslash; case 0x2B: return Key::Comma; case 0x2C: return Key::Slash;
    case 0x2D: return Key::N; case 0x2E: return Key::M; case 0x2F: return Key::Period; case 0x30: return Key::Tab;
    case 0x31: return Key::Space; case 0x32: return Key::Grave; case 0x33: return Key::Backspace; case 0x35: return Key::Escape;
    case 0x36: return Key::RGui; case 0x37: return Key::LGui; case 0x38: return Key::LShift; case 0x39: return Key::CapsLock;
    case 0x3A: return Key::LAlt; case 0x3B: return Key::LCtrl; case 0x3C: return Key::RShift; case 0x3D: return Key::RAlt;
    case 0x3E: return Key::RCtrl;
    case 0x41: return Key::KpPeriod; case 0x43: return Key::KpMultiply; case 0x45: return Key::KpPlus; case 0x47: return Key::NumLock;
    case 0x4B: return Key::KpDivide; case 0x4C: return Key::KpEnter; case 0x4E: return Key::KpMinus;
    case 0x52: return Key::Kp0; case 0x53: return Key::Kp1; case 0x54: return Key::Kp2; case 0x55: return Key::Kp3;
    case 0x56: return Key::Kp4; case 0x57: return Key::Kp5; case 0x58: return Key::Kp6; case 0x59: return Key::Kp7;
    case 0x5B: return Key::Kp8; case 0x5C: return Key::Kp9;
    case 0x60: return Key::F5; case 0x61: return Key::F6; case 0x62: return Key::F7; case 0x63: return Key::F3;
    case 0x64: return Key::F8; case 0x65: return Key::F9; case 0x67: return Key::F11; case 0x69: return Key::PrintScreen;
    case 0x6B: return Key::ScrollLock; case 0x6D: return Key::F10; case 0x6F: return Key::F12; case 0x71: return Key::Pause;
    case 0x72: return Key::Insert; case 0x73: return Key::Home; case 0x74: return Key::PageUp; case 0x75: return Key::Delete;
    case 0x76: return Key::F4; case 0x77: return Key::End; case 0x78: return Key::F2; case 0x79: return Key::PageDown;
    case 0x7A: return Key::F1; case 0x7B: return Key::Left; case 0x7C: return Key::Right; case 0x7D: return Key::Down;
    case 0x7E: return Key::Up;
    default: return Key::Unknown;
    }
}

// NSEventModifierFlags bits (AppKit) -> Mod bits.
constexpr uint64_t kNsCapsLock = 1ull << 16, kNsShift = 1ull << 17, kNsControl = 1ull << 18, kNsOption = 1ull << 19,
                   kNsCommand = 1ull << 20, kNsNumericPad = 1ull << 21;
inline uint32_t ModsFromNsFlags(uint64_t f) {
    uint32_t m = 0;
    if (f & kNsShift) m |= ModShift;
    if (f & kNsControl) m |= ModCtrl;
    if (f & kNsOption) m |= ModAlt;
    if (f & kNsCommand) m |= ModGui;
    if (f & kNsCapsLock) m |= ModCaps;
    return m;
}

// NSEvent.scrollingDeltaY with hasPreciseScrollingDeltas (trackpad, points) vs line deltas (wheel):
// one platform notch = 1.0. A trackpad point count is divided by ~10 points per line.
inline float WheelNotches(double delta, bool precise) { return (float)(precise ? delta / 10.0 : delta); }

} // namespace okay::platform::mac
