# The launcher's Versions page

The launcher keeps other versions of the editor next to the one you are running, so every project can be opened with the version it
was made with. This page is the other half of [project_upgrade.md](project_upgrade.md) (the editor's question when it opens a project of
another version) and uses the release feed described in [release_channels.md](release_channels.md).

The rules are in `launcher/VersionsManager.hpp/.cpp` (no SDL, ImGui or network code: the tests run on a temp folder with an injected
downloader), the download is `launcher/VersionsNet.cpp`, and the page is `launcher/VersionsUI.cpp`.

## What the page shows (nav: **Versions**)

| Card | What it does |
|---|---|
| **This version** | The version you are running, with **Latest** / **LTS** badges (from the version list), **Check again**, and **Open install folder**. If the version list is incomplete or unreachable it says so here. |
| **Update channel** | **Latest** or **Long-term support (LTS)**, a short explanation, and the LTS offer (see below). Also the **Default editor** for projects that have no version yet. |
| **Installed next to this version** | Every version under `Versions/`, with its size, where it came from (kept when the launcher updated / installed by you), **Make default** and **Remove** (which never deletes a folder that holds a project of yours: see "Removing a version never deletes a project"). At the bottom: **Keep previous versions: N** (default 1) and how much room the folder uses. |
| **Available to install** | The versions in the list that are not installed, newest first, with **Install**, a progress bar and **Cancel**. A failed check ("OkayEngine.exe did not match its checksum, so it was not installed. The download may be damaged: try again.") stays under the row until the next try. |

Wording stays plain: "v2.850.x is the long-term-support version: it only gets fixes."

