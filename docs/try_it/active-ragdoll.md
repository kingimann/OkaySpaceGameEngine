---
id: active-ragdoll
row: | active-ragdoll | Active Ragdoll: a character that keeps its balance, stumbles, falls, braces and gets up | Editor: File > Open `examples\active_ragdoll\active_ragdoll.okayscene`, press Play, keys J L U O I F E T G K R P; Add Component > Active Ragdoll | Tests pass on Linux (`active_ragdoll`); the editor and the example were not run on Windows or on a screen |
---
## active-ragdoll. Active Ragdoll: a character that keeps its balance, stumbles, falls, braces and gets up

**What it is.** Our own "dynamic character" (written from published research, see `docs/gameplay/active_ragdoll.md`).
The character's body is simulated: its muscles follow the animation, it keeps its balance when pushed, takes steps to catch
itself, falls when the push is too big (arms out toward the ground, or hands to the head when falling backwards), lies still
for a moment and gets up again, face up or face down, then blends back into its animation.

**How to try it.**
1. In the editor, **File > Open...** and pick `examples\active_ragdoll\active_ragdoll.okayscene`. Press **Play**.
2. The dummy stands on a grey platform with a brown wall on its left (your right is the ledge). The text at the top shows its state.
3. Keys:
   - **J** / **L**: a small push left / right. It sways or takes a step and stays up. Pushed toward the wall it may grab it.
   - **U**: a hard shove toward the wall. **O**: a hard shove the other way: it stumbles off the ledge, falls to the ground,
     lies still and gets up down there.
   - **I**: a push away from you.
   - **F**: shoot it (each shot hits another spot: chest, leg, head, hips). A leg shot makes the leg go weak.
   - **E**: a blast next to it. It is thrown and falls.
   - **T** trip, **G** get up now, **K** go limp (dead), **R** revive, **P** powered on / off (always physical).
4. **You should see:** small pushes end with the dummy standing again within about a second; big ones end on the floor and,
   after about 1 second still, a get-up that takes about 2 seconds. The state text reads Animated, Balancing, Falling, Down,
   GettingUp, Blending.

**On your own character.** Select a character (the built-in Character or an imported humanoid) and use
**Add Component > Active Ragdoll (balance, stumble, fall, get up)**. With a Stat: Health on the same object, damage pushes the
struck part, death goes limp and revive gets up. Scripts: `active_ragdoll("knockdown")`, `active_ragdoll_push(0, 0, 60)`,
`active_ragdoll_shoot(...)`, `active_ragdoll_explode(...)`, `active_ragdoll_state()` (also visual-script nodes).

**Honest status.** Tested headless on Linux only (`ctest -R active_ragdoll`). Never run in a real editor window or on Windows.
Its limbs do not collide with other moving bodies yet, and the get-up uses a helper force (see the guide).
