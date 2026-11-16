Sample files for our own model readers
=======================================

At least one small file for every model reader we wrote ourselves, so you can switch a reader on and import its
file. All of them are our own: copies of our test files, or files written by our script
scripts/gen_own_reader_samples.py. Nothing here was downloaded or made with another program.

How to try one (Windows)
------------------------
1. Open a Command Prompt.
2. cd %LOCALAPPDATA%\OkaySpace
3. set OKAY_OWN_PMX=1          (use the switch from the list below)
4. Tools\OkayEngine.exe
5. Open or make a 3D project. Choose File > Import Model... and pick the file, for example
   examples\own_readers\rig.pmx. The Import Model window lists what it found (meshes, triangles, skeleton,
   clips). Press Import.
   For cube.irr, cube.lws and rig.md5mesh first untick "Copy the file into the project's Assets": each one names
   a second file next to it (cube.irrmesh, cube.lwo, rig.md5anim), and the copy takes only the file you picked.
6. The list below says what you should see. The variable lives only in that Command Prompt window.

Help > About OkaySpace shows which formats our code reads now (readers=own(...)). Today it names the first 20
switches below (FBX to X); the newer ones (AC3D to LWS) are not in that line yet.

The files: switch, file, what you should see
--------------------------------------------
OKAY_OWN_FBX       cube.fbx            a cube (12 triangles) with a small red texture inside the file
                   rig.fbx             a tube on a 3-bone chain (24 triangles), with a clip
OKAY_OWN_COLLADA   skinned_strip.dae   a strip on 2 bones, with a clip
OKAY_OWN_3DS       cube.3ds            a red cube
OKAY_OWN_BVH       walk.bvh            no mesh: a skeleton (empty objects) and 1 clip
OKAY_OWN_OFF       cube.off            a cube
OKAY_OWN_SMD       rig.smd             a small arm (3 triangles) on 4 bones, no clip
OKAY_OWN_MD5       rig.md5mesh         2 triangles on 3 joints; rig.md5anim next to it adds its clip
                   rig.md5anim         no mesh: a skeleton and 1 clip
OKAY_OWN_MS3D      rig.ms3d            4 triangles on 3 joints, with a clip
OKAY_OWN_ASE       scene.ase           3 small faces, a clip, and empty camera, light and helper objects
OKAY_OWN_BLEND     shapes.blend        a square, a triangle and an L shape (Blender 4.x style file)
                   rig.blend           a strip on an armature, with a clip "Walk" (Blender 2.79 style file)
OKAY_OWN_3MF       cube.3mf            a blue cube
OKAY_OWN_AMF       cube.amf            an orange cube
OKAY_OWN_X3D       cube.x3d            a green cube
OKAY_OWN_DXF       cube.dxf            a cube
OKAY_OWN_LWO       cube.lwo            a red-orange cube
OKAY_OWN_MD2       cube.md2            a cube
OKAY_OWN_MD3       cube.md3            a cube
OKAY_OWN_IRRMESH   cube.irrmesh        a light blue cube
OKAY_OWN_B3D       rig.b3d             a gold column on 2 bones; clip "animation" bends the top
OKAY_OWN_X         rig.x               the same column; clip "Bend"
OKAY_OWN_AC3D      cube.ac             a purple cube
OKAY_OWN_OGRE      cube.mesh.xml       a cube
OKAY_OWN_XGL       cube.xgl            a cyan cube
OKAY_OWN_IRR       cube.irr            two cubes: Irrlicht's built-in one (orange) and cube.irrmesh (light blue)
OKAY_OWN_NFF       cube.nff            a yellow cube
OKAY_OWN_TER       hill.ter            a 16 m square patch of ground with a round hill
OKAY_OWN_MDC       cube.mdc            a cube
OKAY_OWN_HMP       hill.hmp            an 8-unit square patch of ground with a round hill
OKAY_OWN_CSM       walk.csm            no mesh: 3 markers (empty objects) and 1 clip
OKAY_OWN_Q3D       cube.q3o            a red cube
OKAY_OWN_RAW       cube.raw            a cube
OKAY_OWN_IQM       rig.iqm             the column on 2 bones (white); clip "bend"
OKAY_OWN_COB       cube.cob            a green cube
OKAY_OWN_PMX       rig.pmx             the gold column on 2 bones (PMX files hold no clips)
OKAY_OWN_LWS       cube.lws            cube.lwo, turning once round in 2 seconds (clip "scene")
(no switch yet)    cube.stl, cube.ply  a cube each. Our STL and PLY readers are chosen when the program is built, so
                                       the download reads these two the old way.
