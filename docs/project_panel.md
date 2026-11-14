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
- **Create.** The **+** button and the right-click menu (grouped: folder, code, scenes and objects, looks and data)
  create a folder, script, visual script, scene, prefab, material, data asset or item. Each one asks for a name first.
  A new visual script opens in the Visual Script editor under that name.
- **Rename (F2).** Keeps the file type. Every scene, prefab, material and other text asset that used the old name is
  updated, the `.okaymeta` (the asset's GUID) moves with the file, colour labels follow, and the open scene is
  updated too.
- **Move.** Drag onto a folder (in the tree or the grid). References are updated the same way. Several selected
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
  Ctrl+A selects all. After you click in the folder tree, Up and Down walk the folders, Right opens a folder (then
  steps into it) and Left closes it (then goes up).
- **Drag and drop.** Drag assets into the Scene view, the Hierarchy and the Inspector's asset fields (payload `ASSET_PATH`).
  Drop files or folders from your file manager onto the editor to import them into the folder you are looking at
  (copied under a free name, all or nothing, and Ctrl+Z takes them out again).
- **Previews.** See "Look" above. If a picture cannot be made (a damaged image, a model over 20 MB, a scene over 4 MB), the type icon shows.
- Changes made outside the editor show up on their own: the panel asks the database for an incremental scan every couple of
  seconds (longer on big projects; files whose size and time did not change are not read again, and nothing is rebuilt when
  nothing changed). There is no file watcher, so a change can take that long to appear.

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
- `editor/ProjectPanelUI.hpp/.cpp`: the name prompt, delete confirmation, references list and import-settings editor.
- `DrawProject` in `editor/main.cpp` draws the panel and calls the above.
- Tests: `tests/test_project_panel.cpp` (ctest name `project_panel`: file operations, references, failure injection) and `tests/test_project_panel_db.cpp` (`project_panel_db`: the listing against a folder as files change, search and filters, find references, thumbnails, rename / move / delete keeping GUIDs, and the refresh time of a 5,000-asset project).

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
