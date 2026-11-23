#include "okay/Input/InputAsset.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace okay {
namespace {

std::string NoSpace(std::string s) { for (char& c : s) if (c == ' ' || c == '\t') c = '_'; return s; }
const char* KindName(BindingSpec::Kind k) {
    switch (k) { case BindingSpec::Kind::Vector2: return "vector2"; case BindingSpec::Kind::Vector3: return "vector3"; case BindingSpec::Kind::Axis1D: return "axis"; case BindingSpec::Kind::Chord: return "chord"; default: return "single"; }
}
bool ParseKind(const std::string& s, BindingSpec::Kind& k) {
    if (s == "single") k = BindingSpec::Kind::Single; else if (s == "vector2") k = BindingSpec::Kind::Vector2; else if (s == "vector3") k = BindingSpec::Kind::Vector3;
    else if (s == "axis") k = BindingSpec::Kind::Axis1D; else if (s == "chord") k = BindingSpec::Kind::Chord; else return false;
    return true;
}
std::vector<std::string> Split(const std::string& line) {
    std::vector<std::string> t; std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        std::size_t j = i; while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != '\r') ++j;
        if (j > i) t.push_back(line.substr(i, j - i));
        i = j;
    }
    return t;
}
bool Opt(const std::string& tok, const char* key, std::string& val) {
    const std::string k = std::string(key) + "=";
    if (tok.compare(0, k.size(), k) != 0) return false;
    val = tok.substr(k.size());
    return true;
}
std::string F(float f) { char b[32]; std::snprintf(b, sizeof b, "%g", f); return b; }

} // namespace

std::string SerializeInputAsset(const ActionSet& set) {
    std::string s = "okayinput 1\n";
    for (const ControlScheme& c : set.Schemes()) {
        s += "scheme " + NoSpace(c.name);
        if (c.keyboard) s += " keyboard";
        if (c.mouse) s += " mouse";
        if (c.gamepad) s += " gamepad";
        if (c.touch) s += " touch";
        s += "\n";
    }
    for (const ActionMap& m : set.Maps())
        s += "map " + NoSpace(m.name) + " priority=" + std::to_string(m.priority) + " consume=" + (m.consume ? "1" : "0") + " enabled=" + (m.enabled ? "1" : "0") + "\n";
    for (const ActionDef& a : set.Actions()) {
        s += "action " + NoSpace(a.name) + " " + ValueTypeName(a.type);
        if (!a.map.empty()) s += " map=" + NoSpace(a.map);
        if (a.pressPoint != 0.5f) s += " press=" + F(a.pressPoint);
        s += "\n";
        for (const BindingSpec& b : a.bindings) {
            // a rebound binding is saved as it was authored: overrides live in their own file
            const std::vector<std::string>& parts = b.original.empty() ? b.parts : b.original;
            s += std::string("  bind ") + KindName(b.kind);
            for (const std::string& p : parts) s += " " + p;
            if (!b.group.empty()) s += " group=" + NoSpace(b.group);
            if (!b.processors.empty()) { std::string p = ProcessorsToString(b.processors); for (char& c : p) if (c == ' ') c = ';'; s += " proc=" + p; }
            s += "\n";
        }
        for (const Interaction& i : a.interactions) s += "  interaction " + i.ToString() + "\n";
        if (!a.processors.empty()) s += "  processor " + ProcessorsToString(a.processors) + "\n";
    }
    return s;
}

