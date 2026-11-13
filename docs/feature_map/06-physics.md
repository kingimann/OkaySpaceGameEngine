# 6. Physics

Rigid bodies, colliders, joints, scene queries and the debug view in 2D and 3D, plus destructibles and projectiles. Code is
`engine/include/okay/Physics/*` (11 headers, about 2.7k lines in `engine/src/Physics`) and two joint headers in `Components/`.
No dedicated physics doc exists (only the layer matrix in `docs/layers_tags_cameras.md`). Ragdolls are 5.8, controllers are area 4.
Since #575 (scene hierarchy) both step functions skip bodies under an inactive parent (`ActiveInHierarchy`, 1.17).

## 6. Physics

### 6.1 2D bodies and colliders
- **What:** Side-view and top-down games: Rigidbody2D bodies and 2D colliders are stepped by Physics2D (gravity, contacts with friction and bounce, triggers, sleeping).
- **Parts:**
  - 6.1.1 2D solver - `Physics/Physics2D` - Step: integrate, resolve contacts, fire collision and trigger callbacks, sleep resting bodies
  - 6.1.2 Rigidbody2D - `Physics/Rigidbody2D` - Dynamic / Kinematic / Static, mass, drag, gravity scale, bounciness, friction, spin (off by default), sleeping, opt-in continuous collision
  - 6.1.3 2D collider shapes - `Physics/Collider2D` - box, circle, capsule, edge (one-way platforms) and polygon colliders, trigger flag, offset, layer
  - 6.1.4 Tilemap collider - `Components/TilemapCollider2D` - builds merged box strips from a sibling Tilemap on Start
- **Code:** `Physics/Physics2D`, `Physics/Rigidbody2D`, `Physics/Collider2D`, `Components/TilemapCollider2D`, `editor/main.cpp` (Add Component and Inspector sections)
- **API:** `Rigidbody2D`, `BoxCollider2D`, `CircleCollider2D`, `CapsuleCollider2D`, `EdgeCollider2D`, `PolygonCollider2D`, `TilemapCollider2D`, `OnCollisionEnter2D`, `OnTriggerEnter2D`, `scene.physics()`, `Collision2D`
- **Editor:** Add Component > Physics 2D rows; Inspector sections for each component; Scene view collider gizmos (View > Scene Overlays > Colliders)
- **Docs:** -
- **Tests:** `physics`, `gravity`, `2d_expand`, `angular2d`, `tilemap_collision`, `physics_nan`, `physics_callback_removal`, `physics_dispatch_removal`
- **Status:** Works - Physics2D::Step and Rigidbody2D read; covered by the tests above (friction, spin, tilemap strips, NaN and freed-pointer safety)
- **Open:** no dedicated physics doc; 2D joints are the Joint2D component (6.3), solved inside Physics2D::Step
- **Related:** 6.3, 6.4, 6.5, 6.7, 2.6, 4.3
- **Words:** physics, gravity, 2D physics, Rigidbody2D, BoxCollider2D, CircleCollider2D, CapsuleCollider2D, PolygonCollider2D, EdgeCollider2D, one-way platform, trigger, TilemapCollider2D, sleeping, friction, bounce, restitution, Box2D-style
- **Seen as:** a sprite falls through the floor; a box jitters or never comes to rest; a ball does not bounce; a platform cannot be jumped up through; tilemap tiles do not block the player; a body spins when it should stay upright
- **Ask me like:** "make a one-way platform you can jump up through"; "why does my 2D ball not bounce off the wall?"

