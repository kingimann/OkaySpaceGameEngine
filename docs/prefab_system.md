# Prefab system: nested prefabs, variants, unpack, apply and revert

This page covers `PrefabTools` (`okay/Scene/PrefabTools.hpp`), which builds on the
prefab instances and overrides described in [prefabs.md](prefabs.md). Everything here is
data and API. The editor calls it through `editor/PrefabEdit.*` (see "In the editor" at the end of this page).

```cpp
#include <Okay.hpp>
using namespace okay;

GameObject* car = SceneSerializer::InstantiateFromFile(scene, "prefabs/car.okayprefab");  // routes nested / variant files
PrefabDiff d;   PrefabTools::Diff(*car, d);                                   // what differs, per record and field
GameObject* c2 = PrefabTools::RevertOverride(scene, *car, 0, "light", "intensity");
PrefabTools::ApplyToSource(*c2, 0, "transform", "position.x");                // rewrite the prefab file
PrefabTools::Unpack(*c2);                                                      // cut the link, keep the objects
```

`SceneSerializer::InstantiateFromFile`, `InstantiatePrefab`, `CapturePrefabOverrides` and
`ReapplyPrefab` hand a nested or variant file to `PrefabTools` on their own, so existing
callers need no change. Plain prefab files take the old path unchanged.

## Object numbering

An override names an object by its **depth-first index in the fully expanded prefab**: 0 is
the root, then each object followed by all its children. For a nested prefab, "expanded" means
the nested objects are in place, so a car holding two wheels (each Wheel + Hub) is
`0 Car, 1 Wheel L, 2 Hub, 3 Wheel R, 4 Hub`. Three places hold overrides and each counts in
its own space:

| Where | Index means |
|---|---|
| `GameObject::prefabOverrides` of an instance root | the expanded prefab of the file it was instantiated from |
| `override` records inside a `nestedprefab` object | the nested prefab's own expanded objects (not the outer file's) |
| `override` / `remove` / `unset` / `under` in a variant file | the expanded **base** prefab, before the variant's own removals and additions |

A prefab file whose objects are not in depth-first order is put in that order when it is
expanded. A second top-level object is moved under the first, with a warning.

## Nested prefabs

A prefab file may hold instances of other prefab files. In the file such an instance is one
object with a `nestedprefab` record:

```
okayscene 3
gameobject 0 "Car"
  transform 0 0 0 0 0 0 1 1 1 1
  parent -1
end
gameobject 1 "Wheel L"
  nestedprefab "prefabs/wheel.okayprefab"
  override 0 "transform -1 0 0 0 0 0 1 1 1 1"
  override 1 "tag \"rim\""
  remove 3
  unset 0 "sprite" 0
  parent 0
end
gameobject 2 "Spoke"
  under 1
  transform 0 0.5 0 0 0 0 1 1 1 1
  parent 1
end
```

- `nestedprefab "<file>"`: the object stands for the whole nested prefab. Its name is the
  nested root's name; the nested prefab's own objects are not copied into this file.
- `override <i> "<record>"`: replace (or add) a record of nested object `i`, as in a scene.
  Any other record written on the object (a `transform`, an `active`) is an override of the
  nested root.
- `remove <i>`: leave out nested object `i` and everything under it (not the root).
- `unset <i> "<record>" [n]`: delete the n-th record of that name from nested object `i`
  (for example a component the nested prefab has and this one does not want).
- An object listed with `parent` pointing at the `nestedprefab` object is added **inside**
  the nested prefab: under its root, or under nested object `i` with `under <i>`.
- A path that does not exist as written is tried again next to the file that names it.

