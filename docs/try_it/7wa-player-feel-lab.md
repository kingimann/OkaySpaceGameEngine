---
id: 7wa
row: | 7wa | Player feel lab: five controller presets (Classic, Modern, Arcade, Realistic, Shooter), stick look ramp, F3 systems page | `examples\player\player_feel_lab.okayscene`, Play, press 1 to 5, press F3 twice | New in `claude/player-feel`; tested headless on Linux, never run on Windows |
---

## 7wa. Player feel lab: compare five controller feels, and see which system costs the frame

Where: `examples\player\player_feel_lab.okayscene`. Open it in `Tools\OkayEngine.exe` (**File > Open...**, **Play**) or in
`Tools\OkaySpacePlayer.exe`. You start in third person at the start sign. **WASD** walk, **Shift** run, **Space** jump, **Ctrl** crouch,
**V** switches to the eyes and back.

**Switch the feel.** Press **1 Classic, 2 Modern, 3 Arcade, 4 Realistic, 5 Shooter**. The sign at the start changes to
"PRESET: <name>", and **F3** shows "feel preset: <name>" under the player lines. Walk the stations after each pick:

| Station | Try | You should see (numbers are from the headless tests, `tests/test_player_feel.cpp`) |
|---|---|---|
| A Run track, markers every 5 m | hold W + Shift, let go at the red line | Arcade stops in about 0.14 m, Classic 0.27 m, Realistic 0.50 m (it also takes longer to get going) |
| B Slalom | weave through the posts | Arcade and Shooter turn sharply; Realistic turns slowly (third person turn speed) |
| C Gaps 2, 3, 4 m | run and jump | Arcade has a double jump, Realistic has the least air steering (about 1.0 m/s sideways against 2.6 for Arcade). A miss drops into the pit and returns you to the start |
| D Towers 2, 4, 8 m | climb the stairs, step off | the camera dips on landing (Realistic most, Arcade least); the 8 m drop is a hard landing (F3 counts them) |
| E Corridor | walk through in third person | the camera pulls in at the walls and eases back out |

**In your own scene.** Select the player, the **Player Controller** panel, **Controller Preset** drop-down: one pick sets speeds,
acceleration, stopping, air control, jump timing, head bob (first person) or turn speed and camera follow (third person). Edit any
value afterwards; the scene saves the numbers. The same panel has **Stick Look Ramp** (gamepad camera acceleration: seconds from 45%
to full look speed at full stick; 0 = off, the default) and, on **Player View**, **Preset Hotkeys (1-5)** (off by default; the lab
has it on; it needs the player to have both views).

**Systems page (F3, second press).** In `OkaySpacePlayer.exe` press F3 once for the frame numbers, again for **Systems**:
milliseconds per system (Physics, Scripts, Animation, Audio, UI, Render, Terrain, Other CPU, Idle/wait) averaged over the last 120
frames with their peaks, and a graph of the last 120 frame times. A third press closes it. In the editor, **Game view > Stats overlay**
shows the same rows while playing (Audio and Render rows are filled by the player only; the editor shows UI and the scene's own
scopes). It measures only while the page is open.

**Compare with before.** `set OKAY_OLD_PLAYER=1` brings back the old player behaviour. `set OKAY_OLD_SCENE_QUERIES=1` brings back
the old scene-wide searches (see the numbers below).

**Checks run here (Linux, headless):** `tests/test_player_feel.cpp` (127 checks: presets, saved, hotkeys, stick ramp, camera
steadiness at 30/60/144/240 fps, eye bob range, foot sliding of the real controller for all presets walking and running, system
rows), `tests/test_player_feel_lab.cpp` (the shipped scene is what the code writes; 1-5 switch the preset), and the player,
controller, net prediction, character and locomotion tests. Frame numbers of the busy scene: `docs/player_audit.md` (section
"Busy scene").
**Not seen:** Windows, a window, the Inspector drop-down, the F3 systems page on a screen, the lab scene on a screen, gamepad
feel with a real pad, whether the preset numbers feel right (they were chosen by measurement and by reasoning, not by playing).