### 6.2 3D bodies and colliders, collider fit
- **What:** Rigidbody3D bodies and 3D colliders (oriented boxes, spheres, capsules, cylinders, exact triangle meshes) stepped by Physics3D, including heightmap terrain ground-following and voxel terrain collision.
- **Parts:**
  - 6.2.1 3D solver - `Physics/Physics3D` - Step: integrate, oriented-box (SAT) contacts, friction, bounce, sleeping, terrain ground-follow, tunnelled-floor catch, triggers
  - 6.2.2 Rigidbody3D - `Physics/Rigidbody3D` - body type, mass, drag, gravity scale, friction, terminal fall speed, axis freeze, spin, sleeping, continuous collision, ground signals (`groundedOnTerrain`, `groundNormal`)
  - 6.2.3 3D collider shapes - `Physics/Collider3D` - box, sphere, capsule, cylinder and mesh colliders; the mesh collider is exact per-triangle static geometry (see 6.2.5)
  - 6.2.5 Mesh collider - `Physics/MeshCollider3D`, `Physics/TriMesh`, `Physics/ConvexContact` - `MeshCollider3D` collides against the MeshRenderer's triangles through a BVH, placed by the world matrix (rotation and non-uniform scale); rays, sphere / capsule / box overlaps and casts, `ResolveSphere`, trigger overlaps and Rigidbody3D contact (sphere, capsule, box bodies; restitution, friction, spin); rebuilt when the mesh changes; a dynamic Rigidbody3D on the same object collides as the convex hull of the mesh (GJK/EPA), and bodies landing on a solid mesh raise OnCollisionEnter3D / Stay / Exit
  - 6.2.4 Fit to object - `Physics/ColliderFit` - `FitColliders` sizes colliders to the SpriteRenderer or MeshRenderer bounds; `autoFit` refits each frame
- **Code:** `Physics/Physics3D`, `Physics/Rigidbody3D`, `Physics/Collider3D`, `Physics/MeshCollider3D`, `Physics/TriMesh`, `Physics/ConvexContact`, `Physics/ColliderFit`, `editor/main.cpp` (Add Component and Inspector sections)
- **API:** `Rigidbody3D`, `BoxCollider3D`, `SphereCollider3D`, `CapsuleCollider3D`, `CylinderCollider3D`, `MeshCollider3D`, `FitColliders`, `scene.physics3D()`, `OnCollisionEnter3D`, `OnTriggerEnter3D`, `Collision3D`
- **Editor:** Add Component > Physics 3D rows, collider inspectors with a Fit to Object button, Mesh Collider inspector (triangle and BVH counts, convex-hull line when the body is dynamic, Rebuild), Scene view collider gizmos (the mesh collider draws its triangle edges, or the hull while the body is dynamic)
- **Docs:** -
- **Tests:** `physics3d`, `meshcollider3d`, `angular3d`, `collider_fit`, `physics_nan`, `physics_callback_removal`, `physics_dispatch_removal`
- **Status:** Works - Physics3D::Step, Rigidbody3D and the collider headers read; sleeping, terrain follow and spin covered by the tests above
- **Open:** the header says bodies must be top-level (un-parented) Transforms; the navmesh bake still treats a MeshCollider3D as its bounds box; no dedicated physics doc
- **Related:** 6.1, 6.3, 6.4, 6.5, 6.8, 2.6, 4.1
- **Words:** Rigidbody, Rigidbody3D, BoxCollider3D, SphereCollider3D, CapsuleCollider3D, CylinderCollider3D, MeshCollider3D, collider fit, autoFit, Fit to Object, SAT, gravity, terrain collision, sleeping, kinematic, triangle mesh, BVH, concave collider, static mesh, convex hull, ConvexContact
- **Seen as:** objects fall through a thin floor; a box stands on its corner or never settles; a collider is bigger or smaller than the mesh; a character sinks into terrain; a parented object does not collide
- **Ask me like:** "the collider does not match my model"; "make this crate stop sliding on the ramp"

### 6.3 Joints
- **What:** Positional constraints between a body and a world point or another body: rigid rod, spring, weld and hinge, with optional breaking.
- **Parts:**
  - 6.3.1 Distance, spring and pin - `Components/Joint2D`, `Components/Joint3D` - rod, damped spring and weld solved by impulse plus position correction after contacts
  - 6.3.2 Hinge - `Components/Joint2D`, `Components/Joint3D`, `Math/JointLimit` - pins a point and lets the body spin; motor in both; angle limits in both (degrees from the start pose; the 3D stop uses `ClampHingeRotation`)
  - 6.3.3 Breakable joints - `Components/Joint2D`, `Components/Joint3D` - `breakable` and `breakForce` snap the joint when stretched, `broken` stops it
