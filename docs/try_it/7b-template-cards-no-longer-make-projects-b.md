---
id: 7b
row: | 7b | Templates page cards open New Project, never create a project | Launcher > Templates, click a card | New in this change; launcher UI unverified |
---
## 7b. Template cards no longer make projects by themselves

Why: some people found about ten projects in the Projects list that they never made. Before this change, one click
(or a double click, or a click while scrolling) on a card on the Templates page made a project at once ("Platformer",
"Platformer 2" and so on) and opened the editor.

1. Open the launcher (`OkaySpace.exe`), go to **Templates** and click any card, for example Platformer.
2. You should land on **Create** with the **New Project** window open on the Details step, that template chosen and the
   name filled in. Look in your Projects folder: no new folder yet.
3. Close the window with Esc (still nothing made), or press Next, then **Create project** to make it.

Cleaning up the extra projects (this note deletes nothing for you): on **Create**, the projects list shows every
folder in your Projects folder that has an Assets folder. The folder is shown on the Create page as the Location.
For each one you do not want, right-click it (or use its menu) and choose **Remove from list** to only hide it, or
**Delete project...** to delete its folder after a confirmation. Names like "Platformer 2" are the repeats. Projects
you did not open since they were made are safe to remove; check the path before you delete.

**Status.** Tested on main once merged: `test_project_create` (289 checks) covers the rule that a template shortcut opens
the Details step and cannot start a second create. Unverified: the launcher screens themselves (the launcher was not
compiled or run on this machine).
