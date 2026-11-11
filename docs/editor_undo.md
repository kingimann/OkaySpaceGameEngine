# Editor undo and redo

How the editor's Ctrl+Z works, which edits it covers, and which edits have an undo of their own or none.
The engine side (`UndoStack`, groups, merging, dirty tracking) is in [undo_recovery.md](undo_recovery.md).

## How it works

The editor owns one `okay::UndoStack` (`EditorState::undoStack()`). Every scene edit is a
`SceneDeltaCommand` (`editor/SceneUndo.hpp`): the scene text (`SceneSerializer::Serialize`) before and
after the edit, cut down to the stretch that differs (`TextDelta`), plus the selection before and after
as GUIDs. Undo and Redo rebuild the scene from the edited text, so an undone delete brings back the same
objects with the same GUIDs, components, children and sibling order, and the same objects are selected.

- **Recording.** An editor action calls `ed.PushUndo()` before it changes anything (the contract the old
  snapshot undo had, so the roughly 450 existing call sites did not change). The step is closed at the
  next boundary: the next `PushUndo`, Undo, Redo, Save, Play, or `CommitUndo()` (the editor calls that
  between frames while no mouse button is down and no widget is active). Closing compares the scene with
  the text at the previous boundary and records only the difference. So a drag of any length is one
  step, a `PushUndo` before a change that did not happen records nothing, and an edit nobody announced is
  caught at the next boundary as a step of its own: the history cannot drift from the scene.
- **Widgets.** Inspector, Hierarchy, Lighting (scene settings), Material, Animation and Custom Actions
  widgets need no call: `WatchEditUndo` (`editor/InspectorPanel.cpp`) opens a session when a widget is
  pressed and records one step when it is let go, if the scene changed (`editor/EditUndo.hpp`). It passes
  the widget's id as a merge key: the same widget edited again within one second, in the same place, joins
  the previous step (`TextDelta::Compose`). Dragging a value away and back leaves no step.
- **Names.** `PushUndo("Paint Roof")` names a step. Otherwise the name is worked out from the change:
  "Create Cube", "Delete 3 Objects", "Move Cube", "Reparent Cube", "Edit Cube", "Edit Hierarchy",
  "Scene Settings" (`DescribeSceneChange`). Edit > Undo and Redo show it ("Undo Delete Cube") and are
  greyed out, with the plain word, when there is nothing to do. View > History lists the names.
- **Compound actions.** Everything changed between two boundaries is one step. An edit of something that is
  not in the scene text (the animation draft) joins the same step with
  `PushUndo(name, std::make_unique<Command>(...))`; the step is an `UndoStack` group, so one Undo reverts
  both, and it exists even when the scene itself did not change.
