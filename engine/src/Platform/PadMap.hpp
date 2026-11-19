#pragma once
// Standard pad layout (A/B/X/Y, bumpers, sticks, triggers, d-pad) for raw HID pads, and the small text file that
// says which raw input of which pad is which standard control. No Windows headers: tested on Linux by
// tests/test_platform_win32_pure.cpp. Used by the own Windows backend for pads that are not XInput devices.
//
// Own design (written from nothing but the idea "a table from raw input to standard control"). The file format is ours:
//
//   # a comment (also after a value)
//   [pad]                      starts one entry; the keys below belong to it
//   vid  = 054c                USB vendor id, hex (required)
//   pid  = 09cc                USB product id, hex (required)
//   name = Some Pad            free text, shown nowhere important (optional)
//   a = b0                     a standard control = a raw input
//   leftx = a0
//   lefty = -a1                a leading minus flips the direction
//   lefttrigger = a4           an axis on a trigger: full range -32768..32767 becomes 0..32767
//   righttrigger = a5+         "+" only the positive half, "-" only the negative half
//   dpup = h0.up               hat 0, direction up (up, right, down, left)
//   lefttrigger = b6           a button on a trigger reads 0 or 32767
//
// Standard controls: leftx lefty rightx righty lefttrigger righttrigger (axes) and a b x y back guide start leftstick
// rightstick leftshoulder rightshoulder dpup dpdown dpleft dpright (buttons). Raw inputs: aN (axis N), bN (button N),
// hN.dir (hat N), all numbered from 0 in the order the backend lists them (RawAxis / RawButton / RawHat). A later
// entry for the same vid and pid replaces an earlier one, so a user file read after a shipped one wins.
// Keys and words are not case sensitive. A line that cannot be read is skipped and reported, the rest still loads.
#include "okay/Platform/Platform.hpp"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace okay::platform::padmap {

constexpr int kAxisCount = 6;      // GamepadAxis
constexpr int kButtonCount = 15;   // GamepadButton

enum class Kind : uint8_t { None, Axis, Button, Hat };
enum class Half : uint8_t { Full, Positive, Negative };
struct Source {
    Kind kind = Kind::None;
    int index = 0;
    bool invert = false;
    Half half = Half::Full;
    uint8_t hatBit = 0;        // Hat: 1 up, 2 right, 4 down, 8 left (the RawHat bits)
};

struct PadMap {
    Source axes[kAxisCount];
    Source buttons[kButtonCount];
    bool Any() const {
        for (const Source& s : axes) if (s.kind != Kind::None) return true;
        for (const Source& s : buttons) if (s.kind != Kind::None) return true;
        return false;
    }
};

/// What the backend currently reads from a pad, in the RawAxis / RawButton / RawHat numbering.
struct RawView {
    const int16_t* axes = nullptr; int axisCount = 0;
    uint32_t buttons = 0; int buttonCount = 0;
    const uint8_t* hats = nullptr; int hatCount = 0;
};

namespace detail {
inline int16_t Neg(int v) { return (int16_t)(v <= -32768 ? 32767 : -v); }
// The value of an axis source as -32768..32767 after invert and half selection (Half::Full keeps the sign).
inline int AxisValue(const Source& s, const RawView& r) {
    if (s.index < 0 || s.index >= r.axisCount || !r.axes) return 0;
    int v = r.axes[s.index];
    if (s.invert) v = Neg(v);
    if (s.half == Half::Positive) v = v > 0 ? v : 0;
    else if (s.half == Half::Negative) v = v < 0 ? (v <= -32768 ? 32767 : -v) : 0;
    return v;
}
inline bool Pressed(const Source& s, const RawView& r) {
    switch (s.kind) {
        case Kind::Button: return s.index >= 0 && s.index < r.buttonCount && s.index < 32 && ((r.buttons >> s.index) & 1u);
        case Kind::Hat: return s.index >= 0 && s.index < r.hatCount && r.hats && (r.hats[s.index] & s.hatBit) != 0;
        case Kind::Axis: return AxisValue(s, r) > 16384;     // past a quarter of the way
        default: return false;
    }
}
} // namespace detail

