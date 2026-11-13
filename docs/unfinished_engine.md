# Engine-wide unfinished audit (new findings)

Base: origin/main 13a892ec. Scope: engine/, editor/, player/, shared/ (launcher excluded). Read-only audit; nothing built or edited.

How this was done: I read `UNFINISHED.md` and `unfinished-audit.md` first and kept only items that are not in them, or that sharpen an item with a concrete file and line. The source code is very clean of TODO or FIXME markers (about 250 hits for TODO, stub, placeholder, "not yet", nearly all benign: UI placeholder text, "speed hack", progress words). The real gaps showed up in four other ways: (1) renderer-by-renderer comparison, (2) library code that no editor or player code ever calls, (3) the `Open:` notes on rows the feature map marks "Works", which the existing lists skipped, and (4) code that accepts a setting and ignores it. Several `Open:` notes in the feature map are now out of date (checked: editor Play already runs on `GameLoop`, ease-curve and Clock fields are in the Inspector, the Problems panel calls `CollectProjectDiagnostics`, Spawner pooling exists, radio toggles exist, camera stacks render in the player, glTF packed metallic-roughness is imported). Those are not listed here as gaps.

Impact: High = a user or player will hit it in normal use. Medium = a visible limit or a feature that exists but cannot be used. Low = polish, consistency, or only affects developers. Size: Small < 1 day, Medium = a few days, Large = a week or more.

## 1. Rendering (software, OpenGL, D3D11, D3D12)

| Item | Where | What is missing | Impact | Size |
|---|---|---|---|---|
| Ambient occlusion (SSAO) does nothing on Direct3D 11 | `engine/src/Render/D3D11Renderer.cpp` (no SSAO code at all; GL has it in `GLRenderer.cpp`). Player prefers D3D before GL: `player/main.cpp:1604-1607, 2277-2317`. Editor checkbox: `editor/main.cpp:11294, 17419` | The "Ambient Occlusion" switch in Lighting and Build Settings is saved and written to the game config, but on Windows, where D3D11 is the default, shipped games never apply it. Only OpenGL (Linux) draws it. The Lighting window says "Applied by the GPU renderers" (`editor/main.cpp:11305`), which overstates this | High | Medium |
| Direct3D 12 has no post-processing at all | `engine/src/Render/D3D12Renderer.cpp` (zero matches for tonemap, bloom, vignette, depth of field, HDR, skinning); `D3D12Renderer.hpp:13-17` | Exposure, tonemap, bloom, vignette, grading, depth of field, AO maps, reflections and MSAA are all missing. "Prefer Direct3D 12" in Build Settings (`editor/main.cpp:17399`) only says it "performs like D3D11", so a game that looks right on D3D11 looks flat on D3D12. Header also says MSAA is "not applied" while `D3D12Renderer.cpp:457` does pick a sample count: header and code disagree | High | Large |
| Direct3D 12 and software have no spot or point light shadows | `localShadow` exists only in `GLRenderer.cpp` and `D3D11Renderer.cpp`; D3D12 has 0 matches, `SoftwareRenderer` has sun shadows only; D3D12 has no cascades (0 matches) | A light with Cast Shadows on works on two backends and silently casts nothing on the other two. The Inspector does not tell the user | Medium | Large |
| D3D12 and software skin on the CPU | `player/main.cpp:2093-2098` (`if (d3d12Ready) gpuSkin = false`) | Many animated characters cost frame time on D3D12 and in the software fallback; there is no GPU skinning path there | Medium | Large |
| Every GPU frame is read back to the CPU | `D3D11Renderer.hpp`, `D3D12Renderer.hpp`, `GLRenderer.hpp` (`RenderToPixels` contract); only the editor's Windows direct path avoids it | No swap-chain presentation in the player, so the GPU path is capped by a copy per frame. Also the "GPU ms" in the profiler is CPU wall time | Medium | Large |
| Fog, post, sky options differ per backend with no in-editor warning | `editor/main.cpp:11305, 11328` only carry a generic note | The same scene looks different on four backends; there is no per-feature "not on this renderer" marker in the Inspector, so users cannot tell which switch is dead for their target | Medium | Small |
| Particles are CPU billboards, lines and trails are unlit and cast no shadows | feature map 2.7/2.12 (`Open:` notes) | Large particle counts cost CPU; no GPU particles or lit trails | Low | Large |
| No Vulkan, Metal, WebGL or WebGPU backend | `docs/feature_map/38-render-backends.md` | Web player runs the software renderer only (`player/main.cpp` skips GPU under Emscripten); macOS has no GPU path except OpenGL | Medium | Large |

