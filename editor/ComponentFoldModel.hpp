// Which Inspector component headers are folded. No ImGui, no pointers: it is a pure model.
//
// The old store (main.cpp sCompOpen) was keyed by the component's memory address ("%p"). Every case that makes new
// component objects (select another object, Play/Stop, Undo/Redo, scene reload, restart) gave a new address, so the
// header fell back to "open"; a recycled address could even hand one object's state to another. This model is keyed
// by what the user means: the component TYPE (header label), so all Rigidbody headers stay as you left them. An
// object may carry its own override (keyed by the object's GUID), which wins over the type's state.
// Only the per-type state is saved (Serialize / Deserialize, one "type 0|1" line each); overrides last for the session
// and nothing is ever written to a scene file.
#pragma once
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace okay::editor {

class ComponentFoldModel {
public:
    // Is this header open? An object's override first, then the type's remembered state, then `defaultOpen`.
    bool IsOpen(const std::string& type, const std::string& objectKey, bool defaultOpen) const {
        if (!objectKey.empty()) {
            auto o = m_override.find(objectKey + "\n" + type);
            if (o != m_override.end()) return o->second;
        }
        auto t = m_type.find(type);
        return t == m_type.end() ? defaultOpen : t->second;
    }
    // The user toggled a header. Goes to the object's override when it has one, else to the type.
    void Set(const std::string& type, const std::string& objectKey, bool open) {
        if (!objectKey.empty()) {
            auto o = m_override.find(objectKey + "\n" + type);
            if (o != m_override.end()) { o->second = open; return; }
        }
        if (type.empty()) return;
        auto t = m_type.find(type);
        if (t == m_type.end() || t->second != open) { m_type[type] = open; m_dirty = true; }
    }
    bool HasOverride(const std::string& type, const std::string& objectKey) const {
        return !objectKey.empty() && m_override.count(objectKey + "\n" + type) > 0;
    }
    // Give this object its own state for this type (starting from what it shows now), or hand it back to the type.
    void SetOverride(const std::string& type, const std::string& objectKey, bool on, bool currentOpen) {
        if (objectKey.empty() || type.empty()) return;
        if (on) m_override[objectKey + "\n" + type] = currentOpen; else m_override.erase(objectKey + "\n" + type);
    }
    // Collapse All / Expand All: every type shown now (and every override on this object) gets `open`.
    void SetAll(const std::vector<std::string>& shownTypes, const std::string& objectKey, bool open) {
        for (const std::string& t : shownTypes) {
            if (t.empty()) continue;
            if (HasOverride(t, objectKey)) m_override[objectKey + "\n" + t] = open;
            else if (!m_type.count(t) || m_type[t] != open) { m_type[t] = open; m_dirty = true; }
        }
    }
    // Ctrl+click on a header: it is set to `open`, every other shown type is collapsed.
    void SetOnly(const std::string& keepType, const std::vector<std::string>& shownTypes, const std::string& objectKey, bool open) {
        std::vector<std::string> others;
        for (const std::string& t : shownTypes) if (t != keepType) others.push_back(t);
        SetAll(others, objectKey, false);
        Set(keepType, objectKey, open);
    }
    // Saved text: "type 0|1" per line (a type name has no line break; names with spaces are fine: the flag is last).
    std::string Serialize() const {
        std::ostringstream o;
        for (const auto& kv : m_type) o << kv.first << '\t' << (kv.second ? 1 : 0) << "\n";
        return o.str();
    }
    // Replace the per-type state from saved text. Bad lines are skipped. Overrides are kept.
    void Deserialize(const std::string& text) {
        m_type.clear();
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t p = line.rfind('\t');
            if (p == std::string::npos || p == 0 || p + 2 != line.size()) continue;
            const char c = line[p + 1];
            if (c != '0' && c != '1') continue;
            m_type[line.substr(0, p)] = c == '1';
        }
        m_dirty = false;
    }
    bool Dirty() const { return m_dirty; }
    void ClearDirty() { m_dirty = false; }
    size_t TypeCount() const { return m_type.size(); }
private:
    std::map<std::string, bool> m_type;       // component type -> open
    std::map<std::string, bool> m_override;   // objectKey + "\n" + type -> open (session only)
    bool m_dirty = false;
};

// A header's key. Several components of one type on one object ("Action" twice) get "Action", "Action #2", ...
// so they fold on their own; `ordinal` is 0 for the first of its type on the object.
inline std::string ComponentFoldKey(const std::string& label, int ordinal) {
    return ordinal <= 0 ? label : label + " #" + std::to_string(ordinal + 1);
}

// The one model both Inspectors (the Dear ImGui one in main.cpp and the window-mode one) share, and its preferences file
// (next to okay_settings.txt, in the working directory).
inline ComponentFoldModel& GlobalComponentFold() { static ComponentFoldModel m; return m; }
inline const char* ComponentFoldFile() { return "okay_inspector_folds.txt"; }
inline void LoadComponentFolds() {
    std::ifstream f(ComponentFoldFile());
    if (!f.good()) return;
    std::ostringstream o; o << f.rdbuf();
    GlobalComponentFold().Deserialize(o.str());
}
// Writes only when something changed; whole file or nothing (temp file, then rename).
inline void SaveComponentFoldsIfDirty() {
    ComponentFoldModel& m = GlobalComponentFold();
    if (!m.Dirty()) return;
    const std::string tmp = std::string(ComponentFoldFile()) + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f.good()) return;
        f << m.Serialize();
        if (!f.good()) return;
    }
    if (std::rename(tmp.c_str(), ComponentFoldFile()) != 0) { std::remove(ComponentFoldFile()); if (std::rename(tmp.c_str(), ComponentFoldFile()) != 0) return; }
    m.ClearDirty();
}

}  // namespace okay::editor
