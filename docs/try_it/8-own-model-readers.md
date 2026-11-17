---
id: 8
group: switches
row: | 8 | Own model readers (off by default) | `OKAY_OWN_STACK=1` or `OKAY_OWN_<FMT>=1`, `examples\own_readers` | Switch works on a release; see the table |
---
### 8. Own model readers

* **Variables.** `OKAY_OWN_STACK=1` turns on all of them (plus the settings below). Or one at a time: `OKAY_OWN_FBX`,
  `OKAY_OWN_COLLADA` (.dae), `OKAY_OWN_3DS`, `OKAY_OWN_BVH`, `OKAY_OWN_OFF`, `OKAY_OWN_SMD`, `OKAY_OWN_MD5`,
  `OKAY_OWN_MS3D`, `OKAY_OWN_ASE`, `OKAY_OWN_3MF`, `OKAY_OWN_AMF`, `OKAY_OWN_X3D`, `OKAY_OWN_DXF`, `OKAY_OWN_LWO`,
  `OKAY_OWN_BLEND`, `OKAY_OWN_MD2`, `OKAY_OWN_MD3`, `OKAY_OWN_IRRMESH`, `OKAY_OWN_B3D`, `OKAY_OWN_X`. Each set to `1`.
  A variable you set to `0` wins over `OKAY_OWN_STACK=1`. The newer readers (AC3D, Ogre, XGL, Irrlicht scene, NFF, MDC,
  HMP, CSM, Q3D, RAW, IQM, COB, NDO and others) have their own `OKAY_OWN_<NAME>` variables listed in
  `docs/own_readers_remaining.md`; the startup line only lists the 20 above, so for those the only proof is an import
  that works.
* **Sample files** (shipped in `examples\own_readers`): `cube.off` (OFF), `cube.3ds` (3DS), `skinned_strip.dae`
  (COLLADA), `cube_gzip.blend` (gzip `.blend`) and `cube_compressed_arrays.fbx` (FBX with zlib arrays). Tiny files from our
  tests (the last two written by our own `scripts/gen_blend_fixtures.py` and `scripts/gen_fbx_fixtures.py`), no outside source.
* **Steps.**
  1. Start the editor with `set OKAY_OWN_OFF=1` (or `OKAY_OWN_STACK=1`), open **Help > About OkaySpace**. It should say
     `readers=own(off)+assimp` (with `OKAY_OWN_STACK=1`: a long `own(fbx,dae,3ds,...)` list).
  2. **File > Import Model...** and choose `examples\own_readers\cube.off`. A white unit cube appears in the scene.
  3. Close the editor, start it again with no variable, import the same file: you get the same cube, read by Assimp.
* **How you can tell it is really ours.** Only the About popup or startup line (`own(off)`). There is no per-import
  log line saying which reader ran, and the two readers give the same cube here (the corpus comparisons in
  `docs/own_stack_rollout.md` say how closely they agree). Honest gap: a "read with the built-in OFF reader" Console
  line on import would close this; it does not exist. Without Assimp in a build, a format whose own reader is off fails
  with `no OFF reader enabled: our own reader is off by default (set OKAY_OWN_OFF=1 ...)`; the Windows release has
  Assimp, so you will not see that message.
* **More sample files** for other formats are under `tests\data\models` in a source checkout (not in the release).
