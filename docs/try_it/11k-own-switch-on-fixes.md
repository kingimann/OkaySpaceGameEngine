---
id: 11k
group: appendix
row: | 11k | Editor fixes on our own code: narrow tab groups show icons instead of cut names, the Console and Game toolbars wrap, `)` steps over the auto-inserted `)`, a panel window with a tooltip over it repaints after a resize | `Tools\OkayEngine.exe` at 1280x800 (section 11k) | New; checked under Xvfb on Linux, **unverified on Windows** |
---
## 11k. Editor fixes on our own code (branch `claude2/own-switch-on-fixes`)

Things that were wrong in the editor with our own code switched on (PR #988). Where the old code (`OKAY_THIRD_PARTY=1`) had the
same problem it is fixed there too.

**1. Tab names are never cut short.** When a group of docked panels is too narrow for all its tab names (the bottom group in the
default layout at 1280x800 showed "Script E..." and "Problen"), the widest tabs other than the selected one show only their icon.
Hover an icon tab for its name, or use the list button (a small down arrow) at the end of the tab strip. When the group is wide
enough again the names come back. Both paths.

**2. The Console's and the Game view's toolbars wrap.** A narrow Console puts "Auto-scroll" and the buttons after it on a second
row instead of cutting it at the edge ("Auto-scro"); "Save to console_log.txt" becomes "Save log" when it would not fit (its
tooltip names the file). The old Console (`OKAY_THIRD_PARTY=1`) puts its category, count pills, Collapse and "..." on a second row
instead of squeezing the search field and cutting the "..." button. The Game view's toolbar flows onto a second row ("Capture
mouse", "Stats", "Physics" no longer end in a lone "C"), and "live" / "press Play to run" sits level with the check boxes.

**3. `)` steps over the `)` the editor typed for you.** In the Script Editor with our own code editor (the default on this
branch), typing `(` adds the `)`; typing `)` right before it now moves past it instead of typing a second one, also right after
Escape closed the suggestion list. Before, `zz(1` then Escape then `)` could give `zz(1))`.

**4. A problem's message is written once.** On the old code editor (`OKAY_THIRD_PARTY=1`) a line with a warning showed the message
twice, one copy over the other. Now once, with "(Alt+Enter to fix)" after it when there is a quick fix.

**5. The Hierarchy's scene line fits.** In a narrow Hierarchy an unsaved scene showed "Own WAV decoder test  ·  uns" with the
object count drawn over the end. When the whole line does not fit it now reads "Own WAV decoder test *". Both paths.

**6. Nothing shows through an open menu.** The Inspector's tab title and "Lock Inspector" read faintly through the Layout dropdown:
the menus were 98.5% opaque (Dear ImGui's own themes 94%). Every menu, combo list, popup and tooltip is now fully opaque in every
theme of the editor and the launcher (`okaytheme::OpaquePopups`). The Console's Category list is drawn in a raised colour, so it no
longer reads as "C All" over "Collapse duplicates". Test: `okayui_host_layering` (a combo opened from the menu bar covers the
Inspector while open, the palettes' popup colour is opaque).

**7. A panel window repaints right away after a resize.** A panel dragged out of the editor into its own window stayed black after
it was resized from outside while a tooltip was showing, until the mouse moved and the tooltip went away. Our renderer only ever
showed the last panel window it drew; now each panel window is shown right after it is drawn.

Steps (Windows, from the release, once this is in one):

1. Start `Tools\OkayEngine.exe` and open a project (for example `examples\own_wav`). Make the editor window about 1280x800
   (or smaller) and pick **Default** in the layout dropdown at the top right.
2. Look at the bottom group (Console, Project, Script Editor, Problems): the selected tab has its full name, one or two others
   are icons only, and no name ends in "..." or is cut off. Hover the icon tab with `</>`: "Script Editor" shows. Click it: it
   opens with its name, another tab turns into an icon. Make the editor window wide: all four names come back.
3. Click the **Console** tab: "Auto-scroll", "Clear on Play", "Pause on error", "Copy all" and "Save log" are on the second row of
   check boxes, all whole.
4. Click the **Game** tab: two toolbar rows, nothing cut at the right edge. Press **Play**: "live" in green on the first row.
   Stop Play.
5. Make a new script in the **Script Editor** (New Script, a name, Create). On an empty line type `zz(` (a `)` appears), then `1`,
   press **Escape**, then type `)`. Expect `zz(1)` with the caret after the `)`; the status bar says `Col 6`. The warning
   "Unknown function 'zz' ..." is written once after the line. In the **Scripting** layout (layout dropdown) the Hierarchy is
   narrow: after any change to the scene its top line ends in " *", not in a cut "unsa".
6. Drag the **Project** tab out of the editor window. Hover one of its toolbar buttons until its tooltip shows, keep the mouse
   there and resize the Project window by its corner. Expect the Project panel drawn at the new size, not a black window.
7. Open the Layout dropdown (top right), the File, Edit and View menus, the Console's Category list and a right-click menu
   in the Hierarchy: nothing from the panels under them shows through or over them.
8. Do steps 1 to 5 and 7 again after `set OKAY_THIRD_PARTY=1` in a Command Prompt (the old SDL and Dear ImGui code): the same, except
   that the old Console has its options behind its "..." button and wraps as described in point 2.

Verified: headless tests (`editor_tabs`, `okayui_screens`, `okayui_codeedit_imgui`, `editor_viewports_own` / `_own_2mon` /
`_sdl2`) and screenshots of the editor under Xvfb on Linux at 1280x800 and 1920x1200, both paths. Not verified: Windows.
