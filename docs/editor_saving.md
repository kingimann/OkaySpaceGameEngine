# Editor saving and loading

This page is about how the **editor** saves and loads the user's own work (scenes, prefabs, scripts, graphs, assets,
settings). It is not about the save-game system for finished games (`SaveFramework`, `GameSaves`, see
[save_framework.md](save_framework.md)): that is a different thing.

Owner request (2026-11-17): the editor must never lose the user's work, saving and loading must be obvious and fast, and
problems must be explained in plain words. Try-it steps: [try_it.md](try_it.md#saves1-editor-saving-and-loading).

## What existed before this change, and the gap

| Area | Already there | Gap found |
| --- | --- | --- |
| Scene save | `SceneSerializer::SaveToFile`: temp file, check, rename, one `.bak` | no flush to disk before the swap; no way to test a half-way failure |
| Other asset saves | scripts and graph tabs through `BackupManager` (atomic, last 5 saves); `DataAsset`, `Prefs`, prefab files through `AtomicWrite` | about 20 saves wrote straight over the file (visual-script graphs, `.okaymesh`, `.okaymat`, behaviour trees, dialogue, quests, timelines, clips, UI themes, NavMesh, weapon book, items, loot, recipes, content editors, localisation CSV, undo of file edits) |
| Autosave and crash recovery | `RecoveryManager` rotating copies in `okay_recovery`, restore box with Compare, File > Autosave (on/off not remembered) | box did not say what is in the copy; no way to keep both versions; copies also written during focus loss without a limit and mid-drag; keep-N fixed at 3 |
| Dirty state | `EditorState::dirty` plus a fingerprint check, `*` in the title, "unsaved" dot, Hierarchy header | status bar "Saved" vanished after 5 seconds, no time |
| Unsaved prompts | Save / Don't Save / Cancel with a list in the quit box | lists were different in each box, no names the way people say them, no "Save All" wording |
| Save All | File > Save Project (Ctrl+Alt+S) saves scene, scripts, graphs, settings | the Console said "scene, 2 scripts", not which |
| Outside changes | prefab mode (`PrefabChangedOnDisk`) and script tabs (modified time) | scenes: nothing; Save silently overwrote a file changed by git or another editor |
| Loading | all-or-nothing loader (`#553`), safe against bad counts; version upgrade prompt and backup (`project_upgrade.md`) | errors named no line or object; no way to open what is readable; Recent list kept missing files |
| Scene file commands | Save, Save As, Merge, Open Recent | no Save a Copy, Revert to Saved, Duplicate Scene, Show in Folder, scene templates |

## What this change does

* **Atomic writes everywhere.** `savefmt::AtomicWriteChecked` (engine) writes `<file>.tmp`, flushes it to the disk, then
  swaps it in. Every save above now goes through it (or through `editor/SaveSafety.hpp`'s `WriteUserFile`, which wraps it).
  A test hook (`savefmt::SetWriteFault`) makes a write fail at the open, half way through the bytes, at the flush or at the
  swap, and optionally leave the half-written temp file behind as a killed editor would; the tests show the old file whole
  after each.
* **A scene keeps one `.bak`** (the previous save). Scripts and graph tabs keep the last five saves under `okay_backups`.
* **Recovery.** Autosave goes to `okay_recovery`, never over your file. The rules are `DecideAutosave`: off during Play,
  never closer than 5 seconds, not while a mouse button is held or a field is being typed in (at most 15 seconds of
  waiting), at once (but still rate limited) when the window loses focus, and slower if a copy was expensive (the cost
  stays under 1 percent of the time). File > Autosave has Enabled (now remembered), Interval, Keep copies and shows the
  cost. On the next start the box says what is in the copy (age and clock time, object count against the saved file) and
  offers **Restore**, **Open Both** (writes `<name> (recovered).okayscene` beside the file and opens it) and **Discard**.
  A normal save or a clean exit removes the copies.
* **Clear state.** `*` in the title, "unsaved" in the selection bar and Hierarchy header, and "Saved 14:03" in the status
  bar, which stays until the scene changes. The quit box and the "save before..." box list the unsaved things by name
  ("3 things have unsaved changes: Level1, Player prefab, enemy.os") with **Save All / Discard / Cancel**. Save All says in
  the Console what it saved, by name, and what it could not.
* **Outside changes.** The open scene file is watched (size and write time about once a second; content is read only when
  that is ambiguous, so a "touch" does not warn). A changed file asks **Reload / Keep Mine** (or, with unsaved edits,
  **Keep Mine / Save a Copy / Reload**). Saving over a changed file asks **Save a Copy / Overwrite / Reload / Cancel**.
  Autosave-to-file never overwrites a changed file on its own. Reload is an undoable step.
* **Loading errors in plain words.** The loader's message now carries the line and the last object it read; the editor
  turns it into a sentence, says that the open scene is unchanged and names the `.bak` when there is one. The box offers
  **Open readable objects (safe mode)** (each object is tried on its own, the good ones open as a NEW unsaved scene, the
  damaged ones are listed, the damaged file is never written) and **Open last good save**.
* **Scene File menu.** Save a Copy, Duplicate Scene, Revert to Saved (undoable), Show in Folder, Save Scene as Template and
  New from Template (`okay_scene_templates`). Names for copies never overwrite: `Level1 copy`, `Level1 copy 2`, ...
* **Open Recent.** Missing files are dropped at start (and by "Remove missing files"), pinned scenes stay on top
  (right-click), ten at most.

## Where the code is

| Piece | File |
| --- | --- |
| Atomic write and the fault hook | `engine/src/Core/SaveFile.cpp`, `engine/include/okay/Core/SaveData.hpp` |
| Scene save/load, error line and object | `engine/src/Scene/SceneSerializer.cpp` |
| Policy (outside change, unsaved list, autosave rules, recovery summary, load errors, recent, copy names) | `editor/SaveSafety.hpp` |
| Safe-mode scene salvage | `editor/SceneSalvage.hpp` |
| Revert, outside-change check in the editor state | `editor/EditorState.cpp` |
| Screens and menu wiring | `editor/main.cpp`, `editor/MenuBarModel.hpp` |
| Tests | `tests/test_editor_saves.cpp`, `tests/test_menu_bar_model.cpp` |

All of it was written from the editor's own needs; no outside code or text.

## What is not done (honest list)

* A progress bar with Cancel for very large scenes. Loading is still one blocking step. `SceneLoadJob` (a step-by-step loader)
  exists in the engine but the editor does not use it yet; it parses the file twice by design (a check pass in a throwaway
  scene, then the real one), which is the double parse an audit noted. It was not changed: it is what keeps the load
  all-or-nothing, and removing it needs the loader to build into a spare scene and swap.
* A Problems-panel entry for missing asset and script references on load (the existing Problems panel checks other things).
* Streaming asset writes that are not whole files still use a plain stream: the pose-clip library (`PoseCapture`), OBJ
  export and model-import side files.
* Per-tab unsaved marks on every editor window tab (the title, status bar, selection bar and Hierarchy header have them).
* Nothing here was run in a real editor window or on Windows.
