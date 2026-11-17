---
id: ui-inspector-lock-undo
---
## Lock Inspector keeps the right object after Undo (bug fix)

**What was wrong:** with **Lock Inspector** on, pressing **Ctrl+Z** could make the Inspector show a *different* object. Undo
rebuilds every object in the scene, and the Inspector remembered the locked object by its place in memory, which a new object
could take over. It now remembers the object by its id (the GUID saved with the scene) and finds it again every frame.

**Try it** (editor, `Tools\OkayEngine.exe`):

1. **File > New Scene**. Add two cubes with the editor's create menu (**Cube**, twice). Name them `A` and `B`.
2. Select `A`. In the Inspector tick **Lock Inspector** (top of the panel).
3. Change something on `A` (for example its X position), then press **Ctrl+Z**.
   You should see: the Inspector still shows `A`, with the old X value.
4. Click `B` in the Hierarchy. The Inspector should still show `A` (it is locked).
5. Add another new object (for example a third **Cube**). The Inspector should still show `A`, not the new object.
6. Delete `A` (select it in the Hierarchy, press **Delete**).
   You should see: the Inspector stays locked and says **"Locked object is gone"**, with an **Unlock Inspector** button.
   It does not jump to another object.
7. Press **Ctrl+Z**. `A` comes back and the locked Inspector shows it again.
8. Click **Unlock Inspector** (or untick **Lock Inspector**): the Inspector follows the selection again.

**Also fixed the same way** (they remember the object by its id now): the Hierarchy **Rename** box, **Save as Prefab** box,
the editor's automatic placeholder camera, the Animator live preview, mesh edit mode (Tab) and the UI drag in the Scene view.
None of these can now act on a different object after an Undo.

**Checked here (Linux, headless):** `tests/test_inspector_lock.cpp` (lock, undo, new object, delete, undo again, new scene).
**Not seen:** the editor on Windows, the "Locked object is gone" message on screen.
