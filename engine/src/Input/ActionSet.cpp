// Typed actions, composites, processors and control schemes (see ActionSet.hpp). Written from the general idea of
// action maps: our own value rules, text forms and thresholds, checked by tests/test_action_set.cpp.
#include "okay/Input/ActionSet.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Core/AccessibilitySettings.hpp"
#include "okay/Core/StringUtil.hpp"
#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace okay {
namespace {

float Comp(const InputValue& v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }
void SetComp(InputValue& v, int i, float f) { (i == 0 ? v.x : i == 1 ? v.y : v.z) = f; }
float Sgn(float f) { return f < 0 ? -1.0f : f > 0 ? 1.0f : 0.0f; }
std::string Num(float f) { char b[32]; std::snprintf(b, sizeof b, "%g", f); return b; }
std::vector<float> Nums(const std::string& s) {
    std::vector<float> o; std::size_t i = 0;
    while (i <= s.size()) {
        std::size_t j = s.find(',', i); if (j == std::string::npos) j = s.size();
        const std::string t = s.substr(i, j - i);
        if (!t.empty()) o.push_back((float)std::atof(t.c_str()));
        i = j + 1;
    }
    return o;
}

} // namespace

const char* ValueTypeName(ValueType t) {
    switch (t) { case ValueType::Axis1D: return "axis"; case ValueType::Vector2: return "vector2"; case ValueType::Vector3: return "vector3"; default: return "button"; }
}
bool ParseValueType(const std::string& s, ValueType& out) {
    if (s == "button") out = ValueType::Button; else if (s == "axis") out = ValueType::Axis1D;
    else if (s == "vector2") out = ValueType::Vector2; else if (s == "vector3") out = ValueType::Vector3; else return false;
    return true;
}
float InputValue::Length() const { return std::sqrt(x * x + y * y + z * z); }

// ---- processors -----------------------------------------------------------------------------------------------------

InputValue Processor::Apply(const InputValue& in, float dt, int slot) {
    InputValue v = in;
    switch (kind) {
        case Kind::Deadzone: {   // below `a` reads 0, the rest is stretched so `b` (default 1) reads 1; on the length
            const float lo = a, hi = b > a ? b : 1.0f;
            const float len = v.Length();
            if (len <= lo) return {};
            const float k = std::min(1.0f, (len - lo) / (hi - lo)) / len;
            return {v.x * k, v.y * k, v.z * k};
        }
        case Kind::Axial: {   // the same reshape on each component, so a forward stick with a small sideways bias does not strafe
            const float lo = a, hi = b > a ? b : 1.0f;
            auto one = [&](float x) {
                const float s = x < 0.0f ? -1.0f : 1.0f;
                const float m = std::fabs(x);
                if (m <= lo || !(hi > lo)) return 0.0f;
                return s * std::min(1.0f, (m - lo) / (hi - lo));
            };
            return {one(v.x), one(v.y), one(v.z)};
        }
        case Kind::Invert:
            if (mask & 1) v.x = -v.x;
            if (mask & 2) v.y = -v.y;
            if (mask & 4) v.z = -v.z;
            return v;
        case Kind::Scale: {   // one number scales everything; two or three scale x, y, z
            if (mask <= 1) return {v.x * a, v.y * a, v.z * a};
            return {v.x * a, v.y * b, mask >= 3 ? v.z * c : v.z};
        }
        case Kind::Normalize: {
            const float len = v.Length();
            return len > 0 ? InputValue{v.x / len, v.y / len, v.z / len} : InputValue{};
        }
        case Kind::Swizzle: {
            InputValue o;
            for (int i = 0; i < 3; ++i) { const int src = (mask >> (3 * i)) & 7; SetComp(o, i, Comp(v, src > 2 ? i : src)); }
            return o;
        }
        case Kind::Smooth: {   // exponential approach: `a` seconds to cover most of the way. Each slot remembers its own output.
            float* st = state;
            bool* pr = &primed;
            if (slot >= 1 && slot <= 4) { st = slotState[slot - 1]; pr = &slotPrimed[slot - 1]; }
            if (!*pr || a <= 0 || dt <= 0) { st[0] = v.x; st[1] = v.y; st[2] = v.z; *pr = true; return v; }
            const float k = 1.0f - std::exp(-dt / a);
            st[0] += (v.x - st[0]) * k; st[1] += (v.y - st[1]) * k; st[2] += (v.z - st[2]) * k;
            return {st[0], st[1], st[2]};
        }
        case Kind::Curve: {   // out = in ^ a keeping the sign; a vector keeps its direction and its length is curved
            const float p = a > 0 ? a : 1.0f;
            const float len = v.Length();
            if (len <= 0) return {};
            const float k = std::pow(std::min(len, 1.0f), p) / len * (len > 1.0f ? len : 1.0f);
            return {v.x * k, v.y * k, v.z * k};
        }
        case Kind::Clamp: {
            const float lo = a, hi = b > a ? b : a;
            return {std::clamp(v.x, lo, hi), std::clamp(v.y, lo, hi), std::clamp(v.z, lo, hi)};
        }
    }
    return v;
}

std::string Processor::ToString() const {
    switch (kind) {
        case Kind::Deadzone: return b > a ? "deadzone(" + Num(a) + "," + Num(b) + ")" : "deadzone(" + Num(a) + ")";
        case Kind::Axial: return b > a ? "axial(" + Num(a) + "," + Num(b) + ")" : "axial(" + Num(a) + ")";
        case Kind::Invert: { std::string s; if (mask & 1) s += 'x'; if (mask & 2) s += 'y'; if (mask & 4) s += 'z'; return "invert(" + s + ")"; }
        case Kind::Scale: return mask >= 3 ? "scale(" + Num(a) + "," + Num(b) + "," + Num(c) + ")" : mask == 2 ? "scale(" + Num(a) + "," + Num(b) + ")" : "scale(" + Num(a) + ")";
        case Kind::Normalize: return "normalize";
        case Kind::Swizzle: { std::string s; for (int i = 0; i < 3; ++i) { const int src = (mask >> (3 * i)) & 7; s += "xyz"[src > 2 ? i : src]; } return "swizzle(" + s + ")"; }
        case Kind::Smooth: return "smooth(" + Num(a) + ")";
        case Kind::Curve: return "curve(" + Num(a) + ")";
        case Kind::Clamp: return "clamp(" + Num(a) + "," + Num(b) + ")";
    }
    return "";
}

