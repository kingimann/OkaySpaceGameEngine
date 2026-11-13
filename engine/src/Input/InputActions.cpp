#include "okay/Input/InputActions.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Core/Prefs.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
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

struct Contribution { float value = 0.0f; Vec2 vector; };
std::unordered_map<std::string, Contribution>& Contributions() {
    static std::unordered_map<std::string, Contribution> c;
    return c;
}

void DefineDefaults() {
    auto& a = Actions();
    a.clear();
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
        case Kind::None:       break;
    }
    return "";
}

std::string InputBinding::Label() const {
    const std::string who = player > 0 ? " (Pad " + std::to_string(player + 1) + ")" : "";
    switch (kind) {
        case Kind::Key:        return Input::KeyName((char)code);
        case Kind::Mouse:      return code == 0 ? "Left Mouse" : code == 1 ? "Right Mouse" : "Middle Mouse";
        case Kind::PadButton:  return Input::GamepadButtonName(code) + who;
        case Kind::Keys4:      return Input::KeyName(keys[0]) + " " + Input::KeyName(keys[2]) + " " +
                                      Input::KeyName(keys[1]) + " " + Input::KeyName(keys[3]);
        case Kind::StickLeft:  return "Left Stick" + who;
        case Kind::StickRight: return "Right Stick" + who;
        case Kind::DPad:       return "D-pad" + who;
        case Kind::MouseDelta: return "Mouse";
        case Kind::None:       break;
    }
    return "None";
}

float InputBinding::Value() const {
    switch (kind) {
        case Kind::Key:       return Input::GetKey((char)code) ? 1.0f : 0.0f;
        case Kind::Mouse:     return Input::GetMouseButton(code) ? 1.0f : 0.0f;
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
            return {(Input::GetKey(keys[3]) ? 1.0f : 0.0f) - (Input::GetKey(keys[2]) ? 1.0f : 0.0f),
                    (Input::GetKey(keys[0]) ? 1.0f : 0.0f) - (Input::GetKey(keys[1]) ? 1.0f : 0.0f)};
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
        return;
    }
    Action a;
    a.name = name; a.direction = direction; a.defaults = parsed; a.bindings = parsed;
    ApplySaved(a);
    Actions().push_back(std::move(a));
}

bool InputActions::Has(const std::string& name) { return Find(name) != nullptr; }
const std::vector<InputActions::Action>& InputActions::All() { EnsureDefaults(); return Actions(); }

InputActions::Action* InputActions::Find(const std::string& name) {
    EnsureDefaults();
    const std::string n = Lower(name);
    for (Action& a : Actions()) if (Lower(a.name) == n) return &a;
    return nullptr;
}

