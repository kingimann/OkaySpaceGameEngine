#pragma once
// The decisions of the web backend (WebBackend.cpp) that need no browser: DOM key codes to our keys, which key presses
// the page must not act on, wheel units, the Gamepad API's standard layout, the canvas size in device pixels, the
// keyboard layout learned from key presses, DOM mouse buttons, cursor names and the Web Audio buffer size. No
// Emscripten header, so tests/test_platform_web_pure.cpp checks every one of them on Linux.
//
// Written from the W3C specifications only: UI Events KeyboardEvent code Values (the `code` names), UI Events
// (`key`, WheelEvent.deltaMode, MouseEvent.button), Gamepad ("Standard Gamepad" layout), CSSOM View
// (devicePixelRatio), CSS Basic User Interface (cursor keywords) and Web Audio (ScriptProcessorNode buffer sizes), and
// the USB HID Usage Tables (keyboard page) for our key numbers. Nothing here follows another project's code.
#include "okay/Platform/Platform.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace okay::platform::web {

// ---- Keys: KeyboardEvent.code -> USB HID usage (keyboard page) ------------------------------------------------
struct DomCodeEntry { const char* code; uint16_t usage; };

/// The physical key of a KeyboardEvent.code value; Key::Unknown for an empty or unknown name.
inline Key KeyFromDomCode(const char* code) {
    if (!code || !*code) return Key::Unknown;
    // KeyA..KeyZ, Digit0..Digit9, F1..F24 and Numpad0..Numpad9 follow a pattern.
    const size_t n = std::strlen(code);
    if (n == 4 && std::strncmp(code, "Key", 3) == 0 && code[3] >= 'A' && code[3] <= 'Z') return (Key)(4 + (code[3] - 'A'));
    if (n == 6 && std::strncmp(code, "Digit", 5) == 0 && code[5] >= '0' && code[5] <= '9')
        return code[5] == '0' ? Key::N0 : (Key)(30 + (code[5] - '1'));
    if (n == 7 && std::strncmp(code, "Numpad", 6) == 0 && code[6] >= '0' && code[6] <= '9')
        return code[6] == '0' ? Key::Kp0 : (Key)(89 + (code[6] - '1'));
    if ((n == 2 || n == 3) && code[0] == 'F' && code[1] >= '1' && code[1] <= '9') {
        int f = code[1] - '0';
        if (n == 3) { if (code[2] < '0' || code[2] > '9') return Key::Unknown; f = f * 10 + (code[2] - '0'); }
        if (f >= 1 && f <= 12) return (Key)(58 + (f - 1));    // F1..F12 = 0x3A..0x45
        if (f >= 13 && f <= 24) return (Key)(104 + (f - 13)); // F13..F24 = 0x68..0x73
        return Key::Unknown;
    }
    static const DomCodeEntry kTable[] = {
        {"Enter", 40}, {"Escape", 41}, {"Backspace", 42}, {"Tab", 43}, {"Space", 44},
        {"Minus", 45}, {"Equal", 46}, {"BracketLeft", 47}, {"BracketRight", 48}, {"Backslash", 49},
        {"Semicolon", 51}, {"Quote", 52}, {"Backquote", 53}, {"Comma", 54}, {"Period", 55}, {"Slash", 56},
        {"CapsLock", 57}, {"PrintScreen", 70}, {"ScrollLock", 71}, {"Pause", 72},
        {"Insert", 73}, {"Home", 74}, {"PageUp", 75}, {"Delete", 76}, {"End", 77}, {"PageDown", 78},
        {"ArrowRight", 79}, {"ArrowLeft", 80}, {"ArrowDown", 81}, {"ArrowUp", 82},
        {"NumLock", 83}, {"NumpadDivide", 84}, {"NumpadMultiply", 85}, {"NumpadSubtract", 86}, {"NumpadAdd", 87},
        {"NumpadEnter", 88}, {"NumpadDecimal", 99}, {"IntlBackslash", 100}, {"ContextMenu", 101}, {"Power", 102},
        {"NumpadEqual", 103}, {"AudioVolumeMute", 127}, {"VolumeMute", 127}, {"AudioVolumeUp", 128}, {"VolumeUp", 128},
        {"AudioVolumeDown", 129}, {"VolumeDown", 129}, {"NumpadComma", 133}, {"IntlRo", 135}, {"KanaMode", 136},
        {"IntlYen", 137}, {"Convert", 138}, {"NonConvert", 139}, {"Lang1", 144}, {"Lang2", 145},
        {"ControlLeft", 224}, {"ShiftLeft", 225}, {"AltLeft", 226}, {"MetaLeft", 227}, {"OSLeft", 227},
        {"ControlRight", 228}, {"ShiftRight", 229}, {"AltRight", 230}, {"MetaRight", 231}, {"OSRight", 231},
    };
    for (const DomCodeEntry& e : kTable)
        if (std::strcmp(e.code, code) == 0) return (Key)e.usage;
    return Key::Unknown;
}

