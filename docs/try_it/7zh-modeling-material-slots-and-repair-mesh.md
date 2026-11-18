---
id: 7zh
row: | 7zh | Modeling: material slots, Repair Mesh, Spin, Extrude Along Path, Array Along Curve, measuring, snapping, modifier cost and Copy, material library, Add to scene | `examples\modeling_tools\material_slots.okayscene` and `spin_path_curve.okayscene` (File > Open...), then the Modeling window | New in this change; real window and GPU renderers unverified |
---
## 7zh. Modeling: material slots and Repair Mesh

**What it is.** The 3D modeler can now give faces to numbered material slots. Each slot has a colour, and changing a slot's colour
recolours every face in it at once. Slots are saved in the scene and in `.okaymesh` files. There is also a **Repair Mesh** button
that cleans up a broken or imported mesh in one click. Slots are colours only for now: per-slot pictures (textures) and shaders are
not built.

**Try the example.**
1. Open `Tools\OkayEngine.exe`, then **File > Open...** and pick `examples\modeling_tools\material_slots.okayscene`.
2. You should see two objects: **Slot House** (cream walls, a red roof, a brown floor and a blue door) and **Slot Ball** (a white
   ball with a green top and an orange bottom). Nothing else is needed: the colours come from the slots.
3. Click **Slot House**, open the **Modeling** window (the menu bar), tick **Edit Mesh**, open the **Paint** area and find **Material Slots**.
   You should see four rows: Slot 0 to Slot 3, each with a round button, a face count and a colour box.
4. Click the colour box of **Slot 1** and pick green. Both roof faces turn green at once. Press **Ctrl+Z**: the roof is red again.
5. Press key **3** (face mode), click one wall. Click the round button of **Slot 3**, press **Assign Sel**. That wall takes the door colour.
   Press **Select Slot** with Slot 3 chosen: the door and that wall are selected. **Clear Slots** removes all slots and the colours they gave (the mesh goes back to its plain colour).
6. Save the scene (Ctrl+S), close it and open it again: the slots and colours are still there.

**Try Repair Mesh.** Select any object with a mesh, tick **Edit Mesh**, press **Delete Faces** on a face or two to make a hole, then
**Repair Mesh**. The hole is capped and the Console says what was done, for example `Repair Mesh: merged 0 verts, capped 1 holes, flipped 0 faces; open edges 4 -> 0 (closed)`.

**Files.** `examples\modeling_tools\slot_house.okaymesh` and `slot_ball.okaymesh` are the same two models as plain files: in the Inspector set **Mesh File**
to one of them (it is in the Modeling window; the exact tab is unverified), or open one in a text editor to see the `slot N r g b a` lines.


**More modeling tools (same example folder).** Open `examples\modeling_tools\spin_path_curve.okayscene`. You should see a blue **Spun Vase**, a **Bent Horn**
(a box with a curved horn pulled out of its top) and a row of small cubes called **Curve Array** following a curve. a picture folder (pictures are no longer stored in the repository) is a
headless software render of it.
1. **Array Along Curve.** Click **Curve Array**. In the Inspector find **Modifiers**: the first one is *Array Along Curve*, with a time like `0.05 ms` under its name.
   Drag one of the curve points: the cubes follow. Press **Wave** or **Helix** or **Arc**: the row takes that shape. Untick **Follow the curve**: the cubes stop turning.
   Open its **...** menu and press **Copy**: a second identical modifier appears below it (the row is now doubled); **Remove** it again.
2. **Spin.** Click **Spun Vase**, open the Modeling window, tick **Edit Mesh**, press key **2** (edge mode) and click one edge of the vase, then press **Spin**
   (Y axis, 360 deg, 16 steps): a ring of new faces is made around the vase. Ctrl+Z removes it.
