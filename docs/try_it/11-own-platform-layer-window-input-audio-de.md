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