- **Code:** `Components/Joint2D`, `Components/Joint3D`, `Math/JointLimit`, `Physics/Physics2D`, `Physics/Physics3D` (the joint pass in Step), `editor/main.cpp` (Joint inspectors)
- **API:** `Joint2D`, `Joint3D`, `Joint2D::Mode`, `connectedBody`, `anchor`, `spring`, `damper`, `breakable`, `useMotor`, `motorSpeed`, `useLimits`, `minAngle`, `maxAngle`
- **Editor:** Add Component > "Joint 2D (distance/spring/pin)" and "Joint 3D (distance/spring/pin/hinge)"; inspectors for all four modes, with Use Limits / Min Angle / Max Angle on a hinge
- **Docs:** -
- **Tests:** `physics_joints`, `2d_expand`, `hinge2d`, `hinge3d`
- **Status:** Works - joint solver pass in Physics3D::Step and the two headers read; distance, spring, pin and hinge covered by the tests above
- **Open:** Joint2D's header still says "no angular/hinge yet" and its Add Component row still says "(distance/spring/pin)" although a Hinge mode exists; joints connect by body name, not by object reference
- **Related:** 6.1, 6.2, 5.8
- **Words:** joint, hinge, spring, rope, rod, pin, weld, fixed joint, revolute, motor, connectedBody, breakForce, ragdoll joint
- **Seen as:** a rope or pendulum stretches or explodes; a door does not swing about its hinge; a spring never settles; a joint snaps unexpectedly
- **Ask me like:** "add a swinging door on a hinge"; "hang a lamp from the ceiling on a spring"

### 6.4 Fixed step with render interpolation, pose history
- **What:** Physics runs in fixed slices (1/50 s by default) whatever the frame rate, and bodies are drawn part way between their last two poses so motion stays smooth.
- **Parts:**
  - 6.4.1 Fixed physics step - `Scene/Scene` - `fixedTimeStep`, `UseFixedStepPhysics`, `maxPhysicsCatchUp` (the step loop is `Scene::StepPhysics`, owned by area 1.3)
  - 6.4.2 Pose history and interpolation - `Physics/PoseHistory` - last two physics poses plus the shown pose per body; `interpolatePhysics` and `PhysicsAlpha()`
- **Code:** `Physics/PoseHistory`, `Scene/Scene` (StepPhysics)
- **API:** `Scene::fixedTimeStep`, `Scene::UseFixedStepPhysics`, `Scene::interpolatePhysics`, `Scene::PhysicsAlpha`, `Scene::kDefaultFixedStep`
- **Editor:** none (the editor Play mode and the player turn it on)
- **Docs:** -
- **Tests:** `fixed_step`
- **Status:** Works - header comments read (player, editor Play and dedicated server use the fixed step; a bare Scene keeps one step per frame); covered by `fixed_step`
- **Open:** a body moved by game code jumps with no blending (by design, per the header); no setting in the editor to change the step rate
- **Related:** 1.1, 6.1, 6.2, 16.2
- **Words:** fixed timestep, FixedUpdate, interpolation, physics rate, PoseHistory, PhysicsAlpha, catch-up, 50 Hz
- **Seen as:** movement stutters or jitters when frame rate differs from the physics rate; the game plays faster or slower at 30 and 240 FPS; a teleported body smears
- **Ask me like:** "make physics frame-rate independent"; "my rigidbody jitters in the camera follow"

### 6.5 Queries and debug draw
- **What:** Rays, line and shape casts and overlap tests against 2D and 3D colliders and terrain, plus an overlay that draws colliders, contacts and the rays scripts cast.
- **Parts:**
  - 6.5.1 3D rays and casts - `Physics/Physics3D` - Raycast, RaycastAll, Linecast, SphereCast, BoxCast, CapsuleCast; layer mask, trigger switch, ignore object, `IgnoreHierarchy`
  - 6.5.2 3D overlaps - `Physics/Physics3D` - OverlapSphere, OverlapBox, OverlapCapsule
  - 6.5.3 2D queries - `Physics/Physics2D` - Raycast, RaycastAll, Linecast, OverlapPoint, OverlapCircle, OverlapBox, OverlapCapsule, CircleCast, BoxCast, CapsuleCast
  - 6.5.4 Debug view - `Physics/PhysicsDebug` - per-frame record of contacts and query rays, off by default; editor menu View > Scene Overlays > Physics Debug
