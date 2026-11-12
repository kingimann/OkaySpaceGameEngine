# Work board

One place to see what is done, what someone is working on, and what is free, so nobody builds
something twice. Update it in the same change that finishes or claims a task.

## Before you start any task

1. `git fetch origin`, then look for the work: `git log origin/main --oneline | grep -i <word>`,
   `git branch -r | grep -i <word>`, and search the repo and these docs:
   `unfinished_engine.md`, `unfinished_launcher.md`, `own_code_roadmap.md`, `THIRD_PARTY.md`,
   `FEATURE_MAP.md`.
2. If it exists in whole or in part, stop and say where. Do not build a second version.
3. Claim it: add a row under "Claimed" in this file, name the branch `<who>/<topic>`, and push the
   branch early (a small first commit with a one-line plan).
4. Work only on your task. Do not merge your own branch. Do not turn on any own-code replacement.

## Rules that apply to every task

- Nothing unfinished ships: every control is reachable, does something you can see, and has a test.
- Own-code replacements for third-party libraries stay **off** until the owner is told they are
  ready and bug free: no default, no user-visible setting (an environment variable, a CMake option
  or a developer flag only), and the docs say "not ready, off".
- Changelog lines list only what works. Do not change the version or `dist/CHANGELOG.txt` in a
  feature change; releases do that.
- Pure logic goes in `engine/` or `shared/` so tests can link it; tests must not write into the
  source tree.
- No Windows macro names (`near`, `far`, ...); no `<mutex>` or `<thread>` in public headers.
- Docs do not name other products. Own code is written from public format descriptions, not
  copied from other libraries.

## Done (on main)

| Area | What | PR |
|---|---|---|
| Model import | Own glTF, OBJ/MTL, STL, PLY readers behind an import seam | #660 |
| Model import | Own FBX reader (off) | #658 |
| Model import | Own COLLADA (.dae) reader (off) | #679 |
| Audio, images, fonts | Own Ogg Vorbis, MP3, image decoders, PNG writer, TrueType, 8x8 font, file dialogs | #644 #642 #649 #639 #641 #640 #655 |
| Scripting | Lua and C# backends removed | #643 |
| Own replacements (all off) | okay::crypto | #656 |
| Own replacements (all off) | okay::platform seam, own Linux and Win32 backends; player runs on the seam | #654 #681 |
| Own replacements (all off) | OkayUI headless harness, draw list, cursor and item queries | #659 #678 |
| Editor | Prefab override UI, prefab mode | #666 #676 |
| Editor | Foliage, terrain layers, decals, cables, ortho views, measuring | #677 |
| Editor | Volumes and zones, generalised I/O links, navigation areas and links, mesh collider | #675 #672 #661 #663 |
| Editor | Project panel on the asset database, undo stack, UX pass | #673 #662 #682 |
| Engine | Accessibility settings, save games without code | #657 #664 |
| Launcher | Kick/ban, blocks and privacy, password reset page, updater checks, account export and delete, reports | #668 #667 #647 #650 #651 #652 |

## Claimed (in progress)

| Task | Who | Branch / PR |
|---|---|---|
| Build pipeline: `<Name>_Data` layout, stripping, report | Claude | #683 |
| OkayUI wiring (developer-only switch) | Claude | #680 |
| Visual script: typed variables, arrays and maps (5a, 5b) | Claude | `claude/vs-variables-collections` |
| Editor Play keys and pads, Direct3D 11 SSAO, Direct3D 12 label | Claude | `claude/engine-top-items` |
| Own 2D painter for the platform seam | Claude | `claude/own-2d-painter` |
| Navigation obstacle carving, test source-tree hygiene | Claude | `claude/nav-obstacles` |

## Free (unclaimed, safe small tasks)

| Task | Notes |
|---|---|
| 3DS (.3ds) reader behind the import seam | Follow the COLLADA reader's structure; off by default |
| BVH animation reader | Animation only; off by default |
| `.off` and other simple mesh formats | One format per task |
| Subtitles and captions, hold-to-toggle, aim assist | `unfinished_engine.md` section 14 |

## Queued next (do not start without asking)

Visual script 5c to 5e (custom events, graph-to-graph calls, custom function nodes), then the
text-script nodes; the rest of the own replacements (Assimp's other formats, editor and launcher
off SDL2 and Dear ImGui, libsodium after an outside review).
