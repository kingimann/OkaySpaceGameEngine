#include "okay/Input/InputActions.hpp"
#include "okay/Core/AccessibilitySettings.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputRecorder.hpp"
#include "okay/Input/InputDebug.hpp"
#include "okay/Input/Haptics.hpp"
#include "okay/Core/Prefs.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

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
const char* const kMouseNames[3] = {"left", "right", "middle"};

Vec2 ClampLength(Vec2 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    return len > 1.0f ? Vec2{v.x / len, v.y / len} : v;
}

std::vector<InputActions::Action>& Actions() {
    static std::vector<InputActions::Action> a;
    return a;
}
// Lower-case name -> index, rebuilt when the list changes size. Lookups used to scan the whole list
// and lower-case two strings per action per call.
std::unordered_map<std::string, std::size_t>& IndexMap() {
    static std::unordered_map<std::string, std::size_t> m;
    return m;
}
bool g_legacy = std::getenv("OKAY_OLD_INPUT") != nullptr && std::getenv("OKAY_OLD_INPUT")[0] != '0';
double g_now = 0.0;
double g_dt = 0.0;
bool g_clockSeen = false;
std::chrono::steady_clock::time_point g_lastClock;

struct Contribution { float value = 0.0f; Vec2 vector; };
std::unordered_map<std::string, Contribution>& Contributions() {
    static std::unordered_map<std::string, Contribution> c;
    return c;
}

void DefineDefaults() {
    auto& a = Actions();
    a.clear();
    IndexMap().clear();
    InputActions::Define("Move", {"keys:w,s,a,d", "keys:up,down,left,right", "stick:left", "dpad"}, true);
    InputActions::Define("Look", {"mouse:delta", "stick:right"}, true);
    InputActions::Define("Jump", {"key:space", "pad:a"});
    InputActions::Define("Sprint", {"key:shift", "pad:ls"});
    InputActions::Define("Crouch", {"key:ctrl", "pad:b"});
    InputActions::Define("Fire", {"mouse:left", "pad:rt"});
    InputActions::Define("Aim", {"mouse:right", "pad:lt"});
    InputActions::Define("Interact", {"key:e", "pad:x"});
    InputActions::Define("Reload", {"key:r", "pad:y"});
    InputActions::Define("Inventory", {"key:tab", "pad:back"});
    InputActions::Define("Pause", {"key:escape", "pad:start"});
}

void EnsureDefaults() {
    static bool done = false;
    if (!done) { done = true; DefineDefaults(); }
}

// The player's saved bindings for `a`, if any, over its defaults.
void ApplySaved(InputActions::Action& a) {
    const std::string key = "input." + Lower(a.name);
    if (!Prefs::Has(key)) return;
    const std::string saved = Prefs::GetString(key);
    std::vector<InputBinding> list;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= saved.size(); ++i)
        if (i == saved.size() || saved[i] == ';') {
            InputBinding b = InputBinding::Parse(saved.substr(start, i - start));
            if (b.kind != InputBinding::Kind::None) list.push_back(b);
            start = i + 1;
        }
    if (!list.empty()) a.bindings = list;
}

// "dead;curve;sensitivity;invertX;invertY", kept only when it differs from neutral.
void ApplySavedResponse(InputActions::Action& a) {
    const std::string key = "inputresp." + Lower(a.name);
    if (!Prefs::Has(key)) return;
    float v[3] = {0.0f, 1.0f, 1.0f}; int inv[2] = {0, 0};
    const std::string t = Prefs::GetString(key);
    if (std::sscanf(t.c_str(), "%f;%f;%f;%d;%d", &v[0], &v[1], &v[2], &inv[0], &inv[1]) < 3) return;
    a.deadzone = std::clamp(v[0], 0.0f, 0.95f); a.curve = std::clamp(v[1], 0.1f, 8.0f);
    a.sensitivity = std::clamp(v[2], 0.05f, 20.0f); a.invertX = inv[0] != 0; a.invertY = inv[1] != 0;
}
void SaveResponse(const InputActions::Action& a) {
    const std::string key = "inputresp." + Lower(a.name);
    if (a.deadzone == 0.0f && a.curve == 1.0f && a.sensitivity == 1.0f && !a.invertX && !a.invertY) { Prefs::Delete(key); return; }
    char buf[96];
    std::snprintf(buf, sizeof buf, "%g;%g;%g;%d;%d", a.deadzone, a.curve, a.sensitivity, a.invertX ? 1 : 0, a.invertY ? 1 : 0);
    Prefs::SetString(key, buf);
}

