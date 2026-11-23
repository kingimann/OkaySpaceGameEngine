# Systems

Generated from the `System:` lines of the feature map pages by `scripts/gen_feature_map.py`; do not edit.
Back to the [feature map](FEATURE_MAP.md).

A **system** is a named part of OkaySpace that does one job from start to end (Physics, Save Games, OkayScript, the Launcher Updater). Each system has one official name and is made of one or more sub-areas of the feature map. Its home area is the area of the sub-area that describes it.

- **Status** comes from the sub-areas' Status lines (Works, Partial, Basic, Planned, In flight); a system made of sub-areas in different states lists how many are in each.
- **Main code** lists its files, or the folders that hold three or more of them, largest first. Engine headers are under `engine/include/okay/`, their code under `engine/src/`.
- **Feature map** links every sub-area that is part of it; the first one is the one that describes it.
- To name or change a system, edit the `- **System:**` line of a sub-area in `docs/feature_map/` (format in `docs/feature_map/README.md`), then run `python3 scripts/gen_feature_map.py`.

## Areas

- [1. Core & scene](#1-core--scene): Game Loop and Time, Timers and Tweens, Scenes and Objects, Scene Files, Prefabs, Save Games, Whole-Scene Save, Localization, ECS World, Logging, Profiler, Asset Manager, Asset Database, Data Assets, Display Settings, Core Utilities, Property Registry
- [2. Rendering & world](#2-rendering--world): Renderer, Materials and Textures, Lighting and Shadows, Sky, Weather and Time of Day, Post-Processing, Terrain and Water, Particles, Culling and LOD, Fonts and Images, Cameras, Splines, Sprites and Tilemaps
- [3. 3D modeling & import](#3-3d-modeling--import): Modeling, Meshes, Model Import, Rigging
- [4. Characters & cameras](#4-characters--cameras): Player Controller, Ready-Made Controllers, Vehicles, Character Creator, Seats
- [5. Animation & IK](#5-animation--ik): Animation, Inverse Kinematics, Ragdolls
- [6. Physics](#6-physics): Physics, Destruction and Projectiles
- [7. Audio](#7-audio): Audio
- [8. Input](#8-input): Input, Input Actions, Simple Input Actions
- [9. AI & navigation](#9-ai--navigation): Navigation, Grid Pathfinding, Behavior Trees, NPCs
- [10. Story & cinematics](#10-story--cinematics): Cutscenes, Dialogue, Quests
- [11. Gameplay systems](#11-gameplay-systems): Inventory and Items, Crafting, Health and Survival, Building, Spawners, Turns and Minimap, Combat, RPG Stats, No-Code Behaviours, Volumes and Zones
- [12. In-game UI](#12-in-game-ui): Game UI
- [13. Scripting](#13-scripting): OkayScript, Typed OkayScript
- [14. Visual scripting](#14-visual-scripting): Visual Scripting, Action Lists
- [15. Editor](#15-editor): Project Panel, Build Settings, Editor Plugins, Undo and Recovery, Editor
- [16. Networking & multiplayer](#16-networking--multiplayer): Networking, Matchmaking and Lobbies, Dedicated Server, Server Anti-Cheat, Voice Chat, Relay
- [17. Launcher & platform](#17-launcher--platform): Launcher, Launcher Updater, Accounts, Community, Marketplace and Workshop, Profiles and Achievements, Steam Integration, Crash Reports and Telemetry, Platform Layer
- [18. Build, export & release](#18-build-export--release): Windows Build and Installer, Web, Mobile and Console Export, CI, Release Pipeline, Game Export
- [19. Docs, tests & code health](#19-docs-tests--code-health): Docs, Tests and Code Health
- [25. Project management (High)](#25-project-management-high): Project Templates and Upgrades
- [28. Version control integration (High)](#28-version-control-integration-high): Version Control
- [32. Accessibility (High)](#32-accessibility-high): Accessibility
- [36. Math & geometry](#36-math--geometry): Math
- [38. Render backends & GPU](#38-render-backends--gpu): Render Backends, Shaders
- [39. Security & anti-cheat](#39-security--anti-cheat): Network Security, Game File Safety
- [40. OkayUI standalone library](#40-okayui-standalone-library): OkayUI
- [41. Build, tools & infrastructure](#41-build-tools--infrastructure): Build System, Online Backend, Own-Stack Switch, Collaboration Tools, Duplicate Guard
- [42. Examples, samples & sandbox](#42-examples-samples--sandbox): Examples, Game Player
- [44. Level design and blockout tools](#44-level-design-and-blockout-tools): Level Design Tools
- [45. Open-world streaming and world partition](#45-open-world-streaming-and-world-partition): World Streaming
- [46. Replay system](#46-replay-system): Replays
- [47. Mod support](#47-mod-support): Mods
- [49. Cloud services](#49-cloud-services): Cloud Services
- [50. Capture and media](#50-capture-and-media): Screenshots and Video
- [54. Debug draw, gizmos, dev console and cheats](#54-debug-draw-gizmos-dev-console-and-cheats): Debug Draw and Dev Console
- [58. Performance budgets and CI perf tests](#58-performance-budgets-and-ci-perf-tests): Performance Checks
- [Duplicates found](#duplicates-found)

## 1. Core & scene

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Game Loop and Time** | Runs the game frame by frame and keeps the clock: frame time, time speed, pause and quit. | Works | `engine/include/okay/Core/` | [1.1](feature_map/01-core-scene.md#11-loop-and-time) |
| **Timers and Tweens** | Does something after a delay or every few seconds, and slides a value smoothly from one number to another. | Works | `engine/include/okay/Core/`, `engine/include/okay/Math/Easing.hpp`, `engine/include/okay/Math/Spring.hpp`, `engine/include/okay/Components/TweenAnimation.hpp` | [1.2](feature_map/01-core-scene.md#12-timers-and-tweens) |
| **Scenes and Objects** | Holds every object in a level with its position, parent and parts, runs their start and update steps, and checks a level for mistakes. | Works | `engine/include/okay/Scene/`, `editor/`, `engine/include/okay/Components/`, `engine/include/okay/Core/AssetDatabase.hpp`, `engine/include/okay/Testing/SceneValidator.hpp` (+1 more) | [1.3](feature_map/01-core-scene.md#13-scene-graph), [1.4](feature_map/01-core-scene.md#14-scene-lifecycle), [1.17](feature_map/01-core-scene.md#117-scene-hierarchy-search-and-validation), [33.2](feature_map/20-35-planned.md#332-scene-validation) |
| **Scene Files** | Writes a level to a readable text file and reads it back, and copies or merges objects. | Works | `engine/include/okay/Scene/`, `engine/src/Scene/SceneTextDetail.cpp`, `engine/src/Scene/SceneTextDetail.hpp` | [1.5](feature_map/01-core-scene.md#15-scene-serializer) |
| **Prefabs** | Saves an object as a reusable file you can place many times; changing the file changes every copy. | 3 Partial, 2 Works | `engine/include/okay/Scene/`, `editor/TemplateTable.hpp`, `editor/main.cpp` | [1.6](feature_map/01-core-scene.md#16-prefabs-and-templates), [21.1](feature_map/20-35-planned.md#211-reusable-objects), [21.2](feature_map/20-35-planned.md#212-nested-prefabs), [21.3](feature_map/20-35-planned.md#213-variants), [21.4](feature_map/20-35-planned.md#214-overrides) |
| **Save Games** | Saves a player's progress in named slots (with checkpoints, versions and checksums) and remembers settings between runs. | 4 Works, 1 Partial | `engine/include/okay/Core/`, `engine/include/okay/Scene/SceneSerializer.hpp`, `engine/include/okay/Scene/UITemplates.hpp`, `engine/src/Core/SaveFrameworkScene.cpp`, `engine/src/Core/SaveFrameworkSync.cpp` (+4 more) | [1.7](feature_map/01-core-scene.md#17-saves-and-prefs), [35.1](feature_map/20-35-planned.md#351-save-slots), [35.2](feature_map/20-35-planned.md#352-checkpoints), [35.3](feature_map/20-35-planned.md#353-serialization), [35.4](feature_map/20-35-planned.md#354-versioned-save-data) |
| **Whole-Scene Save** | The older save slots that capture the whole scene into a `.okaygame` file | Works (duplicate, see [below](#duplicates-found)) | `engine/include/okay/Core/SaveGame.hpp`, `engine/include/okay/Components/AutoSave.hpp` | [1.7](feature_map/01-core-scene.md#17-saves-and-prefs) |
| **Localization** | Shows the game's text in the player's language, with tools to edit, import and preview translations. | 3 Partial, 1 Works | `engine/include/okay/Core/`, `editor/LocalizationWindow.cpp`, `editor/LocalizationWindow.hpp`, `engine/include/okay/Scene/SceneLocalization.hpp` | [1.8](feature_map/01-core-scene.md#18-localization), [1.16](feature_map/01-core-scene.md#116-localization-tools), [31.1](feature_map/20-35-planned.md#311-translation-tables), [31.3](feature_map/20-35-planned.md#313-language-previews) |
| **ECS World** | A second, data-only object store for very many simple things, mirrored into the scene and the network | Works (duplicate, see [below](#duplicates-found)) | `engine/include/okay/ECS/World.hpp`, `engine/include/okay/ECS/Systems.hpp`, `engine/include/okay/ECS/SceneBridge.hpp`, `engine/include/okay/ECS/NetWorld.hpp`, `engine/include/okay/ECS/NetWorldSync.hpp` | [1.9](feature_map/01-core-scene.md#19-events-and-ecs) |
| **Logging** | Writes messages for developers (info, warnings, errors) to log files and shows them in the editor Console and diagnostics panels. | Works | `editor/`, `engine/include/okay/Debug/`, `engine/include/okay/Core/Log.hpp` | [1.10](feature_map/01-core-scene.md#110-logging-and-profiling), [15.11](feature_map/15-editor.md#1511-console-and-diagnostics-panels), [23.4](feature_map/20-35-planned.md#234-error-console) |
| **Profiler** | Measures where each frame's time and memory go, on the CPU and the GPU, so slow parts can be found. | 4 Partial, 1 Works | `engine/include/okay/Debug/`, `engine/include/okay/Core/AssetManager.hpp`, `engine/include/okay/Core/Profiler.hpp`, `editor/main.cpp`, `engine/include/okay/Physics/PhysicsDebug.hpp` (+1 more) | [1.10](feature_map/01-core-scene.md#110-logging-and-profiling), [23.1](feature_map/20-35-planned.md#231-cpugpu-profiler), [23.2](feature_map/20-35-planned.md#232-memory-tracking), [23.3](feature_map/20-35-planned.md#233-frame-debugger), [38.11](feature_map/38-render-backends.md#3811-gpu-timing-and-frame-debugging) |
| **Asset Manager** | Loads each file from disk once, shares it, loads in the background and reloads it when the file changes. | Partial | `engine/include/okay/Core/AssetManager.hpp`, `editor/main.cpp`, `engine/include/okay/Components/ScriptComponent.hpp`, `engine/include/okay/Core/AssetDatabase.hpp` | [1.11](feature_map/01-core-scene.md#111-asset-manager), [34.3](feature_map/20-35-planned.md#343-hot-reload) |
| **Asset Database** | Gives every asset a permanent ID, remembers its import settings and what uses what, and makes its thumbnail. | 5 Partial, 1 Works | `engine/include/okay/Core/`, `engine/include/okay/Render/ModelImport.hpp`, `engine/include/okay/Render/TextureCacheFiles.hpp`, `engine/src/Core/AssetDatabaseDeps.cpp`, `engine/src/Core/AssetDatabaseSearch.cpp` (+2 more) | [1.12](feature_map/01-core-scene.md#112-asset-database), [1.20](feature_map/01-core-scene.md#120-asset-thumbnails), [20.2](feature_map/20-35-planned.md#202-dependencies-and-references), [20.3](feature_map/20-35-planned.md#203-guids), [20.4](feature_map/20-35-planned.md#204-import-settings), [20.5](feature_map/20-35-planned.md#205-thumbnails) |
| **Data Assets** | Small data files for game content (stats, tables), packed and lightly scrambled in shipped games. | 1 Works, 1 Basic | `engine/include/okay/Core/DataAsset.hpp`, `engine/include/okay/Core/DataPack.hpp`, `editor/main.cpp` | [1.14](feature_map/01-core-scene.md#114-data-assets-and-data-packs), [39.10](feature_map/39-security-anticheat.md#3910-data-packs-and-obfuscation) |
| **Display Settings** | The window mode, monitor, resolution, refresh rate, vsync and quality preset a player can choose. | Works | `engine/include/okay/Core/`, `shared/SdlDisplays.cpp`, `shared/SdlDisplays.hpp`, `editor/main.cpp`, `launcher/main.cpp` (+1 more) | [1.15](feature_map/01-core-scene.md#115-display-settings), [37.1](feature_map/37-platform-windowing.md#371-window-fullscreen-and-display-settings) |
| **Core Utilities** | Small shared helpers used everywhere: command line, console variables, paths, jobs on several threads, events, object pools and IDs. | Works | `engine/include/okay/Core/`, `engine/include/okay/Scene/ObjectPool.hpp`, `engine/src/Core/CoreThreads.hpp` | [1.18](feature_map/01-core-scene.md#118-core-runtime-utilities), [1.9](feature_map/01-core-scene.md#19-events-and-ecs), [1.13](feature_map/01-core-scene.md#113-object-pooling) |
| **Property Registry** | Lets tools read, change and watch any component setting by its name; undo, live editing and saving use it. | Partial | `engine/include/okay/Scene/`, `editor/`, `engine/include/okay/Components/FirstPersonHands.hpp`, `engine/src/Scene/BuiltinProperties.cpp` | [1.19](feature_map/01-core-scene.md#119-property-reflection), [34.1](feature_map/20-35-planned.md#341-properties-during-play), [34.2](feature_map/20-35-planned.md#342-live-object-inspector) |

## 2. Rendering & world

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Renderer** | Picks the drawing backend (OpenGL, Direct3D, software) and gets each finished frame onto the screen. | Works | `engine/include/okay/Render/`, `editor/main.cpp`, `player/main.cpp` | [2.1](feature_map/02-rendering-world.md#21-renderers), [38.1](feature_map/38-render-backends.md#381-renderer-interface-and-backend-selection), [38.10](feature_map/38-render-backends.md#3810-frame-and-present-path) |
| **Materials and Textures** | How a surface looks: colour, roughness and shine, the pictures on it, and texture mipmaps, atlases and compression. | 3 Partial, 1 Works | `engine/include/okay/Render/`, `engine/include/okay/Texture/`, `editor/ProjectPanelUI.cpp`, `editor/main.cpp`, `engine/include/okay/Components/MeshRenderer.hpp` (+2 more) | [2.2](feature_map/02-rendering-world.md#22-materials-and-textures), [2.13](feature_map/02-rendering-world.md#213-texture-library-mipmaps-atlases-compression-streaming), [26.2](feature_map/20-35-planned.md#262-material-editor), [26.4](feature_map/20-35-planned.md#264-pbr) |
| **Lighting and Shadows** | Sun, point, spot and area lights, the shadows they cast, and baked lighting. | 1 Works, 1 Planned | `engine/include/okay/Render/`, `engine/include/okay/Components/` | [2.3](feature_map/02-rendering-world.md#23-lighting-and-shadows), [43.4](feature_map/43-proposals.md#434-2d-lighting) |
| **Sky, Weather and Time of Day** | The sky, sun, stars, clouds and fog, rain and snow, and a day-night cycle that games can react to. | 2 Partial, 1 Works, 1 Basic | `engine/include/okay/Components/`, `engine/include/okay/Environment/`, `engine/include/okay/Render/Atmosphere.hpp`, `engine/include/okay/Render/SkyStars.hpp`, `editor/main.cpp` (+2 more) | [2.4](feature_map/02-rendering-world.md#24-sky-fog-and-weather), [53.1](feature_map/43-proposals.md#531-time-of-day-events-and-gameplay), [53.2](feature_map/43-proposals.md#532-weather-gameplay-effects), [53.3](feature_map/43-proposals.md#533-time-and-weather-authoring) |
| **Post-Processing** | Screen effects on the finished picture: bloom, colour grading, depth of field, anti-aliasing and more. | Works | `engine/include/okay/Render/Atmosphere.hpp`, `engine/include/okay/Render/SoftwareRenderer.hpp`, `engine/include/okay/Scene/Scene.hpp`, `engine/include/okay/Core/DisplaySettings.hpp` | [2.5](feature_map/02-rendering-world.md#25-post-processing) |
| **Terrain and Water** | Landscapes you can sculpt, paint and dig, plants and decals on them, and animated water. | 4 Works, 1 Partial, 1 Basic | `engine/include/okay/Components/`, `editor/`, `engine/include/okay/Core/Random.hpp`, `engine/include/okay/Core/Rng.hpp`, `engine/include/okay/Environment/WaterShading.hpp` (+4 more) | [2.6](feature_map/02-rendering-world.md#26-terrain-and-water), [29.1](feature_map/20-35-planned.md#291-terrain-sculpting), [29.2](feature_map/20-35-planned.md#292-vegetation-painting), [29.3](feature_map/20-35-planned.md#293-procedural-generation), [29.4](feature_map/20-35-planned.md#294-decals-and-cables), [52.3](feature_map/43-proposals.md#523-fluids) |
| **Particles** | Smoke, fire, sparks and trails made of many small moving pictures. | Works | `engine/include/okay/Components/ParticleSystem.hpp`, `engine/include/okay/Components/LineRenderer.hpp` | [2.7](feature_map/02-rendering-world.md#27-particles-and-lines) |
| **Culling and LOD** | Skips drawing what the camera cannot see and draws simpler versions of far-away things, so big scenes stay fast. | 4 Partial, 1 Works, 1 Planned | `engine/include/okay/Optimize/`, `engine/include/okay/Render/`, `engine/include/okay/Components/LODGroup.hpp`, `engine/include/okay/Components/WorldStreamer.hpp`, `engine/include/okay/Scene/GameObject.hpp` (+4 more) | [2.8](feature_map/02-rendering-world.md#28-culling-and-skinning), [27.1](feature_map/20-35-planned.md#271-lod-generation), [27.2](feature_map/20-35-planned.md#272-occlusion-culling), [27.3](feature_map/20-35-planned.md#273-batching), [27.4](feature_map/20-35-planned.md#274-streaming), [45.4](feature_map/43-proposals.md#454-distant-scenery) |
| **Fonts and Images** | Loads pictures and fonts and turns text into letters on screen, falling back to system fonts for other alphabets. | 2 Works, 1 Partial | `engine/include/okay/Graphics/`, `engine/include/okay/Core/`, `editor/LocalizationWindow.cpp`, `editor/LocalizationWindow.hpp`, `engine/include/okay/Components/TextRenderer.hpp` (+1 more) | [2.9](feature_map/02-rendering-world.md#29-fonts-and-images), [31.2](feature_map/20-35-planned.md#312-font-fallback), [37.4](feature_map/37-platform-windowing.md#374-system-fonts) |
| **Cameras** | The cameras that film the scene: lens, shake and zoom, follow cameras for first and third person, and shot cameras that blend between views. | Works | `engine/include/okay/Components/`, `editor/FpHandsWindow.cpp`, `editor/FpHandsWindow.hpp`, `engine/include/okay/Scene/EnsureCamera.hpp` | [2.10](feature_map/02-rendering-world.md#210-cameras-and-the-camera-stack), [4.6](feature_map/04-characters-cameras.md#46-third-person-camera), [4.7](feature_map/04-characters-cameras.md#47-first-person-camera-and-hands), [4.8](feature_map/04-characters-cameras.md#48-virtual-cameras-and-dollies), [4.10](feature_map/04-characters-cameras.md#410-camera-component-shake-and-zoom) |
| **Splines** | Curves you draw in the scene that objects follow and that meshes, lines and rows of objects are built along. | Works | `engine/include/okay/Components/Spline.hpp`, `engine/include/okay/Components/SplineMesh.hpp`, `editor/SplineTools.cpp`, `editor/SplineTools.hpp` | [2.11](feature_map/02-rendering-world.md#211-splines-and-spline-tools) |
| **Sprites and Tilemaps** | Flat 2D pictures in the world, sprite sheets cut into frames, flip-book animation and levels painted from tiles. | 2 Works, 2 Basic, 1 Partial | `engine/include/okay/Components/`, `editor/main.cpp`, `engine/include/okay/Scripting/OkayScriptVM.hpp`, `player/main.cpp` | [2.12](feature_map/02-rendering-world.md#212-sprites-and-sprite-animation), [5.10](feature_map/05-animation-ik.md#510-sprite-animation), [43.1](feature_map/43-proposals.md#431-tilemaps-and-tilesets), [43.2](feature_map/43-proposals.md#432-sprite-atlas-and-slicing), [43.3](feature_map/43-proposals.md#433-sprite-animation-tools) |

## 3. 3D modeling & import

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Modeling** | Builds and edits 3D models inside the editor: shapes, cuts, bevels, modifiers, UVs, sculpting and painting. | 7 Works, 2 Partial | `editor/`, `engine/include/okay/Modeling/`, `engine/include/okay/Components/MeshModifiers.hpp`, `engine/include/okay/Physics/ColliderFit.hpp`, `engine/include/okay/Render/Mesh.hpp` (+1 more) | [3.1](feature_map/03-modeling-import.md#31-edit-mode), [3.2](feature_map/03-modeling-import.md#32-edge-and-loop-tools), [3.3](feature_map/03-modeling-import.md#33-modifiers), [3.4](feature_map/03-modeling-import.md#34-uv-tools), [3.8](feature_map/03-modeling-import.md#38-sculpt-and-paint-brushes), [3.9](feature_map/03-modeling-import.md#39-generate-and-whole-mesh-operations), [3.10](feature_map/03-modeling-import.md#310-add-library-and-procedural-props), [3.11](feature_map/03-modeling-import.md#311-guided-creation-templates-parts-safe-regeneration), [3.12](feature_map/03-modeling-import.md#312-simple-view-of-the-modeling-window-beginner-mode) |
| **Meshes** | The 3D shape data that drawing and physics share, the built-in shapes, and the `.okaymesh` file. | Partial | `engine/include/okay/Render/Mesh.hpp`, `engine/include/okay/Modeling/MeshAsset.hpp`, `engine/include/okay/Modeling/PolyMesh.hpp`, `editor/EditorState.cpp`, `editor/EditorState.hpp` | [3.5](feature_map/03-modeling-import.md#35-mesh-assets-and-primitives) |
| **Model Import** | Reads 3D model files from other programs (FBX, glTF, OBJ and more) with their materials, bones and animations. | Partial | `engine/src/Render/`, `engine/include/okay/Render/`, `scripts/`, `editor/MainShared.hpp`, `editor/main.cpp` (+1 more) | [3.6](feature_map/03-modeling-import.md#36-model-import) |
| **Rigging** | Adds a skeleton to a model and sets it up so it can be animated and played as a character. | 1 Works, 1 Partial | `engine/include/okay/Components/Character.hpp`, `engine/include/okay/Components/HumanoidRetarget.hpp`, `editor/main.cpp`, `engine/include/okay/Animation/HumanoidBones.hpp`, `engine/include/okay/Physics/ColliderFit.hpp` (+2 more) | [3.7](feature_map/03-modeling-import.md#37-rigging-and-playable-setup), [30.2](feature_map/20-35-planned.md#302-rig-generation) |

## 4. Characters & cameras

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Player Controller** | Makes a first- or third-person character that walks, runs, jumps, crouches and swims, ready to drop into a scene. | Works | `engine/include/okay/Components/`, `editor/main.cpp`, `engine/include/okay/Physics/PlayerCollision.hpp` | [4.1](feature_map/04-characters-cameras.md#41-player-controller), [4.2](feature_map/04-characters-cameras.md#42-stances-and-movement), [4.12](feature_map/04-characters-cameras.md#412-premade-players) |
| **Ready-Made Controllers** | Ready-made movement for 2D top-down and platformer games, third-person shooters, twin-stick and click-to-move characters. | Works | `engine/include/okay/Components/` | [4.3](feature_map/04-characters-cameras.md#43-2d-and-25d-controllers), [4.4](feature_map/04-characters-cameras.md#44-shooter-and-top-down) |
| **Vehicles** | Drivable cars and boats: throttle, steering, brakes, grip and suspension. | Works | `engine/include/okay/Components/VehicleController.hpp`, `engine/include/okay/Components/VehicleController2D.hpp`, `engine/include/okay/Components/VehicleSeat.hpp`, `engine/include/okay/Components/BoatController.hpp`, `engine/include/okay/Components/Buoyancy.hpp` | [4.5](feature_map/04-characters-cameras.md#45-vehicles) |
| **Character Creator** | The built-in blocky character and its body, colours, hair, clothes and accessories. | 3 Partial, 1 Works | `engine/include/okay/Components/`, `editor/main.cpp` | [4.9](feature_map/04-characters-cameras.md#49-blocky-character-and-customization), [30.1](feature_map/20-35-planned.md#301-customization), [30.3](feature_map/20-35-planned.md#303-clothing), [30.4](feature_map/20-35-planned.md#304-morph-targets) |
| **Seats** | Places a character can sit down on and get up from with the Interact key. | Works | `engine/include/okay/Components/Seat.hpp`, `engine/include/okay/Animation/HumanoidActions.hpp`, `engine/include/okay/Input/GameInput.hpp` | [4.11](feature_map/04-characters-cameras.md#411-seats-and-sitting) |

## 5. Animation & IK

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Animation** | Plays and blends animation clips with a state machine, shares humanoid animations between models, and gives scripts control. | 8 Works, 1 In flight | `engine/include/okay/Components/`, `editor/`, `engine/include/okay/Animation/`, `engine/include/okay/Render/ModelImport.hpp`, `engine/include/okay/Script/AnimIkTools.hpp` (+1 more) | [5.1](feature_map/05-animation-ik.md#51-animator-and-state-machines), [5.2](feature_map/05-animation-ik.md#52-clips-and-curves), [5.3](feature_map/05-animation-ik.md#53-model-clip-player-and-locomotion), [5.4](feature_map/05-animation-ik.md#54-humanoid-retarget-and-library), [5.5](feature_map/05-animation-ik.md#55-root-motion-and-skinning), [5.11](feature_map/05-animation-ik.md#511-animation-script-control), [5.12](feature_map/05-animation-ik.md#512-built-in-action-clips), [5.13](feature_map/05-animation-ik.md#513-player-animation-setup), [15.5](feature_map/15-editor.md#155-animator-animation-tab-timeline) |
| **Inverse Kinematics** | Bends bones so feet stand on the ground, hands reach and hold things, and heads look at targets. | 3 Works, 1 Partial | `engine/include/okay/Components/`, `engine/include/okay/Math/`, `engine/include/okay/Script/AnimIkTools.hpp` | [5.6](feature_map/05-animation-ik.md#56-foot-limb-and-chain-ik), [5.7](feature_map/05-animation-ik.md#57-look-at-aim-and-full-body-ik), [5.9](feature_map/05-animation-ik.md#59-object-holding), [36.5](feature_map/36-math-geometry.md#365-ik-math) |
| **Ragdolls** | Makes a character go limp when it dies or flinch when it is hit. | Works | `engine/include/okay/Components/Ragdoll.hpp` | [5.8](feature_map/05-animation-ik.md#58-ragdolls-and-hit-reactions) |

## 6. Physics

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Physics** | Moves solid 2D and 3D objects under gravity, makes them collide, bounce and join, and answers "what is here?" (cloth, soft bodies and crowds are planned). | 7 Works, 2 Planned, 1 Basic | `engine/include/okay/Physics/`, `engine/include/okay/Components/`, `engine/include/okay/Scene/Layers.hpp`, `engine/include/okay/Scene/Scene.hpp`, `engine/src/Physics/MeshCollider3D.cpp` (+4 more) | [6.2](feature_map/06-physics.md#62-3d-bodies-and-colliders-collider-fit), [6.1](feature_map/06-physics.md#61-2d-bodies-and-colliders), [6.3](feature_map/06-physics.md#63-joints), [6.4](feature_map/06-physics.md#64-fixed-step-with-render-interpolation-pose-history), [6.5](feature_map/06-physics.md#65-queries-and-debug-draw), [6.7](feature_map/06-physics.md#67-collision-layers-and-trigger-filters), [6.8](feature_map/06-physics.md#68-forces-explosions-and-body-push-out), [52.1](feature_map/43-proposals.md#521-cloth-and-hair), [52.2](feature_map/43-proposals.md#522-soft-bodies), [52.5](feature_map/43-proposals.md#525-crowds) |
| **Destruction and Projectiles** | Breaks blocks and objects into pieces, and flies bullets, rockets and arrows. | 1 Works, 1 Partial | `engine/include/okay/Components/Destructible.hpp`, `engine/include/okay/Components/Projectile3D.hpp`, `editor/main.cpp`, `engine/include/okay/Physics/Physics3D.hpp` | [6.6](feature_map/06-physics.md#66-destructibles-and-projectiles), [52.4](feature_map/43-proposals.md#524-destruction) |

## 7. Audio

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Audio** | Plays sound and music in 2D and 3D, mixes it in groups and saves the player's volume settings. | Works | `engine/include/okay/Audio/`, `engine/include/okay/Components/`, `engine/src/Audio/`, `editor/main.cpp`, `player/main.cpp` | [7.1](feature_map/07-audio.md#71-mixer-and-audio-mixer), [7.2](feature_map/07-audio.md#72-formats-and-clips), [7.3](feature_map/07-audio.md#73-audio-sources-and-music-player), [7.4](feature_map/07-audio.md#74-saved-volume-settings) |

## 8. Input

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Input** | Reads the keyboard, mouse, gamepads and touch, and plays controller rumble. | Works | `engine/include/okay/Input/`, `engine/src/Input/GameInput.cpp`, `engine/src/Input/Input.cpp`, `engine/src/Platform/OwnLinuxEvdev.cpp`, `engine/src/Platform/SdlBackend.cpp` (+3 more) | [8.1](feature_map/08-input.md#81-keyboard-and-mouse), [8.2](feature_map/08-input.md#82-gamepad-sdl), [8.4](feature_map/08-input.md#84-cursor-and-touch), [8.5](feature_map/08-input.md#85-gameplay-input-blocking), [8.6](feature_map/08-input.md#86-rumble-and-haptics) |
| **Input Actions** | Named actions such as Jump and Fire that work with any key or button, can be rebound by the player, and support several local players. | 2 Works, 1 Partial | `engine/src/Input/`, `engine/include/okay/Input/`, `editor/InputDebugger.hpp`, `editor/main.cpp`, `engine/include/okay/Components/UIKeybindField.hpp` (+2 more) | [8.3](feature_map/08-input.md#83-input-actions-and-rebinding-ui), [8.7](feature_map/08-input.md#87-input-upgrade-timing-curves-per-player-actions-recording-the-input-panel), [32.4](feature_map/20-35-planned.md#324-remapping) |
| **Simple Input Actions** | The older text-based action table (`InputActions`) the built-in controllers read | Works (duplicate, see [below](#duplicates-found)) | `engine/include/okay/Input/InputActions.hpp`, `engine/src/Input/InputActions.cpp` | [8.3](feature_map/08-input.md#83-input-actions-and-rebinding-ui) |

## 9. AI & navigation

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Navigation** | Works out where characters can walk and walks them to a goal around walls and each other. | Works | `engine/include/okay/Components/`, `engine/include/okay/AI/`, `engine/src/AI/`, `editor/NavigationWindow.cpp`, `editor/NavigationWindow.hpp` | [9.1](feature_map/09-ai-navigation.md#91-nav-mesh-bake-and-queries), [9.2](feature_map/09-ai-navigation.md#92-nav-agents-and-obstacles), [9.6](feature_map/09-ai-navigation.md#96-nav-areas-and-off-mesh-links) |
| **Grid Pathfinding** | Finds a path over a square grid or tilemap, for 2D and tower-defence games | Works (duplicate, see [below](#duplicates-found)) | `engine/include/okay/AI/Pathfinding.hpp`, `engine/src/AI/GridPath2D.cpp`, `engine/include/okay/Components/GridPathAgent2D.hpp`, `engine/include/okay/Components/GridCostLayer.hpp`, `engine/include/okay/Components/GridObstacle2D.hpp` | [9.3](feature_map/09-ai-navigation.md#93-grid-pathfinding-older) |
| **Behavior Trees** | NPC brains drawn as a tree of choices and tasks. | Works | `engine/include/okay/AI/BehaviorTree.hpp`, `engine/src/AI/BehaviorTreeRunner.cpp`, `engine/include/okay/Components/BehaviorTreeRunner.hpp`, `editor/BehaviorTreeWindow.cpp`, `editor/BehaviorTreeWindow.hpp` | [9.4](feature_map/09-ai-navigation.md#94-behavior-trees) |
| **NPCs** | Ready-made NPCs (animals, enemies, guards, companions) that see, hear and react. | Works | `engine/include/okay/AI/Perception.hpp`, `engine/include/okay/Components/NPCController.hpp`, `engine/include/okay/Components/FollowTarget2D.hpp`, `editor/main.cpp` | [9.5](feature_map/09-ai-navigation.md#95-npcs-and-perception) |

## 10. Story & cinematics

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Cutscenes** | Timelines of camera shots, movement, animation, sound and events that play as a cutscene. | Works | `engine/include/okay/Components/Sequence.hpp`, `engine/include/okay/Components/SequencePlayer.hpp`, `editor/TimelineWindow.cpp`, `editor/TimelineWindow.hpp` | [10.1](feature_map/10-story-cinematics.md#101-cutscene-sequences-and-sequence-player) |
| **Dialogue** | Branching conversations with choices, the dialogue box on screen, and subtitles. | 2 Works, 1 Partial | `engine/include/okay/Components/`, `editor/DialogueWindow.cpp`, `editor/DialogueWindow.hpp`, `engine/include/okay/Core/Subtitles.hpp`, `engine/src/Components/StoryKv.hpp` | [10.2](feature_map/10-story-cinematics.md#102-dialogue-graphs-and-dialogue-runner), [10.4](feature_map/10-story-cinematics.md#104-story-ui-dialogue-box-quest-tracker-and-journal), [32.2](feature_map/20-35-planned.md#322-subtitles) |
| **Quests** | Quests with goals and progress, and story variables that remember what the player did. | Works | `engine/include/okay/Components/QuestLog.hpp`, `engine/include/okay/Components/StoryState.hpp`, `engine/src/Components/StoryKv.hpp`, `editor/DialogueWindow.cpp`, `editor/DialogueWindow.hpp` | [10.3](feature_map/10-story-cinematics.md#103-quests-and-story-variables) |

## 11. Gameplay systems

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Inventory and Items** | Bags, grid inventories, chests, equipment, the item list and loot, also kept by the server in multiplayer. | 2 Works, 1 Basic | `engine/include/okay/Components/`, `engine/include/okay/Inventory/`, `engine/src/Inventory/Equipment.cpp`, `engine/src/Inventory/NetInventory.cpp`, `editor/ContentEditors.cpp` | [11.1](feature_map/11-gameplay-systems.md#111-inventory-bag-grid-chests-equipment), [11.2](feature_map/11-gameplay-systems.md#112-items-and-loot), [11.8](feature_map/11-gameplay-systems.md#118-server-authoritative-inventory) |
| **Crafting** | Recipes that turn items into other items at a workbench. | Works | `engine/include/okay/Inventory/Recipe.hpp`, `engine/include/okay/Components/Crafting.hpp`, `engine/include/okay/Components/CraftingMenu.hpp`, `engine/include/okay/Components/CraftingStation.hpp`, `editor/main.cpp` | [11.3](feature_map/11-gameplay-systems.md#113-crafting-and-recipes) |
| **Health and Survival** | Health, hunger, thirst, stamina, temperature and other stats, damage, and death. | Works | `engine/include/okay/Components/`, `editor/main.cpp` | [11.4](feature_map/11-gameplay-systems.md#114-survival-stats-afflictions-zones) |
| **Building** | Lets players (or you in the editor) place walls, floors and parts that snap together. | Works | `engine/include/okay/Components/`, `editor/main.cpp` | [11.5](feature_map/11-gameplay-systems.md#115-building-build-mode-structure-builder-snapping) |
| **Spawners, Turns and Minimap** | Spawns waves of enemies or loot, keeps turn order, and draws a minimap. | Works | `engine/include/okay/Components/Spawner.hpp`, `engine/include/okay/Components/TurnManager.hpp`, `engine/include/okay/Components/Minimap.hpp`, `editor/main.cpp` | [11.6](feature_map/11-gameplay-systems.md#116-spawners-turn-manager-minimap) |
| **Combat** | Melee swings, dodge rolls and dives. | Works | `engine/include/okay/Components/MeleeAttacker.hpp`, `engine/include/okay/Components/DodgeAction.hpp`, `engine/include/okay/Components/DiveAction.hpp`, `editor/main.cpp` | [11.7](feature_map/11-gameplay-systems.md#117-combat-actions-melee-dodge-dive) |
| **RPG Stats** | A simple character sheet with health, mana, levels and XP | Works (duplicate, see [below](#duplicates-found)) | `engine/include/okay/Components/Stats.hpp`, `editor/main.cpp` | [11.9](feature_map/11-gameplay-systems.md#119-rpg-stats-and-xp) |
| **No-Code Behaviours** | Tiny components that move, spin, delete or drag objects with no script. | Works | `engine/include/okay/Components/Mover.hpp`, `engine/include/okay/Components/Spinner.hpp`, `engine/include/okay/Components/Lifetime.hpp`, `engine/include/okay/Components/Draggable.hpp` | [11.10](feature_map/11-gameplay-systems.md#1110-no-code-behaviours-move-spin-lifetime), [11.11](feature_map/11-gameplay-systems.md#1111-world-drag-and-drop) |
| **Volumes and Zones** | Invisible areas that act when something enters them: checkpoint, kill, push, change the look. | 1 Works, 1 Partial | `engine/include/okay/Components/`, `editor/VolumeTools.cpp`, `editor/VolumeTools.hpp`, `engine/include/okay/Physics/Collider2D.hpp`, `engine/include/okay/Physics/Collider3D.hpp` (+3 more) | [11.12](feature_map/11-gameplay-systems.md#1112-volumes-and-zones), [44.4](feature_map/43-proposals.md#444-volumes-and-zones) |

## 12. In-game UI

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Game UI** | Menus, buttons, health bars and other on-screen interface inside your game, built in the editor or written as markup. | 10 Works, 3 Partial | `engine/include/okay/Components/`, `engine/include/okay/UI/`, `editor/`, `engine/include/okay/Scene/`, `tests/ui_golden_scenes.hpp` (+4 more) | [12.1](feature_map/12-ingame-ui.md#121-canvas-anchors-flex-layout-safe-area), [12.2](feature_map/12-ingame-ui.md#122-widgets-button-image-raw-image-text-panel-progress-rating), [12.3](feature_map/12-ingame-ui.md#123-input-widgets-slider-toggle-input-field-dropdown-stepper-keybind), [12.4](feature_map/12-ingame-ui.md#124-lists-tabs-modals-context-menu-tooltips-scroll-views), [12.5](feature_map/12-ingame-ui.md#125-html-ui), [12.6](feature_map/12-ingame-ui.md#126-world-space-ui), [12.7](feature_map/12-ingame-ui.md#127-themes-and-the-launcher-matching-ui-kit), [12.8](feature_map/12-ingame-ui.md#128-loading-pause-and-settings-screens), [12.9](feature_map/12-ingame-ui.md#129-mobile-scaling), [12.10](feature_map/12-ingame-ui.md#1210-ui-editing-in-the-editor), [12.11](feature_map/12-ingame-ui.md#1211-ui-renderer-painters-and-batching), [12.12](feature_map/12-ingame-ui.md#1212-ui-input-focus-and-navigation), [12.13](feature_map/12-ingame-ui.md#1213-data-binding-ui-animation-and-script-control) |

## 13. Scripting

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **OkayScript** | The default script language: small text files that make objects do things, with its own code editor, debugger and profiler. | 5 Works, 2 Partial, 1 Basic | `engine/include/okay/Scripting/`, `editor/`, `editor/vendor/`, `engine/include/okay/Components/ActionList.hpp`, `engine/include/okay/Components/ScriptComponent.hpp` (+5 more) | [13.1](feature_map/13-scripting.md#131-okayscript-vm), [13.3](feature_map/13-scripting.md#133-removed-lua-and-c-mono-backends), [13.4](feature_map/13-scripting.md#134-script-components-and-script-ui), [13.5](feature_map/13-scripting.md#135-script-editor-autocomplete-errors-rename-format), [13.7](feature_map/13-scripting.md#137-built-in-function-library), [55.1](feature_map/43-proposals.md#551-breakpoints-and-stepping-for-text-scripts), [55.2](feature_map/43-proposals.md#552-watch-and-call-stack), [55.3](feature_map/43-proposals.md#553-script-profiler) |
| **Typed OkayScript** | The newer C#-style typed script language and the table of engine calls it can use | 1 Works, 1 Partial (duplicate, see [below](#duplicates-found)) | `engine/src/Script/`, `engine/include/okay/Script/AnimIkTools.hpp`, `engine/include/okay/Scripting/NextScriptVM.hpp`, `engine/include/okay/VisualScript/VsApi.hpp` | [13.2](feature_map/13-scripting.md#132-c-style-language-core), [13.6](feature_map/13-scripting.md#136-engine-api-and-generated-docs) |

## 14. Visual scripting

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Visual Scripting** | Makes game logic by connecting boxes (nodes) with wires instead of typing code. | Works | `engine/include/okay/VisualScript/`, `editor/`, `engine/include/okay/Components/VisualScriptComponent.hpp`, `engine/src/Script/EngineApi.cpp` | [14.1](feature_map/14-visual-scripting.md#141-graph-editor), [14.2](feature_map/14-visual-scripting.md#142-typed-pins-variables-and-scopes-object-references), [14.3](feature_map/14-visual-scripting.md#143-functions-loops-control-flow), [14.4](feature_map/14-visual-scripting.md#144-node-library-physics-story-build-math), [14.5](feature_map/14-visual-scripting.md#145-debugger-breakpoints-step-call-stack-watch), [14.7](feature_map/14-visual-scripting.md#147-engine-api-nodes), [14.8](feature_map/14-visual-scripting.md#148-converting-between-graphs-and-scripts) |
| **Action Lists** | No-code rules: "when this happens, if this is true, do these steps". | Works | `engine/include/okay/Components/ActionList.hpp`, `engine/include/okay/VisualScript/ActionGraph.hpp`, `engine/include/okay/VisualScript/NodesActions.hpp`, `engine/include/okay/Components/NoCode.hpp`, `editor/main.cpp` | [14.6](feature_map/14-visual-scripting.md#146-action-framework) |

## 15. Editor

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Project Panel** | The editor's file browser for the project's assets. | 1 Works, 1 Partial | `editor/`, `engine/include/okay/Core/AssetDatabase.hpp`, `engine/src/Core/AssetDatabaseSearch.cpp`, `tools/project_menu_shots.cpp` | [15.4](feature_map/15-editor.md#154-project-panel-and-asset-manager), [20.1](feature_map/20-35-planned.md#201-asset-browser) |
| **Build Settings** | The editor's Build Settings and Scene Manager: which scenes go into a game and where it is exported to. | Works | `editor/main.cpp`, `editor/SceneManagerPanel.cpp`, `editor/SceneManagerPanel.hpp` | [15.7](feature_map/15-editor.md#157-build-settings), [15.12](feature_map/15-editor.md#1512-scene-manager-panel) |
| **Editor Plugins** | Lets people add their own panels, menu commands and nodes to the editor. | 2 Works, 2 Planned, 1 Partial | `engine/include/okay/VisualScript/`, `engine/include/okay/Scripting/OkayScriptVM.hpp`, `engine/include/okay/Scripting/ScriptUI.hpp`, `examples/editor-plugins/AlignTools.okay`, `examples/editor-plugins/CleanupNames.okay` (+5 more) | [15.8](feature_map/15-editor.md#158-plugin-system-and-plugins-menu), [24.1](feature_map/20-35-planned.md#241-c-plugins), [24.2](feature_map/20-35-planned.md#242-editor-extensions), [24.3](feature_map/20-35-planned.md#243-custom-nodes), [24.4](feature_map/20-35-planned.md#244-plugin-sdk) |
| **Undo and Recovery** | Undo and redo, autosave, crash recovery, backups and saved window layouts in the editor. | 4 Partial, 1 Works | `editor/`, `engine/include/okay/Core/`, `shared/`, `engine/include/okay/Components/AutoSave.hpp`, `engine/include/okay/Scene/SceneSerializer.hpp` (+1 more) | [15.9](feature_map/15-editor.md#159-layout-undo-autosave), [22.1](feature_map/20-35-planned.md#221-undo-history), [22.2](feature_map/20-35-planned.md#222-autosave), [22.3](feature_map/20-35-planned.md#223-crash-recovery), [22.4](feature_map/20-35-planned.md#224-backups) |
| **Editor** | The editor program: Hierarchy, Inspector, Scene and Game views, menus, content windows, settings and help. | 7 Works, 1 Partial | `editor/`, `scripts/check_pure_move.py` | [15.15](feature_map/15-editor.md#1515-editor-shell-startup-and-lifecycle), [15.1](feature_map/15-editor.md#151-hierarchy), [15.2](feature_map/15-editor.md#152-inspector-add-component-menu-character-model-panel), [15.3](feature_map/15-editor.md#153-scene-and-game-views-gizmos-spline-tools), [15.6](feature_map/15-editor.md#156-navigation-behavior-tree-dialogue-localization-modeling-windows), [15.10](feature_map/15-editor.md#1510-splitting-editormaincpp-into-panel-files-45k-lines), [15.13](feature_map/15-editor.md#1513-content-editors-and-tool-windows), [15.14](feature_map/15-editor.md#1514-scene-settings-project-settings-and-help-windows) |

## 16. Networking & multiplayer

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Networking** | Sends game state between players over the internet: packets, sync, prediction, remote calls and networked players. | 5 Works, 2 Basic | `engine/include/okay/Net/`, `engine/src/Crypto/`, `engine/include/okay/Components/`, `editor/NetSimPanel.cpp`, `editor/NetSimPanel.hpp` (+4 more) | [16.1](feature_map/16-networking.md#161-transport-sealed-channels-packets), [16.2](feature_map/16-networking.md#162-server-authority-prediction-lag-compensation), [16.3](feature_map/16-networking.md#163-snapshots-and-sync-interest), [16.8](feature_map/16-networking.md#168-network-player-spawning-and-avatar-motion), [16.10](feature_map/16-networking.md#1610-provider-seams-photon-transport-and-online-services-backend), [16.11](feature_map/16-networking.md#1611-rpc-syncvars-spawning-and-chat), [16.13](feature_map/16-networking.md#1613-network-simulator-and-traffic-panel) |
| **Matchmaking and Lobbies** | Finding and joining games, rooms and lobbies, match flow, results and leaderboards. | Works | `engine/include/okay/Net/`, `docs/sql/matches.sql`, `player/main.cpp` | [16.4](feature_map/16-networking.md#164-matchmaking-queues-server-browser-join-links), [16.9](feature_map/16-networking.md#169-match-results-stats-and-leaderboards), [16.12](feature_map/16-networking.md#1612-rooms-lobby-and-match-flow) |
| **Dedicated Server** | Runs a game with no window as the server everyone connects to, and exports it. | Works | `editor/BuildGame.cpp`, `editor/main.cpp`, `.github/workflows/release.yml`, `engine/include/okay/Core/TickPacer.hpp`, `engine/include/okay/Net/DedicatedServer.hpp` (+1 more) | [16.5](feature_map/16-networking.md#165-dedicated-server-export-relay), [18.7](feature_map/18-build-export-release.md#187-dedicated-server-export-and-linux-server-build) |
| **Server Anti-Cheat** | The server checks what players send, spots cheaters, and handles reports and bans. | 4 Works, 2 Partial | `engine/include/okay/Net/`, `shared/`, `engine/include/okay/Components/AntiCheatConfig.hpp`, `engine/include/okay/Components/NetValidator.hpp`, `tests/sql/supabase_stub.sql` (+7 more) | [16.6](feature_map/16-networking.md#166-anti-cheat-server-side), [39.1](feature_map/39-security-anticheat.md#391-server-authority-checks-and-gameplay-validation), [39.5](feature_map/39-security-anticheat.md#395-runtime-checks-and-the-clock-heartbeat), [39.6](feature_map/39-security-anticheat.md#396-reports-bans-appeals-and-the-hardware-hash), [39.12](feature_map/39-security-anticheat.md#3912-detection-engine-behavioural-detectors-risk-score-evidence-export), [39.13](feature_map/39-security-anticheat.md#3913-cheat-simulator-and-hardening-audit) |
| **Voice Chat** | Voice calls between friends, groups and server voice rooms. | Partial | `launcher/VoiceChat.hpp`, `docs/sql/app_config.sql` | [16.7](feature_map/16-networking.md#167-voice-chat-server) |
| **Relay** | A small server that connects players behind home routers to each other. | 2 Works, 1 Basic | `deploy/voice-server/`, `engine/include/okay/Net/RelayProtocol.hpp`, `engine/include/okay/Net/RelayServer.hpp`, `relay/CMakeLists.txt`, `relay/main.cpp` | [16.14](feature_map/16-networking.md#1614-relay-server-nat-traversal), [39.8](feature_map/39-security-anticheat.md#398-relay-hardening), [41.5](feature_map/41-build-tools-infra.md#415-server-deployment-kits) |

## 17. Launcher & platform

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Launcher** | The OkaySpace app players open first: projects, the games library with the official game, the multiplayer page and settings. | Works | `shared/`, `launcher/`, `docs/sql/official_game_admin.sql`, `engine/include/okay/Platform/GameIpc.hpp`, `scripts/publish-official-game.sh` | [17.1](feature_map/17-launcher-platform.md#171-launcher-shell-and-updater), [17.12](feature_map/17-launcher-platform.md#1712-create-page-projects-templates-and-git), [17.13](feature_map/17-launcher-platform.md#1713-play-page-library-web-games-collections-playtime), [17.14](feature_map/17-launcher-platform.md#1714-official-game-install-update-publish), [17.15](feature_map/17-launcher-platform.md#1715-multiplayer-page-and-game-link) |
| **Launcher Updater** | Checks for new versions of the launcher, editor and player, downloads them and checks their checksums. | Works | `launcher/`, `shared/`, `.github/workflows/release.yml`, `docs/sql/official_game_admin.sql`, `editor/main.cpp` (+1 more) | [17.1](feature_map/17-launcher-platform.md#171-launcher-shell-and-updater), [18.5](feature_map/18-build-export-release.md#185-update-feed) |
| **Accounts** | Sign-up, sign-in and the clients that talk to the online services. | 2 Works, 1 Partial, 1 Basic | `engine/include/okay/Platform/`, `shared/`, `docs/sql/admin.sql`, `docs/sql/social.sql`, `engine/include/okay/Platform/Account/Account.hpp` (+5 more) | [17.2](feature_map/17-launcher-platform.md#172-accounts-and-sign-in), [37.9](feature_map/37-platform-windowing.md#379-online-service-clients-shared-with-area-17), [39.11](feature_map/39-security-anticheat.md#3911-account-backend-and-updater-hardening), [42.4](feature_map/42-examples-samples.md#424-account-server-example) |
| **Community** | Friends, parties, chat and calls, forums, Discord-style community servers and the staff tools that moderate them. | Works | `docs/sql/`, `engine/include/okay/Platform/`, `shared/MemberModeration.hpp`, `shared/UserReports.hpp` | [17.3](feature_map/17-launcher-platform.md#173-friends-following-parties), [17.4](feature_map/17-launcher-platform.md#174-chat-dms-groups-voice-notes-calls-polls-gifs-emoji), [17.5](feature_map/17-launcher-platform.md#175-forums), [17.8](feature_map/17-launcher-platform.md#178-servers-page-discord-style-community-servers), [17.9](feature_map/17-launcher-platform.md#179-admin) |
| **Marketplace and Workshop** | Browse, share and download games, scenes and templates made by others. | Works | `engine/include/okay/Platform/Workshop.hpp` | [17.6](feature_map/17-launcher-platform.md#176-marketplace-and-workshop) |
| **Profiles and Achievements** | Public profiles, badges and achievements. | 1 Works, 1 Partial | `engine/include/okay/Platform/Achievements.hpp`, `engine/include/okay/Platform/Profiles.hpp`, `docs/sql/profile_showcase.sql`, `docs/sql/profile_extras.sql`, `engine/include/okay/Platform/Steam/SteamService.hpp` | [17.7](feature_map/17-launcher-platform.md#177-profiles-badges-achievements), [49.3](feature_map/43-proposals.md#493-achievements-sync) |
| **Steam Integration** | Steam achievements, stats, cloud files, workshop and uploading OkaySpace to Steam (simulated without the Steam SDK). | 2 Works, 2 Basic | `steam/`, `engine/src/Platform/Steam/`, `engine/include/okay/Platform/Steam/Steam.hpp`, `engine/include/okay/Platform/Steam/SteamService.hpp`, `engine/include/okay/Components/SteamManager.hpp` (+2 more) | [17.10](feature_map/17-launcher-platform.md#1710-steam-and-playfab), [18.8](feature_map/18-build-export-release.md#188-steam-release-steampipe), [37.7](feature_map/37-platform-windowing.md#377-steam-backend), [41.7](feature_map/41-build-tools-infra.md#417-steam-upload-kit) |
| **Crash Reports and Telemetry** | Keeps a report when a program crashes, asks before sending anything, and (planned) counts usage. | 4 Partial, 1 Planned | `shared/`, `docs/sql/`, `.github/workflows/crash-issues.yml`, `editor/main.cpp`, `engine/include/okay/Components/SettingsScreen.hpp` (+6 more) | [17.16](feature_map/17-launcher-platform.md#1716-crash-reports-launcher-side), [48.1](feature_map/43-proposals.md#481-crash-reports), [48.2](feature_map/43-proposals.md#482-opt-in-analytics-events), [48.3](feature_map/43-proposals.md#483-consent-and-privacy), [48.4](feature_map/43-proposals.md#484-collection-backend-and-dashboards) |
| **Platform Layer** | Our own layer between the engine and the operating system: windows, cursor, clipboard, file dialogs, folders and HTTP. | 4 Works, 1 Partial, 1 Basic | `engine/src/Platform/`, `engine/include/okay/Platform/`, `shared/`, `docs/platform/web_mobile.md`, `editor/main.cpp` (+7 more) | [17.17](feature_map/17-launcher-platform.md#1717-platform-layer-okayplatform-and-the-own-backend), [37.2](feature_map/37-platform-windowing.md#372-focus-cursor-and-clipboard), [37.3](feature_map/37-platform-windowing.md#373-file-dialogs), [37.5](feature_map/37-platform-windowing.md#375-web-and-mobile-shims), [37.6](feature_map/37-platform-windowing.md#376-native-http-and-the-game-to-launcher-link), [37.8](feature_map/37-platform-windowing.md#378-per-user-folders) |

## 18. Build, export & release

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Windows Build and Installer** | Builds the Windows programs (launcher, editor, player, relay) and the installer and zip. | 1 Works, 1 Partial | `scripts/`, `CMakeLists.txt`, `cmake/mingw-w64-x86_64.toolchain.cmake`, `editor/main.cpp`, `steam/upload.sh` | [18.1](feature_map/18-build-export-release.md#181-windows-build-and-installer), [56.1](feature_map/43-proposals.md#561-desktop-packaging) |
| **Web, Mobile and Console Export** | Runs games in a web browser, on phones, and (experimental, off by default) on an Xbox in Developer Mode. | 3 Basic, 2 Partial, 1 Planned | `scripts/`, `editor/BuildGame.cpp`, `editor/main.cpp`, `engine/include/okay/Components/UISafeArea.hpp`, `engine/include/okay/Components/UITouchScroll.hpp` (+4 more) | [18.2](feature_map/18-build-export-release.md#182-web-and-mobile-export), [18.10](feature_map/18-build-export-release.md#1810-xbox-one-export-experimental-developer-mode-uwp), [38.7](feature_map/38-render-backends.md#387-web-webgl-path), [56.2](feature_map/43-proposals.md#562-mobile-packaging), [56.3](feature_map/43-proposals.md#563-web-export), [56.4](feature_map/43-proposals.md#564-console-platforms) |
| **CI** | The automatic checks GitHub runs on every change (build, tests, linters). | 2 Works, 1 Planned | `.github/workflows/ci.yml`, `.github/PULL_REQUEST_TEMPLATE.md`, `tests/export/verify.sh`, `tests/export/verify.ps1`, `.github/workflows/release.yml` | [18.3](feature_map/18-build-export-release.md#183-ci-jobs), [41.8](feature_map/41-build-tools-infra.md#418-github-workflows-and-templates), [58.3](feature_map/43-proposals.md#583-performance-regression-guard-in-ci) |
| **Release Pipeline** | Turns a version number into a published release, and publishes test builds. | 2 Works, 1 Basic | `launcher/`, `CMakeLists.txt`, `ROADMAP.md`, `.github/workflows/dev-build.yml`, `.github/workflows/release.yml` (+7 more) | [18.4](feature_map/18-build-export-release.md#184-release-workflow), [18.9](feature_map/18-build-export-release.md#189-test-builds-the-dev-channel), [41.9](feature_map/41-build-tools-infra.md#419-versioning-and-release-prep) |
| **Game Export** | Turns a project into a game folder you can ship (`<Game>.exe`), checks it, and lets shipped games download signed content updates. | 1 Works, 1 Basic | `shared/`, `editor/`, `tests/export/`, `engine/include/okay/Core/BuildConfigExtra.hpp`, `engine/include/okay/Core/DataPack.hpp` (+5 more) | [18.6](feature_map/18-build-export-release.md#186-desktop-game-export-build-game-and-the-export-check), [18.11](feature_map/18-build-export-release.md#1811-downloadable-game-content-content-delivery) |

## 19. Docs, tests & code health

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Docs** | The written guides, the roadmap, the generated API reference, and in-editor tutorials, tips and help. | 5 Basic, 2 Partial, 1 Works | `docs/`, `README.md`, `ROADMAP.md`, `editor/VisualScriptNodeHelp.hpp`, `editor/main.cpp` (+5 more) | [19.1](feature_map/19-docs-tests-health.md#191-readme-and-docs), [19.5](feature_map/19-docs-tests-health.md#195-roadmap-and-pr-template), [51.2](feature_map/43-proposals.md#512-guided-tutorials-in-the-editor), [51.3](feature_map/43-proposals.md#513-welcome-screen-and-tips), [51.4](feature_map/43-proposals.md#514-contextual-help), [57.1](feature_map/43-proposals.md#571-static-documentation-site), [57.2](feature_map/43-proposals.md#572-generated-api-reference), [57.3](feature_map/43-proposals.md#573-docs-inside-the-editor) |
| **Tests and Code Health** | The automated tests run after every change, compiler warnings, memory checkers and splitting big files. | 3 Works, 2 Partial, 1 Basic, 1 Planned | `tests/`, `tests/scripts/`, `.gitignore`, `CMakeLists.txt`, `launch.bat` (+13 more) | [19.2](feature_map/19-docs-tests-health.md#192-test-suite), [19.3](feature_map/19-docs-tests-health.md#193-qa-workflow-rig-sweep-test), [19.4](feature_map/19-docs-tests-health.md#194-splitting-the-largest-files-editor-launcher-okayscript-vm-serializer), [19.6](feature_map/19-docs-tests-health.md#196-repository-hygiene), [19.7](feature_map/19-docs-tests-health.md#197-compiler-warnings-and-sanitizers), [33.1](feature_map/20-35-planned.md#331-play-mode-tests), [33.3](feature_map/20-35-planned.md#333-regression-testing) |

## 25. Project management (High)

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Project Templates and Upgrades** | New-project templates, project settings and upgrading old projects to the current version. | 2 Partial, 1 Works, 1 Planned | `engine/include/okay/Scene/`, `engine/include/okay/Core/`, `editor/main.cpp`, `engine/include/okay/Platform/Workshop.hpp`, `launcher/main.cpp` | [25.1](feature_map/20-35-planned.md#251-templates), [25.2](feature_map/20-35-planned.md#252-project-settings), [25.3](feature_map/20-35-planned.md#253-migration), [25.4](feature_map/20-35-planned.md#254-dependency-management) |

## 28. Version control integration (High)

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Version Control** | Git inside the launcher and editor: commit, compare and fix merge conflicts. | 3 Partial, 1 Planned | `engine/include/okay/Scene/`, `editor/main.cpp`, `launcher/main.cpp` | [28.1](feature_map/20-35-planned.md#281-git-integration), [28.2](feature_map/20-35-planned.md#282-diffs), [28.3](feature_map/20-35-planned.md#283-project-history), [28.4](feature_map/20-35-planned.md#284-merge-conflict-tools) |

## 32. Accessibility (High)

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Accessibility** | Playing with a controller only, colour-blind options and other help for players with different needs. | Partial | `engine/include/okay/Input/InputActions.hpp`, `engine/include/okay/Input/PlatformGamepad.hpp`, `engine/include/okay/Components/UINavigation.hpp`, `engine/include/okay/Core/AccessibilitySettings.hpp`, `engine/include/okay/Graphics/ColorVision.hpp` (+1 more) | [32.1](feature_map/20-35-planned.md#321-controller-navigation), [32.3](feature_map/20-35-planned.md#323-colorblind-options) |

## 36. Math & geometry

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Math** | Vectors, rotations, matrices, bounds, easing, noise and random numbers every system uses. | 4 Works, 2 Partial | `engine/include/okay/Math/`, `engine/include/okay/Render/`, `engine/include/okay/Components/Camera.hpp`, `engine/include/okay/Core/Rng.hpp` | [36.1](feature_map/36-math-geometry.md#361-vectors-and-colours), [36.2](feature_map/36-math-geometry.md#362-quaternions-and-matrices), [36.3](feature_map/36-math-geometry.md#363-scalars-interpolation-and-easing), [36.4](feature_map/36-math-geometry.md#364-bounds-rectangles-and-shapes), [36.6](feature_map/36-math-geometry.md#366-geometry-helpers-elsewhere), [36.7](feature_map/36-math-geometry.md#367-noise-curves-colour-spaces-and-random-streams) |

## 38. Render backends & GPU

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Render Backends** | The OpenGL, Direct3D 11, Direct3D 12 and CPU (software) drawing code; the software one needs no graphics card and is used by tests. | 5 Works, 1 Partial | `engine/include/okay/Render/GLRenderer.hpp`, `engine/include/okay/Render/D3D11Renderer.hpp`, `engine/include/okay/Render/D3D12Renderer.hpp`, `engine/include/okay/Render/SoftwareRenderer.hpp`, `engine/include/okay/Render/ConsoleRenderer.hpp` | [38.2](feature_map/38-render-backends.md#382-opengl-backend), [38.3](feature_map/38-render-backends.md#383-direct3d-11-backend), [38.4](feature_map/38-render-backends.md#384-direct3d-12-backend), [38.5](feature_map/38-render-backends.md#385-software-rasterizer), [38.6](feature_map/38-render-backends.md#386-console-renderer), [38.9](feature_map/38-render-backends.md#389-gpu-resources) |
| **Shaders** | The small GPU programs that colour each pixel, including our own `.okayshader` language for custom looks. | 1 Works, 1 Partial, 1 Planned | `engine/include/okay/Render/`, `editor/VisualScriptEditor.hpp`, `engine/include/okay/Components/MeshRenderer.hpp`, `engine/include/okay/VisualScript/NodeGraph.hpp` | [38.8](feature_map/38-render-backends.md#388-shader-pipeline), [26.1](feature_map/20-35-planned.md#261-visual-shader-graphs), [26.3](feature_map/20-35-planned.md#263-custom-shaders) |

## 39. Security & anti-cheat

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Network Security** | Encrypts game traffic, blocks replayed packets and gives players signed passes to join servers. | 2 Partial, 1 Works | `engine/include/okay/Net/`, `supabase/functions/session-ticket/`, `engine/include/okay/Core/`, `shared/`, `docs/sql/reactions_privacy.sql` | [39.2](feature_map/39-security-anticheat.md#392-transport-security-encryption-replay-protection-sealed-join-host-keys), [17.11](feature_map/17-launcher-platform.md#1711-security-plan-sec1), [39.3](feature_map/39-security-anticheat.md#393-session-tickets-and-the-ticket-service) |
| **Game File Safety** | Checks that a game's files were not changed, keeps signing keys out of the code, and stops scripts touching files outside the game. | 2 Works, 1 Partial | `shared/`, `CMakeLists.txt`, `.github/workflows/release.yml`, `cmake/OkayAntiCheatKeys.cmake`, `engine/include/okay/Components/ActionList.hpp` (+6 more) | [39.4](feature_map/39-security-anticheat.md#394-client-integrity-signed-manifest-and-launch-check), [39.7](feature_map/39-security-anticheat.md#397-trusted-keys-and-signing-keys-at-build-time), [39.9](feature_map/39-security-anticheat.md#399-script-and-file-path-safety) |

## 40. OkayUI standalone library

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **OkayUI** | Our own UI toolkit for the launcher, editor panels and script-drawn windows (replacing Dear ImGui) | 8 Works, 2 Partial (duplicate, see [below](#duplicates-found)) | `okayui/tests/`, `shared/`, `okayui/include/okay/UI/`, `okayui/src/`, `editor/` (+14 more) | [40.1](feature_map/40-okayui-library.md#401-core-widgets-and-auto-layout), [40.2](feature_map/40-okayui-library.md#402-launcher-look-theme-and-fonts), [40.3](feature_map/40-okayui-library.md#403-backends-and-frame-loop), [40.4](feature_map/40-okayui-library.md#404-hosts-script-bridge-and-built-in-windows), [40.5](feature_map/40-okayui-library.md#405-how-okayui-relates-to-scene-ui), [40.6](feature_map/40-okayui-library.md#406-headless-software-backend-and-test-harness), [40.7](feature_map/40-okayui-library.md#407-draw-list-cursor-item-state-and-disabled-blocks), [40.8](feature_map/40-okayui-library.md#408-porting-helpers-tooltip-scope-style-fields-mapping-layer-list-clipper), [40.9](feature_map/40-okayui-library.md#409-ported-code-calls-window-frame-context-style-presets-and-child-flags), [40.10](feature_map/40-okayui-library.md#4010-the-inspector-on-okayui-preview-off-by-default) |

## 41. Build, tools & infrastructure

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Build System** | The CMake project, build scripts, small generator tools and pinned dependency versions that build everything. | Works | `scripts/`, `cmake/`, `CMakeLists.txt`, `launch.bat`, `launch.sh` (+10 more) | [41.1](feature_map/41-build-tools-infra.md#411-cmake-project-layout-and-options), [41.2](feature_map/41-build-tools-infra.md#412-cmake-modules-windows-resources-and-toolchain), [41.3](feature_map/41-build-tools-infra.md#413-local-build-and-launch-scripts), [41.4](feature_map/41-build-tools-infra.md#414-developer-tools), [41.10](feature_map/41-build-tools-infra.md#4110-dependencies-and-pinned-versions) |
| **Online Backend** | The Supabase database tables and server functions behind accounts, chat, forums and updates. | Works | `docs/sql/`, `supabase/functions/session-ticket/` | [41.6](feature_map/41-build-tools-infra.md#416-supabase-functions-and-sql-schema) |
| **Own-Stack Switch** | One switch that runs the engine with all our own replacements for third-party code turned on, to try them. | Works | `scripts/`, `shared/OwnStack.hpp`, `shared/OwnSwitches.hpp`, `CMakeLists.txt`, `.github/workflows/ci.yml` (+2 more) | [41.11](feature_map/41-build-tools-infra.md#4111-own-stack-dogfood-switch-and-startup-line) |
| **Collaboration Tools** | Scripts that keep the three Claudes from doing the same work twice: lanes, claims, find-existing and branch lint. | Works | `scripts/`, `.github/workflows/branch-lint.yml` | [41.12](feature_map/41-build-tools-infra.md#4112-collaboration-tooling-lanes-claims-find-existing-collision-check-branch-lint) |
| **Duplicate Guard** | Finds pasted code and new copies of shared helpers and fails CI when it finds them (`scripts/dup_lint.py`). | Works | `scripts/dup_lint.py`, `scripts/dup_baseline.json`, `scripts/install_hooks.sh`, `.github/workflows/dup-scan.yml`, `docs/engine/shared_code.md` | [41.12](feature_map/41-build-tools-infra.md#4112-collaboration-tooling-lanes-claims-find-existing-collision-check-branch-lint) |

## 42. Examples, samples & sandbox

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Examples** | Example scenes, demos, starter assets and sample plugins people can open and learn from. | 5 Works, 1 Partial, 1 Basic | `editor/`, `examples/`, `examples/editor-plugins/AlignTools.okay`, `examples/editor-plugins/CleanupNames.okay`, `sandbox/CMakeLists.txt` (+8 more) | [42.1](feature_map/42-examples-samples.md#421-sandbox-demo-solar-system), [42.2](feature_map/42-examples-samples.md#422-network-demo), [42.3](feature_map/42-examples-samples.md#423-physics-demo), [42.5](feature_map/42-examples-samples.md#425-editor-plugin-examples), [42.7](feature_map/42-examples-samples.md#427-starter-assets), [42.8](feature_map/42-examples-samples.md#428-example-scenes-and-their-check), [51.1](feature_map/43-proposals.md#511-sample-gallery) |
| **Game Player** | The program that plays a built game (`OkaySpacePlayer.exe` or `<Game>.exe`). | Works | `player/main.cpp`, `player/CMakeLists.txt` | [42.6](feature_map/42-examples-samples.md#426-standalone-player-runtime) |

## 44. Level design and blockout tools

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Level Design Tools** | Grey-box shapes on a grid, a prefab brush and measuring tools for laying out levels. | 2 Basic, 1 Works | `engine/include/okay/Components/`, `editor/`, `engine/include/okay/Modeling/PolyOps.hpp`, `engine/include/okay/Render/Mesh.hpp` | [44.1](feature_map/43-proposals.md#441-blockout-and-grid-placement), [44.2](feature_map/43-proposals.md#442-prefab-brush-and-placement), [44.3](feature_map/43-proposals.md#443-measuring-and-level-metrics) |

## 45. Open-world streaming and world partition

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **World Streaming** | Splits a big world into cells that load and unload as the player moves (mostly planned). | 2 Planned, 1 Partial | `engine/include/okay/Components/NavMeshAgent.hpp`, `engine/include/okay/Components/WorldStreamer.hpp`, `engine/include/okay/Scene/SceneSerializer.hpp`, `engine/include/okay/Scene/Transform.hpp`, `engine/include/okay/Core/SaveGame.hpp` (+2 more) | [45.1](feature_map/43-proposals.md#451-cell-authoring-and-baking), [45.2](feature_map/43-proposals.md#452-large-world-coordinates), [45.3](feature_map/43-proposals.md#453-streaming-aware-systems) |

## 46. Replay system

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Replays** | Records a play session and plays it back, including killcams (planned). | Planned | `engine/include/okay/Net/`, `engine/include/okay/Core/FrameRecord.hpp`, `engine/include/okay/Core/Rng.hpp`, `engine/include/okay/ECS/NetWorldSync.hpp`, `engine/include/okay/Testing/PlayTest.hpp` | [46.1](feature_map/43-proposals.md#461-input-recording-and-playback), [46.2](feature_map/43-proposals.md#462-state-replay-and-killcam), [46.3](feature_map/43-proposals.md#463-replay-files-and-viewer) |

## 47. Mod support

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Mods** | Lets players add or replace game content from a Mods folder (planned). | Planned | `engine/include/okay/Core/`, `engine/include/okay/Inventory/ItemDatabase.hpp`, `engine/include/okay/Platform/Workshop.hpp`, `engine/include/okay/Scripting/OkayScriptVM.hpp`, `launcher/main.cpp` (+1 more) | [47.1](feature_map/43-proposals.md#471-mod-loader-and-content-overrides), [47.2](feature_map/43-proposals.md#472-mod-scripting-sandbox), [47.3](feature_map/43-proposals.md#473-mod-packaging-and-sharing) |

## 49. Cloud services

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Cloud Services** | Cloud saves, online leaderboards and settings changed from a server. | 2 Partial, 1 Planned | `engine/include/okay/Core/Cvar.hpp`, `engine/include/okay/Net/MatchResults.hpp`, `engine/include/okay/Platform/PlayFab/PlayFab.hpp`, `engine/include/okay/Platform/Steam/SteamService.hpp`, `engine/include/okay/Scripting/OkayScriptVM.hpp` (+1 more) | [49.1](feature_map/43-proposals.md#491-cloud-saves), [49.2](feature_map/43-proposals.md#492-leaderboards), [49.4](feature_map/43-proposals.md#494-remote-configuration) |

## 50. Capture and media

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Screenshots and Video** | Screenshots, photo mode and video recording and playback. | 3 Planned, 1 Basic | `engine/include/okay/Audio/AudioClip.hpp`, `engine/include/okay/Audio/AudioMixer.hpp`, `engine/include/okay/Core/Game.hpp`, `engine/include/okay/Core/GameLoop.hpp`, `editor/main.cpp` (+5 more) | [50.1](feature_map/43-proposals.md#501-screenshots), [50.2](feature_map/43-proposals.md#502-photo-mode), [50.3](feature_map/43-proposals.md#503-video-recording), [50.4](feature_map/43-proposals.md#504-video-playback) |

## 54. Debug draw, gizmos, dev console and cheats

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Debug Draw and Dev Console** | Lines and shapes drawn to show what the game is thinking, component gizmos, and an in-game console for commands. | 2 Basic, 1 Partial, 1 Planned | `engine/include/okay/Core/`, `editor/main.cpp`, `engine/include/okay/Net/AntiCheat.hpp`, `engine/include/okay/Physics/PhysicsDebug.hpp`, `player/main.cpp` | [54.1](feature_map/43-proposals.md#541-debug-draw-api), [54.2](feature_map/43-proposals.md#542-gizmos-for-components), [54.3](feature_map/43-proposals.md#543-in-game-dev-console), [54.4](feature_map/43-proposals.md#544-cheats-and-debug-commands) |

## 58. Performance budgets and CI perf tests

| System | What it does | Status | Main code | Feature map |
|---|---|---|---|---|
| **Performance Checks** | Frame-time and size budgets, benchmark scenes and build-size reports. | 2 Partial, 1 Planned | `engine/include/okay/Core/`, `engine/include/okay/Render/LightSubset.hpp`, `engine/include/okay/Render/TriangleSpan.hpp`, `tests/bench_frame.cpp`, `tests/test_ui_render_bench.cpp` (+6 more) | [58.1](feature_map/43-proposals.md#581-performance-budgets), [58.2](feature_map/43-proposals.md#582-benchmark-scenes), [58.4](feature_map/43-proposals.md#584-build-size-and-load-time-report) |

## Duplicates found

Systems that do the same job as another system. "Being merged" means a pull request is folding it into the other one; any other row means both stay for now, for the reason given. Found while naming the systems and from the duplicate audits on issue #422; the code is not changed here.

| System | Same job as | State | Feature map |
|---|---|---|---|
| **Whole-Scene Save** | Save Games | Being merged (PR #1132) | [1.7](feature_map/01-core-scene.md#17-saves-and-prefs) |
| **ECS World** | Scenes and Objects | Kept on purpose: opt-in, mirrors objects through `SceneBridge`; no merge planned | [1.9](feature_map/01-core-scene.md#19-events-and-ecs) |
| **Simple Input Actions** | Input Actions | Being merged (PR #1139) | [8.3](feature_map/08-input.md#83-input-actions-and-rebinding-ui) |
| **Grid Pathfinding** | Navigation | Kept: grid and tilemap games; no merge planned | [9.3](feature_map/09-ai-navigation.md#93-grid-pathfinding-older) |
| **RPG Stats** | Health and Survival | A second health holder; one damage path since PR #1091, not one component | [11.9](feature_map/11-gameplay-systems.md#119-rpg-stats-and-xp) |
| **Typed OkayScript** | OkayScript | Two engine binding tables; a merge is planned on branch `claude2/lua-csharp-merge`, no PR yet | [13.2](feature_map/13-scripting.md#132-c-style-language-core) |
| **OkayUI** | Game UI | Kept on purpose: both have buttons, tabs, tooltips and a theme, but OkayUI is for tools and Game UI for designed game menus; see 40.5 | [40.1](feature_map/40-okayui-library.md#401-core-widgets-and-auto-layout) |