## 2. Physics (2D and 3D)

| Item | Where | What is missing | Impact | Size |
|---|---|---|---|---|
| Mesh Collider 3D is static-only and raises no collision callbacks | `engine/include/okay/Physics/Collider3D.hpp` (`MeshCollider3D`), `engine/src/Physics/Physics3D.cpp` (mesh pass in `Step`) | Exact per-triangle collision (BVH, rays, overlaps, casts, `ResolveSphere`, rigidbody contact, trigger overlap) now works against concave, hollow and holed meshes. Still open: a mesh collider cannot be a dynamic body (no convex-hull shape or concave-vs-concave contact in the solver; a dynamic Rigidbody3D on the same object is refused with a warning and an Inspector message); bodies landing on a solid mesh get no `OnCollisionEnter3D/Stay/Exit`; the navmesh bake treats it as its bounding box | Medium | Medium |
| 3D hinge joint has no angle limits | `engine/include/okay/Components/Joint3D.hpp:22` (modes Distance, Spring, Pin, Hinge; no limit fields). 2D has them via `Math/JointLimit.hpp` | Doors and flaps swing a full circle; the 3D joint headers still say "no hinge" in comments | Medium | Small |
| No 2D box or capsule casts, no 2D explosion force | `Physics/Physics2D.hpp` queries; `AddExplosionForce` only in `Rigidbody3D.hpp:111` | 2D games cannot sweep a box or capsule, or blast bodies away, without writing it in script | Medium | Small |
| Bodies must be un-parented; no step-rate setting in the editor | `Rigidbody3D.hpp` header comment; feature map 6.4 | A rigidbody under a moving parent is wrong; no UI for the fixed step | Low | Medium |
| Destruction only on cube-grid blocks | `engine/include/okay/Components/Destructible.hpp:42, 175` (flood fill on `blockTag` grids) | Cannot break arbitrary meshes | Low | Large |
| Vehicle has no get-in or driver seat | `Components/VehicleController.hpp` (no seat, enter or exit code) | A player cannot board a vehicle without custom script; the Seat component is for sitting only | Medium | Medium |

## 3. Animation and IK (animation owner: noted only)

| Item | Where | What is missing | Impact | Size |
|---|---|---|---|---|
| Hand finger joints never posed; hand is one block | `Components/Character.hpp:222` comment; feature map 4.x/5.9 | Held objects have no gripping hand; first-person hands look blocky | Medium | Large |
| Dive, hit, vault and emotes not sent to other players | feature map 5.12/16.x (`netActionParam` plan); `Character.hpp` has `netAction` but only for the listed actions | In multiplayer other players do not see those animations | Medium | Medium |
| No cloth, spring-bone or secondary motion | search for cloth, jiggle, spring bone: no results in `engine/` | Hair, capes, tails stay rigid | Low | Large |
| `ControllerFeel.swim` is ignored | `Components/ControllerFeel.hpp:48` ("The graph ignores it until a swim state exists") | The swim flag drives nothing unless the user builds a swim state by hand | Low | Small |

## 4. Audio