/// True when a KeyboardEvent.key value is one printable character (one code point, not a control character) rather
/// than a key name such as "Enter", "ArrowUp" or "Dead". Such a press types text, through the keypress that follows.
inline bool KeyTypesText(const char* key) {
    if (!key || !*key) return false;
    const unsigned char c0 = (unsigned char)key[0];
    if (c0 < 0x20 || c0 == 0x7F) return false;
    int len = c0 < 0x80 ? 1 : (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xE ? 3 : (c0 >> 3) == 0x1E ? 4 : 0;
    if (len == 0) return false;   // a continuation byte or an invalid lead byte
    for (int i = 1; i < len; ++i)
        if (((unsigned char)key[i] & 0xC0) != 0x80) return false;
    return key[len] == '\0';
}

/// Whether the backend tells the browser not to act on a keydown (scroll with the arrows, move focus with Tab, open
/// its find bar...). A key that types text is let through, so the browser still sends the keypress that carries the
/// text (the keypress is then stopped, which is what keeps Space from scrolling). Ctrl / Command with C, V or X are let
/// through so the page gets its copy, paste and cut events, and F12 so the developer tools still open.
inline bool PreventKeyDown(Key k, bool typesText, bool ctrl, bool meta) {
    if ((ctrl || meta) && (k == Key::C || k == Key::V || k == Key::X)) return false;
    if (k == Key::F12) return false;
    if (typesText && !ctrl && !meta) return false;
    return true;
}

/// Learns the keyboard layout from key presses: which physical key typed which letter, digit or space (KeyboardEvent.key
/// against .code), so Backend::KeyForChar follows an AZERTY or Dvorak keyboard once the key was pressed.
class LayoutMap {
public:
    void Learn(Key physical, const char* key) {
        if (physical == Key::Unknown || !key || !key[0] || key[1]) return;   // one ASCII byte only
        char c = key[0];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ')) return;
        m_map[(unsigned char)c] = physical;
    }
    /// The key learned for `c` (letters in either case), or Key::Unknown when it was never typed.
    Key ForChar(char c) const {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        return (unsigned char)c < 128 ? m_map[(unsigned char)c] : Key::Unknown;
    }
private:
    Key m_map[128] = {};
};

// ---- Mouse and wheel ------------------------------------------------------------------------------------------
/// MouseEvent.button (0 main, 1 auxiliary, 2 secondary, 3 back, 4 forward) -> MouseButton; 0 for anything else.
inline int MouseButtonFromDom(int b) { return b >= 0 && b <= 4 ? (b == 1 ? MouseMiddle : b == 2 ? MouseRight : b + 1) : 0; }

/// WheelEvent delta -> wheel notches. deltaMode 0 = pixels (100 a notch), 1 = lines (3 a notch), 2 = pages (1 a
/// notch). Not a number or a huge value gives 0 or is held to +-1000 notches.
inline float WheelNotches(double delta, int deltaMode) {
    if (!std::isfinite(delta)) return 0.0f;
    const double per = deltaMode == 1 ? 3.0 : deltaMode == 2 ? 1.0 : 100.0;
    double v = delta / per;
    if (v > 1000.0) v = 1000.0;
    if (v < -1000.0) v = -1000.0;
    return (float)v;
}

/// A position inside the canvas (CSS pixels) as 0..1 of its size (touch fingers); 0 for an empty canvas.
inline float Fraction(double pos, double size) {
    if (!(size > 0) || !std::isfinite(pos)) return 0.0f;
    const double f = pos / size;
    return (float)(f < 0 ? 0 : f > 1 ? 1 : f);
}

/// CSS cursor keyword for a CursorShape.
inline const char* CssCursor(CursorShape s) {
    switch (s) {
    case CursorShape::Arrow: return "default";
    case CursorShape::TextInput: return "text";
    case CursorShape::ResizeAll: return "move";
    case CursorShape::ResizeNS: return "ns-resize";
    case CursorShape::ResizeEW: return "ew-resize";
    case CursorShape::ResizeNESW: return "nesw-resize";
    case CursorShape::ResizeNWSE: return "nwse-resize";
    case CursorShape::Hand: return "pointer";
    case CursorShape::Wait: return "wait";
    case CursorShape::Progress: return "progress";
    case CursorShape::NotAllowed: return "not-allowed";
    }
    return "default";
}

// ---- Canvas size ------------------------------------------------------------------------------------------------
/// The canvas's drawing-buffer size for its CSS size: device pixels (CSS * devicePixelRatio) for a high-DPI window,
/// CSS pixels otherwise. Rounded, at least 1, at most kMaxWindowExtent; a ratio that is not a number or outside
/// 0.25..8 counts as 1.
inline void CanvasPixels(double cssW, double cssH, double dpr, bool highDpi, int& outW, int& outH) {
    double s = highDpi && std::isfinite(dpr) && dpr >= 0.25 && dpr <= 8.0 ? dpr : 1.0;
    auto px = [s](double css) {
        if (!std::isfinite(css) || css < 0) css = 0;
        double v = std::floor(css * s + 0.5);
        if (v < 1) v = 1;
        if (v > kMaxWindowExtent) v = kMaxWindowExtent;
        return (int)v;
    };
    outW = px(cssW); outH = px(cssH);
}

// ---- Gamepad API ("standard" mapping) ---------------------------------------------------------------------------
/// The index in Gamepad.buttons of a button of the standard layout.
inline int StandardButtonIndex(GamepadButton b) {
    switch (b) {
    case GamepadButton::A: return 0;
    case GamepadButton::B: return 1;
    case GamepadButton::X: return 2;
    case GamepadButton::Y: return 3;
    case GamepadButton::LeftShoulder: return 4;
    case GamepadButton::RightShoulder: return 5;
    case GamepadButton::Back: return 8;
    case GamepadButton::Start: return 9;
    case GamepadButton::LeftStick: return 10;
    case GamepadButton::RightStick: return 11;
    case GamepadButton::DpadUp: return 12;
    case GamepadButton::DpadDown: return 13;
    case GamepadButton::DpadLeft: return 14;
    case GamepadButton::DpadRight: return 15;
    case GamepadButton::Guide: return 16;
    }
    return -1;
}
constexpr int kStandardLeftTrigger = 6, kStandardRightTrigger = 7;   // analog buttons in the standard layout

/// A stick axis (-1..1, +y down in the standard layout, as ours) -> -32768..32767; NaN = 0.
inline int16_t StickValue(double v) {
    if (!std::isfinite(v)) return 0;
    if (v >= 1.0) return 32767;
    if (v <= -1.0) return -32768;
    const double s = v * (v < 0 ? 32768.0 : 32767.0);
    return (int16_t)std::floor(s + 0.5);
}
/// A trigger (an analog button, 0..1) -> 0..32767; NaN or below 0 = 0.
inline int16_t TriggerValue(double v) {
    if (!std::isfinite(v) || v <= 0.0) return 0;
    if (v >= 1.0) return 32767;
    return (int16_t)std::floor(v * 32767.0 + 0.5);
}

/// The USB vendor and product id in a Gamepad.id string, when the browser puts them there: "Name (... Vendor: 054c
/// Product: 09cc)" or "054c-09cc-Name" (four hex digits each). False (both 0) when neither form is there.
inline bool VendorProductFromId(const char* id, uint16_t& vendor, uint16_t& product) {
    vendor = product = 0;
    if (!id) return false;
    auto hex4 = [](const char* s, uint16_t& out) {
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s[i];
            const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return false;
            v = v * 16 + (unsigned)d;
        }
        out = (uint16_t)v;
        return true;
    };
    const char* vp = std::strstr(id, "Vendor: ");
    const char* pp = std::strstr(id, "Product: ");
    uint16_t v = 0, p = 0;
    if (vp && pp && std::strlen(vp) >= 12 && std::strlen(pp) >= 13 && hex4(vp + 8, v) && hex4(pp + 9, p)) { vendor = v; product = p; return true; }
    if (std::strlen(id) >= 10 && id[4] == '-' && id[9] == '-' && hex4(id, v) && hex4(id + 5, p)) { vendor = v; product = p; return true; }
    return false;
}

// ---- Web Audio --------------------------------------------------------------------------------------------------
/// A ScriptProcessorNode buffer size near `frames`: a power of two from 256 to 16384 (the sizes the node takes),
/// the smallest that holds `frames`.
inline int ProcessorFrames(int frames) {
    int n = 256;
    while (n < frames && n < 16384) n *= 2;
    return n;
}

} // namespace okay::platform::web
