# Install, update and uninstall: how OkaySpace compares with Steam, the Unity Hub and the Epic Games Launcher

Status: research note, 2026-11-15. It goes with [install_audit.md](install_audit.md) (what OkaySpace does today and the plan).

The owner asked for this on purpose (2026-11-15: "compare it to steam, unity and unrel"). **This is the only page in the repo that
may name these products.** They never appear in code, UI strings or other docs. In the audit they are only the short tags
**Steam**, **Hub** and **Epic**:

* **Steam** = the Steam client and its game installs.
* **Hub** = the Unity Hub, installing Unity Editor versions.
* **Epic** = the Epic Games Launcher with Unreal Engine (Library > Engine Versions).

How it was written (CLAUDE.md rules 16 and 19a): from the vendors' public documentation and general knowledge only, in my own words.
Nothing of theirs (text, icons, strings, code, screenshots) is copied. I describe documented behaviour and I say nothing about
their internals. Searches named only the public products, never our project.

## The headline, in eight plain lines

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

## How each fact was read (and how much to trust it)

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

## 1. Where things install, and can the person choose?

| Aspect | Ours today (and with the plan) | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| The launcher or client | `%LOCALAPPDATA%\OkaySpace`, per user. A fresh install shows a folder page; an update reuses the old folder and ignores `/D=` (audit S2, S12). | The install page expects an administrator account ([Installing Steam](https://support.steampowered.com/kb_article.php?p_faqid=144), extract). Moving the client is a documented procedure that keeps the `SteamApps` and `Userdata` folders ([Moving a Steam installation](https://support.steampowered.com/kb_article.php?p_faqid=231), extract). | On Windows (Hub 4.0 and later) the Hub is an MSIX package in a folder Windows manages; older Hubs used `C:\Program Files\Unity Hub` or a chosen folder ([Install the Hub](https://docs.unity.com/en-us/hub/install-hub-win-mac), read). | Not described on the pages I read. | Nothing: keep the per-user folder. |
| Where games or editor versions go | Always `<install>\Versions\<version>`; no setting. A junction might work but I did not run it (audit V17). Plan item 12 adds a setting. | **Library folders on any drive**: add one under Settings > Downloads > Steam Library Folders, install new games there, or move installed games into it ([Moving a Steam installation](https://support.steampowered.com/kb_article.php?p_faqid=231), extract). A per-game "Move Install Folder" button is described in a community guide, not official ([guide](https://steamcommunity.com/sharedfiles/filedetails/?id=1912418281), forum-style). | An **Installs location** setting. Default on Windows `C:\Program Files\Unity\Hub\Editor`. Changing it does **not** move existing installs ([Hub settings](https://docs.unity.com/en-us/hub/settings), read). | A "Choose install location" dialog with **Browse** when an engine version is added; the default is the system's default ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). For games there is a default path setting and a per-game choice ([Epic: change the default path](https://www.epicgames.com/help/c-32735058/c-36403860/how-do-i-change-the-default-installation-path-for-my-games-on-the-epic-games-launcher-a23672320), extract). Moving an installed game is a copy-and-reinstall workaround, not a button ([Epic: move an installed game](https://www.epicgames.com/help/c-202300000001639/c-202300000001735/how-to-move-an-installed-game-from-the-epic-games-launcher-to-another-directory-on-your-computer-a202300000014082), extract). | A "Keep other versions in" setting (all three). Copy the Hub's rule: changing it does not move old installs silently. A **Move** button later. |

## 2. Several versions side by side

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Side by side | Yes: the running version in `Tools\`, others in `Versions\<v>\`. One editor at a time (they share a lock). "Open with" picks one ([launcher_versions.md](launcher_versions.md)). | One installed copy of a game at a time; beta branches are chosen per game (*from memory, not checked*). | Many Editor versions installed side by side (the Installs tab lists them; [Install the Editor](https://docs.unity.com/en-us/hub/add-editor), read). | Many engine versions. **Only one of each version** can be installed ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | Nothing missing. |
| Which version opens a project | The project remembers `engine_version`. "Open with" lists installed editors; the editor asks before it upgrades a project ([project_upgrade.md](project_upgrade.md)). | Not applicable. | A project opens with the Editor version it was saved with. If that version is not installed, the Hub opens a window to pick an installed version or "Install other Editor version" ([Manage projects](https://docs.unity.com/en-us/hub/project-manage), read). | The quick-launch dropdown picks which installed version to start ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). The multiple-installs page does not say which project uses which version ([Multiple installs](https://dev.epicgames.com/documentation/en-us/unreal-engine/multiple-launcher-unreal-engine-installs), read). | We match the Hub (our "Open launcher" window). Maybe copy Epic's quick-launch picker on the editor button. |

## 3. Optional components (platform support, templates, symbols) added or removed later

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| What can be chosen | Nothing. Each version is three fixed files (editor, player, `SDL2.dll`). Setup always installs examples, textures, fonts, the relay and a demo. Debug symbols are a release file that nobody can pick in the app. | Add-on content per game (*from memory, not checked*). | **Modules** (platform support and more): chosen when installing, listed on the install page ([Install the Editor](https://docs.unity.com/en-us/hub/add-editor), read). | An **Options** dialog: required core, starter content, templates and feature packs, engine source, editor symbols for debugging, target platforms ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | A per-version component list built from `versions.json` (it already has sizes and SHA-256 per file): player, relay, examples, symbols. |
| Added or removed later | No. | (*from memory, not checked*) | Yes: Installs > Manage > Manage modules. It works only for Editors the Hub installed, or located ones where the Hub finds the module list online; otherwise reinstall or use the CLI ([Add modules](https://docs.unity.com/en-us/hub/add-modules), read). | Yes, with no full re-download: engine tile menu > Options > Apply ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | Add and remove later (owner decision D3: not yet). |
| Size shown | Versions page shows each version's size. | (*from memory, not checked*) | Not described on the page I read. | The Options dialog shows the storage the selection needs ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | Show the size per component. |

## 4. Disk-space check before downloading

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Before the download | A version install refuses if free space is under what it needs plus 16 MB (`VersionsManager.cpp:495`). Keeping the previous editor leaves 256 MB free. **Setup has no check on an update**, and a full disk leaves a half-written folder (audit S8, finding 5). | The install dialog shows space needed and free (*from memory, not checked*). | **Not described** on the install page ([Install the Editor](https://docs.unity.com/en-us/hub/add-editor), read). | The Options dialog shows the space the choice needs ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | Show need and free space on the drive in our UI (we check but only say it when it fails). |
| When the drive fills mid-way | The scratch folder is removed and nothing is installed. | (*from memory, not checked*) | Not described. | **Since launcher 0.113.0 the download queue pauses and its items wait** when a drive is full; the same release fixed a required update that failed on a full disk ([release notes](https://launcher.epicgames.com/release-notes), read). 0.112.0 warns when the drive a game is on has errors (same page). | Pause and continue instead of fail (plan item 6). |

## 5. Pause, resume, queue, bandwidth limit

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Pause and resume | **Cancel only.** A cancelled or failed download starts again from byte 0 (audit finding 17). | Not confirmed from the pages I could read (*from memory, not checked*: the Download Manager pauses and resumes). | The Downloads panel shows progress and lets you cancel items; the page does not mention pause, resume, queue or retry ([Install the Editor](https://docs.unity.com/en-us/hub/add-editor), read). | Paused downloads exist (a fix says paused downloads used to keep retrying: 0.94.0); the queue pauses by itself on a full drive ([release notes](https://launcher.epicgames.com/release-notes), read). | Pause and Resume with the partial file kept. |
| Queue and priority | One job at a time; no queue. | A Download Manager opened from the Downloads link ([Steam: download manager](https://help.steampowered.com/faqs/view/71AB-698D-57EB-178C), extract). Reordering: *from memory, not checked*. | A setting limits **concurrent downloads** to 2 to 20 and another sets the downloads location ([Hub settings](https://docs.unity.com/en-us/hub/settings), read). | The Preview launcher has **priority downloads** and **scheduled downloads** ([Epic: Preview download settings](https://www.epicgames.com/help/c-32735058/c-31292734/epic-games-launcher-preview-download-settings-a22848313), extract). | A two-item queue (editor first, extras after) is enough. |
| Speed limit | None. curl gives up below 1 KB/s for 60 s. | Settings > Downloads has a limit ([Steam: limit download speed](https://help.steampowered.com/faqs/view/163C-7C89-406E-2F63), page title read, text extract). | Not described. | "Download speed limit" and "Active download speed limit" ([Epic: Preview download settings](https://www.epicgames.com/help/c-32735058/c-31292734/epic-games-launcher-preview-download-settings-a22848313), extract; and [turn off the limit](https://www.epicgames.com/help/c-32735058/c-36403860/a14585910), extract). | An optional speed limit in Settings. |

## 6. Verify and repair

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Check an install | **None for installed versions.** A version missing its player or `SDL2.dll`, or with an empty editor, still shows as installed and Install says "already installed" (audit finding 9). "Get the editor" re-downloads only missing exes of the running install. | Game Properties > Local files > **Verify integrity of game files**. It can take minutes; a few "failed" local config files are normal ([Steam: verify](https://support.steampowered.com/kb/2037-QEUH-3335/verify-integrity-of-game-cache), extract). | No verify is described. If an Editor is missing or damaged, "Manage modules" is not shown; the page says to reinstall through the Hub or use the command line ([Add modules](https://docs.unity.com/en-us/hub/add-modules), read). | Engine tile menu > **Verify**: checks engine files and plugins; **only damaged or missing files are re-downloaded**; unavailable while a hotfix is waiting ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). For games, Library > Manage > Verify ([Epic: verify](https://www.epicgames.com/help/en-US/c-Category_EpicGamesStore/c-EpicGamesStore_LauncherSupport/how-do-i-verify-game-files-in-the-epic-games-launcher-a000084923), extract). | Verify and Repair per version, using our SHA-256 list; re-download only what is wrong (plan item 5). |
| A whole folder | Setup run again is a full reinstall (`NoRepair` is set). | A library folder can be repaired when permissions break: Settings > Downloads > Steam Library Folders, right-click, **Repair Folder** ([Steam knowledge base](https://support.steampowered.com/kb_cat.php?id=59&t=qanda), extract). | Not described. | Not described. | A "repair the install folder" action in Settings (can reuse Verify). |

## 7. Update flow of the launcher and of the versions

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| The launcher or client | Updates itself in place: download, check SHA-256, swap, self-check, roll back ([updater.md](updater.md)). A start-up check is on for new installs. Setup is a second, separate path. | Updates itself (*from memory, not checked*). A client beta can be chosen in Settings (an older page says Account > Beta participation; a 2024 post says Interface > Client Beta Participation: [Steam news](https://store.steampowered.com/news/posts/?enddate=1710892745&feed=steam_clientAll), extract). | Windows manages the updates of the MSIX package; on Windows 11 the Hub can update in the background while it is closed ([Install the Hub](https://docs.unity.com/en-us/hub/install-hub-win-mac), read). | The launcher has "required updates" (a fix for one that failed on a full disk: [release notes](https://launcher.epicgames.com/release-notes), read). | We are fine; keep the careful swap. |
| Engine or editor versions | New versions are installed by hand on the Versions page; an update keeps the replaced editor (Keep previous versions); Latest or LTS channel. **A kept editor can currently update itself in place** (audit finding 1). | Not applicable. | Editors are installed as separate versions. Opening a project with another version is "Change version", and Unity may need to update scripts ([Manage projects](https://docs.unity.com/en-us/hub/project-manage), extract). | New engine versions are added as new tiles. **Hotfixes** apply to an installed version, and Verify is unavailable while one is pending ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | A version stays the version it says it is; only a hotfix changes it in place (plan item 1). |

## 8. Uninstall: what is removed, asked, kept

| Aspect | Ours today (and with the plan) | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| What the uninstall removes | Program files, five settings files, the official game, and the **whole** `examples\`, `Assets\Textures` and `Assets\Fonts` folders (so also the person's own files in them), the shortcuts and registry keys. It **leaves** `Versions\`, logs, state, editor settings and the sign-in token (audit S9, finding 11; ran under wine). | On Windows it deletes the folder Steam was installed in, so games inside it go too unless `steamapps` is moved out first ([Moving a Steam installation](https://support.steampowered.com/kb_article.php?p_faqid=231), extract). | Windows Settings > Apps > Installed apps removes the Hub for your account. Editors: **Uninstall** for ones the Hub installed ([Install the Editor](https://docs.unity.com/en-us/hub/add-editor), read). | Engine version: Remove > Uninstall on the tile ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | Remove every program file, including old versions (plan item 4). |
| What it asks | Nothing. The pages are the default ones. | The page says to move `steamapps` first and offers a Backup and Restore tool ([Moving a Steam installation](https://support.steampowered.com/kb_article.php?p_faqid=231), extract). | Nothing is described. | **Since launcher 0.100.0, uninstalling the launcher asks whether to keep installed games** ([release notes](https://launcher.epicgames.com/release-notes), read). | Ask: "Also remove my projects, settings and sign-in?" Default no (D2). |
| What stays | Projects, `community\`, `downloads\` on purpose; the rest by accident. | Games you moved out; Cloud saves live on Valve's servers (*from memory, not checked*: the [Steam Cloud page](https://partner.steamgames.com/doc/features/cloud) does not say what happens on uninstall). Valve's own backups do not include saved games or custom content for games that do not use Steam Cloud ([Moving a Steam installation](https://support.steampowered.com/kb_article.php?p_faqid=231), extract). | **Hub settings and the projects list stay** in `%APPDATA%\UnityHub` ([Install the Hub](https://docs.unity.com/en-us/hub/install-hub-win-mac), read). | Whether engine installs or projects stay is not stated on the pages I read. | Keep projects and settings by default, and say so. |
| Removing one item | Remove version: whole folder deleted, projects inside included (finding 2). | Not covered. | "**Remove from Hub**" for an added Editor leaves the files on disk ([Install the Editor](https://docs.unity.com/en-us/hub/add-editor), read). Removing a project from the list **never deletes its files** ([Manage projects](https://docs.unity.com/en-us/hub/project-manage), read). | Not described. | The Hub's rule: removing something we did not install never deletes the person's files; refuse when a folder holds a project (plan item 2). |
| A list of leftovers | None. | None described. | None described. | None described. | Show "Kept: ..." on the last page (plan item 4). |

## 9. Keeping user data apart from program files

| Aspect | Ours today (and with the plan) | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Projects or saves | Projects default to `<install>\Projects` (launcher) or `<editor folder>\Projects` (editor), **inside the program folder**. Plan 8 moves new projects to `Documents\OkaySpace Projects`. | Not applicable (games). Cloud syncs a game's save files from places such as `AppData\Local`, `My Documents` and `Saved Games` ([Steam Cloud](https://partner.steamgames.com/doc/features/cloud), read). | Projects can be anywhere. The default is the user's home directory and a **Location** field changes it ([Create a project](https://docs.unity.com/en-us/hub/project-create), read). | New projects default to `Documents\Unreal Projects` (forum answers, not documentation: [forum](https://forums.unrealengine.com/t/how-can-i-change-default-location-of-unreal-projects/277401)); the Project Browser can set a location per project ([Installing and setting up a project](https://dev.epicgames.com/documentation/unreal-engine/installing-and-setting-up-a-project), read). | Projects in a user folder, a per-project location, never in the program folder. |
| Settings, lists, logs | Beside the exe or in the current folder, plus `%APPDATA%\OkaySpace` and `%LOCALAPPDATA%\OkaySpace` (three base folders; audit section 1). | Games in `steamapps` and per-account data in `userdata`, kept apart ([Moving a Steam installation](https://support.steampowered.com/kb_article.php?p_faqid=231), extract). | Hub settings and projects list in `%APPDATA%\UnityHub`; logs in its `logs` folder ([Install the Hub](https://docs.unity.com/en-us/hub/install-hub-win-mac), read; [log files reference](https://docs.unity.com/en-us/engine/6000.6/manual/get-started/project-configuration/log-files), extract). | Launcher logs under `%LOCALAPPDATA%\EpicGamesLauncher\Saved\Logs` ([Epic: gather logs](https://www.epicgames.com/help/en-US/c-Category_EpicGamesStore/c-EpicGamesStore_LauncherSupport/how-do-i-gather-logs-for-epic-games-launcher-a000084789), extract). | Settings, state, logs and caches under `%LOCALAPPDATA%\OkaySpace`; programs replaceable at any time. |

## 10. Per-user or per-machine, and administrator rights

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Scope | **Per user, no administrator** (`RequestExecutionLevel user`; the program manifest says `asInvoker`). `HKCU` only. | The install page says to be logged in as an administrator ([Installing Steam](https://support.steampowered.com/kb_article.php?p_faqid=144), extract). Library folders "need to be writable by all users" ([Steam knowledge base](https://support.steampowered.com/kb_cat.php?id=59&t=qanda), extract). | Hub 4.0 and later on Windows, installed directly, is available only to your account (if you updated from an earlier Hub, other accounts get it the next time they sign in) ([Install the Hub](https://docs.unity.com/en-us/hub/install-hub-win-mac), read). The Editor default is under Program Files ([Hub settings](https://docs.unity.com/en-us/hub/settings), read). | A search extract of Epic's launcher support pages says running the launcher as an administrator is a fix for download problems, and names `C:\Program Files\Epic Games` as the default game folder. I could not read those pages, so the exact source is not pinned (the nearest: [Epic: troubleshooting the launcher](https://www.epicgames.com/help/en-US/c-Category_EpicGamesStore/c-EpicGamesStore_LauncherSupport/troubleshooting-the-epic-games-launcher-a000084770), extract only). | Keep per user (owner decision D8). Warn when a folder the person picked is not writable (plan item 3). |

## 11. Long paths and non-ASCII paths

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Behaviour | Setup and uninstall handle them. **The programs do not**: they start programs with the ANSI code page, and have no long-path setting (audit findings 6 and 22). | Not described on the pages I read. | No official page found. Community reports say the Editor still hits the 260-character limit with Windows long paths enabled ([forum](https://discussions.unity.com/t/unity-editor-still-hits-windows-max-path-260-char-limit-even-with-longpathsenabled-any-real-fix/1732098)). | Epic's release notes say paths in Windows are limited to 260 characters, long paths are opt-in, and support is "not universal" ([UE 4.22 notes](https://docs.unrealengine.com/en-US/Support/Builds/ReleaseNotes/4_22/index.html), extract). Unreal Game Sync blocks paths over 260 and suggests short paths near the drive root ([UGS troubleshooting](https://dev.epicgames.com/documentation/unreal-engine/unreal-game-sync-troubleshooting), extract). | Be honest early: keep default paths short, give a plain message, and fix our own ANSI calls (plan item 7). |

## 12. Offline use

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Behaviour | The launcher runs offline. The Versions page says "Couldn't reach the version list". Installed versions open. Local accounts work. | An Offline Mode exists (*from memory, not checked*). | Licensing has offline paths: a floating licence can be checked out; some licence types can be activated with a licence request ([work offline](https://docs.unity.com/en-us/hub/work-offline), extract). | "Enable Offline Mode Browsing" in Settings; it turns on by itself when the launcher starts offline; some games and features need the internet ([Epic: offline mode](https://www.epicgames.com/help/en-US/c-Category_EpicGamesStore/c-EpicGamesStore_LauncherSupport/how-do-i-run-games-in-offline-mode-using-the-epic-games-launcher-a000084805), extract). | Nothing needed. A small "Offline" label would help. |

## 13. When a version is running

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Behaviour | Setup checks three exes and asks to close them (not the relay or demo). **Uninstall checks nothing.** Remove refuses only when the launcher knows which editor version is open. The self-update refuses while the editor or a game runs. | A game that is already running gives "Failed to start game (app already running)" ([Steam knowledge base](https://support.steampowered.com/kb_cat.php?id=59&t=qanda), extract). Blocking updates of a running game: *from memory, not checked*. | Not described on the pages I read. | A support article exists for the launcher refusing to uninstall because it says it is running (title only: [article](https://www.epicgames.com/help/en-US/c-202300000001639/c-Trending_0/i-can-t-uninstall-the-epic-games-launcher-because-it-says-it-is-currently-running-a202300000014220)). Verify is unavailable while a hotfix waits ([Install Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/install-unreal-engine), read). | A plain "Close X first" with a button, for install, remove and uninstall alike (plan items 3 and 4). |

## 14. Logs, diagnostics and "show in Explorer"

| Aspect | Ours today | Steam | Hub | Epic | What we should copy |
|---|---|---|---|---|---|
| Where the logs are | Three places: `okay_update.log` in the install folder and in `Tools\`, crash logs in `Tools\`; **Setup writes none** (audit finding 24). | A `logs\` folder inside the Steam folder (named on a Steam Link help page: [Steam help](https://help.steampowered.com/en/wizard/HelpWithGameIssue/?appid=353380&issueid=351&nodeid=2&return_nodeid=1), extract). | `%APPDATA%\UnityHub\logs\info-log.json`, opened from Account > Help and Support > Logs ([log files reference](https://docs.unity.com/en-us/engine/6000.6/manual/get-started/project-configuration/log-files), extract). | `%LOCALAPPDATA%\EpicGamesLauncher\Saved\Logs`; turn on debug logging in Settings, then Show Logs; the installer's log is made with `msiexec /L*V` into the root of C: ([Epic: gather logs](https://www.epicgames.com/help/en-US/c-Category_EpicGamesStore/c-EpicGamesStore_LauncherSupport/how-do-i-gather-logs-for-epic-games-launcher-a000084789), extract). | One documented log folder, "Open logs folder", "Copy diagnostics", a Setup log (plan item 9). |
| Show in Explorer | Settings > Maintenance > "Open folder" opens the install folder; the Versions page's "Open install folder" opens `Versions\`. No per-version link. | "Browse local files" per game (*from memory, not checked*). | **Manage > Show in Explorer** for an Editor ([Install the Editor](https://docs.unity.com/en-us/hub/add-editor), read). | Not described on the pages I read. | Show in Explorer per version (plan items 5 and 9). |

---

## What to copy, in order

1. **Hub and Epic: a version never changes into another version by itself** (plan item 1). Cheapest, and it protects projects.
2. **Hub: removing something we did not install never deletes the person's files** (plan item 2).
3. **Epic: the uninstall asks whether to keep what the person made** (plan item 4); the Hub's "settings and project list stay".
4. **Steam and Epic: verify, and re-download only what is wrong** (plan item 5).
5. **All three: choose where things live; keep projects out of the program folder** (plan items 8 and 12).
6. **Epic: pause instead of fail on a full drive; Steam and Epic: pause, resume and a limit** (plan item 6).
7. **Hub and Epic: one documented log folder and a Show in Explorer action** (plan item 9).

## Sources and how each was read (all addresses opened on 2026-11-15)

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
