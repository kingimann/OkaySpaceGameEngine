#pragma once
// Inspectors and create helpers for the overlay widgets: UI Modal, UI Context Menu
// and UI Keybind Field. main.cpp calls these from its inspector loop and menus.
#include "UIInspectorLayout.hpp"   // UIInspectorHooks

namespace oked {

/// Draw the inspector for `comp` if it is a UI Modal, UI Context Menu or UI Keybind Field.
void DrawUIOverlayComponentInspector(okay::Component* comp, okay::editor::EditorState& ed,
                                     okay::Component** toRemove, const UIInspectorHooks& hooks);

/// Create > UI builders.
void BuildUIModalDialog(okay::GameObject* g);   // panel + modal + title + a Close button
void BuildUIToast(okay::GameObject* g);         // grok/ui-toast: a toast stack host
void BuildUIKeybindField(okay::GameObject* g);
void BuildUIContextMenu(okay::GameObject* g);   // a panel that has a right-click menu

} // namespace oked
