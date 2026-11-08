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