| Item | Where | What is missing | Impact | Size |
|---|---|---|---|---|
| No reverb, doppler, occlusion or distance filtering | `Audio/AudioMixer.hpp` (only low-pass and a stereo pan or balance, `kSpatialPan`); no matches for reverb or doppler in `engine/` | Caves, rooms and fast fly-bys sound flat. Spatial audio is pan plus fade only | Medium | Medium |
| Audio import settings stored but never read | `Core/ImportSidecar.hpp` is read only from `engine/src/Render/ModelImport.cpp`; nothing in `engine/src/Audio` | Import options for audio (and texture compress) have no effect. Already hidden in the UI, but the data path is dead | Low | Small |
| Only four fixed buses, no effect chain or snapshots | `AudioMixer.hpp:40` (`SFX, Music, UI, Voice`) | Games cannot add an Ambience or Dialogue bus | Low | Medium |
| Formats: WAV, OGG, MP3 only (no FLAC) | `AudioDecode.cpp:106` | Common asset format rejected with an error | Low | Small |
| One listener only (main camera) | feature map 7.3 | No split-screen audio | Low | Medium |

## 5. Input

| Item | Where | What is missing | Impact | Size |
|---|---|---|---|---|
| Editor Play mode does not feed function keys, Alt, Tab or left/right modifiers | `editor/main.cpp:47007-47027` builds keys by hand; the built game calls `okay::AppendSdlExtendedKeys(ks, down)` at `player/main.cpp:1890` | A game that binds F-keys, Alt or Tab works when built and silently fails in the editor Play button | High | Small |
| Editor Play feeds one controller only | `editor/main.cpp:47025` (`FeedPad(g_pad.get(), down)`), player uses `pads.Feed` for all pads (`player/main.cpp:1891`) | Co-op controller games cannot be tested in the editor | Medium | Small |
| No on-screen virtual stick, D-pad, or touch buttons; no pinch, swipe, long-press recognisers | no matches for joystick, gesture or swipe in `engine/include/okay/Components` or `UI/` | Mobile and web-touch games must build their own controls | Medium | Medium |
| Input contexts (`PushContext`) have no consumer | no reference from `engine/include/okay/Components`, editor or player | The feature exists but a pause menu does not use it; `Input::SetUICaptured` is the real path | Low | Small |
| `PlatformGamepad` has no test | no test names `FeedPad` or `PadSet` (the platform controller mapping is tested in `platform`, not this feed) | Controller mapping regressions go unseen | Low | Small |

## 6. AI and navigation

| Item | Where | What is missing | Impact | Size |
|---|---|---|---|---|
| ~~Moving obstacles do not cut the mesh (no obstacle carving)~~ Resolved: a Nav Mesh Obstacle with Carve on cuts a hole in the baked mesh at run time (box or capsule, carve only when stationary after a settable time, or follow the obstacle); the path search goes around it and agents already walking replan at once. See `docs/navigation.md` (HAVE / GAP table). | `Components/NavMeshObstacle.hpp`, `AI/NavMeshCarve.cpp` | Done. Not done: carving does not run in the editor outside Play, and a carved obstacle is a hole in the mesh, not a per-agent cost. | Medium | Medium |
| NavMesh polygons are axis-aligned rectangles, one agent radius per bake, one surface per scene | feature map 9.1 | Angled walls become staircases; two NPC sizes need two scenes | Medium | Large |
| 2D grid pathfinder: no weights, smoothing or moving obstacles | `AI/Pathfinding.hpp` | 2D games get jagged unweighted paths | Low | Medium |
| Behavior tree cannot run another tree; no breakpoints | `AI/BehaviorTree.hpp:100-110` (no sub-tree node type) | Large trees cannot be modular | Medium | Medium |
| Perception code duplicated (component and tree service) | feature map 9.5 | Two systems to tune | Low | Medium |

## 7. Cinematics, dialogue, quests

