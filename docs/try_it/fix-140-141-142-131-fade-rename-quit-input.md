---
id: fix-140-141-142-131
---
## Bug fixes: particle fade, Scene Manager rename, Exit after a dialog, text box editing (#888: 140, 141, 142, 131)

**140. Particle "Fade Out" now lasts the whole lifetime.** Before, a plain Particle System faded out in a fraction of a
second (faster at higher frame rates), because the fade was multiplied again on every frame.

1. Windows editor: **GameObject > Effects > Particle System**. In the Inspector set **Lifetime** to 4 and leave
   **Fade Out** on and **Color Over Life** off. Press **Play**.
2. You should see each particle fade smoothly over its full 4 seconds (half visible at 2 s). It looks the same with
   VSync on or off.

**141. Scene Manager > Rename no longer saves your unsaved edits or deletes the saved file.**

1. Windows editor: save a scene (for example `MyLevel`). Change something without saving; the title says unsaved.
2. **View > Panels > Scene Manager**, click **Rename** on that scene. The dialog now says "The file is renamed as last
   saved. Your unsaved changes stay unsaved." Enter `MyLevel2` and click **Rename**.
3. You should see: the title still says unsaved, your change is still in the scene, and `MyLevel2.okayscene` on disk
   holds the last *saved* version (open it from the Project panel in another way, or press **File > Open** and choose
   **Don't Save** to see it). Nothing was written until you press Save.

**142. File > Exit and the window's close button keep working after a quit arrives while another dialog is open.**

1. Windows editor: change the scene without saving. Open another scene (Project panel or **File > Open**) so the
   "Save changes?" dialog is up.
2. While it is up, click the window's close button (X). Then press **Cancel** in "Save changes?".
3. You should see the "Unsaved Changes" quit prompt appear as soon as the first dialog closes. Press **Cancel**.
4. Now **File > Exit**: the "Unsaved Changes" prompt appears again (before the fix, Exit and the X did nothing for the
   rest of the session).

**131. In-game text boxes: hold Backspace/Delete, the Delete key, and a movable caret.**

1. Windows editor: **GameObject > UI > Input Field**, press **Play**, click the box and type `hello world`.
2. Press **Left** five times, type `X`: you get `hello Xworld`, and the caret is drawn where you are typing.
3. **Delete** removes the character after the caret, **Backspace** the one before it. **Home** / **End** jump to the
   start / end. Holding Backspace or Delete keeps deleting (after a short pause, like a normal text box); holding Left
   or Right keeps moving.

Test: `ctest -R fix_140_141_142_131` (runs each repro headless: fade at 30, 60 and 144 fps; rename with a real
EditorState and files on disk; the quit prompt's state machine; the input field with a real Input feed).
Not verified on Windows; the editor GUI steps above were not run here (the editor files were only syntax-checked).
