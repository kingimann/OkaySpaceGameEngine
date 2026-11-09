# Automated testing

Feature map area 33. Three pieces, all in `okay::testing` (headers under
`okay/Testing/`, sources in `engine/src/Testing/`, part of `okay_engine`):

| Piece | Header | What it is for |
|---|---|---|
| Play-mode tests (33.1) | `PlayTest.hpp`, `TestRegistry.hpp` | Load a scene, run it headless for N frames, assert on what happened |
| Scene validation (33.2) | `SceneValidator.hpp` | Find missing assets, missing cameras, broken scripts, NaN transforms ... before the game ships |
| Regression helpers (33.3) | `Regression.hpp` | Golden text/binary files, a scene fingerprint, software-rendered golden images |

Nothing here is pulled in by `<Okay.hpp>`; include the header you need.

## Play-mode tests

```cpp
#include "okay/Testing/PlayTest.hpp"
#include "okay/Testing/TestRegistry.hpp"
using namespace okay;
using namespace okay::testing;

OKAY_PLAYTEST_IN(movement, player_walks_right) {
    PlayTest t;                                       // fixed 1/60 s frames
    t.LoadSceneFile("scenes/level1.okayscene");       // or LoadSceneText(...), or build t.scene() by hand
    t.KeyDown('d');                                   // hold D from now on
    t.RunFrames(30);
    OKAY_EXPECT(t.PositionOf("Player").x > 1.0f);
    OKAY_EXPECT(t.RunUntil([&] { return !t.Require("Door")->active; }, 5.0f));
}
```

### How a frame runs

`PlayTest::Step()` does what the player does every frame, in the same order:
clear last frame's typed text, feed the injected input to `Input`, tick
`InputActions`, `Time::Step(dt)`, `Scene::Update(Time::DeltaTime())`. Nothing
reads the wall clock, so a run is repeatable. The constructor also resets the
global clock (`Time::ResetForTesting()`), reseeds `Random::Shared()`
(`PlayTestOptions::randomSeed`) and puts `Input` and `Game` (pause / quit) back to
neutral; the destructor does the same. Process-wide state means **one `PlayTest`
alive at a time**.

`PlayTestOptions`:

| Field | Default | Meaning |
|---|---|---|
| `dt` | 1/60 | simulated seconds per frame |
| `timeScale` | 1 | starting time scale |
| `fixedStepPhysics` | false | false: one physics step per frame; true: the player's fixed 1/50 s steps with interpolation |
| `muteConsole` | true | captured log lines do not print while the test runs |
| `treatWarningsAsErrors` | false | warnings count like errors |
| `randomSeed` | `0xC0FFEE` | seed for `Random::Shared()` |

### The API

* **Scene**: `scene()`, `LoadSceneFile`, `LoadSceneText` (false plus `LastError()` on a bad
  document, the scene stays as it was), `AdoptScene`, `NewScene`, `LoadWarnings()`.
* **Stepping**: `Step`, `RunFrames(n)`, `RunSeconds(s)`, `RunUntil(pred, maxSeconds)`
  (checks `pred` before the first frame and after every frame; the limit is frame time,
  not scaled by the time scale), `Frame()`, `SimulatedSeconds()`, `QuitRequested()`.
  `RunFrames` / `RunUntil` stop early once the game asks to quit.
* **Time**: `SetTimeScale`, `SetPaused` (the pause-menu path).
* **Finding**: `FindObject("Name")` or `("Parent/Child")`, `FindObjects`, `FindObjectWithTag`,
  `FindComponent<T>("Name")`, `FindComponent<T>()`, `PositionOf`, and `Require(name)`, which
  fails the running test and stops it when the object is missing.
* **Input** (applied at the start of the next `Step`, like the player feeds a frame):
  `KeyDown/KeyUp/TapKey` (a char or a name such as `"space"`), `SetMoveKeys({x, y})` (WASD),
  `SetAxis` / `SetLookAxis` / `SetTrigger` / `SetGamepadButton`, `MoveMouse`, `MouseDown/Up`,
  `Click`, `Scroll`, `TypeText`, `Touch`, `ReleaseAll`. `KeyDown` makes `GetKeyDown` true on
  the first frame and `GetKey` after; `KeyUp` gives `GetKeyUp` once; `TapKey` and `Click`
  hold for exactly one frame.
* **Log**: every `Log` line is captured (`LogLines()`, `LogContains`, `CountLog`, `ClearLog`).
  An error nobody declared **fails the test**: call `ExpectError("substring", times)` for
  errors the test provokes on purpose (`times` = exactly that many, `0` = any number,
  not required). `Problems()` lists what would fail; `Finish()` reports it to the running
  test (the destructor does it for you). The previous `Log::sink` keeps receiving lines.