Instantiating the outer prefab instantiates every nested one and applies these records to it.
The nested root gets its own link: `prefabSource` is the nested file and `prefabOverrides` is
how it differs from that file (the outer file's overrides included), so the scene text
(`prefab` + `override` records on the nested root) round trips and a nested instance can be
re-applied, applied or reverted by itself. The outer root's overrides list only what differs
from the outer file's expansion.

`PrefabTools::SavePrefab(root, path)` writes a prefab from live objects the same way:
nested instances become `nestedprefab` objects with their overrides, removals and added
objects. It refuses, and writes nothing, if the result would not load (for example a cycle).

### Errors and limits

| Case | Result |
|---|---|
| A holds B holds A (or A holds A) | error `prefab cycle: a.okayprefab -> b.okayprefab -> a.okayprefab`; nothing is created |
| more than `PrefabTools::kMaxDepth` (16) levels of nesting plus variant chain | error `prefab nesting is deeper than 16 levels: ...` |
| a prefab that expands to more than 200000 objects | error; stops a runaway nest before it eats memory |
| a nested file is missing or damaged | warning, only that nested instance (and anything listed under it) is skipped; the rest is built |
| the top-level file is missing or damaged | error; the scene is exactly as it was |
| `ReapplyPrefab` / `PrefabTools::Reapply` with a nested file that has gone | error; the instance is left alone (it would otherwise lose that part) |

A damaged file is one with no `okayscene` header, a bad version, a duplicate object index, an
object without its closing `end`, a record cut short, or a parent loop. All of these are found
before any object is made, and object loading itself is all-or-nothing.

## Variants

A variant is a prefab file that says which prefab it is a variant of and lists only what it
changes. Instantiating it instantiates the base, then applies the changes. A variant of a
variant works (a chain, cycle checked, counted against the same depth limit).

```
okayscene 3
variant "prefabs/robot.okayprefab"
override 0 "sprite 82 1 0 0 1 1 1"
override 2 "tag \"head\""
unset 2 "boxcollider2d" 0
remove 1
gameobject 0 "Antenna"
  under 2
  transform 0 1 0 0 0 0 1 1 1 1
  parent -1
end
```

- `variant "<base>"`: the base prefab (plain, nested or itself a variant).
- `override`, `unset`, `remove`: as for nested prefabs, with indices in the expanded base.
  All indices mean the base as it was before this file's own removals and additions.
  The root cannot be removed.
- `gameobject` blocks are the variant's **added objects**. A top-level one (`parent -1`) is
  attached as the last child of base object `under <i>` (the root when there is no `under`).
  Others use `parent` between themselves as usual. An added object may itself be a
  `nestedprefab`.
- Records for objects the base no longer has (an `override 9` when the base shrank), and
  added objects whose attach point is gone, are dropped with a warning; the instance is still
  built. The validator lists them.

**Compatibility.** The text is ordinary scene text plus records an older loader does not know.
An older build skips `variant`, `override`, `unset` and `remove` at the top level and `under`
and `nestedprefab` inside an object, each with a warning (that is the format's existing rule
for unknown records), and loads whatever objects the file lists itself, so a variant never
breaks the load of a scene, it only looks like just its added objects. Nothing writes
these records into scene files, only into prefab files.

`PrefabTools::SaveVariant(instance, path)` writes a variant from an edited instance: the
records that differ become `override` (changed or added records) and `unset` (lost ones),
lost objects become `remove`, objects added under the instance become `gameobject` blocks
with `under`. The base is the instance's source prefab, so saving an instance of a variant
makes a variant of that variant. It never writes over the base.

## Unpack

```cpp
int n = PrefabTools::Unpack(*go, /*completely=*/false);
```

Removes `prefabSource` and `prefabOverrides` from the **outermost** instance that contains
`go` (`go` can be any object inside it). With `completely` it also clears every nested
instance. Objects and their ids are not touched. Without `completely` the nested instances
stay linked, and first take over what the outer instance changed inside them (their
overrides are re-captured), so a later re-apply of one nested instance does not lose it.
Returns how many links were removed; 0 when `go` is not part of an instance.

## Differences, revert and apply

```cpp
PrefabDiff d;
PrefabTools::Diff(*instance, d, &err);
for (const auto& r : d.records) puts(r.Describe().c_str());   // "Transform position.x: 0 -> 4"
```

`Diff` compares the instance (every object, nested instances included) with its source file
after loading that file the way the serializer writes it, so a hand formatted file does not
show false differences. It reports:

- **records** (`PrefabRecordDiff`): `Changed`, `Added` (a component or record only the
  instance has) or `Removed` (only the prefab has it). A changed record lists its **fields**:
  well known records use field names (`transform`: `position.x`...`scale.z`; `light`:
  `color.r`, `intensity`, `range`...; `sprite`, `camera`, `rigidbody2d`, colliders), a
  `key=value` record uses its keys, anything else `arg<N>`. When the two lines have a
  different number of fields (an optional trailing block), only the whole record can be used
  (`wholeRecordOnly`).
- **objects** (`PrefabObjectDiff`): objects the instance gained (with their path and parent)
  or lost. Objects are matched by position and name, so a child renamed in place still
  matches. An empty object deleted and a different empty object added at the same place read as
  a rename.

```cpp
GameObject* fresh = PrefabTools::RevertOverride(scene, *instance, object, "light", "intensity");
bool ok = PrefabTools::ApplyToSource(*instance, object, "light", "intensity");
```

- `RevertOverride(scene, instance, object, record, field = "", occurrence = 0)` makes that
  record (or just that field) equal to the prefab again. The instance is rebuilt and the new
  root returned; the old root is queued for destruction like any `Destroy`, so do not keep
  its pointer. Everything else the instance has stays: its other differences (including lost
  components and objects), the objects added under it, names of children, name, parent,
  sibling place and every id. `RevertAll` rebuilds exactly as the prefab says.
- `ApplyToSource(instance, object, record, field = "", occurrence = 0)` writes that record
  or field into the prefab file. With a field, only that field of the prefab's record changes.
  The rewrite keeps every other record of the file as it was (line endings become LF and blank lines are dropped), checks that the result still loads
  as a prefab, and then replaces the file atomically (`savefmt::AtomicWrite`: write
  `path.tmp`, rename). After the write the instance's stored overrides, and those of the
  instances inside and around it, are re-captured. A packed (obfuscated) prefab is read-only
  here; a damaged, missing or invalid result is an error and leaves the file as it was.
  `ApplyAllToSource` does this for every differing record.
- **Where the change goes.** For a plain prefab, into the object's own record (an added
  record is appended, a lost one is deleted). For an object that comes from a variant's base,
  into an `override` (or `unset`) of the variant, and the base file is not touched. For an
  object inside a nested prefab, into an `override` of the `nestedprefab` object in the outer
  file; to change the nested file itself, apply through the nested instance's root.
