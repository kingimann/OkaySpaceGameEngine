# Splitting `editor/main.cpp`: the plan

The owner approved splitting `editor/main.cpp` into smaller files by feature, slowly, so code is easy to find and
duplicates stop. Nothing may break. This page is the map, the order, the rules every move follows and the checks
it must pass. Feature map: 15.10.

Line numbers below are for `origin/main` at `61332f077` (59,952 lines), before move 1. They go stale with every
merge: re-measure a section (grep for its first and last line) right before you move it.

## 1. What is in `main.cpp`

How the numbers were made: a small brace counter (comments and strings skipped) found every top-level definition and
its size; a section is a run of definitions that belong to one feature, cut at the file's own `// ----` headers.
"Changed" is the share of the section's lines that `git blame` dates inside the 9 days of history this clone has
(2026-11-13 to 2026-11-22): a rough measure of how often it is edited. "Needs" counts the names defined elsewhere in
`main.cpp` that the section uses (globals, helpers, structs: each becomes a declaration in a shared header when the
section moves). "Used by" counts the names it defines that the rest of `main.cpp` uses (each becomes a declaration
in the section's new header). Both counts are rough (a few false hits on common words); the compiler gives the exact
list during a move.

| # | Lines | Size | Section | Changed | Needs | Used by | Who calls it | Open PR inside |
|---|---|---|---|---|---|---|---|---|
| 1 | 1-1014 | 1,014 | Includes, script lint, headless self-test (`RunSelfTest`, `RunStaleGraphTest`) | 10% | 0 | 13 | `main`, Actions, Inspector | #1120 (one include) |
| 2 | 1015-1773 | 759 | Crash diagnostics, direct viewport textures, thumbnails, colour and IK helpers | 9% | 2 | 59 | almost everything | - |
| 3 | 1774-1848 | 75 | Built-in 2D sprite shapes | 0% | 0 | 3 | menus, Sprite editor | - |
| 4 | 1849-2120 | 272 | Self-updater (`namespace updater`) | 22% | 2 | 9 | Build Game, update popup, menus, `main` | - |
| 5 | 2121-2335 | 215 | Window flags, UI backend switch, small shared state | 14% | 2 | 117 | everything | #1128 (`UseOkayUIBackend`) |
| 6 | 2336-2846 | 511 | Collider wireframes, physics material fields, settings load and save | 47% | 48 | 27 | Scene view, Inspector, Hierarchy | - |
| 7 | 2847-2985 | 139 | Custom-action library | 1% | 3 | 28 | Actions, Inspector | - |
| 8 | 2986-3102 | 117 | Import Model dialog, script tabs, build-dialog globals | 10% | 0 | 60 | many | - |
| 9 | 3103-3525 | 423 | Build profiles, project settings globals (`g_project`) | 17% | 4 | 153 | many | - |
| 10 | 3526-4401 | 876 | Mesh-edit undo, polygon view, modeling commands, selection tools | 64% | 41 | 67 | Modeling, Scene view, Inspector | - |
| 11 | 4402-4468 | 67 | External-IDE helpers (`namespace extide`) | 6% | 1 | 9 | Project panel, Script Editor, plugins | - |
| 12 | 4469-5615 | 1,147 | **Build Game (`namespace builder`)**: the game, Web / Android / Xbox data, dedicated server | 5% | 8 | 11 | build dialogs, `RunBuildGameCli`, `main`, UI menu runner | - |
| 13 | 5616-5676 | 61 | Editor theme | 13% | 2 | 1 | menus, `main` | - |
| 14 | 5677-6815 | 1,139 | Panel tabs, layouts, panel switcher | 7% | 33 | 24 | menus, Viewport, Inspector | - |
| 15 | 6816-7260 | 445 | UI menu runner, playback speed controls | 70% | 11 | 6 | menus, Viewport, `main` | - |
| 16 | 7261-8571 | 1,311 | Main menu bar, creators, GameObject menus, toolbar, status bar, dock space | 64% | 131 | 18 | `main`, Hierarchy, Scene view | - |
| 17 | 8572-8786 | 215 | Audio Mixer, Crash Log, History panels | 6% | 6 | 4 | `main` | - |
| 18 | 8787-8994 | 208 | Find in Files | 0% | 2 | 17 | Script Editor, Project panel | - |
| 19 | 8995-9166 | 172 | Project panel right-click menus, file pickers | 17% | 7 | 35 | Project panel, Animation, Hierarchy | - |
| 20 | 9167-9806 | 640 | Character model swap, auto-rig, folder tree | 9% | 19 | 34 | Inspector, Hierarchy, Project panel | - |
| 21 | 9807-10100 | 294 | Asset Manager | 0% | 8 | 1 | `main` | - |
| 22 | 10101-10694 | 594 | Editor plugins, bone map, plugin manager | 0% | 9 | 5 | menus, Inspector, `main` | - |
| 23 | 10695-12636 | 1,942 | Asset colour labels, Project panel (`DrawProject`, 1,718 lines) | 31% | 94 | 3 | `main`, Inspector | - |
| 24 | 12637-13718 | 1,082 | Services, Save Manager, Material editor, Stats, Look and Scene settings | 6% | 28 | 8 | `main`, Inspector | - |
| 25 | 13719-14540 | 822 | Quit prompt, shortcuts window, Scripting Reference | 4% | 10 | 6 | `main`, Script Editor | - |
| 26 | 14541-16094 | 1,554 | Script completion, signatures, docs, C# members, code-widget glue | 1% | 12 | 31 | Script Editor only | - |
| 27 | 16095-18703 | 2,609 | Script Editor (`DrawScriptEditor`, one function) | 3% | 59 | 1 | `main` | #1128 |
| 28 | 18704-20118 | 1,415 | Keyboard shortcuts, file / build / export dialogs (`DrawFileDialogs`) | 16% | 95 | 2 | `main` | - |
| 29 | 20119-20986 | 868 | Projects: errors, save prompt, recents, New Project, gate, About, project settings | 12% | 41 | 9 | `main` | - |
| 30 | 20987-22131 | 1,145 | Hierarchy | 31% | 25 | 4 | `main`, Inspector, Scene view | - |
| 31 | 22132-22879 | 748 | UI anchor helpers, Inspector add queue, Actions op table | 0% | 12 | 16 | Inspector, Actions, UV editor | - |
| 32 | 22880-23396 | 517 | OkayScript to visual script, script recipes | 0% | 6 | 5 | Actions, Inspector, `main` | - |
| 33 | 23397-26703 | 3,307 | Animation tab: live preview, shared timeline widgets, character and model clips, object timeline | 0% | 10 | 16 | `main`, Inspector, Scene view | - |
| 34 | 26704-28908 | 2,205 | Actions rules and items, flow graph, particle editors, component header, script fields | 7% | 15 | 42 | Inspector, Scene view | - |
| 35 | 28909-31179 | 2,271 | UV editor, Modeling window (`DrawModeling`, 1,865 lines) | 36% | 96 | 3 | `main` | - |
| 36 | 31180-36700 | 5,521 | Inspector parts drawn by OkayUI (stages 1 to 9) | 100% | 70 | 114 | Inspector | - (being written now) |
| 37 | 36701-49497 | 12,797 | Inspector (`DrawInspector`, **one function**) | 12% | 259 | 1 | `main` | #1059 |
| 38 | 49498-50947 | 1,450 | UI overlay and UI widget editing in the Scene view | 2% | 35 | 16 | Scene view, Inspector | - |
| 39 | 50948-51588 | 641 | Scene view "add here" menu, 2D scene view | 41% | 42 | 4 | Viewport | - |
| 40 | 51589-54843 | 3,255 | 3D scene view (`DrawScene3D`, 3,190 lines) | 23% | 157 | 4 | Viewport | - |
| 41 | 54844-55716 | 873 | Viewport, shared resolution, mobile emulator | 0% | 61 | 11 | `main` | - |
| 42 | 55717-56687 | 971 | Sprite editor, perf overlay, Profiler, Game view | 9% | 50 | 5 | `main` | #1120 (`DrawProfiler`) |
| 43 | 56688-57172 | 485 | Saving, prefab mode, document recovery, `SaveProject` | 3% | 56 | 6 | `main`, launcher gate | - |
| 44 | 57173-57667 | 495 | Launcher gate, single instance, multi-window, hand-off, `RunBuildGameCli` | 12% | 26 | 18 | `main` | - |
| 45 | 57668-59952 | 2,285 | `main()` | 19% | 258 | 0 | - | #1128, #1127, #1120, #1108 |

Open pull requests on 2026-11-22 (`gh api "repos/kingimann/OkaySpaceGameEngine/pulls?state=open"`, then
`git diff --stat origin/main...origin/<branch> -- editor/main.cpp` for each): 13 are open; 5 touch `main.cpp`:
#1128 `claude2/own-codeedit-2` (27+/8-: `UseOkayUIBackend`, `DrawScriptEditor`, `main`), #1127
`claude2/own-platform-launcher` (1 line in `main`), #1120 `claude2/texture-system` (41+: an include, `DrawProfiler`,
`main`), #1108 `claude2/found-console` (2+ in `main`), #1059 `claude/unfinished-ledge-1` (9+/2-: `DrawInspector`).

