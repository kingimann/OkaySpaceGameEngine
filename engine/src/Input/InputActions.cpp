// The name-based front of the engine's one action table (InputRuntime::Actions(), an ActionSet). It keeps no actions,
// bindings or rebinding of its own: the built-in default actions and a game's Define calls are a list of definitions the
// table (and every local player's set) is given, the readers read the table, and every rebind changes the table. Written
// for this engine (duplicate audit #422 item 2: the old separate action table and its sync code are gone).
#include "okay/Input/InputActions.hpp"
#include "okay/Input/InputRuntime.hpp"
#include "okay/Input/ActionSet.hpp"
#include "okay/Input/InputAsset.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputRecorder.hpp"
#include "okay/Input/InputDebug.hpp"
#include "okay/Input/Haptics.hpp"
#include "okay/Input/Gestures.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Core/StringUtil.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace okay {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
// A saved action is "binding;binding" and a four-key binding is "keys:w,s,a,d", so a
// key that IS ';' or ',' (or the '%' used to escape them) is written %3b / %2c / %25.
std::string KeyText(char k) {
    switch (k) {
        case ';': return "%3b";
        case ',': return "%2c";
        case '%': return "%25";
        default:  return Lower(Input::KeyName(k));
    }
}
std::string Unescape(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 3 <= s.size()) {
            const std::string h = s.substr(i + 1, 2);
            if (h == "3b") { out += ';'; i += 2; continue; }
            if (h == "2c") { out += ','; i += 2; continue; }
            if (h == "25") { out += '%'; i += 2; continue; }
            if (h == "2b") { out += '+'; i += 2; continue; }
        }
        out += s[i];
    }
    return out;
}
const char* const kMouseNames[Input::kMouseButtons] = {"left", "right", "middle", "x1", "x2"};   // x1 / x2: the side buttons

} // namespace

// ---- InputBinding ------------------------------------------------------------

InputBinding InputBinding::Parse(const std::string& textIn) {
    InputBinding b;
    std::string text = Lower(Trim(textIn));
    // "@n" on the end names the controller.
    const bool padKind = text.rfind("pad:", 0) == 0 || text.rfind("stick:", 0) == 0 || text.rfind("dpad", 0) == 0;
    if (const std::size_t at = text.rfind('@'); padKind && at != std::string::npos) {
        const std::string n = Trim(text.substr(at + 1));
        if (n.size() == 1 && n[0] >= '0' && n[0] < '0' + Input::kMaxGamepads) b.player = n[0] - '0';
        else return b;   // a bad controller index is not a binding
        text = Trim(text.substr(0, at));
    }
    const std::size_t colon = text.find(':');
    const std::string kind = colon == std::string::npos ? text : text.substr(0, colon);
    const std::string arg = colon == std::string::npos ? "" : Trim(text.substr(colon + 1));
    if (kind == "dpad") { b.kind = Kind::DPad; return b; }
    if (kind == "wheel") {
        if (arg == "up") { b.kind = Kind::Wheel; b.code = 1; }
        else if (arg == "down") { b.kind = Kind::Wheel; b.code = -1; }
        else if (arg == "right") { b.kind = Kind::Wheel; b.code = 2; }
        else if (arg == "left") { b.kind = Kind::Wheel; b.code = -2; }
        return b;
    }
    if (kind == "gesture") {
        const int g = Gestures::NameIndex(arg);
        if (g >= 0) { b.kind = Kind::Gesture; b.code = g; }
        return b;
    }
    if (kind == "chord") {
        std::vector<std::string> parts;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= arg.size(); ++i)
            if (i == arg.size() || arg[i] == '+') { parts.push_back(Trim(arg.substr(start, i - start))); start = i + 1; }
        if (parts.size() >= 2 && parts.size() <= 4) {
            bool ok = true;
            for (std::size_t i = 0; i < parts.size(); ++i) {
                const std::string key = Unescape(parts[i]);
                if (key.empty() || !Input::IsKnownKeyName(key)) { ok = false; break; }
                b.keys[i] = Input::KeyFromName(key);
            }
            if (ok) b.kind = Kind::Chord;
            else for (char& k : b.keys) k = 0;
        }
        return b;
    }
    if (kind == "key" && !arg.empty() && Input::IsKnownKeyName(Unescape(arg))) {
        b.kind = Kind::Key; b.code = (unsigned char)Input::KeyFromName(Unescape(arg));
    } else if (kind == "mouse") {
        if (arg == "delta") { b.kind = Kind::MouseDelta; return b; }
        for (int i = 0; i < Input::kMouseButtons; ++i) if (arg == kMouseNames[i]) { b.kind = Kind::Mouse; b.code = i; }
    } else if (kind == "pad") {
        const int id = Input::GamepadButtonFromName(arg);
        if (id >= 0) { b.kind = Kind::PadButton; b.code = id; }
    } else if (kind == "stick") {
        if (arg == "left") b.kind = Kind::StickLeft;
        else if (arg == "right") b.kind = Kind::StickRight;
    } else if (kind == "keys") {
        std::vector<std::string> parts;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= arg.size(); ++i)
            if (i == arg.size() || arg[i] == ',') { parts.push_back(Trim(arg.substr(start, i - start))); start = i + 1; }
        if (parts.size() == 4) {
            bool ok = true;
            for (int i = 0; i < 4; ++i) {
                const std::string key = Unescape(parts[i]);
                if (key.empty() || !Input::IsKnownKeyName(key)) ok = false;
                else b.keys[i] = Input::KeyFromName(key);
            }
            if (ok) b.kind = Kind::Keys4;
        }
    }
    if (!b.IsGamepad()) b.player = 0;
    return b;
}

