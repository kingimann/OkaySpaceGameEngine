#pragma once
// okay::platform::wl: the parts of the own Windows backend that are plain logic, with no Windows
// header and no operating-system call, so they are compiled into the Linux unit tests
// (tests/test_platform_win32_logic.cpp) and checked there, and into OwnWin32.cpp, which feeds
// them the numbers the Win32 API gives.
//
// Written from Microsoft's public documentation of the messages and structures involved
// (WM_KEYDOWN lParam layout, set-1 keyboard scan codes, CF_UNICODETEXT, XINPUT_GAMEPAD,
// GetDoubleClickTime, USER_DEFAULT_SCREEN_DPI, QueryPerformanceCounter), the Unicode standard
// (UTF-8 and UTF-16 encoding forms) and general knowledge. No code, table or comment was taken
// from SDL, GLFW, Dear ImGui or any other project.
//
// Windows threading rule (CLAUDE.md rule 21a): nothing here uses the standard library's thread, lock,
// condition or once-only facilities (MinGW's win32 thread model cannot compile them).
#include "okay/Platform/Platform.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace okay::platform::wl {

// ---- Keyboard ----
// Windows puts the keyboard's set-1 make code in bits 16..23 of the WM_KEYDOWN / WM_KEYUP lParam and
// the "extended key" flag (the E0 prefix on the wire) in bit 24.

struct KeyLParam {
    int repeatCount = 1;     // bits 0..15
    int scan = 0;            // bits 16..23
    bool extended = false;   // bit 24
    bool wasDown = false;    // bit 30: the key was already down (auto-repeat)
    bool released = false;   // bit 31: transition state, 1 on key up
};
inline KeyLParam DecodeKeyLParam(std::uintptr_t lp) {
    KeyLParam k;
    k.repeatCount = (int)(lp & 0xFFFFu);
    k.scan = (int)((lp >> 16) & 0xFFu);
    k.extended = (lp & (std::uintptr_t(1) << 24)) != 0;
    k.wasDown = (lp & (std::uintptr_t(1) << 30)) != 0;
    k.released = (lp & (std::uintptr_t(1) << 31)) != 0;
    return k;
}

/// Set-1 make code -> physical key (the USB HID numbering of `Key`). Unknown = Key::Unknown.
/// `extended` is the lParam bit 24. The two fake Shift codes Windows inserts around the extended
/// cursor block when Num Lock is on (extended 0x2A and 0x36) are Key::Unknown on purpose.
inline Key KeyFromScan(int scan, bool extended) {
    // Index = make code 0x00..0x58 on the main block (no prefix). Values are HID usage numbers.
    static const std::uint16_t kMain[0x59] = {
        // 0x00
        0, 41, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 45, 46, 42, 43,
        // 0x10: Q W E R T Y U I O P [ ] Enter LCtrl A S
        20, 26, 8, 21, 23, 28, 24, 12, 18, 19, 47, 48, 40, 224, 4, 22,
        // 0x20: D F G H J K L ; ' ` LShift \ Z X C V
        7, 9, 10, 11, 13, 14, 15, 51, 52, 53, 225, 49, 29, 27, 6, 25,
        // 0x30: B N M , . / RShift Kp* LAlt Space Caps F1..F5
        5, 17, 16, 54, 55, 56, 229, 85, 226, 44, 57, 58, 59, 60, 61, 62,
        // 0x40: F6..F10 NumLock ScrollLock Kp7 Kp8 Kp9 Kp- Kp4 Kp5 Kp6 Kp+ Kp1
        63, 64, 65, 66, 67, 83, 71, 95, 96, 97, 86, 92, 93, 94, 87, 89,
        // 0x50: Kp2 Kp3 Kp0 Kp. - - (ISO key) F11 F12
        90, 91, 98, 99, 0, 0, 0, 68, 69};
    if (extended) {
        switch (scan) {
        case 0x1C: return Key::KpEnter;
        case 0x1D: return Key::RCtrl;
        case 0x35: return Key::KpDivide;
        case 0x37: return Key::PrintScreen;
        case 0x38: return Key::RAlt;       // AltGr arrives as a fake LCtrl followed by this
        case 0x45: return Key::NumLock;
        case 0x47: return Key::Home;
        case 0x48: return Key::Up;
        case 0x49: return Key::PageUp;
        case 0x4B: return Key::Left;
        case 0x4D: return Key::Right;
        case 0x4F: return Key::End;
        case 0x50: return Key::Down;
        case 0x51: return Key::PageDown;
        case 0x52: return Key::Insert;
        case 0x53: return Key::Delete;
        case 0x5B: return Key::LGui;
        case 0x5C: return Key::RGui;
        default: return Key::Unknown;
        }
    }
    if (scan == 0x45) return Key::Pause;   // Pause reports make code 0x45 without the extended flag
    return scan >= 0 && scan < 0x59 ? (Key)kMain[scan] : Key::Unknown;
}

