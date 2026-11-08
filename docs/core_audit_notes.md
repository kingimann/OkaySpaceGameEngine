# Core audit notes

A bug-hunting pass over the "Core & scene" area (see `FEATURE_MAP.md`, area 1):
the ECS, the math types, Camera projection, Layers, `DataAsset`, and the input
plumbing. Scene lifecycle, serialization, SaveGame / Prefs / Localization,
Random / EventBus / Time / Log / Profiler and Scheduler / Tween were out of scope
(other work was changing them).

Every fix below has a failing-first test in `tests/test_core_audit.cpp` (or
`tests/test_core_audit_input.cpp`, POSIX only: it drives a pty).

## Bugs fixed

| Area | Bug | Fix | Test section |
| --- | --- | --- | --- |
| `Quat::LookRotation` | A zero forward vector gave a bogus 180 degree rotation instead of "no turn". | Return `Identity` when forward is (almost) zero. | LookRotation |
| `Mathf::SmoothDamp` | `dt == 0` (a paused frame) with the value already at the target set `velocity = 0 / 0` = NaN, which then poisons every later frame. Same hazard in the `Vec2` / `Vec3` overloads. | Guard the divide. | Mathf edge cases |
| `Mathf::Repeat` / `PingPong` | `length == 0` returned NaN. | An empty range returns 0. | Mathf edge cases |
| `Vec3::Slerp` | Opposite vectors: `sin(theta) ~ 0` made the result explode (magnitude 1e7 or NaN). | Swing through a perpendicular axis near 180 degrees. | Vec3::Slerp |
| `ecs::World::add` | Adding to a destroyed entity or to `Null` stored a component for a dead id; every query then visited the zombie. | `add` on a non-live entity stores nothing (returns a scratch copy). | ECS |
| `ecs::World::ensure` | `ensure(Null)` made the null id alive; `ensure(0xFFFFFFFF)` wrapped the id counter so the next `create()` returned `Null`. Both ids can come off the network. | Reject `Null` and the top id. | ECS |
| `ecs::World::each` | Destroying / removing the entity a callback was given swapped the last entry into the slot and the loop skipped it. | Re-visit the slot when the entity under it changed. (Removing an earlier-visited entity is still unsupported; see below.) | ECS |
| `ecs::NetWorld::apply` | A damaged or hostile snapshot could claim billions of entities / components: the count drove a `reserve` of gigabytes and a multi-billion iteration loop, and the truncated tail created default-constructed components on entity 0 (`Null`). | Validate every count against the bytes left before touching the world; stop on a bad block; never apply `Null`. | NetWorld::apply |
| `Camera::ProjectionMatrix` | Aspect 0 (a minimised window), a negative or NaN aspect gave an inf / NaN matrix; `WorldToScreen`, `ScreenPointToRay` and the renderers then produced NaN. Clip planes with `near <= 0` or `far <= near` were passed straight into the matrix. | Fall back to the last good aspect; clamp near > 0 and far > near. | Camera |
| `Layers::MaskFromList` | Layer names were case sensitive, so `"default"` or `"water, ui"` silently matched nothing and the mask became "every layer". | Case-insensitive match. | Layers |
| `DataAsset::Load` | A failed load (missing file) had already cleared the fields. The base-dir fallback concatenated without a separator (`"assets" + "x.okaydata"`). | Clear only after the file opens; join with `/`. | DataAsset |
| `Input::EndSession` | `fcntl(STDIN_FILENO, g_savedFlags, 0)` had its arguments in the wrong order, so the terminal stayed `O_NONBLOCK` after the game exited (the shell then misbehaved). | `fcntl(fd, F_SETFL, flags)`. | `test_core_audit_input` |
| `Input::MouseDelta` | The first `FeedMouse` reported the cursor's absolute position as a delta, so a mouse-look camera jumped on frame one. | The first sample has no delta. | `test_core_audit_input` |
| `InputActions` persistence | Bindings are saved as `a;b;c` and four-key directions as `keys:w,s,a,d`. A player who rebound an action to `;` or `,` (or `%`) got a corrupt string: the binding was dropped on the next load. | Escape those three keys as `%3b` `%2c` `%25` when writing, decode when parsing (the old raw form still parses). | Input bindings |

Looked at and found fine (tests added where cheap): `Quat::LookRotation` for
straight up / down, the Euler <-> quaternion round trip including gimbal lock,
`Quat::Slerp` unit length, every `Easing` endpoint, `Mat4::Inverse`, `Camera`
ray / unproject math, `DataPack` encode / decode.

## Not fixed (seen, not demonstrated or out of scope)

- (Fixed, see `docs/ecs.md`: `each` under mutation, command buffer, O(n) `NetWorld::apply`, delta snapshots.)
- `Application::Run` has no RAII around the terminal session: an exception from a script leaves the terminal in raw mode. It also ignores `Game::QuitRequested()`.
- `Camera::Awake` makes any `main` camera the scene's main camera regardless of `depth`; the doc says the highest depth wins, which `Scene::ResolveMainCamera` does later (Scene.cpp was out of scope here).
- `DataAsset::Cached` keeps a failed load forever (a file created later is never picked up), and `Save` writes multi-line values that `Load` reads back as extra fields.
- `Input::KeyFromName("F1")` falls back to `'f'`; there are no function keys, arrows have synthetic codes below 5, and the key space is a single `char` (no keypad, no non-Latin keys, no left / right modifiers).
- `Bounds::Encapsulate` on a default `Bounds` grows from the origin, not from the first point.
- `SceneBridge` keeps raw `GameObject*`; if something else destroys the object the bridge still dereferences it on the next `sync`.

## Missing engine features, most useful first

1. **Physical key codes and rebindable function / modifier keys.** The key layer is one `char`: no F1-F12, no left / right Shift / Ctrl / Alt, no keypad, no keyboard-layout-independent (scancode) bindings. Rebinding UIs need them.
2. **Input contexts / action maps** (gameplay, menu, vehicle) with a stack, and consume-on-use so a menu eats "Jump". `GameInput` blocks are a coarse substitute.
3. **Gamepad dead zones and response curves** per stick in the Input layer, plus device ids for local multiplayer: `Input` exposes one pad and passes the raw axis through (the runtime may filter before `FeedGamepad`; not checked).
4. **ECS command buffer** (deferred create / destroy / add / remove during iteration), plus queries with `Without<>` and change detection. Needed to make systems safe.
5. **ECS delta snapshots** for `NetWorld` (only changed components, with a sequence number and ack), and a checksum on packets.
6. **Resource / asset manager with handles**: a single `Assets::Load<T>(path)` with reference counting, async load, hot reload, and a normalized virtual path (`assets://`) instead of `DataAsset::BaseDir` plus ad-hoc string joins. The engine has no central asset registry; paths are resolved in each loader.
7. **Camera**: a camera stack with per-camera render targets and priorities, and a multi-camera `ResolveMainCamera` that honours `depth` live.
8. **Math**: a public `Mat4::Decompose` (TRS), `Quat::ToAngleAxis`, `Vec3::OrthoNormalize`, general `Plane` / `Ray` types with `Bounds::IntersectRay` in the Math headers (a frustum lives in `Render/Culling.hpp`), and double-precision variants for large worlds. (Checked by header search only.)
9. **Layers**: a layer collision matrix as a project setting (today a mask is built per query) and named tags with an editor list (tags are free strings).
