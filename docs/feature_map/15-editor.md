# 15. Editor

The OkayEngine editor (Dear ImGui on SDL): docked panels, Scene and Game views, Inspector, Project panel, many tool windows, Play mode and Build Game. About 65k lines in `editor/`, of which `editor/main.cpp` is still about 45k; the split-out panel files hold the rest.
`editor/vendor` (the vendored text editor widget) is out of scope. Only the headless logic (`EditorState`, tabs, game fiber, script text, graph model) has unit tests; the drawing code is checked by the CI `--selftest` run and by hand.
Line ranges for `editor/main.cpp` are approximate (as of 2026-11-08) and move whenever the file is edited. Animation windows belong to Grok's animation sessions; leave them alone. PR #577 (a Cursor session, merged for 2.851.0) redesigned the Visual Script and Script Editor layouts and gave the whole editor a roomier layout; it edited `editor/main.cpp`, `editor/VisualScriptEditor.hpp`, `editor/ConsolePanel.cpp` and `editor/vendor/TextEditor.*` and added `editor/VsCanvasTools.hpp` (14.1.9, 13.5, 15.9.7). Draft PR #584 (updater restart after install, from the same kind of session) touches `editor/main.cpp`, `launcher/main.cpp` and adds `shared/AppRestart.hpp`; it is not on main.

## 15. Editor

### 15.1 Hierarchy
- **What:** The tree of every object in the scene with search, drag and drop re-parenting, rename, and a large right-click menu.
- **Parts:**
  - 15.1.1 Tree, search and selection - `editor/main.cpp` (`DrawHierarchy` ~17259-18391) - rows with icons, expand and collapse, multi-select, flat search list that matches name or type ("camera", "light", "ui")
  - 15.1.2 Object context menu - `editor/main.cpp` (~17259-18391) - Cut, Copy, Paste as Sibling or Child, Duplicate, Group, Focus, Delete, Order, Create Child or Parent, Parent To, Select (children, below, parent, siblings, same name), Transform resets, Copy Text, Save As Prefab
  - 15.1.3 Empty-space context menu - `editor/main.cpp` - create empty, sprite, cube, camera, light, 3D objects and UI widgets, Select All, Deselect, Expand/Collapse All, Sort A-Z
  - 15.1.4 Rig and character entries - `editor/main.cpp` - Use Built-in Animations, Map Humanoid Bones, Rig Model (Humanoid), Add IK Target on model roots
  - 15.1.5 Scene merge drops - `editor/main.cpp` - drop a `.okayscene` on the Hierarchy to merge it into the open scene
- **Code:** `editor/main.cpp` (`DrawHierarchy`), `editor/EditorState.*`
- **API:** menu View > Panels > Hierarchy; shortcuts for rename, duplicate, delete, focus
- **Editor:** Hierarchy panel
- **Docs:** `docs/editor.md` (Editor quality-of-life, Hierarchy right-click)
- **Tests:** `editor` (selftest)
- **Status:** Works - used by every session; the GUI is not unit-tested
- **Open:** polish (carried from the earlier map); #575 (scene hierarchy, search and validation) is engine-side (hierarchy-aware active state, FindByPath, Validate) and the Hierarchy panel does not call `Validate` or `FindByPath` yet; a pinned-object feature named in the old A-Z index is not found by name in the Hierarchy code
- **Related:** 15.2, 1.3, 1.6, 21
- **Words:** Hierarchy, hierarchy panel, pin (Hierarchy), scene tree, parenting, drag to parent, group, prefab save, order, sort, search, filter
- **Seen as:** an object is missing from the tree; dragging does not re-parent; the context menu lacks an entry; the search shows nothing; selection jumps
- **Ask me like:** "work on 15.1.1, hide and lock toggles in the Hierarchy"; "Paste as Child puts the object at the wrong place"

### 15.2 Inspector, Add Component menu, Character Model panel
- **What:** The panel that edits the selected object: name, tag, static flag, and one collapsible card per component, an Add Component menu with search, and the character model setup panel.
- **Parts:**
  - 15.2.1 Inspector shell - `editor/main.cpp` (`DrawInspector` ~27452-37264) - object header, per-component cards, enable checkbox, Remove, multi-edit
  - 15.2.2 Component headers and tints - `editor/main.cpp` (`CompHeader` ~25416), `editor/EditorWidgets.*` - category tint and icon per component, accent colour, icon and search buttons
  - 15.2.3 Add Component menu - `editor/main.cpp` (~36708) - categories (Rendering, Animation, Physics, Lighting, Camera, Scripts, Audio, Gameplay, UI) with search; Scripts lists project `.okay` files and New Script
  - 15.2.4 Component inspectors in main - `editor/main.cpp` - about 175 component headers by name (Sprite Renderer ~28032, Mesh Renderer ~28259, Character ~29603, Camera ~30006, controllers ~33446-34050, UI widgets ~34698-36498, Script ~31140, Visual Script ~28850)
  - 15.2.5 Inspectors in their own files - `editor/NavigationWindow.*`, `editor/BehaviorTreeWindow.*`, `editor/DialogueWindow.*`, `editor/SplineTools.*`, `editor/UIInspectorLayout.*`, `editor/UIInspectorOverlay.*` - split-out inspector bodies called from the main loop
  - 15.2.6 Character Model panel - `editor/main.cpp` (`CharacterModelPanel` ~7269-7390, auto-rig ~7391-7642) - swap model by drag and drop, Rig Model onto the humanoid skeleton
  - 15.2.7 Shared pickers and fields - `editor/main.cpp` (~24195-25415) - object, asset, variable and tag pickers, key bind combo, tween preview, particle gradient and curve editors