- **Code:** `Physics/PhysicsDebug`, `Physics/Physics2D`, `Physics/Physics3D`, `editor/main.cpp` (Physics Debug menu and overlay)
- **API:** `Physics3D::Raycast`, `RaycastAll`, `Linecast`, `SphereCast`, `BoxCast`, `CapsuleCast`, `OverlapSphere`, `Physics2D::Raycast`, `OverlapCircle`, `Physics2D::BoxCast`, `Physics2D::CapsuleCast`, `Physics2D::OverlapCapsule`, `RaycastHit3D`, `RaycastHit2D`, `PhysicsDebug::Collect`, script builtins `raycast_hit`, `box_cast`, `capsule_cast` and `overlap` (registered in area 13)
- **Editor:** View > Scene Overlays > Physics Debug > Show physics, Rays cast from scripts
- **Docs:** `docs/scripting.md`; demo `examples/physics2d/casts_and_blast.okayscene`
- **Tests:** `queries`, `physics_queries_ext`, `physics2d_casts`, `script_physics`, `phys_math`, `physics_nan`
- **Status:** Works - query signatures and the debug record structs read; extended queries and debug recording covered by `physics_queries_ext`; 2D box and capsule casts, overlaps and their script and visual-script nodes covered by `physics2d_casts`; shape casts always end (a NaN or infinite reach is a miss, and a cast far from the origin stops after a fixed number of samples; `physics_nan`)
- **Open:** no physics doc, so the query list is only in the headers and `docs/engine_api.md`; 3D shape casts skip triggers by default while rays include them (by design, per the header)
- **Related:** 6.7, 13.6, 14.4, 9.5
- **Words:** raycast, linecast, spherecast, boxcast, capsulecast, overlap, RaycastHit, hit.point, hit.normal, ignore, layerMask, queryTriggers, physics debug, contact points
- **Seen as:** a ray hits the shooter's own body; a camera ray passes through a wall; a trigger blocks a ray; contact points and rays are hard to see while debugging
- **Ask me like:** "show me what my raycast is hitting"; "sphere-cast for a ledge check that ignores the player"

### 6.6 Destructibles and projectiles
- **What:** Voxel blocks shatter into physics debris and unsupported blocks collapse; bullets, rockets and arrows fly with a swept ray and damage health.
- **Parts:**
  - 6.6.1 Block fracture - `Components/Destructible` - `BreakBlock` and `BreakAt` replace a cube with debris cubes that despawn after `debrisLifetime`
  - 6.6.2 Structural collapse - `Components/Destructible` - blocks no longer connected to the ground or an anchor become dynamic and fall
  - 6.6.3 Destruction gun and debris cleanup - `Components/Destructible` - `breakButton` raycasts from the main camera; `TimedDestroy` removes shards
  - 6.6.4 Projectile - `Components/Projectile3D` - velocity, gravity, pierce, bounce, impact force, damage to the nearest HealthStat (`DamageGameObject`)
