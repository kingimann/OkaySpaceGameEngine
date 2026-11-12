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

## Status at a glance (2026-11-12)

| State | What |
|---|---|
| **Released** | 2.861.0 (large editor and engine release), 2.861.1 and 2.861.2 (Add Node menu speed). All three are on GitHub and in the Supabase bucket with checksums. |
| **Ready, waiting to ship** | 2.862.0: right-click a node, Connect to..., Print with Text and Value, many wires from one flow output (`claude/vs-connect-to`). Run in the real editor, tests pass; waits for CI and a PR. |
| **In progress** | The open pull requests in the table below; the unfinished editor branches (node search, Problems panel, node sidebar). |
| **Not started** | The modular gameplay contracts (design done, build not started), the feature-audit top ten, docs rewrite per area. |
| **Not ready, off** | Every own-code replacement (see "Own replacements" below). |

## Open pull requests

| PR | What it is | State | What decides the merge |
|---|---|---|---|
| #689 | Third-party removal plan, replacement criteria, crypto review package (docs) | CI green | Merge any time. |
| #685 | This board, plus a compiler cache for the Ubuntu CI job | One test failed because of the cache's base-directory setting; fixed on the branch | CI green, then merge. |
| #645 | Proprietary licence | CI green | The licence text is an unreviewed draft: **owner decision**. |
| #680 | OkayUI as an opt-in second backend (What's New, Console) | CI green | Stays developer-only until the owner says OkayUI is ready. |
| #699 | Editor Play keys and pads match the player; Direct3D 11 SSAO; Direct3D 12 label | CI green | Direct3D 11 SSAO has never been seen on a real GPU; it changes what Windows users get. Needs a GPU run first. |
| #687 | Own 2D painter behind the platform seam (off) | CI running | Touches the default player draw path; CI plus a read. |
| #698 | Updater hardening (no install without checksum, all-or-nothing swap, rollback) | CI running | Security sensitive; CI plus a careful read; Windows run is the gap. |
| #683 | Build pipeline: `<Name>_Data` layout, stripping, report, dry run | CI green on the last run | Merge after #685 so CI is faster. |
| #686 | Visual script typed variables, colours, structs, list and map nodes | CI was cancelled, needs a rerun | CI green. Add Var on a vector now adds per component. |
| #726 | Navigation obstacle carving; tests stop writing into the source tree | CI was cancelled, needs a rerun | CI green. |
| #727 | Model import and skinning fixes; model readers under the sanitizers | CI was cancelled, needs a rerun | CI green; the new Assimp job has never run. |
| #728 | Engine hangs, overflows and unsafe casts | CI was cancelled, needs a rerun | CI green. |
| #731 | FBX import hardening | CI was cancelled, needs a rerun | CI green. Small conflicts with #727 expected. |
| #700 | Data races in the server browser and voice chat | CI was cancelled, needs a rerun | CI green. |
| #730 | Visual script clarity audit and pin tooltips | CI was cancelled, needs a rerun | CI green. |

Branches pushed without a pull request (grouped into one pull request later, to keep the count
down): relay protocol v4 and net fixes (`claude/fix-net-issues`), launcher and scripting fixes
(`claude/fix-launcher-script-issues`), typed custom events (`claude/vs-events-delegates`).
Unfinished editor branches: node search ranking (`claude/vs-node-search`, 18 failing checks),
Problems panel (`claude/vs-problems-ui`, panel not yet run in the editor), node list side panel
(`claude/vs-node-sidebar`). None of these ship until they are finished and checked.

## Not done yet (feature list)

| Area | Item | Notes |
|---|---|---|
| Modular engine | Shared gameplay contracts (health, damage, hit zones, interactable, weapon, seat and vehicle, module manifest) | Design in `docs/design/modular_gameplay_contracts.md` on a branch; waiting for the six answers in it. Decisions so far: disabling a module strips its code, new projects ask which modules to include, the interact key defaults to E and can be rebound, no duplicate features (remove or improve). |
| Audit top ten | Optimiser window, traversal on by default, input defaults, one shared weapon component, interactables, runtime inspector keep-changes, per-property network sync, one health system, auto-rig weights, vehicle seats and damage | Each is checked against what already exists before work starts. |
| Visual script | Print node custom text (done in 2.862.0), "what connects here" menu (done in 2.862.0), node search ranking, Problems panel, node side panel, custom events, graph-to-graph calls | The last four are the unfinished branches above. |
| Docs | Per-area docs in more detail: what is done, in progress, not done | This board is the index; area docs follow. |
| Untriaged | OkayUI issues #715, #716, #717, #723 | Not looked at. |

## Deployment notes

- **Relay server**: the net fixes branch changes the relay protocol from v3 to v4 (a nonce). The
  live relay must be upgraded together with the clients, or clients set the unverified-relay flag.
- **Direct3D 11 SSAO** needs a run on a real GPU before it is released.
- **libsodium** needs an outside review before it can be replaced.
- **Fonts**: licence unresolved.
- **Releases** are published to GitHub by the release workflow and to Supabase by
  `scripts/publish-supabase.sh`. The launcher refuses an update with no checksum in `versions.json`,
  so a version must be on Supabase as soon as it is on GitHub. Without a `SUPABASE_SERVICE_KEY`
  repository secret the workflow skips that step and it is done by hand.

## Own replacements (all off)

Own crypto, own platform layer (Linux and Win32), OkayUI, own FBX, COLLADA and 3DS readers and the
own 2D painter stay off and developer-only until the owner says each one is ready and bug free.
Nothing user-visible, nothing default.

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
| Editor | Add Node menu: built once (2.861.1), then only visible rows drawn and results cached (2.861.2) | #740 #746 |
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
