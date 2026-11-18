---
id: 7h
row: | 7h | Project panel right-click menus (asset, folder, tree, Favourites, empty space, Create) | Editor: **View > Panels > Project**, right-click | New in this change; unverified in the real window |
---
## 7h. Project panel right-click menus

**What it is.** The right-click menus of the editor's Project panel were reworked and filled out: one list per place you can click
(an asset, a folder, a folder in the tree on the left, a starred item, the empty space), with a tooltip on every item, greyed items that
say why, and counts when several items are selected ("Delete 3 Items"). Full list of items: `docs/project_panel.md`, section
"Right-click menus". Code: `editor/ProjectContextModel.hpp` (the lists), `editor/ProjectPanelUI.cpp` (drawing, the Move to...
picker, Properties), `DrawProject` in `editor/main.cpp`.

**Getting there.** Open the editor (`Tools\OkayEngine.exe`, or from the launcher). **File > New Project...**, pick any template and
create it (a project is needed: with none open the menus grey out and say "Open a project first."). The **Project** panel shows an
**Assets** folder; if it is hidden use **View > Panels > Project**. Put a few files in it: drag a picture, a sound or a model from your file
manager onto the editor window, or use the Project panel's **Import** button.

**Steps.** (Each step says what you should see.)
1. **An asset.** Right-click a picture. You should see Open (greyed for a picture; the tooltip says "No editor window opens this type. Use Open with Default App."), Rename (F2), Duplicate (Ctrl+D), Cut, Copy, Paste (greyed: "Nothing was
   copied or cut yet."), **Move to...**, Delete, Find References, Show Dependencies, Reimport, Add to Favourites, Tags, Label, **Copy Text**,
   Show in Explorer, **Properties...**, and **Open with Default App**. Hover an item for its tooltip.
   * **Copy Text > Relative Path**, then paste into any text box (the Console filter, a script): `Assets/<your file>`.
   * **Properties...** opens a window: type, folder, size, date in UTC with "N minutes ago", GUID, how many assets use it. **Close** closes it.
   * **Open with Default App** opens the picture in your system's picture program.
   * **Show in Explorer** opens the folder with the file selected.
2. **Several items.** Click one file, Ctrl+click two more, right-click one of them. The first line says **3 items selected**; the items read
   **Delete 3 Items**, **Duplicate 3 Items**, **Copy 3 Items**; Rename and Open are greyed ("Select one item to rename it." / "Select one item to open it."). **Copy Text > Relative Paths
   (one per line)**, then paste into a multi-line box: one path per line. **Duplicate 3 Items** makes three copies ("name copy"); one Ctrl+Z takes all three away.
   Select a folder and a file inside it (open the folder first and use the search box with `ext:png` to list both, or Ctrl+A in a search over the whole project),
   then Duplicate: the folder is copied once (the copy has its own copy of the file); no second "copy" appears beside the original file.
3. **Move to...** Right-click a file, **Move to...**, pick a folder in the window (your Assets folder and its folders; the file's own folder is greyed:
   "It is already in that folder."), press **Move**. Ctrl+Z in the Project panel moves it back. Try it on a folder and pick that folder or a folder inside
   it: they are greyed with "A folder can't be moved into itself." Try it with Ctrl+A in a big folder: the window opens at once.
4. **A scene.** Right-click a `.okayscene` (File > New Scene, then save it into Assets): **Set as Start Scene** puts it first in Scenes In Build.
   Check in **View > Panels > Scene Manager**: it shows "Start scene: <name>". Right-click it again: the item is greyed ("This scene already starts the game.").
5. **A script or data file.** Right-click a script (Create > Script): **Open in External Editor** opens it in Visual Studio Code if you have it,
   else in Notepad. The file is only opened for editing.
6. **A program or script is never started.** In your file manager make a file `test.bat` (any text) in the project's `Assets` folder; wait a moment or press F5
   in the Project panel. Right-click it: **Open with Default App** is greyed and the tooltip says "This file type can run code; open it from the file manager if you
   trust it." There is no Open in External Editor for it. **Show in Explorer** still opens the folder with it selected. The same goes for `.exe .cmd .lnk .ps1 .vbs .js .hta .jar .msi .reg` (and `.sh .py`);
   a picture named `photo.png.exe` counts as an exe. A file named with a `$`, `%` or `'` in it can be shown in Explorer and opened (a text file) like any other.
7. **A folder.** Right-click a folder in the grid: **Create** (made inside that folder; the panel goes there), Paste Into, **Reimport All in Folder**, Expand /
   Collapse. Right-click a folder in the **tree** on the left: the same list, and **Expand All Subfolders**, **Collapse All Subfolders**, **Collapse Other Folders**
   change what the tree shows. Right-click **Assets** in the tree: Rename, Delete, Move and the others are greyed ("The Assets folder itself can't be changed.").
   * **Reimport All in Folder:** put a picture in a folder, open the folder, then replace the picture with a different one in an image editor (keep the name) and save.
     Right-click the folder > Reimport All in Folder. The Console says "Reimported 1 asset" (or more) and the tile shows the new picture and the strip at the bottom the new size.
   * **Create from a folder's menu:** right-click a folder (not the one you are in) > Create > Folder. The name prompt opens; after you press Create the panel shows that folder with
     the new one selected. **Create > Standard Folders** makes Scenes, Scripts, Prefabs, Materials, Models, Textures, Audio (the Console says how many); Ctrl+Z removes them all together.
     **Create > Loot Table** (or Recipe Book) makes "Loot Table.okayloot" in that folder (no name prompt; F2 renames it) and opens the Items window on it.
   * **The folder on screen goes away:** open a folder, then in the **tree** right-click that same folder > Rename and give it a new name: the panel follows to the renamed folder.
     Now right-click it in the tree > Delete and confirm: the panel moves up to the folder above it, Create works there, and Ctrl+Z brings the folder back.
   * **Tree actions leave your selection alone:** select a file in the grid, then in the tree right-click some other folder > Duplicate. The copy appears in that folder's parent,
     and the file you selected is still the selected one (the strip at the bottom still shows it).
8. **Favourites.** Add a file and a folder to Favourites. They appear under FAVOURITES on the left. Right-click one: **Show in Project** / **Open Folder**,
   Remove from Favourites, Copy Text, Show in Explorer, Properties. Remove a starred file that is in another folder, then press Del: nothing is deleted (nothing was selected by that menu).
9. **Prefab items.** Create > Prefab (or select an object in the Hierarchy first and use **Prefab from Selection**). Right-click the new `.okayprefab`:
   * **Open Prefab** shows the prefab alone in the Scene view (double-click does the same).
   * **Place in Scene** adds an instance: it appears in the Hierarchy and the Console says "Instantiated ...".
   * **Place as Child of Selected** is greyed ("Select an object in the Hierarchy first.") until an object is selected; then the instance appears under that object.
   * **Create Variant** makes "<name> Variant.okayprefab" next to it (the Console says so); **Validate Prefab** writes "Prefab <name> is fine (1 file checked)" to the Console.
10. **Tags and Label.** Right-click a picture > **Tags > Add Tag...**, type `hero`, press Add. The strip at the bottom shows "Tags: hero"; type `l:hero` in the search box and the picture is listed.
    **Tags > Remove "hero"** takes it off again. **Label > Red** puts a red dot at the top right of the tile and lists "Red 1" under LABELS on the left; **Label > None** clears it.
11. **Empty space.** Right-click empty space in the grid. **Create** now also has **Dialogue**, **Quest File**, **Behavior Tree**, **Cutscene (Timeline)**:
    each asks for a name, creates the file in the folder you are looking at and opens it in its own window (Dialogue, Dialogue > Quests tab, Behavior Tree, Timeline).
    Also: **Paste** with a count, **Undo <what you did>**, **Import New Asset...** (the file picker), **Select All**, **View** (Grid, List, Small / Medium / Large
    Tiles), **Sort By**, **Up One Folder**, **Open Project Folder in Explorer**, **Properties...** of the folder.
    * **Refresh (F5):** with the Project panel focused press F5, or choose Refresh. The Console says "Refreshed the Project panel: N assets indexed". Copy a new file into `Assets` with your file
      manager and press F5: it is listed at once (without F5 it can take a few seconds).
    * **Select All (Ctrl+A):** every tile gets the accent outline and the strip at the bottom says "N items selected (Delete removes all)".
12. **Smart view.** Click **All Models** (or another smart filter) on the left, then right-click empty space: Create, Paste, Import are greyed and say how to leave the view.

**How you know it worked.** Each step names what you should see. Anything you right-click that does nothing, or an item that stays greyed without a reason in its
tooltip, is a bug: tell us which item.

**Left out on purpose.** Export Package (no package exporter exists), Select Dependencies, a Localization table and an Animation in Create: reasons in
`docs/project_panel.md`.

**Status.** The item lists, labels, greyed reasons, Copy Text, which file types run code, the Move to... rule, the path helpers (nested selections, the nearest folder that exists),
Set as Start Scene, the dates and the Properties rows are tested headlessly (`project_context_model`, `project_menu_ops`). The new story and AI files are read back with the engine's parsers,
and Standard Folders, Duplicate of a selection and the tags a menu lists are tested on temp folders, in `project_menu_ops`. The launcher that starts Notepad, the default app or Explorer
(`editor/LaunchFile.cpp`) is tested on Linux against fake programs on PATH (`launch_file`): a hostile file name reaches the program as one untouched argument and program and script types are
refused before anything starts; its Windows half (`ShellExecuteW`) was only compile-checked with the MinGW cross compiler. **Unverified:** everything you see in the real window (drawing, tooltips, the Move to... and Properties
windows, the Explorer, Notepad and default-app launches on Windows, the tree's expand and collapse, the panel following a renamed folder). Nothing here was run on Windows. The changed editor files (`editor/main.cpp`, `ProjectPanelUI.cpp`, `ProjectMenuDraw.cpp`, `ProjectBrowser.cpp`, `ContentEditors.cpp`, `LaunchFile.cpp`) were compiled with `-fsyntax-only` on Linux with the editor's real flags (no error; `main.cpp` has older warnings of its own) and the launcher, the panel and the menu drawing also with the MinGW Windows cross compiler; the editor was not linked or started.
**Pictures:** a picture folder (pictures are no longer stored in the repository) (listed in `docs/project_panel.md`) are headless renders, **not screenshots of the editor window**: the real item lists and the real menu drawing code, drawn by Dear ImGui into a software surface with ImGui's default look and font (`tools/project_menu_shots.cpp`). The editor itself could not be built and run on the machine that wrote this, so there is no screenshot of the real window; the OkayUI panels have no Project panel, so there is no second copy of this menu to keep in step.