std::string InputBinding::ToString() const {
    const std::string at = player > 0 ? "@" + std::to_string(player) : "";
    switch (kind) {
        case Kind::Key:        return "key:" + KeyText((char)code);
        case Kind::Mouse:      return std::string("mouse:") + kMouseNames[code];
        case Kind::PadButton:  return "pad:" + Input::GamepadButtonKey(code) + at;
        case Kind::Keys4:      return "keys:" + KeyText(keys[0]) + "," + KeyText(keys[1]) + "," + KeyText(keys[2]) + "," + KeyText(keys[3]);
        case Kind::StickLeft:  return "stick:left" + at;
        case Kind::StickRight: return "stick:right" + at;
        case Kind::DPad:       return "dpad" + at;
        case Kind::MouseDelta: return "mouse:delta";
        case Kind::Wheel:      return code == 2 ? "wheel:right" : code == -2 ? "wheel:left" : code >= 0 ? "wheel:up" : "wheel:down";
        case Kind::Gesture:    return std::string("gesture:") + Gestures::NameKey(code);
        case Kind::Chord: {
            std::string out = "chord:";
            for (int i = 0; i < 4 && keys[i]; ++i) {
                if (i) out += '+';
                out += keys[i] == '+' ? std::string("%2b") : KeyText(keys[i]);
            }
            return out;
        }
        case Kind::None:       break;
    }
    return "";
}

std::string InputBinding::Label() const { return Label(Input::PadFamily::Xbox); }

std::string InputBinding::Label(Input::PadFamily family) const {
    const std::string who = player > 0 ? " (Pad " + std::to_string(player + 1) + ")" : "";
    switch (kind) {
        case Kind::Key:        return Input::KeyName((char)code);
        case Kind::Mouse:      return code == 0 ? "Left Mouse" : code == 1 ? "Right Mouse" : code == 2 ? "Middle Mouse" : "Mouse " + std::to_string(code + 1);   // the side buttons: "Mouse 4", "Mouse 5"
        case Kind::PadButton:  return Input::GamepadButtonLabel(code, family) + who;
        case Kind::Keys4:      return Input::KeyName(keys[0]) + " " + Input::KeyName(keys[2]) + " " +
                                      Input::KeyName(keys[1]) + " " + Input::KeyName(keys[3]);
        case Kind::StickLeft:  return "Left Stick" + who;
        case Kind::StickRight: return "Right Stick" + who;
        case Kind::DPad:       return "D-pad" + who;
        case Kind::MouseDelta: return "Mouse";
        case Kind::Wheel:      return code == 2 ? "Wheel Right" : code == -2 ? "Wheel Left" : code >= 0 ? "Wheel Up" : "Wheel Down";
        case Kind::Gesture:    return Gestures::NameLabel(code);
        case Kind::Chord: {
            std::string out;
            for (int i = 0; i < 4 && keys[i]; ++i) { if (i) out += " + "; out += Input::KeyName(keys[i]); }
            return out;
        }
        case Kind::None:       break;
    }
    return "None";
}

float InputBinding::Value() const {
    switch (kind) {
        case Kind::Key:       return Input::GetKeyGameExact((char)code) ? 1.0f : 0.0f;
        case Kind::Mouse:     return Input::GetMouseButton(code) ? 1.0f : 0.0f;
        case Kind::Wheel:     // one notch this frame: code +1 up, -1 down, +2 right, -2 left
            if (code == 2 || code == -2) return Input::MouseWheelX() * (code > 0 ? 1.0f : -1.0f) > 0.0f ? 1.0f : 0.0f;
            return Input::MouseWheel() * (code >= 0 ? 1.0f : -1.0f) > 0.0f ? 1.0f : 0.0f;
        case Kind::Gesture:   return Gestures::Happened(code) ? 1.0f : 0.0f;
        case Kind::Chord: {
            for (int i = 0; i < 4 && keys[i]; ++i) if (!Input::GetKeyGameExact(keys[i])) return 0.0f;
            return 1.0f;
        }
        case Kind::PadButton:
            if (code == Input::PadLT) return Input::GamepadTrigger(0, player);
            if (code == Input::PadRT) return Input::GamepadTrigger(1, player);
            return Input::GetGamepadButton(code, player) ? 1.0f : 0.0f;
        case Kind::None:      return 0.0f;
        default: {
            const Vec2 v = Vector();
            return std::min(1.0f, std::sqrt(v.x * v.x + v.y * v.y));
        }
    }
}

Vec2 InputBinding::Vector() const {
    switch (kind) {
        case Kind::Keys4:
            return {(Input::GetKeyGameExact(keys[3]) ? 1.0f : 0.0f) - (Input::GetKeyGameExact(keys[2]) ? 1.0f : 0.0f),
                    (Input::GetKeyGameExact(keys[0]) ? 1.0f : 0.0f) - (Input::GetKeyGameExact(keys[1]) ? 1.0f : 0.0f)};
        case Kind::StickLeft:  return Input::GamepadAxis(player);
        case Kind::StickRight: return Input::GamepadRightAxis(player);
        case Kind::DPad:
            return {(Input::GetGamepadButton(Input::PadRight, player) ? 1.0f : 0.0f) - (Input::GetGamepadButton(Input::PadLeft, player) ? 1.0f : 0.0f),
                    (Input::GetGamepadButton(Input::PadUp, player) ? 1.0f : 0.0f) - (Input::GetGamepadButton(Input::PadDown, player) ? 1.0f : 0.0f)};
        case Kind::MouseDelta: {
            const float s = InputActions::mouseDeltaScale > 0.0f ? InputActions::mouseDeltaScale : 1.0f;
            const Vec2 d = Input::MouseDelta();
            return {d.x / s, -d.y / s};   // screen y grows down; up is +y here
        }
        default: return {0.0f, 0.0f};
    }
}

// ---- InputActions --------------------------------------------------------------

