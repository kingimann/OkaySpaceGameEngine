# Packaging & distribution layout

The Windows distribution is laid out so a user double-clicks one thing at the top
and everything else is tucked away. This is the **canonical layout** — builds
(and the `scripts/build-windows-dist.sh` script) follow it.

```
OkaySpace.exe        ← the launcher: start here (sign in, create/play games)
SDL2.dll             ← required next to OkaySpace.exe
README.txt           ← short "what do I run" note
Tools/
  OkayEngine.exe        the editor (make games)
  OkaySpacePlayer.exe   standalone runtime that plays an exported game
  okayspace-relay.exe   optional multiplayer NAT relay (see relay.md)
  SDL2.dll              copy (see below)
  VERSION.txt
  accounts.md
```

## Why SDL2.dll appears twice

Windows resolves a load-time-linked DLL from the **directory of the executable
that needs it**. Every OkaySpace `.exe` is dynamically linked against SDL2, so
each one needs `SDL2.dll` in its own folder. That's why one copy sits beside
`OkaySpace.exe` at the top and another sits in `Tools/` beside the editor /
player / relay. It is required, not redundant — **never separate an `.exe` from
an `SDL2.dll`.**

(If you'd rather ship a single DLL, build with `-DOKAY_STATIC_SDL=ON` to link
SDL2 statically; then no `SDL2.dll` ships at all. The default is dynamic linking,
Unity-style, which keeps the exes small and the DLL swappable.)

## Building it

```bash
export SDL2_MINGW_PREFIX=/path/to/SDL2-<ver>/x86_64-w64-mingw32
./scripts/build-windows-dist.sh        # -> build-win-dist/ in the layout above
```

The script builds the launcher, editor, player, and relay, then arranges them as
shown and writes `README.txt`. Zip `build-win-dist/` for release.

## Shipping a single *game* (not the toolset)

When you export a game from the editor (**File → Build Game**), that output is its
own self-contained folder — `<YourGame>.exe` (a copy of the player runtime) +
`SDL2.dll` + your assets. For Steam auto-updates, upload that folder as your depot
(see [steam_release.md](steam_release.md)).
