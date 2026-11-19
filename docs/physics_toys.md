# Physics toys: ready-made physics, no code

Add one from **Add Component > Physics 3D**, set a few numbers, press Play. Or open `examples\physics_toys\physics_toys.okayscene` (ten stations, each labelled).

| Toy | What it does | Main numbers |
|---|---|---|
| Conveyor Belt | carries bodies standing on its top (needs a Box Collider 3D on the same object) | Direction, Speed (negative = backwards), Grip, Reach |
| Force Field | pulls, pushes, a gravity well (inverse square) or a whirlpool, inside a radius; on and off | Mode, Falloff, Radius, Strength (acceleration), Core Radius, Active |
| Gravity Zone | a box of space with its own gravity (floating, moon) | Size, Gravity |
| Turntable | a physical plate turning at a steady speed that carries things (the older Spinner only turns the picture) | Axis, Degrees Per Second, Ramp Time |
| Moving Platform | glides through up to four points, pauses, eases, riders go along | Mode (Ping Pong, Loop), Point Count, Offsets, Speed, Pause, Easing |
| Explosion | a radial impulse (light things fly further), upward kick, a damage hook; by timer (`On Start`) or `Trigger()` from a script; `Explosion::At(scene, pos, force, radius)` for a one-off | Force, Radius, Upward Bias, Damage, On Start, Delay |
| Breakable | a crate that breaks into pieces x pieces x pieces smaller crates when hit harder than Break Speed; pieces vanish after Piece Life | Break Speed, Pieces, Piece Life, Burst Speed |

The Jump Pad already existed (Add Component > Gameplay > Jump Pad).

Code presets (`Components/PhysicsToyPresets.hpp`): `ConveyorBeltPreset`, `JumpPadPreset`, `ForceFieldPreset`, `GravityZonePreset`, `TurntablePreset`, `MovingPlatformPreset`, `BombPreset`,
`BreakableCratePreset`, `SurfacePatchPreset` (Ice, Sticky, Trampoline, Mud...), `PendulumPreset`, `RopePreset`, `WreckingBallPreset`, `DominoPackPreset`, `DoorPreset`. The editor menu
GameObject > 3D Object > Physics Toys calls them (editor code compiled-not-run).

All of them are saved in scenes and listed in the Inspector with plain tooltips (generic property list), work through property paths from scripts (for example `ForceField.active`), and
look at bodies with a physics query each fixed step. They do not touch water, buoyancy or wind.

Written from: this engine's `Physics3D::OverlapBox / OverlapSphere`, `Rigidbody3D` and `Scene`; plain vector maths.

## Not done

One-Way Platform (3D), Rope/Chain as a component (a preset builds one), Vehicle Wheel Collider and Car preset (a vehicle controller already exists), Ragdoll on hit, Glue and Ice surfaces as separate
components (use a Physics Surface material), Visual Script nodes and OkayScript built-ins for the toys.