3. **Extrude Along Path.** Click **Bent Horn** (or a cube), Edit Mesh, key **3** (faces), click the top face, choose **Arc**, press **Extrude Along Path**: a bent arm grows out of the face.
4. **Measuring.** In Edit Mesh, under **Snapping and measuring**, read **Whole mesh** (width x height x depth, area, volume). Press key **1** (vertices), click two corners:
   **Selection** shows their distance and the x, y, z difference. Select edges: it shows their total length; faces: their area.
5. **Snapping.** Set **Snap to** to **Vertex** (or Edge or Face), tick **Snap (Ctrl)**, select a vertex, press **G** and move the mouse near another vertex: a yellow ring shows
   it and the vertex jumps onto it; the bottom line says "snapping to Vertex". Enter confirms, Esc cancels.
6. **Material library.** Select **Slot House**, Edit Mesh, **Material Slots**: pick a slot, open **Material library** and choose *Copper*: that slot takes the colour. Open **Palette** and choose *Barn*: four slots change at once.
7. **Picture per slot.** In a slot row, type an image path (for example one of your own `Assets/brick.png`) in the **picture path** box: in the software renderer the slot's faces show the picture.
   (The OpenGL and Direct3D renderers still show the colour; that part is not built yet.)
8. **Add to scene as object.** Modeling window > **Import** page > **Add to scene as object**: choose **Exact mesh** and press **Add Copy to Scene**: a copy appears next to the object, with a Mesh Collider (choose **Box** or **No collider** for the others).
9. **Shortcuts and recent tools.** Edit Mesh > **Shortcuts and recent tools** lists the keys and the last tools you used.


**Model faster (same section; real window unverified).** Open `examples\modeling_tools\spin_path_curve.okayscene`: it now also has a **Table** and a **Window Wall** made by the "how do I" recipes.
1. Click **Window Wall**, Modeling window, tick **Edit Mesh** (or press Tab). At the top of the 3D view, under the Object / Edit / Vertex / Edge / Face buttons, you should see a row of small tool buttons (Extrude Faces, Inset Faces, ... Repair Mesh, Sym X, Snap, Repeat, Find...). At the bottom-left a grey line says what to do.
2. Press key **3** (faces), click a face of the wall. Hover **Extrude Faces**: the tooltip says what it does. Click it: the face is pushed out. Press **Shift+R**: it extrudes again. Ctrl+Z twice.
3. **Right-click** (click, do not drag) in the 3D view: a menu of tools that fit the selection appears. Click **Inset Faces**.
4. Press **Ctrl+K**: a box opens. Type `bevel`: *Bevel Edges* is first (greyed out until edges are selected; hover for why). Clear it and type `window`: *Make a Window Hole (how do I)*. Select a wall face, press Ctrl+K, type `window`, press **Enter**: an opening with a frame is cut.
5. Select a few faces and press Ctrl+K, type `similar`, Enter on *Select Similar: Facing*: every face pointing the same way is selected. Try *Grow Selection*, *Shrink Selection*, *Select Up-Facing Faces*.
6. Edge mode (key **2**), click an edge, Ctrl+K, `round`, Enter: the edge is rounded. Ctrl+K, `table`, Enter: a new **Table** object appears (Edit Mesh, Material Slots recolours top and legs).
7. Open Edit Mesh > **Shortcuts and recent tools**: the **Recent** line lists your tools; **Undo history** lists them with names. Click an older step: the mesh goes back to before it.
8. Scale an object (the Inspector scale to 2, 1, 1), then Ctrl+K, `apply scale`, Enter: the scale returns to 1 and the mesh is as wide as before (the Measure line stays the same).

