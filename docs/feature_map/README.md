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

## Systems (`docs/SYSTEMS.md`)
Every sub-area belongs to a **system**: a named part of OkaySpace that does one job from start to end
(Physics, Save Games, OkayScript, the Launcher Updater). The `System:` line names it. Exactly one sub-area
of each system describes it (`Name - what it does`); the others give only the name. A sub-area that
holds several systems lists them, each with its own files:
```
- **System:**
  - Save Games - Saves a player's progress in named slots. Code: `Core/SaveFramework`, `Core/Prefs`
  - Whole-Scene Save - The older save slots - being merged into Save Games (PR #1132) Code: `Core/SaveGame`
```
A system that does the same job as another ends its line with `- being merged into <System> (PR #N)`
or `- duplicate of <System> (why both stay)`; these rows go into the "Duplicates found" table.
The script writes `docs/SYSTEMS.md` (one table per area: name, what it does, status, main code,
sub-areas) and warns about a sub-area with no system, a system described twice or never, and a
duplicate that names no system. Use plain words for names, and "Okay" only where the code does
(OkayUI, OkayScript).