bool Processor::Parse(const std::string& text, Processor& out) {
    const std::size_t lp = text.find('(');
    const std::string name = text.substr(0, lp);
    std::string args;
    if (lp != std::string::npos) {
        if (text.back() != ')') return false;
        args = text.substr(lp + 1, text.size() - lp - 2);
    }
    const std::vector<float> n = Nums(args);
    Processor p;
    if (name == "deadzone") { if (n.empty() || n[0] < 0) return false; p.kind = Kind::Deadzone; p.a = n[0]; p.b = n.size() > 1 ? n[1] : 0; }
    else if (name == "axial") { if (n.empty() || n[0] < 0) return false; p.kind = Kind::Axial; p.a = n[0]; p.b = n.size() > 1 ? n[1] : 0; }
    else if (name == "invert") {
        p.kind = Kind::Invert; p.mask = 0;
        if (args.empty()) p.mask = 7;
        for (char ch : args) { if (ch == 'x') p.mask |= 1; else if (ch == 'y') p.mask |= 2; else if (ch == 'z') p.mask |= 4; else return false; }
    }
    else if (name == "scale") { if (n.empty()) return false; p.kind = Kind::Scale; p.a = n[0]; p.b = n.size() > 1 ? n[1] : 0; p.c = n.size() > 2 ? n[2] : 0; p.mask = (int)std::min<std::size_t>(n.size(), 3); }
    else if (name == "normalize") { p.kind = Kind::Normalize; }
    else if (name == "swizzle") {
        if (args.size() != 3) return false;
        p.kind = Kind::Swizzle; p.mask = 0;
        for (int i = 0; i < 3; ++i) { const char ch = args[(std::size_t)i]; if (ch != 'x' && ch != 'y' && ch != 'z') return false; p.mask |= (ch - 'x') << (3 * i); }
    }
    else if (name == "smooth") { if (n.empty() || n[0] < 0) return false; p.kind = Kind::Smooth; p.a = n[0]; }
    else if (name == "curve") { if (n.empty() || n[0] <= 0) return false; p.kind = Kind::Curve; p.a = n[0]; }
    else if (name == "clamp") { if (n.size() < 2 || n[1] < n[0]) return false; p.kind = Kind::Clamp; p.a = n[0]; p.b = n[1]; }
    else return false;
    out = p;
    return true;
}

bool ParseProcessors(const std::string& text, std::vector<Processor>& out) {
    std::vector<Processor> list;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == ';')) ++i;
        std::size_t j = i;
        while (j < text.size() && text[j] != ' ' && text[j] != ';') ++j;
        if (j > i) { Processor p; if (!Processor::Parse(text.substr(i, j - i), p)) return false; list.push_back(p); }
        i = j;
    }
    out = list;
    return true;
}
std::string ProcessorsToString(const std::vector<Processor>& list) {
    std::string s;
    for (const Processor& p : list) { if (!s.empty()) s += ' '; s += p.ToString(); }
    return s;
}

// ---- interactions -------------------------------------------------------------------------------------------------

const char* PhaseName(Phase p) {
    switch (p) { case Phase::Started: return "started"; case Phase::Performed: return "performed"; case Phase::Canceled: return "canceled"; default: return "waiting"; }
}
float Interaction::Progress() const {
    if (kind != Kind::Hold) return 0.0f;
    if (stage == 2) return 1.0f;
    return stage == 1 && a > 0 ? std::min(1.0f, t / a) : 0.0f;
}
std::string Interaction::ToString() const {
    switch (kind) {
        case Kind::Press: return "press";
        case Kind::Release: return "release";
        case Kind::PressRelease: return "press_release";
        case Kind::Hold: return "hold(" + Num(a) + ")";
        case Kind::Tap: return "tap(" + Num(a) + ")";
        case Kind::MultiTap: return "multitap(" + Num(a) + "," + Num(b) + "," + Num(c) + ")";
        case Kind::Repeat: return "repeat(" + Num(a) + "," + Num(b) + ")";
        case Kind::Chorded: return "chorded(" + arg + ")";
    }
    return "";
}
bool Interaction::Parse(const std::string& text, Interaction& out) {
    const std::size_t lp = text.find('(');
    const std::string name = text.substr(0, lp);
    std::string args;
    if (lp != std::string::npos) { if (text.back() != ')') return false; args = text.substr(lp + 1, text.size() - lp - 2); }
    const std::vector<float> n = Nums(args);
    Interaction i;
    if (name == "press") i.kind = Kind::Press;
    else if (name == "release") i.kind = Kind::Release;
    else if (name == "press_release") i.kind = Kind::PressRelease;
    else if (name == "hold") { i.kind = Kind::Hold; i.a = n.empty() ? 0.5f : n[0]; if (i.a <= 0) return false; }
    else if (name == "tap") { i.kind = Kind::Tap; i.a = n.empty() ? 0.2f : n[0]; if (i.a <= 0) return false; }
    else if (name == "multitap") {
        i.kind = Kind::MultiTap; i.a = n.size() > 0 ? n[0] : 2.0f; i.b = n.size() > 1 ? n[1] : 0.3f; i.c = n.size() > 2 ? n[2] : 0.2f;
        if (i.a < 2 || i.b <= 0 || i.c <= 0) return false;
    }
    else if (name == "repeat") { i.kind = Kind::Repeat; i.a = n.size() > 0 ? n[0] : 0.1f; i.b = n.size() > 1 ? n[1] : i.a; if (i.a <= 0 || i.b <= 0) return false; }
    else if (name == "chorded") { if (args.empty()) return false; i.kind = Kind::Chorded; i.arg = args; }
    else return false;
    out = i;
    return true;
}
bool ActionDef::AddInteraction(const std::string& text) {
    Interaction i;
    if (!Interaction::Parse(text, i)) return false;
    interactions.push_back(i);
    return true;
}
float ActionDef::Progress() const {
    for (const Interaction& i : interactions) if (i.kind == Interaction::Kind::Hold) return i.Progress();
    return 0.0f;
}

bool ControlScheme::Uses(Input::Device d) const {
    switch (d) { case Input::Device::Keyboard: return keyboard; case Input::Device::Mouse: return mouse; case Input::Device::Gamepad: return gamepad; default: return touch; }
}

// ---- binding builders -----------------------------------------------------------------------------------------------

static BindingSpec& Push(ActionDef& a, BindingSpec::Kind k, std::vector<std::string> parts, const std::string& group, const std::string& procs) {
    BindingSpec b; b.kind = k; b.parts = std::move(parts); b.group = group;
    ParseProcessors(procs, b.processors);
    a.bindings.push_back(std::move(b));
    return a.bindings.back();
}
BindingSpec& ActionDef::AddBinding(const std::string& p, const std::string& g, const std::string& pr) { return Push(*this, BindingSpec::Kind::Single, {p}, g, pr); }
BindingSpec& ActionDef::AddVector2(const std::string& u, const std::string& d, const std::string& l, const std::string& r, const std::string& g, const std::string& pr) { return Push(*this, BindingSpec::Kind::Vector2, {u, d, l, r}, g, pr); }
BindingSpec& ActionDef::AddVector3(const std::string& u, const std::string& d, const std::string& l, const std::string& r, const std::string& f, const std::string& bk, const std::string& g, const std::string& pr) { return Push(*this, BindingSpec::Kind::Vector3, {u, d, l, r, f, bk}, g, pr); }
BindingSpec& ActionDef::AddAxis(const std::string& n, const std::string& p, const std::string& g, const std::string& pr) { return Push(*this, BindingSpec::Kind::Axis1D, {n, p}, g, pr); }
BindingSpec& ActionDef::AddChord(const std::vector<std::string>& parts, const std::string& g, const std::string& pr) { return Push(*this, BindingSpec::Kind::Chord, parts, g, pr); }

