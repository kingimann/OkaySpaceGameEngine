---
id: 7m
row: | 7m | UI library slice 2: game HUD parts and data-driven glue | File > Examples > UI library > ui_library_2 (then Play), or GameObject > UI > Game HUD and Data; file `examples\ui_library\ui_library_2.okayscene` | New in this change; headless tests and pictures only, real window and editor menus unverified |
---
## 7m. UI library slice 2: game HUD parts and data-driven glue

**What it is.** A UI screen with six cards for the parts added in slice 2 of the in-game UI component library
([ui_library.md](ui_library.md); [ui_component_audit.md](ui_component_audit.md) says what already existed). Game parts: ammo counter,
ability icon with a cooldown ring, interaction prompt, radial menu, kill feed, objective tracker, compass, item tooltip card.
Data parts: two-way binding, show/hide from a condition, a list filled from an array, clickable links and inline pictures in rich
text, Tab order, hot reload of a UI file.

**Steps.**
1. Open the editor from its install folder. **File > Examples > UI library (every in-game UI part) > ui_library_2**. (Or **File >
   Open...** with `<install folder>\examples\ui_library\ui_library_2.okayscene`.)
2. Press **Play**.
3. Card "Ammo, ability, prompt": click **Shoot** a few times and **Reload**; click **Use (or press Q)** and watch the ring sweep and the
   seconds count down; tick **I am near the door** and the "E  Open the door" prompt fades in, untick it and it fades out.
4. Card "Radial menu, kill feed": click a wedge of the wheel (the text changes to what you picked). Click **New kill** twice: lines appear
   at the top right of the screen and fade out later.
5. Card "Objectives, compass": click **Wolf** three times, then **Talk** and **Bridge** (progress 1/3, 2/3, ticks); **Again** starts over.
   Drag the heading slider: the compass strip slides.
6. Card "Item tooltip card": pick another item in the dropdown; the card changes name, colour and stats.
7. Card "Bindings and conditions": drag the volume slider and type in the box (both show the same number); go below 20 for the red
   warning; tick **Mute** for the "Muted" note.
8. Card "List, links, hot reload": **Add a score** and **Clear**; click the blue "manual" link ("Link you clicked: manual"). While the
   game runs, edit and save `examples\ui_library\hot_reload_demo.okayui`: the yellow lines change within half a second.
9. Press **Tab** (and Shift+Tab): the focus ring walks the buttons.
10. Stop Play. **GameObject > UI > Game HUD** and **Data** add each part to your own scene; select one to see its Inspector.

**What you should see.** Numbers, rings, lines and lists changing as described; the prompt hidden until the toggle is on; pictures in
`docs/images/ui_library/` (`hud-*.png`, `rich-links-icons.png`, `example2-*.png`, headless renders).

**Status.** Logic, saving and loading, and the shipped scene are tested headless on Linux (`ui_library2`, `ui_library2_example`,
`ui_library2_shots`; see the PR for the numbers). The pictures are software renders, not a real window. Nothing ran on Windows. The
editor menus and Inspector panels were written and syntax-checked but the editor was not built or run. Link clicks on a scaled
canvas, the Tab key in the editor's Game view and the hot-reload file path (relative to the folder the editor was started in) are
unverified in a real window.
