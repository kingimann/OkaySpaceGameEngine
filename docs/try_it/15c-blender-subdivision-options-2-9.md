---
id: 15c
group: switches
row: | 15c | Blender reader: Subdivision Surface options of Blender 2.9+ (UV Smooth, Boundary Smooth keep corners, limit surface, creases by DeRose's rules) | `OKAY_OWN_BLEND=1`, `examples\own_readers\subsurf_options_42.blend` | Tests only (Linux); synthetic file, not written by Blender |
---
### 15c. Blender files: the Subdivision Surface options of Blender 2.9+

**What it is.** Blender 2.9 and later give the Subdivision Surface modifier more options. Our `.blend` reader (off by
default, `OKAY_OWN_BLEND=1`) now reads and applies them: **UV Smooth** (which UV points stay put: none, corners, junctions,
concave corners, boundaries, or all smoothed), **Boundary Smooth: Keep Corners** (an open mesh keeps its corners),
**Use Limit Surface** (on by default in Blender: the points are moved onto the final smooth surface, so the shape is a
little rounder than plain subdivision) and **Use Creases** (creased edges stay sharp for some levels, by the rules of the
DeRose 1998 paper). Files older than 2.9 behave as before.

**Steps.**
1. `set OKAY_OWN_BLEND=1`, start the editor (`Tools\OkayEngine.exe`).
2. **File > Import Model...**, choose `examples\own_readers\subsurf_options_42.blend` (our own test file in the Blender
   4.2 layout, written by `scripts/gen_blend_fixtures.py`, **not** by Blender). It stores a flat 3 x 3 grid of points with
   the middle point raised, one creased edge, and a Subdivision Surface with 2 levels, limit surface on, UV Smooth
   "Keep Corners" and Boundary Smooth "Keep Corners".
3. You should see a square sheet with a smooth bump in the middle, 128 triangles. Its four corners are exactly at the
   corners of the original square (keep corners); without that option they would be pulled in. The edge from the bump to
   the middle of one side is sharper than the rest (the crease). A texture applied to it covers the whole square to its corners.
4. Your own Blender 2.9+ files with these options can be imported the same way.

**What to expect if it is off** (`OKAY_OWN_BLEND` not set): Assimp reads the file (Assimp does not read these options).

**Status.** Verified here (Linux, headless): `blend_modifiers` (limit positions checked against hand-computed values and
against each other across levels, every UV mode, keep corners, the paper's crease rules, the three new fixture files
against the same steps done by hand), `blend_reader`, `blend_hostile`, `blend_fixtures_check`, `model_corpus`.
**Unverified:** the editor steps (no window opened), anything on Windows, and above all real Blender 2.9+ files: the field
names and option values are from memory of Blender's file format (a `.blend` file names its fields but does not say
what the option numbers mean) and no real file with these options was available.