bool ParseInputAsset(const std::string& text, ActionSet& out, std::string* error) {
    ActionSet set;
    std::istringstream in(text);
    std::string line; int n = 0; bool header = false;
    ActionDef* cur = nullptr;
    auto fail = [&](const std::string& why) { if (error) *error = "line " + std::to_string(n) + ": " + why; return false; };
    while (std::getline(in, line)) {
        ++n;
        const std::vector<std::string> t = Split(line);
        if (t.empty() || t[0][0] == '#') continue;
        if (!header) { if (t[0] != "okayinput" || t.size() < 2 || t[1] != "1") return fail("expected \"okayinput 1\""); header = true; continue; }
        if (t[0] == "scheme") {
            if (t.size() < 2) return fail("scheme needs a name");
            ControlScheme c; c.name = t[1];
            for (std::size_t i = 2; i < t.size(); ++i) {
                if (t[i] == "keyboard") c.keyboard = true; else if (t[i] == "mouse") c.mouse = true; else if (t[i] == "gamepad") c.gamepad = true; else if (t[i] == "touch") c.touch = true;
                else return fail("unknown scheme flag " + t[i]);
            }
            set.AddScheme(c);
        } else if (t[0] == "map") {
            if (t.size() < 2) return fail("map needs a name");
            int pr = 0; bool cons = false, en = true;
            for (std::size_t i = 2; i < t.size(); ++i) {
                std::string v;
                if (Opt(t[i], "priority", v)) pr = std::atoi(v.c_str()); else if (Opt(t[i], "consume", v)) cons = v == "1";
                else if (Opt(t[i], "enabled", v)) en = v != "0"; else return fail("unknown map option " + t[i]);
            }
            set.AddMap(t[1], pr, cons, en);
        } else if (t[0] == "action") {
            if (t.size() < 3) return fail("action needs a name and a type");
            ValueType vt;
            if (!ParseValueType(t[2], vt)) return fail("unknown value type " + t[2]);
            std::string map; float press = 0.5f;
            for (std::size_t i = 3; i < t.size(); ++i) {
                std::string v;
                if (Opt(t[i], "map", v)) map = v; else if (Opt(t[i], "press", v)) press = (float)std::atof(v.c_str()); else return fail("unknown action option " + t[i]);
            }
            cur = &set.Add(t[1], vt, map);
            cur->pressPoint = press;
        } else if (t[0] == "bind") {
            if (!cur) return fail("bind before any action");
            if (t.size() < 3) return fail("bind needs a kind and a path");
            BindingSpec::Kind k;
            if (!ParseKind(t[1], k)) return fail("unknown bind kind " + t[1]);
            std::vector<std::string> parts; std::string group, proc;
            for (std::size_t i = 2; i < t.size(); ++i) {
                std::string v;
                if (Opt(t[i], "group", v)) group = v; else if (Opt(t[i], "proc", v)) proc = v; else parts.push_back(t[i]);
            }
            const std::size_t want = k == BindingSpec::Kind::Single ? 1 : k == BindingSpec::Kind::Vector2 ? 4 : k == BindingSpec::Kind::Vector3 ? 6 : k == BindingSpec::Kind::Axis1D ? 2 : 0;
            if (k == BindingSpec::Kind::Chord ? parts.size() < 2 : parts.size() != want) return fail(std::string("wrong number of inputs for ") + t[1]);
            for (const std::string& p : parts) if (InputBinding::Parse(p).kind == InputBinding::Kind::None) return fail("cannot read input " + p);
            std::vector<Processor> pl;
            if (!ParseProcessors(proc, pl)) return fail("cannot read processors " + proc);
            BindingSpec b; b.kind = k; b.parts = parts; b.group = group; b.processors = pl;
            cur->bindings.push_back(b);
        } else if (t[0] == "interaction") {
            if (!cur) return fail("interaction before any action");
            if (t.size() != 2 || !cur->AddInteraction(t[1])) return fail("cannot read interaction");
        } else if (t[0] == "processor") {
            if (!cur) return fail("processor before any action");
            std::string rest; for (std::size_t i = 1; i < t.size(); ++i) rest += (i > 1 ? " " : "") + t[i];
            if (!ParseProcessors(rest, cur->processors)) return fail("cannot read processors " + rest);
        } else return fail("unknown line \"" + t[0] + "\"");
    }
    if (!header) { if (error) *error = "empty file"; return false; }
    out = set;
    return true;
}

bool LoadInputAsset(const std::string& path, ActionSet& out, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return ParseInputAsset(ss.str(), out, error);
}
bool SaveInputAsset(const std::string& path, const ActionSet& set) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << SerializeInputAsset(set);
    return (bool)f;
}