- **Code:** `editor/main.cpp`, `editor/EditorWidgets.*`, `editor/UIInspectorLayout.*`, `editor/UIInspectorOverlay.*`
- **API:** View > Panels > Inspector; Add Component button under the component list
- **Editor:** Inspector panel
- **Docs:** `docs/editor.md` (Add Component, Component headers)
- **Tests:** `editor` (selftest), `char_editor_parts` (editor-built part rig of a Character)
- **Status:** Works - one header per component type exists; the 9.8k-line function is the biggest part of `main.cpp`
- **Open:** polish (carried from the earlier map); `DrawInspector` is one 9.8k-line function (a splitting candidate, 15.10); nothing found yet for editing component values during Play and keeping them (planned area 34); prefab overrides are shown and edited in the Inspector header, per property and in the Hierarchy (area 21)
- **Related:** 15.10, 12.10, 13.4, 14.1, 21, 34
- **Words:** Inspector, Add Component, component header, enable checkbox, Remove Component, tag, static, Character Model panel, Rig Model, script fields, component search
- **Seen as:** a component has no card or a field is missing; Add Component search cannot find a component; values reset after editing; the Inspector is slow with many components
- **Ask me like:** "work on 15.2.4, add Foot IK weights to the Inspector"; "Add Component should show recently used components"

### 15.3 Scene and Game views, gizmos, spline tools
- **What:** The two views of the world: the Scene view (2D and 3D editing, selection, gizmos, overlays) and the Game view (what the player sees, with resolution and phone presets), plus the spline editing overlay.
- **Parts:**
  - 15.3.1 Scene view shell and toolbar - `editor/main.cpp` (`DrawViewport` ~41508-42171) - 2D/3D switch, UI-only toggle, tool icons, global/local, snap, frame selected, isolate, "..." menu, Overlays menu
  - 15.3.2 2D scene drawing - `editor/main.cpp` (`DrawScene2D` ~38643-39018) - sprites, tilemaps, colliders, selection in 2D
  - 15.3.3 3D scene drawing - `editor/main.cpp` (`DrawScene3D` ~39019-41397) - mesh rendering through the editor camera, selection, picking
  - 15.3.4 Transform gizmo - `editor/main.cpp` (~41023-41265) - Move, Rotate, Scale axis handles, vertex snapping with V (~41266)
  - 15.3.5 Collider and terrain tools in the view - `editor/main.cpp` (~40947 collider face handles, ~40105 terrain sculpt brush, ~40157 mesh edit mode)
  - 15.3.6 View axis gizmo and camera bookmarks - `editor/main.cpp` - top-right axis tips snap the view; View > Appearance > Camera Bookmarks
  - 15.3.7 Scene overlays - `editor/main.cpp`, `editor/NavigationWindow.*` - colliders, physics debug, skeletons, navmesh polygons and agent paths
  - 15.3.8 Spline tools - `editor/SplineTools.*` - draw every spline, edit points and handles, insert, delete, snap to ground; inspectors for spline, follower, spline mesh, instancer, line and trail; GameObject > Spline
  - 15.3.9 Game view - `editor/main.cpp` (`DrawGameView` ~42926-43174) - runs the game image, Resolution presets, Safe area, Stats overlay, Capture mouse, Physics toggle
  - 15.3.10 Mobile emulator and shared game resolution - `editor/main.cpp` (~41398-41507, ~42845-42925) - phone and tablet frames shared by Game view and UI editing
  - 15.3.11 Play mode - `editor/EditorState.*`, `editor/main.cpp` - Play snapshots the scene and Stop restores it; mouse capture rules; Error Pause
  - 15.3.12 Orthographic views - `editor/LevelTools.*` - View options > Perspective / Orthographic and Top, Bottom, Front, Back, Right, Left (numpad 7, 1, 3 with Ctrl for the opposite, numpad 5 switches projection); a true orthographic projection of the Scene view, zoomed with the wheel; a label names the view
- **Code:** `editor/main.cpp`, `editor/SplineTools.*`, `editor/NavigationWindow.*`, `editor/EditorState.*`, `editor/UIPreviewPainter.*`
- **API:** View > Panels > Game; Scene toolbar; GameObject > Spline
- **Editor:** Scene panel, Game panel, UI panel (12.10)
- **Docs:** `docs/editor.md` (Scene & Game view extras), `docs/splines.md`, `docs/web_mobile.md`
- **Tests:** `editor` (selftest, play and stop in `EditorState`), `splines` (engine side)
- **Status:** Works - used daily; only the non-GUI play and snapshot logic is tested
- **Open:** world-space canvases show only in the Game view (12.6); Scene view 3D render does a GPU-to-CPU readback each frame (the code comments call it the biggest per-frame cost); live editing during Play is planned (area 34)
- **Related:** 15.9, 12.10, 2.1, 2.7, 6.5
- **Words:** Scene view, Game view, gizmo, move rotate scale, snap, frame selected, 2D 3D toggle, view cube, axis gizmo, Play, Stop, Capture mouse, resolution, Stats overlay, spline editing, collider handles, orthographic, top view, front view, side view, numpad view, measure tool
- **Seen as:** gizmo handles are hard to hit or move the wrong axis; the Game view differs from the built game; Play does not restore the scene; the mouse is stuck in Play; low FPS in the editor
- **Ask me like:** "work on 15.3.4, a pivot-to-centre toggle for the gizmo"; "the Game view stats overlay is wrong"