- **Code:** `Components/Destructible`, `Components/Projectile3D`, `editor/main.cpp` (Destructible and Projectile 3D inspectors)
- **API:** `Destructible`, `TimedDestroy`, `Projectile3D`, `DamageGameObject`, `BreakBlock`, `BreakAt`, `CollapseUnsupported`
- **Editor:** Add Component > "Destructible (Chaos)" and "Projectile 3D"
- **Docs:** -
- **Tests:** `destructible`
- **Status:** Works for destructibles (Destructible.hpp read; covered by the 175-line `destructible` test); Basic for projectiles (Projectile3D.hpp read; only exercised inside `shooter25d`, area 4's test)
- **Open:** destruction works only on BlockBuilder-style cube grids tagged `blockTag`; no fracture for arbitrary meshes; no test file of its own for Projectile3D (it is only referenced by `shooter25d`); no docs
- **Related:** 11.5, 11.4, 4.4, 6.2
- **Words:** destructible, Chaos, fracture, debris, collapse, shatter, break block, projectile, bullet, rocket, arrow, pierce, ricochet, TimedDestroy
- **Seen as:** blocks vanish instead of breaking; a wall floats after its base is shot out; too many shards slow the game; bullets pass through thin walls or do no damage
- **Ask me like:** "make walls shatter when shot"; "add a grenade that bounces and explodes"

### 6.7 Collision layers and trigger filters
- **What:** A 32 by 32 layer matrix says which collider layers touch; triggers can also react only to a tag or to chosen layers.
- **Parts:**
  - 6.7.1 Layer matrix - `Physics/Physics2D`, `Physics/Physics3D` - `SetLayerCollision`, `LayersCollide`, `CollisionMask`, `ResetLayerCollisions`; since #586 also `ApplyLayerCollisionPreset(Everything | Nothing | SameLayerOnly)`, `SetLayerCollisionGroups(maskA, maskB, enabled)`, `SetLayerCollisionByName`, `DisabledLayerPairs` (docs `layers_tags_cameras.md`); scene file `layermatrix` record
  - 6.7.2 Collider layer and trigger filter fields - `Physics/Collider2D`, `Physics/Collider3D` - `layer`, `triggerTag`, `triggerLayers`
- **Code:** `Physics/Physics2D`, `Physics/Physics3D`, `Physics/Collider2D`, `Physics/Collider3D`, `Scene/Layers`
- **API:** `scene.SetLayerCollision`, `scene.LayerCollisionMask`, `scene.ResetLayerCollisions`, `Collider3D::layer`, `triggerTag`, `triggerLayers`
- **Editor:** collider inspectors (layer, trigger tag); layer matrix editing is in the scene settings (area 1.3)
- **Docs:** `docs/layers_tags_cameras.md`
- **Tests:** `layers`, `physics_queries_ext`
- **Status:** Works - matrix code in both Step functions and the collider fields read; covered by `layers` and the trigger tag tests in `physics_queries_ext`
- **Open:** the matrix itself, its presets and its scene-file record are owned by area 1.3 (#570, #586); this page only covers how the solvers use it
- **Related:** 1.3, 6.1, 6.2, 6.5
- **Words:** collision layers, layer matrix, collision mask, trigger tag, triggerLayers, ignore collision, layerMask, physics layers
- **Seen as:** enemies bump each other when they should pass through; a pickup trigger fires for the wrong object; bullets hit the player who fired them
- **Ask me like:** "make bullets ignore the player layer"; "pickups should only trigger for the Player tag"

### 6.8 Forces, explosions and body push-out
- **What:** The four force modes, explosion shoves, and a lightweight push-out that keeps a transform-moved player (no Rigidbody) from clipping into walls and floors.
- **Parts:**
  - 6.8.1 Force modes - `Physics/ForceMode` - Force, Acceleration, Impulse, VelocityChange used by `AddForce(force, mode)` on both bodies
  - 6.8.2 Explosion force - `Physics/Rigidbody3D`, `Physics/Rigidbody2D` - `AddExplosionForce` with linear falloff and an upward bias, in 3D and in the flat plane
  - 6.8.3 Sphere push-out - `Physics/PlayerCollision` - `ResolvePlayerBody` calls `Physics3D::ResolveSphere` after the Transform moved
- **Code:** `Physics/ForceMode`, `Physics/PlayerCollision`
- **API:** `ForceMode`, `AddForce`, `AddImpulse`, `AddTorque`, `AddForceAtPosition`, `AddExplosionForce`, `ResolvePlayerBody`, `Physics3D::ResolveSphere`
- **Editor:** none
- **Docs:** -
- **Tests:** `physics_explode`, `physics2d_casts`, `phys_math`
- **Status:** Works - ForceMode, AddExplosionForce and ResolvePlayerBody read; explosion falloff and force modes covered by the tests above, including the flat-plane blast
- **Open:** ResolvePlayerBody approximates the body as one sphere
- **Related:** 6.2, 4.1, 4.2, 6.6
- **Words:** AddForce, AddImpulse, ForceMode, VelocityChange, explosion, blast, knockback, AddExplosionForce, push out, depenetration, ResolvePlayerBody
- **Seen as:** a grenade does not push crates; knockback is too weak or too strong; the player clips into a wall corner
- **Ask me like:** "add an explosion that throws the crates"; "give the player knockback when hit"
