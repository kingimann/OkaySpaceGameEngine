# Install, update and uninstall

> **What this is:** The install / uninstall / editor versions audit and plan, and how we compare with Steam and the Unity Hub.
>
> **Status:** combined on 2026-11-20 from 2 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
>
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [Install, uninstall and editor versions: audit and plan (phase 1)](#install-uninstall-and-editor-versions-audit-and-plan-phase-1) (was `docs/install_audit.md`)
- [Install, update and uninstall: how OkaySpace compares with Steam, the Unity Hub and the Epic Games Launcher](#install-update-and-uninstall-how-okayspace-compares-with-steam-the-unity-hub-and-the-epic-games-launcher) (was `docs/install_comparison.md`)

## Install, uninstall and editor versions: audit and plan (phase 1)

*Was `docs/install_audit.md` until 2026-11-20.*

Status: **audit and plan only.** No program code, CMake or workflow was changed. Branch `claude/install-audit`, read from
`origin/main` at `fbdf8698` (2026-11-15).

The owner asked (2026-11-15): "i also want installing to looked into, how editor is installed from launcher, same with how
launcher is installed. make it proper with proper file management. make installing editors on launcher proper and better same
with uninstalling." A comparison with other launchers is in [install_comparison.md](install.md#install-update-and-uninstall-how-okayspace-compares-with-steam-the-unity-hub-and-the-epic-games-launcher) (a separate page on
purpose). In the proposal below, **Hub** and **Epic** are the two other launchers that page compares with.

### Read this first (one minute)

1. **An old editor can update itself in place and then open an old project with no question and no backup** (finding 1).
   That defeats the Versions page.
2. **"Remove version" can delete a project that was made inside the version folder**, and its window says projects are safe
   (finding 2). The editor's default project folder is beside the editor exe, and for a kept editor that is inside `Versions\`.
3. **Setup says your projects are in Documents. They are in the install folder** (finding 3). Nothing is lost today, but the
   promise is false.
4. **The silent installer can say "success" with nothing installed, and a full disk leaves a half-written folder** (finding 5).
5. **Uninstall leaves most of what the launcher made** (all of `Versions\`, logs, settings, the sign-in token) **and says
   nothing** (finding 11). That is the biggest answer to "make uninstalling proper".
6. The root cause of 2, 3, 4 and 11 is the same: **user data, settings, logs and programs all live in one folder** (the install
   folder), and the uninstaller is a hard-coded list of file names.

The plan (section 5) has 12 items. Items 1 and 2 are small and stop data loss; they should go first. Item 8 (move user data out
of the program folder) is the large one and needs the owner's decision.

### How I checked, and what the letters mean

I could not run the real launcher or editor (no Windows, no display). I did two things that do run here:

* **Installer on wine.** The repo's own `scripts/okayspace-installer.nsi`, built with `makensis` from a dummy distribution laid out
  exactly like `scripts/build-windows-dist.sh` writes it (the same folders and file names, 14 files here where the real one has more
  example files; the programs are tiny real Windows exes), and
  run under wine 9.0. This wine cannot run 32-bit programs, so I built a 64-bit copy of the same script (the only difference is
  one added `Target amd64-unicode` line in a scratch copy). Silent mode (`/S`) only: the dialogs cannot be clicked headless.
* **Two small Linux programs** in my scratch folder (not in the repo; the code is in Appendix B), compiled against the real
  `launcher/VersionsManager.cpp` and `editor/ProjectUpgrade.cpp`.

Letters used in tables and findings:

| Letter | Meaning |
|---|---|
| **C** | Traced in code: file and line in the tree above. |
| **R** | Ran it here (the wine runs, or the Linux programs). The result is what I saw. |
| **G** | Guess: Windows behaviour I know in general but could not run. |

Short names for files: `nsi` = `scripts/okayspace-installer.nsi`; `L` = `launcher/main.cpp`; `E` = `editor/main.cpp`;
`VM` = `launcher/VersionsManager.cpp`; `VN` = `launcher/VersionsNet.cpp`; `VU` = `launcher/VersionsUI.cpp`;
`PV` = `shared/ProjectVersion.hpp`; `PU` = `editor/ProjectUpgrade.cpp`.

In the wine runs I created the "user files" myself, with the names and places the code uses (so **C** decides what the programs
write, and **R** shows what the installer does with it). The folder names in the output below are Windows paths inside the wine
prefix; `%LOCALAPPDATA%` was `C:\users\root\AppData\Local` there.

---

### 1. File map

Every file and folder the installer, the launcher, the editor and the player create or read for the user. "Install folder" is
`%LOCALAPPDATA%\OkaySpace` after Setup (nsi:18), or the unzipped folder for the zip. Kinds: PROGRAM (replaceable), USER (the
person's own work), STATE (settings), LOG, CACHE.

| # | Path | Folder it is in | Made by | Kind | Uninstall removes? | "Remove version" removes? | Update keeps it? | Evidence |
|---|---|---|---|---|---|---|---|---|
| M1 | `OkaySpace.exe`, `SDL2.dll`, `README.txt`, `VERSION.txt`, `steam_api64.dll` (Steam builds) | install folder | Setup or zip | PROGRAM | yes, by name | - | Setup overwrites; self-update replaces only `OkaySpace.exe` and writes `VERSION.txt` | C nsi:110,136-141; L:1171,1207. R |
| M2 | `Tools\OkayEngine.exe`, `Tools\OkaySpacePlayer.exe` | install folder | Setup or zip; self-update | PROGRAM | yes, by name | - | replaced by both | C nsi:148-149; L:1152,1159. R |
| M3 | `Tools\okayspace-relay.exe`, `Tools\okayui_d3d11_demo.exe`, `Tools\SDL2.dll`, `Tools\VERSION.txt`, `Tools\accounts.md`, `Tools\steam_api64.dll` | install folder | Setup or zip | PROGRAM | yes, by name | - | only Setup replaces them; self-update never does | C nsi:150-155. R |
| M4 | `Assets\Textures\*`, `Assets\Fonts\*`, `examples\<12 folders>\*` | install folder | Setup or zip | PROGRAM (samples). People also put their own files here | **whole folders removed** (`RMDir /r`), user files too | - | Setup overwrites, edits included | C nsi:157-160. R |
| M5 | `Uninstall.exe` | install folder | Setup | PROGRAM | yes | - | rewritten | C nsi:115,163. R |
| M6 | `OkaySpace.lnk` in the Start Menu Programs folder and on the Desktop (Start in: the install folder) | per-user shell folders | Setup | shortcut | yes | - | re-created on every update, also when the person deleted it | C nsi:112-113,130-131. R |
| M7 | Registry `HKCU\Software\OkaySpace\InstallDir`; `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\OkaySpace` (8 values) | HKCU | Setup | install facts | yes | - | rewritten by Setup only. No program updates `DisplayVersion` | C nsi:117-126,165-166. R. No other registry writes (no file types): C |
| M8 | `launcher.cfg`, `notifications.cfg`, `news.cfg`, `scheduled.cfg`, `chat_seen_<account>.cfg` | install folder | launcher | STATE | yes, by name | - | kept | C L:4740 (not atomic, no error check),2152,4547,6109,2076; nsi:142-145,147. R |
| M9 | `okay_update.log`, `okay_selfupdate.state` (the editor's own `okay_update.log` is in `Tools\`) | install folder, `Tools\` | launcher, editor | LOG, STATE | **no** | - | kept | C L:807,829; E:2138,2166; shared/SelfUpdate.hpp:50. R |
| M10 | `OkaySpace.exe.old`, `.new`, `.failed`; `Tools\*.old`, `Tools\*.new` | install folder, `Tools\` | self-update | backups, scratch | only `OkaySpace.exe.old` | - | `OkaySpace.exe.old` removed at the next start | C launcher/UpdaterCore.hpp:83-85,128; L:17969; nsi:137. R |
| M11 | `Projects\<name>\` (the launcher's default for new projects; `projects_dir=` in `launcher.cfg` changes it) | install folder | launcher | USER | **no, on purpose** | - | kept | C L:19541,4647,1696; nsi:132-135. R |
| M12 | `Tools\Projects\<name>\`; for a kept editor `Versions\<v>\Tools\Projects\<name>\` (the editor's default when it has no recent project) | beside the editor exe | editor | USER | `Tools\Projects`: no, on purpose. `Versions\...`: no (all of `Versions\` stays) | **deleted with the version** | kept | C E:18642-18647. R (Remove) |
| M13 | `community\`, `downloads\` (marketplace zips and `.part` files) | install folder | launcher | USER | no, on purpose | - | kept | C L:1892,591; nsi:132-135. R. No resume: docs/status/unfinished_launcher.md:42 |
| M14 | `Games\Official\` (the official game) with its `.okayofficial\` journal | install folder | launcher | PROGRAM, plus any save the game writes beside itself | **whole folder removed** | - | updated by the launcher's own game updater | C L:1795; shared/OfficialGame.hpp:16-21; nsi:161. R (a file in `Saves\` was deleted) |
| M15 | `Versions\<v>\Tools\{OkayEngine.exe, OkaySpacePlayer.exe, SDL2.dll}`, `Versions\<v>\install.txt` | install folder | launcher (Versions page, update) | PROGRAM | **no** | the whole folder | kept; an update adds a "kept" copy and prunes extras | C VM:96-102,471-579,628-656; docs/release/launcher_versions.md:24-38. R |
| M16 | `Versions\.partial-<v>-N\`, `Versions\.removing-<v>-N\` | install folder | launcher | scratch | **no** | - | deleted at the next launcher start | C VM:47-50,499,643,612-626; VU:706. R |
| M17 | `Tools\okay_crash.txt`, `Tools\okay_crashlog.txt` (kept editor: `Versions\<v>\Tools\...`) | beside the editor exe | editor | LOG | no | deleted with the version | kept | C E:1001-1056,7693; the launcher reads only `Tools\`: L:2232-2237. R |
| M18 | `Tools\plugins_disabled.txt` | beside the editor exe | editor | STATE | no | deleted with the version | kept | C E:9316-9320. R |
| M19 | `imgui.ini`, `okay_recent.txt`, `okay_settings.txt`, `okay_layouts.txt`, `okay_projectview.txt`, `okay_custom_actions.txt` | the **current folder** of the editor process (see finding 12) | editor | STATE | no | only if the editor was started inside a version folder | kept | C E:5355,2534,2555,5329,8096,2707. R (shortcut "Start in") |
| M20 | `okay_ipc_request.txt`, `okay_ipc_ack.txt` | beside the editor exe | launcher, editor | transient | - | deleted with the version | - | C E:47585-47615; L:1510-1519 |
| M21 | `<project>\project.okayproj`, `.okay\thumbs\`, `.okay\build_profiles.cfg`, `.okay\build_live.cfg`, `.okay\favourites.txt`, `.okay\build_report.txt`, `.okay\backups\restore-N\`, and `<Name> (backup v...)` next to the project | the project folder | editor | USER | never touched | never touched (unless the project is inside it, M12) | - | C E:3095-3123,4504,1654; docs/editor/project_upgrade.md; tests/test_launcher_versions.cpp:1036-1063 |
| M22 | `%APPDATA%\OkaySpace\session.txt` (sign-in token), `accounts.db` (local accounts), `servers.cfg`, `ipc\` | roaming profile | launcher, engine | USER, STATE | **no** | - | kept | C engine/include/okay/Platform/Account/AccountService.hpp:859,883,1259-1262; GameIpc.hpp:67-70; L:18119,18149,11058. R |
| M23 | `%LOCALAPPDATA%\OkaySpace\achievements.txt`, `playtime.txt`, `profile_showcase.txt` | **the same folder as the default install folder** | engine, launcher | USER | no | - | kept | C engine/include/okay/Platform/Achievements.hpp:28-40; L:1401,12941. R |
| M24 | `%APPDATA%\<company>\<title>\` (a built game's saves and prefs; "Save to user folder" is on by default) | roaming profile | player | USER | never touched | - | - | C player/main.cpp:1438-1447; editor/ProjectState.hpp:72 |
| M25 | `%TEMP%\okayspace_media\`, `okayspace_release.json`, `okayspace_ver_*.txt`, `okayspace_notes_sb.txt`, `okayspace_official_*`, `okayspace_launcher_open.txt`, `okayspace_editor.lock` and `.handoff` | temp folder | launcher, editor | CACHE | no (Windows clears temp) | - | - | C L:3645,877,965,1019,16713; VM:956-959; E:47346-47352. The launcher's single-instance lock is a Windows mutex, not a file: L:10709 |
| M26 | Not installed, only released: `OkayEngine.debug` (symbols), the zip, the single exes, `CHANGELOG.txt` | GitHub release | release workflow | - | - | - | - | C .github/workflows/release.yml:219-228; scripts/build-windows-dist.sh:202-215 |

Two things the table shows:

* **Three different base folders for per-user data, and a fourth for the rest.** Settings and projects are beside the exe (M8,
  M11), account data is in `%APPDATA%\OkaySpace` (M22), achievements and playtime are in `%LOCALAPPDATA%\OkaySpace` (M23), which
  by default **is** the install folder. The engine already has one rule for this (`okay::Paths`,
  `engine/include/okay/Core/Paths.hpp`: data, config, cache and log folders per platform), but its per-user functions
  (`UserDataDir`, `ConfigDir`, `CacheDir`, `LogDir`) have no caller in the launcher, editor, player or engine code (C: `git grep`).
* **The programs never write the registry** except Setup (C), so no file type is registered (`.okayscene`, `.okayproj`): double-click
  in Explorer does not open the editor (G for what Windows shows then).

---

### 2. Installer scenarios

Each scenario says what the code does, what I ran, and what can go wrong. Wine runs are silent (`/S`) and use the 64-bit copy
of the same script (Appendix A has the commands).

#### S1. Fresh install

* **Code.** Default folder `$LOCALAPPDATA\OkaySpace`, no admin (nsi:18-20). The folder page is shown (nsi:86). `File /r` copies
  the whole distribution (nsi:110), two shortcuts (112-113), `Uninstall.exe` (115), two registry keys (117-126).
* **Ran.** Silent install: 14 files plus `Uninstall.exe`, two shortcuts, two keys. Both shortcuts have "Start in" = the install
  folder (I read the `.lnk`). Apps & features entry has 8 values: name, version, publisher, icon, location, uninstall string,
  `NoModify`, `NoRepair`. No size, no quiet uninstall string.
* **Can go wrong.** The default folder contains the Windows user name (non-ASCII: finding 6). No check for write permission, free
  space (on an update the folder page is skipped, nsi:72-76), OneDrive or length. The Setup welcome text is wrong (finding 3).

#### S2. Update over an existing install

* **Code.** `.onInit` looks for an old install in four places, first hit wins: the registry `InstallDir`, the Apps & features
  `InstallLocation`, the folder Setup runs from, the default folder (nsi:31-54). A hit means "update": no folder page. `File /r`
  overwrites every file in the distribution.
* **Ran.** My `launcher.cfg`, `Projects\` and `Versions\` were kept. Files Setup ships were overwritten, **including an example
  scene and `README.txt` I had edited**. A file a newer release no longer ships (`Tools\old_tool.exe`) stayed for ever (Setup
  never deletes). The Desktop shortcut I had deleted came back. Apps & features showed the new version.
* **Can go wrong.** Mixed versions if one file is locked (S5). A full disk breaks a working install (S8). User edits to shipped
  files are lost without a warning.

#### S3. Update over an unzipped copy, and zip plus installer

* **Code.** The third place `.onInit` looks is `$EXEDIR`, the folder Setup itself sits in (nsi:36). It counts only if
  `OkaySpace.exe` **and** `Tools\OkayEngine.exe` are there (nsi:49-50). The zip does not contain Setup (release.yml:170-180).
* **Ran.** Setup run from outside an unzipped copy: a **second copy** in the default folder; the unzipped copy untouched. Setup
  copied *into* the unzipped folder and run there: that folder is updated in place, `Uninstall.exe` and the shortcuts are written
  for it, and Apps & features points at it. It keeps its name `OkaySpace-windows-v<old version>` for ever.
* **Can go wrong.** Two copies share `%LOCALAPPDATA%\OkaySpace` data (M23) and `%APPDATA%\OkaySpace` (M22), and the launcher's
  single-instance lock has no folder in its name (L:10709), so starting copy B only brings copy A forward (finding 15).

#### S4. An older Setup over a newer install

* **Ran.** Setup 9.9.8 over 9.9.10: it downgraded in silence. `VERSION.txt` and Apps & features went back. No version is compared
  anywhere in the script.
* **Can go wrong.** Someone runs an old download. Projects saved by the newer editor then ask the version question; the launcher
  updates itself again at the next start (a loop of surprises, not damage).

#### S5. Installing while programs are running

* **Code.** Setup tries to open three exes for writing and asks the person to close them: `OkaySpace.exe`,
  `Tools\OkayEngine.exe`, `Tools\OkaySpacePlayer.exe` (nsi:58-70, 99-104). Nothing else is checked: not
  `Tools\okayspace-relay.exe`, not `Tools\okayui_d3d11_demo.exe`.
* **Ran.** Launcher running, silent Setup: **exit code 2, nothing changed, no message** (the wine trace shows the write-open fails
  with a sharing violation, so the script's message box is skipped in silent mode and Setup aborts). Relay running, silent Setup:
  **exit code 0** and every file updated **except the relay**: a mixed-version folder.
* **Can go wrong.** In normal (not silent) mode NSIS shows its own "Error opening file for writing" dialog with Abort, Retry,
  Ignore for the unchecked programs (G). "Ignore" gives the same mixed folder.

#### S6. A path with spaces, non-ASCII letters, or on OneDrive

* **Ran.** Setup and uninstall into `C:\Test Space\Ünï Ökay 日本` both worked (NSIS is Unicode).
* **Can go wrong.** The launcher, not Setup, is the problem: it starts programs with the ANSI code page (finding 6). OneDrive (G):
  `%LOCALAPPDATA%` is not redirected, but the Desktop shortcut goes to the OneDrive Desktop when "Known Folder Move" is on and
  then appears on the person's other PCs (finding 21). An unzipped copy inside a synced Documents or Downloads folder puts
  programs, `Versions\` (about 75 MB per version, from the sizes in docs/release/release_channels.md) and `Projects\` under a sync client.

#### S7. No write permission

* **Ran (silent).** A folder that cannot be created: **exit code 0**, shortcuts and an Apps & features entry written, all pointing
  at nothing (finding 5). A path whose parent is a file: Setup silently used the **default** folder instead.
* **Code.** There is no probe for writing. Per-user Setup into `C:\Program Files` would fail without admin (G). If an admin
  installs there by hand, the launcher cannot save settings (`SavePrefs` never checks the stream, L:4740-4745) and cannot
  self-update (docs/release/updater.md row "Rename refused").

#### S8. Low disk space

* **Ran.** A 700 KB volume and a 1.3 MB distribution: **exit code 2**, 8 of 14 files written, one exe cut short (188,416 of
  250,092 bytes), **no `Uninstall.exe`, no registry entry, no shortcut**. So nothing is registered and nothing cleans it up.
* **Code.** Files are written straight over the old ones (no staging, no `.old`: nsi:110). On an update the same failure damages a
  working install (finding 5). The in-place self-update is the careful one: stage, verify, swap, roll back (docs/release/updater.md:20-33).
* **Guess.** On a fresh install the NSIS folder page shows space needed and free (G).

#### S9. Uninstall

* **Code.** A fixed list (nsi:129-167): two shortcuts; about 20 files by name; `RMDir /r` of `Assets\Textures`, `Assets\Fonts`,
  `examples`, `Games\Official`; `RMDir` (only if empty) of `Tools`, `Assets`, `Games` and the folder itself; both registry keys.
  The comment says projects, `community\` and `downloads\` are kept on purpose (nsi:132-135).
* **Ran.** After use (59 files in the folder), uninstall removed 26 files and left 33 (list in finding 11). Removed: all
  installer files, the five `.cfg` kinds, `Games\Official` with a save I put in it, my own files in `Assets\Textures` and
  `examples\`, `Uninstall.exe`, both shortcuts, both registry keys. Left: `Versions\` whole (with scratch folders),
  `Projects\`, `Tools\Projects\`, `community\`, `downloads\`, every log and state file, all editor settings, `OkaySpace.exe.new`
  and `.failed`, `Tools\*.old`, `achievements.txt` and friends, and everything under `%APPDATA%\OkaySpace` (sign-in token,
  accounts, servers). The install folder stays. Setup's `Delete "$INSTDIR\servers.cfg"` (nsi:146) aims at the wrong folder: the
  real file is in `%APPDATA%\OkaySpace` and stayed. (The `OkaySpace.desktop` file in the wine Desktop is wine's own copy of the
  shortcut, not ours.)
* **Can go wrong.** No check for running programs: **uninstalling with the launcher open left `OkaySpace.exe` behind, removed the
  Apps & features entry and the shortcuts, and returned 0** (finding 7). The uninstall pages are the defaults (nsi:90-91): they
  do not say what stays.

#### S10. Reinstall after uninstall

* **Code.** The registry keys are gone, so Setup uses the default folder. `Projects\` and `Versions\` are still there and are
  found again if the folder is the same (the launcher scans `<install>\Projects`: L:1820). Settings are gone (`launcher.cfg` was
  deleted), so a custom `projects_dir=` is forgotten. The editor's recent list comes back (`okay_recent.txt` was never removed).
* **Can go wrong.** A different folder does not find the old `Projects\` or `Versions\`.

#### S11. Silent install (`/S`)

* **Ran.** `/S` uses the default folder (or the old install's). `/D=<folder>` is honoured only when **no** install is found: with
  an install present Setup updates the old folder and ignores `/D=` (S12). Exit codes seen: 0 ok, 2 aborted (launcher running, full
  disk), **0 when nothing was installed** (finding 5). No log file. No flag to refuse a downgrade (S4).

#### S12. A second copy side by side

* **Ran.** With an install present, `Setup /S /D=C:\Second Copy` updated the first copy and never created `C:\Second Copy`. So a
  second copy is not possible through Setup. By zip it is possible (S3).

#### S13. The launcher's in-place self-update against Setup

* **Code.** The self-update stages, verifies and swaps exactly three programs (`OkayEngine.exe`, `OkaySpacePlayer.exe`,
  `OkaySpace.exe`) and writes the top `VERSION.txt` (L:1152,1159,1171,1207). It never touches `SDL2.dll`, the relay, the D3D11
  demo, `Tools\VERSION.txt`, `README.txt`, `examples\`, `Assets\`, Apps & features or shortcuts. Setup overwrites all of them.
  They do not fight over a file at the same time: the self-update refuses while the editor or a game runs (L:1181) and Setup
  refuses while three exes run.
* **Over time** the folder drifts: new programs, install-time examples and relay, and after the first self-update Apps & features
  shows the install-time version (no program writes the registry). Only a Setup run brings everything back in step (finding 16).
* **Setup's Finish page** starts the launcher, which checks for updates at once; if the feed is newer it self-updates the files Setup
  just wrote. That is normal.

#### S14. Zip and installer together

See S3 and S12: two copies, shared per-user data, one single-instance lock. The zip copy updates itself in its own folder; Setup
and Apps & features know only the Setup copy.

---

### 3. Editor versions from the launcher

How the Versions page installs, removes and keeps editors today, case by case. "Handled" means the code or a test does the
right thing; the right-hand column is what is still missing.

| # | Case | What happens today | Handled? | Evidence | Gap |
|---|---|---|---|---|---|
| V1 | Install | Download and check in `Versions\.partial-<v>-N\Tools`, write `install.txt`, rename the folder into place as the last step. The version exists whole or not at all. | Yes | VM:471-579; test `launcher_versions` "installing: the good path" (tests/test_launcher_versions.cpp:466-521) | - |
| V2 | Cancel | The curl child is stopped, the `.part` file removed, "Cancelled. Nothing was installed." Cancel at five points is tested. | Yes | VN:54-55; VM:520-539; tests:577-604 | The bytes already downloaded are thrown away (no resume, R: Appendix B, R5) |
| V3 | Failed download | The next address is tried (bucket, then GitHub). The last reason is shown. | Partly | VM:529-551 | Many curl failures show as "The download failed (curl exit code 28)." (VN:179). No retry |
| V4 | Full disk | Refused before downloading if free space is under need + 16 MB (VM:495). Disk fills mid-download: curl exit 23 becomes "The disk filled up". The scratch folder is removed. | Yes | VM:494-496; VN:178; tests:606-679 | No pause-and-continue; free space is read once at the start |
| V5 | Checksum mismatch | Not kept; the other source is tried; then "did not match its checksum, so it was not installed". | Yes | VM:541-547; tests:523-575 | - |
| V6 | Remove while that editor runs | Refused only when the launcher knows which version the open editor is (it started it). An editor opened by hand is "assumed to be this version". Otherwise Windows should refuse the rename: "Is one of its files in use?" | Partly | VU:235-246; VM:641,646-647; docs/release/launcher_versions.md:100-101 | The message does not say which program; the hand-started case can mislead (G for the rename failing) |
| V7 | Remove the default editor | Refused: "it is your default editor. Choose another default editor first." | Yes | VU:239-240; tests:733-735 | - |
| V8 | Remove a version a project needs | **Allowed, no check.** The window says projects "can be opened with it again after you install it". | **No** | VU:657; R4 (Appendix B) | Should say which projects need it |
| V9 | Remove a version with a project inside | **Deletes the project.** | **No** | VM:646-650; R1 | Finding 2 |
| V10 | Keep previous versions: N | An update first copies the running editor into `Versions\<v>` if 256 MB would stay free, then prunes extra "kept" copies. Never pruned: versions you installed, the newest patch of the LTS line, the default editor. | Yes | VM:689-766; L:1129-1135,1210-1216; tests:768-893 | Pruning knows nothing about projects (R4) |
| V11 | Leftover `.partial-*` / `.removing-*` | Deleted at the first frame after the launcher starts. A failed delete after a remove says "deleted the next time the launcher starts". | Yes | VM:612-626,651; VU:706; tests:752-763 | Uninstall leaves them (R). No age or owner check (R3, finding 20) |
| V12 | Permissions | A file in the way of `Versions` is a plain failure (tested). A read-only launcher folder gives "Couldn't create the Versions folder (...)". | Yes | VM:490-492; tests:644-652 | `launcher.cfg` and the rest fail silently (finding 19) |
| V13 | Antivirus locks a file | The final rename and Remove's rename are tried once. | **No** | VM:571,646 | Finding 10 (G) |
| V14 | A very slow connection | curl gives up below 1 KB/s for 60 s; our own HTTP client has 60 s per read and 6 h in all. The bar shows bytes. | Partly | VN:117-118,146-147; VU:606-613 | No speed, no time left, no resume |
| V15 | Launcher closed in the middle | A normal close cancels and waits for the thread. A crash or kill leaves the scratch folder (cleaned next start) and **the `curl` program keeps running** (no job object). | Partly | VU:767-773; VN:43 | Finding 17 (G for the orphan) |
| V16 | Windows long paths | No `longPathAware` in the manifest. A scratch file adds about 60 characters to the install folder path. A too-long path gives "Couldn't create a folder to download into (...)". | Partly | cmake/okayspace.exe.manifest; VM:503 | Finding 22 |
| V17 | `Versions` on another drive | Not possible: always `<launcher dir>\Versions`. A junction or symlink should work because every rename stays inside the folder (G, not run on Windows). | **No** | PV:395 | Plan item 12 |
| V18 | A kept editor updates itself | **Replaces the version-labelled file with the newest editor.** | **No** | E:48214,48257-48274,2124-2175 | Finding 1 |
| V19 | Crash reports of a kept editor | Written beside it; the launcher never reads them; Remove deletes them. | **No** | E:1001-1056; L:2232-2237 | Finding 18 |
| V20 | A damaged version | Listed as installed; Install says "already installed"; no verify or repair. | **No** | PV:403-407; VM:480; R2 | Finding 9 |

---

### 4. Findings, ranked (worst first)

Severity words: **data loss**, **broken install**, **confusing**, **cosmetic**. "Confidence" is C, R or G as defined above.

#### Data loss

**1. A kept editor updates itself in place, then opens the old project with no question and no backup.**
* **What.** Every editor, also one that runs from `Versions\<v>\Tools\`, checks for a newer release at start and shows the
  "Engine Update" window (E:48214,48257-48274,19281-19330). "Download & Install", or the saved option "Automatically install
  future updates", replaces the running `OkayEngine.exe` in place (E:2124-2175). The version folder, `install.txt` and the
  Versions page still say v2.850.1, but the file is the newest editor. The editor restarts with its old arguments (E:2163),
  which hold the bare `--keep-version` that "Open with" passes (PV:426). A bare flag is honoured by **any** version
  (PU:821-826), so the new editor opens the older project silently (PU:829-835): no upgrade question, no backup.
* **Who.** Everyone who uses "Open with" or the Versions page. Worst for anyone who ticked "Automatically install future
  updates" once: that choice is saved in `okay_settings.txt` in the shared current folder (E:2561,2629, finding 12), so then
  every kept editor updates itself at start.
* **Reproduce.** Linux, the gate step: Appendix B `repro_gate` (R): a project stamped 2.850.0 and an editor 2.860.0 ask the
  question; add a bare `--keep-version` and it opens silently ("may open silently = YES"); a flag that names 2.850.1 is ignored by
  2.860.0. The update chain is Windows only, unverified (use a **copy** of a project): install an older version, "Open with" it,
  click Download & Install.
* **Severity.** Data loss risk (a project re-saved by a newer editor with no backup). **Confidence.** C, gate step R.

**2. "Remove version" deletes a project that was made inside the version folder, and says projects are safe.**
* **What.** `Remove` renames the whole `Versions\<v>` folder and deletes it (VM:646-650). The editor's default folder for a new
  project is `<folder of the editor exe>\Projects` when it has no recent project (E:18642-18647). For a kept editor that is
  `Versions\<v>\Tools\Projects`. The size in the window counts those files, and the window says "Your projects are not touched"
  (VU:657).
* **Who.** Anyone who makes a project in an editor started from Versions while the recent list is empty (a fresh
  `okay_recent.txt`, or after "Clear").
* **Reproduce.** Appendix B `repro_versions` R1 (R): a project under `Versions/2.850.1/Tools/Projects/MyGame` is gone after `Remove`.
* **Severity.** Data loss. **Confidence.** R for the removal, C for the default folder.

**3. Setup says projects are in Documents. They are in the install folder.**
* **What.** The Setup welcome page says "your games, projects and saves (in your Documents folder) are never touched" (nsi:82).
  No program puts projects in Documents. The launcher's default is `<install>\Projects` (L:19541), the editor's
  `Tools\Projects` (E:18647). Documents appears only as a shortcut in the file browser (shared/FileDialog.cpp:722). Update and
  uninstall do leave projects alone (R), so nothing is lost today. The risk is the belief: a person (or a cleanup tool) treats
  the "program folder" as disposable.
* **Who.** Every user. **Reproduce.** Read nsi:82 and L:19541; R shows `Projects\` survives uninstall.
* **Severity.** Data loss risk through a false promise. **Confidence.** C.

**4. Uninstall deletes the person's own files in four folders.**
* **What.** `RMDir /r` of `Assets\Textures`, `Assets\Fonts`, `examples` and `Games\Official` (nsi:157-161) removes everything inside,
  not only what Setup put there. docs/process/review_log.md:151 already lists this as low.
* **Who.** People who keep their own textures or scenes in those folders (the README tells them to copy from them); anyone whose
  official-game saves sit beside the game.
* **Reproduce.** Wine run (R): `Assets\Textures\my_own_texture.png`, `examples\my_own_scene\scene.okayscene`, an edited example
  and `Games\Official\Saves\slot1.sav` were all deleted.
* **Severity.** Data loss (small). **Confidence.** R.

#### Broken install

**5. Silent Setup can report success with nothing installed; a full disk leaves a half-written folder; updates overwrite in place.**
* **What.** (a) A folder that cannot be created: exit code **0**, with shortcuts and an Apps & features entry that point at an empty
  place. (b) A path whose parent is a file: Setup silently installs into the **default** folder. (c) A full disk: exit code 2,
  8 of 14 files written, one exe cut short (188,416 of 250,092 bytes), no uninstaller, no registry entry, nothing to clean up.
  (d) On an update Setup writes the new bytes straight over the old files (nsi:110): case (c) on an update leaves a **broken
  working install**. The self-update does it carefully (stage, verify, swap, roll back: docs/release/updater.md:20-33); Setup does not.
* **Who.** Anyone installing to a full or protected drive; any IT script that trusts the exit code.
* **Reproduce.** Wine (Appendix A): `/D=` to an uncreatable folder; a 700 KB tmpfs as the target. **Severity.** Broken install.
* **Confidence.** R for silent mode (64-bit copy of the same script). The normal (dialog) mode shows NSIS's own error dialogs: G.

**6. A Windows user name with characters outside the system code page breaks starting the editor and the player.**
* **What.** The default install folder contains the user name. The launcher starts programs with `CreateProcessA` and ANSI strings
  (L:1444,333,408); the editor's crash handler writes with `GetModuleFileNameA` (E:1001). None of the three exes has a UTF-8 code
  page or long-path setting (cmake/okayspace.exe.manifest; all three call `okay_win_version`). docs/release/launcher_versions.md:152-154
  and the open row in docs/status/unfinished_launcher.md ("A launcher folder with non-ASCII characters cannot start the editor") say so.
  The version **download** is fine: it uses `CreateProcessW` (VN:43). Setup and uninstall are fine (R, S6).
* **Who.** People whose account name has characters outside the Windows system code page (for example a Chinese or Cyrillic name
  on an English Windows). It hits the **default** install, not only odd folders.
* **Reproduce.** Windows only, unverified: install, copy the folder to `C:\Test Ünï 日本` on an English Windows, press Play.
* **Severity.** Broken install. **Confidence.** C and documented; Windows unverified.

**7. Uninstalling while the launcher runs leaves a half-removed program and still says success.**
* **What.** The uninstall section never checks for running programs (nsi:129-167; the check at nsi:58-70 is only called by Setup).
* **Who.** Anyone who uninstalls with the launcher open (it can sit in the tray: L:17957).
* **Reproduce.** Wine (R): `OkaySpace.exe` running, `Uninstall.exe /S`: exit code 0, the registry keys and shortcuts gone, `OkaySpace.exe`
  still in the folder. Apps & features no longer lists it, so there is nothing to run again.
* **Severity.** Broken install. **Confidence.** R.

**8. Updating while a program Setup does not check is running gives a mixed-version folder and a success code.**
* **What.** Setup checks three exes (nsi:99-104). Wine (R): the relay running, silent Setup returned **0**, every file new except
  `Tools\okayspace-relay.exe`. `okayui_d3d11_demo.exe` is the same.
* **Severity.** Broken install. **Confidence.** R in silent mode; the dialog mode G.

**9. A damaged version cannot be repaired, and a zero-byte editor counts as installed.**
* **What.** `IsInstalled` only asks whether the editor file exists (PV:403-407). A version without `SDL2.dll` or the player, or with
  a zero-byte editor, is listed, and `Install` answers "already installed" (VM:480). Only Remove then Install fixes it.
  Nothing re-checks an installed version. There is no flush before the rename either, so a power cut right after an install may
  leave an empty file (G). The only repair today is for the running install and only for missing exes ("Get the editor":
  L:19640, 1084).
* **Reproduce.** Appendix B R2 (R). **Severity.** Broken install. **Confidence.** R.

**10. The last rename of an install, and a Remove, are tried once.** *(Guess.)*
* **What.** `fs::rename(scratch, finalDir)` (VM:571) and Remove's `fs::rename(dir, trash)` (VM:646) fail the first time a virus
  scanner or an open Explorer window holds a file or the folder. The messages are "Couldn't move the new version into place
  (...)" and "Is one of its files in use?". No retry, no hint which program.
* **Severity.** Broken install (retry by hand). **Confidence.** G (code C; Windows behaviour not run).

#### Confusing

**11. Uninstall leaves most of what the launcher made, and does not say so.**
* **What.** Wine (R): after simulated use the install folder held 59 files (15 from Setup, 44 that the programs would make);
  uninstall removed 26 and left 33: all of `Versions\`
  (about 75 MB per version in the release example, plus scratch folders), `okay_update.log`, `okay_selfupdate.state`,
  `OkaySpace.exe.new`, `.failed`, `Tools\*.old`, `Tools\` crash logs and `plugins_disabled.txt`, six editor settings files,
  `achievements.txt`, `playtime.txt`, `profile_showcase.txt`, and the on-purpose set (`Projects\`, `Tools\Projects\`, `community\`,
  `downloads\`). Also left: everything in `%APPDATA%\OkaySpace` including `session.txt`, the **sign-in token**. The folder stays.
  The uninstall pages are the defaults and say nothing about it (nsi:90-91). Only part of it is on purpose (nsi:132-135); the rest
  is what a fixed list never learned about.
* **Severity.** Confusing, wastes disk, leaves a token. **Confidence.** R.

**12. Editor settings are saved in the current folder, so they land wherever the editor happened to start.**
* **What.** `imgui.ini`, `okay_recent.txt`, `okay_settings.txt`, `okay_layouts.txt`, `okay_projectview.txt`,
  `okay_custom_actions.txt` are opened with relative paths (E:5355,2534-2542,2555,5329,8093-8096,2703-2707); nothing sets the
  current folder. Both shortcuts start the launcher in the install folder (R) and the launcher starts the editor with no folder
  given (L:1444), so they land in the install folder. An editor started by double-click in `Tools\` or a version folder keeps its
  own set. The Layouts window says "Saved in okay_layouts.txt beside the editor" (E:5569), which is not true in the usual case.
  This is also why "Automatically install future updates" is shared by every editor version (finding 1).
* **Severity.** Confusing. **Confidence.** C, and R for the "Start in" folder.

**13. Setup does things people do not expect.**
* **What.** (R) An older Setup silently downgrades (S4). An update overwrites a shipped file the person edited (an example scene,
  `README.txt`). A Desktop shortcut the person deleted comes back. Files dropped from a release stay for ever.
* **Severity.** Confusing. **Confidence.** R.

**14. Apps & features shows too little and goes stale.**
* **What.** (R) 8 values: no size (`EstimatedSize`), no quiet uninstall string, no help link. `UninstallString` is **not quoted**
  (`C:\...\OkaySpace\Uninstall.exe`; with a space in the user name that is the classic unquoted-path weakness, G). The self-update
  changes the programs but no program writes the registry (C), so after the first self-update the entry shows the install-time
  version (nsi:120 is the only place `DisplayVersion` is written).
* **Severity.** Confusing, cosmetic. **Confidence.** R and C.

**15. A second copy is impossible through Setup, and zip plus installer makes two copies that share data and one lock.**
* **What.** S3 and S12 (R). The launcher's lock is the mutex `Local\OkaySpaceLauncherSingleton` (L:10709): the second copy only
  brings the first forward. Both copies share `%LOCALAPPDATA%\OkaySpace` (M23) and `%APPDATA%\OkaySpace` (M22).
* **Severity.** Confusing. **Confidence.** R and C.

**16. The self-update replaces three programs; the rest of the folder drifts.**
* **What.** L:1152,1159,1171,1207 (C). Not updated: `SDL2.dll`, the relay, the D3D11 demo, `Tools\VERSION.txt`, `README.txt`,
  `examples\`, `Assets\`, Apps & features, shortcuts. After some updates new programs sit beside install-time examples.
* **Severity.** Confusing. **Confidence.** C.

**17. Version downloads cannot resume or retry, show raw curl codes, and can leave an orphan.**
* **What.** Every attempt starts at byte 0: `fs::remove(tmp)` before each try (VM:532) and `fs::remove(dest)` (VN:107). Appendix B R5
  (R): attempt 2 starts with 0 of attempt 1's bytes. No retry. The bar shows bytes only (VU:606-613). Errors other than "file not
  found" and "disk full" read "The download failed (curl exit code 28)." (VN:178-179). If the launcher is killed, `curl.exe` keeps
  running (no job object, VN:43) and the scratch folder cannot be deleted until it ends (G). `curl.exe` is built into Windows 10
  1803 and later (from memory, not checked); on older Windows: "The download program (curl) could not be started." (VN:164).
* **Severity.** Confusing. **Confidence.** R and C.

**18. Crash reports of a kept editor are never shown and die with Remove.**
* **What.** The editor writes them beside its exe (E:1001-1056); the launcher reads only `Tools\` (L:2232-2237).
* **Severity.** Confusing. **Confidence.** C.

**19. Launcher settings are saved without checking and not whole-or-nothing.**
* **What.** `SavePrefs` opens `launcher.cfg` with truncate and never checks the stream (L:4740-4745). A read-only folder (an unzip
  into a protected place, a sync client holding the file) loses every setting with no message; a crash mid-write leaves a cut file.
  The editor writes its files whole-or-nothing (E:2671). Already noted for the read-only case: docs/status/unfinished_launcher.md:52.
* **Severity.** Confusing. **Confidence.** C.

**20. The start-up clean-up of scratch folders has no owner or age check.**
* **What.** `CleanStale` removes every `.partial-*` and `.removing-*` (VM:612-626; VU:706). Appendix B R3 (R): a scratch folder
  created a moment ago is removed. Safe while one launcher runs. The lock is per Windows session (`Local\`), so a second signed-in
  session of the same user could run a second launcher on the same folder (G).
* **Severity.** Cosmetic today. **Confidence.** R and G.

#### Cosmetic or guessed

**21. OneDrive and synced folders.** *(Guess.)* The Desktop shortcut follows OneDrive's Desktop and shows on other PCs; unzipping
into a synced folder puts `Versions\` and `Projects\` under a sync client that can lock files during a swap. **Confidence.** G.

**22. Long paths.** A scratch file is `Versions\.partial-<v>-N\Tools\OkaySpacePlayer.exe.part`: about 60 characters on top of the
install folder. With no `longPathAware` (cmake/okayspace.exe.manifest) an install folder deeper than about 190 characters fails
with "Couldn't create a folder to download into (...)" (VM:503): a plain message the person cannot fix. The zip has a top folder
`OkaySpace-windows-v<version>` (release.yml:174-177) and "Extract All" adds a second with the same name. **Confidence.** C; Windows
unverified.

**23. Wrong or stale statements.** `launcher/VersionsManager.hpp:18-19` and docs/release/release_channels.md:80 say a download without a
checksum may go on if "the person confirms", but docs/release/launcher_versions.md:54-56 and docs/release/updater.md say there is no such path
(`PlanInstall` refuses). The feature map 18.1.4 says "user data is never touched" (docs/feature_map/18-build-export-release.md:17):
uninstall deletes `launcher.cfg`, chat state and user files in four folders (finding 4). The Versions button "Open install folder"
opens `Versions\`, not the install folder (VU:307-311). The Versions page is not in the feature map (docs/status/unfinished_launcher.md:77).
Also the sentences in findings 2, 3 and 12.

**24. No install log, and no "open logs" button.** Setup writes no log (a silent install gives only an exit code). Logs are in
three places (the install folder, `Tools\`, and Setup has none). Settings > Maintenance > Configuration folder opens the install
folder (L:22838); the Versions page opens `Versions\`. **Confidence.** C.

---

### 5. Proposal

Twelve items, ordered by value over risk. "Existing / Gap" follows the rule not to build a second copy of anything: I ran
`python3 scripts/find_existing.py install uninstall version`, which found no open branch on this and no claim about it (one claim
on issue #422 mentions the words, but it is about WAV audio); the feature map has
18.1.4 (installer), 17.1.2 (self-updater) and 17.14 (official game install); the Versions page is not mapped (finding 23).

**Idea seen in** uses the key from the top of this page: Steam, Hub, Epic, or none. The evidence for each is in
[install_comparison.md](install.md#install-update-and-uninstall-how-okayspace-compares-with-steam-the-unity-hub-and-the-epic-games-launcher).

| # | Change in plain words | Size | Owner decision? | Idea seen in |
|---|---|---|---|---|
| 1 | Kept editors never update themselves; a bare `--keep-version` is not trusted | small | no | Hub, Epic |
| 2 | Remove and prune never delete a folder that holds a project; the Remove window names the projects that need the version | small | no | Hub |
| 3 | Setup that is honest and safe: true welcome text, pre-checks, all programs checked, real exit codes, a log, no downgrade by accident, staged update with rollback | medium | no | Steam, Epic |
| 4 | An uninstaller that asks about projects and settings (default keep), removes everything else including `Versions\` and the sign-in token, and lists what it kept | medium | **yes** (D2) | Hub, Epic |
| 5 | Versions page: verify and repair, storage total and free space, "remove copies no project needs", Show in Explorer | medium | no | Steam, Hub, Epic |
| 6 | Downloads that survive a bad connection: resume, retry, pause, speed and time left, plain errors, no orphan | medium | no | Steam, Epic |
| 7 | Paths: wide-character process launch, UTF-8 and long-path manifest | medium | **yes** (D10) | none |
| 8 | Keep user data out of the program folder, with a migration that never moves a project without asking | large | **yes** (D1, D9) | Steam, Hub, Epic |
| 9 | One logs folder, "Open logs folder", "Copy diagnostics", a Setup log | small | no | Hub, Epic |
| 10 | Apps & features done properly (size, quoted and quiet uninstall, version kept in step) and a Desktop shortcut that is made once | small | **yes** (D7, D11) | none |
| 11 | File types `.okayscene` and `.okayproj`: first a launcher router by project version, then an opt-in registration | medium | **yes** (D5) | Epic (from memory) |
| 12 | A setting for where editor versions live (another drive) | medium | **yes** (D4) | Steam, Hub, Epic |

Suggested order for the coordinator: **1 and 2 first** (small, stop data loss, touch `editor/`, `shared/`, `launcher/Versions*`);
**3, 4 and 10 as one agent** (they all edit `scripts/okayspace-installer.nsi`); **5, 6 in one agent** after 2 (same files); **7** on
its own; **8 after the owner answers D1 and D9**; 9 anywhere; 11 and 12 after their decisions. Item 4 removes `Versions\`, so it
must either wait for item 2 or itself keep any folder that holds a `project.okayproj`.

#### 1. Kept editors never update themselves

* **Change.** An editor whose exe sits in a kept copy (`okayver::LauncherDirOf` says the folder is `Versions\<v>\Tools`) skips the
  start-up check, never shows the "Engine Update" window, never auto-installs, and shows one line: "This is a kept copy of v2.850.1.
  Update OkaySpace from the launcher", with the existing **Open launcher** button. The launcher passes `--keep-version=<version>`
  (the form that names a version, honoured only by that version); a bare `--keep-version` is honoured only by a kept copy.
* **Files.** `editor/main.cpp` (check, pick-up, popup, `InstallUpdate`), `editor/ProjectUpgrade.cpp/.hpp`, `shared/ProjectVersion.hpp`
  (a pure `IsKeptCopyDir`), `launcher/VersionsManager.cpp` (`OpenWithArgs`).
* **Test.** Linux unit tests in `project_version`, `project_upgrade`, `launcher_versions`. Appendix B `repro_gate` becomes the
  first failing test.
* **Owner can try it.** Versions page > install an older version > Open with it: no "Engine Update" window; the status bar says
  "(kept)"; its Settings do not offer automatic updates.
* **Existing / Gap.** Existing: the update window and the `--keep-version` gate. Gap: no kept-copy guard, bare flag trusted.
* **Size** small. **Owner decision** no (safe default: never update a kept copy).

#### 2. Remove and prune never delete a folder that holds a project

* **Change.** Before deleting `Versions\<v>`, look for `project.okayproj` (a few levels deep). If found: refuse with plain words
  ("This version folder has a project of yours: ... Move it out first") and an Open folder button. The Remove window also says "3 of
  your projects were made with v2.850.x" (the launcher already reads project versions for the badges). A bulk "Remove copies no
  project needs".
* **Files.** `launcher/VersionsManager.hpp/.cpp` (`FindProjectsIn`, `ProjectsNeeding`), `VersionsUI.cpp`, `tests/test_launcher_versions.cpp`.
* **Test.** Linux unit tests; Appendix B R1 and R4 become the failing tests.
* **Owner can try it.** Make a project in a kept editor's default folder; press Remove on that version: refused, with the path.
* **Existing / Gap.** Existing: `RemoveGuards` (running, launcher, default, in use). Gap: no project check at all.
* **Size** small. **Owner decision** no.

#### 3. Setup that is honest and safe

* **Change.** (a) Welcome text that says where projects are and what is kept. (b) Before touching anything: the folder can be
  created and written (write and delete a probe file), free space is enough (`${GetSize}` and `${DriveSpace}` from NSIS's own
  FileFunc include), and warnings for `Program Files`, OneDrive and non-ASCII folders. (c) Check **every** program in the
  distribution, and ask to close them; in silent mode exit with a clear code and a message. (d) Stage the update (extract beside,
  check the key files, swap with rename-aside `.old`, delete `.old` at the end, roll back on error). (e) Never write the Apps &
  features entry or shortcuts unless the files are in place. (f) A log at `%TEMP%\OkaySpace-setup.log`. (g) Ask before replacing a
  newer install (silent mode: refuse unless told).
* **Files.** `scripts/okayspace-installer.nsi` only.
* **Test.** The wine harness of this audit, turned into `scripts/test_installer_wine.sh` (skipped when `makensis` or wine is
  missing, like the other script tests); `makensis` already runs in the release job.
* **Owner can try it.** Run Setup into a folder you cannot write: it refuses with a message. Run Setup with the launcher open, then
  with the relay open: a dialog each. `OkaySpace-Setup.exe /S` in a terminal: `echo %errorlevel%` is non-zero when it failed.
* **Existing / Gap.** Existing: `WaitUntilClosed` (nsi:58-70), `UseIfInstalled` (nsi:46-54). Gap: all of (b) to (g).
* **Size** medium. **Owner decision** no (safe default: refuse, ask, log).

#### 4. An uninstaller that asks, removes the rest, and lists what it kept

* **Change.** The uninstall page says what will happen and offers one box, "Also remove my projects, settings and sign-in" (default
  **off**). Always: stop if a program runs; remove all program files **including `Versions\`** (keeping any folder that holds a
  project, and saying so), logs, state, `.new`, `.failed`, `.old`, the temp files, and the **sign-in token**; remove the shortcuts and
  registry keys; remove the folder when empty. The last page lists "Kept: ..." with the folders. Replace the hand-written delete
  list with the list of files Setup installed (generated from the distribution at build time), so a person's own files in
  `examples\` and `Assets\` are not deleted.
* **Files.** `scripts/okayspace-installer.nsi`, a small generator script and a step in `release.yml`, docs.
* **Test.** The wine harness; the "after use" tree from this audit (59 files) becomes the expected-leftovers assertion.
* **Owner can try it.** Install, make a project, install a version, uninstall from Settings > Apps: the page asks; afterwards
  `%LOCALAPPDATA%\OkaySpace` holds only the projects (when kept) and `%APPDATA%\OkaySpace` has no `session.txt`.
* **Existing / Gap.** Existing: the uninstall section (nsi:129-167). Gap: no question, no sweep, misses `Versions\`, logs, state and
  the token, deletes user files in shared folders, no running check.
* **Size** medium. **Owner decision** yes: D2 (default keep).

#### 5. Versions page: verify, repair, storage, bulk remove, Show in Explorer

* **Change.** A **Verify** button per version re-checks size and SHA-256 of the editor and player against `versions.json` (and
  `SDL2.dll` against the launcher's copy); **Repair** downloads only what is wrong. A total with free space on the drive, a
  "damaged" mark, **Show in Explorer** per version, and the bulk remove from item 2.
* **Files.** `launcher/VersionsManager.hpp/.cpp` (`VerifyInstalled`, `Repair`), `VersionsUI.cpp`, `tests/test_launcher_versions.cpp`.
* **Test.** Linux unit tests with a temp folder and the injected downloader. Appendix B R2 is the first failing test.
* **Owner can try it.** Delete `SDL2.dll` from a version folder, press Verify: "1 problem"; press Repair: fixed.
* **Existing / Gap.** Existing: `VerifyFile` (VM:457-469), sizes (`ListInstalled`), the Remove confirm, "Get the editor" for missing
  exes. Gap: nothing verifies an installed version; no total, no bulk, no Explorer link.
* **Size** medium. **Owner decision** no.

#### 6. Downloads that survive a bad connection

* **Change.** Keep the `.part` and resume it (curl `-C -`, or a `Range` request in our own client; the full SHA-256 still decides);
  three retries with waits; **Pause** and **Resume** (pause = stop but keep); speed and time left; a plain sentence for each common
  curl code (6, 7, 28, 35, 56, 60); stop `curl.exe` when the launcher dies (a Windows job object) and clean orphans at start; pause
  instead of failing when the drive is full.
* **Files.** `launcher/VersionsNet.cpp`, `VersionsManager.cpp` (a scratch name that stays per version so a restart can resume),
  `VersionsUI.cpp`, tests (the stub `curl` tests already exist: tests:1121-1206).
* **Owner can try it.** Start an install, press Pause, close the launcher, open it, press Resume: it continues from where it was.
* **Existing / Gap.** Existing: the cancellable runner, speed-limit flags, `DiskFull`. Gap: resume, retry, pause, speed, plain codes.
* **Size** medium. **Owner decision** no.

#### 7. Paths: UTF-8 everywhere and long paths on

* **Change.** `CreateProcessW` with `okay::restart::Utf8ToWide` (it exists in `shared/AppRestart.hpp`) in `RunCmd`, `SpawnHidden`,
  `SpawnTracked`; `GetModuleFileNameW` in the crash handler; `activeCodePage` UTF-8 and `longPathAware` in
  `cmake/okayspace.exe.manifest` (older Windows ignores both and the wide calls still work); a plain message for a path over
  about 200 characters.
* **Files.** `launcher/main.cpp` (three helpers), `editor/main.cpp` (crash handler), `cmake/okayspace.exe.manifest`.
* **Test.** Linux: syntax check with the MinGW compiler. Wine: a small program started through the new helper from a `Ünï` folder
  (my harness already runs programs from such a folder). CI: the `windows-cross` job compiles.
* **Owner can try it.** Copy the install folder to `C:\Test Ünï 日本`, start the editor and a game from the launcher.
* **Existing / Gap.** Existing: the documented limitation; wide-character code in `VN:43` and `shared/AppRestart.hpp`. Gap: most
  launch paths still use `CreateProcessA`.
* **Size** medium. **Owner decision** yes: D10 (safe default: wide calls first, manifest after one Windows test).

#### 8. Keep user data out of the program folder

* **Change.** One rule for per-user paths (reuse `okay::Paths`; settings, state, logs and caches in `%LOCALAPPDATA%\OkaySpace\...`,
  account data stays in `%APPDATA%\OkaySpace`). The editor's files get **explicit** paths instead of the current folder. New projects
  default to `Documents\OkaySpace Projects` for new people; existing folders stay where they are and stay listed. A migration card
  on the first start: "Move your settings to <path>?" (copy first, keep a `.migrated` copy for one release); projects default to
  **Leave**, with a Move button that copies, verifies, and only then asks about deleting the old. A `portable.txt` beside the exe
  keeps today's layout for zip users.
* **Files.** `launcher/main.cpp` (about 20 places use `g_exeDir`), `editor/main.cpp` (about 10 relative files), a new
  `shared/UserPaths.hpp`, the installer text, tests.
* **Test.** Linux unit tests for the planner (pure: which file goes where, what is left, never deletes) and temp-folder tests; wine
  for the folders Setup creates.
* **Owner can try it.** After the update, Settings shows a "Data folder" with Open folder; the install folder holds only
  programs; a project made from the launcher is in Documents.
* **Existing / Gap.** Existing: `okay::Paths` (engine/include/okay/Core/Paths.hpp), `account::DefaultConfigDir`,
  `Achievements::Dir`, the launcher's `projects_dir=` and library folders. Gap: nothing uses them for settings; three base folders.
* **Size** large. **Owner decision** yes: D1 and D9.

#### 9. One logs folder, "Open logs", "Copy diagnostics", a Setup log

* **Change.** Settings > Maintenance gets **Open logs folder** and **Copy diagnostics** (versions, paths, free space, last update
  result, last crash line). Logs move to one `logs\` folder after item 8 (before it: show all three paths). Setup writes
  `%TEMP%\OkaySpace-setup.log` and copies it into `logs\`. Each version gets **Show in Explorer**.
* **Files.** `launcher/main.cpp` (Settings rows), `VersionsUI.cpp`, the installer. **Test.** Linux unit test for the diagnostics
  text; wine for the Setup log. **Owner can try it.** Settings > Maintenance > Copy diagnostics, paste into Notepad.
* **Existing / Gap.** Existing: Settings > Maintenance > Open folder (L:22838), `okay_update.log`, the Crash reports page. Gap: scattered.
* **Size** small. **Owner decision** no.

#### 10. Apps & features done properly, and a Desktop shortcut made once

* **Change.** Write `EstimatedSize`, quote `UninstallString`, add `QuietUninstallString`; `URLInfoAbout` only if the owner names a
  page; let the launcher write `DisplayVersion` after a successful self-update (one registry write, Windows only); a Start Menu
  folder entry; the Desktop shortcut on the first install only, with the choice remembered (`DesktopShortcut=0`).
* **Files.** `scripts/okayspace-installer.nsi`, one function in `launcher/main.cpp` after the update succeeds.
* **Test.** The wine harness reads `user.reg`. **Owner can try it.** Settings > Apps > Installed apps: size and version are right
  after a self-update.
* **Existing / Gap.** Existing: the ARP block (nsi:117-126). Gap: finding 14. **Size** small. **Owner decision** yes: D7, D11.

#### 11. File types

* **Change.** Nothing is registered today (C, R). First make `OkaySpace.exe --open <scene>` choose the editor by the project's
  version (the logic exists: `ChooseEditorForOpen`, `OpenWithChoices`). Then an opt-in Setup box registers `.okayscene` and
  `.okayproj` per user (`HKCU\Software\Classes`, no admin) with the verb `OkaySpace.exe --open "%1"`; uninstall removes them.
* **Files.** `launcher/main.cpp` (argument), `launcher/VersionsManager.cpp` (route), the installer. **Test.** Linux unit test for
  routing; wine registry dump. **Owner can try it.** Double-click a `.okayscene`: the right editor opens.
* **Existing / Gap.** Existing: `--open`, `--keep-version`, Open with. Gap: no launcher router, no registration.
* **Size** medium. **Owner decision** yes: D5 (default: not registered until routing exists).

#### 12. Choose where editor versions live

* **Change.** A setting "Keep other versions in: <folder>" (default `<install>\Versions`), stored in `launcher.cfg`; the editor
  finds it through a small file beside the launcher or a `--versions-dir` argument. A **Move** button copies, verifies and then
  removes the old one. Until then, document the junction trick (`mklink /J`, G).
* **Files.** `shared/ProjectVersion.hpp` (`VersionsDir` takes a root), `VersionsManager/UI`, `editor/ProjectUpgradeUI.cpp`.
* **Test.** Linux unit tests with a different root and across a symlink. **Owner can try it.** Settings: set `D:\OkaySpace\Versions`,
  install a version, "Open with" it.
* **Existing / Gap.** Existing: `okayver::VersionsDir(launcherDir)` (PV:395). Gap: no setting. **Size** medium.
  **Owner decision** yes: D4.

---

### 6. What I could not check

Everything on a real Windows machine, and why:

* **The real launcher and editor.** No Windows, no display. Every statement about what they do is from code (C), except the two
  Linux programs.
* **The real (32-bit) installer.** This wine runs only 64-bit programs, so I ran a 64-bit build of the same script. The script
  logic is identical; WOW64 differences do not matter for `HKCU` and `$LOCALAPPDATA`.
* **Dialog mode.** Wine ran headless, so I only ran `/S`. The message boxes (including the "still running" Retry/Cancel and NSIS's
  own write-error dialog), the folder page and the Finish page are unverified.
* **Apps & features / Settings > Installed apps.** Wine's registry shows the values; how Windows 10 and 11 display and run the entry
  (unquoted string, size, repair) is unverified.
* **Real file locking.** Wine reproduced a sharing violation on a running exe, which is the case that mattered, but it is not
  Windows. Virus scanners, Explorer holding a folder, and OneDrive sync locks were not tested at all.
* **Permissions.** I ran as root, so "no write permission" was only tested with paths that cannot be created. ACLs, `Program Files`
  and UAC were not.
* **Code pages.** Whether a particular user name breaks `CreateProcessA` depends on the system code page. Unverified.
* **Windows long paths and `curl.exe`.** Not run; `curl.exe` and its proxy and certificate behaviour on older or managed Windows are
  unverified.
* **Code signing and SmartScreen.** Out of scope (CLAUDE.md rule 14), but an unsigned Setup shows a warning on first run (G).
* **Linux and macOS.** Parked (rule 14).

---

### 7. Owner decisions needed

The coordinator picks a default and carries on. The **safest default** is the first one named in each line.

| # | Question | Recommended default |
|---|---|---|
| D1 | Where should **new projects** go by default? | `Documents\OkaySpace Projects` for new people; **existing projects stay where they are** and are never moved without asking. Safest until decided: keep today's folder, but make Remove and uninstall refuse to delete a folder that holds a project (items 2 and 4). |
| D2 | Should uninstall ask "Remove my projects and settings too?" | Yes, default **No, keep**. Always remove the sign-in token and every program file. |
| D3 | Should the launcher offer **optional components** (player runtime, relay, examples, debug symbols) that can be added or removed later? | Not yet. The Setup list in item 4 is the first step; revisit after items 3 and 5. |
| D4 | Should editor versions be installable on **another drive**? | Yes, as a setting later (item 12); document the junction trick now. |
| D5 | Register `.okayscene` and `.okayproj` (double-click opens the editor)? | Not until `OkaySpace.exe --open` picks the editor by project version; then an **opt-in** box. |
| D6 | May an **older Setup** replace a newer install? | No in silent mode; ask in normal mode. |
| D7 | Desktop shortcut: when? | Make it on the first install only; do not re-create it after the person deleted it. |
| D8 | Install scope: per-user only, or also "for all users"? | **Per-user only** (no admin). The self-update and the settings files need a folder the person can write; the comparison shows the other launchers split on this. |
| D9 | A **portable mode** (zip keeps everything beside the exe) next to the data-folder layout? | Yes: a `portable.txt` marker, shipped in the zip, not in Setup. |
| D10 | Flip the programs to the **UTF-8 code page** and long paths in the manifest? | Do the wide-character calls first; add the manifest keys after one Windows test by the owner. |
| D11 | A support or website link in Apps & features (`HelpLink`)? | Omit until there is a stable page. |

New decisions the comparison raised are D3, D4, D8 and D9; the others came from the audit itself. Everything here is Windows only
(CLAUDE.md rule 14); nothing touches Linux or macOS.

---

### Appendix A. How the wine runs were made

Only in my scratch folder, nothing in the repo. Short version, so the coordinator can repeat it:

```
# a dummy distribution laid out like scripts/build-windows-dist.sh (14 files; the exes are tiny real Windows programs that sleep)
makensis -V1 -DVERSION=9.9.9 -DDISTDIR=$PWD/dist-9.9.9 -DOUTFILE=$PWD/Setup-9.9.9.exe installer-amd64.nsi
#   installer-amd64.nsi = one line "Target amd64-unicode" + the unmodified scripts/okayspace-installer.nsi
export WINEPREFIX=<scratch>/prefix WINEDEBUG=-all; unset DISPLAY
/usr/lib/wine/wine64 Setup-9.9.9.exe /S                          # install; then read C:\users\root\AppData\Local\OkaySpace and user.reg
/usr/lib/wine/wine64 "C:\users\root\AppData\Local\OkaySpace\Uninstall.exe" /S    # uninstall; then `wineserver -w`, then list what is left
# folders with spaces and non-ASCII: /usr/lib/wine/wine64 cmd /c "<setup> /S /D=C:\Test Space\Ünï Ökay 日本"
# full disk: mount -t tmpfs -o size=700k tmpfs /mnt/tinyfs ; Setup /S /D=Z:\mnt\tinyfs\OkaySpace ; umount /mnt/tinyfs
```

What each run gave (silent mode, 64-bit copy of the same script):

| Run | Exit code | State afterwards |
|---|---|---|
| Fresh install | 0 | 14 files + `Uninstall.exe`, 2 shortcuts (Start in = install folder), 2 registry keys (8 values in Apps & features) |
| Update 9.9.9 to 9.9.10 | 0 | user files kept; shipped files overwritten (an edited example and `README.txt` lost); stale file kept; Desktop shortcut re-created |
| Older Setup 9.9.8 over 9.9.10 | 0 | downgraded; Apps & features shows 9.9.8 |
| Launcher running, Setup | **2** | nothing changed, no message |
| Relay running, Setup | **0** | every file new except the relay |
| Uninstall with the launcher running | 0 | `OkaySpace.exe` left; registry and shortcuts gone |
| `/D=` folder that cannot be created | **0** | registry, Apps & features and shortcuts point at nothing |
| `/D=` below a path where a file is in the way | 0 | installed into the **default** folder instead |
| `/D=C:\Second Copy` with an install present | 0 | the first copy updated; no second copy |
| Setup copied into an unzipped folder, run there | 0 | the unzipped folder updated; Apps & features points at it |
| 700 KB volume | **2** | 8 of 14 files, one exe cut short; no uninstaller, no registry, no shortcut |
| `C:\Test Space\Ünï Ökay 日本` install and uninstall | 0 | both worked |

### Appendix B. The two Linux programs (scratch code, not in the build)

Written for this audit from the public interfaces of `launcher/VersionsManager.hpp` and `editor/ProjectUpgrade.hpp`. No outside
code. Compiled with `g++ -std=c++17` next to the real `launcher/VersionsManager.cpp`, `launcher/VersionsNet.cpp`, the nine files
in `engine/src/Crypto/` (for the signed index) and, for the gate, `editor/ProjectUpgrade.cpp` linked with an existing engine
library. They are the failing tests that plan items 1, 2 and 5 should start from.

Output of `repro_versions` (R):

```
== R1: a project that was made inside a kept editor's folder, then the version is removed
   Versions page would say: this version uses 9 KB (the project's files are counted in it)
   Remove() -> Removed: "Removed v2.850.1."
   project folder still exists after Remove: no   <- the confirm window said 'Your projects are not touched'
== R2: a version folder that is damaged (player and SDL2.dll gone, e.g. removed by a virus scanner)
   listed as installed: YES
   Install() on it -> AlreadyInstalled: "v2.850.0 is already installed."   (no repair: only Remove then Install fixes it)
   zero-byte editor counts as installed: YES
== R3: start-up clean-up of scratch folders has no age or owner check
   CleanStale() removed 1 scratch folder(s); the fresh one still exists: no
== R4: pruning and removing know nothing about projects
   PlanPrune(keep previous = 1) would remove: v2.850.1   (the signature has no 'projects that need it' input)
   Remove(2.700.0) with no check for projects stamped 2.700.x -> Removed
== R5: a failed download is never resumed
   attempt 2 started with 0 bytes of attempt 1 on disk (attempt 1 had written 1000)
   scratch folders left after the two failures: none
```

Output of `repro_gate` (R):

```
1. project stamped 2.850.0, editor 2.860.0, no flag:          may open silently = no  (This project was made with v2.850.0 and you haven't chosen w...)
2. same, with a BARE --keep-version (what Open with passes):    may open silently = YES   <- no question, no backup
3. same, with --keep-version=2.850.1 (names the old version):  may open silently = no
```

<details>
<summary>repro_versions.cpp (the core)</summary>

```cpp
// Builds a launcher folder in a temp dir and calls the real okayversions:: functions.
#include "VersionsManager.hpp"
using namespace okayversions; namespace fs = std::filesystem;
// MakeVersion(L, v, source): Versions/<v>/Tools/{editor, OkaySpacePlayer.exe, SDL2.dll} + install.txt, like Install() leaves it.

// R1: put Versions/2.850.1/Tools/Projects/MyGame/{project.okayproj, Assets/level1.okayscene}; Remove(L, "2.850.1", guards)
//     -> Outcome::Removed, and the project folder no longer exists.
// R2: MakeVersion 2.850.0, delete its player and SDL2.dll; ListInstalled lists it; Install(L, plan for 2.850.0, ...) -> AlreadyInstalled.
//     A zero-byte editor file also satisfies okayver::IsInstalled.
// R3: create Versions/.partial-2.860.0-1/Tools/OkayEngine.exe.part just now; CleanStale(L) removes it.
// R4: PlanPrune({2.855.0 kept, 2.850.1 kept}, keepPrevious = 1, "", {}) -> {"2.850.1"}; Remove of an unused-looking version succeeds.
// R5: a downloader that writes half a file then returns DownloadStatus::Failed; call Install twice;
//     the second call sees no bytes of the first in `dest`.
```

</details>

<details>
<summary>repro_gate.cpp (complete)</summary>

```cpp
#include "ProjectUpgrade.hpp"
using namespace okayup;
// root/OldProject/project.okayproj = "engine_version=2.850.0\n"; root/OldProject/Assets/level.okayscene = "okayscene 3\n"
GateEnv env; env.running = "2.860.0";               // a newer editor
{ ProjectGate g(env); g.MayLoad(scene, &why); }     // 1. no flag: false (asks first)
{ ProjectGate g(env); g.NoteKeepRequest(scene, ""); g.MayLoad(scene, &why); }        // 2. bare flag: TRUE (silent)
{ ProjectGate g(env); g.NoteKeepRequest(scene, "2.850.1"); g.MayLoad(scene, &why); } // 3. names another version: false
```

</details>

## Install, update and uninstall: how OkaySpace compares with Steam, the Unity Hub and the Epic Games Launcher

*Was `docs/install_comparison.md` until 2026-11-20.*

Status: research note, 2026-11-15. It goes with [install_audit.md](install.md#install-uninstall-and-editor-versions-audit-and-plan-phase-1) (what OkaySpace does today and the plan).

The owner asked for this on purpose (2026-11-15: "compare it to steam, unity and unrel"). **This is the only page in the repo that
may name these products.** They never appear in code, UI strings or other docs. In the audit they are only the short tags
**Steam**, **Hub** and **Epic**:

* **Steam** = the Steam client and its game installs.
* **Hub** = the Unity Hub, installing Unity Editor versions.
* **Epic** = the Epic Games Launcher with Unreal Engine (Library > Engine Versions).

How it was written (CLAUDE.md rules 16 and 19a): from the vendors' public documentation and general knowledge only, in my own words.
Nothing of theirs (text, icons, strings, code, screenshots) is copied. I describe documented behaviour and I say nothing about
their internals. Searches named only the public products, never our project.

### The headline, in eight plain lines

1. **Behind:** all three let the person choose where programs and versions go (Steam library folders on any drive, the Hub's install
   location, Epic's install-location dialog). We cannot choose where editor versions live, and our default puts projects inside the
   program folder.
2. **Behind:** Steam and Epic can check an install and repair it (Epic re-downloads only damaged or missing files). We have no check
   for an installed version.
3. **Behind:** uninstall. Epic's launcher uninstall asks whether to keep installed games; the Hub keeps its settings and project list
   and never deletes a project when it removes one from the list. Ours asks nothing and leaves most files, a sign-in token included.
4. **Behind:** downloads. Steam and Epic have speed limits and a queue, and Epic pauses its queue when a drive is full. Ours can only
   cancel and starts again from the first byte.
5. **Behind:** the Hub and Epic document one log folder and give a "show in Explorer" or "show logs" action. Ours are scattered and
   Setup writes no log.
6. **Behind:** optional components. Epic and the Hub add or remove components later (symbols, templates, platforms). Ours is three
   fixed files.
7. **Equal:** side-by-side versions with the "this project's version is not installed: pick one or install it" flow match the Hub;
   a per-user install with no administrator matches the Hub's per-account install and is easier than Steam's (its install page expects
   an administrator).
8. **At least not worse:** every version we download is checked against a published SHA-256 list and appears whole or not at all,
   and installed versions open offline. The pages I read do not say how the others check downloads, so I claim no more than that.

### How each fact was read (and how much to trust it)

Every fact has its page address next to it. The tag after a link says how I got it:

| Tag | Meaning |
|---|---|
| **read** | I fetched the page and read it. |
| **extract** | The page could not be fetched (Steam help pages load their text with scripts; Epic support pages answered "403 forbidden"), so I used the search tool's extract of that official page. Treat it as a good lead, not a quote. |
| **forum** | An official community forum thread. It is a person's answer, not documentation. |
| *from memory, not checked* | General knowledge. Not looked up. |

"Not described on the page I read" means exactly that: the fact may exist elsewhere. The full list of addresses, and how each was
read, is at the end.

---

### 1. Where things install, and can the person choose?

| Aspect | Ours today (and with the plan) | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| The launcher or client | `%LOCALAPPDATA%\OkaySpace`, per user. A fresh install shows a folder page; an update reuses the old folder and ignores `/D=` (audit S2, S12). | The install page expects an administrator account ([Installing Steam](https://support.steampowered.com/kb_article.php?p_faqid=144), extract). Moving the client is a documented procedure that keeps the `SteamApps` and `Userdata` folders ([Moving a Steam installation](https://support.steampowered.com/kb_article.php?p_faqid=231), extract). | On Windows (Hub 4.0 and later) the Hub is an MSIX package in a folder Windows manages; older Hubs used `C:\Program Files\Unity Hub` or a chosen folder ([Install the Hub](https://docs.unity.com/en-us/hub/install-hub-win-mac), read). | Not described on the pages I read. | Nothing: keep the per-user folder. |
| Where games or editor versions go | Always `<install>\Versions\<version>`; no setting. A junction might work but I did not run it (audit V17). Plan item 12 adds a setting. | **Library folders on any drive**: add one under Settings > Downloads > Steam Library Folders, install new games there, or move installed games into it ([Moving a Steam installation](https://support.steampowered.com/kb_article.php?p_faqid=231), extract). A per-game "Move Install Folder" button is described in a community guide, not official ([guide](https://steamcommunity.com/sharedfiles/filedetails/?id=1912418281), forum-style). | An **Installs location** setting. Default on Windows `C:\Program Files\Unity\Hub\Editor`. Changing it does **not** move existing installs ([Hub settings](https://docs.unity.com/en-us/hub/settings), read). | A "Choose install location" dialog with **Browse** when an engine version is added; the default is the system's default ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). For games there is a default path setting and a per-game choice ([Epic: change the default path](https://www.epicgames.com/help/c-32735058/c-36403860/how-do-i-change-the-default-installation-path-for-my-games-on-the-epic-games-launcher-a23672320), extract). Moving an installed game is a copy-and-reinstall workaround, not a button ([Epic: move an installed game](https://www.epicgames.com/help/c-202300000001639/c-202300000001735/how-to-move-an-installed-game-from-the-epic-games-launcher-to-another-directory-on-your-computer-a202300000014082), extract). | A "Keep other versions in" setting (all three). Copy the Hub's rule: changing it does not move old installs silently. A **Move** button later. |

### 2. Several versions side by side

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Side by side | Yes: the running version in `Tools\`, others in `Versions\<v>\`. One editor at a time (they share a lock). "Open with" picks one ([launcher_versions.md](launcher_versions.md)). | One installed copy of a game at a time; beta branches are chosen per game (*from memory, not checked*). | Many Editor versions installed side by side (the Installs tab lists them; [Install the Editor](https://docs.unity.com/en-us/hub/add-editor), read). | Many engine versions. **Only one of each version** can be installed ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | Nothing missing. |
| Which version opens a project | The project remembers `engine_version`. "Open with" lists installed editors; the editor asks before it upgrades a project ([project_upgrade.md](../editor/project_upgrade.md)). | Not applicable. | A project opens with the Editor version it was saved with. If that version is not installed, the Hub opens a window to pick an installed version or "Install other Editor version" ([Manage projects](https://docs.unity.com/en-us/hub/project-manage), read). | The quick-launch dropdown picks which installed version to start ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). The multiple-installs page does not say which project uses which version ([Multiple installs](https://dev.epicgames.com/documentation/en-us/unreal-engine/multiple-launcher-unreal-engine-installs), read). | We match the Hub (our "Open launcher" window). Maybe copy Epic's quick-launch picker on the editor button. |

### 3. Optional components (platform support, templates, symbols) added or removed later

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| What can be chosen | Nothing. Each version is three fixed files (editor, player, `SDL2.dll`). Setup always installs examples, textures, fonts, the relay and a demo. Debug symbols are a release file that nobody can pick in the app. | Add-on content per game (*from memory, not checked*). | **Modules** (platform support and more): chosen when installing, listed on the install page ([Install the Editor](https://docs.unity.com/en-us/hub/add-editor), read). | An **Options** dialog: required core, starter content, templates and feature packs, engine source, editor symbols for debugging, target platforms ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | A per-version component list built from `versions.json` (it already has sizes and SHA-256 per file): player, relay, examples, symbols. |
| Added or removed later | No. | (*from memory, not checked*) | Yes: Installs > Manage > Manage modules. It works only for Editors the Hub installed, or located ones where the Hub finds the module list online; otherwise reinstall or use the CLI ([Add modules](https://docs.unity.com/en-us/hub/add-modules), read). | Yes, with no full re-download: engine tile menu > Options > Apply ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | Add and remove later (owner decision D3: not yet). |
| Size shown | Versions page shows each version's size. | (*from memory, not checked*) | Not described on the page I read. | The Options dialog shows the storage the selection needs ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | Show the size per component. |

### 4. Disk-space check before downloading

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Before the download | A version install refuses if free space is under what it needs plus 16 MB (`VersionsManager.cpp:495`). Keeping the previous editor leaves 256 MB free. **Setup has no check on an update**, and a full disk leaves a half-written folder (audit S8, finding 5). | The install dialog shows space needed and free (*from memory, not checked*). | **Not described** on the install page ([Install the Editor](https://docs.unity.com/en-us/hub/add-editor), read). | The Options dialog shows the space the choice needs ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | Show need and free space on the drive in our UI (we check but only say it when it fails). |
| When the drive fills mid-way | The scratch folder is removed and nothing is installed. | (*from memory, not checked*) | Not described. | **Since launcher 0.113.0 the download queue pauses and its items wait** when a drive is full; the same release fixed a required update that failed on a full disk ([release notes](https://launcher.epicgames.com/release-notes), read). 0.112.0 warns when the drive a game is on has errors (same page). | Pause and continue instead of fail (plan item 6). |

### 5. Pause, resume, queue, bandwidth limit

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Pause and resume | **Cancel only.** A cancelled or failed download starts again from byte 0 (audit finding 17). | Not confirmed from the pages I could read (*from memory, not checked*: the Download Manager pauses and resumes). | The Downloads panel shows progress and lets you cancel items; the page does not mention pause, resume, queue or retry ([Install the Editor](https://docs.unity.com/en-us/hub/add-editor), read). | Paused downloads exist (a fix says paused downloads used to keep retrying: 0.94.0); the queue pauses by itself on a full drive ([release notes](https://launcher.epicgames.com/release-notes), read). | Pause and Resume with the partial file kept. |
| Queue and priority | One job at a time; no queue. | A Download Manager opened from the Downloads link ([Steam: download manager](https://help.steampowered.com/faqs/view/71AB-698D-57EB-178C), extract). Reordering: *from memory, not checked*. | A setting limits **concurrent downloads** to 2 to 20 and another sets the downloads location ([Hub settings](https://docs.unity.com/en-us/hub/settings), read). | The Preview launcher has **priority downloads** and **scheduled downloads** ([Epic: Preview download settings](https://www.epicgames.com/help/c-32735058/c-31292734/epic-games-launcher-preview-download-settings-a22848313), extract). | A two-item queue (editor first, extras after) is enough. |
| Speed limit | None. curl gives up below 1 KB/s for 60 s. | Settings > Downloads has a limit ([Steam: limit download speed](https://help.steampowered.com/faqs/view/163C-7C89-406E-2F63), page title read, text extract). | Not described. | "Download speed limit" and "Active download speed limit" ([Epic: Preview download settings](https://www.epicgames.com/help/c-32735058/c-31292734/epic-games-launcher-preview-download-settings-a22848313), extract; and [turn off the limit](https://www.epicgames.com/help/c-32735058/c-36403860/a14585910), extract). | An optional speed limit in Settings. |

### 6. Verify and repair

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Check an install | **None for installed versions.** A version missing its player or `SDL2.dll`, or with an empty editor, still shows as installed and Install says "already installed" (audit finding 9). "Get the editor" re-downloads only missing exes of the running install. | Game Properties > Local files > **Verify integrity of game files**. It can take minutes; a few "failed" local config files are normal ([Steam: verify](https://support.steampowered.com/kb/2037-QEUH-3335/verify-integrity-of-game-cache), extract). | No verify is described. If an Editor is missing or damaged, "Manage modules" is not shown; the page says to reinstall through the Hub or use the command line ([Add modules](https://docs.unity.com/en-us/hub/add-modules), read). | Engine tile menu > **Verify**: checks engine files and plugins; **only damaged or missing files are re-downloaded**; unavailable while a hotfix is waiting ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). For games, Library > Manage > Verify ([Epic: verify](https://www.epicgames.com/help/en-US/c-Category_EpicGamesStore/c-EpicGamesStore_LauncherSupport/how-do-i-verify-game-files-in-the-epic-games-launcher-a000084923), extract). | Verify and Repair per version, using our SHA-256 list; re-download only what is wrong (plan item 5). |
| A whole folder | Setup run again is a full reinstall (`NoRepair` is set). | A library folder can be repaired when permissions break: Settings > Downloads > Steam Library Folders, right-click, **Repair Folder** ([Steam knowledge base](https://support.steampowered.com/kb_cat.php?id=59&t=qanda), extract). | Not described. | Not described. | A "repair the install folder" action in Settings (can reuse Verify). |

### 7. Update flow of the launcher and of the versions

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| The launcher or client | Updates itself in place: download, check SHA-256, swap, self-check, roll back ([updater.md](updater.md)). A start-up check is on for new installs. Setup is a second, separate path. | Updates itself (*from memory, not checked*). A client beta can be chosen in Settings (an older page says Account > Beta participation; a 2024 post says Interface > Client Beta Participation: [Steam news](https://store.steampowered.com/news/posts/?enddate=1710892745&feed=steam_clientAll), extract). | Windows manages the updates of the MSIX package; on Windows 11 the Hub can update in the background while it is closed ([Install the Hub](https://docs.unity.com/en-us/hub/install-hub-win-mac), read). | The launcher has "required updates" (a fix for one that failed on a full disk: [release notes](https://launcher.epicgames.com/release-notes), read). | We are fine; keep the careful swap. |
| Engine or editor versions | New versions are installed by hand on the Versions page; an update keeps the replaced editor (Keep previous versions); Latest or LTS channel. **A kept editor can currently update itself in place** (audit finding 1). | Not applicable. | Editors are installed as separate versions. Opening a project with another version is "Change version", and Unity may need to update scripts ([Manage projects](https://docs.unity.com/en-us/hub/project-manage), extract). | New engine versions are added as new tiles. **Hotfixes** apply to an installed version, and Verify is unavailable while one is pending ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | A version stays the version it says it is; only a hotfix changes it in place (plan item 1). |

### 8. Uninstall: what is removed, asked, kept

| Aspect | Ours today (and with the plan) | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| What the uninstall removes | Program files, five settings files, the official game, and the **whole** `examples\`, `Assets\Textures` and `Assets\Fonts` folders (so also the person's own files in them), the shortcuts and registry keys. It **leaves** `Versions\`, logs, state, editor settings and the sign-in token (audit S9, finding 11; ran under wine). | On Windows it deletes the folder Steam was installed in, so games inside it go too unless `steamapps` is moved out first ([Moving a Steam installation](https://support.steampowered.com/kb_article.php?p_faqid=231), extract). | Windows Settings > Apps > Installed apps removes the Hub for your account. Editors: **Uninstall** for ones the Hub installed ([Install the Editor](https://docs.unity.com/en-us/hub/add-editor), read). | Engine version: Remove > Uninstall on the tile ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | Remove every program file, including old versions (plan item 4). |
| What it asks | Nothing. The pages are the default ones. | The page says to move `steamapps` first and offers a Backup and Restore tool ([Moving a Steam installation](https://support.steampowered.com/kb_article.php?p_faqid=231), extract). | Nothing is described. | **Since launcher 0.100.0, uninstalling the launcher asks whether to keep installed games** ([release notes](https://launcher.epicgames.com/release-notes), read). | Ask: "Also remove my projects, settings and sign-in?" Default no (D2). |
| What stays | Projects, `community\`, `downloads\` on purpose; the rest by accident. | Games you moved out; Cloud saves live on Valve's servers (*from memory, not checked*: the [Steam Cloud page](https://partner.steamgames.com/doc/features/cloud) does not say what happens on uninstall). Valve's own backups do not include saved games or custom content for games that do not use Steam Cloud ([Moving a Steam installation](https://support.steampowered.com/kb_article.php?p_faqid=231), extract). | **Hub settings and the projects list stay** in `%APPDATA%\UnityHub` ([Install the Hub](https://docs.unity.com/en-us/hub/install-hub-win-mac), read). | Whether engine installs or projects stay is not stated on the pages I read. | Keep projects and settings by default, and say so. |
| Removing one item | Remove version: whole folder deleted, projects inside included (finding 2). | Not covered. | "**Remove from Hub**" for an added Editor leaves the files on disk ([Install the Editor](https://docs.unity.com/en-us/hub/add-editor), read). Removing a project from the list **never deletes its files** ([Manage projects](https://docs.unity.com/en-us/hub/project-manage), read). | Not described. | The Hub's rule: removing something we did not install never deletes the person's files; refuse when a folder holds a project (plan item 2). |
| A list of leftovers | None. | None described. | None described. | None described. | Show "Kept: ..." on the last page (plan item 4). |

### 9. Keeping user data apart from program files

| Aspect | Ours today (and with the plan) | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Projects or saves | Projects default to `<install>\Projects` (launcher) or `<editor folder>\Projects` (editor), **inside the program folder**. Plan 8 moves new projects to `Documents\OkaySpace Projects`. | Not applicable (games). Cloud syncs a game's save files from places such as `AppData\Local`, `My Documents` and `Saved Games` ([Steam Cloud](https://partner.steamgames.com/doc/features/cloud), read). | Projects can be anywhere. The default is the user's home directory and a **Location** field changes it ([Create a project](https://docs.unity.com/en-us/hub/project-create), read). | New projects default to `Documents\Unreal Projects` (forum answers, not documentation: [forum](https://forums.unrealengine.com/t/how-can-i-change-default-location-of-unreal-projects/277401)); the Project Browser can set a location per project ([Installing and setting up a project](https://dev.epicgames.com/documentation/unreal-engine/installing-and-setting-up-a-project), read). | Projects in a user folder, a per-project location, never in the program folder. |
| Settings, lists, logs | Beside the exe or in the current folder, plus `%APPDATA%\OkaySpace` and `%LOCALAPPDATA%\OkaySpace` (three base folders; audit section 1). | Games in `steamapps` and per-account data in `userdata`, kept apart ([Moving a Steam installation](https://support.steampowered.com/kb_article.php?p_faqid=231), extract). | Hub settings and projects list in `%APPDATA%\UnityHub`; logs in its `logs` folder ([Install the Hub](https://docs.unity.com/en-us/hub/install-hub-win-mac), read; [log files reference](https://docs.unity.com/en-us/engine/6000.6/manual/get-started/project-configuration/log-files), extract). | Launcher logs under `%LOCALAPPDATA%\EpicGamesLauncher\Saved\Logs` ([Epic: gather logs](https://www.epicgames.com/help/en-US/c-Category_EpicGamesStore/c-EpicGamesStore_LauncherSupport/how-do-i-gather-logs-for-epic-games-launcher-a000084789), extract). | Settings, state, logs and caches under `%LOCALAPPDATA%\OkaySpace`; programs replaceable at any time. |

### 10. Per-user or per-machine, and administrator rights

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Scope | **Per user, no administrator** (`RequestExecutionLevel user`; the program manifest says `asInvoker`). `HKCU` only. | The install page says to be logged in as an administrator ([Installing Steam](https://support.steampowered.com/kb_article.php?p_faqid=144), extract). Library folders "need to be writable by all users" ([Steam knowledge base](https://support.steampowered.com/kb_cat.php?id=59&t=qanda), extract). | Hub 4.0 and later on Windows, installed directly, is available only to your account (if you updated from an earlier Hub, other accounts get it the next time they sign in) ([Install the Hub](https://docs.unity.com/en-us/hub/install-hub-win-mac), read). The Editor default is under Program Files ([Hub settings](https://docs.unity.com/en-us/hub/settings), read). | A search extract of Epic's launcher support pages says running the launcher as an administrator is a fix for download problems, and names `C:\Program Files\Epic Games` as the default game folder. I could not read those pages, so the exact source is not pinned (the nearest: [Epic: troubleshooting the launcher](https://www.epicgames.com/help/en-US/c-Category_EpicGamesStore/c-EpicGamesStore_LauncherSupport/troubleshooting-the-epic-games-launcher-a000084770), extract only). | Keep per user (owner decision D8). Warn when a folder the person picked is not writable (plan item 3). |

### 11. Long paths and non-ASCII paths

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Behaviour | Setup and uninstall handle them. **The programs do not**: they start programs with the ANSI code page, and have no long-path setting (audit findings 6 and 22). | Not described on the pages I read. | No official page found. Community reports say the Editor still hits the 260-character limit with Windows long paths enabled ([forum](https://discussions.unity.com/t/unity-editor-still-hits-windows-max-path-260-char-limit-even-with-longpathsenabled-any-real-fix/1732098)). | Epic's release notes say paths in Windows are limited to 260 characters, long paths are opt-in, and support is "not universal" ([UE 4.22 notes](https://docs.unrealengine.com/en-US/Support/Builds/ReleaseNotes/4_22/index.html), extract). Unreal Game Sync blocks paths over 260 and suggests short paths near the drive root ([UGS troubleshooting](https://dev.epicgames.com/documentation/unreal-engine/unreal-game-sync-troubleshooting), extract). | Be honest early: keep default paths short, give a plain message, and fix our own ANSI calls (plan item 7). |

### 12. Offline use

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Behaviour | The launcher runs offline. The Versions page says "Couldn't reach the version list". Installed versions open. Local accounts work. | An Offline Mode exists (*from memory, not checked*). | Licensing has offline paths: a floating licence can be checked out; some licence types can be activated with a licence request ([work offline](https://docs.unity.com/en-us/hub/work-offline), extract). | "Enable Offline Mode Browsing" in Settings; it turns on by itself when the launcher starts offline; some games and features need the internet ([Epic: offline mode](https://www.epicgames.com/help/en-US/c-Category_EpicGamesStore/c-EpicGamesStore_LauncherSupport/how-do-i-run-games-in-offline-mode-using-the-epic-games-launcher-a000084805), extract). | Nothing needed. A small "Offline" label would help. |

### 13. When a version is running

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Behaviour | Setup checks three exes and asks to close them (not the relay or demo). **Uninstall checks nothing.** Remove refuses only when the launcher knows which editor version is open. The self-update refuses while the editor or a game runs. | A game that is already running gives "Failed to start game (app already running)" ([Steam knowledge base](https://support.steampowered.com/kb_cat.php?id=59&t=qanda), extract). Blocking updates of a running game: *from memory, not checked*. | Not described on the pages I read. | A support article exists for the launcher refusing to uninstall because it says it is running (title only: [article](https://www.epicgames.com/help/en-US/c-202300000001639/c-Trending_0/i-can-t-uninstall-the-epic-games-launcher-because-it-says-it-is-currently-running-a202300000014220)). Verify is unavailable while a hotfix waits ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | A plain "Close X first" with a button, for install, remove and uninstall alike (plan items 3 and 4). |

### 14. Logs, diagnostics and "show in Explorer"

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Where the logs are | Three places: `okay_update.log` in the install folder and in `Tools\`, crash logs in `Tools\`; **Setup writes none** (audit finding 24). | A `logs\` folder inside the Steam folder (named on a Steam Link help page: [Steam help](https://help.steampowered.com/en/wizard/HelpWithGameIssue/?appid=353380&issueid=351&nodeid=2&return_nodeid=1), extract). | `%APPDATA%\UnityHub\logs\info-log.json`, opened from Account > Help and Support > Logs ([log files reference](https://docs.unity.com/en-us/engine/6000.6/manual/get-started/project-configuration/log-files), extract). | `%LOCALAPPDATA%\EpicGamesLauncher\Saved\Logs`; turn on debug logging in Settings, then Show Logs; the installer's log is made with `msiexec /L*V` into the root of C: ([Epic: gather logs](https://www.epicgames.com/help/en-US/c-Category_EpicGamesStore/c-EpicGamesStore_LauncherSupport/how-do-i-gather-logs-for-epic-games-launcher-a000084789), extract). | One documented log folder, "Open logs folder", "Copy diagnostics", a Setup log (plan item 9). |
| Show in Explorer | Settings > Maintenance > "Open folder" opens the install folder; the Versions page's "Open install folder" opens `Versions\`. No per-version link. | "Browse local files" per game (*from memory, not checked*). | **Manage > Show in Explorer** for an Editor ([Install the Editor](https://docs.unity.com/en-us/hub/add-editor), read). | Not described on the pages I read. | Show in Explorer per version (plan items 5 and 9). |

---

### What to copy, in order

1. **Hub and Epic: a version never changes into another version by itself** (plan item 1). Cheapest, and it protects projects.
2. **Hub: removing something we did not install never deletes the person's files** (plan item 2).
3. **Epic: the uninstall asks whether to keep what the person made** (plan item 4); the Hub's "settings and project list stay".
4. **Steam and Epic: verify, and re-download only what is wrong** (plan item 5).
5. **All three: choose where things live; keep projects out of the program folder** (plan items 8 and 12).
6. **Epic: pause instead of fail on a full drive; Steam and Epic: pause, resume and a limit** (plan item 6).
7. **Hub and Epic: one documented log folder and a Show in Explorer action** (plan item 9).

### Sources and how each was read (all addresses opened on 2026-11-15)

**Steam.** [Moving a Steam installation and games](https://support.steampowered.com/kb_article.php?p_faqid=231) (extract) ·
[Verify integrity of game files](https://support.steampowered.com/kb/2037-QEUH-3335/verify-integrity-of-game-cache) (extract) ·
[Installing Steam](https://support.steampowered.com/kb_article.php?p_faqid=144) (extract) ·
[Setting client rates (limit download speed)](https://help.steampowered.com/faqs/view/163C-7C89-406E-2F63) (title read, text extract) ·
[Download manager page](https://help.steampowered.com/faqs/view/71AB-698D-57EB-178C) (extract) ·
[Steam knowledge base: library folder repair, "app already running"](https://support.steampowered.com/kb_cat.php?id=59&t=qanda) (extract) ·
[Steam Cloud documentation](https://partner.steamgames.com/doc/features/cloud) (read) ·
[Steam news with the client beta setting](https://store.steampowered.com/news/posts/?enddate=1710892745&feed=steam_clientAll) (extract) ·
[Steam help page naming `logs\`](https://help.steampowered.com/en/wizard/HelpWithGameIssue/?appid=353380&issueid=351&nodeid=2&return_nodeid=1) (extract) ·
[community guide on moving games](https://steamcommunity.com/sharedfiles/filedetails/?id=1912418281) (community, not official).
The Steam help pages returned only their navigation to the fetch tool, which is why most Steam facts are extracts.

**Hub.** [Hub settings](https://docs.unity.com/en-us/hub/settings) (read) ·
[Download and install the Unity Editor](https://docs.unity.com/en-us/hub/add-editor) (read) ·
[Add modules](https://docs.unity.com/en-us/hub/add-modules) (read) ·
[Install the Hub on Windows or macOS](https://docs.unity.com/en-us/hub/install-hub-win-mac) (read) ·
[Create a new project](https://docs.unity.com/en-us/hub/project-create) (read) ·
[Manage projects](https://docs.unity.com/en-us/hub/project-manage) (read; the "Change version" sentence is a search extract) ·
[Check out a license to work offline](https://docs.unity.com/en-us/hub/work-offline) (extract) ·
[Log files reference](https://docs.unity.com/en-us/engine/6000.6/manual/get-started/project-configuration/log-files) (extract) ·
[a forum thread on the 260-character limit](https://discussions.unity.com/t/unity-editor-still-hits-windows-max-path-260-char-limit-even-with-longpathsenabled-any-real-fix/1732098) (forum).

**Epic.** [Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine) (read) ·
[Installing and setting up a project](https://dev.epicgames.com/documentation/unreal-engine/installing-and-setting-up-a-project) (read) ·
[Multiple launcher installs](https://dev.epicgames.com/documentation/en-us/unreal-engine/multiple-launcher-unreal-engine-installs) (read) ·
[Launcher release notes](https://launcher.epicgames.com/release-notes) (read; newest entry seen: 0.115.0, 2026-11-11) ·
[Epic: verify game files](https://www.epicgames.com/help/en-US/c-Category_EpicGamesStore/c-EpicGamesStore_LauncherSupport/how-do-i-verify-game-files-in-the-epic-games-launcher-a000084923) (extract) ·
[Epic: move an installed game](https://www.epicgames.com/help/c-202300000001639/c-202300000001735/how-to-move-an-installed-game-from-the-epic-games-launcher-to-another-directory-on-your-computer-a202300000014082) (extract) ·
[Epic: change the default install path](https://www.epicgames.com/help/c-32735058/c-36403860/how-do-i-change-the-default-installation-path-for-my-games-on-the-epic-games-launcher-a23672320) (extract; the page answered 403) ·
[Epic: Preview download settings](https://www.epicgames.com/help/c-32735058/c-31292734/epic-games-launcher-preview-download-settings-a22848313) (extract; 403) ·
[Epic: turn off the download speed limit](https://www.epicgames.com/help/c-32735058/c-36403860/a14585910) (extract) ·
[Epic: gather logs](https://www.epicgames.com/help/en-US/c-Category_EpicGamesStore/c-EpicGamesStore_LauncherSupport/how-do-i-gather-logs-for-epic-games-launcher-a000084789) (extract) ·
[Epic: offline mode](https://www.epicgames.com/help/en-US/c-Category_EpicGamesStore/c-EpicGamesStore_LauncherSupport/how-do-i-run-games-in-offline-mode-using-the-epic-games-launcher-a000084805) (extract) ·
[Epic: troubleshooting the launcher](https://www.epicgames.com/help/en-US/c-Category_EpicGamesStore/c-EpicGamesStore_LauncherSupport/troubleshooting-the-epic-games-launcher-a000084770) (extract only) ·
[Epic: launcher says it is running when uninstalling](https://www.epicgames.com/help/en-US/c-202300000001639/c-Trending_0/i-can-t-uninstall-the-epic-games-launcher-because-it-says-it-is-currently-running-a202300000014220) (title only) ·
[Unreal Engine 4.22 release notes](https://docs.unrealengine.com/en-US/Support/Builds/ReleaseNotes/4_22/index.html) (extract) ·
[Unreal Game Sync troubleshooting](https://dev.epicgames.com/documentation/unreal-engine/unreal-game-sync-troubleshooting) (extract) ·
[Epic forum: default project location](https://forums.unrealengine.com/t/how-can-i-change-default-location-of-unreal-projects/277401) (forum).

**What I did not look at:** prices, accounts and sign-in, stores and marketplaces, cloud features other than Steam Cloud's file
sync, and anything about how the vendors build their programs.