## 2. The order

Rules for the order: large (about 1,000 to 3,000 lines), self-contained (few "Needs"), rarely edited, and not inside
an open PR first. What collides with an open PR, what is edited every day and what needs a real refactor go last.

| Move | Section(s) | Size | Why here |
|---|---|---|---|
| 1 (done on `claude2/editor-split-1`) | 12 Build Game → `editor/BuildGame.hpp/.cpp` | 1,145 | Already its own namespace with external linkage, needs only 7 outside names, 5% changed, no open PR, and a CI job (`export-check`) runs it for real. |
| 2 | 4 Self-updater → `editor/Updater.hpp/.cpp` | 272 | Small, and its four declarations that move 1 put in `MainShared.hpp` then move to its own header (the "list shrinks" rule of `MainShared.hpp`). |
| 3 | 26 Script completion and docs (+ 25 Scripting Reference) → `editor/ScriptAssist.*` | 2,376 | 1% changed; used only by the Script Editor. Wait for #1128 (Script Editor) to merge first, it is next door. |
| 4 | 33 Animation tab, in two moves: object timeline + model clips (25228-26703, 1,476) then live preview + widgets + character clips (23397-25227, 1,831) → `editor/AnimationTab*.cpp` | 3,307 | 0% changed, 10 outside names. The shared timeline widgets (`AnimGlyphButton` ...) are already declared in `MainShared.hpp`; they move to the new header. |
| 5 | 18 Find in Files, 21 Asset Manager, 22 Editor plugins → `editor/AssetManagerWindow.*`, `editor/PluginsWindow.*` | 1,096 | 0% changed, few names. |
| 6 | 31 + 32: UI anchor helpers, Actions op table, OkayScript to visual script, recipes → `editor/ActionsTable.*` | 1,265 | 0% changed. Must go before move 9 (Actions editors use it). |
| 7 | 24 Services, Save Manager, Material editor, Stats, Scene settings → `editor/ProjectWindows.*` | 1,082 | 6% changed, 28 names. |
| 8 | 38 UI overlay and UI widget editing → `editor/SceneUIEdit.*` | 1,450 | 2% changed. |
| 9 | 34 Actions editors → `editor/ActionsEditor.*` | 2,205 | 7% changed, many callers in the Inspector (42 names). |
| 10 | 14 Panel tabs and layouts, 17 small panels → `editor/PanelLayout.*` | 1,354 | 7% changed. |
| 11 | 41 Viewport and mobile emulator, 29 Projects and New Project | 1,741 | Low churn, but many names (61 and 41). |
| 12 | 1 self-test (441-897) → `editor/EditorSelfTest.cpp` | ~460 | After #1120 merges (it adds an include there). |
| 13 | 20 Character model swap and auto-rig, 2 helpers | 1,399 | Many callers; do after the panels that call them have moved, so their header is the only door. |
| 14 | 27 Script Editor, 28 file and build dialogs | 4,024 | After #1128. `DrawFileDialogs` needs 95 names: move the build dialog part with Build Game's header. |
| 15 | 23 Project panel, 30 Hierarchy, 35 Modeling and UV, 10 mesh-edit tools, 16 menus | ~7,500 | Edited often (31% to 64%): only in a quiet week, each alone, claimed on #422 first. |
| 16 | 39 + 40 Scene views, 42 Game view and Profiler | ~4,900 | `DrawScene3D` is one 3,190-line function; move whole first, split later. After #1120 (Profiler). |
| 17 | 36 Inspector parts drawn by OkayUI | 5,521 | Written right now (100% of lines changed this week). Move when that work stops, by the person doing it. |
| 18 | 37 `DrawInspector` | 12,797 | One function: a pure move only gives a 12,800-line file. First turn each component's block into its own function (a refactor, its own PR with its own tests), then move groups. After #1059. |
| 19 | 45 `main()` and the window loop | 2,285 | Every open PR touches it. Last. |

