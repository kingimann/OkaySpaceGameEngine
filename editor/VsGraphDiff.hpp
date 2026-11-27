#pragma once
// What changed between two versions of a graph (an earlier save from local history, and the graph now), block by block
// rather than line by line: blocks added, removed, changed (type, text field, typed values) or only moved; wires added
// and removed; variables added, removed or changed. Blocks are matched by id: the editor never reuses an id in a file.
// No ImGui: tests run it; the editor's Compare panel lists it and outlines the blocks on the canvas.
// Included from inside VisualScriptModel.hpp after the graph model. Written for this project.
#include <map>
#include <set>
#include <string>
#include <vector>

namespace okay::editor::vsed {

struct VsDiffNode { int id = -1; std::string type, before, after; };   // before / after: a short description

struct VsGraphDiff {
    std::vector<VsDiffNode> added, removed, changed, moved;
    std::vector<std::string> wiresAdded, wiresRemoved;     // "exec 3:0 -> 5", "data 5 <- 3:0"
    std::vector<std::string> varsAdded, varsRemoved, varsChanged;
    bool empty() const {
        return added.empty() && removed.empty() && changed.empty() && moved.empty() && wiresAdded.empty() &&
               wiresRemoved.empty() && varsAdded.empty() && varsRemoved.empty() && varsChanged.empty();
    }
    /// Ids the canvas outlines: added green, changed amber.
    std::set<int> addedIds() const { std::set<int> s; for (const auto& n : added) s.insert(n.id); return s; }
    std::set<int> changedIds() const { std::set<int> s; for (const auto& n : changed) s.insert(n.id); return s; }
};

/// A block's settings as one line: type, text field and typed values (not where it sits).
inline std::string VsDiffNodeText(const VsEditNode& n) {
    std::string s = n.type;
    if (!n.arg.empty()) s += " \"" + n.arg + "\"";
    for (const auto& kv : n.pinDefault) if (!kv.second.empty()) s += " [" + std::to_string(kv.first) + "]=" + kv.second;
    return s;
}

inline VsGraphDiff VsDiffGraphs(const VsEditGraph& before, const VsEditGraph& after) {
    VsGraphDiff d;
    std::map<int, const VsEditNode*> a, b;
    for (const auto& n : before.nodes) a[n.id] = &n;
    for (const auto& n : after.nodes) b[n.id] = &n;
    for (const auto& kv : b) {
        auto it = a.find(kv.first);
        const VsEditNode& nb = *kv.second;
        if (it == a.end()) { d.added.push_back({nb.id, nb.type, "", VsDiffNodeText(nb)}); continue; }
        const VsEditNode& na = *it->second;
        const std::string ta = VsDiffNodeText(na), tb = VsDiffNodeText(nb);
        if (ta != tb) d.changed.push_back({nb.id, nb.type, ta, tb});
        else if (VsCoord(na.x) != VsCoord(nb.x) || VsCoord(na.y) != VsCoord(nb.y)) d.moved.push_back({nb.id, nb.type, "", ""});
    }
    for (const auto& kv : a) if (!b.count(kv.first)) d.removed.push_back({kv.first, kv.second->type, VsDiffNodeText(*kv.second), ""});
    auto wires = [](const VsEditGraph& g) {
        std::set<std::string> s;
        for (const auto& l : g.execLinks) s.insert("exec " + std::to_string(l.from) + ":" + std::to_string(l.pin) + " -> " + std::to_string(l.to));
        for (const auto& l : g.dataLinks) s.insert("data " + std::to_string(l.consumer) + ":" + std::to_string(l.inPin) + " <- " +
                                                   std::to_string(l.producer) + ":" + std::to_string(l.outPin));
        return s;
    };
    const auto wa = wires(before), wb = wires(after);
    for (const auto& w : wb) if (!wa.count(w)) d.wiresAdded.push_back(w);
    for (const auto& w : wa) if (!wb.count(w)) d.wiresRemoved.push_back(w);
    auto vars = [](const VsEditGraph& g) {
        std::map<std::string, std::string> m;
        for (const auto& v : g.vars) m[v.scope + " " + v.name] = v.type + " = " + v.value;
        return m;
    };
    const auto va = vars(before), vb = vars(after);
    for (const auto& kv : vb) {
        auto it = va.find(kv.first);
        if (it == va.end()) d.varsAdded.push_back(kv.first + " (" + kv.second + ")");
        else if (it->second != kv.second) d.varsChanged.push_back(kv.first + ": " + it->second + " -> " + kv.second);
    }
    for (const auto& kv : va) if (!vb.count(kv.first)) d.varsRemoved.push_back(kv.first);
    return d;
}

inline VsGraphDiff VsDiffGraphTexts(const std::string& beforeText, const std::string& afterText) {
    VsEditGraph a, b;
    a.fromText(beforeText);
    b.fromText(afterText);
    return VsDiffGraphs(a, b);
}

/// The Compare panel: which earlier version is being compared with the graph on screen (one per editor).
struct VsDiffView {
    bool open = false;
    std::string path, label, beforeText;
};
inline VsDiffView& DiffView() { static VsDiffView v; return v; }
/// Called by the local-history menu (main.cpp's VsBackupMenu): compare the graph on screen with an earlier save.
inline void VsShowDiff(const std::string& path, const std::string& label, const std::string& beforeText) {
    VsDiffView& v = DiffView();
    v.open = true; v.path = path; v.label = label; v.beforeText = beforeText;
}

} // namespace okay::editor::vsed
