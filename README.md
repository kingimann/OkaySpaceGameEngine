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
- **No external dependencies** — just a C++17 compiler, CMake, and threads.

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

## Build & run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Run the demo (optional arg = number of frames; omit for the default run)
./build/bin/sandbox

# Run the tests
cd build && ctest --output-on-failure
```

Requirements: CMake ≥ 3.16 and a C++17 compiler (tested with GCC 13 and
Clang 18).

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