const std::string& DefaultInputAssetText() {
    static const std::string t = R"(okayinput 1
# The default input asset for a new project. Edit it, or add your own maps and actions.
scheme Keyboard+Mouse keyboard mouse
scheme Gamepad gamepad
scheme Touch touch
map Gameplay priority=0 consume=0 enabled=1
map UI priority=10 consume=1 enabled=0
action Move vector2 map=Gameplay
  bind vector2 key:w key:s key:a key:d group=Keyboard+Mouse
  bind vector2 key:up key:down key:left key:right group=Keyboard+Mouse
  bind single stick:left group=Gamepad proc=deadzone(0.18,1)
  bind single dpad group=Gamepad
action Look vector2 map=Gameplay
  bind single mouse:delta group=Keyboard+Mouse
  bind single stick:right group=Gamepad proc=deadzone(0.18,1);curve(2)
action Jump button map=Gameplay
  bind single key:space group=Keyboard+Mouse
  bind single pad:a group=Gamepad
action Sprint button map=Gameplay
  bind single key:shift group=Keyboard+Mouse
  bind single pad:ls group=Gamepad
action Crouch button map=Gameplay
  bind single key:ctrl group=Keyboard+Mouse
  bind single pad:b group=Gamepad
action Fire button map=Gameplay
  bind single mouse:left group=Keyboard+Mouse
  bind single pad:rt group=Gamepad
action Aim button map=Gameplay
  bind single mouse:right group=Keyboard+Mouse
  bind single pad:lt group=Gamepad
action Interact button map=Gameplay
  bind single key:e group=Keyboard+Mouse
  bind single pad:x group=Gamepad
action Reload button map=Gameplay
  bind single key:r group=Keyboard+Mouse
  bind single pad:y group=Gamepad
action Inventory button map=Gameplay
  bind single key:tab group=Keyboard+Mouse
  bind single pad:back group=Gamepad
action Pause button map=Gameplay
  bind single key:escape group=Keyboard+Mouse
  bind single pad:start group=Gamepad
action Navigate vector2 map=UI
  bind vector2 key:w key:s key:a key:d group=Keyboard+Mouse
  bind vector2 key:up key:down key:left key:right group=Keyboard+Mouse
  bind single stick:left group=Gamepad proc=deadzone(0.3,1)
  bind single dpad group=Gamepad
action Submit button map=UI
  bind single key:enter group=Keyboard+Mouse
  bind single key:space group=Keyboard+Mouse
  bind single pad:a group=Gamepad
action Cancel button map=UI
  bind single key:escape group=Keyboard+Mouse
  bind single pad:b group=Gamepad
)";
    return t;
}
ActionSet DefaultInputAsset() { ActionSet s; ParseInputAsset(DefaultInputAssetText(), s); return s; }

std::vector<std::string> BindingParts(const InputBinding& b) {
    if (b.kind != InputBinding::Kind::Keys4) return {b.ToString()};
    std::vector<std::string> parts;
    for (char c : b.keys) { InputBinding k; k.kind = InputBinding::Kind::Key; k.code = (unsigned char)c; parts.push_back(k.ToString()); }
    return parts;
}

ActionSet MigrateInputActions() {
    ActionSet set;
    set.AddScheme({"Keyboard+Mouse", true, true, false, false});
    set.AddScheme({"Gamepad", false, false, true, false});
    int prio = 1;
    for (const InputActions::Action& a : InputActions::All()) {
        const std::string map = a.context == "" || a.context == "Gameplay" ? "" : a.context;
        if (!map.empty()) { bool have = false; for (const ActionMap& m : set.Maps()) if (m.name == map) have = true; if (!have) set.AddMap(map, prio++ * 10, false, false); }
        ActionDef& d = set.Add(a.name, a.direction ? ValueType::Vector2 : ValueType::Button, map);
        d.pressPoint = a.threshold;
        std::string proc;
        if (a.deadzone > 0) proc += "deadzone(" + F(a.deadzone) + ") ";
        if (a.curve != 1.0f) proc += "curve(" + F(a.curve) + ") ";
        if (a.sensitivity != 1.0f) proc += "scale(" + F(a.sensitivity) + ") ";
        if (a.invertX || a.invertY) proc += std::string("invert(") + (a.invertX ? "x" : "") + (a.invertY ? "y" : "") + ")";
        for (const InputBinding& b : a.bindings) {
            const std::string group = b.IsGamepad() ? "Gamepad" : "Keyboard+Mouse";
            const std::vector<std::string> p = BindingParts(b);
            if (b.kind == InputBinding::Kind::Keys4) d.AddVector2(p[0], p[1], p[2], p[3], group);
            else d.AddBinding(p[0], group);
        }
        ParseProcessors(proc, d.processors);
    }
    return set;
}

} // namespace okay
