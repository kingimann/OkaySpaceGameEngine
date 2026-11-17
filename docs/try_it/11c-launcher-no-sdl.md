---
id: 11c
group: appendix
row: | 11c | Launcher with no SDL at all (off by default) | `OKAY_LAUNCHER_RENDERER=own` and `OKAY_PLATFORM_BACKEND=own`, then `OkaySpace.exe` (section "Launcher with no SDL at all") | Log says `launcher: no SDL`; voice calls and voice notes use our own audio (waveOut / waveIn); Linux/Xvfb only, unverified on Windows |
---
## Launcher with no SDL at all (branch `claude2/sdl-launcher-no-sdl`, not merged or released yet)

What changed (SDL removal, step 2d): with **both** switches below the launcher does not call SDL at all: its window, events,
drawing, monitor list and sound all come from our own code. It is **off by default**: with no variables nothing changes.

* **Switches.** `OKAY_LAUNCHER_RENDERER=own` and `OKAY_PLATFORM_BACKEND=own` (both; one alone still uses SDL for part of the work).
* **How you can tell it is on.** The footer says `(own renderer: opengl)`; started from a Command Prompt, the launcher prints
  `launcher: no SDL (own renderer, own platform backend)`.

Steps (Windows, `OkaySpace.exe` from the release zip):

1. Open a Command Prompt in the folder of `OkaySpace.exe` and run:
   `set OKAY_LAUNCHER_RENDERER=own`, `set OKAY_PLATFORM_BACKEND=own`, then `OkaySpace.exe`.
2. Expect the splash, then the Create page, as without the variables, and the line `launcher: no SDL ...` in the Command Prompt.
   On a 125% or 150% display the text should be sharp (per-monitor DPI awareness from our own code).
3. Monitors: open the chat in its own window, move it onto a second monitor, close the launcher, unplug (or switch off) that
   monitor, start the launcher again the same way and open the chat: it comes back in the middle of the main monitor, not off screen.
4. Sound, our own Windows audio (waveOut / waveIn): Settings > Voice: the microphone and speaker lists show your devices; the mic
   test shows the level moving when you talk. Record a voice note in a chat and play it back; play a voice note someone sent you.
   Start a voice call: the other side hears you and you hear them.
5. Everything from "Launcher: drawing with our own renderer" (typing, chat window, OkayUI page, taskbar flash) should work the same.
6. Close the launcher with the X and choose Exit: it closes with no error.

If the launcher cannot start this way (no OpenGL), it falls back to SDL by itself and says so in the Command Prompt. If a step
fails only with both variables set, that is our code: report the step.

**Status.** Tested on Linux only, under Xvfb (software OpenGL, ALSA's `null` audio device): `scripts/launcher_no_sdl_check.sh`
puts a trap in front of every SDL function (839) and shows that the launcher with both switches calls none of them, from start to
exit, while it shows the chat window (moved back on screen by the new monitor list), plays a voice note and holds a voice call;
`platform_display*` tests check the monitor list (one screen, a panel, a 150% setting, two monitors, two screens with Xinerama).
The Windows monitor list, DPI awareness and waveOut / waveIn sound passed a MinGW syntax check only. **Not run on Windows: steps
2 to 6 are unverified until you run them.**
