# okay.js (web library with the engine's script API)

Internal record (rule 5). Feature map 18.12. Part 2 of the owner's request "build our own JS engine like three.js on top
of this engine so we can support browser games better". Part 1 is the engine's own JavaScript interpreter for game
scripts (`docs/scripting/javascript.md`, feature map 13.9, branch `claude/javascript-1`).

## What it is

okay.js is a JavaScript library for web pages, written by us: maths, a scene graph, a WebGL renderer, input and a game
loop, as plain ES modules in `web/okayjs/` with no build step and no dependencies. Two uses:

1. **The same game script in the engine and in a page.** A script written for the engine's JavaScript scripts (part 1)
   runs unchanged on okay.js for the parts both have: the same lifecycle methods, globals, type names and member names.
   okay.js runs it with the browser's own JavaScript engine; the engine runs it with okay::js.
2. **A small 3D and 2D library of our own** for web developers who want a page with a scene in it, without the engine.

## Where it sits next to the web player

The web player (feature map 18.2) is the whole engine compiled to WebAssembly: every component, physics, audio, UI,
saves, and 3D through the software rasterizer. okay.js is the light path: a page loads about 100 KB of JavaScript (30 KB
gzipped, not minified) instead of a 6 MB gzipped program, draws with WebGL on the graphics card, and runs only what the library has.
Heavy games (physics, animation, the full component set, the editor's scenes) keep using the wasm player; okay.js is
for light games, small interactive pages and script experiments. The two do not share code: okay.js does not load
the wasm player and the player does not use okay.js.

## Layout

| File | What | Written from |
| --- | --- | --- |
| `web/okayjs/okay.js` | the entry: re-exports everything | - |
| `web/okayjs/src/math.js` | Vector2, Vector3, Quaternion, Matrix4, Color, Mathf | general linear algebra; names and behaviour from `engine/include/okay/Math/` (Vec2, Vec3, Quat, Mat4, Color, Mathf) and the typed script table (`engine/src/Script/EngineApi_Core.cpp`, `EngineApi_Math.cpp`) |
| `web/okayjs/src/scene.js` | Scene, GameObject, Transform, Component, Camera, Light, MeshRenderer, SpriteRenderer | our own design; names from `Scene/Transform.hpp` and `Components/Camera.hpp`, `Light.hpp`, `MeshRenderer.hpp`, `SpriteRenderer.hpp` |
| `web/okayjs/src/geometry.js` | Mesh: Box, Sphere, Plane, Quad, FromArrays (normals worked out when missing) | basic geometry |
| `web/okayjs/src/material.js` | LitMaterial, UnlitMaterial, Texture | - |
| `web/okayjs/src/renderer.js` | Renderer: WebGL 2, WebGL 1 fallback, per-pixel Lambert + Blinn-Phong, sprites, resize and pixel ratio, rebuilding its GPU objects after a lost context (untested) | the WebGL 2.0 / 1.0 and GLSL ES 3.00 / 1.00 specifications; textbook Lambert and Blinn-Phong |
| `web/okayjs/src/input.js` | Input: keyboard, mouse and pointer, touch, gamepad, axes and actions | W3C UI Events (KeyboardEvent code), Pointer Events, Gamepad (standard mapping); key names and frame rules from `engine/include/okay/Input/Input.hpp`, `EngineApi_Input.cpp`, `Scripting/ScriptApiCore.cpp` |
| `web/okayjs/src/script.js` | ScriptComponent: loads script text, a class or an object; lifecycle by name; errors with lines | ECMA-262 (the script is run as a strict function body); handler names from `engine/src/Scripting/ScriptLanguageGlue.cpp` (HandlerNamesFor, LifecycleHandlers) on `claude/javascript-1` |
| `web/okayjs/src/game.js` | Game (the loop, fixed step), Time, Debug | our own; fixed step and clamps from `Core/Time.hpp` (1/50 s, 5 steps at most) and `Core/Application.hpp` (maxDelta 0.25 s); requestAnimationFrame from the HTML standard |
| `web/okayjs/examples/` | `spinning_cube.html` (lit cube, floor, a 2D sprite through an orthographic camera), `script_api.html` (the engine test's script), `scripts/spin.js`, `scripts/key_mover.js` | - |

No code was taken from three.js, Babylon.js, PlayCanvas, gl-matrix or any other library (rule 19a); "like three.js"
names the kind of library, not its design. Library names follow one rule: what the engine has keeps the engine's
spelling (`GetComponent`, `Translate`, `Quaternion.Euler`, `Vector3.Distance`, `fieldOfView`, `orthographicSize`,
`sortOrder`); what only okay.js has uses JavaScript's usual camelCase (`renderer.render`, `game.step`, `setSize`).

## Conventions shared with the engine

- Right-handed, +Y up, +Z forward (`Vector3.forward`), Euler angles in degrees applied Z, then X, then Y
  (`Quat::Euler`); `eulerAngles` reads them back the same way, with the same gimbal-lock rule.
- Cameras look down their own -Z (the engine's `Camera::ViewMatrix` is the inverse of the camera's world matrix and
  `ScreenPointToRay` says "cameras look down -Z"). A directional light shines along its transform's forward (+Z).
- Matrices are column-major (`Mat4::at(col, row)` = `elements[col * 4 + row]`), clip depth -1 to 1 (`Mat4::Perspective`).
- Transform values are copies: `transform.position.x = 5` changes nothing, `transform.position = v` moves the object.
- Lerp clamps t, `Mathf.Round` halves away from zero, `Mathf.Sign(0)` is 1, `Mathf.Sqrt` of a negative is 0.
- Numbers are JavaScript doubles; the engine stores floats. `toString` prints the shortest text that reads back as
  the same float, like the engine.

## The script API it covers

The names come from the engine's JavaScript glue (`engine/src/JavaScript/JavaScriptVM.cpp`) and the typed binding
table (`engine/src/Script/EngineApi*.cpp`, `engine/src/Scripting/ScriptApiCore.cpp`).

| Covered | Notes |
| --- | --- |
| `export default class`, `export default { }`, no export (global functions, also lower-case `update`), `export <declaration>` | the text is run as a strict function body inside an inner function, so a script's own top-level `Time` or `transform` shadows the global; line numbers in errors match the script; a default-exported plain function is not constructed: as in the engine (only a class constructor is made), the top-level functions are the handlers |
| Awake, OnEnable, Start, FixedUpdate(dt), Update(dt), LateUpdate(dt), OnDisable, OnDestroy | in the engine's order: Awake, OnEnable and Start together the first frame the script can run (an inactive object waits, as `ScriptComponent::Start` / `JavaScriptVM::CallStart`); FixedUpdate at 1/50 s, at most 5 steps a frame; OnEnable / OnDisable when the object or the component is switched; on destroy OnDisable only if still enabled, and nothing for a script that never started (`ScriptComponent::OnDestroy`) |
| `this.gameObject`, `this.transform`, globals `gameObject`, `transform`, `GetComponent(name)`, `Destroy(obj)` | |
| Settings: instance fields, replaced from outside (`fields`, what the Inspector does) | text values read as numbers, booleans, `Vector3(1, 2, 3)` |
| GameObject: `name`, `tag`, `active`, `transform`, `GetComponent`, `GameObject.Find`, `GameObject.FindWithTag`, `String(obj)` = `GameObject(Name)`, a destroyed object is an error | |
| Transform: `position`, `localPosition`, `eulerAngles`, `localEulerAngles`, `localScale`, `forward`, `right`, `up`, `gameObject`, `Translate(v or x, y, z)`, `Rotate(v or x, y, z)`, `LookAt(target)` | plus `rotation`, `localRotation`, `parent`, `SetParent`, `TransformPoint` and friends (not in the engine's script table) |
| Vector3: constructor with or without `new`, `x y z`, `magnitude`, `normalized`, `sqrMagnitude`, `add`, `sub`, `scale`, `div`, `negate`, `equals`, `clone`, `set`, `toString`; `zero one up down right left forward back`, `Distance`, `Dot`, `Cross`, `Lerp`, `MoveTowards` | |
| Input: `GetKey`, `GetKeyDown`, `GetKeyUp` (the engine's key names), `GetMouseButton(Down/Up)` (0 left, 1 right, 2 middle), `mousePosition`, `GetAxis("Horizontal" / "Vertical")`, `GetButton(Down/Up)` and `GetActionVector` for Move, Look, Jump, Sprint, Crouch, Fire, Aim, Interact, Reload, Inventory, Pause, `DefineAction`, `gamepadConnected`, `touchScreen`, `Rumble` | a tap between two frames is still seen; the arrows also read as W A S D; typing in a text field is not game input; Space and arrows do not scroll the page |
| Time: `deltaTime`, `fixedDeltaTime`, `timeScale`, `unscaledDeltaTime`, `unscaledTime`, `sessionTime`, `sessionUnscaledTime`, `sessionFrameCount`, `fps`, `paused` | `deltaTime` inside FixedUpdate is the fixed step |
| Debug: `Log`, `LogWarning`, `LogError`; `console.log` | |
| Mathf: `PI`, `Abs Sqrt Sin Cos Tan Floor Ceil Round Sign Clamp01 Min Max Pow Atan2 Clamp Lerp MoveTowards` | plus `Deg2Rad`, `Rad2Deg`, `Epsilon` |

Not covered (a script that uses these runs in the engine only): triggers and collisions (`OnTriggerEnter` and the
rest: okay.js has no physics), Rigidbody and every other engine component (`GetComponent("Light").intensity` works
only for okay.js's own Camera, Light, MeshRenderer and SpriteRenderer, whose fields are named like the engine's but
not all of them exist), the generated binding layer, events with arguments (`OnSequenceEvent`), the Time pause stack
(`PushPause`, `HitStop`, `SlowMotion`), `Input.Gesture`, `swipeDirection`, `pinchScale`, the per-player action calls
(`PlayerActionValue` and the rest; okay.js has one player), Audio, SaveGame, Localization, Navigation, UI, Story,
Splines, Animation. The engine's instruction budget and memory limit do not exist in a page (the browser's own
limits apply), and okay.js has no sandbox: a script in a page can reach `window`, `fetch` and the DOM, which the
engine forbids. Error texts are the browser's, not okay::js's.

## Web-only additions

`Input.GetTouch(i)` / `touchCount` (fingers with phases), `mouseDelta`, `mouseScrollDelta`, `GetGamepadButton`,
`GetGamepadAxis`; `GameObject.layer` with `Camera.cullingMask`; the script global `okay` (the whole library).

## Tests

- `okayjs_unit` (`node --test tests/okayjs/test_okayjs_math.mjs tests/okayjs/test_okayjs_script.mjs`, plain Node):
  plus `test_okayjs_renderer.mjs` (the renderer against a fake WebGL context: no texture is made while a picture loads):
  the maths against hand-worked values and against a second way of computing the same thing, mesh winding, the scene
  graph, the game loop, input from synthetic events and a fake Gamepad API pad, and the engine test's game scripts
  (`tests/test_js_script.cpp` cases 1, 2, 3, 5 and 6 on `claude/javascript-1`).
- `okayjs_browser` (`tests/okayjs/test_okayjs_browser.js`, headless Chromium through Playwright; ctest skips it with
  77 when Playwright is not there; the CI web job runs it): the lit cube's pixels, the floor, the background, the
  sprite, Space reaching the script, resize, pixel ratio 2, WebGL 1 asked for and as the fallback, the script-API page.

## Not done yet

Point and spot lights, shadows, a scene file reader (the engine's `.okayscene`, so an exported scene opens in
okay.js), glTF or OBJ meshes, text, audio, physics, picking (`ScreenPointToRay`), instancing and batching, frustum
culling, sorting of see-through meshes inside one object, sRGB-correct colour (colours are used as they are),
an npm-style package or a minified single file, and checking script parity automatically against part 1 once it is
merged (today the shared script text is copied from `tests/test_js_script.cpp`, which is on `claude/javascript-1`).
Unverified: Firefox, Safari, phones, real touch screens and real gamepads (only headless Chromium with SwiftShader
and a fake Gamepad API pad were run).