OKAY_OWN_NDO       (no file)           our NDO reader only recognises the file and refuses it (docs/own_ndo_reader.md),
                                       so there is nothing to try.

The Import Model window lists the clips; how they play in the editor was not checked. The column rigs (rig.b3d,
rig.x, rig.iqm) bend their top 45 degrees and back in one second. Most shapes are 2 units across; the Import Model
window's "Resize to" makes them bigger.

Some copied test files name pictures that are not in this folder (cube.3ds: RED.TGA; rig.ms3d: skin.jpg and
glass.png; scene.ase: blue.png and skin.jpg; shapes.blend: tex/diffuse.png). They still import; expect plain colours.
rig.ms3d and scene.ase also hold a few broken bits on purpose (they were made to test our readers): our reader skips
those and says so in the Console.

Where the files came from
-------------------------
Written by scripts/gen_own_reader_samples.py from the published structure of each format:
  cube.3mf cube.amf cube.x3d cube.dxf cube.lwo cube.md2 cube.md3 cube.mdc cube.irrmesh cube.irr cube.ac
  cube.mesh.xml cube.xgl cube.nff cube.raw cube.q3o cube.cob cube.lws walk.csm hill.ter hill.hmp
  rig.b3d rig.x rig.iqm rig.pmx
Copies of our own test files (the script copies them too):
  cube.fbx           tests/assets/fbx/cube_zup_cm_bin.fbx      written by scripts/gen_fbx_fixtures.py
  rig.fbx            tests/assets/fbx/rig_bin75.fbx            written by scripts/gen_fbx_fixtures.py
  skinned_strip.dae  tests/data/models/skinned_strip.dae       written by hand for our COLLADA reader test
  cube.3ds           tests/data/models/cube.3ds                written by scripts/gen_3ds_fixtures.py
  walk.bvh           tests/data/models/walk.bvh                written by scripts/gen_bvh_fixtures.py
  cube.off           tests/data/models/cube.off                written by scripts/gen_off_fixtures.py
  rig.smd            tests/data/models/arm.smd                 written by scripts/gen_smd_fixtures.py
  rig.md5mesh        tests/data/models/skel.md5mesh            written by scripts/gen_md5_fixtures.py
  rig.md5anim        tests/data/models/skel.md5anim            written by scripts/gen_md5_fixtures.py
  rig.ms3d           tests/data/models/rig.ms3d                written by scripts/gen_ms3d_fixtures.py
  scene.ase          tests/data/models/scene.ase               written by scripts/gen_ase_fixtures.py
  shapes.blend       tests/data/models/blend_attr_41.blend     written by scripts/gen_blend_fixtures.py
  rig.blend          tests/data/models/blend_skin_anim.blend   written by scripts/gen_blend_fixtures.py
  cube.stl           tests/data/models/cube_ascii.stl          written by hand for our STL reader (PR #660)
  cube.ply           tests/data/models/cube_ascii.ply          written by hand for our PLY reader (PR #660)

The test tests/test_own_reader_samples.cpp imports every file here with our own reader forced on and checks that
every reader has a file. To remake the files, from the source checkout: python3 scripts/gen_own_reader_samples.py
