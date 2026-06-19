# OkaySpaceGameEngine

A small, **Unity-inspired game engine written in modern C++ (C++17)**. It
borrows Unity's core mental model — a scene full of `GameObject`s, each composed
of `Component`s, with scripts that hook into an `Awake` / `Start` / `Update`
lifecycle — and implements it from scratch with zero third-party dependencies.

It ships with a **console (ASCII) renderer**, so the whole engine builds and
runs anywhere, including headless servers and CI, while keeping the renderer
behind an interface so a real GPU backend (OpenGL/Vulkan) can be dropped in.

```
+----------------------------------------------------------------------+
|                                        OO                            |
|                                       oo  AA                         |
|                                                                      |
|                                  @@                                  |
|                                                                      |
|                                  **                                  |
+----------------------------------------------------------------------+
```
*(The sandbox: `@` sun, `O` orbiting planet with a parented `o` moon, `*` inner
planet, `A` player ship.)*

## Why it feels like Unity

| Unity concept        | OkaySpace equivalent                                 |
|----------------------|-----------------------------------------------------|
| `GameObject`         | `okay::GameObject`                                  |
| `Component`          | `okay::Component`                                   |
| `MonoBehaviour`      | `okay::Behaviour` (alias of `Component`)            |
| `Transform`          | `okay::Transform` (full parent/child hierarchy)    |
| `Scene`              | `okay::Scene`                                       |
| `Camera`             | `okay::Camera` (orthographic + perspective)         |
| `SpriteRenderer` / `MeshRenderer` | `okay::SpriteRenderer` / `okay::MeshRenderer` |
| `PlayerPrefs`        | `okay::Prefs`                                       |
| Prefab `.prefab`     | `okay::SceneSerializer` `.okayprefab`               |
| `Vector2/3`, `Quaternion`, `Matrix4x4` | `okay::Vec2/Vec3/Quat/Mat4`      |
| `Mathf`, `Time`, `Input`, `Color` | same names, same spirit               |
| `AddComponent<T>()`, `GetComponent<T>()` | identical templated API       |
| `Awake/Start/Update/LateUpdate/OnDestroy` | same message order           |

## Features

- **Entity–Component model** with templated `AddComponent<T>()` / `GetComponent<T>()`.
- **Lifecycle** driven by the scene: `Awake` → `Start` → `Update` → `LateUpdate`
  → `OnRender` → `OnDestroy`, matching Unity's ordering.
- **Transform hierarchy** with local/world position, rotation, scale, and
  correct world-matrix composition (TRS) through parents.
- **Math library**: `Vec2`, `Vec3`, `Vec4`, `Quat`, `Mat4`, `Mathf` — vectors,
  quaternion rotation/slerp, matrix transforms, ortho/perspective projection.
- **Pluggable renderer** (`IRenderer`) with a built-in `ConsoleRenderer` that
  rasterizes quads into an ASCII frame buffer with depth sorting.
- **Game loop** with frame pacing, delta time, smoothed FPS, and a frame cap.
- **Input**: non-blocking terminal keyboard polling (`GetKey/Down/Up`,
  `AxisWASD`), gracefully no-ops when not attached to a TTY.
- **2D physics** — `Rigidbody2D` + box/circle colliders, a `Physics2D` world
  (gravity, drag, impulse resolution) and `OnCollision2D` / `OnTrigger2D`
  callbacks, stepped automatically by the scene.
- **Scheduler** — `Invoke`, `InvokeRepeating`, and value `Tween`s per scene
  (Unity-style timed callbacks).
- **Sprites, textures & text** — `SpriteRenderer` with optional **image
  textures** (`okay::Image`, PNG/JPG/BMP via stb_image), **sprite-sheet/atlas**
  sub-regions + **`sortOrder`** layering, `SpriteAnimator` flip-book/atlas
  animation, and a `TextRenderer` using a built-in **8×8 bitmap font** (no font
  file) for HUDs and labels. The player draws shaded 3D meshes, rotated/textured
  layered sprites, tilemaps, particles, and text.
- **3D primitives** — `Mesh::Cube/Pyramid/Plane/Sphere/Cylinder`, drawn as
  shaded, back-face-culled, depth-sorted meshes via a perspective `Camera`.
