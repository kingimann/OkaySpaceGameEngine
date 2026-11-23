---
id: 0
group: switches
---
## Switches for our own code (environment variables)

CLAUDE.md rule 17 and `docs/status/own_stack_rollout.md`: our own code exists beside the third-party library and switches by an
environment variable. **Since 2026-11-23 the parts listed in launcher Settings > Experimental are ON by default** (the launcher and
editor set their variables at start; `docs/try_it/own-switch.md`). The variables below are for the rest, or to force a part either
way: a variable you set yourself always wins. You can set them before starting a program, with no rebuild.

**How to set one (Windows).** Open a Command Prompt, then:

```
cd %LOCALAPPDATA%\OkaySpace
set OKAY_OWN_STACK=1
Tools\OkayEngine.exe
```

(PowerShell: `$env:OKAY_OWN_STACK = "1"`.) The variable lives only in that window. Start the editor straight from that
window rather than by double-click so it inherits the variable; starting it from `OkaySpace.exe` run in the same window
should also work, but the launcher passing it on is unverified.

**How to see which parts are ours.** Every program (launcher, editor, player) prints one line at start. From a Command
Prompt you may see it on the screen; in the editor the sure way is **Help > About OkaySpace**, which shows the same
text. The exact format (from `shared/OwnStack.hpp`):

```
[okay] stack: platform=sdl2 http=winhttp readers=ufbx+assimp crypto=libsodium ui=imgui
```

* `readers=own(off,dae,...)+assimp` lists the formats read by our code now. `ufbx` shows while FBX is still ufbx; it
  becomes `own(fbx,...)` with `OKAY_OWN_FBX=1`.
* `http=` is `winhttp` on Windows. `crypto=` must say `libsodium` in every release.
* `OKAY_PRINT_STACK_AND_EXIT=1` prints the line to standard output and quits at once. From a Command Prompt:
  `set OKAY_PRINT_STACK_AND_EXIT=1` then `Tools\OkayEngine.exe > stack.txt` then `type stack.txt`. Unverified on
  Windows: a GUI program may not write to the window, which is why the About popup is the sure way.

Tested on main (Linux): the line format and that each variable changes it, by the `own_stack` test.
