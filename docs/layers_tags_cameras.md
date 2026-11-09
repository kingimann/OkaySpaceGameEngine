# Layers, tags and the camera stack

Feature map 1.3 (scene graph) and 4.8 (cameras). Test: `tests/test_layers_tags_cameras.cpp`.

## Layer collision matrix

A scene has one 32 x 32 matrix saying which collider layers interact. Both physics
worlds (2D and 3D) use it for contacts, and it is saved with the scene.

```cpp
scene.SetLayerCollision(3, 4, false);          // layers 3 and 4 pass through each other
scene.LayersCollide(3, 4);                      // false (symmetric)
scene.LayerCollisionMask(3);                    // bit b set = layer b collides with 3
scene.physics().Raycast(scene, o, d, dist, nullptr, scene.LayerCollisionMask(myLayer));
scene.ResetLayerCollisions();
```

* The matrix is the `Collider2D::layer` / `Collider3D::layer` of each collider, not
  `GameObject::layer` (that one is for camera culling).
* Layer numbers wrap with `& 31`, the same as the query masks. Before, `LayersCollide`
  answered "true" for an out of range layer while queries wrapped it.
* Scene file: one optional `layermatrix r0 ... r31` record (a uint32 row per layer),
  written only when something is disabled. Old scenes lack it and load with everything
  colliding. A pair collides only when both rows allow it. A truncated record fails the
  load. `Scene::Clear()` resets the matrix, so a loaded scene never inherits the old one.

## Tags

`GameObject::tag` is still a free string (scripts assign runtime tags such as
`"Debris"`), now typed `TagString`, a `std::string` that counts assignments.

* `Scene::FindWithTag` and `FindGameObjectsWithTag` read a tag index, rebuilt only
  when objects or tags changed since the last lookup, instead of scanning every object.
* An empty tag matches nothing. Before, `FindWithTag("")` returned the first untagged
  object.
* Lookups do not skip inactive objects (unchanged); filter on `active` if you need it.
* `Tags` (`okay/Scene/Tags.hpp`) is the project's named tag list: `Builtin()`,
  `Custom()`, `Add`, `Remove`, `Contains`, `All`, `IsValidName` (1 to 64 characters, no
  control characters, no leading or trailing space).
* `GameObject::SetTag` applies `Tags::Accepts`: valid name, and listed when
  `Tags::Strict()` is on. Direct assignment (`go->tag = x`) is never refused.
* The editor still keeps its own tag list in the project settings; it is not wired to
  `Tags` yet (the engine side is complete: see "What changed: project tag list" below for
  the small change the editor needs).

## Cameras

* `Camera::Awake` no longer lets a lower-depth main camera take the scene's main camera
  from a higher-depth one. `Scene::ResolveMainCamera` (end of every `Update`) already
  picked by depth; both rules agree now. A non-main camera never outranks a main one.
* `Scene::CameraStack()` returns the usable cameras (enabled, active in the hierarchy),
  lowest depth first, ties in scene order. Enabling, disabling or changing the depth of
  a camera changes the stack on the next call.
* `Camera::ClearPlan(first, color, depth)` says what a camera clears. The first camera
  always clears everything. Later ones: `Skybox` / `SolidColor` clear colour and depth,
  `DepthOnly` clears only depth (new: draw over the cameras below), `DontClear` (new)
  keeps both. The flags are saved as before; unknown values load as `Skybox`.

Not done: the player and editor still draw one camera (the main one). Rendering the
whole stack there means looping their render path over `CameraStack()` with
`ClearPlan` and each camera's viewport rect and culling mask.

## What changed: project tag list, layer names, matrix presets (scene graph 1.3)

The engine side of the editor's tag list and layer names is complete, so wiring the editor is a
small change (see "Editor hook" below). Test: `tests/test_layers_tags_project.cpp`
(ctest `layers_tags_project`).

### Layers (`okay/Scene/Layers.hpp`)

| Call | Notes |
| --- | --- |
| `Layers::IsValid(i)` | 0-31 |
| `Layers::NameToLayer(name)` | case and surrounding space ignored; also reads "Layer 7" and a bare number; -1 when unknown |
| `Layers::IsNamed(i)`, `Name(i)` | `Name` still falls back to "Layer N" |
| `Layers::SetName(i, name)` | refused (false) for a bad index, an invalid name (1-32 characters, no control characters, no leading / trailing space, no `,` or `!`, not only digits) or a name another layer has (case-insensitive) |
| `Layers::ClearName(i)` | back to "Layer N"; layer 0 always keeps a name (it can be renamed) |
| `Layers::ResetNames()`, `DefaultName(i)` | the names a new project has |
| `Layers::Mask({0, 5})`, `MaskToList(mask)` | the inverse of `MaskFromList`; a full mask is "" (blank = every layer), an empty one is "none" (display only) |

