# Live work board

Generated 2026-11-28 10:55 UTC by `scripts/work_board.py` (a scheduled workflow rewrites this about every 10 minutes). Rule 6 (`CLAUDE.md`): a PR merges only when every CI job is finished and green.

Hand-written parts (agents, owner decisions): [docs/status/work_board.md](https://github.com/kingimann/OkaySpaceGameEngine/blob/main/docs/status/work_board.md).

## Open pull requests (2)

| PR | Title | State | CI | Updated |
|---|---|---|---|---|
| [#1369](https://github.com/kingimann/OkaySpaceGameEngine/pull/1369) | OkayAntiCheat round 2: one home (engine/AntiCheat), the okay_anticheat library and a layer | ready to merge | 21 green, 0 running, 0 queued, 0 red | 11 h ago |
| [#1400](https://github.com/kingimann/OkaySpaceGameEngine/pull/1400) | Performance: less memory (fonts, covers), idle wait that wakes at once, launcher idle fram | waiting | 19 green, 1 running, 0 queued, 0 red | 42 min ago |

## Recently merged

| PR | Title | Merged |
|---|---|---|
| [#1399](https://github.com/kingimann/OkaySpaceGameEngine/pull/1399) | OkayUI dock: no blank tabs at launch, Console no longer floats after restart, side tabs li | 68 min ago |
| [#1392](https://github.com/kingimann/OkaySpaceGameEngine/pull/1392) | Event System: the one place game UI input goes through (pointer events, navigation, settin | 81 min ago |
| [#1397](https://github.com/kingimann/OkaySpaceGameEngine/pull/1397) | OkayUI round 6: sharp text at every scale, keyboard focus everywhere, label column, Inspec | 2 h ago |
| [#1398](https://github.com/kingimann/OkaySpaceGameEngine/pull/1398) | Launcher: start the Script Editor, Visual Script and Browser programs from the Create page | 2 h ago |
| [#1396](https://github.com/kingimann/OkaySpaceGameEngine/pull/1396) | Automation round 2: every menu item is a command (299 ops), script.run in OkayScript/Lua/J | 2 h ago |
| [#1394](https://github.com/kingimann/OkaySpaceGameEngine/pull/1394) | okay-cc round 33: 353 engine files compile (was 314), init-captures, reference lifetime, n | 2 h ago |
| [#1393](https://github.com/kingimann/OkaySpaceGameEngine/pull/1393) | OkayUI dock and menus: no empty tabs for closed panels, reopened tabs return to their grou | 2 h ago |
| [#1395](https://github.com/kingimann/OkaySpaceGameEngine/pull/1395) | Own compilers: okay-ms round 36 (466 of 513 Windows, 473 plain; four wrong-code fixes; one | 2 h ago |

## Branches of every lane: `claude/*` (Coordinator), `claude2/*` (Claude 2), `claude3/*` (Claude 3), `grok/*` (Grok), newest first

| Lane | Count |
|---|---|
| Coordinator (`claude/*`) | 17 |
| Claude 2 (`claude2/*`) | 1 |
| Claude 3 (`claude3/*`) | 0 |
| Grok (`grok/*`) | 0 |

| Branch | Lane | Last commit | Head | When |
|---|---|---|---|---|
| `claude/memory-1` | Coordinator | Merge remote-tracking branch 'origin/main' into claude/memory-1 | `8616952` | 43 min ago |
| `claude/okayui-polish-1` | Coordinator | OkayUI: FindWindowByID of a never-drawn docked tab reuses FindWindowByName | `c01c3a0` | 14 h ago |
| `claude/okayms-28` | Coordinator | okay-ms round 33: plan document (363 of 514 engine files compile for Windows, 31 | `d3ee215` | 14 h ago |
| `claude/no-sdl2-2` | Coordinator | Feature map: test lists after the SDL tests left | `654b4e9` | 14 h ago |
| `claude/okay-anticheat-2` | Coordinator | Merge remote-tracking branch 'origin/main' into claude/okay-anticheat-2 | `7a6f8c5` | 18 h ago |
| `claude/asset-packages-1` | Coordinator | Merge remote-tracking branch 'origin/main' into claude/asset-packages-1 | `777e71d` | 20 h ago |
| `claude/device-emulator-1` | Coordinator | WIP: Device emulator: its own model and drawing files, 43 devices, real emulatio | `2a82611` | 29 h ago |
| `claude/archive` | Coordinator | Archive of old claude/* branches on GitHub before deletion (rule 10), 2026-11-27 | `94f44ba` | 32 h ago |
| `claude/editor-winhttp-fix` | Coordinator | Editor: never built as the Xbox app; Quit works from the sign-in and New Project | `82f2711` | 2 d ago |
| `claude/hang-watchdog` | Coordinator | Editor hang watchdog: a stuck main loop is reported (okay_hang.txt, okay_editor_ | `8ad5d68` | 2 d ago |
| `claude/editor-join` | Coordinator | Share the newest-rows query, one unread lock, clearer join names | `ed5d40e` | 2 d ago |
| `claude/editor-signin-fix` | Coordinator | Merge remote-tracking branch 'origin/main' into claude/editor-signin-fix | `18197d3` | 2 d ago |
| `claude/physics-2d-core-1` | Coordinator | Merge remote-tracking branch 'origin/main' into phys-2d | `c7f4cf7` | 3 d ago |
| `claude/backup-physics-takeover-1` | Coordinator | test_physics_grok: one UV-sphere maker for the large-mesh checks (the deep-insid | `689236e` | 3 d ago |
| `claude/okayscript-lang-3` | Coordinator | Merge remote-tracking branch 'origin/main' into claude/okayscript-lang-3 | `02f09f8` | 3 d ago |
| `claude/csharp-2` | Coordinator | Merge branch 'claude/script-editor-1' into claude/csharp-2 | `98641e0` | 3 d ago |
| `claude2/archive` | Claude 2 | Archive two Claude 2 branches before deleting them (rule 10), 2026-11-24 | `3277ba8` | 3 d ago |
| `claude/backup-modules-takeover-1` | Coordinator | Merge remote-tracking branch 'origin/main' into claude/modules-takeover-1 | `6c7a00c` | 3 d ago |

## Latest on the coordination thread (#422)

- 12 d ago: [Claude 2: `claude2/prone-stance` (`857eebd`) green](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6896227136)
- 12 d ago: [Claude 2: `claude2/world-text-rich` engine-linked results](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6896109744)
- 12 d ago: [Claude 2: progress on the four items](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895927754)
- 12 d ago: [Claim: `claude2/software-dof` (item 4, depth of field in the software renderer).](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895835704)
- 12 d ago: [Claim and result: `claude2/prone-stance` pushed at `857eebd`. NOT built yet (queued).](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895820832)

## Claims (issue #422)

| Branch | Lane | Status | Files | Problems |
|---|---|---|---|---|
| [`claude/asset-packages-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7206898952) | coordinator | active | `engine/include/okay/Core/AssetPackage.hpp`, `engine/src/Core/AssetPackage.cpp`, `engine/src/Core/AssetPackageFolder.cpp` | missing Try-it |
| [`claude/asset-streaming-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7176709537) | coordinator | stale, no branch pushed yet | `engine/include/okay/Components/WorldStreamer.hpp`, `cpp`, `Optimize/StreamingGrid` | - |
| [`claude/audio-one-wav-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7117697041) | coordinator | stale, no branch pushed yet | - | - |
| [`claude/browser-js-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7192498459) | coordinator | stale, no branch pushed yet | `engine/src/Browser/**`, `engine/include/okay/Browser/**`, `engine/src/JavaScript/**` | - |
| [`claude/browser-js-2`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7193752642) | coordinator | stale, no branch pushed yet | `engine/src/Browser/Script.*`, `Browser.*`, `Html.cpp` | - |
| [`claude/browser-real-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7198761021) | coordinator | stale, no branch pushed yet | `engine/src/Browser/Html.cpp`, `Css.cpp`, `Layout.cpp` | - |
| [`claude/browser-render-3`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7194124969) | coordinator | stale, no branch pushed yet | `engine/src/Browser/Css.*`, `Layout.*`, `Woff*/Plugins*` | - |
| [`claude/browser-ui-2`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7193752642) | coordinator | stale, no branch pushed yet | `shared/BrowserApp.*`, `shared/BrowserHost.*`, `okayui/screens/BrowserView.cpp` | - |
| [`claude/cli-panel-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7198399594) | coordinator | stale, no branch pushed yet | `tools/okay-cli/**`, `shared/CommandLinePanel.*`, `editor/main.cpp` | - |
| [`claude/crash-report-auto`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6902866537) | coordinator | stale, no branch pushed yet | `shared/CrashHandler.hpp`, `shared/CrashContext.hpp`, `shared/CrashScrub.hpp` | - |
| [`claude/csharp-2`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7135070157) | coordinator | stale, no branch pushed yet | `engine/src/Scripting/CSharp*`, `engine/include/okay/Scripting/CSharp*`, `managed/**` | - |
| [`claude/dev-channel`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6904496725) | coordinator | stale, no branch pushed yet | `github/workflows/dev-build.yml`, `scripts/publish-supabase.sh`, `scripts/versions_index.py` | - |
| [`claude/docs-batch`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903819607) | coordinator | stale, no branch pushed yet | `docs/install_audit.md`, `docs/install_comparison.md`, `docs/own_flip_proposal.md` | - |
| [`claude/editor-freeze-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7176388454) | coordinator | stale, no branch pushed yet | `editor/EditorPerf.hpp`, `editor/` | - |
| [`claude/editor-loading-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7176310450) | coordinator | stale, no branch pushed yet | `shared/Splash.hpp`, `editor/main.cpp` | - |
| [`claude/editor-menus`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6902296013) | coordinator | stale, no branch pushed yet | `editor/MenuBarModel.hpp`, `editor/HierarchyContextModel.hpp`, `editor/MenuEntriesUI.hpp` | - |
| [`claude/editor-perf-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7178139394) | coordinator | stale, no branch pushed yet | `editor/EditorPerf.hpp` | - |
| [`claude/fix-main-red`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7113859815) | coordinator | stale, no branch pushed yet | - | - |
| [`claude/fp-hand-fix`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6906260594) | coordinator | stale, no branch pushed yet | `engine/include/okay/Components/FirstPersonHand.hpp`, `FirstPersonHands.hpp`, `FpHandsPreview.hpp` | - |
| [`claude/grok-stack`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903819607) | coordinator | stale, no branch pushed yet | `tests/test_scene_stray_number.cpp`, `tests/test_switch_off_settings.cpp`, `tests/test_scene_subnormal.cpp` | - |
| [`claude/images-svg-webp-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7202543042) | coordinator | stale, no branch pushed yet | `engine/src/Graphics/**`, `WebP*/Svg*`, `tests/test_image*` | - |
| [`claude/imgui-panels-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7153528965) | coordinator | stale, no branch pushed yet | `editor/*.hpp`, `cpp`, `editor/OkayPanels.*` | - |
| [`claude/imgui-panels-2`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7195516344) | coordinator | stale, no branch pushed yet | `okayui/**`, `shared/OkayUI*`, `editor/launcher` | - |
| [`claude/input-followup-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7143412123) | coordinator | stale, no branch pushed yet | `engine/src/Input/ActionSet.cpp`, `engine/include/okay/Input/ActionSet.hpp`, `tests/test_input_grok.cpp` | - |
| [`claude/input-upgrade`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6905091630) | coordinator | stale, no branch pushed yet | `engine/include/okay/Input/**`, `engine/src/Input/**`, `tests/test_input_deep_*.cpp` | - |
| [`claude/installer-honest`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903484221) | coordinator | stale, no branch pushed yet | `scripts/installer-tests/`, `docs/images/installer/` | - |
| [`claude/js-compat-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7202543042) | coordinator | stale, no branch pushed yet | `engine/src/JavaScript/**`, `engine/include/okay/JavaScript/**`, `tests/test_js_*` | - |
| [`claude/launcher-open-gate`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903385935) | coordinator | stale, no branch pushed yet | `launcher/OpenProjectGate.hpp`, `tests/test_open_project_gate.cpp`, `docs/images/open_gate/` | - |
| [`claude/lua-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7135070157) | coordinator | stale, no branch pushed yet | `engine/src/Scripting/Lua*`, `engine/include/okay/Scripting/Lua*`, `tests/*lua*` | - |
| [`claude/msvc-actionlist-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7117697041) | coordinator | stale, no branch pushed yet | `engine/src/Components/ActionList*.cpp` | - |
| [`claude/multiplayer-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7124170263) | coordinator | stale, no branch pushed yet | `engine/include/okay/Net/**`, `engine/src/Net/**`, `engine/src/VisualScript/**` | - |
| [`claude/no-assimp-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7133731581) | coordinator | stale, no branch pushed yet | `engine/src/Render/ModelImport*.cpp`, `engine/include/okay/Render/ModelImport*.hpp`, `CMakeLists.txt` | - |
| [`claude/no-curl-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7150665749) | coordinator | stale, no branch pushed yet | `engine/src/Net/NativeHttp*`, `engine/src/Net/OwnHttp*`, `launcher/VersionsNet.cpp` | - |
| [`claude/no-sdl2-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7195516344) | coordinator | stale, no branch pushed yet | `engine/src/Platform/**`, `engine/include/okay/Platform/**` | - |
| [`claude/okay-anticheat-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7205958624) | coordinator | stale, no branch pushed yet | `docs/security/okay_anticheat.md` | - |
| [`claude/okay-backend-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7200698970) | coordinator | stale, no branch pushed yet | `docs/platform/okay_backend_audit.md`, `docs/platform/okay_backend.md` | - |
| [`claude/okay-backend-admin-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7205958624) | coordinator | stale, no branch pushed yet | `docs/platform/okay_backend_admin.md` | - |
| [`claude/okay-cli-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7115958870) | coordinator | stale, no branch pushed yet | - | - |
| [`claude/okaybuild-14`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7153528965) | coordinator | stale, no branch pushed yet | `tools/okaybuild/**`, `docs/platform/own_build_plan.md` | - |
| [`claude/okaycc-20`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7149368564) | coordinator | stale, no branch pushed yet | `tools/okaycc/**`, `tests/test_cppdiff.cpp`, `docs/platform/own_compiler_plan.md` | - |
| [`claude/okaycc-32`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7195516344) | coordinator | stale, no branch pushed yet | `tools/okaycc/**`, `docs/platform/own_compiler_plan.md` | - |
| [`claude/okayms-25`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7149368564) | coordinator | stale, no branch pushed yet | `tools/okayms/**`, `docs/platform/own_msvc_plan.md` | - |
| [`claude/okayms-32`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7195516344) | coordinator | stale, no branch pushed yet | `tools/okayms/**`, `docs/platform/own_msvc_plan.md` | - |
| [`claude/okayscript-bind-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7122851979) | coordinator | stale, no branch pushed yet | `engine/src/Scripting/**`, `okayscript/include/okayscript/Host.hpp`, `engine/src/Scripting/OkayScriptVM*` | - |
| [`claude/okayscript-lang-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7118725806) | coordinator | stale, no branch pushed yet | `okayscript/`, `engine/src/Scripting/OkayScriptVM*.cpp`, `OkayScriptVMInternal.hpp` | - |
| [`claude/okayscript-vm-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7122851979) | coordinator | stale, no branch pushed yet | `okayscript/**` | - |
| [`claude/one-inflate`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6902296013) | coordinator | stale, no branch pushed yet | `engine/include/okay/Core/Inflate.hpp`, `tests/inflate_reference_slow.hpp`, `tests/test_inflate_differential.cpp` | - |
| [`claude/own-code-editor-2`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903341750) | coordinator | stale, no branch pushed yet | `okayui/include/okay/Text/TextBuffer.hpp`, `okayui/src/TextBuffer.cpp`, `okayui/tests/test_text_buffer.cpp` | - |
| [`claude/page-viewer-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7173065254) | coordinator | stale, no branch pushed yet | `tests/`, `docs/status` | - |
| [`claude/physics-more`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6904424383) | coordinator | stale, no branch pushed yet | `engine/include/okay/Physics/**`, `engine/src/Physics/**`, `engine/include/okay/Components/Joint*.hpp` | - |
| [`claude/player-deep`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6904495900) | coordinator | stale, no branch pushed yet | `engine/include/okay/Components/FirstPersonController.hpp`, `ThirdPersonController.hpp`, `ThirdPersonShooterController.hpp` | - |
| [`claude/project-layout-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7201996191) | coordinator | stale, no branch pushed yet | `editor/Project*`, `editor/ProjectUpgrade.cpp`, `editor/SceneManagerPanel.cpp` | - |
| [`claude/project-menus`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903051423) | coordinator | stale, no branch pushed yet | `editor/ProjectContextModel.hpp`, `editor/ProjectMenuDraw.cpp`, `editor/ProjectNewAssets.hpp` | - |
| [`claude/release-2-876`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903101150) | coordinator | stale, no branch pushed yet | `dist/CHANGELOG.txt` | - |
| [`claude/release-parallel`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903502484) | coordinator | stale, no branch pushed yet | `scripts/publish-supabase-v1.sh`, `scripts/promote-latest.sh`, `scripts/check-unreleased.sh` | - |
| [`claude/rhi-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7181300212) | coordinator | stale, no branch pushed yet | `engine/include/okay/Render/RHI/` | - |
| [`claude/script-editor-app-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7198399594) | coordinator | stale, no branch pushed yet | `editor/ScriptIde*`, `editor/ScriptAssist.cpp`, `editor/ScriptText.hpp` | - |
| [`claude/script-ide-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7180969758) | coordinator | stale, no branch pushed yet | `editor/ScriptEditor*`, `editor/ScriptAssist*`, `editor/main.cpp` | - |
| [`claude/site-check-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7198761021) | coordinator | stale, no branch pushed yet | `tools/sitecheck/` | - |
| [`claude/templates-work`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6902709811) | coordinator | stale, no branch pushed yet | `editor/TemplateTable.hpp`, `tests/test_templates_work.cpp`, `tests/test_examples_work.cpp` | - |
| [`claude/third-party-2`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7108163807) | coordinator | stale, no branch pushed yet | `shared/WavPlayback.hpp`, `tests/check_wav_seam_wiring.cmake`, `engine/src/Render/ModelImport.cpp` | - |
| [`claude/third-party-3`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7114127311) | coordinator | stale, no branch pushed yet | `ModelImport*.cpp` | - |
| [`claude/third-party-3b`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7116414750) | coordinator | stale, no branch pushed yet | `ModelImport.cpp`, `docs/content/model_readers.md` | - |
| [`claude/third-party-4`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7114127311) | coordinator | stale, no branch pushed yet | `okayui/src/OkayUICodeEdit.cpp` | - |
| [`claude/third-party-5`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7114127311) | coordinator | stale, no branch pushed yet | `windows-tests.yml` | - |
| [`claude/third-party-6`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7114127311) | coordinator | stale, no branch pushed yet | `okayui/`, `editor/OkayUIHostPanels.cpp` | - |
| [`claude/third-party-okaybuild-2`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7120989090) | coordinator | stale, no branch pushed yet | `tools/okaybuild/`, `Still`, `claude/okayscript-lang-1**` | - |
| [`claude/third-party-okaycc-4`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7120989090) | coordinator | stale, no branch pushed yet | `tools/okaycc/` | - |
| [`claude/third-party-okayms-2`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7120989090) | coordinator | stale, no branch pushed yet | `tools/okayms/` | - |
| [`claude/ui-nameplate-worldtext`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903199226) | coordinator | stale, no branch pushed yet | `engine/include/okay/Components/UIOwnerData.hpp`, `engine/src/Components/UIOwnerData.cpp`, `examples/ui_text/ui_text.okayscene` | - |
| [`claude/upgrade-stack`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903306932) | coordinator | stale, no branch pushed yet | `editor/StartupModals.hpp`, `tests/test_startup_modals.cpp`, `tests/test_scene_bom_bug.cpp` | - |
| [`claude/versions-safety`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6903484221) | coordinator | stale, no branch pushed yet | `launcher/VersionsSafety.hpp`, `tests/test_versions_safety.cpp` | - |
| [`claude/voxel-edit-fps`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6904423421) | coordinator | stale, no branch pushed yet | `engine/src/Components/VoxelTerrain.cpp`, `engine/include/okay/Components/VoxelTerrain.hpp`, `VoxelDigger.hpp` | - |
| [`claude/vs-categories`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6902296013) | coordinator | stale, no branch pushed yet | `editor/VisualScriptPlace.hpp`, `editor/VisualScriptPlaceRules.hpp`, `editor/VisualScriptLegacyPlaces.hpp` | - |
| [`claude/vs-ide-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7181038286) | coordinator | stale, no branch pushed yet | `editor/main.cpp` | - |
| [`claude/walk-vs-run`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6904422160) | coordinator | stale, no branch pushed yet | `engine/src/Animation/HumanoidMotion.cpp`, `engine/src/Components/Character.cpp`, `engine/include/okay/Components/Character.hpp` | - |
| [`claude/wav-stream-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7153528965) | coordinator | stale, no branch pushed yet | `engine/src/Audio/AudioStream*`, `engine/include/okay/Audio/AudioStream*`, `engine/src/Audio/WavDecoder*` | - |
| [`claude/woff2-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7198761021) | coordinator | stale, no branch pushed yet | `engine/src/Core/Brotli.*`, `engine/src/Browser/WebFont.*`, `tests/test_brotli.cpp` | - |
| [`claude/xbox-keys-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7117697041) | coordinator | stale, no branch pushed yet | - | - |
| [`claude2/active-ragdoll`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083712383) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/build-export-fix`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083712383) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/dedup-input`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083839019) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/dedup-saves`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083839019) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/dedup-small`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083839019) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/drag-paths-relative`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6904682606) | claude2 | stale, no branch pushed yet | `tests/test_drop_path_relative.cpp`, `docs/try_it.md` | - |
| [`claude2/ecs-world`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7087372137) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/editor-split-1`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7084954477) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/found-console`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7068578972) | claude2 | stale, no branch pushed yet | `editor/ConsolePanel.cpp`, `player/main.cpp`, `Core/Console*` | - |
| [`claude2/found-jobs`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7068578972) | claude2 | stale, no branch pushed yet | `Jobs.*`, `AssetManager.cpp`, `NavMeshSurface.cpp` | - |
| [`claude2/grok-145-146-153-155`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7064574409) | claude2 | stale, no branch pushed yet | `engine/src/Components/Terrain.cpp`, `engine/include/okay/Components/Terrain.hpp`, `engine/include/okay/Components/Light.hpp` | - |
| [`claude2/item-cooldowns`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6898602014) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/item-durability`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6897431646) | claude2 | stale, no branch pushed yet | `InventoryOps.cpp`, `Equipment.cpp`, `inventory.md` | - |
| [`claude2/lod-asset-streaming`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083712383) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/modular-2`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7085741989) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/nan-damage`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6909830824) | claude2 | stale, no branch pushed yet | `SurvivalComponents.hpp`, `Stats.hpp`, `damage/heal` | - |
| [`claude2/one-health`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7087372137) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/one-navigation`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7087372137) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/one-script-api`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7087372137) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/own-fbx-faster`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6918419300) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/own-http-finish`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6929812582) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/own-physics-engine`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083712383) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/own-ttf-render`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6930052734) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/perf-nav-fallback-hierarchy`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6915583630) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/prone-stance`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895820832) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/reduce-motion-bob`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6887483662) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/sdl-player-no-sdl`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6930897379) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/serialize-object-numbers`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6902859455) | claude2 | stale, no branch pushed yet | `engine/src/Scene/SceneSerializer.cpp`, `tests/test_scene_canonical.cpp` | - |
| [`claude2/software-dof`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895835704) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/software-scene-ssao`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6896927774) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/steam-epic-stores-2`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083712383) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/ui-nameplate-bound`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895434594) | claude2 | stale, no branch pushed yet | `UITextBind.*`, `UIBarBind.*`, `UIOwnerData.*` | - |
| [`claude2/ui-shared-code`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7087372137) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/vehicle-seat`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6887652737) | claude2 | stale, no branch pushed yet | `Seat.hpp`, `VehicleController.hpp`, `HumanoidActions.hpp/.cpp` | - |
| [`claude2/vs-net-link-pin`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6896966433) | claude2 | stale, no branch pushed yet | `tests/test_vs_net_link_pin.cpp`, `editor/VisualScriptPalette.hpp`, `tests/test_vs_registry_effect.cpp` | - |
| [`claude2/vs-net-nodes`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6897312650) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/wav-ffmpeg-diff`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6896454520) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/world-streaming`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-7083712383) | claude2 | stale, no branch pushed yet | - | - |
| [`claude2/world-text-rich`](https://github.com/kingimann/OkaySpaceGameEngine/issues/422#issuecomment-6895661261) | claude2 | stale, no branch pushed yet | - | - |

## Collision report

7 lane branches (`claude/*`, `claude2/*`, `claude3/*`, `grok/*`) with changes against `origin/main`. 12 branch pairs touch the same files (3 serious). Generated by `scripts/collision_check.py`; plain rules in `docs/process/collaboration.md`.

### File and line overlaps between branches

| Branches | Same files (not shared files) | Same lines | Lanes |
|---|---|---|---|
| `claude/memory-1` + `claude/no-sdl2-2` | `platform/platform_layer.md`, `editor/EditorPerf.hpp`, `editor/main.cpp`, `Platform/Painter.hpp` +14 | `editor/main.cpp` 11-11 (approx); `Platform/Platform.hpp` 484-484 (approx); `launcher/CoverArtAvatars.cpp` 15-15 (approx) | same lane |
| `claude/device-emulator-1` + `claude/memory-1` | `editor/main.cpp` | `editor/main.cpp` 1113-1113 (approx) | same lane |
| `claude/device-emulator-1` + `claude/okayui-polish-1` | `editor/main.cpp` | `editor/main.cpp` 48682-48682 (approx) | same lane |
| `claude/no-sdl2-2` + `claude/okayui-polish-1` | `editor/main.cpp`, `launcher/main.cpp`, `UI/OkayUI.hpp`, `src/OkayUI.cpp` +5 | - | same lane |
| `claude/no-sdl2-2` + `claude/okay-anticheat-2` | `workflows/ci.yml`, `editor/BuildGame.cpp`, `editor/main.cpp`, `launcher/AntiCheatPages.cpp` +4 | - | same lane |
| `claude/memory-1` + `claude/okayui-polish-1` | `ui/own_ui_toolkit.md`, `editor/main.cpp`, `launcher/main.cpp`, `UI/OkayUI.hpp` +2 | - | same lane |
| `claude/asset-packages-1` + `claude/okay-anticheat-2` | `engine/shared_code.md`, `editor/BuildGame.cpp`, `player/main.cpp`, `shared/AntiCheatIntegrity.hpp` | - | same lane |
| `claude/asset-packages-1` + `claude/no-sdl2-2` | `editor/BuildGame.cpp`, `player/main.cpp` | - | same lane |
| `claude/device-emulator-1` + `claude/no-sdl2-2` | `editor/CMakeLists.txt`, `editor/main.cpp` | - | same lane |
| `claude/memory-1` + `claude/okay-anticheat-2` | `editor/main.cpp`, `launcher/main.cpp` | - | same lane |
| `claude/okay-anticheat-2` + `claude/okayui-polish-1` | `editor/main.cpp`, `launcher/main.cpp` | - | same lane |
| `claude/device-emulator-1` + `claude/okay-anticheat-2` | `editor/main.cpp` | - | same lane |

### Shared and generated files touched by several branches (handshake needed)

| File | Branches |
|---|---|
| `tests/CMakeLists.txt` | 5: `claude/asset-packages-1`, `claude/device-emulator-1`, `claude/memory-1`, `claude/no-sdl2-2`, `claude/okay-anticheat-2` |
| `engine/CMakeLists.txt` | 4: `claude/asset-packages-1`, `claude/memory-1`, `claude/no-sdl2-2`, `claude/okay-anticheat-2` |
| `docs/feature_map/17-launcher-platform.md` | 2: `claude/memory-1`, `claude/no-sdl2-2` |
| `CMakeLists.txt` | 2: `claude/no-sdl2-2`, `claude/okay-anticheat-2` |
| `docs/status/THIRD_PARTY.md` | 2: `claude/no-sdl2-2`, `claude/okay-anticheat-2` |
| `docs/status/own_code_roadmap.md` | 2: `claude/no-sdl2-2`, `claude/okay-anticheat-2` |
| `docs/status/provenance_audit.md` | 2: `claude/no-sdl2-2`, `claude/okay-anticheat-2` |
| `docs/feature_map/40-okayui-library.md` | 2: `claude/no-sdl2-2`, `claude/okayui-polish-1` |

### Same test name, test file or CMake target in more than one place

- test `platform_default_env_own`: `claude/no-sdl2-2`, `main`
- test `platform_default_env_sdl2_ignored`: `claude/no-sdl2-2`, `main`
- test `voice_races`: `claude/no-sdl2-2`, `main`
- target `test_voice_races`: `claude/no-sdl2-2`, `main`

### Branches changing files that another branch's open claim covers

- `claude/no-sdl2-2` changes `editor/BuildGame.cpp`, `player/main.cpp` covered by the claim of `claude/asset-packages-1` (same lane)
- `claude/okay-anticheat-2` changes `editor/BuildGame.cpp`, `player/main.cpp` covered by the claim of `claude/asset-packages-1` (same lane)

### Branches with no claim and no PR (pushed in the last 7 days)

`claude/device-emulator-1`, `claude/no-sdl2-2`, `claude/okayms-28`, `claude/okayui-polish-1`

