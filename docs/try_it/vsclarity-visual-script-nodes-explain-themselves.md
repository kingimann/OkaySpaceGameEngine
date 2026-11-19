---
id: vsclarity
row: | vsclarity | Visual Script nodes explain themselves (plain line, What is this? card, Needs input, Next steps, Try this node, What do you want to do? list) | `Tools\OkayEngine.exe`: View > Editors & Tools > Visual Script, drop any node | Compiled and unit-tested (`vs_guide`); never drawn in a real window, Windows unverified |
---
## vsclarity. Visual Script nodes explain themselves

What changed: when you drop a node you now see what it does, what to plug in, and what to do next. Nothing about saved
graphs or how scripts run changed. Notes and the full gap table: `docs/visual_script_clarity.md`.

Try it on Windows (this is all in the normal editor, nothing to switch on):
1. Start `Tools\OkayEngine.exe`, open a project, select an object and open View > Editors & Tools > Visual Script.
2. Press "Or add a block" (or right-click the canvas, or tap Space) and drop **If Then Else**.
   * Under its title you should see one plain sentence, not a blank line.
   * The node shows an amber **Needs input** badge and the line "Plug the condition in here." The Condition box says "needed".
   * Hover the Condition circle: the tip says what goes in and gives "Example: true".
3. Look at the bottom-right corner of the canvas: the **What is this?** card for the selected node. It lists what it does,
   what to plug in, a tiny example chain, common mistakes and what to do next. Press **Hide** to fold it; close and reopen
   the editor and it stays folded (`helpcard 0` in `okay_vs_usage.txt`). Press **Show** to open it again.
4. In the card, under "Next steps", click a chip (for example **Say Text**). The node is added next to the selected one and
   wired in. Chips only suggest nodes that fit.
5. Drop **Add Numbers** (a value node). Its chips add a node that takes a number, with the wire already drawn from the result.
6. Press **Try this node** at the bottom of the card. A small ready-made graph (On Start, the node with sensible values, a
   Say Text step) appears. Press Play: the Console shows the message, and for a value node it shows the result (Add Numbers: 10).
7. Open the Add Node list (right-click the canvas). In the left list find **What do you want to do?** and click **Move things**,
   **Show messages** and so on. Each row has its plain sentence under the name. Type `move things` in the search: the movement
   nodes come up.
8. Renamed nodes: search `Wait Seconds`, `Flip Flop` or `Every Seconds`. The nodes now called **Wait Then Continue**, **Switch
   Between Two Paths** and **Repeat Every Few Seconds** are found. An old saved graph that used them loads unchanged.

What was and was not verified: the rules behind every item were unit-tested over all nodes in the list on Linux (`vs_guide`,
and the existing `vs_clarity`, `vs_node_docs`, `vs_names`, `vs_start` suites). The drawing was compile-checked only: it was never
shown in a real window, on Linux or on Windows, so card size on a small window and the second line on a narrow node are unchecked.