/// The two-part code MapVirtualKey returns (MAPVK_VK_TO_VSC_EX): low byte = make code, 0xE0 in the
/// high byte for an extended key (0xE1 = the Pause sequence).
inline Key KeyFromScanEx(unsigned scanEx) {
    const unsigned prefix = (scanEx >> 8) & 0xFFu;
    if (prefix == 0xE1) return Key::Pause;
    return KeyFromScan((int)(scanEx & 0xFFu), prefix == 0xE0);
}

/// The reverse of KeyFromScan: the make code (and extended flag) that produces `k`. -1 when no
/// Windows key makes it. Built by searching the forward table, so the two cannot disagree.
inline int ScanFromKey(Key k, bool* extended = nullptr) {
    if (k == Key::Unknown) return -1;
    for (int ext = 0; ext < 2; ++ext)
        for (int s = 1; s < 0x59; ++s)
            if (KeyFromScan(s, ext != 0) == k) {
                if (k == Key::Pause && ext == 0 && s == 0x45) { if (extended) *extended = false; return s; }
                if (extended) *extended = ext != 0;
                return s;
            }
    return -1;
}

/// What a window procedure should do with WM_SYSKEYDOWN / WM_SYSKEYUP for virtual key `vk`: only
/// Alt+F4 (close) and Alt+Space (system menu) go on to DefWindowProc. Anything else (a bare Alt, F10,
/// Alt+Enter) would make Windows enter its menu loop or beep, which freezes or interrupts a game.
inline bool PassSysKeyToDefault(unsigned vk) { return vk == 0x73 /*VK_F4*/ || vk == 0x20 /*VK_SPACE*/; }

/// Which keys are down, as seen from the window's key messages. Cleared of everything on focus loss,
/// which also reports the keys to release so the application does not keep a stuck key.
class KeyState {
public:
    void Down(Key k) { if ((unsigned)k < (unsigned)kKeyCount) m_down[(unsigned)k] = true; }
    void Up(Key k) { if ((unsigned)k < (unsigned)kKeyCount) m_down[(unsigned)k] = false; }
    bool IsDown(Key k) const { return (unsigned)k < (unsigned)kKeyCount && m_down[(unsigned)k]; }
    /// Marks every key up and returns the ones that were down (ascending key number).
    std::vector<Key> ReleaseAll() {
        std::vector<Key> r;
        for (unsigned i = 0; i < (unsigned)kKeyCount; ++i)
            if (m_down[i]) { m_down[i] = false; r.push_back((Key)i); }
        return r;
    }
private:
    bool m_down[kKeyCount] = {};
};

// ---- Text --------------------------------------------------------------------------------------
// wchar_t is 32 bits on Linux and 16 on Windows, so the logic works on char16_t.

/// UTF-8 -> UTF-16. Bytes that are not well-formed UTF-8 (stray continuation bytes, overlong forms,
/// surrogates, code points past U+10FFFF, truncated sequences) each become one U+FFFD; nothing is dropped
/// silently and nothing reads past the end.
inline std::u16string Utf8ToUtf16(const std::string& s) {
    std::u16string out;
    out.reserve(s.size());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { out.push_back((char16_t)c); ++i; continue; }
        int len = 0;
        std::uint32_t cp = 0, minCp = 0;
        if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1Fu; minCp = 0x80; }
        else if (c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0Fu; minCp = 0x800; }
        else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07u; minCp = 0x10000; }
        if (len == 0) { out.push_back(0xFFFD); ++i; continue; }
        std::size_t j = 1;
        bool ok = true;
        for (; j < (std::size_t)len; ++j) {
            if (i + j >= n) { ok = false; break; }
            const unsigned char cc = (unsigned char)s[i + j];
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (!ok || cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            out.push_back(0xFFFD);
            i += ok ? (std::size_t)len : (j > 1 ? j : 1);   // skip the bytes that were part of the bad sequence
            continue;
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back((char16_t)(0xD800 + (cp >> 10)));
            out.push_back((char16_t)(0xDC00 + (cp & 0x3FF)));
        } else out.push_back((char16_t)cp);
        i += (std::size_t)len;
    }
    return out;
}

