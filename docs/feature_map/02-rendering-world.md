# 2. Rendering & world

What you see in a scene beyond the backends themselves: materials and meshes, lights and shadows, sky, fog, weather, post-processing, terrain and water, particles, lines and splines, sprites, text and fonts, cameras and culling.
Covers `engine/include/okay/Render` (everything except the backend classes, which are area 38), `Graphics`, `Environment`, and about 20 headers in `Components/` (Camera, Light, MeshRenderer, Terrain, ParticleSystem, Spline and friends).
Mostly on main and tested. Nothing is open: #574 added the optimization tools (LOD, occlusion, batching, streaming) and GPU and render stats as engine-side library code that no renderer calls yet (areas 23 and 27), and #575 made all four renderers skip children of an inactive parent; the old shader and material editor plan is area 26.

## 2. Rendering & world

### 2.1 Renderers
- **System:** Renderer - Picks the drawing backend (OpenGL, Direct3D, software) and gets each finished frame onto the screen.
  Home: `engine/include/okay/Render/`, `engine/src/Render/`. Entry: `engine/include/okay/Render/Renderer.hpp`. Also called: renderer interface, rendering, backend selection, present.
- **What:** The common face of rendering: the small `IRenderer` interface, the colour type, the console renderer, and the map of which backend draws a frame. The backends themselves (OpenGL, Direct3D 11 and 12, software, web) now live in area 38 so each can be targeted.
- **Parts:**
  - 2.1.1 Renderer interface - `Render/Renderer` - `IRenderer` (BeginFrame, EndFrame, SetCamera, DrawQuad, GetViewport) and `Viewport`; only `ConsoleRenderer` implements it and `SpriteRenderer::OnRender` is the only caller of `DrawQuad`
  - 2.1.2 Colour type - `Render/Color` - RGBA floats, `FromBytes`, `Lerp`, `Luminance`, named constants (see 36.1)
  - 2.1.3 Console renderer - `Render/ConsoleRenderer` - ASCII frame buffer for headless runs and tests (see 38.6)
  - 2.1.4 Backend map - see 38.2 OpenGL, 38.3 Direct3D 11, 38.4 Direct3D 12, 38.5 software, 38.7 web; how a frame is picked and shown is 38.1 and 38.10
  - 2.1.5 Host-drawn layers - particles, lines, trails, UI and text are drawn by the editor and player on top of the 3D image, not inside the GPU backends (see 2.7, 2.9)
- **Code:** `Render/Renderer`, `Render/Color`, `Render/ConsoleRenderer`
- **API:** `IRenderer`, `Viewport`, `Color`, `ConsoleRenderer`, `Scene::Render`, `Application::SetRenderer`
- **Editor:** Scene and Game views (15.3); Stats panel; View > Lighting panel "Rendering" section
- **Docs:** `docs/rendering/rendering.md`
- **Tests:** `software_renderer`
- **Status:** Works - read `Renderer.hpp`, `Color.hpp`, `Scene::Render`; the interface is tiny (five calls) because the 3D path bypasses it
- **Open:** editor Play frame rate when only the software rasterizer is available (old note); the 3D backends do not implement `IRenderer` at all, so a new backend is a copy of the `RenderToPixels` contract, not of this interface
- **Related:** 38, 2.2, 2.8, 15.3
- **Words:** renderer, IRenderer, render pipeline, draw, backend, GPU renderer, console renderer, viewport, Color, drawing
- **Seen as:** the game runs at low FPS in the editor; 3D looks different in the editor and the player; black frame after a driver problem
- **Ask me like:** "Which renderer is drawing my Game view right now?"; "Add a new field to Color without breaking the saved scenes"

### 2.2 Materials and textures
- **System:** Materials and Textures - How a surface looks: colour, roughness and shine, the pictures on it, and texture mipmaps, atlases and compression.
  Home: `engine/include/okay/Render/`, `engine/include/okay/Texture/`. Entry: `engine/include/okay/Render/Material.hpp`. Also called: Material, PBR, texture library, mipmaps, atlases, texture compression. Fix: Texture/ is the texture library and Render/Material the surface; write that split into architecture.md, or move Texture/ under Render/.
- **What:** How a surface looks: reusable material presets, the Mesh Renderer component the backends read, procedural textures, and the mesh geometry with its primitives and loaders.
- **Parts:**
  - 2.2.1 Material asset - `Render/Material` - `.okaymat` preset (colour, emissive, specular, shininess, unlit, double-sided, texture, tiling, shader and toon bands, rim, outline, UV scroll, triplanar); `FromRenderer` and `ApplyTo`
  - 2.2.2 Mesh Renderer component - `Components/MeshRenderer` - the live fields the renderers read: shader mode (Standard, Unlit, Toon, Gradient, Fresnel, Iridescent, Hologram, Posterize, Velvet, Custom), normal, specular and AO maps, metallic, reflectivity, shadows, ground shadow, outline, wireframe, texture filter
  - 2.2.7 Custom surface shaders - `Render/OkayShader` - our own `.okayshader` language: properties, textures and a `surface` block compiled to GLSL, HLSL and a CPU register program; Shader = Custom on the Mesh Renderer, magenta when it does not compile, reloads when the file changes; see 26.3 and `docs/rendering/shaders.md`
  - 2.2.3 Procedural textures - `Render/ProcTexture` - `proc:checker`, `grid`, `brick`, `wood`, `stone`, `tiles`, `marble`, `metal`, `noise`, `grass`, `sand`, `lava`; 256x256, tileable, deterministic, usable anywhere a texture path goes
  - 2.2.4 Mesh geometry - `Render/Mesh` - indexed triangle mesh with optional normals, UVs and per-face colours; primitives (Cube, Sphere, Cylinder, Cone, Torus, TorusKnot, Tube, Capsule, Icosphere...), prop library (Tree, House, Campfire, Bench...), `FromName`
  - 2.2.5 Mesh operations - `Render/Mesh` - smooth normals, UV projections, Combine, Weld, Subdivide, extrude, inset, Boolean, ConvexHull, Decimate, OBJ load and save; shared with the modeling windows (3.1-3.5)
  - 2.2.6 Model loaders - `Render/Gltf`, `Render/ModelImport` - glTF and GLB, OBJ, STL, PLY, FBX built in, many more formats through optional Assimp; base colour, normal, occlusion, packed metallic-roughness (green roughness, blue metallic) and emissive textures land on the MeshRenderer; detail and tests in 3.6
