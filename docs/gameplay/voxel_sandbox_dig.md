# Sandbox digging in first and third person

Written for the voxel Sandbox (`Components/VoxelDigger`, `Components/VoxelAim`, `Scene/SceneTemplates.hpp` `VoxelSandbox`).
The ray march is our own: a fixed-step walk along the view line against the density field (no outside code).

## What the digger does

The terrain is a smooth density field (marching cubes), not blocks. A "hit" is the first point along the ray where the
density passes the surface value. Dig takes ground away inside a brush (sphere, cube, cylinder or plane), Add puts it
back at the air point in front of the surface. Materials, hardness and tool power already decide how fast a dig is
(`docs/rendering/terrain_water.md`); a material harder than the tool cannot be dug and now shows a red marker.

## How the aim works (`VoxelAimFind`)

1. The ray is the main camera's view line (screen centre, where the crosshair is). Nothing starts at the feet or head.
2. **First person:** the camera is at the eyes, so the ray starts at the camera.
3. **Third person:** the camera is more than 0.8 m from the eyes. The ray starts where the view line passes closest to
   the character, so ground between the lens and the character is ignored.
4. **Reach** (`VoxelDigger.reach`, 12 by default, 14 in the Sandbox) is measured from the character's eyes in both views.
5. A hit inside the character's body (capsule 0.45 m radius, 1.8 m tall) is skipped. With the eyes inside solid ground
   nothing is dug (`HeadInGround`).
6. **Walls:** the line from the eyes to the hit must be clear. Aiming over a wall at ground behind it is refused
   (`Blocked`), so a high third-person camera cannot dig through a wall.
7. With no player in the scene (or `playerAim` off) the ray starts at the camera and `range` is the only limit, as before.

The player is found from the camera's parents (First Person, Third Person or Player Controller), else the first Player
Controller in the scene. `VoxelDigger.LastAim()` returns the last result (point, face normal, distances, why it missed).

## Controls

| Action (rebindable) | Default | Does |
|---|---|---|
| `Dig` | none of its own: follows `Fire` (left mouse, RT) until rebound | the digger's main action |
| `Place` | none of its own: follows `Aim` (right mouse, LT) until rebound | the opposite action (adds ground) |
| `PickBlock` | T, right stick click | the material under the crosshair becomes the build material |
| `HotbarNext` / `HotbarPrev` | RB / LB | next / previous build material (brush size when the terrain has no materials) |
| number keys 1-9, wheel | | pick / step the same hotbar |

The actions are in `InputActions` (so they show in the remap screens and are saved in Prefs). The old raw buttons
(`button`, `addButton`, `key`) still work until the matching action is rebound. (Dig and Place have no default keys of their
own so the remap screen does not list shared keys as conflicts; rebind them to give the digger its own keys.) `useActions` and `hotbarKeys` switch
these off; `button < 0` / `addButton < 0` still mean "no such button".

## Feedback

The orange hollow marker sits at the aimed point in both views (at the air point when building). While a dig works
through ground it shrinks and turns white (`StrokeProgress()`, 0..1); if the material is harder than the tool it is
red (`AimBlocked()`). `showProgress` turns this off. The Sandbox also gets a crosshair dot and a help line.

## Sandbox template

`Templates::VoxelSandbox` uses Player Controller View = Both, starting in first person; V (or Back) switches to the
orbit. `examples/voxel/*.okayscene` are written from it (`OKAY_WRITE_VOXEL_EXAMPLES=1 test_voxel_examples`).
How to try it: `docs/try_it.md` 7zsd.

## Tests

`tests/test_voxel_aim.cpp` (`voxel_aim`): ray start and reach in both views, wall, body and head rules, a real Player
Controller in First and Third view digging a known spot, action rebinding, hotbar, pick block, progress, scene save.
