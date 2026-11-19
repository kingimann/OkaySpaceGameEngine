---
id: add-component-menu
row: | add-component-menu | Add Component menu: wide popup, search, categories, Recent and Favorites | Editor: select an object, Inspector, "+ Add Component" (or right-click an object in the Hierarchy, Add Component...) | New; the model is tested, the window was only compiled, not seen |
---
## add-component-menu. Add Component menu: wide popup, search, categories, Recent and Favorites

**What it is.** The window that opens when you press **+ Add Component** in the Inspector was rebuilt: a wide popup with a big
search box, plain-word groups on the left and component cards (name, one line about it, a small badge) on the right.

**Steps.**
1. Open the editor, make or select any object (for example **GameObject > Create Empty**).
2. In the Inspector press **+ Add Component**. (Or right-click the object in the Hierarchy and choose **Add Component...**.)
3. Just type: the search box already has the keyboard. Try `light`, `sound`, `jump`, `rb` and a typo like `cammera`.
4. Press **Down** / **Up** to move, **Enter** to add the highlighted card, **Tab** to move to the group list (Up / Down then
   change the group, Enter or Tab goes back to the cards), **Esc** to close. You can also click a card or its **+**.
5. Click the **star** on a card to keep it under **Favorites**. Add a few components and look at **Recent**.
6. Add a **Rigidbody3D** and open the menu again: the first group is now **For this object** and suggests a Collider.
7. Open it on an object that already has a Rigidbody3D and look at it under **Physics**: it is greyed and says why. Click it or
   press Enter on it: the line at the bottom explains instead of doing nothing.
8. Add **LOD Group** to an object with no Mesh Renderer: the Console and a small line under the button say the Mesh Renderer was
   added too.

**What you should see.** About 520 by 420 pixels (it grows with the editor font). Matching letters are underlined in the accent
colour, the group is shown next to each result when searching, and "Nothing matches" shows "Did you mean..." buttons. Recent and
Favorites are kept after you close the editor (file `okay_addcomponent.txt` next to `okay_settings.txt`).

**Status.** The logic (search ranking, groups, Recent / Favorites file, suggestions, greyed reasons, keys) has automated tests
(`test_add_component_model`). The window itself compiled but was NOT seen on a screen and not run on Windows: layout details may
need a tweak after a first look.
