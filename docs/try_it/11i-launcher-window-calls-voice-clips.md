---
id: 11i
group: appendix
row: | 11i | Launcher window calls through our platform layer; voice clips on our own WAV decoder | `OkaySpace.exe`, chat voice messages | Tested on Linux only |
---
## Launcher: window calls through our platform layer, voice clips on our own decoder as an option (branch `claude2/launcher-own-wav-window`, not merged or released yet)

What changed (SDL removal, stage A):

* **Voice messages.** By default the launcher still reads a voice message's WAV file with SDL (`SDL_LoadWAV`), as
  before. To try our own WAV decoder instead (`launcher/VoiceClipLoad.hpp` over `okay::wav`), start the launcher from a
  Command Prompt after `set OKAY_OWN_WAV=1` (`set OKAY_OWN_WAV=0`, or no variable, is SDL). Playback itself is unchanged
  either way.
* **Window calls.** Showing, raising, restoring, the minimised / hidden / focused checks, size, position, window id and
  minimum size of the launcher window and the separate chat window now go through `okay::platform` (a
  `WrapSdl2Window` view over the same SDL window). The window is still made by SDL, so nothing should look different.

Steps:

1. Start `OkaySpace.exe`. The splash shows, then the launcher. Try to make the window smaller than about 720 x 480: it
   stops there, as before.
2. Sign in. Open a chat, record a voice message (hold the mic button), send it. Press play on it: you hear it, the
   waveform bars show its loudness (not even bars), the time counts, and **1.5x / 2x** speed it up without a higher
   voice. Play a voice message someone else sent, too.
2a. Our own decoder: close the launcher, open a Command Prompt in its folder, run `set OKAY_OWN_WAV=1` and then
   `OkaySpace.exe`. Play the same voice messages: they should sound and look exactly the same as in step 2.
3. Set chat to open in its own window (chat settings), open it. It cannot be made smaller than about 520 x 380. Close
   and reopen the launcher: the chat window comes back where you left it.
4. Minimise the launcher with the chat window open: the chat window keeps working. Restore the launcher from the
   taskbar.
5. In a game, press **Shift+Tab** (Invite friends): the launcher comes to the front, restored if it was minimised.
6. With the launcher minimised or in the background, let an update hint or an incoming call arrive: the taskbar
   button flashes.
7. Close the launcher and start it again: it opens at the size you left it.

If a voice message does not play or plays as noise with `OKAY_OWN_WAV=1` only, that is our decoder; if a window step
behaves differently from the last release, that is the window change: report the step.

**Status.** Tested on Linux only: `test_launcher_wav_window` (every WAV in the repository and five made-up ones read by
SDL and by our reader give the same format and length; the launcher picks SDL by default and with `OKAY_OWN_WAV=0`
and ours with `OKAY_OWN_WAV=1`, and the SDL path gives exactly what the launcher played before and byte-for-byte the same samples, except 8-bit and 24-bit files,
which are within 2 steps of 32,768 because the old launcher code truncated where ours rounds; every truncation of a file fails or reads less; the
window view answers size, position, minimum size, id and the hidden / minimised / focused state like SDL, under SDL's
dummy video driver), the launcher started under Xvfb with a screenshot, and `launcher/main.cpp` passed a MinGW (Windows)
syntax check. Not run on Windows: the steps above are unverified until you run them.