inline void AppendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) out.push_back((char)cp);
    else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

/// UTF-16 -> UTF-8. An unpaired surrogate becomes U+FFFD.
inline std::string Utf16ToUtf8(const char16_t* w, std::size_t n) {
    std::string out;
    if (!w) return out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t u = w[i];
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 1 < n && w[i + 1] >= 0xDC00 && w[i + 1] <= 0xDFFF) {
                AppendUtf8(out, 0x10000 + ((u - 0xD800) << 10) + (w[i + 1] - 0xDC00));
                ++i;
            } else AppendUtf8(out, 0xFFFD);
        } else if (u >= 0xDC00 && u <= 0xDFFF) AppendUtf8(out, 0xFFFD);
        else AppendUtf8(out, u);
    }
    return out;
}
inline std::string Utf16ToUtf8(const std::u16string& w) { return Utf16ToUtf8(w.data(), w.size()); }

/// Length of a UTF-16 string that may lack its terminator: stops at the first 0 or after `maxUnits`.
inline std::size_t BoundedLength16(const char16_t* w, std::size_t maxUnits) {
    std::size_t n = 0;
    while (w && n < maxUnits && w[n] != 0) ++n;
    return n;
}

/// Windows clipboard text uses CR LF line ends. Every lone LF becomes CR LF; an existing CR LF stays.
inline std::string ToClipboardNewlines(const std::string& s) {
    std::string out;
    out.reserve(s.size() + s.size() / 16);
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n' && (i == 0 || s[i - 1] != '\r')) out.push_back('\r');
        out.push_back(s[i]);
    }
    return out;
}
/// The reverse: every CR LF becomes LF. A CR on its own is kept.
inline std::string FromClipboardNewlines(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
        out.push_back(s[i]);
    }
    return out;
}

/// Turns the stream of WM_CHAR UTF-16 code units into UTF-8 text events. Surrogate pairs arrive as two
/// messages; control characters (Backspace, Tab, Enter, Escape and the Ctrl+letter codes), DEL and the
/// C1 range are not text and are dropped. A high surrogate that is not followed by a low one, or a low
/// one with no high one before it, produces U+FFFD.
class CharAssembler {
public:
    /// Feed one code unit; returns the UTF-8 text it completes ("" when nothing yet, or not text).
    std::string Push(std::uint16_t unit) {
        std::string out;
        if (m_high) {
            const std::uint16_t h = m_high;
            m_high = 0;
            if (unit >= 0xDC00 && unit <= 0xDFFF) {
                AppendUtf8(out, 0x10000 + ((std::uint32_t)(h - 0xD800) << 10) + (unit - 0xDC00));
                return out;
            }
            AppendUtf8(out, 0xFFFD);   // the high surrogate had no partner; then handle `unit` normally
        }
        if (unit >= 0xD800 && unit <= 0xDBFF) { m_high = unit; return out; }
        if (unit >= 0xDC00 && unit <= 0xDFFF) { AppendUtf8(out, 0xFFFD); return out; }
        if (unit < 0x20 || unit == 0x7F || (unit >= 0x80 && unit < 0xA0)) return out;
        AppendUtf8(out, unit);
        return out;
    }
    void Reset() { m_high = 0; }
private:
    std::uint16_t m_high = 0;
};

// ---- Time --------------------------------------------------------------------------------------
/// QueryPerformanceCounter ticks -> milliseconds without overflowing 64 bits for any frequency.
inline std::uint64_t TicksToMs(std::uint64_t ticks, std::uint64_t freq) {
    if (freq == 0) return 0;
    return (ticks / freq) * 1000u + (ticks % freq) * 1000u / freq;
}

// ---- DPI and scale -----------------------------------------------------------------------------
constexpr unsigned kBaseDpi = 96;   // USER_DEFAULT_SCREEN_DPI: the DPI at which one unit is one pixel