// ---- ActionSet ------------------------------------------------------------------------------------------------------

void ActionSet::AddScheme(const ControlScheme& s) {
    for (ControlScheme& e : m_schemes) if (e.name == s.name) { e = s; return; }
    m_schemes.push_back(s);
    if (m_scheme.empty()) m_scheme = s.name;
}
bool ActionSet::SetScheme(const std::string& name) {
    for (const ControlScheme& s : m_schemes) if (s.name == name) { m_scheme = name; return true; }
    return false;
}
ActionMap& ActionSet::AddMap(const std::string& name, int priority, bool consume, bool enabled) {
    for (ActionMap& m : m_maps) if (m.name == name) { m.priority = priority; m.consume = consume; m.enabled = enabled; return m; }
    m_maps.push_back({name, priority, consume, enabled});
    return m_maps.back();
}
bool ActionSet::SetMapEnabled(const std::string& n, bool on) { for (ActionMap& m : m_maps) if (m.name == n) { m.enabled = on; return true; } return false; }
bool ActionSet::SetMapPriority(const std::string& n, int p) { for (ActionMap& m : m_maps) if (m.name == n) { m.priority = p; return true; } return false; }
bool ActionSet::MapEnabled(const std::string& n) const { for (const ActionMap& m : m_maps) if (m.name == n) return m.enabled; return n.empty(); }

bool ActionSet::RemoveAction(const std::string& name) {
    for (auto it = m_actions.begin(); it != m_actions.end(); ++it) if (it->name == name) { m_actions.erase(it); ++m_revision; return true; }
    return false;
}
bool ActionSet::RenameAction(const std::string& from, const std::string& to) {
    if (to.empty() || Find(to)) return false;
    ActionDef* a = Find(from);
    if (!a) return false;
    a->name = to;
    ++m_revision;
    for (ActionDef& o : m_actions) for (Interaction& i : o.interactions) if (i.kind == Interaction::Kind::Chorded && i.arg == from) i.arg = to;
    return true;
}
bool ActionSet::RemoveMap(const std::string& name) {
    for (auto it = m_maps.begin(); it != m_maps.end(); ++it) if (it->name == name) {
        for (ActionDef& a : m_actions) if (a.map == name) a.map.clear();
        m_maps.erase(it); return true;
    }
    return false;
}
bool ActionSet::RenameMap(const std::string& from, const std::string& to) {
    if (to.empty()) return false;
    for (const ActionMap& m : m_maps) if (m.name == to) return false;
    for (ActionMap& m : m_maps) if (m.name == from) {
        m.name = to;
        for (ActionDef& a : m_actions) if (a.map == from) a.map = to;
        return true;
    }
    return false;
}
bool ActionSet::RemoveScheme(const std::string& name) {
    for (auto it = m_schemes.begin(); it != m_schemes.end(); ++it) if (it->name == name) {
        for (ActionDef& a : m_actions) for (BindingSpec& b : a.bindings) if (b.group == name) b.group.clear();
        m_schemes.erase(it);
        if (m_scheme == name) m_scheme = m_schemes.empty() ? std::string() : m_schemes.front().name;
        return true;
    }
    return false;
}
bool ActionSet::RenameScheme(const std::string& from, const std::string& to) {
    if (to.empty()) return false;
    for (const ControlScheme& c : m_schemes) if (c.name == to) return false;
    for (ControlScheme& c : m_schemes) if (c.name == from) {
        c.name = to;
        for (ActionDef& a : m_actions) for (BindingSpec& b : a.bindings) if (b.group == from) b.group = to;
        if (m_scheme == from) m_scheme = to;
        return true;
    }
    return false;
}

ActionDef& ActionSet::Add(const std::string& name, ValueType type, const std::string& map) {
    if (!map.empty()) { bool have = false; for (const ActionMap& m : m_maps) if (m.name == map) have = true; if (!have) AddMap(map); }
    ++m_revision;
    if (ActionDef* a = Find(name)) { a->type = type; if (!map.empty()) a->map = map; return *a; }
    m_actions.emplace_back(); m_actions.back().name = name; m_actions.back().type = type; m_actions.back().map = map;
    return m_actions.back();
}
ActionDef* ActionSet::Find(const std::string& name) { for (ActionDef& a : m_actions) if (a.name == name) return &a; return nullptr; }
const ActionDef* ActionSet::Find(const std::string& name) const { for (const ActionDef& a : m_actions) if (a.name == name) return &a; return nullptr; }

bool ActionSet::BindingActive(const BindingSpec& b) const {
    return m_allSchemes || b.group.empty() || m_schemes.empty() || b.group == m_scheme;
}

