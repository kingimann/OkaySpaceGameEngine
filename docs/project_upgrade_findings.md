# Upgrade project: findings for issue #888 (items 5 and 7)

Written from reading the code on `main` at `33499ef` and the known-bug tests on `claude/upgrade-bug-repro` (`ecc2c3e`,
`tests/test_upgrade_bugs.cpp`). **Nothing here was built or run.** Every answer is from reading code. Line numbers are for
`main` at `33499ef`. Other agents are fixing `ProjectRootForScene` and the startup modal fight; this page only reports.

See [project_upgrade.md](project_upgrade.md) for how the feature is meant to work.

## Item 5: every release asks "Upgrade or keep?"

### What decides that the question is asked

| Step | Where | What it does |
|---|---|---|
| 1 | `editor/ProjectUpgrade.cpp:852` | `ProjectGate::Open` reads the project's stamp: the `engine_version=` line of `<project>/project.okayproj`, normalized (`shared/ProjectVersion.hpp:344-356`). |
| 2 | `editor/ProjectUpgradeUI.cpp:440-445`, `:98` | The running version is `OKAY_ENGINE_VERSION`, which is the `VERSION` in `CMakeLists.txt` (`2.874.0` now). |
| 3 | `editor/ProjectUpgrade.cpp:853` | `okayver::Decide(projectVersion, runningVersion)`. |
| 4 | `shared/ProjectVersion.hpp:210-218` | `Decide`: equal is `Same`; the same **line** is `SamePatchLine`; anything else is `Older` or `Newer`. |
| 5 | `shared/ProjectVersion.hpp:157-166` | The line is the **first two numbers**: `LineOf("2.873.0")` is `"2.873"`. |
| 6 | `editor/ProjectUpgrade.cpp:861-870` | `Older` and `Unknown` ask (unless `--keep-version` was given). `Newer` asks. `Same`, `SamePatchLine` and `DevBuild` open silently (`:855-860`). |

The same `Decide` also runs in the safety net `ProjectGate::MayLoad` (`editor/ProjectUpgrade.cpp:833`), in the File > Project Upgrade
window (`editor/ProjectUpgradeUI.cpp:327`) and in the launcher's "older" / "newer" badge (`launcher/VersionsManager.cpp:848`).

**Why every release asks.** A release bumps the second number: 2.871.0, 2.872.0, 2.873.0, 2.874.0 (`git log -p -- CMakeLists.txt`,
`dist/CHANGELOG.txt`). So each release is a new line. A project last saved by 2.873.0 and opened in 2.874.0 is
`Decide("2.873.0", "2.874.0")`: not equal, `LineOf` is `2.873` against `2.874`, so the result is `Older` and the question comes up.
`SamePatchLine` only helps for patch releases like 2.861.1, which are rare.

### Is there a migration step per engine version?

No. There is no table keyed by engine version anywhere.

- The only file migrations are for **scene and prefab files**, keyed by the scene format number (`okayscene N`), not the engine
  version: `engine/src/Scene/SceneMigration.cpp:26-49`.
- The scene format is `okayscene 3` (`engine/include/okay/Scene/SceneSerializer.hpp:72`). Two steps exist:

| Step | Format change | Came in (from `ROADMAP.md`) |
|---|---|---|
| `character-animator` | `okayscene 1 -> 2` (PR #346) | 2.832.0 (`ROADMAP.md:57`) |
| `view-toggle-controllers` | `okayscene 2 -> 3` (PR #510) | 2.840.0 (`ROADMAP.md:49`) |

- `SceneMigration.cpp:24-25` says everything added since (guid, sibling, compoff, override, layermatrix, and later records such as
  `vehicleseat`) was added **without** a format bump: older files just lack those records.
- The upgrade job (`UpgradeJob`, `editor/ProjectUpgrade.cpp:445-491`) runs `SceneMigration::MigrateThroughEngine` on every scene
  and prefab, then rewrites the stamp in `project.okayproj`. It has no other per-version step.

So from 2.840.0 to 2.874.0 (more than 30 releases) no file format changed, but every one of them asks every project. When the person
clicks **Upgrade**, the job still copies the **whole project** into a backup folder (`DoBackup`, `:523`) and then only changes one
line (the stamp). That is a full project copy per project per release, for nothing.

### Proposal (not implemented: the owner decides)

Ask only when a migration really applies to the project's version.

1. **A list of format-changing versions.** Add one table to `shared/ProjectVersion.hpp` (header-only, so the editor, the launcher and
   the tests share it), for example:

   | Engine version | What changed | Files |
   |---|---|---|
   | 2.832.0 | `okayscene 2`: Character gets a visible Animator | scenes, prefabs |
   | 2.840.0 | `okayscene 3`: first/third person player gets both view controllers | scenes, prefabs |

   A row is added in the same PR that raises `kSceneFormatVersion`, or that changes any other project file in a way an older
   editor cannot read.

2. **A new decision, `OlderCompatible`.** In `Decide`: when the project is older, look for a row with
   `projectVersion < row <= runningVersion`. None: `OlderCompatible`. At least one: `Older` (ask, as today).
   `OlderCompatible` behaves like `SamePatchLine`: open with no question, and the next save writes the new stamp.
   Example: a 2.850.0 project in 2.874.0 opens silently (no row after 2.840.0). A 2.835.0 project in 2.874.0 still asks
   (2.840.0 is in the range).

3. **Optional second check, on the files.** When a row is in range, read only the first line (`okayscene N`) of each scene and
   prefab. If every file is already at the current format, nothing would be rewritten, so also open silently. This is read-only,
   so it keeps the promise that nothing is written before the answer. It costs one small read per scene file.

4. **Keep `Newer` asking, always.** New records are added without a format bump, so a format table cannot see them. Whether an
   older editor keeps a record it does not know depends on that older editor's own code. Today's loader tries to carry unknown
   records along (`engine/src/Scene/SceneSerializer.cpp:4238-4239`, `:8950-8954`); the known-bug test `upgrade_bug_2` checks
   that the upgrade keeps them (not run). Older editors may not. So opening a newer project in an older editor stays a question.
   Side effect the owner should know about: after a silent open in 2.874.0 and a save, the project is stamped 2.874.0, so
   opening it again in 2.873.0 asks "newer".

5. **Keep `Unknown` asking.** A project with no stamp cannot be placed in the table. Step 3 could make it silent too; that is
   the owner's call.

6. **A guard test.** A test that fails when `SceneSerializer::kSceneFormatVersion` is bigger than the number of rows the table
   says (2 rows now, format 3). Then nobody can raise the format and forget the row.

What changes for people: projects open with no question after most releases. The launcher badge (`VersionsManager.cpp:848`)
says "older" only when an upgrade really applies. **Keep vX** is only offered when the question is asked. File > Project
Upgrade still works by hand.

The risk: a missing row means a project is upgraded on its first save with no backup. The scene loader still applies old-format
fixes in memory when it loads an old file, so the scene opens correctly; only the backup is skipped. The guard test in step 6 is
what keeps that from happening.

## Item 7: three suspected problems

### 7a. Is a second `--open` dropped while the project gate is busy? **Yes.**

| Step | Where |
|---|---|
| A second launch loses the single-instance check and writes a hand-off file with `open\t<scene>`, then exits. | `editor/main.cpp:47784-47787`, `WriteHandoff` `:47376-47392` |
| The running editor reads it (`PollHandoff`) and calls `RunAfterSavePrompt`. | `editor/main.cpp:48982-48987`, `:47397-47423` |
| With no unsaved changes, `RunAfterSavePrompt` runs the action at once: `g_openScenePath = hoOpen`. | `editor/main.cpp:18574` |
| `DrawNewProjectPopup` copies the path, **clears `g_openScenePath`**, and calls `RunAfterProjectCheck` with **no cancel callback**. | `editor/main.cpp:18909-18914` |
| `ProjectGate::Open`: `if (Busy()) { if (cancel) cancel(); return; }`. Cancel is empty, so nothing happens. | `editor/ProjectUpgrade.cpp:849` |

No message, no console line, and the path is already cleared, so it is never retried. The hand-off file is deleted too
(`editor/main.cpp:47419`). The window is raised (`:48990`), so it looks like the click did something.

Concrete input: start the editor from the launcher on project A, which is older (the "Project version" question is on screen).
In the launcher, click **Open** on project B. The editor comes to the front, the question for A stays, and B is never opened.
File > Open is blocked by the modal, so the hand-off is the main way in. A scene opened through `LoadSceneNow`
(`editor/main.cpp:47031`) has the same pattern (no cancel callback).

The docs say "one question at a time" (`docs/project_upgrade.md`), which is true, but the second request should be queued or the
person told. Not run.

### 7b. Do `ProjectRootOfScene` and `ProjectRootForScene` disagree? **Yes, on several inputs.**

- `ProjectRootOfScene` (`editor/main.cpp:18621-18625`): the scene's folder; if that folder is called `Assets`, its parent. One level
  only, no disk check.
- `ProjectRootForScene` (`editor/ProjectUpgrade.cpp:117-129`): walks up to the **nearest ancestor called `Assets`, at any depth**,
  and returns its parent (`:123-124`). With no `Assets` above, the scene's folder only if it looks like a project
  (`IsProjectRoot`: has `Assets`, `project.okayproj` or `.okay`, `:111-115`), else `""`.
- `EditorState::Load` uses the same walk as `ProjectRootForScene` (`editor/EditorState.cpp:1038-1039`).

| Scene path | `ProjectRootOfScene` | `ProjectRootForScene` | Same? |
|---|---|---|---|
| `P/Assets/Main.okayscene` | `P` | `P` | yes |
| `P/Main.okayscene`, `P` has `project.okayproj` | `P` | `P` | yes |
| `P/Assets/Scenes/Main.okayscene` | `P/Assets/Scenes` | `P` | **no** |
| `P/Levels/a.okayscene` (no `.okay` in `Levels`) | `P/Levels` | `""` | **no** (known-bug test `upgrade_bug_1`) |
| `D:/Downloads/test.okayscene` (loose file) | `D:/Downloads` | `""` | **no** |
| `C:/Users/me/Assets/Downloads/test.okayscene` | `C:/Users/me/Assets/Downloads` | `C:/Users/me` | **no** |
| `Main.okayscene` (relative) | `""` | the current folder, if it looks like a project | **no** |

Where `ProjectRootOfScene` is used, and what goes wrong:

- The startup chooser's recent list (`editor/main.cpp:18861`): a recent scene in `P/Assets/Scenes` (the Scene Manager and
  "save new scene" both put scenes there: `editor/SceneManagerPanel.cpp:551`, `editor/main.cpp:46801`) shows **"Scenes"** as
  the project name and `P/Assets/Scenes` as its path.
- `RecentLocations` (`editor/main.cpp:18631`): the same scene gives `P/Assets` as the "projects location", so New Project and Open
  Project Folder start inside the old project's `Assets` folder.
- The fallback after a load (`editor/main.cpp:18919-18920`): for a loose file the gate says "not a project" but the Project panel
  is rooted at the file's folder anyway.

One more thing the table shows (worth passing to whoever fixes `ProjectRootForScene`): any folder called `Assets` higher up counts.
A loose scene at `C:/Users/me/Assets/Downloads/test.okayscene` gives the root `C:/Users/me`. That folder has no stamp, so the
gate asks the `Unknown` question, and **Upgrade** would try to back up the whole user folder. Not run; from reading
`ProjectRootForScene` and `UpgradeJob::DoScan`.

### 7c. Does File > Open Project Folder only look in `Assets/` and the root? **Yes.**

`OpenProjectFolderDialog` (`editor/main.cpp:18647-18666`) strips a picked `Assets` folder (`:18653`), then looks for the first
`.okayscene` in `root/Assets` and then in `root`, **not recursively** (`:18656-18659`, `fs::directory_iterator`). With none, it shows
"That isn't an OkaySpace project" (`:18661`).

Concrete input: a project `MyGame/` with `project.okayproj` and `Assets/Scenes/Main.okayscene`, and no scene directly in `Assets/`.
Picking `MyGame` shows the error, though it is a real project. New projects put their first scene at `Assets/<Name>.okayscene`
(`editor/EditorState.cpp:1082`), so this hits projects whose scenes were moved or made in `Assets/Scenes` (the Scene Manager's
"new scene" and the first save of an unsaved scene go there).

Two smaller points in the same loop: the "first" scene is in `directory_iterator` order, which is not sorted, so which scene opens
can differ between machines; and the `.okayscene` check is case-sensitive (`Main.OKAYSCENE` is not found).

## What was not checked

- Nothing was built or run, including the known-bug tests on `claude/upgrade-bug-repro`.
- The release version for each scene format step is taken from `ROADMAP.md` rows, not from the release tags.
- Whether any file other than scenes, prefabs and `project.okayproj` (materials, animator graphs, input maps) has a format that an
  older editor cannot read. The proposed table must cover those too if they exist.
