---
id: 11j
group: appendix
row: | 11j | Editor panels dragged out into their own windows on our own code (on with our own platform, the default on this branch) | `Tools\OkayEngine.exe`; drag a panel's tab (for example Project) outside the editor window (section "Editor: panels in their own windows on our own code") | The panel opens in its own window and works there (search, clicks, pictures); dragged back it docks again; Linux/Xvfb only, **unverified on Windows** |
---
## Editor: panels in their own windows on our own code

What changed (SDL removal, editor stage E4, `docs/sdl_removal_plan.md`; branch `claude2/sdl-editor-viewports`): on our own platform
code and our own renderer (the default since "Testing our own replacements", section 0b), a panel dragged out of the editor becomes
**its own window**, made and drawn by our own code. Before, those panels stayed inside the editor. A panel window drawn by our code
also shows pictures (thumbnails, the Scene and Game views). **Fixed on the old path too:** with `OKAY_THIRD_PARTY=1` (SDL) the panel
windows showed those pictures as empty grey boxes; now they show them. The Project panel's type filter no longer cuts "All types" to
"All typ".

* **Switches.** None: it follows our own platform and renderer. `OKAY_EDITOR_OWN_VIEWPORTS=0` keeps the panels inside the editor
  again; `OKAY_THIRD_PARTY=1` (or `OKAY_EDITOR_PLATFORM=sdl`) gives SDL's panel windows.
* **How you can tell it is on.** The Console's first lines say `editor: platform own (...; main window and events on own; panel
  windows on)`.

Steps (Windows, from the release, once this is in one):

1. Start `Tools\OkayEngine.exe` (from the launcher, or a Command Prompt in `%LOCALAPPDATA%\OkaySpace`). Open a project (for example
   the physics example).
2. Press and hold the mouse on the **Project** tab and drag it outside the editor window (onto the desktop or a second monitor), then
   let go. Expect: the Project panel in its own window, with its folders and file pictures (not empty boxes).
3. Click into that window's search box and type a few letters (for example `crate`): the list filters as you type.
4. Drag the window by its tab or title bar back over the editor and drop it on the tab bar where it was: it docks again and its window
   goes away.
5. Close the editor with the X: it closes normally.
6. Do steps 1 to 5 again after `set OKAY_THIRD_PARTY=1` in the Command Prompt: the same, on SDL, with the pictures too.

**Status.** Run on Linux only, under Xvfb with software OpenGL, as the whole editor (steps 2 to 4, with screenshots, and with the SDL
trap: no SDL call), and the test program `editor_viewports` (own, sdl2 and two monitors). **Never run on Windows: steps 1 to 6 are
unverified there until you run them.** On Windows our window position now means the corner of the inside of the window (not of its
frame), as SDL does.
