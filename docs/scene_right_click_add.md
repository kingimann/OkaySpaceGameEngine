# Scene view: right-click to add

Owner request (2026-11-18): "I want to be able to right click inside of the scene and add stuff."

## What it does

Right-click on the Scene view (3D or 2D) and a menu opens at the cursor. It is a click, not a drag: press and release the
right button without moving more than 4 pixels and within 0.35 seconds. Right-drag still orbits (3D) or pans (2D), and
holding the right button to fly with W A S D is untouched.

The menu, from the top:

* a search box (already focused: type `cube`, `light`, `tree`, a prefab name; Up and Down move, Enter adds);
* **Shapes & Things** (3D) or **Sprites & Shapes** (2D), **Lights**, **Camera**, **Empty Object**, **Character / Player**,
  **UI (menus, text, buttons)**, **Audio**, **Effects (particles)**, **Prefabs** (every `.okayprefab` under the project's
  Assets folder);
* when an object is under the cursor: **Focus on X**, **Rename X**, **Duplicate X**, **Copy X**, **Paste Here**, **Delete X**.
  (Paste Here is also offered on empty space once something is copied.) Right-clicking an object selects it first, as the
  Hierarchy does.

## Where the new object goes

1. the surface under the cursor (a collider hit by the physics ray, or the box of a mesh without a collider), else
2. the ground plane at height 0, else
3. 10 metres in front of the camera along the cursor ray.

In a 2D scene it goes to the clicked world position. If Snap is on (toolbar Snap, step from Snap settings), x and z (3D) or
x and y (2D) snap; on a surface or the ground the height is kept so the object still rests on it. A mesh is lifted so it sits
on the surface, not half inside it. The new object is selected, the scene is marked changed, and it is one undo step.
UI pieces (they live under a Canvas) are not moved.

## One copy of the creation code

There was no second menu to write: the Hierarchy [+] button, the Hierarchy right-click menu and GameObject > Create already
share `DrawCreateSlot` in `editor/main.cpp`. The right-click menu draws those same slots (`create.3d`, `create.light`, ...),
so everything they offer is here. What was added:

* `editor/SceneAddMenu.hpp`: pure code with no ImGui, tested by `tests/test_scene_add_menu.cpp` (written first; it did not
  compile until the header existed): click versus drag, ray against the ground / a box, fallback distance, snapping, resting
  on a surface, the menu list, the prefab list, the search.
* The search box needs a flat list of names, which the slots (they draw ImGui items directly) cannot give. So the shape
  tables (`kPrimitives`, `kNature`, `kProps`, `kStructures`) now live in the header and `DrawCreateSlot` loops over them, and
  the creators that were inline in `DrawCreateSlot` (lights, audio, particles, water, terrain, character, player controller,
  tilemap) became small functions (`AddLightObject`, ...) that both the slots and the search call.

Search finds shapes, nature, props, structures, terrain, water, character, player controller, lights, camera, empty object,
audio, particles, 2D sprite and tilemap, and project prefabs. UI widgets are only in the **UI** group, not in the search
(`DrawUIWidgetItems` draws them directly and has no list).

## Not covered

* In mesh edit mode the right-click keeps its own tool menu; during Play the menu is off; while an NPC's waypoints are being
  click-placed it is off.
* The rename box is its own small window (the Hierarchy's lives in the Hierarchy panel).
* Written from the owner's request and the existing menus. No code from another project.
