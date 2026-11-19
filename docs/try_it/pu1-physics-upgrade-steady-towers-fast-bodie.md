---
id: pu1
row: | pu1 | Physics upgrade: steady towers at any step rate, fast bodies, heavy ropes, callbacks that set speed | Open `examples\physics\physics_upgrade.okayscene` in `Tools\OkayEngine.exe` and press Play | Tested headless on Linux; nothing run on Windows or on a screen |
---
## pu1. Physics upgrade: steady towers, fast bodies, heavy ropes

**What it is.** The Improved physics now keeps a tower of ten crates standing at 30, 50, 60 and 144 steps a second (it fell over at 30 and
leaned at 50), a fast ball can no longer jump over a thin plate that can move, a 50 kg ball on a chain of light links hangs close to the chain's real
length, a breakable joint snaps without first stopping the body, and a speed set inside `OnCollisionEnter3D` (a jump pad, a blast) is kept. The
numbers before and after are in [physics_upgrade.md](../physics_upgrade.md).

**Steps.**
1. Open `Tools\OkayEngine.exe` (or the launcher), **File > Open Scene**, pick `examples\physics\physics_upgrade.okayscene` (it ships in the zip and the installer).
2. Press **Play**. Use the character (WASD, mouse) or just watch from the start position.
3. To compare step rates: **Project Settings > Physics > Steps per second**, set 30, press Play again, set 144, press Play again. The tower looks the same.

**What you should see.** Station 1: ten crates stand still and go to sleep after about two seconds. Station 2: a red ball rolls down a short ramp and topples
sixteen dominoes in a line. Station 3: a 50 kg wrecking ball swings on a chain into a wall of crates and scatters them. Station 4: the same crate on ramps of
Ice, Wood and Rubber: it slides on ice, holds on rubber. Station 5: a blue platform glides to and fro; walk onto it and ride.

**Status.** Tested headless on Linux (`tests/test_physics_upgrade.cpp`, `tests/test_physics_examples.cpp`): tower, tunnelling, ropes, joints, dominoes, wall, ramps.
Unverified: Windows, the editor on a screen, how it feels with the real character.
