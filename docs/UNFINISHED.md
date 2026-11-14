# Unfinished features

What is not finished yet, so we can pick it off. Anything unfinished is hidden or labelled in the product (see "Hidden or limited today"); this page is the to-do list behind that.

Sources: the feature map (`docs/FEATURE_MAP.md`, rows marked Partial or Planned, with their Open notes) and the hand-written lists below.

The feature map currently has **123** rows that are Partial or Planned.

## Audits

- [Engine-wide audit](unfinished_engine.md): 92 findings across 14 subsystems (9 High, 57 Medium, 26 Low), each with the file and line, what is missing, impact and size, and a top 15 to tackle first. The High items: accessibility settings that nothing reads, SSAO on Direct3D 11 (written, unrun on a GPU), no post-processing on Direct3D 12 (labelled preview), the editor's Play differing from a built game on keys and gamepads, a mesh collider that is only a bounding box, navigation without off-mesh links or area costs, audio that is mono and fully decoded, editor undo that uses full-scene snapshots, and a save framework with no editor UI.

- [Launcher audit](unfinished_launcher.md): 38 findings in six areas (6 High open, 2 more High done in code and waiting on the owner to run SQL; 15 Medium, 15 Low), with file and line, impact and size, and a top 10 to tackle first. The High items: typing indicators and read receipts need database tables no SQL file creates, voice calls need a deployed voice server, blocking and privacy settings only filter on the device, there is no user report button, the forgot-password email has no landing page, no account deletion or data export, and updates are not signed (kick, ban and admin roles for servers and group chats are now built; the owner applies `docs/sql/member_moderation.sql`).

## Hidden, removed or limited today