namespace {
// Each physical input gets an id so a map (an input context) can claim it from the maps below.
int KeyId(char k) { return 1000 + (unsigned char)k; }
int MouseId(int b) { return 2000 + b; }
int PadId(int button, int player) { return 3000 + player * 100 + button; }
int StickId(bool right, int player) { return (right ? 4100 : 4000) + player; }
constexpr int kMouseDeltaId = 5000;
int WheelId(int dir) { return dir >= 0 ? 5001 : 5002; }

// Calls f(id) for every physical input a binding listens to (no allocation).
template <class F>
void ForClaims(const InputBinding& b, F f) {
    using K = InputBinding::Kind;
    switch (b.kind) {
        case K::Key:        f(KeyId((char)b.code)); break;
        case K::Mouse:      f(MouseId(b.code)); break;
        case K::PadButton:  f(PadId(b.code, b.player)); break;
        case K::Keys4:      for (char k : b.keys) f(KeyId(k)); break;
        case K::Chord:      for (char k : b.keys) if (k) f(KeyId(k)); break;
        case K::Wheel:      f(WheelId(b.code)); break;
        case K::StickLeft:  f(StickId(false, b.player)); break;
        case K::StickRight: f(StickId(true, b.player)); break;
        case K::DPad:       for (int p : {Input::PadUp, Input::PadDown, Input::PadLeft, Input::PadRight}) f(PadId(p, b.player)); break;
        case K::MouseDelta: f(kMouseDeltaId); break;
        case K::None:       break;
    }
}
// Is `id` swallowed: by a map above (`a`) or, for a key, by a held chord (`b`, may be null)?
bool Claimed(int id, const std::vector<int>& a, const std::vector<int>* b) {
    return std::find(a.begin(), a.end(), id) != a.end() || (b && std::find(b->begin(), b->end(), id) != b->end());
}
// A binding read with some of its inputs swallowed: the free keys of a four-key direction or the d-pad still count.
Vec2 MaskedVector(const InputBinding& b, const std::vector<int>& a, const std::vector<int>* c) {
    using K = InputBinding::Kind;
    auto key = [&](char k) { return !Claimed(KeyId(k), a, c) && Input::GetKeyGame(k) ? 1.0f : 0.0f; };
    auto pad = [&](int btn) { return !Claimed(PadId(btn, b.player), a, c) && Input::GetGamepadButton(btn, b.player) ? 1.0f : 0.0f; };
    switch (b.kind) {
        case K::Keys4: return {key(b.keys[3]) - key(b.keys[2]), key(b.keys[0]) - key(b.keys[1])};
        case K::DPad:  return {pad(Input::PadRight) - pad(Input::PadLeft), pad(Input::PadUp) - pad(Input::PadDown)};
        default: {
            bool hit = false;
            ForClaims(b, [&](int id) { if (Claimed(id, a, c)) hit = true; });
            return hit ? Vec2{0.0f, 0.0f} : b.Vector();
        }
    }
}
float MaskedValue(const InputBinding& b, const std::vector<int>& a, const std::vector<int>* c) {
    if (b.IsDirection()) { const Vec2 v = MaskedVector(b, a, c); return std::min(1.0f, std::sqrt(v.x * v.x + v.y * v.y)); }
    bool hit = false;
    ForClaims(b, [&](int id) { if (Claimed(id, a, c)) hit = true; });
    return hit ? 0.0f : b.Value();
}
void Build(const BindingSpec& b) {
    if (b.built) return;
    b.leaves.clear();
    for (const std::string& p : b.parts) b.leaves.push_back(InputBinding::Parse(p));
    b.built = true;
}
// The player's response as processors (the same maths as authored ones), after the action's own processors.
void ApplyResponse(const InputActions::Response& r, InputValue& v, bool button, float dt) {
    if (r.deadzone == 0.0f && r.curve == 1.0f && r.sensitivity == 1.0f && !r.invertX && !r.invertY) return;
    Processor p;
    if (r.deadzone > 0.0f) { p.kind = Processor::Kind::Deadzone; p.a = r.deadzone; p.b = 1.0f; v = p.Apply(v, dt); }
    if (r.curve != 1.0f && r.curve > 0.0f) { p.kind = Processor::Kind::Curve; p.a = r.curve; v = p.Apply(v, dt); }
    if (r.sensitivity != 1.0f) { p.kind = Processor::Kind::Scale; p.a = r.sensitivity; p.mask = 1; v = p.Apply(v, dt); }
    if (r.invertX || r.invertY) { p.kind = Processor::Kind::Invert; p.mask = (r.invertX ? 1 : 0) | (r.invertY ? 2 : 0); v = p.Apply(v, dt); }
    if (button && v.x > 1.0f) v.x = 1.0f;
}
void ClampLength(InputValue& v) { const float len = v.Length(); if (len > 1.0f) { v.x /= len; v.y /= len; v.z /= len; } }
} // namespace

float ShapeResponse(float in, const InputActions::Response& r) {
    InputValue v{in < 0.0f ? 0.0f : (in > 1.0f ? 1.0f : in), 0, 0};
    ApplyResponse(r, v, false, 0.0f);
    return v.x;
}

bool IsBaseMapName(const std::string& m) { return m.empty() || str::EqualsNoCaseAscii(m, "gameplay"); }

float ActionDef::Magnitude() const { return type == ValueType::Button || type == ValueType::Axis1D ? std::fabs(value.x) : value.Length(); }
float ActionDef::PrevMagnitude() const { return type == ValueType::Button || type == ValueType::Axis1D ? std::fabs(prevValue.x) : prevValue.Length(); }

InputValue ActionSet::Eval(const BindingSpec& b, int pad) const {
    Build(b);
    auto leaf = [&](std::size_t i) -> InputValue {
        if (i >= b.leaves.size()) return {};
        InputBinding lb = b.leaves[i];
        if (lb.kind == InputBinding::Kind::None) return {};
        if (pad >= 0) {   // one controller's own reading: its pad (a binding for the first pad follows it), the keyboard and mouse on pad 0
            if (lb.IsGamepad()) { if (lb.player != 0 && lb.player != pad) return {}; lb.player = pad; }
            else if (pad != 0) return {};
        } else if (m_pad >= 0 && lb.IsGamepad()) lb.player = m_pad;   // a paired player reads their own controller
        const std::vector<int>* chords = lb.kind != InputBinding::Kind::Chord && !m_chordClaims.empty() ? &m_chordClaims : nullptr;
        if (m_claims.empty() && !chords) {
            if (lb.IsDirection()) { const Vec2 v = lb.Vector(); return {v.x, v.y, 0}; }
            return {lb.Value(), 0, 0};
        }
        if (lb.IsDirection()) { const Vec2 v = MaskedVector(lb, m_claims, chords); return {v.x, v.y, 0}; }
        return {MaskedValue(lb, m_claims, chords), 0, 0};
    };
    auto s = [&](std::size_t i) { return leaf(i).x; };
    InputValue v;
    switch (b.kind) {
        case BindingSpec::Kind::Single: v = leaf(0); break;
        case BindingSpec::Kind::Axis1D: v.x = s(1) - s(0); break;
        case BindingSpec::Kind::Vector2: {
            v.y = s(0) - s(1); v.x = s(3) - s(2);
            ClampLength(v);
            break;
        }
        case BindingSpec::Kind::Vector3: {
            v.y = s(0) - s(1); v.x = s(3) - s(2); v.z = s(4) - s(5);
            ClampLength(v);
            break;
        }
        case BindingSpec::Kind::Chord: {
            if (b.leaves.empty()) break;
            for (std::size_t i = 0; i + 1 < b.leaves.size(); ++i) if (s(i) < 0.5f) return {};   // a modifier is not held
            v = leaf(b.leaves.size() - 1);
            break;
        }
    }
    return v;
}

void ActionSet::EvalAction(ActionDef& a, bool enabled, float dt) {
    a.live = enabled;
    const bool button = a.type == ValueType::Button;
    auto read = [&](int pad) {
        InputValue best, total; float bestLen = -1.0f; int bestIdx = -1;
        for (std::size_t i = 0; i < a.bindings.size() && enabled; ++i) {
            BindingSpec& b = a.bindings[i];
            if (!BindingActive(b)) continue;
            InputValue v = Eval(b, pad);
            for (Processor& p : b.processors) v = p.Apply(v, dt, pad < 0 ? 0 : pad + 1);
            const float len = v.Length();
            if (len > bestLen) { bestLen = len; best = v; bestIdx = (int)i; }
            total.x += v.x; total.y += v.y; total.z += v.z;
        }
        InputValue v = bestLen < 0 ? InputValue{} : best;
        if (a.sum && !button && bestLen >= 0) { v = total; ClampLength(v); }   // keys and a stick at once add up
        if (pad < 0) a.activeBinding = bestLen > 0 ? bestIdx : -1;
        return v;
    };
    InputValue v = read(-1);
    for (Processor& p : a.processors) v = p.Apply(v, dt);
    switch (a.type) {   // the type decides which components exist
        case ValueType::Button: v = {v.Length(), 0, 0}; break;
        case ValueType::Axis1D: v = {v.x, 0, 0}; break;
        case ValueType::Vector2: v.z = 0; break;
        default: break;
    }
    ApplyResponse(a.response, v, button, dt);
    a.value = v;
    if (!a.perPlayer) return;
    for (int p = 0; p < Input::kMaxGamepads; ++p) {
        ActionDef::PadState& ps = a.pads[p];
        InputValue pv = read(p);
        if (button) pv = {pv.Length(), 0, 0}; else if (a.type == ValueType::Vector2) pv.z = 0;
        ApplyResponse(a.response, pv, button, dt);
        ps.vector = {pv.x, pv.y};
        ps.value = button || a.type == ValueType::Axis1D ? std::fabs(pv.x) : pv.Length();
    }
}

