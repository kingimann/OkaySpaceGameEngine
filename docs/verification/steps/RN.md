# Hand-written verification rows: rendering and post-processing

### V-RN-001 Look presets (sky, fog, post-processing in one go)
- **Map:** 2, 26
- **Where:** editor, Lighting panel
- **Steps:**
  1. Open **View > Panels > Lighting** and the **Post-processing** section.
  2. Open the **Look Preset** combo and pick a preset (for example Sunset or Night).
  3. Press **Reset** next to it.
- **See:** the Scene and Game views change sky, fog and post-processing together; Reset returns to plain colour sky, no fog, no post. The line under the preset says "GPU post chain: on" or "off".
- **Partial:** the Scene view draws through SDL's 2D renderer, so what the GPU chain looks like may differ from a built game (map 38).

### V-RN-002 Bloom, tonemap, exposure, vignette
- **Map:** 2, 26
- **Where:** editor, Lighting panel
- **Steps:**
  1. In **Post-processing** tick **Bloom** and drag **Exposure**.
  2. Change **Tonemap** in the combo, and raise **Vignette**.
  3. Untick them again.
- **See:** bright areas glow with Bloom; the picture darkens or lightens with Exposure; corners darken with Vignette.
- **Partial:** none known.

### V-RN-003 Ambient occlusion and depth of field
- **Map:** 2
- **Where:** editor, Lighting panel
- **Steps:**
  1. In **Post-processing** tick **Ambient Occlusion (SSAO)** and **Depth of Field**.
  2. Press Play on a scene with several objects.
- **See:** contact shadows in corners; far or near objects blur with Depth of Field.
- **Partial:** these need the GPU post chain; the line "GPU post chain" says whether it is on.

### V-RN-004 Fog
- **Map:** 2
- **Where:** editor, Lighting panel
- **Steps:**
  1. Open the **Fog** section, change **Fog Mode**, **Fog Color**, **Fog Start** and **Fog End**.
- **See:** distant objects fade into the fog colour.
- **Partial:** none known.

### V-RN-005 Lights
- **Map:** 2
- **Where:** editor, GameObject menu
- **Steps:**
  1. Add a **Directional Light**, **Point Light** and **Spot Light** from **GameObject > Light**.
  2. Move them and change their colour in the **Inspector**.
- **See:** objects are lit from the light's side; moving the lights changes the shading.
- **Partial:** none known.

### V-RN-006 Terrain and water
- **Map:** 29, 2
- **Where:** editor, GameObject menu
- **Steps:**
  1. Open **GameObject > 3D Object** and add **Terrain**, then **Water**.
  2. Select the terrain and read its Inspector section.
- **See:** a terrain and a water plane appear in the Scene view.
- **Partial:** map area 29 (terrain tools) is mostly planned; sculpting tools may be missing.
- **Grounded:** no

### V-RN-007 Particles
- **Map:** 2
- **Where:** editor, GameObject menu
- **Steps:**
  1. Choose **GameObject > Create Particle System**.
  2. Press Play.
- **See:** particles are emitted from the object.
- **Partial:** none known.

### V-RN-008 2D sprite and tilemap
- **Map:** 43
- **Where:** editor, GameObject menu
- **Steps:**
  1. Choose **File > New 2D Scene**.
  2. Choose **GameObject > Create Sprite** and **GameObject > Create Tilemap**.
- **See:** a sprite and a tilemap object in the 2D Scene view.
- **Partial:** map area 43 is proposals; expect missing 2D tools.

### V-RN-009 Display quality switches
- **Map:** 38, 37
- **Where:** editor, View menu
- **Steps:**
  1. Open **View > Rendering & Performance**.
  2. Toggle **3D Anti-aliasing (2x)**, **VSync** and **Auto performance**.
- **See:** edges smooth with AA on; the frame rate cap follows VSync.
- **Partial:** which render backend a Windows build uses is set in the build (map 38); this row only covers the editor.
