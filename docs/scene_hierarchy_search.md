# Scene hierarchy, search and validation

Feature map 1.3 - 1.5. Everything here is additive: old scene files load unchanged.

## Active state follows the hierarchy

`GameObject::active` is the object's own switch. `GameObject::ActiveInHierarchy()` is true
only when the object and every parent above it are active. The engine tests the second one:
Scene `Update` / `LateUpdate` / `Render`, collider auto-fit, 2D pointer picking, the GL, D3D11,
D3D12 and software renderers (colour and shadow passes) and Physics2D / Physics3D all skip a
child of an inactive parent. `OnDisable` / `OnEnable` follow the same rule, so a script on a
child hears about it when its parent is switched off or on.

`ActiveInHierarchy()` remembers its answer (see "What changed: scene graph, fourth pass"): the
answer is worked out again only after some `active` flag or parent link changed, so a frame over a
very deep chain costs a few reads per component. `go->active` is an `ActiveFlag`, which reads and
assigns like a `bool`.

## Native FixedUpdate

`Component::FixedUpdate(float fixedDeltaTime)` runs from the scene's physics step: once per
fixed step, before the physics world advances (a slow frame runs it several times), or once
per frame with the frame time when `fixedTimeStep` is 0. It runs for started, enabled
components that are active in the hierarchy, and only while `physicsEnabled` is true.

## Unload, load errors and the scene manager

* `Scene::Clear()` and `~Scene()` run `OnDisable` / `OnDestroy` for every component that was
  awakened. Components that never got their `Awake` have nothing to undo.
* `Clear()` also resets the 3D physics world and its gravity, and the scene UI font.
* The scene text has a `gravity3 x y z` record. A file without it loads the default gravity.
* A failed `RequestLoad` / `RequestLoadText` is printed to stderr and kept in
  `Scene::LastLoadError()`; the scene is left as it was. The `done(bool)` callback of
  `RequestLoad` still reports the result.
* `SceneManager::ActiveIndex()` moves only once a load succeeded. `LoadNextScene` and
  `ReloadScene` step from the last requested scene while a load is pending, so two calls in a
  frame step twice.

## Float precision

Scene text writes each float with the fewest digits that read back as the same value
(12345.678 stays 12345.678, 0.5 stays 0.5). Saving, undo and prefab clones no longer drift.
Streams that set their own precision, width or fixed / scientific flags are unchanged.

## Speed

* Mass `Destroy` + `FlushDestroyed` are linear in the number of objects and components, deep chains
  and very wide parents included (the objects leave their parents in one pass, top down).
* `ActiveInHierarchy()` is cached (it is asked for every component, every frame).
* `Serialize` finds parents through a map instead of a scan per object.
* `InstantiateFromFile` keeps the decoded text of up to 32 prefab files, checked against the
  file's size and time, so repeated spawns skip the file read. Changing the file reloads it.
* A component removed by another component's `Awake` / `Start` in the same flush batch is
  skipped instead of touched after it was freed.

## Search

