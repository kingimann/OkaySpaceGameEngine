---
id: pm1
row: | pm1 | Physics Material: how a surface feels (14 presets, .okayphysmat files, Physics Surface component) | Open `examples\physics_materials\physics_materials.okayscene` and press Play; or select an object, Add Component > Physics 3D > Physics Surface | Tested headless on Linux; the Inspector rows were compiled, not opened |
---
## pm1. Physics Material: how a surface feels

**What it is.** A named material says how a surface feels: grip when still, grip when sliding, bounciness, how two surfaces mix, density, rolling friction, damping
(mud, sand), grip on slopes, and the sound, effect and surface names a hit should use. Fourteen are always there (Default, Ice, Rubber, Bouncy Ball, Metal, Wood, Stone,
Glass, Mud, Sand, Sticky, Slippery, Trampoline, Cloth). Plain words and the file format: [physics_materials.md](../physics/physics_materials.md).

**Steps.**
1. **File > Open Scene**, pick `examples\physics_materials\physics_materials.okayscene`, press **Play**.
2. Or on your own object: select it, **Add Component > Physics 3D > Physics Surface**, type a preset name in **Material** (for example `Ice`), press Play.
3. Make your own: write a text file `MyGel.okayphysmat` (see physics_materials.md for the lines), put it anywhere in the project folder; its name then works in **Material**.
4. Tick **Mass From Density** to let the body's mass come from the material's density times the size of its colliders.

**What you should see.** One row per preset. Left: a plain crate sits on a 20 degree ramp of that material: it slides away on Ice and Slippery, stays on Rubber and Sticky. Right: a ball is dropped
on a pad of that material: it bounces high on Trampoline and Bouncy Ball, hardly at all on Stone, Mud and Sand. Each row has a text label.

**Status.** Tested headless on Linux (`tests/test_physics_materials.cpp`: file round trip, presets, ice slides / rubber holds, still grip, bounce height, density to mass, change at run time,
contact reports the material pair, mud slows). Unverified: the editor (the Physics Surface Inspector row comes from the generic property list; the dropdown of presets and the "+ New material" button are
NOT built yet: type the name), Windows, the character controller and vehicles do not yet read the ground material.
