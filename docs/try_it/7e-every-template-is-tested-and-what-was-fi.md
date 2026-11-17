---
id: 7e
row: | 7e | Every template and example scene is tested; the templates that were broken are fixed | New Project (each template), File > Examples, `ctest -R "templates_work\|examples_work"` | Tested on Linux, see "Every template is tested" below; no window opened |
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
