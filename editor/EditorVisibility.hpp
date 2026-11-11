// Editor-only visibility and selection sets (ImGui-free, so it runs headless).
//
//   Hide   GameObject::editorHidden. A hidden object and everything below it is not drawn, not picked and
//          has no gizmo in the Scene view. It never touches `active`, so the Game view, Play and a built
//          game are unaffected. Saved in the scene file as `hidden 1`.
//   Sets   Scene::selectionSets: named groups of objects by GUID, saved as `selset` records.
//
// How Hide combines with Isolate and Lock (the order that decides what you see and can click):
//   1. HIDE WINS. A hidden object is never shown, even inside an isolated subtree; isolating a hidden
//      object shows nothing but its lights. "Show all" (Alt+H) clears every hide flag.
//   2. ISOLATE next: outside the isolated subtree nothing is shown or picked (Lights stay on, as before).
//   3. LOCK only stops Scene-view clicks (and the box pick). It does not hide anything, and Hide does not
//      change Lock: both flags are kept. Hierarchy selection ignores Hide, Isolate and Lock alike.
// Every action below takes one undo step through EditorState::PushUndo() before it changes anything.
#pragma once
#include "EditorState.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Light.hpp"
#include <string>
#include <utility>
#include <vector>

namespace okay::editor::vis {

/// Hidden by its own flag or by any ancestor's.
inline bool IsHidden(const GameObject* go) {
    for (const Transform* t = go ? go->transform : nullptr; t; t = t->Parent())
        if (t->gameObject && t->gameObject->editorHidden) return true;
    return false;
}

/// Drawn in the Scene view: not hidden, and inside the isolated subtree when isolating (Lights always stay).
inline bool SceneViewVisible(const GameObject* go, const GameObject* isolate) {
    if (!go || IsHidden(go)) return false;
    if (isolate && !go->IsSelfOrDescendantOf(isolate) && !go->GetComponent<Light>()) return false;
    return true;
}

/// Clickable in the Scene view: visible and not locked.
inline bool ScenePickable(const GameObject* go, const GameObject* isolate) {
    return SceneViewVisible(go, isolate) && !go->editorLocked;
}

/// Objects carrying their own hide flag (what "3 hidden" counts; children of a hidden object are not added).
int HiddenCount(const Scene& scene);

/// While alive, every object the Scene view must not show has `active` switched off, so the draw and
/// pick code (which already skips inactive objects) skips them. Restores exactly what it changed, also for
/// objects destroyed meanwhile. Use around the Scene view only, never the Game view.
class SceneViewHideScope {
public:
    SceneViewHideScope(Scene& scene, const GameObject* isolate);
    ~SceneViewHideScope();
    SceneViewHideScope(const SceneViewHideScope&) = delete;
    SceneViewHideScope& operator=(const SceneViewHideScope&) = delete;
    int Count() const { return (int)m_off.size(); }
private:
    Scene& m_scene;
    std::vector<std::pair<GameObject*, std::uint64_t>> m_off;
};

// ---- Hide ------------------------------------------------------------------------------------------------
/// Set or clear the hide flag on these objects. One undo step; returns how many changed. Hiding also drops
/// them (and what they hide) from the selection.
int SetHidden(EditorState& ed, const std::vector<GameObject*>& objs, bool hidden);
int HideSelection(EditorState& ed);
/// Clears every hide flag in the scene. Returns how many were cleared.
int UnhideAll(EditorState& ed);

// ---- Selection sets --------------------------------------------------------------------------------------
struct SetResolve {
    std::vector<GameObject*> live;   ///< members that still exist
    int dead = 0;                    ///< GUIDs of the set that no object has any more
};
/// Look the members up without changing anything.
SetResolve ResolveSet(Scene& scene, int index);
/// Make a set from the current selection. An empty name gets "Set N". A name already used gets " 2", " 3".
/// Returns the new index, or -1 when nothing is selected. Names have line breaks stripped.
int CreateSet(EditorState& ed, std::string name);
bool RenameSet(EditorState& ed, int index, const std::string& name);   ///< false: bad index, empty or taken name
bool DeleteSet(EditorState& ed, int index);
/// Select the members (replacing the selection, or adding to it). Dead GUIDs are dropped from the set;
/// the count is returned in *dropped. Returns how many objects were selected.
int SelectSet(EditorState& ed, int index, bool additive = false, int* dropped = nullptr);
/// Add the selection to the set / take it out. Returns how many GUIDs were added / removed.
int AddSelectionToSet(EditorState& ed, int index);
int RemoveSelectionFromSet(EditorState& ed, int index);
/// Hide / show / lock / unlock every member. Dead GUIDs are dropped (count in *dropped). Returns objects changed.
int SetSetHidden(EditorState& ed, int index, bool hidden, int* dropped = nullptr);
int SetSetLocked(EditorState& ed, int index, bool locked, int* dropped = nullptr);

/// Lock or unlock an object and everything below it (the Hierarchy lock icon uses the same).
void SetLockedRecursive(GameObject* go, bool on);

}  // namespace okay::editor::vis
