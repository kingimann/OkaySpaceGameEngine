#pragma once
// Sample scenes for the Hierarchy panel's two versions (okayui/tests/test_okayui_hierarchy.cpp, tests/test_okayui_hierarchy_imgui.cpp):
// a plain tree of named objects standing in for the editor's scene, with the editor's row rules (top-level objects open, deeper ones
// closed, children in order) and a small host that carries out the panel's intents the way the editor does (selection, the arrow, the
// eye and the padlock, renames, drops, Ctrl+A). Std only; it also counts how many rows the panel asked for.
#include "EditorPanelModels.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace hierfix {

namespace P = okaypanels;

struct Node {
    std::string name;
    int parent = -1;
    std::vector<int> kids;
    int icon = P::HierIconDot;
    unsigned badges = 0;
    bool active = true, locked = false;
    int prefab = P::HierPrefabNone;
    bool nested = false, changed = false;
    int diag = 0;
    int open = -1;   // -1: the default (top level open, deeper closed)
};

struct Scene {
    std::vector<Node> nodes;
    std::vector<int> rows;            // the listed rows: node indexes, top to bottom
    std::vector<int> hits;            // search results
    std::set<int> selected;
    int active = -1;                  // the active (last selected) node
    std::string name = "Village";
    bool dirty = false;
    long fills = 0;                   // rows the panel asked for, since the last reset
    int isolate = -1;
    bool sortAZ = false;
    std::vector<std::string> log;     // what the host did, for the tests

    int Add(const std::string& n, int parent = -1, int icon = P::HierIconDot) {
        Node d; d.name = n; d.parent = parent; d.icon = icon;
        nodes.push_back(d);
        const int i = (int)nodes.size() - 1;
        if (parent >= 0) nodes[(std::size_t)parent].kids.push_back(i);
        return i;
    }
    static std::uint64_t Id(int node) { return (std::uint64_t)node + 1; }
    static int NodeOf(std::uint64_t id) { return (int)id - 1; }
    bool IsOpen(int i) const { const auto& n = nodes[(std::size_t)i]; return n.open >= 0 ? n.open == 1 : n.parent < 0; }
    bool Under(int i, int anc) const { for (int p = i; p >= 0; p = nodes[(std::size_t)p].parent) if (p == anc) return true; return false; }

    void Flatten() {
        rows.clear();
        std::vector<int> stack;
        for (int i = (int)nodes.size() - 1; i >= 0; --i) if (nodes[(std::size_t)i].parent < 0) stack.push_back(i);
        while (!stack.empty()) {
            const int i = stack.back(); stack.pop_back();
            rows.push_back(i);
            if (!nodes[(std::size_t)i].kids.empty() && IsOpen(i))
                for (auto it = nodes[(std::size_t)i].kids.rbegin(); it != nodes[(std::size_t)i].kids.rend(); ++it) stack.push_back(*it);
        }
    }
    int Depth(int i) const { int d = 0; for (int p = nodes[(std::size_t)i].parent; p >= 0; p = nodes[(std::size_t)p].parent) ++d; return d; }
    std::string Path(int i) const {
        std::string s;
        for (int p = nodes[(std::size_t)i].parent; p >= 0; p = nodes[(std::size_t)p].parent) s = nodes[(std::size_t)p].name + (s.empty() ? "" : " / ") + s;
        return s;
    }
    void Fill(int node, int objectIndex, P::HierRow& r) const {
        const Node& n = nodes[(std::size_t)node];
        r.id = Id(node); r.name = n.name; r.depth = Depth(node); r.childCount = (int)n.kids.size();
        r.open = r.childCount > 0 && IsOpen(node); r.objectIndex = objectIndex;
        r.selected = selected.count(node) > 0; r.active = n.active; r.locked = n.locked;
        r.dim = !n.active || (isolate >= 0 && !Under(node, isolate));
        r.prefabBlue = !r.dim && n.prefab >= P::HierPrefabRoot;
        r.icon = n.icon; r.prefab = n.prefab; r.prefabNested = n.nested; r.prefabChanged = n.changed;
        r.badges = n.badges; r.diag = n.diag;
    }