* **Events**: `EventRecorder<E> rec(bus)` stores every `E` published on an `EventBus`.
  `EventProbe` is a component that records the messages its object receives
  (`collision_enter:Other`, `trigger_exit:Other`, `start`, `mouse_click`, ...):
  `probe->Saw("collision_enter:Ground")`, `probe->Count("trigger_")`.

### Registry and runner

`OKAY_PLAYTEST(name) { ... }` registers into suite `playtest`;
`OKAY_PLAYTEST_IN(suite, name)` picks the suite (the JUnit class). Assertions record on
the running test: `OKAY_EXPECT(cond)`, `OKAY_EXPECT_NEAR(a, b, eps)`, `OKAY_EXPECT_EQ(a, b)`,
`OKAY_FAIL(msg)`, `OKAY_NOTE(msg)` (context shown with a failure) continue; `OKAY_REQUIRE(cond)`
stops the test; `OKAY_SKIP(reason)` ends it as skipped. An exception that escapes a test
is reported as an error.

A runner is one `main`:

```cpp
int main(int argc, char** argv) { return okay::testing::RunMain(argc, argv); }
```

This repo builds `okay-playtest` (`build/bin/`), which carries the framework's own
play-tests (`tests/playtest/selftest_playtests.cpp`). A game links its tests with
`okay::engine` the same way.

```
okay-playtest                                  run everything
okay-playtest --list                           list "suite.name" of the selected tests
okay-playtest movement                         a bare word matches as a substring
okay-playtest --filter='movement.*,ui.menu*'   * and ? are wildcards, comma separates patterns
okay-playtest --exclude=slow --fail-fast -v
okay-playtest --junit=out/playtest.xml --text=out/playtest.txt
```

Exit code: `0` all passed (skips are fine), `1` a test failed or errored, `2` bad command
line or an unwritable report, `3` the filter matched nothing (`--allow-empty` makes that 0).
Tests run sorted by suite then name, so link order does not matter. The JUnit file has one
`<testsuite>` per suite and `<failure>`, `<error>`, `<skipped>` and `<system-out>` (notes)
elements; CI systems that read JUnit show failures inline. `ctest` runs it as
`playtest_runner`.

## Scene validation

```cpp
ValidatorOptions opt;
opt.assetRoot = "assets";
ValidationReport r = SceneValidator::ValidateFile("scenes/level1.okayscene", opt);
std::cout << r.ToText();        // or r.ToJson()
if (r.HasErrors()) return 1;
```

This is the validator for CI. The scene text has a second, lighter one (`Scene::Validate`,
`SceneSerializer::Validate`); `scene_hierarchy_search.md` ("Which validator?") says when to use which. `ValidateText` and
`ValidateFile` also report `scene.duplicate_guid` and `scene.broken_parent` (the loader repairs both, so only the text shows
them); `checkLinks = false` turns that off.