/// Content scale for a DPI (96 -> 1.0, 144 -> 1.5, 192 -> 2.0). 0 means "unknown" and gives 1.0.
/// Clamped to 0.25 .. 8 so a garbage DPI cannot produce a zero or huge window.
inline float ScaleFromDpi(unsigned dpi) {
    if (dpi == 0) return 1.0f;
    float s = (float)dpi / (float)kBaseDpi;
    return s < 0.25f ? 0.25f : (s > 8.0f ? 8.0f : s);
}
/// `v` device-independent units to pixels at `dpi`, rounded half away from zero (like MulDiv).
inline int ScaleToPixels(int v, unsigned dpi) {
    if (dpi == 0) return v;
    const long long num = (long long)v * (long long)dpi;
    const long long half = kBaseDpi / 2;
    return (int)(num >= 0 ? (num + half) / kBaseDpi : -((-num + half) / kBaseDpi));
}
/// The inverse: pixels at `dpi` to device-independent units.
inline int ScaleFromPixels(int px, unsigned dpi) {
    if (dpi == 0) return px;
    const long long num = (long long)px * (long long)kBaseDpi;
    const long long half = (long long)dpi / 2;
    return (int)(num >= 0 ? (num + half) / (long long)dpi : -((-num + half) / (long long)dpi));
}
/// A client size kept at the same size in device-independent units when the DPI changes.
inline void ResizeForDpiChange(int w, int h, unsigned oldDpi, unsigned newDpi, int& outW, int& outH) {
    if (oldDpi == 0 || newDpi == 0) { outW = w; outH = h; return; }
    outW = (int)(((long long)w * newDpi + oldDpi / 2) / oldDpi);
    outH = (int)(((long long)h * newDpi + oldDpi / 2) / oldDpi);
    if (outW < 1) outW = 1;
    if (outH < 1) outH = 1;
}
/// Top-left of a `w` x `h` window centred in a `screenW` x `screenH` work area at (`left`, `top`),
/// never off the top-left of the screen.
inline void CenteredOrigin(int left, int top, int screenW, int screenH, int w, int h, int& x, int& y) {
    x = left + (screenW - w) / 2;
    y = top + (screenH - h) / 2;
    if (x < left) x = left;
    if (y < top) y = top;
}

// ---- Mouse clicks ------------------------------------------------------------------------------
/// Counts consecutive clicks (1 = single, 2 = double, 3 = triple ...) the way Windows defines a double
/// click: the same button, within `maxIntervalMs` of the last press, and within half the double-click
/// rectangle (GetSystemMetrics SM_CXDOUBLECLK / SM_CYDOUBLECLK, 4 pixels by default) of it.
class ClickTracker {
public:
    ClickTracker(std::uint32_t maxIntervalMs = 500, int rectW = 4, int rectH = 4)
        : m_interval(maxIntervalMs), m_w(rectW), m_h(rectH) {}
    void Configure(std::uint32_t maxIntervalMs, int rectW, int rectH) { m_interval = maxIntervalMs; m_w = rectW; m_h = rectH; Reset(); }
    int Press(int button, int x, int y, std::uint32_t timeMs) {
        const std::uint32_t dt = timeMs - m_time;   // wrap-safe
        const int dx = x - m_x < 0 ? m_x - x : x - m_x;
        const int dy = y - m_y < 0 ? m_y - y : y - m_y;
        if (m_count > 0 && button == m_button && dt <= m_interval && dx <= m_w / 2 && dy <= m_h / 2) {
            if (m_count < 255) ++m_count;
        } else m_count = 1;
        m_button = button; m_x = x; m_y = y; m_time = timeMs;
        return m_count;
    }
    void Reset() { m_count = 0; }
private:
    std::uint32_t m_interval;
    int m_w, m_h;
    int m_count = 0, m_button = 0, m_x = 0, m_y = 0;
    std::uint32_t m_time = 0;
};

