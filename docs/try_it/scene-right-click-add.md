---
id: 7scrc
row: | 7scrc | Right-click in the Scene view to add objects | Editor > Scene view: right-click (no drag) on empty space or an object | Built, unverified in the real editor |
---

## 7scrc. Right-click in the Scene view to add objects

Branch `claude/scene-right-click-add-1`. Not in a release yet.

1. Open the editor, make a new 3D scene (File > New 3D Scene), look at the Scene view.
2. Right-click on empty space (a quick click: do not move the mouse). A menu opens at the cursor with a search box on top, then
   Shapes & Things, Lights, Camera, Empty Object, Character / Player, UI, Audio, Effects, Prefabs.
3. Pick Shapes & Things > Cube. The cube appears where you clicked (on the ground or on the surface under the cursor), is selected,
   and Ctrl+Z removes it in one step.
4. Right-click again, type `sphere` in the box and press Enter. Turn Snap on (toolbar) and add another: it lines up to the grid.
5. Right-click on the cube you made: the menu now also has Focus on Cube, Rename Cube, Duplicate Cube, Copy Cube, Paste Here, Delete Cube.
6. Hold the right button and drag: the view orbits as before; hold it and press W A S D: you fly as before. No menu opens.
7. Make a 2D scene (File > New 2D Scene): right-click, Sprites & Shapes > Sprite: it lands at the clicked spot.
8. Check both the light and the dark theme: the menu text should be as crisp as the other menus.

Status: the click rule, the placement maths, the menu list and the search have a headless test (`scene_add_menu`). The drawing and the
mouse handling in `editor/main.cpp` were only checked with a syntax-only compile on Linux; nothing was run in a real editor or on
Windows, and there are no screenshots. Details: `docs/editor/scene_right_click_add.md`.
