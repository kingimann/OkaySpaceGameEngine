#pragma once
// The Guided page of the Modeling window (GuidedPanel.cpp): pick a category and a starting design, get an ordinary
// editable mesh, then change its settings, swap parts, keep editing by hand. The rules that keep hand edits safe are in
// engine/include/okay/Modeling/Guided.hpp and docs/editor/guided_modeling.md. It is a page of the existing Modeling
// window, not a new window; the host (main.cpp) lends it the mesh edit mode's selection and the thumbnail renderer.
#include "EditorState.hpp"
#include "okay/Render/Mesh.hpp"
#include <functional>
#include <string>
#include <vector>

namespace oked::guided_ui {

struct Host {
    okay::editor::EditorState* ed = nullptr;
    /// A small picture of a mesh, cached by `key` (a texture the caller owns), or null for "not ready yet". `build` makes
    /// the mesh and is only called when the picture is not cached.
    std::function<void*(const std::string& key, const std::function<okay::Mesh()>& build)> thumb;
    /// Polygon faces (of PolyMesh::FromMesh with weld off) selected in mesh edit mode.
    std::function<std::vector<int>(okay::GameObject*)> selectedFaces;
    /// Enter mesh edit mode on `go` in face mode with exactly these polygon faces selected.
    std::function<void(okay::GameObject*, const std::vector<int>&)> selectFaces;
    /// Leave mesh edit mode if `go` is in it (commits the edit session as one undo step).
    std::function<void(okay::GameObject*)> leaveEditMode;
    std::function<void(const std::string&, int)> log;
};

void DrawGuidedPage(Host& host);

// Click-to-draw path (fence and wall): while this is on for the selected object, each click on the ground in the Scene view
// adds a point (snapped to the grid step on the page), Esc or the page's Finish button ends it. main.cpp calls these
// from the Scene view's click handling.
bool PathDrawing(okay::GameObject* selected);
void PathDrawClick(const okay::Vec3& worldPoint);
void PathDrawStop();

} // namespace oked::guided_ui