| Item | Where | What is missing | Impact | Size |
|---|---|---|---|---|
| Sequence has only camera, transform, animation, audio, event tracks | `Components/Sequence.hpp:60` (`SeqTransformTrack` and siblings) | No letterbox, shake, fade or nested-sequence track. A cutscene needs script for bars or screen shake | Medium | Medium |
| Cinematics, dialogue and quests are per client in multiplayer and not saved | feature map 10.x | A save mid-dialogue loses it; clients can desync quests | Medium | Medium |
| Sound captions never fed | `Core/Subtitles.hpp` (`CaptionManager`) has no caller outside `Subtitles.*` and `AccessibilitySettings.*` (no Audio Source option, no `SequencePlayer` link) | Deaf and hard-of-hearing players get no sound captions | Medium | Small |

## 8. Gameplay systems

| Item | Where | What is missing | Impact | Size |
|---|---|---|---|---|
| Four separate health models | `HealthStat`, `SurvivalStats`, RPG `Stats`, `NPCController.health` (feature map 11.4, 11.9, 9.5) | Damage from one system does not reach the others; scripts must know which one to hit | Medium | Large |
| Melee only hits NPCs; no weapon definitions tied to items; no ranged weapon component except projectiles | feature map 11.x (`Open:` on combat row) | Weapons are hand-built every time | Medium | Large |
| No loot-table, recipe-book or item editor windows | `.okayitems`, `.okayloot`, recipe book are text files (feature map 11.2, 11.3) | Designers edit text by hand | Medium | Medium |
| Build mode placements are not synced between players | `docs/build_mode.md` "Not covered yet" | Multiplayer building diverges | Medium | Medium |
| Minimap is 2D top-down only; Turn Manager and Minimap have no docs | feature map 11.x | No 3D or tilted minimap and no doc pages | Low | Medium |

## 9. In-game UI

| Item | Where | What is missing | Impact | Size |
|---|---|---|---|---|
| UI frames have no rotation or scale | `Components/UIFrame.hpp` (only anchors, pivot, size) | Cannot spin or zoom a widget (spinning icons, pop-in) except through UIAnimation tricks | Medium | Medium |
| Missing widgets: search bar, date and time picker, file picker, accordion, breadcrumbs, pagination, skeleton, bottom sheet | `engine/include/okay/Components/UI*.hpp` file list has none of these | App-style UI must be assembled by hand | Medium | Large |
| UI controller focus visuals not wired to accessibility | `UI/FocusGraph` is used by `UINavigation.hpp`; high contrast changes UI text only (see section 14) | Gamepad users get no consistent focus ring | Medium | Medium |
| UI scripting needs the OkayUI bridge; without it `ui_*` calls draw nothing | `Scripting/ScriptUI` has only the OkayUI implementation in `shared/` (feature map 13.x) | A build without OkayUI silently ignores UI script calls | Low | Medium |
| Steeply tilted world-space UI draws as axis-aligned rectangles | `docs/ui_world_space.md` | In-world screens at an angle look wrong | Low | Medium |
| UI text is left-to-right only, no shaping | no bidi, shaping or Arabic code in `engine/src` or `include` | Arabic, Hebrew, Thai text draws wrong or mirrored by hand; the preview mirror only reverses characters | Medium | Large |

## 10. Scripting (OkayScript, typed language) and visual script

| Item | Where | What is missing | Impact | Size |
|---|---|---|---|---|
| Typed script language: `static`, `const` only in the Behaviour class; `new` only for Vector3; `int.Parse` style calls not parsed | `engine/src/Script/Checker.cpp:248, 249, 262, 1688`; `Parser.cpp:901` | Users porting normal C# patterns hit errors saying "for now" | Medium | Large |
| Visual script export still turns Timer into a comment, unknown nodes into `// unsupported node`, and Actions steps into `// TODO` | Timer in `editor/VisualScriptEditor.hpp`; unknown nodes beside it; Actions `// TODO` in `editor/main.cpp` | Wait and Delay emit `after(seconds, "fn")` and the following nodes run in that function (one export-vs-runtime test). Timer, unknown nodes, and the Actions list still skip the real work | Medium | Medium |
| Typed language engine API duplicated from the old VM | `engine/src/Script/EngineApi.cpp` vs `Scripting/OkayScriptVM.cpp` | The two builtin tables can drift | Low | Large |
| Text scripts have no breakpoints or stepping | feature map 13.5/14.5 | Only the visual-script debugger can step | Medium | Large |
| Node definitions must be added in two places | `editor` registry and `VsTypes` pin table (feature map 14.1) | Easy to ship a node that appears but does not run | Low | Medium |

