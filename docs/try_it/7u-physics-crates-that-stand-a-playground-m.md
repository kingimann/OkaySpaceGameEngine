---
id: 7u
row: | 7u | Physics: Improved quality, physics playground, materials, slider joint, debris emitter | Open `examples\physics\physics_playground.okayscene` in `Tools\OkayEngine.exe` and press Play | New in this change; the physics is tested headless, the editor menus and the feel are unverified |
---
## 7u. Physics: crates that stand, a playground, materials, a slider and debris

**What it is.** The physics got a second way to solve contacts, called **Improved** (the old one is **Classic**; old scenes keep it, new scenes
get Improved). Crates stack and stay put, a ball bounces and rolls to a stop, fast bullets stop at thin walls, resting things go to sleep
(cheap) and wake when what holds them up goes away. Also new: a physics material on every collider (icy or bouncy floors), a **Slider** joint, ropes that
stay near their length, joints in the physics debug view and a debris emitter. Plain explanation and the numbers: [physics.md](physics.md) and
[physics_plan.md](physics_plan.md). Code: `Physics/Physics3DImproved.inl`, `Physics/ContactSolver.hpp`, `Physics/BoxManifold.hpp`,
`Physics/DebrisPool.hpp`.

**How to reach it.**

1. Open the editor (`Tools\OkayEngine.exe`, or the launcher). Use **File > Open Scene** and pick `examples\physics\physics_playground.okayscene`
   (it ships next to the editor in the zip and the installer). The scene looks like a big grey floor with nine small stations.
2. Press **Play**. Everything starts at once. Wait about 10 seconds.
3. To compare with the old physics: stop Play, open **Project Settings > Physics**, under "Physics quality of the open scene" choose
   **Classic**, press Play again. Change it back to **Improved**. (The choice is saved with the scene.)

**What you should see (Improved).**

| Station | What to look for |
|---|---|
| 1 Tower | Five crates stand in a column, hardly move, and (after about a second) go to sleep. In Classic the tower leans and falls. |
| 2 Ball | A red ball rolls down a short ramp, bounces and rolls to a slow stop on the floor. |
| 3 Ramps | Three ramps at 20 degrees, the same green crate on each: the plain and the rubber ramp hold it, the icy (light blue) one lets it slide off. The ramps' friction is set per collider (Physics Material). |
| 4 Bullet | A small red ball fires at 60 m/s at a 5 cm thin wall and stops at it. In Classic it goes through. |
| 5 Heavy and light | A 100 kg steel crate is pushed at 5 m/s and shoves the 1 kg wooden crate ahead of it. |
| 6 Pads | The same blue ball dropped on a dark pad (dead) and a black rubber pad (bounces many times; the pad's own bounciness 0.92). |
| 7 Rope | Six balls on distance joints swing from a hook and hang about 5 m long, close to their real length. |
| 8 Slider | A blue drawer slides 3 m along x at 1 m/s and stops at its limit. |
| 9 Debris | A burst of small brown chunks every 1.5 seconds: they fly, land, go to sleep, shrink away. There are never more than 150. |

To see the joints and sleeping bodies: **View > Scene Overlays > Physics Debug > Show physics (Scene + Game)**. Colliders are green, sleeping
ones grey, contacts red, joints orange.

**Try the new settings yourself.**

* Select the **3 Ramp ice** object, find its collider in the Inspector, open **Physics Material**, tick **Own Friction** and set it to 2: the crate now holds.
* Select any crate, **Rigidbody 3D**: **Rolling Resistance** (0 = a ball rolls on for ever, 0.03 default). **Use Child Colliders** makes colliders on child objects part of the body.
* Select the **8 Drawer**, **Joint 3D** (mode **Slider**): change **Max Travel** or **Motor Speed**.
* Select **9 Debris emitter**: **Count**, **Interval**, **Max Active**, **Settled Life**.
* Add your own: **Add Component > Physics 3D > DebrisEmitter** on an empty object, then Play. In code, `DebrisPool` spawns chunks at your own moments (a digger, an explosion).

**Screenshots (headless Linux software renders of the same scene, not the editor window; camera and look differ from the editor):**

* [Start of Play](images/physics/playground_0.png), [after 1.2 s](images/physics/playground_1.png), [after 3 s](images/physics/playground_2.png), [after 8 s](images/physics/playground_3.png)

**Status.** Tested headless on Linux (`tests/test_physics_deep_*.cpp`: manifold, solver, bugs, improved, save, more; the existing physics, joint, hinge,
ragdoll, vehicle, destructible, controller and net-prediction tests stay green). The playground was played headless for 10 seconds and the
positions listed in the plan were read back. **Unverified:** the editor menus and Inspector rows on Windows (they were compiled, not opened), how the scene looks and feels in the real
editor or player, any real GPU. The 2D step has no Improved solver yet (the setting is saved but 2D plays as before).