| Call | Notes |
| --- | --- |
| `scene.FindGameObjectsWithTag(tag)` | every match, in scene order |
| `scene.FindByPath("A/B/C")` | a root named A, its child B, its child C. `Find("A/B/C")` also accepts a path. Duplicate names are all tried (see "fifth pass") |
| `scene.FindAllByPath("A/B")` | every object the path names, in scene and sibling order |
| `FindNearest(scene, "Axle", go)` | the object called "Axle" nearest to `go` in the hierarchy (its own subtree, then its parent's, ...), then the scene-wide `Find` |
| `go->ForEachDescendant(fn, includeInactive = true, includeSelf = false)` | pre-order walk, `fn(GameObject&)` may return `false` to stop |
| `scene.Roots()` | objects without a parent |
| `go->GetOrAddComponent<T>(args...)` | the first `T`, or a new one |
| `go->GetComponentInChildren<T>(includeInactive = false)` | the object itself first, then depth first |
| `go->GetComponentsInChildren<T>(includeInactive = false)` | every match, pre-order |
| `go->GetComponentInParent<T>(includeInactive = false)` | the object, then each parent |
| `go->GetComponentsInParent<T>(includeInactive = false)` | every match on the object and each parent, nearest first |
| `scene.DestroyChildren(go)` | queue every child of `go` (and below) for destruction; `go` stays |
| `scene.Stats()` | counts for an overlay: objects, roots, depth, active, components per type (see below) |

An inactive object is skipped together with its children unless `includeInactive` is true.

## Transform helpers

`LookAt(target, up)`, `SetLossyScale(worldScale)` (and `SetScale`, the same call),
`EulerAngles()` / `SetEulerAngles()`, `LocalEulerAngles()` / `SetLocalEulerAngles()`,
`GetSiblingIndex()` / `SetSiblingIndex(i)` (children use the parent's list, roots use the
scene's root order; the index is clamped), `SetPositionAndRotation(pos, rot)`,
`SetLocalPositionAndRotation(pos, rot)`, `SetParent(parent, keepWorld, siblingIndex)`.
The look-at family: `LookAt(target, up)`, `LookAt(const Transform*, up)`, `LookDirection(dir, up)`,
`LookAtYaw(target, up)` (turn about the up axis only), `LookAt2D(target, Facing2D::Right | Up)` and
`RotateTowards(target, maxDegrees, up)` (see "fifth pass").

## Validation

```cpp
SceneValidationReport r = scene.Validate("Assets");              // a live scene (its saved form)
SceneValidationReport f = SceneSerializer::ValidateFile(path, "Assets");
for (auto& i : r.issues) ...                                      // kind, object, message
std::cerr << r.ToString();
```

| Kind | Meaning |
| --- | --- |
| `MissingComponentType` | an object has a component record this build does not know |
| `UnknownField` | a top-level record this build does not know |
| `MissingAsset` | a texture, mesh, audio, prefab ... file that is not found (as written, then under `assetRoot`) |
| `BrokenParentLink` | `parent N` names an object that is not in the file |
| `DuplicateGuid` | two objects carry the same `guid` record |
| `LoadError` | the text could not be loaded at all |

Limits: unknown keys inside a known key=value record are skipped silently by the loader and
are not reported.

### Which validator? (core wave 6)

There are two, on purpose, and they now share the parts that overlapped:

| | `Scene::Validate`, `SceneSerializer::Validate(Text/File)` | `testing::SceneValidator::Validate(/Text/File)` (see `automated_testing.md`) |
| --- | --- | --- |
| Works on | the scene text (no scene needed; usable by a diff, merge or migration tool) | a loaded `Scene` (the Text and File forms load into a scratch scene) |
| Answers | is the file intact: unknown records and component types, broken parent links, duplicate guids, bad layers, unknown tags, missing assets, does it load | does the scene make sense: missing assets (per object and component), cameras, duplicate names, scripts that do not compile, bad references, NaN transforms, a Rigidbody without a Collider |
| Report | `SceneValidationReport` (kinds) | `ValidationReport` (codes, severities, JSON) |

For CI and "check before you ship" use `SceneValidator::ValidateFile`: its report also carries the two findings only the
text shows (`scene.duplicate_guid`, `scene.broken_parent`, switch `checkLinks`) next to the load warnings. Use the text
validator where there is no scene. What they share:

- "Does this file exist" is decided in one place, `SceneValidator::AssetExists`: a built-in `proc:` texture and a URL are not
  missing files (the text validator used to report `proc:checker` as "file not found"), and the working directory is tried
  as well as the asset root.
- The text validator now also walks the per-object list `SceneValidator::AssetReferences` (every component, fonts, voxel
  terrain), so a missing font is found and the message names the object (`"Label"`, `TextRenderer.fontPath`). The list
  `CollectAssetPaths` keeps is for builds (what must ship: the first component of each kind, a sequence's audio cues, the item
  database) and is still checked first.
- `ValidateSceneLinks(text)` is the one text pass behind both for duplicate guids and broken parent links.
- A damaged data pack is a `LoadError` in `SceneSerializer::ValidateFile` ("tampered or unreadable"), as it is in
  `SceneValidator::ValidateFile` (`scene.load`); it used to unpack to an empty text and report the misleading "missing 'okayscene' header".
- A parent cycle is a load error in both (the loader refuses the file).

## What changed: scene graph audit (1.3)

Tests: `deep_hierarchy`, `transform_graph`, `scene_graph_audit`, `scene_spawn`,
`mover_spinner_lifetime`, `layers_tags_project` (see `layers_tags_cameras.md` for the last one).
Everything is additive: scene files from before load unchanged.

### Bugs fixed

| Bug | Fix |
| --- | --- |
| A parent chain a few tens of thousands deep crashed (the world matrix and lossy scale walked the chain by recursion), and building one with `SetParent` took quadratic time (every call read the whole chain's world pose, even with `worldPositionStays = false`). | The walks are loops; `SetParent` reads the world pose only when asked to keep it, and skips the cycle walk for an object with no children. A 100000-deep chain builds, saves, loads, clones and is destroyed. |
| `ActiveInHierarchy()` stopped looking after 4096 parents and answered "active". | Walks the whole chain. |
| `GetComponentInChildren` and `SceneSerializer::SerializeObject` (every `Instantiate`) recursed per level. | Explicit stacks. |
| `RemoveComponent` held an iterator across `OnDisable` / `OnDestroy`. A handler that removed another component or added one made it erase the wrong element or crash; a handler that asked to remove its own component recursed without end. | The position is looked up again after the callbacks, and a component that is already being removed answers false. |
| `go->tag.clear()`, `+=`, `append` ... left `FindWithTag` returning the object under its old tag. | See `layers_tags_cameras.md`. |
| Reordering root objects (`SetSiblingIndex`, `ReorderSibling`) did not move `StructureVersion()`, so caches keyed on it (UI draw order) kept the old order. | Bumped. |
| An object parented to another during its `OnDestroy` stayed under the frozen shell, inactive for good. | Becomes a root at the pose it had. |
| `SetParent` accepted a destroyed parent and a parent of another scene (a dangling pointer once either scene went). | Both refused (false). A destroyed object cannot be given a parent either. |
| A runtime-only (`transient`) object was not saved, but its children were, with a `parent` link to nothing: they came back as loose roots with a pose meant for a parent, and `Validate` reported a broken link. | Everything under a transient object is runtime-only too (save and clone). |
| A scene saved with one NaN or infinity in it (a physics blow-up, then an autosave) could not be opened: `nan` is not a number to the stream reader. | The reader takes `nan` / `inf`; a transform with a non-finite or all-zero part is reset (with a warning). |
| `layer 99` in a file was stored as 99 (the renderers read it modulo 32, so it behaved as layer 3 while `go->layer == 3` was false). | Stored modulo 32, with a warning (`Validate` kind `BadLayer`). |
| `Mover` / `Spinner` with a NaN velocity or frame time poisoned the position for good. | Skipped. |
| `Spinner` turned by Euler angles each frame, so a spin about several axes depended on the frame rate (one 1 s step and 128 steps of 1/128 s differed by about 50 degrees). | One rotation about the velocity axis: frame-rate independent. A single-axis spin is unchanged. |
| `AddComponent<Transform>()` added a second Transform. | Returns the one it has. |
| `Scene::Destroy(go, NaN)` never destroyed. | Zero, negative and NaN destroy at once. |

### Transform

| Call | Notes |
| --- | --- |
| `GetChild(i)`, `Find("Arm/Hand")`, `FindDeep(name)` | `Find` is a path of direct children; `FindDeep` searches below |
| `IsChildOf(t)`, `Root()`, `Depth()` | `IsChildOf(self)` is true |
| `DetachChildren()` | children become roots, world pose kept, linear time |
| `SetAsFirstSibling()`, `SetAsLastSibling()` | children and root objects |
| `PlaceBefore(anchor)`, `PlaceAfter(anchor)` | reparents (world pose kept) when the anchor has another parent |
| `Translate(delta, Space)`, `Rotate(euler, Space)`, `Rotate(axis, degrees, Space)` | `Space::Local` (the parent's frame, the old `Translate`), `World`, `Self` (own axes; the old `Rotate`). Self and World distances are world units |
| `RotateAround(point, axis, degrees)` | orbit a world point |
| `TransformVector`, `InverseTransformVector` | rotation and scale, no position |
| `GameObject::Path()` | "Root/Arm/Hand", what `FindByPath` reads |

`SetParent` keeps position and rotation exactly and the world scale as `LossyScale()` (under a
rotated parent with non-uniform scale a turned child cannot keep its exact shape; that would need a
shear).

### Spawning and queries

```cpp
scene.Instantiate(prefab, position, rotation);                    // a root at a world pose
scene.Instantiate(prefab, parent);                                // under a parent, local pose kept
scene.Instantiate(prefab, parent, /*worldPositionStays=*/true);
scene.Instantiate(prefab, position, rotation, parent);            // world pose, under a parent
scene.Duplicate(go);              // same parent and local pose, right after the original, "Crate (1)"
scene.UniqueName("Crate", parent); // the first free "Crate (n)" among the siblings
scene.DestroyImmediate(go);       // gone now (from inside an Update it is deferred like Destroy)
Bounds b = Scene::WorldBounds(*go);   // mesh and sprite renderers of go and everything below it
```

A clone copies what the serializer knows (built-in components and scripts, not a plain C++
`Behaviour` subclass nobody registered), keeps the hierarchy below it, and gets fresh ids.
`Instantiate(prefab)` of an object that has a parent still makes a root with the original's local
pose (the editor's Duplicate should call `scene.Duplicate(*go)` to keep the parent). A parent that
`SetParent` refuses leaves the clone a root.

`DestroyImmediate` also runs anything else already queued by `Destroy`. While the scene walks its
component lists (an `Update`, a physics callback, an `OnDestroy`) it only queues, because freeing
components under a running loop is not safe.

`Scene::WorldBounds` skips inactive objects (with their subtrees) and disabled renderers, and is empty
(`IsEmpty()`) when nothing draws. It reads every mesh vertex, so cache it for a big model.

### Mover, Spinner, Lifetime

* `Mover::space` (`Local` default, `World`, `Self`) and `Spinner::space` (`Self` default, `Local`,
  `World`). Saved as an optional trailing number on the `mover` / `spinner` record, written only when it
  is not the default.
* `Lifetime::SetElapsed(s)`, `Restart()`. The seconds already lived are saved (`lifetime 5 1.5`, written
  only when above 0), so a saved game carries on. Disabling the component pauses it.
* The property registry lists `space` for both.

## What changed: scene graph, fourth pass (1.3)

Tests: `transform_world`, `scene_graph_perf`, `scene_reentrant`, `component_order`, `scene_graph_wave4`.
Scene files from before load unchanged; the one new record (`comporder`) is optional.

### Bugs fixed

| Bug | Fix |
| --- | --- |
| `InverseTransformPoint` / `InverseTransformVector` on an object with a zero scale (here or in a parent) returned the world point: `Mat4::Inverse` answers the identity for a singular matrix. | An invertible matrix inverts exactly as before. A singular one gets the pseudo-inverse: each collapsed axis reads 0 and the other axes are exact, so `TransformPoint(InverseTransformPoint(w))` is the nearest point of the object to `w`. |
| `SetPosition` (and so `SetParent` keeping the world pose, a World-space `Mover`, physics interpolation) moved the object under a chain where a stretched object sits above a turned one (a shear): it undid rotation and then scale, not the matrix. `SetPosition(Position())` changed the local position. | The position goes back through the whole parent matrix. A parent with a collapsed axis keeps the old local value on that axis (it used to take an unrelated number there). |
| `ActiveInHierarchy()` walked to the top of the hierarchy for every component every frame: a chain `n` deep cost `n * n / 2` reads per frame (measured: 5000 deep, 2 frames took 3.9 s; 100000 deep would take about 25 minutes by the same growth). | Cached per object; any `active` change or reparent anywhere starts a new epoch, and the next question re-answers the whole hierarchy in `O(n)` (each object reuses its parent's answer). A frame over a chain does linear work. |
| The 2D pointer pass (`OnMouse*`, run every `Update`) read the world position and scale of every active object before checking whether it has a sprite or a 2D box to pick: another `O(n * depth)` per frame on a deep chain, even with the cache above. | The pose is read only for objects that can be picked. |
| `FlushDestroyed` took the objects out of their parents one at a time, reading the world pose through the whole chain and erasing from the front of a child list. A chain built leaf first took `O(n^2)` (20000 deep: 19 s, 50000: 130 s), one parent with `n` children `O(n^2)` too (50000: 1.1 s, 100000 children of a survivor: 4.7 s). | One top-down pass: each dying object gets its world pose from its parent's (carried along), becomes a root shell, and each child list loses all its dying children together. 100000 deep and 200000 wide are well under a second. |
| `Scene::Clear()`, `SceneSerializer::LoadFromFile` and `Deserialize` called from a component's `Update`, `OnDisable`, `OnDestroy` or `Awake` freed the object whose method was running (and the pending-flush batch). A `Clear()` from an `OnDestroy` that ran inside `Clear()` told every component again, without end. | While the scene walks its component lists the call is queued and runs at the end of the frame (see below). A `Clear()` inside `Clear()` is ignored. |
| `Lifetime` summed its seconds as a float: past about 524288 s (6 days) a 60 FPS frame adds nothing any more, so such a lifetime never ended. | Summed as a double. |
| `Instantiate` of a destroyed object made an empty, switched-off object. | Returns `nullptr` (`Duplicate` already did). |
| `Scene::Update(NaN)` or `Update(infinity)` handed the bad time to physics, timers and every script. | Treated as no time. |
| `FindWithTag` checked every object listed under the tag against what it carries now on every call: 10000 objects under one tag made each lookup `O(n)`. `UnlistedTags` scanned its own result per object (10000 different tags: quadratic). | `FindWithTag` checks the first match; the list-returning calls still check every entry. `UnlistedTags` uses a hash. |
| `FindByGuid` scanned every object per call (one lookup per object was quadratic). | A hit is a hash lookup; the index is rebuilt after objects were created, destroyed or reparented. An id that is not in the index, or was changed by `SetGuid` since, is still found by the scan. |
| `Destroy(go, seconds)` scanned the whole waiting list per call to see whether `go` already waits (20000 bullets each given a delayed destroy: quadratic). | An index by instance id. The destruction order is still the order of the requests. |

### Clear and loads from inside callbacks

`Scene::IsUpdating()` is true while the scene walks its component lists and calls user code (Awake, Start,
Update, LateUpdate, FixedUpdate, OnRender, OnEnable, OnDisable, OnDestroy and the physics, timer and tween
callbacks that run inside them). The rule:

* `scene.Clear()` while `IsUpdating()` does nothing now and empties the scene at the end of the frame, after the
  destroy queue was flushed (`RequestClear()` is the same call, spelled for anywhere; `HasPendingClear()` tells).
* `SceneSerializer::LoadFromFile(scene, path)` while `IsUpdating()` becomes `scene.RequestLoad(path)` and returns
  `true` when the file could be read; a bad scene is reported at the end of the frame through `LastLoadError()`,
  as for `RequestLoad`. `SceneSerializer::Deserialize` does the same with `RequestLoadText`. Objects marked
  `DontDestroyOnLoad` come along, as with `RequestLoad`.
* Everything made after the call goes with the old scene (all or nothing): `scene.Clear(); scene.CreateGameObject("X");`
  inside an `Update` leaves an empty scene at the end of the frame. To replace the world use `RequestLoad`.
* A queued `Clear()` runs before a queued load. Outside a frame (editor edit mode, tests, the loading code)
  `Clear()` is immediate as always. An `OnRender` that asks waits for the next `Update` or `FlushDestroyed`.
* `Scene::~Scene` and `Clear()` run their `OnDisable` / `OnDestroy` pass inside this rule, so a handler that calls
  `Clear()` or loads a scene is not re-entered.

### Component order

See `scene_serializer.md`: `GameObject::MoveComponent` and the new `ReorderComponents` are saved in an optional
`comporder` record, written only for an object that was arranged on purpose.

### New

| Call | Notes |
| --- | --- |
| `go->GetComponentsInParent<T>(includeInactive = false)` | every match on the object and each parent, nearest first |
| `go->ReorderComponents({a, b})`, `go->HasCustomComponentOrder()` | the named components come right after the Transform, in that order; saved |
| `scene.DestroyChildren(go)` | queue every child of `go`; returns how many direct children were asked |
| `transform->SetPositionAndRotation(pos, rot)` / `SetLocalPositionAndRotation(pos, rot)` | both at once (world / parent frame; the rotation is normalized) |
| `transform->SetParent(parent, keepWorld, siblingIndex)` | reparent and place among the new siblings (clamped; `nullptr` = among the scene's roots) |
| `scene.RequestClear()`, `HasPendingClear()`, `IsUpdating()` | see above |
| `scene.Stats()` | a `SceneGraphStats`: `objects`, `roots`, `maxDepth`, `activeInHierarchy`, `inactive`, `components`, `runningComponents`, `awaitingStart`, `pendingDestroy`, `shells`, `byType` (name and count, biggest first); `Lines(maxTypes)` and `ToString()` for an overlay |
| `GameObject::ActiveWalkSteps()` | diagnostic: how many objects `ActiveInHierarchy()` has had to look at since the process started (a cache hit adds nothing) |

Already there, so not added: `FindByPath`, `GetComponentInChildren(s)` with an include-inactive flag,
`Transform::GetSiblingIndex` / `SetSiblingIndex` / `SetAsFirstSibling`, `PlaceBefore` / `PlaceAfter`,
`GameObject::SetLayer(layer, includeChildren)`, `Scene::ReorderSibling`.

```cpp
for (const std::string& line : scene.Stats().Lines(3)) overlay.Add(line);   // "Objects: 120 (3 roots, 40 inactive, depth 4)" ...
```

### Notes

* `go->active` is an `ActiveFlag` (a `bool` that counts changes): `if (go->active)` and `go->active = false` work as
  before; taking its address as a `bool*` does not. Reading `ActiveInHierarchy()` from several threads is safe while
  nothing is being changed; the cache is relaxed atomics.
* `Transform::Position()` and the other world reads still walk the chain (the local fields are public, so the
  world matrix is checked level by level); a very deep chain read from its leaf end costs its depth per read.
* `SetParent(parent, true)` keeps position, rotation and `LossyScale()`. Under a parent with a mirror (negative scale)
  or a stretch above a turned child, the matrix itself cannot be kept (that takes a shear); the three numbers are.
* Not done: `Transform::SetParent(nullptr)` called on each of a very large number of children of one parent, one at a time, is
  still `O(n)` per call (a child list is a vector; `DetachChildren()` and `Destroy` are linear). The order of `AddComponent`
  calls is not saved, only an arranged order (see `scene_serializer.md`).
* The wide and deep destroy checks in `scene_graph_perf` rely on size (a 100000-deep chain built leaf first would have taken about 8 minutes
  before, by the growth measured at 20000 deep; 100000 children took 4.7 s); the test has a ctest `TIMEOUT` of 240 s instead of a clock inside it.

## What changed: validators (core wave 6)

See "Which validator?" above. Fixed: `proc:` textures reported as missing files; a damaged data pack reported as "missing 'okayscene' header";
missing fonts and voxel terrain textures not reported by `Scene::Validate`; the two validators disagreeing on where a
file may be. Added: `ValidatorOptions::checkLinks` with `scene.duplicate_guid` and `scene.broken_parent`, and
`ValidateSceneLinks`. Tests: `core_validation`. Not done: the third list, `AssetDatabase::FindMissing` (33.2), is a
different question (what is not in the project's database), and `CollectAssetPaths` was left alone because builds
depend on it.

## What changed: scene graph, fifth pass (1.3)

Tests: `scene_graph_wave5`, `object_pool_wave5`, `component_order` (extended). Scene files from before load unchanged; the
`comporder` record of the fourth pass is now written for more objects (see `scene_serializer.md`).

### World reads: what is true, what was done

A cached world matrix per `Transform` already existed (since before the fourth pass). It cannot be keyed on a dirty flag or a
version that the setters bump: `localPosition`, `localRotation` and `localScale` are public fields that about 2400 places write
directly (components, the editor's drag boxes, the script bindings, tests), and no setter sees `t->localPosition.x = 3`. A cache
that skipped the check would answer wrongly after such a write. So the cache keeps the local values it was built from and
checks them on every read, level by level up the parents. That check is what a world read costs, and it stays proportional to the
depth. What changed:

* An unchanged chain is checked in **one read-only loop** (a few compares per level, no copying, no matrix math); only the levels
  from the first changed one downward are rebuilt, each once. Before, every read first copied the whole chain and then ran a
  refresh call on each level.
* `LossyScale()` is cached with the matrix (it was a separate walk of the chain that multiplied the scales again on every call);
  `Position()`, `Rotation()`, `LossyScale()` and `LocalToWorldMatrix()` now cost the same single check.
* `SetParent(parent, true)` (keep the world pose) walks the new parent's chain **once**, and the old parent's once: it used to
  walk it about three times after the move and once for each of position, rotation and scale before it. Rebuilding a 1500-deep chain
  that way now takes 1124250 level checks (one walk per `SetParent`: the sum of the depths), where the old code did three. The numbers
  it computes are the same expressions as before.
* Diagnostics, counted per thread: `Transform::WorldWalkSteps()` (levels looked at), `Transform::WorldRebuilds()` (matrices
  rebuilt). `scene_graph_wave5` checks that 4000 random edits (plain field writes, `Translate`, `Rotate`, `SetPosition`,
  `SetRotation`, `SetLossyScale`, `SetParent` with and without keeping the world pose, `SetSiblingIndex`, `LookAt`,
  `DetachChildren`) always read the same pose as a walk of the local fields with no cache, and that one edit in the middle of a
  200-deep chain rebuilds exactly the levels below it.
* Thread safety is what it was: reading the world pose of a chain whose caches are current only reads (several threads may);
  a read that has to rebuild writes the caches of that chain, so it must not run at the same time as another read of the same
  chain. Editing the hierarchy is single-threaded.

Not done, and why: reading a leaf of a 5000-deep chain still looks at 5000 levels each time (about 1.1 ns per level: 5.5 microseconds
a read; a 40-bone skeleton is 40 levels). Making that constant needs every
write to the local fields to go through a setter (a breaking change of 2400 call sites and of the editor), or a read pass that
promises not to write (nothing in 1.3 runs one; renderers and physics would have to opt in).

### Child lists

`SetParent(nullptr)` (or to another parent) on one of a parent's 300000 children used to find and erase it in a vector: linear in
the list, so taking all of them out one at a time was quadratic (taking 300000 children out from the front moves about 45 billion list
slots). Now:

* each child knows its slot in its parent's list;
* a list of up to 64 children is erased from as before; a longer one leaves a hole where the child was and sweeps the holes out in
  bulk (when more than half the slots are holes, and when the list is read), so a removal costs a constant and the order of the
  others never changes;
* `Children()` and `GetChild(i)` settle the list first, so a reader never sees a hole (readers on several threads are fine: the
  sweep takes a lock), `ChildCount()` is exact without settling, `GetSiblingIndex()` is a lookup instead of a scan;
* `Transform::ChildListWork()` counts the slots visited or moved by list maintenance: taking all 300000 children out of one parent
  from the front, one at a time, costs 901100 slot visits (3 per child) and about 30 ms on the test machine.

### Bugs fixed

| Bug | Fix |
| --- | --- |
| `FindByPath("Root/Door/Lock")` returned `nullptr` when two siblings were called "Door" and only the second had a "Lock": the search took the first match at each level and never went back. An object whose name contains `/` could not be found by path. | Every match is tried, in scene and sibling order; the path is read from the left against whole names. `FindAllByPath` lists them all. Runs of `/` still count as one. |
| The order of plain `AddComponent` calls was lost on save and load (components came back in the writer's order, so `Update` order changed). | `comporder` is written when the object's order is not the one its records load as; see `scene_serializer.md`. |
| A copy made with `Instantiate` / `Duplicate` resolved the names its components point at (`Joint2D` / `Joint3D` `connectedBody`) with `Scene::Find`, which answers with the first object of that name: the original's. A cloned vehicle's joint pulled on the original's axle. | `FindNearest(scene, name, from)` looks in the asking object's own hierarchy first; the two physics joint loops use it. Other components that point at an object by name (`CameraFollow::targetName`, `Teleporter`, ...) still call `Scene::Find`: they are listed in the report as follow-ups. |
| A tag or a layer name was limited to 64 / 32 **bytes**: a Japanese or Cyrillic name of 22 / 11 characters was refused. Two layers could be named "Enemy" and "enemy" apart, but also "Énemy" and "énemy". | Counted in characters (UTF-8), at most four bytes each so a run of broken bytes cannot grow without end. Layer names compare without regard to case for Latin-1, Latin Extended-A, Greek and Cyrillic too (`Layers::Fold`). |
| `ObjectPool`: a body reused from the pool kept its velocity, spin, waiting forces and render-interpolation history; a `Lifetime` kept the time it had already lived (an object released at 1.9 s of 2 died 0.1 s into its next use). | A reused object is reset: new `Component::OnPoolReuse()`, implemented by `Rigidbody2D`, `Rigidbody3D` and `Lifetime`. |
| `ObjectPool`: a `Destroy(go, seconds)` that was still waiting when the object was released destroyed the object during its next use. | `Release` takes it back (`Scene::CancelDelayedDestroy`). |
| `ObjectPool`: an object queued for destruction (or waiting for a delayed one) while pooled could be handed out and then vanish. | `Get` skips it and forgets it (`Scene::IsDestroyPending`); `Release` of such an object lets it go. |
| `ObjectPool`: the tag, layer, `active` flag and enabled components of a reused object (and the pose, tag, layer and flags of its children) stayed as the last use left them (an enemy tagged "Dead", an AI component switched off on death). | Put back to the template's, when the hierarchy and the components still have the template's shape (the root is always put back). |
| `ObjectPool`: every `Get`, `Release` and count looked up every object the pool held (a pool of 5000 bullets: 10000 locked lookups per call), `Release` searched the idle list, and objects destroyed behind its back counted against `maxSize`. | `Get` and `Release` look at one or two objects; the counts prune only after the scene's structure changed; dead objects are dropped before the limit is checked. `ObjectPool::LookupCount()` counts the work. |

### New

| Call | Notes |
| --- | --- |
| `go->GetOrAddComponent<T>(args...)` | the first `T`, or a new one made with `args` (rules from `RequireComponent` / `DisallowMultipleComponent` apply) |
| `go->ForEachDescendant(fn, includeInactive = true, includeSelf = false)` | pre-order, siblings in order, explicit stack (100000 deep is fine); `fn` may return `bool`: `false` stops and the call returns `false`; a child added to the object just visited is visited |
| `go->HasLiveComponentInHierarchy()` | `HasLiveComponent` over the object and everything below it |
| `transform->LookDirection(dir, up)` | face a direction; a zero or non-finite direction does nothing; a direction along the up hint takes a world axis for the hint, so the result is always finite |
| `transform->LookAtYaw(target, up = Up)` | turn about the up axis only (an enemy facing the player on level ground, a turret base); nothing when the target is straight above or below |
| `transform->LookAt2D(target, Transform::Facing2D::Right / Up)` | turn about Z so the object's right (or up) axis points at the target in the X / Y plane |
| `transform->RotateTowards(target, maxDegrees, up)` | turn at most `maxDegrees` toward facing the target, `true` once it does; frame-rate independent when `maxDegrees` is speed times frame time |
| `transform->LookAt(const Transform*, up)` | the same as `LookAt(target->Position(), up)`; null does nothing |
| `scene.FindAllByPath(path)`, `FindNearest(scene, name, from)` | see above (`okay/Scene/NearestLookup.hpp`) |
| `scene.UpdateCount()`, `scene.IsDestroyPending(go)`, `scene.CancelDelayedDestroy(go)` | counts the Updates that delivered OnEnable / OnDisable; "queued or waiting to be destroyed?"; take a delayed destroy back |
| `Component::OnPoolReuse()` | an `ObjectPool` is handing this component's object out again: reset the runtime state of the last use |
| `ObjectPool::LookupCount()`, `Transform::WorldWalkSteps()`, `WorldRebuilds()`, `ChildListWork()` | diagnostics for tests and overlays |

### Checked and found right

`Scene::Stats()` on 150000 objects with a 50000-deep chain (counts right, `ActiveInHierarchy` walks stay linear, a second call
walks nothing); `Mover` / `Spinner` under a parent that is destroyed in the same frame (they run once more, then freeze with the
shell; an object adopted while its parent is destroyed carries on as a root with its `Mover` running); `Layers` / `Tags` and
`ProjectLists` with 10000 objects carrying their own tag in another language; a very long path through a 20000-deep chain.

### Notes

* A name in a path is matched whole: `FindByPath("a/b")` finds an object called "a/b" and also an object "b" under "a", the first in
  scene order wins.
* Components still point at other objects by name. A clone has the same names as the original, so a component that looks a name up
  with `Scene::Find` reaches the original's object (the joints no longer do). Give the parts of a prefab names that are unique in the
  scene, or look them up with `FindNearest(scene, name, gameObject)` or `transform->Find("Arm/Hand")`.
* `ObjectPool` is main-thread only like the scene, and must not outlive its scene (it keeps a pointer to it).
* Merge: a `comporder` line that two branches both edit (each added a different component to an object whose order is not the
  writer's) is one conflicting record for `SceneMerge`, like any other single-line record.

