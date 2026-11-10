#pragma once
// Editor tools for the in-game (runtime) UI: the UI Theme window with the 12 looks and roles, inspectors for
// the runtime UI components added with them (UI Style Variant, UI Damage Numbers, UI Dialogue Box), extra
// entries for GameObject > UI, and File > Examples > UI Gallery. main.cpp only calls into this file.
#include "EditorState.hpp"
#include "UIInspectorLayout.hpp"

namespace oked {

/// The body of the "UI Theme" window: pick a look, see it, apply it to all UI or the selection,
/// and save / load it as a .okaytheme asset.
void DrawUIThemeWindowBody(okay::editor::EditorState& ed);

/// Draw the inspector for `comp` when it is one of the runtime UI components above. True when handled.
bool DrawRuntimeUIComponentInspector(okay::Component* comp, okay::editor::EditorState& ed,
                                     okay::Component** toRemove, const UIInspectorHooks& hooks);

/// The built-in look chooser used by the UI Theme component's inspector (12 looks + Custom).
bool DrawThemePresetCombo(okay::UITheme& th, okay::editor::EditorState& ed);

/// Extra items for GameObject > UI > Prebuilt Screens / Widgets. `parent` is where new pieces go.
void DrawUIAddMenuExtras(okay::editor::EditorState& ed, okay::GameObject* parent, bool& created);

/// File > Examples: the list of built-in example scenes. `open` runs a loader after the unsaved-changes check.
void DrawExamplesMenu(okay::editor::EditorState& ed,
                      const std::function<void(const std::function<void()>&)>& afterSavePrompt);

/// Replace the open scene with the UI Gallery example.
void OpenUIGalleryExample(okay::editor::EditorState& ed);

} // namespace oked
