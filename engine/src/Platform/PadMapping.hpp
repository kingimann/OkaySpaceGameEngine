#pragma once
// Game controller mapping database (the SDL_GameControllerDB text format) and the maths that turns a raw
// joystick (numbered buttons, axes, hats) into our standard pad (okay::platform::Gamepad names).
// Pure logic: no device, no OS. Own code, not the SDL parser. Format, one controller per line:
//   <32 hex GUID>,<name>,a:b0,b:b1,leftx:a0,lefty:a1,dpup:h0.1,lefttrigger:a4,platform:Linux,
// A binding value is bN (button), aN (axis), hN.M (hat N, bit mask M), with an optional leading '+' or '-'
// (only that half of the axis) and an optional trailing '~' (inverted). Unknown keys are ignored.
#include "okay/Platform/Platform.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay::platform::pad {

struct Guid {
    std::array<uint8_t, 16> b{};
    bool operator==(const Guid& o) const { return b == o.b; }
    bool IsZero() const { for (uint8_t v : b) if (v) return false; return true; }
};

/// 32 hex digits (either case) -> Guid. False for any other length or a non-hex character.
inline bool ParseGuid(const std::string& s, Guid& out) {
    if (s.size() != 32) return false;
    Guid g;
    for (int i = 0; i < 16; i++) {
        int v[2];
        for (int k = 0; k < 2; k++) {
            const char c = s[i * 2 + k];
            if (c >= '0' && c <= '9') v[k] = c - '0';
            else if (c >= 'a' && c <= 'f') v[k] = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v[k] = c - 'A' + 10;
            else return false;
        }
        g.b[i] = (uint8_t)(v[0] * 16 + v[1]);
    }
    out = g;
    return true;
}

inline std::string GuidToString(const Guid& g) {
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (uint8_t v : g.b) { s += hex[v >> 4]; s += hex[v & 15]; }
    return s;
}

/// The GUID a Linux evdev pad gets: bus, vendor, product, version as little-endian 16-bit words at the
/// SDL2 positions (0, 4, 8, 12), the name checksum (bytes 2-3) as given.
inline Guid MakeLinuxGuid(uint16_t bus, uint16_t vendor, uint16_t product, uint16_t version, uint16_t nameCrc = 0) {
    Guid g;
    auto put = [&](int at, uint16_t v) { g.b[at] = (uint8_t)(v & 255); g.b[at + 1] = (uint8_t)(v >> 8); };
    put(0, bus); put(2, nameCrc); put(4, vendor); put(8, product); put(12, version);
    return g;
}

/// Same device ignoring the name checksum (bytes 2-3), the way a database made on another machine matches.
inline bool GuidSameDevice(const Guid& a, const Guid& b) {
    for (int i = 0; i < 16; i++) if ((i < 2 || i > 3) && a.b[i] != b.b[i]) return false;
    return true;
}

/// Where one pad control comes from on the raw device.
struct Binding {
    enum Kind : uint8_t { None, Button, Axis, Hat } kind = None;
    int index = 0;          // button, axis or hat number
    int hatMask = 0;        // Hat: 1 up, 2 right, 4 down, 8 left
    int half = 0;           // Axis: +1 only the positive half, -1 only the negative half, 0 whole axis
    bool invert = false;    // Axis: reversed
    bool Valid() const { return kind != None; }
};

constexpr int kButtonCount = 15;   // GamepadButton::A .. DpadRight
constexpr int kAxisCount = 6;      // GamepadAxis::LeftX .. TriggerRight

struct Mapping {
    Guid guid;
    std::string name;
    std::string platform;           // "" = any
    std::array<Binding, kButtonCount> button{};
    std::array<Binding, kAxisCount> axis{};
};

