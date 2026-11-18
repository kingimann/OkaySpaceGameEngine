# Project panel

The Project panel is the editor's asset browser: a folder tree on the left, the folder's contents on the right
(tiles or a list), a path bar with Back and Forward, search, and a strip at the bottom for the selected asset.

## What you can do

- **Find things.** Type in the search box. Plain words match names, tags and type words ("hero texture"); `t:model`,
  `t:texture`, `t:audio`, `t:script`, `t:scene`, `t:material`, `t:prefab`, `t:data`, `t:folder`, `ext:png`, `l:red` (a colour
  label) or `l:tag` (a tag from the asset's `.okaymeta`) narrow it down. `uses:hero.png` lists the assets that use that file,
  `deps:main.okayscene` the assets that scene uses, `is:unused` the assets nothing uses and `is:broken` the assets that name a
  file that is not there. The **Folder / Project** button next to the type filter chooses where the search looks. Ctrl+F jumps to
  the search box. The left side has smart filters (all models, all textures, recently changed, unused assets) with their counts,
  and your favourite folders. All of it is read from the asset database (see `asset_database.md`), not from the disk.
- **Look.** Grid or list (the button in the toolbar), and a tile-size slider; the **...** menu has Small, Medium and Large and shows
  how many assets are indexed and how long the last refresh took. Textures, audio (a waveform), models, mesh files, materials (a
  sphere), prefabs and scenes get a real picture; a script or data file keeps its type icon. Pictures come from the database's
  thumbnail cache: made on demand, a few milliseconds per frame (the icon shows until a picture is ready), kept in `.okay/thumbs`
  inside the project and used again next time, and made again when the file changes, or when a texture or mesh a prefab, scene
  or material uses changes.
- **Know what it is.** The strip at the bottom shows the selected asset's type, size, picture size, path, GUID (the first 8
  digits; hover for the whole GUID and the content hash), how many assets it uses (and how many through the whole graph), how
  many use it, its tags, and an orange warning when it names a file that is not there (hover to see which). The same warning
  shows as a small triangle on the tile or row.
- **Create.** The **+** button and the right-click menu (grouped: folder, code, scenes and objects, story and AI, looks and data)
  create a folder, script, visual script, scene, prefab, dialogue, quest file, behavior tree, cutscene, material, data asset or item. Each one asks for a name first.
  A new visual script opens in the Visual Script editor under that name.
- **Rename (F2).** Keeps the file type. Every scene, prefab, material and other text asset that used the old name is
  updated, the `.okaymeta` (the asset's GUID) moves with the file, colour labels follow, and the open scene is
  updated too.
- **Move.** Drag onto a folder (in the tree or the grid), or right-click > **Move to...** and pick the folder. References are updated the same way. Several selected
  items move together.
- **Copy, cut, paste, duplicate.** Ctrl+C, Ctrl+X, Ctrl+V and Ctrl+D (or the menu). A name that is taken gets a number.
- **Delete (Del).** Asks first and lists the assets that use it. Deleted items go to `<project>/.okay/trash` (the last
  30 deletions are kept), so **Ctrl+Z** in the Project panel brings them back. Ctrl+Z also undoes a create, rename,
  move, paste or duplicate.
- **Find References / Show Dependencies.** Right-click an asset (or use the Dependencies button in the strip): **Find References** lists
  the assets that use it, **Show Dependencies** lists what it uses. A checkbox switches from the direct links to everything it reaches
  through other assets. Click a line to show that asset. The lists come from the database's dependency graph, which reads scenes,
  prefabs, materials, scripts, clips, dialogue, quests and the other text assets (see `asset_database.md`).
- **Broken References.** The left side lists **Broken References** with a count. Click it for two lists read from the dependency
  graph: *missing* (an asset names a file that is not there; click a line to show the asset that names it) and *stale* (it still
  uses the old path of a file that was moved outside the editor). **Fix stale references** rewrites only those path texts in the
  files that use them, and Ctrl+Z in the Project panel puts the old text back. Missing references cannot be fixed by the panel:
  restore the file or point the asset somewhere else. A scene that is open in the editor keeps its old text until it is opened again.
- **Tags.** Right-click > Tags > Add Tag... stores a tag in the asset's `.okaymeta` (that is the only time the panel writes one besides
  import settings); search for it with `l:tag`.
