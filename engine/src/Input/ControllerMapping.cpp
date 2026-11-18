// Raw controller -> logical pad, the generic fallback and "Controller Setup".
// Written from the general idea of axis / button / hat game controllers; the default
// layout and every threshold are our own choices, checked by tests/test_controller_mapping.cpp.
#include "okay/Input/ControllerMapping.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace okay {
namespace {

std::int16_t Clamp16(int v) { return (std::int16_t)std::max(-32768, std::min(32767, v)); }
std::int16_t AxisAt(const RawPadState& r, int i) { return i >= 0 && i < (int)r.axes.size() ? r.axes[i] : (std::int16_t)0; }
bool ButtonAt(const RawPadState& r, int i) { return i >= 0 && i < (int)r.buttons.size() && r.buttons[i]; }
std::uint8_t HatAt(const RawPadState& r, int i) { return i >= 0 && i < (int)r.hats.size() ? r.hats[i] : (std::uint8_t)0; }
constexpr int kRestLow = -30000;   // an axis that idles below this is a trigger
constexpr int kPress = 16000;      // how far an axis must move to count as pressed or captured

bool Pressed(const PadSource& s, const RawPadState& r) {
    switch (s.kind) {
        case PadSource::Button: return ButtonAt(r, s.index);
        case PadSource::Hat: return (HatAt(r, s.index) & s.dir) != 0;
        case PadSource::AxisDir: { const int v = AxisAt(r, s.index); return (s.dir & 1) ? v > kPress : v < -kPress; }
        case PadSource::Axis: { int v = AxisAt(r, s.index); if (s.restLow) v = (v + 32768) / 2; if (s.invert) v = -v; return v > kPress; }
        default: return false;
    }
}

std::int16_t Value(const PadSource& s, const RawPadState& r, bool trigger) {
    switch (s.kind) {
        case PadSource::Axis: {
            int v = AxisAt(r, s.index);
            if (trigger) {
                if (s.restLow) v = (v + 32768) / 2;     // -32768..32767 -> 0..32767
                if (s.invert) v = 32767 - v;
                return Clamp16(std::max(0, v));
            }
            if (s.invert) v = -v;
            return Clamp16(v);
        }
        case PadSource::None: return 0;
        default: return Pressed(s, r) ? (std::int16_t)32767 : (std::int16_t)0;
    }
}

std::string Token(const PadSource& s) {
    char b[32];
    switch (s.kind) {
        case PadSource::Axis: std::snprintf(b, sizeof b, "a%d%s%s", s.index, s.invert ? "i" : "", s.restLow ? "l" : ""); break;
        case PadSource::Button: std::snprintf(b, sizeof b, "b%d", s.index); break;
        case PadSource::Hat: std::snprintf(b, sizeof b, "h%d.%d", s.index, s.dir); break;
        case PadSource::AxisDir: std::snprintf(b, sizeof b, "d%d.%d", s.index, s.dir); break;
        default: return "-";
    }
    return b;
}

bool ParseToken(const std::string& t, PadSource& out) {
    out = PadSource{};
    if (t == "-") return true;
    if (t.size() < 2) return false;
    const char* p = t.c_str() + 1;
    char* end = nullptr;
    const long idx = std::strtol(p, &end, 10);
    if (end == p || idx < 0 || idx > 255) return false;
    out.index = (std::uint8_t)idx;
    switch (t[0]) {
        case 'a':
            out.kind = PadSource::Axis;
            for (; *end; ++end) { if (*end == 'i') out.invert = true; else if (*end == 'l') out.restLow = true; else return false; }
            return true;
        case 'b': out.kind = PadSource::Button; return *end == 0;
        case 'h': case 'd': {
            out.kind = t[0] == 'h' ? PadSource::Hat : PadSource::AxisDir;
            if (*end != '.') return false;
            const long d = std::strtol(end + 1, &end, 10);
            if (*end || d < 0 || d > 15) return false;
            out.dir = (std::uint8_t)d;
            return true;
        }
        default: return false;
    }
}

} // namespace

const char* MapSourceName(MapSource s) {
    switch (s) {
        case MapSource::Builtin: return "built-in";
        case MapSource::Generic: return "generic";
        case MapSource::User: return "user";
        default: return "none";
    }
}

std::string ControllerMapping::Serialize() const {
    std::string o = "okpad1";
    for (int i = 0; i < kLogicalAxes; ++i) o += " a" + std::to_string(i) + "=" + Token(axis[i]);
    for (int i = 0; i < kLogicalButtons; ++i) o += " b" + std::to_string(i) + "=" + Token(button[i]);
    return o;
}

