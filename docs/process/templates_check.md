# Every template and example is tested

> **What this is:** Owner rule (2026-11-15): "make sure all templates you make for anything works." Two tests now run every project template and every example scene we ship, the way a person would, and fail when one is broken.
>
> **Status:** Internal record (audit, plan or status): read it as a dated record, newest facts in the code.
>
> **Last checked:** 2026-11-20 (moved into `process/`, paths and links checked; the text itself was not re-checked against the code).

Owner rule (2026-11-15): "make sure all templates you make for anything works." Two tests now run every project template
and every example scene we ship, the way a person would, and fail when one is broken. Nothing is skipped silently.

| Test (ctest name) | Source | What it covers |
|---|---|---|
| `templates_work` | `tests/test_templates_work.cpp` | the 17 templates the launcher's Templates page and the editor's New Project form list (`okayproj::Templates()`), and the seven `EditorState` builders no menu reaches (two older ones, plus the five removed on 2026-11-16) |
| `examples_work` | `tests/test_examples_work.cpp` | every `examples/**/*.okayscene` (52 today), every File > Examples entry, and the Windows release's example list |

Both are in the normal CI run (`ctest`), they take about six seconds together (measured on one core, `-O1`), and they need no window.

## Run them

```
cmake --build build --target test_templates_work test_examples_work
ctest --test-dir build -R "templates_work|examples_work" --output-on-failure
```

Run the program itself to see the table of checks and what was measured (how far the player walked, how high it jumped,
how many bricks the ball hit):

```
build/tests/test_templates_work                         # the table
OKAY_TEMPLATES_VERBOSE=1 build/tests/test_templates_work   # plus every warning the Problems panel and the Console gave
build/tests/test_examples_work
```

## templates_work: what is checked, per template

| Column | The check |
|---|---|
| create | The launcher's flow: `PlanProject` names the project after the template, `EditorState::CreateProject` makes it in a temp folder with the editor's own settings writer. The scene is at `Assets/<name>.okayscene`, the standard folders and `project.okayproj` exist, the version stamp says "same version" so the upgrade gate asks nothing, the new project has no unsaved changes, and a second editor reopens it. |
| camera | Exactly one active Main Camera; a 2D template has an orthographic one, a 3D template a perspective one; when Play starts it is the camera the Game view renders through and something is in front of it (the Empty template is empty on purpose). |
| extras | The template's own objects: a light in 3D scenes, none in 2D; the player (or the car, the board, the paddle) exists and carries its controller. |
| roundtrip | Serialise, load, serialise gives the same text; the file on disk is what the scene serialises to; loading skips no record and keeps no "unknown record"; a second round gives the same text again. |
| problems | `CollectProjectDiagnostics`, the model behind the Problems panel, reports no error (missing asset, missing script, bad reference, no camera). Warnings are listed, not failed. |
| play120 | Play runs 120 fixed frames with no Console error and no exception; Stop puts the scene back exactly as it was; the project does not read as "changed" afterwards (so the editor does not ask to save). |
| interact | The family's own check with simulated keys and mouse, through the editor's own key mapping (`editor/EditorPlayInput.hpp`). It runs twice: in the scene the template just built ("fresh") and in the project reopened from disk ("reopened"). |

The interaction checks, by family:

* First and Third Person (it replaced the separate First Person and Third Person templates on 2026-11-17; the old titles still build for old --template arguments, as this one), Sandbox, Third Person Shooter: hold W for a second and the player moves; let go and
  it slows down; Space jumps and gravity brings it back; moving the mouse turns the view.  Third Person Shooter also holds the left mouse button and the targets lose health.
* Platformer, 2.5D Platformer: D moves, Space jumps and lands; 2.5D: W hops to the layer behind, S back.
* Top-Down: D moves, and the walls stop the player. Top-Down Shooter: it moves, Space dashes, the left mouse button fires and
  the magazine drains.
* Point and Click: the click point is worked out through the camera, and the player walks there.
* Vehicle (3D), Vehicle (2D): W drives the car and it slows down when released.
* Vehicle (get in and drive): W does nothing with nobody inside; walk up, press E, the Player sits in the driver's seat, W
  drives and the driver rides along, E gets out.
