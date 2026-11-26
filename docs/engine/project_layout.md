# Project folder layout: audit and rules

Internal record (owner, 2026-11-26: "i want scenes and assests to in the projects. i want it to be like unity", "right now
its saved outside of the project", "there is also a lot of txt files, can you reduce it or clean it up").
System: Project Templates and Upgrades (`shared/ProjectCreate.hpp`, `editor/ProjectUpgrade.cpp`). The folder names follow
the layout most engines use (Unity was the reference for the folder names only; no code, text or pictures were taken).

## 1. The layout (target)

| Folder | What goes in it | Shipped in a build | Safe to delete |
|---|---|---|---|
| `Assets/` | everything the person makes or imports. New scenes go to `Assets/Scenes`, scripts to `Assets/Scripts`, and the other standard folders (`Prefabs`, `Materials`, `Models`, `Textures`, `Audio`) | what the scenes use | no |
| `ProjectSettings/` | `project.okayproj`, `build_profiles.cfg`, `scene_build.txt` (shared settings, kept in version control) | the values, not the files | no |
| `Library/` | caches: thumbnails (`Library/Thumbnails`) | no | yes, rebuilt |
| `Logs/` | the build report and the saved Console log of this project | no | yes |
| `Temp/` | scratch files while the editor runs | no | yes |
| `Builds/` | the default build output (`Builds/<Name>`) | it is the build | yes |
| `.okay/` | per-person editor state that stays (Project panel tabs and favourites, the trash, build dialog state, script history) | no | not the trash |

Older projects keep working: every reader looks in the new place first and then in the old one (`okayproj::SettingsFile`).
A project with no `ProjectSettings` folder keeps writing its settings where they are until it is tidied (section 4), so a
project is never half in one layout and half in the other.

## 2. Audit: where the programs wrote files (main at 3a1135e5f, before this change)

"cwd" is the working folder. On Windows the launcher starts the editor in the editor's own version folder, so a file
written to the working folder lands next to the program and is lost when the version changes.

### 2.1 Project data that landed outside the project

| What | Where it went | file:line |
|---|---|---|
| A never-saved scene with no project open | `cwd/Scenes/<name>.okayscene` | `editor/main.cpp:52342` (`DefaultScenePath`) |
| Save Scene As to any folder | wherever the dialog pointed, with no question; a scene saved outside `Assets` does not even make the folder the open project (`EditorState::Load` only walks up to an `Assets` folder) | `editor/main.cpp:52367`, `editor/EditorState.cpp:1047` |
| Default build output `build/MyGame` | relative to cwd: next to the editor | `editor/main.cpp:2973`, `editor/main.cpp:3040`, used by `editor/BuildGame.cpp:137` |
| Recovery copies of open documents (`okay_recovery/`) | cwd | `editor/main.cpp:52388` |
| Backups made on save (`okay_backups/`) | cwd | `editor/main.cpp:52404` |
| Create-asset actions with no project open (scripts, prefabs, materials, poses, imports) | `cwd/Assets/...` | `editor/main.cpp:1877`, `3388`, `4279`, `5872`, `11474`, `11710`, `17849`, `25413`, `28902`, `34170`, `44882`, `44905`, `45191`, `45203` |
| The Project panel with no project open | lists cwd | `editor/main.cpp:8732` |

### 2.2 Project data that landed in the project, but not where the layout says

| What | Where | file:line |
|---|---|---|
| A new project's first scene | `Assets/<Name>.okayscene` (not `Assets/Scenes`) | `editor/EditorState.cpp:1093`, launcher fallback `launcher/main.cpp:1901`, `okay project new` `tools/okay-cli/Cli.cpp:340` |
| `project.okayproj` | project root | `shared/ProjectCreate.hpp:392` (`SettingsPath`) |
| Build profiles | `.okay/build_profiles.cfg` | `editor/ProjectState.hpp:559` |
| Scenes In Build | `.okay/scene_build.txt` | `editor/SceneManagerPanel.cpp:82`, read by `tools/okay-cli/Cli.cpp:305` |
| Thumbnails cache | `.okay/thumbs` | `editor/ProjectBrowser.cpp:1067` |
| Build report | `.okay/build_report.txt` | `editor/BuildGame.cpp:625` |
| Old flat projects | scenes and assets next to `project.okayproj` | `examples/old_project` |

### 2.3 Readers that only understood scenes directly in `Assets`

These made a scene in `Assets/Scenes` (where Save As already defaulted) look like no project at all, or found the wrong root:

| What | file:line | Effect |
|---|---|---|
| `RootOfScene` | `shared/ProjectDelete.hpp:16` | `<root>/Assets/Scenes/A.okayscene` gave `<root>/Assets/Scenes` as the project root (used by the launcher's list, hide and delete) |
| `SceneIn`, `IsProjectFolder` | `shared/ProjectList.hpp:97`, `shared/ProjectDelete.hpp:24` | a project whose scenes are all in `Assets/Scenes` was not listed |
| `ProjectRootOfScene` (a copy of `RootOfScene`) | `editor/main.cpp:16042` | same as `RootOfScene` |
| Open Project Folder | `editor/main.cpp:16076` | looked for a scene in `Assets` and the root only |

### 2.4 Per-person files outside the person's settings folder

| File | Where | file:line |
|---|---|---|
| `okay_vs_tabs.txt` (open graph tabs) | cwd | `editor/VisualScriptEditor.hpp:3178` |
| `okay_vsnodes.txt` (add-node favourites, recent nodes) | cwd | `editor/VisualScriptPalette.hpp:338` |
| `okay_vs_usage.txt` (node usage counts) | cwd | `editor/VisualScriptUsagePrefs.hpp:45` |
| `okay_vs_bookmarks.txt` (graph bookmarks) | cwd | `editor/VsProjectRefs.hpp:221` |
| `plugins_disabled.txt` | next to the editor | `editor/PluginsWindow.cpp:260` |
| `console_log.txt` (Console > Save) | cwd | `editor/ConsolePanel.cpp:242`, `editor/ConsolePanel.cpp:375` |
| `okay_editor_perf.log` | next to the editor | `editor/main.cpp:2084`, `editor/HangWatchdog.cpp:180` |
| `okay_hang.txt` | next to the editor (the crash folder) | `editor/HangWatchdog.cpp:186` |
| `okay_crash.txt`, `okay_crashlog.txt` | next to the editor | `shared/CrashHandler.hpp:93` (the launcher reads them there: `shared/CrashReports.hpp:287`) |
| `okay_update.log` | next to the editor / launcher | `editor/Updater.cpp:200`, `launcher/main.cpp:722` |
| `launcher.cfg` | next to the launcher | `launcher/main.cpp:1332` |
| `okay_ipc_request.txt`, `okay_ipc_ack.txt` | next to the editor (a hand-shake with the launcher) | `editor/main.cpp:53401` |

Already in the person's settings folder (`%APPDATA%/OkaySpace/config/editor`): `okay_settings.txt`, `okay_recent.txt`,
`okay_layouts.txt`, `okay_custom_actions.txt`, `okay_projectview.txt`, `editor_layout.ini` (`editor/main.cpp:2573`), and
`okay_uihost.txt` (`editor/OkayUIHostMode.cpp:43`).

### 2.5 Every `.txt` the programs write

| File | Written by | Where | Why | How often |
|---|---|---|---|---|
| `okay_settings.txt` | editor | settings folder | editor preferences | on change |
| `okay_recent.txt` | editor | settings folder | recent scenes | on open / save |
| `okay_layouts.txt` | editor | settings folder | named panel layouts | on change |
| `okay_custom_actions.txt` | editor | settings folder | custom toolbar actions | on change |
| `okay_projectview.txt` | editor | settings folder | Project panel favourites (all projects) | on change |
| `okay_uihost.txt` | editor | settings folder | OkayUI host window state | on change |
| `okay_vs_tabs.txt`, `okay_vsnodes.txt`, `okay_vs_usage.txt`, `okay_vs_bookmarks.txt` | editor | cwd (moved, section 3) | visual script editor state | on change |
| `plugins_disabled.txt` | editor | next to the editor (moved) | editor plugins switched off | on change |
| `console_log.txt` | editor | cwd (moved) | Console > Save | when asked |
| `okay_hang.txt` | editor | next to the editor (moved) | what a stuck editor was doing | on a hang |
| `okay_crash.txt`, `okay_crashlog.txt` | editor, launcher | next to the program | crash report and history (trimmed: `CrashHandler.hpp:832`) | on a crash |
| `okay_crash_sent.txt` | launcher | next to the editor | which reports were sent | after sending |
| `okay_ipc_request.txt`, `okay_ipc_ack.txt` | launcher, editor | next to the editor | "save and close" hand-shake; the request is removed after use | on update / quit |
| `okay_update_floor.txt` | launcher | install folder | lowest version an update may go to | on update |
| `okay_testbuild.txt`, `DEV.txt`, `NOTES-DEV.txt` | launcher | install folder | test-build channel marker and notes | on install |
| `VERSION.txt`, `LTS.txt`, `NOTES.txt`, `install.txt`, `CHANGELOG.txt` | release, launcher | install / version folders | release files the launcher needs: kept | on install |
| `okayspace_launcher_open.txt`, `okayspace_notes_sb.txt`, `okayspace_changelog.txt`, `okayspace_official_ver.txt` | launcher, editor | system temp folder | downloads and a single-instance marker | per fetch |
| `account_server.txt`, `account_apikey.txt`, `password_reset_url.txt` | read only (launcher) | next to the launcher | optional overrides; never written | never |
| `playtime.txt`, `profile_showcase.txt` | launcher | achievements folder (per person) | play time, profile showcase | on change |
| `.okay/favourites.txt`, `.okay/projectview.txt` | editor | project | Project panel tabs and favourites of this project | on change |
| `.okay/recent_scripts.txt` | editor | project | recent scripts of this project | on open |
| `.okay/scene_build.txt` | editor | project (moved to `ProjectSettings`) | Scenes In Build | on change |
| `.okay/build_report.txt` | editor | project (moved to `Logs`) | the last build's report | each build |
| `.okay/build_history.txt` | editor | project | build history (Build Settings, Claude 3's lane) | each build |
| `.okay/upgrade_backup.txt`, `.okay/upgrade_report.txt` | editor | inside an upgrade backup folder | backup manifest and report | each upgrade |
| `build_report.txt`, `README.txt`, `THIRD_PARTY_NOTICES.txt` | editor | inside a build | shipped with the game | each build |
| `module_sizes.txt` | release packaging | next to the player | engine module sizes for the build report (read by `editor/BuildGame.cpp:591`); not written by the editor | per release |
| `sync_state.txt`, `current.txt`, `apply.txt` | engine, launcher | per-person data folders | save sync and content-update state | on change |
| `okay_bans.txt` | dedicated server | server folder | banned players | on change |
| `game.log`, `anticheat.log` (player) | a built game | the game's per-person save folder | the game's own log (only when Build Settings turn it on) | per run |

## 3. What changed (branch claude/project-layout-1)

- **The rules, in one place.** `okayproj::layout` in `shared/ProjectCreate.hpp`: the folder names, `SettingsFile` (new place
  first, old place second), `FirstScenePath`, `CheckSaveTarget` (inside Assets / elsewhere in the project / outside it /
  no project), `InsideCopyPath` (never an existing file), `ResolveInProject` (a relative build folder is inside the project),
  `DefaultBuildDir`, `LogFile`. `SettingsPath` and `HasSettingsFile` use them.
- **New projects** (editor, launcher, `okay new`) get `ProjectSettings/` and their first scene in `Assets/Scenes`.
- **Readers.** `RootOfScene` (the nearest `Assets` above the scene), `SceneIn`, `IsProjectFolder`, Open Project Folder, the
  editor's `ProjectRootOfScene` (now the shared one), `okay` `FindProjectRoot` and `MainScene`, the upgrade's `IsProjectRoot`
  and stamp: all understand `Assets/Scenes` and `ProjectSettings`.
- **Saving.** Save Scene As outside `Assets` asks first and offers `Assets/Scenes/<name>` instead (`SaveInsideProjectOrAsk`,
  `editor/ProjectUpgradeUI.cpp`). With no project open a new scene goes to the person's data folder, not the working folder.
  The default build folder is `Builds/<Name>`; a relative build folder (the old `build/MyGame` too) is inside the project.
- **Per-person files** leave the working folder and the program's folder: recovery copies and save backups go to
  `<user data>/Recovery` and `<user data>/Backups`; `okay_vs_tabs.txt`, `okay_vsnodes.txt`, `okay_vs_usage.txt`,
  `okay_vs_bookmarks.txt` and `plugins_disabled.txt` go to the editor's settings folder (`okay::edtabs::UserFile`);
  `okay_editor_perf.log` and `okay_hang.txt` go to the person's log folder. Each old file or folder is copied once and left
  where it was.
- **Project files**: Console > Save writes `Logs/console_log.txt`; the build report is `Logs/build_report.txt`; thumbnails
  are cached in `Library/Thumbnails`; `okay vc` ignores `/Library/`, `/Logs/`, `/Temp/`, `/Builds/`.
- **Logs are limited**: `okay::restart::AppendLog` (the perf log, the update log, the launch log) starts a new file past
  1 MB and keeps one `<name>.old`.

## 4. Tidying an existing project

When a project is opened and some of its files are not where the layout puts them, the editor lists the moves and asks
(once a session). "Not now" moves nothing; "Cancel" does not open the project. "Move the files" (`okayup::ApplyTidy`):

1. plans every move (`okayup::PlanTidy`): scenes, prefabs, materials, models, textures, sounds, scripts, graphs,
   dialogue, quests, behaviour trees, cutscenes and fonts next to `project.okayproj` go into the matching `Assets/<folder>`;
   `project.okayproj`, `.okay/build_profiles.cfg` and `.okay/scene_build.txt` go to `ProjectSettings/`; the old build report
   to `Logs/`; the old thumbnails to `Library/Thumbnails`. Sidecars (`.okaymeta` with the asset id, `.autosave`) move with
   their file. Data files the editor reads at the top of the project (`items.okayitems`, `strings.okayloc`, ...), notes and
   unknown files stay. A file whose new place is taken, a read-only file, and a file a read-only file names stay, with the reason;
2. checks again that every file to change is writable, then copies every file that will move or be rewritten into
   `<Name> (backup before tidy)` next to the project, checks each copy (size and content hash) and writes a manifest, so
   File > Project Upgrade > Restore can put it back;
3. copies each file to its new place and checks the copy;
4. rewrites the references found by the asset database's scanners (scenes, prefabs, materials, text assets, text models)
   and the Scenes In Build list. A path relative to the file that holds it stays relative to it; a project path stays a
   project path; an absolute path inside the project stays absolute. For a file at the top of the project both readings
   are the same: behaviour trees and clip files become project paths (their loaders look from the project folder), the
   rest relative paths (scene loaders look from the scene's folder);
5. only then removes each old file. One that cannot be removed stays next to its checked copy, and is listed.

A failure in steps 2 to 4 takes back everything it made: the project is byte for byte as it was, and no half backup is left.
The recent-scenes list follows a moved scene, and the scene that was being opened is opened from its new place. Running it
on a tidied project does nothing.

Tests: `tests/test_project_upgrade.cpp` ("tidying an old project"), `tests/test_project_create.cpp` (path rules),
`tests/test_project_list.cpp`, `tests/test_project_delete.cpp`, `tests/test_editor_tabs.cpp` (`UserFile`),
`tests/test_app_restart.cpp` (log limit), `tests/test_hang_watchdog.cpp`, `tests/test_okay_cli.cpp`.

## 5. Not done

- With no project open, the create-asset actions listed in 2.1 still write to `cwd/Assets`.
- The crash reports (`okay_crash.txt`, `okay_crashlog.txt`) stay next to the program: the launcher picks them up there to
  send them. Moving them needs the launcher and the editor changed together.
- `launcher.cfg`, `okay_update.log` and the launcher hand-shake files stay next to the launcher (the update flow reads them).
- `.okay/build_live.cfg` and `.okay/build_history.txt` stay in `.okay/` (Build Settings, Claude 3's lane).
- Root-level folders of an old project (for example `Textures/` next to `project.okayproj`) are not moved, only files.
- The launcher's "extra projects" list keeps the old scene path of a project opened from elsewhere and tidied; the
  launcher shows it as missing until it is opened again.
- `Temp/` is defined but nothing writes there yet.
