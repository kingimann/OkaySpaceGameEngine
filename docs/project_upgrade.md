# Project versions and upgrading a project

A project remembers which version of OkaySpace last saved it. When you open it with an editor of another version, the editor
asks before it touches anything: upgrade the project (after a full backup), keep its version, or leave it alone. The rules live in
`shared/ProjectVersion.hpp` (no editor, ImGui or engine code, so the launcher uses the same ones) and `editor/ProjectUpgrade.hpp`
(the backup, the upgrade and the question, no ImGui); the windows are `editor/ProjectUpgradeUI.cpp`.

This is about the **engine version** (`2.855.0`). Scene files have their own format version (`okayscene 3`, with migrations: see
[scene_serializer.md](scene_serializer.md) and [scene_diff_merge.md](scene_diff_merge.md)); an upgrade uses those migrations on every scene
and prefab, but the two numbers are not the same thing.

## The stamp

`<project>/project.okayproj` has one line, `engine_version=2.855.0`: the version of the editor that last saved the project.

- A new project has it from its first moment. Save Scene, Save Project and the automatic saving of Project Settings and Build Settings
  write the running editor's version (only that one line changes when a scene is saved).
- An editor that does not know the key skips the line like any unknown line, so a stamped project still opens in an older editor.
- Saving `project.okayproj` keeps every line the editor does not know (a newer editor's settings, a note you typed), and the stamp is
  written first. Writes are the usual whole-or-nothing ones (`WriteTextAtomic`: a read-only file is refused, the old file stays whole).
- A development build (version `dev`) never asks and never writes a stamp, but keeps the one that is there.
- The stamp is the last writer's, whatever it was before: open a project with an older editor, save, and it says that older version.

## Release lines and patches

A release is `2.<build>.<patch>`. The first two numbers are the **line** (`2.850`); a long-term line only gets fixes, as patch releases
of the same build number (`2.850.1`, `2.850.2`). A patch never changes a file format, so a project stamped `2.850.0` opens in `2.850.1`
(and the reverse) **with no question**: the next save writes the running version. `LineOf("2.850.1")` is `"2.850"`, `SameLine(a, b)`,
`LineLabel("2.850.1")` is `"v2.850.x"` (what the dialogs call a line).

## What happens when a project is opened

`okayver::Decide(projectVersion, runningVersion)` gives one of:

| Decision | The project was last saved by | The editor does |
|---|---|---|
| `Same` | this version | opens it |
| `SamePatchLine` | another patch of this line | opens it; the next save stamps this version |
| `DevBuild` | (the running editor is a development build) | opens it, never asks |
| `Older` | an older line | asks: **Upgrade project to v2.855.0**, **Keep v2.850.x**, **Cancel** |
| `Unknown` | nothing recorded (made before this feature) | asks: **Upgrade project to v2.855.0** or **Cancel** (the old version can't be known, so Keep is not offered) |
| `Newer` | a newer line | asks: **Cancel**, **Open anyway (a backup is made first)**, **Update OkaySpace** |

The question is asked for every way of opening a project: the startup chooser, File > Open Project Folder, File > Open Recent and
File > Open, a scene opened from the Project panel or dropped on the Hierarchy or Scene view, and a project handed over by the launcher
(`--open`). It is asked **before anything of the project is read for writing or written**: no stamp, no autosave, no settings file, not even the
look at `project.okayproj` the loading screen takes. The old shared `project.okayproj` is only copied into a project once the question is answered.
As a safety net, `EditorState::Load` itself refuses a scene of a project nobody has answered for (`EditorState::beforeLoad`), so a future
way of opening scenes that forgot the question cannot skip it.

One question at a time. **Cancel** changes nothing and the editor stays where it was (or shows the project chooser, when nothing was open).
Answering is remembered for the session: once a project is upgraded, opened anyway, or made in this session, it is not asked about again.

### Older: upgrade or keep

- **Upgrade project to v2.855.0** makes the backup, checks and rewrites, stamps, and shows the result (see below).
- **Keep v2.850.x** does not touch the project. If an editor of that line is installed (see "The launcher's Versions folder") this editor
  starts it on the project and closes; the newest installed editor of the line is used (a `2.850.0` project keeps `2.850.1` when that
  is installed). Otherwise: "v2.850.x is not installed. Open the OkaySpace launcher > Versions to install it." with **Back** and **Cancel**.
  The editor that is started gets `--keep-version`, and shows `v2.850.1 (kept)` in the status bar. This editor closes first (without asking
  about unsaved changes again: that was asked before the project was left) and the other one is started after it has exited, through the
  helper process the self-updater uses, so the two never fight over the single-instance lock.

### Newer: open anyway or update

An older editor may drop data it does not understand when it saves. **Update OkaySpace** opens the same window as Engine > Check for
Updates (Download & Install; the editor reopens afterwards with the arguments it was started with). **Open anyway** first makes the same complete backup
copy as an upgrade (`<Name> (backup v2.860.0)`), changes nothing in the project, and then opens it. Why before opening rather than "before
the first save": the editor writes into the project in many places (new scripts, materials, settings, autosave), not only on Save, so
the only point that covers all of them is before the project is opened.

## The upgrade

All of it is stepped a little each frame (about 8 ms), so the window stays alive and shows files done / total, with a Cancel button
until the first file is rewritten.

1. **Look through the project** and list what it holds.
2. **Check every scene and prefab** (`.okayscene`, `.okayprefab`) through the engine: `SceneMigration::MigrateThroughEngine` (the
   registered `N -> N+1` steps, and the loader's own upgrades for old files), then a load of the result into a scratch scene. A file
   that is damaged, newer than this editor reads, or can't be loaded stops everything here, and so does a read-only file that would
   have to change. Nothing has been made or changed at this point.
3. **Check the free space** for the copy (the project's size plus 16 MB) and that the backup place can be written.
4. **Make the backup**: a full copy into the sibling folder `<Name> (backup v2.850.0)` (`(backup before v2.855.0)` when the old version is
   unknown; `... (2)`, `(3)` when the name is taken). It is built as `<name>.partial` and renamed only when it is whole, so a backup is
   either complete or not there. The copy is **verified**: every file's size, the number of items, and the content of the files that
   will be rewritten; a file that changes while it is being copied stops the upgrade.
5. **Rewrite only what changes**: a scene or prefab that is already current is not touched (not even its date); one that needs it is
   written back as canonical text, one file at a time, each whole-or-nothing. A file may not have changed since step 2.
6. **Stamp** `engine_version` last.

**It stops at the first failure and puts everything back.** The files rewritten so far are restored from the backup (their content is
checked against what was backed up), a project.okayproj the stamp had to create is removed, and the window says what failed and what was
done about it. The backup is kept. Because the stamp is written last, a crash or power loss in the middle leaves a project that still asks
next time, and running the upgrade again finishes the job (a second run finds nothing to rewrite).

### What the backup holds and leaves out

Everything in the project folder, byte for byte, dates kept, empty folders too. Left out: `.okay/backups` (the restore copies), build
output (a top-level `build` folder and the output folders named in Build Settings and its profiles, when they are inside the project),
and version control folders (`.git`, `.svn`, `.hg`). Links are copied as links, never followed. The upgrade only ever writes scenes, prefabs
and `project.okayproj`, so what the backup leaves out is never at risk. `<backup>/.okay/upgrade_backup.txt` records the version, the date, the
counts and the files the upgrade rewrote (with their size and a checksum); that is what Restore uses.

The backup goes next to the project, so it needs the free space of a copy and a writable parent folder. Choosing another place is not offered
yet (the sibling place is also the only one the Restore list can find).

### The result

"Upgraded 14 scenes, 3 prefabs; 2 files rewritten. Backup at ...", with **Open project** and **Open backup folder**. A failure shows the
reason (in plain words, with the file) and the restore result ("Every file that had been rewritten was put back from the backup: the
project is exactly as it was", or the files that could not be put back, whose originals are in the backup).

## File > Project Upgrade...

Shows the project's version and this editor's, and the backups next to the project.

- **Upgrade project to v...** runs the same upgrade again on the open project (after the usual Save / Don't Save question, and the scene is
  reopened afterwards). Running it twice changes nothing the second time.
- **Restore...** (per backup) puts back the files that upgrade rewrote (scenes, prefabs, `project.okayproj`) from that backup and removes a
  `project.okayproj` the upgrade had made. Every file it replaces is first copied to `<project>/.okay/backups/restore-N/`, so work done after
  the upgrade is not lost; files you made since are left alone; it is whole-or-nothing too. The editor then restarts on the project so the
  version question is asked afresh (nothing in memory can be written over the restored files). A backup made by Open anyway has nothing to
  restore. Folders that are not ours are listed with "open the folder to copy files by hand".

## The launcher's side: the Versions folder and `--keep-version`

This is the contract the launcher builds on (all of it in `shared/ProjectVersion.hpp`):

**Layout.** A release is `<launcher dir>/OkaySpace.exe` and `<launcher dir>/Tools/OkayEngine.exe` (with its own `SDL2.dll`). The launcher
keeps other versions as complete copies of `Tools`:

```
<launcher dir>/Versions/<version>/Tools/OkayEngine.exe      <version> = NormalizeVersion, e.g. 2.850.1
```

`InstalledEditorPath(launcherDir, version)`, `IsInstalled`, `InstalledVersions(launcherDir)` (newest first), `LauncherDirOf(editorExeDir)`
(works from `Tools` and from a kept copy). The editor looks in `LauncherDirOf(its own folder)/Versions`; started by hand from a build folder
there is no such folder, nothing is installed, and the project can still be upgraded or cancelled.

**Which editor opens a project.** `ReadProjectVersion(projectDir)` gives the stamp (`""` for a project made before this feature). To open it
with its own version: `ResolveKeepTarget(projectVersion, InstalledVersions(launcherDir))` is the newest installed editor of the project's line
(`""` when none is installed: offer to download it). Start `InstalledEditorPath(launcherDir, thatVersion)` with

```
--launcher <pid> --open "<scene>" --keep-version
```

(`KeepVersionArgs(scene)` plus the launcher's own `--launcher <pid>`). The editor that gets `--keep-version` skips the question for that project
for the session when the project is older or unversioned, and shows `v2.850.1 (kept)` in the status bar. A project that is *newer* than the
editor is still asked about (it is the data-loss question). `--keep-version=<version>` is honored only when that is the editor's own version.
An editor from before this feature ignores the flag and opens the project as it always did. `Decide` and `CompareVersions` replace the
launcher's own copy of the comparison (done: the launcher calls `okayver::CompareVersions`).

**One editor at a time.** The editor's single-instance lock and hand-off file are shared by all versions. An editor started while another
runs does not open: it writes its request (`open`, and `keep-version` with its own version) for the running one and exits. The running editor
honors `keep-version` only if it is that version; otherwise it asks its normal question. So to open a project with an older version while a
newer editor is open, close that editor first (the launcher's save-and-close request does this); the editor's own **Keep** closes itself first
and starts the other one after it has exited (the same helper process the self-updater uses).

**What the editor writes.** `engine_version` in `project.okayproj` as above; `<Name> (backup ...)` folders next to a project; under a project
`.okay/backups/restore-N/`. Nothing in `Versions/`.

**The launcher's half is built** ([launcher_versions.md](launcher_versions.md)): the Versions page installs, removes and prunes those folders
(whole or not at all, every download checked against `versions.json`), keeps the editor an update replaces, and offers the Latest / LTS
channel. Project cards show the project's version and an **older: v2.850.x** / **newer** badge; **Open with** starts
`InstalledEditorPath(...)` with `--launcher <pid> --open "<scene>" --keep-version`. A plain **Open** starts the current editor, which asks the
question above (the launcher asks nothing); only a project with no version goes to the launcher's default editor, if one is set. When the open
editor is another version, the launcher uses its save-and-close request first (one editor at a time). The "not installed" window has an
**Open launcher** button: it starts `OkaySpace --versions <version>` (through the same helper process), which shows the Versions page with
that version highlighted; a launcher that is already running is brought forward instead of a second one.

## Command line and environment

| | |
|---|---|
| `--keep-version` / `--keep-version=<ver>` | the version was chosen on purpose for the `--open` project (see above) |
| `OKAY_DEV=1` with `OKAY_FAKE_VERSION=2.855.0` | development builds only: the editor reports and stamps that version, so a project stamped `2.850.0` can be opened by "another" editor and the question tried |

## Failure cases

| What | What happens |
|---|---|
| A scene or prefab is damaged, can't be loaded, or is newer than the editor reads | the upgrade stops before the backup is started; the file is named; nothing changed |
| A file that has to change (or `project.okayproj`) is read-only | stops before the backup, says which file; nothing changed |
| Not enough free space, or the backup place is read-only or missing | stops before the copy; says how much is needed and free |
| The disk fails while copying | the unfinished backup is removed; the project was never touched |
| The disk fails, or a file changes under us, while rewriting | files rewritten so far are restored from the backup; the result window says so; the backup stays |
| Putting a file back fails too | the window lists the file; its original is in the backup |
| Cancel while working | same as a failure before the first rewrite: backup removed, nothing changed |
| The editor is a development build | no question; File > Project Upgrade shows it has no version to upgrade to |
| The editor crashes or loses power mid-upgrade | the stamp is written last, so the project asks again; the upgrade can be run again; the complete backup (if past its copy step) is there |

## Design choices

- **Backup before open for a newer project**, not before the first save: see above.
- **Check first, then back up.** Loading every scene before the copy means a damaged scene costs nothing (no copy, no cleanup).
- **Only upgrade and restore what changes.** The upgrade writes scenes, prefabs and the project file; it never moves or renames anything.
- **Restore restarts the editor.** It is the only way to be sure nothing in memory is written over the restored files, and it reuses the
  question and the gate rather than duplicating them.
- **A development build never writes a stamp.** `dev` is not a version, and a project stamped `dev` would be unreadable to a release.
- **A stamp is the last writer's.** After "Open anyway" and a save in an older editor the project says the older version, which is true.
- **Keep ignores `--keep-version` for a newer project**, because that flag means "I chose this version for an older project"; the data-loss
  question stays.

## Tests

`project_version` (parse and compare matrix, Decide table, the stamp line in text and in files, CRLF, BOM, read-only, release lines, the
keep target, the Versions layout; links nothing), `project_upgrade` (the upgrade end to end, backup completeness, space refusal, the disk
failing at every step in turn, damaged and read-only files, restore, the whole gate state machine) and `editor_hooks` (`EditorState::Load`
under the gate).

## What changed

New: `shared/ProjectVersion.hpp`, `editor/ProjectUpgrade.hpp/.cpp`, `editor/ProjectUpgradeUI.hpp/.cpp`. Edited: `editor/main.cpp` (hook calls),
`editor/EditorState.hpp/.cpp` (`beforeLoad`), `editor/CMakeLists.txt`. The editor's own copy of `CompareVersions` now calls the shared one.

Launcher side (see [launcher_versions.md](launcher_versions.md)): the launcher's own `CompareVersions` now calls the shared one too; the
"v2.850.x is not installed" window gained **Open launcher** (`editor/ProjectUpgradeUI.cpp`).
