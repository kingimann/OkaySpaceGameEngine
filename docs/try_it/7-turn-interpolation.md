---
id: 7
row: | 7 | Turn interpolation | New Project > Third Person, Project Settings > Physics | Shipped |
---
## 7. Turn interpolation

**What it is.** Physics runs at a fixed rate (default 50 steps a second) and each frame draws a blend between steps. A
character that turns every frame in game code used to break that blend, so it moved unevenly. Now position stays smooth
and your turn is drawn as you set it.

**Steps.**
1. New Project > **Third Person**. **Edit > Project Settings...** > Physics: set **Steps per second** to 10 and tick
   **Smooth the motion**.
2. Play. Walk in a tight circle with W and the mouse (or A/D) at a high frame rate.
3. The character slides smoothly. Then untick **Smooth the motion**: at 10 steps a second the movement visibly
   steps. (The fix itself is that with smoothing on, turning no longer makes it stutter.)

**Status.** Tested on main: headless `turn_interpolation` and `controller_interpolation` tests (a real third person
controller at 30, 60 and 144 fps; the test failed on the old code and passes now). Unverified: the feel in the editor.
The before-and-after of the bug needs the old release to compare.