- **Not applied.** Objects added to or lost from the instance are reported by `Diff` but
  `ApplyToSource` only handles records. Save a new prefab or variant to keep a structure
  change (`SavePrefab`, `SaveVariant`).
- Other instances of the same file are not touched by an apply. They differ from the new
  file until they are re-applied (`ReapplyPrefab`).

## Object ids (GUIDs)

The rule:

1. Every instantiation gives every object a **new** id. Two instances of one prefab never
   share an id, and an id in the prefab file (there should be none) is ignored.
2. Overrides hold object **indices**, never ids. The same override list works for any number of
   instances, and survives a re-apply.
3. Re-apply, revert and apply keep each object's id: the new object takes the id of the old
   object it matches (by position and name). An object that is new to the prefab gets a fresh
   id; an object the prefab dropped loses its id. The root keeps its id even if it was
   renamed.
4. Unpack, save and load never change ids.

## Queries

| Call | Answer |
|---|---|
| `IsPrefabInstance(go)` | `go` is an instance root or sits under one |
| `PrefabRoot(go)` | the outermost instance root at or above `go` |
| `NearestPrefabRoot(go)` | the innermost one |
| `NestedInstances(root, recursive)` | instance roots below `root` |
| `OverridesOf(root)` | the stored overrides |
| `FileUsesPrefabStructure(path)` / `UsesPrefabStructure(text)` | the file is nested or a variant |

## Validator

```cpp
PrefabReport r = PrefabTools::ValidateAsset("prefabs/car.okayprefab");
if (!r.Ok()) puts(r.ToString().c_str());
```

Checks the file and every file it reaches (nested prefabs, variant bases): files that cannot be
read or are cut short or malformed, missing nested prefabs and variant bases (errors), cycles
and nesting too deep (errors), a trial load of each file (error if the objects do not load),
and overrides, removals, unsets and attach points that name an object the prefab no longer
has (warnings, the file still loads). Messages name the file and the object, for example
`car.okayprefab: override for object 7 dropped: the nested prefab 'wheel.okayprefab' no longer has it`.
`ValidateInstance(root)` checks one live instance's stored overrides the same way.

## Limits

- An override is a whole record. A field-level revert or apply works by writing the whole
  record with that one field taken from the other side.
- An override cannot add a second record of a name the prefab already has once (a second
  collider of one type): it replaces the first, as in prefabs.md.
- Names of child objects are not overrides: a re-apply gives children the prefab's names (the
  root keeps its name), a revert or apply keeps the names they have.
- Instance and object tracking is by position and name; a prefab that reorders or renames
  many objects at once can pair the wrong ones. Run the validator and re-capture after big
  prefab edits.
- `PrefabTools::Reapply` (used for nested and variant files) keeps the objects added under
  the instance and brings back lost ones; the wave 1 `ReapplyPrefab` path, which plain files
  still take, drops added objects (see prefabs.md). Revert and apply keep both added and lost
  objects as they are.

