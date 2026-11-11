#include "EditorVisibility.hpp"
#include <algorithm>
#include <unordered_set>

namespace okay::editor::vis {

namespace {

std::string CleanName(std::string s) {
    for (char& c : s) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    const auto b = s.find_first_not_of(' ');
    if (b == std::string::npos) return {};
    s = s.substr(b, s.find_last_not_of(' ') - b + 1);
    return s;
}

bool NameTaken(const Scene& scene, const std::string& name, int except) {
    for (int i = 0; i < (int)scene.selectionSets.size(); ++i)
        if (i != except && scene.selectionSets[(std::size_t)i].name == name) return true;
    return false;
}

bool ValidIndex(const Scene& scene, int i) { return i >= 0 && i < (int)scene.selectionSets.size(); }

// Drop GUIDs no object has; returns how many.
int PruneDead(Scene& scene, int index) {
    auto& set = scene.selectionSets[(std::size_t)index];
    std::unordered_set<std::string> alive;
    for (const auto& up : scene.Objects()) if (!up->transient) alive.insert(up->Guid());
    const std::size_t before = set.guids.size();
    set.guids.erase(std::remove_if(set.guids.begin(), set.guids.end(),
                                   [&](const std::string& g) { return !alive.count(g); }), set.guids.end());
    return (int)(before - set.guids.size());
}

std::vector<GameObject*> Selected(EditorState& ed) {
    std::vector<GameObject*> v;
    for (GameObject* g : ed.MultiSelection()) if (g) v.push_back(g);
    if (ed.selected() && std::find(v.begin(), v.end(), ed.selected()) == v.end()) v.push_back(ed.selected());
    return v;
}

}  // namespace

int HiddenCount(const Scene& scene) {
    int n = 0;
    for (const auto& up : scene.Objects()) if (!up->transient && up->editorHidden) ++n;
    return n;
}

SceneViewHideScope::SceneViewHideScope(Scene& scene, const GameObject* isolate) : m_scene(scene) {
    for (const auto& up : scene.Objects()) {
        GameObject* g = up.get();
        if (!g || !g->active || SceneViewVisible(g, isolate)) continue;
        m_off.push_back({g, g->InstanceId()});
        g->active = false;
    }
}

SceneViewHideScope::~SceneViewHideScope() {
    if (m_off.empty()) return;
    // An undo or a delete while the scope was open may have freed objects: touch only the ones still alive.
    std::unordered_set<const GameObject*> alive;
    for (const auto& up : m_scene.Objects()) alive.insert(up.get());
    for (auto& [g, id] : m_off)
        if (alive.count(g) && g->InstanceId() == id) g->active = true;
}

int SetHidden(EditorState& ed, const std::vector<GameObject*>& objs, bool hidden) {
    std::vector<GameObject*> todo;
    for (GameObject* g : objs)
        if (g && g->editorHidden != hidden && std::find(todo.begin(), todo.end(), g) == todo.end()) todo.push_back(g);
    if (todo.empty()) return 0;
    ed.PushUndo();
    for (GameObject* g : todo) {
        g->editorHidden = hidden;
        if (hidden) {   // what you hid should not stay selected: it has no gizmo and cannot be seen
            for (GameObject* s : std::vector<GameObject*>(ed.MultiSelection()))
                if (s && s->IsSelfOrDescendantOf(g)) ed.Deselect(s);
        }
    }
    ed.dirty = true;
    return (int)todo.size();
}

int HideSelection(EditorState& ed) { return SetHidden(ed, Selected(ed), true); }

int UnhideAll(EditorState& ed) {
    std::vector<GameObject*> v;
    for (const auto& up : ed.scene().Objects()) if (up->editorHidden) v.push_back(up.get());
    return SetHidden(ed, v, false);
}

void SetLockedRecursive(GameObject* go, bool on) {
    if (!go) return;
    go->editorLocked = on;
    if (go->transform)
        for (Transform* c : go->transform->Children())
            if (c) SetLockedRecursive(c->gameObject, on);
}

// ---- Sets ------------------------------------------------------------------------------------------------

SetResolve ResolveSet(Scene& scene, int index) {
    SetResolve r;
    if (!ValidIndex(scene, index)) return r;
    for (const std::string& g : scene.selectionSets[(std::size_t)index].guids) {
        GameObject* found = nullptr;
        for (const auto& up : scene.Objects()) if (!up->transient && up->Guid() == g) { found = up.get(); break; }
        if (found) r.live.push_back(found); else ++r.dead;
    }
    return r;
}

int CreateSet(EditorState& ed, std::string name) {
    std::vector<GameObject*> sel = Selected(ed);
    if (sel.empty()) return -1;
    Scene& sc = ed.scene();
    name = CleanName(name);
    if (name.empty()) {
        for (int n = (int)sc.selectionSets.size() + 1;; ++n) {
            name = "Set " + std::to_string(n);
            if (!NameTaken(sc, name, -1)) break;
        }
    } else if (NameTaken(sc, name, -1)) {
        const std::string base = name;
        for (int n = 2;; ++n) { name = base + " " + std::to_string(n); if (!NameTaken(sc, name, -1)) break; }
    }
    ed.PushUndo();
    Scene::SelectionSet set; set.name = name;
    for (GameObject* g : sel) if (!g->transient) set.guids.push_back(g->Guid());
    sc.selectionSets.push_back(std::move(set));
    ed.dirty = true;
    return (int)sc.selectionSets.size() - 1;
}

bool RenameSet(EditorState& ed, int index, const std::string& raw) {
    Scene& sc = ed.scene();
    if (!ValidIndex(sc, index)) return false;
    const std::string name = CleanName(raw);
    if (name.empty() || NameTaken(sc, name, index)) return false;
    if (sc.selectionSets[(std::size_t)index].name == name) return true;
    ed.PushUndo();
    sc.selectionSets[(std::size_t)index].name = name;
    ed.dirty = true;
    return true;
}

bool DeleteSet(EditorState& ed, int index) {
    Scene& sc = ed.scene();
    if (!ValidIndex(sc, index)) return false;
    ed.PushUndo();
    sc.selectionSets.erase(sc.selectionSets.begin() + index);
    ed.dirty = true;
    return true;
}

int SelectSet(EditorState& ed, int index, bool additive, int* dropped) {
    Scene& sc = ed.scene();
    if (dropped) *dropped = 0;
    if (!ValidIndex(sc, index)) return 0;
    SetResolve r = ResolveSet(sc, index);
    if (r.dead > 0) {   // forget the dead ones, as one undo step of their own
        ed.PushUndo();
        PruneDead(sc, index);
        ed.dirty = true;
        if (dropped) *dropped = r.dead;
    }
    if (!additive) ed.Select(nullptr);
    int n = 0;
    for (GameObject* g : r.live) {
        if (ed.IsSelected(g)) continue;
        if (ed.MultiSelection().empty()) ed.Select(g); else ed.ToggleSelect(g);
        ++n;
    }
    return n;
}

int AddSelectionToSet(EditorState& ed, int index) {
    Scene& sc = ed.scene();
    if (!ValidIndex(sc, index)) return 0;
    std::vector<std::string> add;
    auto& set = sc.selectionSets[(std::size_t)index];
    for (GameObject* g : Selected(ed))
        if (!g->transient && std::find(set.guids.begin(), set.guids.end(), g->Guid()) == set.guids.end()) add.push_back(g->Guid());
    if (add.empty()) return 0;
    ed.PushUndo();
    for (auto& g : add) set.guids.push_back(g);
    ed.dirty = true;
    return (int)add.size();
}

int RemoveSelectionFromSet(EditorState& ed, int index) {
    Scene& sc = ed.scene();
    if (!ValidIndex(sc, index)) return 0;
    auto& set = sc.selectionSets[(std::size_t)index];
    std::vector<std::string> drop;
    for (GameObject* g : Selected(ed))
        if (std::find(set.guids.begin(), set.guids.end(), g->Guid()) != set.guids.end()) drop.push_back(g->Guid());
    if (drop.empty()) return 0;
    ed.PushUndo();
    set.guids.erase(std::remove_if(set.guids.begin(), set.guids.end(), [&](const std::string& g) {
        return std::find(drop.begin(), drop.end(), g) != drop.end(); }), set.guids.end());
    ed.dirty = true;
    return (int)drop.size();
}

int SetSetHidden(EditorState& ed, int index, bool hidden, int* dropped) {
    Scene& sc = ed.scene();
    if (dropped) *dropped = 0;
    if (!ValidIndex(sc, index)) return 0;
    SetResolve r = ResolveSet(sc, index);
    int changed = 0;
    std::vector<GameObject*> todo;
    for (GameObject* g : r.live) if (g->editorHidden != hidden) todo.push_back(g);
    if (r.dead > 0 || !todo.empty()) {
        // One undo step for the prune and the flags together: SetHidden pushes its own, so push first and
        // suppress nothing: the prune below happens inside that step only when there are objects to change.
        if (todo.empty()) { ed.PushUndo(); }
        changed = SetHidden(ed, todo, hidden);
        if (r.dead > 0) { PruneDead(sc, index); ed.dirty = true; if (dropped) *dropped = r.dead; }
    }
    return changed;
}

int SetSetLocked(EditorState& ed, int index, bool locked, int* dropped) {
    Scene& sc = ed.scene();
    if (dropped) *dropped = 0;
    if (!ValidIndex(sc, index)) return 0;
    SetResolve r = ResolveSet(sc, index);
    std::vector<GameObject*> todo;
    for (GameObject* g : r.live) if (g->editorLocked != locked) todo.push_back(g);
    if (todo.empty() && r.dead == 0) return 0;
    ed.PushUndo();
    for (GameObject* g : todo) SetLockedRecursive(g, locked);
    if (r.dead > 0) { PruneDead(sc, index); if (dropped) *dropped = r.dead; }
    ed.dirty = true;
    return (int)todo.size();
}

}  // namespace okay::editor::vis
