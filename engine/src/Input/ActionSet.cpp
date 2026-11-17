// Typed actions, composites, processors and control schemes (see ActionSet.hpp). Written from the general idea of
// action maps: our own value rules, text forms and thresholds, checked by tests/test_action_set.cpp.
#include "okay/Input/ActionSet.hpp"
#include "okay/Core/Prefs.hpp"
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

InputValue Processor::Apply(const InputValue& in, float dt) {
    InputValue v = in;
    switch (kind) {
        case Kind::Deadzone: {   // below `a` reads 0, the rest is stretched so `b` (default 1) reads 1; on the length
            const float lo = a, hi = b > a ? b : 1.0f;
            const float len = v.Length();
            if (len <= lo) return {};
            const float k = std::min(1.0f, (len - lo) / (hi - lo)) / len;
            return {v.x * k, v.y * k, v.z * k};
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
        case Kind::Smooth: {   // exponential approach: `a` seconds to cover most of the way
            if (!primed || a <= 0 || dt <= 0) { state[0] = v.x; state[1] = v.y; state[2] = v.z; primed = true; return v; }
            const float k = 1.0f - std::exp(-dt / a);
            state[0] += (v.x - state[0]) * k; state[1] += (v.y - state[1]) * k; state[2] += (v.z - state[2]) * k;
            return {state[0], state[1], state[2]};
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
    for (auto it = m_actions.begin(); it != m_actions.end(); ++it) if (it->name == name) { m_actions.erase(it); return true; }
    return false;
}
bool ActionSet::RenameAction(const std::string& from, const std::string& to) {
    if (to.empty() || Find(to)) return false;
    ActionDef* a = Find(from);
    if (!a) return false;
    a->name = to;
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
    if (ActionDef* a = Find(name)) { a->type = type; if (!map.empty()) a->map = map; return *a; }
    m_actions.emplace_back(); m_actions.back().name = name; m_actions.back().type = type; m_actions.back().map = map;
    return m_actions.back();
}
ActionDef* ActionSet::Find(const std::string& name) { for (ActionDef& a : m_actions) if (a.name == name) return &a; return nullptr; }
const ActionDef* ActionSet::Find(const std::string& name) const { for (const ActionDef& a : m_actions) if (a.name == name) return &a; return nullptr; }

bool ActionSet::BindingActive(const BindingSpec& b) const {
    return b.group.empty() || m_schemes.empty() || b.group == m_scheme;
}

InputValue ActionSet::Eval(const BindingSpec& b) const {
    if (!b.built) {
        b.leaves.clear();
        for (const std::string& p : b.parts) b.leaves.push_back(InputBinding::Parse(p));
        b.built = true;
    }
    auto leaf = [&](std::size_t i) -> InputValue {
        if (i >= b.leaves.size()) return {};
        InputBinding lb = b.leaves[i];
        if (lb.kind == InputBinding::Kind::None) return {};
        if (!m_blocked.empty() && m_blocked.count(lb.ToString())) return {};   // a map above swallowed this input
        if (m_pad >= 0 && lb.IsGamepad()) lb.player = m_pad;     // a paired player reads their own controller
        if (lb.IsDirection()) { const Vec2 v = lb.Vector(); return {v.x, v.y, 0}; }
        return {lb.Value(), 0, 0};
    };
    auto s = [&](std::size_t i) { return leaf(i).x; };
    InputValue v;
    switch (b.kind) {
        case BindingSpec::Kind::Single: v = leaf(0); break;
        case BindingSpec::Kind::Axis1D: v.x = s(1) - s(0); break;
        case BindingSpec::Kind::Vector2: {
            v.y = s(0) - s(1); v.x = s(3) - s(2);
            const float len = v.Length(); if (len > 1.0f) { v.x /= len; v.y /= len; }
            break;
        }
        case BindingSpec::Kind::Vector3: {
            v.y = s(0) - s(1); v.x = s(3) - s(2); v.z = s(4) - s(5);
            const float len = v.Length(); if (len > 1.0f) { v.x /= len; v.y /= len; v.z /= len; }
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
    m_events.clear();
    for (ActionDef& a : m_actions) {
        a.started = a.performed = a.canceled = false;
        if (a.phase == Phase::Performed || a.phase == Phase::Canceled) a.phase = Phase::Waiting;
        a.prevValue = a.value;
    }
    // Evaluate map by map, highest priority first; a consuming map blocks its inputs for the maps below it.
    std::vector<const ActionMap*> order;
    for (const ActionMap& m : m_maps) order.push_back(&m);
    std::stable_sort(order.begin(), order.end(), [](const ActionMap* x, const ActionMap* y) { return x->priority > y->priority; });
    m_blocked.clear();
    auto evalAction = [&](ActionDef& a, bool enabled) {
        InputValue best; float bestLen = -1.0f; int bestIdx = -1;
        for (std::size_t i = 0; i < a.bindings.size() && enabled; ++i) {
            BindingSpec& b = a.bindings[i];
            if (!BindingActive(b)) continue;
            InputValue v = Eval(b);
            for (Processor& p : b.processors) v = p.Apply(v, dt);
            const float len = v.Length();
            if (len > bestLen) { bestLen = len; best = v; bestIdx = (int)i; }
        }
        InputValue v = bestLen < 0 ? InputValue{} : best;
        for (Processor& p : a.processors) v = p.Apply(v, dt);
        switch (a.type) {   // the type decides which components exist
            case ValueType::Button: v = {v.Length(), 0, 0}; break;
            case ValueType::Axis1D: v = {v.x, 0, 0}; break;
            case ValueType::Vector2: v.z = 0; break;
            default: break;
        }
        const bool was = a.down;
        const float mag = a.type == ValueType::Button || a.type == ValueType::Axis1D ? std::fabs(v.x) : v.Length();
        a.value = v;
        a.activeBinding = bestLen > 0 ? bestIdx : -1;
        a.down = mag >= a.pressPoint;
        a.pressed = a.down && !was;
        a.released = !a.down && was;
    };
    auto runMap = [&](const std::string& name, bool enabled, bool consume) {
        std::set<std::string> mine;
        for (ActionDef& a : m_actions) {
            if (a.map != name) continue;
            evalAction(a, enabled);
            if (enabled && consume) for (const BindingSpec& b : a.bindings) if (BindingActive(b) && b.built) for (const InputBinding& l : b.leaves) mine.insert(l.ToString());
        }
        for (const std::string& k : mine) m_blocked.insert(k);
    };
    bool defaultDone = false;
    for (const ActionMap* m : order) {
        if (!defaultDone && m->priority < 0) { runMap("", true, false); defaultDone = true; }   // the unnamed map sits at priority 0
        runMap(m->name, m->enabled, m->consume);
    }
    if (!defaultDone) runMap("", true, false);
    m_blocked.clear();
    // Interactions run after every value is known, so a chorded action can look at another one.
    for (ActionDef& a : m_actions) RunInteractions(a, (a.down && !a.pressed) || a.released, dt);
}

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

std::string ActionSet::ExportOverrides() const {
    std::string s;
    for (const ActionDef& a : m_actions)
        for (std::size_t bi = 0; bi < a.bindings.size(); ++bi) {
            const BindingSpec& b = a.bindings[bi];
            if (b.original.empty() || b.original == b.parts) continue;
            s += "ov " + a.name + " " + std::to_string(bi) + " ";
            for (std::size_t i = 0; i < b.parts.size(); ++i) s += (i ? "," : "") + b.parts[i];
            s += "\n";
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
        if (line.rfind("ov ", 0) != 0) continue;
        const std::size_t s1 = line.find(' ', 3), s2 = s1 == std::string::npos ? s1 : line.find(' ', s1 + 1);
        if (s2 == std::string::npos) continue;
        const std::string name = line.substr(3, s1 - 3);
        const int bi = std::atoi(line.substr(s1 + 1, s2 - s1 - 1).c_str());
        std::vector<std::string> parts;
        std::size_t j = s2 + 1;
        while (j <= line.size()) { std::size_t c = line.find(',', j); if (c == std::string::npos) c = line.size(); parts.push_back(line.substr(j, c - j)); j = c + 1; }
        ActionDef* a = Find(name);
        if (!a || bi < 0 || bi >= (int)a->bindings.size() || parts.size() != a->bindings[(std::size_t)bi].parts.size()) continue;
        bool ok = true;
        for (const std::string& p : parts) if (InputBinding::Parse(p).kind == InputBinding::Kind::None) ok = false;
        if (!ok) continue;
        for (std::size_t k = 0; k < parts.size(); ++k) Rebind(name, bi, (int)k, parts[k]);
        ++applied;
    }
    return applied;
}

void ActionSet::ResetOverrides() {
    for (ActionDef& a : m_actions) for (BindingSpec& b : a.bindings) if (!b.original.empty()) { b.parts = b.original; b.original.clear(); b.built = false; }
}
bool ActionSet::ResetOverride(const std::string& action, int binding) {
    ActionDef* a = Find(action);
    if (!a || binding < 0 || binding >= (int)a->bindings.size() || a->bindings[(std::size_t)binding].original.empty()) return false;
    BindingSpec& b = a->bindings[(std::size_t)binding];
    b.parts = b.original; b.original.clear(); b.built = false;
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