**Hide, drag-select and more (same section; real window unverified).** In `examples\modeling_tools\spin_path_curve.okayscene` the **House Roof Hidden** object has its roof faces hidden.
1. Click **House Roof Hidden**, tick Edit Mesh (Tab). Press **Alt+H**: the red roof faces appear and are selected. Press **H**: they hide again. Select a wall and press **/**: only that wall shows (local view); **/** again shows everything. Ctrl+Z steps back through these.
2. Press **B** and drag a rectangle round some vertices (Vertex mode, key 1): they are selected; try **C** (paint with the circle, wheel = size, Esc ends) and **L** (draw a loop). Without X-ray only the front side is picked; press **Alt+Z** (or the X-ray button) and drag again to select through the mesh.
3. Face mode (key 3), select a face, press **Shift+D**: a copy appears and follows the mouse; Enter keeps it. Press **E** on a face: it is dragged out along its normal; type `0.5` and Enter for an exact distance. Press **I**: move the mouse toward the middle of the face; click to keep the inset.
4. Edge mode (key 2): double-click an edge: its whole loop is selected.
5. Press **Sym Y** (or **Z**) in the toolbar, select a vertex, press **G** and move: the partners across that plane move mirrored. In Edit Mesh tick **Start with symmetry on: X**, leave edit mode and enter it again: Symmetry X is already on.
6. Ctrl+click two or more mesh objects in the Hierarchy, press **Ctrl+K**, type `join` and Enter: they become one object. Type `align` for Align Objects, `separate` on a mesh of several pieces to split it into objects, `cursor` then Enter to add a cube where the mouse pointed.

**Vertex colours, creases, shrinkwrap, reduce, library (same section; real window unverified).** Open `examples\modeling_tools\vertex_paint_creases.okayscene`: a **Gradient Bar** already painted red to blue with vertex colours, a **Creased Cube** (its edges are pink in Edit Mesh) next to a plain cube, both with the Subdivision modifier, and a **Wrap Target** sphere.
1. **Vertex colours.** Click **Gradient Bar**: it shows a smooth red-to-blue gradient. Tick Edit Mesh, Paint section, tick **Paint Brush (drag on mesh)** and **Vertex colours (smooth)**, pick green, drag over the bar: the colour blends in smoothly. Ctrl+Z undoes a stroke. Try **Smooth Colours** and **Clear Vertex Colours**. Switch the renderer in the editor's rendering settings (OpenGL, Direct3D 11, software) and look again.
2. **Creases.** Click **Creased Cube** (it has a Subdivision modifier): it stays boxy; the plain cube beside it rounds into a blob. In Edit Mesh press key **2**, click an edge of the plain cube, set **Crease sharpness** to 2, press **Ctrl+K**, type `crease`, Enter: that edge turns pink and stays sharp after subdivision.
3. **Shrinkwrap.** In the Hierarchy click **Gradient Bar**, Ctrl+click **Wrap Target**, tick Edit Mesh on the bar, select a few vertices (key 1), press Ctrl+K, type `snap`, Enter: those vertices jump onto the sphere. Try `shrinkwrap` for the whole mesh and change **Surface offset**.
4. **Reduce Polygons.** Click the **Wrap Target** sphere (many triangles), Edit Mesh, set **Reduce to %** to 25, Ctrl+K, type `reduce`, Enter: the Console shows the triangle count dropping to about a quarter and the sphere still looks round.
5. **Mesh library.** Use **Save as Mesh Asset** on a model, then Modeling > Import page > **Mesh library**: it appears (press Refresh); **Add** puts a copy in the scene.

**Picture.** a picture folder (pictures are no longer stored in the repository) is a headless software render of the example scene (not a GPU render, not the editor window).

**What was and was not checked.** The tests `modeling_more` and `modeling_more_scene` ran on Linux (slots through scene save/load, `.okaymesh` files,
the software renderer drawing each slot's colour and picture, Repair Mesh, Spin, Extrude Along Path, Array Along Curve, measuring, snapping, the library, the dissolve/weld/delete fixes). Nobody has clicked these buttons in the real
editor window (the editor code was only syntax-checked, the snapping ring and the buttons in steps 1 to 9 are unseen), run the Windows build, or looked at the OpenGL, Direct3D 11 or Direct3D 12 renderers (they draw per-face colours, as before).
