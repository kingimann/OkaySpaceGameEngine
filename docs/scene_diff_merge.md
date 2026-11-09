# Scene diff, merge, canonical form and migration

Engine-side libraries that let version control work with scene (`.okayscene`) and
prefab (`.okayprefab`) files. There is no editor window and no command-line tool yet;
these are the pieces a Git panel, a merge helper or a `git` merge driver would call
(feature map areas 1.5 and 28).

All four work on the **text** of a file (`okayscene N`, one `gameobject` ... `end`
block per object). None of them builds a `Scene`, so they are fast (a 5000 object scene
takes about 100 ms to diff and about 250 ms to merge) and they cope with files this build
only half understands. Objects are paired by their persistent id (`guid`, see
prefabs.md); a file without ids falls back to the name path (`Level/Arena/Enemy`).

| Header | What it gives you |
|---|---|
| `okay/Scene/SceneText.hpp` | the tolerant parser, `SceneText::Canonicalize`, the object model both other tools use |
| `okay/Scene/SceneDiff.hpp` | `SceneDiff::Compute`, `Apply`, readable and JSON output |
| `okay/Scene/SceneMerge.hpp` | `SceneMerge::Merge` (three-way), policies, conflict report |
| `okay/Scene/SceneMigration.hpp` | `SceneMigration::MigrateToCurrent`, the registry of `N -> N+1` steps |

## Canonical form

```cpp
std::string tidy = SceneText::Canonicalize(text);
```

Equal scenes get equal text, so a commit that only re-saved a file shows no diff.
Canonicalize

- uses LF line endings, drops trailing blanks and blank lines, puts single spaces
  between tokens and ends the file with one newline;
- tidies numbers in the records the engine writes numbers into (`2.0` -> `2`,
  `0.50` -> `0.5`, `-0.0` -> `-0`): only trailing zeros go, the value is never changed;
  `1e5`, `.5`, `+1` and `007` stay as written;
- puts an object's bookkeeping records in the writer's order (`guid`, `active`, `tag`,
  `static`, `locked`, `layer`, `uiorder`, `srcscene`, `prefab`, `override`, `parent`,
  `sibling`) and the file-level records likewise (`name`, `uifont`, `gravity`, ...).
  Component records keep their order: it decides the order components are added in;
- numbers the objects 0..n-1 in file order and rewrites `parent` to match. A `parent`
  that names no object becomes `-1` (the loader treats both as "no parent").

It never changes what the scene means: the canonical text loads as the scene the input
loads as (the tests load both with the real loader and compare the re-serialization). It
is idempotent, and the engine's own output is already canonical. What it does not
understand it leaves alone:

- text that does not parse (no header, an unterminated string, an object without `end`,
  a duplicate object number) and a version newer than this build are returned **unchanged**
  (`CanonicalReport::skipped`, with a `note`);
- a record this build does not know is kept byte for byte (only its indentation and
  trailing blanks are normalised), and an object or file that holds one is not reordered;
- **quoted strings are never touched**, including line breaks inside them (a script's
  source can span lines, and a record is a "logical line" that ends only outside a string).

`CanonicalOptions` switches off number tidying, renumbering or header sorting.

## Diff

```cpp
SceneDiff::Result d = SceneDiff::Compute(baseText, otherText);
if (d.ok && !d.Empty()) puts(d.ToText().c_str());      // or d.ToJson()
```

Both inputs are canonicalized first, so layout noise is not a difference. The result lists,
in a fixed order (objects of the other file in file order, then the removed ones):

- **added**, **removed** and **modified** objects, with their guid and name path;
- per object: **renamed**, **reparented** (`Level/Arena -> Level/Player`), **reordered**
  among its siblings (`2 -> 0`), moved in the file's object order, got a new id;
- per record: added, removed or modified, and per field of a modified record
  (`Light.intensity 1 -> 2`, `Transform.px 1 -> 7`, `linerenderer.width 1.5 -> 2`);
