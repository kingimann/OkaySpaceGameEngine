---
id: oscript
row: | oscript | One table of engine functions for OkayScript and C#: `add_torque` in OkayScript, the same save, key, action, force, audio-group and time functions in both | A script on an object, in `Tools\OkayEngine.exe` | New on `claude2/one-script-api`; tested headless (`script_api_parity`), the editor steps are unverified |
---
## oscript. One table of engine functions for OkayScript and C#

What changed: 20 engine functions are now written once and used by both script languages (and by the visual-script
Engine API nodes). Before, each language had its own copy and they disagreed. The guide is
[script_api_registry.md](scripting/script_api_registry.md).

Where: the editor, `Tools\OkayEngine.exe`. Open any 3D scene.

1. **GameObject > 3D Object > Cube.** In the Inspector, **Add Component > Rigidbody** and set Gravity Scale to 0.
2. **Add Component > Script**, choose OkayScript, and type:
   `function start() { add_torque(0, 50, 0); }`
   Press **Play**. You should see the cube start to spin around its up axis. (Before this change OkayScript had no
   `add_torque`; the script would stop with "unknown function".)
3. Change the script to `function start() { add_torque(0, 50, 0); add_force(0, 1, 0); print(has_save("nothing")); }`
   and remove the Rigidbody first. Press **Play**. The Console shows two warnings, `add_torque: 'Cube' has no Rigidbody
   (add one in the Inspector)` and the same for `add_force`, and then `0`: the script kept running.
4. A C# script on the same cube can now read one controller's action: `Input.GetButton("Jump", 0)` (the player number is new
   in C#; OkayScript's `action("Jump", 0)` already had it).

Changed on purpose (say if one of these breaks a game of yours): in C#, `Input.GetKey("up")` is now the up arrow (it was W),
and C# key reads are off while a text field has the keyboard, like OkayScript's always were. `save_exists` is `has_save`
(it always was in practice; a second, unused definition was removed).

Verified here: the headless test `script_api_parity` (every shared function called from both languages, same answer and
same effect). Not verified: the editor steps above (no editor window is opened in these sessions).