| What | State | What is missing |
|---|---|---|
| Texture Filter, Wrap and Mipmaps import controls | Hidden (`kShowTextureSamplerSettings`) | Work in the software and OpenGL renderers; the Direct3D 11 renderer has per-texture filter and wrap code that has not been run, Mipmaps do nothing there, Direct3D 12 reads none |
| Import settings sRGB, Compress | Removed from the UI | The renderers do no colour-space handling and no compressed GPU path exists. Audio loading and mix-down are applied (see `docs/audio.md`); quality is not offered because audio is not re-encoded |
| OBJ/MTL textures with an absolute path | Limited (#753) | An absolute or `../`-escaping `map_Kd` path is dropped on import for safety, so the material has no texture; copy the texture next to the model. A project-root allow-list could bring absolute paths back safely |
| Models already in a scene on import-settings Apply | Limited | Stored meshes are not re-imported; the setting applies the next time the model is added |
| Ten visual-script nodes: `Fn.net_join_link`, `Fn.net_pin_host`, `Fn.pf_*` (delete, leaderboard_top, load, login, login_password, register, save, set_stat) | Hidden from the palette (`kHiddenUntilTested`) | No test covers them; they need a network or PlayFab service |
| 15 online nodes with only indirect coverage (net host/join/relay/queue/browser) | Visible | Never run as nodes in a test; the calls under them are covered by network suites |
| Six engine-API nodes: Input.DefineAction, Input.DefineDirection, Audio.SaveSettings, Rigidbody.AddTorque (covered by other suites); Input.Rumble and Fog.SetMode proven | Partly proven | Show nothing in a headless scene |
| Photon, PlayFab and Custom net providers | C++ extension points only | Need the vendor SDKs and accounts; nothing in the editor selects them |
| Play start hitch in the editor | `EditorState::Play()` still runs on the click frame: snapshot text, loading every AudioSource file (each source holds its own copy of the decoded clip), validating every visual script, and (in the editor) baking each NavMeshSurface with Bake Before Play | `tests/test_play_start_bench.cpp` times each step. Not done: skip the bake when nothing changed, share one decoded clip between sources, spread the work over frames. The GPU side (first draw of the Game view) cannot be measured headless |
| Steamworks backend | Off by default | Needs Valve's proprietary SDK |
| Glass UI theme | Visible, no real blur | The shared draw path cannot read the framebuffer back |
| Rich text tags | Screen-space UI text only | World (3D) text ignores them; italic and bold are approximations |
| UI nameplate | Health bar bound | Name and level are plain text, not bound to data |
| UI Dropdown | Scrolls with wheel and keys | No touch or drag scrolling |
| Inventory screen | Shows the bag, drags and splits stacks, moves between chest, hotbar, backpack and grid bags | Networked and scripted slot-to-grid moves, durability wear and repair; own-code arbiter for wheel and use key (only lean and bag-key clashes are handled), item names cut by measured width and icon file re-check are not ported; see [inventory.md](inventory.md) |
| Mixamo Soldier and Xbot samples | Removed | |
| LedgeGrab hands-on-ledge IK | Honest tooltip: not wired | Animation and IK team |
| App icon | Placeholder OkaySpace logo | Branding decision |
| D3D11 / D3D12 renderers | Not buildable or runnable in our Linux environment | Only compile-checked with MinGW in CI |

## Planned: visual-script core upgrade

Steps 1 and 2 turned out to be mostly shipped already (see "Declared variables, lists, maps and structs" in `docs/visual_scripting.md`); the audit and the remaining gaps are below. Steps 3 to 5 are not started. One at a time, each shipped only when finished, tested and usable in the editor:

1. DONE (earlier work, audited again here): a better variable system. Declared variables (`decl name Type scope default`) with Number, Bool, Text, Vector3, Color, Object, `List<T>`, `Map<T>` and struct types; object, scene and global scopes with defaults editable in the Inspector; typed Set / Add Var; old `var` lines still load (`tests/data/vs_core_legacy.okayvs` is a graph in the old format, run by `vs_variables`). Not done: a declared "local" scope (function locals via Set Local stay untyped), and a separate "graph" scope distinct from "object" (today `object` is the graph's own variables); typed lists of structs.
2. DONE for lists and maps: add, remove (by index and by value), insert, set, find, sort, reverse, clear, copy, for each, length, keys, values, count, plus List Append, List Slice, List Pop, Map Get Or and Map Merge (added now), with element types carried through the graph checker. Not done: lists and maps have no OkayScript export (Generate script lists them as not translated); no sort-by-key or sort-with-comparer; List Pop takes only the last item.
3. Custom events with typed parameters, and delegates (bind, unbind, call).
4. Graphs talking to each other: call a function, fire an event or read a variable on another object's graph, with the graph checker flagging broken links.
5. Custom function nodes: user-defined nodes with typed pins, reusable across graphs and listed in the palette.

The text-script nodes below depend on this.

## Planned: text-script nodes in visual script

Not started. These would let a visual-script graph run a short text script next to the normal nodes. All of it is our own code: no third-party Lua, Python or JavaScript runtime.

Scope: a script node is for what nodes are bad at. That is algorithms and loops, tables and dictionaries, string and data handling, compact rules, and reusing snippets. Engine features (movement, physics, UI, audio, animation, networking) stay as nodes. A script calls the existing node and never reimplements it.

Order, one step at a time, each shipped only when finished and tested in both directions (node to script and script to node):

1. A "Run OkayScript" and expression node: typed pins, exec in and out, errors with line numbers.
2. Lua-style syntax compiled to the same VM.
3. A JavaScript-style subset, then a Python-style subset.

These are "-style" subsets, not the real languages: code written for the real Lua, JavaScript or Python will often not run unchanged, and the standard libraries are not included. Differences (for example 1-based arrays in Lua) will be documented per language.

## Planned work from the to-do list

- Admin: create test servers without a game (not started: "test server" needs a definition from the owner; see unfinished_launcher.md)
- Improve the editor Plugins menu
- Improve inventory systems (survival-game style): partly done (moves, stack and weight rules, slot-to-grid moves); see [inventory.md](inventory.md) for what is left
- Scene Manager window in the editor
- Full UI component library (essential, game, app, layout, data-driven)
- Improve the blocky character's arms, especially in first person
- Script language phases 2 to 5: classes, physics and events, visual-script shared layer, tooling and debugger, export, replace the old VM
- Per-face materials (renderer submeshes)
- Script editor: classes, default templates, no random script on New Script
- Prone for first and third person
- Player customization: more options
- Humanoid animation quality, procedural object holding, 2.5D controller improvements, backpedal and strafe animations, character joint hinges and detail (animation team)
- Splitting the largest files: launcher `main.cpp`, the OkayScript VM, component serializer registry, editor `main.cpp` into panel files
- Remove the hard-coded Supabase URL and key from CMake; stop committing dist binaries
- Security plan SEC1: relay isolation, script file-path safety, updater signing, reactions privacy SQL; anti-cheat hardening
- README matching reality and a component audit document

## From the feature map (Partial and Planned)

### 1. Core & scene

| ID | Feature | Status | Open |
|---|---|---|---|
| [1.6](feature_map/01-core-scene.md#16-prefabs-and-templates) | Prefabs and templates | Partial | prefab mode (editing a prefab in its own scene; Open only isolates the instance); the old... |
| [1.11](feature_map/01-core-scene.md#111-asset-manager) | Asset manager | Partial | move existing loaders (`DataAsset::Cached`, `AudioClip::LoadCached`, the renderers' own loads) onto it; track GPU resources; an... |
| [1.12](feature_map/01-core-scene.md#112-asset-database) | Asset database | Partial | importers reading the remaining settings; GUID references inside scene files; build/export closure wiring; binary formats such as `.fbx` have no dependency edges; no file watcher (the Project panel polls the incremental scan every 2 s or more); an asset has a `.okaymeta` only after it is given a tag or import settings... |
| [1.13](feature_map/01-core-scene.md#113-object-pooling) | Object pooling | Partial | a script builtin and a no-code Spawner option; resetting Rigidbody velocities is each component's job |
| [1.16](feature_map/01-core-scene.md#116-localization-tools) | Localization tools | Partial | JSON import and export in the window, a confirm step before CSV import, the font coverage report, and right-to-left layout (the window shows a mirrored preview of the selected key)... |
| [1.19](feature_map/01-core-scene.md#119-property-reflection) | Property reflection | Partial | not registered yet: controllers, scripts, the other UI widgets, navigation, networking; animation and IK components are left to... |
| [1.20](feature_map/01-core-scene.md#120-asset-thumbnails) | Asset thumbnails | Partial | pictures are made on the main thread inside a per-frame time budget, not on a worker thread; one 128 px size scaled to the tile size; no pictures for fonts or data assets; a scene picture loads the whole scene... |

### 3. 3D modeling & import

| ID | Feature | Status | Open |
|---|---|---|---|
| [3.5](feature_map/03-modeling-import.md#35-mesh-assets-and-primitives) | Mesh assets and primitives | Partial | M5b per-face materials: PolyFace has a material slot but Mesh has no per-triangle slot; old map says Sphere, Cylinder, Cone... |
| [3.6](feature_map/03-modeling-import.md#36-model-import) | Model import | Partial | possible doc bug: docs/animation.md Tips says FBX animations import via Assimp, but FBX is read by the built-in ufbx path; packed... |

### 5. Animation & IK

| ID | Feature | Status | Open |
|---|---|---|---|
| [5.9](feature_map/05-animation-ik.md#59-object-holding) | Object holding | Partial | old map: holding PRs 3-4: body adjust is done (#389), multiplayer sync (PR 4, branch `claude/holding-net`) and grip editor (PR 5... |

### 12. In-game UI

| ID | Feature | Status | Open |
|---|---|---|---|
| [12.7](feature_map/12-ingame-ui.md#127-themes-and-the-launcher-matching-ui-kit) | Themes and the launcher-matching UI kit | Partial | "full component library" (see the missing list in 12.3 and 12.4); stylesheet assets with selectors... |
| [12.10](feature_map/12-ingame-ui.md#1210-ui-editing-in-the-editor) | UI editing in the editor | Partial | UI plan P4: anchor preset picker and pivot handles, UI prefabs with variants, UIDocument-to-hierarchy round trip, a `--selftest`... |
| [12.13](feature_map/12-ingame-ui.md#1213-data-binding-ui-animation-and-script-control) | Data binding, UI animation and script control | Partial | UI plan P3 not found: `ui_create`, `ui_destroy`, `ui_find`, generic `ui_get/ui_set(prop)`, events `on_hover_enter`... |

### 13. Scripting

| ID | Feature | Status | Open |
|---|---|---|---|
| [13.2](feature_map/13-scripting.md#132-c-style-language-core) | C#-style language core | Partial | in flight (branches, no PR, 2026-10-26): claude/csharp-p1-events, -inheritance, -try, -coroutines, -getcomponent... |

### 15. Editor

| ID | Feature | Status | Open |
|---|---|---|---|
| [15.10](feature_map/15-editor.md#1510-splitting-editormaincpp-into-panel-files-45k-lines) | Splitting `editor/main.cpp` into panel files (~45k lines) | Partial | no PR for the next split found (#577 changed about 280 lines of `main.cpp` for the layouts, the Script Editor find and replace... |

### 16. Networking & multiplayer

| ID | Feature | Status | Open |
|---|---|---|---|
| [16.7](feature_map/16-networking.md#167-voice-chat-server) | Voice chat server | Partial | no tests for ADPCM, jitter buffer or room-key handling; launcher voice calls now send version-3 hellos with the relay cookie and... |

### 17. Launcher & platform

| ID | Feature | Status | Open |
|---|---|---|---|
| [17.11](feature_map/17-launcher-platform.md#1711-security-plan-sec1) | Security plan SEC1 | Partial | sign the launcher, editor and player downloads (ed25519 as the anti-cheat manifest does) and verify before swapping; the SEC1... |

### 18. Build, export & release

| ID | Feature | Status | Open |
|---|---|---|---|
| [18.2](feature_map/18-build-export-release.md#182-web-and-mobile-export) | Web and mobile export | Partial | the Build Settings tooltip says "Export an Android Studio project" but the code writes assets and a README only (possible... |

### 19. Docs, tests & code health

| ID | Feature | Status | Open |
|---|---|---|---|
| [19.4](feature_map/19-docs-tests-health.md#194-splitting-the-largest-files-editor-launcher-okayscript-vm-serializer) | Splitting the largest files (editor, launcher, OkayScript VM, serializer) | Planned | splitting `launcher/main.cpp` needs a plan per page (17.1 to 17.16 are the natural seams); `player/main.cpp` is 2,658 lines and... |
| [19.5](feature_map/19-docs-tests-health.md#195-roadmap-and-pr-template) | Roadmap and PR template | Partial | the anti-cheat hardening list still shows items 5, 6, 8, 9, 10, 12, 13, 14 as open, although `git log` has #350, #360, #363... |

### 20. Asset management (Critical)

| ID | Feature | Status | Open |
|---|---|---|---|
| [20.1](feature_map/20-35-planned.md#201-asset-browser) | Asset browser | Partial | Two sources of truth for labels (colour labels in `.okaylabels` vs tags in `.okaymeta`); no file watcher, outside changes show on the next poll (2 s or more); no automated UI test of the window... |
| [20.2](feature_map/20-35-planned.md#202-dependencies-and-references) | Dependencies and references | Partial | possible bug: Build Game collects assets from the open scene only (extra scenes in the build list are copied but not scanned)... |
| [20.3](feature_map/20-35-planned.md#203-guids) | GUIDs | Partial | meta files are matched by content hash (FNV-1a, not cryptographic); files over 512 MB hash only head and tail; a project already... |
| [20.4](feature_map/20-35-planned.md#204-import-settings) | Import settings | Partial | no UI; two places currently own texture options (the asset meta and the MeshRenderer fields), the plan must say which wins... |
| [20.5](feature_map/20-35-planned.md#205-thumbnails) | Thumbnails | Partial | pictures are made on the main thread inside a time budget, not on a worker thread; a scene thumbnail loads the whole scene; no thumbnail for fonts or data assets |

### 21. Prefab system (Critical)

| ID | Feature | Status | Open |
|---|---|---|---|
| [21.2](feature_map/20-35-planned.md#212-nested-prefabs) | Nested prefabs | Partial | objects are matched by depth-first position and name, so reordering many objects can pair the wrong ones; nesting errors are not listed in the Console on load; a second instance of the applied file inside the same outer instance is not updated by Apply... |
| [21.3](feature_map/20-35-planned.md#213-variants) | Variants | Partial | no variant badge on the Project panel tile; scene files never contain variant records (only prefab files do); objects added to or lost from a variant instance cannot be applied into the variant... |

### 22. Undo/redo & recovery (Critical)

| ID | Feature | Status | Open |
|---|---|---|---|
| [22.1](feature_map/20-35-planned.md#221-undo-history) | Undo history | Partial | each snapshot is a full scene text (memory grows with scene size); three separate stacks can confuse Ctrl+Z; behaviour of the... |
| [22.2](feature_map/20-35-planned.md#222-autosave) | Autosave | Partial | a leftover `.autosave` from an older editor can still sit beside the scene until it is recovered or discarded; a project under... |
| [22.3](feature_map/20-35-planned.md#223-crash-recovery) | Crash recovery | Partial | the report format is only parsed by the launcher; no upload; the player has no handler. |
| [22.4](feature_map/20-35-planned.md#224-backups) | Backups | Partial | a scene save still keeps one `.bak` beside the scene, and a leftover `.autosave` from an older editor may too; a project under... |

### 23. Profiling & debugging (Critical)

| ID | Feature | Status | Open |
|---|---|---|---|
| [23.1](feature_map/20-35-planned.md#231-cpugpu-profiler) | CPU/GPU profiler | Partial | the "GPU" number is CPU wall time around the render call; the ring is only 4 seconds at 60 fps; `FrameHistory` keeps 600 frames... |
| [23.2](feature_map/20-35-planned.md#232-memory-tracking) | Memory tracking | Partial | no platform memory numbers (process working set); heap hooks off by default. |
| [23.3](feature_map/20-35-planned.md#233-frame-debugger) | Frame debugger | Partial | each renderer (software, GL, D3D11, D3D12) draws differently, so four hooks are needed. |

### 24. Plugin & extension system (Critical)

| ID | Feature | Status | Open |
|---|---|---|---|
| [24.1](feature_map/20-35-planned.md#241-c-plugins) | C++ plugins | Planned | native plugins can crash the editor; ABI and ownership of memory need an explicit rule; use the property registry (on main, #578... |
| [24.3](feature_map/20-35-planned.md#243-custom-nodes) | Custom nodes | Planned | node text format must keep loading old graphs when a custom node type is missing (placeholder node). |
| [24.4](feature_map/20-35-planned.md#244-plugin-sdk) | Plugin SDK | Partial | versioning rule for the engine ABI is undecided (see 25.3). |

### 25. Project management (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [25.2](feature_map/20-35-planned.md#252-project-settings) | Project settings | Partial | `project.okayproj` is loaded from the current directory, so opening a project from elsewhere depends on the working directory... |
| [25.3](feature_map/20-35-planned.md#253-migration) | Migration | Partial | no policy yet on how long old formats stay readable; `MigrateThroughEngine` drops records this build does not know and gives... |
| [25.4](feature_map/20-35-planned.md#254-dependency-management) | Dependency management | Planned | where packages are hosted (Workshop, GitHub) is undecided. |

### 26. Shader & material editor (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [26.1](feature_map/20-35-planned.md#261-visual-shader-graphs) | Visual shader graphs | Planned | four renderers (software, GL, D3D11, D3D12) must stay consistent; D3D12 has no normal, gloss or AO maps today. |
| [26.2](feature_map/20-35-planned.md#262-material-editor) | Material editor | Partial | preview is not what the renderer draws; materials are copied into the renderer (`ApplyTo`), not referenced, so editing the file... |
| [26.3](feature_map/20-35-planned.md#263-custom-shaders) | Custom shaders | Planned | D3D12 is "v1" and opt-in; shader hot reload would need 34.3. |
| [26.4](feature_map/20-35-planned.md#264-pbr) | PBR | Partial | four renderers to keep equal; D3D12 lacks maps; the software renderer is the reference. |

### 27. Optimization tools (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [27.1](feature_map/20-35-planned.md#271-lod-generation) | LOD generation | Partial | simplifier ignores colour and material changes in its error; a mesh with `cornerUVs` is split into per-vertex UVs first; skinned... |
| [27.2](feature_map/20-35-planned.md#272-occlusion-culling) | Occlusion culling | Partial | CPU rasteriser only (no GPU queries); conservative, so it may draw more than needed; no components yet. |
| [27.3](feature_map/20-35-planned.md#273-batching) | Batching | Partial | `isStatic` is only "a hint for tools"; merged meshes need a draw path in each renderer; skinned and transparent objects are never... |
| [27.4](feature_map/20-35-planned.md#274-streaming) | Streaming | Partial | a loaded cell is created in one frame (hitch risk); saves do not know about unloaded cells (see 35.3); cells are on the XZ plane... |

### 28. Version control integration (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [28.1](feature_map/20-35-planned.md#281-git-integration) | Git integration | Partial | no automated test; no conflict handling after a pull (see 28.4); shelling out to git means errors come back as text only. |
| [28.2](feature_map/20-35-planned.md#282-diffs) | Diffs | Partial | no UI and no tool call these yet; a file without ids pairs objects by name path, so a rename there looks like a removal plus an... |
| [28.3](feature_map/20-35-planned.md#283-project-history) | Project history | Planned | depends on git being installed; projects without git only get local backups. |
| [28.4](feature_map/20-35-planned.md#284-merge-conflict-tools) | Merge conflict tools | Partial | the library only merges text of files at one format version; ours and theirs at different versions are not merged; possible... |

### 29. Terrain & world building tools (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [29.2](feature_map/20-35-planned.md#292-vegetation-painting) | Vegetation painting | Partial | object-per-instance scatter will not scale; no undo grouping for a scatter pass beyond the scene snapshot. |
| [29.3](feature_map/20-35-planned.md#293-procedural-generation) | Procedural generation | Partial | possible duplicate: `Components/Terrain.cpp` has its own noise code next to `Math/Noise`. |

### 30. Character creator (Medium)

| ID | Feature | Status | Open |
|---|---|---|---|
| [30.1](feature_map/20-35-planned.md#301-customization) | Customization | Partial | the part rig and the single-mesh rig are two build paths for one component (see 4.9). |
| [30.2](feature_map/20-35-planned.md#302-rig-generation) | Rig generation | Partial | rigid weights tear at joints; animation and IK code belong to Grok's sessions. |
| [30.3](feature_map/20-35-planned.md#303-clothing) | Clothing | Partial | no layered clothing, cloth physics (see 52). |
| [30.4](feature_map/20-35-planned.md#304-morph-targets) | Morph targets | Partial | shapes are stored sparse and applied to the bind pose; large faces with many shapes cost CPU each frame (not measured). |

### 31. Localization tools (Medium)

| ID | Feature | Status | Open |
|---|---|---|---|
| [31.1](feature_map/20-35-planned.md#311-translation-tables) | Translation tables | Partial | JSON import and export in the window, a confirm step before CSV import, and translation memory. The window uses `LocTable`, `LocCheck` and `LocScan`... |
| [31.2](feature_map/20-35-planned.md#312-font-fallback) | Font fallback | Partial | system fonts differ between machines, so a CJK game should ship its own font; RTL is drawn left to right. |
| [31.3](feature_map/20-35-planned.md#313-language-previews) | Language previews | Partial | the Scene view layout does not mirror for right-to-left languages. The Localization window shows a mirrored `LocPreview` of the selected key. |

### 32. Accessibility (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [32.1](feature_map/20-35-planned.md#321-controller-navigation) | Controller navigation | Partial | one shared ring is drawn from the theme and the accessibility settings; there is no focus sound; the graph is built from screen rectangles each time it is asked |
| [32.2](feature_map/20-35-planned.md#322-subtitles) | Subtitles | Partial | speaker colours need a colour-vision-safe palette (32.3); subtitle size, background and speaker-name rows, and SRT import. An Audio Source caption is shown when sound captions are on. |
| [32.3](feature_map/20-35-planned.md#323-colorblind-options) | Colorblind options | Partial | the GPU renderers do not read the settings; the speaker-colour palette in `CaptionManager` is the only consumer in the engine. |
| [32.4](feature_map/20-35-planned.md#324-remapping) | Remapping | Partial | the UI navigation keys are fixed in code; mouse-button and wheel rebinding coverage is not checked. |

### 33. Automated testing (Critical)

| ID | Feature | Status | Open |
|---|---|---|---|
| [33.1](feature_map/20-35-planned.md#331-play-mode-tests) | Play-mode tests | Partial | a script that reads the wall clock or uses threads makes a play-test non-deterministic (stated in `docs/automated_testing.md`)... |
| [33.3](feature_map/20-35-planned.md#333-regression-testing) | Regression testing | Partial | compilers round shading slightly differently, so goldens need a tolerance; the software renderer draws mesh renderers only (no... |

### 34. Live editing (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [34.1](feature_map/20-35-planned.md#341-properties-during-play) | Properties during Play | Partial | objects created or destroyed in Play have no edit-scene counterpart to keep changes on; the registry does not cover controllers... |
| [34.2](feature_map/20-35-planned.md#342-live-object-inspector) | Live object inspector | Partial | no per-object runtime counters; the live view relies on the same redraw as the edit view. |
| [34.3](feature_map/20-35-planned.md#343-hot-reload) | Hot reload | Partial | in-place replacement happens inside `Pump()`, so readers must be on that thread; a failed half-written file keeps the old data... |

### 35. Save game framework (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [35.2](feature_map/20-35-planned.md#352-checkpoints) | Checkpoints | Partial | AutoSave writes one slot, so a corrupt moment is overwritten; wait for the framework decision (35.1). |

### 36. Math & geometry

| ID | Feature | Status | Open |
|---|---|---|---|
| [36.6](feature_map/36-math-geometry.md#366-geometry-helpers-elsewhere) | Geometry helpers elsewhere | Partial | duplicated ray-box and ray-sphere code in `Physics2D`/`Physics3D`, `EditTransform::Ray`, `Lightmap::RayTriangle` and `Frustum`... |
| [36.7](feature_map/36-math-geometry.md#367-noise-curves-colour-spaces-and-random-streams) | Noise, curves, colour spaces and random streams | Partial | move terrain, textures, particles and meshes onto the shared noise so a seed means the same thing everywhere; `Mesh::PropRand`... |

### 37. Platform & windowing

| ID | Feature | Status | Open |
|---|---|---|---|
| [37.5](feature_map/37-platform-windowing.md#375-web-and-mobile-shims) | Web and mobile shims | Partial | Android and iOS projects are not in the repo; web has no networking; `docs/web_mobile.md` still says to read `SDL_FINGER*`... |

### 38. Render backends & GPU

| ID | Feature | Status | Open |
|---|---|---|---|
| [38.4](feature_map/38-render-backends.md#384-direct3d-12-backend) | Direct3D 12 backend | Partial | possible doc mismatch: the header says `samples` is accepted but MSAA is not applied, while the `.cpp` header comment and code... |
| [38.11](feature_map/38-render-backends.md#3811-gpu-timing-and-frame-debugging) | GPU timing and frame debugging | Partial | wiring real GPU timers into each backend; a frame debugger window (area 23) |

### 39. Security & anti-cheat

| ID | Feature | Status | Open |
|---|---|---|---|
| [39.3](feature_map/39-security-anticheat.md#393-session-tickets-and-the-ticket-service) | Session tickets and the ticket service | Partial | deploy the function and create the key pair (the owner's step, ROADMAP "Waiting on Iman"); wire the launcher or game to fetch a... |
| [39.4](feature_map/39-security-anticheat.md#394-client-integrity-signed-manifest-and-launch-check) | Client integrity: signed manifest and launch check | Partial | set `OKAY_AC_TRUSTED_KEYS` so the check becomes a failure (planned in `release.yml`); hashing every file costs time on big games... |
| [39.5](feature_map/39-security-anticheat.md#395-runtime-checks-and-the-clock-heartbeat) | Runtime checks and the clock heartbeat | Partial | no kernel driver, memory scanning or obfuscation (by design); older clients never answer the heartbeat, so turn `maxClockDrift`... |
| [39.11](feature_map/39-security-anticheat.md#3911-account-backend-and-updater-hardening) | Account, backend and updater hardening | Partial | possible weaknesses: the local password hash is a hand-made chain of SHA-256 rather than a standard KDF, and its salt comes from... |

### 40. OkayUI standalone library

| ID | Feature | Status | Open |
|---|---|---|---|
| [40.3](feature_map/40-okayui-library.md#403-backends-and-frame-loop) | Backends and frame loop | Partial | no test of the D3D11 path found; skipped in the web build (per `CMakeLists.txt` and `scripts/build-web.sh` comments) |

### 43. 2D tools

| ID | Feature | Status | Open |
|---|---|---|---|
| [43.3](feature_map/43-proposals.md#433-sprite-animation-tools) | Sprite animation tools | Partial | first step: a Sprite Animation window with a frame strip, per-frame duration and animation events (call a script function on... |
| [43.4](feature_map/43-proposals.md#434-2d-lighting) | 2D lighting | Planned | first step: an unlit-versus-lit flag and a simple 2D point light with falloff that tints sprites in the software renderer first... |

### 44. Level design and blockout tools

| ID | Feature | Status | Open |
|---|---|---|---|
| [44.3](feature_map/43-proposals.md#443-measuring-and-level-metrics) | Measuring and level metrics | Planned | first step: a Measure tool (click two points, show distance and slope) and a reference-figure toggle showing a 1.8 m person and... |
| [44.4](feature_map/43-proposals.md#444-volumes-and-zones) | Volumes and zones | Partial | first step: a GameObject > Volume menu (trigger, kill, music, spawn) drawn as a tinted box with its name in the Scene view; one... |

### 45. Open-world streaming and world partition

| ID | Feature | Status | Open |
|---|---|---|---|
| [45.1](feature_map/43-proposals.md#451-cell-authoring-and-baking) | Cell authoring and baking | Partial | first step: "Split scene into cells" (grid size, object assignment by position, writes cell prefabs and a manifest) and a Scene... |
| [45.2](feature_map/43-proposals.md#452-large-world-coordinates) | Large world coordinates | Planned | first step: measure where jitter starts in a test scene; then a floating-origin option that shifts the whole scene back when the... |
| [45.3](feature_map/43-proposals.md#453-streaming-aware-systems) | Streaming-aware systems | Planned | first step: per-cell navmesh bake stored with the cell and stitched at load; cell-level state records for saves. |
| [45.4](feature_map/43-proposals.md#454-distant-scenery) | Distant scenery | Planned | first step: merged low-detail meshes per cell ("hierarchical LOD") using the simplifier; billboard impostors for trees. |

### 46. Replay system

| ID | Feature | Status | Open |
|---|---|---|---|
| [46.1](feature_map/43-proposals.md#461-input-recording-and-playback) | Input recording and playback | Planned | first step: record `PlayerInput` per fixed tick plus the random seed and scene file, tie each tick to a `FrameRecorder` marker... |
| [46.2](feature_map/43-proposals.md#462-state-replay-and-killcam) | State replay and killcam | Planned | first step: write snapshots to a ring buffer and a free camera that reads them back. |
| [46.3](feature_map/43-proposals.md#463-replay-files-and-viewer) | Replay files and viewer | Planned | first step: define the file (header with version, scene, seed; per-tick records) with the same versioning rules as saves (35.4). |

### 47. Mod support

| ID | Feature | Status | Open |
|---|---|---|---|
| [47.1](feature_map/43-proposals.md#471-mod-loader-and-content-overrides) | Mod loader and content overrides | Planned | first step: asset lookup through an ordered list of folders (mods first), a `mod.txt` manifest (name, version, load order), and a... |
| [47.2](feature_map/43-proposals.md#472-mod-scripting-sandbox) | Mod scripting sandbox | Planned | first step: a capability flag per script host (files, network, process) defaulting to off for mod scripts. |
| [47.3](feature_map/43-proposals.md#473-mod-packaging-and-sharing) | Mod packaging and sharing | Planned | first step: add a `mod` kind, an installer that unpacks into the game's Mods folder, and an enable and disable list. |

### 48. Crash reporting and telemetry

| ID | Feature | Status | Open |
|---|---|---|---|
| [48.1](feature_map/43-proposals.md#481-crash-reports) | Crash reports | Partial | first step: reuse the editor's handler in the player (report next to the game, plus last Console lines); then an optional upload... |
| [48.2](feature_map/43-proposals.md#482-opt-in-analytics-events) | Opt-in analytics events | Planned | first step: an `Analytics::Event(name, fields)` API that does nothing unless consent is on, writes to a local file first, and a... |
| [48.3](feature_map/43-proposals.md#483-consent-and-privacy) | Consent and privacy | Planned | first step: a first-run consent dialog component and a settings toggle bound to one Prefs key that all senders check. |
| [48.4](feature_map/43-proposals.md#484-collection-backend-and-dashboards) | Collection backend and dashboards | Planned | first step: `crash_reports` and `events` tables with row-level security and an upload function that only inserts; a simple web... |

### 49. Cloud services

| ID | Feature | Status | Open |
|---|---|---|---|
| [49.1](feature_map/43-proposals.md#491-cloud-saves) | Cloud saves | Partial | first step: `ISaveSync` backends for Steam Cloud and PlayFab user data, with "newest wins" and a conflict prompt; size limits per... |
| [49.2](feature_map/43-proposals.md#492-leaderboards) | Leaderboards | Partial | first step: a small `Leaderboards` interface with the three as backends and a UI list component. |
| [49.3](feature_map/43-proposals.md#493-achievements-sync) | Achievements sync | Partial | first step: one `Unlock(id)` that writes local, calls Steam when present and queues an upload when signed in. |
| [49.4](feature_map/43-proposals.md#494-remote-configuration) | Remote configuration | Planned | first step: a signed JSON file fetched at start that sets cvars; falls back to the shipped defaults offline. |

### 50. Capture and media

| ID | Feature | Status | Open |
|---|---|---|---|
| [50.2](feature_map/43-proposals.md#502-photo-mode) | Photo mode | Planned | first step: a `PhotoMode` component: key toggles pause, hides the UI, switches to a free camera with sliders for field of view... |
| [50.3](feature_map/43-proposals.md#503-video-recording) | Video recording | Planned | first step: an image-sequence recorder at a fixed time step (so slow frames do not drop video frames), using... |
| [50.4](feature_map/43-proposals.md#504-video-playback) | Video playback | Planned | first step: choose a codec and a small decoder library (for example a simple open format) and a `VideoPlayer` component drawing... |

### 51. Onboarding, tutorials and samples

| ID | Feature | Status | Open |
|---|---|---|---|
| [51.1](feature_map/43-proposals.md#511-sample-gallery) | Sample gallery | Partial | first step: each template gets a short "What to look at" note shown when it opens (which objects, scripts and settings matter); a... |

### 52. Simulation extras (cloth, soft bodies, fluids, destruction, crowds)

| ID | Feature | Status | Open |
|---|---|---|---|
| [52.1](feature_map/43-proposals.md#521-cloth-and-hair) | Cloth and hair | Planned | first step: a rope and flag component on a particle grid with pin points and wind from `Environment/Wind`. |
| [52.2](feature_map/43-proposals.md#522-soft-bodies) | Soft bodies | Planned | first step: a spring-mass lattice on a mesh (same solver as 52.1). |
| [52.4](feature_map/43-proposals.md#524-destruction) | Destruction | Partial | first step: pre-fractured meshes (a mesh authored as pieces) with a break threshold and impulse. |

### 53. Day-night and weather hooks

| ID | Feature | Status | Open |
|---|---|---|---|
| [53.1](feature_map/43-proposals.md#531-time-of-day-events-and-gameplay) | Time-of-day events and gameplay | Partial | first step: publish events on the `EventBus` (HourChanged, Sunrise, Sunset) and a "run actions at hour X" component; possible... |
| [53.2](feature_map/43-proposals.md#532-weather-gameplay-effects) | Weather gameplay effects | Partial | first step: a `WeatherEffects` component mapping a preset to survival modifiers (wetness, temperature drop, friction scale... |

### 54. Debug draw, gizmos, dev console and cheats

| ID | Feature | Status | Open |
|---|---|---|---|
| [54.3](feature_map/43-proposals.md#543-in-game-dev-console) | In-game dev console | Partial | first step: a backtick-toggled console component using `Console::History/Lines/Complete`, and engine cvars (`r.shadows`... |
| [54.4](feature_map/43-proposals.md#544-cheats-and-debug-commands) | Cheats and debug commands | Planned | first step: register `god`, `give <item> [n]`, `tp <x> <y> <z>`, `time <hour>` against the survival, inventory and day-night... |

### 55. Script debugging and profiling

| ID | Feature | Status | Open |
|---|---|---|---|
| [55.1](feature_map/43-proposals.md#551-breakpoints-and-stepping-for-text-scripts) | Breakpoints and stepping for text scripts | Partial | first step: a line hook in the OkayScript VM and the typed-script interpreter (`engine/src/Script/Interpreter.cpp`) that calls a... |
| [55.2](feature_map/43-proposals.md#552-watch-and-call-stack) | Watch and call stack | Partial | first step: expose the interpreter's frame and locals read-only to the editor while paused. |

### 56. Platform targets and store packaging

| ID | Feature | Status | Open |
|---|---|---|---|
| [56.1](feature_map/43-proposals.md#561-desktop-packaging) | Desktop packaging | Partial | first step: Linux AppImage or tarball for exported games from the same Build dialog; macOS app bundle later (needs a macOS build... |
| [56.3](feature_map/43-proposals.md#563-web-export) | Web export | Partial | first step: package the web build as a zip ready for itch.io from the editor when emscripten is installed. |
| [56.4](feature_map/43-proposals.md#564-console-platforms) | Console platforms | Planned | first step: none until the owner decides; keeping platform code behind the `Platform/*` interfaces helps. |

### 57. Documentation website

| ID | Feature | Status | Open |
|---|---|---|---|
| [57.2](feature_map/43-proposals.md#572-generated-api-reference) | Generated API reference | Partial | first step: generate `scripting_reference.md` from the same builtin table the editor window uses so they cannot drift. |

### 58. Performance budgets and CI perf tests

| ID | Feature | Status | Open |
|---|---|---|---|
| [58.1](feature_map/43-proposals.md#581-performance-budgets) | Performance budgets | Planned | first step: a `budgets.okaybudget` text file (frame ms, draw calls, triangles, texture MB per scene) and a command that reports... |
| [58.3](feature_map/43-proposals.md#583-performance-regression-guard-in-ci) | Performance regression guard in CI | Planned | first step: store the benchmark output as a CI artifact and print a table in the job summary before gating anything. |
| [58.4](feature_map/43-proposals.md#584-build-size-and-load-time-report) | Build size and load-time report | Partial | first step: log scene load time and asset decode time (the async loader in #570 reports progress) in the Profiler. |