- changes to the file-level records (`gravity.y -9.81 -> -20`) and the format version.

Positional records have field names where this file knows them (transform, light, sprite,
camera, rigidbody, colliders, audio, text, ...) and `[3]` otherwise; `key=value` records
use their keys. Several records of one kind (two scripts) are paired by identical text first,
so removing the first one is one removal, not two edits. `ToText(maxValueChars)` shortens long
values; `ToJson()` carries them in full.

```
scene diff: 1 added, 1 removed, 2 modified (1 renamed, 1 reparented)
scene settings:
  gravity.y -9.81 -> -20
+ Level/Arena/Coin [a5000000]
    records: Transform
- Level/Arena/Enemy [a4000000]
~ Level/Hero [a2000000]
    renamed "Player" -> "Hero"
    Light.intensity 1 -> 2
    Tag "player" -> "hero"
```

Without ids, an object that kept its content and parent but got a new name is reported as
renamed (`Options::detectRenames`); the children of a renamed id-less object show as removed
and added. `SceneDiff::Apply(base, diff, &out)` replays a diff on the file it came from and
returns the canonical text of the other side (it refuses a diff that does not fit the base).

## Three-way merge

```cpp
SceneMerge::Options opt;                 // policy: Mark (default), PreferOurs, PreferTheirs
SceneMerge::Result m = SceneMerge::Merge(baseText, oursText, theirsText, opt);
save(m.text);                            // always loads when m.ok
if (!m.Clean()) show(m.ReportText());    // or m.ReportJson() for a tool
```

**Merges automatically:** changes to different objects; to different records of one object;
to different fields of one record (positional records with the same field count, `key=value`
records key by key); both sides making the same change; an object deleted on one side and
untouched on the other; an object added on one side; records added on both sides (a second
script is kept, the same record is one); sibling reorders and additions (the order is the
base's with each side's moves and insertions applied; where both insert at the same place the
result is the same whichever side is "ours").

**Conflicts** are listed in `Result::conflicts` (kind, object, record, field, base / ours /
theirs value, resolution):

| Kind | When |
|---|---|
| `field-changed` / `record-changed` | the same field (or whole record, if its shape differs) changed differently |
| `record-deleted-and-edited`, `object-deleted-and-edited` | deleted on one side, edited on the other |
| `record-added-differently` | both added a record of one kind that may only occur once, differently |
| `renamed-differently`, `parent-changed` | both renamed, or both moved it under different parents |
| `parent-deleted` | a side added or moved an object under one the other side deleted |
| `parent-cycle` | the two reparentings together would make a loop |
| `order-changed` | both moved an object to different places |
| `invalid` | the merged text failed its check and `ours` was returned |

`Policy::PreferOurs` and `PreferTheirs` settle every conflict for that side. `Policy::Mark`
settles every conflicted *item* with ours and expects you to show the report: all changes that
do not conflict are still merged, so a conflicted object keeps their non-conflicting edits, and
the report holds theirs' value of every conflicted item (and, for a deleted-and-edited object, a
snapshot of the edited one) so nothing is lost. A deleted parent drops an object the side that
wins did not add (`dropped`), or keeps it under the nearest parent that survived (`kept under ...`).

**The merged text always loads.** After merging, `SceneText::Validate` checks the structure,
that every guid is valid and unique (a duplicate loses its id; the loader gives a fresh one),
that every parent exists and that there is no loop. `Options::validator` can add a check of
your own, for instance loading the text into a scratch scene:

```cpp
opt.validator = [](const std::string& text, std::string* err) {
    Scene probe("probe");
    return SceneSerializer::Deserialize(probe, text, err);
};
```