namespace {

bool g_legacy = std::getenv("OKAY_OLD_INPUT") != nullptr && std::getenv("OKAY_OLD_INPUT")[0] != '0';
bool g_clockSeen = false;
std::chrono::steady_clock::time_point g_lastClock;

bool IsBase(const std::string& c) { return IsBaseMapName(c); }

// What code asked for: the built-in defaults and every Define / DefineIn / SetContext / SetThreshold / SetPerPlayer, kept so
// a table built again (another asset, a new play session) and every player that joins gets them too.
struct CodeDef {
    std::string name;
    std::vector<InputBinding> bindings;
    bool direction = false;
    bool builtin = false;          ///< one of the engine's defaults: an asset action of that name wins
    std::string context;           ///< "" = the base context
    bool hasContext = false;
    float threshold = -1.0f;       ///< < 0 = not set by code
    bool perPlayer = false;
};
std::vector<CodeDef>& Registry() { static std::vector<CodeDef> r; return r; }
bool g_builtins = false;   // DefineDefaults is running
bool g_defaultsDone = false;

struct ContextEntry { std::string name; InputActions::ContextMode mode; };
std::vector<ContextEntry>& Stack() { static std::vector<ContextEntry> s; return s; }

CodeDef* CodeFind(const std::string& name) {
    const std::string n = Lower(name);
    for (CodeDef& d : Registry()) if (Lower(d.name) == n) return &d;
    return nullptr;
}

void DefineDefaults();
void EnsureDefaults() { if (!g_defaultsDone) { g_defaultsDone = true; DefineDefaults(); } }

ActionSet& T() { EnsureDefaults(); return InputRuntime::Actions(); }

// One InputActions binding as a typed binding (a four-key direction is a vector2 of its keys) and back.
BindingSpec SpecOf(const InputBinding& b, const std::string& group) {
    BindingSpec s;
    s.kind = b.kind == InputBinding::Kind::Keys4 ? BindingSpec::Kind::Vector2 : BindingSpec::Kind::Single;
    s.parts = BindingParts(b);
    s.group = group;
    return s;
}
// The typed binding as one binding this front can show; Kind::None when it cannot (a composite with a pad button in it,
// a 1D axis or a 3D vector): such a binding is still read, just not shown or rebound here.
InputBinding Representable(const BindingSpec& s) {
    InputBinding b;
    if (s.kind == BindingSpec::Kind::Single && s.parts.size() == 1) return InputBinding::Parse(s.parts[0]);
    if (s.kind == BindingSpec::Kind::Vector2 && s.parts.size() == 4) {
        for (std::size_t j = 0; j < 4; ++j) {
            const InputBinding k = InputBinding::Parse(s.parts[j]);
            if (k.kind != InputBinding::Kind::Key || k.code <= 0 || k.code > 255) return InputBinding{};
            b.keys[j] = (char)k.code;
        }
        b.kind = InputBinding::Kind::Keys4;
    }
    return b;
}
// The scheme a new binding belongs to in a set with schemes: the one for its device, "" when there are none.
std::string GroupFor(const ActionSet& set, const InputBinding& b) {
    for (const ControlScheme& c : set.Schemes())
        if (b.IsGamepad() ? c.gamepad : (c.keyboard || c.mouse)) return c.name;
    return "";
}

// "dead;curve;sensitivity;invertX;invertY", kept only when it differs from neutral.
bool LoadResponse(const std::string& action, InputActions::Response& r) {
    const std::string key = "inputresp." + Lower(action);
    if (!Prefs::Has(key)) return false;
    float v[3] = {0.0f, 1.0f, 1.0f}; int inv[2] = {0, 0};
    const std::string t = Prefs::GetString(key);
    if (std::sscanf(t.c_str(), "%f;%f;%f;%d;%d", &v[0], &v[1], &v[2], &inv[0], &inv[1]) < 3) return false;
    r.deadzone = std::clamp(v[0], 0.0f, 0.95f); r.curve = std::clamp(v[1], 0.1f, 8.0f);
    r.sensitivity = std::clamp(v[2], 0.05f, 20.0f); r.invertX = inv[0] != 0; r.invertY = inv[1] != 0;
    return true;
}
void SaveResponse(const std::string& action, const InputActions::Response& r) {
    const std::string key = "inputresp." + Lower(action);
    if (r.deadzone == 0.0f && r.curve == 1.0f && r.sensitivity == 1.0f && !r.invertX && !r.invertY) { Prefs::Delete(key); return; }
    char buf[96];
    std::snprintf(buf, sizeof buf, "%g;%g;%g;%d;%d", r.deadzone, r.curve, r.sensitivity, r.invertX ? 1 : 0, r.invertY ? 1 : 0);
    Prefs::SetString(key, buf);
}

std::string Joined(const std::vector<InputBinding>& b) {
    std::string out;
    for (const auto& x : b) { if (!out.empty()) out += ';'; out += x.ToString(); }
    return out;
}
std::vector<InputBinding> ParseList(const std::string& text) {
    std::vector<InputBinding> list;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i)
        if (i == text.size() || text[i] == ';') {
            InputBinding b = InputBinding::Parse(text.substr(start, i - start));
            if (b.kind != InputBinding::Kind::None) list.push_back(b);
            start = i + 1;
        }
    return list;
}

// Give `set` the action list `list` as its bindings for `d` (the shape-aware ActionSet::SetBindings), keeping each
// binding's scheme where the device class stays the same.
void ApplyList(ActionSet& set, ActionDef& d, const std::vector<InputBinding>& list) {
    std::vector<BindingSpec> specs;
    for (std::size_t i = 0; i < list.size(); ++i) {
        std::string group = GroupFor(set, list[i]);
        if (i < d.bindings.size()) {
            const InputBinding old = Representable(d.bindings[i]);
            if (old.kind != InputBinding::Kind::None && old.IsGamepad() == list[i].IsGamepad()) group = d.bindings[i].group;
        }
        specs.push_back(SpecOf(list[i], group));
    }
    set.SetBindings(d.name, specs);
}

// One code definition into a set: a built-in default only adds what the set lacks; a game's Define replaces the bindings.
void ApplyDef(ActionSet& set, const CodeDef& c) {
    ActionDef* d = set.FindNoCase(c.name);
    const bool keepAsset = d && c.builtin;
    if (!keepAsset) {
        std::string map = c.hasContext ? (IsBase(c.context) ? std::string() : c.context) : (d ? d->map : std::string());
        if (!map.empty()) {
            bool have = false;
            for (const ActionMap& m : set.Maps()) if (Lower(m.name) == Lower(map)) { have = true; map = m.name; }
            if (!have) set.AddMap(map, 0, false, false);   // a context starts off until it is pushed
        }
        const ValueType type = c.direction ? ValueType::Vector2 : ValueType::Button;
        if (!d) d = &set.Add(c.name, type, map);
        else { d->type = type; d->map = map; }
        d->bindings.clear();
        d->authored.clear(); d->reshaped = false;
        for (const InputBinding& b : c.bindings) d->bindings.push_back(SpecOf(b, ""));
        d->sum = c.direction;
    } else if (c.hasContext) {
        d->map = IsBase(c.context) ? (IsBase(d->map) ? d->map : std::string()) : c.context;
        if (!d->map.empty() && !IsBase(d->map)) { bool have = false; for (const ActionMap& m : set.Maps()) if (m.name == d->map) have = true; if (!have) set.AddMap(d->map, 0, false, false); }
    }
    if (c.threshold >= 0.0f) d->pressPoint = c.threshold;
    if (c.perPlayer) d->perPlayer = true;
    LoadResponse(d->name, d->response);
    set.Touch();
}

