---
id: pe1
row: | pe1 | Physics: Engine quality (our own physics engine core), ball and cone-twist joints with drives | Open `examples\physics_engine\physics_engine.okayscene` in `Tools\OkayEngine.exe` and press Play | Tested headless on Linux; the editor menus and the feel on Windows are unverified |
---
## pe1. Physics: the Engine quality, our own physics engine

**What it is.** A third physics quality, **Engine**, runs a 3D scene on our own physics engine core (written from papers,
no other engine's code): a tree that finds touching pairs fast, exact contacts for every shape, a solver that keeps tall
stacks still, fast bodies that never pass through thin walls, groups of bodies solved on every CPU core, and joints for
ragdolls (**Ball** and **Cone Twist** with a drive that holds a pose). Plain explanation and numbers:
[physics_core.md](physics/physics_core.md).

**How to reach it.**

1. Open the editor (`Tools\OkayEngine.exe`, or the launcher). **File > Open Scene**, pick
   `examples\physics_engine\physics_engine.okayscene` (it ships with the zip and the installer).
2. Press **Play** and wait about 10 seconds.
3. For many bodies, open `examples\physics_engine\physics_engine_5k.okayscene` (5,000 crates and balls poured into a pit)
   and press Play.
4. To use it in your own scene: **Project Settings > Physics**, "Physics quality of the open scene", choose **Engine**.
5. A ragdoll-style joint: select an object with a Rigidbody 3D (untick Freeze Rotation), **Add Component > Joint 3D**,
   set **Mode** to **Cone Twist** (or **Ball**), set **Anchor** to the point it turns about, tick **Use Drive**.

**What you should see.**

| Station | What to look for |
|---|---|
| 1 Tower and pyramid | 12 crates in a column and a pyramid of 36 stand still and stop moving (sleep) after a second or two. |
| 2 Limb chains | Three chains of blue limbs swing down from their left ends and stay joined; the third chain is powered and holds itself out sideways. |
| 3 Powered arm | A yellow 2 m arm on a post stays level: its ball joint's drive holds it up. |
| 4 Fast shots | Six small red balls fly at 60 m/s at a 4 cm wall and stop at it; none comes out the other side. |
| 5 Shapes | A capsule, cylinder, cone, hull and compound body fall and settle on the floor. |
| 5k scene | 5,000 bodies fall into the pit and settle; the frame rate stays usable on a 4-core PC (not measured on Windows). |

**If it looks wrong.** The joints in Ball / Cone Twist mode only turn and hold a pose in the Engine quality; in Classic
and Improved they are only a rod (the Console says so once). Unverified: the editor menus, the Inspector fields and the
look on Windows; everything above was checked headless on Linux by `physics_engine_mode` and `physics_examples`.
