---
id: ui-notify-freeze
---
## UI Notify template: no freeze when toasts pile up (bug fix)

The **UI Notify** prebuilt screen froze the editor when the fifth toast was pushed into its stack of four. Fixed: a toast that is
pushed out now stops counting at once.

1. Windows editor: open or create a scene, then **GameObject > UI > Prebuilt Screens > UI Notify** (or the same entry under the
   UI menu), press **Play**.
2. Click the buttons **Info**, **Saved**, **Error**, **Confirm** and **Cancel** one after another, quickly, more than five times.
3. You should see at most four toast cards stacked in the top-right corner. The oldest card disappears when a new one arrives,
   and each card goes away by itself after about six seconds. The editor keeps responding.
4. Test: `ctest -R ui_toast` (it also pushes 10 toasts in one frame and runs the template's click sequence).