- **Code:** `Render/Material`, `Render/ProcTexture`, `Render/Mesh`, `Render/Gltf`, `Render/ModelImport`, `Components/MeshRenderer`, `Render/OkayShader`
- **API:** `Material`, `Material::ApplyTo`, `MeshRenderer`, `oshader::Compile`, `oshader::LoadShaderFile`, `Mesh::Cube`, `Mesh::FromName`, `GenerateProcTexture`, `ImportModel`, `proc:wood`
- **Editor:** Mesh Renderer inspector section and material ball preview (editor/main.cpp); Project panel materials; Modeling window (3.1)
- **Docs:** `docs/rendering/rendering.md`, `docs/rendering/shaders.md`, `docs/editor/modeling.md`
- **Tests:** `material`, `shaders`, `okayshader`, `okayshader_render`, `okayshader_gl`, `mesh3d`, `mesh_variant_hash`, `3d`, `gltf_pbr_maps`
- **Status:** Works - read `Material.hpp`, `ProcTexture.hpp`, headers of `Mesh`, `Gltf`, `ModelImport`; tests cover round trips and primitive geometry
- **Open:** material editor window with live preview, visual shader graph and a full energy-conserving PBR model are area 26; custom shader files work on software and OpenGL, are unverified on Direct3D 11 and draw as Standard on Direct3D 12 (26.3); `Mesh.hpp` is one 4k-line header mixing primitives, props, OBJ and editing ops
- **Related:** 2.1, 3.5, 3.6, 26, 38.8
- **Words:** Material, okaymat, MeshRenderer, shader, custom shader, okayshader, toon, rim light, outline, triplanar, normal map, procedural texture, proc:, Mesh, primitive, OBJ, glTF, FBX, Assimp, materials, textures
- **Seen as:** model dark, inside out or without textures; a material change does not show; texture tiles wrong; a primitive has the wrong winding
- **Ask me like:** "Add a Cartoon rim-light variant of the Toon shader"; "Make a brick wall without a texture file"

### 2.3 Lighting and shadows
- **System:** Lighting and Shadows - Sun, point, spot and area lights, the shadows they cast, and baked lighting.
  Home: `engine/include/okay/Render/`. Entry: `engine/include/okay/Render/Lighting.hpp`. Also called: lights, shadows, lightmaps, light probes, baked lighting.
- **What:** Lights (sun, point, spot, area), how every renderer shades with them, cascaded and local shadows, baked per-face lighting, and a 24-hour day/night cycle.
- **Parts:**
  - 2.3.1 Light component - `Components/Light` - Directional, Point, Spot, Area; colour or Kelvin temperature, intensity, range, linear or inverse-square falloff, spot angle and softness, flicker, shadow strength and bias; `ApplySceneLight` fills `SceneLights`
  - 2.3.2 Shared shading maths - `Render/Lighting` - `SceneLight`, `SceneLights`, `LightSample`, Lambert shade, attenuation, area wrap, hemisphere ambient; the GPU shaders mirror it
  - 2.3.3 Shadow plan - `Render/ShadowSetup` - sun cascades side by side in one atlas, a 4x4 tile atlas for spot (1 tile) and point/area (6 faces), world-space bias, texel snapping; shared by OpenGL and Direct3D 11
  - 2.3.4 Lightmap bake - `Render/Lightmap` - `BakeFaceLighting` writes lighting, shadows and ambient occlusion into a static mesh's per-face colours; editor Bake Lighting button
  - 2.3.5 Day and night - `Components/DayNightCycle` - 24 h clock rotates the sun, tints and dims it, eases ambient, blends the camera sky; publishes `hour`; `SetTime`, `AddHours`
  - 2.3.6 Flashlight - `Components/Flashlight` - toggleable spot light that shines where the camera looks (key F by default) for caves and dark rooms; spawns a child Light and saves with the scene (tested inside `terrain`)
  - 2.3.7 Lighting upgrade - `Render/ShadowMath`, `Render/Lighting`, `Render/LightingPresets` - stable cascades (grid tied to the world), slope bias, soft penumbra and distance fade (software), smooth spot cone (all renderers), soft light roll-off, rectangle Area light, light masks (cookies), real-world lux/lumens with Auto brightness, glow lights from emissive meshes, five lighting presets, baked light probes, scene SSAO in the software renderer; `docs/rendering/lighting.md`
- **Code:** `Render/Lighting`, `Render/ShadowSetup`, `Render/ShadowMath`, `Render/LightingPresets`, `Render/Lightmap`, `Components/Light`, `Components/DayNightCycle`, `Components/Flashlight`
- **API:** `Light`, `Light::Type`, `ApplySceneLight`, `SceneLights`, `PlanShadows`, `BakeFaceLighting`, `DayNightCycle::SetTime`, script `set_light`, `set_ambient`
- **Editor:** View > Lighting panel (Environment, Bake Lighting, Rendering with shadow resolution); GameObject > Light menu; Light and Day/Night inspectors (editor/main.cpp)
- **Docs:** `docs/rendering/lighting.md`, `docs/rendering/lighting.md`, `docs/rendering/lighting.md`
- **Tests:** `lighting`, `light_shadows`, `lighting_upgrade`, `shadows`, `daynight`, `terrain`, `lightmap`
- **Status:** Works - read `Lighting.hpp`, `ShadowSetup.hpp`, `Light.hpp`; tests cover the GPU shadow plan, flicker, save/reload and the software renderer's switches
- **Open:** one Directional light casts sun shadows (the first one with Cast Shadows on; the Inspector warns when more than one asks); at most 16 lights shaded per frame on the GPU; Soft Edges, light masks and rectangle panels run in the software renderer and are written but never run in the Direct3D 11 and OpenGL shaders (OpenGL softens by an approximation); light probes (`Render/LightProbes`, `LightProbeBake`, Lighting window > Light Probes) are software only; no reflection probes; the Direct3D 12 backend has one sun map, no cascades or local shadows (all docs/rendering/lighting.md); lightmap bake stores per-face colours, not UV lightmap textures (Lightmap.hpp)
- **Related:** 2.4, 2.5, 38.2, 38.3, 38.5, 15.3
- **Words:** Light, sun, point light, spot light, area light, shadow, cascade, shadow map, PCF, lightmap, bake, ambient, flicker, DayNightCycle, day night, time of day, lights, shadows, day/night
- **Seen as:** shadows look wrong, blocky or shimmer; a point light casts no shadow; the scene is too dark; sunrise does not change the sky; a flashlight does not light the ground
- **Ask me like:** "Why is my second sun not casting shadows?"; "Run the day/night clock faster and show the hour on the HUD"

### 2.4 Sky, fog and weather
- **System:** Sky, Weather and Time of Day - The sky, sun, stars, clouds and fog, rain and snow, and a day-night cycle that games can react to.
  Home: `engine/include/okay/Environment/`. Entry: `engine/include/okay/Environment/Weather.hpp`. Also called: Atmosphere, sky, fog, day-night cycle, Wind.
