# The build pipeline

> **What this is:** How **File > Build Settings** (and the headless `OkayEngine --build-game`) turns a project into a game you can ship: what a build contains, every option and what it does, the build report, and what is not built yet.
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-20 (moved into `release/`, paths and links checked; the text itself was not re-checked against the code).

How **File > Build Settings** (and the headless `OkayEngine --build-game`) turns a project into a game you can ship:
what a build contains, every option and what it does, the build report, and what is not built yet.

The pure logic (layout planner, dependency walk, manifest, report, zip) is `engine/include/okay/Core/BuildPipeline.hpp`;
the editor's `builder::Build` (in `editor/main.cpp`) uses it; the player finds its files with the same
`FindDataDir`. Tests: `tests/test_build_pipeline.cpp` (unit) and `tests/export/verify_layout.sh` (a real export that is
run).

## What a build looks like

### Folder layout (the default)

```
Build/MyGame/
    MyGame.exe              the player runtime, renamed (Linux: MyGame)
    SDL2.dll                runtime libraries the player needs - nothing else
    MyGame_Data/
        game.okayscene      the first scene in the build; the other scenes sit beside it
        game.okayconfig     window, graphics and scene-list settings the player reads
        build.json          the build manifest (below)
        Textures/hero.png   only the assets the built scenes use, under the path the scene writes
        ...
        StreamingAssets/    the project's StreamingAssets/ folder, copied as it is
```

Nothing else ships: no editor files, no tools, no authoring sources, no unused assets, no test data. The player runtime
is the only program; `SDL2.dll` (Linux: `libSDL2-2.0.so.0`) is copied only if found next to the player or the editor.
A game name with characters a file system rejects (`/ : * ?` ...) gives a safe folder and exe name (`A:B` -> `A_B.exe`
and `A_B_Data/`); the window title keeps the real name.

### Compact layout

The layout older versions made: the exe with a `Data/` folder (or, with "Tidy layout" off, everything beside the exe).
Kept so existing projects and scripts keep working. The dedicated-server export always uses it, and so does anti-cheat
(it checks `Data/manifest.json`): with anti-cheat on, a Folder or Zip request builds the compact layout and the report
says so.

### Zip archive

The Folder layout, packed into one `.zip` named by **Archive name** (`{name}-{version}-{platform}` by default; tokens
`{name} {version} {build} {platform} {date}`). Only the archive stays in the output folder. **Also create a .zip**
(on the Folder or Compact layout) keeps the folder and writes the zip beside it. **Compress the archive** deflates
files that shrink (already-compressed types such as `.png` and `.ogg` are stored); off, everything is stored. The result
shows the archive size next to the folder size.

### build.json

`<Name>_Data/build.json` says which build this is: `game`, `company`, `bundleId`, `version`, `buildNumber`,
`engineVersion`, `date` (UTC), `platform`, `layout`, `development`, `strippedAssets`, `compressed`, `settingsHash` (a hash of
the settings the build used, so two builds can be told apart), `scenes` (in build order), `assetCount`, `totalBytes`.
The player reads it: `MyGame.exe --build-info` prints it; a development build writes the version and build number to
its log and draws them on screen.

## The dependency walk (asset stripping)

With **Strip unused assets** on (the default) a build ships:

1. the assets each scene in the build uses (`SceneSerializer::CollectAssetPaths`: textures, audio, meshes, prefabs
   a spawner makes, sequences, trees, item databases, ...);
2. what those files use in turn - prefabs, materials, clips, dialogue, behavior trees, glTF/obj/mtl and other text
   assets are read for the files they name (the same scanners the Asset Database uses), repeatedly, cycles allowed;
3. each file's `.okaymeta` import settings;
4. the visual-script files of scenes that are copied as files (a scene written from the editor embeds its graphs).

A reference is found as written, then under the project folder, its `Assets/` folder, the referring file's folder and
the scenes' folders. It is stored under the path the scene writes, so the running game finds it where the scene says.
References never climb out of the project with `..`; an absolute path is copied by file name and warned about (the game
would still look for the original path); `@name` pictures, `proc:` textures and URLs are not files. A reference that
resolves to nothing is a **warning** (missing asset) and is not shipped.

With stripping off, every file under `Assets/` ships too (except scenes that are not in the build, editor-only files such as
`.psd .blend .bak`, and `.okaymeta` sidecars), under `Assets/<path>`; the report warns how many unused files and bytes
that added.

