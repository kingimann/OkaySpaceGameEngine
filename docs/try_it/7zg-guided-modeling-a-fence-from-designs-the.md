---
id: 7zg
row: | 7zg | Guided modeling: make a fence from designs, customise it, swap parts, keep editing by hand | Modeling window > **Guided** page; `examples\modeling\guided_fences.okayscene` | New in this change; the engine rules are tested on Linux, the editor page compiles but was **never opened in a real window** (unverified) |
---
## 7zg. Guided modeling: a fence from designs, then your own edits

**What it is.** The existing Modeling window has a new **Guided** page. You pick a category, a thing (today only the
**Fence**), a starting design, and get a normal editable mesh. You can change its settings, swap parts, and keep
modeling by hand; rebuilding never overwrites your hand edits without asking. Written from general parametric
modeling; no code, models or assets from other projects. Design: `docs/guided_modeling_design.md`.

**Status.** The generation, the rebuild rules, the files and undo are tested on Linux (`ctest -R guided_modeling`, about
600 checks). The editor page (`editor/GuidedPanel.cpp`) compiles but nobody has opened it in a real window yet, and
nothing here ran on Windows. Pictures in a picture folder (pictures are no longer stored in the repository) are headless software renders of the generated
meshes (our own small rasteriser), not screenshots of the editor.

**Try it (the example scene).**

1. Open `Tools\OkayEngine.exe`, **File > Open...**, paste the path to `examples\modeling\guided_fences.okayscene`
   (next to `OkaySpace.exe`, or `%LOCALAPPDATA%\OkaySpace\examples\modeling\` for the installer). You should see eight
   fences in a row: picket, post and rail, ranch, wrought iron, chain link, stockade, low decorative, picket with a gate.
2. Click one fence in the Hierarchy. Open **View > Editors & Tools > Modeling** and press **Guided** (first in the list).
   You should see "Customise: Fence ..." with sliders.
3. Drag **Height** and let go. The fence rebuilds and the console says "Updated ...".
4. Under **Parts and materials** change **Pickets and bars** to another design, or the **Posts** design. Change a colour.
5. Open **Each part**, a kind (for example Pickets), press **Select** on one part: it jumps into Edit Mesh with that part's
   faces selected. Move it (G), then go back to the Guided page. Its row now says "[edited by you]".
6. Drag **Height** again. A window asks what to do with the part you edited: **Keep my edits**, **Discard my edits** or
   **Convert to regular mesh**. Try Keep: that part stays as you left it, everything else follows the new height.
   Pressing Cancel changes nothing.
7. In Edit Mesh add something of your own (Add page: a Cube, or extrude a face). Change a setting on the Guided page: your
   addition stays exactly where it was. This is the "your own faces are never touched" rule.
8. **Detach** on a part row, or **Convert to regular mesh**, are explicit: the part (or the whole model) stops following the
   design and stays as plain mesh. **Undo guided step** steps back.

**Try it (make a new one).** Modeling > Guided with nothing selected (or press "Make another one"): press a category
(Vehicles, Characters, Environment, Props say "No designs here yet", on purpose), pick **Fence**, click a starting design
card (pictures are drawn by our own renderer), give it a name and press **Create**. A mesh asset `Assets/<Name>.okaymesh` and
`Assets/<Name>.okayguided` appear in the Project panel.

**Your own parts and templates.** Select faces in Edit Mesh, then Guided > **My parts and templates**: type a name, pick
which kind of part it fits, **Save selection as part**. It then appears in that kind's design list (and in "Each part"). **Save
as template** keeps the whole setup; it shows as another card next to the built-in designs. Files are in
`Assets/GuidedTemplates/` (`.okaypart`, `.okaytemplate`, plain text); copy them to another project to reuse them.

**Not done or not verified (said plainly).** Only the fence exists; walls, stairs, doors, windows, roofs, vehicles,
characters, environment and props come in a later phase. The path is a list of typed points (no drawing in the 3D view). No
collider is added. Material numbers are stored but only colours are drawn today (per-face materials are a separate open
item). The Guided page's look and feel in a real window is unverified.
