# Project panel

The Project panel is the editor's asset browser: a folder tree on the left, the folder's contents on the right
(tiles or a list), a path bar with Back and Forward, search, and a strip at the bottom for the selected asset.

## What you can do

- **Find things.** Type in the search box. Plain words match names; `t:model`, `t:texture`, `t:audio`, `t:script`,
  `t:scene`, `t:material`, `t:prefab`, `t:data`, `t:folder` and `ext:png` narrow it down. The **Folder / Project**
  button next to the type filter chooses where the search looks. Ctrl+F jumps to the search box. The left side has
  smart filters (all models, all textures, recently changed, unused assets) and your favourite folders.
- **Look.** The slider in the toolbar sets the thumbnail size. Image thumbnails are made once, kept in
  `.okay/thumbs` inside the project, and used again next time.
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
- **Find References.** Right-click an asset: lists what uses it and what it uses. Click a line to show it.
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
- **Previews.** Images get a cached thumbnail. Prefabs, scenes and models are rendered into a small picture (a few per
  frame); if one cannot be drawn, the type icon shows instead.
- Changes made outside the editor show up on their own (the panel checks again every couple of seconds, and waits
  longer on big projects).

## Safety

Nothing here can touch files outside the project's Assets folder (links that point out are refused). Every operation
either finishes or puts everything back; a copy or a saved file is written under a temporary name first. Names with
spaces and non-English letters are fine; names the operating system would refuse are rejected with a short reason.

## How it is built

- `editor/ProjectBrowser.hpp/.cpp`: all of the logic, with no window code: folder listing and sorting, file
  operations with undo, reference rewriting, delete reference lists, favourites, import from outside, the clipboard.
  Metas and GUIDs come from `okay::AssetDatabase` (see `asset_database.md`). A test hook makes any file step fail.
- `editor/ProjectPanelUI.hpp/.cpp`: the name prompt, delete confirmation, references list and import-settings editor.
- `DrawProject` in `editor/main.cpp` draws the panel and calls the above.
- Tests: `tests/test_project_panel.cpp` (ctest name `project_panel`).

## Things to know

- Opening a project writes nothing. A `.okaymeta` file (the asset's GUID and import settings) is made only for an
  asset that is given non-default import settings. Commit those with the asset. They are never shown in the panel.
- Rewriting finds references by reading text assets (scenes, prefabs, materials, clips, dialogue, ...). A path built in
  a script at run time, or stored in a binary file, is not found. The panel keeps a small index of what each text
  asset names and refreshes it when the files change.
- The **Scenes In Build** list follows a renamed or moved scene.
