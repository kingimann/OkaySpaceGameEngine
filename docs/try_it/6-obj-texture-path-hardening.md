---
id: 6
row: | 6 | OBJ texture path hardening | `examples\obj_texture_paths` | Shipped; refusal is silent |
---
## 6. OBJ texture path hardening

**What it is.** A model's material file can name a texture by any path. Network paths (`\\host\share`), device names,
and absolute or `..\` paths that leave the allowed folders are now refused before the disk is touched. The model still
loads, just without that texture. **There is no Console message for a refusal.**

**Steps.** `examples\obj_texture_paths` holds three one-cube models and `grid.png`:

| File | Its material names | Should look like |
|---|---|---|
| `cube_ok.obj` | `grid.png` (next to it) | cube with the grid texture |
| `cube_outside.obj` | `../../Assets/Textures/grid.png` (outside the model folder and not in a project) | plain white cube |
| `cube_network.obj` | `\\10.255.255.1\share\grid.png` | plain white cube, and the editor does not hang waiting for a network |

1. In the editor, **File > Import Model...**, choose `cube_ok.obj`. The cube should show the grid.
2. Import `cube_outside.obj` and `cube_network.obj`. Both should be plain, and the editor must stay responsive
   (the network one is the point: it must never try to reach `10.255.255.1`).

**Status.** Tested on main: headless `obj_texture_roots` test (72 checks). Verified here: a direct `Mesh::LoadOBJ` of these
three files laid out like the release (`examples/obj_texture_paths` two levels below an `Assets/Textures/grid.png`):
`cube_ok` gave texture `grid.png` and 24 vertices; `cube_outside` and `cube_network` loaded the same 24 vertices with no
texture. Unverified: what **Import Model...** does with the texture after it
imports (it may copy the model into the project and change where "next to it" is), the editor display, and Windows.
