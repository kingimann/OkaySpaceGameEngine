---
id: input
row: | input | Input upgrade: input playground, rumble patterns, vibration setting, key positions (AZERTY) | Editor: File > Open, `examples\input\input_playground.okayscene`, then Play | Tested headless on Linux; nothing run on Windows or with a real controller or AZERTY keyboard |
---
## input. Input upgrade: playground, rumble patterns, vibration setting, key positions

**What it is.** One scene with eight stations (devices, timing, chords, curves, rebinding, two players, recording) that shows the input system working. New in this step: the vibration setting now switches off all rumble (not only patterns), and a switch to read letters by key position so AZERTY players keep WASD under the same fingers.

**Steps.**
1. Open the editor from the Windows release. Click **File > Open** and choose `examples\input\input_playground.okayscene` (the folder sits beside the editor).
2. Press **Play**. Press keys and move the mouse: station A shows what the game sees. Station F ("Players") is the one for this change.
3. With a controller plugged in: press **H** (heartbeat shake on pad 1) and **J** (pulse on pad 2).
4. Press **0**: the line "Rumble strength" goes from 1 to 0 and the controller stops shaking; press H again, nothing shakes. Press **0** again to turn it back on.
5. Press **K**: the line "Letters read by key position" goes from false to true. On an AZERTY keyboard, W A S D movement now works on the Z Q S D keys (the US places). Press K again to go back.
6. In a built game the same switches are the script calls `haptic_strength(0..1)` and `key_positions(true/false)`; both are kept for the player.

**What you should see.** The station F text changes as described. Without a controller you still see the strength and key-position lines change.

**Status.** Tested headless on Linux (`input_deep`, `input_playground`; the playground test presses H, 0 and K and renders a software picture). Unverified: Windows, a real controller (rumble was only checked as the request the host takes), a real AZERTY keyboard. No Settings-screen toggle yet.