    /// The model the editor would hand the panel this frame (`filter`: the search text; "" = the tree).
    P::HierarchyModel Model(const char* filter = "") {
        P::HierarchyModel m;
        m.sceneName = name; m.dirty = dirty; m.objectCount = (int)nodes.size(); m.sortAZ = sortAZ;
        m.selectedCount = (int)selected.size();
        if (filter && filter[0]) {
            m.searching = true;
            hits.clear();
            for (int i = 0; i < (int)nodes.size(); ++i) if (nodes[(std::size_t)i].name.find(filter) != std::string::npos) hits.push_back(i);
            m.rowCount = (int)hits.size();
            m.fillRow = [this](int i, P::HierRow& r) { ++fills; Fill(hits[(std::size_t)i], i, r); r.path = Path(hits[(std::size_t)i]); };
        } else {
            Flatten();
            m.rowCount = (int)rows.size();
            m.fillRow = [this](int i, P::HierRow& r) { ++fills; Fill(rows[(std::size_t)i], i, r); };
            for (int i = 0; i < (int)rows.size(); ++i) if (rows[(std::size_t)i] == active) m.activeRow = i;
        }
        m.focused = true;
        m.buildMenu = [this](int kind, std::uint64_t row, std::vector<P::PanelMenuEntry>& out) {
            auto item = [](const char* id, const char* label, const char* sc = "") { P::PanelMenuEntry e; e.id = id; e.label = label; e.shortcut = sc; return e; };
            P::PanelMenuEntry sep; sep.kind = P::PanelMenuSeparator;
            if (kind == P::HierMenuRow) {
                out.push_back(item("h.copy", "Copy", "Ctrl+C"));
                out.push_back(item("h.rename", "Rename", "F2"));
                out.push_back(item("h.delete", selected.size() > 1 ? "Delete 2 Objects" : "Delete", "Del"));
                out.push_back(sep);
                P::PanelMenuEntry sub; sub.kind = P::PanelMenuSub; sub.id = "h.order"; sub.label = "Order";
                sub.children.push_back(item("h.moveup", "Move Up", "Ctrl+Up"));
                sub.children.push_back(item("h.movedown", "Move Down", "Ctrl+Down"));
                out.push_back(sub);
                P::PanelMenuEntry off = item("h.unparent", "Unparent (to top level)");
                if (nodes[(std::size_t)NodeOf(row)].parent < 0) { off.enabled = false; off.reason = "It is already at the top level."; }
                out.push_back(off);
            } else {
                out.push_back(item("h.createempty", "Create Empty"));
                out.push_back(sep);
                P::PanelMenuEntry sub; sub.kind = P::PanelMenuSub; sub.id = "create.3d"; sub.label = "3D Object";
                sub.children.push_back(item("create.3d/0:Cube", "Cube"));
                sub.children.push_back(item("create.3d/1:Sphere", "Sphere"));
                out.push_back(sub);
                if (kind == P::HierMenuEmpty) {
                    out.push_back(sep);
                    P::PanelMenuEntry t; t.kind = P::PanelMenuToggle; t.id = "h.sortaz"; t.label = "Sort A-Z"; t.checked = sortAZ;
                    out.push_back(t);
                }
            }
        };
        return m;
    }

    /// What the editor does with an intent (the parts the tests look at).
    void Apply(const P::HierarchyIntent& in) {
        if (in.select) { selected = {NodeOf(in.select)}; active = NodeOf(in.select); }
        if (in.toggle) { const int n = NodeOf(in.toggle); if (selected.count(n)) selected.erase(n); else { selected.insert(n); active = n; } }
        if (in.rangeTo) {
            const std::vector<int> r = P::HierRange(rows, active, NodeOf(in.rangeTo));
            if (r.empty()) { selected = {NodeOf(in.rangeTo)}; active = NodeOf(in.rangeTo); }
            else for (int n : r) selected.insert(n);
        }
        if (in.selectAll) { selected.clear(); for (int n : rows) selected.insert(n); }
        if (in.openId) nodes[(std::size_t)NodeOf(in.openId)].open = in.open;
        if (in.toggleActive) { auto& n = nodes[(std::size_t)NodeOf(in.toggleActive)]; n.active = !n.active; log.push_back("active " + n.name); }
        if (in.toggleLock) { auto& n = nodes[(std::size_t)NodeOf(in.toggleLock)]; n.locked = !n.locked; log.push_back("lock " + n.name); }
        if (in.renameId) { nodes[(std::size_t)NodeOf(in.renameId)].name = in.renameTo; log.push_back("rename " + in.renameTo); }
        if (in.dragged && in.dropWhere >= 0) {
            const int d = NodeOf(in.dragged);
            const int t = in.dropTarget ? NodeOf(in.dropTarget) : -1;
            static const char* kWhere[] = {"before", "inside", "after", "root"};
            log.push_back(std::string("drop ") + nodes[(std::size_t)d].name + " " + kWhere[in.dropWhere] + (t >= 0 ? " " + nodes[(std::size_t)t].name : std::string()));
            if (t < 0 || !Under(t, d)) Move(d, in.dropWhere == P::HierDropRoot ? -1 : t, in.dropWhere);
        }
        if (!in.dropAsset.empty()) log.push_back("asset " + in.dropAsset + (in.dropTarget ? " on " + nodes[(std::size_t)NodeOf(in.dropTarget)].name : std::string(" at top")));
        if (!in.menuId.empty()) {
            log.push_back("menu " + in.menuId);
            if (in.menuId == "h.sortaz") sortAZ = !sortAZ;
        }
        if (in.frame) log.push_back("frame " + nodes[(std::size_t)NodeOf(in.frame)].name);
    }
    void Move(int d, int target, int where) {
        auto detach = [&](int n) {
            const int p = nodes[(std::size_t)n].parent;
            if (p >= 0) { auto& k = nodes[(std::size_t)p].kids; k.erase(std::remove(k.begin(), k.end(), n), k.end()); }
        };
        if (where == P::HierDropRoot || target < 0) { detach(d); nodes[(std::size_t)d].parent = -1; return; }
        detach(d);
        if (where == P::HierDropInside) { nodes[(std::size_t)d].parent = target; nodes[(std::size_t)target].kids.push_back(d); return; }
        const int p = nodes[(std::size_t)target].parent;
        nodes[(std::size_t)d].parent = p;
        if (p >= 0) {
            auto& k = nodes[(std::size_t)p].kids;
            auto it = std::find(k.begin(), k.end(), target);
            k.insert(where == P::HierDropAfter ? it + 1 : it, d);
        }   // (at the top level the fixture keeps the creation order)
    }
};