// The player's saved rebinding of one action (or of all, with ""). An action with an "input.<action>" pref (the form every
// version writes for what this front can show) loads from it; the rest from the typed store "inputov.player0". Save writes
// both from the one table, so they only differ when something outside wrote one of them.
void LoadSaved(ActionSet& set, const std::string& only) {
    std::vector<std::string> fromOld;   // lower-case names loaded from "input.<action>"
    for (const ActionDef& d0 : set.Actions()) {
        if (!only.empty() && Lower(d0.name) != Lower(only)) continue;
        const std::string key = "input." + Lower(d0.name);
        if (!Prefs::Has(key)) continue;
        const std::vector<InputBinding> list = ParseList(Prefs::GetString(key));
        if (list.empty()) continue;
        ApplyList(set, *set.FindNoCase(d0.name), list);
        fromOld.push_back(Lower(d0.name));
    }
    if (!Prefs::Has("inputov.player0")) return;
    const std::string text = Prefs::GetString("inputov.player0");
    std::string mine;   // the lines this load applies
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t e = text.find('\n', i); e = e == std::string::npos ? text.size() : e + 1;
        std::string line = text.substr(i, e - i);
        i = e;
        if (line.empty() || line.back() != '\n') line += '\n';
        const std::size_t s1 = line.find(' ');
        std::size_t s2 = s1 == std::string::npos ? s1 : line.find_first_of(" \n", s1 + 1);
        if (s2 == std::string::npos) continue;
        const std::string who = Lower(line.substr(s1 + 1, s2 - s1 - 1));
        if (!only.empty() && who != Lower(only)) continue;
        if (std::find(fromOld.begin(), fromOld.end(), who) != fromOld.end()) continue;
        mine += line;
    }
    set.ImportOverrides(mine);
}

// ---- the read-only view All() / Find() hand out -----------------------------------------------------------------------
struct View { unsigned serial = 0, rev = 0; std::vector<InputActions::Action> list; };
View& V() { static View v; return v; }
void FillRuntime(InputActions::Action& v, const ActionDef& d) {
    v.value = d.Magnitude();
    v.previous = d.PrevMagnitude();
    v.vector = {d.value.x, d.value.y};
    for (int p = 0; p < Input::kMaxGamepads; ++p) { v.players[p].value = d.pads[p].value; v.players[p].previous = d.pads[p].previous; v.players[p].vector = d.pads[p].vector; }
}
std::vector<InputActions::Action>& ViewList() {
    ActionSet& t = T();
    View& v = V();
    if (v.serial == InputRuntime::ActionsSerial() && v.rev == t.Revision() && v.list.size() == t.Actions().size()) return v.list;
    v.list.resize(t.Actions().size());   // in place: a pointer from Find stays good while the action count does not change
    std::size_t i = 0;
    for (const ActionDef& d : t.Actions()) {
        InputActions::Action& a = v.list[i++];
        a.name = d.name;
        a.direction = d.type == ValueType::Vector2 || d.type == ValueType::Vector3;
        a.bindings.clear();
        for (const BindingSpec& b : d.bindings) a.bindings.push_back(Representable(b));
        a.defaults.clear();
        for (const BindingSpec& b : t.AuthoredBindings(d)) a.defaults.push_back(Representable(b));
        a.context = IsBase(d.map) ? std::string() : d.map;
        a.threshold = d.pressPoint;
        a.deadzone = d.response.deadzone; a.curve = d.response.curve; a.sensitivity = d.response.sensitivity;
        a.invertX = d.response.invertX; a.invertY = d.response.invertY;
        a.perPlayer = d.perPlayer;
        FillRuntime(a, d);
    }
    v.serial = InputRuntime::ActionsSerial(); v.rev = t.Revision();
    return v.list;
}
void RefreshRuntime() {
    std::vector<InputActions::Action>& list = ViewList();
    std::size_t i = 0;
    for (const ActionDef& d : T().Actions()) if (i < list.size()) FillRuntime(list[i++], d);
}

const ActionDef* Def(const std::string& name) { return T().FindNoCase(name); }

void Register(const std::string& name, const std::vector<std::string>& bindings, bool direction, const std::string* context) {
    std::vector<InputBinding> parsed;
    for (const auto& t : bindings) {
        InputBinding b = InputBinding::Parse(t);
        if (b.kind != InputBinding::Kind::None) parsed.push_back(b);
    }
    CodeDef* c = CodeFind(name);
    if (!c) { Registry().emplace_back(); c = &Registry().back(); c->name = name; }
    c->bindings = parsed; c->direction = direction; c->builtin = g_builtins;
    if (context) { c->context = *context; c->hasContext = true; }
    ActionSet* t = InputRuntime::ActionsIfBuilt();
    if (!t) return;   // the table is built from the list (now or later)
    const CodeDef def = *c;
    InputRuntime::ForEachSet([](ActionSet& set, const void* p) { ApplyDef(set, *static_cast<const CodeDef*>(p)); }, &def);
    if (!def.builtin) LoadSaved(*t, def.name);   // a game defining its actions at start keeps the player's own rebinding
    InputRuntime::SyncPlayer0();
}

