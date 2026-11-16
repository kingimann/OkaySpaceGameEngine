---
id: script-debugger
group: main
row: | script-debugger | Debug an OkayScript text script: breakpoints, stepping, watch | File > Open `examples\script_debugger\script_debugger.okayscene`, then View > Editors & Tools > Script Debugger | Unverified in a real window (engine tested headless) |
---
## script-debugger. Debug a text script (OkayScript)

What it is: a debugger for scripts written in OkayScript, with breakpoints on lines, step over / into / out, a call stack,
variables and watch expressions. It only works while the game is playing.

Steps (Windows release):
1. Start the editor. File > Open, pick `examples\script_debugger\script_debugger.okayscene` (in the release folder).
2. Click the Scorekeeper object, then in its Script component click Edit (or open `examples\script_debugger\debug_me.os`
   from the Project panel). The script opens in the Script Editor.
3. Click the line number of `return result;` (line 7). A red dot appears. The Script Debugger window opens.
4. Press Play. After one second the game freezes, the line turns yellow, and the Script Debugger shows
   "Paused at line 7", the call stack `Square()`, `AddScore()`, `update()`, and the variables `n` and `result`.
5. In Watch type `result * 2` and press Add. You should see `result * 2 = ...` update at the next stop.
   Type `total = 5`: it is refused (watches only read).
6. Press Step Out (Shift+F11): it stops in `AddScore()`. Press Continue (F5): the game runs on and stops again one second later.
7. Click the red dot to clear it, press Continue, and the game runs freely. The Console prints `score is now ...`.
8. Tick "Pause on error" to also stop on script errors.

What was verified: the engine part (`script_debugger` test: breakpoints, step into/over/out, call stack, locals, watch,
pause on error, a failing handler, the scene above). What is unverified: the editor window and gutter clicks (the editor
was not run in a real window). Not covered: the C#-style typed interpreter, and code inside coroutines.
