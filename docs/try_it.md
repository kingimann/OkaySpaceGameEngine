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
`spatial_audio`, `cutscene`, `behavior_trees`, `node_playground`, `physics`, `physics2d`, `weapons`, `touch_controls`.
Left out on purpose: `account-server` (a Python server), `netdemo` and `physicsdemo` (C++ sources), `editor-plugins` and
`visual_script` (not scenes). The packaging is checked by reading and by running the copy step of
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
| 7q | SDL2 inventory and small platform additions | `docs/sdl_inventory.md` (read it); the launcher's chat window (five call sites moved) | Not in a release yet. The new own-backend code ran on Linux (Xvfb) only, the Win32 part **never ran**. Launcher: syntax-checked, **not built or run** |
| 8 | Own model readers (off by default) | `OKAY_OWN_STACK=1` or `OKAY_OWN_<FMT>=1`, `examples\own_readers` | Switch works on a release; see the table |
| 9 | Own WAV decoder (off by default) | `OKAY_OWN_WAV=1`, `examples\own_wav` | Switch works; no log line |
| 10 | Own HTTP client (off by default) | none | **Cannot be tried on Windows** |
| 11 | Own platform layer (off by default) | `OKAY_PLATFORM_BACKEND=own`, player only, `examples\node_playground\flow.okayscene` | Seen running on Linux (Xvfb) only; **never run on Windows**; the editor and launcher cannot use it yet |
| 12 | OkayUI instead of Dear ImGui (off by default) | `OKAY_UI_BACKEND=okayui` | Unverified, only a few screens |
| 13 | Own crypto | none, on purpose | **Cannot and must not be tried** |

---

## 1. Vehicle: get in and drive

**What it is.** A car with a driver seat and a passenger seat. You walk up, press E, and the car listens to your keys.
Code: `VehicleSeat` and `VehicleController` (`docs/vehicle_controller.md`).

**Steps (template).**
1. Open the editor (`Tools\OkayEngine.exe`) or the launcher's Create page. Choose **New Project**, pick the template
   **Vehicle (get in and drive)** (group "Game kits"), give it a name, create it.
2. The scene opens with a red car and a person standing at its left (driver) door. The Hierarchy shows `Car`,
   `Passenger Seat`, `Player`, `Ground`.
3. Press **Play**.

**Steps (example file).** Same scene without making a project: **File > Open...**, paste
`<install folder>\examples\vehicle\vehicle_test.okayscene`, press Play.

**In play.**
* Walk to the left side of the car with **W A S D** and the mouse (third person: the camera orbits behind you).
* Within about two metres of the driver door a prompt says **Drive**. Press **E**: you sit in the car.
* **W / S** gas and brake or reverse, **A / D** steer, **Space** handbrake. The chase camera follows the car.
* Press **E** again: you get out at the driver's side and the car brakes to a stop and ignores the keys.
* Walk to the right door, the prompt says **Ride**. Press **E**: you are carried along but the car does not drive.

**How you know it worked.** Before you press E the car does not move when you press W. After E, W moves the car and
the prompt is gone. After the second E you are standing outside again and W moves you, not the car.

**First or third person.** The scene's player is third person. A first-person player can also board (the seat accepts
a player with a first or third person controller). To try it, select `Player`, remove the Third Person Controller and
add a First Person Controller in the Inspector. Unverified in the editor.

**Known rough.** From `docs/vehicle_controller.md`: the wheels are for looks and do not spin or turn; no sitting or
steering animation on the character; no camera per seat; multiplayer does not replicate seats; no script or visual
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

## 7q. SDL2 inventory and small platform additions

**What it is.** (1) A page that counts every SDL call still in the launcher, editor, player and shared code, file by file: `docs/sdl_inventory.md` (made by
`python3 scripts/sdl_inventory.py`), and the plan for getting rid of SDL2: `docs/sdl_removal_plan.md`. These are documents in the repository, not part of the release zip.
(2) Nine small functions and one bridge added to `okay::platform` (window state, focus and pointer questions, and `WrapSdl2Window`; listed in `docs/platform_layer.md`, "Stage 4").
(3) **Five call sites in the launcher now ask `okay::platform` instead of SDL. Nothing should look different.** This change is not in a release yet: the steps below are for the
release that contains it.