void ActionSet::Update(float dt) {
    // Auto switch: follow the device last used (a paired set only follows its own controller).
    if (m_auto && !m_schemes.empty()) {
        const Input::Device d = Input::LastDevice();
        const bool mine = d != Input::Device::Gamepad || m_pad < 0 || Input::LastGamepad() == m_pad;
        if (mine && (!m_seenDevice || d != m_lastDevice)) {
            m_seenDevice = true; m_lastDevice = d;
            bool ok = false;
            for (const ControlScheme& s : m_schemes) if (s.name == m_scheme && s.Uses(d)) ok = true;
            if (!ok) for (const ControlScheme& s : m_schemes) if (s.Uses(d)) { m_scheme = s.name; break; }
        }
    }
    m_time += dt;
    m_lastDt = dt > 0.0f ? dt : 0.0f;
    m_events.clear();
    for (ActionDef& a : m_actions) {
        a.started = a.performed = a.canceled = false;
        if (a.phase == Phase::Performed || a.phase == Phase::Canceled) a.phase = Phase::Waiting;
        a.prevValue = a.value;
        for (ActionDef::PadState& ps : a.pads) ps.previous = ps.value;
    }
    // A chord that is held wins over the single keys inside it (Ctrl+Z does not also Crouch).
    m_chordClaims.clear();
    for (const ActionDef& a : m_actions)
        for (const BindingSpec& b : a.bindings) {
            if (!BindingActive(b)) continue;
            Build(b);
            for (const InputBinding& l : b.leaves)
                if (l.kind == InputBinding::Kind::Chord && l.Value() > 0.0f) ForClaims(l, [&](int id) { m_chordClaims.push_back(id); });
        }
    // Evaluate map by map, highest priority first; a consuming map claims its inputs from the maps below it and a blocking
    // map leaves them nothing. (Insertion sort: stable, and no allocation.)
    m_order.clear();
    for (const ActionMap& m : m_maps) {
        m_order.push_back(&m);
        for (std::size_t i = m_order.size() - 1; i > 0 && m_order[i - 1]->priority < m_order[i]->priority; --i) std::swap(m_order[i - 1], m_order[i]);
    }
    m_claims.clear();
    bool blocked = false;
    auto runMap = [&](const std::string& name, bool enabled, bool consume, bool block) {
        const bool on = enabled && !blocked;
        for (ActionDef& a : m_actions) if (a.map == name) EvalAction(a, on, dt);
        if (on && consume)
            for (const ActionDef& a : m_actions) {
                if (a.map != name) continue;
                for (const BindingSpec& b : a.bindings) {
                    if (!BindingActive(b)) continue;
                    for (InputBinding l : b.leaves) {
                        if (m_pad >= 0 && l.IsGamepad()) l.player = m_pad;
                        ForClaims(l, [&](int id) { m_claims.push_back(id); });
                    }
                }
            }
        if (on && block) blocked = true;
    };
    bool defaultDone = false;
    for (const ActionMap* m : m_order) {
        if (!defaultDone && m->priority < 0) { runMap("", true, false, false); defaultDone = true; }   // the unnamed map sits at priority 0
        runMap(m->name, m->enabled, m->consume, m->block);
    }
    if (!defaultDone) runMap("", true, false, false);
    m_claims.clear();
    // On-screen controls, added after the devices so a key and a virtual stick share one action. A map that is off or
    // blocked does not get them.
    for (ActionDef& a : m_actions) {
        if (!a.contributed) continue;
        if (a.live) {
            if (a.type == ValueType::Vector2 || a.type == ValueType::Vector3) {
                a.value.x += a.contribVector.x; a.value.y += a.contribVector.y;
                ClampLength(a.value);
            } else {
                float extra = a.contribValue;
                const float len = std::sqrt(a.contribVector.x * a.contribVector.x + a.contribVector.y * a.contribVector.y);
                if (len > extra) extra = len > 1.0f ? 1.0f : len;
                if (extra > a.value.x) a.value.x = extra;
            }
        }
        a.contributed = false; a.contribValue = 0; a.contribVector = {0.0f, 0.0f};
    }
    // Hold-to-toggle (accessibility): a button action the player asked to toggle stays on after the input comes up, until
    // the next press.
    const bool anyToggle = Accessibility::AnyToggle();
    for (ActionDef& a : m_actions) {
        if (a.type != ValueType::Button || !anyToggle || !Accessibility::ToggleFor(a.name)) { a.latchActive = a.latchHeld = false; continue; }
        const bool down = a.value.x >= a.pressPoint;
        if (down && !a.latchHeld) a.latchActive = !a.latchActive;   // each press flips it
        a.latchHeld = down;
        a.value.x = a.latchActive ? 1.0f : 0.0f;
    }
    // Held / pressed / released, and the press timing for buffering, double tap and hold to confirm.
    for (ActionDef& a : m_actions) {
        const bool was = a.down;
        a.down = a.Magnitude() >= a.pressPoint;
        a.pressed = a.down && !was;
        a.released = !a.down && was;
        if (a.pressed) { a.prevPressedAt = a.pressedAt; a.pressedAt = m_time; a.pressConsumed = false; a.heldSince = m_time; }
        if (!a.down) a.heldSince = -1.0;
    }
    // Interactions run after every value is known, so a chorded action can look at another one.
    for (ActionDef& a : m_actions) RunInteractions(a, (a.down && !a.pressed) || a.released, dt);
}

ActionDef* ActionSet::FindNoCase(const std::string& name) { return const_cast<ActionDef*>(static_cast<const ActionSet*>(this)->FindNoCase(name)); }
const ActionDef* ActionSet::FindNoCase(const std::string& name) const {
    if (m_indexRev != m_revision || m_indexSize != m_actions.size()) {
        m_index.clear();
        for (std::size_t i = 0; i < m_actions.size(); ++i) m_index.emplace_back(str::LowerAscii(m_actions[i].name), i);
        std::sort(m_index.begin(), m_index.end());
        m_indexRev = m_revision; m_indexSize = m_actions.size();
    }
    const std::string n = str::LowerAscii(name);
    auto it = std::lower_bound(m_index.begin(), m_index.end(), n, [](const std::pair<std::string, std::size_t>& e, const std::string& k) { return e.first < k; });
    if (it == m_index.end() || it->first != n || it->second >= m_actions.size()) return nullptr;
    return &m_actions[it->second];
}

