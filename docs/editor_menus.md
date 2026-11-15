# Editor menus: audit and map

The editor's main menu bar and the Hierarchy panel's right-click menus, cleaned up on 2026-11-15 at the owner's request
("clean the menu bar better, and improve the context menus, add more"). This page has three parts: the **map** of the menus as
they are now, the **audit** of the old menus (what every item was, where it lived, what was done with it), and **how it is built
and tested**. The Inspector and Project panel menus are separate pieces of work (branches `claude/inspector-menus` and
`claude/project-menus`) and are not covered here.

The owner typed "inspector" twice; this was read as: the main menu bar plus the Hierarchy panel's menus (the Inspector and Project
menus went to the other two agents).

How to try everything below in the Windows editor is in [try_it.md](try_it.md) (rows 14 and 15).

## 1. The map: the menu bar now

`File  Edit  GameObject  View  Plugins  Help`. The **Window** menu (one item, a duplicate) and the **Engine** menu (a duplicate and
Check for Updates) are gone. Greyed-out items now say why when you hover them. Shortcuts are shown on the right and are only ever keys
that really work.

```
File
  New 2D Scene
  New 3D Scene
  Open Scene...                  Ctrl+O
  Open Recent >                  (greyed with a reason when empty)
  Examples >                     UI Gallery, UI Gallery (phone), Visual Script Node Playground >, Node Playground (flow and variables) >
  ---
  Save Scene                     Ctrl+S         (greyed in Play: "Stop Play first ...")
  Save Scene As...               Ctrl+Shift+S   (greyed in Play)
  Save Project                   Ctrl+Alt+S
  ---
  New Project...
  Open Project Folder...
  Project Upgrade...             (greyed without a project, or in Play, with its own reason)
  ---
  Import Model...
  Merge Scene into Current...
  ---
  Autosave >
  Build Settings...              Ctrl+B
  ---
  Exit
Edit
  Undo <what>                    Ctrl+Z         (greyed: "Nothing to undo yet." / "Undo is off while the game runs ...")
  Redo <what>                    Ctrl+Y
  ---
  Cut                            Ctrl+X         (greyed with several selected: it would delete them all and keep one)
  Copy                           Ctrl+C
  Paste                          Ctrl+V
  Duplicate / Duplicate 3 Objects  Ctrl+D
  Delete / Delete 3 Objects      Del
  ---
  Select All
  Deselect
  ---
  Preferences >                  UI Scale, Density, Theme, Accent color   (was View > Appearance)
  Project Settings...
GameObject
  Create Empty
  Create Empty Child             (needs a selection)
  Create Empty Parent            Ctrl+Shift+G   (needs a selection; "... for 3 Objects" with several)
  ---
  2D Object >      Sprite, 2D Character (Sprite Sheet)..., 2D Shape >, Tilemap
  3D Object >      the 21 primitives, Nature >, Props >, Structures >, Terrain, Voxel Terrain (caves), Water, Character
  Player >         the 9 ready-made players
  Light >          Directional, Point, Spot
  Camera >         Camera, then Cinemachine: Virtual Camera, Cinemachine Brain, Dolly Path, Dolly Cart
  Audio >          Audio Source, Music Player                                   (new)
  Effects >        Particle System, Spline, Foliage Layer, Decal Projector, Cable, Volume >
  UI >             Basic, Controls, HUD, Layout & Overlays, App, Kit, Prebuilt Screens, Canvas, Event System, World Label,
                   the three inventories, UI Document, Save Selected as UI, 3D UI (in-world)
  ---
  Instantiate Prefab...
  ---
  Array Duplicate...             (needs a selection)
  Snap Position to Grid          (needs a selection)
View
  Panels >                       Hierarchy, Inspector, Project, Console, Problems, Game | Services, Scene Manager, Net,
                                 Save Manager, Variables (watch) | History, Stats, Lighting, Profiler, Asset Manager, Crash Log, Audio Mixer
  Editors & Tools >              Custom Actions, Visual Script, Script Editor | Animation, Animator, Timeline, Navigation,
                                 Dialogue & Quests, Behavior Tree | Modeling, UV Editor, Sprite Editor | UI Editor (separate tab),
                                 UI Theme, Items Loot & Recipes, Weapons, Localization
  Layout >                       Built-in, Saved, Save Layout..., Delete Layout, Maximize Viewport, Side Tabs, Reset Layout
  ---
  Frame Selected                 F              (new in the menu; the key already worked)
  Isolate Selection              Shift+I        (new in the menu; "Exit Isolation" while isolating)
  UI Editing Mode
  ---
  Scene Overlays >
  Camera Bookmarks >             (Go to slot N is greyed, with the reason, until the slot is saved)
  ---
  Rendering & Performance >
Plugins                          (unchanged)
Help
  Scripting Reference
  Documentation (online)         (new: opens the docs folder on GitHub in the browser)
  Keyboard Shortcuts
  ---
  Check for Updates...           (was Engine > Check for Updates...)
  Report a Bug...                (new: opens the issue page; the version line is put on the clipboard first)
  ---
  About OkaySpace
```

