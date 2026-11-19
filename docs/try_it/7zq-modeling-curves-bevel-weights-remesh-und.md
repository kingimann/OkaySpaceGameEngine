---
id: 7zq
row: | 7zq | Modeling: curves, bevel weights, Quad Remesh, reference images, mirror/undo/snapping fixes, faces keeping slots and UVs, slot remove and reorder | `examples\modeling_tools\curves_bevel.okayscene` (File > Open...), then the Modeling window > Edit Mesh | Linux headless tests only; editor window, Direct3D and OpenGL per-slot pictures unverified |
---
## 7zq. Modeling: curves, bevel weights, remesh, undo and attribute fixes

**What it is.** More of the modeler (same Modeling window as 7zh): Bezier curves to meshes, edge bevel weights, Quad Remesh, saved reference images, and fixes so edits keep material slots, UVs, vertex colours and seams.

**Steps.** Open the Modeling window and follow these.

More new steps (same branch):
- **Curves and bevel weights example**: File > Open `examples\modeling_tools\curves_bevel.okayscene` (picture: a picture folder (pictures are no longer stored in the repository)). You see a pipe, a flat ribbon, a swept square tube and a cube with only its top edges rounded, with three hint lines on screen. Press Play to look at it, Stop to edit. Select the cube, open Edit Mesh: its top edges are drawn green (bevel weights).
- **Coloured edges**: in Edit Mesh, creased edges are pink and bevel-weighted edges are green. Select two edges, Ctrl+K, type "crease" or "bevel weight", run Set; the colours appear at once. Clear removes them.
- **Quad Remesh**: add a Sphere, open Modeling > Edit Mesh, set "Quads target" to 300, press Ctrl+K and run "Quad Remesh". The Console says how many triangles became how many quads, and the sphere is now made of quads (switch on the wireframe to see them). Undo brings the old mesh back. Limits: it does not follow flow lines; slots, vertex colours and UVs are dropped.
- **Reference images are saved**: Import page > Reference images: add a PNG, save the scene, reopen it: the picture plane is still there (path, plane, size, opacity, flip, visible).
- **Direct3D per-slot pictures** (Windows, Direct3D 11 or 12 renderer): give two slots different pictures on the Material slots page; each slot should show its own picture. Written but never run on a GPU.
- **Mirror and Undo fixes** (Edit Mesh): add a Mirror modifier to a half model that has UVs or material slots; the mirrored half now keeps its picture and colours. In Edit Mesh, add a modifier from the Inspector, change the mesh, then press Ctrl+Z: the modifier list goes back together with the mesh (Ctrl+Y brings both back). Edge snapping ignores the hidden diagonal inside a quad.
- **Faces keep their look through edits**: in Edit Mesh on a mesh with two material slots (Material slots page) and UVs, select a face and Extrude, Inset, Subdivide, Poke or Duplicate it, or delete a face; the new faces keep the slot colour of the face they came from and the picture does not jump. The same holds for the Array, Solidify, Subdivision, Decimate and Displace modifiers (add one in the Inspector > Modifiers).
- **Slots: remove and reorder; UVs survive edits** (Edit Mesh > Material slots page): with two or more slots, pick one and press `- Slot` (its faces fall back to slot 0, later slots move down) or `Up` / `Down` (faces keep their colour, the slot just changes place). Ctrl+Z undoes each. UVs: unwrap a mesh (UV page > Smart Unwrap), then Subdivide or Poke a few faces: the new faces keep the UV layout (no jump or stretch in the UV view) and marked seams stay seams, including through the Array and Subdivision modifiers.

**What it is.** The 3D modeler can now give faces to numbered material slots. Each slot has a colour, and changing a slot's colour

**Status.** The engine side is covered by the Linux tests `modeling_more`, `modeling_more_scene` and `modeling_fuzz` (headless). The editor buttons and menus here were not compiled with the editor on Linux and not clicked in the Windows editor; the Direct3D 11/12 and OpenGL per-slot drawing never ran on a GPU. The picture a picture folder (pictures are no longer stored in the repository) is a headless software render.