bool ControllerMapping::Parse(const std::string& text, ControllerMapping& out) {
    ControllerMapping m;
    std::size_t i = 0;
    auto next = [&](std::string& tok) {
        while (i < text.size() && text[i] == ' ') ++i;
        if (i >= text.size()) return false;
        const std::size_t s = i;
        while (i < text.size() && text[i] != ' ') ++i;
        tok = text.substr(s, i - s);
        return true;
    };
    std::string tok;
    if (!next(tok) || tok != "okpad1") return false;
    int seen = 0;
    while (next(tok)) {
        const std::size_t eq = tok.find('=');
        if (eq == std::string::npos || eq < 2) return false;
        const char kind = tok[0];
        char* end = nullptr;
        const long n = std::strtol(tok.c_str() + 1, &end, 10);
        if (end != tok.c_str() + eq) return false;
        PadSource src;
        if (!ParseToken(tok.substr(eq + 1), src)) return false;
        if (kind == 'a' && n >= 0 && n < kLogicalAxes) m.axis[n] = src;
        else if (kind == 'b' && n >= 0 && n < kLogicalButtons) m.button[n] = src;
        else return false;
        ++seen;
    }
    if (seen != kLogicalAxes + kLogicalButtons) return false;
    m.source = MapSource::User;
    out = m;
    return true;
}

int ControllerMapping::MappedCount() const {
    int n = 0;
    for (const PadSource& s : axis) n += s.kind != PadSource::None;
    for (const PadSource& s : button) n += s.kind != PadSource::None;
    return n;
}

bool ControllerMapping::operator==(const ControllerMapping& o) const {
    for (int i = 0; i < kLogicalAxes; ++i) if (!(axis[i] == o.axis[i])) return false;
    for (int i = 0; i < kLogicalButtons; ++i) if (!(button[i] == o.button[i])) return false;
    return source == o.source;
}

LogicalPadState ApplyMapping(const ControllerMapping& m, const RawPadState& raw) {
    LogicalPadState o;
    for (int i = 0; i < kLogicalAxes; ++i) o.axis[i] = Value(m.axis[i], raw, i >= LAxTriggerLeft);
    for (int i = 0; i < kLogicalButtons; ++i) o.button[i] = Pressed(m.button[i], raw);
    return o;
}

ControllerMapping BuildGenericMapping(const RawPadState& idle, const GamepadInfo*) {
    ControllerMapping m;
    m.source = MapSource::Generic;
    auto axis = [](int i, bool low) { PadSource s; s.kind = PadSource::Axis; s.index = (std::uint8_t)i; s.restLow = low; return s; };
    auto btn = [](int i) { PadSource s; s.kind = PadSource::Button; s.index = (std::uint8_t)i; return s; };
    const int na = (int)idle.axes.size(), nb = (int)idle.buttons.size(), nh = (int)idle.hats.size();
    // Axes: the ones resting at their minimum are triggers, the rest are sticks in order.
    std::vector<int> trig, stick;
    for (int i = 0; i < na; ++i) (idle.axes[i] <= kRestLow ? trig : stick).push_back(i);
    if (trig.empty() && na >= 6) { stick = {0, 1, 2, 3}; trig = {4, 5}; }
    const bool zeroRestTriggers = !trig.empty() && idle.axes[trig[0]] > kRestLow;
    for (int k = 0; k < 4 && k < (int)stick.size(); ++k) m.axis[LAxLeftX + k] = axis(stick[k], false);
    if (trig.size() >= 1) m.axis[LAxTriggerLeft] = axis(trig[0], !zeroRestTriggers);
    if (trig.size() >= 2) m.axis[LAxTriggerRight] = axis(trig[1], !zeroRestTriggers);
    // Buttons: A B X Y LB RB Back Start L3 R3 Guide.
    static const int kOrder[] = {LBtA, LBtB, LBtX, LBtY, LBtLeftShoulder, LBtRightShoulder, LBtBack, LBtStart,
                                 LBtLeftStick, LBtRightStick, LBtGuide};
    for (int i = 0; i < nb && i < (int)(sizeof(kOrder) / sizeof(kOrder[0])); ++i) m.button[kOrder[i]] = btn(i);
    // A pad with no analogue triggers but a button count that fits has them as buttons 6 / 7 in some layouts;
    // we do not guess that: Controller Setup is the way to fix it.
    // D-pad: the first hat, else four buttons after the usual eleven (up, down, left, right).
    if (nh >= 1) {
        const int dirs[4] = {HatUp, HatDown, HatLeft, HatRight};
        for (int k = 0; k < 4; ++k) { PadSource s; s.kind = PadSource::Hat; s.index = 0; s.dir = (std::uint8_t)dirs[k]; m.button[LBtDpadUp + k] = s; }
    } else if (nb >= 15) {
        for (int k = 0; k < 4; ++k) m.button[LBtDpadUp + k] = btn(11 + k);
    }
    return m;
}