`View` keeps **Panels**, **Editors & Tools** and **Layout** where every other doc already says they are ("View > Editors & Tools >
Navigation" and so on); only the empty Window menu was removed. If the owner prefers a Unity-style Window menu for those three,
it is a data change in `MenuBarModel.hpp` (move the three entries to a new top menu) plus a rename of "View >" in about 25 doc lines.

The shared Create lists (2D, 3D, Player, Light, Camera, Audio, Effects, UI) are the same everywhere: GameObject, the Hierarchy [+]
button, the Hierarchy right-click menus and Create Child.

## 2. The map: the Hierarchy menus now

Source of truth: `editor/HierarchyContextModel.hpp` (what is in the menu and when it is greyed out). Drawn by `DrawHierarchy` in
`editor/main.cpp` (`HierRowAction`, `HierEmptyAction`).

**Right-click an object** (it is selected first, unless it is already part of a selection):

```
Copy                           Ctrl+C         (with several selected: copies the one you clicked)
Cut                            Ctrl+X         (greyed with several selected, with the reason)
Paste                          Ctrl+V         (next to it; greyed with a reason when nothing is copied)
Paste as Child
---
Rename                         F2
Duplicate / Duplicate 3 Objects  Ctrl+D
Delete / Delete 3 Objects      Del
Delete Children                (greyed: "This object has no children.")
---
Create Empty Child
Create Child >                 2D Object, 3D Object, Player, Light, Camera, Audio, Effects, UI   (was 7 primitives)
Create Parent / Group 3 Objects  (Ctrl+Shift+G for several)
Parent To >
Unparent (to top level)        (greyed at the top level)
Order >                        Move Up Ctrl+Up, Move Down Ctrl+Down, Move to Top, Move to Bottom (greyed at the ends, with the reason)
---
Select >                       Children, Everything Below, Parent, Siblings, Same Name, Same Type (Light)   (Same Type is new)
Expand All Below / Collapse All Below   (parents only)
---
Deactivate / Activate          (counts with several; now undoable)
Lock in Scene View / Unlock in Scene View
Focus in Scene                 F
Isolate / Exit Isolation       Shift+I
Transform >                    Reset Position, Rotation, Scale, All, Move to View Centre
---
Add Component...               (new: selects it, shows the Inspector and opens its Add Component browser)
Prefab >                       (prefab parts only) Open, Select Prefab Asset, Revert All, Apply All, Create Variant..., Update All Instances, Unpack, Unpack Completely
Save As Prefab...
Rig & IK >                     (objects that can be a model or a character) Add IK Target, Use Built-in Animations, Map Humanoid Bones..., Rig Model (Humanoid)
---
Copy Name or Path >            Name, Path
```

**Right-click the empty space** under the rows: Create Empty; the eight Create categories; Paste; Select All (Ctrl+A), Deselect; Expand
All, Collapse All, Sort A-Z. **The [+] button** at the top: Create Empty, Create Empty Child, Group Selected (Ctrl+Shift+G) and the
eight Create categories.

### Hierarchy: Existing / Gap

| Suggested item | Existing | Gap and what was done |
|---|---|---|
| Create Child (submenu) | `Create Child` with 7 primitives (Cube, Sphere, Cylinder, Plane, Sprite, Camera, Light) | Extended: every creator the editor has (the eight categories), shared with GameObject. Not a second copy. |
| Duplicate, Rename (F2), Delete | all three | Plural labels ("Delete 3 Objects"). |
| Copy, Paste, Paste as Child | Copy, Cut, Paste As Sibling, Paste As Child | Kept. "Paste As Sibling" is now "Paste". Greyed with a reason when nothing is copied. **Cut is greyed with several selected** (see section 5). |
| Group / Create Empty Parent | `Create Parent` (one object) and `Group Selected` (several) | Merged into one item: "Create Parent", or "Group N Objects" for several. |
| Unparent | `Unparent (to top level)`, greyed without a reason | Reason added. |
| Focus in Scene (F) | `Focus` | Renamed. |
| Select Children, Select Same Type | Select > Children, Everything Below, Parent, Siblings, Same Name | **Same Type added** (all cameras, all lights, all meshes, ...: the kind comes from the row icon's rules). |
| Expand / Collapse All Children | `Expand All Below`, `Collapse All Below` | Kept. |
| Toggle Active | `Deactivate` / `Activate` | Plural labels; **was not undoable, now is** (the eye icon already was). |
| Copy Path / Name | `Copy Text > Name, Path` | Renamed "Copy Name or Path". |
| Save as Prefab, Select Prefab Asset, Unpack | all of them, in `Prefab >` and `Save As Prefab...` | "Select Asset" renamed "Select Prefab Asset". Greyed items (Revert, Apply, Unpack Completely) say why. |
| Add Component | the Inspector's "+ Add Component" browser (categories, search, Recent) | **Added**: "Add Component..." selects the object, shows the Inspector and opens that same browser. No second list. |
| Move to Top / Up / Down / Bottom | `Order >` with the four | Greyed at the ends, with the reason. |
| Empty space: Create, Paste, Expand All, Collapse All, Select All | Create Empty / Sprite / Cube / Camera / Light, 3D Object (5 shapes), UI (about 30 widgets), Paste, Select All, Deselect, Expand/Collapse All, Sort A-Z | Create is now the full shared list. The five quick creators are inside the categories (Sprite in 2D Object, Cube in 3D Object, Camera, Light). |
| Create Empty Child | existed, **not undoable** | Now uses the editor's own create (one undo step). |
| [+] button | Empty, Sprite, Camera, Cube, Group Selected, UI Button, UI Text, 3D UI (in-world) | Create Empty, Create Empty Child, Group Selected and the same eight categories. Sprite, Cube, Camera, UI Button, UI Text and 3D UI are inside them (2D Object, 3D Object, Camera, UI > Basic, UI > 3D UI). |
| Delete (row menu) | destroyed the object at once, while the rows below it were still being drawn from it | Deferred until the tree is drawn, like Cut and Delete Children. |

## 3. The audit: the old menu bar

"Old code" is `DrawMenuAndToolbar` in `editor/main.cpp` on main at `33499ef7` (lines 6419 to 7200). "Now" is where the item is today
(an id of `MenuBarModel.hpp`, or the function that draws it in `main.cpp`).

### Problems the owner named

| Problem | What it really was | What was done |
|---|---|---|
| "Examples" twice in File | **Three** submenus called Examples: one inline with a Node Playground list, one from `oked::DrawExamplesMenu` (UI Gallery), one inline with the Visual Script playground and the *same* Node Playground list | One `Examples` submenu: UI Gallery, UI Gallery (phone), Visual Script Node Playground, Node Playground (flow and variables). The repeated list is gone. `DrawExampleItems` (UIRuntimeTools.cpp) draws the UI Gallery items, so examples added there appear in the one menu. |
| "Layout" in more than one place | (1) the tab right-click menu, (2) the Layout dropdown at the top right, (3) View > Layout all call **one** function (`DrawLayoutMenuItems`); (4) GameObject > UI > "Layout" was a different thing (UI containers) | (1) to (3): kept, one implementation, three entry points. (4) renamed "Layout & Overlays". View also had a loose "Reset Layout" beside the Layout menu, which is also inside it: the loose one is removed. |
| Very long View menu | Panels, Editors & Tools, Rendering & Performance, UI backend, Scene Overlays, Appearance (4 more submenus), Camera Bookmarks, Layout, Reset Layout | Appearance moved to Edit > Preferences; the loose Reset Layout removed; Frame / Isolate / UI Editing Mode added in one tidy group; View now has 9 entries. |
| Items without shortcuts | Copy, Paste, Duplicate, Delete, Undo, Save etc. were mixed between GameObject and Edit; Edit had no clipboard | Edit holds the clipboard commands; every key that works is shown (a test checks that a shown key is a real key and appears once per menu). |
| Inconsistent names | "Open..." beside "Save Scene", "Create Sprite" vs "2D Shape", "Delete Selected" vs "Delete" | "Open Scene...", names inside the category menus drop "Create", Edit uses Cut / Copy / Paste / Duplicate / Delete. |
| Greyed items without a reason | Open Recent, Delete Layout, Go to slot N, Node Playground, Save Selected as UI, Save in Play | Each says why on hover (the model has a `reason` for every disabled item; a test fails if one is missing). |

### File

| Item | Shortcut | What it does | Old code | Verdict | Now |
|---|---|---|---|---|---|
| New Project... | | opens the New Project dialog | 6436 | keep, moved after Save | `file.newproject` |
| Open Project Folder... | | picks a project folder | 6438 | keep | `file.openproject` |
| Project Upgrade... | | upgrade or restore this project (ProjectUpgradeUI.cpp) | 6439 | keep | `file.upgrade` (slot) |
| New 2D Scene, New 3D Scene | | new scene after the unsaved-changes question | 6441 | keep, first | `file.new2d`, `file.new3d` |
| Examples (1st) > Node Playground | | opens a playground scene | 6443 | remove-duplicate | merged into `file.examples` |
| Import Model... | | OS file picker, then the import dialog | 6459 | keep | `file.import` |
| Open... | Ctrl+O | Open Scene dialog | 6461 | rename: Open Scene... | `file.open` |
| Examples (2nd) via `DrawExamplesMenu` | | UI Gallery, UI Gallery (phone) | 6462 | merge | `file.examples` |
| Open Recent > | | recent scenes with thumbnails, Clear | 6463 | keep; reason when empty | `file.recent` (slot) |
| Examples (3rd) | | Visual Script Node Playground >, Node Playground (flow and variables) > | 6483 | merge | `file.examples` |
| Save Scene | Ctrl+S | saves; the first save asks where | 6511 | keep; greyed in Play | `file.save` |
| Save Scene As... | Ctrl+Shift+S | save under a new name | 6514 | keep; greyed in Play | `file.saveas` |
| Save Project | Ctrl+Alt+S | saves scene, scripts, graphs, settings | 6515 | keep | `file.saveproject` |
| Merge Scene into Current... | | appends another scene | 6519 | keep | `file.merge` |
| Autosave > | | enable, interval, recovery copies | 6531 | keep | `file.autosave` (slot) |
| Build Settings... | Ctrl+B | the export window | 6546 | keep (also in Engine: removed there) | `file.build` |
| Exit | | quit | 6548 | keep | `file.exit` |

### Edit

| Item | Shortcut | What it does | Old code | Verdict | Now |
|---|---|---|---|---|---|
| Undo <what>, Redo <what> | Ctrl+Z, Ctrl+Y | scene or mesh undo, named | 6561, 6568 | keep | `edit.undo`, `edit.redo` |
| Project Settings... | | project settings window | 6579 | keep | `edit.projectsettings` |
| (missing) Cut, Copy, Paste, Duplicate, Delete | the keys worked | the commands lived in GameObject (Copy, Paste, Duplicate Selected, Delete Selected) and Hierarchy | | move | `edit.cut/copy/paste/duplicate/delete` |

### View

| Item | What it does | Old code | Verdict | Now |
|---|---|---|---|---|
| Panels > (19 toggles) | show or hide a panel | 6584 | keep | `view.panels` |
| Editors & Tools > (17 toggles; UI Editing Mode among them) | show or hide a tool window | 6610 | keep; UI Editing Mode moved to View | `view.tools`, `view.uionly` |
| Rendering & Performance > | anti-aliasing, renderer status, UI backend (developer only), VSync, Smooth frame time, Auto performance | 6645 | keep | `view.rendering` (slot) |
| Scene Overlays > | colliders, volumes, physics debug, skeletons, gizmos, skybox, clear console on Play | 6706 | keep | `view.overlays` (slot) |
| Appearance > UI Scale, Density, Theme, Accent color | editor look | 6730 | move | Edit > `edit.preferences` |
| Camera Bookmarks > | save and go to 3 camera slots | 6768 | keep; reason on empty slots | `view.bookmarks` (slot) |
| Layout > | presets, saved layouts, side tabs, maximize | 6791 | keep | `view.layout` (slot) |
| Reset Layout (loose) | back to the default layout | 6792 | remove-duplicate (inside Layout) | `view.layout` |

### Window, Engine, Plugins

| Item | Old code | Verdict | Now |
|---|---|---|---|
| Window > Items, Loot & Recipes | 6795 | remove-duplicate (View > Editors & Tools has it) | `tool.items`; the Window menu is gone |
| Engine > Build Settings... (Ctrl+B) | 7181 | remove-duplicate (File has it) | `file.build` |
| Engine > Check for Updates... | 7183 | move | Help `help.updates` |
| Plugins | 7193 | keep, unchanged (drawn by `DrawPluginsMenu`) | `plugins` (host drawn) |

### GameObject

| Item | What it does | Old code | Verdict | Now |
|---|---|---|---|---|
| Create Empty | new empty object | 6802 | keep | `go.empty` |
| Create Sprite | new sprite | 6803 | rename, move to 2D Object | `create.2d` |
| 2D Character (Sprite Sheet)... | sprite-sheet dialog | 6804 | move | `create.2d` |
| 2D Shape > | premade shapes saved to Assets/Sprites | 6805 | move | `create.2d` |
| Sprite Editor... | opens the sprite editor | 6813 | remove-duplicate (same window as View > Editors & Tools > Sprite Editor) | `tool.spriteeditor` |
| Create Camera | new camera | 6814 | rename, move | `create.camera` |
| Player > (9) | ready-made players | 6816 | move to the category list | `create.player` |
| 3D Object > | primitives, Nature, Props, Structures, Terrain, Voxel Terrain, Water, Character | 6866 | keep | `create.3d` |
| Light > | Directional, Point, Spot | 6946 | keep | `create.light` |
| Cinemachine > | Virtual Camera, Brain, Dolly Path, Dolly Cart | 6968 | move into Camera | `create.camera` |
| Create Particle System | new particles | 7005 | rename, move to Effects | `create.effects` |
| Spline | a three-point curve | 7011 | move to Effects (its tooltip was on Cable: fixed) | `create.effects` |
| Foliage Layer, Decal Projector, Cable | `LevelCreateMenuItems` | 7012 | move to Effects | `create.effects` |
| Volume > | draw or create volumes | 7014 | move to Effects | `create.effects` |
| Create Tilemap | new tilemap | 7015 | rename, move to 2D Object | `create.2d` |
| UI > | every UI element, prefab screens, inventories | 7024 | keep | `create.ui` |
| Instantiate Prefab... | opens the prefab picker | 7151 | keep | `go.instantiate` |
| Duplicate Selected | Ctrl+D | 7152 | move to Edit | `edit.duplicate` |
| Array Duplicate... | copies in a row, grid or ring | 7155 | keep | `go.arraydup` |
| Snap Position to Grid | rounds the position | 7157 | keep | `go.snap` |
| Copy, Paste | Ctrl+C, Ctrl+V | 7164, 7168 | move to Edit | `edit.copy`, `edit.paste` |
| Delete Selected | Del | 7175 | move to Edit | `edit.delete` |

### Help

| Item | Old code | Verdict | Now |
|---|---|---|---|
| Scripting Reference | 7195 | keep | `help.scripting` |
| Keyboard Shortcuts | 7196 | keep | `help.shortcuts` |
| About OkaySpace | 7197 | keep | `help.about` |

### Existing / Gap for what was added to the menu bar

| New item | Existing | Gap |
|---|---|---|
| Edit > Cut, Copy, Paste, Duplicate, Delete | the keys (`HandleShortcuts`), and copies of Copy / Paste / Duplicate / Delete in GameObject | the Edit menu had none; Cut was only a key and a Hierarchy item |
| Edit > Select All, Deselect | Hierarchy empty-space menu | no menu-bar entry |
| Edit > Preferences | View > Appearance | moved here as the owner's list asks |
| View > Frame Selected, Isolate Selection | the F and Shift+I keys, and the Hierarchy item | not in any menu bar |
| GameObject > Create Empty Child / Parent | Hierarchy items "Create Empty Child", "Create Parent" | not in the menu bar |
| GameObject > Audio > Audio Source, Music Player | Inspector > Add Component > Audio has both (the same line builds the object) | no way to create a sound object from the menu |
| Help > Documentation (online), Report a Bug... | About > GitHub button (`extide::RevealInFiles`); the Crash Log panel | no direct route to the docs or the issue page |

## 3b. Screenshots (the real editor, run on Linux)

These are screenshots of the **real editor** built from this branch on Linux (`okay-editor`, engine at `-O1`, `editor/main.cpp` at `-O0` to fit
in memory, no `NDEBUG`, so Dear ImGui's asserts were live), started with `--dev` (skips the launcher gate) from a project made with
`--new-project ... --template "Third Person"`, in a virtual display (Xvfb, 1600x900) with the software renderer, the mouse driven by `xdotool`.
They are not from Windows. Cropped to the editor window. The folder is `docs/images/editor_menus/`.

| Screenshot | What it shows |
|---|---|
| [file.png](images/editor_menus/file.png) | File: the groups and the right-aligned keys |
| [file_examples.png](images/editor_menus/file_examples.png) | File > Examples: the one merged submenu (the tooltip is the UI Gallery's; "Node Playground" is greyed because no playground scenes sit next to this build) |
| [file_play_save_greyed.png](images/editor_menus/file_play_save_greyed.png) | File while the game runs (a project from the "3D Scene" template): Save Scene and Save Scene As greyed, the reason on hover |
| [edit_undo_named.png](images/editor_menus/edit_undo_named.png) | Edit after Create Child: "Undo Create Hemisphere", Paste greyed (nothing copied) |
| [gameobject.png](images/editor_menus/gameobject.png) | GameObject: Create Empty / Child / Parent, then the eight categories |
| [gameobject_audio_source_created.png](images/editor_menus/gameobject_audio_source_created.png) | After GameObject > Audio > Audio Source: the object is in the Hierarchy and selected, its component in the Inspector |
| [view.png](images/editor_menus/view.png) | View: nine entries |
| [help.png](images/editor_menus/help.png) | Help |
| [hierarchy_row_menu.png](images/editor_menus/hierarchy_row_menu.png) | Hierarchy right-click on the Player (it has children) |
| [hierarchy_create_child.png](images/editor_menus/hierarchy_create_child.png) | Create Child > 3D Object (the list is shifted up to fit the screen) |
| [hierarchy_create_child_result.png](images/editor_menus/hierarchy_create_child_result.png) | The click on "Hemisphere": it is now a child of the Player (Player (2)); Edit > Undo took it away again |
| [hierarchy_three_selected.png](images/editor_menus/hierarchy_three_selected.png) | Three crates selected: "Duplicate 3 Objects", "Delete 3 Objects", "Group 3 Objects", Cut greyed |
| [hierarchy_same_type_reason.png](images/editor_menus/hierarchy_same_type_reason.png) | Select > Same Type (Light), greyed: "No other Light in the scene." |
| [hierarchy_add_component.png](images/editor_menus/hierarchy_add_component.png) | Add Component... on the Ground: the Inspector's own browser opened |
| [hierarchy_empty_space.png](images/editor_menus/hierarchy_empty_space.png) | Right-click on the empty space |

What was clicked in that run, and seen to work: every top menu opened; File > Examples; Create Audio Source; Create Child > 3D Object >
Hemisphere (a child of the Player) and then Edit > Undo; Edit > Cut then Edit > Paste; the row menu's Delete; the row menu with three selected;
Select > Same Type on a Light (greyed) and on a mesh (selected the 8 meshes); Add Component... (the Inspector's browser opened);
Rig & IK > Add IK Target; GameObject > Create Empty Parent, then Create Empty Child; View > Isolate Selection and the label turning into
Exit Isolation; View > Panels (a check mark per open panel; clicking Problems closed that tab); View > Layout > Delete Layout hover reason;
Camera Bookmarks "Go to slot 1" hover reason; the Play button, then File and Edit while the game ran; the [+] button; the empty-space menu; Help
> Report a Bug... (no crash; there is no browser here, so the page and the clipboard line were not seen).
**Not clicked:** the items under Open Recent, Autosave, Import Model, Merge Scene, Build Settings, New Project; Edit > Preferences items; the
other Create items (2D, Player, Light, Camera, Effects, UI); Instantiate Prefab, Array Duplicate, Snap; Help > Documentation, Check for
Updates, Keyboard Shortcuts, About, Scripting Reference; the prefab menu (no prefab in the test project); Parent To, Order, Transform
and the Expand/Collapse items; Paste as Child; Open Recent greyed with no history (the test project always had its scene in the list).

## 4. How it is built and tested

* `editor/MenuBarModel.hpp` (header only, no ImGui): `Entry` (kind, id, label, shortcut, tip, reason, enabled, children), the builders
  `FileMenu`, `EditMenu`, `GameObjectMenu`, `ViewMenu`, `HelpMenu`, the shared `CreateCategories()`, the list of keys that work
  (`KnownShortcuts()`), and `State` (the plain values a menu needs: playing, selection count, undo names, clipboard, ...).
* `editor/HierarchyContextModel.hpp` (header only): `RowMenu(RowState)`, `EmptyMenu(EmptyState)`, `CreatePopup(EmptyState)`.
* `editor/MenuEntriesUI.hpp` (ImGui, header only): `DrawEntries(entries, host)`: draws items, toggles, submenus, the reason or tip on
  hover. The host (main.cpp) runs the ids.
* `editor/main.cpp`: `DrawMainMenus`, `RunMenuAction`, `MenuFlag` (toggle id to flag), `DrawMenuSlot` and `DrawCreateSlot` (the parts
  that depend on live data and the creators, moved out of the old inline code unchanged), `HierRowAction`, `HierEmptyAction`.
* Tests: `tests/test_menu_bar_model.cpp` (ctest `menu_bar_model`) and `tests/test_hierarchy_context_model.cpp`
  (`hierarchy_context_model`). They check the order, that nothing is listed twice (the three Examples, the double Reset Layout and Items
  Loot & Recipes are reproduced as negative controls), that every disabled item has a reason, that every shown key is a working key,
  plural labels, enable rules for every state, that every item of the old menus still has a home (a table of old item to new id), and
  **wiring**: every id a menu can produce has a handler in `editor/*.cpp` (the test reads the sources), so an entry cannot be a dead
  click. It does not run the handlers; those are checked by clicking (see section 5).

To add a menu item: add an `Item(...)` in the right builder in `MenuBarModel.hpp`, run its id in `RunMenuAction`, and re-run the
test; a missing reason, a duplicate label or a shortcut that is not in `KnownShortcuts()` fails it. A window toggle also needs its flag
in `MenuFlag`.

## 5. Found, not done

* **Ctrl+X with several objects selected deletes all of them but puts only one on the clipboard** (`HandleShortcuts`: it serializes
  `ed.selected()` and then calls `DeleteSelected()`). The menus (Edit > Cut and the Hierarchy Cut) are greyed with several selected, so they
  cannot lose anything; the key itself was not changed here (it is in Claude 2's lane). Needs a test and a fix (copy all, or refuse).
* Copy puts one object on the clipboard (the clicked or active one) even with several selected.
* The menus cannot be driven from a test: a mistake in `RunMenuAction` (what a click does) is only caught by clicking. Those
  actions are the old code moved, but see the status in [try_it.md](try_it.md).
* Create Child keeps the local transform, so a new child sits at its parent's origin (the old Create Child did the same); UI pieces
  go under the Canvas, or inside the selected UI panel, not under the clicked row.
* The OkayUI port covers the Console, Crash Log and Audio Mixer panels only; no menu bar or Hierarchy code has an OkayUI twin, so nothing
  had to be kept in sync.