// ---- Event queue -------------------------------------------------------------------------------
/// The queue between the window procedure (which only appends) and PollEvent (which hands events to
/// the application one by one). Bounded, so an application that stops polling cannot grow it without
/// limit, and it merges the events that only carry "the latest value":
///   * a MouseMove right after a MouseMove of the same window becomes one event: the newest position
///     and the summed motion (dx, dy), so relative mouse input loses nothing;
///   * a WindowResized right after a WindowResized of the same window keeps the newest size.
/// When full it first throws away the oldest mouse move; then the oldest event that is not a Quit,
/// WindowClose, key up or button up; and only then refuses the new event.
class EventQueue {
public:
    explicit EventQueue(std::size_t capacity = 8192) : m_cap(capacity < 4 ? 4 : capacity) {}
    void Push(const Event& e) {
        if (!m_q.empty()) {
            Event& last = m_q.back();
            if (e.type == EventType::MouseMove && last.type == EventType::MouseMove && last.windowId == e.windowId) {
                last.x = e.x; last.y = e.y; last.dx += e.dx; last.dy += e.dy; last.timestampMs = e.timestampMs;
                return;
            }
            if (e.type == EventType::WindowResized && last.type == EventType::WindowResized && last.windowId == e.windowId) {
                last.width = e.width; last.height = e.height; last.timestampMs = e.timestampMs;
                return;
            }
        }
        if (m_q.size() >= m_cap && !MakeRoom()) { ++m_dropped; return; }
        m_q.push_back(e);
    }
    bool Pop(Event& out) {
        if (m_q.empty()) return false;
        out = std::move(m_q.front());
        m_q.pop_front();
        return true;
    }
    std::size_t Size() const { return m_q.size(); }
    bool Empty() const { return m_q.empty(); }
    void Clear() { m_q.clear(); }
    /// Events refused or thrown away because the queue was full (all time).
    std::size_t Dropped() const { return m_dropped; }
private:
    static bool Precious(const Event& e) {
        return e.type == EventType::Quit || e.type == EventType::WindowClose || e.type == EventType::KeyUp ||
               e.type == EventType::MouseButtonUp || e.type == EventType::GamepadAdded || e.type == EventType::GamepadRemoved;
    }
    bool MakeRoom() {
        for (auto it = m_q.begin(); it != m_q.end(); ++it)
            if (it->type == EventType::MouseMove) { m_q.erase(it); ++m_dropped; return true; }
        for (auto it = m_q.begin(); it != m_q.end(); ++it)
            if (!Precious(*it)) { m_q.erase(it); ++m_dropped; return true; }
        return false;   // nothing may go: refuse the new event
    }
    std::size_t m_cap;
    std::size_t m_dropped = 0;
    std::deque<Event> m_q;
};

// ---- XInput game controllers -------------------------------------------------------------------
// Values from the documented XINPUT_GAMEPAD structure (xinput.h); OwnWin32.cpp static_asserts them
// against the real header when it is built for Windows.
namespace xinput {
constexpr std::uint16_t kDpadUp = 0x0001, kDpadDown = 0x0002, kDpadLeft = 0x0004, kDpadRight = 0x0008;
constexpr std::uint16_t kStart = 0x0010, kBack = 0x0020, kLeftThumb = 0x0040, kRightThumb = 0x0080;
constexpr std::uint16_t kLeftShoulder = 0x0100, kRightShoulder = 0x0200;
constexpr std::uint16_t kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;
constexpr int kMaxPads = 4;   // XUSER_MAX_COUNT
} // namespace xinput

/// Whether `b` is pressed in an XINPUT_GAMEPAD::wButtons mask. XInput has no Guide button here.
inline bool PadButtonDown(std::uint16_t wButtons, GamepadButton b) {
    std::uint16_t m = 0;
    switch (b) {
    case GamepadButton::A: m = xinput::kA; break;
    case GamepadButton::B: m = xinput::kB; break;
    case GamepadButton::X: m = xinput::kX; break;
    case GamepadButton::Y: m = xinput::kY; break;
    case GamepadButton::Back: m = xinput::kBack; break;
    case GamepadButton::Guide: return false;
    case GamepadButton::Start: m = xinput::kStart; break;
    case GamepadButton::LeftStick: m = xinput::kLeftThumb; break;
    case GamepadButton::RightStick: m = xinput::kRightThumb; break;
    case GamepadButton::LeftShoulder: m = xinput::kLeftShoulder; break;
    case GamepadButton::RightShoulder: m = xinput::kRightShoulder; break;
    case GamepadButton::DpadUp: m = xinput::kDpadUp; break;
    case GamepadButton::DpadDown: m = xinput::kDpadDown; break;
    case GamepadButton::DpadLeft: m = xinput::kDpadLeft; break;
    case GamepadButton::DpadRight: m = xinput::kDpadRight; break;
    }
    return (wButtons & m) != 0;
}
/// XInput sticks are +y up; the seam is +y down. `-1 - v` maps -32768..32767 onto itself exactly
/// (plain negation would turn -32768 into 32768, which does not fit).
inline std::int16_t PadStickY(std::int16_t v) { return (std::int16_t)(-1 - (int)v); }
/// 0..255 trigger -> 0..32767, rounded; 255 is exactly 32767.
inline std::int16_t PadTrigger(std::uint8_t t) { return (std::int16_t)(((int)t * 32767 + 127) / 255); }
/// Seam rumble strength (0..65535) is XInput's motor speed unchanged.
inline std::uint16_t PadMotor(std::uint16_t v) { return v; }