### Tags (`okay/Scene/Tags.hpp`)

`Tags::WhyInvalid(name)` gives the reason text an editor can show next to a bad name,
`Tags::IsBuiltin`, `Tags::Index(name)` (position in `All()`), `Tags::Rename(old, new)` (a custom
tag, keeping its place; refused for built-ins, bad names and names already listed). `Tags::Add`
stops at `Tags::kMaxCustom` (1000) so a damaged project file cannot make the list huge.

`Scene::UnlistedTags()` lists the tags objects carry that are not in the list (sorted, once each);
`Scene::ReplaceTag(from, to)` renames the tag on every object that carries it (`""` untags) and
returns the count. Use it together with `Tags::Rename` for a "rename tag" button. A `to` that
`Tags::Accepts` refuses changes nothing.

`GameObject::tag` is a `TagString`. Every `std::string` call that changes it (`clear`, `+=`,
`append`, `assign`, `insert`, `erase`, `replace`, `resize`, `push_back`, `pop_back`, `swap`, a
non-const `operator[]` / `at` / `begin` / `end` / `data`) now counts as a change, not just
assignment, so the tag index cannot go stale. Lookups also compare what they find with what the
object carries now, and rebuild the index once when they disagree.

### Project file (`okay/Scene/ProjectLists.hpp`)

`ProjectLists` reads and writes three kinds of line of `project.okayproj` (the editor's one
`key=value` per line file) and leaves every other line alone:

```
tag=Boss                 a custom tag (the same lines the editor already writes)
strict_tags=1            Tags::Strict()
layer_name_8=Enemy       a layer name that differs from the default; "layer_name_4=" is a default
                         name cleared on purpose
```

| Call | Notes |
| --- | --- |
| `ProjectLists::Write()` | the lines above, stable order (tags as listed, layers by index) |
| `ProjectLists::ApplyLine(key, value)` | true when the key is ours and the value was taken |
| `ProjectLists::Apply(text)` / `Load(path)` | every line; LF or CRLF; bad lines are skipped |
| `ProjectLists::Save(path)` | rewrites only our lines in an existing file (creates it when missing) |
| `ProjectLists::Reset()` | no custom tags, not strict, default layer names |

**Editor hook (for after the editor rewrite lands).** `editor/main.cpp` keeps `g_project.customTags`
and its own preset list. The change: in `LoadProjectSettings` call
`okay::ProjectLists::ApplyLine(k, v)` first for every line (it returns false for the keys the editor
owns, so its `if / else if` chain follows); in `SaveProjectSettings` append
`f << okay::ProjectLists::Write();` instead of the `tag=` loop; replace `kTagPresets` and
`g_project.customTags` with `Tags::All()` / `Tags::Add` / `Tags::Remove` / `Tags::Rename` plus
`Scene::ReplaceTag`; use `go->SetTag(...)` (it returns false for a name the list refuses in strict
mode, and `Tags::WhyInvalid` says why); and give the layer dropdown a rename box that calls
`Layers::SetName` / `ClearName`. The tag lines the editor writes today are the same `tag=` lines,
so existing project files load unchanged.

### Collision matrix helpers

```cpp
scene.ApplyLayerCollisionPreset(Scene::LayerCollisionPreset::SameLayerOnly);   // or Everything, Nothing
scene.SetLayerCollisionGroups(Layers::Mask({8}), Layers::Mask({9, 10}), false); // every pair between two sets
scene.SetLayerCollisionByName("Enemy", "Player", false);                        // false when a name is unknown
for (auto [a, b] : scene.DisabledLayerPairs()) { /* a <= b */ }
```

`Everything` is the default; `Nothing` turns every pair off (a layer ignores itself too);
`SameLayerOnly` keeps only (n, n). The matrix is still saved with the scene (`layermatrix`).

### Queries

`Scene::FindGameObjectsOnLayer(layer)`, `CountOnLayer(layer)`, `CountWithTag(tag)`,
`FindGameObjectsWithName(name)`; `GameObject::SetLayer(layer, includeChildren = false)` (false and
nothing changed for a layer outside 0-31), `GameObject::CompareTag(tag)`.

### Validation

`Scene::Validate` / `SceneSerializer::Validate` report two more kinds: `BadLayer` (a `layer`
outside 0-31 in the file; the loader used it modulo 32 and now stores that value) and
`UnknownTag` (an object carries a tag that is not in the list; reported only when
`Tags::Strict()` is on, since free tags are normal otherwise).
