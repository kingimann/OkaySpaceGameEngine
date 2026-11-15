# Hand-written verification rows: editor basics

### V-ED-001 Start the editor and show or hide panels
- **Map:** 15
- **Where:** `Tools\OkayEngine.exe` (or from the launcher)
- **Steps:**
  1. Start `Tools\OkayEngine.exe`. The window shows docked panels and a toolbar.
  2. Open **View > Panels** and untick then tick **Hierarchy**, **Inspector**, **Project**, **Console**.
  3. Open **View > Panels > Game**.
- **See:** each panel disappears and comes back where it was; the Game panel opens as a tab.
- **Partial:** `editor/main.cpp` is one very large file with no automated window test; only `--selftest` runs in CI.

### V-ED-002 New Project dialog
- **Map:** 15, 25
- **Where:** editor, File menu
- **Steps:**
  1. Choose **File > New Project...**.
  2. Pick a template (for example **Third Person**), name it and create it.
  3. Look at the **Hierarchy** panel.
- **See:** a project folder is made, its scene opens and the Hierarchy lists the template's objects.
- **Partial:** the template list is shared with the launcher (`shared/ProjectCreate.hpp`).

### V-ED-003 Play and Stop
- **Map:** 15
- **Where:** editor toolbar
- **Steps:**
  1. Open a template scene (for example **File > New 3D Scene**).
  2. Press the triangle Play button on the toolbar, or press Ctrl+P. Hover it: the tooltip says "Play (Ctrl+P)".
  3. Press it again (tooltip "Stop (Ctrl+P)") or Ctrl+P.
- **See:** the scene runs in the Game panel; on Stop the scene returns to exactly what it was before Play (moved objects go back).
- **Partial:** Play is disabled while a prefab is open for editing (the tooltip says why).

### V-ED-004 Save, open and recent scenes
- **Map:** 15, 1
- **Where:** editor, File menu
- **Steps:**
  1. Make a change (for example **GameObject > 3D Object > Cube**) and press Ctrl+S (**Save Scene**; a new scene asks for a name via **Save Scene As...**).
  2. Choose **File > New 3D Scene** (it asks about unsaved changes first).
  3. Press Ctrl+O (**Open...**), paste the path of your scene, or press **Browse...**, and open it. Also try **Open Recent**.
- **See:** the saved cube is back after reopening; the title shows no unsaved mark after saving.
- **Partial:** `.okayscene` stores what the validator knows; unknown fields are dropped.

### V-ED-005 Undo and redo
- **Map:** 22, 15
- **Where:** editor, Edit menu
- **Steps:**
  1. Add a cube with **GameObject > 3D Object > Cube**, then delete it with **Delete Selected** (Del).
  2. Open the **Edit** menu: the first entry says what Ctrl+Z would undo. Press Ctrl+Z, twice.
  3. Press Ctrl+Y.
- **See:** the cube returns on the first undo and disappears on the second; redo brings it back. Undo and Redo are greyed when nothing can be undone.
- **Partial:** map area 22 (undo/redo and recovery) is listed as planned in parts; check which sub-areas say Works.
- **Link:** [editor_undo.md](../../editor_undo.md)

### V-ED-006 Unsaved changes prompt
- **Map:** 15
- **Where:** editor
- **Steps:**
  1. Change something in a scene without saving.
  2. Choose **File > New 3D Scene**, or **Exit** from the File menu.
- **See:** a prompt with Save, Don't Save and Cancel; Cancel keeps the scene as it is.
- **Partial:** none known.

### V-ED-007 Autosave
- **Map:** 15, 22
- **Where:** editor, File menu
- **Steps:**
  1. Open **File > Autosave** and tick **Enabled**.
  2. Make a change and wait a minute or two.
  3. Look at the Console for an autosave line.
- **See:** an autosave happens without a prompt. The option **Also save the real file** controls whether your own scene file is written.
- **Partial:** crash recovery after a forced close has not been verified on Windows.