### 15.4 Project panel and asset manager
- **What:** The file browser of the project's Assets folder (folders, thumbnails, smart filters, search, color labels, drag and drop into the scene) and a window that imports marketplace downloads into the project.
- **Parts:**
  - 15.4.1 Project panel - `editor/main.cpp` (`DrawProject`), `editor/ProjectBrowser.*` - folder tree, list and grid, search (`t:model`, `ext:png`, `l:tag`, `is:unused`, `is:broken`, `uses:`, `deps:`), smart filters (All Models, Recently Changed, Unused Assets), multi-select; the listing, counts, folders and dependency lookups come from `AssetDatabase`
  - 15.4.2 Asset icons and thumbnails - `editor/main.cpp`, `editor/ProjectBrowser.*` - drawn icons per asset kind while a picture is not made yet; pictures from `ThumbnailCache` (textures, audio waveforms, models, meshes, materials, prefabs, scenes), tile size Small / Medium / Large and a slider, grid or list
  - 15.4.3 Asset color labels - `editor/main.cpp` (~8542-8589) - per-project labels stored in `.okaylabels`
  - 15.4.4 Import Model - `editor/main.cpp` (~2407-2512) - OS file picker first, then a settings dialog
  - 15.4.5 Asset Manager window - `editor/main.cpp` (`DrawAssetManager` ~7643-7935) - lists marketplace downloads by type (sidecar `.okayasset`) and imports them into Assets
  - 15.4.6 Opening assets - `editor/main.cpp` - double-click routes to Script Editor, Material, Data Asset, Behavior Tree, Dialogue, Timeline, Localization windows
- **Code:** `editor/main.cpp`
- **API:** View > Panels > Project, View > Panels > Asset Manager, File > Import Model
- **Editor:** Project panel, Asset Manager window
- **Docs:** `docs/editor.md` (Drag & drop from Project)
- **Tests:** `assetmanager`, `asset_database`, `project_panel`, `project_panel_db`
- **Status:** Works - the panel reads from the asset database and thumbnail cache; the window itself is only checked by running the editor (screenshots), the model has tests
- **Open:** colour labels are still the editor's own list (`.okaylabels`), separate from the tags in `.okaymeta`; changes made outside the editor show up on the next poll (2 s or more), not at once; thumbnails are made on the main thread within a frame budget
- **Related:** 20, 3.6, 1.6, 17.6, 15.2
- **Words:** Project panel, assets, Assets folder, import, asset browser, asset manager, marketplace downloads, thumbnails, labels, unused assets, drag from project
- **Seen as:** an asset does not show or has the wrong icon; import fails; dragging into the scene does nothing; the panel is slow on a big project; marketplace download is not listed
- **Ask me like:** "work on 15.4.1, show GUIDs and dependencies in the Project panel"; "unused assets list is wrong"

### 15.5 Animator, Animation tab, Timeline
- **What:** The animation tools: the Animator state-machine graph, the Animation tab (clips, dope sheet, curves, record), the cutscene Timeline, and the first-person hands preview. Owned by Grok's animation sessions; describe, do not edit.
- **Parts:**
  - 15.5.1 Animator window - `editor/AnimatorWindow.*` - state graph canvas, transitions, parameters, layers with bone masks, problems list, live state in Play
  - 15.5.2 Animator preview - `editor/AnimatorPreview.hpp` - one live preview at a time in the Scene view, restores the pose on stop
  - 15.5.3 Animation tab - `editor/main.cpp` (~19492-22983, `DrawAnimationEditor` ~21803) - clip list, dope sheet, curve editor, record mode, onion skin; Character clips (~19869), imported-model clips (~21360), object timeline (~21978)
  - 15.5.4 Timeline window - `editor/TimelineWindow.*` - `.okayseq` cutscene editor: tracks, shots, keys, cues, live preview in Scene and Game views, record keys, play through a Sequence Player
  - 15.5.5 Shared timeline widgets - `editor/MainShared.hpp` - transport, ruler, playhead and key widgets used by the Animation tab and the Timeline
  - 15.5.6 First Person Hands window - `editor/FpHandsWindow.*` - preview window for the first-person hands (4.7)
  - 15.5.7 Humanoid bone map window - `editor/main.cpp` (`DrawBoneMap` ~8414-8478) - map a model's bones to the humanoid skeleton
- **Code:** `editor/AnimatorWindow.*`, `editor/AnimatorPreview.hpp`, `editor/TimelineWindow.*`, `editor/FpHandsWindow.*`, `editor/MainShared.hpp`, `editor/main.cpp`
- **API:** View > Editors & Tools > Animation, Animator (state graph), Timeline (cutscenes)
- **Editor:** Animation tab, Animator window, Timeline window
- **Docs:** `docs/animation.md`, `docs/cutscenes.md`, `docs/editor.md`
- **Tests:** `animator_edit`, `anim_edit` (clip editing tools on AnimClip, engine side), `sequence` (engine side); the windows themselves are untested
- **Status:** Works - windows exist and run; the animation PRs are in Grok's lane (in flight, #554 and others)
- **Open:** possible overlap: the Animation tab (~3.5k lines in `main.cpp`) and `AnimatorWindow` both edit state machines or clips; splitting the Animation tab out is part of 15.10; Grok's pending PRs may touch these files
- **Related:** 5.1, 5.2, 10.1, 15.10, 4.7
- **Words:** Animator window, state machine graph, Animation tab, dope sheet, curves, record, onion skin, Timeline, cutscene editor, sequence, keyframes, bone mask, FP hands preview
- **Seen as:** a state or transition cannot be created; the preview does not restore the pose; Timeline playback differs from Play; the Animation tab loses a key
- **Ask me like:** "work on 15.5.4, add an audio waveform to the Timeline"; "which file has the Animator window"

