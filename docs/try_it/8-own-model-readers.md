---
id: 8
group: switches
row: | 8 | Own model readers: **the default** since 2026-11-17 (`OKAY_THIRD_PARTY=1` = back to Assimp / ufbx) | import anything; `examples\own_readers`; `OKAY_THIRD_PARTY=1` or `OKAY_OWN_<FMT>=0` to go back | Not in a release yet (branch `claude2/own-readers-integration`); Linux tests only, Windows unverified |
---
### 8. Own model readers

* **Now the default (owner approval 2026-11-17; branch `claude2/own-readers-integration`, not in a release yet).** Every model
  format that has a reader of ours is read by it with nothing set: FBX, COLLADA, Blender, 3DS, STL, PLY, `.x` and about 40 more
  (not NDO: that reader refuses every file). Assimp and ufbx are still in the program: a file ours refuses goes to them.
  **Steps, once a release has it:**
  1. Start `Tools\OkayEngine.exe` with nothing set. **File > Import Model...**, choose `examples\own_readers\cube.off`. A white
     cube appears and the **Console** says `model import '...cube.off': read by the own OFF reader in ... ms`.
     **Help > About OkaySpace** says `readers=own(fbx,dae,3ds,...)+assimp`.
  2. Close it. In a Command Prompt: `set OKAY_THIRD_PARTY=1`, then start `Tools\OkayEngine.exe` from that window. Import the same
     file: the same cube, and the Console says `read by Assimp in ... ms`. About says `readers=ufbx+assimp`. This is the way back
     for **every** model format (only the exact value `1` counts).
  3. Close it, `set OKAY_THIRD_PARTY=`, `set OKAY_OWN_OFF=0`, start again and import: `read by Assimp` for `.off` only; any other
     format is still ours. `OKAY_OWN_FBX=0` sends FBX back to ufbx; `OKAY_OWN_STL=0` / `OKAY_OWN_PLY=0` work for STL and PLY.
  4. If a model looks wrong with ours and right with `OKAY_THIRD_PARTY=1`, that is a bug in our reader: please report the file.
  Checked on Linux by `own_dispatch_c`, `own_dispatch_d`, `own_dispatch_gap`, `own_stack`, `own_stack_third_party` and
  `model_readers`; **nothing here was run on Windows** and no editor window was opened (unverified).
* The text below is from before the switch, when every reader had to be switched on; the variables still work the same way.

* **Variables.** `OKAY_OWN_STACK=1` turns on all of them (plus the settings below). Or one at a time: `OKAY_OWN_FBX`,
  `OKAY_OWN_COLLADA` (.dae), `OKAY_OWN_3DS`, `OKAY_OWN_BVH`, `OKAY_OWN_OFF`, `OKAY_OWN_SMD`, `OKAY_OWN_MD5`,
  `OKAY_OWN_MS3D`, `OKAY_OWN_ASE`, `OKAY_OWN_3MF`, `OKAY_OWN_AMF`, `OKAY_OWN_X3D`, `OKAY_OWN_DXF`, `OKAY_OWN_LWO`,
  `OKAY_OWN_BLEND`, `OKAY_OWN_MD2`, `OKAY_OWN_MD3`, `OKAY_OWN_IRRMESH`, `OKAY_OWN_B3D`, `OKAY_OWN_X`. Each set to `1`.
  A variable you set to `0` wins over `OKAY_OWN_STACK=1`. The newer readers (AC3D, Ogre, XGL, Irrlicht scene, NFF, MDC,
  HMP, CSM, Q3D, RAW, IQM, COB, NDO and others) have their own `OKAY_OWN_<NAME>` variables listed in
  `docs/content/own_readers_remaining.md`; the startup line only lists the 20 above, so for those the only proof is an import
  that works.