void DefineDefaults() {
    Registry().clear();
    g_builtins = true;
    InputActions::Define("Move", {"keys:w,s,a,d", "keys:up,down,left,right", "stick:left", "dpad"}, true);
    InputActions::Define("Look", {"mouse:delta", "stick:right"}, true);
    InputActions::Define("Jump", {"key:space", "pad:a"});
    InputActions::Define("Sprint", {"key:shift", "pad:ls"});
    InputActions::Define("Crouch", {"key:ctrl", "pad:b"});
    InputActions::Define("Fire", {"mouse:left", "pad:rt"});
    InputActions::Define("Aim", {"mouse:right", "pad:lt"});
    InputActions::Define("Interact", {"key:e", "pad:x"});
    InputActions::Define("Reload", {"key:r", "pad:y"});
    // Voxel sandbox (VoxelDigger): dig, place, pick the material under the aim, and step the hotbar. Rebindable like the rest.
    // Dig and Place start with no bindings of their own: until a player rebinds them the digger uses Fire (left mouse, RT) and
    // Aim (right mouse, LT), so no two actions share a default key (the remap screen reports shared keys as conflicts).
    InputActions::Define("Dig", {});
    InputActions::Define("Place", {});
    InputActions::Define("PickBlock", {"key:t", "pad:rs"});
    InputActions::Define("HotbarNext", {"pad:rb"});
    InputActions::Define("HotbarPrev", {"pad:lb"});
    InputActions::Define("Inventory", {"key:tab", "pad:back"});
    InputActions::Define("Pause", {"key:escape", "pad:start"});
    // Built-in controllers' extra keys (prone, lean, dash). They start with no binding of their own: their stock keys
    // (Z, Q, E, Space) are shared with Interact and Jump, or are keys games often give their own actions, and the
    // Controls screen would report those as clashes. The controllers keep their stock keys until the player binds
    // these (ControlKey's stockKey).
    InputActions::Define("Prone", {});
    InputActions::Define("LeanLeft", {});
    InputActions::Define("LeanRight", {});
    InputActions::Define("Dash", {});
    // Vehicle Controller driving keys, in the "Vehicle" context: a key may drive and walk (W is Throttle and Move), and
    // the Controls screen reports a shared key only inside one context. While "Vehicle" is not pushed the controller
    // reads them straight from their bindings. Keyboard only: the controller's own pad fields stay as they are.
    InputActions::DefineIn("Vehicle", "Throttle", {"key:w", "key:up"});
    InputActions::DefineIn("Vehicle", "Brake", {"key:s", "key:down"});
    InputActions::DefineIn("Vehicle", "SteerLeft", {"key:a", "key:left"});
    InputActions::DefineIn("Vehicle", "SteerRight", {"key:d", "key:right"});
    InputActions::DefineIn("Vehicle", "Handbrake", {"key:space"});
    InputActions::DefineIn("Vehicle", "Boost", {"key:shift"});
    InputActions::DefineIn("Vehicle", "GearUp", {"key:x"});
    InputActions::DefineIn("Vehicle", "GearDown", {"key:z"});
    InputActions::DefineIn("Vehicle", "Horn", {"key:h"});
    InputActions::DefineIn("Vehicle", "Headlights", {"key:l"});
    g_builtins = false;
}

void PushTo(ActionSet& set, const ContextEntry& c) {
    set.PushMap(c.name, c.mode == InputActions::ContextMode::Consume, c.mode == InputActions::ContextMode::Block);
}

} // namespace

void InputActions::PrepareSet(ActionSet& set) {
    EnsureDefaults();
    for (const CodeDef& c : Registry()) ApplyDef(set, c);
    for (const ActionDef& d : set.Actions()) { InputActions::Response r; if (LoadResponse(d.name, r)) set.FindNoCase(d.name)->response = r; }
    for (const ContextEntry& c : Stack()) PushTo(set, c);
}
void InputActions::PrepareTable(ActionSet& set) {
    PrepareSet(set);
    LoadSaved(set, "");
}

void InputActions::Define(const std::string& name, const std::vector<std::string>& bindings, bool direction) {
    EnsureDefaults();
    Register(name, bindings, direction, nullptr);
}
void InputActions::DefineIn(const std::string& context, const std::string& name,
                            const std::vector<std::string>& bindings, bool direction) {
    EnsureDefaults();
    Register(name, bindings, direction, &context);
}

bool InputActions::Has(const std::string& name) { return Def(name) != nullptr; }
const std::vector<InputActions::Action>& InputActions::All() { return ViewList(); }
InputActions::Action* InputActions::Find(const std::string& name) {
    std::vector<Action>& list = ViewList();
    for (Action& a : list) if (str::EqualsNoCaseAscii(a.name, name)) return &a;
    return nullptr;
}

void InputActions::SetLegacy(bool on) { g_legacy = on; }
bool InputActions::Legacy() { return g_legacy; }
double InputActions::Now() { return T().Time(); }

float InputActions::ShapeValue(float in, const Response& r) { return ShapeResponse(in, r); }
bool InputActions::SetResponse(const std::string& action, const Response& rIn) {
    const ActionDef* d = Def(action);
    if (!d) return false;
    Response r;
    r.deadzone = std::clamp(rIn.deadzone, 0.0f, 0.95f);
    r.curve = std::clamp(rIn.curve, 0.1f, 8.0f);
    r.sensitivity = std::clamp(rIn.sensitivity, 0.05f, 20.0f);
    r.invertX = rIn.invertX; r.invertY = rIn.invertY;
    SaveResponse(d->name, r);
    struct Arg { std::string name; Response r; } arg{d->name, r};
    InputRuntime::ForEachSet([](ActionSet& set, const void* p) {
        const Arg& a = *static_cast<const Arg*>(p);
        if (ActionDef* x = set.FindNoCase(a.name)) { x->response = a.r; set.Touch(); }
    }, &arg);
    return true;
}
InputActions::Response InputActions::GetResponse(const std::string& action) {
    const ActionDef* d = Def(action);
    return d ? d->response : Response{};
}

void InputActions::Tick() {
    // The clock: real elapsed time between ticks (a long pause counts as a quarter second).
    const auto nowClock = std::chrono::steady_clock::now();
    float dt = 0.0f;
    if (g_clockSeen) dt = std::min(0.25f, std::chrono::duration<float>(nowClock - g_lastClock).count());
    g_clockSeen = true; g_lastClock = nowClock;
    Tick(dt);
}

void InputActions::Tick(float dt) {
    ActionSet& t = T();
    const float d = dt > 0.0f ? dt : 0.0f;
    InputHaptics::Update(d);
    InputRecorder::BeginFrame(d);   // playback puts the recorded frame over what the runtime fed
    Gestures::ApplyBound();         // gestures bound with Gestures::Bind drive their actions (once per Gestures::Tick)
    t.Update(d);                    // the one evaluation: every action, map (context), chord, response and on-screen control
    InputRuntime::Update(d);        // local players read the same frame from their copies
    RefreshRuntime();
    InputRecorder::EndFrame(d);
    InputDebug::Record();
}

