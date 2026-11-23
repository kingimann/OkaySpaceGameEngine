---
id: one-input
row: | one-input | One input system: one action table and one rebinding store for the built-in controllers, scripts (`action*` and `ia_*`), both Controls screens and local players | Editor: File > Open `examples\input\input_playground.okayscene`, Play, press 6 (station E); or a scene with GameObject > Player and GameObject > UI > Layout & Overlays > Keybind Field (Input Action = Jump) | Tested headless on Linux (30 input, UI and controller suites); nothing run on Windows, in a real editor window or with a real controller |
---
## one-input. One input system

**What it is.** The engine had three input systems in the feature map (the devices, the typed actions with the `.okayinput`
asset, and the older action table the built-in controllers read). Now there is one: one table of actions and one place
where the player's rebinding is kept. A key rebound anywhere (the game's Controls screen, a script, the Input panel) changes
it for the built-in controllers, `action*` scripts, `ia_*` scripts and the typed Controls screen at once, and it is still
there after a restart. Old scenes, scripts, `.okayinput` files and saved bindings load unchanged.

**Steps.**
1. Open the editor from the Windows release. Click **File > Open** and choose `examples\input\input_playground.okayscene`, then **Play**.
2. Station E ("Rebinding") shows `Jump = Space` and a new line `One input system: the typed side reads the same store, typed Jump: Space`.
3. Press **6** (Jump -> E with a swap). Both lines now say E: `Jump = E` and `typed Jump: E`. Press **7** to put everything back; both say Space again.
4. Second check, a built-in controller and a key-binding field: **File > New Scene**, then **GameObject > Player** and
   **GameObject > UI > Layout & Overlays > Keybind Field**. Select the field and in the Inspector set **Input Action** to
   **Jump** (the list now holds every action of the one table, the asset's Navigate / Submit / Cancel too). Press **Play**,
   click the field and press **J**. The player now jumps with J and no longer with Space. Stop and press Play again: J still
   jumps (the rebinding was saved once, for every reader).
5. Old scenes still play: open any scene under `examples\input\` or a project made before this change and press Play; the controls work as before.

**What you should see.** In step 3 both Jump lines change together; in step 4 the controller follows the key you chose on
the typed Controls screen, and it is kept after Play is pressed again.

**Status.** Tested headless on Linux: `input_one_store` (the same key through every old name, a rebind seen by all and
kept after a restart, the pause menu's Block reaching the typed player), `input_playground` (the new station E line) and
28 other input, UI and controller suites. Unverified: Windows, a real editor window, a real controller. Changed on purpose:
the shipped asset's stick bindings for Move and Look lost a second dead zone and Look's curve (the controllers never had
them), and pushing the "UI" context also turns on the asset's UI map (`docs/engine/input.md`, "One input system").
