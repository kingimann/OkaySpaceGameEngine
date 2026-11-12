#pragma once
// The editor's scene edits as commands for okay::UndoStack (ImGui-free, so it is tested headless).
//
// An edit is stored as a TextDelta: the scene text (SceneSerializer::Serialize) before and after, cut down
// to the stretch that differs. A slider drag on one object therefore costs a few hundred bytes, not a copy
// of the whole scene. (Deleting or inserting an object renumbers the objects behind it, so the stretch then
// reaches to the end of the scene text: the worst case is the old full snapshot, never worse.)
//
// SceneDeltaCommand applies a delta to the live scene by rebuilding it from the edited text, so an undone
// delete brings the object back with the same GUID, components, children and sibling position. Both
// directions check that the scene is in the state the command expects; if not (an edit nobody recorded)
// the command refuses and the stack drops it as stale instead of corrupting the scene.
#include "okay/Core/Undo.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace okay::editor {

/// The difference between two texts: what both share at the start and at the end is not stored.
struct TextDelta {
    std::size_t prefix = 0;       ///< bytes the two texts share at the start
    std::size_t suffix = 0;       ///< bytes they share at the end
    std::string before, after;    ///< what lies between, in the old and in the new text
    std::size_t lenBefore = 0, lenAfter = 0;

    static TextDelta Make(const std::string& a, const std::string& b) {
        TextDelta d;
        d.lenBefore = a.size(); d.lenAfter = b.size();
        const std::size_t m = std::min(a.size(), b.size());
        std::size_t p = 0;
        while (p < m && a[p] == b[p]) ++p;
        std::size_t s = 0;
        while (s < m - p && a[a.size() - 1 - s] == b[b.size() - 1 - s]) ++s;
        d.prefix = p; d.suffix = s;
        d.before = a.substr(p, a.size() - p - s);
        d.after = b.substr(p, b.size() - p - s);
        return d;
    }
    bool Empty() const { return before == after; }
    std::size_t Bytes() const { return before.size() + after.size(); }

    /// `cur` is the old text: produce the new one. False if `cur` is not the old text.
    bool ApplyForward(const std::string& cur, std::string& out) const { return Apply(cur, lenBefore, before, after, out); }
    /// `cur` is the new text: produce the old one.
    bool ApplyBackward(const std::string& cur, std::string& out) const { return Apply(cur, lenAfter, after, before, out); }

    /// a (T0 -> T1) followed by b (T1 -> T2) as one delta T0 -> T2. False when the two changed places that
    /// do not touch (the text between them is not kept), so they stay two steps.
    static bool Compose(const TextDelta& a, const TextDelta& b, TextDelta& out) {
        if (a.lenAfter != b.lenBefore) return false;
        const std::size_t aS = a.prefix, aE = a.prefix + a.after.size();    // a's change, in T1
        const std::size_t bS = b.prefix, bE = b.prefix + b.before.size();   // b's change, in T1
        if (aE < bS || bE < aS) return false;
        const std::size_t u0 = std::min(aS, bS), u1 = std::max(aE, bE);
        std::string s1(u1 - u0, '\0');
        std::vector<char> known(u1 - u0, 0);
        auto put = [&](std::size_t at, const std::string& t) {
            for (std::size_t i = 0; i < t.size(); ++i) {
                const std::size_t k = at - u0 + i;
                if (known[k] && s1[k] != t[i]) return false;
                s1[k] = t[i]; known[k] = 1;
            }
            return true;
        };
        if (!put(aS, a.after) || !put(bS, b.before)) return false;
        const std::string s0 = s1.substr(0, aS - u0) + a.before + s1.substr(aE - u0);
        const std::string s2 = s1.substr(0, bS - u0) + b.after + s1.substr(bE - u0);
        TextDelta inner = Make(s0, s2);
        out.prefix = u0 + inner.prefix;
        out.suffix = (a.lenAfter - u1) + inner.suffix;
        out.before = std::move(inner.before);
        out.after = std::move(inner.after);
        out.lenBefore = a.lenBefore; out.lenAfter = b.lenAfter;
        return true;
    }

private:
    bool Apply(const std::string& cur, std::size_t expectLen, const std::string& from, const std::string& to, std::string& out) const {
        if (cur.size() != expectLen || prefix + from.size() + suffix != expectLen) return false;
        if (cur.compare(prefix, from.size(), from) != 0) return false;
        out.clear();
        out.reserve(prefix + to.size() + suffix);
        out.append(cur, 0, prefix);
        out += to;
        out.append(cur, cur.size() - suffix, suffix);
        return true;
    }
};

