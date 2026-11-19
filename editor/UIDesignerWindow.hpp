#pragma once
// The UI designer's ImGui side: rulers with draggable guides on the UI editing view, and the UI Layers panel
// (hide, lock, rename, drag to reorder or nest). The logic is in UIDesignerModel.hpp (tested headless); this file only
// turns mouse and keys into calls and draws. Guides and locks live for the session and are not saved in scenes
// (hiding uses the object's own Active flag, which scenes already save).
#include "EditorState.hpp"
#include "imgui.h"
#include <vector>

namespace oked {

/// Handle the pointer on the rulers and guides. Returns true when it is using the mouse this frame (a guide being
/// dragged, or a click on a ruler), so the widget editing must leave the click alone. Call before widget editing.
/// panelPos/panelEnd: the whole view; viewPos: top-left of the (zoomed) design screen; zoom: the view zoom.
bool UIDesignerRulerInput(ImVec2 panelPos, ImVec2 panelEnd, ImVec2 viewPos, float zoom, bool hovered, const ImGuiIO& io);
/// Draw the rulers (tick marks in design pixels), the guides, a Fit button and the Layers toggle.
/// Sets *fit when the Fit button was clicked.
void UIDesignerDrawRulers(ImDrawList* dl, ImVec2 panelPos, ImVec2 panelEnd, ImVec2 viewPos, float zoom, bool* fit);
/// Guide positions as snap targets, in screen pixels of the zoomed design screen (added to the drag's candidate lists).
void UIDesignerGuideCandidates(std::vector<float>& xs, std::vector<float>& ys, float zoom);
/// Is the widget locked in the Layers panel (not pickable on the canvas)?
bool UIDesignerLocked(const okay::GameObject* g);
/// The UI Layers window (only drawn while it is switched on from the ruler corner).
void UIDesignerLayersWindow(okay::editor::EditorState& ed);

}  // namespace oked
