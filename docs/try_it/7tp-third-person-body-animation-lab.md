---
id: 7tp
row: | 7tp | Third person body animation lab: eight-way walk, stop step, chest follows the look, held items, camera | `examples\player\third_person_anim_lab.okayscene`, Play | New in `claude/player-tp-anim-1`; tested headless on Linux, never run on Windows |
---

## 7tp. Third person body animation lab

Where: `examples\player\third_person_anim_lab.okayscene` (in the release zip and installer under `examples\player`). Open it in
`Tools\OkayEngine.exe` (**File > Open...**, then **Play**) or run it in `Tools\OkaySpacePlayer.exe`. You start in third person
behind the character at the start, with a ring of eight yellow markers around you (the sign names the directions).

Keys: **WASD** walk, **Shift** run, **Ctrl** crouch, **Space** jump, **right mouse** aim (hold), **G** pick up the nearest item
(hold G and let go to throw it).

| Try | What to do | What you should see (numbers are from the headless test `tests/test_player_tp_anim.cpp`) |
|---|---|---|
| A. Eight directions | Hold the right mouse button so the body faces the camera, then walk toward each marker with W, W+D, D, S+D, S, S+A, A, W+A | The legs turn toward the direction you travel; going sideways or backwards the arms swing about half as far as forward and the chest leans a few degrees into a sidestep; the feet do not skate (planted-foot speed is under 30% of the ground speed in all eight directions) |
| B. Stopping | Hold W + Shift down the track, let go at the red line | Before: the planted foot slid back under the body (about 0.6 m for 0.35 m of travel). Now the stride is held while you slow, then the foot that is in the air steps in and the other foot steps after it. About half a second to stand still |
| C. Chest follows the look | Hold the right mouse, move the mouse up and down and sideways while standing and while walking | The chest and arms turn toward where you look (45% of it) and the head the rest; the hips and legs keep the walking direction |
| D. Held items | Stand at the bench, press G near the pistol, the rifle, the mug, the crate, the sword (throw each one first: hold G, let go) | Each item is held differently: pistol in one hand, rifle and crate in both hands, mug from underneath, sword in one hand; hands stay outside the torso |
| E. Camera | Back up toward the wall behind the start; hold the right mouse | The camera pulls in at the wall and eases out; right mouse moves it close over the shoulder |

**Turn the new look off to compare:** the chest follow can be turned off by setting `Character.upperBodyAim` to 0 (code or script);
the old head-only look comes back. The stop step has no switch (it only shows on the built-in body, not on an imported rig).

**Checks run here (Linux, headless):** `tests/test_player_tp_anim.cpp` (eight-direction slide, pose continuity through the state
ladder, stop from a run, turn in place, eight weights sum to one, chest aim, held poses, camera, the lab scene file),
`test_locomotion`, `test_gait_continuity`, `test_player_feel` and the animation tests. **Not seen:** Windows, a window, the lab on a
screen, a real mouse or gamepad, remote players in a real network game (the look is in the network message, the stop step on a
remote copy was not measured), imported rigs.
