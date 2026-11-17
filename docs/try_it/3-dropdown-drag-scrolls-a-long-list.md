---
id: 3
row: | 3 | Dropdown: drag scrolls a long list | Any UI Dropdown with Max Visible set | Shipped |
---
## 3. Dropdown: drag scrolls a long list

**What it is.** A UI Dropdown with more options than it shows. Press and drag inside the open list to scroll it by whole
rows. A plain tap still picks a row, and after a drag, letting go never picks.

**Steps.**
1. New Project > 3D Scene (or any). **GameObject > UI > Dropdown**.
2. In the Inspector set **Max Visible** to 4 and add at least 10 options.
3. Press Play. Click the dropdown to open it. Press the mouse on a row, move the mouse more than 10 pixels up or down
   while holding, and the list scrolls row by row; let go: nothing is picked.
4. Open it again and click a row without moving: that row is picked.

**How you know it worked.** The list moves under the drag and the selected value does not change until you tap.

**Status.** Tested on main: `runtime_ui` test (drag, tap, clamp). Unverified: a real mouse or finger in the editor.
There is no momentum (no flick). Touch only exists on a touch screen.
