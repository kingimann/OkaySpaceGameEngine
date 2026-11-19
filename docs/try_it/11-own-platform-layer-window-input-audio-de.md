---
id: 11
group: switches
row: | 11 | Own platform layer (off by default) | `OKAY_PLATFORM_BACKEND=own` | Unverified on Windows, never run there |
---
### 11. Own platform layer (window, input, audio device)

`OKAY_PLATFORM_BACKEND=own` selects it, and `OKAY_OWN_STACK=1` sets it on Linux only (not on Windows, on purpose). The
Win32 version has only ever run under wine, never on real Windows, and the editor and launcher still open their windows
through SDL2 anyway, so only the seam parts (timers, audio, gamepads, voice) would change. If you want to try the player:
`set OKAY_PLATFORM_BACKEND=own` then `Tools\OkaySpacePlayer.exe` on an exported game. The startup line says
`platform=own` when it took effect; if this release was not built with the own backend it stays `platform=sdl2`.
**Unverified, not ready**, expect problems. Never ship with it.

Audio on the own Windows backend (WASAPI) is new and has never run on Windows: with `OKAY_PLATFORM_BACKEND=own` the player's
sound goes through it. If you hear nothing, or the startup log shows an audio error, that is the thing to report. A Linux check of
its pure parts: `ctest -R platform_audio_pump`.

**Try the own Windows backend with the self-test (4 steps).** The release does not build it unless asked
(`-DOKAY_BUILD_PLATFORM_SELFTEST=ON`); when it is built it is `Tools\okay-platform-selftest.exe`.
1. Plug in a pad if you have one. Double-click `Tools\okay-platform-selftest.exe`. A console and a window open for 20 seconds.
2. In the window: move the mouse, click, scroll, type letters, press keys, move the pad, press T to hear the 440 Hz tone again. Esc ends it early.
3. You should see colour bars, an orange square sliding along the bottom and a black cross at the mouse, and hear one second of a tone.
4. The console shows PASS, FAIL or NOT TESTED per item and saves it to `okay-platform-selftest.txt` next to the program. Send that file.
`--sdl2` runs the same checks on SDL2 to compare. `OKAY_OWN_PLATFORM=1` is a short way to write `OKAY_PLATFORM_BACKEND=own` for the player.
It has only run on Linux (Xvfb) so far; the Windows run is the point.

**Try a non-XInput pad (Windows, own backend, never run yet).** Use a DirectInput-only pad (an old generic USB pad, not an Xbox pad).
In step 2 of the self-test above, move its sticks and press its buttons. You should see lines like `pad raw axis 0 value 32767`,
`pad raw button 3 down`, `pad raw hat 0 bits 2` in the console, and "gamepads found" should name it "HID game controller VVVV:PPPP".
Unplug and replug it: a "gamepad removed / added" event is counted. Xbox pads still go through XInput. If nothing appears, send the file.
