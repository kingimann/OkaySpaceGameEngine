# Try it: everything you can test in the Windows release

Owner rule (2026-11-15): "everything that's being made I should be able to test; if I can't test it out it's
useless." This page lists each thing that is shipped (or finished and waiting for the next release), how to reach it
in the release you run (`OkaySpace.exe`, `Tools\OkayEngine.exe`, `Tools\OkaySpacePlayer.exe`, the installer or the
zip), and how to tell it worked. CLAUDE.md rule 4a keeps it current: every change adds its row here.

Honest status words used below:

* **Verified here** means a command was run on the Linux machine that wrote this page, and the result was seen.
* **Tested on main** means a headless test exists and passed in CI when its change merged; it was not re-run for this page.
* **Unverified** means it was read from the code only. Nothing in these sessions runs on Windows or opens a real editor
  window, so every "you should see" line about the editor is unverified until you look. Tell us when one is wrong.

## Where the files are

Since this change the zip and the installer carry an `examples` folder next to `OkaySpace.exe`:

| You used | The examples are in |
|---|---|
| The installer (`OkaySpace-Setup.exe`) | `%LOCALAPPDATA%\OkaySpace\examples\` (paste that into the Explorer address bar) |
| The zip | the unzipped `OkaySpace-<version>\examples\` folder |

The editor is `Tools\OkayEngine.exe` in the same folder (or start it from the launcher). In the editor, **File > Open...**
asks for a scene path: type or paste it, or press **Browse...**. A scene finds the files it uses (sounds, scripts,
sequences) next to itself, so the folder works from any install path.

What is in `examples\` (about 0.5 MB, all small): `vehicle`, `own_wav`, `own_readers`, `obj_texture_paths`,
`spatial_audio`, `cutscene`, `behavior_trees`, `node_playground`, `physics`, `physics2d`, `weapons`, `touch_controls`,
`ui_text`.
Left out on purpose: `account-server` (a Python server), `netdemo` and `physicsdemo` (C++ sources), `editor-plugins` and
`visual_script` (not scenes). The packaging is checked by reading and by running the copy step of

What is in `examples\` (about 1 MB, all small): `vehicle`, `own_wav`, `own_readers`, `obj_texture_paths`,
`spatial_audio`, `cutscene`, `behavior_trees`, `node_playground`, `physics`, `physics2d`, `weapons`, `touch_controls`, and
`visual_script\playground` (the 40 scenes behind File > Examples > Visual Script Node Playground; earlier releases left this
folder out, so that menu said "scenes not found"). Left out on purpose: `account-server` (a Python server), `netdemo` and
`physicsdemo` (C++ sources), `editor-plugins`, and the loose graphs in `visual_script` itself (not scenes). The packaging is
checked by reading and by running the copy step of
`scripts/build-windows-dist.sh` on Linux (see "How the packaging was checked" at the end); nobody has run the whole
release job or the installer build for this change.

## The list

| # | What | Where to reach it in the release | Status |
|---|---|---|---|
| 1 | Vehicle: get in a car and drive | New Project template **Vehicle (get in and drive)**, or `examples\vehicle\vehicle_test.okayscene` | Template and example new in this change |
| 2 | First person head bob follows Reduce Motion | New Project > First Person, plus the Settings screen | Shipped (2.873.0 and earlier) |
| 3 | Dropdown: drag scrolls a long list | Any UI Dropdown with Max Visible set | Shipped |
| 4 | Play start: shared audio, skipped navmesh bake | `examples\spatial_audio`, the Navigation window | Shipped; only the nav skip is visible |
| 5 | Navmesh from a Mesh Collider | View > Editors & Tools > Navigation | Shipped |
| 6 | OBJ texture path hardening | `examples\obj_texture_paths` | Shipped; refusal is silent |
| 7 | Turn interpolation | New Project > Third Person, Project Settings > Physics | Shipped |
| 7b | Templates page cards open New Project, never create a project | Launcher > Templates, click a card | New in this change; launcher UI unverified |
| 7g | Install, uninstall and Versions: see today's behaviour (audit, nothing changed) | Setup, Settings > Apps, `%LOCALAPPDATA%\OkaySpace`, the launcher's Versions page | Audit only: no program change in this branch. Windows steps unverified |
| 7j | Nameplates bound to their characters, and a rich-text sign | `examples\ui_text\ui_text.okayscene` (File > Open..., then Play) | New in this change; real window unverified |
| 8 | Own model readers (off by default) | `OKAY_OWN_STACK=1` or `OKAY_OWN_<FMT>=1`, `examples\own_readers` | Switch works on a release; see the table |
| 9 | Own WAV decoder (off by default) | `OKAY_OWN_WAV=1`, `examples\own_wav` | Switch works; no log line |
| 10 | Own HTTP client (off by default) | none | **Cannot be tried on Windows** |
| 11 | Own platform layer (off by default) | `OKAY_PLATFORM_BACKEND=own` | Unverified on Windows, never run there |
| 12 | OkayUI instead of Dear ImGui (off by default) | `OKAY_UI_BACKEND=okayui` | Unverified, only a few screens |
| 11b | Launcher drawn by our own renderer (off by default) | `OKAY_LAUNCHER_RENDERER=own`, then `OkaySpace.exe` (section "Launcher: drawing with our own renderer") | Footer says "own renderer"; the chat opens in its own window and the OkayUI preview works with it too; Linux/Xvfb only, unverified on Windows |
| 11c | Launcher with no SDL at all (off by default) | `OKAY_LAUNCHER_RENDERER=own` and `OKAY_PLATFORM_BACKEND=own`, then `OkaySpace.exe` (section "Launcher with no SDL at all") | Log says `launcher: no SDL`; voice calls and voice notes use our own audio (waveOut / waveIn); Linux/Xvfb only, unverified on Windows |
| 11d | Editor input and 3D-view OpenGL through our own code (off by default) | `OKAY_EDITOR_PLATFORM=own` (optionally also `OKAY_PLATFORM_BACKEND=own`), then `Tools\OkayEngine.exe` (section "Editor: input and the 3D views' OpenGL through our own code") | Console's first lines say `editor: platform own (...)`; the editor works as usual except that panels cannot be dragged out into their own windows; untested in a real editor run and on Windows |
| 11e | Editor drawn by our own renderer (off by default) | `OKAY_EDITOR_PLATFORM=own` and `OKAY_EDITOR_RENDERER=own`, then `Tools\OkayEngine.exe` (section "Editor: drawing with our own renderer") | Console's first lines say `editor: renderer own (opengl painter ...)`; no multi-window panels; **untested in a real editor run and on Windows** |
| 13 | Own crypto | none, on purpose | **Cannot and must not be tried** |
| 14 | One SHA-256 (file checksums, account passwords, anti-cheat tag, voice key) | Nothing new to click: sign in with an existing local account (Account tab), use the launcher's update check, join an anti-cheat checked server, join a voice room; all must work exactly as before. For the checks themselves see `docs/sha256_comparison.md` section "How to try it" | Tests only (Linux); launcher, Windows and real accounts unverified |
| 7f | Crash reports: a detailed report, sent for you, turned into a GitHub issue | `OKAY_TEST_CRASH_LATE=Test.Step` crashes the editor on purpose; launcher Settings > Crash reports | Local half new; the server half needs the owner's one-time setup (see 7f) |
| 15 | One shared inflate (zlib/gzip) decoder: PNG, `.blend`, FBX, `.x`, `.zgl`, 3MF/AMF, HTTP gzip | open a PNG texture; `examples\own_readers\cube_gzip.blend`, `cube_compressed_arrays.fbx` | PNG always on; the readers need their switch; HTTP gzip is Linux only |
| 16 | Main menu bar clean-up: one Examples, Edit clipboard commands, GameObject categories, View, Help | The bar at the top of `Tools\OkayEngine.exe` | New in this change; see section 16 for what was and was not seen |
| 17 | Hierarchy right-click menus: Create Child with every category, Add Component, Select Same Type, "Delete 3 Objects" | Right-click in the Hierarchy panel of `Tools\OkayEngine.exe` | New in this change; see section 17 |
| 7n | Every own replacement in one table: the switch, the sample, how to tell it is on, and what is missing | section 7n below and `docs/own_flip_proposal.md` | Table is new. Read from the code and from CI logs, not run on Windows |
| 7e | Every template and example scene is tested; the templates that were broken are fixed | New Project (each template), File > Examples, `ctest -R "templates_work\|examples_work"` | Tested on Linux, see "Every template is tested" below; no window opened |
| 7h | Project panel right-click menus (asset, folder, tree, Favourites, empty space, Create) | Editor: **View > Panels > Project**, right-click | New in this change; unverified in the real window |
| 7d | Visual script Add Node menu: 70 topics instead of 19 | Visual Script window, right-click the canvas or press Space | New in this change; the list and search are tested, the drawing is unverified |
| 7s | Digging the voxel terrain no longer drops the frame rate | `examples\voxel\voxel_big_world.okayscene`, or New Project > Sandbox; `OKAY_VOXEL_OLD_REMESH=1` for the old behaviour | New in this change; CPU numbers measured, GPU and Windows unverified |
| 7t | Voxel terrain digs like real life: materials, debris you pick up, building from what you hold, Ctrl+Z | `examples\voxel\voxel_realistic.okayscene` | New in this change; Linux headless tests and pictures only, Windows and the editor click path unverified |
| 7r | The character walks when you walk and runs when you hold Shift | New Project > Third Person (or First Person, press V), Play, W then Shift+W; older project: Player Controller > Use natural walk and run speeds | New in this change; the stride is measured by a test and drawn in pictures, the feel in the real editor and on Windows is unverified |
| 7u | Physics: Improved quality, physics playground, materials, slider joint, debris emitter | Open `examples\physics\physics_playground.okayscene` in `Tools\OkayEngine.exe` and press Play | New in this change; the physics is tested headless, the editor menus and the feel are unverified |
| 7v | Test builds: try a branch in the launcher in about 30 minutes, before it is released | Ask the coordinator for a test build; then Launcher > Settings > Behavior > Startup & updates > **Get test builds** (off by default) | New in this change; the rules and the publisher are tested, the workflow, the real server and the launcher screens were never run (see section 7v) |

---

## 1. Vehicle: get in and drive

**What it is.** A car with a driver seat and a passenger seat. You walk up, press E, and the car listens to your keys.
Code: `VehicleSeat` and `VehicleController` (`docs/vehicle_controller.md`).

**Steps (template).**
1. Open the editor (`Tools\OkayEngine.exe`) or the launcher's Create page. Choose **New Project**, pick the template
   **Vehicle (get in and drive)** (group "Game kits"), give it a name, create it.
2. The scene opens on a two-lane road with a red car (about 4 m long) and a person standing at its driver door, which is
   on the -X side: the car's right side seen from behind. Cones and crates are further down the road. The Hierarchy
   shows `Car` (with its wheels, seats and glass as children), `Passenger Seat`, `Player`, `Road`, `Ground`.
3. Press **Play**.

**Steps (example file).** Same scene without making a project: **File > Open...**, paste
`<install folder>\examples\vehicle\vehicle_test.okayscene`, press Play.

**In play.**
* Walk to the driver door (the side with the steering wheel) with **W A S D** and the mouse (third person: the camera orbits behind you).
* Within about two metres of the driver door a prompt says **Drive**. Press **E**: you sit down in the driver seat (the
  chair sit pose, hands forward near the wheel) and the camera swings behind and above the car.
* **W / S** gas and brake or reverse, **A / D** steer (D turns right), **Space** handbrake. The wheels roll and the
  front wheels turn. The chase camera follows the car. Weave through the cones.
* Press **E** again: you stand up beside the car, the camera goes back to you, and the car brakes to a stop and ignores the keys.
* Walk to the other door (+X side), the prompt says **Ride**. Press **E**: you sit in the passenger seat and are carried
  along, but the car does not drive.
* **First person:** press **V** before getting in. After **E** the camera sits at your eyes in the car.
* **Moving a seat to another car** (a bug fix, see `docs/vehicle_controller.md`): add a second car, press Play, sit in
  the car, drag the seat object onto the other car in the Hierarchy, press **E**. The first car must not drive by itself.

**How you know it worked.** Before you press E the car does not move when you press W. After E, W moves the car and
the prompt is gone. After the second E you are standing outside again and W moves you, not the car.

**First or third person.** The scene's player is third person. A first-person player can also board (the seat accepts
a player with a first or third person controller). To try it, select `Player`, remove the Third Person Controller and
add a First Person Controller in the Inspector. Unverified in the editor.

**Known rough.** From `docs/vehicle_controller.md`: hands do not hold the wheel (generic chair sit); the car is a stack
of boxes; first person has no mouse look from the seat; the look was only checked in software-renderer screenshots; multiplayer does not replicate seats; no script or visual
script node for enter and exit yet; a scene saved while someone sits stores the person standing free.

**Tested on main:** the headless test `vehicle_seat` loads `examples/vehicle/vehicle_test.okayscene` from disk and plays
the get in, drive, get out and ride steps with simulated key presses. **Unverified:** the real editor window, the
chooser card, the Windows exe, the camera feel. The editor method `NewVehicleSeats3D` was added by copying
`NewVehicle3D` and was not compiled on this machine (the editor is not built here).

## 2. First person head bob and Reduce Motion

**What it is.** The first-person camera bobs and sways while you walk and dips on a hard landing. With Reduce Motion
on, the bob and sway ease out and the landing dip is gone. Footstep events still fire.

**Where the setting is.** It is a row on the game's **Settings screen** (a UI panel with a **Reduce motion** switch,
saved as `a11y.reduceMotion` in the game's prefs). The editor itself has no accessibility window.

**Steps.**
1. New Project > **First Person**. Press Play, walk with **W A S D**, and watch the camera: it bobs up and down a little
   and sways. Press Stop.
2. **GameObject > UI > Prebuilt Screens > Settings**. A Settings panel is added to the scene (the Console says what it
   hid, if anything).
3. Press Play again. The Settings panel is on screen: click **Reduce motion** so it is on, then click **Done**.
4. Walk again. The bob and sway are gone (the camera glides). Jump off a ledge or from a jump: no dip on landing.
5. Open Settings again and turn Reduce motion off: the bob eases back in.

**How you know it worked.** The same walk, with the switch off, bobs; with it on, the view is steady.

**Status.** Tested on main: the headless test `reduce_motion_bob`. Unverified: steps 2 to 5 in the real editor (the menu
path and that Done hides the panel and gives you the mouse back for walking were read from code, not clicked).

## 3. Dropdown: drag scrolls a long list

**What it is.** A UI Dropdown with more options than it shows. Press and drag inside the open list to scroll it by whole
rows. A plain tap still picks a row, and after a drag, letting go never picks.

**Steps.**
1. New Project > 3D Scene (or any). **GameObject > UI > Dropdown**.
2. In the Inspector set **Max Visible** to 4 and add at least 10 options.
3. Press Play. Click the dropdown to open it. Press the mouse on a row, move the mouse more than 10 pixels up or down
   while holding, and the list scrolls row by row; let go: nothing is picked.
4. Open it again and click a row without moving: that row is picked.

**How you know it worked.** The list moves under the drag and the selected value does not change until you tap.

**Status.** Tested on main: `runtime_ui` test (drag, tap, clamp). Unverified: a real mouse or finger in the editor.
There is no momentum (no flick). Touch only exists on a touch screen.

## 4. Play start: shared audio and the skipped navmesh bake

Two speed-ups to Play start (`docs/audio.md`, `docs/navigation.md`).

**Audio sharing.** Sources that use the same sound file now hold one decoded copy. **There is no log line or other
sign of this in the editor**: it only saves memory and time. Open `examples\spatial_audio\spatial_audio.okayscene`
(seven sources on one `tone.wav`) and press Play: it should start without a pause, and playing a second time starts at
once. That is as far as you can see it. Honest gap: a "N sources share M clips" Console line would make this visible; it
does not exist yet. Tested on main: the headless tests (`play_start_bench` and the audio tests).

**Skipped navmesh bake.** A Nav Mesh Surface with **Bake before Play** on re-bakes when Play starts, unless nothing it
reads has changed since the last bake.
1. New Project > 3D Scene. Open **View > Editors & Tools > Navigation** and click **Add Nav Mesh Surface**.
2. Click **Bake** once. The Console says `Navigation: baked N polygons, ... in X ms -> <file>.okaynav`.
3. With **Bake before Play** ticked, press Play, then Stop, then Play again, touching nothing.
4. The Console now also says `Navigation: bake skipped, nothing the bake reads has changed since the last one`
   (this line is new in this change; the older build only repeated the "baked" line with the old numbers).
5. Move the floor or any collider, press Play: no "skipped" line, a fresh "baked" line.

**Status.** Unverified in the editor (the skip logic is covered by headless tests; the new Console line is a one-line
edit that was not compiled here).

## 5. Navmesh from a Mesh Collider

**What it is.** The navigation bake now reads the real triangles of a Mesh Collider, not just its bounding box. A
ramp, a floor with a hole or a concave room bakes as it is.

**Steps.**
1. New Project > 3D Scene. Add an object with a Mesh Collider 3D: select the floor or a cube, **Add Component >
   Mesh Collider 3D** (any object that has a mesh can take one), and remove the Box Collider if it has one.
2. **View > Editors & Tools > Navigation** > **Add Nav Mesh Surface** > **Bake**.
3. The Console prints `Navigation: baked N polygons, M links from K cells`. The Navigation window shows the polygons.
4. For a visible difference use a ramp or an L-shaped mesh: the walkable area follows its real shape (a box collider
   would give a plain rectangle).

**Limits.** Meshes over 2,000,000 triangles are cut off with a warning in the Console (`Navigation: the bake may be
missing geometry`). One crack anywhere in a mesh makes the whole collider two-sided (`docs/navigation.md`).

**Status.** Tested on main: the headless test `navmesh_mesh_collider`. Unverified: the editor windows and menu names.

## 6. OBJ texture path hardening

**What it is.** A model's material file can name a texture by any path. Network paths (`\\host\share`), device names,
and absolute or `..\` paths that leave the allowed folders are now refused before the disk is touched. The model still
loads, just without that texture. **There is no Console message for a refusal.**

**Steps.** `examples\obj_texture_paths` holds three one-cube models and `grid.png`:

| File | Its material names | Should look like |
|---|---|---|
| `cube_ok.obj` | `grid.png` (next to it) | cube with the grid texture |
| `cube_outside.obj` | `../../Assets/Textures/grid.png` (outside the model folder and not in a project) | plain white cube |
| `cube_network.obj` | `\\10.255.255.1\share\grid.png` | plain white cube, and the editor does not hang waiting for a network |

1. In the editor, **File > Import Model...**, choose `cube_ok.obj`. The cube should show the grid.
2. Import `cube_outside.obj` and `cube_network.obj`. Both should be plain, and the editor must stay responsive
   (the network one is the point: it must never try to reach `10.255.255.1`).

**Status.** Tested on main: headless `obj_texture_roots` test (72 checks). Verified here: a direct `Mesh::LoadOBJ` of these
three files laid out like the release (`examples/obj_texture_paths` two levels below an `Assets/Textures/grid.png`):
`cube_ok` gave texture `grid.png` and 24 vertices; `cube_outside` and `cube_network` loaded the same 24 vertices with no
texture. Unverified: what **Import Model...** does with the texture after it
imports (it may copy the model into the project and change where "next to it" is), the editor display, and Windows.

## 7. Turn interpolation

**What it is.** Physics runs at a fixed rate (default 50 steps a second) and each frame draws a blend between steps. A
character that turns every frame in game code used to break that blend, so it moved unevenly. Now position stays smooth
and your turn is drawn as you set it.

**Steps.**
1. New Project > **Third Person**. **Edit > Project Settings...** > Physics: set **Steps per second** to 10 and tick
   **Smooth the motion**.
2. Play. Walk in a tight circle with W and the mouse (or A/D) at a high frame rate.
3. The character slides smoothly. Then untick **Smooth the motion**: at 10 steps a second the movement visibly
   steps. (The fix itself is that with smoothing on, turning no longer makes it stutter.)

**Status.** Tested on main: headless `turn_interpolation` and `controller_interpolation` tests (a real third person
controller at 30, 60 and 144 fps; the test failed on the old code and passes now). Unverified: the feel in the editor.
The before-and-after of the bug needs the old release to compare.

---

## 7b. Template cards no longer make projects by themselves

Why: some people found about ten projects in the Projects list that they never made. Before this change, one click
(or a double click, or a click while scrolling) on a card on the Templates page made a project at once ("Platformer",
"Platformer 2" and so on) and opened the editor.

1. Open the launcher (`OkaySpace.exe`), go to **Templates** and click any card, for example Platformer.
2. You should land on **Create** with the **New Project** window open on the Details step, that template chosen and the
   name filled in. Look in your Projects folder: no new folder yet.
3. Close the window with Esc (still nothing made), or press Next, then **Create project** to make it.

Cleaning up the extra projects (this note deletes nothing for you): on **Create**, the projects list shows every
folder in your Projects folder that has an Assets folder. The folder is shown on the Create page as the Location.
For each one you do not want, right-click it (or use its menu) and choose **Remove from list** to only hide it, or
**Delete project...** to delete its folder after a confirmation. Names like "Platformer 2" are the repeats. Projects
you did not open since they were made are safe to remove; check the path before you delete.

**Status.** Tested on main once merged: `test_project_create` (289 checks) covers the rule that a template shortcut opens
the Details step and cannot start a second create. Unverified: the launcher screens themselves (the launcher was not
compiled or run on this machine).

---

## 7g. Install, uninstall and Versions: look at today's behaviour yourself (audit)

Why: the install audit (`docs/install_audit.md`) found things it could only read in code or run on a Linux test machine. Nothing
in the program changed in that branch. These steps let you see on Windows whether it is right. They use a **copy** of a project, never
a real one, because two of the checks touch projects.

1. **Where do my projects go?** Install with `OkaySpace-Setup.exe`. Open the launcher, **Create**, make a project. Paste
   `%LOCALAPPDATA%\OkaySpace\Projects` into the Explorer address bar. The audit says your project is **there** (the Setup welcome
   page says Documents; it is wrong).
2. **What does uninstall leave?** In the launcher open **Versions** and install one older version. Then uninstall from
   **Settings > Apps > Installed apps > OkaySpace**. Look again at `%LOCALAPPDATA%\OkaySpace` and `%APPDATA%\OkaySpace`. The
   audit predicts these are left: `Versions\`, `Projects\`, `downloads\`, `community\`, the `.log` and `okay_*.txt` files, `imgui.ini`,
   `achievements.txt`, and in `%APPDATA%\OkaySpace` the `session.txt`, `accounts.db` and `servers.cfg`. Please tell us if the list
   differs (that is how the audit gets checked).
3. **Uninstall with the launcher open.** Install again, leave the launcher open, uninstall. The audit predicts `OkaySpace.exe` stays
   and the Apps list no longer shows the program.
4. **Does an old editor update itself?** (Use a copy of a project.) Install an older version on the Versions page, right-click the
   copy of a project > **Open with** that version. The audit predicts an **Engine Update** window; do **not** press Download & Install
   unless you want to see the problem (the old editor is replaced by the newest one and reopens your project without asking).

**Status.** Windows steps unverified. What was run: the installer under wine on Linux (install, update, uninstall, running programs,
full disk, odd folders) and two small test programs against the real Versions and project-gate code. Details, results and the plan to
fix each item are in `docs/install_audit.md`; the comparison with other launchers is `docs/install_comparison.md`.

## 7j. Nameplates that show their own character, and a sign with rich text

**What it is.** A small flat scene with two characters, a Goblin Scout and an Orc Chief. Above each one is a nameplate (a
dark card with the name, a gold level pill and a red health bar). Each plate is a child of its character and shows **that**
character's name, level and health, live. The Goblin loses a little health by itself soon after Play starts, so you can see
its bar drop. Behind them is a wooden sign whose text uses the rich-text tags: a big bold title, a gold line, red bold and
italic words and a small line. Code: `UIOwnerData` (the `owner.` keys), `UIGallery::AddNameplate`, `ui2d::DrawWorldText`
(`docs/ui_runtime.md`, `docs/ui_world_space.md`).

**Steps.**
1. Open the editor (`Tools\OkayEngine.exe`, or start it from the launcher). **File > Open...**, paste
   `<install folder>\examples\ui_text\ui_text.okayscene` (installer: `%LOCALAPPDATA%\OkaySpace\examples\ui_text\ui_text.okayscene`),
   press Open.
2. The Hierarchy lists `Main Camera`, `Ground`, `Sign Post`, `Sign Board`, `Sign Text`, `Goblin Scout`, `Orc Chief`,
   `Canvas` (with `Hints`), `EventSystem` and `Director`. Expand `Goblin Scout`: it has a child `Nameplate`.
3. The scene is flat (2D). In the **Scene** view header the button next to the view name says **3D** or **2D**: if it says
   3D, click it once so it says 2D. Look at the **Game** tab for the plates (see "Known rough").
4. Press **Play**. In the Game view you should see a blue sky, green ground, the brown sign at the top, a green box at the
   left (the Goblin) and a red-brown box at the right (the Orc). Above each box is a plate: the name on the left
   ("Goblin Scout", "Orc Chief"), a gold pill on the right ("Lv 3", "Lv 12"), and a red bar under them, full at the start.
5. About a second after Play the Goblin's bar drops, then drops again every 1.2 seconds, four times. The Orc's bar does not move.
6. Click once in the Game view so it has the keyboard, then press **1** (the Goblin loses 8 health), **2** (the Orc loses 12),
   **3** (the Goblin gains 100 experience: the pill changes to "Lv 4" and its bar fills up again) and **H** (both are healed).
   Each plate changes only for its own character.
7. Stop Play. Expand `Goblin Scout > Nameplate > SwatchPlate`, select `Name` and look at **UI Text Bind** in the Inspector.
   The **Format** row is `{owner.name|Goblin}`; hover it for the list of keys; the line under the rows says
   `Owner: Goblin Scout`. Select `SwatchLevel > Label` (format `Lv {owner.level|1}`) and `Health` (UI Bar Bind, variable
   `owner.healthFrac`).
8. Select `Sign Text`. In its **Text** section **Rich Text** is ticked (hover it for the tags). In the text box, wrap a word
   in `[size=250]` ... `[/size]` and look at the Scene or Game view: the lines under it move down and never overlap (before this
   change the big letters climbed into the line above).
9. Make your own (optional): select any object that has a **Stats** component (**Add Component > Stats** on a new sprite or
   cube works), then **GameObject > UI > Prebuilt Screens > Nameplate (in the world)**. The new `Nameplate` becomes that
   object's child, above its head, and shows its name, level and health when you press Play. With nothing suitable
   selected it is made at the scene root and you drag it onto a character in the Hierarchy yourself.
10. The player draws it too (the flat pass that draws world text and the nameplates): in a command prompt in the install
    folder run `Tools\OkaySpacePlayer.exe examples\ui_text\ui_text.okayscene`. Press **1**, **2**, **3** and **H** as in step 6.

**How you know it worked.** The two plates show different names, levels and bars, and a key press moves only the plate of
the character it hurts. The title line of the sign is on its own line and the lines under it start below it. The level pill is
inside the card at any size of the window.

**Known rough.** World text (the sign) is drawn in flat scenes only; a 3D scene does not draw it. The nameplate is a
World Space UI object: the Game view and the player draw it; whether the Scene view previews it is unverified. The sign uses
the 8x8 pixel font (no new font system). A big `[size]` run does not change how a line is wrapped or centred.

**Pictures** (headless renders through the same UI pass as the game, not a real window; they are in `docs/images/ui_text/`;
the test `ui_text_shots` makes the "after" ones again, the two "before" ones are the unmerged branches' own renders):

| File | What it shows |
|---|---|
| `before_nameplate_overhang.png` | Before: the level pill sticks out of the card, the texts sit at the top-left of their boxes, the card is smaller than its contents |
| `after_nameplate_scaled_with_distance.png` | After, same camera and scale (90 design pixels per unit, scaling with distance): everything inside the card, the level centred in its pill |
| `after_nameplate_default.png` | After, with the default Constant size: the plate keeps 220 x 62 pixels, the text stays readable |
| `before_world_text_overlap.png` | Before: the big word "larger" climbs into the line above it |
| `after_world_text.png` | After: the line with the big word is as tall as it, the lines below start under it |
| `sample_scene_start.png` | `examples\ui_text` at the start of Play: two plates with full bars, the sign, the hints |
| `sample_scene_after_hits.png` | The same after about six seconds and a press of 2: the Goblin's bar is at 40 percent, the Orc's at 90 |

**Status.** Tested here on Linux, headless (no window), every run with 0 failures:
* `ui_nameplate_bound` 158 checks (where every piece of the plate is drawn, at three distances and with two fonts, a long
  name, a three-digit level; the `owner.` keys; save and load), `world_text_rich` 272 (the line height and the rich-text tags),
  `ui_text_example` 35 (loads `examples/ui_text/ui_text.okayscene` from disk and plays it: ticks, the script, key presses, the
  level-up), `ui_text_shots` 15 (the pictures above), `runtime_ui` 1303.
* The older UI suites that draw through the changed renderer: `ui` 178, `ui_extra` 150, `uiwidgets` 28, `ui_templates` 152,
  `ui_widgets_more_shots` 9, `accessibility_render` 17, `ui_focus_render` 7, `ui_system` 84, `uidocument` 83. ctest on the
  eleven suites that touch this change (`text`, `runtime_ui`, `ui_renderer_options`, `save_coverage`, `worldcanvas`,
  `ui_renderer_golden`, `ui_text_shots`, `accessibility_wired`, `ui_nameplate_bound`, `world_text_rich`, `ui_text_example`):
  11 of 11 passed.
* Reproduced before fixing: the same two new tests, linked with the renderer from before this change, fail 24 of 158
  (`ui_nameplate_bound`) and 2 of 272 (`world_text_rich`) checks; with the fix both are 0.
* All of the above ran on the branch before two merges: `main` (one SHA-256, vehicles, owner rules; no conflicts) and Claude 2's
  nine newer commits of `claude2/world-text-rich` (its own fix for the same line-overlap problem, which was compared with
  mine and left out of the code, plus its overlap test, its picture block and its text; conflicts in `docs/ui_runtime.md`,
  `editor/main.cpp` and `tests/test_world_text_rich.cpp`). After those merges only the cheap checks were repeated: a configure
  of the merged tree, the feature map check, the branch lint, the MinGW win32 syntax check and a syntax-only compile of the
  editor, the player, the renderer and the tests. **The suites were not re-run on the merged tree here** (the shared build
  queue was too long and the disk too full for a second build tree); CI does that. Claude 2's new overlap test was checked
  by hand against the merged layout only.

**Unverified:** the real editor window and menus (steps 1, 3, 7, 8, 9), the Game view drawing of the plates in the editor, the
player run in step 10 (the player's drawing code was only syntax-checked, never run), the Windows exe and the installer copy of
the folder (the example-copy block of `scripts/build-windows-dist.sh` was run alone and does put `examples/ui_text/ui_text.okayscene`
in the output; `bash -n` of the whole script is clean). The editor code for the new Inspector rows and
menu entry was only syntax-checked, not run. The pictures are headless renders, not screenshots of a window.

## 7f. Crash reports: detailed report, sending, GitHub issues

**What it is.** When the editor crashes it now writes a much fuller report (version and build, Windows version, CPU, memory, graphics card, the
scene's name, the last 64 steps, the last 200 Console lines, a backtrace). The launcher shows it, lets you read exactly what would be sent
(names, folders, emails and addresses removed), tells you once that a sent report becomes a **public GitHub issue**, sends it for you (switch **on by
default**), and a GitHub job turns it into an issue. Full story: [crash_reports.md](crash_reports.md).

> **The server half (steps 9 and 10) cannot work until the owner has done two one-time steps, in the browser, in this order:**
> - **Owner step A:** run `docs/sql/crash_reports.sql` once in the Supabase SQL editor (open the project, **SQL editor**, paste the whole file,
>   run it; it is safe to run again).
> - **Owner step B:** set the repository variable `CRASH_ISSUES_ENABLED` to `true` (GitHub repository > **Settings > Secrets and variables >
>   Actions > Variables > New repository variable**). Until it is `true`, the 30-minute schedule skips the job (it shows as skipped, never red).
>
> The GitHub secret `SUPABASE_SERVICE_KEY` must exist too (releases already use it, so it normally does). Never paste a key in chat.
> Steps 1 to 8 and 11 need none of this.

**Steps (Windows release).**
1. **Quit the launcher first.** It is single-instance: a launcher that is already running (also in the tray) never passes `OKAY_TEST_CRASH_LATE` on
   to the editor, so the test crash would not happen. Close the editor. Open **Command Prompt** in the folder with `OkaySpace.exe` (zip: the unzipped
   folder; installer: `%LOCALAPPDATA%\OkaySpace`). To keep the files easy to find, first run: `set OKAY_CRASH_DIR=%TEMP%\crashtest` after
   `mkdir %TEMP%\crashtest`.
2. Run `set OKAY_TEST_CRASH_LATE=Test.Step` and then `OkaySpace.exe` (the launcher). The editor the launcher starts inherits both settings.
3. In the launcher open the editor the way you normally do (it must be started from the launcher). After a couple of seconds, at the 120th frame, the editor crashes on purpose and the box
   **OkaySpace crashed** appears. Click OK.
   (`OKAY_TEST_CRASH=Test.Step` crashes before the window opens instead: the report then has no scene or graphics card yet.)
4. **Where the report is:** `okay_crash.txt` (latest) and `okay_crashlog.txt` (all) in `%TEMP%\crashtest`, or, without `OKAY_CRASH_DIR`, next to
   `OkayEngine.exe` (`Tools\`). Open it in Notepad: you should see `Version`, `Build`, `Last step: Test.Step`, `OS`, `CPU`, `GPU`, `Backtrace`, then
   `--- Breadcrumbs`, `--- Console` (the last line says the crash was on purpose).
5. In the launcher a notification "The editor crashed" appears. Open **Settings > Crash reports**. You should see a card "Access violation"
   with the time and version and, after a dot, **waiting to be sent**. (Crash reports that were already on your computer before the launcher
   first ran this version say "not sent (from before sending was switched on)" and are never sent; only crashes from now on are.) The switch card
   above it shows the same words you will see in the next step: what is sent and that it becomes a **public** GitHub issue.
6. **The one-time notice.** About 15 to 40 seconds after the crash, before anything is sent, a window **Crash reports are sent automatically**
   opens in the launcher. It says what is sent, that it is published as a public issue on GitHub, that the server sees your connection address, and how
   to turn it off. Nothing is sent until you answer. Click **OK, keep sending** to carry on (or **Turn it off**: the card then says "not sent (sending
   is off)"). It appears once. To see it again: quit the launcher, open `launcher.cfg` (next to `OkaySpace.exe`) in Notepad, delete the line
   `crash_notice=1`, save, start the launcher.
7. Click **What would be sent** on the card (or **Preview** in the SENDING CRASH REPORTS card). You should see the report with your folders
   shown as `%USERPROFILE%` where the editor was installed under your user folder, and the columns (report id, signature, version, ...). The test
   report does not contain your computer name, so you will not see `<host>` in it; your login name or computer name would only show as `<user>` or
   `<host>` if it appeared in the text (for example in a Console line). Nothing has been sent by looking.
8. **Turn it off:** switch **Send crash reports automatically** off: the card says "not sent (sending is off)". (Or start the launcher with
   `set OKAY_CRASH_UPLOAD=0`.) Crashes that happen while it is off are never sent later, even after you switch it on again or unset the variable.
   Switch it on again to continue.
9. **Sending** (needs owner step A): after you answered the notice the card changes to **sent** within about 20 seconds and a toast says "Crash report
   sent. Thank you." If the table is missing, the page says "The server has no crash_reports table yet ... run docs/sql/crash_reports.sql" and the
   report is kept. To see the row: Supabase > Table editor > `crash_reports` (or SQL editor:
   `select created_at, signature, version, exception, top_frame, processed_at, issue_number from crash_reports order by created_at desc;`).
10. **The issue** (needs owner steps A and B): GitHub > Actions > **Crash reports to issues** > Run workflow. The **dry run** box is ticked by default:
    the log shows the issue text and changes nothing. Untick it to make the issue for real, or wait up to 30 minutes for the schedule once the variable is
    `true`. An issue `Crash: Access violation in ...` appears with label `crash`; it says the text is untrusted data. Crash again the same way and send: the
    issue gets a **comment**, not a second issue. (The schedule only runs once this change is on `main`.)
11. **Report on GitHub** (no server needed): on the card click **Report on GitHub**. Your browser opens GitHub's new-issue page with the title, a table
    and the backtrace filled in, and the whole cleaned report is on your clipboard: paste it into the issue. Read it, then Submit or close the tab.

**Try the issue text without Windows or any secret** (any machine with Python): `python3 scripts/crash_to_issue.py --rows-file
tests/data/crash_rows_sample.json --dry-run` prints the two issues and one comment the three sample rows would make (the third row had a name, an email,
an address and a token left in; the output shows them removed).

**How you know it worked.** The report has the lines in step 4; the card says what its sending state is; the notice appears once, before the first
send; the preview has no folder with your name in it; with sending off nothing is ever sent, also later; one crash makes one issue, the same crash
again a comment.

**Status.** Verified here (Linux): the report builder, the parser and signature, the handler (a child process really faults: segmentation fault,
divide by zero, abort, uncaught exception, stack overflow; the files are `0600` and a planted symlink is not followed), the scrubber, the upload logic
against a fake server (the notice rule, the ledger that follows the switch), the SQL against a local PostgreSQL (insert-only, no reads, the caps,
30 senders at once, the self-pruning, an upgrade from the first version), the issue script (72 unit tests, 16 over HTTP) and the workflow file's
safety properties. **Unverified:** everything that needs Windows (the Windows handler, the message box, function names from dbghelp), the editor
and launcher as running programs (they were only syntax-checked, so the notice window has not been seen on a screen), the upload to the real
Supabase, the GitHub workflow on GitHub, the browser page of step 11, and `OKAY_TEST_CRASH_LATE` in a running editor.

---

## 16. Main menu bar (cleaned up)

What changed and why: [editor_menus.md](editor_menus.md) (the map, and a table of every old item and where it went).
Where: the menu bar at the top of the editor, `Tools\OkayEngine.exe` (or start the editor from the launcher). Open any project.

1. Click **File**. You should see, in this order: New 2D Scene, New 3D Scene, Open Scene... (Ctrl+O), Open Recent, **one** Examples
   (it used to be three), then Save Scene / Save Scene As... / Save Project with their keys, then New Project... / Open Project Folder... /
   Project Upgrade..., then Import Model... / Merge Scene into Current..., then Autosave and Build Settings... (Ctrl+B), then Exit.
   Open **Examples**: UI Gallery, UI Gallery (phone), Visual Script Node Playground, Node Playground (flow and variables).
   With no recent scenes, hover **Open Recent**: the tip says why it is greyed. Press Play, then open File: **Save Scene** is greyed and
   the tip says "Stop Play first ...".
2. Click **Edit**. Undo and Redo name the step ("Undo Move") and are grey, with a tip, when there is nothing to undo. With nothing selected,
   Cut, Copy, Duplicate and Delete are grey ("Select an object ... first."). Select an object: they work. Ctrl+click three objects in the
   Hierarchy, open Edit again: the labels read **Duplicate 3 Objects** and **Delete 3 Objects**, and Cut is grey with the reason. **Preferences**
   holds UI Scale, Density, Theme and Accent color (they used to be View > Appearance). **Project Settings...** is at the bottom.
3. Click **GameObject**. Create Empty, Create Empty Child and Create Empty Parent (Ctrl+Shift+G) come first; Child and Parent need a selection.
   Then the categories: **2D Object, 3D Object, Player, Light, Camera, Audio, Effects, UI**. **Audio > Audio Source** (new) puts a sound object in
   the scene and selects it; its Audio Source component is in the Inspector. Everything the old menu offered is in one of the categories
   (the table in editor_menus.md says which).
4. Click **View**. **Panels**, **Editors & Tools** and **Layout** are where they always were. Below: **Frame Selected (F)**, **Isolate Selection
   (Shift+I)**, **UI Editing Mode**, **Scene Overlays**, **Camera Bookmarks** (hover "Go to slot 1" before saving one: the tip says to save
   first), **Rendering & Performance**. There is no loose "Reset Layout" any more: it is inside **Layout**.
5. Click **Help**. **Documentation (online)** opens the docs folder on GitHub in your browser. **Report a Bug...** opens the issue page and
   puts `OkaySpace v<version>` on the clipboard (paste it somewhere to see). **Check for Updates...** moved here from the old Engine menu.
6. The **Window** and **Engine** menus are gone: their items were duplicates (Items, Loot & Recipes is in View > Editors & Tools; Build
   Settings is in File) or moved (Check for Updates to Help).

Screenshots of the real editor (built and run on Linux, in a virtual display; not Windows): [docs/images/editor_menus/](images/editor_menus/)
(`file.png`, `file_examples.png`, `file_play_save_greyed.png`, `edit_undo_named.png`, `gameobject.png`, `view.png`, `help.png`). What was and
was not clicked is listed in [editor_menus.md](editor_menus.md) section 3b; "Checks run for this change" is at the end of section 17.

## 17. Hierarchy right-click menus

Where: the **Hierarchy** panel (left side by default). Map and the Existing / Gap table: [editor_menus.md](editor_menus.md), section 2.

1. Right-click an object. The menu starts with Copy, Cut, Paste, Paste as Child, then Rename (F2), Duplicate, Delete, Delete Children, then
   Create Empty Child, **Create Child**, Create Parent, Parent To, Unparent, Order, then Select, Expand All Below, then Deactivate, Lock, Focus in
   Scene, Isolate, Transform, then **Add Component...**, Prefab, Save As Prefab..., Rig & IK, Copy Name or Path.
2. **Create Child > 3D Object > Cube** (the submenu used to hold only 7 shapes; it now has 2D Object, 3D Object, Player, Light, Camera,
   Audio, Effects, UI). A cube appears inside the object you clicked, at its origin, and is selected. **Ctrl+Z** removes it.
3. Ctrl+click three objects, right-click one: the menu says **Delete 3 Objects**, **Duplicate 3 Objects**, **Deactivate 3 Objects**,
   **Group 3 Objects**, and Cut is grey with the reason.
4. **Select > Same Type (Light)** (the kind is the row's icon: Camera, Light, Mesh, Sprite, ...): every object of that kind is selected.
   If there is no other one the item is grey and says so.
5. **Add Component...**: the object is selected, the Inspector comes forward and its Add Component browser opens, ready to type in.
6. Right-click the first row: **Move Up** and **Move to Top** are grey ("It is already the first of its siblings."). Right-click an object with
   no children: **Delete Children** is grey ("This object has no children.").
7. Right-click the empty space under the rows: Create Empty, the eight Create categories, Paste, Select All, Deselect, Expand All, Collapse All,
   Sort A-Z. The **[+]** button at the top has Create Empty, Create Empty Child, Group Selected and the same categories.

Screenshots of the real editor (Linux, virtual display): [docs/images/editor_menus/](images/editor_menus/) (`hierarchy_row_menu.png`,
`hierarchy_create_child.png`, `hierarchy_create_child_result.png`, `hierarchy_three_selected.png`, `hierarchy_same_type_reason.png`,
`hierarchy_add_component.png`, `hierarchy_empty_space.png`).

**Checks run for this change** (menus, rows 16 and 17). Run here, on Linux, and seen:

* `tests/test_menu_bar_model.cpp` (ctest `menu_bar_model`) and `tests/test_hierarchy_context_model.cpp` (`hierarchy_context_model`), built
  with `g++ -std=c++17 -Wall -Wextra` and run: no failures (3079 and 2064 checks). The two tests also read `editor/*.cpp` and fail if a
  menu id has no handler: that was shown by renaming a handler and seeing the test fail, then restoring it.
* `editor/main.cpp` and `editor/UIRuntimeTools.cpp` with the editor's flags and `-fsyntax-only -Wall -Wextra`: no error and no warning in the
  files changed.
* **The real editor**, built from this branch (`okay-editor`, Linux, engine `-O1`, `main.cpp` `-O0`, asserts on) and run under Xvfb with the
  software renderer, started with `--dev`, the mouse driven by `xdotool`: the clicks listed in [editor_menus.md](editor_menus.md) section 3b
  worked as described (Create Child and its undo, Add Component, Same Type, Cut / Paste, Delete, Create Empty Parent / Child, Isolate, Panels,
  Audio Source, the greyed items and their reasons, also while the game runs). No crash and no
  Dear ImGui assert while clicking, in 15 sessions of this kind. The one assert seen was at shutdown, in two sessions that were closed with a
  menu still open (`ImGui::DestroyPlatformWindow`, a pop-out menu's window still alive when the context is destroyed); asserts are off in a
  release build, and it was not looked into further.
* `python3 scripts/gen_feature_map.py --check`, `scripts/branch_lint.py all`, `scripts/bug_patterns.py`: clean.

**Not seen:** anything on Windows (the editor was never run there); the Windows build (MinGW) of the changed files was not compiled; the
items listed as "not clicked" in editor_menus.md section 3b (among them the Order submenu and Open Recent greyed with no history: the test
project always has its scene in the list); Help > Documentation and Report a Bug open a browser, which this machine does not
have, so the page and the clipboard line were not seen. The software renderer was used, so nothing about the GPU path was seen.
## 7h. Project panel right-click menus

**What it is.** The right-click menus of the editor's Project panel were reworked and filled out: one list per place you can click
(an asset, a folder, a folder in the tree on the left, a starred item, the empty space), with a tooltip on every item, greyed items that
say why, and counts when several items are selected ("Delete 3 Items"). Full list of items: `docs/project_panel.md`, section
"Right-click menus". Code: `editor/ProjectContextModel.hpp` (the lists), `editor/ProjectPanelUI.cpp` (drawing, the Move to...
picker, Properties), `DrawProject` in `editor/main.cpp`.

**Getting there.** Open the editor (`Tools\OkayEngine.exe`, or from the launcher). **File > New Project...**, pick any template and
create it (a project is needed: with none open the menus grey out and say "Open a project first."). The **Project** panel shows an
**Assets** folder; if it is hidden use **View > Panels > Project**. Put a few files in it: drag a picture, a sound or a model from your file
manager onto the editor window, or use the Project panel's **Import** button.

**Steps.** (Each step says what you should see.)
1. **An asset.** Right-click a picture. You should see Open (greyed for a picture; the tooltip says "No editor window opens this type. Use Open with Default App."), Rename (F2), Duplicate (Ctrl+D), Cut, Copy, Paste (greyed: "Nothing was
   copied or cut yet."), **Move to...**, Delete, Find References, Show Dependencies, Reimport, Add to Favourites, Tags, Label, **Copy Text**,
   Show in Explorer, **Properties...**, and **Open with Default App**. Hover an item for its tooltip.
   * **Copy Text > Relative Path**, then paste into any text box (the Console filter, a script): `Assets/<your file>`.
   * **Properties...** opens a window: type, folder, size, date in UTC with "N minutes ago", GUID, how many assets use it. **Close** closes it.
   * **Open with Default App** opens the picture in your system's picture program.
   * **Show in Explorer** opens the folder with the file selected.
2. **Several items.** Click one file, Ctrl+click two more, right-click one of them. The first line says **3 items selected**; the items read
   **Delete 3 Items**, **Duplicate 3 Items**, **Copy 3 Items**; Rename and Open are greyed ("Select one item to rename it." / "Select one item to open it."). **Copy Text > Relative Paths
   (one per line)**, then paste into a multi-line box: one path per line. **Duplicate 3 Items** makes three copies ("name copy"); one Ctrl+Z takes all three away.
   Select a folder and a file inside it (open the folder first and use the search box with `ext:png` to list both, or Ctrl+A in a search over the whole project),
   then Duplicate: the folder is copied once (the copy has its own copy of the file); no second "copy" appears beside the original file.
3. **Move to...** Right-click a file, **Move to...**, pick a folder in the window (your Assets folder and its folders; the file's own folder is greyed:
   "It is already in that folder."), press **Move**. Ctrl+Z in the Project panel moves it back. Try it on a folder and pick that folder or a folder inside
   it: they are greyed with "A folder can't be moved into itself." Try it with Ctrl+A in a big folder: the window opens at once.
4. **A scene.** Right-click a `.okayscene` (File > New Scene, then save it into Assets): **Set as Start Scene** puts it first in Scenes In Build.
   Check in **View > Panels > Scene Manager**: it shows "Start scene: <name>". Right-click it again: the item is greyed ("This scene already starts the game.").
5. **A script or data file.** Right-click a script (Create > Script): **Open in External Editor** opens it in Visual Studio Code if you have it,
   else in Notepad. The file is only opened for editing.
6. **A program or script is never started.** In your file manager make a file `test.bat` (any text) in the project's `Assets` folder; wait a moment or press F5
   in the Project panel. Right-click it: **Open with Default App** is greyed and the tooltip says "This file type can run code; open it from the file manager if you
   trust it." There is no Open in External Editor for it. **Show in Explorer** still opens the folder with it selected. The same goes for `.exe .cmd .lnk .ps1 .vbs .js .hta .jar .msi .reg` (and `.sh .py`);
   a picture named `photo.png.exe` counts as an exe. A file named with a `$`, `%` or `'` in it can be shown in Explorer and opened (a text file) like any other.
7. **A folder.** Right-click a folder in the grid: **Create** (made inside that folder; the panel goes there), Paste Into, **Reimport All in Folder**, Expand /
   Collapse. Right-click a folder in the **tree** on the left: the same list, and **Expand All Subfolders**, **Collapse All Subfolders**, **Collapse Other Folders**
   change what the tree shows. Right-click **Assets** in the tree: Rename, Delete, Move and the others are greyed ("The Assets folder itself can't be changed.").
   * **Reimport All in Folder:** put a picture in a folder, open the folder, then replace the picture with a different one in an image editor (keep the name) and save.
     Right-click the folder > Reimport All in Folder. The Console says "Reimported 1 asset" (or more) and the tile shows the new picture and the strip at the bottom the new size.
   * **Create from a folder's menu:** right-click a folder (not the one you are in) > Create > Folder. The name prompt opens; after you press Create the panel shows that folder with
     the new one selected. **Create > Standard Folders** makes Scenes, Scripts, Prefabs, Materials, Models, Textures, Audio (the Console says how many); Ctrl+Z removes them all together.
     **Create > Loot Table** (or Recipe Book) makes "Loot Table.okayloot" in that folder (no name prompt; F2 renames it) and opens the Items window on it.
   * **The folder on screen goes away:** open a folder, then in the **tree** right-click that same folder > Rename and give it a new name: the panel follows to the renamed folder.
     Now right-click it in the tree > Delete and confirm: the panel moves up to the folder above it, Create works there, and Ctrl+Z brings the folder back.
   * **Tree actions leave your selection alone:** select a file in the grid, then in the tree right-click some other folder > Duplicate. The copy appears in that folder's parent,
     and the file you selected is still the selected one (the strip at the bottom still shows it).
8. **Favourites.** Add a file and a folder to Favourites. They appear under FAVOURITES on the left. Right-click one: **Show in Project** / **Open Folder**,
   Remove from Favourites, Copy Text, Show in Explorer, Properties. Remove a starred file that is in another folder, then press Del: nothing is deleted (nothing was selected by that menu).
9. **Prefab items.** Create > Prefab (or select an object in the Hierarchy first and use **Prefab from Selection**). Right-click the new `.okayprefab`:
   * **Open Prefab** shows the prefab alone in the Scene view (double-click does the same).
   * **Place in Scene** adds an instance: it appears in the Hierarchy and the Console says "Instantiated ...".
   * **Place as Child of Selected** is greyed ("Select an object in the Hierarchy first.") until an object is selected; then the instance appears under that object.
   * **Create Variant** makes "<name> Variant.okayprefab" next to it (the Console says so); **Validate Prefab** writes "Prefab <name> is fine (1 file checked)" to the Console.
10. **Tags and Label.** Right-click a picture > **Tags > Add Tag...**, type `hero`, press Add. The strip at the bottom shows "Tags: hero"; type `l:hero` in the search box and the picture is listed.
    **Tags > Remove "hero"** takes it off again. **Label > Red** puts a red dot at the top right of the tile and lists "Red 1" under LABELS on the left; **Label > None** clears it.
11. **Empty space.** Right-click empty space in the grid. **Create** now also has **Dialogue**, **Quest File**, **Behavior Tree**, **Cutscene (Timeline)**:
    each asks for a name, creates the file in the folder you are looking at and opens it in its own window (Dialogue, Dialogue > Quests tab, Behavior Tree, Timeline).
    Also: **Paste** with a count, **Undo <what you did>**, **Import New Asset...** (the file picker), **Select All**, **View** (Grid, List, Small / Medium / Large
    Tiles), **Sort By**, **Up One Folder**, **Open Project Folder in Explorer**, **Properties...** of the folder.
    * **Refresh (F5):** with the Project panel focused press F5, or choose Refresh. The Console says "Refreshed the Project panel: N assets indexed". Copy a new file into `Assets` with your file
      manager and press F5: it is listed at once (without F5 it can take a few seconds).
    * **Select All (Ctrl+A):** every tile gets the accent outline and the strip at the bottom says "N items selected (Delete removes all)".
12. **Smart view.** Click **All Models** (or another smart filter) on the left, then right-click empty space: Create, Paste, Import are greyed and say how to leave the view.

**How you know it worked.** Each step names what you should see. Anything you right-click that does nothing, or an item that stays greyed without a reason in its
tooltip, is a bug: tell us which item.

**Left out on purpose.** Export Package (no package exporter exists), Select Dependencies, a Localization table and an Animation in Create: reasons in
`docs/project_panel.md`.

**Status.** The item lists, labels, greyed reasons, Copy Text, which file types run code, the Move to... rule, the path helpers (nested selections, the nearest folder that exists),
Set as Start Scene, the dates and the Properties rows are tested headlessly (`project_context_model`, `project_menu_ops`). The new story and AI files are read back with the engine's parsers,
and Standard Folders, Duplicate of a selection and the tags a menu lists are tested on temp folders, in `project_menu_ops`. The launcher that starts Notepad, the default app or Explorer
(`editor/LaunchFile.cpp`) is tested on Linux against fake programs on PATH (`launch_file`): a hostile file name reaches the program as one untouched argument and program and script types are
refused before anything starts; its Windows half (`ShellExecuteW`) was only compile-checked with the MinGW cross compiler. **Unverified:** everything you see in the real window (drawing, tooltips, the Move to... and Properties
windows, the Explorer, Notepad and default-app launches on Windows, the tree's expand and collapse, the panel following a renamed folder). Nothing here was run on Windows. The changed editor files (`editor/main.cpp`, `ProjectPanelUI.cpp`, `ProjectMenuDraw.cpp`, `ProjectBrowser.cpp`, `ContentEditors.cpp`, `LaunchFile.cpp`) were compiled with `-fsyntax-only` on Linux with the editor's real flags (no error; `main.cpp` has older warnings of its own) and the launcher, the panel and the menu drawing also with the MinGW Windows cross compiler; the editor was not linked or started.
**Pictures:** `docs/images/project_menus/*.png` (listed in `docs/project_panel.md`) are headless renders, **not screenshots of the editor window**: the real item lists and the real menu drawing code, drawn by Dear ImGui into a software surface with ImGui's default look and font (`tools/project_menu_shots.cpp`). The editor itself could not be built and run on the machine that wrote this, so there is no screenshot of the real window; the OkayUI panels have no Project panel, so there is no second copy of this menu to keep in step.

---

## 7d. Add Node menu: 70 topics instead of 19

**What it is.** The panel that opens when you add a node to a visual script (right-click the canvas, press Space, press
Add node, or drop a wire on empty space). It had 19 topics, and one of them, Advanced, held 515 nodes. Now it has 70
specific topics in 14 sections, and every node is in the topic you would look in. Nothing was removed and your saved
graphs are unchanged (a graph stores a node's type, never its topic). Code: `editor/VisualScriptCategories.hpp`,
`editor/VisualScriptPlaceRules.hpp`; the list and the rules are in [visual_scripting_naming.md](visual_scripting_naming.md),
every node under its topic in [visual_scripting_topics.md](visual_scripting_topics.md), and the before-and-after
measurement in [visual_script_category_audit.md](visual_script_category_audit.md).

**How to reach it.**

1. Open the editor (`Tools\OkayEngine.exe`, or the launcher), make or open any project, select an object (for example
   the cube of a new project) and add a **Visual Script** component in the Inspector.
2. In the Inspector's Visual Script section press **Open in node editor** (or open **View > Editors & Tools > Visual
   Script**).
3. Right-click the empty canvas, or press **Space**. The Add Node panel opens.

**What you should see.**

1. On the left, under Starter, Recently used, All and Favourites, the topics in small groups under grey headings:
   Events, Flow, Variables / Lists & Saving, Math, Text, Objects, Movement & Physics, Gameplay, Characters & AI,
   Sound / Camera & Screen, Scenes & Environment, Input, Online, Debugging. Each topic shows how many nodes it holds.
   **Click a heading to fold its topics** (click again to open them). Hover a topic for a one-line description.
2. Click a topic with many nodes (for example Numbers, Tweens or Multiplayer): the subcategories appear under it
   (Rounding & Snapping, Limits & Wrapping, ... ). Hover one for its description. Click one to see only its nodes.
3. Twelve nodes and where to find each (the name on the node, then topic / subcategory):

   | Node | Where it is |
   |---|---|
   | On Start, On Every Frame | Game Events |
   | On Key Pressed | Input Events (it used to be under Input / Keys) |
   | On Collision Start | Collision & Trigger Events |
   | Wait Seconds | Waiting & Timers |
   | Spawn | Create & Destroy |
   | Move Towards | Move & Follow |
   | Add Force | Forces & Velocity |
   | Set Weather | Weather & Fog (it used to be under Scene / Atmosphere) |
   | Play Sound | Sound Effects |
   | Start Quest | Quests & Dialogue / Quests |
   | Host Game | Multiplayer / Hosting & Joining |

4. **Search still finds everything.** Type `weather`: Set Weather and its twin Weather Set are at the top, and both sit in
   Weather & Fog. Type `scene` or
   `atmosphere`: the nodes that used to be under Scene (Set Weather, Set Fog, Set Post Effect, ...) are in the list too,
   because the old topic name is kept as a search word. Type `advanced`: the text-script functions that used to be in
   the Advanced bucket are listed.
5. **Favourites keep working.** Click the star on On Key Pressed, close the panel and open it again: the star is still
   there (it is stored by node name in `okay_vsnodes.txt`, never by topic).

Screenshots (headless renders: the real Add Node panel code drawn with ImGui's software renderer and its default font, so
the font and window frame differ from the editor):

* [Numbers open, with its subcategories and the hover description](images/vs_palette_topics.png)
* [Typing `weather`: nine nodes, all in Weather & Fog](images/vs_palette_search_weather.png)
* [The Events section folded away](images/vs_palette_folded_section.png)

Status: the topic list, the rule that every node has exactly one place, the size limits, the search words for old topic
names and the favourites keys are checked by `tests/test_vs_categories.cpp` (compiled and run with g++ on Linux). The
counts above come from that test's dump. The three screenshots show the panel working with the new topics. **Unverified:**
how the sidebar looks and feels in the real editor window (font, dot colours, scrolling); nothing was run on Windows.

## 7r. The walk looks like a walk, Shift looks like a run

Why: "the character walking is basically running". The player used to move at 4.5 m/s for plain W, which is a jog, and at 8 m/s for
Shift, which is a sprint, so both looked like running (the legs stepped about 270 times a minute for W and 250 for Shift). Now W is a
real walk (2 m/s, about 125 steps a minute, one foot always on the ground, arms hanging and swinging a little) and Shift is a real run
(5.5 m/s, about 185 steps a minute, both feet leave the ground, elbows bent, chest leaning forward). The full numbers, the targets and
the pictures are in [walk_vs_run.md](walk_vs_run.md).

**A new project.**
1. Open the launcher (`OkaySpace.exe`) > **Create** > **New Project**. On the first step pick **Third Person** (under **All templates** if the starter
   cards do not show it), press **Next**, name the project, press **Next**, then **Create project**. (**First Person** works too: the body is not drawn from
   the eyes, so press **V** in Play to switch to the third-person view and watch it.)
2. In the editor press **Play** (the triangle at the top). Click the Game view once so it has the keyboard.
3. Hold **W**. You should see the character walk: an easy stride, the arms hanging with a small swing, the body upright, never both feet
   off the ground. The camera moves slowly: 2 m/s.
4. Now hold **Shift + W**. You should see it run: a faster step, the elbows bent, the chest leaning forward, and both feet leaving the ground
   for a moment each step; the camera moves much faster. Let go of Shift: it goes back to the walk with no flicker.
5. Select the Player in the Hierarchy and look at **Player Controller** (Third Person settings): **Walk Speed 2**, **Run Speed 5.5**.

**An older project** (made before this change). It keeps the speeds it saved, so it will look the same until you opt in:
1. Open the project (the editor asks about upgrading it first: answer as you like; this change does not touch the speeds).
2. Select the Player. In the Inspector, in the **Player Controller** panel (or the First-person / Third-person settings of a bare controller,
   or the Third Person Shooter Controller), press **Use natural walk and run speeds**. Hover it for what it sets.
   Walk Speed becomes 2 and Run Speed 5.5 (5 for the shooter); Crouch and Prone Speed become 1 and 0.5 only if they were still 2.2 and 1.1.
3. Press **Ctrl+Z**: the old numbers come back. Press Play to compare.
4. If you set Walk Speed above 3, an amber line under it says the legs have to jog (that is real: a walk does not fit that speed).

**Bring the old look back to compare** (same build, no reinstall). Close the editor, then in a Command Prompt:
`set OKAY_OLD_GAIT=1` and `Tools\OkayEngine.exe` (in PowerShell: `$env:OKAY_OLD_GAIT = "1"` then `.\Tools\OkayEngine.exe`).
Make a new Third Person project and Play: W is the hurried 4.5 m/s jog again. **How to tell the old one is on:** a new Player Controller
shows Walk Speed 4.5 and Run Speed 8 (the natural one shows 2 and 5.5), and the Inspector shows an amber "The OLD walk and run is on"
line under the button. Close the editor and start it again without the variable to get back to the natural one.

**Status.** Verified here (Linux, headless): `walk_vs_run` (the walk and the run on every measure, in a new project's Animator setup and with the
Character on its own, plus no flicker, no foot skating and the button's rules), `walk_vs_run_old` (the same program with the old gait on, showing the old
walk was not clearly different), and the existing locomotion, jitter, mixamo, proxy, NPC and controller tests. The pictures are software renders from
Linux, not screenshots of Windows. **Unverified:** the feel in the real editor and in the Windows player, the Inspector button and hint drawing
(compile-checked only), a real Mixamo model (the checks use the test rigs), and whether 2 m/s feels right for your game. The default speeds are one number in
`NaturalGait.hpp` and two fields in the Inspector if you want them faster (above about 2.4 m/s the legs start to jog).

## 7u. Physics: crates that stand, a playground, materials, a slider and debris

**What it is.** The physics got a second way to solve contacts, called **Improved** (the old one is **Classic**; old scenes keep it, new scenes
get Improved). Crates stack and stay put, a ball bounces and rolls to a stop, fast bullets stop at thin walls, resting things go to sleep
(cheap) and wake when what holds them up goes away. Also new: a physics material on every collider (icy or bouncy floors), a **Slider** joint, ropes that
stay near their length, joints in the physics debug view and a debris emitter. Plain explanation and the numbers: [physics.md](physics.md) and
[physics_plan.md](physics_plan.md). Code: `Physics/Physics3DImproved.inl`, `Physics/ContactSolver.hpp`, `Physics/BoxManifold.hpp`,
`Physics/DebrisPool.hpp`.

**How to reach it.**

1. Open the editor (`Tools\OkayEngine.exe`, or the launcher). Use **File > Open Scene** and pick `examples\physics\physics_playground.okayscene`
   (it ships next to the editor in the zip and the installer). The scene looks like a big grey floor with nine small stations.
2. Press **Play**. Everything starts at once. Wait about 10 seconds.
3. To compare with the old physics: stop Play, open **Project Settings > Physics**, under "Physics quality of the open scene" choose
   **Classic**, press Play again. Change it back to **Improved**. (The choice is saved with the scene.)

**What you should see (Improved).**

| Station | What to look for |
|---|---|
| 1 Tower | Five crates stand in a column, hardly move, and (after about a second) go to sleep. In Classic the tower leans and falls. |
| 2 Ball | A red ball rolls down a short ramp, bounces and rolls to a slow stop on the floor. |
| 3 Ramps | Three ramps at 20 degrees, the same green crate on each: the plain and the rubber ramp hold it, the icy (light blue) one lets it slide off. The ramps' friction is set per collider (Physics Material). |
| 4 Bullet | A small red ball fires at 60 m/s at a 5 cm thin wall and stops at it. In Classic it goes through. |
| 5 Heavy and light | A 100 kg steel crate is pushed at 5 m/s and shoves the 1 kg wooden crate ahead of it. |
| 6 Pads | The same blue ball dropped on a dark pad (dead) and a black rubber pad (bounces many times; the pad's own bounciness 0.92). |
| 7 Rope | Six balls on distance joints swing from a hook and hang about 5 m long, close to their real length. |
| 8 Slider | A blue drawer slides 3 m along x at 1 m/s and stops at its limit. |
| 9 Debris | A burst of small brown chunks every 1.5 seconds: they fly, land, go to sleep, shrink away. There are never more than 150. |

To see the joints and sleeping bodies: **View > Scene Overlays > Physics Debug > Show physics (Scene + Game)**. Colliders are green, sleeping
ones grey, contacts red, joints orange.

**Try the new settings yourself.**

* Select the **3 Ramp ice** object, find its collider in the Inspector, open **Physics Material**, tick **Own Friction** and set it to 2: the crate now holds.
* Select any crate, **Rigidbody 3D**: **Rolling Resistance** (0 = a ball rolls on for ever, 0.03 default). **Use Child Colliders** makes colliders on child objects part of the body.
* Select the **8 Drawer**, **Joint 3D** (mode **Slider**): change **Max Travel** or **Motor Speed**.
* Select **9 Debris emitter**: **Count**, **Interval**, **Max Active**, **Settled Life**.
* Add your own: **Add Component > Physics 3D > DebrisEmitter** on an empty object, then Play. In code, `DebrisPool` spawns chunks at your own moments (a digger, an explosion).

**Screenshots (headless Linux software renders of the same scene, not the editor window; camera and look differ from the editor):**

* [Start of Play](images/physics/playground_0.png), [after 1.2 s](images/physics/playground_1.png), [after 3 s](images/physics/playground_2.png), [after 8 s](images/physics/playground_3.png)

**Status.** Tested headless on Linux (`tests/test_physics_deep_*.cpp`: manifold, solver, bugs, improved, save, more; the existing physics, joint, hinge,
ragdoll, vehicle, destructible, controller and net-prediction tests stay green). The playground was played headless for 10 seconds and the
positions listed in the plan were read back. **Unverified:** the editor menus and Inspector rows on Windows (they were compiled, not opened), how the scene looks and feels in the real
editor or player, any real GPU. The 2D step has no Improved solver yet (the setting is saved but 2D plays as before).
## 7v. Test builds: try a branch before it is released

**What it is.** Today a change waits about an hour for the pull-request checks, is merged, and then a release builds for 15 to 30 minutes before you
can see it. A **test build** skips all of that: the coordinator builds one branch for you, it is published to its own folder on the update server,
and your launcher offers it **only if you switched "Get test builds" on**. It never touches the normal "latest" update, and a normal player never
sees it. It may be broken: that is what a test is. Rules and files: [release_channels.md](release_channels.md#test-builds-for-the-owner-to-try-a-branch-not-a-channel-for-players).

**How to try it.**

1. **Ask the coordinator** to start a test build of a branch ("start a test build of walk-vs-run"). They start the workflow **Test build (dev
   channel)** from `main`; it takes **about 15 to 30 minutes**. When it is done its page says "Test build published", the version (for example
   `2.876.0.17`) and the branch. You cannot start it yourself: it needs GitHub, and it only runs from `main`.
2. In the launcher open **Settings** (the sidebar), the **Behavior** tab, the card **STARTUP & UPDATES**, and switch **Get test builds** on. A
   message says "Test builds are on. They can be broken. Switch this off here to stop getting them."
3. **What you should see** within a minute or so (or click the version number at the top of the sidebar to look right now):
   * a message "Test build of walk-vs-run (abc1234) is ready (v2.876.0.17)";
   * in the sidebar a card **Test build available** with the text "Test build of walk-vs-run (abc1234) is ready" and an **Install now** button.
4. Press **Install now**. The **Software Update** window shows `v2.876.0 » v2.876.0.17`, the line **Test build of walk-vs-run (abc1234)** in amber,
   and the coordinator's note under "What's new". It downloads and checks the files like any update (a file that does not match its checksum is
   refused and nothing is installed). When it says "Update installed" press **Restart now**.
5. After the restart: **Settings > About** says "Version 2.876.0.17 (test build)" and, under it in amber, "Test build of walk-vs-run (abc1234)";
   the sidebar version says `v2.876.0.17 test`; the editor's **Help > About** says `v2.876.0.17 (test build)` and its window title ends in
   `[v2.876.0.17 (test build)]`; the **Versions** page says "You're running v2.876.0.17 (test build)".
6. **To stop getting test builds:** Settings > Behavior > Startup & updates > switch **Get test builds** off. A message says "Test builds are off.
   Nothing new will be offered." (**Reset preferences** on the same tab also turns it off.)
7. **To go back to the stable version** (while a test build is installed): close the editor, then in the same card press **Go back to the stable
   version** (it shows when Get test builds is off; the row above it names the test build you run). The update window says "Going back to the
   newest normal release (a test build is replaced)" and `v2.876.0.17 » v2.876.0`; press **Restart now**. After the restart About says "Version
   2.876.0" with no test line. (Otherwise the next normal release replaces the test build by itself.)
8. If the launcher does not start at all after a test build: run `OkaySpace-Setup.exe` from the normal release page over the top; it is a normal
   installer and replaces everything.

**Limits.** A test build replaces only the launcher, the editor and the player, like any in-app update. If a branch adds an example scene or another
file that ships in the zip or installer (the `examples\` folder), that file is **not** in the test build: ask for it separately or wait for a release.

**How to tell it is on or off.** With the switch off the launcher never asks the update server for the test folder, so nothing about test builds
can appear. With it on, the launcher reads `dev/versions.json` on the update server; the file `dev/DEV.txt` there names the newest test build.

**Status.** Checked on Linux, not on Windows:
`dev_channel` (16,516 checks: the version order, the normal reader ignoring test versions, the test reader, the signature rule, the setting, the
same install rules), `self_update` (the way back is judged strictly), `dev_publish_script` (the publisher against a fake storage server: only `dev/`
is written, in order, the real release files are byte for byte unchanged, and every refusal writes nothing), `dev_index` (self-test) and
`dev_workflow_lint` (the workflow read and its shell steps run offline). The launcher and editor files were compiled with `g++ -fsyntax-only` and
the MinGW win32 compiler; they were not built or run. **Never run:** the workflow on GitHub, a write to the real Supabase storage, anything on Windows,
and every screen above (no screenshot: the launcher was not built here). The wording in step 3 to 7 is what the code says; tell us what you actually see.

## Switches that are off by default (own code)

CLAUDE.md rule 17 and `docs/own_stack_rollout.md`: our own code exists beside the third-party library, off by default,
and switches on by an environment variable. **The release does not set any of them.** You can set them yourself before
starting a program, with no rebuild.

**How to set one (Windows).** Open a Command Prompt, then:

```
cd %LOCALAPPDATA%\OkaySpace
set OKAY_OWN_STACK=1
Tools\OkayEngine.exe
```

(PowerShell: `$env:OKAY_OWN_STACK = "1"`.) The variable lives only in that window. Start the editor straight from that
window rather than by double-click so it inherits the variable; starting it from `OkaySpace.exe` run in the same window
should also work, but the launcher passing it on is unverified.

**How to see which parts are ours.** Every program (launcher, editor, player) prints one line at start. From a Command
Prompt you may see it on the screen; in the editor the sure way is **Help > About OkaySpace**, which shows the same
text. The exact format (from `shared/OwnStack.hpp`):

```
[okay] stack: platform=sdl2 http=winhttp readers=ufbx+assimp crypto=libsodium ui=imgui
```

* `readers=own(off,dae,...)+assimp` lists the formats read by our code now. `ufbx` shows while FBX is still ufbx; it
  becomes `own(fbx,...)` with `OKAY_OWN_FBX=1`.
* `http=` is `winhttp` on Windows. `crypto=` must say `libsodium` in every release.
* `OKAY_PRINT_STACK_AND_EXIT=1` prints the line to standard output and quits at once. From a Command Prompt:
  `set OKAY_PRINT_STACK_AND_EXIT=1` then `Tools\OkayEngine.exe > stack.txt` then `type stack.txt`. Unverified on
  Windows: a GUI program may not write to the window, which is why the About popup is the sure way.

Tested on main (Linux): the line format and that each variable changes it, by the `own_stack` test.

### 8. Own model readers

* **Variables.** `OKAY_OWN_STACK=1` turns on all of them (plus the settings below). Or one at a time: `OKAY_OWN_FBX`,
  `OKAY_OWN_COLLADA` (.dae), `OKAY_OWN_3DS`, `OKAY_OWN_BVH`, `OKAY_OWN_OFF`, `OKAY_OWN_SMD`, `OKAY_OWN_MD5`,
  `OKAY_OWN_MS3D`, `OKAY_OWN_ASE`, `OKAY_OWN_3MF`, `OKAY_OWN_AMF`, `OKAY_OWN_X3D`, `OKAY_OWN_DXF`, `OKAY_OWN_LWO`,
  `OKAY_OWN_BLEND`, `OKAY_OWN_MD2`, `OKAY_OWN_MD3`, `OKAY_OWN_IRRMESH`, `OKAY_OWN_B3D`, `OKAY_OWN_X`. Each set to `1`.
  A variable you set to `0` wins over `OKAY_OWN_STACK=1`. The newer readers (AC3D, Ogre, XGL, Irrlicht scene, NFF, MDC,
  HMP, CSM, Q3D, RAW, IQM, COB, NDO and others) have their own `OKAY_OWN_<NAME>` variables listed in
  `docs/own_readers_remaining.md`; the startup line only lists the 20 above, so for those the only proof is an import
  that works.
* **Sample files** (shipped in `examples\own_readers`): `cube.off` (OFF), `cube.3ds` (3DS), `skinned_strip.dae`
  (COLLADA), `cube_gzip.blend` (gzip `.blend`) and `cube_compressed_arrays.fbx` (FBX with zlib arrays). Tiny files from our
  tests (the last two written by our own `scripts/gen_blend_fixtures.py` and `scripts/gen_fbx_fixtures.py`), no outside source.
* **Steps.**
  1. Start the editor with `set OKAY_OWN_OFF=1` (or `OKAY_OWN_STACK=1`), open **Help > About OkaySpace**. It should say
     `readers=own(off)+assimp` (with `OKAY_OWN_STACK=1`: a long `own(fbx,dae,3ds,...)` list).
  2. **File > Import Model...** and choose `examples\own_readers\cube.off`. A white unit cube appears in the scene.
  3. Close the editor, start it again with no variable, import the same file: you get the same cube, read by Assimp.
* **How you can tell it is really ours.** Only the About popup or startup line (`own(off)`). There is no per-import
  log line saying which reader ran, and the two readers give the same cube here (the corpus comparisons in
  `docs/own_stack_rollout.md` say how closely they agree). Honest gap: a "read with the built-in OFF reader" Console
  line on import would close this; it does not exist. Without Assimp in a build, a format whose own reader is off fails
  with `no OFF reader enabled: our own reader is off by default (set OKAY_OWN_OFF=1 ...)`; the Windows release has
  Assimp, so you will not see that message.
* **More sample files** for other formats are under `tests\data\models` in a source checkout (not in the release).

### 9. Own WAV decoder

* **Variable.** `OKAY_OWN_WAV=1` (and `0` to force the old one). It is **not** part of `OKAY_OWN_STACK=1` and **not** in
  the startup line; it is in the engine's `AudioClip::LoadWAV` (whole-file loads; streamed large files still use
  the old parser) and in the launcher's voice messages (see "Launcher: window calls through our platform layer, voice
  clips on our own decoder as an option").
* **Sample file.** `examples\own_wav\mulaw_tone.wav` (2 s of a 440 Hz sine, 8-bit mu-law, 22,050 Hz; we generated it
  from our own signal, no outside source) and the scene `own_wav_test.okayscene` that plays it.
* **Steps.**
  1. Start the editor with no variable. **File > Open...** `examples\own_wav\own_wav_test.okayscene`, Play. The old
     decoder does not read the format tag: it treats the bytes as plain 8-bit samples, so you should hear the same
     note but **distorted and buzzy**, at a different loudness (worked out by computing what the old code does to
     these bytes: its output is nowhere near the sine, error 1.09 on a 0.5 amplitude signal; not heard).
  2. Close the editor, `set OKAY_OWN_WAV=1`, start it again, open the same scene, Play. You should hear a **clean, soft
     steady tone** (about A, 440 Hz). That difference is the proof the own decoder is in use.
* **Verified here:** `okay::wav::DecodeWav` (compiled on its own) reads this file as 22,050 Hz mono, 44,100 frames,
  mu-law, peak 0.512, and every sample within 0.016 of an ideal 440 Hz sine of amplitude 0.5. The
  `own_wav` test covers mu-law. **Unverified:** what you hear on Windows.

### 10. Own HTTP client

Cannot be tried on Windows today. Windows already uses WinHTTP (the system's own client, `http=winhttp`), our client is
not compiled into Windows programs, and `OKAY_OWN_HTTP=1` does nothing there. It exists for Linux builds only
(`-DOKAY_OWN_HTTP=ON` at build time, then `OKAY_OWN_HTTP=1`; the line then says `http=own`). To try it on Windows we
would need our own TLS, which is cryptography and waits for the outside review (rule 18). Tested on main on Linux only:
its test programs (`own_http`, `own_http_net`).

### 11. Own platform layer (window, input, audio device)

`OKAY_PLATFORM_BACKEND=own` selects it, and `OKAY_OWN_STACK=1` sets it on Linux only (not on Windows, on purpose). The
Win32 version has only ever run under wine, never on real Windows, and the editor and launcher still open their windows
through SDL2 anyway, so only the seam parts (timers, audio, gamepads, voice) would change. If you want to try the player:
`set OKAY_PLATFORM_BACKEND=own` then `Tools\OkaySpacePlayer.exe` on an exported game. The startup line says
`platform=own` when it took effect; if this release was not built with the own backend it stays `platform=sdl2`.
**Unverified, not ready**, expect problems. Never ship with it.
The editor has its own switch for its input and the 3D views' OpenGL context: `OKAY_EDITOR_PLATFORM=own` (section "Editor: input and
the 3D views' OpenGL through our own code"); with `OKAY_PLATFORM_BACKEND=own` as well, that OpenGL context is our own WGL code.

### 12. OkayUI

`OKAY_UI_BACKEND=okayui` (or `OKAY_OWN_STACK=1`) starts the launcher and editor on OkayUI where it is compiled in. Only
the screens that have an OkayUI port change (in the editor, the Console panel today); everything else stays Dear ImGui.
The About popup shows `ui=okayui` when it took effect. Whether the Windows release contains OkayUI is unverified: if not,
the line stays `ui=imgui`. Nothing was run in a real window.

### 13. Own cryptography

Not switchable, by owner decision (2026-11-14, CLAUDE.md rule 18). libsodium is the only crypto in a release, the
environment variable `OKAY_OWN_CRYPTO` does nothing at run time, and the startup line must say `crypto=libsodium`.
Nothing to try.

### 7n. Every own replacement at a glance: how to try each, and what is missing

This is the one table for everything that is built and switched off. It is the same table as section 5 of `docs/own_flip_proposal.md`,
which also says how ready each item is, in what order they could become the default, and what blocks deleting the library each one replaces
(CLAUDE.md rules 4a and 17). Set a switch the way the section "Switches that are off by default" says: an environment variable in the
Command Prompt, then start the program from that window. The sure way to see which parts are ours is **Help > About OkaySpace** in the editor.

Checked by reading the code and the CI logs on 2026-11-15. Nothing here was run on Windows.

| Item | The switch | A sample that ships in the download | How you can tell it is on | Gap |
|---|---|---|---|---|
| FBX | `OKAY_OWN_FBX=1` | **none.** Use any `.fbx` you own | About popup: `readers=own(fbx,...)`, and the word `ufbx` goes away | no sample |
| COLLADA | `OKAY_OWN_COLLADA=1` | `examples\own_readers\skinned_strip.dae` | `readers=own(dae)` | none |
| 3DS | `OKAY_OWN_3DS=1` | `examples\own_readers\cube.3ds` | `readers=own(3ds)` | none |
| OFF | `OKAY_OWN_OFF=1` | `examples\own_readers\cube.off` | `readers=own(off)` | none |
| BVH, SMD, MD5, MS3D, ASE, Blender | `OKAY_OWN_<FMT>=1` | **none** | `readers=own(<fmt>)` | no sample |
| 3MF, AMF, X3D, DXF, LWO, MD2, MD3, IRRMESH, B3D, X | `OKAY_OWN_<FMT>=1` | **none** | `readers=own(<fmt>)` | no sample |
| AC3D, OGRE, XGL, IRR, NFF, TER, MDC, HMP, CSM, Q3D, RAW, IQM, COB, NDO, PMX, LWS | `OKAY_OWN_<FMT>=1` | **none** | **nothing**: they are not in the stack line and no log line says which reader ran | no sample and no way to tell |
| STL and PLY first | build option `OKAY_PREFER_OWN_READERS` only | none | nothing | no run-time switch, no sample, no sign |
| WAV | `OKAY_OWN_WAV=1` (`0` = old) | `examples\own_wav\own_wav_test.okayscene` with `mulaw_tone.wav` | you hear it: old = buzzy, own = a clean 440 Hz tone | no startup word, no log line |
| Own HTTP | Linux builds only: `OKAY_OWN_HTTP=1` | none | `http=own` (Linux) | cannot be tried on the Windows download |
| Own platform layer | `OKAY_PLATFORM_BACKEND=own`, then `Tools\OkaySpacePlayer.exe` | any exported game | `platform=own` | unverified on Windows; editor and launcher still use SDL windows |
| OkayUI | `OKAY_UI_BACKEND=okayui` | none needed | About popup `ui=okayui` | unverified in the Windows build |
| CodeEdit (script editor) | `OKAY_UI_BACKEND=okayui` and `OKAY_DEV=1` | any script | none | never run in a real window |
| okay::crypto | none, on purpose | none | the line says `crypto=libsodium` | nothing to try |

The gaps (no sample for 32 of the 35 reader switches, no way to tell that the 16 newest readers or WAV are on, no run-time switch for STL
and PLY, no import log line) are "Step 0" of the proposal. Until they are closed, a PR that adds or changes one of these must say so
at the top (rule 4a).

### 15. One shared inflate (zlib / gzip / DEFLATE) decoder

**What it is.** Compressed data in PNG pictures, gzip `.blend` files, `.x` MSZIP, `.zgl`, 3MF/AMF zip packages, the arrays
in binary FBX files and gzip answers from our own HTTP client used to be decoded by three separate copies of the same
decoder. Now there is one (`engine/include/okay/Core/Inflate.hpp`) and the others call it. Nothing should look different:
this change is about having less code to get wrong. Plain words: if every picture and model that worked before still
loads, it worked.

**Steps.**
1. **PNG (always on).** In the editor, **File > Import Model...** and choose `examples\obj_texture_paths\cube_ok.obj`. The
   cube shows the grid texture. That texture is `grid.png`, a PNG, and every PNG goes through the shared decoder. Any
   other PNG you drop in as a texture does the same.
2. **gzip `.blend`.** `set OKAY_OWN_BLEND=1` (or `OKAY_OWN_STACK=1`), start the editor, **File > Import Model...**,
   choose `examples\own_readers\cube_gzip.blend` (a Blender file saved with "Compress File", our own test file). A small model
   appears (the `blend_reader` test checks its numbers). (Without Assimp in the build this reader is the only way a `.blend` loads at all.)
3. **FBX with compressed arrays.** `set OKAY_OWN_FBX=1`, start the editor, import
   `examples\own_readers\cube_compressed_arrays.fbx` (a binary FBX 7.4 whose vertex and index arrays are zlib packed).
   A cube should appear. (The About popup should then list `fbx` among the own readers.)
4. **Not shipped as samples:** `.x` with MSZIP, `.zgl`, 3MF and AMF. Their files are only built inside the tests
   (`x_reader`, `xgl_reader`, `3mf_reader`, `amf_reader`). If you have your own `.x` (MSZIP), `.zgl` or `.3mf` file,
   import it with `OKAY_OWN_X=1`, `OKAY_OWN_XGL=1` or `OKAY_OWN_3MF=1`.
5. **gzip HTTP answers.** Only our own HTTP client uses this, and it cannot be tried on Windows (see 10). On Linux:
   build with `-DOKAY_OWN_HTTP=ON`, run with `OKAY_OWN_HTTP=1`, and fetch a URL whose server sends
   `Content-Encoding: gzip`; the program must get the plain bytes (the tests `own_http` and `own_http_net` do this).

**How you know it worked.** There is no log line that names the decoder (it is a function, not a switch). What you can
see: the grid texture on the cube (step 1), the cube from each file (steps 2 and 3), and, on a machine with a
build, the test run `ctest -R inflate_differential` (and `ctest -R png_hint`), which prints how many damaged and valid
streams it compared with an independent slow decoder and ends with `ok:`.

**Status.** Verified here (Linux, headless): `inflate_differential` (about 67,000 streams, ASan and UBSan clean),
`own_http_inflate`, `png_hint`, and the existing `image_decoder`, `blend_reader`, `3mf_reader`, `amf_reader`, `fbx_reader`,
`fbx_hostile`, `x_reader`, `xgl_reader`, `own_http` tests. **Unverified:** every editor step above (nothing here opens a
real editor window), anything on Windows, real-world files other than our own samples.

---

## Every template is tested (and what was fixed)

The two tests `templates_work` and `examples_work` run every New Project template and every example scene the way a person
does (keys, mouse, Play, Stop, save, reopen) and fail when one is broken. What each one checks, how to add a template or an
example, and the one known failure are in [templates_check.md](templates_check.md).

**Commands** (a Linux checkout):

```
cmake --build build --target test_templates_work test_examples_work
ctest --test-dir build -R "templates_work|examples_work" --output-on-failure
build/tests/test_templates_work      # prints the table: 22 templates by 7 checks (plus 2 builders no menu reaches), and what was measured
build/tests/test_examples_work       # prints one line per example scene
```

**What changed in the templates and examples, and how to see it in the release.** Nothing here was run in a real window:
the checks are headless, so every "you should see" line is unverified until you look.

1. **Main Menu.** New Project > Main Menu, press Play, click **Start**. You should see the menu disappear and the text "Your
   game starts here" with a **Back to menu** button; Back brings the menu again. (Before: Start tried to load `game.okayscene`,
   which a new project does not have, so nothing happened and the Console showed an error.) To load your own level, change
   the Start button's script to `load_scene("<path of your scene>")`.
2. **Third Person Shooter.** New Project > Third Person Shooter, press Play, hold the **left mouse button** facing the red
   balls. They lose health, vanish after a few hits and come back three seconds later. R reloads. (Before: nothing fired.)
3. **Top-Down.** New Project > Top-Down, press Play, hold **D**. The player stops at the grey wall on the right instead of
   walking through it.
4. **Humanoid and Sandbox.** New Project > Humanoid (or Sandbox), press Play, then Stop. The title bar must not show unsaved
   changes and the editor must not ask to save when you close it. (Before: it did.)
5. **Examples.** File > Examples > Visual Script Node Playground > any demo, then Play. The coloured shapes are now in the
   picture (before, the camera looked away and only the on-screen text showed). The same for `examples\behavior_trees\*`,
   `examples\weapons\arena.okayscene`, `examples\spatial_audio\spatial_audio.okayscene`, `examples\own_wav\own_wav_test.okayscene`,
   `examples\physics\step_and_parent.okayscene` (it now has a camera, a light and a crate that falls onto a floor) and
   `examples\touch_controls`.
6. **Card text.** The launcher's cards for Vehicle (3D), Vehicle (2D) and Sandbox now say what the templates do.

Status: tested on Linux (headless, about 6 seconds). Not run: a real window, the launcher, Windows. Before and after pictures of six
of the changed example scenes are in [templates_check.md](templates_check.md#pictures-the-cameras-that-looked-away-before-and-after);
they are headless software renders (meshes only, no on-screen text), not window screenshots.

## How the packaging was checked (and what was not)

* `scripts/build-windows-dist.sh` now copies the `examples\` folders listed in `EXAMPLE_DIRS` (and `grid.png` for the OBJ
  samples) into `build-win-dist/examples/`. The zip step in `release.yml` and `scripts/release-local.sh` run
  `cp -r build-win-dist/*`, and the installer script `scripts/okayspace-installer.nsi` installs `build-win-dist/*`
  recursively (`File /r`), so both pick the folder up with no further change. The uninstaller now removes `examples\`.
* Run here: `bash -n` on the script, and the new copy block on its own against the real source tree, then a listing of
  the result. **Not run:** the full Windows cross build, the zip, `makensis`, the installer, anything on Windows.
* `release.yml` was not changed and no workflow was run.

## Remove from list and Delete project (launcher Create page)

**Where.** Launcher > **Create**. Your projects are the folders inside the **Projects** folder next to `OkaySpace.exe`
(the Location shown on the page), plus projects you opened from other places with **Open folder**.

**Steps.**
1. Make two projects (New Project), or open a project folder. Right-click a project card.
2. Click **Remove from list**. A toast says "Removed <name> from the list (its files are still on disk)" and the card
   goes at once. Close the launcher and open it again: it is still gone. Open the folder in Explorer: the files are there.
   To get it back, use **Open folder** and pick it (it is also un-hidden when you make a project of the same name).
3. Right-click another project > **Delete project...**. The popup names the folder. **Delete from disk** moves the whole
   folder to the Recycle Bin (Linux: deletes it for good). The card goes and a toast says "Moved <name> to the Recycle Bin".
   When it cannot, the popup stays open with a red line saying why: the editor is open ("close the editor first"), the
   folder is your Projects folder / home / the launcher's folder, or it has no scene, or a file is in use.

Status: the list logic (hide, restart, a project that is both in the folder and opened by path, slash and trailing-slash
forms, more than 100 hidden projects, delete clean-up) is tested by `tests/test_project_list.cpp` (g++ on Linux). The
Recycle Bin call and the clicks were not run on Windows.

## Checks run for this page

Run on Linux, on the branch `claude/try-it`:

* `tests/test_project_create.cpp`, built on its own with g++ (it needs only the shared header), run from the source
  tree so the launcher and editor title cross-check really ran: **282 checks, 0 failures** (the same test on main:
  278). The test now expects 22 templates and that the editor's list equals the shared list, which includes
  "Vehicle (get in and drive)".
* `python3 scripts/gen_feature_map.py` then `--check`: up to date.
* The `examples` copy block of `scripts/build-windows-dist.sh`, run alone: 12 folders, 42 files, about 0.5 MB (472 KB on disk), no
  `.py`, `.cpp` or `__pycache__`. `bash -n` on the whole script.
* `okay::wav::DecodeWav` on `mulaw_tone.wav` and `Mesh::LoadOBJ` on the three OBJ samples, as described above.

**Not run:** `test_templates`, `vehicle_seat` and the other engine tests (the engine build was stopped; no engine
code changed). Compiling the editor and launcher (`EditorState::NewVehicleSeats3D`, the one-line launcher glyph change
and the new Console line in `NavigationWindow.cpp` were written by copying neighbouring code and were not compiled).
Anything on Windows. The NSIS installer and the zip.

## Console: Collapse button (branch `claude/console-collapse`, not merged or released yet)

Owner request 2026-11-15: "for console allow user to collapse the same log."

1. Open the editor (`Tools\OkayEngine.exe` or Open from the launcher) and show **View > Console**.
2. Make a log repeat. Easiest: press **Play** on a scene whose script logs every frame (for example a script that calls
   `print` in `update`), or use any scene that logs the same warning many times.
3. In the Console toolbar, to the right of the three count pills (Messages, Warnings, Errors), click **Collapse**.
   Hover it: the tooltip says "Merge identical messages into one row with a count".
4. You should see one row per distinct message, with `x37` (the count) in front of the text and the time of the newest
   copy. The count keeps growing while Play runs. Click the row to see the details (count, first and newest time).
5. Click **Collapse** again: every line is back. Close and reopen the editor: the button keeps its last state
   (stored as `consolecollapse` in `okay_settings.txt`). It is off the first time.
6. Search, the category dropdown and the pills still work while collapsed, and Clear empties everything.

Rule: two lines merge only if level, category (the leading `[tag]`) and text are exactly the same, even when they are not
next to each other. `hp 10` and `hp 9` stay two rows. The pill counts still count every line, not rows.

**Status.** Logic tested headless (`test_debug_console`, 136 checks, run on Linux). `editor/ConsolePanel.cpp` and
`editor/main.cpp` were NOT compiled (no SDL in this environment); the button and label are unverified until you look.

## Launcher: threads on our own code (branch `claude2/launcher-own-threads`, not merged or released yet)

What changed: the launcher's background work (news, downloads, the update check, chat, voice, the Git buttons, every
"working..." job) used SDL's threads, locks and counters. It now uses our own (`launcher/LauncherThreads.hpp` over
`engine/src/Core/CoreThreads.hpp`). Nothing should look different. This is a check that it still behaves the same.

1. Start `OkaySpace.exe`. The start-up update check runs and the launcher opens. Expect the same as before (no hang on
   "Checking for updates").
2. **Home / What's New**: the news and link previews load. **Account**: sign in; your avatar loads.
3. **Versions**: install an editor version. The progress bar moves; **Cancel** stops it; installing again finishes.
4. Click the version number at the bottom of the left bar (tooltip "Check for updates"). The Software Update window
   answers ("up to date" or offers the update) and the launcher does not freeze.
5. **Create** page: right-click a project that is a Git folder and pick **Git: Pull**, or use **Commit & Push**, or
   **Clone** a repository. A message says it worked or failed, as before.
6. Chat: send a message and an image; start a voice call and the mic test; hang up.
7. Quit the launcher while a download runs. It closes within a few seconds and does not crash.

If any step hangs or crashes where the last release did not, that is this change: report the step.

**Status.** Tested on Linux only: `test_core_threads` (mutex under contention, recursive lock, a woken waiter, exact atomic
adds, join, detach, names; also clean under ThreadSanitizer), the launcher built natively, and `launcher/main.cpp` passed a
MinGW (Windows) syntax check. Not run on Windows: the steps above are unverified until you run them.

## Launcher: window calls through our platform layer, voice clips on our own decoder as an option (branch `claude2/launcher-own-wav-window`, not merged or released yet)

What changed (SDL removal, stage A):

* **Voice messages.** By default the launcher still reads a voice message's WAV file with SDL (`SDL_LoadWAV`), as
  before. To try our own WAV decoder instead (`launcher/VoiceClipLoad.hpp` over `okay::wav`), start the launcher from a
  Command Prompt after `set OKAY_OWN_WAV=1` (`set OKAY_OWN_WAV=0`, or no variable, is SDL). Playback itself is unchanged
  either way.
* **Window calls.** Showing, raising, restoring, the minimised / hidden / focused checks, size, position, window id and
  minimum size of the launcher window and the separate chat window now go through `okay::platform` (a
  `WrapSdl2Window` view over the same SDL window). The window is still made by SDL, so nothing should look different.

Steps:

1. Start `OkaySpace.exe`. The splash shows, then the launcher. Try to make the window smaller than about 720 x 480: it
   stops there, as before.
2. Sign in. Open a chat, record a voice message (hold the mic button), send it. Press play on it: you hear it, the
   waveform bars show its loudness (not even bars), the time counts, and **1.5x / 2x** speed it up without a higher
   voice. Play a voice message someone else sent, too.
2a. Our own decoder: close the launcher, open a Command Prompt in its folder, run `set OKAY_OWN_WAV=1` and then
   `OkaySpace.exe`. Play the same voice messages: they should sound and look exactly the same as in step 2.
3. Set chat to open in its own window (chat settings), open it. It cannot be made smaller than about 520 x 380. Close
   and reopen the launcher: the chat window comes back where you left it.
4. Minimise the launcher with the chat window open: the chat window keeps working. Restore the launcher from the
   taskbar.
5. In a game, press **Shift+Tab** (Invite friends): the launcher comes to the front, restored if it was minimised.
6. With the launcher minimised or in the background, let an update hint or an incoming call arrive: the taskbar
   button flashes.
7. Close the launcher and start it again: it opens at the size you left it.

If a voice message does not play or plays as noise with `OKAY_OWN_WAV=1` only, that is our decoder; if a window step
behaves differently from the last release, that is the window change: report the step.

**Status.** Tested on Linux only: `test_launcher_wav_window` (every WAV in the repository and five made-up ones read by
SDL and by our reader give the same format and length; the launcher picks SDL by default and with `OKAY_OWN_WAV=0`
and ours with `OKAY_OWN_WAV=1`, and the SDL path gives exactly what the launcher played before and byte-for-byte the same samples, except 8-bit and 24-bit files,
which are within 2 steps of 32,768 because the old launcher code truncated where ours rounds; every truncation of a file fails or reads less; the
window view answers size, position, minimum size, id and the hidden / minimised / focused state like SDL, under SDL's
dummy video driver), the launcher started under Xvfb with a screenshot, and `launcher/main.cpp` passed a MinGW (Windows)
syntax check. Not run on Windows: the steps above are unverified until you run them.

## Launcher: drawing with our own renderer as an option (branch `claude2/sdl-launcher-painter`, not merged or released yet)

What changed (SDL removal, stage B): the launcher can draw without `SDL_Renderer` and without Dear ImGui's SDL backends. It is
**off by default**: with no variable the launcher draws exactly as before.

* **Switch.** `OKAY_LAUNCHER_RENDERER=own` (anything else, or no variable, is the old SDL path).
* **How you can tell it is on.** The footer at the bottom of every page reads `OkaySpace v... (own renderer: opengl)`.

Steps:

1. Open a Command Prompt in the folder of `OkaySpace.exe`, run `set OKAY_LAUNCHER_RENDERER=own`, then `OkaySpace.exe`.
2. The splash shows, then the Create page. The footer says `(own renderer: opengl)`. The page should look the same as without the
   variable: fonts, colours, rounded corners, icons.
3. Click into the **Search** box next to "Recent projects" and type a few letters, including Backspace, arrows, Ctrl+A, Ctrl+C and
   Ctrl+V. The mouse pointer turns into a text cursor over the box.
4. Click **Play**, **Friends**, **Settings** in the sidebar; scroll a long page with the mouse wheel; resize the window and make it small.
5. Sign in and open a chat: avatars and images show. With the setting **Open chat in its own window** on (Settings, the default), the chat opens
   in its own window ("OkaySpace Chat"), drawn by our renderer too: type in it, scroll it, drag a file onto it, move and resize it,
   close it with its X and open it again. Minimise the launcher: the chat window keeps going.
5a. Settings > Appearance > UI backend: **OkayUI (preview)**, then open **What's New**: the page is drawn by OkayUI on our renderer.
   Click "Check for updates" and scroll. (Before, `own` switched OkayUI off.)
5b. With a language that uses an input method (Japanese, Chinese, Korean): click into a text box and start typing; the input
   method's candidate list opens next to the text cursor, not in a corner of the screen.
5c. Leave the launcher in the background while a call rings or an update is found: its taskbar button flashes (the chat window's
   button for a call while the chat window is open).
6. Move the launcher to a monitor with 125% or 150% scaling: the text stays sharp and clicks land where you click.
7. Close the window with the X: the "Exit or Minimize" question appears; Minimize, then restore from the taskbar.
8. Close the launcher, start it again **without** the variable: the footer no longer mentions the renderer.

If something looks different, is missing or does not react only with the variable set, that is our renderer: report the step and,
if you can, a screenshot of both. If `own` cannot start (no OpenGL), the launcher falls back to SDL by itself and draws as before.

**Status.** Tested on Linux only, under Xvfb with Mesa's software OpenGL (no real GPU): `launcher_imgui_renderer` (our ImGui renderer
against a reference rasteriser, software painter; the OkayUI host fed from our events gives the same input as from SDL's),
`launcher_imgui_renderer_gl` (the same on our OpenGL painter, sdl2 and own windows; two painters on two windows drawn in turn; the IME
caret), `platform_state` (the taskbar-flash hint read back from the X server), and `scripts/launcher_renderer_check.sh` (screenshots
with both renderers, typing, a click on Play, the chat window and the OkayUI page with both renderers, frame time). The launcher, the
seam, the OpenGL painter and the Win32 flash, IME and message-hook code passed a MinGW (Windows) syntax check only. **Not run on
Windows: every step above is unverified until you run it** (on Linux the tray is not used and the flash is the urgency hint).

## Launcher with no SDL at all (branch `claude2/sdl-launcher-no-sdl`, not merged or released yet)

What changed (SDL removal, step 2d): with **both** switches below the launcher does not call SDL at all: its window, events,
drawing, monitor list and sound all come from our own code. It is **off by default**: with no variables nothing changes.

* **Switches.** `OKAY_LAUNCHER_RENDERER=own` and `OKAY_PLATFORM_BACKEND=own` (both; one alone still uses SDL for part of the work).
* **How you can tell it is on.** The footer says `(own renderer: opengl)`; started from a Command Prompt, the launcher prints
  `launcher: no SDL (own renderer, own platform backend)`.

Steps (Windows, `OkaySpace.exe` from the release zip):

1. Open a Command Prompt in the folder of `OkaySpace.exe` and run:
   `set OKAY_LAUNCHER_RENDERER=own`, `set OKAY_PLATFORM_BACKEND=own`, then `OkaySpace.exe`.
2. Expect the splash, then the Create page, as without the variables, and the line `launcher: no SDL ...` in the Command Prompt.
   On a 125% or 150% display the text should be sharp (per-monitor DPI awareness from our own code).
3. Monitors: open the chat in its own window, move it onto a second monitor, close the launcher, unplug (or switch off) that
   monitor, start the launcher again the same way and open the chat: it comes back in the middle of the main monitor, not off screen.
4. Sound, our own Windows audio (waveOut / waveIn): Settings > Voice: the microphone and speaker lists show your devices; the mic
   test shows the level moving when you talk. Record a voice note in a chat and play it back; play a voice note someone sent you.
   Start a voice call: the other side hears you and you hear them.
5. Everything from "Launcher: drawing with our own renderer" (typing, chat window, OkayUI page, taskbar flash) should work the same.
6. Close the launcher with the X and choose Exit: it closes with no error.

If the launcher cannot start this way (no OpenGL), it falls back to SDL by itself and says so in the Command Prompt. If a step
fails only with both variables set, that is our code: report the step.

**Status.** Tested on Linux only, under Xvfb (software OpenGL, ALSA's `null` audio device): `scripts/launcher_no_sdl_check.sh`
puts a trap in front of every SDL function (839) and shows that the launcher with both switches calls none of them, from start to
exit, while it shows the chat window (moved back on screen by the new monitor list), plays a voice note and holds a voice call;
`platform_display*` tests check the monitor list (one screen, a panel, a 150% setting, two monitors, two screens with Xinerama).
The Windows monitor list, DPI awareness and waveOut / waveIn sound passed a MinGW syntax check only. **Not run on Windows: steps
2 to 6 are unverified until you run them.**

## Editor: input and the 3D views' OpenGL through our own code (branch `claude2/sdl-editor-seam`, not merged or released yet)

What changed (SDL removal, editor stage E1): with the switch below, the editor's Dear ImGui gets its mouse, keyboard, typed text,
clipboard and cursor shapes from **our own code** (the same code the launcher uses with `OKAY_LAUNCHER_RENDERER=own`), and the hidden
OpenGL context the Scene and Game views render into is made through our platform layer. SDL still draws the editor's window and
still reads the events from Windows; only what happens with them changed. It is **off by default**: with no variable nothing changes.

* **Switch.** `OKAY_EDITOR_PLATFORM=own` (anything else, or no variable, is the old SDL path). Adding `OKAY_PLATFORM_BACKEND=own` also
  makes the 3D views' OpenGL context with our own Windows code (WGL) instead of SDL's.
* **How you can tell it is on.** The Console's first lines say `editor: platform own (ImGui glue on okay::platform; viewport GL on sdl2; ...)`
  (`... on own` with both variables). Without the variable it says `editor: platform sdl (...)`.

Steps (Windows, from the release):

1. Open a Command Prompt and run:
   `cd %LOCALAPPDATA%\OkaySpace`, `set OKAY_EDITOR_PLATFORM=own`, then `Tools\OkayEngine.exe`.
2. Expect the editor as usual. Open the Console: the first lines name the platform (above).
3. Mouse: click menus, drag a panel's tab to dock it somewhere else, drag a splitter, scroll the Hierarchy with the wheel and with a
   touchpad (small, smooth steps should scroll a little, not jump). The cursor should change to arrows over splitters and to a text
   bar over text fields.
4. Keyboard: rename an object in the Inspector (type letters with accents too, for example "é"), use Backspace, the arrows, Home / End,
   Ctrl+A, Ctrl+C, Ctrl+V; paste text copied from Notepad, and copy text out to Notepad.
5. 3D views: the Scene and Game views draw as usual (View > GPU renderer: the OpenGL one). Repeat with `set OKAY_PLATFORM_BACKEND=own`
   added before starting: the Console then says `viewport GL on own`, and the views must still draw.
6. Play: press Play in a 3D template, walk with WASD, look with the mouse, press Esc, type into a game text field if the template has one.
7. Drop a picture from Explorer onto the editor window: it is imported into the Assets folder.
8. Close the editor with the X: it closes normally.

Known differences with the switch on (on purpose, for now): panels cannot be dragged out of the editor into their own windows (View >
multi-window does nothing); ImGui's own Ctrl shortcuts in text fields follow the key's place on the keyboard, so on an AZERTY keyboard
Ctrl+A is the key where Q is; a slider or splitter drag stops at the window's edge. If anything else behaves differently only with the
switch on, that is our code: report the step.

**Status.** Tested on Linux only, under Xvfb, **without the editor itself** (the full editor is too big to build on the test machine):
`editor_platform` and `editor_platform_own` check the window, the 3D views' OpenGL context (3.2 compatibility, depth, and SDL's own
context given back so the UI still draws), mouse, wheel, keys, typed text, clipboard, cursor shape and a dropped file through the
switch, and that our ImGui input code gives ImGui exactly what ImGui's SDL code gives it for the same events (27 frames, all
identical). `editor/main.cpp` passes a compile check with the change. **Never run in a real editor session and never on Windows:
steps 2 to 8 are unverified until you run them.**

## Editor: drawing with our own renderer (branch `claude2/sdl-editor-gfx`, not merged or released yet)

What changed (SDL removal, editor stage E2): with the switches below, the editor's whole window (every panel, the Scene and Game views,
thumbnails, icons, previews, the loading screen) is drawn by **our own OpenGL painter and our own Dear ImGui renderer** (the same code
the launcher uses with `OKAY_LAUNCHER_RENDERER=own`) instead of SDL's renderer. It is **off by default**: with no variable nothing changes.

* **Switch.** `OKAY_EDITOR_RENDERER=own`, and it **only works together with `OKAY_EDITOR_PLATFORM=own`** (section "Editor: input and the
  3D views' OpenGL through our own code"). With `OKAY_EDITOR_RENDERER=own` alone the editor draws as before and the Console says
  `... (OKAY_EDITOR_RENDERER=own could not start: it needs OKAY_EDITOR_PLATFORM=own as well)`.
* **How you can tell it is on.** The Console's first lines say `editor: renderer own (opengl painter on a sdl2 window, own ImGui renderer)`.
  Without it they say `editor: renderer sdl (SDL_Renderer direct3d11, ...)`.

Steps (Windows, from the release, once this branch is in one):

1. Open a Command Prompt and run: `cd %LOCALAPPDATA%\OkaySpace`, `set OKAY_EDITOR_PLATFORM=own`, `set OKAY_EDITOR_RENDERER=own`, then
   `Tools\OkayEngine.exe`.
2. Expect the loading screen, then the editor as usual. Open the Console: the first lines name the renderer (above).
3. Look at every kind of picture: the Scene and Game views (move the camera, resize the panels), the Project panel's thumbnails, a
   material or model preview, the Modeling panel's primitive tiles, the Sprite Editor (paint a few pixels: they must stay sharp), a UI
   Image in the Inspector ("Set Native Size" shows the picture's size), a text in a TTF font in the Game view, the first-person hands
   preview. Each must look as it does without the variables.
4. View > VSync on and off; take a screenshot with the diagnostics shot if you use it.
5. Press Play in a 3D template; if it shows OkayUI widgets (or Tools > Test UI), they must draw on top.
6. Close the editor with the X: it closes normally.

Known differences with the switch on (on purpose, for now): panels cannot be dragged out into their own windows (as with
`OKAY_EDITOR_PLATFORM=own`); on Windows the Direct3D 11 views do not draw straight into the window's textures, they are copied (read
back and uploaded) like the OpenGL ones, which may be slower on big views.

**Status.** Tested on Linux only, under Xvfb with software OpenGL, **without the editor itself** (the full editor is too big to build on
the test machine): `editor_gfx` and `editor_gfx_own` draw the same editor-like frame (shapes, clipping, text, a thumbnail, a 3D view
rendered in the views' own OpenGL context, a texture with blending off, a texture made again) through SDL's renderer and through ours and
find them identical pixel for pixel. `editor/main.cpp` passes a compile check (Linux and MinGW) with the change. **Never run in a real
editor session and never on Windows: steps 2 to 6 are unverified until you run them.**

## Team tooling (not part of the Windows release)

How to try the collaboration tools ([collaboration.md](collaboration.md)); these run on a Linux checkout and on GitHub, not in
`OkaySpace.exe`:

1. Open `WORK_BOARD.md` on the `work-board` branch (after the workflow has run once on main): it now has a **Claims** table and
   a **Collision report**.
2. `python3 scripts/find_existing.py tween` prints what already exists for "tween" (feature map, code, tests, branches).
3. Push a branch named `claude2/x` that adds `#include <mutex>` to a header under `engine/include`: the **Branch lint** check
   fails with the file and line and says to use `debugutil::SpinLock`.
4. Open a PR without a "How to try it" section: the **PR hygiene** check fails and says what to add.

## UI Notify template: no freeze when toasts pile up (bug fix)

The **UI Notify** prebuilt screen froze the editor when the fifth toast was pushed into its stack of four. Fixed: a toast that is
pushed out now stops counting at once.

1. Windows editor: open or create a scene, then **GameObject > UI > Prebuilt Screens > UI Notify** (or the same entry under the
   UI menu), press **Play**.
2. Click the buttons **Info**, **Saved**, **Error**, **Confirm** and **Cancel** one after another, quickly, more than five times.
3. You should see at most four toast cards stacked in the top-right corner. The oldest card disappears when a new one arrives,
   and each card goes away by itself after about six seconds. The editor keeps responding.
4. Test: `ctest -R ui_toast` (it also pushes 10 toasts in one frame and runs the template's click sequence).

## 7s. Digging the voxel terrain no longer drops the frame rate

**What it is.** Holding the dig (left mouse) or build (right mouse) button in a voxel terrain used to re-build and re-upload the
whole terrain every frame. Now only the chunks you touched are. Details and numbers: `docs/voxel_edit_perf.md`.

**Steps.**
1. Start `Tools\OkayEngine.exe`. **File > Open...** and open `<install folder>\examples\voxel\voxel_big_world.okayscene`
   (a bigger hill than the Sandbox, so the old stutter is easy to see). The normal size is `voxel_sandbox.okayscene`, or New Project >
   Sandbox.
2. Open **View > Panels > Profiler** and press **Play**. Click in the Game view, walk with WASD, look with the mouse.
3. Hold **left mouse** on the ground (dig) and **right mouse** (build). Watch the frame rate (the Game view's stats, or **F3** in
   the player). You should see it stay steady. In the Profiler the rows `Voxel.Edit`, `Voxel.Mesh` and `Voxel.Upload` appear while
   you hold the button and each stays well under 1 ms.
4. Press Stop, select the **Terrain** object: the Voxel Terrain card says "Drawn as one mesh". While Playing and after your first
   dig it says "Drawn as N chunk objects", and the Hierarchy shows `Voxel Chunk x,y,z` children (they are never saved).
5. To feel the old behaviour: close the editor, open a Command Prompt, run `set OKAY_VOXEL_OLD_REMESH=1`, start
   `Tools\OkayEngine.exe` from that same window and repeat steps 2-3. The frame rate drops while you hold the button and
   `Voxel.Mesh` plus `Voxel.Upload` are several times bigger. The Terrain card then says so.

**Status.** Tests run on Linux (`ctest -R "voxel|mesh_hash|terrain_water_speed"`): 127 + 10 + 36 + 13 + 55 + 25 + 13 checks, 0
failures. Software renderer pixels identical in both modes. CPU time per dig frame measured 3x to 16x lower; **no GPU, no Windows,
no real editor window was available**, so the real frame rates and the Inspector and Profiler lines are unverified until you look.

## 7t. Voxel terrain digs like real life

**What it is.** Dug ground no longer disappears. The ground has materials (grass, dirt, clay, stone, ore, sand, snow) with their own
colour and hardness. Digging drops debris that falls; walk up to it and it is counted ("Dirt 3.20 m3" on screen). Right mouse
builds with what you hold. Pieces you cut loose fall. Ctrl+Z takes back the last stroke. All the options are in the Inspector.
Details: `docs/terrain_water.md`, "Real-life digging".

**Steps.**
1. Start `Tools\OkayEngine.exe`. **File > Open...** and open `<install folder>\examples\voxel\voxel_realistic.okayscene`.
2. Press **Play**. Click in the Game view, walk with WASD, look with the mouse.
3. Hold **left mouse** on the ground. You should see the ground carve away and small blocks of it fall and bounce. Walk up to the
   blocks: the text at the top-left changes from "Nothing collected" to something like "Dirt 3.20 m3 Grass 1.10 m3". Blocks you
   do not pick up lie for a few seconds and then melt back into the ground where they are.
4. Hold **left mouse** on a dark grey patch (stone): it digs much more slowly than dirt. Ore (yellow) is too hard for the
   starting tool and does not dig at all (raise **Tool Power** on the Voxel Digger card to dig it).
5. Hold **right mouse**: it builds ground back, using up the count. With nothing collected, nothing builds.
6. Cut under a ledge or a thin column of ground from the side and release the button: the cut-off piece falls as one lump with its
   own shape and settles back into the ground.
7. Press **Ctrl+Z** after letting go of the mouse: the last stroke is taken back.
8. Press Stop and select the **Terrain** object. The **Voxel Terrain** card has a **Materials** table (colour, hardness, drop...);
   the **Voxel Digger** card has **Real life**: a Preset (Realistic, Arcade, Creative), Dug Ground, brush shape, depth limit, bedrock
   height, debris limit and the count text. Pick **Arcade** and Play again: the ground vanishes as it did before.

**Pictures** (software renders from the headless test machine, not the GPU renderer): `docs/images/voxel/1_layers_cut.png` (a cut
through the layers), `2_dig_debris_falling.png` (a dig with falling debris), `3_after_release.png`.

**Status.** Tests (`ctest -R "voxel_materials|voxel_digger_real|voxel_examples"`): 60 + 34 + 58 checks, 0 failures on Linux. Volume
kept: a stroke of 63 m3 put back came to 63.1 m3 (restored to 0.0 percent), debris that landed returned the volume to within 0.05
percent. **Unverified:** Windows, a real GPU, the editor's Inspector click path, the feel of the controls, and the sound and particle
hooks per material (the table has the fields, the digger still plays the one dig sound).

### 7t, more options (second pass)

1. In `voxel_realistic.okayscene`, select **Terrain**. In the **Voxel Terrain** card tick **Catch bodies that fall out of the world**.
   Play, and push a prop off the side of the world: it is put back on the ground instead of falling for ever. (Off: it falls.)
2. In the **Voxel Digger** card, with **Slump loose ground** on, raise **Slump Passes** to 8 and dig into a sand slope (pale yellow):
   the sand runs farther down than with 1.
3. In the **Voxel Terrain** card, type a **Seed** and press **Generate**: the same seed gives the same hill every time.
4. Move or turn the **Terrain** object (Inspector: Position, Rotation) before pressing Play: digging, the brush circle and the
   falling debris all still match the ground.
5. Dust colour follows the dug material (stone dust is grey, dirt dust brown). A per-material dig sound needs a Voxel Digger object
   with an Audio Source and a WAV file named in the material's table entry; this is unverified.
Tests: `ctest -R voxel_more`.

### 7t, tools, items, veins, sound (third pass)

1. **Pickaxe and shovel.** Voxel Digger card: tick **Tools from inventory** (the list shows `shovel:2,pickaxe:5,iron_pickaxe:7`).
   Play on a scene that has an Inventory (the player). With empty hands you can only dig soft ground (sand, snow); stone
   will not dig. Give the player an item called `pickaxe` (Inventory card, or a pickup) and stone digs.
2. **Stackable dirt you can pick up and place.** Tick **Give inventory items**, dig: stacks of `dirt`, `stone`, `ore`... arrive in
   the bag (8 dirt per cubic metre, set in the Materials table). Tick **Build from items**: right mouse now builds with those items
   and uses them up; with none it does nothing.
3. **Ore veins.** Voxel Terrain card: set **Ore Veins** to 30, press **Regenerate layers**: long winding yellow veins appear in the stone
   (dig a trench to see them). The same **Seed** gives the same veins. **Topsoil**, **Dirt Depth**, **Rock Slope** and **Ore Pockets** are
   sliders for the layers.
4. **Dig sound per material.** Put a WAV file path in a material's table entry (code: `materials[i].digSound`), add an Audio Source to the
   digger's object: the first time that material is dug the file is loaded and plays while you dig it. Tested headless that the clip is
   loaded; the actual sound output is unverified.
5. The brush marker now matches the brush shape (cube, flat disc, can).
Tests: `ctest -R "voxel_more|voxel_bughunt"`.

### 7t, fourth pass (switches, all off by default)

**Tools scene.** Open `examples\voxel\voxel_tools.okayscene`, Play. The player holds a shovel and a pickaxe (the text on screen says what to
try). Left mouse digs dirt and sand with either tool; stone needs the pickaxe (the shovel's power is 2, stone is 4); ore needs a better tool
(`iron_pickaxe:7` in the digger's Tools list; give the player an item of that name to dig it). Walk onto falling debris: stacks of `dirt`, `stone`
and so on arrive in the bag. Right mouse builds with dirt items and uses them up.

1. **Cave-in.** Voxel Digger card, **Cave-in Span** to 4. Dig a wide tunnel (hold left mouse and sweep sideways under a hill), then let go: the roof
   that is more than about 4 voxels from a wall comes down as debris. A narrow tunnel holds.
2. **Big world generation.** In code or a script: `terrain.BeginGenerate(0.5, 8, 0.3, seed, true)`; the terrain fills itself a few milliseconds a
   frame and shows up when done (`Generating()`, `GenerateProgress()`).
3. **Water and lava.** Voxel Terrain card: tick **Water / lava flow in holes** (press **Lava preset** for lava). Voxel Digger card: type `f` in
   **Pour Key**. Play, dig a pit, aim at it and press F: water (blue blocks) runs down and fills the pit from the bottom. Dig beside it and it flows out.
4. **Footprints.** Voxel Terrain card: tick **Footprints and tracks**. Play and walk on dirt, grass or sand: a trail of small dents. Stone takes no print.
5. The foliage brush now finds the ground on a voxel terrain fast (no visible change; `ctest -R voxel_more` times it).
Tests: `ctest -R "voxel_more|voxel_bughunt|voxel_examples"`.