void InputActions::Contribute(const std::string& action, float value) { if (!action.empty()) T().Contribute(action, value); }
void InputActions::ContributeVector(const std::string& action, const Vec2& vector) { if (!action.empty()) T().ContributeVector(action, vector); }
void InputActions::ClearContributions() { T().ClearContributions(); }

bool InputActions::SetContext(const std::string& action, const std::string& context) {
    const ActionDef* d = Def(action);
    if (!d) return false;
    CodeDef* c = CodeFind(d->name);
    if (!c) {   // an asset action moved by code: remember it as a code definition that keeps the asset's bindings
        Registry().emplace_back(); c = &Registry().back(); c->name = d->name; c->builtin = true;
    }
    c->context = context; c->hasContext = true;
    const CodeDef def = *c;
    InputRuntime::ForEachSet([](ActionSet& set, const void* p) { ApplyDef(set, *static_cast<const CodeDef*>(p)); }, &def);
    return true;
}
void InputActions::PushContext(const std::string& context, ContextMode mode) {
    if (IsBase(context)) return;
    PopContext(context);
    Stack().push_back({context, mode});
    T();
    InputRuntime::ForEachSet([](ActionSet& set, const void* p) { PushTo(set, *static_cast<const ContextEntry*>(p)); }, &Stack().back());
}
void InputActions::PopContext(const std::string& context) {
    auto& s = Stack();
    const std::size_t before = s.size();
    s.erase(std::remove_if(s.begin(), s.end(), [&](const ContextEntry& c) { return Lower(c.name) == Lower(context); }), s.end());
    if (s.size() != before) InputRuntime::ForEachSet([](ActionSet& set, const void* p) { set.PopMap(*static_cast<const std::string*>(p)); }, &context);
}
void InputActions::ClearContexts() {
    while (!Stack().empty()) PopContext(Stack().back().name);
}
bool InputActions::ContextActive(const std::string& context) {
    if (IsBase(context)) return true;
    for (const ActionMap& m : T().Maps()) if (Lower(m.name) == Lower(context)) return m.enabled;
    return false;
}
std::string InputActions::TopContext() { return Stack().empty() ? std::string() : Stack().back().name; }
bool InputActions::KeyConsumed(char key) { InputBinding b; b.kind = InputBinding::Kind::Key; b.code = (unsigned char)key; return T().Consumed(b); }
bool InputActions::PadButtonConsumed(int button, int player) {
    InputBinding b; b.kind = InputBinding::Kind::PadButton; b.code = button; b.player = player;
    return T().Consumed(b);
}
void InputActions::SetThreshold(const std::string& action, float t) {
    const ActionDef* d = Def(action);
    if (!d) return;
    CodeDef* c = CodeFind(d->name);
    if (!c) { Registry().emplace_back(); c = &Registry().back(); c->name = d->name; c->builtin = true; }
    c->threshold = t < 0.01f ? 0.01f : (t > 1.0f ? 1.0f : t);
    const CodeDef def = *c;
    InputRuntime::ForEachSet([](ActionSet& set, const void* p) {
        const CodeDef& c = *static_cast<const CodeDef*>(p);
        if (ActionDef* x = set.FindNoCase(c.name)) { x->pressPoint = c.threshold; set.Touch(); }
    }, &def);
}

bool InputActions::Get(const std::string& name) { const ActionDef* d = Def(name); return d && d->down; }
bool InputActions::Down(const std::string& name) { const ActionDef* d = Def(name); return d && d->pressed; }
bool InputActions::Up(const std::string& name) { const ActionDef* d = Def(name); return d && d->released; }
float InputActions::Value(const std::string& name) { const ActionDef* d = Def(name); return d ? d->Magnitude() : 0.0f; }
Vec2 InputActions::Vector(const std::string& name) { const ActionDef* d = Def(name); return d ? Vec2{d->value.x, d->value.y} : Vec2{0.0f, 0.0f}; }

void InputActions::SetPerPlayer(const std::string& action, bool on) {
    const ActionDef* d = Def(action);
    if (!d) return;
    CodeDef* c = CodeFind(d->name);
    if (!c) { Registry().emplace_back(); c = &Registry().back(); c->name = d->name; c->builtin = true; }
    c->perPlayer = on;
    const CodeDef def = *c;
    InputRuntime::ForEachSet([](ActionSet& set, const void* p) {
        const CodeDef& c = *static_cast<const CodeDef*>(p);
        if (ActionDef* x = set.FindNoCase(c.name)) x->perPlayer = c.perPlayer;
    }, &def);
}
static const ActionDef::PadState* PadOf(const std::string& name, int player, float& threshold) {
    const ActionDef* d = Def(name);
    if (!d || player < 0 || player >= Input::kMaxGamepads) return nullptr;
    threshold = d->pressPoint;
    return &d->pads[player];
}
bool InputActions::Get(const std::string& name, int p) { float t = 0.5f; const auto* s = PadOf(name, p, t); return s && s->value >= t; }
bool InputActions::Down(const std::string& name, int p) { float t = 0.5f; const auto* s = PadOf(name, p, t); return s && s->value >= t && s->previous < t; }
bool InputActions::Up(const std::string& name, int p) { float t = 0.5f; const auto* s = PadOf(name, p, t); return s && s->value < t && s->previous >= t; }
float InputActions::Value(const std::string& name, int p) { float t = 0.5f; const auto* s = PadOf(name, p, t); return s ? s->value : 0.0f; }
Vec2 InputActions::Vector(const std::string& name, int p) { float t = 0.5f; const auto* s = PadOf(name, p, t); return s ? s->vector : Vec2{0.0f, 0.0f}; }

bool InputActions::Buffered(const std::string& name, float window) { return T().Buffered(name, window); }
bool InputActions::Consume(const std::string& name) { return T().ConsumeAny(name); }
bool InputActions::DoubleTap(const std::string& name, float window) { return T().DoubleTap(name, window); }
float InputActions::HeldFor(const std::string& name) { return T().HeldFor(name); }
float InputActions::HoldProgress(const std::string& name, float seconds) {
    if (!(seconds > 0.0f)) return 0.0f;
    return std::min(1.0f, HeldFor(name) / seconds);
}
bool InputActions::HoldReached(const std::string& name, float seconds) { return T().HoldReached(name, seconds); }