- **What:** The atmosphere of a scene: a gradient or sun-driven procedural sky with stars and clouds, three fog modes, rain, snow and storms with gusts and lightning, and the global wind particles and water follow.
- **Parts:**
  - 2.4.1 Scene render settings - `Scene/Scene` (RenderSettings) - sky mode and colours, sun disc, stars, ambient mode, fog, post and weather, all saved with the scene
  - 2.4.2 Sky - `Render/Atmosphere`, `Render/SkyStars` - colour gradient, or a procedural sky from the sun's height (day blue, dusk orange, night with stars, sun disc and halo, haze, drifting clouds); deterministic star placement
  - 2.4.3 Fog - `Render/Atmosphere` - Linear, Exponential and Height fog; `FogFactor`; match-sky colour option
  - 2.4.4 Weather - `Environment/Weather` - Rain, Snow, Storm presets built from the particle system on a transient rig that follows the camera; cross-fade, gusts, lightning flash, thunder clip
  - 2.4.5 Wind - `Environment/Wind` - global `WindVector()` (the weather's wind) and `LightningFlash()` read by particles and water; the base the wind field starts from
  - 2.4.6 Look presets - `Render/Atmosphere` - Default, Cinematic, Vivid, Noir, Sunset, Night, Foggy morning, Storm; `ApplyLookPreset`, `ResetLook`
  - 2.4.7 Script access - `Render/Atmosphere` - `PostSet`, name lookups behind `post_set`, `fog_set`, `weather_set`, `time_of_day`
  - 2.4.8 Wind field - `Environment/WindField` - the wind at any world position and time: weather wind plus Wind Zones (everywhere, directional, outward; box, sphere, cylinder), gusts that travel downwind, deterministic value-noise swirl, a strength-over-time curve, priority blending; `WindField::At`
  - 2.4.9 Wind components - `Components/WindZone` - Wind Zone, Wind Drag (the wind pushes a Rigidbody 3D or a ragdoll's bones), Wind Sway (trees, grass, flags bend on the CPU in every renderer), Wind Sound (our own looping noise); Foliage, Cable and Particle "Affected by wind" read the same field
- **Code:** `Render/Atmosphere`, `Render/SkyStars`, `Environment/Weather`, `Environment/Wind`, `Environment/WindField`, `Components/WindZone`, `Scene/Scene`
- **API:** `Scene::RenderSettings`, `SkyPaletteFor`, `FogFactor`, `UpdateWeather`, `WindVector`, `WindField::At`, `WindZone`, `WindDrag`, `WindSway`, `WindAmbience`, `ApplyLookPreset`, script `weather_set`, `fog_set`, `Weather.Set`, `wind_speed`, `wind_set`, `wind_gust`, `Wind.At`, `Wind.SetGust`
- **Editor:** View > Lighting panel: Sky, Fog, Weather and Wind sections and the Look Preset combo (`DrawLookSections` in editor/main.cpp); GameObject > Effects > Environment and Add Component > Environment (`editor/WindTools.cpp`); wind arrows in the Scene view
- **Docs:** `docs/rendering/rendering.md`, `docs/gameplay/wind.md`
- **Tests:** `post_sky`, `wind_field`, `wind`
- **Status:** Works - read `Atmosphere.hpp` and `Weather.hpp`; the procedural sky and clouds are drawn by the GPU backends (docs), the software renderer shows a plain gradient
- **Open:** old scenes load with everything off by design; the procedural sky, cloud layer and lightning exposure flash have no software-renderer version; weather depends on the particle system drawn by the host (2.7); no volumetric fog or sky reflections beyond the ambient and reflective-material colours; wind: sway is done on the CPU (not in a GPU vertex shader), trails, spline followers and fog are not linked to the wind, Water still reads only the weather's wind, no multiplayer sync of the wind, no Visual Script nodes (script names only), nothing run on Windows or a GPU (`docs/gameplay/wind.md`)
- **Related:** 2.3, 2.5, 2.7, 38.8
- **Words:** sky, skybox, procedural sky, clouds, stars, fog, weather, rain, snow, storm, lightning, wind, gust, breeze, sway, flag, trees blowing, wind zone, fan, look preset, Atmosphere, Weather, WindVector
- **Seen as:** the sky does not change with the sun; distant objects pop in without fog; rain follows the camera in the editor preview; wind does not move particles; trees and grass stand still in the wind; a flag does not wave; a box is not pushed by the wind
- **Ask me like:** "Add a foggy morning preset that also lowers the sun"; "Make storms play thunder 2 seconds after the flash"; "Make the trees sway in a gusty wind"; "Add a fan that blows the leaves away"

### 2.5 Post-processing
- **System:** Post-Processing - Screen effects on the finished picture: bloom, colour grading, depth of field, anti-aliasing and more.
  Home: `engine/include/okay/Render/`. Entry: `engine/include/okay/Render/Atmosphere.hpp`. Also called: post effects, bloom, color grading, depth of field, anti-aliasing.
- **What:** Screen effects on the finished frame: exposure, tonemap, bloom, colour grading, vignette, anti-aliasing, ambient occlusion and depth of field, with a software fallback for some.
- **Parts:**
  - 2.5.1 Settings and chain order - `Scene/Scene` (PostSettings), `Render/Atmosphere` - exposure, tonemap (None, ACES, Filmic), bloom, grading (temperature, tint, saturation, contrast, lift/gamma/gain), vignette; chain: SSAO, bloom, exposure/tonemap/grade/vignette, depth of field, FXAA, in an HDR target
  - 2.5.2 GPU chain - `Render/GLRenderer`, `Render/D3D11Renderer` - OpenGL runs every effect; Direct3D 11 has bloom, tonemap, grading, vignette, FXAA, an SSAO pass and a depth of field pass (the last two compile-checked only, UNVERIFIED on a GPU); depth of field keeps the frame's coverage (premultiplied blur), so the background the host paints (sky pass off: solid-colour camera, Sky off) shows through out-of-focus empty pixels instead of an opaque black blur (OpenGL checked on Mesa under Xvfb by `gl_post_alpha`, Direct3D 11 compile-checked); Bloom Softness and Ambient Occlusion Power (`Render/PostQualityMath.hpp`) are shared by OpenGL and Direct3D 11 (CPU-tested maths, shaders unrun); Direct3D 12 has none (see 38)
  - 2.5.3 Software passes - `Render/SoftwareRenderer` - its own CPU SSAO, bloom, tonemap, colour grade, vignette and FXAA (scene switches and the process-wide ones), and the scene's depth of field (`ApplySceneDof`: half-res premultiplied 9-tap binomial blur, the shared focus maths of `DofMath.hpp`, straight-line eye distance from the W-buffer; perspective cameras only) and AO Power (`SSAOPower`), row-parallel
  - 2.5.4 Quality presets - `Core/DisplaySettings` - Low/Medium/High flips shadows, bloom, SSAO, FXAA and per-pixel lighting, and sets shadow map resolution, cascade count and shadow distance (Low 512/1/40 m, Medium 1024/2/60 m, High 2048/3/80 m)
- **Code:** `Render/Atmosphere`, `Render/SoftwareRenderer`, `Scene/Scene`, `Core/DisplaySettings`
- **API:** `Scene::RenderSettings::PostSettings`, `PostActive`, `TonemapACES`, `ExposureScale`, `BloomEnabled()`, `SSAOEnabled()`, script `post_set`
- **Editor:** View > Lighting panel: Post-processing section and Look Preset; "Rendering (Lighting & Post FX)" holds the editor's own process-wide switches
- **Docs:** `docs/rendering/rendering.md`
- **Tests:** `post_sky`, `dof_math`, `post_quality_math`, `ssao_math`, `software_scene_post`, `gl_post_alpha`
- **Status:** Works - read `Atmosphere.hpp`, the post fields of `Scene.hpp` and the effect counts in the backends; settings round-trip per `post_sky`
- **Open:** SSAO and depth of field on Direct3D 11 have never run on a GPU; Direct3D 12 has no post chain, and software skips depth of field with an orthographic camera and ignores Bloom Softness; the Lighting window, Build Settings and the Light Inspector name the switches a renderer ignores (`docs/rendering/rendering.md`); FXAA and per-pixel lighting switches only affect software; no motion blur, lens flare or colour LUT
- **Related:** 2.4, 1.15, 38.2, 38.3, 38.5, 26
- **Words:** post-processing, post FX, bloom, tonemap, ACES, exposure, colour grading, vignette, FXAA, SSAO, ambient occlusion, depth of field, DoF, HDR, look preset
- **Seen as:** bloom or occlusion missing on one renderer; the frame looks washed out or too contrasty; vignette drawn twice; effects change between editor and player
- **Ask me like:** "Check the Direct3D 11 depth of field on a real GPU"; "Turn on bloom only for the Night preset"

### 2.6 Terrain and water
- **System:** Terrain and Water - Landscapes you can sculpt, paint and dig, plants and decals on them, and animated water.
  Home: `engine/include/okay/Components/Terrain.hpp`, `engine/include/okay/Components/Water.hpp`. Entry: `engine/include/okay/Components/Terrain.hpp`. Also called: Terrain, VoxelTerrain, Water, foliage, vegetation.
- **What:** World-building components: sculptable heightmap terrain, cave-capable voxel terrain, runtime digging, animated water with buoyancy, world streaming by cells, and 2D tilemaps.
- **Parts:**
  - 2.6.1 Heightmap terrain - `Components/Terrain` - square height grid, brushes (raise, smooth, flatten, noise, erode), generators, hydraulic and thermal erosion, terrace, heightmap PNG import and export; builds the sibling MeshRenderer's mesh
  - 2.6.2 Terrain digging - `Components/TerrainDigger` - hold the mouse in Play to dig, raise, smooth or flatten under the crosshair; no caves or overhangs
  - 2.6.3 Voxel terrain - `Components/VoxelTerrain` - 3D density field meshed with marching cubes: caves, tunnels, overhangs; Dig, Add, Smooth, DigBox, RemoveFloaters
  - 2.6.4 Voxel digging - `Components/VoxelDigger`, `Components/VoxelAim` - left mouse / RT digs, right mouse / LT adds, aim through the crosshair in first and third person (reach from the character, no digging inside the body or through a wall), marker with dig progress, rebindable Dig / Place / PickBlock / Hotbar actions, number-key and wheel hotbar; skips rebuilds when nothing changed
  - 2.6.5 Water - `Components/Water` - animated wave mesh, reflective semi-transparent material that follows the wind, buoyancy (`fluidDensity`, `linearDrag`) for rigidbodies
  - 2.6.8 Water surface and waves - `Components/Water`, `Environment/WaterWaves` - Style (Classic, Ocean, Calm lake), a seeded Gerstner wave sum (direction, steepness, count, seed), a current, and `SurfaceAt` (height, slope, flow) that the picture and the physics share
  - 2.6.10 Water look - `Components/Water`, `Environment/WaterShading` - sea states and 8 presets, depth colour, crest and shore foam, crest glow, never-still ripples (`proc:water_normal`), rings of squares that follow the camera, splash rings and spray, wake hook (`AddWake`), underwater fog and bubbles
  - 2.6.9 Buoyancy - `Components/Buoyancy` - sample points across the hull, Archimedes push per point, drag per hull direction against the moving water, angular damping, splash events, runs in FixedUpdate; crates float at the right draft, heavy things sink
  - 2.6.6 World streaming - `Components/WorldStreamer` - cell prefabs `<prefix>_<x>_<z>.okayprefab` loaded within a radius and unloaded past a larger one (hysteresis)
  - 2.6.7 Tilemap - `Components/Tilemap`, `Components/TilemapCollider2D` - integer tile grid with cell/world conversion; static box colliders merged per row
- **Code:** `Components/Terrain`, `Components/TerrainDigger`, `Components/VoxelTerrain`, `Components/VoxelDigger`, `Components/Water`, `Environment/WaterWaves`, `Environment/WaterShading`, `Components/Buoyancy`, `Components/WorldStreamer`, `Components/Tilemap`, `Components/TilemapCollider2D`
- **API:** `Terrain::RaiseAt`, `Terrain::Erode`, `VoxelTerrain::Dig`, `TerrainDigger`, `Water`, `WorldStreamer`, `Tilemap`, `TilemapCollider2D`
- **Editor:** GameObject > 3D Object > Terrain, Voxel Terrain (caves), Water and GameObject > 2D Object > Tilemap; their Inspector sections and the Terrain sculpt tools (editor/main.cpp)
- **Docs:** `docs/rendering/terrain_water.md`, `docs/rendering/terrain_water.md`
- **Tests:** `boat`, `terrain`, `terrain_ops`, `voxel`, `voxel_dig`, `worldstreamer`, `tilemap_collision`, `particles_tilemap`, `world_edges`
- **Status:** Works - read the headers of every component above; `terrain` also covers water, flashlight and the sandbox template; `world_edges` covers the edge of a raised terrain (normals and slope one cell from the edge used to read height 0 past the grid, so bodies slid and plants were refused there; now one-sided at the edge), terrain size 0 (no NaN), Foliage Scatter leaning with Align to Surface like the brush, and a water Detail from a damaged scene file (limited to 1..1024 instead of crashing the load)
- **Open:** vegetation and detail painting, procedural scatter and a proper sculpting UI are area 29; streaming is by prefab cell files baked by hand, not by the `Optimize/StreamingGrid` planner of #574 (area 27)
- **Related:** 2.2, 2.4, 6.2, 27, 29, 1.6
- **Words:** Terrain, heightmap, sculpt, brush, erosion, VoxelTerrain, marching cubes, cave, dig, TerrainDigger, VoxelDigger, Water, ocean, lake, buoyancy, WorldStreamer, world partition, streaming, Tilemap, tile
- **Seen as:** the ground has holes or cracks; digging is slow or leaves floating bits; the water has no waves or sinks objects; far chunks pop in or never unload; the player falls through the tilemap
- **Ask me like:** "Dig a tunnel with the voxel digger and keep the roof"; "Stream a 10 by 10 grid of cells around the player"

### 2.7 Particles and lines
- **System:** Particles - Smoke, fire, sparks and trails made of many small moving pictures.
  Home: `engine/include/okay/Components/ParticleSystem.hpp`, `engine/include/okay/Components/LineRenderer.hpp`. Entry: `engine/include/okay/Components/ParticleSystem.hpp`. Also called: ParticleSystem, VFX, trails, LineRenderer.
- **What:** Particle emitters with gradients, curves, motion modules and presets, plus ribbon-style Line and Trail renderers for lasers, ropes, sword swipes and projectile tails.
- **Parts:**
  - 2.7.1 Particle emitter - `Components/ParticleSystem` - shapes (Point, Circle, Sphere, Box, Cone, Edge), emission rate, burst, duration and loop, lifetime, start velocity, gravity, damping, wind influence, ground plane with bounce, and Collide with Terrain (`collideTerrain`, `Components/ParticleSystem.cpp`): with collision on, particles land on every active Terrain (moved, turned or scaled), bounce off its slope and slide down it; saved as its own `particlesterrain` record; Inspector Collision section (editor syntax-checked only, not run), property `collideTerrain`; one point test per particle per frame, so a very fast particle can pass through a thin ridge
  - 2.7.2 Particle looks - `Components/ParticleSystem` - colour gradient, size and speed curves, fade, rotation, billboard or stretch; presets Fire, Smoke, Sparks, Explosion, Rain, Snow, Magic, Dust
  - 2.7.3 Motion modules - `Components/ParticleSystem` - noise, orbit, vortex, inherited velocity, local space
  - 2.7.4 Line renderer - `Components/LineRenderer` - camera-facing or flat ribbons; points from the Inspector, a script or a Spline; stretch or tile texture
  - 2.7.5 Trail renderer - `Components/LineRenderer` - fading ribbon behind a moving object; saved through the component serializer registry
  - 2.7.6 How they are drawn - built on the CPU each frame and drawn over the 3D image by the player and editor, depth-sorted; hide behind geometry where scene depth is known (software path, or a half-resolution depth pass in the player and the GPU editor view)
- **Code:** `Components/ParticleSystem`, `Components/LineRenderer`
- **API:** `ParticleSystem`, `ParticleSystem::ApplyPreset`, `ParticleGradient`, `ParticleCurve`, `LineRenderer`, `TrailRenderer`, `BuildRibbon`, script `emit`
- **Editor:** Particle System, Line Renderer and Trail Renderer inspector sections (editor/main.cpp); spline-driven lines in 2.11
- **Docs:** `docs/rendering/particles.md`, `docs/engine/splines.md` (lines and trails), `docs/rendering/rendering.md` (weather)
- **Tests:** `particles_fx`, `particles_serialize`, `particles_tilemap`, `world_edges`
- **Status:** Works - read the `ParticleSystem.hpp` fields and `LineRenderer.hpp` header; tests cover gradients, curves, presets and scene round trips; `world_edges` covers Max Particles changed while it runs (the pool now follows it; before, it kept the size it had at the start), a burst far larger than the pool (one walk over the pool, was 26 s for 100 million), an infinite or enormous emission rate (filled the pool once and returns; an infinite rate used to hang the frame) and a NaN lifetime or size (such particles used to live forever)
- **Open:** particles are CPU billboards drawn by the host, not GPU particles; lines and trails are unlit and cast no shadows; one Line and one Trail renderer per object; an object with two Particle Systems (a scene file or a script can make one; Add Component allows one) saves and simulates both, but the editor and the player draw only the first (`GetComponent` in the four draw loops of `editor/main.cpp` and `player/main.cpp`); particles do not collide with meshes, colliders or water
- **Related:** 2.4, 2.11, 38.10, 11.7
- **Words:** particles, ParticleSystem, emitter, burst, gradient, trail, TrailRenderer, LineRenderer, laser, rope, ribbon, fire, smoke, sparks, explosion, rain, lines, trails
- **Seen as:** particles show through walls in the editor; a trail looks flat or jagged; an explosion preset is too slow; too many particles tank the frame rate
- **Ask me like:** "Add an ember preset that rises and flickers"; "Make the sword trail fade faster"

### 2.8 Culling and skinning
- **System:** Culling and LOD - Skips drawing what the camera cannot see and draws simpler versions of far-away things, so big scenes stay fast.
  Home: `engine/include/okay/Optimize/`. Entry: `engine/include/okay/Optimize/LodGroup.hpp`. Also called: LOD, LODGroup, occlusion culling, batching, MeshSimplify.
- **What:** Skip what the camera cannot see before any draw work, and let the GPU deform skinned characters when it can.
- **Parts:**
  - 2.8.1 Frustum culling - `Render/Culling` - `Frustum`, a world bounding sphere per object (cached local bounds for static meshes, bone positions for skinned ones), `CulledByFrustum`
  - 2.8.2 Culling switch and counters - `Render/Culling` - `FrustumCullingEnabled()` to debug a missing object, `LastCullCounters()` drawn/culled counts for the Stats panel
  - 2.8.3 GPU skinning - `Render/GLRenderer`, `Render/D3D11Renderer` - bone rows in the vertex shader (`SupportsGpuSkinning`, `GpuSkinJoints`); otherwise the CPU builds the pose (SkinnedMesh is 5.5)
  - 2.8.4 LOD, occlusion, batching - `Components/LODGroup`, `Components/Occluder`, `Render/Visibility`, `Optimize/LodGroup`, `OcclusionBuffer`, `BatchPlanner` - the renderers ask Visibility before they draw: LOD Group picks a mesh per camera, Occluder components hide what is behind them, static and small dynamic batches merge where the pixels stay the same
- **Code:** `Render/Culling`, `Optimize/LodGroup`, `Optimize/OcclusionBuffer`, `Optimize/BatchPlanner`, `Optimize/MeshSimplify`
- **API:** `Frustum`, `CulledByFrustum`, `FrustumCullingEnabled`, `LastCullCounters`, `GLRenderer::SupportsGpuSkinning`
- **Editor:** Stats panel (drawn, frustum-culled, occluded, LOD level, batches, draws saved); LOD Group and Occluder inspectors; Scene view distance rings on a selected LOD Group
- **Docs:** `docs/engine/optimization_tools.md`
- **Tests:** `frustum_culling`, `visibility_lod`, `optimize_simplify`, `optimize_occlusion`, `optimize_batching`
- **Status:** Works - frustum culling, GPU skinning (OpenGL and Direct3D 11), and the LOD / occlusion / batching pass are in the renderers. A scene with no LOD Group and no Occluder still draws every visible mesh; batching only merges opaque, unskinned, non-scrolling meshes that share a material
- **Open:** streaming is still area 27.4 (World Streamer does not use StreamingGrid); software and Direct3D 12 skinning stays on the CPU; occlusion is a CPU buffer and can cost more than it saves when few triangles are hidden
- **Related:** 27, 5.5, 38.2, 38.3, 2.10
- **Words:** culling, frustum culling, frustum, bounding sphere, GPU skinning, LOD, occlusion, culled objects, draw calls
- **Seen as:** low FPS in big scenes; objects vanish at screen edges; a skinned character is slow
- **Ask me like:** "Turn culling off to see if my mesh is being culled"; "Why do skinned characters cost so much in the software renderer?"

### 2.9 Fonts and images
- **System:** Fonts and Images - Loads pictures and fonts and turns text into letters on screen, falling back to system fonts for other alphabets.
  Home: `engine/include/okay/Graphics/`, `engine/src/Graphics/`. Entry: `engine/include/okay/Graphics/Font.hpp`. Also called: Font, TtfFont, Image, system fonts, font fallback.
- **What:** The CPU side of text and pictures: the built-in 8x8 font, TrueType fonts baked into glyph atlases, the compiled-in UI font, the RGBA image buffer, and the world and screen text component.
- **Parts:**
  - 2.9.1 Bitmap font - `Graphics/Font` - `Font8x8` built-in hand-drawn 8x8 monochrome ASCII glyphs (printable 32-126), `Pixel`, `MeasureWidth`
  - 2.9.2 TrueType fonts - `Graphics/TtfFont` - glyph atlas over our own reader, any glyph baked on first use, atlas grows, `Generation()` tells hosts to re-upload
  - 2.9.6 TrueType reader and rasterizer - `Graphics/TtfRaster` - our own sfnt parser (TrueType, OpenType CFF, .ttc; cmap 0/4/6/12/13; glyf with composite glyphs; CFF Type 2 charstrings incl. CID-keyed) and exact-area anti-aliased glyph rasterizer, unhinted; every read bounds-checked so a corrupt font loads as nothing instead of crashing; `ShelfPacker` and `BakeGlyph` for atlases
  - 2.9.3 Built-in UI font - `Graphics/EmbeddedFont` - the launcher's Roboto font compiled in as base85
  - 2.9.4 Images - `Graphics/Image`, `Core/ImageDecode` - RGBA8 buffer, load PNG (every colour type and depth, Adam7, tRNS), JPG (baseline and progressive, any chroma subsampling, restart markers), BMP, TGA, GIF (first frame, or every frame with `DecodeGifFrames`: the frame count is read first, the decoder's live memory is at most 512 MiB, and an animation claiming more than 4096 bytes of frames per byte of file over 64 MiB is refused), HDR and PSD (and PNM P1 to P6, `DecodePnm`, routed from `DecodeImage` only with `OKAY_OWN_PNM=1`, off by default) through our own decoder (`Core/ImageDecode`, no third-party code; also the launcher's cover art, avatars and chat images), save PNG through our own encoder (`Core/ImageWrite`), get/set pixel, fill
  - 2.9.5 Text component - `Components/TextRenderer` - world or screen-space text, pixel size, alignment, wrap, auto size, shadow, outline, gradient, typewriter reveal, localized text
- **Code:** `Graphics/Font`, `Graphics/TtfFont`, `Graphics/TtfRaster`, `Graphics/EmbeddedFont`, `Graphics/Image`, `Core/ImageDecode`, `Components/TextRenderer`
- **API:** `Font8x8`, `TtfFont`, `ttf::Face`, `ttf::BakeGlyph`, `Image::Load`, `Image::Save`, `DecodeImage`, `DecodeGifFrames`, `GetImageInfo`, `Inflate`, `TextRenderer`, `TextRenderer::text`, `TextRenderer::screenSpace`
- **Editor:** Text Renderer inspector section; Lighting panel "UI Font" (editor/main.cpp); thumbnails in the Project panel
- **Docs:** `docs/engine/localization.md` (Fonts section)
- **Tests:** `text`, `ttf`, `ttf_rasterizer`, `image`, `image_decoder`, `loaders_robust`, `image_hostile`
- **Status:** Works - read the headers; the `ttf` test skips its assertions when no system font exists, so CI proves less than a dev box; `ttf_rasterizer` needs no system font (synthetic fonts, DejaVu from assets/fonts, fuzzed files)
- **Open:** fonts are not hinted, and kerning, GPOS/GSUB, variable fonts and colour or bitmap glyph tables (emoji fonts like Noto Color Emoji) are not read; where two contours of one glyph overlap, the overlap's anti-aliased edge pixels can be a little too dark; right-to-left scripts draw unshaped and left to right (docs/engine/localization.md); the pixel font is ASCII only; no signed-distance-field text; `loaders_robust` also guards an image header claiming gigapixels; the decoder refuses CMYK/YCCK, arithmetic-coded and 12-bit JPEGs, RLE BMPs and PNM/PIC files, and fails on truncated data instead of showing the part it got; JPEG pixels match libjpeg to within 3 levels, not bit for bit; a real animated GIF of tiny changes on a large canvas (over 64 MiB of frames, more than 4096:1) is refused by the plausibility rule
- **Related:** 1.8, 12.2, 2.2, 1.12
- **Words:** font, TTF, OTF, TTC, TrueType, OpenType, CFF, glyf, cmap, rasterizer, TtfFont, TtfRaster, Font8x8, bitmap font, Image, PNG, JPEG, progressive JPEG, GIF decode, image decoder, inflate, texture load, TextRenderer, text, glyph atlas, Roboto, fonts
- **Seen as:** letters show as boxes; text is blurry at some sizes; a huge image crashes the load; text sits in the wrong corner
- **Ask me like:** "Use a pixel font for the score and Roboto for the menu"; "Why does Japanese text fall back to another font?"

### 2.10 Cameras and the camera stack
- **System:** Cameras - The cameras that film the scene: lens, shake and zoom, follow cameras for first and third person, and shot cameras that blend between views.
  Home: `engine/include/okay/Components/Camera.hpp`. Entry: `engine/include/okay/Components/Camera.hpp`. Also called: Camera, camera stack, virtual cameras, Cinemachine, camera shake.
- **What:** The Camera component (lens, viewport, culling mask, clear mode, framing effects), how a scene picks its main camera, and the camera stack that several cameras can form.
- **Parts:**
  - 2.10.1 Camera component - `Components/Camera` - orthographic or perspective, field of view (vertical or horizontal), physical lens (focal length, sensor), near and far, viewport rect, culling mask, clear flags, depth
  - 2.10.2 Projection and picking - `Components/Camera` - `ViewMatrix`, `ProjectionMatrix`, `WorldToScreen`, `WorldToViewportPoint`, `ScreenPointToRay`, `ScreenToWorldPoint`, `IsPointVisible`
  - 2.10.3 Framing and effects - `Components/Camera` - zoom, dutch angle, lens shift, pixel-perfect snap, trauma-based `Shake`, `ZoomTo`, `ignoreObject` for first person
  - 2.10.4 Main camera choice - `Scene/EnsureCamera`, `Scene/Scene` - `PickRenderCamera` (Main flag, then depth), `ResolveMainCamera` every frame, `EnsureSceneCamera` creates one when missing
  - 2.10.5 Camera stack - `Render/CameraStack`, `Scene/Scene`, `Components/Camera` - `CameraStack()` lowest depth first; the editor Game view (including Play) and the player composite every camera's viewport, culling mask and clear mode. One camera keeps the previous full-frame path. A named Render Target keeps that camera's picture (`CameraTarget`)
- **Code:** `Components/Camera`, `Scene/EnsureCamera`
- **API:** `Camera`, `Camera::ScreenPointToRay`, `Camera::Shake`, `Scene::CameraStack`, `Scene::ResolveMainCamera`, `PickRenderCamera`, `EnsureSceneCamera`
- **Editor:** Camera inspector, Camera Preview window and the Game view (editor/main.cpp, 15.3)
- **Docs:** `docs/gameplay/cameras.md`
- **Tests:** `camera_cine`, `worldtoscreen`, `scene_cameras`, `layers_tags_cameras`, `camera_stack`, `core_audit`
- **Status:** Works - one camera still renders exactly as before; two or more are drawn lowest depth first into their rectangles in the Game view, Play mode and the player. Depth Only and Don't Clear overlay the cameras underneath. The Inspector lists all four clear flags and can add an overlay camera
- **Open:** a 2D orthographic scene still draws sprites from the main camera only; overlay cameras do not depth-test against the cameras under them (they draw on top inside their rectangle)
- **Related:** 4.6, 4.7, 4.8, 1.3, 2.8, 12.6
- **Words:** Camera, main camera, camera stack, depth, clear flags, culling mask, viewport rect, split screen, minimap camera, field of view, orthographic, perspective, screen shake, pixel perfect, ScreenPointToRay, WorldToScreen
- **Seen as:** the wrong camera renders after switching; a minimap camera draws over the main view or replaces it; picking rays miss; UI labels do not line up with objects; the camera shakes forever
- **Ask me like:** "Add a picture-in-picture rear-view camera"; "Shake the camera on explosions, fading out in half a second"

### 2.11 Splines and spline tools
- **System:** Splines - Curves you draw in the scene that objects follow and that meshes, lines and rows of objects are built along.
  Home: `engine/include/okay/Components/Spline.hpp`, `engine/include/okay/Components/SplineMesh.hpp`. Entry: `engine/include/okay/Components/Spline.hpp`. Also called: Spline, SplineMesh, curves, spline paths.
- **What:** A curve you draw in the Scene view that followers, meshes, instancers and lines build on, with the editor overlay for editing it.
- **Parts:**
  - 2.11.1 Spline curve - `Components/Spline` - cubic Bezier or Catmull-Rom, open or closed, per-point roll and width/height scale, arc-length table, rotation-minimising frames
  - 2.11.2 Spline follower - `Components/Spline` - moves an object along a spline (platforms, carts, patrols, dolly cameras)
  - 2.11.3 Spline mesh and instancer - `Components/SplineMesh` - sweep a profile or bend a source mesh along the curve (roads, pipes, fences); place objects, prefabs or built-in meshes at a spacing
  - 2.11.4 Editor tools - `editor/SplineTools.*` - Scene-view overlay (points, handles, insert, delete, snap to ground), inspectors, GameObject > Effects > Spline, live rebuild of meshes and trail previews
  - 2.11.5 Lines and trails on splines - see 2.7.4 and 2.7.5
- **Code:** `Components/Spline`, `Components/SplineMesh`, `editor/SplineTools.*`
- **API:** `Spline`, `SplineFollower`, `SplineMesh`, `SplineInstancer`, `DrawSplineOverlay`
- **Editor:** GameObject > Effects > Spline; Scene-view spline overlay and the Spline, Follower, Spline Mesh and Instancer inspectors (`editor/SplineTools.*`)
- **Docs:** `docs/engine/splines.md`
- **Tests:** `splines`
- **Status:** Works - read the headers and `SplineTools.hpp`; the 928-line `splines` test covers evaluation, arc length, frames, editing, meshes, instancers and scene records
- **Open:** clicking a Catmull-Rom curve inserts a point that changes the shape slightly (only Bezier splits are exact); a road has no underside (the end caps close the ends; turn Cap Ends off to keep them open); one Spline per object (docs/engine/splines.md); dolly cameras use `DollyPath` (4.8), not this
- **Related:** 2.7, 4.8, 15.3, 29
- **Words:** spline, Bezier, Catmull-Rom, SplineFollower, SplineMesh, SplineInstancer, road, fence, rail, path, curve, handle, dolly, splines
- **Seen as:** the curve flips or twists; a road has gaps; instanced trees float above terrain; points cannot be picked in the Scene view
- **Ask me like:** "Lay a road along this spline and put lamp posts every 10 metres"; "Make the cart follow the path at constant speed"

### 2.12 Sprites and sprite animation
- **System:** Sprites and Tilemaps - Flat 2D pictures in the world, sprite sheets cut into frames, flip-book animation and levels painted from tiles.
  Home: `engine/include/okay/Components/SpriteRenderer.hpp`, `engine/include/okay/Components/Tilemap.hpp`. Entry: `engine/include/okay/Components/SpriteRenderer.hpp`. Also called: SpriteRenderer, SpriteAnimator, Tilemap, 2D sprites, sprite sheets.
- **What:** The 2D drawing components: textured, tinted, sorted sprites and flip-book animation from frame lists or sprite-sheet rows.
- **Parts:**
  - 2.12.1 Sprite renderer - `Components/SpriteRenderer` - size, colour, texture, sub-region (`uvMin`, `uvMax`), sorting layer and order, flipX and flipY, smooth or pixel filter, console glyph
  - 2.12.2 Sprite animator - `Components/SpriteAnimator` - frame list or atlas columns/rows, named clips (`SpriteClip`), `Play(name)`, fps and loop
  - 2.12.3 Sorting layers - `Components/SpriteRenderer` - `SortKey` = sorting layer then order, drawn back to front
- **Code:** `Components/SpriteRenderer`, `Components/SpriteAnimator`
- **API:** `SpriteRenderer`, `SpriteRenderer::sortingLayer`, `SpriteAnimator`, `SpriteClip`, `SpriteAnimator::Play`
- **Editor:** Sprite Renderer and Sprite Animator inspector sections (editor/main.cpp); 2D Scene view (15.3)
- **Docs:** -
- **Tests:** `spriteanim`, `2d_expand`
- **Status:** Works - read both headers; `2d_expand` also covers 2D physics, so part of it belongs to 6.1
- **Open:** no dedicated doc page; no sprite atlas importer or slicing tool in the editor (the `okay-texture atlas` command packs sprites, 2.13); sprite masks and 9-slice are UI-only (12.2)
- **Related:** 2.10, 2.6, 6.1, 12.2
- **Words:** sprite, SpriteRenderer, SpriteAnimator, sprite sheet, atlas, flip book, sorting layer, order in layer, flipX, pixel art
- **Seen as:** sprites draw in the wrong order; pixel art looks blurry; a sprite animation plays the wrong row; a character faces the wrong way
- **Ask me like:** "Put the player in front of the foreground trees"; "Play the 'run' row of this sprite sheet at 12 fps"
### 2.13 Texture library (mipmaps, atlases, compression, streaming)
- **System:** Materials and Textures
- **What:** Our own CPU texture processing: build mip chains, pack sprite atlases, convert pixel formats, block-compress to BC1/BC2/BC3/BC4/BC5, write and read DDS files, load .dds textures in every renderer (OpenGL keeps them compressed when the GPU can), and plan which mip of each texture to keep for a memory budget.
- **Parts:**
  - 2.13.1 Mip chains - `Texture/Mip` - box, Lanczos3 and Kaiser filters for any size; sRGB-correct (linear light), alpha-weighted, alpha-test coverage kept, normal-map renormalising
  - 2.13.2 Atlas packer - `Texture/Atlas` - MaxRects with padding, bleed, optional rotation, power-of-two growth, composed sheet and a stable JSON manifest
  - 2.13.3 Format conversion - `Texture/Convert` - RGBA8, BGRA8, RGB565, RGBA16F, R8; premultiply and un-premultiply; swizzle and channel pack; half-float conversion
  - 2.13.4 Block compression - `Texture/Bc`, `Texture/Dds` - our own BC1/BC2/BC3/BC4/BC5 encoder (axis fit, least squares, endpoint search) and CPU decoder; DDS writer and reader with mips (DXT1/3/5, ATI1/ATI2, DX10 header, masked RGB/BGR/565/luminance/alpha pixels, read budgets, every-truncation tested)
  - 2.13.7 GPU upload - `Render/GpuTexture`, `GLRenderer::TextureFor`, `GetCachedTexture` - a `.dds` texture path, or a picture whose import settings say `compress` (compressed copy kept in `<folder>/.okaycache/<file>.dds`, with a `.src` record of the picture and .okaymeta size and hash, so any change rebuilds it; a game build ships the copy next to the picture, so the game does not encode at run time), goes to OpenGL with `glCompressedTexImage2D` when S3TC / RGTC is present (asked at run time), else decoded on the CPU; Direct3D 11 uploads the BC blocks as they are (`D3D11Renderer::TextureFor`); the software and Direct3D 12 renderers draw .dds decoded to RGBA8; `OKAY_GL_NO_COMPRESSED=1` forces the CPU decode in OpenGL
  - 2.13.5 Streaming planner - `Texture/Stream` - camera distance and screen size to the mip each texture needs, memory budget through `Optimize/MipBudget`, load and evict amounts, per-frame upload cap
  - 2.13.6 Tool and editor hook - `tools/okaytexture`, `Texture/Export`, `editor/ProjectPanelUI` - `okay-texture mips|compress|atlas|info|stream`; Project panel Import Settings "Generate mipmaps / compress" writes a .dds beside the picture
- **Code:** `Texture/Texture`, `Texture/Mip`, `Texture/Atlas`, `Texture/Convert`, `Texture/Bc`, `Texture/Dds`, `Texture/Stream`, `Texture/Export`, `Render/GpuTexture`, `tools/okaytexture/main.cpp`, `examples/texture`, `editor/ProjectPanelUI.cpp`
- **API:** `okay::tex::GenerateMips`, `GenerateMipsRGBA8`, `MipOptions`, `PackAtlas`, `ComposeAtlasRGBA8`, `AtlasToJson`, `ConvertPixels`, `FloatToHalf`, `EncodeBc`, `DecodeBc`, `WriteDds`, `ReadDds`, `ReadDdsFile`, `DdsLimits`, `DdsLevelToRGBA8`, `gputex::Source`, `gputex::PlanUpload`, `GLRenderer::UploadInfo`, `StreamPlanner`, `BuildDdsFile`, `BuildAtlasFiles`; command `okay-texture`
- **Editor:** Project panel > picture > Import Settings > "Compress on the GPU" and "Generate mipmaps / compress"
- **Docs:** `docs/rendering/own_texture_library.md`
- **Tests:** `tex_mip`, `tex_atlas`, `tex_convert`, `tex_bc`, `tex_dds_stream`, `tex_dds_load`, `gl_dds_upload` (Mesa llvmpipe through EGL; SKIPPED without one)
- **Status:** Partial - the library, tool, editor button, .dds loading and the OpenGL compressed upload work and are tested on Linux (the GL part on Mesa llvmpipe); nothing was run on Windows or a real GPU
- **Open:** the Direct3D 11 BC upload was only compile-checked with MinGW, never run; Direct3D 12 draws .dds decoded to RGBA8 (no BC upload there); the editor's "Compress on the GPU" box was not compiled or clicked in this change; cube maps, volumes, arrays and BC6H/BC7 .dds files are refused; BC6H/BC7/ASTC/ETC are not written; no streaming loop in the renderer (the planner is the planning half); the glyph `ShelfPacker` is separate on purpose; Windows build and editor section unverified
- **Related:** 2.9, 2.12, 20.4, 27.4
- **Words:** mipmap, mip chain, atlas, sprite sheet, texture packer, BC1, BC3, BC4, BC5, DXT1, DXT5, DDS, texture compression, Lanczos, Kaiser, premultiplied alpha, alpha coverage, normal map mips, half float, RGB565, texture streaming, okay-texture
- **Seen as:** far away textures shimmer or look dark; cut-out leaves vanish in the distance; I need one picture from many sprites; textures use too much memory
- **Ask me like:** "Pack these icons into one atlas"; "Make mipmaps and compress this picture"; "Which mip does this texture need at 100 pixels?"

