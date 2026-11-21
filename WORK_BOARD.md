# Live work board

Generated 2026-11-21 16:24 UTC by `scripts/work_board.py` (a scheduled workflow rewrites this about every 10 minutes). Rule 6 (`CLAUDE.md`): a PR merges only when every CI job is finished and green.

Hand-written parts (agents, owner decisions): [docs/status/work_board.md](https://github.com/kingimann/OkaySpaceGameEngine/blob/main/docs/status/work_board.md).

## Open pull requests (10)

| PR | Title | State | CI | Updated |
|---|---|---|---|---|
| [#1053](https://github.com/kingimann/OkaySpaceGameEngine/pull/1053) | Dev picture tools use our own BMP writer instead of SDL_SaveBMP | draft, ready to merge | 17 green, 0 running, 0 queued, 0 red | 10 h ago |
| [#1055](https://github.com/kingimann/OkaySpaceGameEngine/pull/1055) | Own Win32 IME: clause boundaries and focus-loss clearing, tested | ready to merge | 1 green, 0 running, 0 queued, 0 red | 14 h ago |
| [#1058](https://github.com/kingimann/OkaySpaceGameEngine/pull/1058) | Xbox UWP: touch tracker and text composition, wired into the backend (Linux-tested) | draft, ready to merge | 17 green, 0 running, 0 queued, 0 red | 10 h ago |
| [#1059](https://github.com/kingimann/OkaySpaceGameEngine/pull/1059) | LedgeGrab: hands go on the ledge (LedgeHandsIK component driving LimbIK), headless-tested | draft, ready to merge | 17 green, 0 running, 0 queued, 0 red | 10 h ago |
| [#1074](https://github.com/kingimann/OkaySpaceGameEngine/pull/1074) | Windows tests: 140 failures down to 32 (paths, line endings, a UTF-8 crash, test fixes) | draft, red | 18 green, 0 running, 0 queued, 2 red (Cross-compile Windows build (MinGW), Build Windows tests (MinGW cross-compile)) | 2 h ago |
| [#1088](https://github.com/kingimann/OkaySpaceGameEngine/pull/1088) | Image decoders: hostile-input test and a fix for a 930 MB animated GIF memory spike | draft, red | 17 green, 0 running, 0 queued, 1 red (Scripting and robustness tests (ASan + UBSan)) | 1 h ago |
| [#1090](https://github.com/kingimann/OkaySpaceGameEngine/pull/1090) | Own OGRE reader: binary .skeleton files (off by default, OKAY_OWN_OGRE_SKELBIN) | ready to merge | 18 green, 0 running, 0 queued, 0 red | 1 h ago |
| [#1091](https://github.com/kingimann/OkaySpaceGameEngine/pull/1091) | Health and damage: one damage path for HealthStat, SurvivalStats, Stats and NPCController  | draft, ready to merge | 1 green, 0 running, 0 queued, 0 red | 80 min ago |
| [#1092](https://github.com/kingimann/OkaySpaceGameEngine/pull/1092) | Colliders: real Cylinder, Cone, Convex Hull, Compound, Plane and Heightfield shapes, harde | draft, ready to merge | 1 green, 0 running, 0 queued, 0 red | 70 min ago |
| [#1093](https://github.com/kingimann/OkaySpaceGameEngine/pull/1093) | Fixes #888 findings 145, 146, 153, 155: terrain resize keeps sculpting, lights under a swi | waiting | 17 green, 1 running, 0 queued, 0 red | 38 min ago |

## Recently merged

| PR | Title | Merged |
|---|---|---|
| [#1089](https://github.com/kingimann/OkaySpaceGameEngine/pull/1089) | Editor no longer crashes on std::exit with our own window layer | 37 min ago |
| [#1087](https://github.com/kingimann/OkaySpaceGameEngine/pull/1087) | Release 2.901.0 | 2 h ago |
| [#1084](https://github.com/kingimann/OkaySpaceGameEngine/pull/1084) | Camera: zones, presets, and CameraFollow/NaN/orbit robustness fixes | 4 h ago |
| [#1082](https://github.com/kingimann/OkaySpaceGameEngine/pull/1082) | Animation: slerp linear rotation keys, angle-based key reduction, sign-flip fix | 4 h ago |
| [#1086](https://github.com/kingimann/OkaySpaceGameEngine/pull/1086) | Release 2.900.0 | 4 h ago |
| [#1071](https://github.com/kingimann/OkaySpaceGameEngine/pull/1071) | Inventory: tool wear on Use, wheel and use-key arbiter, item names fitted to slot width | 5 h ago |
| [#1083](https://github.com/kingimann/OkaySpaceGameEngine/pull/1083) | Colliders: real Cylinder, Cone, Convex Hull, Compound, Plane, Heightfield; 2D rotating cap | 6 h ago |
| [#1076](https://github.com/kingimann/OkaySpaceGameEngine/pull/1076) | Vehicles: street-style control scheme with remapping, seat fit and safer exit (pure logic  | 6 h ago |

## Branches by `claude/*` (main session and its agents) and `claude2/*` (second Claude), newest first

| Branch | Last commit | Head | When |
|---|---|---|---|
| `claude/colliders-1` | Colliders: hardening (NaN / huge / flat / empty input, size caps, hull hang fix) | `5671f76` | 1 h ago |
| `claude/controller-1` | One plain controller Inspector (15 settings, feature boxes), registry label/unit | `b476e12` | 2 h ago |
| `claude/area1-1` | WIP (agent stopped by the usage limit, NOT built or tested) | `df7ebc1` | 2 h ago |
| `claude/anim-blend-1` | Add headless transition-pop test (unrun draft): per-frame vertex speed through i | `0269cb0` | 12 h ago |
| `claude/add-component-menu-2` | docs: feature map for the Add Component menu round 2 | `25de362` | 1 d ago |
| `claude/csharp-host-1` | C# host: export of runtime and scripts, docs records (migration section 8, featu | `4de3846` | 1 d ago |
| `claude/core-jobs-1` | Jobs: profiler bridge (queue, helping, per-priority), parallel scan hashing and  | `aaccc65` | 1 d ago |
| `claude/ecs-1` | ECS upgrade: ASan/UBSan/LSan results for ecs_refs and ecs_fuzz | `4f9b838` | 2 d ago |
| `claude/build-settings-3` | Build Settings round 3 (3): player reads the new keys, window pages for Web/Andr | `92c62c8` | 2 d ago |
| `claude/add-component-menu-1` | Add Component browser: include the new header in main.cpp | `9096222` | 2 d ago |
| `claude/core-assets-1` | Docs: asset manager locking and TSan note | `8a30337` | 2 d ago |
| `claude/core-perf-1` | Jobs: small loops stay on the shared-counter path; measured numbers in docs | `aec900f` | 2 d ago |
| `claude/easy-graph-usage-1` | docs: UNFINISHED row for the Visual Script usage work | `0c224fe` | 3 d ago |
| `claude/easy-modeling-1` | Advanced Modeling: foldable page list (chevron, Ctrl+\, thin strip, auto-fold wh | `b01eacd` | 3 d ago |
| `claude/faster-latest` | Release: mark latest as soon as the Windows programs are published (do not wait  | `c5b0716` | 4 d ago |
| `claude/editor-saves-1` | Editor saving and loading: atomic writes everywhere, outside-change warning, rec | `3e52c8a` | 4 d ago |
| `claude/foundation-debugger-1` | OkayScript text debugger: line breakpoints, stepping, call stack, watch, Script  | `91ef5dd` | 4 d ago |
| `claude/foundation-matgraph-1` | Material graph foundation: .okaymatgraph asset, 28 nodes, CPU evaluator, GLSL 12 | `ceb7ba6` | 4 d ago |
| `claude/foundation-gpu-1` | GPU backend foundation: shared contract, backend chooser, off-by-default Vulkan  | `19357f6` | 4 d ago |
| `claude/editor-two-themes` | Merge origin/main into claude/editor-two-themes (Add Node menu, inflate, release | `9693618` | 5 d ago |
| `claude/grok-stack` | Try-it: items 5 to 8 for the physics, spring joint, navigation and foliage fixes | `ac77203` | 5 d ago |
| `claude/collab-appendonly` | Collaboration lint: append-only files never block, a bare Agree counts | `68a67d5` | 5 d ago |
| `claude/focused-feynman-kwy51j` | Own model readers e5: TER, MDC, HMP, CSM, Q3D, SIB, RAW, IQM, COB, NDO, PMX, LWS | `24efceb` | 5 d ago |
| `claude/flaky-timing-tests` | Tests: make wall-clock checks robust on slow CI runners (calibrated, best-of-N,  | `daf31f5` | 6 d ago |
| `claude/close-test-gaps` | Close test gaps: ApiTimed stall, many distinct XGL meshes, seam refusals | `68b37f2` | 6 d ago |
| `claude/core-assets` | Core assets review fixes: open by the caller's path, UNC/long-path keys, watcher | `ef9fd38` | 6 d ago |
| `claude/export-honesty` | Editor: honest Web/Android export labels and an export report | `88fbd33` | 6 d ago |
| `claude/core-loc-thumbs` | Localization window: import preview, JSON, font check, RTL order; font and data  | `abbce1e` | 6 d ago |
| `claude/core-prefab-mode` | Prefabs: re-apply keeps objects added under a plain-file instance | `3f2b80b` | 6 d ago |
| `claude/core-pool-reflect` | Core 1.13, 1.19: register 8 more component types, docs show pooling is finished | `6c7a899` | 6 d ago |
| `claude/crypto-review-grok` | Review request for an outside AI reviewer of okay::crypto (branch only) | `b3428c8` | 6 d ago |
| `claude/asset-licences-notices` | Asset licence inventory and third-party notices in release packages | `147033f` | 6 d ago |
| `claude/foundation-gi-probes` | GI design page: add provenance section | `d386d61` | 6 d ago |
| `claude/foundation-material-graph` | Material graph: replace the noise hash with our own formula (no code from other  | `09fdd49` | 6 d ago |
| `claude/foundation-world-streaming` | World streaming phase 1: PartitionStreamer, budgeted and sync loaders, saved cel | `ffe6cb8` | 6 d ago |
| `claude/foundation-mesh-lod` | Virtual geometry phase 2 start: meshlet builder and CPU culling reference, with  | `d529251` | 7 d ago |
| `claude/align-distribute-mirror-rebased` | Fix the three new editor tests for main's undo and engine names | `a1323de` | 7 d ago |
| `claude/csharp-getcomponent` | Docs: unfinished row for 13.2 lists only what is still open | `00fd4be` | 7 d ago |
| `claude/csharp-coroutines` | Docs: typed OkayScript coroutines are in the feature map and unfinished rows | `e7ddb2a` | 7 d ago |
| `claude/csharp-try` | Docs: typed OkayScript try / catch / finally is in the feature map and unfinishe | `d37268d` | 7 d ago |

## Latest on the coordination thread (#422)

- 6 d ago: [Claude 2: `claude2/prone-stance` (`857eebd`) green](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6896227136)
- 6 d ago: [Claude 2: `claude2/world-text-rich` engine-linked results](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6896109744)
- 6 d ago: [Claude 2: progress on the four items](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895927754)
- 6 d ago: [Claim: `claude2/software-dof` (item 4, depth of field in the software renderer).](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895835704)
- 6 d ago: [Claim and result: `claude2/prone-stance` pushed at `857eebd`. NOT built yet (queued).](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895820832)

## Claims (issue #422)

| Branch | Lane | Status | Files | Problems |
|---|---|---|---|---|
| [`claude2/grok-145-146-153-155`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7064574409) | claude2 | active | `engine/src/Components/Terrain.cpp`, `engine/include/okay/Components/Terrain.hpp`, `engine/include/okay/Components/Light.hpp` | missing Gap; tests/CMakeLists.txt overlaps claude2/own-ogre-skeleton-bin (tests/CMakeLists.txt); release or agree first |
| [`claude2/okayui-inspector-9`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7063279981) | claude2 | active | `shared/InspectorFieldsModel.hpp`, `okayui/screens/OkayUIEditorPanels.*`, `editor/EditorPanelViews.cpp` | shared/InspectorFieldsModel.hpp is in the coordinator lane (needs 'Agree: claude2/okayui-inspector-9' from that lane); editor/EditorPanelVie |
| [`claude2/own-ogre-skeleton-bin`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7064255038) | claude2 | active | `engine/src/Render/OgreXmlReader.cpp`, `engine/include/okay/Render/ModelData.hpp`, `tests/test_ogre_skelbin_reader.cpp` | missing Try-it; tests/CMakeLists.txt overlaps claude2/grok-145-146-153-155 (tests/CMakeLists.txt); release or agree first |
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

106 claude/* and claude2/* branches with changes against `origin/main`. 2286 branch pairs touch the same files (871 serious). Generated by `scripts/collision_check.py`; plain rules in `docs/process/collaboration.md`.

### File and line overlaps between branches

| Branches | Same files (not shared files) | Same lines | Lanes |
|---|---|---|---|
| `claude2/okayui-inspector-8` + `claude2/own-switch-on` | `docs/UNFINISHED.md`, `docs/editor.md`, `okayui/children_tooltip_shapes.png`, `okayui/dockbuilder_tall_preset.png` +102 | `docs/editor.md` 187-187 (approx); `docs/imgui_port_report.md` 12-14 (approx); `docs/navigation.md` 242-242 (approx) | same lane |
| `claude2/okayui-inspector-8` + `claude2/own-switch-on-exit` | `docs/UNFINISHED.md`, `docs/editor.md`, `okayui/children_tooltip_shapes.png`, `okayui/dockbuilder_tall_preset.png` +102 | `docs/editor.md` 187-187 (approx); `docs/imgui_port_report.md` 12-14 (approx); `docs/navigation.md` 242-242 (approx) | same lane |
| `claude2/okayui-inspector-9` + `claude2/own-switch-on` | `docs/UNFINISHED.md`, `docs/editor.md`, `okayui/children_tooltip_shapes.png`, `okayui/dockbuilder_tall_preset.png` +102 | `docs/editor.md` 187-187 (approx); `docs/imgui_port_report.md` 12-14 (approx); `docs/navigation.md` 242-242 (approx) | same lane |
| `claude2/okayui-inspector-9` + `claude2/own-switch-on-exit` | `docs/UNFINISHED.md`, `docs/editor.md`, `okayui/children_tooltip_shapes.png`, `okayui/dockbuilder_tall_preset.png` +102 | `docs/editor.md` 187-187 (approx); `docs/imgui_port_report.md` 12-14 (approx); `docs/navigation.md` 242-242 (approx) | same lane |
| `claude2/okayui-inspector-5` + `claude2/own-switch-on` | `docs/UNFINISHED.md`, `docs/editor.md`, `okayui/children_tooltip_shapes.png`, `okayui/dockbuilder_tall_preset.png` +101 | `docs/editor.md` 187-187 (approx); `docs/imgui_port_report.md` 12-14 (approx); `docs/navigation.md` 242-242 (approx) | same lane |
| `claude2/okayui-inspector-5` + `claude2/own-switch-on-exit` | `docs/UNFINISHED.md`, `docs/editor.md`, `okayui/children_tooltip_shapes.png`, `okayui/dockbuilder_tall_preset.png` +101 | `docs/editor.md` 187-187 (approx); `docs/imgui_port_report.md` 12-14 (approx); `docs/navigation.md` 242-242 (approx) | same lane |
| `claude2/okayui-inspector-6` + `claude2/own-switch-on` | `docs/UNFINISHED.md`, `docs/editor.md`, `okayui/children_tooltip_shapes.png`, `okayui/dockbuilder_tall_preset.png` +101 | `docs/editor.md` 187-187 (approx); `docs/imgui_port_report.md` 12-14 (approx); `docs/navigation.md` 242-242 (approx) | same lane |
| `claude2/okayui-inspector-6` + `claude2/own-switch-on-exit` | `docs/UNFINISHED.md`, `docs/editor.md`, `okayui/children_tooltip_shapes.png`, `okayui/dockbuilder_tall_preset.png` +101 | `docs/editor.md` 187-187 (approx); `docs/imgui_port_report.md` 12-14 (approx); `docs/navigation.md` 242-242 (approx) | same lane |
| `claude2/okayui-inspector-7` + `claude2/own-switch-on` | `docs/UNFINISHED.md`, `docs/editor.md`, `okayui/children_tooltip_shapes.png`, `okayui/dockbuilder_tall_preset.png` +101 | `docs/editor.md` 187-187 (approx); `docs/imgui_port_report.md` 12-14 (approx); `docs/navigation.md` 242-242 (approx) | same lane |
| `claude2/okayui-inspector-7` + `claude2/own-switch-on-exit` | `docs/UNFINISHED.md`, `docs/editor.md`, `okayui/children_tooltip_shapes.png`, `okayui/dockbuilder_tall_preset.png` +101 | `docs/editor.md` 187-187 (approx); `docs/imgui_port_report.md` 12-14 (approx); `docs/navigation.md` 242-242 (approx) | same lane |
| `claude/okaybuild-5` + `claude/tools-merge-1` | `docs/own_build_plan.md`, `docs/own_code_roadmap.md`, `docs/own_compiler_plan.md`, `docs/own_msvc_plan.md` +111 | `docs/own_build_plan.md` 7-8 (approx); `docs/own_code_roadmap.md` 54-54 (approx); `docs/own_compiler_plan.md` 14-14 (approx) | same lane |
| `claude/own-platform-5` + `claude2/own-switch-on` | `cmake/OkayPlatform.cmake`, `docs/own_code_roadmap.md`, `docs/platform_layer.md`, `docs/sdl_inventory.md` +32 | `cmake/OkayPlatform.cmake` 45-45 (approx); `docs/own_code_roadmap.md` 26-26 (approx); `docs/platform_layer.md` 309-310 (approx) | CROSS-LANE |

... and 2274 more pairs (run the script for all).

### Shared and generated files touched by several branches (handshake needed)

| File | Branches |
|---|---|
| `tests/CMakeLists.txt` | 88: `claude/add-component-menu-1`, `claude/add-component-menu-2`, `claude/anim-blend-1`, `claude/area1-1`, `claude/build-settings-3` ... |
| `docs/feature_map/_index_az.md` (generated) | 49: `claude/build-settings-3`, `claude/core-assets-1`, `claude/core-jobs-1`, `claude/core-perf-1`, `claude/csharp-host-1` ... |
| `docs/try_it.md` | 41: `claude/add-component-menu-1`, `claude/add-component-menu-2`, `claude/build-settings-3`, `claude/core-jobs-1`, `claude/core-perf-1` ... |
| `docs/FEATURE_MAP.md` (generated) | 28: `claude/core-assets-1`, `claude/csharp-host-1`, `claude/easy-modeling-1`, `claude/ecs-1`, `claude/health-1` ... |
| `engine/CMakeLists.txt` | 23: `claude/colliders-1`, `claude/core-assets-1`, `claude/csharp-host-1`, `claude/ecs-1`, `claude/health-1` ... |
| `docs/feature_map/_index_symptoms.md` (generated) | 20: `claude/csharp-host-1`, `claude/ecs-1`, `claude/grok-stack`, `claude/installer-honest`, `claude/own-code-editor-2` ... |
| `engine/src/Scene/SceneSerializer.cpp` | 15: `claude/colliders-1`, `claude/core-assets-1`, `claude/grok-stack`, `claude/health-1`, `claude/particles-2` ... |
| `docs/feature_map/15-editor.md` | 13: `claude/add-component-menu-1`, `claude/add-component-menu-2`, `claude/project-layout-1`, `claude/project-tabs-1`, `claude/scene-right-click-add-1` ... |
| `docs/status/UNFINISHED.md` | 11: `claude/anim-blend-1`, `claude/colliders-1`, `claude/controller-1`, `claude/health-1`, `claude/inspector-1` ... |
| `docs/README.md` | 11: `claude/csharp-host-1`, `claude/installer-honest`, `claude/own-code-editor-2`, `claude/scripting-migration-1`, `claude/ui-designer-1` ... |

### Same test name, test file or CMake target in more than one place

- test `add_component_model`: `claude/add-component-menu-1`, `claude/add-component-menu-2`
- test `xbox_uwp_padhub`: `claude/anim-blend-1`, `claude/controller-1`, `claude/inspector-1`, `claude/integration-1`, `claude/own-platform-gaps-1`, `claude/own-platform-gaps-2`, `claude/unfinished-ledge-1`, `claude/vehicle-2`, `claude/verify-1`, `claude/xbox-glue-1`, `claude/xbox-input-1`
- test `vehicle_editor_model`: `claude/anim-blend-1`, `claude/inspector-1`, `claude/integration-1`, `claude/vehicle-2`, `main`
- test `walk_dynamics`: `claude/anim-blend-1`, `main`
- test `bmp_writer`: `claude/anim-blend-1`, `claude/controller-1`, `claude/inspector-1`, `claude/integration-1`, `claude/own-platform-gaps-1`, `claude/own-platform-gaps-2`, `claude/unfinished-ledge-1`, `claude/vehicle-2`, `claude/verify-1`, `claude/xbox-glue-1`, `claude/xbox-input-1`, `main`
- test `inventory_textfit`: `claude/anim-blend-1`, `claude/inspector-1`, `claude/integration-1`, `main`
- test `build_settings`: `claude/build-settings-3`, `main`
- test `collider_shapes`: `claude/colliders-1`, `main`
- test `controller_tuning`: `claude/controller-1`, `main`
- test `jobs_bench`: `claude/core-jobs-1`, `claude/core-perf-1`
- test `jobs_steal`: `claude/core-jobs-1`, `claude/core-perf-1`
- test `frame_arena_bench`: `claude/core-jobs-1`, `claude/core-perf-1`
- test `trace_capture`: `claude/core-jobs-1`, `claude/core-perf-1`
- test `memory_track`: `claude/core-jobs-1`, `claude/core-perf-1`
- test `lua_fuzz`: `claude/csharp-host-1`, `claude/scripting-migration-1`

### Branches changing files that another branch's open claim covers

- `claude/build-settings-3` changes `Components/ParticleSystem.hpp` covered by the claim of `claude2/grok-145-146-153-155` (**other lane**)
- `claude/own-third-party-1` changes `Render/ModelData.hpp`, `Render/OgreXmlReader.cpp` covered by the claim of `claude2/own-ogre-skeleton-bin` (**other lane**)
- `claude/particles-2` changes `Components/ParticleSystem.hpp` covered by the claim of `claude2/grok-145-146-153-155` (**other lane**)
- `claude/particles-3` changes `Components/ParticleSystem.hpp` covered by the claim of `claude2/grok-145-146-153-155` (**other lane**)
- `claude/wind-2` changes `Components/ParticleSystem.hpp` covered by the claim of `claude2/grok-145-146-153-155` (**other lane**)
- `claude2/okayui-inspector-5` changes `editor/EditorPanelViews.cpp`, `screens/OkayUIEditorPanels.cpp`, `screens/OkayUIEditorPanels.hpp` covered by the claim of `claude2/okayui-inspector-9` (same lane)
- `claude2/okayui-inspector-6` changes `editor/EditorPanelViews.cpp`, `screens/OkayUIEditorPanels.cpp`, `screens/OkayUIEditorPanels.hpp` covered by the claim of `claude2/okayui-inspector-9` (same lane)
- `claude2/okayui-inspector-7` changes `editor/EditorPanelViews.cpp`, `screens/OkayUIEditorPanels.cpp`, `screens/OkayUIEditorPanels.hpp` covered by the claim of `claude2/okayui-inspector-9` (same lane)
- `claude2/okayui-inspector-8` changes `editor/EditorPanelViews.cpp`, `screens/OkayUIEditorPanels.cpp`, `screens/OkayUIEditorPanels.hpp` covered by the claim of `claude2/okayui-inspector-9` (same lane)
- `claude2/own-switch-on` changes `editor/EditorPanelViews.cpp`, `screens/OkayUIEditorPanels.cpp`, `screens/OkayUIEditorPanels.hpp` covered by the claim of `claude2/okayui-inspector-9` (same lane)
- `claude2/own-switch-on-exit` changes `editor/EditorPanelViews.cpp`, `screens/OkayUIEditorPanels.cpp`, `screens/OkayUIEditorPanels.hpp` covered by the claim of `claude2/okayui-inspector-9` (same lane)

### Branches touching files outside their lane

- `claude/anim-blend-1` (coordinator): 7 files, e.g. `engine/include/okay/Platform/BmpWriter.hpp`, `okayui/include/okay/UI/OkayUI.hpp`, `okayui/include/okay/UI/OkayUICodeEdit.hpp`
- `claude/controller-1` (coordinator): 7 files, e.g. `engine/include/okay/Platform/BmpWriter.hpp`, `okayui/include/okay/UI/OkayUI.hpp`, `okayui/include/okay/UI/OkayUICodeEdit.hpp`
- `claude/grok-stack` (coordinator): 1 files, e.g. `tests/test_dxf_reader.cpp`
- `claude/inspector-1` (coordinator): 7 files, e.g. `engine/include/okay/Platform/BmpWriter.hpp`, `okayui/include/okay/UI/OkayUI.hpp`, `okayui/include/okay/UI/OkayUICodeEdit.hpp`
- `claude/integration-1` (coordinator): 7 files, e.g. `engine/include/okay/Platform/BmpWriter.hpp`, `okayui/include/okay/UI/OkayUI.hpp`, `okayui/include/okay/UI/OkayUICodeEdit.hpp`
- `claude/okayui-next-3` (coordinator): 64 files, e.g. `engine/include/okay/Platform/Painter.hpp`, `engine/src/Platform/Painter.cpp`, `engine/src/Platform/SdlPainter.cpp`
- `claude/own-code-6` (coordinator): 1 files, e.g. `engine/src/Core/ImageDecode.cpp`
- `claude/own-code-editor-2` (coordinator): 17 files, e.g. `editor/ScriptEditorCodeEdit.cpp`, `editor/ScriptEditorCodeEdit.hpp`, `editor/ScriptEditorWidget.hpp`
- `claude/own-platform-5` (coordinator): 34 files, e.g. `engine/include/okay/Platform/Painter.hpp`, `engine/include/okay/Platform/Platform.hpp`, `engine/include/okay/Platform/WindowLogic.hpp`
- `claude/own-platform-6` (coordinator): 16 files, e.g. `engine/include/okay/Platform/Painter.hpp`, `engine/include/okay/Platform/Platform.hpp`, `engine/src/Platform/D3D11Batch.hpp`
- `claude/own-platform-gaps-1` (coordinator): 8 files, e.g. `engine/include/okay/Platform/BmpWriter.hpp`, `okayui/include/okay/UI/OkayUI.hpp`, `okayui/include/okay/UI/OkayUICodeEdit.hpp`
- `claude/own-platform-gaps-2` (coordinator): 12 files, e.g. `engine/include/okay/Platform/BmpWriter.hpp`, `engine/include/okay/Platform/Platform.hpp`, `engine/src/Platform/OwnWin32.cpp`
- `claude/own-switch-panel-1` (coordinator): 4 files, e.g. `shared/OwnStack.hpp`, `shared/OwnSwitches.hpp`, `tests/test_own_stack.cpp`
- `claude/own-third-party-1` (coordinator): 2 files, e.g. `engine/include/okay/Render/ModelData.hpp`, `engine/src/Render/OgreXmlReader.cpp`
- `claude/particles-3` (coordinator): 3 files, e.g. `engine/include/okay/Platform/Painter.hpp`, `engine/src/Platform/SdlPainter.cpp`, `okayui/soft/OkayUI_SoftPainter.cpp`

### Branches with no claim and no PR (pushed in the last 7 days)

`claude/add-component-menu-1`, `claude/add-component-menu-2`, `claude/anim-blend-1`, `claude/area1-1`, `claude/build-settings-3`, `claude/controller-1`, `claude/core-assets-1`, `claude/core-jobs-1`, `claude/core-perf-1`, `claude/csharp-host-1`, `claude/easy-graph-usage-1`, `claude/easy-modeling-1`, `claude/ecs-1`, `claude/inspector-1`, `claude/inspector-collapse-1`, `claude/integration-1`, `claude/modeling-no-advanced-1`, `claude/node-search-popup-1`, `claude/okay-build-1`, `claude/okay-build-2`, `claude/okaybuild-2`, `claude/okaybuild-3`, `claude/okaybuild-4`, `claude/okaybuild-5`, `claude/okaycc-2`, `claude/okaycc-3`, `claude/okayms-2`, `claude/okayms-3`, `claude/okayms-3-r23b`, `claude/okayui-next-3` ... (+39)

