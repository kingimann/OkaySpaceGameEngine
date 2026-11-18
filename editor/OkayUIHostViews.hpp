#pragma once
// The Scene and Game views in window mode (docs/own_ui_toolkit.md 6.24 step 6). The scene is rendered by the engine's software renderer (editor/SceneViewCore.hpp,
// no Dear ImGui), uploaded to a texture of the window's painter and shown in a docked OkayUI window. The Scene view orbits with the mouse (left or right drag)
// and zooms with the wheel over the same camera values the Dear ImGui editor uses; the Game view looks through the scene's main camera. Not done yet:
// the grid, gizmos, selection, labels, 2D scenes, ortho/axis views and the GPU renderers (the Dear ImGui editor still has all of those).
#include "OkayUIHostMode.hpp"
#include "okay/UI/OkayUI.hpp"

#include <functional>

namespace okay { class Scene; struct Vec3; namespace platform { class Painter; } }

namespace oked {
namespace wm {

/// Where the views get their scene and orbit camera. The pointers point at the editor's own camera fields so both editors share them.
struct ViewSource {
    std::function<okay::Scene*()> scene;
    float* yaw = nullptr; float* pitch = nullptr; float* dist = nullptr;   // degrees, degrees, distance
    okay::Vec3* target = nullptr;
};

/// Wire the "Scene" and "Game" windows (kPanelScene, kPanelGame) into `panels`. `painter` must outlive the panels' use of it and be the painter the frame loop draws
/// through. `st` (may be null) is the host state the windows' close buttons write to.
void AddSceneViews(HostModeState::Panels& panels, HostModeState* st, okay::platform::Painter* painter, const ViewSource& src);

/// What the last frame of a view did, for tests and tools.
struct ViewProbe {
    OkayUI::Rect image;          // where the picture was shown (window pixels)
    int texW = 0, texH = 0;      // the render size
    int litPixels = 0;           // pixels of the picture that are not transparent
    int frames = 0;              // pictures drawn so far
    const char* note = "";       // why nothing was drawn ("" when something was)
};
const ViewProbe& GetViewProbe(bool game);

} // namespace wm
} // namespace oked
