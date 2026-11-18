---
id: 15b
group: switches
row: | 15b | Blender reader applies Mirror and Subdivision Surface modifiers (off by default, like the reader) | `OKAY_OWN_BLEND=1`, `examples\own_readers\mirror_subdiv_box.blend` | Tests only (Linux); not run in a Windows editor |
---
### 15b. Blender files: Mirror and Subdivision Surface modifiers are applied

**What it is.** Our own `.blend` reader (off by default, `OKAY_OWN_BLEND=1`) used to show a Blender model as it is stored,
without its modifiers, so a model with a Subdivision Surface looked blocky and a mirrored model showed only one half.
Now it applies the two modifiers Assimp applies, Mirror and Subdivision Surface, in the order of the modifier stack. Any
other modifier (Array, Boolean, Solidify, ...) is skipped and named in one log line, for example
`modifier 'Array' (Array) not applied (not supported) on 1 object(s)`.

**Steps.**
1. `set OKAY_OWN_BLEND=1`, start the editor (`Tools\OkayEngine.exe`).
2. **File > Import Model...**, choose `examples\own_readers\mirror_subdiv_box.blend` (our own test file, written by
   `scripts/gen_blend_fixtures.py`, not by Blender). It stores half a box (one side open) with Mirror, Array and
   Subdivision Surface level 1.
3. You should see one closed, rounded box, symmetric left to right (the mirror filled in the missing half and the
   subdivision rounded it), 80 triangles. One bottom edge on each side stays straight (it has a crease). The console
   or log shows the warning about the Array modifier.
4. If you have Blender files with Subdivision Surface or Mirror of your own, import them the same way: the result should
   look like Blender's viewport (the subdivision level used is the higher of the viewport and render levels).

**What to expect if it is off** (`OKAY_OWN_BLEND` not set): Assimp reads the file, as before.

**Status.** Verified here (Linux, headless): `blend_modifiers` (hand-computed results, the sample file, the four Assimp
test files with modifiers: triangle counts equal Assimp's on `SuzanneSubdiv_252`, `yxa_1` and `HUMAN`), `blend_reader`,
`blend_hostile`, `model_corpus`, ASan and UBSan runs. **Unverified:** the editor steps (no window opened), anything on
Windows, real Blender 2.8+ files (the corpus files are Blender 2.49 to 2.60), and Blender's own output (no Blender run).
