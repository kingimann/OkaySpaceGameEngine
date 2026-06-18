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
- **Starter templates** — `Templates::Platformer` / `TopDown` / **`CoinCollector`**
  (a complete, playable game: WASD player, follow camera, trigger-collected
  coins, and a score HUD) build component-wired scenes from the New Project flow.
- **Utilities** — seedable `Random`, a typed `EventBus`, `Rect`/`Bounds`, extra
  `Mathf` (InverseLerp, SmoothDamp, LerpAngle…), and `Scene::FindObjectsOfType<T>`.
- **Multiplayer** — cross-platform UDP networking (`NetworkManager`) with a
  client/server join handshake and snapshot state sync.
- **Visual scripting** — a node-graph runtime (events, math, branches,
  variables, transform actions) you build in code or load from text.
- **Text scripting** — a built-in language (works everywhere) plus optional
  **Lua** and **C#** backends behind the same `IScriptVM` interface. OkayScript
  has `for`/`while`/`if`, functions, strings, and a deep builtin set: input
  (keyboard+mouse), `spawn`/`destroy`, `load_scene`, physics `raycast_hit`/
  `overlap`, component control (`set_text`/`set_color`/`set_texture`/`play_sound`),
  `prefs_*` save data, and `on_trigger()`/`on_collision()` event handlers.
  See [`docs/scripting.md`](docs/scripting.md).
- **Steam** and **PlayFab** integrations — full API surfaces with in-memory
  simulation backends by default; real Steamworks/REST backends behind flags.
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
- **Online services built into the engine & editor** — Steam (achievements/stats),
  PlayFab (login/leaderboards), and multiplayer (host/join) live in the editor's
  **Services** panel. Ship on Steam via [`docs/steam_release.md`](docs/steam_release.md).
- **Scene serialization** — save/load scenes (and the hierarchy) to readable
  `.okayscene` text files via `SceneSerializer`.
- **Build games to a standalone `.exe`** — the editor's **Build Game** (Ctrl+B)
  writes your scene next to a tiny SDL2 **player runtime** (`player/`), renamed
  `<Game>.exe`. The result is a double-clickable game (sprites in 2D, shaded
  meshes in 3D, audio, input) you can ship — see [`docs/editor.md`](docs/editor.md).
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

## Documentation

- [`docs/making_a_game.md`](docs/making_a_game.md) — **start here**: empty editor → shipped game.
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
