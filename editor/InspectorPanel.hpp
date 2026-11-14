#pragma once
// The drawing side of the Inspector / Hierarchy improvements (InspectorPanel.cpp); the
// logic is in InspectorModel.hpp. Everything here is a small hook the big windows call:
//  - WatchEditUndo: one undo step for every field edit in the Inspector and Hierarchy
//  - AssetField / ObjectField: reference fields (drag and drop, picker, clear)
//  - DrawFallbackInspector: a card for components without a hand-written Inspector,
//    built from the property registry, editing every selected object at once
//  - the component menu (copy / paste / reset values), prefab changes, the Play-mode note
#include "EditorState.hpp"
#include "imgui.h"
#include <functional>
#include <string>
#include <vector>

namespace okay { class Component; class GameObject; }

namespace oked {

/// Call once per frame, before the Hierarchy and Inspector are drawn.
void WatchEditUndo(okay::editor::EditorState& ed);

/// A file field: shows the file name, takes a drop from the Project panel (only files of `kind`: "image",
/// "audio", "model", "font", "prefab", ... or "" for any), has a picker listing the project's files of that
/// kind, and a clear button. `value` is the path as the Project panel drags it. True when it changed.
bool AssetField(const char* label, std::string& value, const char* kind, const std::string& projectDir,
                bool mixed = false, const char* tooltip = nullptr);
/// A scene-object field: shows the object's name, takes a drag from the Hierarchy, picks from a searchable list
/// of the scene's objects, clears. `guid` is the object's GameObject::Guid() ("" = none). True when it changed.
bool ObjectField(const char* label, std::string& guid, okay::Scene& scene, bool mixed = false,
                 const char* tooltip = nullptr);

/// For the older path text boxes: call right after the InputText. Adds a drop-down arrow (and a right-click menu)
/// with "Choose from project..." and "Clear". True when the path changed.
bool AssetPathPicker(std::string& value, const char* kind = "");
/// A thin, hard-to-miss marker for fields that follow a drag from the Project panel: draws an outline on the
/// last item while a compatible payload is being dragged. For the older text fields.
void HighlightAssetDrop(const char* kind = "");

// ---- Components -----------------------------------------------------------------------------------

using CompHeaderFn = bool (*)(const char*, okay::Component*, okay::Component**, bool);

/// Start of one Inspector frame (clears the "this component was drawn" bookkeeping).
void BeginInspectorFrame();
/// After the hand-written drawers had their turn for `comp`: if none of them drew anything (the cursor did not
/// move from `cursorYBefore`), draw it from the property registry. No-op for components without registered
/// properties. `targets` are all selected objects: edits go to every one that has the component.
void DrawFallbackInspector(okay::editor::EditorState& ed, okay::GameObject* go, okay::Component* comp,
                           okay::Component*& toRemove, float cursorYBefore, CompHeaderFn header);
/// The "Copy Values / Paste Values / Reset" rows of a component's menu. Call inside its popup.
void ComponentValueMenu(okay::Component* comp);
/// Run what the menu asked for. After the component loop, with the object that was drawn.
void ApplyComponentValueRequests(okay::editor::EditorState& ed, okay::GameObject* go);

// ---- Prefab instances (logic: PrefabEdit.hpp, PrefabTools) ----------------------------------------------------
/// What the editor's main.cpp provides to the prefab header: find the asset in the Project panel, and open the prefab for editing.
struct PrefabUiHooks {
    std::function<void(const std::string& path)> selectAsset;
    std::function<void(okay::GameObject* instanceRoot)> open;
};
void SetPrefabUiHooks(PrefabUiHooks hooks);
/// Refresh the list of changes for the selected object's instance (cheap; re-reads the file only now and then).
void RefreshPrefabChanges(okay::GameObject* go);
/// Forget the cached list now (after anything that rewrote a prefab or an instance).
void InvalidatePrefabChanges();
/// True when this component is changed in the instance compared with its prefab (for the header tint).
bool PrefabChangedFor(const okay::Component* comp);
/// The "Prefab: <name>" header with Open, Select Asset, Revert All, Apply All, Create Variant, Update All and Unpack. For any object in an instance.
void DrawPrefabHeader(okay::editor::EditorState& ed, okay::GameObject* go);
/// The "Changed from the prefab" list for the selected object: per component and per field Revert / Apply, added and lost components and child objects.
void DrawPrefabChanges(okay::editor::EditorState& ed, okay::GameObject* go);
/// Right after a field's widget: when that property differs from the prefab, draws the marker and the right-click menu
/// (Revert Property / Apply Property to Prefab). `label` is the text the field is drawn with. Called by FieldTip and the generic card.
void PrefabFieldMark(const char* label);
/// The prefab rows of a component's "..." menu (Revert / Apply Component). Call inside its popup.
void PrefabComponentMenu(okay::Component* comp);
/// Actions the Hierarchy's context menu asks for on an instance (the Inspector runs the same code from its buttons).
enum class PrefabAction { RevertAll, ApplyAll, Unpack, UnpackCompletely, UpdateAll };
void QueuePrefabAction(PrefabAction action, okay::GameObject* instanceRoot);
/// Make the Inspector show the "Create Variant" name box for this instance on its next frame.
void OpenCreateVariantDialog();
/// Carry out what the buttons queued. True when the instance was rebuilt: the object the Inspector shows is gone, so stop drawing this frame.
bool RunPrefabRequests(okay::editor::EditorState& ed);

/// Hand-written Inspectors: tell the tooltip lookup which component is being drawn (CompHeader does it) ...
void SetTipComponent(const okay::Component* comp);
/// ... then call right after a field's widget: shows the field's tooltip (registry text or the plain default) on hover.
void FieldTip(const char* label);
/// With several objects selected: one line under a component header naming the values that differ between them
/// ("Different on the selected objects: Intensity, Color"). No-op otherwise.
void DrawMixedNote(const okay::Component* comp);

/// Play mode: a note across the top that edits are not kept. No-op while editing.
void DrawPlayModeNote(okay::editor::EditorState& ed);

} // namespace oked