/// Parse one binding value such as "b3", "+a2", "-a1~", "a4~", "h0.4". Limits: numbers up to 255 (hats 0..255,
/// mask 1..15) so a hostile file cannot index far.
inline bool ParseBinding(const std::string& v, Binding& out) {
    size_t i = 0;
    Binding b;
    if (i < v.size() && (v[i] == '+' || v[i] == '-')) { b.half = v[i] == '+' ? 1 : -1; i++; }
    if (i >= v.size()) return false;
    const char kind = v[i++];
    auto num = [&](int& n) {
        if (i >= v.size() || v[i] < '0' || v[i] > '9') return false;
        int x = 0, digits = 0;
        while (i < v.size() && v[i] >= '0' && v[i] <= '9') { x = x * 10 + (v[i] - '0'); i++; if (++digits > 3) return false; }
        if (x > 255) return false;
        n = x; return true;
    };
    if (kind == 'b') { b.kind = Binding::Button; if (!num(b.index)) return false; b.half = 0; }
    else if (kind == 'a') { b.kind = Binding::Axis; if (!num(b.index)) return false; }
    else if (kind == 'h') {
        b.kind = Binding::Hat; b.half = 0;
        if (!num(b.index)) return false;
        if (i >= v.size() || v[i] != '.') return false;
        i++;
        if (!num(b.hatMask) || b.hatMask < 1 || b.hatMask > 15) return false;
    } else return false;
    if (i < v.size() && v[i] == '~') { if (b.kind == Binding::Axis) b.invert = true; i++; }
    if (i != v.size()) return false;
    out = b;
    return true;
}

inline int ButtonSlot(const std::string& key) {
    static const char* names[kButtonCount] = {"a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick",
                                              "leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright"};
    for (int i = 0; i < kButtonCount; i++) if (key == names[i]) return i;
    return -1;
}
inline int AxisSlot(const std::string& key) {
    static const char* names[kAxisCount] = {"leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger"};
    for (int i = 0; i < kAxisCount; i++) if (key == names[i]) return i;
    return -1;
}

/// Parse one database line. False for a comment, a blank line or a line without a valid GUID and name.
/// A binding value that does not parse leaves that control unmapped; the rest of the line still counts.
inline bool ParseMappingLine(const std::string& lineIn, Mapping& out) {
    std::string line = lineIn;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
    size_t s = 0;
    while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) s++;
    if (s >= line.size() || line[s] == '#') return false;
    std::vector<std::string> f;
    size_t pos = s;
    while (true) {
        const size_t c = line.find(',', pos);
        if (c == std::string::npos) { f.push_back(line.substr(pos)); break; }
        f.push_back(line.substr(pos, c - pos));
        pos = c + 1;
    }
    if (f.size() < 3) return false;
    Mapping m;
    if (!ParseGuid(f[0], m.guid)) return false;
    if (f[1].empty() || f[1].size() > 200) return false;
    m.name = f[1];
    for (size_t k = 2; k < f.size(); k++) {
        const size_t colon = f[k].find(':');
        if (colon == std::string::npos) continue;
        const std::string key = f[k].substr(0, colon), val = f[k].substr(colon + 1);
        if (key == "platform") { m.platform = val; continue; }
        const int bs = ButtonSlot(key), as = AxisSlot(key);
        if (bs < 0 && as < 0) continue;
        Binding b;
        if (!ParseBinding(val, b)) continue;
        if (bs >= 0) m.button[bs] = b; else m.axis[as] = b;
    }
    out = m;
    return true;
}

