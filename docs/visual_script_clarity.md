# Visual Script: nodes that explain themselves

Owner feedback (2026-11-18): "i think the nodes should be more easier. its not self explanatory, i drag in a node and i
have no clue what or how im supposed to use it".

Goal: when someone drops a node into the Visual Script editor they understand at once what it does, what to plug into
it, and what to do next. The graph file format and the way scripts run are not changed.

This page is the Existing / Gap table (what we had on `main` before this pass, read from the code), the 15 nodes a new
user would find most confusing, what this pass added, and what is not done. Earlier audits:
[visual_script_clarity_audit.md](visual_script_clarity_audit.md) (what connects to what),
[visual_scripting_naming.md](visual_scripting_naming.md) (the naming rules).

How this was found: by reading the editor code and the node tables (`editor/VisualScript*.hpp`,
`VisualScriptNodeDocs.inc`, the Add Node list, the recipe cards, the Saved values panel). The desktop editor was not
run (no display in this session), so "what a new user would not understand" is a reading of the words on screen, not a
user test. Say so to anyone who quotes this page.

## Existing / Gap

| Part | Existing before this pass | Gap | What this pass did |
|---|---|---|---|
| One plain sentence on the node | A grey line under the title, from `VsNodeHelp` (about 700 hand-written lines). Nodes without one showed "Needs: <hint>" or nothing. | Engine and script-function nodes (most of the 2271 in the palette) had no sentence. | `VsPlainLine` gives every node one plain sentence (help text, else the first sentence of its doc, else its title), cut with "..." when the node is narrow. Shown under the title. |
| Pin hover tips | `VsPinTooltip`: pin name, type, the doc meaning, a "what can connect" line. | No example value. Wording was type names ("Vector3"), not everyday words. | `VsPinTip`: meaning, what to plug in ("a number"), and "Example: 5". For flow pins: how to wire them. |
| "What is this?" help | Hover tooltip on the title (Does / Needs / Gives) and a Details side panel with the full doc table. | The tooltip needs a hover and a wait; the side panel is closed by default and long. Nothing said what mistakes to avoid. | An always-visible card at the bottom right for the selected node: what it does, what to plug in, a tiny example chain, common mistakes, what to do next. It folds up with Hide and remembers that (`helpcard` in `okay_vs_usage.txt`). |
| Empty required input | A red ring on 4 pins (Branch condition, Look At, Move Towards...), a "Missing" badge, a "Required: x" line that overlapped the title line. | Only a handful of nodes knew what they needed. The badge word "Missing" does not say what to do. Object inputs with nothing in them silently did nothing. | `VsPinNeedsInput`: also any object input other than Target. The pin gets the ring, the box says "needed", the node badge says "Needs input" and the line under the title says "Plug the condition in here." |
| What comes next | "Pick a recipe" cards for an empty graph, a one-line next-step bar for the first steps, a wire dropped on empty space opens the list filtered to nodes that fit. | Nothing after you drop a single node. | "Next steps" chips on the card: up to 4 nodes that usually follow (hand-picked pairs, then starter nodes, then basic nodes), each checked against the typed-pin rule (`PinCompatible`). One click adds the node beside the selected one and wires it. |
| Plain titles | `VisualScriptNames.hpp` and `VisualScriptPlain.hpp` already renamed most nodes. The clarity audit (row 7) proposed more renames. | A few names still make people stop: two "wait" nodes with near-identical names, "Fixed Value", "Flip Flop", "Gate", "Cooldown". | 8 titles renamed (table below). The type written in saved graphs is unchanged, so old graphs load as before; the old title stays a search word. |
| A working example | Recipe cards (12 one-click graphs) and 8 starter templates. | Only for common jobs, not for the node you just dropped. | "Try this node" on the card: adds a small ready-made graph (an event, the node with sensible values typed in, a Say Text step) next to your work. Press Play and the Console shows it ran. Built for every node in the list; the common ones are run in the test. |
| Add Node list | Topics (about 70) in sections, a sentence under each row, a preview pane, Beginner mode with 6 groups, plain-word search with synonyms. | The topic names are by area (Position, Rotation & Scale, Tweens), not by what you want to do. | A new "What do you want to do?" group in the left list, in both Beginner and full mode: Start something, React to input, Move things, Show messages, Play sounds, Make choices, Wait and repeat, Remember things, Do maths, Create and remove things, Score and health, Other things. Typing the heading finds its nodes too. |

## The 15 most confusing nodes (for a new user)

Chosen by reading each node's title, pins and docs and asking: would a 10 year old know what this is for, what goes in,
and what comes out? Order is by how likely a beginner is to meet it.