namespace {

struct ContextEntry { std::string name; InputActions::ContextMode mode; };
std::vector<ContextEntry>& Stack() { static std::vector<ContextEntry> s; return s; }
bool IsBase(const std::string& c) { return c.empty() || Lower(c) == "gameplay"; }

// Each physical input gets an id so a context can claim it from the ones below.
int KeyId(char k) { return 1000 + (unsigned char)k; }
int MouseId(int b) { return 2000 + b; }
int PadId(int button, int player) { return 3000 + player * 100 + button; }
int StickId(bool right, int player) { return (right ? 4100 : 4000) + player; }
constexpr int kMouseDeltaId = 5000;

void ClaimsOf(const InputBinding& b, std::vector<int>& out) {
    using K = InputBinding::Kind;
    switch (b.kind) {
        case K::Key:        out.push_back(KeyId((char)b.code)); break;
        case K::Mouse:      out.push_back(MouseId(b.code)); break;
        case K::PadButton:  out.push_back(PadId(b.code, b.player)); break;
        case K::Keys4:      for (char k : b.keys) out.push_back(KeyId(k)); break;
        case K::StickLeft:  out.push_back(StickId(false, b.player)); break;
        case K::StickRight: out.push_back(StickId(true, b.player)); break;
        case K::DPad:       for (int p : {Input::PadUp, Input::PadDown, Input::PadLeft, Input::PadRight}) out.push_back(PadId(p, b.player)); break;
        case K::MouseDelta: out.push_back(kMouseDeltaId); break;
        case K::None:       break;
    }
}
bool Claimed(const std::vector<int>& claimed, int id) {
    return std::find(claimed.begin(), claimed.end(), id) != claimed.end();
}

// A binding read with some of its inputs swallowed by a higher context.
Vec2 MaskedVector(const InputBinding& b, const std::vector<int>& claimed) {
    using K = InputBinding::Kind;
    auto key = [&](char k) { return !Claimed(claimed, KeyId(k)) && Input::GetKey(k) ? 1.0f : 0.0f; };
    auto pad = [&](int btn) { return !Claimed(claimed, PadId(btn, b.player)) && Input::GetGamepadButton(btn, b.player) ? 1.0f : 0.0f; };
    switch (b.kind) {
        case K::Keys4: return {key(b.keys[3]) - key(b.keys[2]), key(b.keys[0]) - key(b.keys[1])};
        case K::DPad:  return {pad(Input::PadRight) - pad(Input::PadLeft), pad(Input::PadUp) - pad(Input::PadDown)};
        default: {
            std::vector<int> ids; ClaimsOf(b, ids);
            for (int id : ids) if (Claimed(claimed, id)) return {0.0f, 0.0f};
            return b.Vector();
        }
    }
}
float MaskedValue(const InputBinding& b, const std::vector<int>& claimed) {
    if (b.IsDirection()) {
        const Vec2 v = MaskedVector(b, claimed);
        return std::min(1.0f, std::sqrt(v.x * v.x + v.y * v.y));
    }
    std::vector<int> ids; ClaimsOf(b, ids);
    for (int id : ids) if (Claimed(claimed, id)) return 0.0f;
    return b.Value();
}

// Everything the stack above the base context swallows.
std::vector<int> ConsumedByStack() {
    std::vector<int> claimed;
    for (const ContextEntry& c : Stack()) {
        if (c.mode == InputActions::ContextMode::Passthrough) continue;
        for (const InputActions::Action& a : Actions())
            if (!IsBase(a.context) && Lower(a.context) == Lower(c.name))
                for (const InputBinding& b : a.bindings) ClaimsOf(b, claimed);
    }
    return claimed;
}

} // namespace

void InputActions::Tick() {
    EnsureDefaults();
    for (Action& a : Actions()) { a.previous = a.value; a.value = 0.0f; a.vector = {0.0f, 0.0f}; }

    auto sample = [](Action& a, const std::vector<int>& claimed) {
        Vec2 v{0.0f, 0.0f};
        float best = 0.0f;
        for (const InputBinding& b : a.bindings) {
            if (a.direction) { const Vec2 d = MaskedVector(b, claimed); v.x += d.x; v.y += d.y; }
            else best = std::max(best, MaskedValue(b, claimed));
        }
        if (a.direction) {
            a.vector = ClampLength(v);
            best = std::sqrt(a.vector.x * a.vector.x + a.vector.y * a.vector.y);
        }
        a.value = best;
    };

    // Top of the stack first; each layer sees what the layers above left over.
    std::vector<int> claimed;
    bool blocked = false;
    const auto& stack = Stack();
    for (std::size_t i = stack.size(); i-- > 0 && !blocked;) {
        const ContextEntry& c = stack[i];
        for (Action& a : Actions())
            if (!IsBase(a.context) && Lower(a.context) == Lower(c.name)) sample(a, claimed);
        if (c.mode == ContextMode::Block) blocked = true;
        else if (c.mode == ContextMode::Consume)
            for (const Action& a : Actions())
                if (!IsBase(a.context) && Lower(a.context) == Lower(c.name))
                    for (const InputBinding& b : a.bindings) ClaimsOf(b, claimed);
    }
    if (!blocked)
        for (Action& a : Actions())
            if (IsBase(a.context)) sample(a, claimed);

    // On-screen controls, added after the devices so a key and a virtual stick
    // share one action. A menu that blocks the base context does not also get them.
    for (Action& a : Actions()) {
        auto it = Contributions().find(Lower(a.name));
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
    const bool pad = nb.IsGamepad();
    InputBinding* slot = nullptr;
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
    for (Action& a : Actions()) ApplySaved(a);
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

void InputActions::Restore() { Stack().clear(); DefineDefaults(); }

} // namespace okay
