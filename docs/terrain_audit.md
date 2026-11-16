# Terrain audit: voxel terrain, heightmap terrain, water, foliage, streaming

What each part does, whether it is **OK**, a **BUG**, **SLOW** or a **LIMIT** (limitation), the number or behaviour seen, and what
was done. Written on branch `claude/voxel-edit-fps`. Machine: shared 4 cores, `-O2` for the benches (the player's physics step
and a few probes ran against an `-O0` library, said where). No GPU, no Windows. Tests for the fixes: `test_voxel_edit`,
`test_voxel_chunks`, `test_mesh_hash`, `test_terrain_water_speed`, `test_terrain_audit`. The dig frame rate itself is in
`docs/voxel_edit_perf.md`.

## Voxel terrain (`VoxelTerrain`, `VoxelDigger`)

| Area | What it does | Verdict | Seen | Done |
|---|---|---|---|---|
| Generation | Fractal noise surface + optional caves, clamped to a band of 3 voxels, bottom row sealed | OK | 1 ms (Sandbox), 5 ms (160x64x160), 13 ms (256x64x256) | - |
| Density storage | one `float` per cell, row-major | OK / LIMIT | 0.9 MB Sandbox, 6.2 MB large, 16 MB huge; the Inspector lets the grid go to 512 per side (134 M cells, 537 MB, no guard) | not changed (listed) |
| Save / load | one byte per cell (signed, over the clamp band), base64, one text line | OK / LIMIT | text 0.29 MB / 2.1 MB / 5.3 MB; save 9 / 16 / 38 ms; load + first mesh 13 / 72 / 308 ms; worst density error 0.018 (voxel 1.5), 0.012 (voxel 1.0); a blob of the wrong size is dropped without a message (the terrain loads empty) | per-face colours no longer saved (0.4 MB of the Sandbox scene, rebuilt on load); density text unchanged |
| Marching cubes | 16-cube chunks, gradient normals read the field half a cell either side | OK, was SLOW | 0.7 ms for 2 chunks | 2.7x faster, bit-identical to the kept reference loop (`BuildMeshReference`) |
| Normals at chunk borders | a vertex reads cells c-1 .. c+2 | **BUG** | the dirty box marked only c-1 .. c: 4 of 60 random edits left a mesh different from a full rebuild (stale normals beside every hole) | exact dirty set; border and grid-edge cells tested one at a time |
| Edits near the grid edge | brush box is clamped to the grid; `SampleDensity` clamps coordinates | OK | no crash or hole in 25 random edits on 7 grids | - |
| Brush input | `Dig`, `Add`, `SmoothAt`, `DigBox` | **BUG** | radius 0 (cell exactly under the brush), a NaN strength or position wrote NaN into the field; 5 of 7 checks failed | ignored now |
| Smooth | relaxes the box from a snapshot | SLOW | copied the whole field every call: 0.5 ms (large), 3.2 ms (huge) per frame | copies the box only; results bit-identical (tested) |
| Remove Floaters | flood fill on release | SLOW | 2-3 ms Sandbox, 20 ms large, 86 ms huge on EVERY release; found crumbs re-meshed every chunk (12 ms Sandbox, 190 ms huge) | one full sweep, then local (0.004-0.02 ms) and only those chunks re-meshed; same cells cleared (40 random rounds tested) |
| Stroke strength | `strength x dt` per frame | LIMIT | one 0.5 s hitch dug 0.5 s worth at once | at most 0.1 s per frame |
| Aim | ray from the camera | **BUG** | used the first camera in the scene (a minimap camera that comes first steered the brush); searched the scene every frame | scene main camera; remembers the terrain |
| Memory | chunk meshes (kept) + combined mesh | OK / SLOW | the old path held the surface twice (17 MB x 2 on the huge terrain) | chunk objects hold it once |
| Draw | one mesh | SLOW | the whole terrain re-hashed, re-expanded and uploaded per edit | chunk objects (see perf doc) |
| World edge | collision only inside the grid | LIMIT | a body 3 m past the edge fell to y = -106 m in 5 s; the sides of the block are open in the picture | listed in UNFINISHED |
| Physics vs voxels | Physics3D section 5: capsule of probe spheres against the field | LIMIT (physics lane) | 100 bodies 1.1 ms, 400 bodies 14 ms per step (`-O0` library, so about 3x less in a release); my probe bodies never reached "asleep" (unconfirmed why) | not touched |
| Foliage on a voxel terrain | brush casts rays at every mesh | SLOW | `RaycastSurfaces` 1.1 ms a ray (Sandbox), 5.1 ms (large) against one mesh, 0.32 / 1.7 ms against chunk objects (`-O0` library); a brush stamp is up to 1600 rays | listed (needs a field raycast) |
| Editor | Generate / Solid Slab / Clear on the card; no Scene-view dig tool (digging is Play only) | LIMIT | - | build line added to the card |

## Heightmap terrain (`Terrain`, `TerrainDigger`)

| Area | What it does | Verdict | Seen | Done |
|---|---|---|---|---|
| `Terrain::Apply` | rebuilds the whole mesh; normals by `Mesh::ComputeSmoothNormals`, which welds every vertex through a `std::map` | **SLOW** | per Apply, digging calls it every frame: 1.9 ms at 64x64, 8.2 ms at 128, 35 ms at 256, 169 ms at 512 | normals added up without the weld: 0.28 / 1.3 / 4.8 / 24 ms; the normals are bit-identical (7 grids tested), `SmoothNoWeld` itself 17.7 -> 0.7 ms on 66 k vertices |
| `TerrainDigger` input | digs while held | **BUG** | dug while a bag/chest/menu or text box was open (it only checked pause); aimed from the first camera | `GameInputBlocked` and the main camera, like the Voxel Digger |
| Brushes | `BrushSpan` limits the cells touched | OK | brush 0.003 ms | - |
| Dig floor | `digDepth` stops a held dig | OK | - | - |
| Save | heights as text, layers as run-length | not measured | - | - |