`Validate(scene)`, `ValidateText(text)` and `ValidateFile(path)` never change the scene
(the last two load into a scratch scene; a document that does not load is one
`scene.load` error, skipped records become `scene.load_warning`). Relative asset paths
are looked for under `assetRoot`, then each of `searchDirs` (`ValidateFile` adds the
scene's own folder); `fileExists` replaces the file test for packed archives. `proc:`
textures and URLs are built in and always found.

| Code | Severity | Meaning |
|---|---|---|
| `asset.missing` | error | a texture, audio clip, mesh, prefab, data file or font a component names does not exist |
| `asset.prefab_missing` | warning | an instance's source prefab is gone (it still works, it just cannot Apply / Revert) |
| `scene.no_camera` | warning | no `Camera` at all |
| `scene.no_active_camera` | warning | cameras exist but none is enabled and active |
| `name.duplicate` | warning | siblings share a name, so `Find("X")` reaches only the first |
| `component.duplicate_singleton` | warning | two `EventSystem`, `DayNightCycle`, `CinemachineBrain`, `PauseMenu` or `SteamManager` |
| `script.missing_file` | error, or warning if the scene carries a copy of the source | a Script or Visual Script file path that does not exist |
| `script.empty` | warning | a script with neither source nor file |
| `script.compile` | error | the script text (or the referenced file) does not compile |
| `script.backend` | error | the script's language is not in this build |
| `ref.missing_object` | warning | a `CameraFollow`, `FollowTarget2D`, `Minimap`, `Teleporter` or `TriggerZone` names an object that is not there |
| `transform.non_finite` | error | NaN or infinity in position, rotation or scale |
| `transform.zero_scale` | warning | a scale component is exactly 0 |
| `transform.rotation_unnormalized` | warning | a rotation quaternion that is not unit length |
| `transform.extreme_position` | warning | beyond `maxCoordinate` (default 1e6) on an axis |
| `object.inactive` | info | the top of an inactive subtree (reported once, with the descendant count) |
| `object.layer_range` | warning | a layer outside 0..31 |
| `physics.rigidbody_no_collider` | warning | a `Rigidbody2D` / `Rigidbody3D` with no collider on the object |
| `scene.duplicate_guid` | warning | `ValidateText` / `ValidateFile`: two objects carry the same `guid` (the loader gives one a new guid) |
| `scene.broken_parent` | error | `ValidateText` / `ValidateFile`: `parent N` names an object that is not in the file (it loads as a root) |

`ValidatorOptions` has a `check*` switch per group (including `checkLinks` for the two codes above), severities for missing assets, no
camera and duplicate names, and `allowDuplicateNames`. The asset fields it follows are
the same ones `SceneSerializer::CollectAssetPaths` ships with a build (plus fonts and the
prefab source); `SceneValidator::AssetReferences(go)` lists them per object. Objects a
component generated at run time (`transient`) and destroyed objects are skipped.

`ToText()` prints a summary line and one line per issue, errors first:

```
Scene 'Level 1': 1 error, 1 warning, 0 infos
[ERROR] World/Hero: Missing texture 'textures/hero.png' (SpriteRenderer.texture) [asset.missing]
[WARNING] Wall: 3 objects are named 'Wall' at the scene root; Find("Wall") only reaches the first [name.duplicate]
```

## Regression helpers

### Golden files

```cpp
GoldenResult r = CompareTextToGolden(actualText, "tests/golden/report.txt");
GoldenResult b = CompareBinaryToGolden(bytes, "tests/golden/mesh.bin", binaryOptions);
```

Text comparison normalises line endings, trailing spaces and trailing blank lines (all
switchable), and can accept numbers that differ by `numberTolerance` (absolute) or
`relativeTolerance` (a number inside a word such as `Cube2` is text, not a number).
The result says which lines differ. Binary comparison allows a per-byte tolerance and a
count of bytes allowed beyond it, or compares little-endian `float32` values within
`floatTolerance`. `CompareText` and `CompareBinary` compare two in-memory values.

**Updating a golden on purpose**: run with `OKAY_UPDATE_GOLDEN=1` (or set `update = true`).
Every check then rewrites its golden file instead of comparing; review the diff in version
control and commit it. A missing golden is a failure with `missing = true`.

### Scene digest

`DigestScene(scene)` is one line per object (path, active, layer, tag, **world** position
/ rotation / scale rounded to `decimals` (3), component types) sorted, and a 64-bit FNV hash
of the lines. Scenes with the same objects in the same state give the same digest however
they were built; moves below the rounding step do not change it; `q` and `-q` count as one
rotation. Use it to pin behaviour:

```cpp
t.RunFrames(120);
GoldenResult r = CheckSceneGolden(t.scene(), "tests/golden/after_two_seconds.txt");
OKAY_EXPECT(r.ok); if (!r.ok) OKAY_NOTE(r.message);   // the changed lines, "- old" / "+ new"
```

`HashScene` gives just the number; `DiffDigests(a, b)` lists the changed lines.

### Golden images

```cpp
Image img;
SceneRenderOptions view; view.width = 160; view.height = 90;
RenderSceneToImage(scene, img, view);                  // software renderer, in memory
ImageCompareOptions tol; tol.channelTolerance = 6; tol.maxDifferingFraction = 0.01;
GoldenResult r = CheckImageGolden(img, "tests/golden/level1.png", tol, "out/level1_diff.png");
```

`RenderSceneToImage` draws the scene's meshes with the engine's `Raster` / `RenderMeshes`
(the reference 3D path) from the main camera (or `options.camera`) into an opaque RGBA
`Image`; pixels no mesh covers show `options.background`. It draws **MeshRenderers only**:
sprites, UI, particles, terrain overlays and the sky are painted by the player and editor, not
by this pass, so they are not part of the image. The output repeats exactly on one machine
and build; different compilers round shading slightly differently, so commit goldens with a
small tolerance (`tests/test_regression.cpp` uses 6 levels on up to 1% of pixels).

`CompareImages` reports differing pixels, the largest and mean channel difference and a
**diff image** (red where a pixel is off, brighter = further, a dimmed copy elsewhere);
`CheckImageGolden` saves it to `diffPath` when the images differ. Goldens are PNG, or binary
PPM by the `.ppm` extension (`LoadImageFile` also reads ASCII PPM).

## Tests of the testing code

`test_testing_registry`, `test_playtest`, `test_scene_validator` and `test_regression` in
`tests/`, plus the `okay-playtest` self-tests. The committed goldens are
`tests/golden/regression/cube.png` (software-rendered cube) and `mover_world.txt` (scene
digest after one second of play); refresh them with
`OKAY_UPDATE_GOLDEN=1 build/tests/test_regression` and
`OKAY_UPDATE_GOLDEN=1 build/bin/okay-playtest selftest.world`.

## Not covered yet

The editor's test-runner window, UI-widget and audio assertions, networked (multi-process)
play-tests, and golden images of what the GPU renderers draw (the helper uses the software
renderer only). A script that reads the wall clock, a thread, or a network socket makes a
play-test non-deterministic; `PlayTest` cannot prevent that.