If a check fails the merge falls back: `text` is `ours` (canonical), `fellBack` is set and an
`invalid` conflict says why. If an input does not parse (or has a newer `okayscene` version, or
ours and theirs have different versions) nothing is merged: `ok` is false, `error` says why
and `text` is `ours` exactly as given. An empty base means "both sides created the file".
Merge the sides at one format version (see Migration below).

## Migration

```cpp
SceneMigration::Result r = SceneMigration::MigrateToCurrent(oldText);
// r.ok, r.text (canonical), r.fromVersion, r.toVersion, r.log, r.needsEngine
```

A step upgrades `okayscene N` to `N+1` and is registered with
`SceneMigration::Register(step)` (at most one per `N`; `Steps()` and `Describe()` list them).
A step has an optional `apply(doc, log, error)` that edits the parsed document, an optional
`affects(doc)`, and a name and description. The framework raises the version number, collects
the log and writes the result in canonical layout. A file newer than the build, text that is not a
scene, a missing step or a step that fails give `ok == false` and the text **untouched**.

What the repository history shows (`git log -S"okayscene 2"` and `-S"okayscene 3"` on
SceneSerializer.cpp):

| Step | Change | Done by |
|---|---|---|
| 1 -> 2 | a Character body gets the visible Animator with the default humanoid graph (PR #346) | the loader, for every Character in a file older than 2 |
| 2 -> 3 | a switchable first / third person player gets both view controllers and a camera on it (PR #510) | the loader, for such a player in a file older than 3 |

Both upgrades need the engine's component classes, so they cannot be written as text. The two
built-in steps therefore only raise the version number when the scene has nothing for them to
do (no `character` record, no `playerview` record); otherwise `MigrateToCurrent` stops there
(`needsEngine`, text left at that version) and `SceneMigration::MigrateThroughEngine(text)`
lets the loader do the work: it loads the text into a scratch scene and writes it back at the
current version. That round trip drops records this build does not know and gives objects
without ids new ones, so use it on files from older builds, which have neither. Everything
else added to the format since (`guid`, `sibling`, `compoff`, `override`, `layermatrix`,
...) was added without a version bump and is optional, so older files load as they are.

To add a step when the format changes: bump `SceneSerializer::kSceneFormatVersion`, register a
`Step{from = old, ...}` whose `apply` rewrites the records, and add a hand-written old-format
fixture to `tests/test_scene_migration.cpp`.

## Tests

| Executable | Covers |
|---|---|
| `test_scene_canonical` | whitespace, strings, numbers, ordering, numbering, untouched input; random scenes messed up and canonicalized (the result loads as the same scene), truncation and byte-damage fuzzing |
| `test_scene_diff` | each kind of change, record and field names, JSON validity, determinism, diff-then-apply equals the other side on random edits, 5000 objects under a time bound, fuzzing |
| `test_scene_merge` | each auto-merge and each conflict, policies, `merge(base,x,x) == x`, `merge(base,base,x) == x`, `merge(base,x,base) == x` (also with the shortcuts off), random non-overlapping edits merge cleanly, completely and symmetrically, random overlapping edits always load, 5000 objects, fuzzing |
| `test_scene_migration` | hand-written version 1 and 2 files, a real version 1 file from `tests/export`, engine-applied upgrades, refusal of newer files, a registry exercised with SYNTHETIC steps registered only in the test, fuzzing |

Set `OKAY_TEST_SEEDS=n` to run the property tests over `n` random seeds instead of the default.

## Limits

- Objects are paired by guid, or by name path without ids. A renamed object in a file without
  ids is one removal and one addition for merge purposes unless its content is unchanged.
- Fields of a record merge individually only when all three versions have the same number of
  values (or are `key=value`); otherwise a record that both sides changed is one conflict.
- Component order inside an object is not merged as a conflict: the order is the base's with each
  side's insertions applied.
- Canonicalize only tidies numbers in records this file lists as all-numeric; it cannot know
  what a record from a newer build holds.
- Not included: the editor UI, a `git` merge driver executable and the Git glue itself.