* **Sample files** (shipped in `examples\own_readers`): `cube.off` (OFF), `cube.3ds` (3DS), `skinned_strip.dae`
  (COLLADA), `cube_gzip.blend` (gzip `.blend`) and `cube_compressed_arrays.fbx` (FBX with zlib arrays). Tiny files from our
  tests (the last two written by our own `scripts/gen_blend_fixtures.py` and `scripts/gen_fbx_fixtures.py`), no outside source.
* **Steps.**
  1. Start the editor with `set OKAY_OWN_OFF=1` (or `OKAY_OWN_STACK=1`), open **Help > About OkaySpace**. It should say
     `readers=own(off)+assimp` (with `OKAY_OWN_STACK=1`: a long `own(fbx,dae,3ds,...)` list).
  2. **File > Import Model...** and choose `examples\own_readers\cube.off`. A white unit cube appears in the scene.
  3. Close the editor, start it again with no variable, import the same file: you get the same cube, read by Assimp.
* **How you can tell it is really ours.** Since 2026-11-16 (branch `claude2/own-readers-gap-2`, not in a release yet) every
  import writes one line in the **Console**: `model import '...cube.off': read by the own OFF reader in 0.05 ms (whole
  import 0.40 ms)`. Without the variable the same import says `read by Assimp in ...`. If ours refuses a file and Assimp
  reads it, the line says `read by Assimp (the own OFF reader refused it: <why>)`. FBX says `ufbx` or `the own FBX reader`,
  glTF `the own glTF reader`. Before that change only the About popup or startup line (`own(off)`) told you. Tested on
  Linux by `lwo_reader` (the line for a `.lxo` import); the Console showing it is **unverified** (no editor window was
  opened). Without Assimp in a build, a format whose own reader is off fails
  with `no OFF reader enabled: our own reader is off by default (set OKAY_OWN_OFF=1 ...)`; the Windows release has
  Assimp, so you will not see that message.
* **More sample files** for other formats are under `tests\data\models` in a source checkout (not in the release).
* **The "Assimp gap" readers (2026-11-16, off by default)**: OGRE binary meshes, ZAE archives and `.xml` model files. Each has
  a cube in `examples\own_readers`, written by our own `scripts/gen_gap_fixtures.py` (no outside source):
  * **OGRE binary `.mesh`**: `set OKAY_OWN_OGREBIN=1`, start the editor, **File > Import Model...**, choose
    `examples\own_readers\cube.mesh`. A unit cube appears (12 triangles, material `Cube/White`; no `.material` file ships, so
    it is the default white). `docs/content/model_readers.md`.
  * **ZAE (COLLADA in a ZIP)**: `set OKAY_OWN_ZAE=1`, import `examples\own_readers\cube.zae`. A red unit cube appears.
    For a ZAE with textures inside, ours writes each image next to the `.zae` as `<name>_zae_<file>`. `docs/content/model_readers.md`.
  * **`.xml` (Irrlicht scene or mesh, COLLADA, XGL, OGRE XML by the root element)**: `set OKAY_OWN_XML=1`, import
    `examples\own_readers\cube_irrmesh.xml`. A green unit cube appears. `docs/content/model_readers.md`.
  * **Modo `.lxo`** (added on branch `claude2/own-readers-gap-2`, not in a release yet): `set OKAY_OWN_LXO=1`, import
    `examples\own_readers\cube.lxo`. A **blue** unit cube appears (material `Blue`, colour 0.2 / 0.4 / 0.9, from the file's
    material item). The Console line says `read by the own LXO reader`. Without the variable Assimp reads it: the same cube,
    but **grey** (Assimp does not read Modo's material items) and the line says `read by Assimp`. `docs/content/model_readers.md`,
    section LXO.
  * Without the variable the same files are read by Assimp (the Windows release has it), so you see the same cube (grey for
    `.lxo`); the Console line on each import names the reader (see above). Checked on Linux by the `own_dispatch_gap` test (all
    four samples read as 12 triangles, area 6); **not run on Windows**.