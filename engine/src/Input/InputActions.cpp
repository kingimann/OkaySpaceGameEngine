#include "okay/Input/InputActions.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Core/Prefs.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>

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
std::string KeyText(char k) { return Lower(Input::KeyName(k)); }
const char* const kMouseNames[3] = {"left", "right", "middle"};

Vec2 ClampLength(Vec2 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    return len > 1.0f ? Vec2{v.x / len, v.y / len} : v;
}

std::vector<InputActions::Action>& Actions() {
    static std::vector<InputActions::Action> a;
    return a;
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
    const std::string text = Lower(Trim(textIn));
    const std::size_t colon = text.find(':');
    const std::string kind = colon == std::string::npos ? text : text.substr(0, colon);
    const std::string arg = colon == std::string::npos ? "" : Trim(text.substr(colon + 1));
    if (kind == "dpad") { b.kind = Kind::DPad; return b; }
    if (kind == "key" && !arg.empty() && Input::IsKnownKeyName(arg)) {
        b.kind = Kind::Key; b.code = (unsigned char)Input::KeyFromName(arg);
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
                if (parts[i].empty() || !Input::IsKnownKeyName(parts[i])) ok = false;
                else b.keys[i] = Input::KeyFromName(parts[i]);
            }
            if (ok) b.kind = Kind::Keys4;
        }
    }
    return b;
}

std::string InputBinding::ToString() const {
    switch (kind) {
        case Kind::Key:        return "key:" + KeyText((char)code);
        case Kind::Mouse:      return std::string("mouse:") + kMouseNames[code];
        case Kind::PadButton:  return "pad:" + Input::GamepadButtonKey(code);
        case Kind::Keys4:      return "keys:" + KeyText(keys[0]) + "," + KeyText(keys[1]) + "," + KeyText(keys[2]) + "," + KeyText(keys[3]);
        case Kind::StickLeft:  return "stick:left";
        case Kind::StickRight: return "stick:right";
        case Kind::DPad:       return "dpad";
        case Kind::MouseDelta: return "mouse:delta";
        case Kind::None:       break;
    }
    return "";
}

std::string InputBinding::Label() const {
    switch (kind) {
        case Kind::Key:        return Input::KeyName((char)code);
        case Kind::Mouse:      return code == 0 ? "Left Mouse" : code == 1 ? "Right Mouse" : "Middle Mouse";
        case Kind::PadButton:  return Input::GamepadButtonName(code);
        case Kind::Keys4:      return Input::KeyName(keys[0]) + " " + Input::KeyName(keys[2]) + " " +
                                      Input::KeyName(keys[1]) + " " + Input::KeyName(keys[3]);
        case Kind::StickLeft:  return "Left Stick";
        case Kind::StickRight: return "Right Stick";
        case Kind::DPad:       return "D-pad";
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
            if (code == Input::PadLT) return Input::GamepadTrigger(0);
            if (code == Input::PadRT) return Input::GamepadTrigger(1);
            return Input::GetGamepadButton(code) ? 1.0f : 0.0f;
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
        case Kind::StickLeft:  return Input::GamepadAxis();
        case Kind::StickRight: return Input::GamepadRightAxis();
        case Kind::DPad:
            return {(Input::GetGamepadButton(Input::PadRight) ? 1.0f : 0.0f) - (Input::GetGamepadButton(Input::PadLeft) ? 1.0f : 0.0f),
                    (Input::GetGamepadButton(Input::PadUp) ? 1.0f : 0.0f) - (Input::GetGamepadButton(Input::PadDown) ? 1.0f : 0.0f)};
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

void InputActions::Tick() {
    EnsureDefaults();
    for (Action& a : Actions()) {
        a.previous = a.value;
        Vec2 v{0.0f, 0.0f};
        float best = 0.0f;
        for (const InputBinding& b : a.bindings) {
            if (a.direction) { const Vec2 d = b.Vector(); v.x += d.x; v.y += d.y; }
            else best = std::max(best, b.Value());
        }
        if (a.direction) {
            a.vector = ClampLength(v);
            best = std::sqrt(a.vector.x * a.vector.x + a.vector.y * a.vector.y);
        }
        a.value = best;
    }
}

bool InputActions::Get(const std::string& name) { const Action* a = Find(name); return a && a->value >= 0.5f; }
bool InputActions::Down(const std::string& name) { const Action* a = Find(name); return a && a->value >= 0.5f && a->previous < 0.5f; }
bool InputActions::Up(const std::string& name) { const Action* a = Find(name); return a && a->value < 0.5f && a->previous >= 0.5f; }
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

void InputActions::Restore() { DefineDefaults(); }

} // namespace okay