### 15.6 Navigation, Behavior Tree, Dialogue, Localization, Modeling windows
- **What:** The editor windows for world-building and content systems: navigation bake, behavior trees, dialogue and quests, the string table, and the 3D modeling and UV tools.
- **Parts:**
  - 15.6.1 Navigation window - `editor/NavigationWindow.*` - bake settings, Bake and Clear with progress, Scene overlays (polygons, agent paths), send-agent mode, inspectors and Add Component rows for NavMesh components, bake before Play
  - 15.6.2 Behavior Tree window - `editor/BehaviorTreeWindow.*` - `.okaybt` canvas, node palette, node inspector, blackboard, validation, live view in Play, Runner inspector
  - 15.6.3 Dialogue and Quests window - `editor/DialogueWindow.*` - `.okaydialog` graph editor with Test panel, Quests tab for `.okayquest`, inspectors for Dialogue Runner, Quest Log, Quest Trigger, Story State
  - 15.6.4 Localization window - `editor/LocalizationWindow.*` - string table as keys by languages, completeness, CSV import and export, pseudo-localized preview, Find in Scene, toolbar language preview, Localized inspector options, CJK font merge, Build Game copy
  - 15.6.5 Modeling window - `editor/main.cpp` (`DrawModeling` ~26150-27440, edit mode helpers ~3021-3433) - mesh edit tools (area 3)
  - 15.6.6 UV Editor - `editor/main.cpp` (`DrawUVEditor` ~25876-26149) - UV layout tools (3.4)
- **Code:** `editor/NavigationWindow.*`, `editor/BehaviorTreeWindow.*`, `editor/DialogueWindow.*`, `editor/LocalizationWindow.*`, `editor/main.cpp`
- **API:** View > Editors & Tools > Navigation, Dialogue & Quests, Behavior Tree, Modeling, UV Editor, Localization
- **Editor:** each window listed above
- **Docs:** `docs/navigation.md`, `docs/behavior_trees.md`, `docs/dialogue_quests.md`, `docs/localization.md`, `docs/modeling.md`, `docs/editor.md`
- **Tests:** `editor` (selftest only); engine side: `navmesh`, `behavior_tree`, `dialogue`, `quests`, `localization`, `meshedit`
- **Status:** Works - each window has its own file or function; none has a unit test
- **Open:** Modeling and UV Editor still live in `main.cpp`. The Localization window calls the translation grid, the missing-key and placeholder report, the project scan and the pseudo-locale preview (31.1, 31.3).
- **Related:** 9.1, 9.4, 10.2, 10.3, 1.8, 3.1, 31
- **Words:** Navigation window, bake navmesh, Behavior Tree window, Dialogue window, Quests tab, Localization window, string table, Modeling window, UV Editor
- **Seen as:** the navmesh does not bake or shows wrong polygons; a tree node cannot be wired; the dialogue Test panel does not match Play; a language preview shows keys; CJK text shows boxes
- **Ask me like:** "work on 15.6.4, missing-key report in the Localization window"; "the behavior tree window does not show the live node"

### 15.7 Build Settings
- **What:** File > Build Settings (Ctrl+B): export the current scenes as a standalone game for Windows, Web, Android or a dedicated server, with named build profiles.
- **Parts:**
  - 15.7.1 Build Settings dialog - `editor/main.cpp` (~15948-16650) - name, output folder, platform buttons, scenes list, Build and Build and Run, checks every visual script first and stops on errors
  - 15.7.2 Build Game export - `editor/main.cpp` (~3434-3970) - serializes scenes, copies the player runtime and the assets the scenes reference, writes the data folder
  - 15.7.3 Build profiles - `editor/main.cpp` (`BuildSettings` ~2513-2700) - save and switch named configurations
  - 15.7.4 Dedicated server export - `editor/main.cpp` (~3971-4272) - game with no window plus `server.cfg` and deploy scripts
  - 15.7.5 Web and Android export - `editor/main.cpp` - writes a ready-to-build project rather than a binary
  - 15.7.6 Anti-cheat opt-in - `editor/main.cpp` - Build Game writes the integrity manifest when enabled
  - 15.7.7 Scene build list - `editor/SceneManagerPanel.*` - the "Scenes In Build" list the dialog edits (15.12)
- **Code:** `editor/main.cpp`, `editor/SceneManagerPanel.*`
- **API:** File > Build Settings... (Ctrl+B); toolbar Build button
- **Editor:** Build Settings modal
- **Docs:** `docs/packaging.md`, `docs/editor.md` (Building a standalone game), `docs/web_mobile.md`, `docs/dedicated-server.md`
- **Tests:** none for the dialog
- **Status:** Works - platforms 0 Desktop, 1 Web, 2 Android, 3 Dedicated server per the code
- **Open:** the Android and Web targets export a project that still needs outside tools (Emscripten, NDK) per `docs/editor.md`; about 1.5k lines of build code sit in `main.cpp`
- **Related:** 18.1, 18.2, 16.5, 14.2, 25
- **Words:** Build Settings, Build Game, export, standalone game, player runtime, dedicated server export, build profile, Ctrl+B, Web build, Android build
- **Seen as:** a build is missing an asset; the exe does not start; the build stops with a visual script error; the server export misses a file
- **Ask me like:** "work on 15.7.2, only copy used assets"; "the web export is missing my fonts"

