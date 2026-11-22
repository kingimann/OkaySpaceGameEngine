# Collider upgrade (internal record)

Owner report: "fix the colliders, they're all box colliders and don't act like they should. add many different types of colliders."
Branch `claude/colliders-1`. Written from general convex-geometry definitions (support functions, GJK / EPA, ear clipping,
quadric ray tests, solid-body inertia of primitives). No code from any other engine or library (rule 19a).

## Cause of the complaint (found by reading the code)

| Where | What it did | Effect |
| --- | --- | --- |
| `CapsuleCollider3D::Segment` | long axis was the local X/Y/Z axis, never turned by the object | a capsule rolled onto its side still collided as upright |
| `CylinderCollider3D` | a capsule with a slightly shorter segment | rounded ends, 0.475 m too tall; flat top and rim did not exist |
| `ResolveSphere` (3D) | capsule / cylinder pushed out of their world AABB | a box, in the "move and slide" helper |
| `TestColliders` (3D, Classic) | capsule vs box used the segment point nearest the box centre | a leaning capsule on a big floor sank until that one point touched |
| `InvInertia` (3D, Classic) | every shape but the sphere used its bounding box | wrong turning, no real inertia for long shapes |
| Editor debug draw / gizmo | cylinder drawn as a capsule; box drawn without its rotation | the picture did not match the physics |
| `CapsuleCollider2D::Segment`, `PolyShapeCollider2D::WorldPoints` | ignored the object's turn | capsules, edges, polygons did not rotate |
| `RayCollider` (2D) | capsule / edge / polygon ray hit their bounding box | a ray in the empty corner "hit" |
| `Physics2D::OverlapPoint` | no polygon / edge branch | polygon never found by a point query |
| 2D polygon | boundary-only closest point, radius 0 | hollow: a ball inside was never pushed out; box corners poked through |
| 2D capsule vs box, `OverlapBox` | nearest segment point to the box centre / circle stand-in | wrong touch at corners |
| `OneWayPassThrough` | used the unrotated one-way normal | a turned one-way edge blocked the wrong side |

## Existing / Gap

| Piece | Existing before | Gap (now done unless noted) |
| --- | --- | --- |
| 3D shapes | Box, Sphere, Capsule, Cylinder (capsule-like), Mesh (static triangles; dynamic = convex hull) | real Cylinder, Cone, Convex Hull, Compound, Plane, Heightfield |
| 3D narrow phase | OBB SAT, sphere / capsule reductions, GJK / EPA only for dynamic mesh hulls (`ConvexContact.hpp`) | one generic pair test (`TestGeneric`) over support functions, plane and heightfield tests, mesh-vs-generic |
| 3D rays / overlaps / casts | exact for box, sphere, capsule, mesh | exact for every new shape (`RayShape`, `GenericVs`) |
| 3D inertia | sphere exact, else bounding box (Classic); Improved had per-shape tensors | `MassShape` (volume, centre of mass, inertia) for the new solids; used by both modes |
| 3D physics material | `PhysicsSurface`, `PhysicsMaterialAsset`, per-collider friction / bounciness | nothing new needed: contacts read the surface of the object, so every new collider uses it (tested on a Plane) |
| 3D contact offset | none | `Collider3D::contactOffset` (skin) |
| 2D shapes | Box, Circle, Capsule, Edge, Polygon | Composite; Polygon solid (ear clipping for concave); Edge = chain; Circle / Capsule kept |
| 2D narrow phase | OBB SAT, circle reductions | GJK / EPA in 2D (`ConvexContact2D.hpp`) over convex pieces (`ColliderShapes2D.hpp`) |
| 2D rays / overlaps | exact box and circle only | exact for all (rays by piece, point / circle / box / capsule queries by piece) |
| 2D inertia | box, circle, else AABB | capsule, polygon, composite from the real area |
| Auto fit | `FitColliders` box / sphere / capsule (+ 2D box, circle, capsule) | Cone, Convex Hull (wrap around the mesh), Heightfield (copy a Terrain) |
| Editor | Add Component entries for the older shapes; box face handles ("Edit Collider") | entries for each new type; wireframes for every shape; handles for sphere / capsule / cylinder / cone; box handles follow rotation |
| Saved scenes | `boxcollider3d`, ... records, `physmat3d`, `trigfilter3d` | new types write their own reflected record (nothing for scenes that do not use them); `colskin3d` / `colskin2d` only when a contact offset is set |

## Notes and limits (honest)

- A Compound under a non-uniform scale squashes each part along its own axes (exact for even scale).
- Inertia of a stretched collider uses the mean stretch; the Classic solver turns a body with one number (the mean of the three).
- The Improved solver gives one contact point per generic pair (not a manifold), except where the old code already did.
- Heightfield contacts ignore hits whose normal is far from the triangle's own up normal (cell-seam ghosts); a steep ridge seen from the side can lose a contact.
- 2D concave polygons are cut into triangles (ear clipping); an outline that crosses itself is not cut.
- Limits that keep bad or huge input from eating memory or time: a Convex Hull keeps at most 4,096 points (a bigger
  cloud is thinned by farthest-point sampling, so a saved hull with more points loads as a slightly smaller shape); a
  dynamic Mesh collider thins its mesh the same way before wrapping it; a Compound holds at most 1,024 parts; a
  Heightfield grid is at most 2,049 x 2,049 samples (a resolution-2048 Terrain); heights and the height scale are kept
  within +-1,000,000 m, and NaN or infinity reads as 0.
