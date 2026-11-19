---
id: pt1
row: | pt1 | Physics toys: Conveyor Belt, Force Field, Gravity Zone, Turntable, Moving Platform, Explosion, Breakable | Open `examples\physics_toys\physics_toys.okayscene` and press Play; Add Component > Physics 3D | Tested headless on Linux; menu entry under GameObject not built |
---
## pt1. Physics toys: ready-made physics for no-code games

**What it is.** Small components you add, tune with plain Inspector numbers and press Play: **Conveyor Belt** (carries what stands on it), **Force Field** (attract, repel, gravity well, whirlpool;
turn on and off), **Gravity Zone** (a box with its own gravity: floating, moon), **Turntable** (a physical round plate that carries things), **Moving Platform** (glides through up to four points, riders go along),
**Explosion** (a shove, by timer or from script) and **Breakable** (breaks into pieces when hit hard). The jump pad already existed (Add Component > Gameplay > Jump Pad). Details: [physics_toys.md](../physics_toys.md).

**Steps.**
1. **File > Open Scene**, pick `examples\physics_toys\physics_toys.okayscene`, press **Play**. Walk along the row of ten stations (WASD, mouse); each has a text label.
2. Or add one yourself: make a cube, give it a **Box Collider 3D** and a static **Rigidbody 3D**, then **Add Component > Physics 3D > Conveyor Belt**; drop a crate on it.
3. Change numbers while it plays only in the Inspector fields (Speed, Strength, Radius, Gravity...): each has a plain tooltip.

**What you should see.** 1 crates ride the belt; 2 a ball bounces on the green pad; 3 balls fall to the blue attractor and run from the orange repeller; 4 balls fall into the purple well and swirl round the teal whirlpool;
5 a ball floats in the zero-g zone, drifts down slowly in the moon zone and falls normally outside; 6 crates are carried round the turntable; 7 a crate rides the platform; 8 after 3 seconds the bomb goes off and the breakable crates burst into pieces
that vanish after a few seconds; 9 a pendulum and a rope swing; 10 a ball bounces high on the trampoline patch, a crate slides far on ice and stops quickly on the sticky patch.

**Status.** Tested headless on Linux (`tests/test_physics_toys.cpp`: every toy measured, saved and loaded back; `tests/test_physics_examples.cpp`). Not built: the GameObject > Physics Toys menu (presets exist as code in
`Components/PhysicsToyPresets.hpp` and are used by the example scene), Visual Script nodes for the toys (a script can switch a Force Field by property: `ForceField.active`), One-Way Platform, Rope/Chain component, Vehicle wheel.
Unverified: Windows and the editor on a screen.
