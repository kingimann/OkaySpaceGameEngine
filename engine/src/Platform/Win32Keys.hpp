#pragma once
// Windows hardware scan code (set 1 make code, as in the high bits of a key message's lParam) to
// our Key (USB HID usage numbering). Pure: no Windows headers, tested on Linux by
// tests/test_platform_audio_pump.cpp. Written from the public Microsoft "Keyboard scan code
// specification" table and the USB HID usage tables (keyboard page).
#include "okay/Platform/Platform.hpp"

namespace okay::platform::win32keys {

/// `scan` is bits 16..23 of lParam, `ext` is bit 24 (the extended-key flag).
inline Key KeyFromScan(int scan, bool ext) {
    static const uint16_t kTab[89] = {
        0, 41, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 45, 46, 42, 43,
        20, 26, 8, 21, 23, 28, 24, 12, 18, 19, 47, 48, 40, 224, 4, 22,
        7, 9, 10, 11, 13, 14, 15, 51, 52, 53, 225, 49, 29, 27, 6, 25,
        5, 17, 16, 54, 55, 56, 229, 85, 226, 44, 57, 58, 59, 60, 61, 62,
        63, 64, 65, 66, 67, 83, 71, 95, 96, 97, 86, 92, 93, 94, 87, 89,
        90, 91, 98, 99, 0, 0, 100, 68, 69};   // 0x56 = the extra key on ISO keyboards (between left Shift and Z)
    if (ext) {
        switch (scan) {
        case 0x1C: return Key::KpEnter;   case 0x1D: return Key::RCtrl;   case 0x35: return Key::KpDivide;
        case 0x37: return Key::PrintScreen; case 0x38: return Key::RAlt;  case 0x47: return Key::Home;
        case 0x48: return Key::Up;        case 0x49: return Key::PageUp;  case 0x4B: return Key::Left;
        case 0x4D: return Key::Right;     case 0x4F: return Key::End;     case 0x50: return Key::Down;
        case 0x51: return Key::PageDown;  case 0x52: return Key::Insert;  case 0x53: return Key::Delete;
        case 0x5B: return Key::LGui;      case 0x5C: return Key::RGui;    case 0x5D: return Key::Application;
        case 0x45: return Key::NumLock;
        default: return Key::Unknown;
        }
    }
    if (scan == 0x45) return Key::Pause;   // Pause sends a non-extended 0x45 with a different prefix
    return scan >= 0 && scan < 89 ? (Key)kTab[scan] : Key::Unknown;
}

} // namespace okay::platform::win32keys