## Settings

Build Settings are saved per project (`.okay/build_live.cfg`, and named profiles in `.okay/build_profiles.cfg`) and come
back the next time. The settings of the Output page are new here; the older ones are listed in the audit.

| Setting | Where | Effect |
|---|---|---|
| Layout | Output | Folder, Compact or Zip archive (above) |
| Tidy layout | Output (Compact) | `Data/` subfolder or everything beside the exe |
| Archive name, Compress | Output (Zip, or "also zip") | the zip's file name and whether files are deflated |
| Build number, Count up after each build | Output | written to `build.json` and the game; the editor adds 1 after each real build (a dry run or a failed build does not) |
| Strip unused assets | Output | the dependency walk above, or every project asset |
| Clean output folder | Output | removes `Data/`, `<Name>_Data/` and old scene/config files first |
| Open the output folder / Also create a .zip | Output | after a real build |
| Dry Run (button) | footer | the build without writing anything; the report only |
| Build & Run (button) | footer | builds, then launches the exe |

## Build report

After every build the result window shows the report in tabs - **By kind** (executable, libraries, scenes, config,
textures, audio, models, fonts, scripts, data, StreamingAssets, other), **By type** (extension), **Files** (largest first,
with why each file is there), **Left out** (project files that did not ship and the reason: *not used by any scene in
the build*, *scene is not in the build*, *editor-only file*) and **Warnings** - plus total size, archive size and build
time. **Copy report** copies it; the same text is saved to `.okay/build_report.txt` in the project (never into the game
folder). Warnings include: missing assets, absolute asset paths, a big (1 MB+) project file that nothing uses and was left
out, a layout that was changed (anti-cheat), a scene that could not be read, an icon or splash file that is missing, and the
number of unused files shipped when stripping is off.

### Dry run

A dry run builds in a scratch folder that is deleted again, so the report is exact and the output folder is not touched
(no folder, no zip). From the command line:

```
OkayEngine --build-game <scene> <out-dir> [--name GAME] [--layout folder|compact|zip] [--dry-run] [--report FILE]
           [--no-strip] [--no-compress] [--zip] [--version V] [--build-number N] [--archive-name PATTERN] [--dev]
```

It starts from the project's saved Build Settings (when the scene is inside a project) and the flags override them for that
one build; it never changes the saved settings. Exit status 0 means the build (or dry run) finished and the export check
found no problem; the check's line and each problem are printed.

## Building in the background, the output folder and the export check (2026-11-22)

Owner report (Windows release, 2026-11-22): "very laggy, glitchy and it didn't export properly". What changed:

- **The build runs off the main thread.** Build, Build & Run and Dry Run first read what they need from the editor on
  the main thread (`builder::PrepareBuild`: the open scene as text with the files it uses, every other built scene's
  file list, the string table, the physics settings). The rest (`builder::RunPreparedBuild`: copying, the player
  download if needed, compressed texture copies, the report, the zip, the export check) runs on a worker thread
  (`shared/OkayThreads.hpp`, no `<thread>`, rule 21a). A **Building** window shows the step, a progress bar (the asset
  copy counts file by file), the time and **Cancel**; the editor keeps drawing. Cancel stops after the current file and
  says the folder is incomplete. Quitting the editor cancels and waits. Server, Web, Android and Xbox exports still run
  on the main thread. `OkayEngine --build-game` runs the same two halves on one thread.
- **A relative output folder is inside the project.** The default `build/MyGame` was resolved against the editor
  process's working folder; started from the launcher on Windows that is the install folder, so the game was written
  somewhere nobody looks (`bsm::ResolveOutputDir`, test `build_settings` "output folder"). The Output page shows the full
  path under the box, the summary and the result say where it went, and **Open folder** opens it.
- **The export check.** After every build (and dry run) `buildpipe::CheckExport` reads the finished folder the way the
  player will: its data folder (`FindDataDir`), `game.okayconfig` (packed or not), the start scene and every `scene=`,
  the icon / splash / boot logo it names, and every file each shipped scene and prefab names, looked up as data folder +
  the path as written. A visual-script or script file whose graph or source is written in the scene is not required. It
  also checks the program and (Windows) `SDL2.dll` beside it, and warns when a shipped text file names a folder of the
  building computer (project, editor). Problems are listed in red in Build Result, in the Console, in the Advanced page's
  log and in `.okay/build_report.txt`; the build counts as failed in the history, and `--build-game` exits with 1.