/// Which of the four XInput slots hold a controller, turned into added / removed events. Instance ids
/// are never reused: each new connection of a slot gets the next number (like a joystick instance id).
class PadSlotTracker {
public:
    struct Change { bool added; int slot; int index; int instanceId; };
    /// Feed the connected-slot bitmask (bit n = slot n); returns what changed since the last call.
    /// `index` of an added pad is its position among the connected slots after the change (what
    /// Backend::OpenGamepad takes); `instanceId` is the id its Gamepad will report.
    std::vector<Change> Update(unsigned mask) {
        std::vector<Change> out;
        mask &= (1u << xinput::kMaxPads) - 1u;
        for (int s = 0; s < xinput::kMaxPads; ++s) {
            const bool was = (m_mask >> s) & 1u, now = (mask >> s) & 1u;
            if (was && !now) out.push_back({false, s, -1, m_instance[s]});
            if (!was && now) { m_instance[s] = ++m_nextInstance; out.push_back({true, s, 0, m_instance[s]}); }
        }
        m_mask = mask;
        for (auto& c : out) if (c.added) c.index = IndexOfSlot(c.slot);
        return out;
    }
    unsigned Mask() const { return m_mask; }
    int Count() const { int n = 0; for (int s = 0; s < xinput::kMaxPads; ++s) n += (m_mask >> s) & 1u; return n; }
    /// Slot of the index-th connected controller, -1 when there are not that many.
    int SlotOfIndex(int index) const {
        int seen = 0;
        for (int s = 0; s < xinput::kMaxPads; ++s)
            if ((m_mask >> s) & 1u) { if (seen == index) return s; ++seen; }
        return -1;
    }
    /// Position of `slot` among the connected ones, -1 when it is not connected.
    int IndexOfSlot(int slot) const {
        if (slot < 0 || slot >= xinput::kMaxPads || !((m_mask >> slot) & 1u)) return -1;
        int idx = 0;
        for (int s = 0; s < slot; ++s) idx += (m_mask >> s) & 1u;
        return idx;
    }
    int InstanceOfSlot(int slot) const { return slot >= 0 && slot < xinput::kMaxPads ? m_instance[slot] : 0; }
private:
    unsigned m_mask = 0;
    int m_instance[xinput::kMaxPads] = {0, 0, 0, 0};
    int m_nextInstance = 0;
};

/// "Is it time yet" for work that must not run every frame: XInputGetState on an empty slot is slow
/// (documented), so empty slots are looked at about once a second.
class IntervalGate {
public:
    explicit IntervalGate(std::uint64_t intervalMs = 1000) : m_interval(intervalMs) {}
    bool Due(std::uint64_t nowMs) {
        if (m_first || nowMs - m_last >= m_interval) { m_first = false; m_last = nowMs; return true; }
        return false;
    }
private:
    std::uint64_t m_interval, m_last = 0;
    bool m_first = true;
};

/// XInputSetState keeps vibrating until told to stop, so a timed rumble needs a clock: Set remembers
/// when each slot's motors must stop, Expired returns the slots whose time is up (and forgets them).
class RumbleSchedule {
public:
    void Set(int slot, std::uint16_t lo, std::uint16_t hi, std::uint32_t ms, std::uint64_t nowMs) {
        if (slot < 0 || slot >= xinput::kMaxPads) return;
        if ((lo == 0 && hi == 0) || ms == 0) { m_active[slot] = false; return; }
        m_active[slot] = true;
        m_until[slot] = nowMs + ms;
    }
    unsigned Expired(std::uint64_t nowMs) {
        unsigned mask = 0;
        for (int s = 0; s < xinput::kMaxPads; ++s)
            if (m_active[s] && nowMs >= m_until[s]) { m_active[s] = false; mask |= 1u << s; }
        return mask;
    }
    bool Active(int slot) const { return slot >= 0 && slot < xinput::kMaxPads && m_active[slot]; }
    void Cancel(int slot) { if (slot >= 0 && slot < xinput::kMaxPads) m_active[slot] = false; }
private:
    bool m_active[xinput::kMaxPads] = {false, false, false, false};
    std::uint64_t m_until[xinput::kMaxPads] = {0, 0, 0, 0};
};

} // namespace okay::platform::wl