/// A standard axis. Sticks: -32768..32767, +y down; triggers 0..32767. A source that is not there reads 0.
inline int16_t StandardAxis(const PadMap& m, GamepadAxis a, const RawView& r) {
    const int ai = (int)a;
    if (ai < 0 || ai >= kAxisCount) return 0;
    const Source& s = m.axes[ai];
    const bool trigger = a == GamepadAxis::TriggerLeft || a == GamepadAxis::TriggerRight;
    if (s.kind == Kind::None) return 0;
    if (s.kind == Kind::Axis) {
        if (s.index < 0 || s.index >= r.axisCount || !r.axes) return 0;   // the pad has no such axis
        const int v = detail::AxisValue(s, r);
        if (trigger) return (int16_t)(s.half == Half::Full ? (v + 32768) >> 1 : v);
        return (int16_t)v;
    }
    return detail::Pressed(s, r) ? (int16_t)32767 : (int16_t)0;
}
inline bool StandardButton(const PadMap& m, GamepadButton b, const RawView& r) {
    const int bi = (int)b;
    if (bi < 0 || bi >= kButtonCount) return false;
    return detail::Pressed(m.buttons[bi], r);
}

/// A guess for a pad nobody described, from how many axes, buttons and hats it has. The usual layout of a two-stick
/// pad: axes 0 and 1 the left stick, 2 and 3 the right stick, 4 and 5 the triggers; buttons 0..3 A B X Y, 4 and 5 the
/// bumpers, 6 Back, 7 Start, 8 and 9 the stick clicks, 10 Guide; hat 0 the d-pad. It is only a guess: a pad that
/// is laid out differently needs an entry in the map file.
inline PadMap GuessMap(int axisCount, int buttonCount, int hatCount) {
    PadMap m;
    auto ax = [&](GamepadAxis a, int i) { if (i < axisCount) { m.axes[(int)a].kind = Kind::Axis; m.axes[(int)a].index = i; } };
    auto bt = [&](GamepadButton b, int i) { if (i < buttonCount) { m.buttons[(int)b].kind = Kind::Button; m.buttons[(int)b].index = i; } };
    ax(GamepadAxis::LeftX, 0); ax(GamepadAxis::LeftY, 1);
    if (axisCount >= 4) { ax(GamepadAxis::RightX, 2); ax(GamepadAxis::RightY, 3); }
    if (axisCount >= 6) { ax(GamepadAxis::TriggerLeft, 4); ax(GamepadAxis::TriggerRight, 5); }
    const GamepadButton order[] = {GamepadButton::A, GamepadButton::B, GamepadButton::X, GamepadButton::Y, GamepadButton::LeftShoulder,
                                   GamepadButton::RightShoulder, GamepadButton::Back, GamepadButton::Start, GamepadButton::LeftStick,
                                   GamepadButton::RightStick, GamepadButton::Guide};
    for (int i = 0; i < 11; ++i) bt(order[i], i);
    if (hatCount >= 1) {
        const GamepadButton d[] = {GamepadButton::DpadUp, GamepadButton::DpadRight, GamepadButton::DpadDown, GamepadButton::DpadLeft};
        for (int i = 0; i < 4; ++i) { Source& s = m.buttons[(int)d[i]]; s.kind = Kind::Hat; s.index = 0; s.hatBit = (uint8_t)(1u << i); }
    }
    return m;
}

// ---- the text file ----------------------------------------------------------------------------------------------

struct Entry { uint16_t vid = 0, pid = 0; std::string name; PadMap map; };

class Database {
public:
    /// Read a file's text. Returns how many entries were added or replaced; `errors` (if given) gets one line per problem.
    int Parse(const std::string& text, std::vector<std::string>* errors = nullptr);
    const Entry* Find(uint16_t vid, uint16_t pid) const {
        for (const Entry& e : m_entries) if (e.vid == vid && e.pid == pid) return &e;
        return nullptr;
    }
    size_t Size() const { return m_entries.size(); }
private:
    std::vector<Entry> m_entries;
};

namespace detail {
inline std::string Lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return s; }
inline std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}
inline bool ParseInt(const std::string& s, size_t& i, int& out) {
    if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;
    long v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); if (v > 1000) return false; ++i; }
    out = (int)v;
    return true;
}
inline bool ParseHex16(const std::string& s, uint16_t& out) {
    if (s.empty() || s.size() > 6) return false;
    size_t i = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) i = 2;
    if (i >= s.size()) return false;
    uint32_t v = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        const int h = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (h < 0) return false;
        v = v * 16 + (uint32_t)h;
    }
    if (v > 0xFFFF) return false;
    out = (uint16_t)v;
    return true;
}
/// "-a1", "a4+", "b3", "h0.up". Returns false for anything else.
inline bool ParseSource(const std::string& in, Source& out) {
    const std::string s = Lower(Trim(in));
    size_t i = 0;
    Source r;
    if (i < s.size() && s[i] == '-') { r.invert = true; ++i; }
    if (i >= s.size()) return false;
    const char k = s[i++];
    if (k != 'a' && k != 'b' && k != 'h') return false;
    if (!ParseInt(s, i, r.index)) return false;
    if (k == 'a') {
        r.kind = Kind::Axis;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) { r.half = s[i] == '+' ? Half::Positive : Half::Negative; ++i; }
    } else if (k == 'b') {
        r.kind = Kind::Button;
        if (r.invert) return false;
    } else {
        r.kind = Kind::Hat;
        if (r.invert || i >= s.size() || s[i] != '.') return false;
        const std::string d = s.substr(i + 1);
        i = s.size();
        r.hatBit = d == "up" ? 1 : d == "right" ? 2 : d == "down" ? 4 : d == "left" ? 8 : 0;
        if (!r.hatBit) return false;
    }
    if (i != s.size()) return false;
    out = r;
    return true;
}
inline bool ControlIndex(const std::string& key, bool& isAxis, int& idx) {
    static const char* axes[kAxisCount] = {"leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger"};
    static const char* btns[kButtonCount] = {"a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder",
                                             "rightshoulder", "dpup", "dpdown", "dpleft", "dpright"};
    for (int i = 0; i < kAxisCount; ++i) if (key == axes[i]) { isAxis = true; idx = i; return true; }
    for (int i = 0; i < kButtonCount; ++i) if (key == btns[i]) { isAxis = false; idx = i; return true; }
    return false;
}
} // namespace detail