### 15.8 Plugin system and Plugins menu
- **What:** Editor plugins written in OkayScript: a file in `<project>/EditorPlugins/` or the editor's `plugins/` folder can add a panel, a menu command, and react to selection, through an `editor_*` API.
- **Parts:**
  - 15.8.1 Plugin host - `editor/main.cpp` (~7936-8288) - plugin list, loading, hot reload on save, `// @name` headers, enable and disable, plugin panel bridge
  - 15.8.2 Editor API - `editor/main.cpp` (`RegisterPluginBuiltins` ~7991) - about 25 `editor_*` builtins (selection, create, rename, parent, transforms, undo point, log, input, heading, hint)
  - 15.8.3 Plugins menu - `editor/main.cpp` (`DrawPluginsMenu` ~8289-8405) - project plugins first, then global ones, error marks, Turned off list, New Plugin
  - 15.8.4 Plugin Manager - `editor/main.cpp` (`DrawPluginManager` ~8479-8541) - errors, turn off, open folder
  - 15.8.5 Examples - `examples/editor-plugins` - `AlignTools.okay` and `CleanupNames.okay`
- **Code:** `editor/main.cpp`, `examples/editor-plugins/AlignTools.okay`, `examples/editor-plugins/CleanupNames.okay`, `Scripting/OkayScriptVM`
- **API:** hooks `start`, `on_gui`, `on_menu`, `on_selection`, `on_tick`; `editor_*` functions; `OkayScriptVM::AddBuiltin`
- **Editor:** Plugins menu, Plugins > Plugin Manager..., Plugins > New Plugin...
- **Docs:** `docs/editor-plugins.md`
- **Tests:** `editor_plugins`
- **Status:** Works - plugins load, show panels and commands; the Plugins menu usability item from the earlier map was merged in #479
- **Open:** `editor_plugins` runs the examples against its own fake editor, so the real `RegisterPluginBuiltins` is not under test (possible drift); the planned C++ plugin ABI, editor extension points and custom nodes are area 24
- **Related:** 24, 13.1, 40.4, 14.4
- **Words:** plugins, Plugins menu, editor plugin, EditorPlugins, Plugin Manager, editor_ API, on_gui, on_menu, AlignTools
- **Seen as:** a plugin does not load or shows "(error)"; a plugin panel does not draw; two plugins with the same name clash; reload on save does nothing
- **Ask me like:** "work on 15.8.2, add editor_find_by_tag"; "my plugin shows an error in the menu"

