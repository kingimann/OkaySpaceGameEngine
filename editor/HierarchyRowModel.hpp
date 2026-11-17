#pragma once
// The Hierarchy panel's row layout as data (no ImGui), for Finding 99.
//
// Before this, the panel walked the whole scene tree every frame and drew every row, even the ones
// scrolled far out of view: about 5 ms a frame at 3,000 objects. Now:
//   * Model keeps which objects are open (folded out) and a flat list of the rows the tree shows, in
//     the order a top-to-bottom walk visits them. The list is rebuilt only when the scene's objects or
//     parents change, when an object is opened or closed, or when the A-Z sort is turned on or off
//     (with the sort on, a renamed object also counts as a change).
//   * VisibleRange() says which rows of that list fall inside the scrolled view, so the panel draws only
//     those (plus one row either side) and leaves blank space for the rest.
//   * RevealScroll() gives the scroll position that brings a row into view (selecting an object in the
//     Scene view scrolls the Hierarchy to it).
//
// The rules match the old recursive draw: top-level objects start open, everything deeper starts closed,
// children are listed in their sibling order (or by name with Sort A-Z), and in a combined scene the
// top-level objects are grouped by the scene they came from, each group under a header row.
//
// Written from the panel's own behaviour (editor/main.cpp DrawHierarchy); no outside code.
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "HierarchyPanelModel.hpp"   // the clip math, shared with the panel's two versions

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace oked::hierrows {

/// One line of the Hierarchy: an object, or (header = true) the title of a combined scene's group.
struct Row {
    okay::GameObject* go = nullptr;   ///< null on a header row
    int depth = 0;                    ///< 0 for top-level objects
    bool hasChildren = false;
    bool open = false;                ///< folded out (only meaningful with children)
    bool header = false;
    std::string section;              ///< header rows: the source scene ("" = the main scene)
    int objectIndex = -1;             ///< position among the object rows (zebra striping, keyboard order)
};

/// The rows from `first` up to (not including) `last` are inside the view. The clip math is shared with the panel's two versions
/// (shared/HierarchyPanelModel.hpp), so the editor and the OkayUI port agree on which rows show.
using ClipRange = okaypanels::HierClipRange;

/// Which of `rowCount` rows of height `rowHeight` show when the list is scrolled down by `scrollY` in a view
/// `viewHeight` tall. `overscan` extra rows are kept on each side (so a row half in view is always drawn).
inline ClipRange VisibleRange(int rowCount, float rowHeight, float scrollY, float viewHeight, int overscan = 1) {
    return okaypanels::HierVisibleRange(rowCount, rowHeight, scrollY, viewHeight, overscan);
}

/// The scroll position that shows row `row`: unchanged when the row is already fully in view, otherwise the
/// row is placed `anchor` (0 = top, 1 = bottom) of the way down the view. Never below 0.
inline float RevealScroll(int row, float rowHeight, float scrollY, float viewHeight, float anchor = 0.4f) {
    return okaypanels::HierRevealScroll(row, rowHeight, scrollY, viewHeight, anchor);
}

class Model {
public:
    /// Is `go` folded out? Explicitly set state wins; otherwise top-level objects are open.
    bool IsOpen(const okay::GameObject* go) const {
        if (!go) return false;
        auto it = m_open.find(go);
        if (it != m_open.end()) return it->second;
        return !(go->transform && go->transform->Parent());
    }
    void SetOpen(const okay::GameObject* go, bool open) {
        if (!go || IsOpen(go) == open) { if (go) m_open[go] = open; return; }
        m_open[go] = open;
        ++m_stateVersion;
    }
    /// Expand all / Collapse all: every object with children.
    void SetAllOpen(okay::Scene& scene, bool open) {
        for (const auto& up : scene.Objects())
            if (up && up->transform && up->transform->ChildCount() > 0) SetOpen(up.get(), open);
    }
    /// Open or close `root` and everything under it that has children.
    void SetSubtreeOpen(okay::GameObject* root, bool open) {
        if (!root || !root->transform) return;
        std::vector<okay::Transform*> stack{root->transform};
        while (!stack.empty()) {
            okay::Transform* t = stack.back(); stack.pop_back();
            if (!t || !t->gameObject) continue;
            if (t->ChildCount() > 0) SetOpen(t->gameObject, open);
            for (okay::Transform* c : t->Children()) stack.push_back(c);
        }
    }
    /// Open every parent of `go`, so its row is listed.
    void OpenAncestors(const okay::GameObject* go) {
        if (!go || !go->transform) return;
        for (okay::Transform* p = go->transform->Parent(); p; p = p->Parent())
            if (p->gameObject) SetOpen(p->gameObject, true);
    }
    /// Force the next Refresh to rebuild (for example after a rename with Sort A-Z on).
    void Invalidate() { ++m_stateVersion; }

    /// Rebuild the rows if the scene, the open state or the sort setting changed since the last call.
    /// Returns true when it rebuilt.
    bool Refresh(okay::Scene& scene, bool sortAZ) {
        const std::uint64_t names = sortAZ ? NameKey(scene) : 0;
        if (m_built && m_sceneUid == scene.Uid() && m_structure == scene.StructureVersion() &&
            m_builtState == m_stateVersion && m_sort == sortAZ && m_names == names && m_count == scene.Objects().size())
            return false;
        m_built = true;
        m_sceneUid = scene.Uid(); m_structure = scene.StructureVersion(); m_builtState = m_stateVersion;
        m_sort = sortAZ; m_names = names; m_count = scene.Objects().size();
        Build(scene);
        ++m_rebuilds;
        return true;
    }

    const std::vector<Row>& Rows() const { return m_rows; }
    /// The object rows only, top to bottom (Shift+click ranges, arrow keys, Ctrl+A).
    const std::vector<okay::GameObject*>& ObjectRows() const { return m_objects; }
    /// Index in Rows() of `go`'s row, or -1 when it is not listed (a closed parent hides it).
    int RowOf(const okay::GameObject* go) const {
        auto it = m_rowOf.find(go);
        return it == m_rowOf.end() ? -1 : it->second;
    }
    std::uint64_t Rebuilds() const { return m_rebuilds; }

private:
    // A cheap fingerprint of every object's name, so the A-Z order follows renames.
    static std::uint64_t NameKey(okay::Scene& scene) {
        std::uint64_t h = 1469598103934665603ull;   // FNV-1a offset basis (a public, general-purpose hash)
        for (const auto& up : scene.Objects()) {
            if (!up) continue;
            for (unsigned char c : up->name) { h ^= c; h *= 1099511628211ull; }
            h ^= 0xFFu; h *= 1099511628211ull;   // name separator
        }
        return h;
    }

    void Build(okay::Scene& scene) {
        m_rows.clear(); m_objects.clear(); m_rowOf.clear();
        // Forget the open state of objects that are gone (a new object could reuse the address).
        if (!m_open.empty()) {
            std::unordered_set<const okay::GameObject*> alive;
            alive.reserve(scene.Objects().size());
            for (const auto& up : scene.Objects()) alive.insert(up.get());
            for (auto it = m_open.begin(); it != m_open.end();) it = alive.count(it->first) ? std::next(it) : m_open.erase(it);
        }
        std::vector<okay::GameObject*> roots;
        for (const auto& up : scene.Objects())
            if (up && up->transform && up->transform->Parent() == nullptr) roots.push_back(up.get());
        const auto byName = [](okay::GameObject* a, okay::GameObject* b) { return a->name < b->name; };
        if (m_sort) std::sort(roots.begin(), roots.end(), byName);
        bool anyMerged = false;
        for (okay::GameObject* r : roots) if (!r->sourceScene.empty()) { anyMerged = true; break; }
        if (anyMerged)   // the main scene ("") first, each source scene's objects together
            std::stable_sort(roots.begin(), roots.end(),
                             [](okay::GameObject* a, okay::GameObject* b) { return a->sourceScene < b->sourceScene; });
        std::string section = "\x01";   // differs from every real value, "" included
        std::vector<std::pair<okay::GameObject*, int>> stack;   // (object, depth), walked depth first
        for (okay::GameObject* root : roots) {
            if (anyMerged && root->sourceScene != section) {
                section = root->sourceScene;
                Row h; h.header = true; h.section = section;
                m_rows.push_back(h);
            }
            stack.clear();
            stack.push_back({root, 0});
            while (!stack.empty()) {
                auto [go, depth] = stack.back(); stack.pop_back();
                Row r;
                r.go = go; r.depth = depth;
                r.hasChildren = go->transform && go->transform->ChildCount() > 0;
                r.open = r.hasChildren && IsOpen(go);
                r.objectIndex = (int)m_objects.size();
                m_rowOf[go] = (int)m_rows.size();
                m_rows.push_back(r);
                m_objects.push_back(go);
                if (!r.open) continue;
                std::vector<okay::GameObject*> kids;
                for (okay::Transform* c : go->transform->Children()) if (c && c->gameObject) kids.push_back(c->gameObject);
                if (m_sort) std::sort(kids.begin(), kids.end(), byName);
                for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back({*it, depth + 1});   // first child on top
            }
        }
    }

    std::unordered_map<const okay::GameObject*, bool> m_open;
    std::uint64_t m_stateVersion = 0;
    bool m_built = false;
    std::uint64_t m_sceneUid = 0, m_structure = 0, m_builtState = 0, m_names = 0, m_rebuilds = 0;
    std::size_t m_count = 0;
    bool m_sort = false;
    std::vector<Row> m_rows;
    std::vector<okay::GameObject*> m_objects;
    std::unordered_map<const okay::GameObject*, int> m_rowOf;
};

} // namespace oked::hierrows
