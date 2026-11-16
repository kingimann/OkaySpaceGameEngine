---
id: 16
row: | 16 | Main menu bar clean-up: one Examples, Edit clipboard commands, GameObject categories, View, Help | The bar at the top of `Tools\OkayEngine.exe` | New in this change; see section 16 for what was and was not seen |
---
## 16. Main menu bar (cleaned up)

What changed and why: [editor_menus.md](editor_menus.md) (the map, and a table of every old item and where it went).
Where: the menu bar at the top of the editor, `Tools\OkayEngine.exe` (or start the editor from the launcher). Open any project.

1. Click **File**. You should see, in this order: New 2D Scene, New 3D Scene, Open Scene... (Ctrl+O), Open Recent, **one** Examples
   (it used to be three), then Save Scene / Save Scene As... / Save Project with their keys, then New Project... / Open Project Folder... /
   Project Upgrade..., then Import Model... / Merge Scene into Current..., then Autosave and Build Settings... (Ctrl+B), then Exit.
   Open **Examples**: UI Gallery, UI Gallery (phone), Visual Script Node Playground, Node Playground (flow and variables).
   With no recent scenes, hover **Open Recent**: the tip says why it is greyed. Press Play, then open File: **Save Scene** is greyed and
   the tip says "Stop Play first ...".
2. Click **Edit**. Undo and Redo name the step ("Undo Move") and are grey, with a tip, when there is nothing to undo. With nothing selected,
   Cut, Copy, Duplicate and Delete are grey ("Select an object ... first."). Select an object: they work. Ctrl+click three objects in the
   Hierarchy, open Edit again: the labels read **Duplicate 3 Objects** and **Delete 3 Objects**, and Cut is grey with the reason. **Preferences**
   holds UI Scale, Density, Theme and Accent color (they used to be View > Appearance). **Project Settings...** is at the bottom.
3. Click **GameObject**. Create Empty, Create Empty Child and Create Empty Parent (Ctrl+Shift+G) come first; Child and Parent need a selection.
   Then the categories: **2D Object, 3D Object, Player, Light, Camera, Audio, Effects, UI**. **Audio > Audio Source** (new) puts a sound object in
   the scene and selects it; its Audio Source component is in the Inspector. Everything the old menu offered is in one of the categories
   (the table in editor_menus.md says which).
4. Click **View**. **Panels**, **Editors & Tools** and **Layout** are where they always were. Below: **Frame Selected (F)**, **Isolate Selection
   (Shift+I)**, **UI Editing Mode**, **Scene Overlays**, **Camera Bookmarks** (hover "Go to slot 1" before saving one: the tip says to save
   first), **Rendering & Performance**. There is no loose "Reset Layout" any more: it is inside **Layout**.
5. Click **Help**. **Documentation (online)** opens the docs folder on GitHub in your browser. **Report a Bug...** opens the issue page and
   puts `OkaySpace v<version>` on the clipboard (paste it somewhere to see). **Check for Updates...** moved here from the old Engine menu.
6. The **Window** and **Engine** menus are gone: their items were duplicates (Items, Loot & Recipes is in View > Editors & Tools; Build
   Settings is in File) or moved (Check for Updates to Help).

Screenshots of the real editor (built and run on Linux, in a virtual display; not Windows): [docs/images/editor_menus/](images/editor_menus/)
(`file.png`, `file_examples.png`, `file_play_save_greyed.png`, `edit_undo_named.png`, `gameobject.png`, `view.png`, `help.png`). What was and
was not clicked is listed in [editor_menus.md](editor_menus.md) section 3b; "Checks run for this change" is at the end of section 17.
