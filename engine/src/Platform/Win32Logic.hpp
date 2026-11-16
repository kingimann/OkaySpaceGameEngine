#pragma once
// The pure (no windows.h) logic of the own Win32 backend: key tables, DPI arithmetic, double-click
// counting, raw-mouse accumulation, XInput value mapping and controller hot-plug tracking.
// Kept free of OS headers so it is unit-tested on Linux (tests/test_platform_pure.cpp); OwnWin32.cpp
// static_asserts the constants below against the real Windows headers when it is compiled for Windows.
#include "okay/Platform/Platform.hpp"
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace okay::platform::win32 {

// ---- keys ---------------------------------------------------------------------------------------
// `scan` is bits 16..23 of a WM_KEYDOWN lParam, `ext` is bit 24.
inline Key KeyFromScan(int scan, bool ext) {
    static const uint16_t kTab[89] = {
        0, 41, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 45, 46, 42, 43,
        20, 26, 8, 21, 23, 28, 24, 12, 18, 19, 47, 48, 40, 224, 4, 22,
        7, 9, 10, 11, 13, 14, 15, 51, 52, 53, 225, 49, 29, 27, 6, 25,
        5, 17, 16, 54, 55, 56, 229, 85, 226, 44, 57, 58, 59, 60, 61, 62,
        63, 64, 65, 66, 67, 83, 71, 95, 96, 97, 86, 92, 93, 94, 87, 89,
        90, 91, 98, 99, 0, 0, 0, 68, 69};
    if (ext) {
        switch (scan) {
        case 0x1C: return Key::KpEnter;   case 0x1D: return Key::RCtrl;   case 0x35: return Key::KpDivide;
        case 0x37: return Key::PrintScreen; case 0x38: return Key::RAlt;  case 0x47: return Key::Home;
        case 0x48: return Key::Up;        case 0x49: return Key::PageUp;  case 0x4B: return Key::Left;
        case 0x4D: return Key::Right;     case 0x4F: return Key::End;     case 0x50: return Key::Down;
        case 0x51: return Key::PageDown;  case 0x52: return Key::Insert;  case 0x53: return Key::Delete;
        case 0x5B: return Key::LGui;      case 0x5C: return Key::RGui;    case 0x45: return Key::NumLock;
        default: return Key::Unknown;
        }
    }
    if (scan == 0x45) return Key::Pause;   // Pause is a non-extended 0x45; NumLock is the extended one
    return scan >= 0 && scan < 89 ? (Key)kTab[scan] : Key::Unknown;
}

// Windows virtual-key code for a Key (0 = none). Used by IsKeyDown (GetAsyncKeyState). Values are the
// documented VK_* constants; OwnWin32.cpp checks a sample of them against <winuser.h>.
inline int VkForKey(Key k) {
    const int ik = (int)k;
    if (ik >= (int)Key::A && ik <= (int)Key::Z) return 'A' + (ik - (int)Key::A);
    if (ik >= (int)Key::N1 && ik <= (int)Key::N9) return '1' + (ik - (int)Key::N1);
    if (ik >= (int)Key::F1 && ik <= (int)Key::F12) return 0x70 + (ik - (int)Key::F1);
    if (ik >= (int)Key::Kp1 && ik <= (int)Key::Kp9) return 0x61 + (ik - (int)Key::Kp1);
    switch (k) {
    case Key::N0: return '0';
    case Key::Return: return 0x0D;   case Key::Escape: return 0x1B;   case Key::Backspace: return 0x08;
    case Key::Tab: return 0x09;      case Key::Space: return 0x20;
    case Key::Minus: return 0xBD;    case Key::Equals: return 0xBB;   case Key::LeftBracket: return 0xDB;
    case Key::RightBracket: return 0xDD; case Key::Backslash: return 0xDC; case Key::Semicolon: return 0xBA;
    case Key::Apostrophe: return 0xDE; case Key::Grave: return 0xC0;  case Key::Comma: return 0xBC;
    case Key::Period: return 0xBE;   case Key::Slash: return 0xBF;    case Key::CapsLock: return 0x14;
    case Key::PrintScreen: return 0x2C; case Key::ScrollLock: return 0x91; case Key::Pause: return 0x13;
    case Key::Insert: return 0x2D;   case Key::Home: return 0x24;     case Key::PageUp: return 0x21;
    case Key::Delete: return 0x2E;   case Key::End: return 0x23;      case Key::PageDown: return 0x22;
    case Key::Right: return 0x27;    case Key::Left: return 0x25;     case Key::Down: return 0x28; case Key::Up: return 0x26;
    case Key::NumLock: return 0x90;  case Key::KpDivide: return 0x6F; case Key::KpMultiply: return 0x6A;
    case Key::KpMinus: return 0x6D;  case Key::KpPlus: return 0x6B;   case Key::KpEnter: return 0x0D;   // shares VK_RETURN
    case Key::Kp0: return 0x60;      case Key::KpPeriod: return 0x6E;
    case Key::LCtrl: return 0xA2;    case Key::LShift: return 0xA0;   case Key::LAlt: return 0xA4;      case Key::LGui: return 0x5B;
    case Key::RCtrl: return 0xA3;    case Key::RShift: return 0xA1;   case Key::RAlt: return 0xA5;      case Key::RGui: return 0x5C;
    default: return 0;
    }
}

// ---- DPI ----------------------------------------------------------------------------------------
constexpr unsigned kBaseDpi = 96;
// v * dpi / 96 rounded to nearest (halves away from zero), as MulDiv does; dpi 0 means "unknown" = 96.
inline int ScaleForDpi(int v, unsigned dpi) {
    if (dpi == 0) dpi = kBaseDpi;
    const long long num = (long long)v * (long long)dpi;
    return (int)(num >= 0 ? (num + kBaseDpi / 2) / kBaseDpi : -((-num + kBaseDpi / 2) / kBaseDpi));
}
inline int UnscaleForDpi(int v, unsigned dpi) {
    if (dpi == 0) dpi = kBaseDpi;
    const long long num = (long long)v * (long long)kBaseDpi;
    return (int)(num >= 0 ? (num + (long long)dpi / 2) / (long long)dpi : -((-num + (long long)dpi / 2) / (long long)dpi));
}
inline float DpiScaleFactor(unsigned dpi) { return (dpi == 0 ? kBaseDpi : dpi) / (float)kBaseDpi; }

// ---- double-click counting (the class has no CS_DBLCLKS: that would swallow the second button-down) ----
class ClickCounter {
public:
    // maxDelayMs / maxDist come from GetDoubleClickTime() and SM_CXDOUBLECLK / 2.
    int Down(int button, int x, int y, uint32_t timeMs, uint32_t maxDelayMs, int maxDist) {
        const bool same = m_count > 0 && button == m_button && (uint32_t)(timeMs - m_time) <= maxDelayMs &&
                          std::abs(x - m_x) <= maxDist && std::abs(y - m_y) <= maxDist;
        m_count = same ? m_count + 1 : 1;
        m_button = button; m_x = x; m_y = y; m_time = timeMs;
        return m_count;
    }
    void Reset() { m_count = 0; }
private:
    int m_count = 0, m_button = 0, m_x = 0, m_y = 0;
    uint32_t m_time = 0;
};

// ---- raw mouse (WM_INPUT) -----------------------------------------------------------------------
// RAWMOUSE.usFlags bit 0 (MOUSE_MOVE_ABSOLUTE) = lLastX/lLastY are absolute (remote desktop, pen, some
// virtual machines), else relative counts. Absolute samples become deltas against the previous one.
class RawMouseAccumulator {
public:
    void Add(uint16_t usFlags, long lastX, long lastY) {
        if (usFlags & 1) {
            if (m_haveAbs) { m_dx += (int)(lastX - m_absX); m_dy += (int)(lastY - m_absY); }
            m_absX = lastX; m_absY = lastY; m_haveAbs = true;
        } else { m_dx += (int)lastX; m_dy += (int)lastY; }
    }
    void Take(int& dx, int& dy) { dx = m_dx; dy = m_dy; m_dx = m_dy = 0; }
    void ResetAbsolute() { m_haveAbs = false; }
private:
    int m_dx = 0, m_dy = 0;
    long m_absX = 0, m_absY = 0;
    bool m_haveAbs = false;
};

// ---- XInput -------------------------------------------------------------------------------------
enum : uint16_t {
    kXiDpadUp = 0x0001, kXiDpadDown = 0x0002, kXiDpadLeft = 0x0004, kXiDpadRight = 0x0008,
    kXiStart = 0x0010, kXiBack = 0x0020, kXiLeftThumb = 0x0040, kXiRightThumb = 0x0080,
    kXiLeftShoulder = 0x0100, kXiRightShoulder = 0x0200, kXiA = 0x1000, kXiB = 0x2000, kXiX = 0x4000, kXiY = 0x8000,
};
inline uint16_t XiButtonMask(GamepadButton b) {
    switch (b) {
    case GamepadButton::A: return kXiA;           case GamepadButton::B: return kXiB;
    case GamepadButton::X: return kXiX;           case GamepadButton::Y: return kXiY;
    case GamepadButton::Back: return kXiBack;     case GamepadButton::Guide: return 0;   // XInput hides the Guide button
    case GamepadButton::Start: return kXiStart;
    case GamepadButton::LeftStick: return kXiLeftThumb;   case GamepadButton::RightStick: return kXiRightThumb;
    case GamepadButton::LeftShoulder: return kXiLeftShoulder; case GamepadButton::RightShoulder: return kXiRightShoulder;
    case GamepadButton::DpadUp: return kXiDpadUp;     case GamepadButton::DpadDown: return kXiDpadDown;
    case GamepadButton::DpadLeft: return kXiDpadLeft; case GamepadButton::DpadRight: return kXiDpadRight;
    }
    return 0;
}
// XInput sticks are +y up; the platform layer (like SDL) is +y down. -(-32768) does not fit in int16.
inline int16_t XiFlipY(int16_t v) { return v == -32768 ? (int16_t)32767 : (int16_t)-v; }
// Triggers are 0..255, the platform layer's are 0..32767.
inline int16_t XiTrigger(uint8_t t) { return (int16_t)(((int)t * 32767 + 127) / 255); }

// Which of the four XInput slots are connected, with a monotonically increasing instance id per connection
// (so a pad that is unplugged and plugged again is a new device, as with SDL), and a cheap hot-plug schedule:
// slots that are connected are re-read every Poll (a successful read is cheap), but a slot that is empty is
// only probed once per `probeIntervalMs`, because XInputGetState on an empty slot is the slow case.
class PadTracker {
public:
    static constexpr int kSlots = 4;
    struct Change { bool added; int slot; int instance; int deviceIndex; };
    explicit PadTracker(uint64_t probeIntervalMs = 1000) : m_interval(probeIntervalMs) {}

    // probe(slot) -> connected now? Appends the changes since the last Poll to `out`.
    template <class Probe>
    void Poll(uint64_t nowMs, Probe&& probe, std::vector<Change>& out) {
        for (int s = 0; s < kSlots; ++s) {
            if (!m_connected[s] && m_everProbed[s] && nowMs - m_lastProbe[s] < m_interval) continue;
            m_everProbed[s] = true; m_lastProbe[s] = nowMs;
            Set(s, probe(s), out);
        }
    }
    // A state read for a connected slot failed: it is gone.
    void MarkLost(int slot, std::vector<Change>& out) { if (slot >= 0 && slot < kSlots) Set(slot, false, out); }
    bool Connected(int slot) const { return slot >= 0 && slot < kSlots && m_connected[slot]; }
    int Instance(int slot) const { return Connected(slot) ? m_instance[slot] : 0; }
    int Count() const { int n = 0; for (int s = 0; s < kSlots; ++s) n += m_connected[s] ? 1 : 0; return n; }
    int SlotOfIndex(int index) const {
        int seen = 0;
        for (int s = 0; s < kSlots; ++s) if (m_connected[s]) { if (seen == index) return s; ++seen; }
        return -1;
    }
    int IndexOfSlot(int slot) const {
        int seen = 0;
        for (int s = 0; s < slot && s < kSlots; ++s) seen += m_connected[s] ? 1 : 0;
        return seen;
    }
    bool Scanned() const { for (int s = 0; s < kSlots; ++s) if (m_everProbed[s]) return true; return false; }
private:
    void Set(int s, bool now, std::vector<Change>& out) {
        if (now == m_connected[s]) return;
        m_connected[s] = now;
        if (now) { m_instance[s] = ++m_nextInstance; out.push_back({true, s, m_instance[s], IndexOfSlot(s)}); }
        else out.push_back({false, s, m_instance[s], IndexOfSlot(s)});
    }
    uint64_t m_interval;
    bool m_connected[kSlots] = {false, false, false, false};
    bool m_everProbed[kSlots] = {false, false, false, false};
    uint64_t m_lastProbe[kSlots] = {0, 0, 0, 0};
    int m_instance[kSlots] = {0, 0, 0, 0};
    int m_nextInstance = 0;
};

} // namespace okay::platform::win32