/// A set of mappings, keyed by GUID. Later lines replace earlier ones for the same GUID and platform.
class MappingDb {
public:
    /// Add every parsable line of `text`; returns how many were added. At most `maxLines` lines are read.
    size_t AddText(const std::string& text, size_t maxLines = 100000) {
        size_t added = 0, lines = 0, pos = 0;
        while (pos <= text.size() && lines < maxLines) {
            size_t e = text.find('\n', pos);
            if (e == std::string::npos) e = text.size();
            Mapping m;
            if (ParseMappingLine(text.substr(pos, e - pos), m)) { Add(m); added++; }
            lines++;
            pos = e + 1;
        }
        return added;
    }
    void Add(const Mapping& m) {
        auto& v = m_byGuid[GuidToString(m.guid)];
        for (auto& x : v) if (x.platform == m.platform) { x = m; return; }
        v.push_back(m);
    }
    size_t Size() const { size_t n = 0; for (auto& kv : m_byGuid) n += kv.second.size(); return n; }
    /// The mapping for `guid` on `platform` ("Linux", "Windows", "Mac OS X"): exact GUID first, then the same
    /// device ignoring the name checksum. An entry with no platform matches everywhere. Null when none.
    const Mapping* Find(const Guid& guid, const std::string& platform) const {
        auto pick = [&](const std::vector<Mapping>& v) -> const Mapping* {
            const Mapping* any = nullptr;
            for (auto& m : v) { if (m.platform == platform) return &m; if (m.platform.empty()) any = &m; }
            return any;
        };
        auto it = m_byGuid.find(GuidToString(guid));
        if (it != m_byGuid.end()) if (const Mapping* m = pick(it->second)) return m;
        for (auto& kv : m_byGuid) {
            if (kv.second.empty() || !GuidSameDevice(kv.second[0].guid, guid)) continue;
            if (const Mapping* m = pick(kv.second)) return m;
        }
        return nullptr;
    }
private:
    std::unordered_map<std::string, std::vector<Mapping>> m_byGuid;
};

/// What the raw device reports now. Axes are -32768..32767, hats are a 4-bit mask.
struct RawState {
    std::vector<int16_t> axes;
    std::vector<uint8_t> buttons;
    std::vector<uint8_t> hats;
};

struct PadState {
    std::array<int16_t, kAxisCount> axis{};
    std::array<bool, kButtonCount> button{};
};

namespace detail {
inline int Lerp(int v, int a, int b, int c, int d) {
    if (a == b) return c;
    const long long x = (long long)c + ((long long)(v - a) * (d - c)) / (b - a);
    const long long lo = c < d ? c : d, hi = c < d ? d : c;
    return (int)(x < lo ? lo : (x > hi ? hi : x));
}
// Is this binding "on" (for a button target)?
inline bool On(const Binding& b, const RawState& r) {
    switch (b.kind) {
    case Binding::Button: return (size_t)b.index < r.buttons.size() && r.buttons[b.index] != 0;
    case Binding::Hat: return (size_t)b.index < r.hats.size() && (r.hats[b.index] & b.hatMask) != 0;
    case Binding::Axis: {
        if ((size_t)b.index >= r.axes.size()) return false;
        int v = r.axes[b.index];
        if (b.invert) v = v == -32768 ? 32767 : -v;
        if (b.half < 0) return v < -16384;
        return v > 16384;
    }
    default: return false;
    }
}
} // namespace detail

/// Turn a raw state into the standard pad through `m`. Sticks come out -32768..32767, triggers 0..32767, an
/// axis fed by a button or hat is the end of its range while held. Indexes past what the device reports read as
/// not pressed / centred, so a wrong mapping cannot read out of range.
inline PadState Apply(const Mapping& m, const RawState& r) {
    PadState out;
    for (int i = 0; i < kButtonCount; i++) out.button[i] = detail::On(m.button[i], r);
    for (int i = 0; i < kAxisCount; i++) {
        const Binding& b = m.axis[i];
        const bool trigger = i >= (int)GamepadAxis::TriggerLeft;
        const int outLo = trigger ? 0 : -32768, outHi = 32767;
        if (b.kind == Binding::Axis) {
            if ((size_t)b.index >= r.axes.size()) { out.axis[i] = 0; continue; }
            int inA = -32768, inB = 32767;
            if (b.half > 0) { inA = 0; inB = 32767; }
            else if (b.half < 0) { inA = 0; inB = -32768; }
            if (b.invert) { const int t = inA; inA = inB; inB = t; }
            out.axis[i] = (int16_t)detail::Lerp(r.axes[b.index], inA, inB, outLo, outHi);
        } else if (b.kind != Binding::None) {
            out.axis[i] = (int16_t)(detail::On(b, r) ? outHi : (trigger ? 0 : -32768));
        } else {
            out.axis[i] = 0;
        }
    }
    return out;
}

} // namespace okay::platform::pad
