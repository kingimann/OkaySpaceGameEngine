#pragma once
// The Scene and Game views in window mode (docs/ui/own_ui_toolkit.md 6.24 step 6). The scene is rendered by the engine's software renderer (editor/SceneViewCore.hpp,
// no Dear ImGui), uploaded to a texture of the window's painter and shown in a docked OkayUI window. The Scene view orbits with the mouse (left or right drag)
// and zooms with the wheel over the same camera values the Dear ImGui editor uses; the Game view looks through the scene's main camera. Not done yet:
// the grid, gizmos, selection, labels, 2D scenes, ortho/axis views and the GPU renderers (the Dear ImGui editor still has all of those).
#include "OkayUIHostMode.hpp"
#include "okay/UI/OkayUI.hpp"

#include <functional>

#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
namespace okay { class Scene; class GameObject; namespace platform { class Painter; } }

namespace oked {
namespace wm {

/// Where the views get their scene and orbit camera. The pointers point at the editor's own camera fields so both editors share them.
struct ViewSource {
    std::function<okay::Scene*()> scene;
    float* yaw = nullptr; float* pitch = nullptr; float* dist = nullptr;   // degrees, degrees, distance
    okay::Vec3* target = nullptr;
    // the 2D view (used when `is2D` says the scene is 2D): the editor's camera position and zoom (world units visible vertically)
    std::function<bool()> is2D;
    okay::Vec2* cam2dPos = nullptr; float* cam2dZoom = nullptr;
    // selection and edits (the editor's EditorState behind them); all optional: without `select` clicking does not pick, without `selected` there is no gizmo
    std::function<okay::GameObject*()> selected;
    std::function<void(okay::GameObject*, bool toggle)> select;   // null object = clear the selection
    std::function<void(const char* what)> beginEdit;               // called once when a gizmo drag starts (one undo step); `what` is "Move", "Rotate" or "Scale"
    std::function<void()> edited;                                  // called each frame the drag changes the object
};
enum class GizmoMode { Move, Rotate, Scale };

/// Wire the "Scene" and "Game" windows (kPanelScene, kPanelGame) into `panels`. `painter` must outlive the panels' use of it and be the painter the frame loop draws
/// through. `st` (may be null) is the host state the windows' close buttons write to.
void AddSceneViews(HostModeState::Panels& panels, HostModeState* st, okay::platform::Painter* painter, const ViewSource& src);

/// What the last frame of a view did, for tests and tools.
struct ViewProbe {
    OkayUI::Rect image;          // where the picture was shown (window pixels)
    int texW = 0, texH = 0;      // the render size
    int litPixels = 0;           // pixels of the picture that are not transparent
    int frames = 0;              // pictures drawn so far
    bool view2D = false;         // the last Scene picture was the 2D view
    int spritesDrawn = 0;        // sprites in the 2D picture
    int gridLines = 0;           // grid lines drawn
    int outlineEdges = 0;        // selection outline edges drawn
    int gizmoAxes = 0;           // gizmo handles drawn (3 when an object is selected)
    bool gizmoDragging = false;
    GizmoMode mode = GizmoMode::Move;
    okay::GameObject* picked = nullptr;   // what the last click picked
    OkayUI::Rect gizmoHandle[3];          // the end of each axis handle (x, y, z), window pixels
    OkayUI::Rect modeButton[3], gridCheck;   // the toolbar
    const char* note = "";       // why nothing was drawn ("" when something was)
};
const ViewProbe& GetViewProbe(bool game);

} // namespace wm
} // namespace oked
