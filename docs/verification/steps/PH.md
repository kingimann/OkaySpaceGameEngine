# Hand-written verification rows: physics, vehicles and navigation

### V-PH-001 Vehicle (get in and drive) template
- **Map:** 6, 4
- **Where:** editor, File > New Project...
- **Needs:** a release that contains PR #887 (not merged when this was written); on older releases use V-PH-002
- **Steps:**
  1. Choose **File > New Project...** and the template Vehicle (get in and drive) (group Game kits).
  2. Press Play. Walk to the left door of the red car with W A S D; a prompt says Drive. Press E.
  3. Drive with W and S (gas and brake), A and D (steer), Space (handbrake). Press E again to get out.
  4. Walk to the right door (prompt says Ride) and press E.
- **See:** the car ignores keys until you press E at the driver door; you get out at the driver side; at the passenger door you are carried but cannot drive.
- **Partial:** wheels are for looks only and do not spin; no sitting animation; seats are not replicated in multiplayer; no script node to enter or exit yet (docs/gameplay/vehicles.md).
- **Link:** [vehicle_controller.md](../../gameplay/vehicles.md#vehicle-controller-arcade)
- **Grounded:** no

### V-PH-002 Vehicle seats example scene
- **Map:** 6, 11
- **Where:** editor, File > Open...
- **Steps:**
  1. Choose **File > Open...** and open `examples/vehicle/vehicle_test.okayscene` (in the release, the `examples` folder next to `OkaySpace.exe` if it is shipped; see try_it.md).
  2. Press Play and use E, W A S D and Space as in V-PH-001.
- **See:** the same get-in, drive, get-out and ride behaviour.
- **Partial:** the release may not ship the `examples` folder yet (PR #887); then open the scene from a source checkout. The headless test `vehicle_seat` covers the logic.
- **Link:** [../../try_it.md](../../try_it.md)

### V-PH-003 Vehicle (3D) template with suspension
- **Map:** 6
- **Where:** editor, File > New Project...
- **Steps:**
  1. Choose **File > New Project...** and the **Vehicle (3D)** template.
  2. Press Play and drive with W A S D.
- **See:** a car with suspension that drives on the road.
- **Partial:** see docs/gameplay/vehicles.md for known rough edges.
- **Link:** [vehicle_controller.md](../../gameplay/vehicles.md#vehicle-controller-arcade)

### V-PH-004 Navmesh bake
- **Map:** 9
- **Where:** editor, View menu
- **Steps:**
  1. Open a 3D scene with a floor. Open **View > Editors & Tools > Navigation**.
  2. Press **Add Nav Mesh Surface**, then **Bake**.
  3. Read the Console: "Navigation: baked N polygons ...".
- **See:** translucent blue polygons cover the floor in the Scene view; a `.okaynav` file appears next to the scene.
- **Partial:** one nav surface per scene is the normal case; carving only works while the scene runs (docs/gameplay/navigation.md).
- **Link:** [navigation.md](../../gameplay/navigation.md)

### V-PH-005 Navmesh from a Mesh Collider
- **Map:** 9, 6
- **Where:** editor
- **Steps:**
  1. In a 3D scene select the floor or a cube and press **Add Component**, choose **Mesh Collider 3D**; remove its Box Collider.
  2. Open **View > Editors & Tools > Navigation**, press **Add Nav Mesh Surface** and **Bake**.
  3. Use a ramp or an L-shaped mesh to see the difference.
- **See:** the baked area follows the real triangles, not a box; the Console prints the polygon and link counts.
- **Partial:** over 2,000,000 triangles are cut off with a warning; one crack makes the collider two-sided (docs/gameplay/navigation.md).
- **Link:** [../../try_it.md](../../try_it.md)

### V-PH-006 Skipped bake before Play
- **Map:** 9
- **Where:** editor, Navigation window
- **Steps:**
  1. With a baked surface tick **Bake Before Play**.
  2. Press Play, Stop, Play without touching anything.
  3. Move a collider and press Play.
- **See:** the second Play does not rebake (the Console may say the bake was skipped); after you move a collider it rebakes.
- **Partial:** the "skipped" Console line is new and was never seen in a real editor.
- **Link:** [../../try_it.md](../../try_it.md)

### V-PH-007 Show physics shapes
- **Map:** 6
- **Where:** editor, View menu
- **Steps:**
  1. Open **View > Scene Overlays > Physics Debug** and tick **Show physics (Scene + Game)**.
  2. Press Play with a falling Rigidbody 3D (**Add Component**).
- **See:** collider outlines and contacts are drawn in the Scene and Game views.
- **Partial:** none known.

### V-PH-008 Rigidbody falls and collides
- **Map:** 6
- **Where:** editor
- **Steps:**
  1. Add a **Plane** and a **Cube** above it (**GameObject > 3D Object**).
  2. Select the cube, **Add Component** and choose **Rigidbody 3D**.
  3. Press Play.
- **See:** the cube falls and rests on the plane. On Stop it returns to its old place.
- **Partial:** the plane needs a collider; the editor adds colliders for primitives but check the Inspector.