- **WAV audio** — `AudioClip::LoadWAV` (8/16/24/32-bit + float, resampled) and
  `SaveWAV`; `AudioSource` clip paths load in the built game and mix live.
- **Gameplay components (no scripting needed)** — `Mover` (constant velocity),
  `Spinner` (constant rotation), `Lifetime` (auto-destroy), and `CameraFollow`
  (smooth chase camera). All serialized and editable in the Inspector.
- **Mouse + keyboard input** — `GetKey/Down/Up`, `AxisWASD`, `MousePosition`,
  `GetMouseButton/Down/Up`, fed from the editor/player windows or a terminal.
- **A\* pathfinding** — `Pathfinding::AStar` over a grid (4/8-directional) or
  directly over a `Tilemap`, for enemy nav and click-to-move.
- **Easing & Tween** — the classic easing curve set (`Ease`) plus a one-shot
  `Tween` for game-feel animation.
- **Persistent save data** — `Prefs` (PlayerPrefs-style key/value) for high
  scores and settings; the player auto-loads/saves it.
- **Starter templates** — `Platformer` / `TopDown` / **`CoinCollector`** (a
  complete, playable game) / **`MainMenu`** (UI panel + title + Start button) /
  **`Inventory`** (drag-and-drop bag with slots) build component-wired scenes
  from the New Project flow.
- **Utilities** — seedable `Random`, a typed `EventBus`, `Rect`/`Bounds`, extra
  `Mathf` (InverseLerp, SmoothDamp, LerpAngle…), and `Scene::FindObjectsOfType<T>`.
- **Multiplayer — host your own server, in ~5 lines** — cross-platform UDP
  networking (`NetworkManager`): a client/server join handshake, snapshot state
  sync, player **names** + a server **roster**, peer **joined/left** callbacks,
  **broadcast** and **targeted** (`SendTo`) custom messages, and a script API so
  any player can host (`net_host(port)`) or join (`net_join(ip, port)`) and
  exchange events (`net_send` / `net_poll`). There's a **Multiplayer starter
  template** (press **H** to host, **J** to join). See the API below.
- **Visual scripting** — an `ActionList` (Game-Creator-2 style:
  Trigger → Conditions → Instructions) with ~60 ops including variables/math,
  transform/physics, prefs, scene loading, **and networking** (`net_host`,
  `net_join`, `net_send`), plus a node-graph runtime you build in code or text.
