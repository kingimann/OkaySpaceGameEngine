# Scene hierarchy, search and validation

Feature map 1.3 - 1.5. Everything here is additive: old scene files load unchanged.

## Active state follows the hierarchy

`GameObject::active` is the object's own switch. `GameObject::ActiveInHierarchy()` is true
only when the object and every parent above it are active. The engine tests the second one:
Scene `Update` / `LateUpdate` / `Render`, collider auto-fit, 2D pointer picking, the GL, D3D11,
D3D12 and software renderers (colour and shadow passes) and Physics2D / Physics3D all skip a
child of an inactive parent. `OnDisable` / `OnEnable` follow the same rule, so a script on a
child hears about it when its parent is switched off or on.

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

* Mass `Destroy` + `FlushDestroyed` are linear in the number of objects and components.
* `Serialize` finds parents through a map instead of a scan per object.
* `InstantiateFromFile` keeps the decoded text of up to 32 prefab files, checked against the
  file's size and time, so repeated spawns skip the file read. Changing the file reloads it.
* A component removed by another component's `Awake` / `Start` in the same flush batch is
  skipped instead of touched after it was freed.

## Search

| Call | Notes |
| --- | --- |
| `scene.FindGameObjectsWithTag(tag)` | every match, in scene order |
| `scene.FindByPath("A/B/C")` | a root named A, its child B, its child C. `Find("A/B/C")` also accepts a path |
| `scene.Roots()` | objects without a parent |
| `go->GetComponentInChildren<T>(includeInactive = false)` | the object itself first, then depth first |
| `go->GetComponentsInChildren<T>(includeInactive = false)` | every match, pre-order |
| `go->GetComponentInParent<T>(includeInactive = false)` | the object, then each parent |

An inactive object is skipped together with its children unless `includeInactive` is true.

## Transform helpers

`LookAt(target, up)`, `SetLossyScale(worldScale)` (and `SetScale`, the same call),
`EulerAngles()` / `SetEulerAngles()`, `LocalEulerAngles()` / `SetLocalEulerAngles()`,
`GetSiblingIndex()` / `SetSiblingIndex(i)` (children use the parent's list, roots use the
scene's root order; the index is clamped).

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
are not reported; the asset check covers what `CollectAssetPaths` knows about.

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
