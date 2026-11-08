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
  `Tags` yet.

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