### 15.9 Layout, undo, autosave
- **What:** The editor's state and shell behaviours: panel tabs and layouts, undo and redo, autosave with a recovery prompt, and the non-GUI editor model.
- **Parts:**
  - 15.9.1 Editor model - `editor/EditorState.*` - scene, selection, create helpers, Play and Stop with snapshot restore, editor camera; free of ImGui so tests can use it
  - 15.9.2 Undo and redo - `editor/EditorState.*`, `editor/SceneUndo.hpp`, `editor/EditUndo.hpp` - scene edits are `SceneDeltaCommand`s on an `UndoStack` (text delta, selection by GUID, up to 100 steps, drags and repeated edits of one widget merged, named steps); Edit menu shows "Undo <name>" and greys out; History panel lists and jumps to steps (`DrawHistory`); no recording and no undo during Play; New / Open start an empty history; saved state follows undo
  - 15.9.3 Autosave and recovery - `editor/main.cpp`, `editor/DocRecovery.hpp` - recovery-folder copies of dirty graphs, scripts and the scene; a newer leftover `<scene>.autosave` is still offered and is not written again
  - 15.9.4 Panel tabs and layouts - `editor/EditorTabs.hpp`, `editor/main.cpp` (~4337-5388) - saved named layouts, presets (Default, Tall, Wide, 2 by 3, Scripting, Animation), Ctrl+Tab switcher, tab overflow, side tabs
  - 15.9.5 Edit-mode undo for meshes - `editor/main.cpp` (~3021) - undo inside mesh edit mode
  - 15.9.6 Focus-aware undo - `editor/LocalizationWindow.*`, `editor/BehaviorTreeWindow.*`, `editor/DialogueWindow.*`, `editor/TimelineWindow.*` - while one of these windows has focus, Ctrl+Z and Ctrl+Y act on its own file (string table, tree, dialogue, sequence), not on the scene
  - 15.9.7 Roomier shared layout - `editor/main.cpp`, `editor/ConsolePanel.cpp` - (#577) larger shared spacing, type size and hit targets for the menu, docks, Hierarchy, Inspector, Project browser, scene toolbar and Console; the default dock keeps a wider Inspector and Hierarchy without giving up the Scene view, also at 1280x800; the camera projection combo is no longer clipped (per the PR description; the drawing code was not read)
  - 15.9.8 Scene view overlays, toolbar, density and maximize - `editor/EditorLayoutMath.hpp`, `editor/EditorWidgets.*`, `editor/main.cpp` - optional collapsible camera overlay and stats line that step clear of each other and the view-axis gizmo, collision-free object names that fade when crowded, icon tooltips and a Toolbar labels mode that collapses into the ... menu when narrow, Compact / Comfortable density, Shift+Space maximize viewport, Collapse / Expand Group on docked panels, the last panel arrangement restored at start; all saved in `okay_settings.txt`
- **Code:** `editor/EditorState.*`, `editor/EditorTabs.hpp`, `editor/main.cpp`
- **API:** Edit > Undo / Redo, View > Layout, File > Autosave, View > Panels > History
- **Editor:** Edit menu, History panel, Layout dropdown
- **Docs:** `docs/editor.md` (Panel tabs & layouts, Editor quality-of-life), `docs/editor_undo.md`
- **Tests:** `editor_undo` (every edit kind do / undo / redo by scene text, merge, GUID restore, limit, Play, saved tracking), `editor_tabs`, `editor_layout` (overlay anchoring, label placement and crowd fade, density metrics, toolbar plan, maximize state, view preferences round trip), `ui_frame`, `editor_hooks`, `inspector_model`, `project_save`, `editor` (selftest)
- **Status:** Works - scene undo on `UndoStack` and tabs are tested headlessly; the Edit menu, shortcuts, History panel and `WatchEditUndo` are GUI code that was compiled but not run; autosave has no test
- **Open:** audit in `docs/editor_undo.md`; each Undo rebuilds the whole scene (no per-object or per-property commands); moving an asset the scene uses clears the scene history; mesh edit, Project panel, Timeline, Localization and script editors keep their own undo; rolling backups and a restore UI are planned (area 22)
- **Related:** 22, 15.3, 15.11, 15.12
- **Words:** undo, redo, history, autosave, crash recovery, console (editor), editor, layout, tabs, Ctrl+Tab, save layout, side tabs, panel switcher, snapshot undo, delta undo, undo stack, Undo menu
- **Seen as:** undo skips or restores too much; the layout resets; the editor crashes or shows a black screen; a crash loses work; the recovery prompt does not appear; Ctrl+Tab picks the wrong panel
- **Ask me like:** "work on 15.9.2, per-object undo commands so Undo does not rebuild the scene"; "layouts reset after restart"

### 15.10 Splitting `editor/main.cpp` into panel files (~45k lines)
- **What:** The ongoing move of panels out of the huge `main.cpp` into their own files with small headers, so each can be edited and reviewed alone.
- **Parts:**
  - 15.10.1 Split out so far (windows and panels) - `editor/AnimatorWindow.*`, `editor/BehaviorTreeWindow.*`, `editor/ConsolePanel.*`, `editor/DialogueWindow.*`, `editor/FpHandsWindow.*`, `editor/LocalizationWindow.*`, `editor/NavigationWindow.*`, `editor/NetSimPanel.*`, `editor/SceneManagerPanel.*`, `editor/TimelineWindow.*`
  - 15.10.2 Split out so far (helpers) - `editor/EditorWidgets.*`, `editor/SplineTools.*`, `editor/UIInspectorLayout.*`, `editor/UIInspectorOverlay.*`, `editor/UIPreviewPainter.*`
  - 15.10.3 Shared headers - `editor/EditorCommon.hpp` (what every file includes), `editor/MainShared.hpp` (functions still in `main.cpp` the split files call; the list shrinks as the split goes on), `editor/ScriptText.hpp`, `editor/EditorTabs.hpp`, `editor/GameFiber.hpp`
  - 15.10.4 Big pieces still in `main.cpp` (candidates, approximate) - Inspector ~9.8k lines (27452-37264), Scene and Game views ~5.9k (37265-43174), Script Editor with completion and docs ~5k (10940-15670), Animation tab ~3.5k (19492-22983), Actions editors ~2.5k, Project panel ~1.2k, Hierarchy ~1.1k, Modeling and UV ~1.6k, Build code ~1.5k, tab and layout system ~1k
  - 15.10.5 Build wiring - `editor/CMakeLists.txt` - the editor target, selftest ctest `editor`, OkayUI link, Windows resources `editor/okayspace.rc`, `editor/okayspace.ico`
- **Code:** `editor/main.cpp`, `editor/EditorCommon.hpp`, `editor/MainShared.hpp`, `editor/CMakeLists.txt`, `editor/AnimatorWindow.*`, `editor/ConsolePanel.*`, `editor/NetSimPanel.*`, `editor/SceneManagerPanel.*`
- **API:** none (internal structure)
- **Editor:** none
- **Docs:** -
- **Tests:** `editor` (selftest), `editor_tabs`, `game_fiber`
- **Status:** Partial - several windows already live in their own files (listed above) but `main.cpp` is still about 45k lines and holds the largest panels; no PR or branch for the next split found
- **Open:** no PR for the next split found (#577 changed about 280 lines of `main.cpp` for the layouts, the Script Editor find and replace bar and the Visual Script canvas, and draft PR #584 changes about 60 more, so expect merge conflicts for anyone moving those panels); `DrawFlowGraph` (~1,060 lines) is retired and uncalled and could be deleted instead of moved (14.6); `editor/main.cpp` also holds the headless selftest and a self-updater (~1439-2114); the Scripting side asks for splitting the OkayScript VM too (19.4)
- **Related:** 19.4, 15.2, 15.3, 13.5
- **Words:** main.cpp, split, refactor, panel files, MainShared, EditorCommon, giant file
- **Seen as:** merge conflicts in `main.cpp`; slow rebuilds after a one-line editor change; two agents editing the same function
- **Ask me like:** "work on 15.10.4, split the Project panel out of main.cpp"; "which parts of main.cpp are still unsplit"

### 15.11 Console and diagnostics panels
- **What:** The panels that show what the editor and game are doing: the Console log, Crash Log, Stats, Profiler, History, the Net panel and the live variable watch.
- **Parts:**
  - 15.11.1 Console - `editor/ConsolePanel.*` - draws `ConsoleModel::Rows()`; severity filters with counts, a category dropdown, search, collapse of repeated lines, details, Error Pause, Open Script, tab badge
  - 15.11.2 Crash Log and crash diagnostics - `editor/main.cpp` (`DrawCrashLog` ~6642, crash handler ~549-788) - reads crash reports from earlier runs
  - 15.11.3 Stats - `editor/main.cpp` (`DrawStats` ~10247-10358) - object, mesh and triangle counts, FPS
  - 15.11.4 Profiler - `editor/main.cpp` (`DrawProfiler` ~42668-42844) - frame, CPU and GPU timeline you can pause and scrub, per-section self and total times, CSV copy; times only while open
  - 15.11.5 Net panel - `editor/NetSimPanel.*` - network simulator toggles and bytes and messages per object
  - 15.11.6 Variables watch - `editor/main.cpp` (`DrawVarWatch` ~2252) - live visual-script variables
  - 15.11.7 Status bar - `editor/main.cpp` (`DrawStatusBar` ~6412) - selection, object count, tool, mode, FPS, and a Problems count that opens the Problems panel
  - 15.11.8 Problems panel - `editor/ProblemsPanel.*`, `Debug/Diagnostics`, `Debug/ProjectDiagnostics` - one live list of what is wrong, grouped by object, asset or script, with error / warning / info filters; a click selects the object, opens the script or frames the graph node; Fix adds a missing component, clears a broken reference, re-imports a file that is on disk, adds a Main Camera, or (level rules, see docs/editor.md "Problems panel: the rules") resets a zero scale or collider size, deletes an exact duplicate, adds a Kinematic Rigidbody or a Light; a row about two objects selects both and frames the pair; rows sort by severity, object and rule; the scene fingerprint includes transforms so level rules refresh on a move
  - 15.11.9 Inline markers - `editor/main.cpp` (Hierarchy rows, Inspector component headers), `editor/VisualScriptEditor.hpp` - a warning or error badge with a tooltip on the object and on the component; a long name is cut with an ellipsis before the badge; graph nodes keep an error or warning badge and a bad wire's pins are drawn red
- **Code:** `editor/ConsolePanel.*`, `editor/ProblemsPanel.*`, `editor/NetSimPanel.*`, `editor/main.cpp`, `Debug/Diagnostics`, `Debug/ProjectDiagnostics`
- **API:** `ConsoleLog(text, level)`; `EditorDiagnostics`, `CollectProjectDiagnostics`, `ApplyDiagnosticFix`, `AddNodeRequirement`; View > Panels > Console, Problems, Crash Log, Stats, Profiler, Net, Variables (watch)
- **Editor:** each panel; Problems is docked with the Console
- **Docs:** `docs/editor.md` (Scene & Game view extras), `docs/core_logging_profiling.md`
- **Tests:** `profiler`, `profiler_export`, `log` (engine side), `crash_reports`, `diagnostics`, `debug_console`, `level_validation`
- **Status:** Works - the Console, Problems and the inline badges read one diagnostic list; the list updates in edit mode when the scene changes
- **Open:** planned area 23 still wants a CPU and GPU timeline window, memory tracking and a frame debugger
- **Related:** 23, 1.10, 19.2, 33.2, 14.2, 13.5
- **Words:** Console, log, errors, warnings, Error Pause, Crash Log, Stats, Profiler, FPS, Net panel, watch, status bar, Problems, diagnostics, Fix, badge
- **Seen as:** the Console is flooded by one repeated line; errors are not shown; the profiler shows nothing; stats differ from the build; a problem does not select its object
- **Ask me like:** "work on 15.11.8, add a project rule to Problems"; "clicking a warning should open the script"

### 15.12 Scene Manager panel
- **What:** The project's scene list, file actions and the build order that Build Settings uses.
- **Parts:**
  - 15.12.1 Scene list and actions - `editor/SceneManagerPanel.*` - scenes of the project with thumbnails and file actions
  - 15.12.2 Scenes In Build - `editor/SceneManagerPanel.*` - the ordered list with on/off, persisted per project and published to the engine SceneManager once a frame
- **Code:** `editor/SceneManagerPanel.*`
- **API:** `SceneBuildSync`, `SceneBuildSnapshot`, `SceneBuildApply`, `GetSceneThumb`; View > Panels > Scene Manager
- **Editor:** Scene Manager panel; Build Settings scene list
- **Docs:** `docs/scene_management.md`
- **Tests:** `scenemanager` (engine side)
- **Status:** Works - separate file; no test of the panel
- **Open:** none found
- **Related:** 15.7, 1.4
- **Words:** Scene Manager, scenes in build, build order, scene list, scene thumbnails
- **Seen as:** a scene is missing from a build; the build order resets; thumbnails are stale
- **Ask me like:** "work on 15.12.2, drag to reorder the build list"

### 15.13 Content editors and tool windows
- **What:** Smaller editors for assets and data: sprites, materials, data assets, items, save files, audio mixer and the UI theme window.
- **Parts:**
  - 15.13.1 Sprite Editor - `editor/main.cpp` (`DrawSpriteEditor` ~42208-42417) - pixel paint, shapes, flood fill
  - 15.13.2 Material editor - `editor/main.cpp` (`DrawMaterialEditor` ~10174) - `.okaymat` albedo, emissive, specular, texture, tiling, Apply to Selection
  - 15.13.3 Data Asset editor - `editor/main.cpp` (`DrawDataAssetEditor` ~10009) - data assets for scripts
  - 15.13.4 Items, Loot & Recipes - `editor/ContentEditors.cpp` - items, loot tables and crafting recipes (11.2, 11.3); Window menu and View > Editors & Tools
  - 15.13.5 Save Manager - `editor/main.cpp` (`DrawSaveManager` ~10046) - browse and edit `.okaysave` files
  - 15.13.6 Audio Mixer - `editor/main.cpp` (`DrawAudioMixer` ~6588) - mixer buses and volumes (7.1)
  - 15.13.7 UI Theme window - `editor/main.cpp` (`DrawUIThemer` ~42586) - see 12.7
  - 15.13.8 Custom Actions window - `editor/main.cpp` (`DrawCustomActions` ~2219) - see 14.6
- **Code:** `editor/main.cpp`, `editor/ContentEditors.cpp`
- **API:** View > Editors & Tools, View > Panels, and Window > Items, Loot & Recipes
- **Editor:** each window
- **Docs:** `docs/editor.md`, `docs/save_games.md`, `docs/audio.md`
- **Tests:** `dataasset`, `savedata`, `item_database`, `item_loot_recipes`, `audio_mixer` (engine side)
- **Status:** Works - most of these windows are functions in `main.cpp`; items, loot and recipes are `ContentEditors.cpp`
- **Open:** none found; candidates for the split (15.10); planned area 26 adds a real material editor with live preview and shader graphs
- **Related:** 2.2, 7.1, 11.2, 1.7, 26, 12.7
- **Words:** Sprite Editor, Material editor, Data Asset, Item Database, Save Manager, Audio Mixer, UI Theme window
- **Seen as:** a material does not apply to the selection; a save file edit is not kept; the item list does not refresh; the mixer window does not match the game
- **Ask me like:** "work on 15.13.2, material preview sphere"; "the Save Manager cannot edit nested values"

### 15.14 Scene settings, project settings and help windows
- **What:** Windows that configure the scene and project, show services status, and explain the editor: Lighting (scene settings), Project Settings, Services, Keyboard Shortcuts, About, update and quit prompts.
- **Parts:**
  - 15.14.1 Lighting window - `editor/main.cpp` (`DrawSceneSettings` ~10495-10800, look sections ~10359) - Environment (sky and light), Rendering (lighting and post FX), Debug View, Lightmap Baking and the scene UI font; the sky, fog, weather and post-processing sections
  - 15.14.2 Project Settings - `editor/main.cpp` (`DrawProjectSettings` ~16929) - project-wide defaults saved to `project.okayproj` (company, version, default size)
  - 15.14.3 Services window - `editor/main.cpp` (`DrawServices` ~9805) - status of the online services that ship in the engine: Steam and Multiplayer
  - 15.14.4 Keyboard Shortcuts - `editor/main.cpp` (`DrawShortcutsWindow` ~10837) - list of editor shortcuts
  - 15.14.5 About, update and quit prompts - `editor/main.cpp` (`DrawAboutPopup` ~16869, `DrawUpdatePopup` ~17000, `DrawQuitPrompt` ~10801)
- **Code:** `editor/main.cpp`
- **API:** Edit > Project Settings; View > Panels > Lighting, Services; Help menu
- **Editor:** each window
- **Docs:** `docs/editor.md`, `docs/lighting.md`, `docs/rendering.md`
- **Tests:** none
- **Status:** Works - implemented; no tests
- **Open:** a full project settings window with migration and package management is planned (area 25); the Lighting window mixes sky, fog and UI font settings under one name
- **Related:** 2.3, 2.4, 2.5, 17.10, 25
- **Words:** Lighting window, scene settings, Project Settings, Services, Keyboard Shortcuts, About, quit prompt
- **Seen as:** a lighting setting does not save; defaults in Project Settings are ignored by a new build; the shortcuts list is out of date
- **Ask me like:** "work on 15.14.2, add a physics defaults section to Project Settings"

### 15.15 Editor shell, startup and lifecycle
- **What:** What runs around the panels: the main loop, menu and toolbar, dock space, the launcher handshake, single-instance guard, multi-window support, the self-updater and the headless selftest.
- **Parts:**
  - 15.15.1 Menu and toolbar - `editor/main.cpp` (`DrawMenuAndToolbar` ~5551-6411, `DrawDockSpace` ~6520) - File, Edit, View, GameObject, Engine, Help menus; Play, Stop, Step (advance one frame) and Build buttons
  - 15.15.2 Main loop - `editor/main.cpp` (`main` ~43674-45136) - frame loop, panel draw order with profiler scopes, camera safety, dev screenshot hooks (`OKAY_UI_*` variables)
  - 15.15.3 Launcher gate and handshake - `editor/main.cpp` (~43267, ~43572, `DrawProjectGate` ~16655, `DrawNewProjectPopup` ~16748) - open a project from the launcher, "save and close" requests
  - 15.15.4 Single instance and multi-window - `editor/main.cpp` (~43329-43571) - hand-off to a running editor, dual-monitor viewport windows
  - 15.15.5 Self-updater - `editor/main.cpp` (~1439-2114) - download and relaunch updates, runtime fetcher; the editor update and the player fetch now require a SHA-256 from `versions.json` (`updater::FetchVerified`) and the restart helper self-checks the new editor and rolls back (`docs/updater.md`)
  - 15.15.6 Theme and fonts - `editor/main.cpp` (~4273), `shared/OkayTheme.hpp` - Slate theme, accent colours, UI scale
  - 15.15.7 Headless selftest - `editor/main.cpp` (~1-445, `RunSelfTest`) - ctest `editor` runs `OkayEngine --selftest` with no display
- **Code:** `editor/main.cpp`, `editor/EditorCommon.hpp`, `editor/EditorWidgets.*`, `editor/okayspace.rc`, `editor/okayspace.ico`
- **API:** command line `--selftest`; env `OKAY_UI_*` dev hooks
- **Editor:** whole window
- **Docs:** `docs/editor.md` (Building and running, Self-updating)
- **Tests:** `editor` (selftest)
- **Status:** Works - used on every launch; CI runs the selftest
- **Open:** the launcher update flow (17.1) and the editor's updater are separate code paths; no test for the single-instance or multi-window logic found
- **Related:** 17.1, 18.5, 15.9, 15.10
- **Words:** editor startup, launcher handshake, single instance, multi-window, dual monitor, self-updater, selftest, menu bar, toolbar, dock space
- **Seen as:** the editor opens twice or the second launch does nothing; a black window on start; the update prompt loops; the toolbar button does nothing
- **Ask me like:** "work on 15.15.4, multi-window crash"; "what does --selftest cover"
