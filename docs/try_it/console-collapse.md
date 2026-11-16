---
id: console-collapse
---
## Console: Collapse button (branch `claude/console-collapse`, not merged or released yet)

Owner request 2026-11-15: "for console allow user to collapse the same log."

1. Open the editor (`Tools\OkayEngine.exe` or Open from the launcher) and show **View > Console**.
2. Make a log repeat. Easiest: press **Play** on a scene whose script logs every frame (for example a script that calls
   `print` in `update`), or use any scene that logs the same warning many times.
3. In the Console toolbar, to the right of the three count pills (Messages, Warnings, Errors), click **Collapse**.
   Hover it: the tooltip says "Merge identical messages into one row with a count".
4. You should see one row per distinct message, with `x37` (the count) in front of the text and the time of the newest
   copy. The count keeps growing while Play runs. Click the row to see the details (count, first and newest time).
5. Click **Collapse** again: every line is back. Close and reopen the editor: the button keeps its last state
   (stored as `consolecollapse` in `okay_settings.txt`). It is off the first time.
6. Search, the category dropdown and the pills still work while collapsed, and Clear empties everything.

Rule: two lines merge only if level, category (the leading `[tag]`) and text are exactly the same, even when they are not
next to each other. `hp 10` and `hp 9` stay two rows. The pill counts still count every line, not rows.

**Status.** Logic tested headless (`test_debug_console`, 136 checks, run on Linux). `editor/ConsolePanel.cpp` and
`editor/main.cpp` were NOT compiled (no SDL in this environment); the button and label are unverified until you look.
