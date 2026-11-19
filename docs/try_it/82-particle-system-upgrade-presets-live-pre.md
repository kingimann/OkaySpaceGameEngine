---
id: 82
row: | 82 | Particle system upgrade: 17 one-click presets, live preview, shapes, ribbons, collision, script API | GameObject > Effects > Particle Effects; Add Component > Particle effect presets; example scene `examples\particles\particles.okayscene`; Script `Particles.Play()` | Headless tests and renders only; no Windows or GPU run (see the page) |
---
## 82. Particle system upgrade

**What it is.** The Particle System got 17 ready-made looks you pick with one click, a live preview with Play / Pause /
Restart / Step buttons, more ways to start particles (hemisphere, mesh skin, bursts, per distance), ribbon trails, flat and
upright billboards, picture sheets, bouncing off the scene, sub-effects, fewer particles far away, and a script object
called `Particles`. Old scenes load as they were.

**Steps.**
1. Start the editor and open `examples\particles\particles.okayscene` with **File > Open** (it is in the `examples` folder
   next to the program). Press **Play**. Fly around with **W A S D** while holding the **right mouse button**, **Space** goes up,
   **C** goes down. Every effect stands on its own pedestal with its name above it.
2. In any scene choose **GameObject > Effects > Particle Effects > Fire** (or Smoke, Rain, Confetti ...). The object appears
   already moving: the Inspector shows a small **Live preview** with the same particles.
3. In the Inspector, click **Pause**, then **Step** a few times: the effect moves one small step each click. **Restart**
   starts it again. **Speed** slows the preview down.
4. Select an object, **Add Component > Particle effect presets > Portal**. Open **Look extras** and move **Trail length**:
   the sparks get ribbon tails. Open **Hitting things** and tick **Hit things in the scene**, put a Box Collider under
   the effect and press Play: the particles bounce off it.
5. Right-click in the Scene view: the "add here" menu lists the same presets under **Effects**.
6. Scripts: put `GetComponent<Particles>().Burst(30);` in a script on an object that has a Particle System (or use the
   **Burst Particles** node in a Visual Script). `Particles.UsePreset("Rain")`, `.SetRate(40)`, `.SetColor(1, 0, 0, 1)`,
   `.Play()`, `.Stop()` work the same way.

**What you should see.** Fire flickers orange and red, Smoke grows and drifts, Rain falls as streaks, Confetti bursts as
tumbling coloured cubes and lands, Portal shows a glowing ring whose sparks spiral inward leaving ribbons, Campfire is
already burning when the scene opens. The live preview and the Scene view show the same particles.

**Status.** Verified here (Linux, no window): the simulation, the emission shapes, the over-life modules, the caps, the
collision, the sub-effects, the draw list, the 17 presets, the scene save and load, and the shipped example scene are
covered by the test `particles_upgrade` (and a headless picture of every preset, see `docs/particles_upgrade.md`).
Unverified: the editor menus, the Inspector rows and the live preview (compiled only, never drawn in a window), the
player drawing, any GPU renderer, and Windows. Glow (additive) is drawn as a brighter normal blend in the editor and
the player, because their painters have no additive mode.
