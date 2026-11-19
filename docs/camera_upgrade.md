# Camera upgrade

Owner request: "improve the camera system". Branch `claude/camera-2`. This page says what the engine already had, what was
missing, what was added (inside the existing parts, not as a second camera system), how to try it, and what was and was not
checked. Everything here was checked **headless on Linux only** (numbers over simulated frames). Nothing was run on Windows or
on a screen; see "Unverified".

## Existing / Gap

| Area | Existing (before this change) | Gap | What was done |
|---|---|---|---|
| Camera component | `Components/Camera`: perspective and orthographic, physical lens, lens shift, dutch, zoom, culling mask, viewport rect, clear flags and the camera stack (`Scene::CameraStack`, `Render/CameraStack.hpp`), render targets, trauma shake, `ZoomTo`, pixel-perfect snapping in `EyeMatrix` | Shake was one trauma blob (no presets, one noise made from two sines); no FOV kick or held FOV change; no hit flash hook; pixel snapping had no test | Added `ShakePreset` (Impact, Explosion, Footstep, Handheld), `AddShake`, `SetHandheld`, `KickFov`, `SetFovOffset`, `HitPulse` on the same component. The trauma shake is kept as it was. Pixel snapping is now tested (it was already correct) |
| Simple chase camera | `Components/CameraFollow`: exponential ease toward a named object | Not touched. Branch `claude2/camera-follow-fixes` (Claude 2) already fixes its NaN and world-space problems, so it is left alone to avoid a conflict | Nothing |
| Shots and brain | `VirtualCamera` + `CinemachineBrain`: priority, Follow and Look At by name, binding mode, exponential damping, angular dead zone, FOV, dutch, confiner box, handheld noise and impulse, FreeLook orbit, tracked dolly. Brain: blend time and an ease on/off switch | (a) The shot aimed the **opposite way** (it rotated +Z to the target; a camera looks down -Z). (b) damping was an exponential ease only (no velocity, stops dead). (c) no screen dead zone / soft zone (only an angle), no look ahead, no groups, no collision, no see-through. (d) blends froze the shot being left, an interrupted blend was not tested, only two curves (on/off), no per-shot blend. (e) no way for a script to cut to a shot. (f) the brain fought a playing cutscene and popped when it ended | All fixed inside `VirtualCamera` / `CinemachineBrain` (see "What is in it") |
| Third-person controller cameras | `PlayerCamera.hpp` (`PlaceThirdPersonOrbit`), `ControllerFeel.hpp`: `FollowPivot`, `LookSmoother`, `SpringArm` (sphere sweep, triggers ignored), `FovKick` for sprint and aim FOV, first-person camera | `SpringArm` lived in the controller header, so the shot rigs could not use it | `SpringArm` moved to `Components/CameraCollision.hpp` (same code, ControllerFeel includes it) and the rig uses it. Controllers unchanged. Sprint FOV for a rig is `Camera.SetFovOffset` |
| Camera stack | `CameraStack.hpp`: ordered passes, viewport rect, clear flags, composite | The player and editor still draw only the main camera (`docs/layers_tags_cameras.md`) | Not done (render path, outside this change) |
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

## What is in it (and what each file was written from)

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

## Editor

* Add Component > Camera: **Camera Rig** (a Follow rig, ready to use, and the brain is put on the main camera) and **Camera Room**.
* GameObject > Camera (and the Hierarchy Create menu): **Camera Rig: Follow / Orbit / Look At** (select the object to follow first), **Camera Room (2D)**.
* Inspector, Virtual Camera: new sections Camera Rig (style buttons, smoothing style), Framing, Group, 2D camera, Walls, Blend into this shot, each
  with plain tooltips. Cinemachine Brain: blend style, custom curve with a small drawing. Camera: "Camera Effects" test buttons (Play).
* Scene view: the shot's frustum, and for the selected rig the dead zone (green) and soft zone (orange), the wall probe (green line, red where
  a wall pulls it in), a circle round a group, the confine box. Camera Rooms are yellow boxes. Select a rig and the **Camera Preview** inset
  shows what it sees (from where it would stand when nothing plays), with the zones drawn on the picture.

## Try it

See `docs/try_it.md`, entry `camera-rigs`, and the example scenes `examples/camera/camera_rigs.okayscene` and
`examples/camera/camera_2d_rooms.okayscene` (shipped in the Windows release under `examples\camera\`).

## Tests

RESULTS_PLACEHOLDER

## Unverified

* Windows: nothing was run. The editor was not built or opened (Linux, no screen), so the menu entries, Inspector panels, gizmo and the
  preview inset were compiled only if listed above and never looked at.
* GPU: nothing was drawn. The see-through fade assumes the renderers draw a mesh with alpha below 1 as transparent (the renderers' own code
  says so); it was checked on the number (the renderer's colour alpha), not on a picture.
* Mouse look of the Orbit style and the keys of the example scene need a person at the keyboard.

## Not done

* Dialogue lines have no camera field (only `Camera.BlendTo` from a dialogue event).
* The player and editor still draw only the main camera (the camera stack render path).
* Cinematic extras that were not asked for: depth-of-field follow focus, camera rails editor changes, split-screen.
* Pixel-perfect orthographic size (a whole-number zoom) exists as a function (`cam::PixelPerfectOrthoSize`) but no Camera switch uses it yet.
