#pragma once
// Cut / Copy / Paste of scene objects (Ctrl+X / Ctrl+C / Ctrl+V in the editor). ImGui-free, so
// tests/test_edit_clipboard.cpp drives exactly what the shortcuts run.
//
// The clipboard is one scene text (SceneSerializer::SerializeObjects) that holds EVERY selected
// object with everything below it. A selected object whose parent (or grandparent...) is also
// selected is not put on it a second time: it comes along with that parent. Paste adds every
// object back, nudges each pasted top-level object half a unit so it is visible next to the
// original, and selects them all. Cut is one undo step (the delete), Paste is one undo step.
//
// The text is the same format a single copied object always had (one object gives exactly the
// old text), so every other place that reads the clipboard with InstantiateFromText still works:
// it adds all the objects too.
//
// Written for this engine from its own scene text format (engine/src/Scene/SceneSerializer.cpp); no outside code.
#include "EditorState.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace oked {

using okay::GameObject;
using okay::editor::EditorState;

/// The objects a Cut / Copy puts on the clipboard: every selected object that has no selected
/// object above it, in selection order, without nulls or repeats. `primary` is used when
/// `selection` is empty (the editor's selection always holds the primary object otherwise).
inline std::vector<GameObject*> ClipboardRoots(const std::vector<GameObject*>& selection, GameObject* primary) {
    std::vector<GameObject*> picked = selection;
    if (picked.empty() && primary) picked.push_back(primary);
    std::vector<GameObject*> roots;
    for (GameObject* g : picked) {
        if (!g || g->IsDestroyed() || std::find(roots.begin(), roots.end(), g) != roots.end()) continue;
        bool aboveIsPicked = false;
        for (okay::Transform* t = g->transform ? g->transform->Parent() : nullptr; t && !aboveIsPicked; t = t->Parent())
            if (t->gameObject && std::find(picked.begin(), picked.end(), t->gameObject) != picked.end()) aboveIsPicked = true;
        if (!aboveIsPicked) roots.push_back(g);
    }
    return roots;
}

/// The clipboard text for `roots` (each with its children). Empty when `roots` is empty.
inline std::string ClipboardText(const std::vector<GameObject*>& roots) {
    if (roots.empty()) return std::string();
    std::vector<const GameObject*> c(roots.begin(), roots.end());
    return okay::SceneSerializer::SerializeObjects(c);
}

/// A short name for the console: the object's name for one, "3 objects" for several.
inline std::string ClipboardLabel(const std::vector<GameObject*>& roots) {
    if (roots.empty()) return "nothing";
    if (roots.size() == 1) return roots[0] ? roots[0]->name : std::string("nothing");
    return std::to_string(roots.size()) + " objects";
}

/// Copy the selection. Returns how many top-level objects went on the clipboard (0: nothing is
/// selected and `clipboard` is left as it was). The scene and the selection do not change.
inline std::size_t CopySelection(EditorState& ed, std::string& clipboard) {
    const std::vector<GameObject*> roots = ClipboardRoots(ed.MultiSelection(), ed.selected());
    if (roots.empty()) return 0;
    clipboard = ClipboardText(roots);
    return roots.size();
}

/// Cut the selection: copy it, then delete every selected object (EditorState::DeleteSelected, one
/// undo step). Returns how many top-level objects went on the clipboard (0: nothing selected,
/// nothing deleted, `clipboard` left as it was).
inline std::size_t CutSelection(EditorState& ed, std::string& clipboard) {
    const std::size_t n = CopySelection(ed, clipboard);
    if (n > 0) ed.DeleteSelected();
    return n;
}

/// Paste: add every object on the clipboard to the scene in one undo step, move each pasted
/// top-level object by `nudge` and select them all (the last one is the primary selection, as
/// Duplicate does). Returns the pasted top-level objects in clipboard order; empty (and no undo
/// step, no change) when the clipboard is empty or cannot be read.
inline std::vector<GameObject*> PasteClipboard(EditorState& ed, const std::string& clipboard,
                                               const okay::Vec3& nudge = okay::Vec3{0.5f, 0.5f, 0.0f}) {
    std::vector<GameObject*> roots;
    if (clipboard.empty()) return roots;
    okay::Scene& scene = ed.scene();
    ed.PushUndo("Paste");
    const std::size_t base = scene.Objects().size();   // everything from here on is new
    if (!okay::SceneSerializer::InstantiateFromText(scene, clipboard)) { ed.DiscardLastUndo(); return roots; }   // bad text: nothing added
    const auto& objs = scene.Objects();
    for (std::size_t i = base; i < objs.size(); ++i) {
        GameObject* go = objs[i].get();
        if (go && go->transform && go->transform->Parent() == nullptr) roots.push_back(go);
    }
    if (roots.empty()) { ed.DiscardLastUndo(); return roots; }
    for (GameObject* go : roots) go->transform->localPosition += nudge;
    ed.Select(roots.front());
    for (std::size_t i = 1; i < roots.size(); ++i) ed.ToggleSelect(roots[i]);
    ed.dirty = true;
    return roots;
}

}  // namespace oked
