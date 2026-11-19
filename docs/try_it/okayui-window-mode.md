---
id: okayui-window
row: | okayui-window | OkayUI window mode (editor window and frame run by OkayUI, no Dear ImGui): Scene and Game views, Hierarchy, Inspector, Console, Project, dialogs, menu bar | `OKAY_DEV=1 OKAY_UI_BACKEND=okayui OKAY_UI_HOST=okayui`; steps in the section "OkayUI window mode" | Headless tests only; the editor was never built or started with it |
---
## OkayUI window mode (off by default; Scene, Game, Hierarchy, Inspector, Console, Project, dialogs)

What it is: the editor's window, frame and panels run by OkayUI alone, with Dear ImGui not started. It is a dev switch, so three settings are needed.

1. Start the editor (Windows: `OkayEngine.exe`, from a console or a shortcut) with the environment variables `OKAY_DEV=1`, `OKAY_UI_BACKEND=okayui` and `OKAY_UI_HOST=okayui`. Optional: `OKAY_UI_HOST_PROJECT=<a project folder>` (otherwise the current folder is used if it has an `Assets` folder; without a project the Project window and Scene Manager are not offered) and `OKAY_UI_HOST_STATE=<file>` (where the layout is remembered).
2. You should see: a window titled "OkaySpace Editor (OkayUI window mode)" and, in the console, the line `[okayui-host] window mode on (OKAY_UI_HOST=okayui): OkayUI owns the window; Dear ImGui is not started`. Any other combination of settings opens the normal editor.
3. Try: the tabs Scene and Game in the middle (the Scene tab shows the scene's meshes; drag with the mouse over the picture to orbit, use the wheel to zoom; Game looks through the main camera; 2D scenes show nothing yet). Hierarchy and Project on the left (click a row to select, `+` adds an object, right-click Duplicate or Delete, type in the search box), Inspector on the right (the selected object's fields; change one and the scene is marked unsaved), the Console along the bottom (it shows the lines the editor logs). Drag a tab to move a panel, click its x to close it; close and reopen the window and the layout comes back.
4. To see the error and save questions: with an unsaved scene, use the Scene Manager's open-scene button on another scene; "Save changes?" appears (Save, Don't Save, Cancel). A failed action shows "Something went wrong".
5. Pictures: `OKAYUI_SHOT=file.png` with the headless tests `okayui_hostviews`, `okayui_hostobjects`, `okayui_hostdialogs` writes a picture of the window (software-rendered, 8x8 test font; the real window uses the launcher look). **These are headless renders, not screenshots of the real editor window.**
6. **Honest status:** the editor program itself was not built or started with this; everything above was run only in headless tests. If something in steps 1 to 4 does not work in the real window, that is a bug to report.