void ActionSet::PushMap(const std::string& map, bool consume, bool block) {
    ActionMap* m = nullptr;
    for (ActionMap& e : m_maps) if (str::EqualsNoCaseAscii(e.name, map)) m = &e;
    bool added = false;
    if (!m) { m = &AddMap(map, 0, false, false); added = true; }
    Pushed saved{m->name, m->priority, m->consume, m->block, m->enabled, added};
    for (auto it = m_pushed.begin(); it != m_pushed.end(); ++it)
        if (it->name == m->name) { saved = *it; m_pushed.erase(it); break; }   // re-pushed: keep what it was before the first push
    m_pushed.push_back(saved);
    int top = 0;
    for (const ActionMap& e : m_maps) if (&e != m && e.priority > top) top = e.priority;
    m->priority = top + 1; m->consume = consume; m->block = block; m->enabled = true;
}
void ActionSet::PopMap(const std::string& map) {
    for (auto it = m_pushed.begin(); it != m_pushed.end(); ++it) {
        if (!str::EqualsNoCaseAscii(it->name, map)) continue;
        for (ActionMap& e : m_maps)
            if (e.name == it->name) { e.priority = it->priority; e.consume = it->consume; e.block = it->block; e.enabled = it->enabled; }
        m_pushed.erase(it);
        return;
    }
}
bool ActionSet::Consumed(const InputBinding& leaf) const {
    bool hit = false;
    for (const ActionMap& m : m_maps) {
        if (!m.enabled || m.priority <= 0 || IsBaseMapName(m.name)) continue;
        if (m.block) return true;
        if (!m.consume) continue;
        for (const ActionDef& a : m_actions) {
            if (a.map != m.name) continue;
            for (const BindingSpec& b : a.bindings) {
                if (!BindingActive(b)) continue;
                Build(b);
                for (const InputBinding& l : b.leaves)
                    ForClaims(l, [&](int id) { ForClaims(leaf, [&](int want) { if (id == want) hit = true; }); });
            }
        }
    }
    return hit;
}

void ActionSet::Contribute(const std::string& action, float value) {
    if (ActionDef* a = FindNoCase(action)) { if (!a->contributed || value > a->contribValue) a->contribValue = value; a->contributed = true; }
}
void ActionSet::ContributeVector(const std::string& action, const Vec2& v) {
    if (ActionDef* a = FindNoCase(action)) { a->contribVector.x += v.x; a->contribVector.y += v.y; a->contributed = true; }
}
void ActionSet::ClearContributions() { for (ActionDef& a : m_actions) { a.contributed = false; a.contribValue = 0; a.contribVector = {0.0f, 0.0f}; } }

void ActionSet::Emit(ActionDef& a, int idx, Phase ph, bool down, float held) {
    PhaseEvent e; e.action = a.name; e.phase = ph; e.interaction = idx; e.value = a.value; e.held = held; e.down = down;
    if (ph == Phase::Started) a.started = true; else if (ph == Phase::Performed) a.performed = true; else if (ph == Phase::Canceled) a.canceled = true;
    a.phase = ph;
    m_events.push_back(e);
    for (std::size_t i = 0; i < m_subs.size(); ++i) { auto fn = m_subs[i].second; if (fn) fn(e); }   // a copy: a callback may subscribe
}

void ActionSet::RunInteractions(ActionDef& a, bool wasDown, float dt) {
    using K = Interaction::Kind;
    if (a.interactions.empty()) {
        if (a.type == ValueType::Button) {
            if (a.down && !wasDown) { Emit(a, -1, Phase::Started); Emit(a, -1, Phase::Performed); }
            else if (!a.down && wasDown) Emit(a, -1, Phase::Canceled);
        } else {
            const bool now = a.value.Length() > 0, was = a.prevValue.Length() > 0;
            if (now && !was) Emit(a, -1, Phase::Started);
            if (now && !(a.value == a.prevValue)) Emit(a, -1, Phase::Performed);
            if (!now && was) Emit(a, -1, Phase::Canceled);
        }
        return;
    }
    for (std::size_t k = 0; k < a.interactions.size(); ++k) {
        Interaction& it = a.interactions[k];
        const int idx = (int)k;
        bool down = a.down;
        const bool was = wasDown;
        if (it.kind == K::Chorded) continue;   // a chorded entry has no events of its own: it gates the others
        // any Chorded interaction in the list gates this action's presses
        bool gated = false;
        for (const Interaction& o : a.interactions) if (o.kind == K::Chorded) { const ActionDef* g = Find(o.arg); if (!g || !g->down) gated = true; }
        if (gated) down = false;
        const bool pressEdge = down && !was, releaseEdge = !down && was;
        switch (it.kind) {
            case K::Press: if (pressEdge) { Emit(a, idx, Phase::Started); Emit(a, idx, Phase::Performed); } break;
            case K::Release:
                if (pressEdge) { it.stage = 1; Emit(a, idx, Phase::Started); }
                else if (it.stage == 1 && releaseEdge) { it.stage = 0; Emit(a, idx, Phase::Performed, false); }
                else if (it.stage == 1 && gated) { it.stage = 0; Emit(a, idx, Phase::Canceled); }
                break;
            case K::PressRelease:
                if (pressEdge) { Emit(a, idx, Phase::Started); Emit(a, idx, Phase::Performed, true); }
                else if (releaseEdge) Emit(a, idx, Phase::Performed, false);
                break;
            case K::Hold:
                if (it.stage == 0 && pressEdge) { it.stage = 1; it.t = 0; Emit(a, idx, Phase::Started); }
                else if (it.stage == 1) {
                    it.t += dt;
                    if (!down) { it.stage = 0; Emit(a, idx, Phase::Canceled, false, it.t); }
                    else if (it.t >= it.a) { it.stage = 2; Emit(a, idx, Phase::Performed, true, it.t); }
                } else if (it.stage == 2 && !down) it.stage = 0;
                break;
            case K::Tap:
                if (it.stage == 0 && pressEdge) { it.stage = 1; it.t = 0; Emit(a, idx, Phase::Started); }
                else if (it.stage == 1) {
                    it.t += dt;
                    if (!down) { it.stage = 0; Emit(a, idx, it.t <= it.a ? Phase::Performed : Phase::Canceled, false, it.t); }
                    else if (it.t > it.a) { it.stage = 3; Emit(a, idx, Phase::Canceled, true, it.t); }
                } else if (it.stage == 3 && !down) it.stage = 0;
                break;
            case K::MultiTap:
                if (it.stage == 2) { it.gap += dt; if (!pressEdge && it.gap > it.b) { it.stage = 0; it.taps = 0; Emit(a, idx, Phase::Canceled); } }
                if ((it.stage == 0 || it.stage == 2) && pressEdge) { if (it.taps == 0) Emit(a, idx, Phase::Started); it.stage = 1; it.t = 0; }
                else if (it.stage == 1) {
                    it.t += dt;
                    if (!down) {
                        if (it.t <= it.c) {
                            if (++it.taps >= (int)it.a) { it.taps = 0; it.stage = 0; Emit(a, idx, Phase::Performed, false, it.t); }
                            else { it.stage = 2; it.gap = 0; }
                        } else { it.stage = 0; it.taps = 0; Emit(a, idx, Phase::Canceled, false, it.t); }
                    } else if (it.t > it.c) { it.stage = 3; it.taps = 0; Emit(a, idx, Phase::Canceled, true, it.t); }
                } else if (it.stage == 3 && !down) it.stage = 0;
                break;
            case K::Repeat:
                if (it.stage == 0 && pressEdge) { it.stage = 1; it.t = 0; it.next = it.b; Emit(a, idx, Phase::Started); Emit(a, idx, Phase::Performed); }
                else if (it.stage == 1) {
                    it.t += dt;
                    if (!down) { it.stage = 0; Emit(a, idx, Phase::Canceled, false, it.t); }
                    else while (it.t >= it.next) { Emit(a, idx, Phase::Performed, true, it.t); it.next += it.a; }
                }
                break;
            case K::Chorded: break;
        }
    }
}