**What can and cannot be tried on Windows.** There is no switch for this: it is the default (sdl2) backend, so there is nothing to turn on. Only the sdl2 answers of the new
functions are reachable, through the launcher's five call sites. The own-backend versions (X11 on Linux, Win32 on Windows) are run only by the test program `test_platform_state`, on Linux, under a
virtual screen; no shipped program calls them, and **the Win32 versions have never run**. The editor was not changed.

**Steps (Windows, the release you run).**
1. Read `docs/sdl_inventory.md`, section 1 (the headline numbers) and section 4 (the ranking), then `docs/sdl_removal_plan.md` (what happens next, in order).
2. Start `OkaySpace.exe` and sign in. Open a chat (the default is its own window; if yours opens inside the launcher, turn on the setting **Open chat in its own window**). Minimise the chat window and
   restore it from the taskbar: it must come back drawn normally (not blank, not frozen). While it is minimised the launcher does not draw it (that is the "is it minimised" question).
3. The launcher counts the open conversation as "being looked at" while one of its windows has the keyboard focus (the other person then sees you are in the chat, and your unread dot clears).
   This needs a second account that sends you a message: with the chat focused the dot clears; while another program has the focus new messages stay unread until you come back.
4. Click the pop-in icon at the top of the chat list (its tooltip says "Put chat back inside the launcher"), open a conversation, then drag one small file from Explorer onto the chat panel inside the launcher window: a toast
   "Sending <file name>" should appear and the file goes to that conversation (**it really sends it**, so use a harmless file and a test account; the limit is 25 MB). Dropping the file anywhere else on
   the launcher works as before. (This is the pointer position on the whole desktop.)
5. One more site (the launcher keeps the window alive while it checks an expiring sign-in at start) is hard to trigger by hand; it is only syntax-checked.

**What was checked.** On Linux, Xvfb with no window manager, SDL 2.30.0: `test_platform_state` (ctest `platform_state_sdl2`, `platform_state_own`) 76 checks and 58 checks, 0 failures, three more runs of each the
same, also under ASan and UBSan; the five older platform tests plus the two new ones: 7 of 7 passed; `scripts/tests/test_sdl_inventory.py`: 12 tests. The test was written first and failed
(20 of 57 on sdl2, 19 of 57 on own) before the code. MinGW compiled `OwnWin32.cpp` and `Platform.cpp` (syntax only, no warnings). `launcher/main.cpp` was syntax-checked before and after the five changes and
**not built or run**, so the five call sites are unverified in the real program. CI does not run `platform_state_*` (no xdotool there; it reports skipped). Details: `docs/platform_layer.md`, "What was verified (stage 4)".

---

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
  (COLLADA). Tiny hand-written files from our tests, no outside source.
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
  the startup line; it is only in the engine's `AudioClip::LoadWAV` (whole-file loads; streamed large files still use
  the old parser).
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

**What it is.** `okay::platform` is the layer between our programs and the operating system: windows, keyboard, mouse, sound, game pads, timers.
Today it uses SDL2 (the default). We are writing our own version (X11 on Linux, Win32 on Windows). It is **off by default** and not ready.
**The honest summary: only the player can run on it. It has been seen running on Linux, under a virtual screen. It has never run on real Windows.**
The editor and the launcher cannot use it yet (see the end of this section).

**Step A: check that the switch took effect (no window opens).** In a Command Prompt, in the folder that holds `OkaySpace.exe`:
1. `set OKAY_PRINT_STACK_AND_EXIT=1`
2. `Tools\OkaySpacePlayer.exe`. It prints one line and exits. It starts `[okay] stack: platform=sdl2 ...`. That is the normal setup.
3. `set OKAY_PLATFORM_BACKEND=own` and run it again. The line now says `platform=own`. If it still says `platform=sdl2`, this release was built
   without the own backend.