- **Selection.** Restored by GUID: Undo selects what was selected before the edit, Redo what the edit left selected.
- **Limits.** 100 steps (oldest dropped), 64 MB (the stack's default), one-second merge window.
  A property edit in a 400-object scene costs about 300 bytes (the test prints the numbers). Inserting or
  deleting an object renumbers the objects behind it in the scene text, so such a step stores everything
  from that object to the end of the scene: the worst case is the old full snapshot, never worse.
- **Safety.** A command refuses (and the stack drops it as stale) if the scene is not in the state it
  expects; the scene is never half-edited (`Deserialize` is all-or-nothing). `UndoError()` says why and the
  Edit menu writes it to the Console.
- **Scene changes.** New Scene, the templates, Open, Recover and New Project start an empty history (a
  different document; before, New Scene could be undone and Open could not). A failed Open or New Project
  leaves the history as it was. Moving or renaming an asset the open scene uses rewrites its asset paths
  and ends the scene's Undo history (the stored pieces still name the old path); the Console says so.
  Undo of the move itself is the Project panel's own Undo.
- **Play.** Nothing is recorded and Undo / Redo are off while the game runs (the menu says why in a
  tooltip); the running game is thrown away on Stop. The history made before Play is untouched, and works
  again after Stop with no phantom step.
- **Saved state.** `MarkClean` (Save) puts a save point in the stack. Undo back to it clears the
  unsaved mark and the title-bar dot; Redo away from it sets them again. An edit after a save never merges into the saved step.
- **Keys.** Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z in `HandleShortcuts`; they do nothing while a text field has
  the keyboard (`io.WantTextInput`) or while the script / graph editors have focus.

## Audit: what goes through the UndoStack

Checked by reading the call sites in `editor/main.cpp`, `EditorState.cpp`, `InspectorPanel.cpp` (not by
running the GUI; see "Not verified"). "Stack" = a `SceneDeltaCommand` on the editor's `UndoStack`.

| Edit | Undo | How |
|---|---|---|
| Hierarchy: create (empty, sprite, camera, primitives, templates objects), delete, delete children, duplicate, group, paste, instantiate asset | Stack | `EditorState::Create*`, `DeleteSelected`, ... call `PushUndo`; tested for each |
| Hierarchy: reparent by drag, reorder siblings, rename | Stack | `PushUndo` in `DrawHierarchy` plus `WatchEditUndo` |
| Inspector: any field (transform, every component property, sliders, text, colour, pickers, tag, layer) | Stack, one step per drag or edit, merged when the same widget is edited again | `WatchEditUndo` |
| Inspector: add, remove, enable, reorder component | Stack | `PushUndo` / `WatchEditUndo` |
| Scene view: move, rotate, scale gizmos and drags, UI rect drags, align and distribute | Stack | `PushUndo` at drag start in `DrawViewport` / `SceneObjectDragChip` |
| Lighting window and scene settings (gravity, sky, fog, post, name, UI font) | Stack, named "Scene Settings" | `WatchEditUndo` now covers the window (before: only Inspector and Hierarchy) |
| Material window | Stack for what it writes into the scene; a material asset file is a saved file with no undo step | same |
| Animation tab: clip draft edits | Stack: command `AnimDraftCommand` in the same step as any scene change | `PushAnimDraftUndo` (before: matched by undo depth, which broke whenever a step did not change the scene) |
| Animation tab: other widgets that write the scene (model animator, object timeline) | Stack | `PushUndo` and `WatchEditUndo` |
| Any other scene change with no `PushUndo` (a plugin, a window not listed) | Stack, as an "Edit" step, picked up at the next boundary or when the Edit menu opens | drift capture |
| Mesh edit mode (Modeling, UV Editor) | Own stack while the mode is on (`g_meshUndo`); leaving the mode is one scene step | separate on purpose: vertex-level steps on a 100k-vertex mesh are not text |
| Project panel: create, rename, move, copy, delete (to `.okay/trash`) | Own stack (`okaypb::Browser`), Ctrl+Z while the panel has focus; the Edit menu does not cover it | files, not scene text; the Console says "Ctrl+Z in the Project panel brings them back" |
| Timeline, Localization (string table), Dialogue, Behavior Tree, Animator graph, visual script graphs, script editor | Own undo per window while it has focus | their own documents |
| Scene settings that live in the project, not the scene (Project Settings, Build Settings, tags and layers lists, item database, input map) | None | saved files; no scene step is recorded and no step is pretended |
| Play-mode edits | None, by design | thrown away on Stop |
| Delete a scene file (Scene Manager), Clear crash reports, Delete download (Asset Manager) | None, and the UI says so before it happens | "This cannot be undone" in the confirmation, or in the menu label and tooltip |
| Host / Join / Stop network | None (a session, not an edit) | Undo stops a running host first (as before) |

Rule kept: an action that calls `PushUndo` but changes nothing in the scene no longer leaves a do-nothing
step in the list (the old snapshot undo recorded one, so Ctrl+Z "did nothing" once).

## Not verified, and known limits

- The GUI was not run for this change. Everything in `EditorState`, `SceneUndo.hpp` and `EditUndo.hpp` is
  tested headless (`editor_undo`, plus `editor_hooks`, `inspector_model`, `project_save`, `ui_frame`); the
  menu, the shortcuts, `WatchEditUndo` and the History panel are compiled but not exercised.
- Scene steps made before an asset path rewrite are dropped, not rewritten.
- Undo and Redo cost one scene serialisation and one scene rebuild each, as before. Big scenes (thousands
  of objects) feel that. A finer command set (per-object create / delete / reparent through `ObjectOps`,
  per-property `SetProperty`) would avoid the rebuild and keep selection and Inspector state; it is the next step.
- The Edit menu covers scene (and mesh-mode) undo. A window with its own undo keeps its own Ctrl+Z.

## Tests

`editor_undo` (`tests/test_editor_undo.cpp`): text delta and compose; the command on a stand-in scene
(do, undo, redo, stale refusal, merge); 25 kinds of edit with the scene text compared after Do, Undo, Redo
and Undo again; delete restores GUIDs, children, components, sibling order and selection; selection and the
redo branch; grouping, names, editor-side commands in the same step, unannounced edits; merging and the
merge window; the 100-step limit and memory; New / Open / failed Open; Play; saved tracking; asset-path rewrite.