int ActionSet::Subscribe(std::function<void(const PhaseEvent&)> fn) { const int id = m_nextSub++; m_subs.emplace_back(id, std::move(fn)); return id; }
void ActionSet::Unsubscribe(int id) { m_subs.erase(std::remove_if(m_subs.begin(), m_subs.end(), [&](const auto& p) { return p.first == id; }), m_subs.end()); }
bool ActionSet::Started(const std::string& n) const { const ActionDef* a = Find(n); return a && a->started; }
bool ActionSet::Performed(const std::string& n) const { const ActionDef* a = Find(n); return a && a->performed; }
bool ActionSet::Canceled(const std::string& n) const { const ActionDef* a = Find(n); return a && a->canceled; }
Phase ActionSet::PhaseOf(const std::string& n) const { const ActionDef* a = Find(n); return a ? a->phase : Phase::Waiting; }
float ActionSet::Progress(const std::string& n) const { const ActionDef* a = Find(n); return a ? a->Progress() : 0.0f; }

InputValue ActionSet::Value(const std::string& n) const { const ActionDef* a = Find(n); return a ? a->value : InputValue{}; }
bool ActionSet::Pressed(const std::string& n) const { const ActionDef* a = Find(n); return a && a->pressed; }
bool ActionSet::Held(const std::string& n) const { const ActionDef* a = Find(n); return a && a->down; }
bool ActionSet::Released(const std::string& n) const { const ActionDef* a = Find(n); return a && a->released; }

bool ActionSet::Buffered(const std::string& name, float window) const {
    const ActionDef* a = FindNoCase(name);
    if (!a || a->pressConsumed || !(window >= 0.0f) || a->pressedAt < -1.0e8) return false;
    return m_time - a->pressedAt <= (double)window;
}
bool ActionSet::Consume(const std::string& name, float window) {
    if (!Buffered(name, window)) return false;
    ActionDef* a = FindNoCase(name);
    a->pressConsumed = true;
    return true;
}
bool ActionSet::DoubleTap(const std::string& name, float window) const {
    const ActionDef* a = FindNoCase(name);
    if (!a || !a->pressed || !(window >= 0.0f) || a->prevPressedAt < -1.0e8) return false;
    return a->pressedAt - a->prevPressedAt <= (double)window;
}
float ActionSet::HeldFor(const std::string& name) const {
    const ActionDef* a = FindNoCase(name);
    return a && a->heldSince >= 0.0 ? (float)(m_time - a->heldSince) : 0.0f;
}
bool ActionSet::HoldReached(const std::string& name, float seconds) const {
    const ActionDef* a = FindNoCase(name);
    if (!a || a->heldSince < 0.0 || !(seconds > 0.0f)) return false;
    const double held = m_time - a->heldSince;
    return held >= (double)seconds && held - (double)m_lastDt < (double)seconds;
}

// ---- runtime rebinding -------------------------------------------------------------------------------------------

bool ActionSet::Rebind(const std::string& action, int binding, int part, const std::string& path) {
    ActionDef* a = Find(action);
    if (!a || binding < 0 || binding >= (int)a->bindings.size()) return false;
    BindingSpec& b = a->bindings[(std::size_t)binding];
    if (part < 0 || part >= (int)b.parts.size()) return false;
    if (InputBinding::Parse(path).kind == InputBinding::Kind::None) return false;
    if (b.original.empty()) b.original = b.parts;
    b.parts[(std::size_t)part] = path;
    b.built = false;
    ++m_revision;
    return true;
}

std::vector<std::string> ActionSet::Conflicts(const std::string& action, int binding, int part, const std::string& path) const {
    std::vector<std::string> out;
    const ActionDef* me = Find(action);
    if (!me) return out;
    const std::string want = InputBinding::Parse(path).ToString();
    const std::string group = binding >= 0 && binding < (int)me->bindings.size() ? me->bindings[(std::size_t)binding].group : "";
    for (const ActionDef& a : m_actions) {
        if (a.map != me->map) continue;
        for (std::size_t bi = 0; bi < a.bindings.size(); ++bi) {
            const BindingSpec& b = a.bindings[bi];
            if (!b.group.empty() && !group.empty() && b.group != group) continue;
            for (std::size_t pi = 0; pi < b.parts.size(); ++pi) {
                if (&a == me && (int)bi == binding && (int)pi == part) continue;
                if (InputBinding::Parse(b.parts[pi]).ToString() != want) continue;
                out.push_back(a.name + (b.parts.size() > 1 ? "." + std::to_string(bi) + "." + std::to_string(pi) : ""));
            }
        }
    }
    return out;
}

const char* BindingKindName(BindingSpec::Kind k) {
    switch (k) { case BindingSpec::Kind::Vector2: return "vector2"; case BindingSpec::Kind::Vector3: return "vector3"; case BindingSpec::Kind::Axis1D: return "axis"; case BindingSpec::Kind::Chord: return "chord"; default: return "single"; }
}
bool ParseBindingKind(const std::string& s, BindingSpec::Kind& k) {
    if (s == "single") k = BindingSpec::Kind::Single; else if (s == "vector2") k = BindingSpec::Kind::Vector2; else if (s == "vector3") k = BindingSpec::Kind::Vector3;
    else if (s == "axis") k = BindingSpec::Kind::Axis1D; else if (s == "chord") k = BindingSpec::Kind::Chord; else return false;
    return true;
}

std::vector<BindingSpec> ActionSet::AuthoredBindings(const ActionDef& a) const {
    std::vector<BindingSpec> out = a.reshaped ? a.authored : a.bindings;
    if (!a.reshaped)
        for (BindingSpec& b : out) if (!b.original.empty()) { b.parts = b.original; b.original.clear(); b.built = false; }
    return out;
}

