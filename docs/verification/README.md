# Verify every feature on the real Windows release

You asked for a way to go through every feature and check it works on the release you really run. These pages are that list. Each line is a GitHub task-list checkbox, so you can tick it in an issue and the issue shows your progress.

**Nothing here was run in a real window by us.** Every "You should see" line is *expected, not yet seen on Windows*. Where a step could not be matched to the code it says `(steps unverified)`.

## How to use it

1. Install the release (`OkaySpace-Setup.exe` or the zip). Start `OkaySpace.exe`; the editor is `Tools\OkayEngine.exe`, the player is `Tools\OkaySpacePlayer.exe`. [try_it.md](../try_it.md) (if present in your checkout) says where the example scenes are and how to switch the own-code options on.
2. Open a checklist below (or its GitHub issue). Go row by row. Each row has a stable id such as `V-ED-012`.
3. Do the **Steps**. Compare with **You should see**.
4. If it works, tick the box. If it does not, leave the box empty and comment on the issue with the row id and what happened.
5. Rows that name a **Needs** line only work on a release that contains that change.

## Legend

| Mark | Meaning |
|---|---|
| `- [ ]` | not checked yet. Tick it when the row works for you |
| `V-XX-NNN` | a row with written steps (important or recent feature) |
| `V-XX-S<area>` | a smoke test for a whole feature-map area, built from the map. Its steps are `(steps unverified)` because they come from the map, not from clicking |
| `(steps unverified)` | we could not match every step to the code, or the row is a smoke test. Expect to explore |
| Expected, not yet seen on Windows | all "You should see" lines. We have only read code and run headless tests |
| Known partial | what the map or the docs say is missing or rough. A gap listed here is **not** a bug report |

## How to report a failure

Comment on the checklist issue (or open one) with:

```
V-ED-012 failed
Release: <version from Help > About OkaySpace>
What I did: ...
What I saw: ...
What I expected: ...
```

Screenshots help. One comment per row. The row id is how we find the feature in [the feature map](../FEATURE_MAP.md).

## Checklists

| File | Rows | Of which written steps | Smoke tests |
|---|---|---|---|
| [Launcher and accounts](01-launcher-and-accounts.md) | 19 | 15 | 4 |
| [Editor basics](02-editor-basics.md) | 24 | 17 | 7 |
| [Scene and components](03-scene-and-components.md) | 15 | 9 | 6 |
| [Characters and animation](04-characters-and-animation.md) | 11 | 7 | 4 |
| [Physics, vehicles and navigation](05-physics-vehicles-navigation.md) | 11 | 8 | 3 |
| [Rendering and post-processing](06-rendering-and-post-processing.md) | 18 | 9 | 9 |
| [Audio and input](07-audio-and-input.md) | 7 | 5 | 2 |
| [In-game UI, scripting and visual scripting](08-ui-and-scripting.md) | 16 | 11 | 5 |
| [Gameplay, story and samples](09-gameplay-story-and-samples.md) | 14 | 7 | 7 |
| [Networking, multiplayer and anti-cheat](10-networking-and-multiplayer.md) | 8 | 6 | 2 |
| [Build, export, updates and tooling](11-build-export-and-updates.md) | 15 | 7 | 8 |
| [Own-code switches (off by default)](12-own-code-switches.md) | 11 | 10 | 1 |

## Keeping it in sync

Add or change a row by editing `docs/verification/steps/<GROUP>.md` (hand-written rows) or the feature map (smoke rows), then run `python3 scripts/gen_verify_checklist.py`. `--check` fails when a file is stale (the ctest `verify_checklist` runs it); `--check-labels` fails when a step names a menu item, variable or path that is no longer in the source (ctest `verify_checklist_labels`). Every new feature must add its row here, as well as to [try_it.md](../try_it.md) and the feature map (CLAUDE.md rule 5).
