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
| **Installed next to this version** | Every version under `Versions/`, with its size, where it came from (kept when the launcher updated / installed by you), **Make default** and **Remove**. At the bottom: **Keep previous versions: N** (default 1) and how much room the folder uses. |
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
   `<bucket>/VERSION.txt` and `LTS.txt`: a newer head there is added, without checksums.
3. **Download address**: `<bucket>/<bucketPath>` when the version is `inBucket`, then (and when it is not) the `archive` GitHub address
   with the same file names. If the bucket copy fails or does not match its checksum, the GitHub copy is tried.
4. **Every file's size and SHA-256 must match the index before it is moved into place.** Nothing that failed is ever kept or run.
5. `SDL2.dll` is not in the index today: it is copied from the launcher's own `Tools/` (same SDL 2.x), with a note in
   [release_channels.md](release_channels.md) asking the release tooling to publish it.

**No index at all** (offline bucket, old server): the heads of `VERSION.txt` (bucket and GitHub, the newest wins) and `LTS.txt` give a short
list without checksums, and the page says so. Such an install is refused unless the server's reported size is known, the downloaded
size matches it exactly, **and you confirm** ("Install without a checksum?"). With nothing reachable the page says "Couldn't reach the
version list" with **Try again**, and every other part of the launcher behaves as it always did.

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
versions you installed yourself, the newest installed patch of the LTS line, the default editor. The downloads of the update itself are
also checked against `versions.json` when it lists the version (a mismatch fails the update and leaves the live files alone).

## Project cards and Open with (Create page)

* Each card shows the project's version (`okayver::ReadProjectVersion`; "version unknown" when there is none) and a small badge when it is
  not this editor's line: **older: v2.850.x**, **newer** (hover for the sentence).
* Right-click (or **...**) > **Open with** lists every installed editor, the project's line first ("this project's version"), then
  **Get more versions...**. The editor is started as `<launcher dir>/Versions/<v>/Tools/OkayEngine --launcher <pid> --open "<scene>"
  --keep-version`.
* A plain **Open** starts this launcher's editor, which asks the upgrade question itself: the launcher asks nothing. The exception is the
  default editor: a project with no version goes to it (with `--keep-version`).
* **One editor at a time.** The editors share a single-instance lock, so a second one only hands its project to the first. If the open
  editor is another version than the one wanted, the launcher asks it to save and close (the same window the update uses), then starts the
  other. The launcher remembers which version it started last; an editor opened by hand is taken to be this version's.

## Command line

| | |
|---|---|
| `OkaySpace --versions [<version>]` | open the Versions page, highlighting that version (or the newest of its line). If a launcher is already running, the second one leaves a note (`okayspace_launcher_open.txt` in the temp folder) and exits; the running one polls it, comes forward and opens the page. |
| `OKAY_UPDATE_SERVER=http://127.0.0.1:<port>` | test the page against a local fake release feed (the launcher's existing override; plain http is accepted only for 127.0.0.1 / localhost) |

The editor's "v2.850.x is not installed" window has an **Open launcher** button that starts `OkaySpace --versions <version>` through the
same helper process the self-updater uses.

## launcher.cfg

`update_channel=latest|lts`, `keep_previous=1` (0 to 9), `default_editor=2.850.1`, `lts_line=2.850` (the LTS line last seen, so
pruning can protect it while offline).

## Failure cases

| What | What happens |
|---|---|
| Not enough free space for an install | refused before downloading, says how much is needed and free |
| The disk fills mid-download, or any step fails | the scratch folder is removed; no version folder; other versions untouched |
| Cancel | the curl child is stopped, the partial file removed, "Nothing was installed" |
| A file does not match its checksum / size | not kept; the other source is tried; then "did not match its checksum, so it was not installed" |
| No checksum known | refused unless the size is known and matches and you confirmed |
| Remove the running version, the launcher's version, the default editor, a version an open editor uses | refused with the reason |
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
