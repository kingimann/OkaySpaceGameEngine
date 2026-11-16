---
id: 0
group: switches
---
## Switches that are off by default (own code)

CLAUDE.md rule 17 and `docs/own_stack_rollout.md`: our own code exists beside the third-party library, off by default,
and switches on by an environment variable. **The release does not set any of them.** You can set them yourself before
starting a program, with no rebuild.

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