## 11. Networking and anti-cheat

| Item | Where | What is missing | Impact | Size |
|---|---|---|---|---|
| Deaths that no replicated hit caused are not replicated | `Net/NetworkManager.cpp` (`__dmg` carries hits; `__respawn` carries respawns) | A kill volume or a script that kills a player is not sent to other peers; only the respawn is (respawn, dive, hit, vault and emotes are replicated: `docs/multiplayer_audit.md`) | Low | Small |
| Client anti-cheat probes are Windows only | `shared/AntiCheatChecks.hpp:20, 48, 129, 303` (`_WIN32`; "an empty probe = not checked on this platform") | Linux and macOS clients have no debugger, module or clock probes | Medium | Medium |
| No replay or spectator recording over the network | no replay code in `engine/include/okay/Net`; `Core/FrameRecord.hpp` is local only and has no editor or player caller | No kill-cam, demo or server replays | Low | Large |
| `NativeHttp` is Windows only | `Platform/NativeHttp.hpp:19, 32` | Other systems shell out to a `curl` process, so a game on a machine without curl cannot call accounts or matchmaking | Medium | Medium |
| Web build has no networking, matchmaking or server browser | `Net/Matchmaker.cpp`, `ServerBrowser.cpp`, `MatchResults.cpp` guard on `__EMSCRIPTEN__` | Browser games are single-player | Medium | Large |
| Net sim and replication layers lack named tests | no test names `NetDelta`, `QPose`, ADPCM or jitter buffer (feature map 16.2, 16.7) | Regressions in snapshot compression go unseen | Low | Small |
| No CI job builds Photon or Steam adapters | `.github/workflows`; `scripts/build-windows-dist.sh:66` only sets Steam from an env var | Optional adapters can stop compiling unnoticed | Medium | Small |

## 12. Platform, Steam, build and export

| Item | Where | What is missing | Impact | Size |
|---|---|---|---|---|
| Android and iOS export only produce assets plus a README | `editor/main.cpp` Build code; tooltip says "Export an Android Studio project" (feature map 18.2) | Misleading label; there is no mobile project in the repo | Medium | Large |
| Web and mobile have no touch gestures beyond mouse emulation | feature map 18.2 / 37.x | See section 5 | Medium | Medium |
| No multi-monitor or resolution list; no shared window layer | feature map 37.1; each host repeats SDL setup | Fullscreen picks the default monitor; no resolution menu in the shipped Settings screen | Medium | Medium |
| No clipboard API for games or scripts | no `clipboard` in `engine/include`, `engine/src`, `player` (only a comment in `SceneSerializer.hpp:123`) | Cannot paste into in-game text fields | Medium | Small |
| No file dialog for scripts or the player | each editor window calls tinyfd directly (feature map 37.3) | Games cannot ask for a file | Low | Small |
| Fonts not bundled; only fixed font paths | feature map 37.4 | A Linux machine without the fonts shows missing glyphs | Medium | Small |

## 13. Editor and player

