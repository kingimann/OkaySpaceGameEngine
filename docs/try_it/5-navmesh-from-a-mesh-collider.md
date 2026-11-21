---
id: 5
row: | 5 | Navmesh from a Mesh Collider | View > Editors & Tools > Navigation | Shipped |
---
## 5. Navmesh from a Mesh Collider

**What it is.** The navigation bake now reads the real triangles of a Mesh Collider, not just its bounding box. A
ramp, a floor with a hole or a concave room bakes as it is.

**Steps.**
1. New Project > 3D Scene. Add an object with a Mesh Collider 3D: select the floor or a cube, **Add Component >
   Mesh Collider 3D** (any object that has a mesh can take one), and remove the Box Collider if it has one.
2. **View > Editors & Tools > Navigation** > **Add Nav Mesh Surface** > **Bake**.
3. The Console prints `Navigation: baked N polygons, M links from K cells`. The Navigation window shows the polygons.
4. For a visible difference use a ramp or an L-shaped mesh: the walkable area follows its real shape (a box collider
   would give a plain rectangle).

**Limits.** Meshes over 2,000,000 triangles are cut off with a warning in the Console (`Navigation: the bake may be
missing geometry`). One crack anywhere in a mesh makes the whole collider two-sided (`docs/gameplay/navigation.md`).

**Status.** Tested on main: the headless test `navmesh_mesh_collider`. Unverified: the editor windows and menu names.