std::string DeviceKey(std::uint16_t v, std::uint16_t p, int axes, int buttons, int hats) {
    char b[48];
    std::snprintf(b, sizeof b, "%04x:%04x:a%db%dh%d", (unsigned)v, (unsigned)p, axes < 0 ? 0 : axes, buttons < 0 ? 0 : buttons, hats < 0 ? 0 : hats);
    return b;
}

// ---- Controller Setup ------------------------------------------------------------------------------

namespace {
const int kStepButton[] = {  // for steps A .. DpadRight
    LBtA, LBtB, LBtX, LBtY, LBtLeftShoulder, LBtRightShoulder, LBtBack, LBtStart, LBtLeftStick, LBtRightStick,
    LBtDpadUp, LBtDpadDown, LBtDpadLeft, LBtDpadRight};
bool IsAxisStep(int s) { return s <= (int)ControllerSetup::Step::RightY; }
bool IsTriggerStep(int s) { return s == (int)ControllerSetup::Step::LeftTrigger || s == (int)ControllerSetup::Step::RightTrigger; }
bool IsDpadStep(int s) { return s >= (int)ControllerSetup::Step::DpadUp; }
PadSource& Slot(ControllerMapping& m, int step) {
    return step < (int)ControllerSetup::Step::A ? m.axis[step] : m.button[kStepButton[step - (int)ControllerSetup::Step::A]];
}
const PadSource& Slot(const ControllerMapping& m, int step) {
    return step < (int)ControllerSetup::Step::A ? m.axis[step] : m.button[kStepButton[step - (int)ControllerSetup::Step::A]];
}
}

void ControllerSetup::Begin(const RawPadState& idle, ControllerFamily family) {
    m_idle = idle;
    m_map = ControllerMapping{};
    m_map.source = MapSource::User;
    m_family = family;
    m_step = 0;
    m_active = true; m_finished = false; m_wantNeutral = false;
    m_last.clear();
}

bool ControllerSetup::Used(const PadSource& s) const {
    for (int i = 0; i < (int)Step::Count; ++i) {
        const PadSource& u = Slot(m_map, i);
        if (u.kind == PadSource::None || u.kind != s.kind || u.index != s.index) continue;
        if (s.kind == PadSource::Hat || s.kind == PadSource::AxisDir) { if (u.dir == s.dir) return true; }
        else return true;
    }
    return false;
}

bool ControllerSetup::Neutral(const RawPadState& now) const {
    for (int i = 0; i < (int)now.axes.size(); ++i)
        if (std::abs((int)now.axes[i] - (int)AxisAt(m_idle, i)) > 8000) return false;
    for (int i = 0; i < (int)now.buttons.size(); ++i) if (now.buttons[i] && !ButtonAt(m_idle, i)) return false;
    for (int i = 0; i < (int)now.hats.size(); ++i) if (now.hats[i] != HatAt(m_idle, i)) return false;
    return true;
}

void ControllerSetup::Advance() {
    m_wantNeutral = true;
    if (++m_step >= (int)Step::Count) { m_finished = true; m_active = false; }
}

void ControllerSetup::Skip() { if (m_active) { m_last = "skipped"; Advance(); m_wantNeutral = false; } }   // the next press counts at once
void ControllerSetup::Cancel() { m_active = false; m_wantNeutral = false; }
void ControllerSetup::Back() {
    if (!m_active || m_step == 0) return;
    --m_step;
    Slot(m_map, m_step) = PadSource{};
    m_wantNeutral = false;
    m_last = "back";
}

