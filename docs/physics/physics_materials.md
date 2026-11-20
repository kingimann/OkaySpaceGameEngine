# Physics Material: how a surface feels

A **Physics Material** is a named bundle that says how a surface feels. Give an object one (Add Component > Physics 3D > **Physics Surface**, then type the
material name in **Material**) and every collider on it, and on its children if you tick **Include Children**, feels that way.

## The fourteen built-in materials

They are always there, no file needed. Numbers: grip while still / grip while sliding / bounciness / density in kg per cubic metre.

| Name | Still | Sliding | Bounce | Density | Notes |
|---|---|---|---|---|---|
| Default | 0.60 | 0.50 | 0.00 | 1000 | what an object with no material uses |
| Ice | 0.05 | 0.02 | 0.05 | 917 | mixing: the smaller grip wins, so anything slides on it |
| Rubber | 1.20 | 1.00 | 0.60 | 1100 | mixing: the larger wins |
| Bouncy Ball | 0.80 | 0.60 | 0.92 | 1100 | |
| Metal | 0.50 | 0.40 | 0.20 | 7800 | |
| Wood | 0.55 | 0.45 | 0.30 | 600 | |
| Stone | 0.70 | 0.60 | 0.10 | 2600 | |
| Glass | 0.45 | 0.35 | 0.40 | 2500 | |
| Mud | 1.00 | 0.90 | 0.00 | 1700 | slows what touches it (damping 1.5, spin damping 3, rolling 0.3) |
| Sand | 0.80 | 0.70 | 0.00 | 1600 | damping 0.8 |
| Sticky | 2.50 | 2.00 | 0.00 | 1200 | grip on slopes x2 |
| Slippery | 0.02 | 0.01 | 0.00 | 1000 | |
| Trampoline | 0.60 | 0.50 | 0.97 | 400 | the larger bounce wins |
| Cloth | 0.80 | 0.70 | 0.02 | 300 | light damping |

Each also names an impact sound, a slide sound and a hit effect (`impact_wood`, `slide_ice`, `fx_sparks`...). They are names only: a script or an audio hook reads them; the engine
does not ship those sounds.

## Your own material: a `.okayphysmat` file

Plain text. The first line is the format name and version; every other line is `key value` (text values run to the end of the line). Unknown keys are skipped, a bad
number keeps the default, a newer format version is refused.

```
okayphysmat 1
name Gel
surface Gel
staticFriction 1.5
dynamicFriction 0.7
bounciness 0.35
frictionCombine Maximum
bounceCombine Minimum
density 1100
rollingFriction 0.2
linearDamping 0.5
angularDamping 1.5
slopeGrip 1.7
impactSound splat
slideSound squish
hitEffect fx_goo
```

Load it with `PhysicsMaterials::LoadFile(path)` or a whole folder with `PhysicsMaterials::ScanFolder(dir)`. A Physics Surface whose material name is not a preset scans the project's asset folder once by itself, in the editor and in the player, so a `.okayphysmat` file just works by name. A project material with the same name as a preset replaces it.

## What the physics does with it

* Sliding grip, bounciness and the combine modes go into the collider's existing fields (so the Classic step also feels them).
* Improved quality also uses: **still grip** (below 0.12 m/s sliding the still grip applies, so a crate stays on a slope that it would slide down once moving), rolling friction,
  damping and slope grip.
* **Mass from density**: tick it and the body's mass is density x the volume of its colliders (a 1 m cube of Metal is 7800 kg).
* A contact tells scripts the two materials: `Collision3D::surface` (the other side) and `ownSurface`.
* `PhysicsSurface::SetMaterial("Ice")`, or `AssignPhysicsMaterial(object, "Ice")`, changes it while the game runs. The property `PhysicsSurface.material` works from scripts too.
* Old scenes load unchanged: an object with no Physics Surface keeps its collider's own friction and bounciness exactly as before.

Written from textbook contact friction (a still and a sliding coefficient, still at least sliding), restitution and density; see [physics_upgrade.md](physics_upgrade.md).

## Not done

* The Inspector part (dropdown of presets and project materials, "+ New material", live sliders inside the collider's Physics Material section) is editor source that was **compiled-not-run**. Edited project materials are saved to `Materials/` under the asset folder.
* The character controller (`CharacterController3D`) and `VehicleController` now scale acceleration, braking and sideways grip by the ground material (`Physics/GroundGrip.hpp`; ice 0.04, rubber 2). Tested for the character (slides 7.5 m on ice, 0.24 m on plain ground); the vehicle change is not tested by a driving test.
* Other controllers (first/third person players) are not changed.
* Terrain / voxel material defaults.
* Visual Script nodes ("Make this object icy"): use the property `PhysicsSurface.material`.
