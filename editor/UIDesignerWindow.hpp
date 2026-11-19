#pragma once
// The UI designer's ImGui side: rulers with draggable guides on the UI editing view, and the UI Layers panel
// (hide, lock, rename, drag to reorder or nest). The logic is in UIDesignerModel.hpp (tested headless); this file only
// turns mouse and keys into calls and draws. Guides and locks live for the session and are not saved in scenes
// (hiding uses the object's own Active flag, which scenes already save).
#include "EditorState.hpp"
#include "imgui.h"
#include <vector>

namespace oked {

/// Tell the designer which scene is open: guides saved with it (a UI Guides object) are loaded when it changes.
void UIDesignerSyncScene(okay::Scene& scene);
/// Handle the pointer on the rulers and guides. Returns true when it is using the mouse this frame (a guide being
/// dragged, or a click on a ruler), so the widget editing must leave the click alone. Call before widget editing.
/// panelPos/panelEnd: the whole view; viewPos: top-left of the (zoomed) design screen; zoom: the view zoom.
bool UIDesignerRulerInput(ImVec2 panelPos, ImVec2 panelEnd, ImVec2 viewPos, float zoom, bool hovered, const ImGuiIO& io, bool* fit);
/// Draw the rulers (tick marks in design pixels), the guides, a Fit button and the Layers toggle.
/// The buttons (L, Fit, zoom %) sit in their own bar above the rulers.
void UIDesignerDrawRulers(ImDrawList* dl, ImVec2 panelPos, ImVec2 panelEnd, ImVec2 viewPos, float zoom);
/// Were the rulers drawn for this panel last frame (other overlays keep out of their area)?
bool UIDesignerRulersShown(ImVec2 panelPos);
/// Pull a handle point / a label box inside the area the rulers leave free (no-op when no rulers).
ImVec2 UIDesignerClampPoint(ImVec2 p);
ImVec2 UIDesignerClampLabel(ImVec2 pos, ImVec2 size);
/// Guide positions as snap targets, in screen pixels of the zoomed design screen (added to the drag's candidate lists).
void UIDesignerGuideCandidates(std::vector<float>& xs, std::vector<float>& ys, float zoom);
/// Is the widget locked in the Layers panel (not pickable on the canvas)?
bool UIDesignerLocked(const okay::GameObject* g);
/// The UI Layers window (only drawn while it is switched on from the ruler corner).
void UIDesignerLayersWindow(okay::editor::EditorState& ed);

/// Keyboard shortcuts (Ctrl+A, Ctrl+Shift+I / A / L / H, Ctrl+G, Ctrl+P, F1). Call each frame; `active` = the UI view has the keyboard.
void UIDesignerKeys(okay::editor::EditorState& ed, bool active);
/// The Ctrl+P action finder, the shortcut list and Find and replace windows (when open).
void UIDesignerOverlays(okay::editor::EditorState& ed);
/// True once after the Fit command ran from the action finder (the caller resets zoom and pan).
bool UIDesignerTakeFitRequest();

/// Rotate handle for a selected widget that has a UI Frame (a round dot above it; Shift snaps to 15 degrees). Input returns true while dragging.
bool UIDesignerRotateInput(okay::editor::EditorState& ed, ImVec2 viewPos, ImVec2 viewSize, const ImGuiIO& io);
void UIDesignerDrawRotate(ImDrawList* dl, okay::editor::EditorState& ed, ImVec2 viewPos, ImVec2 viewSize);

}  // namespace oked
