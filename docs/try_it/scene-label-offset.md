---
id: scene-label-offset
---
## Scene view: the selected object's name sits on its object (bug fix)

The small name box of the selected object in the 3D Scene view used to be drawn a gizmo's length to the right of the
object, often on top of the next object (select cube A and its "A" sat on cube B). It now sits just above the object,
beside the gizmo's up arrow, and never on a gizmo handle.

1. Make a new 3D scene. Add two cubes (**GameObject > Cube** twice). Set the first one's Position to
   -1.5, 0.5, 0 and the second one's to 1.5, 0.5, 0. Rename them A and B.
2. Select A. Its name box sits just above the orange cube A, to the side of the green arrow, not on B. Select B: its box
   sits just above B.
3. Press the Rotate tool (the circle arrow button). The name stays by the object, inside the rings, or straight above the
   rings when the object is small; it is never on a ring. Try the Scale tool and a few window sizes: the same.
4. Switch the view to **2D**: the name sits at the object's corner, as before.

Tests: `ctest -R editor_layout` (the placement maths, with the numbers from the screenshot that showed the bug).
Checked by screenshots on Linux (Xvfb) at 1280x800, 1600x900 and 1920x1200; not checked on Windows.
