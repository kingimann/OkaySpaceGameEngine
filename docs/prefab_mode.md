# Prefab mode

Prefab mode opens a `.okayprefab` on its own so you edit the prefab, not an instance of it. The scene you were
in is put aside and comes back exactly as you left it. It is built on the prefab layer in
[prefab_system.md](prefab_system.md) (`PrefabTools::Instantiate`, `SavePrefab`, `Reapply`, `CaptureOverrides`,
`ValidateAsset`) and on the editor's undo stack ([editor_undo.md](editor_undo.md)); it adds no second prefab
format and no second save path.

## Using it

Open a prefab in any of three ways:

- Double-click a `.okayprefab` in the Project panel (right-click > **Open Prefab**). **Place in Scene** is
  in the same menu when you want a copy in the scene instead.
- Right-click an object in the Hierarchy that is a prefab instance, or sits inside one > **Open Prefab**.
- Select an instance and press **Open** in the Inspector's Prefab box.

A bar appears at the top of the Scene view, the Scene view's background and frame turn blue-green, and the
window title shows the path (`Project / level > enemy`):

- **< Back** returns one level. If the prefab has unsaved changes you are asked: Save, Don't Save, Cancel.
- The **breadcrumbs** `Scene > enemy > weapon`: each earlier crumb is a button that goes back to it (an
  asterisk marks a level with unsaved changes).
- **Save** (also Ctrl+S, Save Scene, Save Project) writes the prefab file. It is greyed out until something changed.
- **Revert** throws the changes since the last save away and reads the file again. It is one undo step, so
  Undo brings your edits back. It is greyed out until something changed.

Inside prefab mode the Hierarchy, Inspector and Scene view show only the prefab; edit it as you would a
scene. To drill in, open a nested prefab instance (it appears as an instance inside the prefab, linked to its
own file) the same ways: another level opens, `Scene > squad > enemy`. Placing a prefab from the Project panel
while one is open adds an instance of it inside the open prefab (a nested prefab, saved as a link).

## What happens when you go back

- The scene, its selection (by object id), the Scene view's camera and zoom, and its own undo history are
  restored. Nothing you did in the prefab is in the scene's undo list, except the next point.
- If the prefab was saved, every instance of it in the scene (and every instance that holds it as a nested
  prefab) is rebuilt from the file with `PrefabTools::Reapply`: it keeps its name, parent, place in the list,
  object ids and what you overrode on it (position, colour and so on). That is one undo step,
  **Update Prefab Instances**, and the scene counts as having unsaved changes (the instances inside the
  scene file changed). The Console says how many were updated.
- Before the scene is put aside, the overrides of its instances are recorded (they are compared with the
  files while the files still match the instances). That is also an undo step, **Record Prefab Overrides**,
  and does not mark a clean scene as unsaved.
- Without Save, leaving with **Don't Save** discards the prefab's changes: the file and the scene stay as
  they were. A prefab saved earlier in the same visit still updates the instances.
- Going back out of several levels (a crumb) closes them one at a time, innermost first, and asks once for
  all the unsaved ones.

## Rules

- **Play is off** while a prefab is open (the Play button is greyed out and says why; Ctrl+P and the
  menu do the same). Go back to the scene to play.
- **Undo is per level.** Opening a prefab starts an empty history for it; the scene's (and an outer prefab's)
  history is kept and comes back. Undo and Redo, the Edit menu and View > History always show the open level's.
- **Unsaved changes** are tracked per level. Quitting while any level has some asks, and Save saves them all:
  the open prefab, outer prefabs and the scene put aside (`EditorState::SaveAllPrefabLevels`).
- **Other scenes cannot be opened** and the scene is not saved with Save Scene As while a prefab is open
  (the scene is put aside): the editor says to go Back first. New Scene and the other ways of replacing the
  scene are refused the same way in the UI; calling `EditorState::NewScene` from code ends the prefab session.
- **A prefab already open further out cannot be opened again** (it would be edited twice); a prefab that
  cannot load, is missing, or is not a `.okayprefab` is refused with the reason.

## What it does not do

- **Variants are not opened.** A variant (`variant "<base>"`) is a set of differences from its base;
  saving it as a plain prefab would drop the link. The editor says so; open the base prefab, or change the
  variant through its instances' overrides in the Inspector.
- **Only the prefab's root is saved.** Objects you create outside the root (top level) are not part of the
  prefab; the Console warns with the count when you save. Drag them under the root.
- **No crash-recovery copy** of a prefab being edited: the recovery autosave covers scenes. Save is
  explicit.
- A prefab file changed outside the editor while it is open is overwritten by Save; there is no merge.
- A scene without a file yet cannot be saved from inside a prefab (Save Project says so); go Back and save it.

## For code

`EditorState` (editor/EditorState.hpp, ImGui-free): `OpenPrefab(path)`, `OpenPrefabOf(object)`,
`CanOpenPrefab(path, &why)`, `SavePrefab(&error, &warning)`, `RevertPrefab()`, `ClosePrefab(save)`,
`ClosePrefabTo(depth, save)`, `SaveAllPrefabLevels()`, `RefreshPrefabInstances(path)`, `InPrefabMode()`,
`PrefabDepth()`, `PrefabCrumbs()`, `PrefabRootObject()`, `PlayBlockedReason()`. Tests: `prefab_mode`
(tests/test_prefab_mode.cpp). One engine change came with it: `PrefabTools::Reapply` now keeps a top-level
instance's place in the scene's list (it was appended at the end).