const InputBinding* InputActions::Current(const std::string& name, bool gamepad) {
    const Action* a = Find(name);
    if (!a) return nullptr;
    for (const InputBinding& b : a->bindings)
        if (b.kind != InputBinding::Kind::None && b.IsGamepad() == gamepad && b.kind != InputBinding::Kind::MouseDelta) return &b;
    return nullptr;
}

bool InputActions::SetBindings(const std::string& name, const std::vector<InputBinding>& list) {
    ActionSet& t = T();
    ActionDef* d = t.FindNoCase(name);
    if (!d) return false;
    ApplyList(t, *d, list);
    InputRuntime::SyncPlayer0();
    return true;
}

bool InputActions::Rebind(const std::string& name, const std::string& text, int part) {
    const Action* a = Find(name);
    if (!a) return false;
    InputBinding nb = InputBinding::Parse(text);
    if (nb.kind == InputBinding::Kind::None) return false;
    // A direction action (Move, Look) needs something that points: a lone key or button would
    // replace its four-key binding and leave it reading (0, 0) for ever.
    if (a->direction && part < 0 && !nb.IsDirection()) return false;
    std::vector<InputBinding> list = a->bindings;
    const bool pad = nb.IsGamepad();
    InputBinding* slot = nullptr;
    for (InputBinding& b : list)
        if (b.kind != InputBinding::Kind::None && b.IsGamepad() == pad && b.kind != InputBinding::Kind::MouseDelta && b.IsDirection() == nb.IsDirection()) { slot = &b; break; }
    if (!slot)
        for (InputBinding& b : list)
            if (b.kind != InputBinding::Kind::None && b.IsGamepad() == pad && b.kind != InputBinding::Kind::MouseDelta) { slot = &b; break; }
    if (part >= 0 && part < 4) {
        // One key of a four-key direction.
        if (!slot || slot->kind != InputBinding::Kind::Keys4 || nb.kind != InputBinding::Kind::Key) return false;
        slot->keys[part] = (char)nb.code;
    } else if (slot) {
        *slot = nb;
    } else {
        list.push_back(nb);
    }
    const std::string real = a->name;
    SetBindings(real, list);
    Save();
    return true;
}

void InputActions::Reset(const std::string& name) {
    ActionSet& t = T();
    for (const ActionDef& d : t.Actions())
        if (name.empty() || Lower(d.name) == Lower(name)) {
            t.SetBindings(d.name, t.AuthoredBindings(d));
            Prefs::Delete("input." + Lower(d.name));
        }
    InputRuntime::SyncPlayer0();
    Save();
}

void InputActions::Load() {
    ActionSet& t = T();
    t.ResetOverrides();
    LoadSaved(t, "");
    for (const ActionDef& d : t.Actions()) { Response r; if (LoadResponse(d.name, r)) t.FindNoCase(d.name)->response = r; }
    t.Touch();
    InputRuntime::SyncPlayer0();
}

void InputActions::Save() {
    ActionSet& t = T();
    t.SaveOverrides("player0");
    // The same store in the per-action form every earlier version wrote and read: only what the player changed, and only
    // for actions this front can show whole.
    for (const Action& a : All()) {
        const std::string key = "input." + Lower(a.name);
        bool whole = true;
        for (const InputBinding& b : a.bindings) if (b.kind == InputBinding::Kind::None) whole = false;
        const std::string now = Joined(a.bindings);
        if (!whole || now == Joined(a.defaults)) Prefs::Delete(key);
        else Prefs::SetString(key, now);
    }
    InputRuntime::SyncPlayer0();
}

std::string InputActions::ExportBindings() {
    std::string out = "okayinput 1\n";
    for (const Action& a : All()) out += a.name + "=" + Joined(a.bindings) + "\n";
    return out;
}
bool InputActions::ImportBindings(const std::string& text) {
    std::size_t pos = text.find('\n');
    if (text.rfind("okayinput ", 0) != 0 || pos == std::string::npos) return false;
    if (std::atoi(text.c_str() + 10) != 1) return false;   // a newer file than we understand
    bool any = false;
    while (pos != std::string::npos && pos + 1 < text.size()) {
        const std::size_t end = text.find('\n', pos + 1);
        const std::string line(text, pos + 1, end == std::string::npos ? std::string::npos : end - pos - 1);
        pos = end;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (!Def(line.substr(0, eq))) continue;
        const std::vector<InputBinding> list = ParseList(line.substr(eq + 1));
        if (!list.empty() && SetBindings(line.substr(0, eq), list)) any = true;
    }
    if (any) Save();
    return any;
}
bool InputActions::SaveProfile(const std::string& profile) {
    if (profile.empty()) return false;
    Prefs::SetString("inputprofile." + Lower(profile), ExportBindings());
    return true;
}
bool InputActions::LoadProfile(const std::string& profile) {
    if (!HasProfile(profile)) return false;
    return ImportBindings(Prefs::GetString("inputprofile." + Lower(profile)));
}
bool InputActions::HasProfile(const std::string& profile) { return !profile.empty() && Prefs::Has("inputprofile." + Lower(profile)); }
void InputActions::DeleteProfile(const std::string& profile) { Prefs::Delete("inputprofile." + Lower(profile)); }

bool InputActions::ApplyPreset(const std::string& preset) {
    const std::string p = Lower(preset);
    struct Row { const char* action; const char* left; const char* right; };
    static const Row kRows[] = {
        {"Jump", "key:space", "key:o"}, {"Sprint", "key:shift", "key:p"}, {"Crouch", "key:c", "key:u"},
        {"Fire", "key:f", "key:h"}, {"Aim", "key:g", "key:y"}, {"Interact", "key:e", "key:n"},
        {"Reload", "key:r", "key:m"}, {"Inventory", "key:tab", "key:enter"}, {"Pause", "key:escape", "key:backspace"},
    };
    if (p != "left" && p != "right") return false;
    for (const Row& r : kRows) Rebind(r.action, p == "left" ? r.left : r.right);
    Rebind("Move", p == "left" ? "keys:w,s,a,d" : "keys:i,k,j,l");
    return true;
}