void ControllerSetup::Feed(const RawPadState& now) {
    if (!m_active) return;
    if (m_wantNeutral) { if (!Neutral(now)) return; m_wantNeutral = false; }
    const int step = m_step;
    PadSource got;
    char what[48] = "";
    const int na = (int)now.axes.size(), nb = (int)now.buttons.size(), nh = (int)now.hats.size();
    auto dev = [&](int i) { return (int)now.axes[i] - (int)AxisAt(m_idle, i); };
    if (IsAxisStep(step)) {
        for (int i = 0; i < na && got.kind == PadSource::None; ++i) {
            if (AxisAt(m_idle, i) <= kRestLow || std::abs(dev(i)) <= kPress) continue;
            PadSource s; s.kind = PadSource::Axis; s.index = (std::uint8_t)i; s.invert = dev(i) < 0;
            if (Used(s)) continue;
            got = s;
            std::snprintf(what, sizeof what, "axis %d%s", i, s.invert ? " (inverted)" : "");
        }
    } else if (IsTriggerStep(step)) {
        for (int i = 0; i < na && got.kind == PadSource::None; ++i) {
            if (dev(i) <= kPress) continue;
            PadSource s; s.kind = PadSource::Axis; s.index = (std::uint8_t)i; s.restLow = AxisAt(m_idle, i) <= kRestLow;
            if (Used(s)) continue;
            got = s;
            std::snprintf(what, sizeof what, "axis %d%s", i, s.restLow ? " (rests low)" : "");
        }
        for (int i = 0; i < nb && got.kind == PadSource::None; ++i) {
            if (!now.buttons[i] || ButtonAt(m_idle, i)) continue;
            PadSource s; s.kind = PadSource::Button; s.index = (std::uint8_t)i;
            if (Used(s)) continue;
            got = s; std::snprintf(what, sizeof what, "button %d (digital trigger)", i);
        }
    } else {
        for (int i = 0; i < nb && got.kind == PadSource::None; ++i) {
            if (!now.buttons[i] || ButtonAt(m_idle, i)) continue;
            PadSource s; s.kind = PadSource::Button; s.index = (std::uint8_t)i;
            if (Used(s)) continue;
            got = s; std::snprintf(what, sizeof what, "button %d", i);
        }
        if (got.kind == PadSource::None && IsDpadStep(step))
            for (int i = 0; i < nh && got.kind == PadSource::None; ++i) {
                const std::uint8_t d = (std::uint8_t)(now.hats[i] & ~HatAt(m_idle, i));
                if (!d) continue;
                PadSource s; s.kind = PadSource::Hat; s.index = (std::uint8_t)i; s.dir = d;
                if (Used(s)) continue;
                got = s; std::snprintf(what, sizeof what, "hat %d bits %d", i, (int)d);
            }
        if (got.kind == PadSource::None)   // a digital control reported on an axis (trigger as a button, d-pad as axes)
            for (int i = 0; i < na && got.kind == PadSource::None; ++i) {
                if (AxisAt(m_idle, i) <= kRestLow) { if (dev(i) <= 24000) continue; }
                else if (std::abs(dev(i)) <= 24000) continue;
                PadSource s; s.kind = PadSource::AxisDir; s.index = (std::uint8_t)i; s.dir = dev(i) > 0 ? 1 : 2;
                if (Used(s)) continue;
                got = s; std::snprintf(what, sizeof what, "axis %d as a button", i);
            }
    }
    if (got.kind == PadSource::None) return;
    Slot(m_map, step) = got;
    m_last = what;
    Advance();
}

std::string ControllerSetup::Prompt() const {
    if (m_finished) return "Done";
    if (!m_active) return "";
    const ControllerFamily f = m_family;
    auto nm = [&](int b) { return PadButtonName(b, f); };
    switch ((Step)m_step) {
        case Step::LeftX: return "Push the LEFT stick all the way to the RIGHT";
        case Step::LeftY: return "Push the LEFT stick all the way DOWN";
        case Step::RightX: return "Push the RIGHT stick all the way to the RIGHT";
        case Step::RightY: return "Push the RIGHT stick all the way DOWN";
        case Step::LeftTrigger: return "Pull the LEFT trigger (" + nm(Input::PadLT) + ") all the way";
        case Step::RightTrigger: return "Pull the RIGHT trigger (" + nm(Input::PadRT) + ") all the way";
        case Step::A: return "Press the BOTTOM face button (" + nm(Input::PadA) + ")";
        case Step::B: return "Press the RIGHT face button (" + nm(Input::PadB) + ")";
        case Step::X: return "Press the LEFT face button (" + nm(Input::PadX) + ")";
        case Step::Y: return "Press the TOP face button (" + nm(Input::PadY) + ")";
        case Step::LeftShoulder: return "Press the LEFT bumper (" + nm(Input::PadLB) + ")";
        case Step::RightShoulder: return "Press the RIGHT bumper (" + nm(Input::PadRB) + ")";
        case Step::Back: return "Press the BACK button (" + nm(Input::PadBack) + ")";
        case Step::Start: return "Press the START button (" + nm(Input::PadStart) + ")";
        case Step::LeftStick: return "CLICK the left stick in (" + nm(Input::PadLeftStick) + ")";
        case Step::RightStick: return "CLICK the right stick in (" + nm(Input::PadRightStick) + ")";
        case Step::DpadUp: return "Press the D-pad UP";
        case Step::DpadDown: return "Press the D-pad DOWN";
        case Step::DpadLeft: return "Press the D-pad LEFT";
        case Step::DpadRight: return "Press the D-pad RIGHT";
        default: return "";
    }
}

} // namespace okay
