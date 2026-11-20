---
id: cam-up
row: | cam-up | Camera rigs and effects (Follow, Orbit, Look At, Group, walls, blends, shake presets, 2D rooms) | File > Open `examples\camera\camera_rigs.okayscene` and `examples\camera\camera_2d_rooms.okayscene`, then Play; GameObject > Camera > Camera Rig | Built and tested headless on Linux only; editor and Windows not run |
---
## cam-up. Camera rigs and effects

**What it is.** A camera rig is a shot that follows, circles or watches something for you, smoothly. It can look a little ahead of a
moving target, frame several things at once, stay out of walls, fade things in the way, blend to other shots and shake on hits. A 2D
camera can be held inside rooms and slide from room to room. Details: `docs/gameplay/camera_upgrade.md`.

**Steps (3D example).**
1. Open the editor. File > Open, pick `examples\camera\camera_rigs.okayscene`, press Play.
2. Walk the blue capsule with W A S D. The camera follows smoothly and leans a little ahead of you.
3. Walk behind the orange wall (toward the camera). The camera moves in instead of going through the wall, and glides back out.
4. Press `2`: the Orbit shot. Hold the right mouse button and drag to circle the player.
5. Press `3` (Look At: the camera stands still and turns to watch you), `4` (Group: it frames you and the orange ball and steps back
   to fit both), `5` (See-Through: the camera stays behind the wall and the wall turns see-through), `0` (back to automatic).
6. Effects: `Z` impact, `X` explosion, `C` footstep, `V` field of view kick, `B` hit flash (the screen edges darken), `H` handheld wobble on or off.
7. Make your own: select an object, then GameObject > Camera > Camera Rig: Follow (or Orbit, Look At). Select the new rig: the Inspector
   has sections Camera Rig, Framing, Group, Walls and Blend, and the Scene view shows its frustum, dead zone (green), soft zone (orange) and
   the wall probe. The Camera Preview inset (bottom right of the Scene view) shows what the rig sees.
8. Blend styles: select the Main Camera, look at Cinemachine Brain > Blend style (Cut, Linear, Ease In and Out, Ease In, Ease Out, Custom
   curve). Then press `1` and `2` in Play to see the difference.

**Steps (2D example).**
1. File > Open `examples\camera\camera_2d_rooms.okayscene`, Play.
2. Walk with W A S D. The view stays inside Room A. Walk right out of the room: the camera slides to Room B. Walk up: Room C.
3. Select "Shot 2D": Camera Rig, Framing and 2D camera sections hold these settings. Select a Camera Room (yellow box in the Scene view)
   and change its Size.

**From a script.** `Camera.Shake("impact", 1)`, `Camera.BlendTo("Shot Group", 0.8)`, `Camera.SetTarget("Boss")`, `Camera.AddToGroup("Boss")`,
`Camera.FovKick(10, 0.5)`, `Camera.SetFovOffset(8, 0.3)` (sprint), `Camera.HitPulse(0.9)`, `Camera.SetHandheld(1)`. In a visual script search the
palette for `Camera.` (each one is a node).

**What you should see.** No jitter at any frame rate, no pop when a wall moves in or out or when you switch shots part way through a
blend, and the example's two help lines of keys in the top left.

**Status.** Numbers were tested headless on Linux at 30, 60 and 144 frames a second (`camera_math`, `camera_rig`). The editor was
not run (menus, Inspector, gizmo and preview inset are compiled but unseen) and nothing ran on Windows or a GPU.