/// A small village: a camera, a light, a group of houses (open), a player prefab (blue, with a script and a rigidbody), an object that
/// is off, a locked one, a variant, a nested prefab with changes, a sprite with a problem, UI pieces, particles.
inline Scene Village() {
    Scene s;
    const int cam = s.Add("Main Camera", -1, P::HierIconCamera);
    s.nodes[(std::size_t)cam].badges = P::HierBadgeScript;
    s.Add("Sun", -1, P::HierIconLight);
    s.nodes.back().badges = P::HierBadgeLight;
    const int houses = s.Add("Houses", -1, P::HierIconFolder);
    for (int i = 0; i < 3; ++i) {
        const int h = s.Add("House " + std::to_string(i + 1), houses, P::HierIconMesh);
        s.nodes[(std::size_t)h].badges = P::HierBadgeCollider;
        if (i == 1) s.nodes[(std::size_t)h].active = false;
        if (i == 2) { s.nodes[(std::size_t)h].locked = true; s.Add("Chimney", h, P::HierIconMesh); }
    }
    const int player = s.Add("Player", -1, P::HierIconMesh);
    s.nodes[(std::size_t)player].prefab = P::HierPrefabRoot;
    s.nodes[(std::size_t)player].badges = P::HierBadgeScript | P::HierBadgeRigidbody | P::HierBadgeCollider | P::HierBadgeAnimator;
    s.nodes[(std::size_t)player].changed = true;
    const int hat = s.Add("Hat", player, P::HierIconMesh);
    s.nodes[(std::size_t)hat].prefab = P::HierPrefabPart;
    const int cart = s.Add("Cart (variant)", -1, P::HierIconMesh);
    s.nodes[(std::size_t)cart].prefab = P::HierPrefabVariant;
    s.nodes[(std::size_t)cart].nested = true;
    s.Add("Campfire Smoke", -1, P::HierIconParticles);
    s.nodes.back().badges = P::HierBadgeParticles;
    const int sign = s.Add("Sign With A Rather Long Name That Will Not Fit In The Panel", -1, P::HierIconSprite);
    s.nodes[(std::size_t)sign].diag = 2;
    const int canvas = s.Add("Canvas", -1, P::HierIconCanvas);
    s.Add("Start Button", canvas, P::HierIconButton);
    s.Add("Volume", canvas, P::HierIconBar);
    s.Add("Music", -1, P::HierIconAudio);
    s.selected = {player}; s.active = player;
    s.Flatten();
    return s;
}

/// A big scene: `count` objects in groups of 250 under "Group 1", "Group 2", ... (all open: every object is a row).
inline Scene Big(int count) {
    Scene s;
    s.name = "Big";
    s.nodes.reserve((std::size_t)count + (std::size_t)count / 250 + 2);
    int made = 0, g = 0;
    static const int kIcons[] = {P::HierIconMesh, P::HierIconLight, P::HierIconSprite, P::HierIconAudio, P::HierIconParticles};
    while (made < count) {
        const int grp = s.Add("Group " + std::to_string(++g), -1, P::HierIconFolder);
        ++made;
        for (int k = 0; k < 249 && made < count; ++k, ++made) {
            const int n = s.Add("Rock " + std::to_string(made), grp, kIcons[made % 5]);
            s.nodes[(std::size_t)n].badges = (made % 3 == 0) ? P::HierBadgeCollider : 0u;
            s.nodes[(std::size_t)n].active = (made % 17) != 0;
        }
        s.nodes[(std::size_t)grp].open = 1;
    }
    s.selected = {1}; s.active = 1;
    s.Flatten();
    return s;
}

} // namespace hierfix