## 3. How each move is done

1. **Claim it** on issue #422 ("moving section N, lines a-b, to X.cpp; please do not edit it until merged"), from a
   fresh `origin/main`, on a branch `claude2/editor-split-N`.
2. **Re-measure** the section on today's `main` and check the open PRs again (the commands in section 1).
3. **New files.** `editor/<Feature>.hpp` declares what the rest of `main.cpp` calls (and holds, moved unchanged, the
   structs and inline helpers those callers need). `editor/<Feature>.cpp` starts with `#include "EditorCommon.hpp"`
   (what every editor file includes, with the same `using namespace okay; using namespace okay::editor;` as
   `main.cpp`), its own header, `MainShared.hpp`, and the headers it needs; then `namespace oked {`, the moved code,
   `} // namespace oked`. Add the `.cpp` to the `okay-editor` source list in `editor/CMakeLists.txt` with a comment
   (the `okay-editor-okayui` target copies that list, so it gets the file too).
4. **Shared state: reuse, never add a second.** There is no editor context struct: the shared state is
   `okay::editor::EditorState` (`editor/EditorState.hpp`, passed as `ed`) plus file globals in namespace `oked`. What a
   moved file still needs from `main.cpp` is declared in `editor/MainShared.hpp` (that is its job: "functions and
   state still defined in main.cpp that the split-out panels use"). Each declaration moves to the owner's own header
   when that owner moves out (move 2 does this for the updater). Do not add an `EditorContext` struct; do not copy a
   helper into the new file.
5. **The code moves verbatim.** Only what is needed to compile changes: includes; `static` becomes plain (external
   linkage) for a function or global that both files use, with an `extern` / prototype in a header; a default
   argument moves from the definition to the header declaration; namespace lines. No renames, no reformatting, no
   "while I am here" fixes. A duplicate found during the move is written down in section 5, not fixed.
6. **Write the rules file** `docs/editor/main_split/NN_<feature>.rules` that lists every allowed edit (see 4.4).
7. **Run the checks** (section 4), push, and ask the main session to open the PR. Merge it fast; then merge `main` into
   every open branch that touches `main.cpp` (CLAUDE.md rule 8).

Things that look harmless and are not:
- A `static` helper that exists in `main.cpp` and also in a header the new file includes: the call can silently
  bind to the other one. Before the move, list every name the section uses that `main.cpp` defines (the compiler's
  "not declared" errors give most; grep the rest) and make sure each resolves to the same function.
- `#define`s made in `main.cpp` before the section (for example `OKAY_BREADCRUMB`, the `Button` / `Checkbox`
  highlight macros of the Modeling window) do not exist in the new file. Grep the section for every macro name
  `main.cpp` defines above it.
- `inline` functions used on both sides must go into the header whole (as `HumanBytes` did in move 1).
- Code under `#if defined(_WIN32)` or `OKAY_XBOX_UWP` is not compiled on Linux: say so as unverified, and let the
  Windows cross-compile job check the `_WIN32` side.

## 4. The checks for every move

1. **The editor builds**: configure with `-DOKAY_BUILD_EDITOR=ON`, compile `main.cpp` alone (`ninja -j1`, `-O0 -g0`
   if memory is short), then the new file, then link `OkayEngine`.
2. **`Build & self-test editor` passes**: `./build/bin/OkayEngine --selftest`, as the CI job does.
3. **The editor tests that link the moved files pass** (`grep -n "editor/<File>" tests/CMakeLists.txt`). If none link
   them, run the tests that exercise the feature instead (move 1: `tests/export/verify.sh` and `verify_layout.sh`).
4. **`git diff` is a pure move**, checked by `scripts/check_pure_move.py` (standard library only; its test is
   `tests/scripts/test_check_pure_move.py`):
   ```
   python3 scripts/check_pure_move.py --base origin/main --source editor/main.cpp \
       --dest editor/<Feature>.hpp --dest editor/<Feature>.cpp --dest editor/MainShared.hpp \
       --rules docs/editor/main_split/NN_<feature>.rules
   ```
   It matches the lines removed from `main.cpp` against the lines added to the destination files in order (so a
   reordered line fails), and fails on anything left over unless it is scaffolding (blank lines, `//` comments,
   `#pragma once`, `#include`, namespace lines) or listed in the rules file (`change: old ==> new`, `add: regex`,
   `drop: regex`). A change rule that matches nothing also fails, so the rules stay exact.
5. **No conflict markers**: `git grep --cached -n -E '^(<<<<<<<|=======|>>>>>>>)( |$)' -- .`
6. **Docs**: this page (mark the move done, fix the line numbers of later sections if you re-measured), the feature
   map rows that named `main.cpp` for the moved code, then `python3 scripts/gen_feature_map.py` and `--check`.

## 5. Duplicates found so far (fix after the moves, one at a time, with tests)

- `builder::HumanBytes` (now `editor/BuildGame.hpp`) and `okay::buildpipe::HumanBytes` both format a byte count;
  `BuildGame.cpp` uses both.
- `Lower(std::string)` is copied in `AnimatorWindow.cpp`, `BehaviorTreeWindow.cpp`, `ContentEditors.cpp`,
  `DialogueWindow.cpp`, `InspectorModel.cpp`, `LocalizationWindow.cpp`, `ProjectBrowser.cpp`, `ProjectUpgrade.cpp`,
  `WeaponEditor.cpp` and `main.cpp`.
- `extide::WriteFile` (`main.cpp`), `WriteFile` in `ContentEditors.cpp` and `builder::WriteFileExact` write a text
  file three ways.

## 6. How other branches survive

Each move is small and merged fast, it is claimed on #422 before it starts, and `main` is merged into the open
branches right after, so a branch that edited moved code gets one clear conflict (the code is now in the new file)
instead of a slow drift.

## 7. Done

| Move | Branch | What | Result |
|---|---|---|---|
| 1 | `claude2/editor-split-1` | `namespace builder` (main.cpp 4469-5613, 1,145 lines) → `editor/BuildGame.hpp` (types, `HumanBytes`, declarations) and `editor/BuildGame.cpp`; 7 declarations added to `MainShared.hpp` (updater, `g_project`, the Xbox export result). Rules: `docs/editor/main_split/01_build_game.rules`. | `check_pure_move.py`: PURE MOVE (1,146 lines removed, 1,144 back unchanged, 2 default-argument edits). Build and test results are in the PR. |
