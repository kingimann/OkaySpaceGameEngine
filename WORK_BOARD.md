# Live work board

Generated 2026-11-23 02:48 UTC by `scripts/work_board.py` (a scheduled workflow rewrites this about every 10 minutes). Rule 6 (`CLAUDE.md`): a PR merges only when every CI job is finished and green.

Hand-written parts (agents, owner decisions): [docs/status/work_board.md](https://github.com/kingimann/OkaySpaceGameEngine/blob/main/docs/status/work_board.md).

## Open pull requests (21)

| PR | Title | State | CI | Updated |
|---|---|---|---|---|
| [#1074](https://github.com/kingimann/OkaySpaceGameEngine/pull/1074) | Windows tests: fixes for paths, line endings, a UTF-8 crash and test bugs (last full Windo | draft, red | 19 green, 0 running, 0 queued, 1 red (Run tests on Windows) | 21 h ago |
| [#1120](https://github.com/kingimann/OkaySpaceGameEngine/pull/1120) | Texture system: one shared texture asset with handles, per-texture settings, gamma-correct | waiting | 3 green, 1 running, 14 queued, 0 red | 39 min ago |
| [#1138](https://github.com/kingimann/OkaySpaceGameEngine/pull/1138) | Active Ragdoll: our own physics-driven character (balance, stepping, bracing, head protect | waiting | 3 green, 1 running, 14 queued, 0 red | 39 min ago |
| [#1140](https://github.com/kingimann/OkaySpaceGameEngine/pull/1140) | Duplicates removed: mesh hashes, node titles, Clip Player name, curve formulas, zip writer | waiting | 19 green, 1 running, 0 queued, 0 red | 4 h ago |
| [#1141](https://github.com/kingimann/OkaySpaceGameEngine/pull/1141) | Editor split, move 1: Build Game code from main.cpp to editor/BuildGame.cpp (pure move, ch | waiting | 19 green, 1 running, 0 queued, 0 red | 1 h ago |
| [#1142](https://github.com/kingimann/OkaySpaceGameEngine/pull/1142) | Character controller: one plain Player Controller panel, explained animators (the part of  | ready to merge | 18 green, 0 running, 0 queued, 0 red | 4 h ago |
| [#1143](https://github.com/kingimann/OkaySpaceGameEngine/pull/1143) | Release 2.903.2 | red | 18 green, 0 running, 0 queued, 2 red (lint, Cross-compile Windows build (MinGW)) | 4 h ago |
| [#1148](https://github.com/kingimann/OkaySpaceGameEngine/pull/1148) | Own readers: one uniform hostile-input sweep over six readers, and an IQM tiny-framerate f | red | 17 green, 1 running, 1 queued, 1 red (lint) | 52 min ago |
| [#1149](https://github.com/kingimann/OkaySpaceGameEngine/pull/1149) | CI: cap every big build's parallel jobs by memory, so runners are not starved and lost (Wi | waiting | 19 green, 1 running, 0 queued, 0 red | 1 h ago |
| [#1152](https://github.com/kingimann/OkaySpaceGameEngine/pull/1152) | Remove the copied code that #1126 (own model readers) brought into main | waiting | 18 green, 1 running, 1 queued, 0 red | 1 h ago |
| [#1155](https://github.com/kingimann/OkaySpaceGameEngine/pull/1155) | One navigation system: Grid Pathfinding folded into Navigation, one grid A* for 2D grids a | waiting | 6 green, 0 running, 12 queued, 0 red | 40 min ago |
| [#1156](https://github.com/kingimann/OkaySpaceGameEngine/pull/1156) | Remove the unused ECS World (a second object store nothing used) | waiting | 9 green, 4 running, 6 queued, 0 red | 43 min ago |
| [#1158](https://github.com/kingimann/OkaySpaceGameEngine/pull/1158) | Modules, steps 1-5: build grouped into 40 modules, module registry and log line, module si | waiting | 8 green, 3 running, 7 queued, 0 red | 40 min ago |
| [#1159](https://github.com/kingimann/OkaySpaceGameEngine/pull/1159) | One script command table for OkayScript and typed OkayScript (C#): first 20 shared command | red | 15 green, 3 running, 0 queued, 1 red (Scripting and robustness tests (ASan + UBSan)) | 58 min ago |
| [#1160](https://github.com/kingimann/OkaySpaceGameEngine/pull/1160) | One physics engine: our own core is the engine's 3D physics; Classic, Improved and the sep | waiting | 13 green, 5 running, 1 queued, 0 red | 49 min ago |
| [#1161](https://github.com/kingimann/OkaySpaceGameEngine/pull/1161) | Checks before the push: precheck (with --tests) and pre-push hook; PRs run only lint + Win | waiting | 19 green, 0 running, 1 queued, 0 red | 77 min ago |
| [#1162](https://github.com/kingimann/OkaySpaceGameEngine/pull/1162) | Visual Script: right-answer tests for 66 more nodes (maths, logic, text, maps, transform,  | draft, red | 17 green, 2 running, 0 queued, 1 red (body) | 1 h ago |
| [#1163](https://github.com/kingimann/OkaySpaceGameEngine/pull/1163) | Visual Script editor: no false red problems from script export; copy/paste keeps variables | draft, red | 9 green, 4 running, 5 queued, 1 red (body) | 46 min ago |
| [#1164](https://github.com/kingimann/OkaySpaceGameEngine/pull/1164) | Xbox UWP round 4: key layouts in the backend, splash in the runtime, microphone capture se | red | 10 green, 6 running, 0 queued, 2 red (body, lint) | 41 min ago |
| [#1165](https://github.com/kingimann/OkaySpaceGameEngine/pull/1165) | Visual Script: forward nodes face the same way as Look At (Shoot, Spawn Projectile, Raycas | draft, waiting | 2 green, 1 running, 15 queued, 0 red | 32 min ago |
| [#1166](https://github.com/kingimann/OkaySpaceGameEngine/pull/1166) | Visual Script: 34 new friendly nodes (camera, movement, damage, vehicles, UI, input, audio | draft, waiting | 0 green, 1 running, 0 queued, 0 red | 9 min ago |

## Recently merged

| PR | Title | Merged |
|---|---|---|
| [#1131](https://github.com/kingimann/OkaySpaceGameEngine/pull/1131) | Xbox UWP round 3: pending saves before suspend, saved focusable flag, splash model, keyboa | 62 min ago |
| [#1150](https://github.com/kingimann/OkaySpaceGameEngine/pull/1150) | Image hostile test: 11,736 PNM inputs (no decoder bug found) | 62 min ago |
| [#1139](https://github.com/kingimann/OkaySpaceGameEngine/pull/1139) | One rebinding store: InputActions and ActionSet share player 0's bindings (Grok #422 item  | 74 min ago |
| [#1147](https://github.com/kingimann/OkaySpaceGameEngine/pull/1147) | Systems list: every system named, with what it does (docs/SYSTEMS.md, generated from the f | 87 min ago |
| [#1145](https://github.com/kingimann/OkaySpaceGameEngine/pull/1145) | Duplicate guard fixes: no crash on root files of slash-named branches; stacked branches an | 1 h ago |
| [#1128](https://github.com/kingimann/OkaySpaceGameEngine/pull/1128) | Own code editor: ImGui popups draw over it, framed line numbers, current-line outline, edi | 1 h ago |
| [#1132](https://github.com/kingimann/OkaySpaceGameEngine/pull/1132) | One save store (SaveGame becomes GameSaves' whole-scene mode) and one .okaymeta reader (Gr | 2 h ago |
| [#1146](https://github.com/kingimann/OkaySpaceGameEngine/pull/1146) | CLAUDE.md: nothing off by default (rule 17), improve and add on, never a second copy (new  | 2 h ago |

## Branches of every lane: `claude/*` (Coordinator), `claude2/*` (Claude 2), `claude3/*` (Claude 3), newest first

| Lane | Count |
|---|---|
| Coordinator (`claude/*`) | 358 |
| Claude 2 (`claude2/*`) | 194 |
| Claude 3 (`claude3/*`) | 0 |

| Branch | Lane | Last commit | Head | When |
|---|---|---|---|---|
| `claude/vs-new-nodes-1` | Coordinator | Visual Script friendly blocks: dispatch tests for On Button Clicked and On Damag | `a0ddaf4` | 13 min ago |
| `claude/vs-forward-fix-1` | Coordinator | Visual Script: nodes that use the way an object faces now use its -Z, like Look  | `6fd4716` | 33 min ago |
| `claude2/texture-system` | Claude 2 | Merge remote-tracking branch 'origin/main' into claude2/texture-system | `c8144c6` | 42 min ago |
| `claude2/active-ragdoll` | Claude 2 | Merge remote-tracking branch 'origin/main' into claude2/active-ragdoll | `c05ecec` | 42 min ago |
| `claude2/one-navigation` | Claude 2 | Merge remote-tracking branch 'origin/main' into claude2/one-navigation | `15f43ff` | 42 min ago |
| `claude2/modular-s3-newproject` | Claude 2 | Merge remote-tracking branch 'origin/main' into claude2/modular-s3-newproject | `e7f4425` | 42 min ago |
| `claude2/ecs-world` | Claude 2 | Merge remote-tracking branch 'origin/main' into claude2/ecs-world | `8bf66e9` | 43 min ago |
| `claude/vs-editor-bugs-1` | Coordinator | Visual Script editor: export-only notes leave Problems; Copy carries declared va | `71a3e2c` | 46 min ago |
| `claude/own-readers-hostile-2` | Coordinator | Hostile tests: test_q3bsp_hostile uses the shared counting allocator too (its co | `7f523ac` | 53 min ago |
| `claude2/one-script-api` | Claude 2 | Regenerate the feature map and code index after merging main | `27894f6` | 59 min ago |
| `claude2/physics-no-classic` | Claude 2 | Docs: regenerate SYSTEMS.md after merging main (the physics row lists the core's | `355e856` | 59 min ago |
| `claude/xbox-uwp-6` | Coordinator | Xbox pieces test: remove the leftover fake-host lines my last edit left outside  | `c9e0c1c` | 61 min ago |
| `claude2/precheck` | Claude 2 | Checks before the push: PRs run only lint, PR-body and the Windows build on GitH | `8e5ea1d` | 78 min ago |
| `claude2/modular-2` | Claude 2 | Record modular-s3-newproject as built on: this WIP branch is superseded by the s | `eebef98` | 89 min ago |
| `claude/vs-nodes-work-1` | Coordinator | Visual Script: right-answer tests for 66 nodes that had only an effect test | `b3e72d3` | 1 h ago |
| `claude2/readers-dedup` | Claude 2 | Merge remote-tracking branch 'origin/main' into claude2/readers-dedup | `dde255f` | 1 h ago |
| `claude2/editor-split-1` | Claude 2 | Merge remote-tracking branch 'origin/main' into claude2/editor-split-1 | `878301a` | 1 h ago |
| `claude2/mingw-jobs-by-memory` | Claude 2 | Revert the larger-runner variable (owner, 2026-11-23: never mind) | `bf5fbf7` | 1 h ago |
| `claude2/one-health` | Claude 2 | WIP: one health per object (Survival Stats, RPG Stats, NPC Controller use the He | `950823d` | 2 h ago |
| `claude/xbox-uwp-5` | Coordinator | Xbox pieces test: remove the leftover fake-host lines my last edit left outside  | `871d3ed` | 2 h ago |
| `claude2/modular-s3-addcomp` | Claude 2 | WIP (not built, not tested): Add Component hides components of modules turned of | `9bedde8` | 2 h ago |
| `claude2/modular-s3-settings` | Claude 2 | Merge claude2/modular-s0-sizes; code index and feature map regenerated | `728e6f3` | 2 h ago |
| `claude2/modular-s0-sizes` | Claude 2 | Merge claude2/modular-s0-manifest; code index and feature map regenerated | `282ead8` | 2 h ago |
| `claude2/modular-s0-manifest` | Claude 2 | Merge claude2/modular-s0-build; code index and feature map regenerated | `9a4b18b` | 2 h ago |
| `claude2/modular-s0-build` | Claude 2 | Merge origin/main; code index and feature map regenerated | `9b9de60` | 2 h ago |
| `claude2/ui-shared-code` | Claude 2 | Game UI and OkayUI share their word wrap, tooltip placement and HSV code | `ec502ab` | 3 h ago |
| `claude2/own-physics-engine` | Claude 2 | Tests: physics3d_determinism builds its scene with the shared helper; mark the c | `a087231` | 4 h ago |
| `claude2/dedup-small` | Claude 2 | WIP (review of #1140): BT HealthFraction reads HealthOf; Play Clip asks for a Ch | `e197539` | 4 h ago |
| `claude2/release-2.903.2` | Claude 2 | Release 2.903.2 | `c8d5511` | 4 h ago |
| `claude/controller-2` | Coordinator | One plain controller Inspector (15 settings, feature boxes), registry label/unit | `8665fc6` | 5 h ago |
| `claude/vehicle-3` | Coordinator | Vehicle: controller reads the control scheme, seat uses fit/blend/clip/tap-hold, | `55adfe6` | 5 h ago |
| `claude2/dup-guard` | Claude 2 | PR hygiene: only 'Existing code' is required; 'How to try it' is a note (CLAUDE. | `f46ef01` | 6 h ago |
| `claude/own-codeedit-3` | Coordinator | Roadmap: CodeEdit step 30 (editor links and starts under Xvfb) | `5768343` | 7 h ago |
| `claude/own-readers-hostile-1` | Coordinator | Hostile sweep over the six late own readers; IQM tiny framerate fix; COB test ti | `8ec926b` | 7 h ago |
| `claude2/build-export-fix` | Claude 2 | WIP Build window: background build with progress and Cancel, export check in the | `e2f67d4` | 7 h ago |
| `claude2/lod-asset-streaming` | Claude 2 | WIP LOD cross-fade, LOD bias and overlay, LODs on import, texture mip and mesh s | `e0e4db0` | 7 h ago |
| `claude2/steam-epic-stores-2` | Claude 2 | WIP Store Kit: visual script nodes for stats, cloud saves and presence; File > E | `2eea57e` | 7 h ago |
| `claude2/world-streaming` | Claude 2 | WIP: world, level and chunk streaming, World Partition (engine done and mostly t | `42de301` | 7 h ago |
| `claude2/steam-epic-stores` | Claude 2 | Store Kit: Visual Script places for the kit's Fn.* nodes | `a600387` | 10 h ago |
| `claude2/modeling-tab-3` | Claude 2 | Editor: G no longer turns into an edge slide in the same frame; Rip starts its m | `5a569b8` | 15 h ago |

## Latest on the coordination thread (#422)

- 7 d ago: [Claude 2: `claude2/prone-stance` (`857eebd`) green](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6896227136)
- 7 d ago: [Claude 2: `claude2/world-text-rich` engine-linked results](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6896109744)
- 7 d ago: [Claude 2: progress on the four items](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895927754)
- 7 d ago: [Claim: `claude2/software-dof` (item 4, depth of field in the software renderer).](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895835704)
- 7 d ago: [Claim and result: `claude2/prone-stance` pushed at `857eebd`. NOT built yet (queued).](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895820832)

## Claims (issue #422)

| Branch | Lane | Status | Files | Problems |
|---|---|---|---|---|
| [`claude2/active-ragdoll`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083712383) | claude2 | active | - | claim has no Existing/Gap/Files/Test-first/Try-it lines |
| [`claude2/build-export-fix`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083712383) | claude2 | active | - | claim has no Existing/Gap/Files/Test-first/Try-it lines |
| [`claude2/dedup-input`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083839019) | claude2 | active, no branch pushed yet | - | claim has no Existing/Gap/Files/Test-first/Try-it lines |
| [`claude2/dedup-saves`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083839019) | claude2 | active, no branch pushed yet | - | claim has no Existing/Gap/Files/Test-first/Try-it lines |
| [`claude2/dedup-small`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083839019) | claude2 | active | - | claim has no Existing/Gap/Files/Test-first/Try-it lines |
| [`claude2/ecs-world`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7087372137) | claude2 | active | - | claim has no Existing/Gap/Files/Test-first/Try-it lines |
| [`claude2/editor-split-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7084954477) | claude2 | active | - | claim has no Existing/Gap/Files/Test-first/Try-it lines |
| [`claude2/lod-asset-streaming`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083712383) | claude2 | active | - | claim has no Existing/Gap/Files/Test-first/Try-it lines |
| [`claude2/modular-2`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7085741989) | claude2 | active | - | missing Files; missing Test-first; missing Try-it |
| [`claude2/one-health`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7087372137) | claude2 | active | - | missing Files; missing Test-first; missing Try-it |
| [`claude2/one-navigation`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7087372137) | claude2 | active | - | claim has no Existing/Gap/Files/Test-first/Try-it lines |
| [`claude2/one-script-api`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7087372137) | claude2 | active | - | missing Gap; missing Files; missing Test-first; missing Try-it |
| [`claude2/own-physics-engine`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083712383) | claude2 | active | - | claim has no Existing/Gap/Files/Test-first/Try-it lines |
| [`claude2/steam-epic-stores-2`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083712383) | claude2 | active | - | claim has no Existing/Gap/Files/Test-first/Try-it lines |
| [`claude2/ui-shared-code`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7087372137) | claude2 | active | - | claim has no Existing/Gap/Files/Test-first/Try-it lines |
| [`claude2/world-streaming`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083712383) | claude2 | active | - | claim has no Existing/Gap/Files/Test-first/Try-it lines |
| [`claude/crash-report-auto`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6902866537) | coordinator | stale, no branch pushed yet | `shared/CrashHandler.hpp`, `shared/CrashContext.hpp`, `shared/CrashScrub.hpp` | - |
| [`claude/dev-channel`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6904496725) | coordinator | stale, no branch pushed yet | `github/workflows/dev-build.yml`, `scripts/publish-supabase.sh`, `scripts/versions_index.py` | - |
| [`claude/docs-batch`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903819607) | coordinator | stale, no branch pushed yet | `docs/install_audit.md`, `docs/install_comparison.md`, `docs/own_flip_proposal.md` | - |
| [`claude/editor-menus`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6902296013) | coordinator | stale, no branch pushed yet | `editor/MenuBarModel.hpp`, `editor/HierarchyContextModel.hpp`, `editor/MenuEntriesUI.hpp` | - |
| [`claude/fp-hand-fix`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6906260594) | coordinator | stale, no branch pushed yet | `engine/include/okay/Components/FirstPersonHand.hpp`, `FirstPersonHands.hpp`, `FpHandsPreview.hpp` | - |
| [`claude/grok-stack`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903819607) | coordinator | stale | `tests/test_scene_stray_number.cpp`, `tests/test_switch_off_settings.cpp`, `tests/test_scene_subnormal.cpp` | - |
| [`claude/input-upgrade`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6905091630) | coordinator | stale, no branch pushed yet | `engine/include/okay/Input/**`, `engine/src/Input/**`, `tests/test_input_deep_*.cpp` | - |
| [`claude/installer-honest`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903484221) | coordinator | stale | `scripts/installer-tests/`, `docs/images/installer/` | - |
| [`claude/launcher-open-gate`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903385935) | coordinator | stale | `launcher/OpenProjectGate.hpp`, `tests/test_open_project_gate.cpp`, `docs/images/open_gate/` | - |
| [`claude/one-inflate`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6902296013) | coordinator | stale, no branch pushed yet | `engine/include/okay/Core/Inflate.hpp`, `tests/inflate_reference_slow.hpp`, `tests/test_inflate_differential.cpp` | - |
| [`claude/own-code-editor-2`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903341750) | coordinator | stale | `okayui/include/okay/Text/TextBuffer.hpp`, `okayui/src/TextBuffer.cpp`, `okayui/tests/test_text_buffer.cpp` | - |
| [`claude/physics-more`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6904424383) | coordinator | stale, no branch pushed yet | `engine/include/okay/Physics/**`, `engine/src/Physics/**`, `engine/include/okay/Components/Joint*.hpp` | - |
| [`claude/player-deep`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6904495900) | coordinator | stale | `engine/include/okay/Components/FirstPersonController.hpp`, `ThirdPersonController.hpp`, `ThirdPersonShooterController.hpp` | - |
| [`claude/project-menus`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903051423) | coordinator | stale, no branch pushed yet | `editor/ProjectContextModel.hpp`, `editor/ProjectMenuDraw.cpp`, `editor/ProjectNewAssets.hpp` | - |
| [`claude/release-2-876`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903101150) | coordinator | stale, no branch pushed yet | `dist/CHANGELOG.txt` | - |
| [`claude/release-parallel`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903502484) | coordinator | stale | `scripts/publish-supabase-v1.sh`, `scripts/promote-latest.sh`, `scripts/check-unreleased.sh` | - |
| [`claude/templates-work`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6902709811) | coordinator | stale, no branch pushed yet | `editor/TemplateTable.hpp`, `tests/test_templates_work.cpp`, `tests/test_examples_work.cpp` | - |
| [`claude/ui-nameplate-worldtext`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903199226) | coordinator | stale, no branch pushed yet | `engine/include/okay/Components/UIOwnerData.hpp`, `engine/src/Components/UIOwnerData.cpp`, `examples/ui_text/ui_text.okayscene` | - |
| [`claude/upgrade-stack`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903306932) | coordinator | stale | `editor/StartupModals.hpp`, `tests/test_startup_modals.cpp`, `tests/test_scene_bom_bug.cpp` | - |
| [`claude/versions-safety`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903484221) | coordinator | stale | `launcher/VersionsSafety.hpp`, `tests/test_versions_safety.cpp` | - |
| [`claude/voxel-edit-fps`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6904423421) | coordinator | stale, no branch pushed yet | `engine/src/Components/VoxelTerrain.cpp`, `engine/include/okay/Components/VoxelTerrain.hpp`, `VoxelDigger.hpp` | - |
| [`claude/vs-categories`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6902296013) | coordinator | stale, no branch pushed yet | `editor/VisualScriptPlace.hpp`, `editor/VisualScriptPlaceRules.hpp`, `editor/VisualScriptLegacyPlaces.hpp` | - |
| [`claude/walk-vs-run`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6904422160) | coordinator | stale, no branch pushed yet | `engine/src/Animation/HumanoidMotion.cpp`, `engine/src/Components/Character.cpp`, `engine/include/okay/Components/Character.hpp` | - |
| [`claude2/drag-paths-relative`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6904682606) | claude2 | stale | `tests/test_drop_path_relative.cpp`, `docs/try_it.md` | - |
| [`claude2/found-console`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7068578972) | claude2 | stale, no branch pushed yet | `editor/ConsolePanel.cpp`, `player/main.cpp`, `Core/Console*` | - |
| [`claude2/found-jobs`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7068578972) | claude2 | stale, no branch pushed yet | `Jobs.*`, `AssetManager.cpp`, `NavMeshSurface.cpp` | - |
| [`claude2/grok-145-146-153-155`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7064574409) | claude2 | stale, no branch pushed yet | `engine/src/Components/Terrain.cpp`, `engine/include/okay/Components/Terrain.hpp`, `engine/include/okay/Components/Light.hpp` | - |
| [`claude2/item-cooldowns`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6898602014) | claude2 | stale | - | - |
| [`claude2/item-durability`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6897431646) | claude2 | stale | `InventoryOps.cpp`, `Equipment.cpp`, `inventory.md` | - |
| [`claude2/nan-damage`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6909830824) | claude2 | stale | `SurvivalComponents.hpp`, `Stats.hpp`, `damage/heal` | - |
| [`claude2/own-fbx-faster`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6918419300) | claude2 | stale | - | - |
| [`claude2/own-http-finish`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6929812582) | claude2 | stale | - | - |
| [`claude2/own-ttf-render`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6930052734) | claude2 | stale | - | - |
| [`claude2/perf-nav-fallback-hierarchy`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6915583630) | claude2 | stale | - | - |
| [`claude2/prone-stance`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895820832) | claude2 | stale | - | - |
| [`claude2/reduce-motion-bob`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6887483662) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/sdl-player-no-sdl`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6930897379) | claude2 | stale | - | - |
| [`claude2/serialize-object-numbers`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6902859455) | claude2 | stale | `engine/src/Scene/SceneSerializer.cpp`, `tests/test_scene_canonical.cpp` | - |
| [`claude2/software-dof`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895835704) | claude2 | stale | - | - |
| [`claude2/software-scene-ssao`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6896927774) | claude2 | stale | - | - |
| [`claude2/ui-nameplate-bound`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895434594) | claude2 | stale | `UITextBind.*`, `UIBarBind.*`, `UIOwnerData.*` | - |
| [`claude2/vehicle-seat`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6887652737) | claude2 | stale | `Seat.hpp`, `VehicleController.hpp`, `HumanoidActions.hpp/.cpp` | - |
| [`claude2/vs-net-link-pin`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6896966433) | claude2 | stale | `tests/test_vs_net_link_pin.cpp`, `editor/VisualScriptPalette.hpp`, `tests/test_vs_registry_effect.cpp` | - |
| [`claude2/vs-net-nodes`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6897312650) | claude2 | stale | - | - |
| [`claude2/wav-ffmpeg-diff`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6896454520) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/world-text-rich`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895661261) | claude2 | stale | - | - |

## Collision report

116 lane branches (`claude/*`, `claude2/*`, `claude3/*`) with changes against `origin/main`. 2702 branch pairs touch the same files (1388 serious). Generated by `scripts/collision_check.py`; plain rules in `docs/process/collaboration.md`.

### File and line overlaps between branches

| Branches | Same files (not shared files) | Same lines | Lanes |
|---|---|---|---|
| `claude/own-platform-6` + `claude2/own-switch-on-exit` | `cmake/OkayPlatform.cmake`, `docs/own_code_roadmap.md`, `docs/platform_layer.md`, `docs/sdl_inventory.md` +29 | `docs/own_code_roadmap.md` 26-26 (approx); `docs/sdl_inventory.md` 3-3 (approx); `editor/CMakeLists.txt` 19-19 (approx) | CROSS-LANE |
| `claude/own-platform-6` + `claude2/sdl-player-no-sdl` | `cmake/OkayPlatform.cmake`, `docs/own_code_roadmap.md`, `docs/platform_layer.md`, `docs/sdl_inventory.md` +27 | `docs/own_code_roadmap.md` 26-26 (approx); `docs/sdl_inventory.md` 3-3 (approx); `editor/CMakeLists.txt` 19-19 (approx) | CROSS-LANE |
| `claude/particles-2` + `claude/wind-2` | `docs/UNFINISHED.md`, `docs/rendering.md`, `docs/scripting.md`, `try_it/wind-field.md` +27 | `docs/UNFINISHED.md` 20-20 (approx); `docs/rendering.md` 140-140 (approx); `docs/scripting.md` 634-634 (approx) | same lane |
| `claude/particles-3` + `claude/wind-2` | `docs/UNFINISHED.md`, `docs/rendering.md`, `docs/scripting.md`, `try_it/wind-field.md` +27 | `docs/UNFINISHED.md` 20-20 (approx); `docs/rendering.md` 140-140 (approx); `docs/scripting.md` 634-634 (approx) | same lane |
| `claude/okayui-next-3` + `claude2/own-switch-on-exit` | `docs/imgui_port_report.md`, `docs/own_code_roadmap.md`, `docs/own_ui_toolkit.md`, `docs/platform_layer.md` +29 | `docs/imgui_port_report.md` 12-13 (approx); `docs/own_code_roadmap.md` 26-26 (approx); `docs/sdl_inventory.md` 3-3 (approx) | CROSS-LANE |
| `claude/own-code-editor-2` + `claude2/own-switch-on-exit` | `docs/UNFINISHED.md`, `docs/own_code_roadmap.md`, `docs/own_ui_code_editor.md`, `docs/own_ui_toolkit.md` +12 | `docs/own_code_roadmap.md` 49-49 (approx); `docs/own_ui_code_editor.md` 100-100 (approx); `editor/ScriptEditorCodeEdit.cpp` 10-10 (approx) | CROSS-LANE |
| `claude/okayui-next-3` + `claude2/sdl-player-no-sdl` | `docs/own_code_roadmap.md`, `docs/platform_layer.md`, `docs/sdl_inventory.md`, `editor/CMakeLists.txt` +17 | `docs/own_code_roadmap.md` 26-26 (approx); `docs/sdl_inventory.md` 3-3 (approx); `editor/CMakeLists.txt` 116-116 (approx) | CROSS-LANE |
| `claude/own-readers-hostile-1` + `claude2/modular-2` | `content/model_readers.md`, `content/own_readers_remaining.md`, `content/own_readers_speed.md`, `content/release_note_dropped_model_formats.md` +39 | `content/model_readers.md` 2160-2160 (approx); `content/own_readers_remaining.md` 49-50 (approx); `status/own_stack_rollout.md` 63-63 (approx) | CROSS-LANE |
| `claude/build-settings-3` + `claude/project-upgrade-save-2` | `try_it/87-project-upgrade-and-saving-fixes.md`, `editor/ProjectState.hpp`, `editor/ProjectUpgrade.cpp`, `editor/ProjectUpgrade.hpp` +10 | `editor/ProjectState.hpp` 196-196 (approx); `editor/ProjectUpgrade.cpp` 130-130 (approx); `editor/ProjectUpgrade.hpp` 34-34 (approx) | same lane |
| `claude2/dup-guard` + `claude2/modular-2` | `.github/PULL_REQUEST_TEMPLATE.md`, `workflows/branch-lint.yml`, `workflows/ci.yml`, `workflows/dup-scan.yml` +46 | `workflows/branch-lint.yml` 1-3 (approx); `process/README.md` 11-11 (approx); `process/collaboration.md` 122-122 (approx) | same lane |
| `claude/vehicle-2` + `claude/vehicle-3` | `editor/VehicleEditorModel.hpp`, `editor/VehicleEditorUi.hpp`, `editor/main.cpp`, `Components/VehicleController.hpp` +7 | `editor/VehicleEditorUi.hpp` 4-5 (approx); `Components/VehicleSeat.hpp` 238-238 (approx); `Scene/SceneTemplates.hpp` 889-889 (approx) | same lane |
| `claude2/modular-2` + `claude2/one-health` | `editor/main.cpp`, `Components/Damage.hpp`, `AI/BehaviorTree.cpp`, `AI/BehaviorTreeRunner.cpp` +4 | `Components/Damage.hpp` 102-102 (approx); `AI/BehaviorTree.cpp` 413-413 (approx); `AI/BehaviorTreeRunner.cpp` 5-5 (approx) | same lane |

... and 2690 more pairs (run the script for all).

### Shared and generated files touched by several branches (handshake needed)

| File | Branches |
|---|---|
| `tests/CMakeLists.txt` (lanes: claude2, coordinator) | 99: `claude/add-component-menu-2`, `claude/anim-blend-1`, `claude/area1-1`, `claude/build-settings-3`, `claude/controller-1` ... |
| `docs/feature_map/_index_az.md` (generated) (lanes: claude2, coordinator) | 61: `claude/build-settings-3`, `claude/core-jobs-1`, `claude/csharp-host-1`, `claude/ecs-1`, `claude/grok-stack` ... |
| `docs/FEATURE_MAP.md` (generated) (lanes: claude2, coordinator) | 46: `claude/csharp-host-1`, `claude/ecs-1`, `claude/installer-honest`, `claude/own-code-editor-2`, `claude/particles-2` ... |
| `docs/status/UNFINISHED.md` (lanes: claude2, coordinator) | 40: `claude/anim-blend-1`, `claude/controller-1`, `claude/controller-2`, `claude/inspector-1`, `claude/integration-1` ... |
| `engine/CMakeLists.txt` (lanes: claude2, coordinator) | 39: `claude/csharp-host-1`, `claude/ecs-1`, `claude/own-platform-6`, `claude/own-readers-hostile-1`, `claude/particles-2` ... |
| `docs/try_it.md` (lanes: claude2, coordinator) | 30: `claude/add-component-menu-2`, `claude/build-settings-3`, `claude/core-jobs-1`, `claude/grok-stack`, `claude/installer-honest` ... |
| `docs/feature_map/_index_symptoms.md` (generated) (lanes: claude2, coordinator) | 26: `claude/csharp-host-1`, `claude/ecs-1`, `claude/grok-stack`, `claude/installer-honest`, `claude/own-code-editor-2` ... |
| `engine/src/Scene/SceneSerializer.cpp` (lanes: claude2, coordinator) | 26: `claude/grok-stack`, `claude/particles-2`, `claude/particles-3`, `claude/player-deep`, `claude/time-1` ... |
| `engine/src/Scripting/OkayScriptVM.cpp` (lanes: claude2, coordinator) | 16: `claude/particles-2`, `claude/particles-3`, `claude/time-1`, `claude/wind-2`, `claude2/active-ragdoll` ... |
| `CMakeLists.txt` (lanes: claude2, coordinator) | 15: `claude/csharp-host-1`, `claude/okayui-next-3`, `claude/own-platform-6`, `claude/own-readers-hostile-1`, `claude/windows-sdk-1` ... |

### Same test name, test file or CMake target in more than one place

- test `xbox_uwp_padhub`: `claude/anim-blend-1`, `claude/controller-1`, `claude/inspector-1`, `claude/integration-1`, `claude/vehicle-2`, `claude/verify-1`, `claude/xbox-glue-1`
- test `inventory_textfit`: `claude/anim-blend-1`, `claude/inspector-1`, `claude/integration-1`, `main`
- test `vehicle_editor_model`: `claude/anim-blend-1`, `claude/inspector-1`, `claude/integration-1`, `claude/vehicle-2`, `main`
- test `bmp_writer`: `claude/anim-blend-1`, `claude/controller-1`, `claude/inspector-1`, `claude/integration-1`, `claude/vehicle-2`, `claude/verify-1`, `claude/xbox-glue-1`, `main`
- test `walk_dynamics`: `claude/anim-blend-1`, `main`
- test `build_settings`: `claude/build-settings-3`, `main`
- test `controller_feel2`: `claude/controller-1`, `claude/controller-2`
- test `controller_tuning`: `claude/controller-1`, `main`
- test `jobs_adoption`: `claude/core-jobs-1`, `main`
- test `lua_fuzz`: `claude/csharp-host-1`, `claude/scripting-migration-1`
- test `lua_lang`: `claude/csharp-host-1`, `claude/scripting-migration-1`
- test `lua_binding`: `claude/csharp-host-1`, `claude/scripting-migration-1`
- test `scene_bom_bug_load_from_file`: `claude/launcher-open-gate`, `claude/upgrade-stack`
- test `startup_modals`: `claude/launcher-open-gate`, `claude/upgrade-stack`
- test `okayui_sceneview`: `claude/okayui-next-3`, `main`

### The same new code on two open branches (exact copies of 80+ tokens)

- `claude/csharp-host-1` `engine/src/Lua/LuaCompile.cpp:25-26` = `claude2/lua-csharp-merge` `editor/main.cpp:15880-15881` (88 tokens)
- `claude/scripting-migration-1` `engine/src/Lua/LuaCompile.cpp:25-26` = `claude2/lua-csharp-merge` `editor/main.cpp:15880-15881` (88 tokens)
- `claude/csharp-host-1` `engine/src/Lua/LuaCompile.cpp:25-26` = `claude2/lua-scripting` `editor/main.cpp:15780-15781` (88 tokens)
- `claude/scripting-migration-1` `engine/src/Lua/LuaCompile.cpp:25-26` = `claude2/lua-scripting` `editor/main.cpp:15780-15781` (88 tokens)
- `claude/controller-1` `editor/main.cpp:28232-38529` = `claude/controller-2` `editor/main.cpp:28593-44623` (525 tokens)
- `claude/csharp-host-1` `engine/src/Lua/LuaCompile.cpp:23-28` = `claude2/lua-scripting` `engine/src/Lua/LuaCompiler.cpp:29-34` (101 tokens)
- `claude/scripting-migration-1` `engine/src/Lua/LuaCompile.cpp:23-28` = `claude2/lua-scripting` `engine/src/Lua/LuaCompiler.cpp:29-34` (101 tokens)
- `claude/csharp-host-1` `engine/src/Lua/LuaCompile.cpp:23-28` = `claude2/lua-csharp-merge` `engine/src/Lua/LuaCompiler.cpp:29-34` (101 tokens)
- `claude/scripting-migration-1` `engine/src/Lua/LuaCompile.cpp:23-28` = `claude2/lua-csharp-merge` `engine/src/Lua/LuaCompiler.cpp:29-34` (101 tokens)
- `claude/particles-3` `editor/main.cpp:31227-31292` = `claude/wind-2` `editor/main.cpp:31246-42086` (101 tokens)
- `claude/particles-2` `editor/main.cpp:31227-31292` = `claude/wind-2` `editor/main.cpp:31246-42086` (101 tokens)
- `claude/csharp-host-1` `engine/src/Lua/LuaCompile.cpp:25-26` = `claude2/lua-csharp-merge` `engine/src/Lua/LuaScriptVM.cpp:29-31` (93 tokens)
- `claude/scripting-migration-1` `engine/src/Lua/LuaCompile.cpp:25-26` = `claude2/lua-csharp-merge` `engine/src/Lua/LuaScriptVM.cpp:29-31` (93 tokens)
- `claude/add-component-menu-2` `editor/AddComponentModel.cpp:427-436` = `claude2/lua-csharp-merge` `engine/src/Lua/LuaScriptVM.cpp:739-746` (131 tokens)
- `claude/csharp-host-1` `engine/src/Lua/LuaCompile.cpp:25-26` = `claude2/lua-scripting` `engine/src/Lua/LuaScriptVM.cpp:29-31` (93 tokens)
- `claude/scripting-migration-1` `engine/src/Lua/LuaCompile.cpp:25-26` = `claude2/lua-scripting` `engine/src/Lua/LuaScriptVM.cpp:29-31` (93 tokens)
- `claude/add-component-menu-2` `editor/AddComponentModel.cpp:427-436` = `claude2/lua-scripting` `engine/src/Lua/LuaScriptVM.cpp:724-731` (131 tokens)
- `claude/scripting-migration-1` `engine/src/Lua/LuaLib.cpp:116-125` = `claude2/lua-csharp-merge` `engine/src/Lua/LuaLibString.cpp:103-118` (111 tokens)
- `claude/scripting-migration-1` `engine/src/Lua/LuaLib.cpp:116-125` = `claude2/lua-scripting` `engine/src/Lua/LuaLibString.cpp:100-115` (111 tokens)
- `claude/scripting-migration-1` `engine/src/Lua/LuaLib.cpp:126-136` = `claude2/lua-csharp-merge` `engine/src/Lua/LuaLibString.cpp:119-129` (82 tokens)

### Similar new names on two open branches

- `claude/add-component-menu-2` TestPrefs (`tests/test_add_component_model.cpp:218`) ~ `claude/okayms-3-r23b` test_refs (`tools/okayms/tests/test_copy.cpp:163`): spelled almost the same
- `claude/add-component-menu-2` TestWords (`tests/test_add_component_model.cpp:64`) ~ `claude/own-code-editor-2` TestWords (`okayui/tests/test_text_buffer.cpp:251`): same name
- `claude/add-component-menu-2` EditDistance (`editor/AddComponentModel.cpp:431`) ~ `claude2/lua-csharp-merge` EditDistance (`engine/src/Lua/LuaScriptVM.cpp:741`): same name
- `claude/add-component-menu-2` EditDistance (`editor/AddComponentModel.cpp:431`) ~ `claude2/lua-scripting` EditDistance (`engine/src/Lua/LuaScriptVM.cpp:726`): same name
- `claude/add-component-menu-2` DrawStar (`editor/AddComponentMenu.cpp:50`) ~ `claude2/own-switch-on-exit` DrawStar (`okayui/screens/OkayUILauncherLook.hpp:142`): same name
- `claude/build-settings-3` XmlEscape (`editor/BuildOutputTexts.hpp:60`) ~ `claude/csharp-host-1` XmlEscape (`engine/src/CSharp/CSharpCompiler.cpp:106`): same name
- `claude/build-settings-3` SceneKeyForProject (`editor/ProjectUpgrade.hpp:38`) ~ `claude/project-upgrade-save-2` SceneKeyForProject (`editor/ProjectUpgrade.cpp:131`): same name
- `claude/build-settings-3` NowText (`editor/BuildSettingsModel.hpp:765`) ~ `claude2/okayui-inspector-9` RowText (`editor/main.cpp:32651`): spelled almost the same
- `claude/build-settings-3` CleanName (`editor/BuildSettingsModel.hpp:477`) ~ `claude2/own-fbx-faster` CleanName (`engine/src/Render/FbxScene.cpp:264`): same name
- `claude/build-settings-3` NowText (`editor/BuildSettingsModel.hpp:765`) ~ `claude2/own-switch-on-exit` RowText (`okayui/screens/OkayUILauncherMorePages.cpp:669`): spelled almost the same
- `claude/core-jobs-1` TestOffByDefault (`tests/test_memory_track.cpp:49`) ~ `claude/okayms-3-r23b` test_off_by_default (`tools/okayms/tests/test_lines.cpp:211`): same name ignoring case and _
- `claude/core-jobs-1` MakeProject (`tests/test_jobs_adoption.cpp:25`) ~ `claude/versions-safety` MakeProject (`tests/test_versions_safety.cpp:101`): same name
- `claude/core-jobs-1` NowMs (`tests/test_jobs_adoption.cpp:21`) ~ `claude2/own-physics-engine` NowMs (`engine/src/Physics/Core/World.cpp:14`): same name
- `claude/core-jobs-1` NowMs (`tests/test_jobs_adoption.cpp:21`) ~ `claude2/physics-no-classic` NowMs (`engine/src/Physics/Core/World.cpp:15`): same name
- `claude/csharp-host-1` FakeBackend (`tests/test_csharp_host.cpp:48`) ~ `claude/okayui-next-3` FakeBackend (`okayui/tests/FakePlatformBackend.hpp:10`): same name
- `claude/csharp-host-1` ReadAll (`engine/src/CSharp/CSharpCompiler.cpp:178`) ~ `claude/own-code-editor-2` ReadAll (`tests/test_script_editor_sample.cpp:19`): same name
- `claude/csharp-host-1` ReadAll (`engine/src/CSharp/CSharpCompiler.cpp:178`) ~ `claude/own-readers-hostile-1` ReadAll (`tests/test_own_e4_samples.cpp:25`): same name
- `claude/csharp-host-1` CoStatus (`engine/src/Lua/LuaCore.hpp:117`) ~ `claude2/lua-csharp-merge` CoStatus (`engine/include/okay/Lua/Lua.hpp:173`): same name
- `claude/csharp-host-1` LuaError (`engine/src/Lua/LuaCore.hpp:135`) ~ `claude2/lua-csharp-merge` LuaError (`engine/include/okay/Lua/Lua.hpp:194`): same name
- `claude/csharp-host-1` SyntaxErr (`tests/test_lua_lang.cpp:37`) ~ `claude2/lua-csharp-merge` SyntaxError (`engine/include/okay/Lua/Lua.hpp:200`): spelled almost the same

### Branches touching files outside their lane

- `claude/build-settings-3` (coordinator): 4 files, e.g. `editor/BuildSettingsModel.hpp`, `editor/BuildSettingsWindow.cpp`, `editor/BuildSettingsWindow.hpp`
- `claude/csharp-host-1` (coordinator): 16 files, e.g. `engine/include/okay/Lua/Lua.hpp`, `engine/include/okay/Lua/LuaScriptVM.hpp`, `engine/src/Lua/LuaCompile.cpp`
- `claude/grok-stack` (coordinator): 3 files, e.g. `engine/include/okay/Physics/JointSpring.hpp`, `engine/src/Physics/Physics2D.cpp`, `engine/src/Physics/Physics3D.cpp`
- `claude/own-codeedit-3` (coordinator): 1 files, e.g. `editor/MainShared.hpp`
- `claude/scripting-migration-1` (coordinator): 13 files, e.g. `engine/include/okay/Lua/Lua.hpp`, `engine/include/okay/Lua/LuaScriptVM.hpp`, `engine/src/Lua/LuaCompile.cpp`
- `claude/vs-forward-fix-1` (coordinator): 1 files, e.g. `tests/test_physics_queries_ext.cpp`
- `claude/windows-tests-fix-1` (coordinator): 1 files, e.g. `tests/test_build_settings.cpp`
- `claude2/active-ragdoll` (claude2): 8 files, e.g. `editor/VisualScriptPlaceRules.hpp`, `editor/main.cpp`, `engine/include/okay/Components/RagdollShared.hpp`
- `claude2/area1-core-3` (claude2): 14 files, e.g. `editor/VisualScriptLegacyPlaces.hpp`, `editor/VisualScriptPlaceRules.hpp`, `editor/main.cpp`
- `claude2/area2-render-2` (claude2): 12 files, e.g. `engine/include/okay/Render/Atmosphere.hpp`, `engine/include/okay/Render/DofMath.hpp`, `engine/include/okay/Render/GLRenderer.hpp`
- `claude2/area3-mesh-tools` (claude2): 6 files, e.g. `editor/main.cpp`, `engine/include/okay/Modeling/PolyOps.hpp`, `engine/include/okay/Render/Mesh.hpp`
- `claude2/build-export-fix` (claude2): 9 files, e.g. `editor/BuildSettingsModel.hpp`, `editor/BuildSettingsWindow.cpp`, `editor/LocalizationWindow.cpp`
- `claude2/csharp-scripting` (claude2): 16 files, e.g. `editor/main.cpp`, `engine/include/okay/Scripting/NextScriptVM.hpp`, `engine/include/okay/Scripting/ScriptVM.hpp`
- `claude2/csharp-scripting-2` (claude2): 22 files, e.g. `editor/main.cpp`, `engine/include/okay/Scripting/CSharpAssist.hpp`, `engine/include/okay/Scripting/NextScriptVM.hpp`
- `claude2/dedup-small` (claude2): 37 files, e.g. `editor/AnimComponentNames.hpp`, `editor/EditorWidgets.cpp`, `editor/VisualScriptFriendly.hpp`

### Generated files changed without their source (edited by hand?)

- `claude2/modular-s0-build` changed `docs/engine/code_index.md` only; regenerate with `python3 scripts/gen_code_index.py`

### Branches with no claim and no PR (pushed in the last 7 days)

`claude/add-component-menu-2`, `claude/anim-blend-1`, `claude/area1-1`, `claude/build-settings-3`, `claude/controller-1`, `claude/core-jobs-1`, `claude/csharp-host-1`, `claude/ecs-1`, `claude/inspector-1`, `claude/inspector-collapse-1`, `claude/integration-1`, `claude/okay-build-1`, `claude/okay-build-2`, `claude/okayms-3-r23b`, `claude/okayui-next-3`, `claude/own-codeedit-3`, `claude/own-platform-6`, `claude/own-readers-hostile-1`, `claude/own-third-party-1`, `claude/particles-2`, `claude/particles-3`, `claude/project-upgrade-save-2`, `claude/script-profiler-1`, `claude/script-protect-1`, `claude/scripting-migration-1`, `claude/time-1`, `claude/ui-designer-1`, `claude/ui-designer-2`, `claude/ui-designer-3`, `claude/ui-designer-4` ... (+31)