Opened with `--versions 2.850.1` (the editor's **Open launcher** button, see below) the page shows a banner ("The editor needs v2.850.x to
open that project with its own version. Install v2.850.1 below.") and the matching row is highlighted and scrolled into view.

## The folder

```
<launcher dir>/Versions/<version>/Tools/OkayEngine.exe          <version> is canonical: 2.850.1
                                       /OkaySpacePlayer.exe
                                       /SDL2.dll
<launcher dir>/Versions/<version>/install.txt                   source=kept | installed, date
<launcher dir>/Versions/.partial-<version>-N/                   scratch while a version is being made (never listed)
<launcher dir>/Versions/.removing-<version>-N/                  scratch while one is being deleted (never listed)
```

This is the layout the editor reads (`okayver::InstalledEditorPath`, `InstalledVersions`). A version folder is **whole or not there**:
files are downloaded, checked and assembled in a `.partial-*` folder, and that folder is renamed into place as the very last step. A
failed, cancelled or full-disk install removes its scratch folder; a remove renames the folder away first, so the version disappears at
once or not at all. Leftover scratch folders from a crash are deleted when the launcher starts.

## Where a version comes from, and how it is checked

1. The launcher fetches `<bucket>/versions.json` (the bucket address it already uses for updates) and reads it tolerantly: the schema must
   be exactly `1`; an entry it cannot use is skipped; sizes must be whole numbers under 4 GiB; checksums are 64 hex digits; the `archive`
   and `bucketPath` templates cannot leave their place; garbage, a newer schema or a 404 page is "no index".
2. The index can lag (it is written after the files, and a failure there only warns), so the heads are checked against
   `<bucket>/VERSION.txt` and `LTS.txt`: a newer head there is listed, but **it cannot be installed** until `versions.json` carries its
   checksums (the row says "no checksum: can't be installed here" and the install says why).
3. **Download address**: `<bucket>/<bucketPath>` when the version is `inBucket`, then (and when it is not) the `archive` GitHub address
   with the same file names. If the bucket copy fails or does not match its checksum, the GitHub copy is tried.
4. **Every file's size and SHA-256 must match the index before it is moved into place.** Nothing that failed is ever kept or run.
5. `SDL2.dll` is not in the index today: it is copied from the launcher's own `Tools/` (same SDL 2.x), with a note in
   [release_channels.md](release_channels.md) asking the release tooling to publish it.

**No index at all** (offline bucket, old server): the heads of `VERSION.txt` (bucket and GitHub, the newest wins) and `LTS.txt` give a short
list without checksums, and the page says so. **Nothing in that list can be installed**: there is no "install anyway" (the old "Install
without a checksum?" question is gone). The row says "no checksum: can't be installed here", and trying it explains that versions.json
could not be read from the release server and what to do. With nothing reachable the page says "Couldn't reach the version list" with
**Try again**, and every other part of the launcher behaves as it always did. How the updater itself treats a missing or damaged index
is in [updater.md](updater.md).

The downloads only ever go under `<launcher dir>/Versions`. The launcher never writes into a project folder.

## The channel (Latest or LTS)

Settings > Behavior > Startup & updates (and the Versions page) has the channel.

* **Latest** (default): follows `VERSION.txt`, exactly as before.
* **Long-term support**: follows `LTS.txt`. The launcher only offers the LTS line's patches; if there is no LTS information (offline, or no
  line has been promoted yet) it does **not** fall back to the latest version (that would move you off the line you chose); it says so.

The launcher itself is **never downgraded in place**. When it is newer than the LTS line, choosing LTS installs the LTS editor next to it
in `Versions/` and offers **Use v2.850.x as my default editor**. The default editor is used when opening a project that has no version
recorded, and any installed editor can be picked for one project from its **Open with** menu. `okayversions::DecideChannel` holds this
decision (a test walks every combination and checks an in-place update never goes to an older version than the one that runs).

## Keeping the previous editor on update

When the launcher installs an update it first copies the current `Tools/` editor, player and `SDL2.dll` into
`Versions/<current>/Tools/` (all or nothing), unless that folder already exists, **Keep previous versions** is 0, or the build is a
development build (no version). If free space would drop under 256 MB after the copy it is skipped and the update says so ("Not enough
free disk space to keep v2.855.0 ... The update goes on without keeping it."). A failed copy never blocks the update; if the update itself
then fails, the copy is deleted again (it would only duplicate what still runs).

After a successful update the automatic copies beyond **Keep previous versions: N** are removed, newest kept first. **Never** pruned:
versions you installed yourself, the newest installed patch of the LTS line, the default editor. The downloads of the update itself
(the in-place updater, `launcher/UpdaterCore.hpp`) REQUIRE `versions.json` to list a size and SHA-256 for each file: with no index, no entry for
the version or no checksum the update is refused, and so is a failed download, a size or checksum mismatch or a failed swap. Each case shows its
reason in the update dialog, is logged in `okay_update.log`, and leaves the installed files as they were (every file is staged and checked before any is replaced).

## Removing a version never deletes a project

The editor's default folder for a new project is beside its exe. For a kept editor that is `Versions/<v>/Tools/Projects`, so a project made there
lives **inside the version folder** (before this change "Remove" deleted it, while its window said "Your projects are not touched").
Before anything is deleted the launcher looks through the folder (`ScanFolderForProjects` and `CheckBeforeDelete` in
`launcher/VersionsManager.cpp`; no window, nothing written):

* **What counts as yours:** a `project.okayproj` file, a scene (`.okayscene`), or any file inside an `Assets` folder (names are not case
  sensitive). **Not yours:** a `.okay` folder on its own, an empty `Assets` folder, the editor's own files (crash logs, `imgui.ini`, ...).
* **The look is bounded:** 12 folders deep and 20,000 files and folders (`ScanLimits`; a real version folder holds about ten), breadth first, so
  the shallowest project is named first. **Links are never followed** (a symbolic link, and on Windows a junction): a folder with a link in it
  is refused too, because deleting could reach what the link points at. A link to a file, or to nowhere, is only a link.
* **When in doubt, nothing is deleted.** A project, a link, a folder too big or too deep to be sure about, or one that could not be read is
  refused with a plain reason: "This version folder has a project of yours: `<path>`. Move it out first. Nothing was removed." (`DeleteCheck`).

Every delete of a version folder goes through that check: the **Remove** button; the **prune** after an update and "Remove them now" (a version
that stays says why: `PruneResult::notRemoved`); installing or keeping a version over a folder that is "in the way" (a folder of that name with
no editor in it, which used to be deleted whole); discarding a kept copy after a failed update; and the start-up clean-up of scratch folders.

**The Remove window** looks when it opens (not every frame). With a project inside it shows the message and an **Open folder** button (the
same opener as "Open install folder"; it shows the folder that holds the project) and **Close**: there is no Remove button until the project is
moved out (cut it and paste it into your Projects folder). Either way it says how many of your projects were made with that release line (after "This frees 41.2 MB.
Your projects are not touched." when nothing is in the way): "3 of your projects were made with v2.850.x. You can open them with that version again after you
install it." (`CountProjectsMadeWith`; the project versions are the ones the launcher already reads for the card badges). `Remove` itself looks again,
so a project that arrives while the window is open is still refused. There is no bulk "remove copies no project needs" yet (audit item 5).

Headless renders of the two states (Dear ImGui into a software surface on Linux, the real `VersionsUI.cpp` drawing code, a made-up folder, not the launcher program and not Windows):
[with a project inside](images/versions_safety/remove_refused_project_inside.png) and
[after the project was moved out](images/versions_safety/remove_confirm_after_moving_out.png).

## Project cards and Open with (Create page)

* Each card shows the project's version (`okayver::ReadProjectVersion`; "version unknown" when there is none) and a small badge when it is
  not this editor's line: **older: v2.850.x**, **newer** (hover for the sentence).
* Right-click (or **...**) > **Open with** lists every installed editor, the project's line first ("this project's version"), then
  **Get more versions...**. The editor is started as `<launcher dir>/Versions/<v>/Tools/OkayEngine --launcher <pid> --open "<scene>"
  --keep-version=<v>`. The flag **names the version**: only an editor of exactly `<v>` trusts it (see "Kept copies never update themselves").
* A plain **Open** starts this launcher's editor, which asks the upgrade question itself: the launcher asks nothing. The exception is the
  default editor: a project with no version goes to it (with `--keep-version=<v>`).
* **One editor at a time.** The editors share a single-instance lock, so a second one only hands its project to the first. If the open
  editor is another version than the one wanted, the launcher asks it to save and close (the same window the update uses), then starts the
  other. The launcher remembers which version it started last; an editor opened by hand is taken to be this version's.

## Kept copies never update themselves

An editor that runs from `<launcher dir>/Versions/<v>/Tools` is a **kept copy** (`okayver::IsKeptCopyDir`, `shared/ProjectVersion.hpp`: pure text,
so both kinds of slash, a trailing slash and, on Windows, any letter case work; the folder must be `<anything>/Versions/<version>/Tools` with the
version spelled the way the launcher writes it). Before this change every editor, a kept one too, checked for a newer release at start-up and offered
**Download & Install**. That replaced the version-labelled file with the newest editor and restarted it with the same arguments, and the newest
editor then opened the old project with no question and no backup. Now a kept copy:

* does not check for updates at start-up, never shows the **Engine Update** window, never installs an update (`InstallUpdate` refuses), and so
  never offers "Automatically install future updates" (that box lives in the Engine Update window);
* shows **one plain line** where the update window would be (Help > Check for Updates..., and the "Update OkaySpace" button of the version
  question): "This is a kept copy of v2.850.1. Update OkaySpace from the launcher.", with the existing **Open launcher** button (it opens this
  page); no Download & Install;
* shows `(kept)` after its version in the status bar.

Two rules about `--keep-version` close the rest of that chain (`okayver::HonoursKeepFlag`, tested in `versions_safety` and `project_upgrade`):
the launcher passes the form that **names a version**, and only an editor of exactly that version trusts it; a **bare** `--keep-version` (what
older launchers passed to every editor) is trusted only by a kept copy, never by the current editor. So even a kept editor that did replace itself
would not make the newest editor open the old project silently: it asks its upgrade question as usual.

What this does not do:

* A kept copy made **before** this change is an older program with the old code. It still updates itself, and nothing in the launcher can change a
  program that is already on disk. The named flag still stops the silent open for it. Every copy kept from now on carries the new code.
* The check is by folder name only, so a folder that merely looks like `.../Versions/<version>/Tools` counts as a kept copy (it just will not update
  itself: the safe side). A kept copy moved somewhere else is no longer one.

## Command line

| | |
|---|---|
| `OkaySpace --versions [<version>]` | open the Versions page, highlighting that version (or the newest of its line). If a launcher is already running, the second one leaves a note (`okayspace_launcher_open.txt` in the temp folder) and exits; the running one polls it, comes forward and opens the page. |
| `OKAY_UPDATE_SERVER=http://127.0.0.1:<port>` | test the page against a local fake release feed (the launcher's existing override; plain http is accepted only for 127.0.0.1 / localhost) |

The editor's "v2.850.x is not installed" window has an **Open launcher** button that starts `OkaySpace --versions <version>` through the
same helper process the self-updater uses.

## launcher.cfg

`update_channel=latest|lts`, `keep_previous=1` (0 to 9), `default_editor=2.850.1`, `lts_line=2.850` (the LTS line last seen, so
pruning can protect it while offline). Settings > Behavior > Reset preferences puts the channel back to
Latest, Keep previous versions to 1 and the default editor to "this version" (installed versions are left alone).

## Failure cases

| What | What happens |
|---|---|
| Not enough free space for an install | refused before downloading, says how much is needed and free |
| The disk fills mid-download, or any step fails | the scratch folder is removed; no version folder; other versions untouched |
| Cancel | the curl child is stopped, the partial file removed, "Nothing was installed" |
| A file does not match its checksum / size | not kept; the other source is tried; then "did not match its checksum, so it was not installed" |
| No checksum known | refused before anything is downloaded ("no checksum in the version list"; there is no install-anyway) |
| Remove the running version, the launcher's version, the default editor, a version an open editor uses | refused with the reason |
| Remove (or prune, or install over) a version folder that holds a project of yours, has a link in it, or is too big, too deep or unreadable to check | refused with the reason and the path; nothing deleted; the window offers **Open folder** |
| A remove cannot delete everything after renaming | the version is gone from the list; the scratch folder is deleted at the next start |
| The Versions folder cannot be made (a file is in the way, read-only) | a plain failure message |

## Tests

`launcher_versions`: the old and shared comparison agree for every value the launcher compares; `versions.json` (garbage, missing fields,
schema 0/2/huge/text, huge numbers, hostile names and templates, duplicates, a BOM, a 4 MB body); the VERSION.txt / LTS.txt fallback and
head reconciliation; download sources and plans; install (good path, bucket tampered then GitHub, both tampered, wrong sizes, 404, cancel
at five points, full disk, a failure at every one of the 13 steps in turn); installing without a checksum; remove with every guard;
keep-on-update with a failure at every step; prune planning and on disk; the channel decision for every combination; settings lines; badges;
Open with choices, command-line quoting for both platforms and unicode/space/quote paths; the project folder staying byte-identical; the
`--versions` command line and hand-off; the cancellable download with a stub `curl` (no network).

`versions_safety` (`tests/test_versions_safety.cpp`, temp folders only): kept-copy detection in every path spelling; the `--keep-version` matrix (bare or
named, kept copy or current editor) and the arguments the launcher builds; what counts as a project; Remove refused with a project beside the exe, deep
inside, in `Tools/Projects`, a scene or an `Assets` folder with no project file, a link, a loop, a folder too big or too deep (and an unreadable one,
when not run as root); the same refusal for the prune, Install, KeepCurrent, DiscardKept and CleanStale; a version with no project still removed
(only an `.okay` folder, an empty `Assets` skeleton, the editor's own files); the count of projects per version. The editor's side of the flag is in
`project_upgrade`.

## Design choices

* **Rename into place.** The only atomic step common to every file system the launcher runs on is renaming a folder: everything else is
  prepared beside it.
* **The index is the root of trust** (checksums and the GitHub address come from it), so a version that is not in it can only be installed
  with a typed confirmation.
* **LTS never falls back to Latest.** Offline, or with no LTS published, choosing LTS changes nothing; a silent move to a newer line would
  be the one thing a person on LTS does not want.
* **Only automatic copies are pruned.** Anything installed on purpose stays until it is removed on purpose.
* **No second prompt.** The editor owns the upgrade question; the launcher only picks which editor starts.
* **Windows paths.** The launcher's own folder is found with the wide-character API; its older helpers still turn paths into the ANSI code
  page on Windows (`CreateProcessA`), so a launcher installed in a folder with characters outside that code page cannot start an editor
  (this was already so). On other platforms every path with spaces, quotes or unicode is quoted per argument.

## What changed

New: `launcher/VersionsManager.hpp/.cpp`, `launcher/VersionsNet.hpp/.cpp`, `launcher/VersionsUI.hpp/.cpp`, `tests/test_launcher_versions.cpp`, this
page. Edited: `launcher/main.cpp` (hook calls: version comparison, prefs, update flow, command line and hand-off, nav row and page, Settings
rows, project cards and Open with), `launcher/CMakeLists.txt`, `editor/ProjectUpgradeUI.cpp` (the **Open launcher** button),
`tests/CMakeLists.txt`, [project_upgrade.md](project_upgrade.md), [release_channels.md](release_channels.md). The launcher's own
`CompareVersions` now calls `okayver::CompareVersions` (one difference: `0.0.0` used to equal text that is no version, now it is newer).

Later, from the install audit ([install_audit.md](install_audit.md), findings 1 and 2): `shared/ProjectVersion.hpp` (`IsKeptCopyDir`, `HonoursKeepFlag`,
`ParseKeepFlag`, the named `KeepVersionArgs`), `launcher/VersionsManager.*` (`ScanFolderForProjects`, `CheckBeforeDelete`, `CountProjectsMadeWith`, the named
`OpenWithArgs`, the guard in every delete), `launcher/VersionsUI.*` (the Remove window, `Host::projectVersions`), `launcher/main.cpp` (two small calls),
`editor/ProjectUpgrade.*` and `editor/ProjectUpgradeUI.*` (the gate, the kept-copy window), `editor/main.cpp` (five small edits in the update code),
`tests/test_versions_safety.cpp`, `tests/test_project_upgrade.cpp`. All of it was written from the audit's findings and the repository's own interfaces (no outside code); the
scan walks folders breadth first with `std::filesystem`, and the Windows reparse-point check follows the Win32 documentation (`FindFirstFileW`, the symlink and
mount-point reparse tags).