* Coin Collector (no longer offered): walking onto the coins scores each and removes it, and the HUD shows the score.
* Snake (no longer offered): the snake moves by itself, W turns it, food is on the board. Brick Breaker: the ball moves, A and D move the paddle,
  bricks get hit and the score rises.
* Main Menu: the Start button is a real button; clicking it does something that exists (hides the menu and shows the Game
  part; it must not try to load a scene that is not there); Back returns; Quit calls `quit()`.
* Inventory (no longer offered): the item grid is wired to the Player's bag, a stack drags to another slot and back, the chest and the workbench
  are there.
* Multiplayer: the host and join controls exist, the status line says "offline", and the scene runs with no network.
* 3D Scene, 2D Scene, Empty: they run.

Besides the per-template table, the test checks the table itself (`editor/TemplateTable.hpp`):

* every title in `okayproj::Templates()` has a builder, and no builder lacks a launcher entry;
* titles are unique, non-empty and legal project names; no two titles share a builder; the card's three feature lines exist;
* an unknown title has no builder (`FindBuilder` gives null); the editor's user-facing path still falls back to the 3D Scene
  but now says so in the Console (`BuilderOrDefault`); an empty title (no `--template`) is the quiet default;
* the launcher's second list, the quick templates behind the Templates page cards (`launcher/main.cpp`), names only real
  templates;
* every `void New...()` in `editor/EditorState.hpp` is either in the table or listed in `MakeNotOffered()` with a reason (see
  below), and an entry there that is offered by now, or gone, fails as stale;
* the version gate really asks about an older project and about one with no stamp, so "no question" for a new project means
  something.

