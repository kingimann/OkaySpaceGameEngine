---
id: 7w
row: | 7w | Player obstacle course: slopes, stairs, steps, moving platforms, water, voxel hill, with the player numbers on F3 | `examples\player\player_obstacle_course.okayscene`, Play, press F3 | New in this branch (`claude/player-deep`); tested headless on Linux, never run on Windows |
---

## 7w. Player obstacle course and the player's own numbers

Where: `examples\player\player_obstacle_course.okayscene` (next to the other examples, see "Where the files are"). Open it in
`Tools\OkayEngine.exe` with **File > Open...** and press **Play**, or open it in `Tools\OkaySpacePlayer.exe`. You start on the ground
with a third-person player. **WASD** walk, **Shift** run, **Space** jump, **Ctrl** crouch, **V** switches to the eyes and back.
**Press F3**: below the frame numbers you see the player's lines (grounded or in the air, slope in degrees against the limit,
speed, velocity, the ground normal, what you stand on). In the editor the same lines are in the Game view stats.

Walk -Z (straight ahead) past the signs. Each sign says what to try; what you should see:

| Station | Try | You should see |
|---|---|---|
| 1 Ramps 15, 30, 45, 55, 65 degrees | walk up each, stop half way up | 15 to 45: you walk up and stand still on it with no creeping (F3: "held on slope"). 55 and 65: you slide back down; F3 says TOO STEEP |
| 2 Stairs | walk up, turn, walk down | you stay on the steps going down (F3 stays "grounded"), no hopping |
| 3 Steps 0.10, 0.25, 0.35, 0.60 m | walk into each | the first three you climb in one go, the 0.60 m block stops you |
| 4 Plank over a gap | walk along it, step off the side | you fall into the gap, nothing sticks |
| 5 Tunnel 1.3 m | crouch (Ctrl), go in, press Ctrl again inside | you fit when crouched; standing up inside is refused until you are out |
| 6 Moving platform and turning bridge | stand on the platform, jump off sideways | you ride it with no slipping; jumping off keeps the platform's speed; the bridge carries you round |
| 7 Thin wall (2 cm) | hold Shift and run into it | you stop; you do not get through |
| 8 Water | walk into the shallow end, then the deep end | shallow: you wade; deep: you swim (Space up, Ctrl down). (Swim into the stone edge on the east side to climb out.) |
| 9 Voxel terrain patch | walk over the hill and the hole | you follow the ground without bouncing |
| 10 Ladder tower | walk into the ladder and hold W; let go; press Space | you climb at 2.5 m/s, hang still when you let go, Space jumps off; the jump down from the top is a "hard landing" (F3 shows "last landing ... hard landings N") |
| 11 Pool bank (east edge of the pool) | swim to the stone edge and keep pushing W | you climb out onto the bank (before, you stayed in the water) |
| Variable jump (the course player has it on) | tap Space, then hold Space | a tap is a short hop, holding is the full jump. Off in a fresh player (Inspector: Variable Jump) |
| Gizmo | in the editor select the player | a blue ring at the feet, four orange spokes rising at the Slope Limit, a green ring at the Step Offset height (Scene view, Gizmos on) |

**Compare with the old player**: close the game, set the environment variable `OKAY_OLD_PLAYER=1` (for example in a command
prompt: `set OKAY_OLD_PLAYER=1` then start `OkaySpacePlayer.exe` from that prompt, or start the editor from it), and walk the same
stations. F3 then adds "[old player path]" to the first player line. With it set: standing on ramps 25 to 49 degrees creeps, the
stairs down hop, a jump from the platform loses its speed, and every frame costs more (see `docs/player_audit.md`).

Settings you can now change and that are saved: select the player, the **Player Controller** panel, **Step Offset** and **Slope
Limit** (first and third person).

**Pictures** (headless software render, flat shading, no signs or water shine, made by `tests/render_course.cpp`): a picture folder (pictures are no longer stored in the repository) (`02_ramps.png`, `07_f3_player_lines_on_30_degree_ramp.png` shows the F3 lines as the 8x8 font draws them; the real F3 panel was not seen).

**Checks run here (Linux, headless)**: `tests/test_player_deep.cpp` (860 checks, each fails with `OKAY_OLD_PLAYER=1`),
`tests/test_player_course.cpp` (the shipped file is what the code writes, and a player walks stations 1, 3, 7), and the older player,
controller, stance, character and multiplayer prediction tests: all pass. Frame numbers: `docs/player_audit.md`.
**Not seen:** Windows, a window, a picture of the course (none was made), the F3 text on screen, the Inspector fields, real feel.
