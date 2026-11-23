# Feature map pages

`docs/FEATURE_MAP.md` (and `docs/SYSTEMS.md`, `_index_az.md`, `_index_symptoms.md`) are generated from the pages in
this folder by `scripts/gen_feature_map.py`. Edit a page and check it with `python3 scripts/gen_feature_map.py --validate`
(precheck and branch-lint run the same). Do not commit the generated files: `.github/workflows/regen-docs.yml` rebuilds
them on main after every merge, so two PRs never conflict in them. To see the result locally, run the script without
options, then put main's copies back with `bash scripts/precheck.sh --restore-generated`.
`scripts/check_feature_map.py --list` lists source files that no page mentions.

## Page layout
```
# <Area number>. <Area name>

<2-4 lines: what the area is, which parts of the repo it covers>

## <Area number>. <Area name>

### 5.6 Foot, limb and chain IK
- **System:** Inverse Kinematics - one plain line for a non-programmer (or just the name, when the system is described in another sub-area)
- **What:** one or two plain sentences.
- **Parts:**
  - 5.6.1 Two-bone foot IK - `Animation/FootIK` - what it does in one line
- **Code:** `Animation/FootIK`, `editor/AnimatorWindow.*`
- **API:** classes, components, script builtins, menu items an author would type
- **Editor:** window, menu or inspector, or "none"
- **Docs:** `docs/gameplay/animation.md`, or "-"
- **Tests:** ctest names (the part after `test_`)
- **Status:** Works | Partial | Basic | Planned | In flight - then one line why
- **Open:** semicolon-separated: missing, known bugs, in-flight PRs
- **Related:** sub-area IDs, e.g. 5.7, 6.6
- **Words:** comma-separated terms people use (class names, menu names, synonyms)
- **Seen as:** semicolon-separated things you see when it is wrong or when you want it
- **Ask me like:** "example request"
```
One page per area, named `NN-slug.md`. A page may hold several areas (`20-35-planned.md`). Files whose
name starts with `_` (`_header.md`, `_footer.md`) are the hand-written text around the generated
tables. Paths in `Code:` are relative to `engine/include/okay/` without extension for engine headers
(`Animation/FootIK` covers the .hpp and the matching .cpp) and repo paths for everything else.
Required lines: What, Code, Status, Words. Part IDs are `N.M.K` and unique inside their sub-area.

## Systems (`docs/SYSTEMS.md`, the system registry)
Every sub-area belongs to a **system**: a named part of OkaySpace that does one job from start to end
(Physics, Save Games, OkayScript, the Launcher Updater). The `System:` lines are **the one registry** of
systems: `docs/SYSTEMS.md` is generated from them, `scripts/find_existing.py` searches them first and
`scripts/dup_lint.py` checks new folders against them. Exactly one sub-area of each system describes it
(`Name - what it does`) and gives its registry fields on the next line; the others give only the name:
```
- **System:** Physics - Moves solid 2D and 3D objects under gravity, makes them collide and join.
  Home: `engine/include/okay/Physics/`, `engine/src/Physics/`. Entry: `engine/include/okay/Physics/Physics3D.hpp`. Also called: Classic physics, Improved physics, own core.
```
- **Home:** the folder(s) or files that hold the system's own code (full repo paths; a glob such as
  `engine/include/okay/Components/Vehicle*.hpp` is fine; add `(planned)` after a path that does not exist yet).
  New code for the system goes here. An editor window (`editor/`) or a component (`engine/include/okay/Components/`)
  for a subsystem is not a second home. A Home with two folders lands in the "Still duplicated" table.
- **Entry:** the one file to open first (main header, program, script). Required, like Home.
- **Doc:** the one guide. Optional: the default is the first existing `.md` in the sub-area's Docs line.
- **Also called:** old and other names (earlier names, class names, frozen scene keys, env vars and CMake
  options, the third-party part it replaces), comma-separated. A search for any of them lands on the system,
  and the A-Z index lists them. One name belongs to one system: the script warns when two systems share one.
- **Fix:** for a system in the "Still duplicated" table, what will make it one (optional; else "pick one home").
- The owner **lane** is not written: it comes from `docs/lanes.json` for the Entry file.

A sub-area that holds several systems lists them, each with its own files and its fields on the next line:
```
- **System:**
  - Save Games - Saves a player's progress in named slots. Code: `Core/SaveFramework`, `Core/Prefs`
    Home: `engine/include/okay/Core/`. Entry: `engine/include/okay/Core/SaveFramework.hpp`.
  - Whole-Scene Save - The older save slots - being merged into Save Games (PR #1132) Code: `Core/SaveGame`
    Home: `engine/include/okay/Core/SaveGame.hpp`. Entry: `engine/include/okay/Core/SaveGame.hpp`. Fix: delete it after the merge.
```
A system that does the same job as another ends its line with `- being merged into <System> (PR #N)`
or `- duplicate of <System> (why both stay)`; these rows go into the "Still duplicated" table.
The script writes `docs/SYSTEMS.md` (one table per area: name, what it does, home, entry, doc, lane, also
called, status, sub-areas) and warns about a sub-area with no system, a system described twice or never, a
system with no Home or Entry, a Home, Entry or Doc path that does not exist, a name shared by two systems,
a system name with Own/New/Improved/Classic/V2 in it, and a duplicate that names no system.
**Naming:** one plain name per system, and "Okay" only where the code does (OkayUI, OkayScript); no
"Own", "New", "Improved", "Classic" or "V2" (CLAUDE.md rule 19e, `docs/engine/architecture.md#naming`).
