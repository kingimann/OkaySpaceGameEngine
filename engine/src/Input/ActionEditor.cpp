#include "okay/Input/ActionEditor.hpp"
#include "okay/Input/PadProfiles.hpp"
#include <cstdio>

namespace okay {
namespace {
bool Spaced(const std::string& s) { return s.empty() || s.find_first_of(" \t\r\n") != std::string::npos; }
std::string Num(float f) { if (f > -0.005f && f < 0.005f) f = 0.0f; char b[32]; std::snprintf(b, sizeof b, "%.2f", f); return b; }
std::size_t PartCount(BindingSpec::Kind k) { return k == BindingSpec::Kind::Single ? 1 : k == BindingSpec::Kind::Vector2 ? 4 : k == BindingSpec::Kind::Vector3 ? 6 : k == BindingSpec::Kind::Axis1D ? 2 : 2; }
}

bool ActionEditor::Load(const std::string& file, std::string* error) {
    if (!LoadInputAsset(file, set, error)) return false;
    path = file; dirty = false;
    return true;
}
bool ActionEditor::Save() { if (path.empty() || !SaveInputAsset(path, set)) return false; dirty = false; return true; }
bool ActionEditor::SaveAs(const std::string& file) { if (!SaveInputAsset(file, set)) return false; path = file; dirty = false; return true; }

bool ActionEditor::AddMap(const std::string& name, int priority, bool consume) {
    if (Spaced(name)) return false;
    for (const ActionMap& m : set.Maps()) if (m.name == name) return false;
    set.AddMap(name, priority, consume, true);
    return Touch(true);
}
bool ActionEditor::AddAction(const std::string& name, ValueType type, const std::string& map) {
    if (Spaced(name) || set.Find(name)) return false;
    if (!map.empty()) { bool have = false; for (const ActionMap& m : set.Maps()) if (m.name == map) have = true; if (!have) return false; }
    set.Add(name, type, map);
    return Touch(true);
}
bool ActionEditor::AddBinding(const std::string& action, BindingSpec::Kind kind, const std::string& group) {
    ActionDef* a = set.Find(action);
    if (!a) return false;
    BindingSpec b; b.kind = kind; b.group = group; b.parts.assign(PartCount(kind), "key:space");
    a->bindings.push_back(b);
    return Touch(true);
}
bool ActionEditor::RemoveBinding(const std::string& action, int binding) {
    ActionDef* a = set.Find(action);
    if (!a || binding < 0 || binding >= (int)a->bindings.size()) return false;
    a->bindings.erase(a->bindings.begin() + binding);
    return Touch(true);
}
bool ActionEditor::SetPart(const std::string& action, int binding, int part, const std::string& path) {
    ActionDef* a = set.Find(action);
    if (!a || binding < 0 || binding >= (int)a->bindings.size()) return false;
    BindingSpec& b = a->bindings[(std::size_t)binding];
    if (part < 0 || part >= (int)b.parts.size() || InputBinding::Parse(path).kind == InputBinding::Kind::None) return false;
    b.parts[(std::size_t)part] = path; b.built = false; b.original.clear();
    return Touch(true);
}
bool ActionEditor::SetGroup(const std::string& action, int binding, const std::string& group) {
    ActionDef* a = set.Find(action);
    if (!a || binding < 0 || binding >= (int)a->bindings.size()) return false;
    a->bindings[(std::size_t)binding].group = group;
    return Touch(true);
}
bool ActionEditor::SetProcessors(const std::string& action, int binding, const std::string& text) {
    ActionDef* a = set.Find(action);
    if (!a) return false;
    std::vector<Processor> list;
    if (!ParseProcessors(text, list)) return false;
    if (binding < 0) a->processors = list;
    else if (binding < (int)a->bindings.size()) a->bindings[(std::size_t)binding].processors = list;
    else return false;
    return Touch(true);
}
bool ActionEditor::AddInteraction(const std::string& action, const std::string& text) {
    ActionDef* a = set.Find(action);
    return a && Touch(a->AddInteraction(text));
}
bool ActionEditor::RemoveInteraction(const std::string& action, int index) {
    ActionDef* a = set.Find(action);
    if (!a || index < 0 || index >= (int)a->interactions.size()) return false;
    a->interactions.erase(a->interactions.begin() + index);
    return Touch(true);
}
bool ActionEditor::AddScheme(const std::string& name, bool k, bool m, bool g, bool t) {
    if (Spaced(name)) return false;
    for (const ControlScheme& c : set.Schemes()) if (c.name == name) return false;
    set.AddScheme({name, k, m, g, t});
    return Touch(true);
}

std::vector<std::string> ActionEditor::Validate() const {
    std::vector<std::string> out;
    auto hasScheme = [&](const std::string& g) { for (const ControlScheme& c : set.Schemes()) if (c.name == g) return true; return false; };
    auto hasMap = [&](const std::string& m) { for (const ActionMap& x : set.Maps()) if (x.name == m) return true; return false; };
    for (const ControlScheme& c : set.Schemes()) if (Spaced(c.name)) out.push_back("scheme \"" + c.name + "\": the name is empty or has a space");
    for (const ActionMap& m : set.Maps()) if (Spaced(m.name)) out.push_back("map \"" + m.name + "\": the name is empty or has a space");
    for (std::size_t i = 0; i < set.Actions().size(); ++i) {
        const ActionDef& a = set.Actions()[i];
        if (Spaced(a.name)) out.push_back("action \"" + a.name + "\": the name is empty or has a space");
        for (std::size_t j = i + 1; j < set.Actions().size(); ++j) if (set.Actions()[j].name == a.name) out.push_back("action " + a.name + ": defined twice");
        if (!a.map.empty() && !hasMap(a.map)) out.push_back("action " + a.name + ": map " + a.map + " does not exist");
        if (a.bindings.empty()) out.push_back("action " + a.name + ": no bindings");
        for (std::size_t b = 0; b < a.bindings.size(); ++b) {
            const BindingSpec& bs = a.bindings[b];
            const std::string where = "action " + a.name + " binding " + std::to_string(b + 1);
            if (!bs.group.empty() && !hasScheme(bs.group)) out.push_back(where + ": group " + bs.group + " is not a control scheme");
            for (const std::string& p : bs.parts) if (InputBinding::Parse(p).kind == InputBinding::Kind::None) out.push_back(where + ": cannot read input \"" + p + "\"");
        }
        for (const Interaction& it : a.interactions)
            if (it.kind == Interaction::Kind::Chorded && !set.Find(it.arg)) out.push_back("action " + a.name + ": chorded(" + it.arg + ") names a missing action");
    }
    return out;
}

std::vector<std::string> ActionEditor::Outline() const {
    std::vector<std::string> out;
    out.push_back(std::string("ASSET  ") + (path.empty() ? "(not saved)" : path) + (dirty ? "  *" : ""));
    std::string sc = "schemes:";
    for (const ControlScheme& c : set.Schemes()) sc += " " + c.name;
    out.push_back(sc);
    auto list = [&](const std::string& map, const std::string& title) {
        out.push_back(title);
        for (const ActionDef& a : set.Actions())
            if (a.map == map) out.push_back("    " + a.name + "  [" + ValueTypeName(a.type) + "]  " + std::to_string(a.bindings.size()) + " binding" + (a.bindings.size() == 1 ? "" : "s") +
                                            (a.interactions.empty() ? "" : "  " + a.interactions[0].ToString()));
    };
    for (const ActionMap& m : set.Maps()) list(m.name, "MAP " + m.name + "  priority " + std::to_string(m.priority) + (m.consume ? "  consumes" : "") + (m.enabled ? "" : "  (off)"));
    bool anyDefault = false;
    for (const ActionDef& a : set.Actions()) if (a.map.empty()) anyDefault = true;
    if (anyDefault) list("", "MAP (default)");
    return out;
}

std::vector<ActionEditor::ControlRow> ActionEditor::ControlRows() const {
    std::vector<ControlRow> rows;
    for (const ActionDef& a : set.Actions()) {
        ControlRow row;
        row.action = a.name;
        row.type = ValueTypeName(a.type);
        auto take = [](std::string& dest, const std::string& text) {
            if (text.empty()) return;
            if (!dest.empty()) dest += "  ";
            dest += text;
        };
        for (const BindingSpec& b : a.bindings) {
            bool pad = false, other = false;
            std::string label;
            const char* kind = b.kind == BindingSpec::Kind::Vector2 ? "vector " : b.kind == BindingSpec::Kind::Vector3 ? "vector3 " :
                               b.kind == BindingSpec::Kind::Axis1D ? "axis " : b.kind == BindingSpec::Kind::Chord ? "chord " : "";
            for (std::size_t i = 0; i < b.parts.size(); ++i) {
                const InputBinding leaf = InputBinding::Parse(b.parts[i]);
                if (leaf.kind == InputBinding::Kind::None) continue;
                if (leaf.IsGamepad()) pad = true; else other = true;
                if (!label.empty()) label += " / ";
                label += leaf.Label();
            }
            if (label.empty()) continue;
            label = kind + label;
            if (pad && !other) take(row.gamepad, label);
            else take(row.keyboard, label);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<std::string> ActionEditor::Describe(const std::string& action) const {
    std::vector<std::string> out;
    const ActionDef* a = set.Find(action);
    if (!a) { out.push_back("(no action selected)"); return out; }
    out.push_back(a->name + "   type " + ValueTypeName(a->type) + "   map " + (a->map.empty() ? "(default)" : a->map) + "   press point " + Num(a->pressPoint));
    static const char* kPart[][6] = {{"input"}, {"up", "down", "left", "right"}, {"up", "down", "left", "right", "forward", "back"}, {"negative", "positive"}, {"modifier", "input"}};
    for (std::size_t i = 0; i < a->bindings.size(); ++i) {
        const BindingSpec& b = a->bindings[i];
        const int row = b.kind == BindingSpec::Kind::Single ? 0 : b.kind == BindingSpec::Kind::Vector2 ? 1 : b.kind == BindingSpec::Kind::Vector3 ? 2 : b.kind == BindingSpec::Kind::Axis1D ? 3 : 4;
        std::string line = std::to_string(i + 1) + ". ";
        line += row == 0 ? "single" : row == 1 ? "2D vector" : row == 2 ? "3D vector" : row == 3 ? "1D axis" : "chord";
        for (std::size_t p = 0; p < b.parts.size(); ++p) line += "  " + std::string(row == 4 ? (p + 1 < b.parts.size() ? "modifier" : "input") : (p < 6 && kPart[row][p] ? kPart[row][p] : "")) + "=" + b.parts[p];
        line += "   group " + (b.group.empty() ? std::string("(all)") : b.group);
        if (!b.processors.empty()) line += "   " + ProcessorsToString(b.processors);
        out.push_back(line);
    }
    for (const Interaction& i : a->interactions) out.push_back("interaction " + i.ToString());
    if (!a->processors.empty()) out.push_back("action processors " + ProcessorsToString(a->processors));
    return out;
}

std::vector<std::string> ControllerDebugLines() {
    std::vector<std::string> out;
    const char* dev = Input::LastDevice() == Input::Device::Gamepad ? "controller" : Input::LastDevice() == Input::Device::Mouse ? "mouse" : Input::LastDevice() == Input::Device::Touch ? "touch" : "keyboard";
    out.push_back(std::string("last used device: ") + dev + (Input::LastDevice() == Input::Device::Gamepad ? " " + std::to_string(Input::LastGamepad() + 1) : std::string()));
    bool any = false;
    for (int p = 0; p < Input::kMaxGamepads; ++p) {
        const PadProfile& pr = PadProfiles::Get(p);
        if (!pr.present) continue;
        any = true;
        out.push_back("controller " + std::to_string(p + 1) + ": " + (pr.id.name.empty() ? pr.info.familyName : pr.id.name) + "  [" + pr.info.familyName + ", " + FormatUsbId(pr.id.vendor, pr.id.product) +
                      ", mapping " + MapSourceName(pr.mapSource) + ", battery " + PadProfiles::BatteryText(p) + "]");
    }
    if (!any) out.push_back("no controller connected");
    return out;
}

std::vector<std::string> ActionSetDebugLines(const ActionSet& set, int maxActions) {
    std::vector<std::string> out;
    std::string head = "scheme " + set.Scheme() + (set.AutoSwitch() ? " (auto)" : "") + "   pad " + (set.Pad() < 0 ? std::string("any") : std::to_string(set.Pad() + 1)) + "   maps:";
    for (const ActionMap& m : set.Maps()) head += " " + m.name + (m.enabled ? "(on)" : "(off)");
    out.push_back(head);
    int n = 0;
    for (const ActionDef& a : set.Actions()) {
        if (n++ >= maxActions) { out.push_back("..."); break; }
        std::string v;
        if (a.type == ValueType::Button) v = a.down ? "DOWN" : "up";
        else if (a.type == ValueType::Axis1D) v = Num(a.value.x);
        else if (a.type == ValueType::Vector2) v = Num(a.value.x) + "," + Num(a.value.y);
        else v = Num(a.value.x) + "," + Num(a.value.y) + "," + Num(a.value.z);
        const bool live = a.phase == Phase::Waiting && (a.down || a.value.Length() > 0);   // held steady: no phase change this frame
        std::string line = a.name + " = " + v + "   " + (live ? "active" : PhaseName(a.phase));
        if (a.Progress() > 0) line += "   hold " + Num(a.Progress());
        if (a.activeBinding >= 0) line += "   via " + (a.bindings[(std::size_t)a.activeBinding].parts.empty() ? std::string() : a.bindings[(std::size_t)a.activeBinding].parts[0]);
        out.push_back(line);
    }
    return out;
}

} // namespace okay
