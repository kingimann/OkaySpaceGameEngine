---
id: 7zsd
row: | 7zsd | Sandbox digging works in first and third person; crosshair, dig feedback, rebindable dig/place/pick actions, hotbar | New Project > Sandbox, or File > Examples > voxel > `voxel_sandbox` (also `voxel_big_world`); press V to switch view | New in this change; tested headless on Linux, not run in a window or on Windows |
---
## 7zsd. Sandbox digging: first and third person

**What it is.** The Sandbox's digger now aims the same way in both views. The aim line always goes through the
crosshair (the centre of the screen). In third person it starts where that line passes the character (not at the lens
behind you), the reach is measured from the character, and it will not dig inside your own body, with your head in the
ground, or the ground behind a wall you are looking over. The same Player Controller View setting (First Person,
Third Person, Both) is used; the Sandbox template now uses Both and starts in first person.

**Steps.**
1. Open the editor, choose New Project > Sandbox (or File > Examples > voxel > `voxel_sandbox`), press Play.
2. Look at the ground. A hollow orange marker shows where you will dig. Hold left mouse (or RT on a pad): the ground
   goes, the marker shrinks and turns white as the ground under it is taken away. Right mouse (or LT) builds.
3. Press V (or Back on a pad) to switch to third person. Dig again: the marker is still under the crosshair, and
   ground more than 14 m from the character is out of reach (the marker disappears).
4. Stand with a wall of ground between you and a spot you look at over your shoulder: it will not dig that spot.
5. Press 1 to 9 to pick the material to build with, or use the mouse wheel or the bumpers (LB / RB) to step through
   them. With a terrain that has no materials, the wheel changes the brush size instead. T (or a click on the right
   stick) picks the material under the crosshair.
6. Rebind: the actions are named Dig, Place, PickBlock, HotbarNext and HotbarPrev (the Input settings / remap
   screen list them; until you rebind Dig and Place they follow Fire and Aim, i.e. left and right mouse, RT and LT). Once an action is rebound its old default button stops working.

**What you should see.** The same dig in both views, a crosshair dot in the middle of the screen, the help line at
the top listing the controls. A material the tool is too weak for (Realistic preset, hardness above the tool
power) shows a red marker.

**Status.** Verified here: the `voxel_aim` test (ray start, reach, wall, body, both views with a real Player
Controller, actions, hotbar, progress, scene save) on Linux. Unverified: nothing was run in a window, on Windows
or with a real pad or touch screen.