inline int Database::Parse(const std::string& text, std::vector<std::string>* errors) {
    if (text.size() > (1u << 20)) { if (errors) errors->push_back("file too big (over 1 MB), ignored"); return 0; }
    int added = 0, lineNo = 0;
    bool open = false, haveVid = false, havePid = false;
    Entry cur;
    auto err = [&](const std::string& m) { if (errors) errors->push_back("line " + std::to_string(lineNo) + ": " + m); };
    auto finish = [&]() {
        if (!open) return;
        open = false;
        if (!haveVid || !havePid) { if (errors) errors->push_back("entry before line " + std::to_string(lineNo) + ": needs vid and pid, skipped"); return; }
        for (Entry& e : m_entries) if (e.vid == cur.vid && e.pid == cur.pid) { e = cur; ++added; return; }
        if (m_entries.size() >= 4096) { if (errors) errors->push_back("too many entries (4096), rest skipped"); return; }
        m_entries.push_back(cur);
        ++added;
    };
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        ++lineNo;
        if (line.size() > 512) { err("line too long, skipped"); continue; }
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        line = detail::Trim(line);
        if (line.empty()) continue;
        if (detail::Lower(line) == "[pad]") { finish(); open = true; haveVid = havePid = false; cur = Entry(); continue; }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) { err("expected key = value"); continue; }
        if (!open) { err("key before the first [pad]"); continue; }
        const std::string key = detail::Lower(detail::Trim(line.substr(0, eq)));
        const std::string val = detail::Trim(line.substr(eq + 1));
        if (key == "vid") { if (detail::ParseHex16(val, cur.vid)) haveVid = true; else err("bad vid (hex up to ffff)"); continue; }
        if (key == "pid") { if (detail::ParseHex16(val, cur.pid)) havePid = true; else err("bad pid (hex up to ffff)"); continue; }
        if (key == "name") { cur.name = val.substr(0, 80); continue; }
        bool isAxis = false; int idx = 0;
        if (!detail::ControlIndex(key, isAxis, idx)) { err("unknown key '" + key + "'"); continue; }
        Source s;
        if (!detail::ParseSource(val, s)) { err("bad input '" + val + "' for " + key); continue; }
        if (isAxis) cur.map.axes[idx] = s; else cur.map.buttons[idx] = s;
    }
    finish();
    return added;
}

// ---- product name -------------------------------------------------------------------------------------------------

/// A product string from HidD_GetProductString, made safe to show: control characters become spaces, runs of spaces
/// collapse, the ends are trimmed, and it is cut to 63 bytes without splitting a UTF-8 character. Empty when nothing is left.
inline std::string CleanProductName(const std::string& in) {
    std::string o;
    bool space = true;
    for (unsigned char c : in) {
        if (c < 0x20 || c == 0x7F) { c = ' '; }
        if (c == ' ') { if (!space) o.push_back(' '); space = true; continue; }
        o.push_back((char)c);
        space = false;
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    if (o.size() > 63) {
        size_t n = 63;
        while (n > 0 && ((unsigned char)o[n] & 0xC0) == 0x80) --n;   // do not cut inside a character
        o.resize(n);
        while (!o.empty() && o.back() == ' ') o.pop_back();
    }
    return o;
}
/// The name when the product string is missing or empty.
inline std::string FallbackPadName(uint16_t vid, uint16_t pid) {
    char b[48]; std::snprintf(b, sizeof(b), "HID game controller %04X:%04X", vid, pid);
    return b;
}

} // namespace okay::platform::padmap
