# Visual script clarity audit

Owner report: "the nodes in visual script need to be more straight forward. it's
very confusing on what node works with what node."

This page is the design input for fixing that. It has the data (every node, every
pin, which output can feed which input), the confusion log from building five small
behaviours in the real editor as a first-time user, the inconsistencies found, a
ranked plan, and what this pass already shipped. Numbers are from the node table as
it is on `main` (`Registry()` plus the engine's `SignatureFor`), counted by a
throwaway program that walks the same tables the editor uses; the checks that guard
the shipped part are in `tests/test_vs_clarity.cpp`.

What already existed before this pass (so it is not proposed again): typed pin
colours, refused wires with a reason and a conversion tip, a graph checker, topic and
level browsing (Basic / Intermediate / Advanced), named-pin dropdowns, starter graphs,
recently used nodes, a node-docs table with a description and a doc for every pin, a
header tooltip with description and requirements, a pin tooltip with the pin's meaning,
and, while a wire is being dragged, a green ring on pins that fit and a dark disc on
pins that do not (with the reason on hover). A wire dropped on empty space opens the
Add Node list limited to nodes that have a compatible pin.

## 1. Inventory

| | Count |
|---|---:|
| Node types in the registry | 2681 |
| Hidden as duplicates or untested | 410 (306 script-function aliases, 104 engine-binding twins) |
| Nodes a person can pick in the palette | 2271 |
| of which hand-written nodes | 1014 |
| of which engine-binding nodes (`Transform.Translate/3` style) | 201 |
| of which script-function nodes (`Fn.*`, "First / Second / Third / Fourth -> Result") | 1056 |
| Events (start a flow) | 60 (only 10 sit in the Events topic; the rest are filed under Input, Physics, Time...) |
| Actions (take a flow in) | 1561 |
| Values (no flow pins) | 650 |
| Basic / Intermediate / Advanced | 297 / 1802 / 172 |
| Data input pins / data output pins | 6069 / 1791 |
| Nodes with a Target pin ("which object", empty means this one) | 390 |
| Nodes with a text box setting (key, name, prefab, seconds...) | 170 |
| Nodes that name the component they need | 164 |
| Nodes where a missing required input is flagged on the node | 14 |

Almost half of the palette (1056 of 2271, 46%) is the script-function family. Every
one of them has the same four unnamed, untyped inputs and one untyped output. 1049 of
those 1056 show the same help sentence ("Calls this text-script builtin on the
current object...") and 5280 of the 6043 generic-named pins in the palette are theirs.

## 2. What connects to what

Pin types: Number, Bool, Text, Vector3, Object, List, Map, and Any. The rule is
`PinCompatible` in `engine/src/VisualScript/VsTypes.cpp`; the editor uses it to refuse
a wire and to filter the Add Node list. Reading across: can an output of the row type
feed an input of the column type?

| from \ to | Number | Bool | Text | Vector3 | Object | List | Map | Any |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| Number  | yes | yes (0 is false) | yes (written out) | no | no | no | no | yes |
| Bool    | yes (false 0, true 1) | yes | yes | no | no | no | no | yes |
| Text    | no | no | yes | no | yes (read as a name) | no | no | yes |
| Vector3 | no | no | yes | yes | no | no | no | yes |
| Object  | no | yes (still exists?) | yes | no | yes | no | no | yes |
| List    | no | no | yes | no | no | yes | no | yes |
| Map     | no | no | yes | no | no | no | yes | yes |
| Any     | yes | yes | yes | yes | yes | yes | yes | yes |

Of the 42 ordered pairs between two different concrete types, 10 are accepted (4
change the meaning of the value: Number to Bool, Bool to Number, Text to Object, Object
to Bool; 6 only write the value out as text) and 32 are refused. Only 14 of the 32 refusals carry a "use this instead" tip; 18 say only
"a X can't go into a Y input": Number to List or Map, Bool to Vector3, List or Map,
Text to Vector3, List or Map, Vector3 to Bool, List or Map, Object to List or Map,
List to Bool or Vector3, Map to Number, Bool or Vector3.

How many of the 6069 input pins in the palette accept each output type:

| An output of type | accepted by (inputs) | share |
|---|---:|---:|
| Number | 5571 | 91.8% |
| Bool | 5571 | 91.8% |
| Object | 5002 | 82.4% |
| Text | 4905 | 80.8% |
| Vector3 | 4550 | 75.0% |
| List | 4509 | 74.3% |
| Map | 4498 | 74.1% |
| Any | 6069 | 100% |

70% of all input pins (4272) are `Any`, almost all of them on script-function nodes,
so for those the answer to "what works with what" is "anything", and a wrong wire is
never refused. On the 1215 hand-written and engine-binding nodes only 48 of 1845
inputs are `Any`, and the picture is much stricter: a Number feeds 73% of inputs, a
Text 37%, an Object 42%, a Vector3 18%, a List 15%, a Map 15%.

Pin type counts, palette-wide. Inputs: Any 4272, Number 983, Object 414, Text 219,
Bool 97, Vector3 59, List 18, Map 7. Outputs: Any 1089, Number 363, Bool 202, Text 60,
Vector3 46, Object 20, List 10, Map 1.

Things the matrix shows that a person cannot guess:

* It is not symmetric. An Object can feed a Bool, a Bool cannot feed an Object. A Text
  can feed an Object (read as a name), an Object can feed a Text. Number and Bool are
  interchangeable, Text and Number are not.
* Any pin that takes Text takes everything. So a Vector3 into a Text pin works, but a
  Vector3 into a Number pin does not, and the colour of the wire changes meaning along
  the way.
* There are 20 Object outputs in the whole palette and 414 Object inputs. Most Object
  inputs are the optional Target pin, which is also an inline text/drop slot, so it is
  rarely clear that it can be wired at all.

## 3. Inconsistencies

### Same title, different node
286 titles are used by more than one registry entry (667 entries). The palette hides
most of the twins (410 hidden), and three titles still appear twice in the palette with
different pins: **Set Velocity** (X, Y, Z, Target vs Right, Up, Forward, Target),
**Format Number** (Value vs Value, Decimals; one is under Text, one under UI), and
**Send Message** (an online message with Data vs an object message with Message,
Target).

### Same job, two names (all visible together)
* Key Held / Key Down / Key Up, and Get Key / Get Key Down / Get Key Up (Basic level
  both: same three jobs, text-key-in-a-pin versus text-key-in-the-box).
* Upper Case / Lower Case and Uppercase Text / Lowercase Text; Text Length and Count
  Letters.
* Min / Max and Select Larger / Select Smaller; Clamp, Clamp Range, Keep at Most, Keep
  at Least.
* Set Position, Teleport, Set Position (Vec3), Move By (Vec3), Move to Position, Move
  Towards.
* Add Var / Add Score / Add Scene Var / Add Global Var; Set Var / Set Scene Var / Set
  Global Var / Set Story Var / Save Value / Set Local.
* Print, Log Warning, Log Error (same pins), Log Once.
* Spawn, Spawn Pickup, Spawn Projectile, Spawn Every Few Seconds, Net Spawn.
* Wait Seconds, Wait Once, Every Seconds, On Every Few Seconds, Every Few Seconds,
  Count Down, Cooldown: seven ways to say "something with seconds".
* Set Color (R, G, B, A) next to Set Text Color and Tween Color (Red, Green, Blue,
  Opacity).
* 151 groups of 470 nodes have an identical pin layout (flow in, flow out, same
  inputs) with different titles, for example Translate / Set Position / Set Scale /
  Rotate (3D) / Set Rotation (3D), which all read "X, Y, Z, Target".

### The same pin name means different things
* **Object** is the thing to act on in Teleport and Set Active, but the thing to act
  *towards* in Look At, Move Towards, Face Object. **Target** (381 nodes) is always
  the object to act on. 22 nodes use Object, with both meanings.
* **Seconds** is a pin on 39 nodes (Open Door, Tween Position, Count Down...) and a
  text box setting on 6 (Every Seconds, Wait Seconds, Wait Once, Cooldown...). Count
  Down has both.
* X, Y, Z on Set Position and Translate, but Right, Up, Forward on Move to Position,
  Set Velocity (the Gl-family twin) and Look at Position.
* R, G, B, A on Set Color, Red, Green, Blue, Opacity on Set Text Color and Tween
  Color; neither says if 0 to 1 or 0 to 255.
* **Result** is the output of 343 hand-written nodes and says nothing; **Value** (146),
  **Out** (45), **A / B** (27 each), **In** (18), **Bool** (19), **t**, **dt**, **Deg**,
  **Pos** also appear. 21 of the 297 Basic nodes use one of the short names
  (A, B, Out, In, Bool, t, dt, Deg, Pos), against the naming rule in
  `docs/visual_scripting_naming.md`.

### Titles that break the project's own naming rule
* 84 titles contain an abbreviation or engine word: Get Var, Set Var, Add Var, Toggle
  Var, Make Vec3, Vec X / Y / Z, Vec3 Add..., Anim Trigger, Anim Set Float, Nav Move To,
  IK Look At, Deg to Rad, My Net Id.
* 34 titles contain symbols or digits: Add (+), Sub (-), Mul (x), Div (/), Mod (%),
  Pow (^), Random 0-1, Clamp 0-1, Concat (text), Log (ln), Repeat (wrap), Rotate (3D),
  Raycast 2D / 3D.
* 72 titles are not in title case, for example Ease in, Pick up, Not and, Get Key up.

### Names that do not say what the node does
Examples a first-time user cannot decode from the title alone:

| Title | What it is |
|---|---|
| Const | a fixed number or text typed in the node |
| Equals vs Compare | Equals checks two values are the same; Compare checks greater/less |
| Sequence | run its outputs one after another (pins named First, Second) |
| Every Seconds | lets the flow through once per interval; the interval is a hidden text box |
| Count Down | waits the seconds you set, then carries on: a second way to say Wait Seconds |
| Show Win | records a win, freezes time, and writes into a text object that must be named `WinText` |
| Gap Below One | one minus the value |
| Is Slightly True / Is Mostly True / Is Very True | thresholds on a number (not a Bool test) |
| Pick Best Score | index of the largest of four numbers |
| Run Stopwatch | a stopwatch that gives seconds |
| Fade Screen | sets the opacity of a UI panel that must be named `Fade` (or wired in); it is not a screen effect |
| Time (output named t), Delta Time (output named dt) | two time readings with names of one letter |

### Docs and preconditions
* 201 engine-binding nodes have no hand-written help line; their description is the
  binding's own one-liner.
* 1049 of 1056 script-function nodes have the same generic help.
* Component requirements are known for 164 nodes and shown in the header tooltip, but
  only 14 nodes flag a missing required input on the node itself (Branch's condition,
  Spawn's prefab, Load Scene's scene, the variable and event names, Look At's object).
  Every other "needs something" is learned by running the game and seeing nothing
  happen: Play Sound with no Audio Source, Set Text on an object with no text, Add
  Force on an object with no Rigidbody, Spawn with no prefab chosen.
* 1716 of the 2271 nodes only work in Play mode; that is in the tooltip text, not in
  the node.

## 4. Walkthrough: five behaviours from scratch

The real editor (software renderer, 1600 by 900, a fresh profile and project, an
empty graph, no documentation read) was driven with a mouse and keyboard as a
first-time user would. The graph was empty and an object called Agent was selected.
Each behaviour was built from the Add Node list. Screenshots were kept during the
audit; they are not part of the repository.

| Behaviour | What a first-time user types | What comes back | The right node, and where it was |
|---|---|---|---|
| (a) When the player presses Space, print a message | "space", then "key", then "print" | "space" finds Remove Spaces, Trim Spaces, Text is Null or White Space. "key" finds 16 rows, the first one a script function with four unnamed inputs. "print" finds Print first. | On Key Pressed (Basic, 12th row of "key", its box already says Space) then Print |
| (b) On start, spawn an object and move it | "spawn", then "move" | Spawn, Spawn Prefab (2D), Spawn Projectile, Spawn Every Few Seconds, Spawn Pickup, Spawn Wave, Spawner Alive, Spawner Start... more than 20 rows | Spawn needs a prefab; the node says "no prefab file is set" in red (good); the box is labelled "Pre None" |
| (c) Every second, change an object's colour | "every second", then "color" | "every second" finds Every Seconds first (good). "color" finds Color first (a script function, filed under Advanced), then Color Blend Between, Set Text Color, Tween Color, then Set Color | Every Seconds then Set Color (Intermediate, fifth row, pins R G B A) |
| (d) When two objects collide, add 1 to a score and show it in a UI text | "collide", then "score", then "get score" | "collide" finds On Trigger Enter first and does not list On Collision Start at all (12 rows, ending in Raycast Hits). "score" finds Score first (a script function). Enter on "get score" inserted that same Score node | On Collision Start, Add Score, Get Score, Set Text (its Text pin takes the score) |
| (e) If health is below 20, turn the screen red | "health", then "red" | "health" is clean: Get Health, Get Health Portion, Is Health Low (takes a portion, not 20)... "red" finds Reduce, Build Redo, Squared Distance, Get Remembered Value: no screen effect | None that a search finds. Set Background Color, Flash and Post Set exist only as script functions; Fade Screen needs a UI panel named Fade |

### Confusion log, top 15

1. **The first row is often a script function.** Searching "key", "color" and "score"
   puts `Key`, `Color` and `Score` first, in the Advanced topic, each with the same
   four boxes First / Second / Third / Fourth and a Result, all grey, all untyped. Enter
   on the first row (the natural thing to do) inserts it. Two such nodes (Score and
   Mouse Position Vertical) look identical on the canvas and do unrelated things.
2. **One job, many names.** "key" lists Key Held, Key Down, Key Is Held, Key Just
   Pressed, Key Pressed, On Key Pressed, Get Key Down for what is one idea (a key was
   pressed). Nothing says which to use; Basic is the only hint and ten of the sixteen are Basic.
3. **The player's word does not find the node.** "space" (the key) finds text
   functions, "collide" does not list On Collision Start, "red" finds Reduce. There is
   no synonym or topic for "when X touches Y" or "screen effect".
4. **Contradictory status on one node.** A new On Key Pressed shows a green "Ready"
   badge, an amber warning dot and a band saying "this event does nothing: nothing is
   w..." cut off by the node width; the toolbar says "1 problem". A Print with an empty
   Value says "No problems" and prints nothing.
5. **"Suggested for this pin" does not narrow.** Dropping a wire from an event's flow
   pin on empty space says "nodes that fit this flow pin" and lists 1751 of 2461
   entries, Branch first, Print far below. Dropping a Number output says "nodes that
   fit a Number pin": 1819 entries, the first rows Branch, Run Once, Count Down (a
   Number converts to a Bool, so every flow node with a Condition fits), not Add or Compare.
6. **Pin colours and shapes are never explained.** The empty-graph card, the toolbar
   and the tooltips do not say what a white triangle, a ring, a filled dot or a grey
   ring mean, or that grey means "anything".
7. **A pin tooltip said nothing about connections, and two tooltips stacked.** Hovering
   an input showed the generic sentence ("wire a value in, drop an object or file, or type
   one") with the real tooltip drawn under it, repeating the type and meaning.
   Neither said what can be wired in. Fixed in this pass.
8. **Settings hide in the node as text boxes.** The key of On Key Pressed, the interval
   of Every Seconds and the prefab of Spawn are boxes, not pins (170 nodes), while
   Open Door, Tween Position and 37 others take Seconds as a pin and Count Down has both.
   Set Text shows two object slots ("Obj ScoreTe..." and "Obj None") with no word on which wins.
9. **Missing setup is silent except in 14 nodes.** Spawn without a prefab is flagged;
   Show Win without a text object named WinText, Fade Screen without a panel named Fade,
   Play Sound without an Audio Source, Add Force without a Rigidbody are not flagged on the node.
10. **Nodes land on top of each other.** A node added from a wire drop is placed under
    the pointer; the next one lands on it. A drag that started on Spawn's Object output
    moved the Print node instead, because Print sat under that point.
11. **Requirement text is vague.** The tooltip for Print says "No single component is
    required beyond this object's Visual Script, unless the description names one";
    Get Health says "No component is required. It only computes a value from its
    inputs" though it reads the target's health.
12. **Descriptions repeat the title or a template.** "On Trigger Enter. Runs when..."
    (the title again); the Print Value pin says "The value being stored, sent or tested".
    201 engine-binding nodes and 1049 script functions share generated text.
13. **Jargon in names.** Const, Concat (text), Add Var, Vec3 Add, Anim Set Float, Nav
    Move To, Get Key up, Set Position (Vec3), pins named A, B, Out, In, t, dt, Result.
14. **Events are everywhere.** 60 event nodes, 10 under Events. On Key Pressed is under
    Input, On Collision Start under Physics, On Every Few Seconds under Time.
15. **Typing with no palette open edits the graph.** The canvas takes single keys
    (C starts a comment) and Ctrl+A then Backspace deletes every node; a "Comment" box
    opened this way did not close on Escape. This is the one confusion that lost work
    in the audit session.

What works well and should be kept: the per-node "Required" band (Spawn), the green
ring on pins that fit while dragging, the refusal reason with a conversion tip, the
palette's right-hand pane with the description and every pin, On Key Pressed defaulting
to Space, and the automatic flow wire when a node is added from a flow pin.

## 5. Ranked plan

Value is how much of the confusion in section 4 it removes for a first-time user.
Effort: S = a day or less, M = a few days, L = a week or more. "Branch" says whether it
touches code that another open branch is changing (key node to Debug Log wires and
exec fan-out, ranked and typo-tolerant search with aliases, the Problems panel, typed
variables and collections, custom events); those items wait for that branch or are
done with it.

| # | Item | Value | Effort | Branch | Status |
|--:|---|---|---|---|---|
| 1 | "Connects to / comes from" on every pin tooltip, generated from `PinCompatible`, with the conversion each accepted wire applies and three real nodes to try | very high | S | none | shipped |
| 2 | "Does / Needs / Gives" lines at the top of every node header tooltip, generated from the node table and docs | very high | S | none | shipped |
| 3 | The 1056 script-function nodes: group them under one "Script functions" topic below everything else, show a plain "(script function)" tag, and replace First to Fourth with the real argument names and types from the script builtin table (that table does not carry them today; it needs one row per builtin: name, argument names, types, result type) | very high | L | search (palette lists) | plan |
| 4 | A pin legend (colour, shape, what a triangle is, what a ring is) behind a "?" in the canvas toolbar, plus a one-time "How nodes connect" overlay on the first empty graph: three sentences and an arrow from an event to an action | high | S | Problems panel (toolbar) | plan |
| 5 | While dragging a wire, write the verdict beside the cursor ("Number into Bool: 0 is false") and on each candidate pin; today the ring and the dark disc show it, the words show only on hover of a refused pin | high | M | key to Debug Log wire refusal | plan |
| 6 | Wire dropped on empty space: put exact-type nodes of the Basic level first, then conversions, and say "nodes that accept a Number" in the list header. The compatibility filter exists (`VsCompatible`); the ranking and header are new | high | S | search (ranking) | plan |
| 7 | Rename the worst titles through the alias table so old graphs and old searches keep working: Const to Value, Concat (text) to Join Text, Add (+) to Add, Sub (-) to Subtract, Mul (x) to Multiply, Div (/) to Divide, Mod (%) to Remainder, Pow (^) to Power, Random 0-1 to Random Fraction, Get Var / Set Var / Add Var / Toggle Var to Get Variable ..., Vec X / Y / Z to Get Vector X ..., Every Seconds to Repeat Every, Count Down to Wait Then Continue, Gap Below One to One Minus, Is Slightly / Mostly / Very True to Is Low / Is Middle / Is High | high | M | search (aliases) | plan, needs the alias table from the search branch |
| 8 | Fix pin names: Result and Value become what the node gives (Sum, Product, Is Close...), Out, In, A, B, Bool, t, dt, Deg, Pos in the 59 hand-written nodes that still carry them; Object versus Target made one rule (Target = the thing acted on, Toward = the thing aimed at); R/G/B/A to Red/Green/Blue/Opacity; Seconds as a pin everywhere | high | M | none, but every pin name is saved in files: needs the same alias table | plan |
| 9 | Say what is missing on the node itself. Extend the 14 "required" rows (Branch condition, prefab, scene, names) to every node whose component or named object is required, with the existing "Add Rigidbody" style button: Play Sound, Set Text, Add Force, Show Win (a `WinText` object), Fade Screen (a panel named `Fade`) | very high | M | Problems panel | plan |
| 10 | Collapse near-duplicates: Key Held / Get Key, Key Down / Get Key Down, Upper Case / Uppercase Text, Min / Select Smaller, Set Position / Teleport / Set Position (Vec3), Set Velocity (two), Format Number (two), Send Message (two), Count Down / Wait Seconds. Keep the old types loading; show one row | medium | M | search (dedupe rule) | plan |
| 11 | Recipes: right-click an event, "Add what happens next": When key pressed, do... (Print, Move, Spawn); When two objects collide... (Add Score, Set Text); Every N seconds, do...; If a value is low, do... Each inserts three or four already-wired nodes with the required boxes filled in or flagged | high | M | events/delegates (event nodes) | plan |
| 12 | Events in one place: all 60 event nodes listed under Events (each also stays in its own topic), starting with the six everyday ones | medium | S | search (palette topics) | plan |
| 13 | A defaults pass: a node dropped with nothing wired either does a visible thing (Print writes "Hello"; Set Color picks a colour; Spawn creates the default cube) or says plainly why not, on the node | medium | M | none | plan |
| 14 | Plain-English issue text for every checker message (what is wrong, which node, what to do), reviewed once against the 40 most common ones | medium | S | Problems panel | plan |
| 15 | "Play this node": run the selected node and its inputs once against the selected object and show the value on its pins (the live value bubbles and the step debugger exist; this is a one-click wrapper) | medium | L | none | plan |

Order of work after this pass: 3, 9, 4, 6 first (the first-time user's biggest
walls), then 7 and 8 together because they share the alias table, then 5, 11, 10, 12,
13, 14, 15.

Not proposed: reducing the node count. The 2271 palette nodes are mostly the script
engine's own functions; the problem is presentation, not quantity.

## 6. Shipped in this pass

Two small items from the plan, both generated from the tables the editor already
trusts, in `editor/VisualScriptClarity.hpp`:

* **Pin tooltips say what the pin connects to or comes from** (plan item 1). For a
  value pin the text lists the types it can be wired to (or from), computed from
  `PinCompatible`, then what the conversion does when the types differ ("Number going
  into Bool: 0 is false, anything else is true"; the six types that go into Text are one
  line), then up to three everyday nodes to try (same type first, Basic first, never a
  script function or a Target pin), then the types that are refused with the first
  "use this instead" tip. A flow pin says it connects to a triangle and never to a value
  pin. An Any pin says its type is not checked. A required input that is still empty
  says so in red. The old, second tooltip that was drawn over the real one is gone.
* **Node header tooltips open with Does / Needs / Gives** (plan item 2): what the node
  does (the first real sentence of its description, never the title repeated), what it
  needs (a flow wire, each input with its type, "optional: empty means this object" for
  Target, required settings, the component it needs) and what it gives (each output with
  its type, or "the flow continues", or "the flow it starts"). It is generated for all
  2681 registry nodes, so a node added later has one without further work.

`tests/test_vs_clarity.cpp` checks that every one of the 2681 nodes has a one-liner
with all three parts and no placeholder text; that 50 everyday nodes (events, flow,
variables, math, input, movement, objects, UI) read as real sentences and carry their
real pins, settings and components; that the accepted and refused type pairs in the
tooltip are exactly `PinCompatible`'s (49 pairs, 17 accepted, 32 refused, both
directions); the conversion note exists exactly when a different-type pair is accepted;
the text for Number, Vector3, Bool, Object, Text, Any and flow pins; and that every
suggested partner node is a real pin that `PinCompatible` accepts, once per type and
direction.

Left for later, with the reason in the plan table: everything that changes the
palette lists, titles or pin names (search, aliases and Problems branches), the
legend and first-run overlay (toolbar code those branches also touch), and the
script-function family (needs a new builtin signature table).

## 7. Known gaps

* The numbers are from the node table as built from `main` on the audit date; a node
  added later changes them. They were counted by a throwaway program, not by a test, so
  the page does not stay in sync by itself. The checks that do stay in sync are the ones
  in `tests/test_vs_clarity.cpp`.
* The walkthrough used the editor's own search and a mouse, on a slow software
  renderer; the findings about ranking and wording are of the build as it was, and the
  search branch is expected to change items 1, 3 and 12 of the log.
* "Needs" lists what the node table and docs know. For the 1056 script functions that is
  only "First to Fourth, optional", because the builtin table carries no argument names;
  that is plan item 3.
* The five behaviours were built as far as the Add Node list and the first wires; they
  were not run in Play mode, so what happens at runtime when a required object is
  missing is taken from the node documentation, not observed.
