#pragma once
// The Hierarchy and Inspector in window mode (docs/own_ui_toolkit.md 6.24 step 9). The Dear ImGui editor's Hierarchy (DrawHierarchy, ~580 lines) and Inspector
// (DrawInspector, ~12,000 lines of per-component drawers) live inside main.cpp and use its statics throughout, so they are not moved. These two windows are
// written here over the parts that are already separate: the Hierarchy over EditorState, the Hierarchy search model (InspectorModel.hpp: HierFilter) and the
// scene; the Inspector over the property registry through the editor's own generic field code (InspectorPanel.cpp, compiled a second time against OkayUI under
// renamed entry points, like the panels in OkayUIHostPanels.cpp). Every component that has registered properties gets its card; the hand-written
// per-component drawers of the Dear ImGui Inspector (curve editors, bone pickers, previews and so on) are not part of this.
#include "OkayUIHostMode.hpp"
#include "okay/UI/OkayUI.hpp"
#include <vector>
#include <string>
namespace okay { class GameObject; }

namespace okay { namespace editor { class EditorState; } }

namespace oked {
namespace wm {

/// Wire the "Hierarchy" and "Inspector" windows (kPanelHierarchy, kPanelInspector) over the editor state into `panels`. `st` (may be null) gets their open flags.
void AddObjectPanels(HostModeState::Panels& panels, HostModeState* st, okay::editor::EditorState* ed);

/// What the last frame of the two windows drew, for tests and tools.
struct ObjectsProbe {
    int hierarchyRows = 0;                    // rows drawn in the Hierarchy
    OkayUI::Rect firstRow, createButton, filterField, activeField, createEmpty;   // window pixels (activeField: the object's "active" check box in the Inspector)
    std::string selectedName;                 // what the Inspector showed ("" = nothing selected)
    int inspectorCards = 0;                   // component cards drawn (Transform and the object block count)
    int fieldsDrawn = 0;
    std::vector<OkayUI::Rect> rowRects; std::vector<okay::GameObject*> rowObjects;   // the Hierarchy rows as drawn last frame (window pixels)
    bool dragging = false;                    // a Hierarchy row is being dragged
    int reparented = 0;                       // objects moved by the last drop
    OkayUI::Rect addComponentButton, firstRemoveButton; int addMenuItems = 0; OkayUI::Rect firstCategory, firstAddItem;   // the Inspector's Add Component button, the first component's remove (x) button
    int fieldsDrawnDummy = 0;                      // property fields drawn in the Inspector
};
const ObjectsProbe& GetObjectsProbe();

} // namespace wm
} // namespace oked