| # | Node (title on screen) | What a new user would not understand | What helps now |
|---|---|---|---|
| 1 | If Then Else (`Branch`) | Its Condition input needs a yes-or-no value from another node; left empty it just does nothing. | Needs-input badge, plain hint "plug the condition in here", mistake line, next steps lead into Say Text / Play Sound. |
| 2 | Wait Then Continue (`Delay`) vs Wait The First Time Only (`Wait`) | Two nodes both called "Wait ..." with nearly the same name; one waits every time, one only the first time. | Renamed so the difference is in the name. Old names "Wait Seconds" and "Wait Once" still found by search. |
| 3 | Repeat Every Few Seconds (`Timer`) vs On Every Few Seconds (`OnTimer`) | Same words, one is a start-event and one is a step in the middle of a chain. | Timer renamed to "Repeat Every Few Seconds"; the card says whether it starts a flow or sits inside one. |
| 4 | Switch Between Two Paths (`FlipFlop`) | "Flip Flop" is a nickname from electronics. It sends the first signal one way, the next the other way. | Renamed; plain sentence; example chain. |
| 5 | Open Or Close The Way (`Gate`) | Does it block everything? When does it open? | Renamed; the sentence says what it does; mistakes line. |
| 6 | Limit How Often (`Cooldown`) | "Cooldown" is a game word; people expect a countdown on screen. | Renamed; sentence. |
| 7 | Do These In Order (`Sequence`) | People think it waits between steps. It runs them one after another in the same moment. | Sentence and the "Then" pins are explained on hover. |
| 8 | Type In A Value (`Const`) | "Fixed Value" suggests it cannot change. It is just a box to type a number or word into. | Renamed; example value shown. |
| 9 | Blend Between (`Lerp`) | Three inputs (from, to, how far from 0 to 1) and no hint which is which. | Pin tips with examples ("Example: 5" per pin), what-to-plug-in list on the card. |
| 10 | The Target input (on 390 nodes) | "Which object?" It is empty on purpose: empty means this object. People think it is missing. | The tip says "Leave it empty to use this object", the card and mistakes line say the same, and Target is never flagged as needing input. |
| 11 | Get Distance From Zero / Make Length One / Compare Directions (vector maths) | Names describe maths, not a job. | Plain sentences and example values; "Do maths" heading. Not renamed (see "Not done"). |
| 12 | Repeat Counting (`ForLoop`) and Run a Number of Times (`DoN`) | Two repeat nodes, with a counter output nobody explains. | Sentences and pin tips; "Wait and repeat" heading. |
| 13 | Switch Number (`SwitchInt`) | Many outputs, one per number; unclear which fires. | Pin tips; sentence. Not renamed. |
| 14 | Get / Set / Add Saved Value (`GetVar`, `SetVar`, `AddVar`) | The text box takes a NAME; the node does not show the value. Typing a name that was never set reads as 0. | Needs input on the name, a sentence, "Remember things" heading, "Try this node" sets then reads a value. |
| 15 | Script function nodes (`Fn.*`, 1056 of the 2271 palette nodes) | Four unnamed inputs "First Second Third Fourth" and one "Result". | Their line says which script function they run, pin tips say "plug in any value". The pins are still unnamed: this is the biggest remaining gap. |

## Renamed titles

The graph file stores the type, not the title, so every saved graph loads unchanged. The old title is a search word.

| Type | Old title | New title |
|---|---|---|
| `Timer` | Every Seconds | Repeat Every Few Seconds |
| `Wait` | Wait Once | Wait The First Time Only |
| `Delay` | Wait Seconds | Wait Then Continue |
| `Cooldown` | Cooldown | Limit How Often |
| `FlipFlop` | Flip Flop | Switch Between Two Paths |
| `Gate` | Gate | Open Or Close The Way |
| `Sequence` | Do in Order | Do These In Order |
| `Const` | Fixed Value | Type In A Value |

## Code map

| File | What |
|---|---|
| `editor/VisualScriptGuide.hpp` | Pure functions (no ImGui): `VsPlainLine`, `VsPinExample`, `VsPinTip`, `VsPinNeedsInput`, `VsMissingHint`, `VsNextSteps`, `VsHelpCardFor`, `VsTryGraph`, `VsIntentFor`. Written from this page, from the node tables and from `PinCompatible`. |
| `editor/VisualScriptGuideDraw.hpp` | Paints the card, the chips and the Try button. |
| `editor/VisualScriptPlain.hpp` | `kVsPlainRenames` (the 8 renames). |
| `editor/VisualScriptEditor.hpp`, `VisualScriptPalette.hpp` | Wire all of the above into the node, the pin tips and the Add Node list. |
| `tests/test_vs_guide.cpp` | Runs the checks below over every node in the list. |

Everything here is our own code, written from the node tables in this repository. No outside code, text or assets.

## Tests

`vs_guide` (`tests/test_vs_guide.cpp`): for every node in the list, a plain sentence (no word from the banned list in
`VisualScriptGlossary.hpp`), a hover tip with an example for every pin, a complete help card (what it does, what to plug
in, a tiny example, common mistakes, a next step), one of the 12 headings; every next-step chip points at a node that
exists and whose pins fit (checked against `PinCompatible`); the Try graph for every node is built, written, read back and
keeps its node; a few are run in a headless scene; renamed nodes keep their type and their old search word; an old saved
graph loads; the card's open/closed state round-trips.

## Not done

* The desktop editor was not run, on Linux or Windows. The card, chips, badge and list were compile-checked and their
  rules unit-tested, but nothing was drawn and clicked. Check the layout (card size on a small window, the node's
  second line on a narrow node) when you first open it.
* The card is English text only and sits over the canvas corner; it does not move if a node is under it. Hide folds it.
* The script-function (`Fn.*`) nodes still have unnamed pins.
* Next-step chips are hand-picked pairs for about 18 nodes, then a general rule. They are good, not smart.
* The "needs input" rule covers object inputs and the old list. A number input left empty reads as 0 and is not flagged
  (it has a sensible default).
* The vector-maths names (item 11) and Switch Number (item 13) are not renamed yet.