## Water

| Area | What it does | Verdict | Seen | Done |
|---|---|---|---|---|
| Wave mesh | rebuilt every frame: positions, indices, `ComputeSmoothNormals` | **SLOW** | 0.55 ms (48), 2.4 ms (96), 4.5 ms (128) per frame, always, even idle; the Sandbox lake is 48 | same normals without the weld: 0.077 / 0.32 / 0.58 ms |
| Buoyancy | per body, from the collider box | OK | - | - |
| Renderers | a new mesh each frame, so GL / D3D11 expand and upload it each frame | LIMIT | 4608 triangles in the Sandbox | listed |

## World streaming (`WorldStreamer`)

| Area | What it does | Verdict | Seen | Done |
|---|---|---|---|---|
| Cells | loads prefab files near the target, unloads past the ring | OK | `test_worldstreamer` 75 checks | - |
| Edited terrain | an unloaded cell is destroyed | **BUG** | a dug voxel cell came back whole after walking away and returning | a cell whose terrain was edited (`VoxelTerrain::editCount`) is kept loaded |
| Hitches | cells load synchronously, all new ones in one frame | LIMIT | by reading: a ring of 5 new cells in one frame | listed |

## Second pass (real-life digging and a moved terrain)

| Area | What it does | Verdict | Seen | Done |
|---|---|---|---|---|
| Voxel Digger aim | the ray was turned into the terrain's space by subtracting its position only | **BUG** | by reading: a terrain that is rotated or scaled was dug at the wrong place, the brush marker and the dust too | the aim, the marker, the dust and the debris spawn use the terrain's own transform (`tests/test_voxel_more.cpp`, three moved, turned and scaled terrains) |
| Debris landing | the landing spot was the debris position minus the terrain position | **BUG** | same: volume was built back in the wrong place on a turned or scaled terrain | worked out with `InverseTransformPoint`; debris also takes the terrain's rotation and scale |
| World edge | a body past the side of the grid falls for ever | **BUG** (was a listed limit) | y = -106 m after 5 s | option `VoxelTerrain::catchFalling` puts it back on the ground, or calls `onFall`; off by default |
| Debris cap | counted at the first frame of a piece, so a frame could go over the cap | **BUG** | peak 51 with a cap of 48 | counted when the piece is made |
| Piece colliders | one box per piece | LIMIT | by reading | listed |
| Foliage ray cost on voxel meshes | 1.1 ms per ray single, 0.32 ms chunked | LIMIT | measured earlier | listed |
| Network | dig edits are local only: in multiplayer each player's terrain would differ | LIMIT | by reading: `VoxelTerrain` has no replication, edits are not sent | listed (the host would have to own the field and send `Edit` calls) |

## Bug hunt (third pass, `tests/test_voxel_bughunt.cpp` and `tests/test_voxel_more.cpp`)

Each row was tried with a test. "OK" means the test passed without a change.

| Way to break it | Result | Done |
|---|---|---|
| Grids of 0, 1, 2, 3x1x5 cells: every brush, slump, loose pieces, undo, save, load, Update | OK (a grid is never smaller than 2 cells per side, `Resize` clamps) | - |
| NaN, infinite, enormous (1e30) and tiny (1e-30) radius, centre, amount, tool power, bedrock height, depth limit, volume | OK, the field stays finite; `AddVolume` of 1e12 stops | - |
| Edits on every grid corner, edge and just outside the grid, with and without materials, then compare the drawn surface with a full rebuild | OK, 0 triangles differ (the chunked surface and normals at the borders agree) | - |
| 3000 fast random edits in one stroke with slumps, then Ctrl+Z | OK, exact restore of density and materials | - |
| A stroke begun twice without ending | OK | - |
| Save, load, save, load | OK, worst density error 0.015, no drift on later round trips, materials equal | - |
| Play, dig with the Realistic digger, Stop (snapshot restored) | OK, field back within 0.05, no undo history, store not saved, debris not saved | - |
| Two terrains in one scene, camera over the second | OK, only the aimed terrain changes; destroying one leaves the other's chunks | - |
| Terrain under an inactive parent | OK, reactivating shows the dug terrain | - |
| A tool too weak for the ground (`power` below the hardness) | **BUG**: it still lowered the air cells beside the surface, so the surface eroded a little although `removed` was 0 | an air cell may now only change if a solid neighbour is a material the tool can dig |
| Brush marker with Cube, Cylinder or Plane brush | **BUG** (looks): the marker was always a sphere | the marker mesh and scale follow the shape |
| Editor undo and redo of voxel edits | UNVERIFIED: the Inspector buttons (Generate, Solid Slab, Clear, Layered ground) are scene edits; Ctrl+Z in Play belongs to the digger. No headless way to press the editor's undo | listed |
| Streaming of edited voxel cells | covered by the first audit (`editCount` keeps the cell) | - |
| Dig edits over the network | LIMIT: not replicated | listed |

## What is not covered

Physics core (another lane), the player controller (another lane), vegetation placement quality, heightmap terrain save size, Direct3D 12.
