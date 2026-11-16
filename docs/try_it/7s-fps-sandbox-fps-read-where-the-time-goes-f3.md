---
id: 7s-fps
group: appendix
---

## 7s. Sandbox fps: read where the time goes (F3) and the dig-time fix

The Sandbox got slow from the first click on because the player and the editor ran a hidden software render of the whole scene, with shadows, every
frame once the digger's dust particle object existed. It now runs only while dust, lines or trails are actually alive, and without shadows.

1. Start the player (or the editor, press Play) on `examples\voxel\voxel_sandbox.okayscene` and press **F3** (in the editor: the Game view's stats overlay).
2. Read the lines under the frame times:
   * `renderer: Direct3D 11 (GPU)` (or OpenGL). If it says `SOFTWARE (CPU ...)` the GPU renderer is not being used and the game runs on the CPU at a few frames a
     second: tell us the reason shown on that line.
   * `voxel: 1 terrain, 13122 triangles, 25 chunk objects of 75` (or `one combined mesh` before the first dig).
   * `voxel: terrain 0.00 ms/frame, digger 0.02 ms/frame, last rebuild 0.49 ms (4 chunks)`. All three should stay far below 1 ms, also while you hold the mouse.
   * `draw: N drawn, M frustum-culled ...` once the GPU renderer reports it.
3. Hold the left mouse for a few seconds, let go, wait two seconds: the frame rate should not stay lower than before the first click.
4. To compare with the previous way: start from a Command Prompt with `set OKAY_VOXEL_OLD_REMESH=1` (old rebuild of the whole terrain on every edit; the F3 line
   then ends with `OLD REMESH ON`).
5. On a machine that falls back to the software renderer, shadows are now capped at 1024 texels (about four times cheaper). `set OKAY_SOFTWARE_SHADOW_CAP=4096` brings the old
   sharper shadows (and the old cost) back.
Tests: `ctest -R "voxel_overlay_depth"`; numbers: `bench_voxel_frame`.
