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
`platform=own` when it took effect; if this release was not built with the own backend it stays `platform=sdl2`. With it the player makes no SDL call at all and its
resolution list is our own (section "Player with no SDL, game resolutions on our own code").
**Unverified, not ready**, expect problems. Never ship with it.
The editor has its own switch for its input and the 3D views' OpenGL context: `OKAY_EDITOR_PLATFORM=own` (section "Editor: input and
the 3D views' OpenGL through our own code"); with `OKAY_PLATFORM_BACKEND=own` as well, that OpenGL context is our own WGL code.
