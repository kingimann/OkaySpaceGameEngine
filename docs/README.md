# OkaySpace documentation: start here

> **What this is:** the one page to open first. It says what the engine is, what is going on now, and where every other page lives.
>
> **Status:** the docs were sorted into folders and overlapping pages were combined on 2026-11-20. No facts were dropped.
>
> **Last checked:** 2026-11-20, against `main` (version 2.893.0 in `CMakeLists.txt`).

## What OkaySpace is

OkaySpace is a game engine written in C++. It has three programs:

- the **launcher** (`OkaySpace.exe`), where you sign in, find projects and update;
- the **editor** (`OkayEngine.exe`), where you build scenes, add scripts and press Play;
- the **player** (`OkaySpacePlayer.exe`), which runs a finished game.

Releases are for Windows only for now. The goal is to write every part ourselves. A few outside libraries are still
used while our own versions are built (SDL2, Dear ImGui, Assimp, curl, libsodium). Our own versions stay off until they
are finished and the owner says yes.

## What is going on now

**Newest version: 2.893.0.** It brings a new camera system (cameras that follow, orbit or look at things, blend, shake,
and avoid walls) and a sharper in-game UI (a UI Quality setting). The UI look was only checked on the software
renderer, not on a real graphics card. The work board said on 2026-11-20 that this release was waiting for its version
pull request (#1038) to merge. Check the GitHub Releases page to see if it is out.

**The three releases before it:** 2.892.0 (physics materials and toys, better lighting, realistic water),
2.891.0 (wind, a bigger Build Settings window), 2.890.0 (boat and water, Visual Script blocks that explain themselves).
Every release, in plain words: [dist/CHANGELOG.txt](../dist/CHANGELOG.txt).

**Being worked on** (from the [work board](status/work_board.md), a hand-written snapshot from 2026-11-20):

- open pull requests: vehicles (open-world driving), Visual Script panning, Ctrl+S saving, Modeling side bar;
- finished branches that still need a pull request: the Add Component menu, camera zones, real collider shapes, Visual
  Script for beginners, particles, a UI designer, Lua and C# scripting;
- our own replacements (all off by default): a build tool, the platform layer instead of SDL2, OkayUI instead of
  Dear ImGui, our own compiler, our own model readers instead of Assimp.

**Waiting on the owner:** branch protection on `main`, Windows tests of the newest release, whether the OkayUI
programs ship, a Windows PC with .NET to try C#, and deleting OkayScript once Lua and C# work.

The live board (rewritten from GitHub every 10 minutes): [WORK_BOARD.md](WORK_BOARD.md).

## Where things are

Each folder has a `README.md` that lists its pages with one line each.

| Folder | What is in it |
|---|---|
| [status/](status/README.md) | What is going on and what is left: the work board, the unfinished lists, the plan to replace outside libraries, where code came from. |
| [process/](process/README.md) | How the work is done: the two Claudes, agent briefs, reviews, testing, QA. |
| [engine/](engine/README.md) | The core: runtime, time, maths, scenes, prefabs, assets, saving, input, languages, settings, debug and speed tools. |
| [gameplay/](gameplay/README.md) | Game pieces: animation, cameras, AI and paths, dialogue, cutscenes, vehicles, weapons, survival, wind, your first game. |
| [rendering/](rendering/README.md) | How things look: the renderers and their status, lighting, terrain and water, particles, textures. |
| [physics/](physics/README.md) | Physics, all in one page. |
| [audio/](audio/README.md) | Sound, our own WAV decoder, the voice server. |
| [scripting/](scripting/README.md) | OkayScript and Visual Script. |
| [editor/](editor/README.md) | The editor: panels, menus, undo, projects, level tools, modeling. |
| [ui/](ui/README.md) | In-game UI, and OkayUI (our own UI toolkit). |
| [online/](online/README.md) | Multiplayer, servers, accounts, the Supabase backend, anti-cheat, security, crash reports. |
| [platform/](platform/README.md) | Our own platform layer, build tool, compilers, crypto, HTTP client, Xbox and web exports. |
| [content/](content/README.md) | Getting models in: import, our own model readers, real-file tests. |
| [release/](release/README.md) | Getting builds to players: packaging, releasing, channels, the updater, installing, downloadable content. |

## Long lists (look things up, do not read top to bottom)

These stay at the top of `docs/` because scripts and tests write or read them there. Do not edit the made ones by hand.

- [FEATURE_MAP.md](FEATURE_MAP.md): every system, where its code, tests and docs are, and what is still open
  (made by `scripts/gen_feature_map.py` from the pages in [feature_map/](feature_map/)).
- [status/UNFINISHED.md](status/UNFINISHED.md): everything that is not finished (hand-written).
- [try_it.md](try_it.md): old "how to try it" entries (made by `scripts/gen_try_it.py` from [try_it/](try_it/));
  no longer required (rule 4a), kept.
- [verification/README.md](verification/README.md): the owner's test checklists for the Windows release (made by
  `scripts/gen_verify_checklist.py`).
- [engine_api.md](engine_api.md): every function scripts can call (made by `test_vs_types`).
- [visual_scripting_nodes.md](visual_scripting_nodes.md), [visual_scripting_topics.md](visual_scripting_topics.md),
  [visual_script_category_audit.md](visual_script_category_audit.md), [visual_scripting_naming.md](visual_scripting_naming.md),
  [visual_script_coverage.md](visual_script_coverage.md): every Visual Script node, where it sits in the menu, its
  name, and whether it is tested.
- [ui_catalogue.md](ui_catalogue.md): every UI component, style and icon (made by `scripts/gen_ui_catalogue.py`).
- [imgui_port_report.md](imgui_port_report.md): how much of the Dear ImGui use OkayUI covers (made by `scripts/imgui_usage_report.py`).
- [sdl_inventory.md](sdl_inventory.md): every SDL2 call that is left (made by `scripts/sdl_inventory.py`).
- [lanes.json](lanes.json) and [sql/](sql/): who owns which files, and the database scripts.

## Rules and plans

- [../CLAUDE.md](../CLAUDE.md): the owner's standing rules for every session and agent.
- [../ROADMAP.md](../ROADMAP.md): releases and plans.
- Pictures are not stored in the repository (owner rule, 2026-11-17). Pages describe them in words.
- The old index linked an `archive/` folder of old reports. That folder is not in the repository, so the link was dropped.
