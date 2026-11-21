# Cameras, layers and tags

> **What this is:** The camera upgrade, and layers, tags and the camera stack.
>
> **Status:** combined on 2026-11-20 from 2 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
>
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [Camera upgrade](#camera-upgrade) (was `docs/camera_upgrade.md`)
- [Layers, tags and the camera stack](#layers-tags-and-the-camera-stack) (was `docs/layers_tags_cameras.md`)

## Camera upgrade

*Was `docs/camera_upgrade.md` until 2026-11-20.*

Owner request: "improve the camera system". Branch `claude/camera-2`. This page says what the engine already had, what was
missing, what was added (inside the existing parts, not as a second camera system), how to try it, and what was and was not
checked. Everything here was checked **headless on Linux only** (numbers over simulated frames). Nothing was run on Windows or
on a screen; see "Unverified".

### Existing / Gap

| Area | Existing (before this change) | Gap | What was done |
|---|---|---|---|
| Camera component | `Components/Camera`: perspective and orthographic, physical lens, lens shift, dutch, zoom, culling mask, viewport rect, clear flags and the camera stack (`Scene::CameraStack`, `Render/CameraStack.hpp`), render targets, trauma shake, `ZoomTo`, pixel-perfect snapping in `EyeMatrix` | Shake was one trauma blob (no presets, one noise made from two sines); no FOV kick or held FOV change; no hit flash hook; pixel snapping had no test | Added `ShakePreset` (Impact, Explosion, Footstep, Handheld), `AddShake`, `SetHandheld`, `KickFov`, `SetFovOffset`, `HitPulse` on the same component. The trauma shake is kept as it was. Pixel snapping is now tested (it was already correct) |
| Simple chase camera | `Components/CameraFollow`: exponential ease toward a named object | Not touched. Branch `claude2/camera-follow-fixes` (Claude 2) already fixes its NaN and world-space problems, so it is left alone to avoid a conflict | Nothing |
| Shots and brain | `VirtualCamera` + `CinemachineBrain`: priority, Follow and Look At by name, binding mode, exponential damping, angular dead zone, FOV, dutch, confiner box, handheld noise and impulse, FreeLook orbit, tracked dolly. Brain: blend time and an ease on/off switch | (a) The shot aimed the **opposite way** (it rotated +Z to the target; a camera looks down -Z). (b) damping was an exponential ease only (no velocity, stops dead). (c) no screen dead zone / soft zone (only an angle), no look ahead, no groups, no collision, no see-through. (d) blends froze the shot being left, an interrupted blend was not tested, only two curves (on/off), no per-shot blend. (e) no way for a script to cut to a shot. (f) the brain fought a playing cutscene and popped when it ended | All fixed inside `VirtualCamera` / `CinemachineBrain` (see "What is in it") |
| Third-person controller cameras | `PlayerCamera.hpp` (`PlaceThirdPersonOrbit`), `ControllerFeel.hpp`: `FollowPivot`, `LookSmoother`, `SpringArm` (sphere sweep, triggers ignored), `FovKick` for sprint and aim FOV, first-person camera | `SpringArm` lived in the controller header, so the shot rigs could not use it | `SpringArm` moved to `Components/CameraCollision.hpp` (same code, ControllerFeel includes it) and the rig uses it. Controllers unchanged. Sprint FOV for a rig is `Camera.SetFovOffset` |
| Camera stack | `CameraStack.hpp`: ordered passes, viewport rect, clear flags, composite | The player and editor still draw only the main camera (`docs/gameplay/cameras.md`) | Not done (render path, outside this change) |
| Cutscene camera | `SequencePlayer` camera track and keyed shake (`SetCutsceneShake`) | The brain kept writing the camera while a sequence played, and jumped back when it ended | Brain yields while a sequence plays and blends back (switch "Let cutscenes take over") |
| Dialogue | `DialogueRunner` has no camera hook | No camera shots in dialogue | Still none. A dialogue event can call `Camera.BlendTo("Shot", 0.5)`; wiring a "camera shot" field into dialogue lines is not done |
| 2D | Orthographic camera, pixel snapping, vcam confiner box (centre only) | No view-aware confinement, no rooms, no 2D zone follow | `ortho2D`, zones, `confineToView`, `CameraRoom` + `useRooms` |
| Editor | Camera inspector, frustum gizmo and Camera Preview picture-in-picture for a selected **Camera**, Create > Camera > Virtual Camera / Brain / Dolly | No rig entry, no zone / probe gizmo, no preview for a rig | See "Editor" |
| Scripts | `camera_shake`, `camera_zoom_to`, `camera_fov`, `camera_follow`, ...; VS nodes Camera Shake, Set Camera Target, Zoom, Switch Camera | No presets, no blend, no group, no FOV kick | `Camera.*` builtins (each also a visual-script node `Fn.Camera.*`) |
| Saving | `camera`, `vcam`, `cmbrain`, `camerafollow` lines; property registry knew `Camera` | New settings needed to save; registry did not know the rigs | Keyed lines `camerax`, `vcamx`, `cmbrainx`, `camroom`; old files load as before; `VirtualCamera`, `CinemachineBrain`, `CameraRoom` in the property registry |

Branches looked at: `claude2/camera-follow-fixes` (CameraFollow only), `claude/camera-follow`, `claude/layers-tags-camera-stack`,
`claude/scripting-camera` (older work already in main). Nothing in flight touches `VirtualCamera`, `CinemachineBrain` or `Camera.cpp`.
The "camera and animation smoothing plan PR1 to PR6" pieces that shipped are the ones listed above: `FollowPivot`, `LookSmoother`,
the shared `PlaceThirdPersonOrbit` and the controller render smoothing. Those are left as they are.

### What is in it (and what each file was written from)

Rule 19a: all code is ours. Written from general algorithms, not from any other engine.

| File | What | Written from |
|---|---|---|
| `engine/include/okay/Components/CameraMath.hpp` | Pure maths, no scene: critically damped spring, gradient noise, shake presets and mixer, blend curves, dead and soft zone rule, look ahead filter, target group frame, 2D confine and rooms, pixel snap, fade tracker | The closed-form solution of the second-order spring ODE; the usual gradient-noise construction; a cubic Bezier solved by Newton and bisection; our own zone rule (below) |
| `engine/include/okay/Components/CameraCollision.hpp` | `SpringArm` (moved, one new switch: snap in or ease in) | Existing engine code, moved |
| `engine/include/okay/Components/CameraRoom.hpp` | A box a 2D camera is held inside | The box containment rule |
| `VirtualCamera.hpp`, `CinemachineBrain.hpp`, `Camera.hpp/.cpp` | The rig layer, the blends, the effects | Same pieces |

How each piece behaves, in plain words:

* **Spring damping.** `Damping style = Spring` follows a target with a critically damped spring. It never overshoots, it keeps its
  speed when the target turns, and after one second the camera is in the same place at 30, 60 and 144 frames a second.
  `Smooth (classic)` is the old exponential ease and is still the default for old scenes. "Position damping" is the speed in both:
  higher is snappier.
* **Dead zone and soft zone.** Fractions of the screen. Inside the dead zone nothing moves. In the soft zone the camera eases the target
  back to the dead zone's edge (speed: "Catch-up speed"). Past the soft zone it catches up at once, so the target can never leave the
  screen. A soft zone smaller than the dead zone means hard edges.
* **Look ahead.** The shift is the target's speed (smoothed, so a body moved by 50 Hz physics does not make a 144 fps camera wobble)
  times "Look ahead (seconds)", limited to "Look ahead most", eased with a spring. A teleport (more than 8 units in a frame) resets it.
* **Group.** Names in "Group". The camera aims at the middle of the box round them and steps back (or zooms a 2D camera out) so the whole
  group stays in view. `Camera.AddToGroup` and `Camera.RemoveFromGroup` change it while playing.
* **Walls.** A sphere sweep from the target to the camera against solid colliders. Triggers never count; "Layers that block" picks layers.
  By default it jumps in at once (so the camera is never inside a wall) and glides back out; "Jump in at once" off eases in instead.
* **See-through.** Things between the camera and the target fade to "See-through amount" and come back. It changes the object's colour
  alpha, so the object must use a material that draws with alpha (the engine's renderers do for alpha below 1).
* **Blends.** Brain "Blend style": Cut, Linear, Ease In and Out (the default and the old behaviour), Ease In, Ease Out, Custom curve
  (four numbers like a web page's cubic-bezier). Each shot can have its own "Blend time" and "Blend style". The shot being left keeps
  moving during the blend; changing your mind half way starts from the picture on screen, so there is no pop. `Camera.BlendTo` keeps one shot
  live until `Camera.BlendTo("")`.
* **Cutscenes.** While a Sequence Player plays, the brain stops writing the camera. When it ends the brain blends from where the camera
  is. Turn off with "Let cutscenes take over".
* **2D.** "Flat 2D camera" slides with the target and never turns; "View height" is the units shown from the middle to the top. With
  Confine to box on, "Keep the picture edges inside the box" keeps the edges (not just the middle) inside. "Use rooms" holds the camera in
  the Camera Room the player is in and slides to the next room over "Room slide time". A room smaller than the screen holds the view in its
  middle. Pixel perfect is the Camera's existing switch.
* **Effects.** `Camera.ShakePreset` / `Camera.Shake("impact" | "explosion" | "footstep" | "handheld", strength)`. Shakes stack (up to 16).
  `KickFov(degrees, seconds)` widens the view smoothly and settles; `SetFovOffset(degrees, seconds)` holds a change (sprint, aim).
  `HitPulse(strength)` darkens the screen edges for a moment (it adds to the scene's vignette and takes its share back out).
  Players who turned on Reduce Motion get none of the shake or FOV movement.

### Editor

* Add Component > Camera: **Camera Rig** (a Follow rig, ready to use, and the brain is put on the main camera) and **Camera Room**.
* GameObject > Camera (and the Hierarchy Create menu): **Camera Rig: Follow / Orbit / Look At** (select the object to follow first), **Camera Room (2D)**.
* Inspector, Virtual Camera: new sections Camera Rig (style buttons, smoothing style), Framing, Group, 2D camera, Walls, Blend into this shot, each
  with plain tooltips. Cinemachine Brain: blend style, custom curve with a small drawing. Camera: "Camera Effects" test buttons (Play).
* Scene view: the shot's frustum, and for the selected rig the dead zone (green) and soft zone (orange), the wall probe (green line, red where
  a wall pulls it in), a circle round a group, the confine box. Camera Rooms are yellow boxes. Select a rig and the **Camera Preview** inset
  shows what it sees (from where it would stand when nothing plays), with the zones drawn on the picture.

### Try it

See `docs/try_it.md`, entry `camera-rigs`, and the example scenes `examples/camera/camera_rigs.okayscene` and
`examples/camera/camera_2d_rooms.okayscene` (shipped in the Windows release under `examples\camera\`).

### Tests

Run on Linux, headless, a Release build with `-O0`, on the branch head. All pass.

| Test | What | Result |
|---|---|---|
| `camera_math` (new) | spring (30/60/144 fps same place, no overshoot, no NaN, a five second hitch), noise, shake presets (bounded, die away, same at any rate), blend curves, zones, look ahead (also under 50 Hz stepping), groups, confine, rooms, pixel snap, fade tracker | 4043 checks, 0 failures |
| `camera_rig` (new) | a shot faces its target; spring follow and 50 Hz stepped target at 30/60/144 fps; dead and soft zone; look ahead; group; wall collision (pull in, glide out, triggers and layers ignored, same at every rate); see-through fade; blends (cut, linear, ease, custom, interrupted, per-shot, forced); effects (presets, reduce motion, FOV kick, sprint FOV, hit flash); 2D zones, confine, rooms, pixel snap; NaN and zero or huge steps; save and load and old files; `ApplyStyle`; script builtins; the two example scenes played | 11768 checks, 0 failures |
| `cinemachine`, `camera_cine`, `script_camera`, `scene_cameras`, `layers_tags_cameras`, `camera_stack` | the existing camera tests | pass |
| `player`, `view_ground` | the third-person controller (uses the moved `SpringArm`) | pass |
| `save_coverage`, `duplicate_components`, `property_registry` | every component saves, loads and is in the registry (now with `CameraRoom`, the rigs) | pass |
| `vs_script_coverage` | every builtin has a visual-script node (the new `Camera.*` ones too; `docs/visual_script_coverage.md` and the coverage graph were regenerated) | pass |
| `examples_work` | every example scene plays 60 frames cleanly, the two new ones included | 86 scenes, 0 failed |

The editor (`editor/main.cpp`) was syntax-checked with the compiler (`-fsyntax-only`, no errors) but not linked or run.


### Behaviour changes to put in the release notes

* **Cameras now face what they look at.** A virtual camera with a Look At target (or an Orbit / FreeLook rig) used to aim the opposite way, because
  the shot turned its +Z toward the target but a camera looks down -Z. Saved scenes with such a camera now look at the target. Checked
  `examples/`: only the two new `examples/camera/` scenes use a Look At or Orbit virtual camera, so no older example changes. No opt-out.
  Changelog sentence: "Camera shots (virtual cameras) that look at something now really face it; before they pointed the other way."
* **The brain lets a playing cutscene have the camera** ("Let cutscenes take over", on by default) and blends back when it ends. Switch it off on the
  Cinemachine Brain to keep the old behaviour.
* The hit flash is added when drawing (`RenderSettings::vignettePulse`, never saved); collision and see-through measure from the real target, not the
  look-ahead point; things made see-through are restored whenever the setting, the shot or the component goes away, and a scene saved meanwhile stores
  the original alpha (`MeshRenderer::fadeBaseAlpha`, runtime only).

### Unverified

* Windows: nothing was run. The editor was not built or opened (Linux, no screen), so the menu entries, Inspector panels, gizmo and the
  preview inset were syntax-checked only and never looked at.
* GPU: nothing was drawn. The see-through fade assumes the renderers draw a mesh with alpha below 1 as transparent (the renderers' own code
  says so); it was checked on the number (the renderer's colour alpha), not on a picture.
* Mouse look of the Orbit style and the keys of the example scene need a person at the keyboard.

### Not done

* Dialogue lines have no camera field (only `Camera.BlendTo` from a dialogue event).
* The player and editor still draw only the main camera (the camera stack render path).
* Cinematic extras that were not asked for: depth-of-field follow focus, camera rails editor changes, split-screen.
* Pixel-perfect orthographic size (a whole-number zoom) exists as a function (`cam::PixelPerfectOrthoSize`) but no Camera switch uses it yet.

## Layers, tags and the camera stack

*Was `docs/layers_tags_cameras.md` until 2026-11-20.*

Feature map 1.3 (scene graph) and 4.8 (cameras). Test: `tests/test_layers_tags_cameras.cpp`.

### Layer collision matrix

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

### Tags

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
* The editor's tag dropdown, "Add" field and project settings file use `Tags` (see "Editor
  hook" below; done).

### Cameras

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

### What changed: project tag list, layer names, matrix presets (scene graph 1.3)

The engine side of the editor's tag list and layer names is complete, so wiring the editor is a
small change (see "Editor hook" below). Test: `tests/test_layers_tags_project.cpp`
(ctest `layers_tags_project`).

#### Layers (`okay/Scene/Layers.hpp`)

| Call | Notes |
| --- | --- |
| `Layers::IsValid(i)` | 0-31 |
| `Layers::NameToLayer(name)` | case and surrounding space ignored; also reads "Layer 7" and a bare number; -1 when unknown |
| `Layers::IsNamed(i)`, `Name(i)` | `Name` still falls back to "Layer N" |
| `Layers::SetName(i, name)` | refused (false) for a bad index, an invalid name (1-32 characters, no control characters, no leading / trailing space, no `,` or `!`, not only digits) or a name another layer has (case-insensitive) |
| `Layers::ClearName(i)` | back to "Layer N"; layer 0 always keeps a name (it can be renamed) |
| `Layers::ResetNames()`, `DefaultName(i)` | the names a new project has |
| `Layers::Mask({0, 5})`, `MaskToList(mask)` | the inverse of `MaskFromList`; a full mask is "" (blank = every layer), an empty one is "none" (display only) |

#### Tags (`okay/Scene/Tags.hpp`)

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

#### Project file (`okay/Scene/ProjectLists.hpp`)

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

**Editor hook (done).** The editor's own tag list (`g_project.customTags` and a preset array) is gone;
`editor/main.cpp` now reads and writes the same lists the engine does:

- `LoadProjectSettings` calls `okay::ProjectLists::Reset()` once the file opens, then
  `okay::ProjectLists::ApplyLine(k, v)` for every line first (it returns false for the keys the editor
  owns, so the `if / else if` chain follows), and `SaveProjectSettings` ends with
  `f << okay::ProjectLists::Write();` in place of the `tag=` loop. The tag lines are the same `tag=`
  lines the editor always wrote, so an existing `project.okayproj` loads unchanged (a duplicate or an
  invalid name is skipped, as `Tags::Add` does); `strict_tags=` and `layer_name_N=` are new optional lines.
- The object inspector's **Tag** dropdown lists "Untagged", `Tags::Builtin()` and `Tags::Custom()`. Picking
  one calls `go->SetTag(...)`. The "new tag..." field calls `Tags::Add`, saves the project file when the
  name is new, applies it, and shows `Tags::WhyInvalid(name)` in red when the name is refused (or "the tag
  list is full"). Right-click a custom tag: **Rename** (`Tags::Rename`, then `Scene::ReplaceTag(old, new)`
  on this scene, one undo step for the objects, the project file saved) or **Remove tag from list**
  (`Tags::Remove`; objects keep the tag). Other scenes and prefabs keep the old name until they are
  opened and the tag is renamed there too (`Scene::UnlistedTags()` finds tags a scene carries that the
  list lacks).
- The **Layer** dropdown on an object and the camera's **Culling Mask** list show
  `Layers::Name(i)`. Right-click a layer in either list to rename it: a text box with
  `Layers::DefaultName(i)` (or "Layer N") as its hint; Enter calls `Layers::SetName`, an empty box or
  "Reset to default" calls `Layers::ClearName` (or sets the default name back), a refused name shows why
  in red (`Layers::IsValidName`, or another layer has it), and the project file is saved. The layer
  mask text fields (`Ground Layers`, `Collision Layers`, trigger filters) already accept names, so they
  follow the renames.
- **Project Settings > Tags and Layers (wired, editor wiring pass).** A **Strict tags** switch (`Tags::Strict()`, saved as
  `strict_tags=1`); the tag list with each tag's object count in the open scene (`Scene::CountWithTag`), **Rename**
  (`Tags::Rename`, then `Scene::ReplaceTag` on this scene) and **Remove**, and an **Add tag** field (`Tags::Add`,
  `Tags::WhyInvalid` shown in red); a warning with an **Add them** button for tags the open scene carries that the list
  lacks (`Scene::UnlistedTags`; in red when Strict is on, since `SetTag` would refuse them); and the 32 layer names
  as text boxes (`Layers::SetName` / `ClearName`, the default name as the hint, **Reset** per layer and **Reset all layer
  names**). Every change is written to `project.okayproj` at once through `ProjectLists`.
- The Project Settings section was checked in a real editor window (an Xvfb display) in the editor wiring pass; the right-click menus in the lists were not re-checked. The data path it drives is covered by
  `layers_tags_project` and by `editor_hooks` (an old project file's `tag=` lines load into the list in
  order and write back the same, a rename keeps the tag's place and renames the objects).

#### Collision matrix helpers

```cpp
scene.ApplyLayerCollisionPreset(Scene::LayerCollisionPreset::SameLayerOnly);   // or Everything, Nothing
scene.SetLayerCollisionGroups(Layers::Mask({8}), Layers::Mask({9, 10}), false); // every pair between two sets
scene.SetLayerCollisionByName("Enemy", "Player", false);                        // false when a name is unknown
for (auto [a, b] : scene.DisabledLayerPairs()) { /* a <= b */ }
```

`Everything` is the default; `Nothing` turns every pair off (a layer ignores itself too);
`SameLayerOnly` keeps only (n, n). The matrix is still saved with the scene (`layermatrix`).

#### Queries

`Scene::FindGameObjectsOnLayer(layer)`, `CountOnLayer(layer)`, `CountWithTag(tag)`,
`FindGameObjectsWithName(name)`; `GameObject::SetLayer(layer, includeChildren = false)` (false and
nothing changed for a layer outside 0-31), `GameObject::CompareTag(tag)`.

#### Validation

`Scene::Validate` / `SceneSerializer::Validate` report two more kinds: `BadLayer` (a `layer`
outside 0-31 in the file; the loader used it modulo 32 and now stores that value) and
`UnknownTag` (an object carries a tag that is not in the list; reported only when
`Tags::Strict()` is on, since free tags are normal otherwise).

### What changed: tags and layers, fourth pass (1.3)

Test: `scene_graph_wave4` (the rest of this pass is in `scene_hierarchy_search.md`).

* `Scene::FindWithTag` checks only the first entry of the tag's list against what that object carries now (it used to check
  every entry on every call, so a lookup under a tag that 10000 objects carry was a 10000-step loop). `FindGameObjectsWithTag`
  and `CountWithTag` still check every entry they return or count. A tag changed through `go->tag = ...` (or any of the
  `TagString` calls) is seen by the next lookup, as before.
* Checked with 10000 objects and found right: tag lookup after `go->tag = ...`, after `Scene::ReplaceTag`
  (rename, untag, a refused name), and after a destroy; `FindGameObjectsOnLayer` / `CountOnLayer` on every layer;
  `ProjectLists::Apply` with 10000 `tag=` and `layer_name_N=` lines (the tag list stops at `Tags::kMaxCustom`, the layer
  lines that name a layer past 31 are skipped, nothing loops).
* `Scene::DestroyChildren(go)`, `GameObject::GetComponentsInParent<T>()`, `Scene::Stats()` and the other additions are listed in
  `scene_hierarchy_search.md`.

### What changed: tags and layers, fifth pass (1.3)

Tests: `scene_graph_wave5` (suite "tag and layer names in other languages"), `object_pool_wave5`.

* **Length counts characters.** `Tags::IsValidName` allowed 64 bytes and `Layers::IsValidName` 32, so a Japanese tag of 22
  characters or a Cyrillic layer name of 11 was refused although the messages said "characters". Both limits are now counted in
  characters (UTF-8; `Tags::CharCount`), and a name is still at most four bytes per character, so a run of stray continuation bytes
  cannot make a name without end. A name that is not valid UTF-8 is accepted as before (existing project files keep loading).
* **Layer names compare without regard to case in other scripts too.** `Layers::NameToLayer`, `Layers::SetName` (a second layer
  cannot take a name that differs only in case) and `Layers::MaskFromList` fold case with `Layers::Fold` / `Layers::FoldCode`:
  ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic. "ÉNEMY" finds the layer called "énemy". Other scripts (and "E" against
  "É") are compared as they are. Names are not normalized: an accented letter written as one code point and as two do not match.
* Checked and found right: 10000 objects each carrying a different tag in another language (lookups, `CountWithTag`,
  `UnlistedTags`, `ReplaceTag`), a project file with such layer names written and read back by `ProjectLists`, tags that differ only
  in case (two tags), names that are not valid UTF-8.
* `Scene::FindGameObjectsOnLayer(layer)` / `CountOnLayer(layer)` are the "find objects by layer" calls (no second name was added).
