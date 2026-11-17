---
id: create-remove-project
---
## Remove from list and Delete project (launcher Create page)

**Where.** Launcher > **Create**. Your projects are the folders inside the **Projects** folder next to `OkaySpace.exe`
(the Location shown on the page), plus projects you opened from other places with **Open folder**.

**Steps.**
1. Make two projects (New Project), or open a project folder. Right-click a project card.
2. Click **Remove from list**. A toast says "Removed <name> from the list (its files are still on disk)" and the card
   goes at once. Close the launcher and open it again: it is still gone. Open the folder in Explorer: the files are there.
   To get it back, use **Open folder** and pick it (it is also un-hidden when you make a project of the same name).
3. Right-click another project > **Delete project...**. The popup names the folder. **Delete from disk** moves the whole
   folder to the Recycle Bin (Linux: deletes it for good). The card goes and a toast says "Moved <name> to the Recycle Bin".
   When it cannot, the popup stays open with a red line saying why: the editor is open ("close the editor first"), the
   folder is your Projects folder / home / the launcher's folder, or it has no scene, or a file is in use.

Status: the list logic (hide, restart, a project that is both in the folder and opened by path, slash and trailing-slash
forms, more than 100 hidden projects, delete clean-up) is tested by `tests/test_project_list.cpp` (g++ on Linux). The
Recycle Bin call and the clicks were not run on Windows.