- **The scene file in the build** is written as it is: no `.savetmp` / `.bak` beside it (the editor's safe-save of a
  project scene made `game.okayscene.bak` appear in builds made without "Clean output folder").
- **Per-frame disk work removed from the window**: the Scenes page asked the disk up to three times per listed scene
  every frame (exists, and "is this the open scene" with two more exists and an equivalent), the summary asked four more
  times for the icon and splash, and the "Load file..." menu listed the BuildProfiles folder every frame while open. Now
  they are asked when the list or path changes and once a second.
- **Glitches**: Width / Height were clamped to 320 x 240 while being typed (so typing a new size jumped); the clamp now
  waits until the box is left. The Resolution and Quality preset boxes showed the last preset picked, even after the
  size or the toggles were changed; they now show the preset that matches, or Custom.
- Build commands (Advanced page) run with `cd <project> &&` in the shell instead of changing the editor's own working
  folder, which a worker thread must not do.

## Audit (before this work) and what changed

| Area | Before | Now |
|---|---|---|
| Build Settings window | Profiles, six pages (Scenes, Player, Display, Graphics, Audio & Input, Output), platform picker | same, plus layout, build number, stripping, compression, archive name, Dry Run |
| Desktop output | `<Name>.exe` + `SDL2.dll` + `Data/` (scenes, config, assets) | `<Name>.exe` + `<Name>_Data/` (default); `Data/` stays as the Compact layout |
| Assets shipped | only those the **open** scene named; other scenes in the build brought no assets; nothing read inside prefabs or materials | every built scene, and the files those files use (dependency walk); report of what was left out |
| StreamingAssets | none | copied verbatim, `Paths::StreamingAssetsDir()` in the game |
| Manifest | `Data/manifest.json` (file hashes) written on every build, used only by anti-cheat | written only with anti-cheat on; `build.json` (build info) in every build |
| Version | text field, written to the config, never read by the game | also `build.json`, `--build-info`, development overlay and log |
| Build number | none | persisted, auto count-up |
| Identifier | written to the config, ignored by the player | in `build.json` and `--build-info` |
| Archive | "Create a .zip" ran the system `zip`/PowerShell | built in the editor (deflate or store), size reported, Zip layout |
| Result window | one table, four colours, flat file list | tabs by kind / type / files / left out / warnings, time, archive size |
| Report file | none | `.okay/build_report.txt` |
| Headless | `--build-game SCENE OUT [--name]` | plus the flags above and a dry run |
| Scenes in build, order, company, window mode, resolution, vsync, FPS, icon, splash, development build (log + F1 panel), encrypt, anti-cheat, clean, open folder, Build & Run | present | unchanged |
| Web / Android export | scene, config and assets in `game/` / `assets/` plus a script or README; ignores the desktop options | unchanged |
| Dedicated server export | needs `Data/` beside the exe | unchanged (forces the Compact layout) |
| CI | `tests/export/verify.sh` (Ubuntu) and `verify.ps1` (Windows) export and run a game | both still pass on the Folder layout; `verify_layout.sh` added on Ubuntu |

Options that were accepted but did nothing before this work: **Identifier** (the player never read it), **Version** (the
player never read it), and the manifest written on every build (only anti-cheat reads it). They now feed `build.json`.

## Known gaps

- **Pack file** (all assets in one archive the player reads directly): not built. The player loads assets by file path in
  many places, so a pack needs a virtual file layer first; loose files are what ships.
- **Plugins/**: the engine has no native-plugin loader, so a build has no `Plugins/` folder.
- **Debug symbols**: the player is built without a separate symbol file to ship, so there is no option for it.
- **Icon on the exe**: the icon is the window/taskbar icon the player sets at run time; the exe file itself keeps the
  engine's icon (changing it needs a resource edit of the exe).
- **Scripting backend / stripping of code**: there is one runtime, no choice to make.
- **Anti-cheat with the Folder layout**: anti-cheat verifies `Data/manifest.json`, so it forces the Compact layout.
- **Web, Android and server targets** ignore Layout, Strip, Compress and the build number (they have their own output).
- StreamingAssets has an engine API (`Paths::StreamingAssetsDir`, `Paths::StreamingPath`) but no script node yet.