| Item | Where | What is missing | Impact | Size |
|---|---|---|---|---|
| Editor undo is on `UndoStack` now (resolved, see `docs/editor_undo.md`), but each step is a scene-text delta and each Undo rebuilds the whole scene | `editor/SceneUndo.hpp`, `EditorState.cpp`; GUI paths (Edit menu, shortcuts, History panel, `WatchEditUndo`) compiled but never run | Inserting or deleting an early object stores text up to the end of the scene; moving an asset the scene uses clears the scene history; mesh edit, Project panel, Timeline, Localization and the script editors keep their own undo; no per-object or per-property commands, so Inspector state and big scenes feel the rebuild | Medium | Medium |
| Engine accessors with no editor or player caller | `Debug/DrawCallRecorder`, `GpuTimer`, `MemoryTracker`, `FrameRecord`, `Optimize/StreamingGrid`, `Core/Cvar` and `Core/Jobs` stats: zero mentions in `editor/` and `player/` | Code and tests exist, no window or switch shows them (partly known as 23.x; this lists the rest) | Medium | Large |
| Project panel: no operating-system file watcher | `editor/ProjectBrowser.cpp` `Poll` (incremental `AssetDatabase::Scan`, at most every 2 s and 30x the last scan time; about 45 ms for 5,000 unchanged assets in a Release build, 135 ms in Debug, in the test) | A file changed outside the editor shows up on the next poll, not at once | Low | Medium |
| Project panel: thumbnails are made on the main thread | `Browser::ThumbFrame` budget (10 ms per frame), generators in `editor/main.cpp` (`RenderPrefabImage` and friends) | A folder of many big models fills in over several frames and the frame time grows by up to the budget; a worker thread needs a scene renderer that does not touch editor state | Low | Large |
| Project panel: colour labels are not the database's labels | `.okaylabels` in `editor/main.cpp` vs `AssetRecord::labels` | Two label systems; colour labels do not travel in the `.okaymeta` | Low | Medium |
| Plugin API has no native plugins | feature map 24.1 (Planned) | Only script-level plugin windows | Medium | Large |
| `editor/main.cpp` and `player/main.cpp` still hold panels, build, UI and server entry | feature map 15.10, 19.4 | Slow to change, merge conflicts | Low | Large |
| Dead code: `DrawFlowGraph` (about 1,060 lines), `g_bevelWidth` | `editor/main.cpp:24374, 3032` (already in `unfinished-audit.md`) | None for users | Low | Small |

## 14. Accessibility, localization, serialization and shared

| Item | Where | What is missing | Impact | Size |
|---|---|---|---|---|
| Accessibility: five settings are wired, five are not (partly done) | Wired and tested (`accessibility_wired`, `accessibility_render`): text size (screen-space text, button, toggle, keybind, tooltip, context-menu labels), colour filter (3D frame in the player plus UI flat colours and text), high contrast (UI text), reduce motion (camera shake, UI slide, pop and loop animations), reduce flashing (`flash` action, UI blink loop). All five rows sit on the Settings screen (`SettingsScreen`, saved to Prefs). NOT DONE and not in the UI: subtitles and sound captions (no widget draws `CaptionManager` lines), hold-to-toggle, aim assist, screen-reader hints, head bob and landing dip under reduce motion, Achromatopsia (no correction exists) | A player can use the five wired rows; the others still have no reader, so no control for them is offered. Wired limits: slider, dropdown, input and progress text keep their size; images drawn by UI widgets are not colour filtered; the editor and launcher (okayui) do not read the settings; Settings screens saved before this change lack the new rows until the Settings prefab is added again | Medium | Medium |
| Localization tools (`LocTable`, `LocCheck`, `LocPreview`, `LocScan`) not reachable from the editor | zero references in `editor/` or `player/` | Missing-key reports, placeholder mismatches and pseudo-locale preview cannot be used by a normal user (this sharpens 1.16 and 31.1 with the exact consumer count) | Medium | Medium |
| Duplicate components of one type save only the first (except a few listed types) | `SceneSerializer.cpp` (feature map 1.7) | Two colliders or two lights on one object lose one on save | Medium | Medium |
| Right-to-left languages unsupported (see section 9) | see above | | Medium | Large |
| Lightmap baking has no test | `Render/Lightmap.hpp` (`BakeFaceLighting`); no test names it; editor calls it | Bake regressions invisible | Low | Small |
| Other code with no test naming it | `UITouchScroll`, `ItemPresentation` (`DescribeItem`, `InventoryKeyWarning`), `LoadingScreenLayout`, `DeathLock` | Small features without any safety net | Low | Small |
| Anti-cheat Windows probes vs Linux: stubbed clock probe | `shared/AntiCheatChecks.hpp:337` (`#else`) | Behaviour differs by OS | Low | Small |