/// Which objects were selected (by GUID: an object that is rebuilt keeps its GUID, its pointer changes).
struct SelectionRef {
    bool valid = false;                 ///< false: leave the selection alone
    std::vector<std::string> guids;
    std::string primary;
};

/// How a command reaches the live scene. Owned by the editor state and shared by its commands.
struct SceneAccess {
    std::function<std::string()> capture;                                          ///< the scene as text, now
    std::function<bool(const std::string&, const SelectionRef&)> apply;            ///< replace the scene, restore the selection
};

class SceneDeltaCommand : public ICommand {
public:
    SceneDeltaCommand(std::string name, std::shared_ptr<SceneAccess> scene, TextDelta delta,
                      SelectionRef selBefore, SelectionRef selAfter, std::uint64_t mergeKey = 0)
        : m_name(std::move(name)), m_scene(std::move(scene)), m_delta(std::move(delta)),
          m_selBefore(std::move(selBefore)), m_selAfter(std::move(selAfter)), m_key(mergeKey) {}

    bool Do() override { return Run(false); }
    bool Undo() override { return Run(true); }
    std::string Name() const override { return m_name; }
    bool IsNoOp() const override { return m_delta.Empty(); }
    std::size_t MemoryBytes() const override {
        std::size_t n = sizeof(*this) + m_name.capacity() + m_delta.Bytes();
        for (const auto& g : m_selBefore.guids) n += g.capacity() + sizeof(std::string);
        for (const auto& g : m_selAfter.guids) n += g.capacity() + sizeof(std::string);
        return n;
    }
    /// Consecutive edits with the same non-zero key (one field nudged again and again) are one step.
    bool Merge(const ICommand& next) override {
        const auto* o = dynamic_cast<const SceneDeltaCommand*>(&next);
        if (!o || m_key == 0 || o->m_key != m_key) return false;
        TextDelta joined;
        if (!TextDelta::Compose(m_delta, o->m_delta, joined)) return false;
        m_delta = std::move(joined);
        m_selAfter = o->m_selAfter;
        return true;
    }
    const TextDelta& Delta() const { return m_delta; }
    std::uint64_t MergeKey() const { return m_key; }

private:
    bool Run(bool backward) {
        if (!m_scene || !m_scene->capture || !m_scene->apply) return false;
        std::string out;
        const std::string cur = m_scene->capture();
        if (!(backward ? m_delta.ApplyBackward(cur, out) : m_delta.ApplyForward(cur, out))) return false;
        return m_scene->apply(out, backward ? m_selBefore : m_selAfter);
    }
    std::string m_name;
    std::shared_ptr<SceneAccess> m_scene;
    TextDelta m_delta;
    SelectionRef m_selBefore, m_selAfter;
    std::uint64_t m_key;
};

// ---- Naming: what did this edit do? -------------------------------------------------------------------

namespace detail {
struct ObjInfo {
    std::string name;
    std::string body;       ///< the object's lines without its index and parent link
    std::string parent;     ///< the parent as a GUID ("-" for a root)
    std::string sibling;    ///< the sibling position record, if the order was changed
    bool onlyTransform = false;
    std::string withoutTransform;   ///< body minus the transform line
};
struct SceneInfo {
    std::string header;                         ///< everything before the first object
    std::map<std::string, ObjInfo> objects;     ///< by GUID
};

inline std::string Unquote(const std::string& line, std::size_t from) {
    const std::size_t a = line.find('"', from);
    if (a == std::string::npos) return {};
    std::string out;
    for (std::size_t i = a + 1; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) { out += line[++i]; continue; }
        if (line[i] == '"') break;
        out += line[i];
    }
    return out;
}