std::string Joined(const std::vector<InputBinding>& b) {
    std::string out;
    for (const auto& x : b) { if (!out.empty()) out += ';'; out += x.ToString(); }
    return out;
}

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
        for (int i = 0; i < 3; ++i) if (arg == kMouseNames[i]) { b.kind = Kind::Mouse; b.code = i; }
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
        case Kind::Wheel:      return code >= 0 ? "wheel:up" : "wheel:down";
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
        case Kind::Mouse:      return code == 0 ? "Left Mouse" : code == 1 ? "Right Mouse" : "Middle Mouse";
        case Kind::PadButton:  return Input::GamepadButtonLabel(code, family) + who;
        case Kind::Keys4:      return Input::KeyName(keys[0]) + " " + Input::KeyName(keys[2]) + " " +
                                      Input::KeyName(keys[1]) + " " + Input::KeyName(keys[3]);
        case Kind::StickLeft:  return "Left Stick" + who;
        case Kind::StickRight: return "Right Stick" + who;
        case Kind::DPad:       return "D-pad" + who;
        case Kind::MouseDelta: return "Mouse";
        case Kind::Wheel:      return code >= 0 ? "Wheel Up" : "Wheel Down";
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
        case Kind::Key:       return Input::GetKeyGame((char)code) ? 1.0f : 0.0f;
        case Kind::Mouse:     return Input::GetMouseButton(code) ? 1.0f : 0.0f;
        case Kind::Wheel:     return Input::MouseWheel() * (code >= 0 ? 1.0f : -1.0f) > 0.0f ? 1.0f : 0.0f;
        case Kind::Chord: {
            for (int i = 0; i < 4 && keys[i]; ++i) if (!Input::GetKeyGame(keys[i])) return 0.0f;
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
            return {(Input::GetKeyGame(keys[3]) ? 1.0f : 0.0f) - (Input::GetKeyGame(keys[2]) ? 1.0f : 0.0f),
                    (Input::GetKeyGame(keys[0]) ? 1.0f : 0.0f) - (Input::GetKeyGame(keys[1]) ? 1.0f : 0.0f)};
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

void InputActions::Define(const std::string& name, const std::vector<std::string>& bindings, bool direction) {
    EnsureDefaults();
    std::vector<InputBinding> parsed;
    for (const auto& t : bindings) {
        InputBinding b = InputBinding::Parse(t);
        if (b.kind != InputBinding::Kind::None) parsed.push_back(b);
    }
    // A game defining its actions at start keeps the player's own rebinding.
    if (Action* a = Find(name)) {
        a->defaults = parsed; a->bindings = parsed; a->direction = direction;
        ApplySaved(*a);
        ApplySavedResponse(*a);
        return;
    }
    Action a;
    a.name = name; a.direction = direction; a.defaults = parsed; a.bindings = parsed;
    a.key = Lower(name);
    ApplySaved(a);
    ApplySavedResponse(a);
    Actions().push_back(std::move(a));
    IndexMap()[Lower(name)] = Actions().size() - 1;
}

bool InputActions::Has(const std::string& name) { return Find(name) != nullptr; }
const std::vector<InputActions::Action>& InputActions::All() { EnsureDefaults(); return Actions(); }

InputActions::Action* InputActions::Find(const std::string& name) {
    EnsureDefaults();
    if (g_legacy) {   // the original lookup: lower-case the name and every action's name, scan the list
        const std::string n = Lower(name);
        for (Action& a : Actions()) if (Lower(a.name) == n) return &a;
        return nullptr;
    }
    auto& map = IndexMap();
    std::string n = name;
    for (char& c : n) c = (char)std::tolower((unsigned char)c);
    auto it = map.find(n);
    if (it != map.end() && it->second < Actions().size()) return &Actions()[it->second];
    return nullptr;
}

namespace {

struct ContextEntry { std::string name; InputActions::ContextMode mode; };
std::vector<ContextEntry>& Stack() { static std::vector<ContextEntry> s; return s; }
bool IsBase(const std::string& c) { return c.empty() || Lower(c) == "gameplay"; }
bool SameContext(const InputActions::Action& a, const ContextEntry& c) { return !IsBase(a.context) && Lower(a.context) == Lower(c.name); }

// Each physical input gets an id so a context can claim it from the ones below.
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
void ClaimsOf(const InputBinding& b, std::vector<int>& out) { ForClaims(b, [&](int id) { out.push_back(id); }); }
bool Claimed(const std::vector<int>& claimed, int id) {
    return std::find(claimed.begin(), claimed.end(), id) != claimed.end();
}

// A binding read with some of its inputs swallowed by a higher context.
Vec2 MaskedVector(const InputBinding& b, const std::vector<int>& claimed) {
    using K = InputBinding::Kind;
    auto key = [&](char k) { return !Claimed(claimed, KeyId(k)) && Input::GetKeyGame(k) ? 1.0f : 0.0f; };
    auto pad = [&](int btn) { return !Claimed(claimed, PadId(btn, b.player)) && Input::GetGamepadButton(btn, b.player) ? 1.0f : 0.0f; };
    switch (b.kind) {
        case K::Keys4: return {key(b.keys[3]) - key(b.keys[2]), key(b.keys[0]) - key(b.keys[1])};
        case K::DPad:  return {pad(Input::PadRight) - pad(Input::PadLeft), pad(Input::PadUp) - pad(Input::PadDown)};
        default: {
            bool hit = false;
            ForClaims(b, [&](int id) { if (Claimed(claimed, id)) hit = true; });
            return hit ? Vec2{0.0f, 0.0f} : b.Vector();
        }
    }
}
float MaskedValue(const InputBinding& b, const std::vector<int>& claimed) {
    if (b.IsDirection()) {
        const Vec2 v = MaskedVector(b, claimed);
        return std::min(1.0f, std::sqrt(v.x * v.x + v.y * v.y));
    }
    bool hit = false;
    ForClaims(b, [&](int id) { if (Claimed(claimed, id)) hit = true; });
    return hit ? 0.0f : b.Value();
}

// Everything the stack above the base context swallows.
std::vector<int> ConsumedByStack() {
    std::vector<int> claimed;
    for (const ContextEntry& c : Stack()) {
        if (c.mode == InputActions::ContextMode::Passthrough) continue;
        for (const InputActions::Action& a : Actions())
            if (SameContext(a, c))
                for (const InputBinding& b : a.bindings) ClaimsOf(b, claimed);
    }
    return claimed;
}

// A player's own copy of a binding: a controller binding written for the first controller follows the
// player (Jump "pad:a" is A on whichever controller asks); the keyboard and mouse belong to player 0.
bool ForPlayer(const InputBinding& b, int player, InputBinding& out) {
    out = b;
    if (b.IsGamepad()) { if (b.player == 0) out.player = player; return b.player == 0 || b.player == player; }
    return player == 0;
}

// The response curve of one reading, 0..1 in, shaped out.
float ShapeMagnitude(float in, const InputActions::Response& r) {
    if (in <= 0.0f) return 0.0f;
    float t = in;
    if (r.deadzone > 0.0f) {
        if (t <= r.deadzone) return 0.0f;
        t = (t - r.deadzone) / (1.0f - r.deadzone);
    }
    if (t > 1.0f) t = 1.0f;
    if (r.curve != 1.0f && r.curve > 0.0f) t = std::pow(t, r.curve);
    return t * r.sensitivity;
}
bool IsNeutral(const InputActions::Action& a) {
    return a.deadzone == 0.0f && a.curve == 1.0f && a.sensitivity == 1.0f && !a.invertX && !a.invertY;
}
InputActions::Response ResponseOf(const InputActions::Action& a) { return {a.deadzone, a.curve, a.sensitivity, a.invertX, a.invertY}; }

void ApplyResponseTo(const InputActions::Action& a, float& value, Vec2& vec, bool direction) {
    if (IsNeutral(a)) return;
    const InputActions::Response r = ResponseOf(a);
    if (direction) {
        const float len = std::sqrt(vec.x * vec.x + vec.y * vec.y);
        const float t = ShapeMagnitude(len, r);
        vec = len > 0.0f ? Vec2{vec.x / len * t, vec.y / len * t} : Vec2{0.0f, 0.0f};
        if (a.invertX) vec.x = -vec.x;
        if (a.invertY) vec.y = -vec.y;
        value = std::sqrt(vec.x * vec.x + vec.y * vec.y);
    } else {
        value = std::min(1.0f, ShapeMagnitude(value, r));
    }
}

} // namespace

void InputActions::SetLegacy(bool on) { g_legacy = on; }
bool InputActions::Legacy() { return g_legacy; }
double InputActions::Now() { return g_now; }

float InputActions::ShapeValue(float in, const Response& r) { return ShapeMagnitude(in < 0.0f ? 0.0f : (in > 1.0f ? 1.0f : in), r); }
bool InputActions::SetResponse(const std::string& action, const Response& r) {
    Action* a = Find(action);
    if (!a) return false;
    a->deadzone = std::clamp(r.deadzone, 0.0f, 0.95f);
    a->curve = std::clamp(r.curve, 0.1f, 8.0f);
    a->sensitivity = std::clamp(r.sensitivity, 0.05f, 20.0f);
    a->invertX = r.invertX; a->invertY = r.invertY;
    SaveResponse(*a);
    return true;
}
InputActions::Response InputActions::GetResponse(const std::string& action) {
    const Action* a = Find(action);
    return a ? ResponseOf(*a) : Response{};
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
    EnsureDefaults();
    g_dt = dt > 0.0f ? dt : 0.0f;
    g_now += g_dt;
    InputHaptics::Update(g_dt);
    InputRecorder::BeginFrame(g_dt);   // playback puts the recorded frame over what the runtime fed
    auto& actions = Actions();
    const bool legacy = g_legacy;
    // Actions can be added behind our back (tests clear the list): keep the index and keys in step.
    if (IndexMap().size() != actions.size()) {
        IndexMap().clear();
        for (std::size_t i = 0; i < actions.size(); ++i) { actions[i].key = Lower(actions[i].name); IndexMap()[actions[i].key] = i; }
    }
    for (Action& a : actions) { a.previous = a.value; a.value = 0.0f; a.vector = {0.0f, 0.0f}; }
    const bool ctxEmpty = Stack().empty();
    static std::vector<int> chordClaims;
    chordClaims.clear();

    // A chord that is held wins over the single keys inside it (Ctrl+Z does not also Crouch).
    bool anyChord = false;
    for (const Action& a : actions)
        for (const InputBinding& b : a.bindings)
            if (b.kind == InputBinding::Kind::Chord) { anyChord = true; if (b.Value() > 0.0f) ClaimsOf(b, chordClaims); }

    auto sample = [&](Action& a, const std::vector<int>& claimed) {
        Vec2 v{0.0f, 0.0f};
        float best = 0.0f;
        const bool fast = !legacy && claimed.empty() && chordClaims.empty();
        for (const InputBinding& b : a.bindings) {
            if (anyChord && b.kind != InputBinding::Kind::Chord && !chordClaims.empty()) {   // the chord's keys are spoken for
                std::vector<int> merged = claimed; merged.insert(merged.end(), chordClaims.begin(), chordClaims.end());
                if (a.direction) { const Vec2 d = MaskedVector(b, merged); v.x += d.x; v.y += d.y; }
                else best = std::max(best, MaskedValue(b, merged));
                continue;
            }
            if (fast) {
                if (a.direction) { const Vec2 d = b.Vector(); v.x += d.x; v.y += d.y; }
                else best = std::max(best, b.Value());
            } else if (a.direction) { const Vec2 d = MaskedVector(b, claimed); v.x += d.x; v.y += d.y; }
            else best = std::max(best, MaskedValue(b, claimed));
        }
        if (a.direction) {
            a.vector = ClampLength(v);
            best = std::sqrt(a.vector.x * a.vector.x + a.vector.y * a.vector.y);
        }
        a.value = best;
        ApplyResponseTo(a, a.value, a.vector, a.direction);
    };

    // Top of the stack first; each layer sees what the layers above left over.
    static std::vector<int> claimed;
    claimed.clear();
    bool blocked = false;
    const auto& stack = Stack();
    if (!ctxEmpty) {
        for (std::size_t i = stack.size(); i-- > 0 && !blocked;) {
            const ContextEntry& c = stack[i];
            for (Action& a : actions)
                if (SameContext(a, c)) sample(a, claimed);
            if (c.mode == ContextMode::Block) blocked = true;
            else if (c.mode == ContextMode::Consume)
                for (const Action& a : actions)
                    if (SameContext(a, c))
                        for (const InputBinding& b : a.bindings) ClaimsOf(b, claimed);
        }
    }
    if (!blocked)
        for (Action& a : actions)
            if (a.context.empty() || IsBase(a.context)) sample(a, claimed);

    // Several local players: the same bindings read for each controller.
    for (Action& a : actions) {
        if (!a.perPlayer) continue;
        for (int p = 0; p < Input::kMaxGamepads; ++p) {
            Action::PlayerState& ps = a.players[p];
            ps.previous = ps.value; ps.value = 0.0f; ps.vector = {0.0f, 0.0f};
            if (blocked && IsBase(a.context)) continue;
            Vec2 v{0.0f, 0.0f}; float best = 0.0f;
            for (const InputBinding& orig : a.bindings) {
                InputBinding b;
                if (!ForPlayer(orig, p, b)) continue;
                if (a.direction) { const Vec2 d = MaskedVector(b, claimed); v.x += d.x; v.y += d.y; }
                else best = std::max(best, MaskedValue(b, claimed));
            }
            if (a.direction) { ps.vector = ClampLength(v); best = std::sqrt(ps.vector.x * ps.vector.x + ps.vector.y * ps.vector.y); }
            ps.value = best;
            ApplyResponseTo(a, ps.value, ps.vector, a.direction);
        }
    }

    // On-screen controls, added after the devices so a key and a virtual stick
    // share one action. A menu that blocks the base context does not also get them.
    if (!Contributions().empty()) {
        for (Action& a : actions) {
            auto it = Contributions().find(legacy ? Lower(a.name) : a.key);
            if (it == Contributions().end()) continue;
            if (IsBase(a.context)) { if (blocked) continue; }
            else if (!ContextActive(a.context)) continue;
            const Contribution& c = it->second;
            if (a.direction) {
                a.vector = ClampLength(a.vector + c.vector);
                a.value = std::sqrt(a.vector.x * a.vector.x + a.vector.y * a.vector.y);
            } else {
                float extra = c.value;
                const float len = std::sqrt(c.vector.x * c.vector.x + c.vector.y * c.vector.y);
                if (len > extra) extra = len > 1.0f ? 1.0f : len;
                if (extra > a.value) a.value = extra;
            }
        }
        Contributions().clear();
    }
    // Hold-to-toggle: a button action the player asked to toggle stays on after
    // the key comes up, until the next press. Direction actions (Move, Look) are
    // left as sampled. Off, the raw value is what Get/Down/Up already used.
    static std::unordered_map<std::string, HoldToggle> latches;
    for (Action& a : actions) {
        if (a.direction) continue;
        if (legacy) {
            HoldToggle& latch = latches[Lower(a.name)];
            if (!Accessibility::ToggleFor(a.name)) { latch.Reset(); continue; }
            const bool down = a.value >= a.threshold;
            a.value = latch.Update(down, true) ? 1.0f : 0.0f;
            continue;
        }
        if (!Accessibility::AnyToggle() || !Accessibility::ToggleFor(a.name)) { a.latchActive = a.latchHeld = false; continue; }
        const bool down = a.value >= a.threshold;
        if (down && !a.latchHeld) a.latchActive = !a.latchActive;   // each press flips it (HoldToggle, inlined)
        a.latchHeld = down;
        a.value = a.latchActive ? 1.0f : 0.0f;
    }

    InputRecorder::EndFrame(g_dt);
    InputDebug::Record();

    // Timing for buffering, double tap and hold to confirm.
    for (Action& a : actions) {
        const bool down = a.value >= a.threshold;
        const bool was = a.previous >= a.threshold;
        a.pressedNow = down && !was;
        if (a.pressedNow) { a.prevPressedAt = a.pressedAt; a.pressedAt = g_now; a.pressConsumed = false; a.heldSince = g_now; }
        if (!down) a.heldSince = -1.0;
    }
}

void InputActions::Contribute(const std::string& action, float value) {
    if (action.empty()) return;
    Contribution& c = Contributions()[Lower(action)];
    if (value > c.value) c.value = value;
}
void InputActions::ContributeVector(const std::string& action, const Vec2& vector) {
    if (action.empty()) return;
    Contribution& c = Contributions()[Lower(action)];
    c.vector.x += vector.x;
    c.vector.y += vector.y;
}
void InputActions::ClearContributions() { Contributions().clear(); }

void InputActions::DefineIn(const std::string& context, const std::string& name,
                            const std::vector<std::string>& bindings, bool direction) {
    Define(name, bindings, direction);
    SetContext(name, context);
}
bool InputActions::SetContext(const std::string& action, const std::string& context) {
    Action* a = Find(action);
    if (!a) return false;
    a->context = IsBase(context) ? "" : context;
    a->ctxKey = Lower(a->context);
    return true;
}
void InputActions::PushContext(const std::string& context, ContextMode mode) {
    if (IsBase(context)) return;
    PopContext(context);
    Stack().push_back({context, mode});
}
void InputActions::PopContext(const std::string& context) {
    auto& s = Stack();
    s.erase(std::remove_if(s.begin(), s.end(), [&](const ContextEntry& c) { return Lower(c.name) == Lower(context); }), s.end());
}
void InputActions::ClearContexts() { Stack().clear(); }
bool InputActions::ContextActive(const std::string& context) {
    if (IsBase(context)) return true;
    for (const ContextEntry& c : Stack()) if (Lower(c.name) == Lower(context)) return true;
    return false;
}
std::string InputActions::TopContext() { return Stack().empty() ? std::string() : Stack().back().name; }
static bool StackBlocks() {
    for (const ContextEntry& c : Stack()) if (c.mode == InputActions::ContextMode::Block) return true;
    return false;
}
bool InputActions::KeyConsumed(char key) { return StackBlocks() || Claimed(ConsumedByStack(), KeyId(key)); }
bool InputActions::PadButtonConsumed(int button, int player) { return StackBlocks() || Claimed(ConsumedByStack(), PadId(button, player)); }
void InputActions::SetThreshold(const std::string& action, float t) {
    if (Action* a = Find(action)) a->threshold = t < 0.01f ? 0.01f : (t > 1.0f ? 1.0f : t);
}

bool InputActions::Get(const std::string& name) { const Action* a = Find(name); return a && a->value >= a->threshold; }
bool InputActions::Down(const std::string& name) { const Action* a = Find(name); return a && a->value >= a->threshold && a->previous < a->threshold; }
bool InputActions::Up(const std::string& name) { const Action* a = Find(name); return a && a->value < a->threshold && a->previous >= a->threshold; }
float InputActions::Value(const std::string& name) { const Action* a = Find(name); return a ? a->value : 0.0f; }
Vec2 InputActions::Vector(const std::string& name) { const Action* a = Find(name); return a ? a->vector : Vec2{0.0f, 0.0f}; }

void InputActions::SetPerPlayer(const std::string& action, bool on) { if (Action* a = Find(action)) a->perPlayer = on; }
static const InputActions::Action::PlayerState* PlayerOf(const std::string& name, int player) {
    const InputActions::Action* a = InputActions::Find(name);
    if (!a || player < 0 || player >= Input::kMaxGamepads) return nullptr;
    return &a->players[player];
}
bool InputActions::Get(const std::string& name, int p) { const auto* s = PlayerOf(name, p); return s && s->value >= Find(name)->threshold; }
bool InputActions::Down(const std::string& name, int p) { const auto* s = PlayerOf(name, p); const float t = s ? Find(name)->threshold : 0.5f; return s && s->value >= t && s->previous < t; }
bool InputActions::Up(const std::string& name, int p) { const auto* s = PlayerOf(name, p); const float t = s ? Find(name)->threshold : 0.5f; return s && s->value < t && s->previous >= t; }
float InputActions::Value(const std::string& name, int p) { const auto* s = PlayerOf(name, p); return s ? s->value : 0.0f; }
Vec2 InputActions::Vector(const std::string& name, int p) { const auto* s = PlayerOf(name, p); return s ? s->vector : Vec2{0.0f, 0.0f}; }

bool InputActions::Buffered(const std::string& name, float window) {
    const Action* a = Find(name);
    return a && !a->pressConsumed && a->pressedAt > -1.0e8 && g_now - a->pressedAt <= (double)window;
}
bool InputActions::Consume(const std::string& name) {
    Action* a = Find(name);
    if (!a || a->pressConsumed || a->pressedAt < -1.0e8) return false;
    a->pressConsumed = true;
    return true;
}
bool InputActions::DoubleTap(const std::string& name, float window) {
    const Action* a = Find(name);
    return a && a->pressedNow && a->prevPressedAt > -1.0e8 && a->pressedAt - a->prevPressedAt <= (double)window;
}
float InputActions::HeldFor(const std::string& name) {
    const Action* a = Find(name);
    return a && a->heldSince >= 0.0 ? (float)(g_now - a->heldSince) : 0.0f;
}
float InputActions::HoldProgress(const std::string& name, float seconds) {
    if (!(seconds > 0.0f)) return 0.0f;
    return std::min(1.0f, HeldFor(name) / seconds);
}
bool InputActions::HoldReached(const std::string& name, float seconds) {
    const Action* a = Find(name);
    if (!a || a->heldSince < 0.0 || !(seconds > 0.0f)) return false;
    const double held = g_now - a->heldSince;
    return held >= (double)seconds && held - g_dt < (double)seconds;   // crossed the line during this very frame
}

const InputBinding* InputActions::Current(const std::string& name, bool gamepad) {
    const Action* a = Find(name);
    if (!a) return nullptr;
    for (const InputBinding& b : a->bindings)
        if (b.IsGamepad() == gamepad && b.kind != InputBinding::Kind::MouseDelta) return &b;
    return nullptr;
}

bool InputActions::Rebind(const std::string& name, const std::string& text, int part) {
    Action* a = Find(name);
    if (!a) return false;
    InputBinding nb = InputBinding::Parse(text);
    if (nb.kind == InputBinding::Kind::None) return false;
    // A direction action (Move, Look) needs something that points: a lone key or button would
    // replace its four-key binding and leave it reading (0, 0) for ever.
    if (a->direction && part < 0 && !nb.IsDirection()) return false;
    const bool pad = nb.IsGamepad();
    InputBinding* slot = nullptr;
    for (InputBinding& b : a->bindings)
        if (b.IsGamepad() == pad && b.kind != InputBinding::Kind::MouseDelta && b.IsDirection() == nb.IsDirection()) { slot = &b; break; }
    if (!slot)
        for (InputBinding& b : a->bindings)
            if (b.IsGamepad() == pad && b.kind != InputBinding::Kind::MouseDelta) { slot = &b; break; }
    if (part >= 0 && part < 4) {
        // One key of a four-key direction.
        if (!slot || slot->kind != InputBinding::Kind::Keys4 || nb.kind != InputBinding::Kind::Key) return false;
        slot->keys[part] = (char)nb.code;
    } else if (slot) {
        *slot = nb;
    } else {
        a->bindings.push_back(nb);
    }
    Save();
    return true;
}

void InputActions::Reset(const std::string& name) {
    EnsureDefaults();
    for (Action& a : Actions())
        if (name.empty() || Lower(a.name) == Lower(name)) {
            a.bindings = a.defaults;
            Prefs::Delete("input." + Lower(a.name));
        }
}

void InputActions::Load() {
    EnsureDefaults();
    for (Action& a : Actions()) { ApplySaved(a); ApplySavedResponse(a); }
}

void InputActions::Save() {
    EnsureDefaults();
    for (const Action& a : Actions()) {
        const std::string key = "input." + Lower(a.name);
        const std::string now = Joined(a.bindings);
        if (now == Joined(a.defaults)) Prefs::Delete(key);   // only what the player changed
        else Prefs::SetString(key, now);
    }
}

std::string InputActions::ExportBindings() {
    EnsureDefaults();
    std::string out = "okayinput 1\n";
    for (const Action& a : Actions()) out += a.name + "=" + Joined(a.bindings) + "\n";
    return out;
}
bool InputActions::ImportBindings(const std::string& text) {
    EnsureDefaults();
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
        Action* a = Find(line.substr(0, eq));
        if (!a) continue;
        std::vector<InputBinding> list;
        const std::string v(line, eq + 1);
        std::size_t start = 0;
        for (std::size_t i = 0; i <= v.size(); ++i)
            if (i == v.size() || v[i] == ';') {
                InputBinding b = InputBinding::Parse(v.substr(start, i - start));
                if (b.kind != InputBinding::Kind::None) list.push_back(b);
                start = i + 1;
            }
        if (!list.empty()) { a->bindings = list; any = true; }
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

void InputActions::Restore() { Stack().clear(); g_now = 0.0; g_clockSeen = false; DefineDefaults(); }

} // namespace okay