### V-ED-008 Hierarchy: duplicate, group, copy
- **Map:** 15, 1
- **Where:** editor, Hierarchy panel and GameObject menu
- **Steps:**
  1. Create two cubes (**GameObject > 3D Object > Cube**).
  2. Select one and press Ctrl+D (**Duplicate Selected**).
  3. Select two objects and press Ctrl+Shift+G (**Group Selected**).
- **See:** a copy appears next to the original; the group object contains the two selected ones in the Hierarchy.
- **Partial:** none known.

### V-ED-009 Add a component in the Inspector
- **Map:** 15, 1
- **Where:** editor, Inspector panel
- **Steps:**
  1. Select a cube.
  2. In the **Inspector** press **Add Component** and type "Audio" in the search box.
  3. Choose **Audio Source**.
- **See:** an Audio Source section appears in the Inspector; a cube you added it to can be undone with Ctrl+Z.
- **Partial:** none known.

### V-ED-010 Project panel: create files
- **Map:** 15, 20
- **Where:** editor, Project panel (needs an open project)
- **Steps:**
  1. Right-click in an empty part of the **Project** panel.
  2. Create a **Folder**, then a **Script** and a **Scene** inside it.
  3. Right-click the script and choose **Edit Script**.
- **See:** the files appear in the panel and the script opens in the Script Editor.
- **Partial:** with no project open the panel says to use File > New Project first.

### V-ED-011 Theme and layout
- **Map:** 15
- **Where:** editor, View menu
- **Steps:**
  1. Open **View > Appearance > Theme** and pick **Light**, then **Classic**, then **Slate**.
  2. Drag a panel tab to another place. Then choose **View > Layout > Reset Layout**.
- **See:** the colours change at once; Reset Layout puts the panels back.
- **Partial:** none known.

### V-ED-012 Console and Problems panels
- **Map:** 15, 54
- **Where:** editor
- **Steps:**
  1. Open **View > Panels > Console**; create an object and read the log line ("Created Cube").
  2. Tick **Clear Console on Play** in the **View** menu, press Play.
  3. Open **View > Panels > Problems**.
- **See:** log lines appear as you work; the Console is empty at the start of Play; Problems lists scene warnings (or is empty).
- **Partial:** none known.

### V-ED-013 Keyboard shortcuts and About
- **Map:** 15
- **Where:** editor, Help menu
- **Steps:**
  1. Open **Help > Keyboard Shortcuts**.
  2. Open **Help > About OkaySpace** and read the version and the stack line (see the own-code rows).
- **See:** a list of shortcuts, and a version number that matches the launcher.
- **Partial:** none known.

### V-ED-014 Project Settings
- **Map:** 15, 25
- **Where:** editor, Edit menu
- **Steps:**
  1. Choose **Edit > Project Settings...**.
  2. Look through the tabs and change a value, then close and reopen the window.
- **See:** the value is kept with the project.
- **Partial:** none known.

### V-ED-015 Plugins menu
- **Map:** 24
- **Where:** editor, Plugins menu
- **Steps:**
  1. Open the **Plugins** menu and choose **Plugin Manager...**.
  2. Choose **New Plugin...** and give it a name.
  3. Choose **Reload All**.
- **See:** the plugin manager lists plugins; a new plugin gets a template script you can open with **Edit Script**.
- **Partial:** area 24 is mostly planned in the map; only the basic plugin loader exists.
- **Link:** [editor-plugins.md](../../editor-plugins.md)

### V-ED-016 History, Stats and Profiler panels
- **Map:** 22, 23
- **Where:** editor, View menu
- **Steps:**
  1. Open **View > Panels** and turn on **History**, **Stats** and **Profiler**.
  2. Do a few edits and press Play.
- **See:** History lists your edits; Stats and Profiler show numbers while the game runs.
- **Partial:** numbers are not compared against anything (no budgets yet, map area 58).

### V-ED-017 Scene View camera and gizmos
- **Map:** 15
- **Where:** editor, Scene panel
- **Steps:**
  1. Open **View > Scene Overlays** and toggle **Gizmos (selection / camera / lights)** and **Colliders (gizmos)**.
  2. Select a cube and drag its gizmo in the Scene view.
- **See:** the overlays appear and vanish; dragging moves the cube and Ctrl+Z puts it back.
- **Partial:** none known.
