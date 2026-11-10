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
- **Import Settings.** For textures, models and audio, the info strip has an Import Settings button (stored in the
  asset's `.okaymeta`). The same panel is available to other windows as `oked::projectpanel::DrawImportSettings`.
- **Reimport** reads a file again after you changed it in another program.
- **Keyboard.** Arrow keys move the selection (Shift adds), Enter opens, Backspace goes up a folder, Home and End jump,
  Ctrl+A selects all.
- **Drag and drop.** Drag assets into the Scene view, the Hierarchy and the Inspector's asset fields (payload `ASSET_PATH`).
  Drop files from your file manager onto the editor to import them into the folder you are looking at.
- Changes made outside the editor show up on their own (the panel checks again every couple of seconds, and waits
  longer on big projects).

## Safety

Nothing here can touch files outside the project's Assets folder (links that point out are refused). Every operation
either finishes or puts everything back; a copy or a saved file is written under a temporary name first. Names with
spaces and non-English letters are fine; names the operating system would refuse are rejected with a short reason.

## How it is built

- `editor/ProjectBrowser.hpp/.cpp`: all of the logic, with no window code: folder listing and sorting, file
  operations with undo, reference rewriting, delete reference lists, the clipboard. It is built on
  `okay::AssetDatabase` (see `asset_database.md`). A test hook makes any file step fail.
- `editor/ProjectPanelUI.hpp/.cpp`: the name prompt, delete confirmation, references list and import-settings editor.
- `DrawProject` in `editor/main.cpp` draws the panel and calls the above.
- Tests: `tests/test_project_panel.cpp` (ctest name `project_panel`).

## Things to know

- The first time the panel opens a project, the asset database indexes it and writes a small `.okaymeta` file next to
  every asset (it holds the GUID, labels and import settings). Commit them with the assets. They are never shown in
  the panel.
- Import settings are stored and signalled, but the importers do not read them yet.
- Rewriting finds references by reading text assets (scenes, prefabs, materials, clips, dialogue, ...). A path built in
  a script at run time, or stored in a binary file, is not found.