namespace {
bool SameBinding(const InputBinding& a, const InputBinding& b) {
    if (a.kind != b.kind || a.code != b.code || a.player != b.player) return false;
    for (int i = 0; i < 4; ++i) if (a.keys[i] != b.keys[i]) return false;
    return true;
}
// The action follows its bindings instead of the controller's key: it was rebound and the key is one of
// the action's default bindings of that kind (the designer did not pick a key of their own).
bool FollowsAction(const std::string& action, InputBinding::Kind kind, int code, int stock = 0) {
    const InputActions::Action* a = InputActions::Find(action);
    if (!a || !InputActions::Rebound(action)) return false;
    if (kind == InputBinding::Kind::Key && stock != 0 && code == stock) return true;
    for (const InputBinding& d : a->defaults) if (d.kind == kind && d.code == code) return true;
    return false;
}
// Tick samples it: a base action, or one whose context is pushed. Anything else is read from the devices.
bool Live(const InputActions::Action& a) { return a.context.empty() || InputActions::ContextActive(a.context); }
bool ActionHeld(const std::string& name) {
    const InputActions::Action* a = InputActions::Find(name);
    if (!a) return false;
    return Live(*a) ? InputActions::Get(name) : InputActions::DirectHeld(name);
}
bool ActionDown(const std::string& name) {
    const InputActions::Action* a = InputActions::Find(name);
    if (!a) return false;
    return Live(*a) ? InputActions::Down(name) : InputActions::DirectDown(name);
}
// Pressed this frame, from the device's own edge.
bool BindingDown(const InputBinding& b) {
    using K = InputBinding::Kind;
    switch (b.kind) {
        case K::Key: return Input::GetKeyDown((char)b.code);
        case K::Mouse: return Input::GetMouseButtonDown(b.code);
        case K::PadButton: return Input::GetGamepadButtonDown(b.code, b.player);
        case K::Wheel: case K::Gesture: return b.Value() > 0.0f;   // one notch, or one gesture, is already one frame
        case K::Chord: {
            bool any = false;
            for (char k : b.keys) {
                if (!k) break;
                if (!Input::GetKey(k)) return false;
                if (Input::GetKeyDown(k)) any = true;
            }
            return any;
        }
        default: return false;   // a direction is not a button
    }
}
} // namespace

bool InputActions::Rebound(const std::string& name) {
    const Action* a = Find(name);
    if (!a) return false;
    if (a->bindings.size() != a->defaults.size()) return true;
    for (std::size_t i = 0; i < a->bindings.size(); ++i) if (!SameBinding(a->bindings[i], a->defaults[i])) return true;
    return false;
}
bool InputActions::ControlKey(const std::string& action, char key, char stockKey) {
    if (!key) return false;
    return FollowsAction(action, InputBinding::Kind::Key, (unsigned char)key, (unsigned char)stockKey) ? ActionHeld(action) : Input::GetKey(key);
}
bool InputActions::ControlKeyDown(const std::string& action, char key, char stockKey) {
    if (!key) return false;
    return FollowsAction(action, InputBinding::Kind::Key, (unsigned char)key, (unsigned char)stockKey) ? ActionDown(action) : Input::GetKeyDown(key);
}
bool InputActions::ControlMouse(const std::string& action, int button) {
    if (button < 0) return false;
    return FollowsAction(action, InputBinding::Kind::Mouse, button) ? ActionHeld(action) : Input::GetMouseButton(button);
}
bool InputActions::ControlMouseDown(const std::string& action, int button) {
    if (button < 0) return false;
    return FollowsAction(action, InputBinding::Kind::Mouse, button) ? ActionDown(action) : Input::GetMouseButtonDown(button);
}
bool InputActions::ControlDirKeyDown(const std::string& action, int part, char key) {
    if (!key || part < 0 || part > 3) return false;
    const Action* a = Find(action);
    if (!a || !Rebound(action)) return Input::GetKeyDown(key);
    bool isDefault = false;
    for (const InputBinding& d : a->defaults)
        if (d.kind == InputBinding::Kind::Keys4 && d.keys[part] == key) isDefault = true;
    if (!isDefault) return Input::GetKeyDown(key);   // the designer's own key
    if (Input::TextFocused()) return false;
    for (const InputBinding& b : a->bindings)
        if (b.kind == InputBinding::Kind::Keys4 && b.keys[part] && Input::GetKeyDown(b.keys[part])) return true;
    return false;
}
Vec2 InputActions::ControlAxis(const std::string& up, const std::string& down, const std::string& left, const std::string& right) {
    if (!Rebound(up) && !Rebound(down) && !Rebound(left) && !Rebound(right)) return Input::AxisWASD();
    if (Input::TextFocused()) return {0.0f, 0.0f};
    Vec2 v;
    v.y = (ActionHeld(up) ? 1.0f : 0.0f) - (ActionHeld(down) ? 1.0f : 0.0f);
    v.x = (ActionHeld(right) ? 1.0f : 0.0f) - (ActionHeld(left) ? 1.0f : 0.0f);
    return v;
}
bool InputActions::MenuKeyDown(const std::string& action, char key) {
    if (!key) return false;
    return FollowsAction(action, InputBinding::Kind::Key, (unsigned char)key) ? DirectDown(action) : Input::GetKeyDown(key);
}
bool InputActions::DirectHeld(const std::string& name) {
    const Action* a = Find(name);
    if (!a) return false;
    for (const InputBinding& b : a->bindings) if (!b.IsDirection() && b.Value() >= a->threshold) return true;
    return false;
}
bool InputActions::DirectDown(const std::string& name) {
    const Action* a = Find(name);
    if (!a) return false;
    for (const InputBinding& b : a->bindings) if (BindingDown(b)) return true;
    return false;
}
Vec2 InputActions::ControlMove() {
    return Rebound("Move") ? Vector("Move") : Input::AxisWASD();
}

void InputActions::Restore() {
    ClearContexts();
    InputRuntime::RebuildActions();   // the table (and its clock) is built again from the asset and the defaults
    Registry().clear();
    g_defaultsDone = false;
    g_clockSeen = false;
    EnsureDefaults();
}

} // namespace okay
