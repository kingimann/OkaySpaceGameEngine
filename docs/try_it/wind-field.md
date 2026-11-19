---
id: wind-field
row: | wind-field | Wind: trees, grass, flags, leaves, a pushed box and the sound of wind | Editor: File > Open `examples\wind\wind.okayscene`, press Play; GameObject > Effects > Environment; Add Component > Environment; Lighting window > Wind | Tests pass on Linux (`wind_field`, `wind`); the editor and the example were not run on Windows or on a screen |
---
## wind-field. Wind: trees, grass, flags, leaves, a pushed box and the sound

**What it is.** One wind that blows trees, grass, a flag, a rope, leaves (particles) and a light box, with a wind sound that gets
louder in strong wind. You set the wind in one place: a **Wind** object (or the Wind Zone component). A scene with no wind object
looks and plays exactly as before. Details: `docs/wind.md`.

**Steps (the example).**
1. Start `Tools\OkayEngine.exe` and open a project.
2. File > Open, pick `examples\wind\wind.okayscene` (it is in the folder you installed to).
3. Press **Play**. Wait about 10 seconds: the wind rises and falls on a 24-second cycle (calm, storm, calm).
4. Look and listen.

**What you should see and hear.**
- (The camera looks toward -Z, so +X is to your right.) The trees, pines and little bushes lean to the right and shiver; stronger in the gusts. The one tree in the shaded corner on
  the right (inside the "Calm zone") stands still: that is a wind-free zone.
- The red flag on the pole waves and points downwind. The rope between the two posts swings sideways.
- Orange leaves drift across from the left (Particle System, "Affected by wind" at 1).
- The wooden box near the camera is pushed along in the strong gusts (Wind Drag, 8 kg; in a 14-second headless run it slid about 4 m and stopped between gusts).
- A soft "whoosh" that swells with the wind (Wind Sound: made by the engine, no sound file).

**Steps (make your own).**
1. Open any scene. GameObject > Effects > Environment > **Wind (whole scene)**. Select it: arrows appear in the Scene view.
   Set **Strength** to 8, **Gusts** to 0.5.
2. Select a tree or bush mesh: Add Component > Environment > **Wind Sway**. Press Play: it leans downwind.
3. Select a Foliage layer: set **Sway in the wind** (Inspector) to 0.8. Select a Cable: set **Sway in the wind**.
4. A physics box: Add Component > Environment > **Wind Drag** (it adds a Rigidbody 3D if missing).
5. A Particle System: the **Affected by wind** slider (1 = drifts with the wind, 0 = ignores it).
6. View > Lighting (Lighting window) > **Wind**: compass direction, strength, gusts, and a live arrow.
7. Fan: GameObject > Effects > Environment > **Fan (blows outward)**: leaves and light boxes near it blow away from the middle.

**Scripts.** In an OkayScript: `wind_set(12, 90)` (strength 12, blowing toward +X), `wind_gust(0.8, 0.3)`, `wind_speed()` (speed
where the camera is). In the new script language: `Wind.SetStrength(12)`, `Wind.SetGust(0.8, 0.3)`, `Wind.At(Vec3(0, 1, 0))`,
`Wind.Strength`, `Wind.Direction`.

**Pictures.** `docs/images/wind-example-t04.png` and `wind-example-t12.png` are headless software renders of the scene at 4 s and 12 s (still
images: they show the scene, not the motion, and the software renderer does not draw particles or the rope).

**Status.** Tested on Linux: the wind maths (noise, gust period, zone blending, no NaN) and the components in a scene (zones, particles,
drag, sway, foliage, cable, sound, save/load). Not run: Windows, a GPU renderer, the editor on a screen. The example scene was made by
a throw-away generator, loaded and run for 14 simulated seconds headless (22 swaying plant chunks, the box moved, no NaN), and rendered to the two pictures above with the software renderer; nobody has watched it move on a screen.