**Scenes nothing offers.** `EditorState` has six builders that no menu, launcher card or New Project form calls (the two older ones are listed first; Coin Collector, Snake, Brick Breaker and Inventory were taken off the New Project lists on 2026-11-16 and stay here so they keep being tested; the Humanoid scene and its builder were deleted on 2026-11-17):
`NewPlatformer3D` (a 3D platformer) and `NewTerrainSandbox` (the older heightmap sandbox that the voxel Sandbox replaced in the
launcher). A person cannot start them, so under the owner's rule 4a they are not "done"; they are listed in `MakeNotOffered()`
with that reason and run through the same seven columns as the templates (all pass today: the 3D platformer's player walks 4.8 m
in a second and jumps 2.4 m; the terrain sandbox's player walks, looks around, and digging lowers the ground). Offering one is
the four steps under "Add a template"; deleting one is deleting its `EditorState` method and its row. Which of the two is the
owner's call: nothing was added to the launcher here.

## examples_work: what is checked

* Every `examples/**/*.okayscene` is copied to a temp folder (nothing can be written into the checkout) and then: the engine
  loads it with no skipped record; the Problems model reports no error (the scene's own folder is the asset root, because a
  scene finds its files next to itself); the editor opens it; Play runs 60 frames with no Console error and no exception; Stop
  restores it; and when Play starts the camera shows something.
* File > Examples > UI Gallery and UI Gallery (phone): built into a project by `oked::BuildUIGalleryExample`, saved, reopened,
  the same text after a round trip, no Problems error, 60 Play frames.
* File > Examples > Visual Script Node Playground: every demo in `VsPlaygroundDemos()` has its scene and graph file, the object it
  selects exists, some object runs the graph, and every playground scene is offered by some demo (`coverage.okayscene` is the
  fixture of another test).
* File > Examples > Node Playground (flow and variables): `NodePlaygroundScenes()` finds exactly the scenes in the folder.
* The Windows release: every folder a File > Examples entry opens scenes from is in `EXAMPLE_DIRS` of
  `scripts/build-windows-dist.sh`, and every listed folder exists.

A scene that cannot run in a plain headless build goes in `kSkips` in the test, with its reason. The list is empty today.

## Add a template

Four places, and the test fails until they agree:

1. `shared/ProjectCreate.hpp`: a row in `Templates()` (title, one honest sentence, group) and a row in `TemplateFeatureLines`.
2. The builder: a function in `engine/include/okay/Scene/SceneTemplates.hpp` (`Templates::MyGame`), and `EditorState::NewMyGame`
   in `editor/EditorState.hpp/.cpp` that calls it. A template always ends with exactly one main camera.
3. `editor/TemplateTable.hpp`: one row, title and builder. This is the only place the editor looks a title up.
4. `tests/test_templates_work.cpp`: one row in `MakeSpecs()` (light or not, 2D or 3D, the player object and its controller, and
   how to drive it). A new family of game gets its own `Drive...` function there. A template with no row fails.

Then `ctest -R templates_work` must pass. If the template belongs on the Templates page cards, add it to the quick list in
`launcher/main.cpp` too (the test checks the title). Update the feature map page and check it with
`python3 scripts/gen_feature_map.py --validate` (the generated `docs/FEATURE_MAP.md` is rebuilt on main).

## Add an example scene

Put it in `examples/<folder>/`. Three things the test caught in the scenes that were there:

* The engine's camera looks down its own **-Z**. A camera with no rotation placed at negative z looks away from the origin.
  42 hand-written example scenes did exactly that and showed an empty picture (only the on-screen text). Use a rotation that
  looks at the content (the editor's Game view shows it), or put the camera at positive z.
* `gravity` takes two numbers (`gravity 0 -9.81`); `gravity3` takes three. A third number after `gravity` is read as a record
  called "0" and the loader warns about it.
* A scene needs a main camera. The physics example had none.

If the folder should be in the Windows release, add it to `EXAMPLE_DIRS` in `scripts/build-windows-dist.sh`. Then
`ctest -R examples_work` must pass.

## Known failures (kept in the test, in `kKnownFailures`)

A check that fails because of a bug deep in an engine system is listed in the test with its reason, so it stays visible and the
suite stays green. A listed failure that stops happening fails the test, so the list cannot rot. Today:

* **Sandbox, interact**: a player standing still on the voxel terrain creeps downhill at about 0.2 m/s. Physics3D's voxel
  contact (`engine/src/Physics/Physics3D.cpp`, section 5) pushes a body out along the surface normal but has no friction and
  never sets `Rigidbody3D::groundNormal`, so the first person controller's slope hold never sees the slope. Handed to Claude 2
  (physics); the test's `kKnownFailures` entry names the two messages it excuses.

## Handed over (engine behaviour, not changed here)

* **Object numbers in the scene text have gaps.** `SceneSerializer::Serialize` numbers each object by its place in the scene,
  including objects it does not write (a runtime-only object, a Character's generated Rig). Any object created after such an
  object gets a number that a reload does not reproduce, so the text of "the scene with the rig" and "the scene just loaded"
  differ, and the editor's "unsaved changes" check, which compares those texts, says the project changed after Play and Stop.
  `tests/test_scene_canonical.cpp` ("Object numbers with gaps") documents the gap as the writer's behaviour and the canonical
  form (`SceneText::Canonicalize`) closes it, so the fix (number only the written objects, 5 lines in `Serialize`) changes that
  test and was left to the owner of the serializer. The Humanoid template avoids it by object order. A scene a person builds
  by hand (Separate Into Parts, then add objects) can still hit it.
* **Sandbox: the player creeps down the voxel terrain.** See "Known failures" above.
* **The File menu has three rows named Examples** (`editor/main.cpp` builds a menu with that name three times; the first and the
  third list the same node playground scenes). Not touched here: `claude/editor-menus` (claim and PR on issue #422) is rebuilding
  the menu bar and names this problem. `examples_work` checks the scenes the menu opens, not the menu itself (no window).
* **Two scenes nothing offers.** `NewPlatformer3D` and `NewTerrainSandbox` in `EditorState` are not reachable from the launcher or
  any editor menu (see "Scenes nothing offers" above). They work and are tested; whether to offer or delete them is the owner's
  call.
* **Duplicate names in the Problems panel.** Brand new First and Third Person, Third Person Shooter, Top-Down Shooter, Point
  and Click, Vehicle (3D), Vehicle (get in and drive), Vehicle (2D) and Coin Collector projects show one or more
  "N objects are named 'Crate' at the scene root" warnings (Crate, Target, Target Dummy, Cover, Block, Lane Dash, Cone, Coin, Step). They are
  warnings, not errors, and the scripts and tests look the objects up by those names, so they were not renamed.

## What the first run found, and what was done (2026-11-15)

Fixed in the templates and examples (data and builders):

* Main Menu: Start loaded `game.okayscene`, a scene no new project has, so clicking it showed nothing and logged an error.
  Start now hides the menu and shows the Game part of the scene; Back returns.
* Third Person Shooter: left mouse fired nothing (the controller only announces a shot; no gun was attached) and the targets
  had no health. It now has a Ranged Weapon (hitscan, 12 rounds) and the targets take damage and come back.
* Top-Down: the walls had no collider and the player no body, so the player walked through them.
* Humanoid (since deleted): root motion lost its hips link on the first save (only the name is saved); the template now sets the name.
* Sandbox: the terrain was built with full precision but saved with one byte per voxel, so the first Play, Stop or reopen
  changed it and the editor asked to save a scene nobody had changed. The terrain is now rounded to what the file stores.
* Humanoid (since deleted): the project read as changed after the first Play and Stop, so the editor asked to save a scene nobody had touched.
  Cause: the scene file numbers objects by their place in the scene, and the part rig that Foot IK is wired to is generated
  (the file leaves it out and it is rebuilt on load), so a scene with the rig came out with a gap in the numbers and the same
  scene reloaded did not. The template now puts the camera in before the player, so the rig is last and leaves no gap. The
  general cause is below under "Handed over".
* The launcher's blurbs for Vehicle (3D) ("with suspension": it is off by default), Vehicle (2D) ("side-on, hill-climb": it is
  top-down) and Sandbox ("blocks": it digs and fills terrain) said things the templates do not do. The text now says what they do.
* 42 example scenes had a camera looking away from their content; two had a bad `gravity` line; the physics example had no
  camera and nothing to see. All fixed.
* The Windows release did not carry `examples/visual_script/playground`, so File > Examples > Visual Script Node Playground said
  "scenes not found" there. It is in `EXAMPLE_DIRS` now.

## Pictures: the cameras that looked away, before and after

These are **headless software renders**, not window screenshots: 640 x 360, meshes only (no on-screen text, no UI), after 30
frames of Play. Each pair is the same scene file from `main` before this change (left) and from this branch (right). They were
made with a throwaway 30-line program that loads the scene, runs 30 frames and calls `okay::testing::RenderSceneToImage`
(`engine/include/okay/Testing/Regression.hpp`); that program is not in the repo. A blank sky on the left is the bug: the scene
runs, the camera looks the other way.

| Scene | Before | After |
|---|---|---|
| `examples/weapons/arena.okayscene` |  |  |
| `examples/behavior_trees/breakpoint_demo.okayscene` |  |  |
| `examples/own_wav/own_wav_test.okayscene` |  |  |
| `examples/visual_script/playground/movement.okayscene` |  |  |
| `examples/spatial_audio/spatial_audio.okayscene` |  |  |
| `examples/physics/step_and_parent.okayscene` | no picture: the scene had no camera, the renderer refused |  |

The other 37 of the 42 scenes whose camera was turned to face the content are not pictured; `examples_work` checks every one of
them (`DrawnInView`: at least one drawn object is inside the picture).

## Not covered, and not verified

* A real window, the launcher's pages, the editor's menus and Windows itself. These tests call `EditorState::Tick` the way the
  editor's Play does, with keys and the mouse fed through the same mapping, but nothing here opens a window.
* The project settings the editor shell applies (gravity, physics steps per second); the tests use the engine defaults.
* Pixels: the headless render used to look at a few scenes (`RenderSceneToImage`) draws meshes only.
* Where the editor looks for the examples on Windows (File > Examples finds `examples/...` from the working folder and its
  parents): unverified. If the editor is started from a folder that is not inside the install folder, the menu says the scenes
  were not found.
* The launcher's own text is not generated from the table: the quick list in `launcher/main.cpp` is checked only for its titles.