bool ActionSet::SetBindings(const std::string& action, const std::vector<BindingSpec>& list) {
    ActionDef* a = Find(action);
    if (!a) return false;
    const std::vector<BindingSpec> auth = AuthoredBindings(*a);
    bool same = list.size() == auth.size();
    for (std::size_t i = 0; same && i < list.size(); ++i)
        same = list[i].kind == auth[i].kind && list[i].parts.size() == auth[i].parts.size();
    if (same) {   // the authored shape: plain per-binding overrides
        a->authored.clear(); a->reshaped = false;
        a->bindings = auth;
        for (std::size_t i = 0; i < list.size(); ++i)
            for (std::size_t k = 0; k < list[i].parts.size(); ++k)
                if (InputBinding::Parse(list[i].parts[k]).ToString() != InputBinding::Parse(auth[i].parts[k]).ToString())
                    Rebind(a->name, (int)i, (int)k, list[i].parts[k]);
    } else {
        a->authored = auth; a->reshaped = true;
        a->bindings = list;
        for (std::size_t i = 0; i < a->bindings.size(); ++i) {
            BindingSpec& b = a->bindings[i];
            b.original.clear(); b.built = false;
            if (b.processors.empty() && i < auth.size() && auth[i].kind == b.kind) b.processors = auth[i].processors;   // keep the authored shaping in place
        }
    }
    ++m_revision;
    return true;
}

std::string ActionSet::ExportOverrides() const {
    std::string s;
    for (const ActionDef& a : m_actions) {
        if (a.reshaped) {   // the list's shape changed: one line for all of it ("ovlist Jump single key:j;single pad:a group=Gamepad")
            s += "ovlist " + a.name + " ";
            for (std::size_t bi = 0; bi < a.bindings.size(); ++bi) {
                const BindingSpec& b = a.bindings[bi];
                const std::vector<std::string>& parts = b.original.empty() ? b.parts : b.original;
                s += (bi ? ";" : "") + std::string(BindingKindName(b.kind));
                for (const std::string& p : parts) s += " " + p;
                if (!b.group.empty()) s += " group=" + b.group;
            }
            s += "\n";
        }
        for (std::size_t bi = 0; bi < a.bindings.size(); ++bi) {
            const BindingSpec& b = a.bindings[bi];
            if (b.original.empty() || b.original == b.parts) continue;
            s += "ov " + a.name + " " + std::to_string(bi) + " ";
            for (std::size_t i = 0; i < b.parts.size(); ++i) s += (i ? "," : "") + b.parts[i];
            s += "\n";
        }
    }
    for (const std::string& k : m_keep) {   // lines for actions this set cannot take, unless it now has its own line for one
        const std::size_t s1 = k.find(' '), s2 = s1 == std::string::npos ? s1 : k.find(' ', s1 + 1);
        const std::string who = s2 == std::string::npos ? std::string() : k.substr(s1 + 1, s2 - s1 - 1);
        if (who.empty() || s.find(" " + who + " ") == std::string::npos) s += k + "\n";
    }
    return s;
}

int ActionSet::ImportOverrides(const std::string& text) {
    int applied = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t e = text.find('\n', i); if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(i, e - i);
        i = e + 1;
        if (line.rfind("ovlist ", 0) == 0) {   // a whole list: "ovlist <action> <kind> <part>... [group=G];..."
            const std::size_t s1 = line.find(' ', 7);
            ActionDef* a = s1 == std::string::npos ? nullptr : Find(line.substr(7, s1 - 7));
            if (!a) { if (s1 != std::string::npos && std::find(m_keep.begin(), m_keep.end(), line) == m_keep.end()) m_keep.push_back(line); continue; }
            std::vector<BindingSpec> list;
            bool ok = true;
            std::size_t j = s1 + 1;
            while (ok && j <= line.size()) {
                std::size_t semi = line.find(';', j); if (semi == std::string::npos) semi = line.size();
                const std::string spec = line.substr(j, semi - j);
                j = semi + 1;
                BindingSpec b;
                std::size_t k = 0;
                bool first = true;
                while (k < spec.size()) {
                    std::size_t sp = spec.find(' ', k); if (sp == std::string::npos) sp = spec.size();
                    const std::string tok = spec.substr(k, sp - k);
                    k = sp + 1;
                    if (tok.empty()) continue;
                    if (first) { ok = ParseBindingKind(tok, b.kind); first = false; }
                    else if (tok.rfind("group=", 0) == 0) b.group = tok.substr(6);
                    else if (InputBinding::Parse(tok).kind == InputBinding::Kind::None) ok = false;
                    else b.parts.push_back(tok);
                }
                if (first && spec.empty() && line.size() == s1 + 1) break;   // "ovlist Dig " = no bindings at all
                if (first || b.parts.empty()) ok = false;
                list.push_back(std::move(b));
            }
            if (ok && SetBindings(a->name, list)) ++applied;
            continue;
        }
        if (line.rfind("ov ", 0) != 0) continue;
        const std::size_t s1 = line.find(' ', 3), s2 = s1 == std::string::npos ? s1 : line.find(' ', s1 + 1);
        if (s2 == std::string::npos) continue;
        const std::string name = line.substr(3, s1 - 3);
        const int bi = std::atoi(line.substr(s1 + 1, s2 - s1 - 1).c_str());
        std::vector<std::string> parts;
        std::size_t j = s2 + 1;
        while (j <= line.size()) { std::size_t c = line.find(',', j); if (c == std::string::npos) c = line.size(); parts.push_back(line.substr(j, c - j)); j = c + 1; }
        ActionDef* a = Find(name);
        if (!a || bi < 0 || bi >= (int)a->bindings.size() || parts.size() != a->bindings[(std::size_t)bi].parts.size()) {
            if (std::find(m_keep.begin(), m_keep.end(), line) == m_keep.end()) m_keep.push_back(line);   // kept for the asset it was made for
            continue;
        }
        bool ok = true;
        for (const std::string& p : parts) if (InputBinding::Parse(p).kind == InputBinding::Kind::None) ok = false;
        if (!ok) continue;
        for (std::size_t k = 0; k < parts.size(); ++k) Rebind(name, bi, (int)k, parts[k]);
        ++applied;
    }
    return applied;
}

void ActionSet::ResetOverrides() {
    m_keep.clear();
    for (ActionDef& a : m_actions) {
        if (a.reshaped) { a.bindings = a.authored; a.authored.clear(); a.reshaped = false; }
        for (BindingSpec& b : a.bindings) if (!b.original.empty()) { b.parts = b.original; b.original.clear(); b.built = false; }
    }
    ++m_revision;
}
bool ActionSet::ResetOverride(const std::string& action, int binding) {
    ActionDef* a = Find(action);
    if (!a || binding < 0 || binding >= (int)a->bindings.size() || a->bindings[(std::size_t)binding].original.empty()) return false;
    BindingSpec& b = a->bindings[(std::size_t)binding];
    b.parts = b.original; b.original.clear(); b.built = false;
    ++m_revision;
    return true;
}
void ActionSet::SaveOverrides(const std::string& key) const {
    const std::string t = ExportOverrides();
    if (t.empty()) Prefs::Delete("inputov." + key); else Prefs::SetString("inputov." + key, t);
}
int ActionSet::LoadOverrides(const std::string& key) {
    return Prefs::Has("inputov." + key) ? ImportOverrides(Prefs::GetString("inputov." + key)) : 0;
}

} // namespace okay
