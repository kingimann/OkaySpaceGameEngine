# OkaySurvival — the sample game

A small standalone survival game built entirely on the OkaySpace engine. It's the
flagship example of shipping a game *outside* the editor: the game is just the engine's
**player runtime** loading a `game.okayscene`, exactly what the editor produces with
**Build Game**.

## What it is

A third-person survival sandbox: your blocky character's **hunger, thirst, stamina**
(and oxygen, warmth) drain over a **day/night cycle**. Replenish at resource spots,
avoid hazards, survive.

- WASD move, Shift run, Space jump (ThirdPersonController).
- **Berry Bush** (green) — stand on it to eat (restores hunger).
- **Well** (blue) — stand on it to drink (restores thirst).
- **Campfire** (orange) — warms you.
- **Lake** (blue, large) — gets you wet and submerged (oxygen drains — don't linger).
- **Toxic Pit** (purple) — poisons you.
- HUD bars (top-left): Health, Hunger, Thirst, Stamina, Oxygen; a clock shows the time.
- Progress auto-saves to `okaysurvival.okayprefs` and reloads on next launch.

Every mechanic is a native engine component — `SurvivalStats`, `SurvivalZone`,
`DayNightCycle`, `PoisonStat`, `WetnessStat`, `SurvivalSave`, `ThirdPersonController`,
`Character`, `UIProgressBar`, `UITextBind` — no game-specific C++ at runtime.

## How it's built

`game/okaysurvival_gen.cpp` assembles the scene with the engine API and writes the two
files a shipped game needs:

- `game.okayscene` — the world (serialized via `SceneSerializer`).
- `game.okayconfig` — window title/size + the startup scene.

Build and run the generator, then drop the player runtime beside its output:

```
cmake --build build --target okaysurvival_gen
./build/game/okaysurvival_gen  out/
cp OkaySpacePlayer(.exe) out/OkaySurvival(.exe)   # rename the runtime to the game
cp SDL2.dll out/                                  # Windows
./out/OkaySurvival
```

The player loads `game.okayscene` next to it (per `game.okayconfig`'s `startup=`), so
the folder is a complete, double-click game. You can also open `game.okayscene` in the
editor to tweak it, or regenerate from source.
