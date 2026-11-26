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

## 3. Next

The changes follow in the same branch: the layout rules, new projects, safe save paths, the tidy-up of existing
projects and the `.txt` clean-up. This section is filled in with them.