## Counts

High: 7. Medium: 58. Low: 29. Total rows: 94.

## Top 15 to tackle first

1. **(Mostly resolved) Accessibility settings** (sec 14). Text size, colour filter, high contrast, reduce motion and reduce flashing now change the player and are on the Settings screen. Still open: subtitles and captions, hold-to-toggle, aim assist, screen-reader hints, editor and launcher chrome.
2. **SSAO missing on Direct3D 11** (sec 1). Windows is the main target and D3D11 is its default; a lighting switch that does nothing there is the most visible renderer gap.
3. **Editor Play lacks the extended keys and extra pads** (sec 5). A few lines copying `AppendSdlExtendedKeys` and `pads.Feed` fix games that behave differently in Play and in a build.
4. **(Resolved) Lua and C# backends removed** (sec 10). The optional third-party backends were dropped; old scenes naming them load and report "language not supported".
5. **Mesh Collider 3D** (sec 2). Exact triangle collision for static geometry is done; the remaining gap is a convex-hull shape so a mesh can be a dynamic body, and collision callbacks for bodies landing on it.
6. **(Resolved) Save framework no-code route** (was sec 13). A Saveable component stored in the scene file (transform, active, registry properties, script and visual-script variables), a Save Settings component (autosave interval and on scene start), Save / Load / Delete / Continue / slot exists / last saved time as script builtins, visual-script `Fn.game_*` nodes and Action List `game_*` actions, and a Save Slots UI screen. See `docs/save_framework.md`.
7. **Direct3D 12 has no post-processing** (sec 1). Either bring it to parity or label it "preview" in Build Settings and keep it off by default.
8. ~~**Audio is mono and fully decoded** (sec 4).~~ Done: clips keep stereo, long music streams from disk in a fixed small amount of memory (AudioSource "Stream", Music Player), see `docs/audio.md`. The other audio gaps stay in section 4.
9. **(Resolved) Undo uses full snapshots** (sec 13). The editor now records scene deltas on `UndoStack` (named steps, merged drags, GUID-exact delete restore, history kept across Play, no recording during Play). What is left is a narrower Medium row in sec 13: per-object commands, and the unrun GUI paths.
10. ~~**Navigation: off-mesh links and area costs** (sec 6).~~ Done: Nav Mesh Modifier areas with per-area costs and agent masks, and Nav Mesh Links (jump, drop, ladder) that agents plan through and cross, see `docs/navigation.md`. Obstacle carving is done too (resolved row in sec 6), see `docs/navigation.md`.
11. **Cast shadows from spot and point lights on D3D12 and software** (sec 1) or a visible warning in the Light Inspector (small).
12. **(Resolved) Visual script export turns Wait or Delay into comments** (sec 10). Wait and Delay emit `after(seconds, "fn")`; the nodes that follow run in that function after the delay. One test runs the export and the graph for the same delay. Timer is still a comment.
13. **Joint3D hinge limits** (sec 2). Small change with big effect on doors and levers; reuse `Math/JointLimit.hpp`.
14. **Sound captions and respawn replication** (sec 7, 11). Small, user-visible multiplayer and accessibility fixes.
15. **Localization tools into the Localization window** (sec 14). The checks and previews are built and tested; the window just needs to call them, which also removes duplicated grid logic.