4. `set OKAY_PRINT_STACK_AND_EXIT=` (empty) to turn the print-and-exit off again. Closing the window forgets all the variables.
(The same line is written at the top of every program's log. The code that makes it is `shared/OwnStack.hpp`, checked by the `own_stack` test; it was not run on Windows.)

**Step B: run the player on it (Windows: unverified, please try it and tell us what you see).**
1. `set OKAY_PLATFORM_BACKEND=own`
2. `Tools\OkaySpacePlayer.exe examples\node_playground\flow.okayscene 2> player_log.txt` (the `examples` folder is next to `OkaySpace.exe`).
3. What you should see: a window with the node playground scene, as without the variable. Click in it, press keys, **F3** (performance panel), **F11**
   (fullscreen and back), **Esc** (quit).
4. `type player_log.txt` should contain `[okay] stack: platform=own ...` and `INFO: 2D painter: software`. "software" is our own painter that draws with
   the processor. It is slower than the SDL one and a few pixels differ at the edges of text and rounded shapes.
5. For comparison, clear the variable (`set OKAY_PLATFORM_BACKEND=`) and run step 2 again.
Expect **no sound** on Windows: the Windows own backend has no audio device yet. If the window stays black, never appears, or the player exits at once,
that is the useful result: send us `player_log.txt`.

**What works on the own backend, and what does not** (from `docs/platform_layer.md`; the Linux items were run under Xvfb with xdotool, not on a real desktop):

| Works (Linux, own backend) | Does not work or not written |
|---|---|
| window, resize, fullscreen (needs a window manager), window icon | file drag and drop onto the window (Linux), touch, IME composition, high-DPI scaling, Wayland without XWayland |
| keyboard, mouse, mouse look, typed text, the clipboard between programs | a message box on Linux (the text goes to the log), monitor and resolution lists in Settings (still asked of SDL) |
| 3D through GLX, 2D through the software painter | any sound on Windows; a real GPU for the 2D painter |
| sound through ALSA, game pads through the kernel (evdev) | everything above on Windows: written and compile-checked, never run there |

**Linux (what was actually run).** With a built player (`-DOKAY_BUILD_PLAYER=ON`):
```
Xvfb :99 -screen 0 1024x768x24 &
DISPLAY=:99 OKAY_PLATFORM_BACKEND=own build/bin/okay-player examples/node_playground/flow.okayscene
```
The log shows `platform=own` and `2D painter: software`; the scene runs; **Esc** exits with code 0. This was verified in the stage 3 work
(`docs/platform_layer.md`, "What was verified"); it was not re-run for this page. The tests: `ctest -R platform` (needs Xvfb and xdotool for the window ones).

**The editor and the launcher cannot be tried on the own backend yet.** They create their windows and draw through SDL directly, so `platform=own`
there changes only timers, sound and game pads. On Windows that would **break voice chat and sounds** (no audio device in our Win32 backend), so
do not set it for them. `docs/sdl_inventory.md` lists every SDL call that is left, file by file; `docs/sdl_removal_plan.md` says in which order they go.

### 12. OkayUI

`OKAY_UI_BACKEND=okayui` (or `OKAY_OWN_STACK=1`) starts the launcher and editor on OkayUI where it is compiled in. Only
the screens that have an OkayUI port change (in the editor, the Console panel today); everything else stays Dear ImGui.
The About popup shows `ui=okayui` when it took effect. Whether the Windows release contains OkayUI is unverified: if not,
the line stays `ui=imgui`. Nothing was run in a real window.

### 13. Own cryptography

Not switchable, by owner decision (2026-11-14, CLAUDE.md rule 18). libsodium is the only crypto in a release, the
environment variable `OKAY_OWN_CRYPTO` does nothing at run time, and the startup line must say `crypto=libsodium`.
Nothing to try.

---

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

## Team tooling (not part of the Windows release)

How to try the collaboration tools ([collaboration.md](collaboration.md)); these run on a Linux checkout and on GitHub, not in
`OkaySpace.exe`:

1. Open `WORK_BOARD.md` on the `work-board` branch (after the workflow has run once on main): it now has a **Claims** table and
   a **Collision report**.
2. `python3 scripts/find_existing.py tween` prints what already exists for "tween" (feature map, code, tests, branches).
3. Push a branch named `claude2/x` that adds `#include <mutex>` to a header under `engine/include`: the **Branch lint** check
   fails with the file and line and says to use `debugutil::SpinLock`.
4. Open a PR without a "How to try it" section: the **PR hygiene** check fails and says what to add.
