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
| `Camera`             | `okay::Camera` (orthographic)                       |
| `SpriteRenderer`     | `okay::SpriteRenderer`                              |
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
- **Utilities** — seedable `Random`, a typed `EventBus`, `Rect`/`Bounds`, extra
  `Mathf` (InverseLerp, SmoothDamp, LerpAngle…), and `Scene::FindObjectsOfType<T>`.
- **Multiplayer** — cross-platform UDP networking (`NetworkManager`) with a
  client/server join handshake and snapshot state sync.
- **Visual scripting** — a node-graph runtime (events, math, branches,
  variables, transform actions) you build in code or load from text.
- **Text scripting** — a built-in language (works everywhere) plus optional
  **Lua** and **C#** backends behind the same `IScriptVM` interface.
- **Steam** and **PlayFab** integrations — full API surfaces with in-memory
  simulation backends by default; real Steamworks/REST backends behind flags.
- **Self-updating launcher** that pulls the latest from GitHub, rebuilds, runs.
- **Desktop GUI editor** (Dear ImGui + SDL2 + OpenGL) — Unity-style hierarchy,
  inspector, scene viewport, and Play/Stop, with scene save/load. See
  [`docs/editor.md`](docs/editor.md).
- **Scene serialization** — save/load scenes (and the hierarchy) to readable
  `.okayscene` text files via `SceneSerializer`.
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
  src/                        # implementations
sandbox/                      # example "solar system" game
tests/                        # dependency-free unit tests (CTest)
```

## How to run it

You have three options, easiest first.

### 1. Just play (prebuilt Windows binary)

Download **`dist/OkaySpace.exe`** and double-click it. No build required.
(`dist/OkaySpace-Launcher.exe` is the self-updating version — see below.)

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

// A script, just like a MonoBehaviour.
class Spinner : public Behaviour {
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
    player->AddComponent<Spinner>();

    app.Run(scene); // pumps Time, Input, Update, and Render every frame
}
```

## Extending it

The renderer is the obvious next layer: implement `okay::IRenderer`'s five
methods against OpenGL/SDL and hand it to `Application::SetRenderer(...)` — the
scene, components, and scripts need no changes. Other natural additions:
physics/colliders as components, an asset/resource system, and an event/message
bus.

## License

MIT — see `LICENSE`.
