#pragma once
// Inspectors and create helpers for the UI layout widgets: flex layout fields on a
// Layout Group, the scroll axis on a Scroll View, and the UI Flex Item, UI Mask and
// UI List View components. main.cpp calls these from its inspector loop and menus.
#include "EditorState.hpp"

namespace oked {

/// main.cpp's shared inspector pieces, handed in so this file stays independent.
struct UIInspectorHooks {
    bool (*header)(const char* label, okay::Component* comp, okay::Component** toRemove, bool removable);
    void (*anchorCombo)(const char* id, okay::UIAnchor& anchor, okay::editor::EditorState& ed);
};

/// Draw the inspector for `comp` if it is a UI Flex Item, UI Mask or UI List View.
void DrawUILayoutComponentInspector(okay::Component* comp, okay::editor::EditorState& ed,
                                    okay::Component** toRemove, const UIInspectorHooks& hooks);
/// The Flex Row / Flex Column fields of a Layout Group (box size, justify, align, wrap).
void DrawLayoutGroupFlexFields(okay::UILayoutGroup& lg, okay::editor::EditorState& ed);
/// A Scroll View's axis and horizontal extent fields.
void DrawScrollViewAxisFields(okay::UIScrollView& sv, okay::editor::EditorState& ed);

/// Create > UI > Layout builders.
void BuildUIFlexBox(okay::GameObject* g);
void BuildUIMaskedPanel(okay::GameObject* g);
void BuildUIListView(okay::GameObject* g);   // adds a row template child (a button) and sample entries

} // namespace oked