inline SceneInfo Parse(const std::string& text) {
    SceneInfo si;
    struct Raw { std::string name, guid, body, withoutTransform; int parent = -1; std::string sibling; int index = -1; };
    std::vector<Raw> raws;
    Raw cur; bool inObj = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        if (!inObj) {
            if (line.compare(0, 11, "gameobject ") == 0) {
                inObj = true; cur = Raw{};
                cur.index = std::atoi(line.c_str() + 11);
                cur.name = Unquote(line, 11);
            } else si.header += line + "\n";
            continue;
        }
        if (line == "end") { raws.push_back(cur); inObj = false; continue; }
        if (line.compare(0, 8, "  guid \"") == 0) cur.guid = Unquote(line, 6);
        if (line.compare(0, 9, "  parent ") == 0) { cur.parent = std::atoi(line.c_str() + 9); continue; }
        if (line.compare(0, 10, "  sibling ") == 0) { cur.sibling = line; continue; }
        cur.body += line + "\n";
        if (line.compare(0, 12, "  transform ") != 0) cur.withoutTransform += line + "\n";
    }
    std::map<int, std::string> guidOf;
    for (const Raw& r : raws) guidOf[r.index] = r.guid;
    for (const Raw& r : raws) {
        if (r.guid.empty()) continue;
        ObjInfo oi;
        oi.name = r.name; oi.body = r.body; oi.withoutTransform = r.withoutTransform;
        auto pg = guidOf.find(r.parent);
        oi.parent = pg == guidOf.end() ? std::string("-") : pg->second;
        oi.sibling = r.sibling;
        si.objects[r.guid] = std::move(oi);
    }
    return si;
}
}  // namespace detail

/// "Delete Cube", "Create 3 Objects", "Move Cube", "Reparent Cube", "Edit Cube", "Scene Settings" ...
/// worked out from the two scene texts. Never empty.
inline std::string DescribeSceneChange(const std::string& before, const std::string& after) {
    constexpr std::size_t kMaxParse = 24u * 1024u * 1024u;
    if (before.size() > kMaxParse || after.size() > kMaxParse) return "Edit";
    const detail::SceneInfo a = detail::Parse(before), b = detail::Parse(after);
    std::vector<std::string> removed, added, edited, moved, reparented, reordered;
    for (const auto& kv : a.objects) {
        auto it = b.objects.find(kv.first);
        if (it == b.objects.end()) { removed.push_back(kv.second.name); continue; }
        const detail::ObjInfo& x = kv.second; const detail::ObjInfo& y = it->second;
        if (x.body == y.body && x.parent == y.parent && x.sibling == y.sibling && x.name == y.name) continue;
        if (x.withoutTransform == y.withoutTransform && x.parent == y.parent && x.sibling == y.sibling && x.name == y.name) moved.push_back(y.name);
        else if (x.body == y.body && x.name == y.name && x.parent != y.parent) reparented.push_back(y.name);
        else if (x.body == y.body && x.name == y.name) reordered.push_back(y.name);
        else edited.push_back(y.name);
    }
    for (const auto& kv : b.objects) if (!a.objects.count(kv.first)) added.push_back(kv.second.name);
    auto many = [](const char* verb, const std::vector<std::string>& v) {
        return v.size() == 1 ? std::string(verb) + " " + v[0] : std::string(verb) + " " + std::to_string(v.size()) + " Objects";
    };
    // Objects that come or go move the sibling positions and parent numbers of others: that is not an edit of theirs.
    if (!added.empty() || !removed.empty()) {
        if (!edited.empty() || (!added.empty() && !removed.empty())) return "Edit Hierarchy";
        return removed.empty() ? many("Create", added) : many("Delete", removed);
    }
    if (!reparented.empty() && edited.empty()) return many("Reparent", reparented);   // the old neighbours' positions shift with it
    if (reparented.empty() && edited.empty() && moved.empty() && !reordered.empty()) return "Reorder Hierarchy";
    const std::size_t kinds = (!edited.empty()) + (!moved.empty()) + (!reparented.empty());
    if (kinds == 1) {
        if (!moved.empty()) return many("Move", moved);
        return many("Edit", edited);
    }
    if (kinds > 1) return "Edit " + std::to_string(edited.size() + moved.size() + reparented.size()) + " Objects";
    if (a.header != b.header) return "Scene Settings";
    return "Edit";
}

}  // namespace okay::editor