## In the editor

The Inspector, the Hierarchy and the Project panel use `PrefabTools` through `editor/PrefabEdit.*`
(no drawing, tested by `prefab_editor`) and `editor/InspectorPanel.*` (the drawing). The editor has no
prefab code of its own: reading and writing prefab files, overrides, nesting and variants are all
`PrefabTools`; `PrefabEdit` adds undo, the update of other instances and the names the Inspector shows.

**What you see**
- Hierarchy: an instance root has a blue mark on its icon, an instance of a variant a violet one, a prefab nested
  inside a bigger one a square with an inner square, and an orange dot when the instance differs from its prefab
  (hover the mark for the kind and the number of changes). The row menu has a **Prefab** submenu: Open, Select
  Asset, Revert All, Apply All, Create Variant..., Update All Instances, Unpack, Unpack Completely.
- Inspector header **Prefab: <name>** (with "Prefab Variant of <base>" or "nested in <outer>" beside it) and the
  buttons **Open**, **Select Asset**, **Revert All**, **Apply All**, **Create Variant**, **Update All**, **Unpack**.
  An object inside an instance shows the same header with **Select Instance Root**.
- A property that differs from the prefab has a blue bar at the left of its row. Right-click it:
  **Revert Property** or **Apply Property to Prefab** (a vector or colour acts on all its parts that differ).
- A component that differs has a blue header bar and **Revert Component to Prefab** / **Apply Component to
  Prefab** in its `...` menu. A component added to the instance reads **Revert Added Component** (it is removed).
- **Changed from the prefab** lists, for the selected object, each component with Revert / Apply, each field with
  its old and new value and its own Revert / Apply, components added or lost, and (on the root) child objects added
  or lost with Revert / Apply.
- Project panel: right-click a `.okayprefab` > **Create Variant** makes `<name> Variant.okayprefab` next to it.

**What the actions do**
- *Revert* uses `RevertOverride` / `RevertAll`; the instance is rebuilt with its name, place and ids kept. An added
  child is deleted; a lost child is brought back from the prefab.
- *Apply* uses `ApplyToSource` / `ApplyAllToSource` and writes the prefab file (for a variant instance the variant
  file, for an instance inside another prefab the inner file; the base is never touched). For a plain prefab,
  *Apply* of an added or lost child object rewrites the prefab with `SavePrefab`; for variants and nested
  instances the button says why it is off (keep the structure with **Create Variant**).
- Apply then updates **the other instances in the scene**: each one's own overrides are captured against the old
  file first, then it is rebuilt from the new file with `Reapply`. An instance that changed the same field keeps its
  value; the others take the new one. Instances of prefabs that use the applied file (variants, outer prefabs) are
  updated the same way.
- **Create Variant** (Inspector) saves what the instance changed with `SaveVariant` and links the instance to the
  new variant. **Create Variant** (Project panel) writes a variant file with no differences yet.
- **Update All** re-applies every instance of the prefab from the file, keeping what each changed (use it after
  the file was edited outside the editor). **Unpack** / **Unpack Completely** use `Unpack`.
- **Open** shows the instance on its own in the Scene view (Isolate; Esc leaves) and selects it; **Apply All**
  writes the edits back. A separate prefab-editing scene with a breadcrumb is not built (feature map 21.1).

**Undo.** Every action is one step, on the editor's `UndoStack`. `EditorState::PushUndo(name)` opens the step (named
"Apply to Enemy", "Revert Prefab", "Create Prefab Variant" ...); the scene part is recorded at the next boundary as a
`SceneDeltaCommand`. The prefab files an action wrote join that same step through `EditorState::AttachUndoFiles`, which
adds a file command (`EditorState::MakeFileUndo`, the same thing as `PushUndo(name, std::unique_ptr<ICommand> alsoUndo)`)
holding the text before and after, or "did not exist". One Ctrl+Z puts the old file text back together with the old
scene and Ctrl+Y writes it again; a step that changes only a file (a new variant file) still exists. `DiscardLastUndo`
drops the open step when the action failed and changed nothing.

**Limits in the editor**
- Apply updates the other instances, but not a second instance of the applied file that sits inside the same
  outer prefab instance.
- Objects added or lost can be applied only to plain prefab files (see above).
- The Hierarchy mark and the "N changes" figure are refreshed about every 1.5 seconds.