- **Import Settings.** For textures and models, the info strip has an Import Settings button (stored in the asset's
  `.okaymeta`). Everything offered has an effect, and the settings file is copied into a build beside the asset.
  Textures: **Largest side** (the picture is made smaller when loaded). Filter, Wrap and Mipmaps are honoured by the
  software and OpenGL renderers but are hidden here until the Direct3D renderers read them (the `.okaymeta` keys and
  the code stay; one flag, `kShowTextureSamplerSettings` in `ProjectPanelUI.cpp`, shows them again).
  Models: **Scale** and **Normals** (from the file, calculated with a **Smoothing angle**, or flat) and **Import
  animations** (off drops the clips and leaves the rest pose). Models apply when the model is added to a scene (rigged
  parts keep the file's normals); parts already in a scene are not changed. **Apply** reloads the picture right away
  and redraws a model's preview. Not offered because nothing reads them: sRGB, Compress, and the audio options. The
  same box is available to other windows as `oked::projectpanel::DrawImportSettings`.
- **Favourites.** Right-click a file or folder > Add to Favourites. Starred items show a star and are listed under
  FAVOURITES on the left. They are kept in the project (`.okay/favourites.txt`) and follow renames and moves.
- **Reimport** reads a file again after you changed it in another program.
- **Keyboard.** Arrow keys move the selection (Shift adds), Enter opens, Backspace goes up a folder, Home and End jump,
  Ctrl+A selects all, F5 refreshes. After you click in the folder tree, Up and Down walk the folders, Right opens a folder (then
  steps into it) and Left closes it (then goes up).
- **Drag and drop.** Drag assets into the Scene view, the Hierarchy and the Inspector's asset fields (payload `ASSET_PATH`).
  Drop files or folders from your file manager onto the editor to import them into the folder you are looking at
  (copied under a free name, all or nothing, and Ctrl+Z takes them out again).
- **Previews.** See "Look" above. If a picture cannot be made (a damaged image, a model over 20 MB, a scene over 4 MB), the type icon shows.
- Changes made outside the editor show up on their own: the panel asks the database for an incremental scan every couple of
  seconds (longer on big projects; files whose size and time did not change are not read again, and nothing is rebuilt when
  nothing changed). There is no file watcher, so a change can take that long to appear.

## Right-click menus

Right-click an asset or folder in the grid or list, a folder in the tree on the left (the **Assets** folder too), a starred item
under FAVOURITES, or the empty space of the folder you are looking at. The toolbar **+** shows the same Create list. Every item
does the same thing wherever you reach it. The lists are made from a plain description (`editor/ProjectContextModel.hpp`, tested
without any window), so the order, the labels and what is greyed follow rules:

- An item that can never apply to what you clicked is left out (Find References on a folder). An item that applies but cannot run
  right now stays in, greyed, and its tooltip says why ("Select one item to rename it.", "Nothing was copied or cut yet.", "Open a
  project first.", "The Assets folder itself can't be changed.").
- With several items selected the labels count them (**Delete 3 Items**, **Duplicate 3 Items**, **Cut / Copy 3 Items**,
  **Reimport 3 Items**, **Add 3 Items to Favourites**), the first line says **3 items selected**, and items that only make sense for one
  item (Open, Rename, Properties, Find References, Tags) are greyed or left out.
- Hover any item for a line about what it does. The name of the file manager follows the system: **Show in Explorer** on Windows,
  **Show in File Manager** elsewhere.
- A menu in the folder tree or the Favourites list acts on the one folder or file you clicked. It does **not** select it (it may be in a
  folder that is not on screen), so a later Del cannot reach something you cannot see. In the grid, right-click an item outside the
  selection and the selection becomes that item; right-click one inside it and the whole selection is used.
- If the folder on screen is renamed or moved (from the grid, the tree, Move to... or Ctrl+Z) the panel follows it; if it is deleted, or
  vanishes behind the editor's back, the panel goes to the nearest folder above it that is still there. A folder that is gone has
  Create, Paste and Import greyed ("This folder is not there any more.").

**An asset** (one item or a selection)

| Item | What it does |
|---|---|
| Open / Open Prefab | Opens it in its editor (scene: loads it; script: the Script Editor; a model: adds it to the scene; and so on). Enter does the same. One item only. Greyed for a type with no editor window (pictures, audio, fonts): "Use Open with Default App." |
| Place in Scene, Place as Child of Selected, Create Variant, Validate Prefab | Prefab only. Place as Child is greyed until an object is selected in the Hierarchy. |
| Add to Open Scene, **Set as Start Scene** | Scene only. Set as Start Scene puts the scene first in Scenes In Build and ticks it (the Scene Manager shows and saves the list), so a built game starts there. It is greyed when the scene already starts the game. |
| Make Playable Character, Edit Script | Model and script. |
| **Open in External Editor** | Text files (scripts, scenes, prefabs, materials, data, story and AI files): Visual Studio Code when it is installed, else Notepad on Windows (`open -t` on macOS, the first text editor found on Linux). The editor is named, never asked from the file type, so the file is only edited. Not offered for a type that runs code (below). |
| **Open with Default App** | The program the system uses for that type. **Greyed for a type that runs code**: programs, installers, shortcuts and script files (`.exe .com .scr .bat .cmd .msi .lnk .url .reg .inf .dll .ps1 .vbs .vbe .js .jse .wsf .wsh .hta .jar .chm`, macro Office files, `.sh .command .app .pkg .desktop .appimage .py .rb .pl` and the rest of the list in `RunnableExtensions`; the last extension decides and Windows' trailing dots and spaces are cut, so `a.png.exe` and `setup.exe.` count; on Linux and macOS a file with no extension that has an execute bit counts too). The tooltip says "This file type can run code; open it from the file manager if you trust it." A project's assets come from outside (the marketplace, a zip), so the panel never starts them for you; Show in Explorer still works. The same check runs again inside the launcher as a last gate. |
| Rename (F2), Duplicate (Ctrl+D), Cut (Ctrl+X), Copy (Ctrl+C), Paste (Ctrl+V), Delete (Del) | As in "What you can do" above. Paste shows how many items it will paste and is greyed with "Nothing was copied or cut yet." when the clipboard is empty. |
| **Move to...** | Opens a folder picker over the project's folders. Folders the items cannot go to (the item itself, a folder inside it, where it already is) are greyed with the reason. Pressing Move moves them like a drag does: a taken name gets a number, links are updated, Ctrl+Z moves them back. |
| Find References, Show Dependencies, Find References in Scene | One file. As above. |
| Reimport | Reads the file again. For a folder it is **Reimport All in Folder** (every asset inside, below it too). |
| Add / Remove Favourites, Tags, Label | As above. Favourites and Label apply to the whole selection. |
| **Copy Text** | **Name**, **Relative Path** (`Assets/Models/Hero.fbx`, the way scenes write a path) or **Full Path**. With several items selected, one per line. |
| Show in Explorer | Opens the folder; on Windows and macOS the file is picked there. No command line is built (Windows: `ShellExecuteW` with the wide path and Explorer named; macOS and Linux: the program is started directly, no shell), so no character in a name (`$`, `%`, a quote, a space, a backtick) matters. |
| **Properties...** | A window with the type, folder, size, date (UTC, with "2 days ago"), GUID, whether a `.okaymeta` keeps it, how many assets it uses and how many use it, broken references (in orange), tags, label, favourite, and both paths. Copy Path puts the `Assets/...` path on the clipboard. For a folder: how many files and folders it holds and how big it is. |

**A folder** gets the same items plus **Create** (the submenu, made inside that folder, then the panel goes there; **Standard Folders**
is made all together or not at all, inside Assets only, and one Ctrl+Z takes them away),
**Paste Into**, **Reimport All in Folder**, and for the tree **Expand All Subfolders**, **Collapse All Subfolders** and **Collapse Other
Folders** (they only change what the tree on the left shows). The **Assets** folder in the tree has the same menu with Rename,
Duplicate, Cut, Move, Delete and Favourites greyed ("The Assets folder itself can't be changed.").

**Favourites** list: **Open Folder** / **Show in Project**, **Remove from Favourites**, Copy Text, Show in Explorer, Properties.

**Empty space**

| Item | What it does |
|---|---|
| Create | The list below, made in the folder being shown. |
| Paste, Undo (Ctrl+Z) | Paste shows the count. Undo names the last operation ("Undo Rename Hero.png") and is greyed when there is nothing to undo. |
| **Import New Asset...** | The same file picker as the toolbar's Import button; the files are copied into this folder. |
| **Refresh** (F5) | Looks at the disk now (the panel also does it every few seconds). |
| **Select All** (Ctrl+A) | Selects everything listed. |
| View | Grid, List, and **Small / Medium / Large Tiles** (the tile sizes are for the grid). |
| Sort By | Name, Type, Size, Date Modified (ticked). |
| Up One Folder, Add / Remove Folder from Favourites, Copy Text, Show in Explorer | For the folder being shown. |
| **Open Project Folder in Explorer** | The folder that holds `Assets` and `project.okayproj`. |
| **Properties...** | Of the folder being shown. |

While a smart filter is showing (All Models, Recently Changed, ...) the list is not one folder, so Create, Paste, Import, Up and the
folder's Favourites are greyed and the reason says how to leave the smart view (the **x** next to the path at the top).

**Create** (the submenu of a folder, the empty space and the toolbar **+**): Folder; Code: Script, Visual Script; Scenes and objects:
Scene, Prefab (or **Prefab from Selection** while an object is selected in the Hierarchy); **Story and AI**: **Dialogue** (a conversation
graph with one starting line, opens in the Dialogue window), **Quest File** (one example quest, opens in the Dialogue window's Quests tab),
**Behavior Tree** (a Selector with a Wait under it, opens in the Behavior Tree window), **Cutscene (Timeline)** (an empty sequence, opens in the
Timeline window); Looks and data: Material, Data Asset, Item, Loot Table, Recipe Book; Standard Folders. Everything except the last four asks
for a name first (a name that is taken starts at "Name 2"); nothing is written over an existing file. The last four are made by the
Items window: **Loot Table** and **Recipe Book** make a file in the folder whose menu you used, named "Loot Table" (then "Loot Table 2", ...)
with no name prompt (rename it with F2), and open it there; **Item** adds an item to the project's item list and makes no file in the
folder. **Standard Folders** makes Scenes, Scripts, Prefabs, Materials, Models, Textures and Audio, leaving any that exist alone.

**Not in the menus, on purpose:** *Export Package* (the editor has no package exporter yet; Build Settings builds a game, not a package),
*Select Dependencies* (the grid shows one folder, and a selection that reached into other folders could delete files you
cannot see; **Show Dependencies** lists them and a click shows each one), a *Localization table* in Create (a project has one string table,
`strings.okayloc`, edited in the Localization window) and *Animation* (animation clips come from a model or are captured on a model, not
made empty). *Read-only package folders* do not exist in a project today, so nothing is greyed for them.

**Pictures.** a picture folder (pictures are no longer stored in the repository) holds renders of these menus. They are **not screenshots of the editor window**: the real item
lists (`ProjectContextModel.hpp`) are drawn by the real menu code (`ProjectMenuDraw.cpp`) with Dear ImGui into a software surface, using
ImGui's default dark look and built-in font (made by `tools/project_menu_shots.cpp`, steps in its first lines), so colours and fonts differ
from the editor's skin. The items, their order, what is greyed and the tooltips are the real ones.

| Picture | What it shows |
|---|---|
|  | Right-click a picture: Open greyed (no editor window for pictures), Open with Default App, the edit items, Move to..., Reimport, Tags, Label, Copy Text, Show in Explorer, Properties. |
|  | Three pictures selected, two files copied earlier: "Delete 3 Items", "Paste 2 Items", Rename greyed. |
|  | A script: Edit Script, **Open in External Editor** (an editor is named, never "open") and Open with Default App. |
|  | `tool.bat`: **Open with Default App is greyed** with the reason in its tooltip, and there is no Open in External Editor. |
|  | A folder in the grid with **Create** open (grouped; Loot Table and Recipe Book make a file in this folder). |
|  | **Assets** in the folder tree: Rename, Duplicate, Cut, Copy, Move to..., Delete and Favourites are greyed. |
|  | Empty space with **View** open (Grid, List, tile sizes ticked), Undo naming the last operation. |
|  | Empty space while a smart filter shows: Create, Paste, Undo, Import and Properties are greyed. |
|  | The Create list (also the toolbar **+**). |
|  | A starred folder under FAVOURITES. |
|  | **Properties...** of a picture: size, date in UTC with "2 days ago", GUID, uses, tags, label, both paths. |
|  | A scene with broken references (orange) and a start scene note. |
|  | A folder: how many files and folders it holds and how big it is. |

## Safety

Nothing here can touch files outside the project's Assets folder (links that point out are refused). Every operation
either finishes or puts everything back; a copy or a saved file is written under a temporary name first. Names with
spaces and non-English letters are fine; names the operating system would refuse are rejected with a short reason.

## How it is built

- `editor/ProjectBrowser.hpp/.cpp`: all of the logic, with no window code: the listing (`Browser::List`), counts, folder tree,
  search, asset info and tags, thumbnails (`Browser::Thumb`), file operations with undo, reference rewriting, delete reference
  lists, favourites, import from outside, the clipboard. The index, GUIDs, the dependency graph, labels and import settings are
  `okay::AssetDatabase`'s and the pictures `okay::ThumbnailCache`'s (see `asset_database.md`); this class owns one of each per project.
  There is no scan of the folder of its own. A test hook makes any file step fail.
- `editor/ProjectPanelUI.hpp/.cpp`: the name prompt, delete confirmation, references list, import-settings editor, the **Move to...** folder
  picker and the **Properties** window.
- `editor/ProjectMenuDraw.cpp`: `DrawContextMenu` (draws a menu list with ticks, shortcuts, submenus and the tooltips) and `DrawPropertyRows`;
  ImGui only, so the screenshots below are drawn with this very code.
- `editor/ProjectContextModel.hpp`: the right-click menus as plain data (header-only, no window code), plus the pure helpers behind them:
  Copy Text, the `Assets/...` path, which file types run code, the Move to... rule, Set as Start Scene and the Properties rows. These checks
  only decide what is greyed: the file operations check every path again, with links resolved, before they touch anything.
- `editor/ProjectPaths.hpp`: path text helpers shared by the menus and `ProjectBrowser.cpp` (`Normalize`, `SameOrInside`, `Outermost` of a
  selection in n log n time, `NearestFolder`); header-only, tested without the engine.
- `editor/LaunchFile.hpp/.cpp`: starting other programs on purpose, with no shell (`OpenWithDefaultApp`, `OpenInTextEditor`, `Reveal`).
- `editor/ProjectNewAssets.hpp`: the starting text of a new dialogue, quest file, behavior tree and cutscene (written by the same classes the
  windows save with).
- `DrawProject` in `editor/main.cpp` draws the panel and calls the above; one function (`runAction`) runs the chosen id for every menu.
- Tests: `tests/test_project_panel.cpp` (ctest name `project_panel`: file operations, references, failure injection) and `tests/test_project_panel_db.cpp` (`project_panel_db`: the listing against a folder as files change, search and filters, find references, thumbnails, rename / move / delete keeping GUIDs, and the refresh time of a 5,000-asset project). The menus: `tests/test_project_context_model.cpp` (`project_context_model`: order, labels, plurals, greyed items and their reasons, no doubled separators, Copy Text, the `Assets/...` path, which file types run code, path text, the Move to... rule, Set as Start Scene, dates, the Properties rows; header-only, no engine; includes the types that run code, `Outermost` of a nested selection, `NearestFolder`, the Move to... rule on a 20,000-item selection) and `tests/test_project_menu_ops.cpp` (`project_menu_ops`: the new Create files read back and validated, Standard Folders, Reimport All in Folder, folder numbers, Duplicate of a whole selection (one undo, nothing copied twice), Paste on a selection, which types are text (`.js` is not), and that the Move to... picker's rule and `Browser::Move` agree)) and `tests/test_launch_file.cpp` (`launch_file`, Linux only: the launcher with fake `xdg-open` and `code` programs on PATH; a file name with `$(...)`, backticks, quotes or `;` arrives as one argument, a name that starts with `-` is not an option, programs and scripts are refused before anything starts, the text editor is named, a missing file or program is said in words, a relative PATH entry is ignored).

## Things to know

- Opening a project writes nothing. Every asset is in the database with a GUID that lasts for the session; a `.okaymeta` file (the
  asset's GUID, tags and import settings) is made only for an asset that is given a tag or non-default import settings, with the GUID
  it already had. Commit those with the asset. They are never shown in the panel. Renaming or moving keeps an asset's GUID (by its
  `.okaymeta`, or by its content within the session) and rewrites the paths that name it in scenes, prefabs, materials and the other text assets.
- Rename, move and delete are undone with Ctrl+Z in the Project panel, which also puts the open scene's paths back. The open scene's text
  is rewritten in place (its own undo history is rewritten too, so Ctrl+Z in the scene keeps working); a change that
  edits scene objects goes through the editor's normal undo.
- Rewriting finds references through the database's dependency graph (scenes, prefabs, materials, clips, dialogue, ...). A path built
  in a script at run time, or stored in a binary file, is not found. The database re-reads only the text assets that changed.
- The colour labels (Label menu) are the editor's own list in `.okaylabels`; tags are the database's labels in the `.okaymeta`. They are separate.
- The **Scenes In Build** list follows a renamed or moved scene (and every scene inside a moved folder), and follows it back on undo; the path mapping is tested in `project_panel_db`.