- **Text scripting (OkayScript)** — a built-in language (works everywhere; an
  optional **C#** backend sits behind the same `IScriptVM`). Has
  `if`/`while`/`for`/**foreach**/`break`/`continue`, functions, the **ternary**
  `?:`, **arrays** and **maps**, a full **string** library, and a deep builtin
  set: input (keyboard+mouse), 2D/3D transform control, `spawn`/`destroy`,
  `load_scene` + **Scene Manager** (`load_scene_index`/`load_next_scene`),
  **networking** (`net_*`), physics `raycast_hit`/`overlap`, component control
  (`set_text`/`set_color`/`set_texture`/`play_sound`/`set_progress`),
  audio/gravity/time-scale, `prefs_*` save data, and
  `on_trigger()`/`on_collision()`/`on_click()` handlers.
  See [`docs/scripting.md`](docs/scripting.md).
- **Terrain** — a Unity-style heightmap terrain you sculpt with a brush (drag in
  the 3D view to raise, Shift to lower) or generate (Flatten / Smooth / Randomize
  / Hills), rendered as a generated mesh. Create from GameObject > 3D Object >
  Terrain; the heightmap saves with the scene.
- **Materials** — reusable surface presets (albedo, emissive, specular, texture,
  tiling, unlit, double-sided) saved as `.okaymat` assets and applied to any
  Mesh Renderer (Save/Load in the inspector, or drag a `.okaymat` from Project).
- **In-game UI like Unity** — a **Canvas** (CanvasScaler: constant-pixel or
  scale-with-screen) parents the widgets, with one **Event System** routing
  pointer input. Widgets: `UIButton` (`on_click()`), `UIPanel`, `UIImage`,
  `UISlider`, `UIToggle`, `UIProgressBar`, `UIInputField` (real OS text input —
  caret, scroll, Integer/Decimal/Password types), `UIDropdown`, `UITooltip`,
  and screen-space text. Widgets are richly customizable (rounded corners,
  borders, gradients, font scale, fills, outlines, data-bound text). A
  **Scroll View** (wheel-scrollable, clipped) and **Layout Group** make
  scrollable, auto-arranged lists — all rendered identically in built games.
- **Unity-style UI editing** — select, drag and resize any widget in the Scene
  view with anchor-correct handles; **snapping** to a pixel grid plus smart
  edge/center **alignment guides** to the canvas and siblings; arrow-key
  nudging; a **3×3 anchor preset** grid (re-anchors without moving the widget);
  Bring to Front / Send to Back, Center in Canvas, and Fill Width/Height/Canvas.
- **UI Toolkit (OkayUI markup)** — a **UIDocument** authors whole HUDs/menus as
  text: every widget type, customization keys (corner/border/gradient/font/
  align/outline/fill/…), `tooltip=`, percent sizing (`50%`), `name=` (script-
  addressable), reusable `style` classes, parameterized `define` custom widgets,
  live data **binding** (`bind="Score: {score}"`), and inline validation with
  line numbers.
- **Tweening (DOTween-style) & saves** — animate from OkayScript with
  `tween_move`/`tween_move3`/`tween_scale`/`tween_rotate`/`tween_color`/
  `tween_fade` (any easing), each with an optional **on-complete callback**;
  plus **`tween_loop_move`** (ping-pong floaters/patrols), **`tween_punch_scale`**
  / **`tween_punch_pos`** and **`tween_shake`** for game-feel "juice". And a save
  system (`save_game`/`load_game`/`save_exists` slots).
- **Drag & drop (UI *and* world items)** — make any UI widget draggable with a
  **UI Draggable** component (drop onto a **UI Drop Target**), or any world
  sprite draggable with **Draggable** (drop onto a **Drop Zone**). Both fire
  `on_drop()` / `on_receive()` (scripts read `ui_drop_source/target()` and
  `drop_source/target()`) and share rich options: lock-axis, drag threshold,
  bring-to-front, **snap-into-slot/zone** (instant inventories), return-to-start,
  **accept-tag** filtering, **grid snap** (board/tile games), drag-scale, and
  hover highlight + `on_hover_enter/exit()`. The **Inventory** starter template
  is a working drag-and-drop bag.
- **Scriptable Objects (Data Assets)** — reusable `.okaydata` files of named
  fields for item/enemy/level definitions and config (Unity's ScriptableObject).
  Create via **Project → New Data Asset**, edit fields in the editor, read from
  OkayScript with `data_num`/`data_str`/`data_has` and write with
  `data_set`/`data_save`.
- **Raycasting from script** — `raycast(ox,oy,dx,dy[,max])` (and 3D
  `raycast3(...)`) return the **name of the object hit**, with detail accessors
  `ray_hit()`, `ray_object()`, `ray_x/y()` (point), `ray_nx/ny()` (normal) and
  `ray_dist()` — for shooting, line-of-sight, ground checks and click-to-select.
- **Build for desktop, web & mobile** — one project, every target: a
  self-contained Windows `.exe`, a **WebAssembly** build via Emscripten
  (`scripts/build-web.sh`), and Android/iOS via SDL2.
  See [`docs/web_mobile.md`](docs/web_mobile.md).
- **Steam** integration — a broad API surface (achievements + **progress**,
  stats + **increment**, **leaderboards**, **Steam Cloud**, friends, overlay)
  with an in-memory simulation backend by default and the real Steamworks
  backend behind `-DOKAY_WITH_STEAM`.
- **Self-updating launcher** that pulls the latest from GitHub, rebuilds, runs.
- **Desktop GUI editor** (Dear ImGui docking + SDL2) — Unity-style **docked**
  Hierarchy / Scene / Inspector / Console / **Services** / **Script Editor**
  panels, a Play·Stop·Step·Build toolbar, a polished dark theme, a **New Project**
  flow with 2D / 3D and **playable templates**, a **2D/3D scene viewport** (orbit
  camera + shaded meshes), a searchable Hierarchy + Add Component, a filterable
  colored Console, a filesystem **asset browser** (open scenes / drop in prefabs),
  Add Component / Inspector for every component, scene save/load, **Build Game**,
  and an in-app self-updater. Ships as a single self-contained `.exe`
  (`dist/OkaySpaceEngine.exe`). See [`docs/editor.md`](docs/editor.md).
- **Online services built into the engine & editor** — Steam (achievements,
  stats, leaderboards, cloud) and multiplayer (host/join, roster, chat) live in
  the editor's **Services** panel. Ship on Steam via
  [`docs/steam_release.md`](docs/steam_release.md).
- **Scene Manager + Scenes panel** — a build list of the project's scenes with
  by-index / by-name loading, Load Next and Reload (Unity's SceneManager +
  Build Settings).
- **Prefabs** — save any GameObject (and children) as a `.okayprefab` from the
  Hierarchy right-click, drag it back in to instantiate.
- **Scene serialization** — save/load scenes (and the hierarchy, plus per-scene
  sky/ambient render settings) to readable `.okayscene` text files.
- **Unity-like Build Settings** — the editor's **Build Game** (Ctrl+B) dialog
  has product/company names, window size (+ presets), fullscreen/resizable/
  vsync, "include all project scenes", and a development-build flag. It writes
  your scene(s) + a `game.okayconfig` next to a tiny SDL2 **player runtime**,
  renamed `<Game>.exe` — a double-clickable game (2D sprites, shaded 3D meshes
  with **skybox + lighting**, audio, input) you can ship.
  See [`docs/editor.md`](docs/editor.md).
- **Core has no external dependencies** — just a C++17 compiler, CMake, threads.
  Optional backends (Lua, C#/Mono, Steam, PlayFab/libcurl) and the editor
  (SDL2/OpenGL) are opt-in.

## Project layout

```
engine/
  include/Okay.hpp            # single public header
  include/okay/Math/          # Vec2/3/4, Quat, Mat4, Mathf
  include/okay/Core/          # Application, Time, Log
  include/okay/Scene/         # Component, Transform, GameObject, Scene
  include/okay/Components/    # Camera, SpriteRenderer
  include/okay/Render/        # IRenderer, ConsoleRenderer, Color
  include/okay/Input/         # Input
  include/okay/Graphics/      # Image (stb), Font (8x8 bitmap)
  include/okay/AI/            # Pathfinding (A*)
  third_party/                # vendored single-header libs (stb, font8x8)
  src/                        # implementations
editor/                       # Dear ImGui + SDL2 desktop editor (the engine app)
player/                       # standalone SDL2 runtime that runs a built game
sandbox/                      # example "solar system" game
docs/                         # editor, scripting, Steam release guides
tests/                        # dependency-free unit tests (CTest)
```

## How to run it

You have three options, easiest first.

### 1. Open the engine (prebuilt Windows binary)

Download **`dist/OkaySpaceEngine.exe`** and double-click it — that single,
self-contained `.exe` *is* the engine: the full Dear ImGui editor. Make a scene,
press **Play**, then **File → Build Game** (Ctrl+B) to export a standalone
`<Game>.exe`. It self-updates from GitHub via **Engine → Check for Updates**.

(`dist/OkaySpace.exe` is the headless console sandbox demo;
`dist/OkaySpace-Launcher.exe` is the self-updating launcher — see below.)

### 2. The launcher (build, auto-update, and run in one step)

```bash
./launch.sh            # Linux/macOS   (first run builds the launcher)
launch.bat             # Windows
```

The launcher checks GitHub for a newer version, pulls it, rebuilds, and starts
the game. Handy flags: `--check-only`, `--no-update`, `--no-run`,
`--game <name>`, and `-- <args passed to the game>`.

### 3. Build from source with CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/bin/sandbox            # run the demo (optional arg = frame count)
cd build && ctest --output-on-failure   # run the tests
```

In an interactive terminal the demo runs until you press **Q** (move with
**WASD**); when piped/redirected it runs a fixed number of frames so it stays
headless-friendly. Or use the helper script: `./scripts/build.sh`.

Requirements: CMake ≥ 3.16 and a C++17 compiler (tested with GCC 13 and
Clang 18). The core build needs nothing else.

### The visual editor

Want a Unity-style editor window (hierarchy, inspector, scene view, Play button)?

```bash
# Debian/Ubuntu: sudo apt-get install libsdl2-dev libgl1-mesa-dev
cmake -S . -B build -DOKAY_BUILD_EDITOR=ON
cmake --build build -j
./build/bin/okay-editor
```

Full guide: [`docs/editor.md`](docs/editor.md).

### Optional backends

These are off by default to keep the core dependency-free:

```bash
cmake -S . -B build \
  -DOKAY_WITH_LUA=ON \        # real Lua scripting   (needs liblua dev)
  -DOKAY_WITH_CSHARP=ON \     # real C# scripting     (needs Mono + mcs)
  -DOKAY_WITH_PLAYFAB=ON \    # real PlayFab REST     (needs libcurl)
  -DOKAY_WITH_STEAM=ON -DSTEAMWORKS_SDK_PATH=/path/to/sdk  # real Steamworks
```

Without these flags the engine still exposes the same scripting languages and
Steam/PlayFab APIs via built-in/simulation backends.

## A taste of the API

```cpp
#include <Okay.hpp>
using namespace okay;

// A script, just like a MonoBehaviour. (There's also a built-in okay::Spinner.)
class MySpin : public Behaviour {
public:
    float speed = 90.0f; // degrees/second
    void Update(float dt) override {
        transform->Rotate({0, 0, speed * dt});
    }
};

int main() {
    Application app({/*title*/ "My Game", /*w*/ 80, /*h*/ 30});
    Scene scene("Main");

    auto* cam = scene.CreateGameObject("Camera")->AddComponent<Camera>();
    cam->orthographicSize = 8.0f;

    GameObject* player = scene.CreateGameObject("Player");
    player->AddComponent<SpriteRenderer>()->glyph = '@';
    player->AddComponent<MySpin>();

    app.Run(scene); // pumps Time, Input, Update, and Render every frame
}
```

## Multiplayer in ~5 lines (host your own server)

Multiplayer is built in — no external service, no account. Any player can host
a server on a port; others join by IP. From OkayScript:

```c
// Press H to host, J to join the machine at 127.0.0.1.
function update(d) {
  if (key_down("h")) net_host(45000);            // start a server on this PC
  if (key_down("j")) net_join("127.0.0.1", 45000); // connect to a host

  // Send your position to everyone, ~10x/sec.
  net_send("pos", x() + "," + y());

  // Receive whatever peers sent this frame.
  while (net_poll()) {
    if (net_msg_channel() == "pos") { /* net_msg_data(), net_msg_from() */ }
  }
}
```

Full script API: `net_host(port)`, `net_join(ip, port)`, `net_disconnect()`,
`net_connected()`, `net_is_server()`, `net_is_client()`, `net_id()`,
`net_peers()`, `net_name(name)`, `net_send(channel, data)`,
`net_send_to(id, channel, data)`, `net_poll()` + `net_msg_channel/data/from()`,
and **synced variables** `net_set(key, value)` / `net_get(key)` — a
server-authoritative shared store (scores, game phase) that every peer sees,
auto-synced to new joiners.

**No code at all?** Add a **Network Manager** component, set **Auto Start =
Host on Play** (or **Join**) with a port/name in the Inspector, and press Play —
the component hosts/joins on its own, broadcasting its object's Transform. The
same actions are also **visual-scripting** ops (`net_host`, `net_join`,
`net_send`, `net_set`), and the editor's **Services → Multiplayer** panel does
host/join + roster + chat. The **New Project → Multiplayer** template is a
ready-to-run example. Build the game and share `<Game>.exe` — one player hosts,
the rest join over LAN or a forwarded port.

## Documentation

- [`docs/making_a_game.md`](docs/making_a_game.md) — **start here**: empty editor → shipped game.
- [`docs/web_mobile.md`](docs/web_mobile.md) — build your game for the **web** (WASM) and **mobile**.
- [`docs/editor.md`](docs/editor.md) — the desktop editor and **Build Game**.
- [`docs/scripting.md`](docs/scripting.md) — the OkayScript language + builtins.
- [`docs/visual_scripting.md`](docs/visual_scripting.md) — the node-graph runtime.
- [`docs/steam_release.md`](docs/steam_release.md) — shipping on Steam.

## Extending it

The renderer is the obvious next layer: implement `okay::IRenderer`'s five
methods against OpenGL/SDL and hand it to `Application::SetRenderer(...)` — the
scene, components, and scripts need no changes. Other natural additions:
an asset/resource manager, sprite-sheet atlases, and an audio effects graph.

## License

MIT — see `LICENSE`.
