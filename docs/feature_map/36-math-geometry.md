# 36. Math & geometry

The numbers underneath everything: vectors, quaternions, matrices, scalar helpers, easing, bounds and rectangles, the IK solvers' maths, and the geometry helpers (frustum, rays, closest points) that are scattered through rendering, physics and modeling.
Covers `engine/include/okay/Math` (15 headers, all header-only, about 1.7k lines) plus geometry code in `Render/Culling`, `Render/Lightmap`, `Components/Camera` and `Render/Mesh`.
Small and stable on main; PR #571 (core wave 4, branch claude/core-wave4-combined) adds Mat3, rays, planes, shapes, intersection tests, integer vectors, noise, curves and colour spaces.
This area was split out of area 1 so maths can be targeted on its own; it is big enough to stand alone because of PR #571 and the IK solvers.

## 36. Math & geometry

### 36.1 Vectors and colours
- **What:** The 2D, 3D and 4D float vectors every system uses, and the RGBA colour type.
- **Parts:**
  - 36.1.1 Vec2 - `Math/Vec2` - 2D vector with Dot, Cross, Angle, SignedAngle, Perpendicular, Rotate, Reflect, ClampMagnitude, MoveTowards, SmoothDamp
  - 36.1.2 Vec3 - `Math/Vec3` - 3D vector: Cross, Project, ProjectOnPlane, Reflect, Angle, SignedAngle, Slerp, ClampMagnitude, MoveTowards, SmoothDamp; constants Zero, One, Up, Right, Forward (+Z), Back
  - 36.1.3 Vec4 - `Math/Vec4` - homogeneous coordinates and colours; `xyz()`, Dot, index access
  - 36.1.4 Colour - `Render/Color` - RGBA floats in 0..1, `FromBytes`, `Lerp`, `Luminance`, constants Black, White, Red and more
  - 36.1.5 Integer vectors - in flight (PR #571): `Vec2i`, `Vec3i` in `Math/VecInt` with exact compare, hashing, FloorDiv/FloorMod for chunk and tile maths
- **Code:** `Math/Vec2`, `Math/Vec3`, `Math/Vec4`, `Render/Color`
- **API:** `Vec2`, `Vec3`, `Vec4`, `Vec3::Cross`, `Vec3::Slerp`, `Vec3::SmoothDamp`, `Vec3::ClampMagnitude`, `Color::FromBytes`, `Color::Lerp`
- **Editor:** vector and colour fields in the Inspector (15.2)
- **Docs:** `docs/core_audit_notes.md` (math fixes); in flight: the math_geometry page on PR #571, not on main
- **Tests:** `math`, `math_extra`, `phys_math`, `2d_expand`, `core_audit`
- **Status:** Works - read the headers; opposite-vector `Vec3::Slerp` and `SmoothDamp` with zero dt fixed in #565, with tests
- **Open:** integer vectors and colour parsing (`Color::Parse`, colour spaces) are in PR #571; Color lives in `Render/` although it is pure maths
- **Related:** 36.2, 36.3, 2.1, 12
- **Words:** Vec2, Vec3, Vec4, Vector3, vector, dot product, cross product, slerp, SmoothDamp, Color, RGBA, lerp
- **Seen as:** a character spins the wrong way; a camera glides forever toward a target; NaN positions after a pause; a colour comes out too dark
- **Ask me like:** "Project this velocity onto the slope"; "Why does SmoothDamp give NaN when the game is paused?"

### 36.2 Quaternions and matrices
- **What:** Rotation as quaternions and 4x4 column-major matrices for transforms, view and projection.
- **Parts:**
  - 36.2.1 Quaternion - `Math/Quat` - `Euler`, `AngleAxis`, `FromToRotation`, `LookRotation`, `Slerp`, `Lerp`, `RotateTowards`, `Angle`, `ToEuler`, `FromRotationMatrix`, `Inverse`, `Conjugate`
  - 36.2.2 4x4 matrix - `Math/Mat4` - column-major, `TRS`, `Translate`, `Rotate`, `Scale`, `LookAt`, `Perspective`, `Ortho`, `Inverse`, `Determinant`, `Transpose`, `MultiplyPoint`, `MultiplyVector`, `PlanarShadow` (flat ground shadow)
  - 36.2.3 Conventions - Euler angles apply Z, X, Y (test comment); `Vec3::Forward` is +Z yet cameras and player models face -Z (test comments), so mirror-flipped views are the usual mistake
  - 36.2.4 Mat3 and Decompose - in flight (PR #571): `Math/Mat3`, `Mat4::Decompose` (translate, rotate, scale), `Quat::ToAngleAxis`
- **Code:** `Math/Quat`, `Math/Mat4`
- **API:** `Quat::Euler`, `Quat::LookRotation`, `Quat::Slerp`, `Mat4::TRS`, `Mat4::LookAt`, `Mat4::Perspective`, `Mat4::Inverse`
- **Editor:** Transform rotation fields and gizmos (15.2, 15.3)
- **Docs:** `docs/core_audit_notes.md`
- **Tests:** `math`, `3d`, `math_extra`, `phys_math`, `prefs_misc`, `core_audit`
- **Status:** Works - read the headers; `LookRotation` zero forward, tiny-matrix `Inverse` and Euler gimbal cases fixed with tests (#565)
- **Open:** PR #571 narrows the `Quat::ToEuler` gimbal-lock zone to about 0.08 degrees so roll survives at 89 degrees; no double-precision variants for large worlds (core_audit_notes); `Mat4::Decompose` is missing on main
- **Related:** 36.1, 1.3, 2.10, 5.5
- **Words:** Quat, quaternion, Euler, gimbal lock, LookRotation, Mat4, matrix, TRS, view matrix, projection matrix, Perspective, Ortho, inverse, column-major
- **Seen as:** objects rotate around the wrong axis; a model faces backwards; the camera view is mirrored; rotation flips near straight up or down
- **Ask me like:** "Rotate this vector 30 degrees about the slope normal"; "Get position, rotation and scale back out of a Mat4"

### 36.3 Scalars, interpolation and easing
- **What:** Scalar helpers and the curves used for animation and game feel.
- **Parts:**
  - 36.3.1 Scalar helpers - `Math/Mathf` - Clamp, Lerp, InverseLerp, SmoothStep, Repeat, PingPong, MoveTowards, DeltaAngle, LerpAngle, SmoothDamp, SmoothDampAngle, Approximately
  - 36.3.2 Easing functions - `Math/Easing` - `Ease` enum and `Easing::*` (Quad, Cubic, Sine, Expo, Back, Elastic, Bounce in/out/in-out), used by tweens, animation clips and UI animation
  - 36.3.3 Umbrella header - `Math/Math` - includes every Math header except the IK ones
  - 36.3.4 Curves - in flight (PR #571): `Math/Curves` shared curve helpers
- **Code:** `Math/Mathf`, `Math/Easing`, `Math/Math`
- **API:** `Mathf::Lerp`, `Mathf::SmoothDamp`, `Mathf::Clamp01`, `Ease`, `Easing::QuadOut`, `Easing::BounceOut`
- **Editor:** ease picker and curve preview in the Animator, Timeline and Tween Animation inspectors (editor/main.cpp)
- **Docs:** `docs/timers_and_tweens.md`
- **Tests:** `easing`, `math`, `math_extra`, `core_features`, `core_audit`
- **Status:** Works - every easing endpoint was checked in the core audit (#565); new easings are appended to the enum so saved numbers keep their meaning
- **Open:** `Mathf::Infinity` is the largest finite float on main (a real infinity arrives with PR #571); `Mathf::SmoothStep` argument order changes in PR #571
- **Related:** 1.2, 5.2, 12.4, 36.1
- **Words:** Mathf, Lerp, Clamp, SmoothStep, SmoothDamp, Repeat, PingPong, easing, Ease, ease in, ease out, bounce, elastic, back
- **Seen as:** an animation overshoots or snaps; a camera follow lags or oscillates; an angle jumps at 360
- **Ask me like:** "Use a springy ease for the menu pop-in"; "Damp the camera toward the target without overshoot"

### 36.4 Bounds, rectangles and shapes
- **What:** Axis-aligned boxes and 2D rectangles, and the shape and intersection types that grow around them.
- **Parts:**
  - 36.4.1 Bounds - `Math/Bounds` - centre and extents box: `Contains`, `Intersects`, `Encapsulate`, `SetMinMax`
  - 36.4.2 Rect - `Math/Rect` - 2D rectangle by min corner and size: `Contains`, `Overlaps`, `Centered`, `MinMax`
  - 36.4.3 Rays, planes, shapes, intersections - in flight (PR #571): `Math/Ray`, `Math/Geometry` (`Plane`, `Sphere`, `Capsule`, `OBB`, `ViewFrustum`, `Geom::Raycast`, `Geom::Overlap`), `Bounds::Raycast`
- **Code:** `Math/Bounds`, `Math/Rect`
- **API:** `Bounds`, `Bounds::Encapsulate`, `Rect`, `Rect::Overlaps`
- **Editor:** none (used by selection boxes and marquee selection in the editor windows)
- **Docs:** in flight: the math_geometry page on PR #571, not on main
- **Tests:** `core_features`, `core_audit`
- **Status:** Basic - read both headers (44 and 37 lines): only the minimum operations exist on main
- **Open:** possible bug: `Bounds::Encapsulate` on a default `Bounds` grows from the origin, not from the first point (fixed by the empty-box state in PR #571); `Rect::Overlaps` counts shared edges as overlap (PR #571 makes it strict); no ray, plane, sphere, capsule or oriented-box types on main
- **Related:** 36.6, 6.5, 2.8
- **Words:** Bounds, AABB, Rect, rectangle, box, extents, Encapsulate, intersect, Contains, Overlaps, ray, plane, OBB
- **Seen as:** a box grows from the world origin; two touching UI rectangles count as overlapping; a marquee picks nodes it only touches
- **Ask me like:** "Raycast against this box and return the hit distance"; "Grow the bounds to include every child mesh"

### 36.5 IK math
- **What:** The solvers and helpers behind foot, limb, chain and look-at IK, kept free of scene dependencies so the animation components share them.
- **Parts:**
  - 36.5.1 Two-bone solver - `Math/TwoBoneIK` - analytic law-of-cosines solve with pole hint, bend limits and soft reach
  - 36.5.2 FABRIK - `Math/Fabrik` - long-chain solver with pole target and bend limits
  - 36.5.3 CCD - `Math/CCD` - cyclic coordinate descent chain solver
  - 36.5.4 Shared helpers - `Math/IKMath` - `Finite`, `SafeNormalize`, `AimBone`, world rotation blending, one pole convention
  - 36.5.5 Joint limits - `Math/JointLimit` - hinge and ball limits measured from the rest pose, twist extraction
  - 36.5.6 Weight fade - `Math/IKFade` - smoothstep fade of an IK component's influence in and out
- **Code:** `Math/TwoBoneIK`, `Math/Fabrik`, `Math/CCD`, `Math/IKMath`, `Math/JointLimit`, `Math/IKFade`
- **API:** `SolveTwoBoneIK`, `SolveFabrik`, `SolveCCD`, `IKJointLimit`, `IKFade::Step`, `AimBone`
- **Editor:** IK gizmos and the Animator window (5.6, 5.7, editor/AnimatorWindow.*)
- **Docs:** `docs/ik.md`
- **Tests:** `ik_core`, `anim_ik`
- **Status:** Works - read the headers (each guards non-finite input); belongs to the animation and IK work: IK pull requests (#535-#539, #548-#551, #554, #484) are Grok's sessions, do not edit these files without asking
- **Open:** IK rewrite plan (5.7); in-flight IK PRs may change these headers
- **Related:** 5.6, 5.7, 36.1, 36.2
- **Words:** IK, two-bone, FABRIK, CCD, pole, joint limit, hinge, ball joint, soft IK, IKFade, AimBone
- **Seen as:** a knee bends backwards; a limb pops at full extension; a bone stretches or turns NaN
- **Ask me like:** "Limit the elbow to 150 degrees without changing the solver"

### 36.6 Geometry helpers elsewhere
- **What:** Geometry code that lives outside `Math/` because a system needed it first: the view frustum, ray and triangle tests, camera rays, closest-point and ray tests in physics, mesh queries.
- **Parts:**
  - 36.6.1 Frustum and bounding spheres - `Render/Culling` - `Frustum` from a view-projection matrix, `MeshLocalSphere`, `ObjectWorldSphere`, `CulledByFrustum` (see 2.8)
  - 36.6.2 Ray against triangle - `Render/Lightmap` - `RayTriangle` used by the lightmap baker's shadow rays (see 2.3)
  - 36.6.3 Camera rays and projection - `Components/Camera` - `ScreenPointToRay`, `ScreenToWorldPoint`, `WorldToScreen` (see 2.10)
  - 36.6.4 Mesh queries - `Render/Mesh` - `Bounds`, `Topology`, `PointInside`, `Bisect`, `ConvexHull`, `FaceNormal`, `ProjectUV*` (see 2.2)
  - 36.6.5 Physics ray and distance tests - `Physics/Physics3D`, `Physics/Physics2D` - `RayAABB`, `RaySphere`, capsule rays, `ClosestPointOnTri` live inside the physics sources (see 6.5)
  - 36.6.6 Modeling ray - `Modeling/EditTransform` - its own `Ray` struct (see 3.1)
- **Code:** `Render/Culling`, `Render/Lightmap`, `Components/Camera`, `Render/Mesh`
- **API:** `Frustum`, `RayTriangle`, `Camera::ScreenPointToRay`, `Mesh::PointInside`, `Physics3D::Raycast`
- **Editor:** Scene view picking and gizmos (15.3)
- **Docs:** -
- **Tests:** `frustum_culling`, `camera_cine`, `worldtoscreen`, `mesh3d`
- **Status:** Partial - each helper works where it lives, but there are separate private copies and no shared ray, plane or shape types on main
- **Open:** duplicated ray-box and ray-sphere code in `Physics2D`/`Physics3D`, `EditTransform::Ray`, `Lightmap::RayTriangle` and `Frustum`; PR #571's `ViewFrustum` is a second frustum type that is not wired to `Render/Culling`; ray against triangle is explicitly not in PR #571
- **Related:** 36.4, 2.8, 2.10, 6.5, 3.1
- **Words:** frustum, ray, raycast, intersection, closest point, ray triangle, picking, bounding sphere, point inside mesh, ScreenPointToRay
- **Seen as:** a click selects the wrong object; shadows leak through a thin wall; a ray misses a rotated box
- **Ask me like:** "One shared Ray and Plane type for physics, picking and culling"

### 36.7 Noise, curves, colour spaces and random streams
- **What:** Shared procedural helpers that exist today as several private copies, and the shared versions being added.
- **Parts:**
  - 36.7.1 Existing private noise - `Render/ProcTexture` (`Hash01`, `ValueNoise`, `Fbm`, `Worley`), `Components/Terrain` (`ValueNoise`, `Generate`), `Render/Mesh` (`ValueNoise`, `PropRand`), `Components/ParticleSystem` (`Noise3`), `Components/VirtualCamera` (`Noise`)
  - 36.7.2 Shared noise and curves - in flight (PR #571): `Math/Noise` (`NoiseField`), `Math/Curves`
  - 36.7.3 Colour spaces - in flight (PR #571): `Math/ColorSpace` (`Hsv`, `Hsl`), `Color::Parse`
  - 36.7.4 Random streams - in flight (PR #571): `Core/Rng` (PCG32 with streams); `Core/Random` on main is the xorshift generator (1.1)
- **Code:** `Render/ProcTexture`
- **API:** `GenerateProcTexture`, `proctex_detail::Fbm`; in flight: `NoiseField`, `Rng`, `Hsv`
- **Editor:** none
- **Docs:** in flight: the math_geometry page on PR #571, not on main
- **Tests:** `mesh_variant_hash`
- **Status:** In flight - PR #571 adds the shared versions; on main only the private copies exist
- **Open:** PR #571 brings its own test `math_geometry`; after it merges, move terrain, textures, particles and meshes onto the shared noise so a seed means the same thing everywhere; `Mesh::PropRand` overflowed for variants 6 and up (fixed, test `mesh_variant_hash`)
- **Related:** 1.1, 2.2, 2.6, 2.7, 29
- **Words:** noise, Perlin, value noise, Fbm, Worley, simplex, curves, colour space, HSV, HSL, Rng, PCG, random stream, seed
- **Seen as:** the same seed gives different terrain in two tools; procedural textures tile with seams
- **Ask me like:** "Use one noise function for terrain, clouds and particles"
