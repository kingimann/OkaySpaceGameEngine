# Visual scripting nodes

What every node in the Visual Script palette does, which pins it has, and what it needs.
Generated from the same table the editor shows in the node search and on node tooltips.
Hand-written nodes: `editor/VisualScriptNodeDocs.inc`. Engine API nodes: the binding table in `VisualScript/VsApi`.

## Nodes

### On Start

`OnStart`

Runs once when the object starts (the scene loads or the object is spawned).

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Start finishes, the flow continues from here to the next step. | — |

**Example:** Wire On Start into Print, and type a sentence into Print, to log it when the scene loads.

**Related:** On Enable, On Update, Print

### On Update

`OnUpdate`

Runs every frame. Put movement and checks that must happen all the time here.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Update finishes, the flow continues from here to the next step. | — |

**Example:** Wire On Update into Translate, and Axis X times Delta Time into Translate's X, to slide left and right.

**Related:** On Late Update, On Fixed Update, Delta Time

### Branch

`Branch`

Goes out True or False depending on the condition wired into Cond.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Branch runs when a wire reaches this pin. | — |
| True | out | Flow | The flow continues out of True. Wire this to the step that should run for that case. | — |
| False | out | Flow | The flow continues out of False. Wire this to the step that should run for that case. | — |
| Cond | in | Bool | The condition. True and false take different paths. It is a Bool. Unwired, this is false. | false |

**Example:** Wire a Key Held into Cond. Wire the True pin into the step that should run while the key is down.

**Related:** Compare, Check, Gate, Select

### Sequence

`Sequence`

Runs output 0, then output 1, in that order, every time it runs.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Sequence runs when a wire reaches this pin. | — |
| 0 | out | Flow | The flow continues out of 0. Wire this to the step that should run for that case. | — |
| 1 | out | Flow | The flow continues out of 1. Wire this to the step that should run for that case. | — |

**Example:** Put Sequence on the flow after an event such as On Start. The chain runs when that event fires.

### Once

`Once`

Lets the flow through the first time only; after that it stops here.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Once runs when a wire reaches this pin. | — |
| flow | out | Flow | After Once finishes, the flow continues from here to the next step. | — |

**Example:** Put Once on the flow after an event such as On Start. The chain runs when that event fires.

### Every (s)

`Timer`

Lets the flow through at most once every N seconds. Put it under On Update for repeating actions.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

**Setting:** seconds. The value typed into the node. Default: 1.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Every (s) runs when a wire reaches this pin. | — |
| flow | out | Flow | After Every (s) finishes, the flow continues from here to the next step. | — |

**Example:** Put Every (s) on the flow after an event such as On Start. The chain runs when that event fires.

### Get Var

`GetVar`

Reads a variable on this object by name.

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** name. The value typed into the node. Default: x.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire Get Var's Value output into another node's input. It has no flow of its own.

**Related:** Set Var, Has Var, On Var Changed

### Set Var

`SetVar`

Stores Value in a variable on this object (creates it if it doesn't exist).

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** name. The value typed into the node. Default: x.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Var runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Var finishes, the flow continues from here to the next step. | — |
| Value | in | Any | The value being stored, sent or tested. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put Set Var after an event such as On Start or On Update, and wire a value into Value.

**Related:** Get Var, Add Var, Toggle Var

### Add Var

`AddVar`

Adds Amount to a number variable, e.g. +1 to score or -10 to health.

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Add Var runs when a wire reaches this pin. | — |
| flow | out | Flow | After Add Var finishes, the flow continues from here to the next step. | — |
| Amount | in | Number | How much to add. Negative numbers subtract. It is a Number. Unwired, this is 0. | 0 |

**Example:** Put Add Var after an event such as On Start or On Update, and wire a value into Amount.

### Const

`Const`

A fixed value you type in: a number, text, or true/false.

**Requires:** No component is required. It only computes a value from its inputs.

**Setting:** value. The value typed into the node. Default: 0.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire Const's Value output into another node's input. It has no flow of its own.

### Add (+)

`Add`

Add (+) — A + B.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Number | Opacity, from 0 (invisible) to 1 (solid). It is a Number. Unwired, this is 0. | 0 |
| B | in | Number | Blue, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Add (+)'s Out output into another node's input. It has no flow of its own.

**Related:** Sub, Mul, Add Var

### Sub (-)

`Sub`

Sub (-) — A - B.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Number | Opacity, from 0 (invisible) to 1 (solid). It is a Number. Unwired, this is 0. | 0 |
| B | in | Number | Blue, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Sub (-)'s Out output into another node's input. It has no flow of its own.

### Mul (x)

`Mul`

Mul (x) — A x B.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Number | Opacity, from 0 (invisible) to 1 (solid). It is a Number. Unwired, this is 0. | 0 |
| B | in | Number | Blue, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Mul (x)'s Out output into another node's input. It has no flow of its own.

### Div (/)

`Div`

Div (/) — A / B.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Number | Opacity, from 0 (invisible) to 1 (solid). It is a Number. Unwired, this is 0. | 0 |
| B | in | Number | Blue, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Div (/)'s Out output into another node's input. It has no flow of its own.

### Min

`Min`

Min — The smaller of A and B.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Number | Opacity, from 0 (invisible) to 1 (solid). It is a Number. Unwired, this is 0. | 0 |
| B | in | Number | Blue, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Min's Out output into another node's input. It has no flow of its own.

### Max

`Max`

Max — The larger of A and B.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Number | Opacity, from 0 (invisible) to 1 (solid). It is a Number. Unwired, this is 0. | 0 |
| B | in | Number | Blue, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Max's Out output into another node's input. It has no flow of its own.

### Compare

`Compare`

Compares A and B with the chosen test (<, >, =, ...) and gives true or false.

**Requires:** No component is required. It only computes a value from its inputs.

**Setting:** The comparison: <, >, <=, >=, == or !=.. The comparison: <, >, <=, >=, == or !=. Default: >.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Number | Opacity, from 0 (invisible) to 1 (solid). It is a Number. Unwired, this is 0. | 0 |
| B | in | Number | Blue, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Compare's Bool output into another node's input. It has no flow of its own.

### And

`And`

True only when both A and B are true.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Bool | Opacity, from 0 (invisible) to 1 (solid). It is a Bool. Unwired, this is false. | false |
| B | in | Bool | Blue, from 0 to 1. It is a Bool. Unwired, this is false. | false |
| Out | out | Bool | The result. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire And's Out output into another node's input. It has no flow of its own.

### Or

`Or`

True when A or B (or both) is true.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Bool | Opacity, from 0 (invisible) to 1 (solid). It is a Bool. Unwired, this is false. | false |
| B | in | Bool | Blue, from 0 to 1. It is a Bool. Unwired, this is false. | false |
| Out | out | Bool | The result. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Or's Out output into another node's input. It has no flow of its own.

### Not

`Not`

Turns true into false and false into true.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Bool | The value to transform. It is a Bool. Unwired, this is false. | false |
| Out | out | Bool | The result. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Not's Out output into another node's input. It has no flow of its own.

### Key Held

`Key`

True while the chosen key is held down.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

**Setting:** key. The value typed into the node. Default: space.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Down | out | Bool | True while the button is held. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Key Held's Down output into another node's input. It has no flow of its own.

### Key Down

`KeyDown`

True only on the frame the chosen key is pressed.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

**Setting:** key. The value typed into the node. Default: space.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Pressed | out | Bool | True only on the frame it is pressed. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Key Down's Pressed output into another node's input. It has no flow of its own.

### Axis X

`AxisX`

Left / right input from -1 to 1 (A/D, arrow keys or a gamepad stick).

**Requires:** No component is required. It reads the player or the clock while the game is playing.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| X | out | Number | The X value. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Axis X's X output into another node's input. It has no flow of its own.

### Axis Y

`AxisY`

Up / down input from -1 to 1 (W/S, arrow keys or a gamepad stick).

**Requires:** No component is required. It reads the player or the clock while the game is playing.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Y | out | Number | The Y value. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Axis Y's Y output into another node's input. It has no flow of its own.

### Time

`Time`

Seconds since the game started.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| t | out | Number | Seconds since the game started. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Time's t output into another node's input. It has no flow of its own.

### Delta Time

`DeltaTime`

Seconds since the last frame. Multiply speeds by it so movement is the same on every computer.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| dt | out | Number | Seconds since the last frame. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Delta Time's dt output into another node's input. It has no flow of its own.

### Get X

`GetX`

This object's X position.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| X | out | Number | The X value. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Get X's X output into another node's input. It has no flow of its own.

### Get Y

`GetY`

This object's Y position.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Y | out | Number | The Y value. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Get Y's Y output into another node's input. It has no flow of its own.

### Translate

`Translate`

Moves this object by X, Y, Z from where it is now.

**Requires:** No extra component is required. It moves this object's Transform (or Target). It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Translate runs when a wire reaches this pin. | — |
| flow | out | Flow | After Translate finishes, the flow continues from here to the next step. | — |
| X | in | Number | The X part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Y | in | Number | The Y part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Z | in | Number | The Z part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Translate after an event such as On Start or On Update, and wire a value into X.

**Related:** Move By, Set Position, Axis X

### Set Position

`SetPosition`

Puts this object at X, Y, Z.

**Requires:** No extra component is required. It moves this object's Transform (or Target). It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Position runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Position finishes, the flow continues from here to the next step. | — |
| X | in | Number | The X part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Y | in | Number | The Y part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Z | in | Number | The Z part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Set Position after an event such as On Start or On Update, and wire a value into X.

### Rotate

`Rotate`

Turns this object by Deg degrees (2D).

**Requires:** No extra component is required. It moves this object's Transform (or Target). It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Rotate runs when a wire reaches this pin. | — |
| flow | out | Flow | After Rotate finishes, the flow continues from here to the next step. | — |
| Deg | in | Number | An angle in degrees. It is a Number. Unwired, this is 0. | 0 |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Rotate after an event such as On Start or On Update, and wire a value into Deg.

### Print

`Print`

Writes a message to the Console. Type your own Text, wire in a Value (a key being held, a number, a position...) or both: with both it prints `Text: Value`.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Print runs when a wire reaches this pin. | — |
| flow | out | Flow | After Print finishes, the flow continues from here to the next step. | — |
| Text | in | Text | Your message. Type it into the box, or wire a text in. | empty text |
| Value | in | Any | Optional. Anything: a Bool prints `true` or `false`, a number its digits, a position its x, y, z. With Text typed too, the line is `Text: Value`. | nothing |

**Example:** Put Print after an event such as On Update, type `Space held` into Text and wire Key Held into Value: the Console shows `Space held: true`.

**Related:** Log Warning, Log Error, To Text

### Play Sound

`PlaySound`

Plays this object's Audio Source.

**Requires:** Needs an Audio Source on this object (or on Target). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Play Sound runs when a wire reaches this pin. | — |
| flow | out | Flow | After Play Sound finishes, the flow continues from here to the next step. | — |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Play Sound on the flow after an event such as On Start. The chain runs when that event fires.

### Spawn

`Spawn`

Creates a copy of a prefab at X, Y, Z (or at this object when they're empty).

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** prefab. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Spawn runs when a wire reaches this pin. | — |
| flow | out | Flow | After Spawn finishes, the flow continues from here to the next step. | — |
| X | in | Number | The X part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Y | in | Number | The Y part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Z | in | Number | The Z part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Object | out | Object | The object that was found, or nothing. It is an Object. Wire it into an input that expects an Object. | — |

**Example:** Put Spawn after an event such as On Start or On Update, and wire a value into X.

**Related:** Destroy, Find Object

### Destroy

`Destroy`

Removes this object from the scene.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Destroy runs when a wire reaches this pin. | — |
| flow | out | Flow | After Destroy finishes, the flow continues from here to the next step. | — |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Destroy after an event such as On Start or On Update, and wire a value into Target.

### Mod (%)

`Mod`

The remainder of A / B (e.g. 7 mod 3 = 1). Useful for wrapping counters.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Number | Opacity, from 0 (invisible) to 1 (solid). It is a Number. Unwired, this is 0. | 0 |
| B | in | Number | Blue, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Mod (%)'s Out output into another node's input. It has no flow of its own.

### Pow (^)

`Pow`

Pow (^) — A to the power B (A^B).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Number | Opacity, from 0 (invisible) to 1 (solid). It is a Number. Unwired, this is 0. | 0 |
| B | in | Number | Blue, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Pow (^)'s Out output into another node's input. It has no flow of its own.

### Abs

`Abs`

The number without its sign: -5 becomes 5.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Abs's Out output into another node's input. It has no flow of its own.

### Negate

`Neg`

Flips the sign: 5 becomes -5.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Negate's Out output into another node's input. It has no flow of its own.

### Sqrt

`Sqrt`

Sqrt — Square root.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Sqrt's Out output into another node's input. It has no flow of its own.

### Sin

`Sin`

Sine of an angle in radians. Wire in Time for a smooth wave.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Sin's Out output into another node's input. It has no flow of its own.

### Cos

`Cos`

Cosine of an angle in radians.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Cos's Out output into another node's input. It has no flow of its own.

### Floor

`Floor`

Rounds down to a whole number: 2.7 becomes 2.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Floor's Out output into another node's input. It has no flow of its own.

### Round

`Round`

Rounds to the nearest whole number.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Round's Out output into another node's input. It has no flow of its own.

### Sign

`Sign`

-1 for negative numbers, 1 for positive, 0 for zero.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Sign's Out output into another node's input. It has no flow of its own.

### Clamp

`Clamp`

Keeps Value between Min and Max.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | in | Number | The value being stored, sent or tested. It is a Number. Unwired, this is 0. | 0 |
| Min | in | Number | The low end of the range. It is a Number. Unwired, this is 0. | 0 |
| Max | in | Number | The high end of the range. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Clamp's Out output into another node's input. It has no flow of its own.

### Lerp

`Lerp`

Blends from A to B by T (0 = A, 1 = B, 0.5 = halfway).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Number | Opacity, from 0 (invisible) to 1 (solid). It is a Number. Unwired, this is 0. | 0 |
| B | in | Number | Blue, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| T | in | Number | How far along the blend, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Lerp's Out output into another node's input. It has no flow of its own.

### Random 0-1

`Random`

A random number between 0 and 1.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Random 0-1's Out output into another node's input. It has no flow of its own.

### Random Range

`RandomRange`

A random number between Min and Max.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Min | in | Number | The low end of the range. It is a Number. Unwired, this is 0. | 0 |
| Max | in | Number | The high end of the range. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Random Range's Out output into another node's input. It has no flow of its own.

### Xor

`Xor`

True when exactly one of A and B is true.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Bool | Opacity, from 0 (invisible) to 1 (solid). It is a Bool. Unwired, this is false. | false |
| B | in | Bool | Blue, from 0 to 1. It is a Bool. Unwired, this is false. | false |
| Out | out | Bool | The result. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Xor's Out output into another node's input. It has no flow of its own.

### Equals

`Equals`

True when A and B are the same.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Any | Opacity, from 0 (invisible) to 1 (solid). It is an Any. Unwired, this is nothing. | nothing |
| B | in | Any | Blue, from 0 to 1. It is an Any. Unwired, this is nothing. | nothing |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Equals's Bool output into another node's input. It has no flow of its own.

### Not Equals

`NotEquals`

True when A and B are different.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Any | Opacity, from 0 (invisible) to 1 (solid). It is an Any. Unwired, this is nothing. | nothing |
| B | in | Any | Blue, from 0 to 1. It is an Any. Unwired, this is nothing. | nothing |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Not Equals's Bool output into another node's input. It has no flow of its own.

### Key Up

`KeyUp`

True only on the frame the chosen key is released.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

**Setting:** key. The value typed into the node. Default: space.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Released | out | Bool | True only on the frame it is released. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Key Up's Released output into another node's input. It has no flow of its own.

### Mouse X

`MouseX`

The mouse pointer's X position on screen.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| X | out | Number | The X value. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Mouse X's X output into another node's input. It has no flow of its own.

### Mouse Y

`MouseY`

The mouse pointer's Y position on screen.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Y | out | Number | The Y value. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Mouse Y's Y output into another node's input. It has no flow of its own.

### Make Vec3

`MakeVec3`

Builds a vector from X, Y, Z.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| X | in | Number | The X part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Y | in | Number | The Y part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Z | in | Number | The Z part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Vec3 | out | Vector3 | The vector result. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Make Vec3's Vec3 output into another node's input. It has no flow of its own.

### Get Position

`GetPosition`

This object's position as a vector.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Pos | out | Vector3 | The position. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Get Position's Pos output into another node's input. It has no flow of its own.

### Get Z

`GetZ`

This object's Z position.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Z | out | Number | The Z value. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Get Z's Z output into another node's input. It has no flow of its own.

### Set Scale

`SetScale`

Sets this object's size.

**Requires:** No extra component is required. It moves this object's Transform (or Target). It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Scale runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Scale finishes, the flow continues from here to the next step. | — |
| X | in | Number | The X part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Y | in | Number | The Y part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Z | in | Number | The Z part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Set Scale after an event such as On Start or On Update, and wire a value into X.

### Set Rotation

`SetRotation`

Sets this object's 2D rotation to Deg degrees.

**Requires:** No extra component is required. It moves this object's Transform (or Target). It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Rotation runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Rotation finishes, the flow continues from here to the next step. | — |
| Deg | in | Number | An angle in degrees. It is a Number. Unwired, this is 0. | 0 |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Set Rotation after an event such as On Start or On Update, and wire a value into Deg.

### Wait (s)

`Wait`

Stops the flow until N seconds have passed, then lets it through every time after that.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

**Setting:** seconds. The value typed into the node. Default: 1.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Wait (s) runs when a wire reaches this pin. | — |
| flow | out | Flow | After Wait (s) finishes, the flow continues from here to the next step. | — |

**Example:** Put Wait (s) on the flow after an event such as On Start. The chain runs when that event fires.

### Log Warning

`LogWarn`

Writes Value to the Console as a yellow warning.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Log Warning runs when a wire reaches this pin. | — |
| flow | out | Flow | After Log Warning finishes, the flow continues from here to the next step. | — |
| Value | in | Text | The value being stored, sent or tested. It is a Text. Unwired, this is empty text. | empty text |

**Example:** Put Log Warning after an event such as On Start or On Update, and wire a value into Value.

### Log Error

`LogError`

Writes Value to the Console as a red error.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Log Error runs when a wire reaches this pin. | — |
| flow | out | Flow | After Log Error finishes, the flow continues from here to the next step. | — |
| Value | in | Text | The value being stored, sent or tested. It is a Text. Unwired, this is empty text. | empty text |

**Example:** Put Log Error after an event such as On Start or On Update, and wire a value into Value.

### Add Force

`AddForce`

Pushes this object's Rigidbody steadily (use under On Update, e.g. for thrust).

**Requires:** Needs a Rigidbody2D or a Rigidbody3D on this object (or on Target). A 2D body is used when both are present. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Add Force runs when a wire reaches this pin. | — |
| flow | out | Flow | After Add Force finishes, the flow continues from here to the next step. | — |
| X | in | Number | The X part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Y | in | Number | The Y part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Z | in | Number | The Z part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Add Force after an event such as On Start or On Update, and wire a value into X.

### Set Velocity

`SetVelocity`

Sets this object's Rigidbody speed directly.

**Requires:** Needs a Rigidbody2D or a Rigidbody3D on this object (or on Target). A 2D body is used when both are present. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Velocity runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Velocity finishes, the flow continues from here to the next step. | — |
| X | in | Number | The X part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Y | in | Number | The Y part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Z | in | Number | The Z part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Set Velocity after an event such as On Start or On Update, and wire a value into X.

### Add Impulse

`AddImpulse`

Gives this object's Rigidbody a sudden push, e.g. a jump or knockback.

**Requires:** Needs a Rigidbody2D or a Rigidbody3D on this object (or on Target). A 2D body is used when both are present. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Add Impulse runs when a wire reaches this pin. | — |
| flow | out | Flow | After Add Impulse finishes, the flow continues from here to the next step. | — |
| X | in | Number | The X part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Y | in | Number | The Y part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Z | in | Number | The Z part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Add Impulse after an event such as On Start or On Update, and wire a value into X.

### Approx Equal

`Approx`

True when A and B are nearly equal. Safer than Equals for decimal numbers.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Number | Opacity, from 0 (invisible) to 1 (solid). It is a Number. Unwired, this is 0. | 0 |
| B | in | Number | Blue, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Approx Equal's Bool output into another node's input. It has no flow of its own.

### Between

`Between`

True when Value is between Min and Max (inclusive).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | in | Number | The value being stored, sent or tested. It is a Number. Unwired, this is 0. | 0 |
| Min | in | Number | The low end of the range. It is a Number. Unwired, this is 0. | 0 |
| Max | in | Number | The high end of the range. It is a Number. Unwired, this is 0. | 0 |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Between's Bool output into another node's input. It has no flow of its own.

### Chance

`Chance`

True with the given chance each time it is read (e.g. 0.1 = 10%).

**Requires:** No component is required. It only computes a value from its inputs.

**Setting:** 0..1. The value typed into the node. Default: 0.5.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Chance's Bool output into another node's input. It has no flow of its own.

### Has Var

`HasVar`

True when this object has a variable with that name.

**Requires:** No component is required. It only computes a value from its inputs.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Has Var's Bool output into another node's input. It has no flow of its own.

### Select

`Select`

Gives A when Cond is true, otherwise B.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Cond | in | Bool | The condition. True and false take different paths. It is a Bool. Unwired, this is false. | false |
| A | in | Any | Opacity, from 0 (invisible) to 1 (solid). It is an Any. Unwired, this is nothing. | nothing |
| B | in | Any | Blue, from 0 to 1. It is an Any. Unwired, this is nothing. | nothing |
| Out | out | Any | The result. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire Select's Out output into another node's input. It has no flow of its own.

### Concat (text)

`Concat`

Joins two pieces of text: "Score: " + 10.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Text | Opacity, from 0 (invisible) to 1 (solid). It is a Text. Unwired, this is empty text. | empty text |
| B | in | Text | Blue, from 0 to 1. It is a Text. Unwired, this is empty text. | empty text |
| Out | out | Text | The result. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Concat (text)'s Out output into another node's input. It has no flow of its own.

### Vec X

`VecX`

Vec X — The X part of a vector.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Vec | in | Vector3 | The vector to read or change. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| X | out | Number | The X value. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Vec X's X output into another node's input. It has no flow of its own.

### Vec Y

`VecY`

Vec Y — The Y part of a vector.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Vec | in | Vector3 | The vector to read or change. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Y | out | Number | The Y value. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Vec Y's Y output into another node's input. It has no flow of its own.

### Vec Z

`VecZ`

Vec Z — The Z part of a vector.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Vec | in | Vector3 | The vector to read or change. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Z | out | Number | The Z value. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Vec Z's Z output into another node's input. It has no flow of its own.

### Velocity X

`VelX`

This object's speed along X (from its Rigidbody).

**Requires:** Needs a Rigidbody2D or a Rigidbody3D on this object (or on Target). Without one the speed is 0.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| X | out | Number | The X value. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Velocity X's X output into another node's input. It has no flow of its own.

### Velocity Y

`VelY`

This object's speed along Y (from its Rigidbody).

**Requires:** Needs a Rigidbody2D or a Rigidbody3D on this object (or on Target). Without one the speed is 0.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Y | out | Number | The Y value. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Velocity Y's Y output into another node's input. It has no flow of its own.

### Velocity Z

`VelZ`

This object's speed along Z (from its Rigidbody).

**Requires:** Needs a Rigidbody2D or a Rigidbody3D on this object (or on Target). Without one the speed is 0.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Z | out | Number | The Z value. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Velocity Z's Z output into another node's input. It has no flow of its own.

### Ease In

`EaseIn`

Shapes a 0..1 value so it starts slow and speeds up.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| T | in | Number | How far along the blend, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Ease In's Out output into another node's input. It has no flow of its own.

### Ease Out

`EaseOut`

Shapes a 0..1 value so it starts fast and slows down at the end.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| T | in | Number | How far along the blend, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Ease Out's Out output into another node's input. It has no flow of its own.

### Ease In-Out

`EaseInOut`

Shapes a 0..1 value so it starts and ends slowly.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| T | in | Number | How far along the blend, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Ease In-Out's Out output into another node's input. It has no flow of its own.

### On Collision

`OnCollision`

Runs when this object bumps into another solid object. Other Name / Other Tag tell you what it hit.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Collision finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Collision into the graph and wire it the way its pins describe.

### On Trigger

`OnTrigger`

Runs when something enters this object's trigger area (a collider set to Trigger).

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Trigger finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Trigger into the graph and wire it the way its pins describe.

### On Click

`OnClick`

Runs when this object (or this UI button) is clicked.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Click finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Click into the graph and wire it the way its pins describe.

### On Anim Event

`OnAnimEvent`

Runs when an animation fires an event. Leave the name blank to catch every event.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

**Setting:** event (blank = any). The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Anim Event finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Anim Event into the graph and wire it the way its pins describe.

### Anim Event Name

`AnimEventName`

The name of the animation event that started this chain.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Name | out | Text | The name. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Anim Event Name's Name output into another node's input. It has no flow of its own.

### Other Name

`OtherName`

The name of the object this one collided with or triggered.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Name | out | Text | The name. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Other Name's Name output into another node's input. It has no flow of its own.

### Other Tag

`OtherTag`

The tag of the object this one collided with or triggered.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Tag | out | Text | The tag. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Other Tag's Tag output into another node's input. It has no flow of its own.

### Tan

`Tan`

Tangent of an angle in radians.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Tan's Out output into another node's input. It has no flow of its own.

### Atan2

`Atan2`

The angle (radians) of the direction X, Y. Use it to aim at something.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Y | in | Number | The Y part, in world units. It is a Number. Unwired, this is 0. | 0 |
| X | in | Number | The X part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Rad | out | Number | The angle in radians. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Atan2's Rad output into another node's input. It has no flow of its own.

### Exp

`Exp`

Exp — e raised to the input.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Exp's Out output into another node's input. It has no flow of its own.

### Log (ln)

`Log`

Log (ln) — Natural logarithm (ln).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Log (ln)'s Out output into another node's input. It has no flow of its own.

### Inverse Lerp

`InverseLerp`

The opposite of Lerp: how far Value is between A and B, as 0..1.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Number | Opacity, from 0 (invisible) to 1 (solid). It is a Number. Unwired, this is 0. | 0 |
| B | in | Number | Blue, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Value | in | Number | The value being stored, sent or tested. It is a Number. Unwired, this is 0. | 0 |
| T | out | Number | What Inverse Lerp produces on T. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Inverse Lerp's T output into another node's input. It has no flow of its own.

### Remap

`Remap`

Converts Value from one range to another, e.g. 0..100 health to 0..1 bar width.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | in | Number | The value being stored, sent or tested. It is a Number. Unwired, this is 0. | 0 |
| In Min | in | Number | The low end of the range you are converting from. It is a Number. Unwired, this is 0. | 0 |
| In Max | in | Number | The high end of the range you are converting from. It is a Number. Unwired, this is 1. | 1 |
| Out Min | in | Number | The low end of the range you are converting to. It is a Number. Unwired, this is 0. | 0 |
| Out Max | in | Number | The high end of the range you are converting to. It is a Number. Unwired, this is 1. | 1 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Remap's Out output into another node's input. It has no flow of its own.

### Smooth Step

`SmoothStep`

A smooth 0..1 curve as X goes from Edge0 to Edge1.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Edge0 | in | Number | The start of the smooth range. It is a Number. Unwired, this is 0. | 0 |
| Edge1 | in | Number | The end of the smooth range. It is a Number. Unwired, this is 1. | 1 |
| X | in | Number | The X part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Smooth Step's Out output into another node's input. It has no flow of its own.

### Ping-Pong

`PingPong`

Moves T back and forth between 0 and Length.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| T | in | Number | How far along the blend, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Length | in | Number | The length of one back-and-forth or wrap. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Ping-Pong's Out output into another node's input. It has no flow of its own.

### Repeat (wrap)

`Repeat01`

Loops T between 0 and Length, like a saw wave. Good for looping motion.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| T | in | Number | How far along the blend, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Length | in | Number | The length of one back-and-forth or wrap. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Repeat (wrap)'s Out output into another node's input. It has no flow of its own.

### Move Towards (value)

`MoveTowardsValue`

Moves Current towards Target by at most Max Step. Smooth counting without overshooting.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Current | in | Number | The value to move from. It is a Number. Unwired, this is 0. | 0 |
| Target | in | Number | The object to act on. Leave it empty to use this object (the one the graph is on). It is a Number. Unwired, this is this object. | this object |
| Max Step | in | Number | The most the value may change. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Move Towards (value)'s Out output into another node's input. It has no flow of its own.

### Delta Angle

`DeltaAngle`

The shortest turn from one angle to another, in degrees (-180..180).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| From | in | Number | The angle you are coming from, in degrees. It is a Number. Unwired, this is 0. | 0 |
| To | in | Number | The angle you are going to, in degrees. It is a Number. Unwired, this is 0. | 0 |
| Deg | out | Number | The angle in degrees. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Delta Angle's Deg output into another node's input. It has no flow of its own.

### Vec3 Add

`Vec3Add`

Vec3 Add — Adds two vectors.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Vector3 | Opacity, from 0 (invisible) to 1 (solid). It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| B | in | Vector3 | Blue, from 0 to 1. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Vec3 | out | Vector3 | The vector result. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Vec3 Add's Vec3 output into another node's input. It has no flow of its own.

### Vec3 Subtract

`Vec3Sub`

Subtracts vector B from A. B to A gives the direction from B to A.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Vector3 | Opacity, from 0 (invisible) to 1 (solid). It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| B | in | Vector3 | Blue, from 0 to 1. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Vec3 | out | Vector3 | The vector result. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Vec3 Subtract's Vec3 output into another node's input. It has no flow of its own.

### Vec3 Scale

`Vec3Scale`

Multiplies a vector by a number (makes it longer or shorter).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Vec | in | Vector3 | The vector to read or change. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Scale | in | Number | The new size, or a multiplier. It is a Number. Unwired, this is 1 (unchanged length). | 1 (unchanged length) |
| Vec3 | out | Vector3 | The vector result. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Vec3 Scale's Vec3 output into another node's input. It has no flow of its own.

### Vec3 Length

`Vec3Length`

Vec3 Length — How long a vector is.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Vec | in | Vector3 | The vector to read or change. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Length | out | Number | The length. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Vec3 Length's Length output into another node's input. It has no flow of its own.

### Vec3 Normalize

`Vec3Normalize`

The same direction with length 1.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Vec | in | Vector3 | The vector to read or change. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Vec3 | out | Vector3 | The vector result. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Vec3 Normalize's Vec3 output into another node's input. It has no flow of its own.

### Vec3 Dot

`Vec3Dot`

Dot product: how much two directions point the same way (1 same, 0 sideways, -1 opposite, for unit vectors).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Vector3 | Opacity, from 0 (invisible) to 1 (solid). It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| B | in | Vector3 | Blue, from 0 to 1. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Dot | out | Number | How much the two directions agree. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Vec3 Dot's Dot output into another node's input. It has no flow of its own.

### Vec3 Distance

`Vec3Distance`

The distance between two points.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Vector3 | Opacity, from 0 (invisible) to 1 (solid). It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| B | in | Vector3 | Blue, from 0 to 1. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Dist | out | Number | The distance. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Vec3 Distance's Dist output into another node's input. It has no flow of its own.

### To Text

`ToString`

Turns a value into text.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | in | Any | The value being stored, sent or tested. It is an Any. Unwired, this is nothing. | nothing |
| Text | out | Text | The text result. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire To Text's Text output into another node's input. It has no flow of its own.

### Format Number

`ToFixed`

Turns a number into text with a set number of decimals (e.g. 3.14).

**Requires:** No component is required. It only computes a value from its inputs.

**Setting:** decimals. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | in | Number | The value being stored, sent or tested. It is a Number. Unwired, this is 0. | 0 |
| Text | out | Text | The text result. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Format Number's Text output into another node's input. It has no flow of its own.

### Text Length

`TextLength`

How many characters are in the text.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Text | in | Text | The text to read or change. It is a Text. Unwired, this is empty text. | empty text |
| Len | out | Number | What Text Length produces on Len. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Text Length's Len output into another node's input. It has no flow of its own.

### Contains

`Contains`

True when Text contains Part.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Text | in | Text | The text to read or change. It is a Text. Unwired, this is empty text. | empty text |
| Part | in | Text | The piece of text to search for. It is a Text. Unwired, this is empty text. | empty text |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Contains's Bool output into another node's input. It has no flow of its own.

### Upper Case

`Upper`

Upper Case — MAKES TEXT UPPER CASE.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Text | in | Text | The text to read or change. It is a Text. Unwired, this is empty text. | empty text |
| Text | out | Text | The text result. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Upper Case's Text output into another node's input. It has no flow of its own.

### Lower Case

`Lower`

Lower Case — makes text lower case.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Text | in | Text | The text to read or change. It is a Text. Unwired, this is empty text. | empty text |
| Text | out | Text | The text result. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Lower Case's Text output into another node's input. It has no flow of its own.

### Object Exists

`ObjectExists`

True when an object with that name is in the scene.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Object | in | Object | An object reference, or its name. It is an Object. Unwired, this is no object. | no object |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Object Exists's Bool output into another node's input. It has no flow of its own.

### Object Position

`ObjectPosition`

The position of an object by name (or self / other).

**Requires:** No extra component is required. It reads this object's Transform (or Target).

**Setting:** name / self / other. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Object | in | Object | An object reference, or its name. It is an Object. Unwired, this is no object. | no object |
| Pos | out | Vector3 | The position. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Object Position's Pos output into another node's input. It has no flow of its own.

### Distance To

`DistanceTo`

How far this object is from another object.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

**Setting:** name / other. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Object | in | Object | An object reference, or its name. It is an Object. Unwired, this is no object. | no object |
| Dist | out | Number | The distance. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Distance To's Dist output into another node's input. It has no flow of its own.

### Self Name

`SelfName`

Self Name — This object's name.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Name | out | Text | The name. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Self Name's Name output into another node's input. It has no flow of its own.

### Look At

`LookAt`

Turns this object to face another object (by name, or the one it hit).

**Requires:** No extra component is required. It moves this object's Transform (or Target). It runs in Play mode.

**Setting:** name / other. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Look At runs when a wire reaches this pin. | — |
| flow | out | Flow | After Look At finishes, the flow continues from here to the next step. | — |
| Object | in | Object | An object reference, or its name. It is an Object. Unwired, this is no object. | no object |

**Example:** Put Look At after an event such as On Start or On Update, and wire a value into Object.

### Move Towards

`MoveTowards`

Moves this object towards another object at Speed units per second.

**Requires:** No extra component is required. It moves this object's Transform (or Target). It runs in Play mode.

**Setting:** name / other. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Move Towards runs when a wire reaches this pin. | — |
| flow | out | Flow | After Move Towards finishes, the flow continues from here to the next step. | — |
| Speed | in | Number | How fast to move, in units per second. It is a Number. Unwired, this is 2 units per second. | 2 units per second |
| Object | in | Object | An object reference, or its name. It is an Object. Unwired, this is no object. | no object |

**Example:** Put Move Towards after an event such as On Start or On Update, and wire a value into Speed.

### Set Active

`SetActive`

Turns an object on or off. A turned-off object is hidden and its scripts stop.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** name / self / other. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Active runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Active finishes, the flow continues from here to the next step. | — |
| On | in | Bool | True turns it on, false turns it off. It is a Bool. Unwired, this is true (turned on). | true (turned on) |
| Object | in | Object | An object reference, or its name. It is an Object. Unwired, this is no object. | no object |

**Example:** Put Set Active after an event such as On Start or On Update, and wire a value into On.

### Set Text

`SetText`

Changes the words on a text object.

**Requires:** Needs a Text renderer on the named object. Runs in Play mode.

**Setting:** text object name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Text runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Text finishes, the flow continues from here to the next step. | — |
| Text | in | Text | The text to read or change. It is a Text. Unwired, this is empty text. | empty text |
| Object | in | Object | An object reference, or its name. It is an Object. Unwired, this is no object. | no object |

**Example:** Put Set Text after an event such as On Start or On Update, and wire a value into Text.

### Load Scene

`LoadScene`

Switches to another scene by name.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** scene name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Load Scene runs when a wire reaches this pin. | — |
| flow | out | Flow | After Load Scene finishes, the flow continues from here to the next step. | — |

**Example:** Put Load Scene on the flow after an event such as On Start. The chain runs when that event fires.

### Mouse Held

`MouseButton`

True while a mouse button is held (0 left, 1 right, 2 middle).

**Requires:** No component is required. It reads the player or the clock while the game is playing.

**Setting:** 0 L / 1 R / 2 M. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Down | out | Bool | True while the button is held. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Mouse Held's Down output into another node's input. It has no flow of its own.

### Mouse Pressed

`MouseButtonDown`

True only on the frame a mouse button is pressed.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

**Setting:** 0 L / 1 R / 2 M. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Pressed | out | Bool | True only on the frame it is pressed. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Mouse Pressed's Pressed output into another node's input. It has no flow of its own.

### Cooldown (s)

`Cooldown`

Lets the flow through, then blocks it for N seconds. Stops attacks or jumps being spammed.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

**Setting:** seconds. The value typed into the node. Default: 1.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Cooldown (s) runs when a wire reaches this pin. | — |
| flow | out | Flow | After Cooldown (s) finishes, the flow continues from here to the next step. | — |

**Example:** Put Cooldown (s) on the flow after an event such as On Start. The chain runs when that event fires.

### Anim Trigger

`AnimTrigger`

Fires a trigger on this object's Animator (e.g. Jump, Attack).

**Requires:** Needs an Animator state machine on this object, a child or a parent (or on Target). Runs in Play mode.

**Setting:** parameter. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Anim Trigger runs when a wire reaches this pin. | — |
| flow | out | Flow | After Anim Trigger finishes, the flow continues from here to the next step. | — |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Anim Trigger on the flow after an event such as On Start. The chain runs when that event fires.

### Anim Set Float

`AnimSetFloat`

Sets a number parameter on this object's Animator (e.g. Speed).

**Requires:** Needs an Animator state machine on this object, a child or a parent (or on Target). Runs in Play mode.

**Setting:** parameter. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Anim Set Float runs when a wire reaches this pin. | — |
| flow | out | Flow | After Anim Set Float finishes, the flow continues from here to the next step. | — |
| Value | in | Number | The value being stored, sent or tested. It is a Number. Unwired, this is 0. | 0 |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Anim Set Float after an event such as On Start or On Update, and wire a value into Value.

### Anim Set Bool

`AnimSetBool`

Sets a true/false parameter on this object's Animator (e.g. Grounded).

**Requires:** Needs an Animator state machine on this object, a child or a parent (or on Target). Runs in Play mode.

**Setting:** parameter. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Anim Set Bool runs when a wire reaches this pin. | — |
| flow | out | Flow | After Anim Set Bool finishes, the flow continues from here to the next step. | — |
| Value | in | Bool | The value being stored, sent or tested. It is a Bool. Unwired, this is false. | false |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Anim Set Bool after an event such as On Start or On Update, and wire a value into Value.

### Anim State

`AnimState`

The name of the Animator state this object is in now.

**Requires:** Needs an Animator state machine on this object, a child or a parent (or on Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| State | out | Text | The state name. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Anim State's State output into another node's input. It has no flow of its own.

### IK Look At

`IkLook`

Makes the character's head look at a point (Weight 0..1, 0 lets go).

**Requires:** Needs a Full Body IK component on this object (or on Target). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. IK Look At runs when a wire reaches this pin. | — |
| flow | out | Flow | After IK Look At finishes, the flow continues from here to the next step. | — |
| Point | in | Vector3 | A point in the world. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Weight | in | Number | How strongly to apply this, from 0 (off) to 1 (fully). It is a Number. Unwired, this is 1 (fully looking). | 1 (fully looking) |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put IK Look At after an event such as On Start or On Update, and wire a value into Point.

### IK Hand Reach

`IkHand`

Makes the character's left or right hand reach for a point (Weight 0 lets go).

**Requires:** Needs a Full Body IK component on this object (or on Target). Runs in Play mode.

**Setting:** left / right. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. IK Hand Reach runs when a wire reaches this pin. | — |
| flow | out | Flow | After IK Hand Reach finishes, the flow continues from here to the next step. | — |
| Point | in | Vector3 | A point in the world. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Weight | in | Number | How strongly to apply this, from 0 (off) to 1 (fully). It is a Number. Unwired, this is 1 (fully reaching). | 1 (fully reaching) |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put IK Hand Reach after an event such as On Start or On Update, and wire a value into Point.

### Set Weather

`SetWeather`

Switches the scene's weather to none / rain / snow / storm at an Intensity 0..1 (it fades over the scene's weather fade time).

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** none / rain / snow / storm. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Weather runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Weather finishes, the flow continues from here to the next step. | — |
| Intensity | in | Number | How strong the weather is, from 0 to 1. It is a Number. Unwired, this is leave the scene's current intensity. | leave the scene's current intensity |

**Example:** Put Set Weather after an event such as On Start or On Update, and wire a value into Intensity.

### Set Fog

`SetFog`

Turns the scene's fog On or off, fading from Start to End world units from the camera.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Fog runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Fog finishes, the flow continues from here to the next step. | — |
| On | in | Bool | True turns it on, false turns it off. It is a Bool. Unwired, this is true (fog on). | true (fog on) |
| Start | in | Number | Where the effect begins, in world units from the camera. It is a Number. Unwired, this is leave the scene's fog start. | leave the scene's fog start |
| End | in | Number | Where the effect is complete, in world units from the camera. It is a Number. Unwired, this is leave the scene's fog end. | leave the scene's fog end |

**Example:** Put Set Fog after an event such as On Start or On Update, and wire a value into On.

### Set Post Effect

`SetPostEffect`

Sets one post-processing value by name (bloom, exposure, saturation, vignette, fxaa, ...; see docs/rendering.md). 0 / 1 for switches.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** bloom / exposure / saturation / .... The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Post Effect runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Post Effect finishes, the flow continues from here to the next step. | — |
| Value | in | Number | The value being stored, sent or tested. It is a Number. Unwired, this is 1. | 1 |

**Example:** Put Set Post Effect after an event such as On Start or On Update, and wire a value into Value.

### Nav Move To

`NavMoveTo`

Sends this object's Nav Mesh Agent walking to a point over the navmesh.

**Requires:** Needs a Nav Mesh Agent on this object (or on Target), and a baked nav mesh in the scene. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Nav Move To runs when a wire reaches this pin. | — |
| flow | out | Flow | After Nav Move To finishes, the flow continues from here to the next step. | — |
| Point | in | Vector3 | A point in the world. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Nav Move To after an event such as On Start or On Update, and wire a value into Point.

### Nav Stop

`NavStop`

Stops this object's Nav Mesh Agent where it is.

**Requires:** Needs a Nav Mesh Agent on this object (or on Target). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Nav Stop runs when a wire reaches this pin. | — |
| flow | out | Flow | After Nav Stop finishes, the flow continues from here to the next step. | — |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Nav Stop on the flow after an event such as On Start. The chain runs when that event fires.

### Nav Arrived

`NavArrived`

True once the Nav Mesh Agent reached its last Nav Move To point.

**Requires:** Needs a Nav Mesh Agent on this object (or on Target). False when there is none.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Nav Arrived's Bool output into another node's input. It has no flow of its own.

### Set Blackboard

`BtSet`

Writes a value into the Behavior Tree Runner's blackboard (the key is the setting).

**Requires:** Needs a Behavior Tree Runner on this object (or on Target). Runs in Play mode.

**Setting:** key. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Blackboard runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Blackboard finishes, the flow continues from here to the next step. | — |
| Value | in | Any | The value being stored, sent or tested. It is an Any. Unwired, this is nothing. | nothing |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Set Blackboard after an event such as On Start or On Update, and wire a value into Value.

### Get Blackboard

`BtGet`

Reads a value from the Behavior Tree Runner's blackboard.

**Requires:** Needs a Behavior Tree Runner on this object (or on Target).

**Setting:** key. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire Get Blackboard's Value output into another node's input. It has no flow of its own.

### Run Tree

`BtRun`

Loads a .okaybt behavior tree on this object's Behavior Tree Runner and starts it (empty: starts the current one over).

**Requires:** Needs a Behavior Tree Runner on this object (or on Target). Runs in Play mode.

**Setting:** tree file. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Run Tree runs when a wire reaches this pin. | — |
| flow | out | Flow | After Run Tree finishes, the flow continues from here to the next step. | — |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Run Tree on the flow after an event such as On Start. The chain runs when that event fires.

### Get Point On Spline

`GetSplinePoint`

The world point a fraction T (0 to 1) of the way along the Spline on this object or the Target. Even steps of T are even steps on the curve.

**Requires:** Needs a Spline on this object (or on Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| T (0-1) | in | Number | How far along the spline, from 0 (the start) to 1 (the end). It is a Number. Unwired, this is 0. | 0 |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Point | out | Vector3 | Where the hit happened. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Get Point On Spline's Point output into another node's input. It has no flow of its own.

### Spline Length

`SplineLength`

The length of the Spline on this object or the Target, in world units.

**Requires:** Needs a Spline on this object (or on Target). The length is 0 when there is none.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Length | out | Number | The length. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Spline Length's Length output into another node's input. It has no flow of its own.

### Set Line Points

`SetLinePoints`

Replaces the points of the Line Renderer on this object or the Target with a list of vectors (a laser from the gun to what it hits, a rope).

**Requires:** Needs a Line Renderer on this object (or on Target). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Line Points runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Line Points finishes, the flow continues from here to the next step. | — |
| Points | in | List | A list of positions the line passes through. It is a List. Unwired, this is an empty list. | an empty list |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Set Line Points after an event such as On Start or On Update, and wire a value into Points.

### Clear Trail

`ClearTrail`

Removes the trail of the Trail Renderer on this object or the Target at once (after a teleport, a respawn).

**Requires:** Needs a Trail Renderer on this object (or on Target). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Clear Trail runs when a wire reaches this pin. | — |
| flow | out | Flow | After Clear Trail finishes, the flow continues from here to the next step. | — |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Clear Trail on the flow after an event such as On Start. The chain runs when that event fires.

### Play Music

`MusicPlay`

Crossfades the scene's Music Player to a track: a file name, or a playlist number (empty: the current track, or the first).

**Requires:** Needs a Music Player in the scene. Runs in Play mode.

**Setting:** track (file or number). The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Play Music runs when a wire reaches this pin. | — |
| flow | out | Flow | After Play Music finishes, the flow continues from here to the next step. | — |

**Example:** Put Play Music on the flow after an event such as On Start. The chain runs when that event fires.

### Stop Music

`MusicStop`

Fades the scene's music out over Fade seconds (0 cuts it) and stops.

**Requires:** Needs a Music Player in the scene. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Stop Music runs when a wire reaches this pin. | — |
| flow | out | Flow | After Stop Music finishes, the flow continues from here to the next step. | — |
| Fade (s) | in | Number | How long the fade takes, in seconds. 0 cuts immediately. It is a Number. Unwired, this is 1 second. | 1 second |

**Example:** Put Stop Music after an event such as On Start or On Update, and wire a value into Fade (s).

### Pause Music

`MusicPause`

Pauses the scene's music where it is.

**Requires:** Needs a Music Player in the scene. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Pause Music runs when a wire reaches this pin. | — |
| flow | out | Flow | After Pause Music finishes, the flow continues from here to the next step. | — |

**Example:** Put Pause Music on the flow after an event such as On Start. The chain runs when that event fires.

### Resume Music

`MusicResume`

Resume Music — Resumes paused music.

**Requires:** Needs a Music Player in the scene. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Resume Music runs when a wire reaches this pin. | — |
| flow | out | Flow | After Resume Music finishes, the flow continues from here to the next step. | — |

**Example:** Put Resume Music on the flow after an event such as On Start. The chain runs when that event fires.

### Next Music Track

`MusicNext`

Crossfades to the next track in the Music Player's playlist (wraps round).

**Requires:** Needs a Music Player in the scene. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Next Music Track runs when a wire reaches this pin. | — |
| flow | out | Flow | After Next Music Track finishes, the flow continues from here to the next step. | — |

**Example:** Put Next Music Track on the flow after an event such as On Start. The chain runs when that event fires.

### Camera Shake

`CameraShake`

Shakes the main camera. Strength is 0 to 1 and stacks with a shake already running (an explosion, a hard landing).

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Camera Shake runs when a wire reaches this pin. | — |
| flow | out | Flow | After Camera Shake finishes, the flow continues from here to the next step. | — |
| Strength | in | Number | How hard, from 0 to 1. It is a Number. Unwired, this is 0.5. | 0.5 |

**Example:** Put Camera Shake after an event such as On Start or On Update, and wire a value into Strength.

### Set Camera FOV

`SetFov`

Sets the main camera's field of view in degrees (clamped to 1 to 179): a sprint zoom, a scope.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Camera FOV runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Camera FOV finishes, the flow continues from here to the next step. | — |
| Degrees | in | Number | The field of view in degrees. It is a Number. Unwired, this is 60. | 60 |

**Example:** Put Set Camera FOV after an event such as On Start or On Update, and wire a value into Degrees.

### Camera Follow

`CameraFollow`

Makes the main camera ease after the named object (adds a Camera Follow component if it has none). Smoothing: higher is snappier, 0 or less snaps.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Camera Follow runs when a wire reaches this pin. | — |
| flow | out | Flow | After Camera Follow finishes, the flow continues from here to the next step. | — |
| Object name | in | Text | The name of the object to follow. It is a Text. Unwired, this is empty text. | empty text |
| Smoothing | in | Number | How quickly the camera catches up. Higher is snappier. 0 snaps. It is a Number. Unwired, this is 0. | 0 |

**Example:** Put Camera Follow after an event such as On Start or On Update, and wire a value into Object name.

### Play Particles

`ParticlePlay`

Starts continuous emission on the Particle System on this object or the Target.

**Requires:** Needs a Particle System on this object (or on Target). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Play Particles runs when a wire reaches this pin. | — |
| flow | out | Flow | After Play Particles finishes, the flow continues from here to the next step. | — |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Play Particles on the flow after an event such as On Start. The chain runs when that event fires.

### Stop Particles

`ParticleStop`

Stops continuous emission on the Particle System on this object or the Target (live particles finish).

**Requires:** Needs a Particle System on this object (or on Target). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Stop Particles runs when a wire reaches this pin. | — |
| flow | out | Flow | After Stop Particles finishes, the flow continues from here to the next step. | — |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Stop Particles on the flow after an event such as On Start. The chain runs when that event fires.

### Burst Particles

`ParticleBurst`

Emits Count particles at once (default 20) from the Particle System on this object or the Target.

**Requires:** Needs a Particle System on this object (or on Target). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Burst Particles runs when a wire reaches this pin. | — |
| flow | out | Flow | After Burst Particles finishes, the flow continues from here to the next step. | — |
| Count | in | Number | How many. It is a Number. Unwired, this is 20. | 20 |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Burst Particles after an event such as On Start or On Update, and wire a value into Count.

### For Loop

`ForLoop`

Runs Body once for every number from First to Last (Index is the current number), then runs Done.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. For Loop runs when a wire reaches this pin. | — |
| Body | out | Flow | The flow continues out of Body. Wire this to the step that should run for that case. | — |
| Done | out | Flow | The flow continues out of Done. Wire this to the step that should run for that case. | — |
| First | in | Number | The first number of the loop, included. It is a Number. Unwired, this is 0. | 0 |
| Last | in | Number | The last number of the loop, included. It is a Number. Unwired, this is 9. | 9 |
| Index | out | Number | The current number, starting at 0. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Put For Loop after an event such as On Start or On Update, and wire a value into First.

**Related:** Repeat N, While, For Each, Break

### Repeat N

`Repeat`

Runs Body N times in a row, right away.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

**Setting:** count. The value typed into the node. Default: 1.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Repeat N runs when a wire reaches this pin. | — |
| Body | out | Flow | The flow continues out of Body. Wire this to the step that should run for that case. | — |

**Example:** Put Repeat N on the flow after an event such as On Start. The chain runs when that event fires.

### Flip-Flop

`FlipFlop`

Alternates between output A and output B each time it runs. Handy for on/off toggles.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Flip-Flop runs when a wire reaches this pin. | — |
| A | out | Flow | The flow continues out of A. Wire this to the step that should run for that case. | — |
| B | out | Flow | The flow continues out of B. Wire this to the step that should run for that case. | — |

**Example:** Put Flip-Flop on the flow after an event such as On Start. The chain runs when that event fires.

### Gate

`Gate`

Lets the flow through only while Open is true.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Gate runs when a wire reaches this pin. | — |
| flow | out | Flow | After Gate finishes, the flow continues from here to the next step. | — |
| Open | in | Bool | When this is false the flow stops here. It is a Bool. Unwired, this is true (the flow passes). | true (the flow passes) |

**Example:** Put Gate after an event such as On Start or On Update, and wire a value into Open.

### Do N Times

`DoN`

Lets the flow through the first N times only. Count says how many times it has passed.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

**Setting:** N. The value typed into the node. Default: 1.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Do N Times runs when a wire reaches this pin. | — |
| flow | out | Flow | After Do N Times finishes, the flow continues from here to the next step. | — |
| Count | out | Number | How many. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Put Do N Times on the flow after an event such as On Start. The chain runs when that event fires.

### Switch (int)

`SwitchInt`

Picks an output by number: 0, 1 or 2, or Default for any other value.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Switch (int) runs when a wire reaches this pin. | — |
| 0 | out | Flow | The flow continues out of 0. Wire this to the step that should run for that case. | — |
| 1 | out | Flow | The flow continues out of 1. Wire this to the step that should run for that case. | — |
| 2 | out | Flow | The flow continues out of 2. Wire this to the step that should run for that case. | — |
| Default | out | Flow | The flow continues out of Default. Wire this to the step that should run for that case. | — |
| Value | in | Number | The value being stored, sent or tested. It is a Number. Unwired, this is 0. | 0 |

**Example:** Put Switch (int) after an event such as On Start or On Update, and wire a value into Value.

### Toggle Var

`Toggle`

Flips a true/false variable: true becomes false and false becomes true.

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Toggle Var runs when a wire reaches this pin. | — |
| flow | out | Flow | After Toggle Var finishes, the flow continues from here to the next step. | — |

**Example:** Put Toggle Var on the flow after an event such as On Start. The chain runs when that event fires.

### Random Int

`RandomInt`

A random whole number from Min to Max (both included).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Min | in | Number | The low end of the range. It is a Number. Unwired, this is 0. | 0 |
| Max | in | Number | The high end of the range. It is a Number. Unwired, this is 10. | 10 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Random Int's Out output into another node's input. It has no flow of its own.

### Ceil

`Ceil`

Rounds up to a whole number: 2.1 becomes 3.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Ceil's Out output into another node's input. It has no flow of its own.

### Fraction

`Frac`

Just the part after the decimal point: 2.75 becomes 0.75.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Fraction's Out output into another node's input. It has no flow of its own.

### One Minus

`OneMinus`

1 minus the input. Turns 0..1 around (0 becomes 1).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire One Minus's Out output into another node's input. It has no flow of its own.

### Clamp 0-1

`Saturate`

Keeps the value between 0 and 1.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Clamp 0-1's Out output into another node's input. It has no flow of its own.

### Asin

`Asin`

Arc sine: the angle (radians) for a sine value.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Rad | out | Number | The angle in radians. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Asin's Rad output into another node's input. It has no flow of its own.

### Acos

`Acos`

Arc cosine: the angle (radians) for a cosine value.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In | in | Number | The value to transform. It is a Number. Unwired, this is 0. | 0 |
| Rad | out | Number | The angle in radians. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Acos's Rad output into another node's input. It has no flow of its own.

### Deg to Rad

`Deg2Rad`

Converts degrees to radians.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Deg | in | Number | An angle in degrees. It is a Number. Unwired, this is 0. | 0 |
| Rad | out | Number | The angle in radians. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Deg to Rad's Rad output into another node's input. It has no flow of its own.

### Rad to Deg

`Rad2Deg`

Converts radians to degrees.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Rad | in | Number | The Rad input of Rad to Deg. It is a Number. Unwired, this is 0. | 0 |
| Deg | out | Number | The angle in degrees. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Rad to Deg's Deg output into another node's input. It has no flow of its own.

### Vec3 Cross

`Vec3Cross`

Cross product: a vector at right angles to both A and B.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Vector3 | Opacity, from 0 (invisible) to 1 (solid). It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| B | in | Vector3 | Blue, from 0 to 1. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Vec3 | out | Vector3 | The vector result. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Vec3 Cross's Vec3 output into another node's input. It has no flow of its own.

### Vec3 Lerp

`Vec3Lerp`

Blends from point A to point B by T (0..1).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Vector3 | Opacity, from 0 (invisible) to 1 (solid). It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| B | in | Vector3 | Blue, from 0 to 1. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| T | in | Number | How far along the blend, from 0 to 1. It is a Number. Unwired, this is 0.5 (halfway). | 0.5 (halfway) |
| Vec3 | out | Vector3 | The vector result. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Vec3 Lerp's Vec3 output into another node's input. It has no flow of its own.

### Vec3 Negate

`Vec3Negate`

Points a vector the opposite way.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Vec | in | Vector3 | The vector to read or change. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Vec3 | out | Vector3 | The vector result. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Vec3 Negate's Vec3 output into another node's input. It has no flow of its own.

### Forward Vector

`ForwardVector`

The direction this object is facing.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Vec3 | out | Vector3 | The vector result. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Forward Vector's Vec3 output into another node's input. It has no flow of its own.

### Right Vector

`RightVector`

The direction to this object's right.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Vec3 | out | Vector3 | The vector result. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Right Vector's Vec3 output into another node's input. It has no flow of its own.

### Up Vector

`UpVector`

The direction above this object.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Vec3 | out | Vector3 | The vector result. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Up Vector's Vec3 output into another node's input. It has no flow of its own.

### Get Scale

`GetScale`

This object's scale as a vector.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Scale | out | Vector3 | The scale. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Get Scale's Scale output into another node's input. It has no flow of its own.

### Get Rotation

`GetRotation`

This object's rotation as X, Y, Z angles in degrees.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Euler | out | Vector3 | The rotation as X, Y, Z degrees. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Get Rotation's Euler output into another node's input. It has no flow of its own.

### Set Position (Vec3)

`SetPositionVec`

Puts this object at a position vector.

**Requires:** No extra component is required. It moves this object's Transform (or Target). It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Position (Vec3) runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Position (Vec3) finishes, the flow continues from here to the next step. | — |
| Pos | in | Vector3 | A position in the world. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Set Position (Vec3) after an event such as On Start or On Update, and wire a value into Pos.

### Move By (Vec3)

`MoveBy`

Moves this object by an offset vector.

**Requires:** No extra component is required. It moves this object's Transform (or Target). It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Move By (Vec3) runs when a wire reaches this pin. | — |
| flow | out | Flow | After Move By (Vec3) finishes, the flow continues from here to the next step. | — |
| Offset | in | Vector3 | How far to move from the current position. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Move By (Vec3) after an event such as On Start or On Update, and wire a value into Offset.

### Rotate (3D)

`Rotate3`

Turns this object by X, Y, Z degrees.

**Requires:** No extra component is required. It moves this object's Transform (or Target). It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Rotate (3D) runs when a wire reaches this pin. | — |
| flow | out | Flow | After Rotate (3D) finishes, the flow continues from here to the next step. | — |
| X | in | Number | The X part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Y | in | Number | The Y part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Z | in | Number | The Z part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Rotate (3D) after an event such as On Start or On Update, and wire a value into X.

### Set Rotation (3D)

`SetRotation3`

Sets this object's rotation to X, Y, Z degrees.

**Requires:** No extra component is required. It moves this object's Transform (or Target). It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Rotation (3D) runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Rotation (3D) finishes, the flow continues from here to the next step. | — |
| X | in | Number | The X part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Y | in | Number | The Y part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Z | in | Number | The Z part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Set Rotation (3D) after an event such as On Start or On Update, and wire a value into X.

### Set Color

`SetColor`

Tints this object's sprite, mesh or text (R, G, B, A from 0 to 1).

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Color runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Color finishes, the flow continues from here to the next step. | — |
| R | in | Number | Red, from 0 to 1. It is a Number. Unwired, this is 1. | 1 |
| G | in | Number | Green, from 0 to 1. It is a Number. Unwired, this is 1. | 1 |
| B | in | Number | Blue, from 0 to 1. It is a Number. Unwired, this is 1. | 1 |
| A | in | Number | Opacity, from 0 (invisible) to 1 (solid). It is a Number. Unwired, this is 1 (opaque). | 1 (opaque) |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Set Color after an event such as On Start or On Update, and wire a value into R.

### Play Clip

`PlayClip`

Plays an animation clip by name on this object's character or model.

**Requires:** Needs an Animator on this object (or on Target) that has the named clip. Runs in Play mode.

**Setting:** clip name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Play Clip runs when a wire reaches this pin. | — |
| flow | out | Flow | After Play Clip finishes, the flow continues from here to the next step. | — |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Play Clip on the flow after an event such as On Start. The chain runs when that event fires.

### Play Action

`PlayAction`

Goes to a built-in action state (Death, or a directional clip such as Death_F). Blend is seconds.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** action. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Play Action runs when a wire reaches this pin. | — |
| flow | out | Flow | After Play Action finishes, the flow continues from here to the next step. | — |
| Blend | in | Number | How many seconds the transition takes. It is a Number. Unwired, this is 0.2 seconds. | 0.2 seconds |

**Example:** Put Play Action after an event such as On Start or On Update, and wire a value into Blend.

### Is In Action

`IsInAction`

True while a built-in action state is playing.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| In Action | out | Bool | True while a built-in action is playing. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Is In Action's In Action output into another node's input. It has no flow of its own.

### Die

`Die`

Kills this object (Health) and plays Death. Direction: forward, back, left or right.

**Requires:** Meant for a character with a Health stat. It asks that character to die and play the death direction. Runs in Play mode.

**Setting:** forward / back / left / right. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Die runs when a wire reaches this pin. | — |
| flow | out | Flow | After Die finishes, the flow continues from here to the next step. | — |

**Example:** Put Die on the flow after an event such as On Start. The chain runs when that event fires.

### Revive

`Revive`

Respawns a dead Health stat, or clears the Animator's dead flag so the body gets up.

**Requires:** Meant for a character with a Health stat or an Animator that is flagged dead. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Revive runs when a wire reaches this pin. | — |
| flow | out | Flow | After Revive finishes, the flow continues from here to the next step. | — |

**Example:** Put Revive on the flow after an event such as On Start. The chain runs when that event fires.

### Dive

`Dive`

Dolphin dive: sprinting and on the ground, launch forward and land prone.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Dive runs when a wire reaches this pin. | — |
| flow | out | Flow | After Dive finishes, the flow continues from here to the next step. | — |

**Example:** Put Dive on the flow after an event such as On Start. The chain runs when that event fires.

### Sit

`Sit`

Sits on a named Seat, or on the ground (ground / knees / chair). Move or jump stands up.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** seat name, ground, knees, chair. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Sit runs when a wire reaches this pin. | — |
| flow | out | Flow | After Sit finishes, the flow continues from here to the next step. | — |

**Example:** Put Sit on the flow after an event such as On Start. The chain runs when that event fires.

### Stand Up

`StandUp`

Leaves a sit through Stand Up and frees the Seat.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Stand Up runs when a wire reaches this pin. | — |
| flow | out | Flow | After Stand Up finishes, the flow continues from here to the next step. | — |

**Example:** Put Stand Up on the flow after an event such as On Start. The chain runs when that event fires.

### Is Sitting

`IsSitting`

True while sitting or standing back up.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Sitting | out | Bool | True while sitting or standing back up. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Is Sitting's Sitting output into another node's input. It has no flow of its own.

### Interact

`Interact`

Sits on the nearest Seat, or the ground if the floor is flat. Stands up if already sitting.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Interact runs when a wire reaches this pin. | — |
| flow | out | Flow | After Interact finishes, the flow continues from here to the next step. | — |

**Example:** Put Interact on the flow after an event such as On Start. The chain runs when that event fires.

### Seat Prompt

`SeatPrompt`

The prompt of the nearest Seat in range (empty when none).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Prompt | out | Text | The words to show the player, or empty when there is nothing to show. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Seat Prompt's Prompt output into another node's input. It has no flow of its own.

### Hit React

`HitReact`

Plays a hit flinch. Direction is where the hit came from. Heavy plays a full-body stumble.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** forward / back / left / right. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Hit React runs when a wire reaches this pin. | — |
| flow | out | Flow | After Hit React finishes, the flow continues from here to the next step. | — |
| Heavy | in | Bool | True plays the heavier reaction. It is a Bool. Unwired, this is false. | false |

**Example:** Put Hit React after an event such as On Start or On Update, and wire a value into Heavy.

### Roll

`Roll`

Plays a roll or dodge clip in that direction. It does not move the body.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** forward / back / left / right. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Roll runs when a wire reaches this pin. | — |
| flow | out | Flow | After Roll finishes, the flow continues from here to the next step. | — |

**Example:** Put Roll on the flow after an event such as On Start. The chain runs when that event fires.

### Dodge

`Dodge`

Plays that clip and, on a first- or third-person controller, bursts that way. Spends stamina when a Stamina stat is present.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** forward / back / left / right. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Dodge runs when a wire reaches this pin. | — |
| flow | out | Flow | After Dodge finishes, the flow continues from here to the next step. | — |

**Example:** Put Dodge on the flow after an event such as On Start. The chain runs when that event fires.

### Vault

`Vault`

Probe ahead and vault a thin obstacle. Runs even when Can Vault is off.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Vault runs when a wire reaches this pin. | — |
| flow | out | Flow | After Vault finishes, the flow continues from here to the next step. | — |

**Example:** Put Vault on the flow after an event such as On Start. The chain runs when that event fires.

### Mantle

`Mantle`

Probe ahead and mantle a low or high ledge. Runs even when Can Mantle is off.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Mantle runs when a wire reaches this pin. | — |
| flow | out | Flow | After Mantle finishes, the flow continues from here to the next step. | — |

**Example:** Put Mantle on the flow after an event such as On Start. The chain runs when that event fires.

### Ledge Grab

`LedgeGrab`

In the air, catch a high ledge and hang. Hands-on-ledge IK is not wired.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Ledge Grab runs when a wire reaches this pin. | — |
| flow | out | Flow | After Ledge Grab finishes, the flow continues from here to the next step. | — |

**Example:** Put Ledge Grab on the flow after an event such as On Start. The chain runs when that event fires.

### Climb Up

`ClimbUp`

Climb from a hang onto the lip remembered by the grab.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Climb Up runs when a wire reaches this pin. | — |
| flow | out | Flow | After Climb Up finishes, the flow continues from here to the next step. | — |

**Example:** Put Climb Up on the flow after an event such as On Start. The chain runs when that event fires.

### Let Go

`LetGo`

Drop from a hang. Does not teleport.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Let Go runs when a wire reaches this pin. | — |
| flow | out | Flow | After Let Go finishes, the flow continues from here to the next step. | — |

**Example:** Put Let Go on the flow after an event such as On Start. The chain runs when that event fires.

### Is Hanging

`IsHanging`

True while hanging, grabbing a ledge, or shimmying.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Hanging | out | Bool | True while hanging from a ledge. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Is Hanging's Hanging output into another node's input. It has no flow of its own.

### Shimmy

`Shimmy`

While hanging, lean along the lip. -1 left, +1 right.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Shimmy runs when a wire reaches this pin. | — |
| flow | out | Flow | After Shimmy finishes, the flow continues from here to the next step. | — |
| Amount | in | Number | How much to add. Negative numbers subtract. It is a Number. Unwired, this is 0. | 0 |

**Example:** Put Shimmy after an event such as On Start or On Update, and wire a value into Amount.

### Set Time Scale

`SetTimeScale`

Changes how fast game time runs (1 normal, 0.5 slow motion, 0 frozen).

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Time Scale runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Time Scale finishes, the flow continues from here to the next step. | — |
| Scale | in | Number | The new size, or a multiplier. It is a Number. Unwired, this is 1 (normal speed). | 1 (normal speed) |

**Example:** Put Set Time Scale after an event such as On Start or On Update, and wire a value into Scale.

### Set Blend Shape

`SetBlendShape`

Sets a blend shape (face shape / morph) weight on this object's model.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** shape name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Blend Shape runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Blend Shape finishes, the flow continues from here to the next step. | — |
| Weight | in | Number | How strongly to apply this, from 0 (off) to 1 (fully). It is a Number. Unwired, this is 1. | 1 |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Set Blend Shape after an event such as On Start or On Update, and wire a value into Weight.

### Blend Shape

`GetBlendShape`

Reads a blend shape weight on this object's model.

**Requires:** No component is required. It only computes a value from its inputs.

**Setting:** shape name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Weight | out | Number | What Blend Shape produces on Weight. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Blend Shape's Weight output into another node's input. It has no flow of its own.

### Raycast 2D

`Raycast`

True when a line from the origin in the given direction hits a 2D collider within Max Dist.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Origin X | in | Number | The X of the ray's start. It is a Number. Unwired, this is 0. | 0 |
| Origin Y | in | Number | The Y of the ray's start. It is a Number. Unwired, this is 0. | 0 |
| Dir X | in | Number | The X of the ray's direction. It is a Number. Unwired, this is 0. | 0 |
| Dir Y | in | Number | The Y of the ray's direction. It is a Number. Unwired, this is 0. | 0 |
| Max Dist | in | Number | How far the cast may travel. It is a Number. Unwired, this is no practical limit. | no practical limit |
| Hit | out | Bool | True when something was hit. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Raycast 2D's Hit output into another node's input. It has no flow of its own.

### Mouse Wheel

`MouseWheel`

How far the scroll wheel moved this frame.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Delta | out | Number | How far the wheel moved this frame. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Mouse Wheel's Delta output into another node's input. It has no flow of its own.

### Time Scale

`TimeScale`

How fast game time runs (1 normal, 0.5 slow motion, 0 paused).

**Requires:** No component is required. It reads the player or the clock while the game is playing.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Scale | out | Number | The scale. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Time Scale's Scale output into another node's input. It has no flow of its own.

### Frame Count

`FrameCount`

How many frames have been drawn since the game started.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Frame | out | Number | How many frames have been drawn. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Frame Count's Frame output into another node's input. It has no flow of its own.

### Host Game

`NetHost`

Starts hosting an online game on this computer (default port 45000).

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

**Setting:** port (45000). The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Host Game runs when a wire reaches this pin. | — |
| flow | out | Flow | After Host Game finishes, the flow continues from here to the next step. | — |

**Example:** Put Host Game on the flow after an event such as On Start. The chain runs when that event fires.

### Join Game

`NetJoin`

Joins an online game at a host address (and optional port).

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

**Setting:** host [port]. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Join Game runs when a wire reaches this pin. | — |
| flow | out | Flow | After Join Game finishes, the flow continues from here to the next step. | — |

**Example:** Put Join Game on the flow after an event such as On Start. The chain runs when that event fires.

### Disconnect

`NetDisconnect`

Disconnect — Leaves the online game.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Disconnect runs when a wire reaches this pin. | — |
| flow | out | Flow | After Disconnect finishes, the flow continues from here to the next step. | — |

**Example:** Put Disconnect on the flow after an event such as On Start. The chain runs when that event fires.

### Is Connected

`NetConnected`

True while connected to an online game.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Is Connected's Bool output into another node's input. It has no flow of its own.

### Is Server

`NetIsServer`

True on the computer hosting the game.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Is Server's Bool output into another node's input. It has no flow of its own.

### Is Client

`NetIsClient`

True on a computer that joined someone else's game.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Is Client's Bool output into another node's input. It has no flow of its own.

### Peer Count

`NetPeers`

How many players are connected.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Count | out | Number | How many. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Peer Count's Count output into another node's input. It has no flow of its own.

### My Net Id

`NetId`

This player's network id.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Id | out | Number | The network id. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire My Net Id's Id output into another node's input. It has no flow of its own.

### Send Message

`NetSend`

Sends data to the other players on a channel.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

**Setting:** channel. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Send Message runs when a wire reaches this pin. | — |
| flow | out | Flow | After Send Message finishes, the flow continues from here to the next step. | — |
| Data | in | Any | The payload sent to the other players. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put Send Message after an event such as On Start or On Update, and wire a value into Data.

### Set Net Var

`NetSetVar`

Sets a variable that is synced to every player.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Net Var runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Net Var finishes, the flow continues from here to the next step. | — |
| Value | in | Any | The value being stored, sent or tested. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put Set Net Var after an event such as On Start or On Update, and wire a value into Value.

### Get Net Var

`NetGetVar`

Reads a variable synced between players.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire Get Net Var's Value output into another node's input. It has no flow of its own.

### Send Chat

`NetChat`

Sends a chat message to everyone in the game.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Send Chat runs when a wire reaches this pin. | — |
| flow | out | Flow | After Send Chat finishes, the flow continues from here to the next step. | — |
| Text | in | Text | The text to read or change. It is a Text. Unwired, this is empty text. | empty text |

**Example:** Put Send Chat after an event such as On Start or On Update, and wire a value into Text.

### Call RPC

`NetRpc`

Runs a named function on the other players' machines (remote procedure call).

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

**Setting:** rpc name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Call RPC runs when a wire reaches this pin. | — |
| flow | out | Flow | After Call RPC finishes, the flow continues from here to the next step. | — |
| Data | in | Any | The payload sent to the other players. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put Call RPC after an event such as On Start or On Update, and wire a value into Data.

### Net Spawn

`NetSpawnOwned`

Spawns a prefab that every player sees, owned by this player.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

**Setting:** prefab. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Net Spawn runs when a wire reaches this pin. | — |
| flow | out | Flow | After Net Spawn finishes, the flow continues from here to the next step. | — |

**Example:** Put Net Spawn on the flow after an event such as On Start. The chain runs when that event fires.

### Net Despawn

`NetDespawn`

Removes a networked object for every player.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Net Despawn runs when a wire reaches this pin. | — |
| flow | out | Flow | After Net Despawn finishes, the flow continues from here to the next step. | — |
| Net Id | in | Text | The network id of the object to remove. It is a Text. Unwired, this is empty text. | empty text |

**Example:** Put Net Despawn after an event such as On Start or On Update, and wire a value into Net Id.

### Set Ready

`NetReady`

Marks this player ready (or not) in the lobby.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Ready runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Ready finishes, the flow continues from here to the next step. | — |
| Ready | in | Bool | True marks this player ready, false clears it. It is a Bool. Unwired, this is false. | false |

**Example:** Put Set Ready after an event such as On Start or On Update, and wire a value into Ready.

### Start Match

`NetStartMatch`

Starts the match for everyone (host).

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Start Match runs when a wire reaches this pin. | — |
| flow | out | Flow | After Start Match finishes, the flow continues from here to the next step. | — |

**Example:** Put Start Match on the flow after an event such as On Start. The chain runs when that event fires.

### Match Started

`NetMatchStarted`

True once the match has started.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Match Started's Bool output into another node's input. It has no flow of its own.

### All Ready

`NetAllReady`

True when every player in the lobby is ready.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire All Ready's Bool output into another node's input. It has no flow of its own.

### Call Object RPC

`NetObjRpc`

Runs a named function on the other players' copy of THIS object. The object needs a Network Object with the same id on each machine.

**Requires:** Needs a Network Object on this object, with the same id on each machine. Runs in Play mode, online.

**Setting:** rpc name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Call Object RPC runs when a wire reaches this pin. | — |
| flow | out | Flow | After Call Object RPC finishes, the flow continues from here to the next step. | — |
| Data | in | Any | The payload sent to the other players. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put Call Object RPC after an event such as On Start or On Update, and wire a value into Data.

### Set Sync Var

`SyncVarSet`

Sets a Sync Var on this object. The owner sends it to everyone; other players' copies follow.

**Requires:** Needs a Network Object on this object so the value is published to other players. Runs in Play mode, online.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Sync Var runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Sync Var finishes, the flow continues from here to the next step. | — |
| Value | in | Any | The value being stored, sent or tested. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put Set Sync Var after an event such as On Start or On Update, and wire a value into Value.

### Get Sync Var

`SyncVarGet`

Reads a Sync Var on this object (the latest value the owner published).

**Requires:** Needs a Network Object on this object. Reads the last value the owner published.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire Get Sync Var's Value output into another node's input. It has no flow of its own.

### On Object RPC

`OnNetRpc`

Runs when another player calls this object's RPC of this name. The Value output is the text they sent.

**Requires:** Needs a Network Object on this object. Runs in Play mode when another player calls this object's RPC.

**Setting:** rpc name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Object RPC finishes, the flow continues from here to the next step. | — |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire On Object RPC's Value output into another node's input. It has no flow of its own.

### Steam Name

`SteamName`

The player's Steam name.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Name | out | Text | The name. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Steam Name's Name output into another node's input. It has no flow of its own.

### Achievement Unlocked

`SteamIsUnlocked`

True when the player already has this achievement.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

**Setting:** achievement id. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Achievement Unlocked's Bool output into another node's input. It has no flow of its own.

### Unlock Achievement

`SteamUnlock`

Unlocks a Steam achievement.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

**Setting:** achievement id. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Unlock Achievement runs when a wire reaches this pin. | — |
| flow | out | Flow | After Unlock Achievement finishes, the flow continues from here to the next step. | — |

**Example:** Put Unlock Achievement on the flow after an event such as On Start. The chain runs when that event fires.

### Get Steam Stat

`SteamGetStat`

Reads a Steam stat (e.g. total kills).

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

**Setting:** stat. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | out | Number | The value that was read. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Get Steam Stat's Value output into another node's input. It has no flow of its own.

### Set Steam Stat

`SteamSetStat`

Sets a Steam stat. Use Store Stats afterwards to upload it.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

**Setting:** stat. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Steam Stat runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Steam Stat finishes, the flow continues from here to the next step. | — |
| Value | in | Number | The value being stored, sent or tested. It is a Number. Unwired, this is 0. | 0 |

**Example:** Put Set Steam Stat after an event such as On Start or On Update, and wire a value into Value.

### Store Stats

`SteamStore`

Uploads changed Steam stats and achievements.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Store Stats runs when a wire reaches this pin. | — |
| flow | out | Flow | After Store Stats finishes, the flow continues from here to the next step. | — |

**Example:** Put Store Stats on the flow after an event such as On Start. The chain runs when that event fires.

### Submit Score

`SteamLeaderboard`

Submits a score to a Steam leaderboard.

**Requires:** No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.

**Setting:** leaderboard. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Submit Score runs when a wire reaches this pin. | — |
| flow | out | Flow | After Submit Score finishes, the flow continues from here to the next step. | — |
| Score | in | Number | The score to submit. It is a Number. Unwired, this is 0. | 0 |

**Example:** Put Submit Score after an event such as On Start or On Update, and wire a value into Score.

### On Key Pressed

`OnKey`

Runs once when the chosen key is pressed.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

**Setting:** key. The value typed into the node. Default: space.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Key Pressed finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Key Pressed into the graph and wire it the way its pins describe.

### On Key Released

`OnKeyUp`

Runs once when the chosen key is let go.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

**Setting:** key. The value typed into the node. Default: space.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Key Released finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Key Released into the graph and wire it the way its pins describe.

### On Timer

`OnTimer`

Runs every N seconds, e.g. a spawner or a pulse. Set the interval in the node.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

**Setting:** every N seconds. The value typed into the node. Default: 1.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Timer finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Timer into the graph and wire it the way its pins describe.

### On Custom Event

`OnEvent`

Runs when another script sends this event name (Send Event / Broadcast Event). Value is what it sent.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

**Setting:** event name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Custom Event finishes, the flow continues from here to the next step. | — |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire On Custom Event's Value output into another node's input. It has no flow of its own.

### On Collision Exit

`OnCollisionExit`

Runs once when this object stops touching something it collided with.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Collision Exit finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Collision Exit into the graph and wire it the way its pins describe.

### On Trigger Exit

`OnTriggerExit`

Runs once when something leaves this object's trigger area.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Trigger Exit finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Trigger Exit into the graph and wire it the way its pins describe.

### On Mouse Enter

`OnMouseEnter`

Runs once when the mouse pointer moves onto this object.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Mouse Enter finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Mouse Enter into the graph and wire it the way its pins describe.

### On Mouse Down

`OnMouseDown`

Runs when a mouse button goes down over this object.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Mouse Down finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Mouse Down into the graph and wire it the way its pins describe.

### On Mouse Up

`OnMouseUp`

Runs when a mouse button is released over this object.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Mouse Up finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Mouse Up into the graph and wire it the way its pins describe.

### On Mouse Over

`OnMouseOver`

Runs every frame while the mouse pointer is over this object.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Mouse Over finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Mouse Over into the graph and wire it the way its pins describe.

### On Collision Stay

`OnCollisionStay`

Runs every frame while this object keeps touching another solid object.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Collision Stay finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Collision Stay into the graph and wire it the way its pins describe.

### On Late Update

`OnLateUpdate`

Runs every frame after every On Update. Good for cameras that follow something.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Late Update finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Late Update into the graph and wire it the way its pins describe.

### On Fixed Update

`OnFixedUpdate`

Runs on the physics step, not every drawn frame. Put forces and velocity changes here so they stay steady when the frame rate changes.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Fixed Update finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Fixed Update into the graph and wire it the way its pins describe.

### On Enable

`OnEnable`

Runs when this object becomes active (including the first time it starts).

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Enable finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Enable into the graph and wire it the way its pins describe.

### On Disable

`OnDisable`

Runs when this object is turned off, before it stops updating.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Disable finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Disable into the graph and wire it the way its pins describe.

### On Destroy

`OnDestroy`

Runs when this object is about to be removed from the scene.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Destroy finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Destroy into the graph and wire it the way its pins describe.

### Action

`Action`

A ready-made step (Move, Jump, Play Sound...). Pick it and set it up in the node.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** The action step, chosen in the node.. The action step, chosen in the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Action runs when a wire reaches this pin. | — |
| flow | out | Flow | After Action finishes, the flow continues from here to the next step. | — |

**Example:** Put Action on the flow after an event such as On Start. The chain runs when that event fires.

### Action Steps

`Actions`

A list of ready-made steps that run one after another.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** The list of action steps, edited in the node.. The list of action steps, edited in the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Action Steps runs when a wire reaches this pin. | — |
| flow | out | Flow | After Action Steps finishes, the flow continues from here to the next step. | — |

**Example:** Put Action Steps on the flow after an event such as On Start. The chain runs when that event fires.

### Check

`Condition`

A ready-made check (Is Grounded, Has Tag...). Pick it in the node; goes out True or False.

**Requires:** No component is required. It only computes a value from its inputs.

**Setting:** The check, chosen in the node.. The check, chosen in the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Check runs when a wire reaches this pin. | — |
| True | out | Flow | The flow continues out of True. Wire this to the step that should run for that case. | — |
| False | out | Flow | The flow continues out of False. Wire this to the step that should run for that case. | — |

**Example:** Put Check on the flow after an event such as On Start. The chain runs when that event fires.

### Delay (s)

`Delay`

Pauses this chain for N seconds, then carries on from here (like a countdown).

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

**Setting:** seconds. The value typed into the node. Default: 1.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Delay (s) runs when a wire reaches this pin. | — |
| flow | out | Flow | After Delay (s) finishes, the flow continues from here to the next step. | — |

**Example:** Put Delay (s) on the flow after an event such as On Start. The chain runs when that event fires.

### On Mouse Exit

`OnMouseExit`

Runs once when the mouse pointer leaves this object.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Mouse Exit finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Mouse Exit into the graph and wire it the way its pins describe.

### Send Event

`SendEvent`

Sends an event to one object (named in Target). Its On Custom Event with this name runs.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** event name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Send Event runs when a wire reaches this pin. | — |
| flow | out | Flow | After Send Event finishes, the flow continues from here to the next step. | — |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Value | in | Any | The value being stored, sent or tested. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put Send Event after an event such as On Start or On Update, and wire a value into Target.

### Broadcast Event

`BroadcastEvent`

Sends an event to every object in the scene that listens for this name.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** event name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Broadcast Event runs when a wire reaches this pin. | — |
| flow | out | Flow | After Broadcast Event finishes, the flow continues from here to the next step. | — |
| Value | in | Any | The value being stored, sent or tested. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put Broadcast Event after an event such as On Start or On Update, and wire a value into Value.

### Event Value

`EventValue`

The value sent with the custom event that started this chain.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire Event Value's Value output into another node's input. It has no flow of its own.

### Event Name

`EventName`

The name of the custom event that started this chain.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Name | out | Text | The name. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Event Name's Name output into another node's input. It has no flow of its own.

### On Object Placed

`OnObjectPlaced`

Runs when a player places an object in build mode (also on undo / redo). Other Name is the object; Event Value its build id.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Object Placed finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Object Placed into the graph and wire it the way its pins describe.

### On Object Removed

`OnObjectRemoved`

Runs when a player deletes an object in build mode (also on undo / redo). Other Name is the object.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Object Removed finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Object Removed into the graph and wire it the way its pins describe.

### On Logic Signal

`OnLogicSignal`

Runs when a wired door, light, spawner or event turns on or off. Event Name is its signal name; Event Value is true when on.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Logic Signal finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Logic Signal into the graph and wire it the way its pins describe.

### On Item Added

`OnItemAdded`

Runs when items come into this object's Inventory by any route (picked up, crafted, a chest or bag move, a script). Event Name is the item; Event Value how many.

**Requires:** Runs only if this object has an Inventory that raises the event.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Item Added finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Item Added into the graph and wire it the way its pins describe.

### On Item Removed

`OnItemRemoved`

Runs when items leave this object's Inventory (used, dropped, crafted with, moved out). Event Name is the item; Event Value how many.

**Requires:** Runs only if this object has an Inventory that raises the event.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Item Removed finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Item Removed into the graph and wire it the way its pins describe.

### On Inventory Full

`OnInventoryFull`

Runs when items didn't fit this object's Inventory (an add, a pickup or a chest move ran out of room). Event Name is the item; Event Value how many were left over.

**Requires:** Runs only if this object has an Inventory that raises the event.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Inventory Full finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Inventory Full into the graph and wire it the way its pins describe.

### On Item Used

`OnItemUsed`

Runs when an item from this object's Inventory is used (eaten, a build piece picked, equipped). Event Name is the item.

**Requires:** Runs only if this object has an Inventory that raises the event.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Item Used finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Item Used into the graph and wire it the way its pins describe.

### On Equip

`OnEquip`

Runs when this object's Equipment puts an item on. Event Name is the slot (Head, Main Hand, ...); Event Value the item.

**Requires:** Runs only if this object has an Equipment component that raises the event.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Equip finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Equip into the graph and wire it the way its pins describe.

### On Craft

`OnCraft`

Runs each time this object's Crafting or Crafting Station makes a recipe (instantly, or when a timed craft finishes). Event Name is the recipe's output item.

**Requires:** Runs only if this object has a Crafting or Crafting Station component.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Craft finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Craft into the graph and wire it the way its pins describe.

### On Sequence Event

`OnSequenceEvent`

Runs when a playing sequence (cutscene) crosses an event on its event track. Leave the name blank to catch every event; Event Name reads it.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

**Setting:** event (blank = any). The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Sequence Event finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Sequence Event into the graph and wire it the way its pins describe.

### Play Sequence

`PlaySequence`

Plays a cutscene: the Sequence Player whose file has this name, or Assets/<name>.okayseq.

**Requires:** Needs a Sequence Player in the scene whose file matches the name, or a file Assets/<name>.okayseq. Runs in Play mode.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Play Sequence runs when a wire reaches this pin. | — |
| flow | out | Flow | After Play Sequence finishes, the flow continues from here to the next step. | — |

**Example:** Put Play Sequence on the flow after an event such as On Start. The chain runs when that event fires.

### Stop Sequence

`StopSequence`

Stops every playing sequence; the camera goes back to the game.

**Requires:** Stops every Sequence Player in the scene. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Stop Sequence runs when a wire reaches this pin. | — |
| flow | out | Flow | After Stop Sequence finishes, the flow continues from here to the next step. | — |

**Example:** Put Stop Sequence on the flow after an event such as On Start. The chain runs when that event fires.

### Start Dialogue

`StartDialogue`

Starts the conversation on the named object's Dialogue Runner (or the graph with that name; empty = this object's runner).

**Requires:** Needs a Dialogue Runner on the named object (or on this object when the name is empty). Runs in Play mode.

**Setting:** object / graph name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Start Dialogue runs when a wire reaches this pin. | — |
| flow | out | Flow | After Start Dialogue finishes, the flow continues from here to the next step. | — |

**Example:** Put Start Dialogue on the flow after an event such as On Start. The chain runs when that event fires.

### On Dialogue Event

`OnDialogueEvent`

Runs when a dialogue reaches an Event node on this object (the NPC) or on the player. Event Name is the event's name.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Dialogue Event finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Dialogue Event into the graph and wire it the way its pins describe.

### On Dialogue End

`OnDialogueEnd`

Runs when a conversation on this object (or with the player) ends. Event Name is the graph's name.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Dialogue End finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Dialogue End into the graph and wire it the way its pins describe.

### Set Story Var

`SetStoryVar`

Sets a global story variable (the scene's Story State; saved with the game).

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** variable. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Story Var runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Story Var finishes, the flow continues from here to the next step. | — |
| Value | in | Any | The value being stored, sent or tested. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put Set Story Var after an event such as On Start or On Update, and wire a value into Value.

### Get Story Var

`GetStoryVar`

Reads a global story variable (0 / empty when it was never set).

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** variable. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire Get Story Var's Value output into another node's input. It has no flow of its own.

### Start Quest

`StartQuest`

Starts a quest from the scene's Quest Log (its prerequisites must be completed).

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** quest id. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Start Quest runs when a wire reaches this pin. | — |
| flow | out | Flow | After Start Quest finishes, the flow continues from here to the next step. | — |

**Example:** Put Start Quest on the flow after an event such as On Start. The chain runs when that event fires.

### Complete Objective

`CompleteObjective`

Marks one objective (Index, 0-based) of an active quest done; the quest completes when every required objective is.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** quest id. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Complete Objective runs when a wire reaches this pin. | — |
| flow | out | Flow | After Complete Objective finishes, the flow continues from here to the next step. | — |
| Index | in | Number | Which entry. The first is 0. It is a Number. Unwired, this is 0 (the first objective). | 0 (the first objective) |

**Example:** Put Complete Objective after an event such as On Start or On Update, and wire a value into Index.

### Quest State

`QuestState`

A quest's state: inactive, active, completed or failed.

**Requires:** No component is required. It only computes a value from its inputs.

**Setting:** quest id. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| State | out | Text | The state name. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Quest State's State output into another node's input. It has no flow of its own.

### On Quest Started

`OnQuestStarted`

Runs when a quest starts (on the Quest Log's object and on the player). Event Name is the quest id.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Quest Started finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Quest Started into the graph and wire it the way its pins describe.

### On Quest Completed

`OnQuestCompleted`

Runs when a quest completes. Event Name is the quest id; Event Value how many times it has been completed.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Quest Completed finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Quest Completed into the graph and wire it the way its pins describe.

### On Quest Failed

`OnQuestFailed`

Runs when a quest fails. Event Name is the quest id.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Quest Failed finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Quest Failed into the graph and wire it the way its pins describe.

### On Objective Completed

`OnObjectiveCompleted`

Runs when an objective completes. Event Name is the quest id; Event Value the objective index.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Objective Completed finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Objective Completed into the graph and wire it the way its pins describe.

### On Quest Event

`OnQuestEvent`

Runs when a completed quest's reward names an event. Event Name is that event.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Quest Event finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Quest Event into the graph and wire it the way its pins describe.

### On Cheat Detected

`OnCheatDetected`

Server only: runs when anti-cheat refuses something a client sent. Event Name is what was caught ("speed hack", "forged transform", "rpc flood", "clock drift", ...); Event Value is the player's peer id; Get Var cheat_detail is the object, prefab or key involved.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Cheat Detected finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Cheat Detected into the graph and wire it the way its pins describe.

### On Validate Rpc

`OnValidateRpc`

Server only: runs before a client's RPC is accepted. Event Name is the RPC, Event Value its data, Get Var rpc_sender the player. Use Reject Rpc to refuse it.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Validate Rpc finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Validate Rpc into the graph and wire it the way its pins describe.

### Reject Rpc

`RejectRpc`

Inside On Validate Rpc: refuses the RPC being checked. It won't run or reach anyone, and counts as a cheat attempt by the sender.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Reject Rpc runs when a wire reaches this pin. | — |
| flow | out | Flow | After Reject Rpc finishes, the flow continues from here to the next step. | — |

**Example:** Put Reject Rpc on the flow after an event such as On Start. The chain runs when that event fires.

### On Validate Hit

`OnValidateHit`

Server only: runs for each hit claim (Claim Hit / net_hit) that passed the server's own checks: where the shooter was, range, line of sight, where the target was. Event Name is the weapon; Event Value is the shooter's peer id; Get Var hit_target is the target's sync id and hit_damage the damage (Set Var hit_damage to change it). Use Reject Hit to refuse it.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Validate Hit finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Validate Hit into the graph and wire it the way its pins describe.

### Reject Hit

`RejectHit`

Inside On Validate Hit: refuses the hit being checked. It does no damage and counts as a cheat attempt by the shooter.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Reject Hit runs when a wire reaches this pin. | — |
| flow | out | Flow | After Reject Hit finishes, the flow continues from here to the next step. | — |

**Example:** Put Reject Hit on the flow after an event such as On Start. The chain runs when that event fires.

### Server Budget

`ServerBudget`

Server only: a player's Net Validator budget for the RPC named here (ammo). Player is their peer id; Add tops it up first (a reload or pickup; 0 just reads). Left is what's left, or -1 if no Net Validator has a budget for that RPC.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** rpc name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Server Budget runs when a wire reaches this pin. | — |
| flow | out | Flow | After Server Budget finishes, the flow continues from here to the next step. | — |
| Player | in | Number | The player's peer id. It is a Number. Unwired, this is 0. | 0 |
| Add | in | Number | How much to add before reading. 0 only reads. It is a Number. Unwired, this is 0 (just read the budget). | 0 (just read the budget) |
| Left | out | Number | How many did not fit, or how much budget is left. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Put Server Budget after an event such as On Start or On Update, and wire a value into Player.

### Enter Build Mode

`EnterBuildMode`

Puts the player into build mode (needs a Build Mode component in the scene).

**Requires:** Needs a Build Mode component in the scene (usually on the player). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Enter Build Mode runs when a wire reaches this pin. | — |
| flow | out | Flow | After Enter Build Mode finishes, the flow continues from here to the next step. | — |

**Example:** Put Enter Build Mode on the flow after an event such as On Start. The chain runs when that event fires.

### Exit Build Mode

`ExitBuildMode`

Takes the player out of build mode.

**Requires:** Needs a Build Mode component in the scene. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Exit Build Mode runs when a wire reaches this pin. | — |
| flow | out | Flow | After Exit Build Mode finishes, the flow continues from here to the next step. | — |

**Example:** Put Exit Build Mode on the flow after an event such as On Start. The chain runs when that event fires.

### In Build Mode?

`InBuildMode`

True while the player is in build mode.

**Requires:** Needs a Build Mode component in the scene. False when there is none.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire In Build Mode?'s Bool output into another node's input. It has no flow of its own.

### Set Build Palette

`SetBuildPalette`

Replaces what players can place. List items as Name=prefab, separated by commas; add |icon.png after a prefab for a hotbar icon.

**Requires:** Needs a Build Mode component in the scene. Runs in Play mode.

**Setting:** Name=prefab|icon, .... The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Build Palette runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Build Palette finishes, the flow continues from here to the next step. | — |

**Example:** Put Set Build Palette on the flow after an event such as On Start. The chain runs when that event fires.

### Save Creation

`SaveCreation`

Saves everything players placed, with its wiring, under this name in the save folder. Saved is true if it worked.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** creation name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Save Creation runs when a wire reaches this pin. | — |
| flow | out | Flow | After Save Creation finishes, the flow continues from here to the next step. | — |
| Saved | out | Bool | True when the save worked. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Put Save Creation on the flow after an event such as On Start. The chain runs when that event fires.

### Load Creation

`LoadCreation`

Replaces what players placed with a saved creation of this name. Loaded is true if it worked.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** creation name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Load Creation runs when a wire reaches this pin. | — |
| flow | out | Flow | After Load Creation finishes, the flow continues from here to the next step. | — |
| Loaded | out | Bool | True when the load worked. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Put Load Creation on the flow after an event such as On Start. The chain runs when that event fires.

### Add Item

`AddItem`

Puts this item in the bag. Count defaults to 1; Left is how many did not fit.

**Requires:** Needs an Inventory on this object (or on its root). Runs in Play mode.

**Setting:** item. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Add Item runs when a wire reaches this pin. | — |
| flow | out | Flow | After Add Item finishes, the flow continues from here to the next step. | — |
| Count | in | Number | How many. It is a Number. Unwired, this is 1. | 1 |
| Left | out | Number | How many did not fit, or how much budget is left. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Put Add Item after an event such as On Start or On Update, and wire a value into Count.

### Remove Item

`RemoveItem`

Takes Count (default 1) of this item out of the bag, all or nothing. Removed is true if it worked.

**Requires:** Needs an Inventory on this object (or on its root). Runs in Play mode.

**Setting:** item. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Remove Item runs when a wire reaches this pin. | — |
| flow | out | Flow | After Remove Item finishes, the flow continues from here to the next step. | — |
| Count | in | Number | How many. It is a Number. Unwired, this is 1. | 1 |
| Removed | out | Bool | True when the items were taken out. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Put Remove Item after an event such as On Start or On Update, and wire a value into Count.

### Has Item?

`HasItem`

True if the bag holds at least Count (default 1) of this item.

**Requires:** Needs an Inventory on this object (or on its root).

**Setting:** item. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Count | in | Number | How many. It is a Number. Unwired, this is 1. | 1 |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Has Item?'s Bool output into another node's input. It has no flow of its own.

### Item Count

`ItemCount`

How many of this item the bag holds.

**Requires:** Needs an Inventory on this object (or on its root). The count is 0 when there is none.

**Setting:** item. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Count | out | Number | How many. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Item Count's Count output into another node's input. It has no flow of its own.

### Equip

`EquipItem`

Moves this item from the bag onto the equipment slot named in Slot (head, ...). Equipped is true if it worked. Needs an Equipment component.

**Requires:** Needs an Equipment component, and an Inventory to take the item from. Runs in Play mode.

**Setting:** item. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Equip runs when a wire reaches this pin. | — |
| flow | out | Flow | After Equip finishes, the flow continues from here to the next step. | — |
| Slot | in | Text | The equipment slot name, such as head or main hand. It is a Text. Unwired, this is empty text. | empty text |
| Equipped | out | Bool | True when the item was equipped. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Put Equip after an event such as On Start or On Update, and wire a value into Slot.

### Craft

`CraftItem`

Crafts the recipe that makes this item, Times (default 1) times. Crafted is true if it worked. Needs a Crafting component.

**Requires:** Needs a Crafting component on this object. Runs in Play mode.

**Setting:** recipe output item. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Craft runs when a wire reaches this pin. | — |
| flow | out | Flow | After Craft finishes, the flow continues from here to the next step. | — |
| Times | in | Number | How many times to craft. It is a Number. Unwired, this is 1. | 1 |
| Crafted | out | Bool | True when the craft worked. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Put Craft after an event such as On Start or On Update, and wire a value into Times.

### Loot Roll

`LootRoll`

Rolls this loot table into the bag; what does not fit drops next to the object. Rolled is how many items came up.

**Requires:** Needs an Inventory on this object (or on its root). Runs in Play mode.

**Setting:** loot table. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Loot Roll runs when a wire reaches this pin. | — |
| flow | out | Flow | After Loot Roll finishes, the flow continues from here to the next step. | — |
| Rolled | out | Number | How many items the roll produced. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Put Loot Roll on the flow after an event such as On Start. The chain runs when that event fires.

### Spawn Pickup

`SpawnPickup`

Drops Count (default 1) of this item into the world at X, Y, Z. Name is the new pickup object's name.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** item. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Spawn Pickup runs when a wire reaches this pin. | — |
| flow | out | Flow | After Spawn Pickup finishes, the flow continues from here to the next step. | — |
| Count | in | Number | How many. It is a Number. Unwired, this is 1. | 1 |
| X | in | Number | The X part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Y | in | Number | The Y part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Z | in | Number | The Z part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Name | out | Text | The name. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Put Spawn Pickup after an event such as On Start or On Update, and wire a value into Count.

### Set Object Var

`SetObjectVar`

Stores Value in a variable on another object, named in Target.

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** variable. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Object Var runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Object Var finishes, the flow continues from here to the next step. | — |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Value | in | Any | The value being stored, sent or tested. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put Set Object Var after an event such as On Start or On Update, and wire a value into Target.

### Get Object Var

`GetObjectVar`

Reads a variable from another object, named in Target.

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** variable. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire Get Object Var's Value output into another node's input. It has no flow of its own.

### Save Value

`SaveValue`

Saves Value under a key on the player's computer, so it is still there next time the game runs.

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** key. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Save Value runs when a wire reaches this pin. | — |
| flow | out | Flow | After Save Value finishes, the flow continues from here to the next step. | — |
| Value | in | Any | The value being stored, sent or tested. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put Save Value after an event such as On Start or On Update, and wire a value into Value.

### Load Value

`LoadValue`

Loads a value saved with Save Value. Default is used when nothing has been saved yet.

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** key. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Default | in | Any | Used when nothing has been saved under this key yet. It is an Any. Unwired, this is nothing. | nothing |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire Load Value's Value output into another node's input. It has no flow of its own.

### Format Text

`Format`

Builds text from a pattern, e.g. "Score: {0}  Lives: {1}".

**Requires:** No component is required. It only computes a value from its inputs.

**Setting:** Score: {0}. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| {0} | in | Any | The value that replaces {0} in the pattern. It is an Any. Unwired, this is nothing. | nothing |
| {1} | in | Any | The value that replaces {1} in the pattern. It is an Any. Unwired, this is nothing. | nothing |
| {2} | in | Any | The value that replaces {2} in the pattern. It is an Any. Unwired, this is nothing. | nothing |
| Text | out | Text | The text result. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Format Text's Text output into another node's input. It has no flow of its own.

### Substring

`Substring`

Part of the text: Length characters starting at Start (0 = first).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Text | in | Text | The text to read or change. It is a Text. Unwired, this is empty text. | empty text |
| Start | in | Number | Where the effect begins, in world units from the camera. It is a Number. Unwired, this is 0. | 0 |
| Length | in | Number | The length of one back-and-forth or wrap. It is a Number. Unwired, this is 0. | 0 |
| Text | out | Text | The text result. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Substring's Text output into another node's input. It has no flow of its own.

### Replace Text

`Replace`

Replaces every Find in the text With something else.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Text | in | Text | The text to read or change. It is a Text. Unwired, this is empty text. | empty text |
| Find | in | Text | The text to look for. It is a Text. Unwired, this is empty text. | empty text |
| With | in | Text | The text to put in its place. It is a Text. Unwired, this is empty text. | empty text |
| Text | out | Text | The text result. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Replace Text's Text output into another node's input. It has no flow of its own.

### To Number

`ToNumber`

Turns text like "42" into the number 42.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Text | in | Text | The text to read or change. It is a Text. Unwired, this is empty text. | empty text |
| Number | out | Number | What To Number produces on Number. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire To Number's Number output into another node's input. It has no flow of its own.

### Starts With

`StartsWith`

True when the text starts with Prefix.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Text | in | Text | The text to read or change. It is a Text. Unwired, this is empty text. | empty text |
| Prefix | in | Text | The beginning to test. It is a Text. Unwired, this is empty text. | empty text |
| Yes | out | Bool | True when the test passes. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Starts With's Yes output into another node's input. It has no flow of its own.

### Ends With

`EndsWith`

True when the text ends with Suffix.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Text | in | Text | The text to read or change. It is a Text. Unwired, this is empty text. | empty text |
| Suffix | in | Text | The ending to test. It is a Text. Unwired, this is empty text. | empty text |
| Yes | out | Bool | True when the test passes. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire Ends With's Yes output into another node's input. It has no flow of its own.

### Trim Spaces

`Trim`

Removes spaces from the start and end of text.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Text | in | Text | The text to read or change. It is a Text. Unwired, this is empty text. | empty text |
| Text | out | Text | The text result. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Trim Spaces's Text output into another node's input. It has no flow of its own.

### Index Of

`IndexOf`

Where Find first appears in the text (-1 if it's not there).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Text | in | Text | The text to read or change. It is a Text. Unwired, this is empty text. | empty text |
| Find | in | Text | The text to look for. It is a Text. Unwired, this is empty text. | empty text |
| Index | out | Number | The current number, starting at 0. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Index Of's Index output into another node's input. It has no flow of its own.

### Localize

`Localize`

The key's text in the player's language (the Localization window's string table), with {0}..{2} filled from the inputs; a number also picks the plural form.

**Requires:** No component is required. It only computes a value from its inputs.

**Setting:** key. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| {0} | in | Any | The value that replaces {0} in the pattern. It is an Any. Unwired, this is nothing. | nothing |
| {1} | in | Any | The value that replaces {1} in the pattern. It is an Any. Unwired, this is nothing. | nothing |
| {2} | in | Any | The value that replaces {2} in the pattern. It is an Any. Unwired, this is nothing. | nothing |
| Text | out | Text | The text result. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Localize's Text output into another node's input. It has no flow of its own.

### Get Language

`GetLanguage`

The current language's code (en, fr...).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Code | out | Text | The language code, such as en. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Get Language's Code output into another node's input. It has no flow of its own.

### Set Language

`SetLanguage`

Shows every localized text in another language of the string table and remembers the choice for the next run. Wire a code into Code, or type it.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** code (fr, de...). The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Language runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Language finishes, the flow continues from here to the next step. | — |
| Code | in | Text | A language code, such as en or fr. It is a Text. Unwired, this is empty text. | empty text |

**Example:** Put Set Language after an event such as On Start or On Update, and wire a value into Code.

### On Language Changed

`OnLanguageChanged`

Runs when the player switches language. Event Name is the new language's code.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Language Changed finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Language Changed into the graph and wire it the way its pins describe.

### Perlin Noise

`Noise`

Smooth random (Perlin) noise for X, Y. Nearby inputs give similar values; good for wobble and terrain.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| X | in | Number | The X part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Y | in | Number | The Y part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Value | out | Number | The value that was read. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Perlin Noise's Value output into another node's input. It has no flow of its own.

### Snap To Step

`Snap`

Rounds Value to the nearest multiple of Step (grid snapping).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | in | Number | The value being stored, sent or tested. It is a Number. Unwired, this is 0. | 0 |
| Step | in | Number | The grid size to snap to. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Snap To Step's Out output into another node's input. It has no flow of its own.

### Wrap

`Wrap`

Wraps Value back into Min..Max, so going past Max starts again at Min.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | in | Number | The value being stored, sent or tested. It is a Number. Unwired, this is 0. | 0 |
| Min | in | Number | The low end of the range. It is a Number. Unwired, this is 0. | 0 |
| Max | in | Number | The high end of the range. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Wrap's Out output into another node's input. It has no flow of its own.

### Average

`Average`

Halfway between A and B.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| A | in | Number | Opacity, from 0 (invisible) to 1 (solid). It is a Number. Unwired, this is 0. | 0 |
| B | in | Number | Blue, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| Out | out | Number | The result. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Average's Out output into another node's input. It has no flow of its own.

### Distance 2D

`Distance2D`

The distance between two points (X1, Y1) and (X2, Y2).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| X1 | in | Number | The X of the first point. It is a Number. Unwired, this is 0. | 0 |
| Y1 | in | Number | The Y of the first point. It is a Number. Unwired, this is 0. | 0 |
| X2 | in | Number | The X of the second point. It is a Number. Unwired, this is 0. | 0 |
| Y2 | in | Number | The Y of the second point. It is a Number. Unwired, this is 0. | 0 |
| Distance | out | Number | How far away the hit is. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Distance 2D's Distance output into another node's input. It has no flow of its own.

### Find With Tag

`FindWithTag`

The name of an object with this tag.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

**Setting:** tag. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Object | out | Object | The object that was found, or nothing. It is an Object. Wire it into an input that expects an Object. | — |

**Example:** Wire Find With Tag's Object output into another node's input. It has no flow of its own.

### Count With Tag

`CountWithTag`

How many objects have this tag (e.g. enemies left).

**Requires:** No extra component is required. It reads this object's Transform (or Target).

**Setting:** tag. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Count | out | Number | How many. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Count With Tag's Count output into another node's input. It has no flow of its own.

### Nearest With Tag

`NearestWithTag`

The name of the closest object with this tag. Good for targeting enemies or pickups.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

**Setting:** tag. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Object | out | Object | The object that was found, or nothing. It is an Object. Wire it into an input that expects an Object. | — |

**Example:** Wire Nearest With Tag's Object output into another node's input. It has no flow of its own.

### Teleport

`Teleport`

Moves an object (this one by default) straight to X, Y, Z. Leave an input empty to keep that axis.

**Requires:** No extra component is required. It moves this object's Transform (or Target). It runs in Play mode.

**Setting:** object (self). The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Teleport runs when a wire reaches this pin. | — |
| flow | out | Flow | After Teleport finishes, the flow continues from here to the next step. | — |
| X | in | Number | The X part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Y | in | Number | The Y part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Z | in | Number | The Z part, in world units. It is a Number. Unwired, this is 0. | 0 |
| Object | in | Object | An object reference, or its name. It is an Object. Unwired, this is no object. | no object |

**Example:** Put Teleport after an event such as On Start or On Update, and wire a value into X.

### Set Visible

`SetVisible`

Shows or hides this object's mesh or sprite. Its scripts and collisions keep running.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Visible runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Visible finishes, the flow continues from here to the next step. | — |
| Visible | in | Bool | True shows it, false hides it. It is a Bool. Unwired, this is true (shown). | true (shown) |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |

**Example:** Put Set Visible after an event such as On Start or On Update, and wire a value into Visible.

### Pause Game

`PauseGame`

Pauses or resumes the game (Paused true / false).

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Pause Game runs when a wire reaches this pin. | — |
| flow | out | Flow | After Pause Game finishes, the flow continues from here to the next step. | — |
| Paused | in | Bool | True pauses, false resumes. It is a Bool. Unwired, this is true (paused). | true (paused) |

**Example:** Put Pause Game after an event such as On Start or On Update, and wire a value into Paused.

### Quit Game

`QuitGame`

Quit Game — Closes the game.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Quit Game runs when a wire reaches this pin. | — |
| flow | out | Flow | After Quit Game finishes, the flow continues from here to the next step. | — |

**Example:** Put Quit Game on the flow after an event such as On Start. The chain runs when that event fires.

### Show Cursor

`ShowCursor`

Shows or hides the mouse cursor.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Show Cursor runs when a wire reaches this pin. | — |
| flow | out | Flow | After Show Cursor finishes, the flow continues from here to the next step. | — |
| Visible | in | Bool | True shows it, false hides it. It is a Bool. Unwired, this is true (cursor shown). | true (cursor shown) |

**Example:** Put Show Cursor after an event such as On Start or On Update, and wire a value into Visible.

### Random Branch

`ChanceBranch`

Goes out True with the given chance (0 to 1, e.g. 0.25 = 25%), otherwise False.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Random Branch runs when a wire reaches this pin. | — |
| True | out | Flow | The flow continues out of True. Wire this to the step that should run for that case. | — |
| False | out | Flow | The flow continues out of False. Wire this to the step that should run for that case. | — |
| Chance | in | Number | The odds from 0 to 1. 0.25 is 25 percent. It is a Number. Unwired, this is 0.5 (a coin flip). | 0.5 (a coin flip) |

**Example:** Put Random Branch after an event such as On Start or On Update, and wire a value into Chance.

### Counter

`Counter`

Counts how many times it has run (read it from Count). Wire true into Reset to start again from 0.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Counter runs when a wire reaches this pin. | — |
| flow | out | Flow | After Counter finishes, the flow continues from here to the next step. | — |
| Reset | in | Bool | True starts the count again at 0. It is a Bool. Unwired, this is false. | false |
| Count | out | Number | How many. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Put Counter after an event such as On Start or On Update, and wire a value into Reset.

### Every N Times

`EveryN`

Lets the flow through every Nth time it runs (e.g. N = 3: every third time).

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Every N Times runs when a wire reaches this pin. | — |
| flow | out | Flow | After Every N Times finishes, the flow continues from here to the next step. | — |
| N | in | Number | How many times between each pass. It is a Number. Unwired, this is 2. | 2 |

**Example:** Put Every N Times after an event such as On Start or On Update, and wire a value into N.

### Show UI

`ShowUI`

Shows a UI object, playing its entrance animation if it has one.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** UI object (self). The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Show UI runs when a wire reaches this pin. | — |
| flow | out | Flow | After Show UI finishes, the flow continues from here to the next step. | — |

**Example:** Put Show UI on the flow after an event such as On Start. The chain runs when that event fires.

### Hide UI

`HideUI`

Hides a UI object, playing its exit animation if it has one.

**Requires:** No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.

**Setting:** UI object (self). The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Hide UI runs when a wire reaches this pin. | — |
| flow | out | Flow | After Hide UI finishes, the flow continues from here to the next step. | — |

**Example:** Put Hide UI on the flow after an event such as On Start. The chain runs when that event fires.

### On Trigger Stay

`OnTriggerStay`

Runs every frame while something stays inside this object's trigger area (healing zones, lava).

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Trigger Stay finishes, the flow continues from here to the next step. | — |

**Example:** Drop On Trigger Stay into the graph and wire it the way its pins describe.

### Raycast 3D

`Raycast3D`

Shoots an invisible line (from this object, forward, unless you wire Origin / Direction) and tells you what it hit, where, and how far away.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

**Setting:** layers (blank = all). The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Origin | in | Vector3 | Where the cast starts. Unwired, it starts at this object. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Direction | in | Vector3 | Which way the cast points. Unwired, it points along this object's forward. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Max Dist | in | Number | How far the cast may travel. It is a Number. Unwired, this is 0. | 0 |
| Ignore Triggers | in | Bool | True skips trigger colliders. It is a Bool. Unwired, this is false. | false |
| Hit | out | Bool | True when something was hit. It is a Bool. Wire it into an input that expects a Bool. | — |
| Point | out | Vector3 | Where the hit happened. It is a Vector3. Wire it into an input that expects a Vector3. | — |
| Normal | out | Vector3 | The direction the surface faces at the hit. It is a Vector3. Wire it into an input that expects a Vector3. | — |
| Distance | out | Number | How far away the hit is. It is a Number. Wire it into an input that expects a Number. | — |
| Object | out | Object | The object that was found, or nothing. It is an Object. Wire it into an input that expects an Object. | — |

**Example:** Wire Raycast 3D's Hit output into another node's input. It has no flow of its own.

**Related:** Sphere Cast, Raycast All 3D, Overlap Sphere

### Sphere Cast

`SphereCast3D`

Like Raycast 3D but with a thick ball instead of a thin line, so it can't slip through small gaps.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

**Setting:** layers (blank = all). The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Origin | in | Vector3 | Where the cast starts. Unwired, it starts at this object. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Direction | in | Vector3 | Which way the cast points. Unwired, it points along this object's forward. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Radius | in | Number | The thickness of the ball, in world units. It is a Number. Unwired, this is 0. | 0 |
| Max Dist | in | Number | How far the cast may travel. It is a Number. Unwired, this is 0. | 0 |
| Hit | out | Bool | True when something was hit. It is a Bool. Wire it into an input that expects a Bool. | — |
| Point | out | Vector3 | Where the hit happened. It is a Vector3. Wire it into an input that expects a Vector3. | — |
| Normal | out | Vector3 | The direction the surface faces at the hit. It is a Vector3. Wire it into an input that expects a Vector3. | — |
| Distance | out | Number | How far away the hit is. It is a Number. Wire it into an input that expects a Number. | — |
| Object | out | Object | The object that was found, or nothing. It is an Object. Wire it into an input that expects an Object. | — |

**Example:** Wire Sphere Cast's Hit output into another node's input. It has no flow of its own.

### Raycast All 3D

`RaycastAll3D`

Everything a line passes through, nearest first: how many, their names, and the closest one.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

**Setting:** layers (blank = all). The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Origin | in | Vector3 | Where the cast starts. Unwired, it starts at this object. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Direction | in | Vector3 | Which way the cast points. Unwired, it points along this object's forward. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Max Dist | in | Number | How far the cast may travel. It is a Number. Unwired, this is 0. | 0 |
| Count | out | Number | How many. It is a Number. Wire it into an input that expects a Number. | — |
| Objects | out | List | The objects that were found, as a list. It is a List. Wire it into an input that expects a List. | — |
| Nearest | out | Object | The closest object. It is an Object. Wire it into an input that expects an Object. | — |

**Example:** Wire Raycast All 3D's Count output into another node's input. It has no flow of its own.

### Overlap Sphere

`OverlapSphere3D`

The objects inside a ball around a point (this object if Center is unwired). Great for explosions and pickups nearby.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

**Setting:** layers (blank = all). The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Center | in | Vector3 | The middle of the volume. Unwired, it is this object's position. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Radius | in | Number | The thickness of the ball, in world units. It is a Number. Unwired, this is 0. | 0 |
| Count | out | Number | How many. It is a Number. Wire it into an input that expects a Number. | — |
| Objects | out | List | The objects that were found, as a list. It is a List. Wire it into an input that expects a List. | — |

**Example:** Wire Overlap Sphere's Count output into another node's input. It has no flow of its own.

### Overlap Box

`OverlapBox3D`

The objects inside a box around a point. Half Size is half the box's width, height and depth.

**Requires:** No component is required. It reads the player or the clock while the game is playing.

**Setting:** layers (blank = all). The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Center | in | Vector3 | The middle of the volume. Unwired, it is this object's position. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Half Size | in | Vector3 | Half the box's width, height and depth. It is a Vector3. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Count | out | Number | How many. It is a Number. Wire it into an input that expects a Number. | — |
| Objects | out | List | The objects that were found, as a list. It is a List. Wire it into an input that expects a List. | — |

**Example:** Wire Overlap Box's Count output into another node's input. It has no flow of its own.

### Contact Point

`ContactPoint`

Where the collision or trigger touch happened, as a point in the world.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Point | out | Vector3 | Where the hit happened. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Contact Point's Point output into another node's input. It has no flow of its own.

### Contact Normal

`ContactNormal`

Which way the touched surface faces at the contact (a direction pointing at the other object).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Normal | out | Vector3 | The direction the surface faces at the hit. It is a Vector3. Wire it into an input that expects a Vector3. | — |

**Example:** Wire Contact Normal's Normal output into another node's input. It has no flow of its own.

### Impact Speed

`ImpactSpeed`

How hard the collision was: the speed the two objects hit each other at (0 for triggers).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Speed | out | Number | How hard the two objects hit. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Impact Speed's Speed output into another node's input. It has no flow of its own.

### Self

`Self`

This object, the one the graph is on. Wire it into any Target or Object input.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Object | out | Object | The object that was found, or nothing. It is an Object. Wire it into an input that expects an Object. | — |

**Example:** Wire Self's Object output into another node's input. It has no flow of its own.

### Other Object

`Other`

The other object from the collision, trigger or mouse event that started this chain. Wire it into a Target input to act on what you hit.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Object | out | Object | The object that was found, or nothing. It is an Object. Wire it into an input that expects an Object. | — |

**Example:** Wire Other Object's Object output into another node's input. It has no flow of its own.

### Find Object

`FindObject`

The object with the name you type. Wire it into a Target input. Empty when no object has that name.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

**Setting:** object name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Object | out | Object | The object that was found, or nothing. It is an Object. Wire it into an input that expects an Object. | — |

**Example:** Wire Find Object's Object output into another node's input. It has no flow of its own.

### Get Name

`GetName`

The name of this object, or of Target when that pin is wired.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Name | out | Text | The name. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Get Name's Name output into another node's input. It has no flow of its own.

### Get Tag

`GetTag`

The tag of this object, or of Target when that pin is wired.

**Requires:** No extra component is required. It reads this object's Transform (or Target).

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object to act on. Leave it empty to use this object (the one the graph is on). It is an Object. Unwired, this is this object. | this object |
| Tag | out | Text | The tag. It is a Text. Wire it into an input that expects a Text. | — |

**Example:** Wire Get Tag's Tag output into another node's input. It has no flow of its own.

### Function

`FunctionStart`

The start of a function you can call from anywhere in this graph. The text is its signature: Name(param: Type) -> result: Type.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

**Setting:** Name(a: Number) -> out: Number. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After Function finishes, the flow continues from here to the next step. | — |

**Example:** Drop Function into the graph and wire it the way its pins describe.

### Return

`Return`

Sends the flow back to the Call Function that ran this function, along with the return values.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

**Setting:** Which function, chosen from the functions in this graph.. Which function, chosen from the functions in this graph.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Return runs when a wire reaches this pin. | — |

**Example:** Put Return on the flow after an event such as On Start. The chain runs when that event fires.

### Call Function

`CallFunction`

Runs a function defined by a Function node in this graph, then continues. Pick the function in the node.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

**Setting:** Which function, chosen from the functions in this graph.. Which function, chosen from the functions in this graph.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Call Function runs when a wire reaches this pin. | — |
| flow | out | Flow | After Call Function finishes, the flow continues from here to the next step. | — |

**Example:** Put Call Function on the flow after an event such as On Start. The chain runs when that event fires.

**Related:** Function, Return, Set Local

### Set Local

`SetLocal`

Stores Value in a local variable of the function that is running. Locals exist only while that call runs.

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Local runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Local finishes, the flow continues from here to the next step. | — |
| Value | in | Any | The value being stored, sent or tested. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put Set Local after an event such as On Start or On Update, and wire a value into Value.

### Get Local

`GetLocal`

Reads a local variable of the function that is running.

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire Get Local's Value output into another node's input. It has no flow of its own.

### While

`While`

Runs Body again and again while Cond is true (checked before each run), then runs Done.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. While runs when a wire reaches this pin. | — |
| Body | out | Flow | The flow continues out of Body. Wire this to the step that should run for that case. | — |
| Done | out | Flow | The flow continues out of Done. Wire this to the step that should run for that case. | — |
| Cond | in | Bool | The condition. True and false take different paths. It is a Bool. Unwired, this is false. | false |

**Example:** Put While after an event such as On Start or On Update, and wire a value into Cond.

### For Each

`ForEach`

Runs Body once for every item in a list (Item and Index say which), then runs Done.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. For Each runs when a wire reaches this pin. | — |
| Body | out | Flow | The flow continues out of Body. Wire this to the step that should run for that case. | — |
| Done | out | Flow | The flow continues out of Done. Wire this to the step that should run for that case. | — |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |
| Item | out | Any | The current list entry. It is an Any. Wire it into an input that expects an Any. | — |
| Index | out | Number | The current number, starting at 0. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Put For Each after an event such as On Start or On Update, and wire a value into List.

### Break

`Break`

Leaves the loop whose Body this is; the loop goes on to Done.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Break runs when a wire reaches this pin. | — |

**Example:** Put Break on the flow after an event such as On Start. The chain runs when that event fires.

### Continue

`Continue`

Skips the rest of this run of a loop's Body; the loop goes on to the next item.

**Requires:** No component is required. It changes the order of the flow. It runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Continue runs when a wire reaches this pin. | — |

**Example:** Put Continue on the flow after an event such as On Start. The chain runs when that event fires.

### New List

`NewList`

A new, empty list. Keep it in a variable, then List Add to it. The text field can name the item type (Number, Vector3...) so the checker catches wrong items.

**Requires:** No component is required. It only computes a value from its inputs.

**Setting:** item type (optional). The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| List | out | List | The new list. It is a List. Wire it into an input that expects a List. | — |

**Example:** Wire New List's List output into another node's input. It has no flow of its own.

### List Add

`ListAdd`

Adds an item to the end of a list.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. List Add runs when a wire reaches this pin. | — |
| flow | out | Flow | After List Add finishes, the flow continues from here to the next step. | — |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |
| Item | in | Any | One entry of the list. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put List Add after an event such as On Start or On Update, and wire a value into List.

### List Remove At

`ListRemoveAt`

Removes the item at an index (the first is 0).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. List Remove At runs when a wire reaches this pin. | — |
| flow | out | Flow | After List Remove At finishes, the flow continues from here to the next step. | — |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |
| Index | in | Number | Which entry. The first is 0. It is a Number. Unwired, this is 0. | 0 |

**Example:** Put List Remove At after an event such as On Start or On Update, and wire a value into List.

### List Get

`ListGet`

The item at an index (the first is 0).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |
| Index | in | Number | Which entry. The first is 0. It is a Number. Unwired, this is 0. | 0 |
| Item | out | Any | The current list entry. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire List Get's Item output into another node's input. It has no flow of its own.

### List Length

`ListLength`

How many items a list has.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |
| Count | out | Number | How many. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire List Length's Count output into another node's input. It has no flow of its own.

### List Contains

`ListContains`

True when the list has this item.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |
| Item | in | Any | One entry of the list. It is an Any. Unwired, this is nothing. | nothing |
| Bool | out | Bool | True or false. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire List Contains's Bool output into another node's input. It has no flow of its own.

### List Insert

`ListInsert`

Puts an item into a list at an index (0 = the front); the items after it move up. An index past the end is reported and nothing changes.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. List Insert runs when a wire reaches this pin. | — |
| flow | out | Flow | After List Insert finishes, the flow continues from here to the next step. | — |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |
| Index | in | Number | Which entry. The first is 0. It is a Number. Unwired, this is 0. | 0 |
| Item | in | Any | One entry of the list. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put List Insert after an event such as On Start or On Update, and wire a value into List.

### List Set

`ListSet`

Replaces the item at an index. An index outside the list is reported and nothing changes.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. List Set runs when a wire reaches this pin. | — |
| flow | out | Flow | After List Set finishes, the flow continues from here to the next step. | — |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |
| Index | in | Number | Which entry. The first is 0. It is a Number. Unwired, this is 0. | 0 |
| Item | in | Any | One entry of the list. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put List Set after an event such as On Start or On Update, and wire a value into List.

### List Find

`ListFind`

The index of the first item equal to Item (-1 and Found = false when it isn't there).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |
| Item | in | Any | One entry of the list. It is an Any. Unwired, this is nothing. | nothing |
| Index | out | Number | The current number, starting at 0. It is a Number. Wire it into an input that expects a Number. | — |
| Found | out | Bool | What List Find produces on Found. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Wire List Find's Index output into another node's input. It has no flow of its own.

### List Remove Item

`ListRemoveItem`

Removes the first item equal to Item. Removed says whether there was one.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. List Remove Item runs when a wire reaches this pin. | — |
| flow | out | Flow | After List Remove Item finishes, the flow continues from here to the next step. | — |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |
| Item | in | Any | One entry of the list. It is an Any. Unwired, this is nothing. | nothing |
| Removed | out | Bool | True when the items were taken out. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Put List Remove Item after an event such as On Start or On Update, and wire a value into List.

### List Sort

`ListSort`

Sorts the list in place, smallest first (Descending = largest first). Numbers sort as numbers, text alphabetically, mixed lists by kind.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. List Sort runs when a wire reaches this pin. | — |
| flow | out | Flow | After List Sort finishes, the flow continues from here to the next step. | — |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |
| Descending | in | Bool | The Descending input of List Sort. It is a Bool. Unwired, this is false. | false |

**Example:** Put List Sort after an event such as On Start or On Update, and wire a value into List.

### List Reverse

`ListReverse`

Reverses the list in place.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. List Reverse runs when a wire reaches this pin. | — |
| flow | out | Flow | After List Reverse finishes, the flow continues from here to the next step. | — |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |

**Example:** Put List Reverse after an event such as On Start or On Update, and wire a value into List.

### List Clear

`ListClear`

Removes every item from the list.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. List Clear runs when a wire reaches this pin. | — |
| flow | out | Flow | After List Clear finishes, the flow continues from here to the next step. | — |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |

**Example:** Put List Clear after an event such as On Start or On Update, and wire a value into List.

### List Copy

`ListCopy`

A new list with the same items. Deep also copies lists and maps inside it; without it they stay shared.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |
| Deep | in | Bool | The Deep input of List Copy. It is a Bool. Unwired, this is false. | false |
| Copy | out | List | What List Copy produces on Copy. It is a List. Wire it into an input that expects a List. | — |

**Example:** Wire List Copy's Copy output into another node's input. It has no flow of its own.

### Map Clear

`MapClear`

Removes every entry from the map.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Map Clear runs when a wire reaches this pin. | — |
| flow | out | Flow | After Map Clear finishes, the flow continues from here to the next step. | — |
| Map | in | Map | The Map input of Map Clear. It is a Map. Unwired, this is an empty map. | an empty map |

**Example:** Put Map Clear after an event such as On Start or On Update, and wire a value into Map.

### Map Count

`MapCount`

How many entries a map has.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Map | in | Map | The Map input of Map Count. It is a Map. Unwired, this is an empty map. | an empty map |
| Count | out | Number | How many. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Map Count's Count output into another node's input. It has no flow of its own.

### Map Copy

`MapCopy`

A new map with the same entries. Deep also copies lists and maps inside it; without it they stay shared.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Map | in | Map | The Map input of Map Copy. It is a Map. Unwired, this is an empty map. | an empty map |
| Deep | in | Bool | The Deep input of Map Copy. It is a Bool. Unwired, this is false. | false |
| Copy | out | Map | What Map Copy produces on Copy. It is a Map. Wire it into an input that expects a Map. | — |

**Example:** Wire Map Copy's Copy output into another node's input. It has no flow of its own.

### List Append

`ListAppend`

Adds every item of Other to the end of List. Appending a list to itself doubles it.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. List Append runs when a wire reaches this pin. | — |
| flow | out | Flow | After List Append finishes, the flow continues from here to the next step. | — |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |
| Other | in | List | The Other input of List Append. It is a List. Unwired, this is an empty list. | an empty list |

**Example:** Put List Append after an event such as On Start or On Update, and wire a value into List.

### List Slice

`ListSlice`

A new list with Count items starting at Start (nothing wired into Count means everything from Start). Start past the end of the list is reported.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |
| Start | in | Number | Where the effect begins, in world units from the camera. It is a Number. Unwired, this is 0. | 0 |
| Count | in | Number | How many. It is a Number. Unwired, this is 0. | 0 |
| Slice | out | List | What List Slice produces on Slice. It is a List. Wire it into an input that expects a List. | — |

**Example:** Wire List Slice's Slice output into another node's input. It has no flow of its own.

### List Pop

`ListPop`

Removes the last item of a list and gives it back. On an empty list Popped is false and nothing changes.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. List Pop runs when a wire reaches this pin. | — |
| flow | out | Flow | After List Pop finishes, the flow continues from here to the next step. | — |
| List | in | List | The list to read or change. It is a List. Unwired, this is an empty list. | an empty list |
| Item | out | Any | The current list entry. It is an Any. Wire it into an input that expects an Any. | — |
| Popped | out | Bool | What List Pop produces on Popped. It is a Bool. Wire it into an input that expects a Bool. | — |

**Example:** Put List Pop after an event such as On Start or On Update, and wire a value into List.

### Map Get Or

`MapGetOr`

The value stored under Key, or Default when the map has no such key.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Map | in | Map | The Map input of Map Get Or. It is a Map. Unwired, this is an empty map. | an empty map |
| Key | in | Text | The Key input of Map Get Or. It is a Text. Unwired, this is empty text. | empty text |
| Default | in | Any | Used when nothing has been saved under this key yet. It is an Any. Unwired, this is nothing. | nothing |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire Map Get Or's Value output into another node's input. It has no flow of its own.

### Map Merge

`MapMerge`

Copies every entry of From into Into. A key in both takes the value from From.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Map Merge runs when a wire reaches this pin. | — |
| flow | out | Flow | After Map Merge finishes, the flow continues from here to the next step. | — |
| Into | in | Map | The Into input of Map Merge. It is a Map. Unwired, this is an empty map. | an empty map |
| From | in | Map | The angle you are coming from, in degrees. It is a Map. Unwired, this is an empty map. | an empty map |

**Example:** Put Map Merge after an event such as On Start or On Update, and wire a value into Into.

### New Struct

`StructNew`

A new struct of the type named in the text field, every field at its default. Declare structs in the Variables panel.

**Requires:** No component is required. It only computes a value from its inputs.

**Setting:** struct name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Struct | out | Map | What New Struct produces on Struct. It is a Map. Wire it into an input that expects a Map. | — |

**Example:** Wire New Struct's Struct output into another node's input. It has no flow of its own.

### Get Field

`StructGet`

Reads one field of a struct. A field the struct doesn't have is reported.

**Requires:** No component is required. It only computes a value from its inputs.

**Setting:** field. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Struct | in | Map | The Struct input of Get Field. It is a Map. Unwired, this is an empty map. | an empty map |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire Get Field's Value output into another node's input. It has no flow of its own.

### Set Field

`StructSet`

Sets one field of a struct. The value must fit the field's type, or it is reported and nothing changes.

**Requires:** No component is required. It only computes a value from its inputs.

**Setting:** field. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Field runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Field finishes, the flow continues from here to the next step. | — |
| Struct | in | Map | The Struct input of Set Field. It is a Map. Unwired, this is an empty map. | an empty map |
| Value | in | Any | The value being stored, sent or tested. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put Set Field after an event such as On Start or On Update, and wire a value into Struct.

### Make Color

`MakeColor`

A colour from red, green, blue and alpha, each 0 to 1 (alpha defaults to 1).

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| R | in | Number | Red, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| G | in | Number | Green, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| B | in | Number | Blue, from 0 to 1. It is a Number. Unwired, this is 0. | 0 |
| A | in | Number | Opacity, from 0 (invisible) to 1 (solid). It is a Number. Unwired, this is 0. | 0 |
| Color | out | Color | What Make Color produces on Color. It is a Color. Wire it into an input that expects a Color. | — |

**Example:** Wire Make Color's Color output into another node's input. It has no flow of its own.

### Split Color

`SplitColor`

The red, green, blue and alpha of a colour, each 0 to 1.

**Requires:** No component is required. It only computes a value from its inputs.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Color | in | Color | The Color input of Split Color. It is a Color. | — |
| R | out | Number | What Split Color produces on R. It is a Number. Wire it into an input that expects a Number. | — |
| G | out | Number | What Split Color produces on G. It is a Number. Wire it into an input that expects a Number. | — |
| B | out | Number | What Split Color produces on B. It is a Number. Wire it into an input that expects a Number. | — |
| A | out | Number | What Split Color produces on A. It is a Number. Wire it into an input that expects a Number. | — |

**Example:** Wire Split Color's R output into another node's input. It has no flow of its own.

### All With Tag

`AllWithTag`

Every active object with this tag, as a list (walk it with For Each).

**Requires:** No extra component is required. It reads this object's Transform (or Target).

**Setting:** tag. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Objects | out | List | The objects that were found, as a list. It is a List. Wire it into an input that expects a List. | — |

**Example:** Wire All With Tag's Objects output into another node's input. It has no flow of its own.

### Get Scene Var

`GetSceneVar`

Reads a scene variable: shared by every graph in this scene.

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire Get Scene Var's Value output into another node's input. It has no flow of its own.

### Set Scene Var

`SetSceneVar`

Sets a scene variable: every graph in this scene sees it.

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Scene Var runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Scene Var finishes, the flow continues from here to the next step. | — |
| Value | in | Any | The value being stored, sent or tested. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put Set Scene Var after an event such as On Start or On Update, and wire a value into Value.

### Add Scene Var

`AddSceneVar`

Adds to a scene variable (a shared score, a counter...).

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Add Scene Var runs when a wire reaches this pin. | — |
| flow | out | Flow | After Add Scene Var finishes, the flow continues from here to the next step. | — |
| Amount | in | Number | How much to add. Negative numbers subtract. It is a Number. Unwired, this is 0. | 0 |

**Example:** Put Add Scene Var after an event such as On Start or On Update, and wire a value into Amount.

### Get Global Var

`GetGlobalVar`

Reads a global variable: kept when another scene loads.

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire Get Global Var's Value output into another node's input. It has no flow of its own.

### Set Global Var

`SetGlobalVar`

Sets a global variable: it's kept when another scene loads.

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Set Global Var runs when a wire reaches this pin. | — |
| flow | out | Flow | After Set Global Var finishes, the flow continues from here to the next step. | — |
| Value | in | Any | The value being stored, sent or tested. It is an Any. Unwired, this is nothing. | nothing |

**Example:** Put Set Global Var after an event such as On Start or On Update, and wire a value into Value.

### Add Global Var

`AddGlobalVar`

Adds to a global variable (total coins across levels...).

**Requires:** No extra component is required. Object variables live on the Visual Script. It runs in Play mode.

**Setting:** name. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. Add Global Var runs when a wire reaches this pin. | — |
| flow | out | Flow | After Add Global Var finishes, the flow continues from here to the next step. | — |
| Amount | in | Number | How much to add. Negative numbers subtract. It is a Number. Unwired, this is 0. | 0 |

**Example:** Put Add Global Var after an event such as On Start or On Update, and wire a value into Amount.

### On Var Changed

`OnVarChanged`

Runs when this object's variable changes, with its new Value and the Old one.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

**Setting:** variable. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Var Changed finishes, the flow continues from here to the next step. | — |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |
| Old | out | Any | The value before it changed. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire On Var Changed's Value output into another node's input. It has no flow of its own.

### On Scene Var Changed

`OnSceneVarChanged`

Runs when a scene variable changes, with its new Value and the Old one.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

**Setting:** variable. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Scene Var Changed finishes, the flow continues from here to the next step. | — |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |
| Old | out | Any | The value before it changed. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire On Scene Var Changed's Value output into another node's input. It has no flow of its own.

### On Global Var Changed

`OnGlobalVarChanged`

Runs when a global variable changes, with its new Value and the Old one.

**Requires:** No component is required. It runs in Play mode when its event happens on this object.

**Setting:** variable. The value typed into the node.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | out | Flow | After On Global Var Changed finishes, the flow continues from here to the next step. | — |
| Value | out | Any | The value that was read. It is an Any. Wire it into an input that expects an Any. | — |
| Old | out | Any | The value before it changed. It is an Any. Wire it into an input that expects an Any. | — |

**Example:** Wire On Global Var Changed's Value output into another node's input. It has no flow of its own.

## Engine API / GameObject

### GameObject.name

`GameObject.name`

The object's name (as shown in the Hierarchy).

**Requires:** Uses this object's GameObject. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose GameObject this call uses. Leave it empty to use this object. It is an Object. | this object |
| name | out | Text | name (Text), the value gameObject.name gives back. | — |

**Example:** In a script this is gameObject.name.

**Related:** Other GameObject nodes

### Set GameObject.name

`GameObject.name=`

The object's name (as shown in the Hierarchy).

**Requires:** Uses this object's GameObject. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| name | in | Text | name (Text), passed into gameObject.name = name. Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose GameObject this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is gameObject.name = name.

**Related:** Other GameObject nodes

### GameObject.tag

`GameObject.tag`

GameObject.tag — The object's tag.

**Requires:** Uses this object's GameObject. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose GameObject this call uses. Leave it empty to use this object. It is an Object. | this object |
| tag | out | Text | tag (Text), the value gameObject.tag gives back. | — |

**Example:** In a script this is gameObject.tag.

**Related:** Other GameObject nodes

### Set GameObject.tag

`GameObject.tag=`

Set GameObject.tag — The object's tag.

**Requires:** Uses this object's GameObject. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| tag | in | Text | tag (Text), passed into gameObject.tag = tag. Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose GameObject this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is gameObject.tag = tag.

**Related:** Other GameObject nodes

### GameObject.active

`GameObject.active`

Whether the object is active. Inactive objects don't update or draw.

**Requires:** Uses this object's GameObject. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose GameObject this call uses. Leave it empty to use this object. It is an Object. | this object |
| active | out | Bool | active (Bool), the value gameObject.active gives back. | — |

**Example:** In a script this is gameObject.active.

**Related:** Other GameObject nodes

### Set GameObject.active

`GameObject.active=`

Whether the object is active. Inactive objects don't update or draw.

**Requires:** Uses this object's GameObject. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| active | in | Bool | active (Bool), passed into gameObject.active = active. Unwired, this is false. | false |
| Target | in | Object | The object whose GameObject this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is gameObject.active = active.

**Related:** Other GameObject nodes

### GameObject.transform

`GameObject.transform`

The object's Transform (position, rotation, scale).

**Requires:** Uses this object's GameObject. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose GameObject this call uses. Leave it empty to use this object. It is an Object. | this object |
| transform | out | Object | transform (Object), the value gameObject.transform gives back. | — |

**Example:** In a script this is gameObject.transform.

**Related:** Other GameObject nodes

### GameObject.Find(name)

`GameObject.Find/1`

The first active object with this name, or null if there is none.

**Requires:** No component on this object is required. GameObject is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| name | in | Text | name (Text), passed into GameObject.Find(name). Unwired, this is empty text. | empty text |
| Result | out | Object | Result (Object), the value GameObject.Find(name) gives back. | — |

**Example:** In a script this is GameObject.Find(name).

**Related:** Other GameObject nodes

### GameObject.FindWithTag(tag)

`GameObject.FindWithTag/1`

The first active object with this tag, or null if there is none.

**Requires:** No component on this object is required. GameObject is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| tag | in | Text | tag (Text), passed into GameObject.FindWithTag(tag). Unwired, this is empty text. | empty text |
| Result | out | Object | Result (Object), the value GameObject.FindWithTag(tag) gives back. | — |

**Example:** In a script this is GameObject.FindWithTag(tag).

**Related:** Other GameObject nodes

## Engine API / Transform

### Transform.position

`Transform.position`

Transform.position — Position in the world.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |
| position | out | Vector3 | position (Vector3), the value transform.position gives back. | — |

**Example:** In a script this is transform.position.

**Related:** Other Transform nodes

### Set Transform.position

`Transform.position=`

Set Transform.position — Position in the world.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| position | in | Vector3 | position (Vector3), passed into transform.position = position. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is transform.position = position.

**Related:** Other Transform nodes

### Transform.localPosition

`Transform.localPosition`

Position relative to the parent.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |
| localPosition | out | Vector3 | localPosition (Vector3), the value transform.localPosition gives back. | — |

**Example:** In a script this is transform.localPosition.

**Related:** Other Transform nodes

### Set Transform.localPosition

`Transform.localPosition=`

Position relative to the parent.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| localPosition | in | Vector3 | localPosition (Vector3), passed into transform.localPosition = localPosition. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is transform.localPosition = localPosition.

**Related:** Other Transform nodes

### Transform.eulerAngles

`Transform.eulerAngles`

Rotation in the world, as X / Y / Z angles in degrees.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |
| eulerAngles | out | Vector3 | eulerAngles (Vector3), the value transform.eulerAngles gives back. | — |

**Example:** In a script this is transform.eulerAngles.

**Related:** Other Transform nodes

### Set Transform.eulerAngles

`Transform.eulerAngles=`

Rotation in the world, as X / Y / Z angles in degrees.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| eulerAngles | in | Vector3 | eulerAngles (Vector3), passed into transform.eulerAngles = eulerAngles. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is transform.eulerAngles = eulerAngles.

**Related:** Other Transform nodes

### Transform.localEulerAngles

`Transform.localEulerAngles`

Rotation relative to the parent, in degrees.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |
| localEulerAngles | out | Vector3 | localEulerAngles (Vector3), the value transform.localEulerAngles gives back. | — |

**Example:** In a script this is transform.localEulerAngles.

**Related:** Other Transform nodes

### Set Transform.localEulerAngles

`Transform.localEulerAngles=`

Rotation relative to the parent, in degrees.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| localEulerAngles | in | Vector3 | localEulerAngles (Vector3), passed into transform.localEulerAngles = localEulerAngles. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is transform.localEulerAngles = localEulerAngles.

**Related:** Other Transform nodes

### Transform.localScale

`Transform.localScale`

Scale relative to the parent.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |
| localScale | out | Vector3 | localScale (Vector3), the value transform.localScale gives back. | — |

**Example:** In a script this is transform.localScale.

**Related:** Other Transform nodes

### Set Transform.localScale

`Transform.localScale=`

Scale relative to the parent.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| localScale | in | Vector3 | localScale (Vector3), passed into transform.localScale = localScale. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is transform.localScale = localScale.

**Related:** Other Transform nodes

### Transform.forward

`Transform.forward`

The direction the object faces (its +Z), in the world.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |
| forward | out | Vector3 | forward (Vector3), the value transform.forward gives back. | — |

**Example:** In a script this is transform.forward.

**Related:** Other Transform nodes

### Transform.right

`Transform.right`

The object's right (+X), in the world.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |
| right | out | Vector3 | right (Vector3), the value transform.right gives back. | — |

**Example:** In a script this is transform.right.

**Related:** Other Transform nodes

### Transform.up

`Transform.up`

The object's up (+Y), in the world.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |
| up | out | Vector3 | up (Vector3), the value transform.up gives back. | — |

**Example:** In a script this is transform.up.

**Related:** Other Transform nodes

### Transform.gameObject

`Transform.gameObject`

The object this Transform belongs to.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |
| gameObject | out | Object | gameObject (Object), the value transform.gameObject gives back. | — |

**Example:** In a script this is transform.gameObject.

**Related:** Other Transform nodes

### Transform.Translate(offset)

`Transform.Translate/1`

Move by an offset (relative to the parent).

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| offset | in | Vector3 | offset (Vector3), passed into transform.Translate(offset). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is transform.Translate(offset).

**Related:** Other Transform nodes

### Transform.Translate(x, y, z)

`Transform.Translate/3`

Move by x, y, z (relative to the parent).

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| x | in | Number | x (Number), passed into transform.Translate(x, y, z). Unwired, this is 0. | 0 |
| y | in | Number | y (Number), passed into transform.Translate(x, y, z). Unwired, this is 0. | 0 |
| z | in | Number | z (Number), passed into transform.Translate(x, y, z). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is transform.Translate(x, y, z).

**Related:** Other Transform nodes

### Transform.Rotate(degrees)

`Transform.Rotate/1`

Turn by X / Y / Z degrees around the object's own axes.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| degrees | in | Vector3 | degrees (Vector3), passed into transform.Rotate(degrees). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is transform.Rotate(degrees).

**Related:** Other Transform nodes

### Transform.Rotate(x, y, z)

`Transform.Rotate/3`

Turn by x, y, z degrees around the object's own axes.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| x | in | Number | x (Number), passed into transform.Rotate(x, y, z). Unwired, this is 0. | 0 |
| y | in | Number | y (Number), passed into transform.Rotate(x, y, z). Unwired, this is 0. | 0 |
| z | in | Number | z (Number), passed into transform.Rotate(x, y, z). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is transform.Rotate(x, y, z).

**Related:** Other Transform nodes

### Transform.LookAt(target)

`Transform.LookAt/1`

Turn so forward points at a world position.

**Requires:** Uses this object's Transform. Leave Target empty to use this object. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| target | in | Vector3 | target (Vector3), passed into transform.LookAt(target). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose Transform this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is transform.LookAt(target).

**Related:** Other Transform nodes

## Engine API / Vector3

### Vector3.magnitude

`Vector3.magnitude`

Vector3.magnitude — The length.

**Requires:** No component on this object is required. Vector3 is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Vector3 | in | Vector3 | Vector3 (Vector3), passed into vector.magnitude. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| magnitude | out | Number | magnitude (Number), the value vector.magnitude gives back. | — |

**Example:** In a script this is vector.magnitude.

**Related:** Other Vector3 nodes

### Vector3.normalized

`Vector3.normalized`

The same direction with length 1.

**Requires:** No component on this object is required. Vector3 is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Vector3 | in | Vector3 | Vector3 (Vector3), passed into vector.normalized. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| normalized | out | Vector3 | normalized (Vector3), the value vector.normalized gives back. | — |

**Example:** In a script this is vector.normalized.

**Related:** Other Vector3 nodes

### Vector3.zero

`Vector3.zero`

Vector3.zero — (0, 0, 0).

**Requires:** No component on this object is required. Vector3 is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| zero | out | Vector3 | zero (Vector3), the value Vector3.zero gives back. | — |

**Example:** In a script this is Vector3.zero.

**Related:** Other Vector3 nodes

### Vector3.one

`Vector3.one`

Vector3.one — (1, 1, 1).

**Requires:** No component on this object is required. Vector3 is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| one | out | Vector3 | one (Vector3), the value Vector3.one gives back. | — |

**Example:** In a script this is Vector3.one.

**Related:** Other Vector3 nodes

### Vector3.up

`Vector3.up`

Vector3.up — (0, 1, 0).

**Requires:** No component on this object is required. Vector3 is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| up | out | Vector3 | up (Vector3), the value Vector3.up gives back. | — |

**Example:** In a script this is Vector3.up.

**Related:** Other Vector3 nodes

### Vector3.down

`Vector3.down`

Vector3.down — (0, -1, 0).

**Requires:** No component on this object is required. Vector3 is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| down | out | Vector3 | down (Vector3), the value Vector3.down gives back. | — |

**Example:** In a script this is Vector3.down.

**Related:** Other Vector3 nodes

### Vector3.right

`Vector3.right`

Vector3.right — (1, 0, 0).

**Requires:** No component on this object is required. Vector3 is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| right | out | Vector3 | right (Vector3), the value Vector3.right gives back. | — |

**Example:** In a script this is Vector3.right.

**Related:** Other Vector3 nodes

### Vector3.left

`Vector3.left`

Vector3.left — (-1, 0, 0).

**Requires:** No component on this object is required. Vector3 is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| left | out | Vector3 | left (Vector3), the value Vector3.left gives back. | — |

**Example:** In a script this is Vector3.left.

**Related:** Other Vector3 nodes

### Vector3.forward

`Vector3.forward`

Vector3.forward — (0, 0, 1).

**Requires:** No component on this object is required. Vector3 is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| forward | out | Vector3 | forward (Vector3), the value Vector3.forward gives back. | — |

**Example:** In a script this is Vector3.forward.

**Related:** Other Vector3 nodes

### Vector3.back

`Vector3.back`

Vector3.back — (0, 0, -1).

**Requires:** No component on this object is required. Vector3 is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| back | out | Vector3 | back (Vector3), the value Vector3.back gives back. | — |

**Example:** In a script this is Vector3.back.

**Related:** Other Vector3 nodes

### Vector3.Distance(a, b)

`Vector3.Distance/2`

Distance between two points.

**Requires:** No component on this object is required. Vector3 is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| a | in | Vector3 | a (Vector3), passed into Vector3.Distance(a, b). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| b | in | Vector3 | b (Vector3), passed into Vector3.Distance(a, b). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Result | out | Number | Result (Number), the value Vector3.Distance(a, b) gives back. | — |

**Example:** In a script this is Vector3.Distance(a, b).

**Related:** Other Vector3 nodes

### Vector3.Dot(a, b)

`Vector3.Dot/2`

Vector3.Dot(a, b) — Dot product.

**Requires:** No component on this object is required. Vector3 is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| a | in | Vector3 | a (Vector3), passed into Vector3.Dot(a, b). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| b | in | Vector3 | b (Vector3), passed into Vector3.Dot(a, b). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Result | out | Number | Result (Number), the value Vector3.Dot(a, b) gives back. | — |

**Example:** In a script this is Vector3.Dot(a, b).

**Related:** Other Vector3 nodes

### Vector3.Cross(a, b)

`Vector3.Cross/2`

Vector3.Cross(a, b) — Cross product.

**Requires:** No component on this object is required. Vector3 is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| a | in | Vector3 | a (Vector3), passed into Vector3.Cross(a, b). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| b | in | Vector3 | b (Vector3), passed into Vector3.Cross(a, b). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Result | out | Vector3 | Result (Vector3), the value Vector3.Cross(a, b) gives back. | — |

**Example:** In a script this is Vector3.Cross(a, b).

**Related:** Other Vector3 nodes

### Vector3.Lerp(a, b, t)

`Vector3.Lerp/3`

Blend from a (t = 0) to b (t = 1).

**Requires:** No component on this object is required. Vector3 is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| a | in | Vector3 | a (Vector3), passed into Vector3.Lerp(a, b, t). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| b | in | Vector3 | b (Vector3), passed into Vector3.Lerp(a, b, t). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| t | in | Number | t (Number), passed into Vector3.Lerp(a, b, t). Unwired, this is 0. | 0 |
| Result | out | Vector3 | Result (Vector3), the value Vector3.Lerp(a, b, t) gives back. | — |

**Example:** In a script this is Vector3.Lerp(a, b, t).

**Related:** Other Vector3 nodes

### Vector3.MoveTowards(current, target, maxDistance)

`Vector3.MoveTowards/3`

Step from current toward target, moving at most maxDistance.

**Requires:** No component on this object is required. Vector3 is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| current | in | Vector3 | current (Vector3), passed into Vector3.MoveTowards(current, target, maxDistance). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| target | in | Vector3 | target (Vector3), passed into Vector3.MoveTowards(current, target, maxDistance). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| maxDistance | in | Number | maxDistance (Number), passed into Vector3.MoveTowards(current, target, maxDistance). Unwired, this is 0. | 0 |
| Result | out | Vector3 | Result (Vector3), the value Vector3.MoveTowards(current, target, maxDistance) gives back. | — |

**Example:** In a script this is Vector3.MoveTowards(current, target, maxDistance).

**Related:** Other Vector3 nodes

## Engine API / Rigidbody

### Rigidbody.velocity

`Rigidbody.velocity`

Speed and direction in units per second.

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |
| velocity | out | Vector3 | velocity (Vector3), the value GetComponent<Rigidbody>().velocity gives back. | — |

**Example:** In a script this is GetComponent<Rigidbody>().velocity.

**Related:** Other Rigidbody nodes

### Set Rigidbody.velocity

`Rigidbody.velocity=`

Speed and direction in units per second.

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| velocity | in | Vector3 | velocity (Vector3), passed into GetComponent<Rigidbody>().velocity = velocity. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Rigidbody>().velocity = velocity.

**Related:** Other Rigidbody nodes

### Rigidbody.angularVelocity

`Rigidbody.angularVelocity`

Spin around X / Y / Z in radians per second.

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |
| angularVelocity | out | Vector3 | angularVelocity (Vector3), the value GetComponent<Rigidbody>().angularVelocity gives back. | — |

**Example:** In a script this is GetComponent<Rigidbody>().angularVelocity.

**Related:** Other Rigidbody nodes

### Set Rigidbody.angularVelocity

`Rigidbody.angularVelocity=`

Spin around X / Y / Z in radians per second.

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| angularVelocity | in | Vector3 | angularVelocity (Vector3), passed into GetComponent<Rigidbody>().angularVelocity = angularVelocity. Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Rigidbody>().angularVelocity = angularVelocity.

**Related:** Other Rigidbody nodes

### Rigidbody.mass

`Rigidbody.mass`

How heavy the body is (more than 0).

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |
| mass | out | Number | mass (Number), the value GetComponent<Rigidbody>().mass gives back. | — |

**Example:** In a script this is GetComponent<Rigidbody>().mass.

**Related:** Other Rigidbody nodes

### Set Rigidbody.mass

`Rigidbody.mass=`

How heavy the body is (more than 0).

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| mass | in | Number | mass (Number), passed into GetComponent<Rigidbody>().mass = mass. Unwired, this is 0. | 0 |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Rigidbody>().mass = mass.

**Related:** Other Rigidbody nodes

### Rigidbody.gravityScale

`Rigidbody.gravityScale`

How strongly gravity pulls it: 1 normal, 0 none.

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |
| gravityScale | out | Number | gravityScale (Number), the value GetComponent<Rigidbody>().gravityScale gives back. | — |

**Example:** In a script this is GetComponent<Rigidbody>().gravityScale.

**Related:** Other Rigidbody nodes

### Set Rigidbody.gravityScale

`Rigidbody.gravityScale=`

How strongly gravity pulls it: 1 normal, 0 none.

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| gravityScale | in | Number | gravityScale (Number), passed into GetComponent<Rigidbody>().gravityScale = gravityScale. Unwired, this is 0. | 0 |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Rigidbody>().gravityScale = gravityScale.

**Related:** Other Rigidbody nodes

### Rigidbody.drag

`Rigidbody.drag`

How quickly it slows down by itself (0 = never).

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |
| drag | out | Number | drag (Number), the value GetComponent<Rigidbody>().drag gives back. | — |

**Example:** In a script this is GetComponent<Rigidbody>().drag.

**Related:** Other Rigidbody nodes

### Set Rigidbody.drag

`Rigidbody.drag=`

How quickly it slows down by itself (0 = never).

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| drag | in | Number | drag (Number), passed into GetComponent<Rigidbody>().drag = drag. Unwired, this is 0. | 0 |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Rigidbody>().drag = drag.

**Related:** Other Rigidbody nodes

### Rigidbody.isKinematic

`Rigidbody.isKinematic`

When true, forces and gravity don't move it; only scripts do.

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |
| isKinematic | out | Bool | isKinematic (Bool), the value GetComponent<Rigidbody>().isKinematic gives back. | — |

**Example:** In a script this is GetComponent<Rigidbody>().isKinematic.

**Related:** Other Rigidbody nodes

### Set Rigidbody.isKinematic

`Rigidbody.isKinematic=`

When true, forces and gravity don't move it; only scripts do.

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| isKinematic | in | Bool | isKinematic (Bool), passed into GetComponent<Rigidbody>().isKinematic = isKinematic. Unwired, this is false. | false |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Rigidbody>().isKinematic = isKinematic.

**Related:** Other Rigidbody nodes

### Rigidbody.AddForce(force)

`Rigidbody.AddForce/1`

Push it, over the next physics step (heavier bodies move less).

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| force | in | Vector3 | force (Vector3), passed into GetComponent<Rigidbody>().AddForce(force). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Rigidbody>().AddForce(force).

**Related:** Other Rigidbody nodes

### Rigidbody.AddImpulse(impulse)

`Rigidbody.AddImpulse/1`

Kick it at once (a jump, a hit).

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| impulse | in | Vector3 | impulse (Vector3), passed into GetComponent<Rigidbody>().AddImpulse(impulse). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Rigidbody>().AddImpulse(impulse).

**Related:** Other Rigidbody nodes

### Rigidbody.AddTorque(torque)

`Rigidbody.AddTorque/1`

Twist it around X / Y / Z.

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| torque | in | Vector3 | torque (Vector3), passed into GetComponent<Rigidbody>().AddTorque(torque). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Rigidbody>().AddTorque(torque).

**Related:** Other Rigidbody nodes

### Rigidbody.AddForceAtPosition(force, point)

`Rigidbody.AddForceAtPosition/2`

Push it at a world point (off-centre pushes also spin it).

**Requires:** Needs a Rigidbody on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| force | in | Vector3 | force (Vector3), passed into GetComponent<Rigidbody>().AddForceAtPosition(force, point). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| point | in | Vector3 | point (Vector3), passed into GetComponent<Rigidbody>().AddForceAtPosition(force, point). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose Rigidbody this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Rigidbody>().AddForceAtPosition(force, point).

**Related:** Other Rigidbody nodes

## Engine API / PlayerView

### PlayerView.SetView(view)

`PlayerView.SetView/1`

Ease to a view: 0 first person (camera at the eyes), 1 third person (over the shoulder).

**Requires:** Needs a PlayerView on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| view | in | Number | view (Number), passed into GetComponent<PlayerView>().SetView(view). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose PlayerView this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<PlayerView>().SetView(view).

**Related:** Other PlayerView nodes

### PlayerView.GetView()

`PlayerView.GetView/0`

The view asked for: 0 first person, 1 third person. The camera may still be on its way there.

**Requires:** Needs a PlayerView on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose PlayerView this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Number | Result (Number), the value GetComponent<PlayerView>().GetView() gives back. | — |

**Example:** In a script this is GetComponent<PlayerView>().GetView().

**Related:** Other PlayerView nodes

## Engine API / PlayerController

### PlayerController.SetView(view)

`PlayerController.SetView/1`

Ease to a view: 0 first person, 1 third person. Does nothing unless the View is Both.

**Requires:** Needs a PlayerController on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| view | in | Number | view (Number), passed into GetComponent<PlayerController>().SetView(view). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose PlayerController this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<PlayerController>().SetView(view).

**Related:** Other PlayerController nodes

### PlayerController.GetView()

`PlayerController.GetView/0`

The view the player is in: 0 first person, 1 third person.

**Requires:** Needs a PlayerController on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose PlayerController this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Number | Result (Number), the value GetComponent<PlayerController>().GetView() gives back. | — |

**Example:** In a script this is GetComponent<PlayerController>().GetView().

**Related:** Other PlayerController nodes

### PlayerController.SetViewMode(mode)

`PlayerController.SetViewMode/1`

Which views the player has: 0 first person only, 1 third person only, 2 both.

**Requires:** Needs a PlayerController on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| mode | in | Number | mode (Number), passed into GetComponent<PlayerController>().SetViewMode(mode). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose PlayerController this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<PlayerController>().SetViewMode(mode).

**Related:** Other PlayerController nodes

## Engine API / UIColorPicker

### UIColorPicker.SetValue(index)

`UIColorPicker.SetValue/1`

Select a swatch by index. The chip shows that color.

**Requires:** Needs a UIColorPicker on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| index | in | Number | index (Number), passed into GetComponent<UIColorPicker>().SetValue(index). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIColorPicker this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIColorPicker>().SetValue(index).

**Related:** Other UIColorPicker nodes

### UIColorPicker.Value()

`UIColorPicker.Value/0`

The selected swatch, or -1.

**Requires:** Needs a UIColorPicker on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose UIColorPicker this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Number | Result (Number), the value GetComponent<UIColorPicker>().Value() gives back. | — |

**Example:** In a script this is GetComponent<UIColorPicker>().Value().

**Related:** Other UIColorPicker nodes

## Engine API / Crosshair

### Crosshair.SetSpread(pixels)

`Crosshair.SetSpread/1`

Set the extra gap around the ticks, in pixels.

**Requires:** Needs a Crosshair on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| pixels | in | Number | pixels (Number), passed into GetComponent<Crosshair>().SetSpread(pixels). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose Crosshair this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Crosshair>().SetSpread(pixels).

**Related:** Other Crosshair nodes

### Crosshair.SetSpreadRecover(pixelsPerSecond)

`Crosshair.SetSpreadRecover/1`

How fast the extra gap eases back to 0. 0 leaves the spread where it is.

**Requires:** Needs a Crosshair on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| pixelsPerSecond | in | Number | pixelsPerSecond (Number), passed into GetComponent<Crosshair>().SetSpreadRecover(pixelsPerSecond). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose Crosshair this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Crosshair>().SetSpreadRecover(pixelsPerSecond).

**Related:** Other Crosshair nodes

### Crosshair.KickSpread(pixels)

`Crosshair.KickSpread/1`

Push the ticks apart. They ease back while spread recover is above 0.

**Requires:** Needs a Crosshair on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| pixels | in | Number | pixels (Number), passed into GetComponent<Crosshair>().KickSpread(pixels). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose Crosshair this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Crosshair>().KickSpread(pixels).

**Related:** Other Crosshair nodes

### Crosshair.Spread()

`Crosshair.Spread/0`

The extra gap around the ticks, in pixels.

**Requires:** Needs a Crosshair on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Crosshair this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Number | Result (Number), the value GetComponent<Crosshair>().Spread() gives back. | — |

**Example:** In a script this is GetComponent<Crosshair>().Spread().

**Related:** Other Crosshair nodes

## Engine API / UIRadialProgress

### UIRadialProgress.SetValue(value)

`UIRadialProgress.SetValue/1`

Fill fraction from 0 to 1.

**Requires:** Needs a UIRadialProgress on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| value | in | Number | value (Number), passed into GetComponent<UIRadialProgress>().SetValue(value). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIRadialProgress this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIRadialProgress>().SetValue(value).

**Related:** Other UIRadialProgress nodes

### UIRadialProgress.SetThickness(pixels)

`UIRadialProgress.SetThickness/1`

Ring width in pixels. 0 draws a pie.

**Requires:** Needs a UIRadialProgress on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| pixels | in | Number | pixels (Number), passed into GetComponent<UIRadialProgress>().SetThickness(pixels). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIRadialProgress this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIRadialProgress>().SetThickness(pixels).

**Related:** Other UIRadialProgress nodes

### UIRadialProgress.SetStartAngle(degrees)

`UIRadialProgress.SetStartAngle/1`

Where the arc starts. 0 is 12 o'clock.

**Requires:** Needs a UIRadialProgress on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| degrees | in | Number | degrees (Number), passed into GetComponent<UIRadialProgress>().SetStartAngle(degrees). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIRadialProgress this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIRadialProgress>().SetStartAngle(degrees).

**Related:** Other UIRadialProgress nodes

### UIRadialProgress.SetClockwise(on)

`UIRadialProgress.SetClockwise/1`

UIRadialProgress.SetClockwise(on) — Fill clockwise when on.

**Requires:** Needs a UIRadialProgress on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| on | in | Bool | on (Bool), passed into GetComponent<UIRadialProgress>().SetClockwise(on). Unwired, this is false. | false |
| Target | in | Object | The object whose UIRadialProgress this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIRadialProgress>().SetClockwise(on).

**Related:** Other UIRadialProgress nodes

### UIRadialProgress.SetSpin(on)

`UIRadialProgress.SetSpin/1`

Spin the arc as a loader.

**Requires:** Needs a UIRadialProgress on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| on | in | Bool | on (Bool), passed into GetComponent<UIRadialProgress>().SetSpin(on). Unwired, this is false. | false |
| Target | in | Object | The object whose UIRadialProgress this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIRadialProgress>().SetSpin(on).

**Related:** Other UIRadialProgress nodes

### UIRadialProgress.SetShowPercent(on)

`UIRadialProgress.SetShowPercent/1`

Draw the fill as a percent in the middle.

**Requires:** Needs a UIRadialProgress on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| on | in | Bool | on (Bool), passed into GetComponent<UIRadialProgress>().SetShowPercent(on). Unwired, this is false. | false |
| Target | in | Object | The object whose UIRadialProgress this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIRadialProgress>().SetShowPercent(on).

**Related:** Other UIRadialProgress nodes

## Engine API / UISlotGrid

### UISlotGrid.SetValue(index)

`UISlotGrid.SetValue/1`

Select a slot by index. -1 clears the selection.

**Requires:** Needs a UISlotGrid on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| index | in | Number | index (Number), passed into GetComponent<UISlotGrid>().SetValue(index). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UISlotGrid this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UISlotGrid>().SetValue(index).

**Related:** Other UISlotGrid nodes

### UISlotGrid.SetColumns(columns)

`UISlotGrid.SetColumns/1`

How many slots across (1 to 16).

**Requires:** Needs a UISlotGrid on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| columns | in | Number | columns (Number), passed into GetComponent<UISlotGrid>().SetColumns(columns). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UISlotGrid this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UISlotGrid>().SetColumns(columns).

**Related:** Other UISlotGrid nodes

### UISlotGrid.SetRows(rows)

`UISlotGrid.SetRows/1`

How many slots down (1 to 16).

**Requires:** Needs a UISlotGrid on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| rows | in | Number | rows (Number), passed into GetComponent<UISlotGrid>().SetRows(rows). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UISlotGrid this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UISlotGrid>().SetRows(rows).

**Related:** Other UISlotGrid nodes

### UISlotGrid.Value()

`UISlotGrid.Value/0`

The selected slot, or -1.

**Requires:** Needs a UISlotGrid on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose UISlotGrid this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Number | Result (Number), the value GetComponent<UISlotGrid>().Value() gives back. | — |

**Example:** In a script this is GetComponent<UISlotGrid>().Value().

**Related:** Other UISlotGrid nodes

## Engine API / UIDropdown

### UIDropdown.SetSearchable(on)

`UIDropdown.SetSearchable/1`

Show a search row that filters the options while the list is open.

**Requires:** Needs a UIDropdown on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| on | in | Bool | on (Bool), passed into GetComponent<UIDropdown>().SetSearchable(on). Unwired, this is false. | false |
| Target | in | Object | The object whose UIDropdown this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIDropdown>().SetSearchable(on).

**Related:** Other UIDropdown nodes

### UIDropdown.SetMaxVisible(rows)

`UIDropdown.SetMaxVisible/1`

How many matching options the open list shows. 0 shows every match.

**Requires:** Needs a UIDropdown on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| rows | in | Number | rows (Number), passed into GetComponent<UIDropdown>().SetMaxVisible(rows). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIDropdown this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIDropdown>().SetMaxVisible(rows).

**Related:** Other UIDropdown nodes

### UIDropdown.SetOptionIcon(index, glyph)

`UIDropdown.SetOptionIcon/2`

A short glyph drawn in front of one option.

**Requires:** Needs a UIDropdown on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| index | in | Number | index (Number), passed into GetComponent<UIDropdown>().SetOptionIcon(index, glyph). Unwired, this is 0. | 0 |
| glyph | in | Text | glyph (Text), passed into GetComponent<UIDropdown>().SetOptionIcon(index, glyph). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose UIDropdown this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIDropdown>().SetOptionIcon(index, glyph).

**Related:** Other UIDropdown nodes

### UIDropdown.Searchable()

`UIDropdown.Searchable/0`

True when the open list has a search row.

**Requires:** Needs a UIDropdown on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose UIDropdown this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Bool | Result (Bool), the value GetComponent<UIDropdown>().Searchable() gives back. | — |

**Example:** In a script this is GetComponent<UIDropdown>().Searchable().

**Related:** Other UIDropdown nodes

## Engine API / UIScrollView

### UIScrollView.SetInertia(rate)

`UIScrollView.SetInertia/1`

Wheel glide. 0 scrolls one notch immediately. Higher stops the glide sooner.

**Requires:** Needs a UIScrollView on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| rate | in | Number | rate (Number), passed into GetComponent<UIScrollView>().SetInertia(rate). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIScrollView this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIScrollView>().SetInertia(rate).

**Related:** Other UIScrollView nodes

### UIScrollView.SetElastic(on)

`UIScrollView.SetElastic/1`

Let the content pass the ends, then spring back.

**Requires:** Needs a UIScrollView on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| on | in | Bool | on (Bool), passed into GetComponent<UIScrollView>().SetElastic(on). Unwired, this is false. | false |
| Target | in | Object | The object whose UIScrollView this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIScrollView>().SetElastic(on).

**Related:** Other UIScrollView nodes

### UIScrollView.SetBarStyle(style)

`UIScrollView.SetBarStyle/1`

Scrollbar: 0 the usual bar, 1 thin, 2 hidden until the view is scrolling.

**Requires:** Needs a UIScrollView on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| style | in | Number | style (Number), passed into GetComponent<UIScrollView>().SetBarStyle(style). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIScrollView this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIScrollView>().SetBarStyle(style).

**Related:** Other UIScrollView nodes

## Engine API / UITabs

### UITabs.SetClosable(on)

`UITabs.SetClosable/1`

Draw an X on each tab. Clicking it removes that tab.

**Requires:** Needs a UITabs on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| on | in | Bool | on (Bool), passed into GetComponent<UITabs>().SetClosable(on). Unwired, this is false. | false |
| Target | in | Object | The object whose UITabs this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UITabs>().SetClosable(on).

**Related:** Other UITabs nodes

### UITabs.CloseTab(index)

`UITabs.CloseTab/1`

Remove one tab and fire on_tab_close.

**Requires:** Needs a UITabs on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| index | in | Number | index (Number), passed into GetComponent<UITabs>().CloseTab(index). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UITabs this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UITabs>().CloseTab(index).

**Related:** Other UITabs nodes

### UITabs.Count()

`UITabs.Count/0`

UITabs.Count() — How many tabs are left.

**Requires:** Needs a UITabs on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose UITabs this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Number | Result (Number), the value GetComponent<UITabs>().Count() gives back. | — |

**Example:** In a script this is GetComponent<UITabs>().Count().

**Related:** Other UITabs nodes

## Engine API / UITooltip

### UITooltip.SetRichText(on)

`UITooltip.SetRichText/1`

Draw <b> and #rrggbb in the tooltip text.

**Requires:** Needs a UITooltip on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| on | in | Bool | on (Bool), passed into GetComponent<UITooltip>().SetRichText(on). Unwired, this is false. | false |
| Target | in | Object | The object whose UITooltip this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UITooltip>().SetRichText(on).

**Related:** Other UITooltip nodes

### UITooltip.SetOffset(x, y)

`UITooltip.SetOffset/2`

Pixels from the cursor to the box. 14, 14 is the usual spot.

**Requires:** Needs a UITooltip on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| x | in | Number | x (Number), passed into GetComponent<UITooltip>().SetOffset(x, y). Unwired, this is 0. | 0 |
| y | in | Number | y (Number), passed into GetComponent<UITooltip>().SetOffset(x, y). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UITooltip this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UITooltip>().SetOffset(x, y).

**Related:** Other UITooltip nodes

### UITooltip.SetDelay(seconds)

`UITooltip.SetDelay/1`

How long the pointer must hover before the tooltip shows.

**Requires:** Needs a UITooltip on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| seconds | in | Number | seconds (Number), passed into GetComponent<UITooltip>().SetDelay(seconds). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UITooltip this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UITooltip>().SetDelay(seconds).

**Related:** Other UITooltip nodes

## Engine API / UIInputField

### UIInputField.SetMultiline(on)

`UIInputField.SetMultiline/1`

Enter inserts a newline instead of submitting.

**Requires:** Needs a UIInputField on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| on | in | Bool | on (Bool), passed into GetComponent<UIInputField>().SetMultiline(on). Unwired, this is false. | false |
| Target | in | Object | The object whose UIInputField this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIInputField>().SetMultiline(on).

**Related:** Other UIInputField nodes

### UIInputField.SetMaxLength(length)

`UIInputField.SetMaxLength/1`

The most characters the field accepts.

**Requires:** Needs a UIInputField on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| length | in | Number | length (Number), passed into GetComponent<UIInputField>().SetMaxLength(length). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIInputField this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIInputField>().SetMaxLength(length).

**Related:** Other UIInputField nodes

### UIInputField.SetContentType(type)

`UIInputField.SetContentType/1`

0 standard, 1 integer, 2 decimal, 3 password.

**Requires:** Needs a UIInputField on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| type | in | Number | type (Number), passed into GetComponent<UIInputField>().SetContentType(type). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIInputField this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIInputField>().SetContentType(type).

**Related:** Other UIInputField nodes

## Engine API / UIToast

### UIToast.Push(title, body, kind)

`UIToast.Push/3`

Show a toast. kind: 0 info, 1 success, 2 warning, 3 error.

**Requires:** Needs a UIToast on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| title | in | Text | title (Text), passed into GetComponent<UIToast>().Push(title, body, kind). Unwired, this is empty text. | empty text |
| body | in | Text | body (Text), passed into GetComponent<UIToast>().Push(title, body, kind). Unwired, this is empty text. | empty text |
| kind | in | Number | kind (Number), passed into GetComponent<UIToast>().Push(title, body, kind). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIToast this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIToast>().Push(title, body, kind).

**Related:** Other UIToast nodes

### UIToast.SetDuration(seconds)

`UIToast.SetDuration/1`

How long a new toast stays up, in seconds.

**Requires:** Needs a UIToast on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| seconds | in | Number | seconds (Number), passed into GetComponent<UIToast>().SetDuration(seconds). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIToast this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIToast>().SetDuration(seconds).

**Related:** Other UIToast nodes

### UIToast.SetCorner(corner)

`UIToast.SetCorner/1`

Where the stack sits: 0 top-right, 1 top-left, 2 bottom-right, 3 bottom-left, 4 top-center.

**Requires:** Needs a UIToast on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| corner | in | Number | corner (Number), passed into GetComponent<UIToast>().SetCorner(corner). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIToast this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIToast>().SetCorner(corner).

**Related:** Other UIToast nodes

### UIToast.SetMaxStack(count)

`UIToast.SetMaxStack/1`

How many toasts stay on screen. The oldest one drops off past this.

**Requires:** Needs a UIToast on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| count | in | Number | count (Number), passed into GetComponent<UIToast>().SetMaxStack(count). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIToast this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIToast>().SetMaxStack(count).

**Related:** Other UIToast nodes

### UIToast.Count()

`UIToast.Count/0`

How many toasts are on screen right now.

**Requires:** Needs a UIToast on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose UIToast this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Number | Result (Number), the value GetComponent<UIToast>().Count() gives back. | — |

**Example:** In a script this is GetComponent<UIToast>().Count().

**Related:** Other UIToast nodes

## Engine API / UIModal

### UIModal.Open()

`UIModal.Open/0`

UIModal.Open() — Show the dialog.

**Requires:** Needs a UIModal on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| Target | in | Object | The object whose UIModal this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIModal>().Open().

**Related:** Other UIModal nodes

### UIModal.Close()

`UIModal.Close/0`

Hide the dialog. Fires on_modal_close.

**Requires:** Needs a UIModal on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| Target | in | Object | The object whose UIModal this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIModal>().Close().

**Related:** Other UIModal nodes

### UIModal.SetStyleCard(on)

`UIModal.SetStyleCard/1`

Turn the card styling on or off. Off leaves the dialog as it was authored.

**Requires:** Needs a UIModal on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| on | in | Bool | on (Bool), passed into GetComponent<UIModal>().SetStyleCard(on). Unwired, this is false. | false |
| Target | in | Object | The object whose UIModal this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIModal>().SetStyleCard(on).

**Related:** Other UIModal nodes

### UIModal.SetCardRadius(radius)

`UIModal.SetCardRadius/1`

Corner radius of the card named Card, in pixels.

**Requires:** Needs a UIModal on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| radius | in | Number | radius (Number), passed into GetComponent<UIModal>().SetCardRadius(radius). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIModal this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIModal>().SetCardRadius(radius).

**Related:** Other UIModal nodes

### UIModal.SetTitle(text)

`UIModal.SetTitle/1`

Title text. Empty leaves whatever the Title label already says.

**Requires:** Needs a UIModal on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| text | in | Text | text (Text), passed into GetComponent<UIModal>().SetTitle(text). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose UIModal this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIModal>().SetTitle(text).

**Related:** Other UIModal nodes

### UIModal.SetMessage(text)

`UIModal.SetMessage/1`

Body text. Empty leaves whatever the Message label already says.

**Requires:** Needs a UIModal on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| text | in | Text | text (Text), passed into GetComponent<UIModal>().SetMessage(text). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose UIModal this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIModal>().SetMessage(text).

**Related:** Other UIModal nodes

### UIModal.SetConfirmText(text)

`UIModal.SetConfirmText/1`

Confirm button label. Empty leaves the button's own label.

**Requires:** Needs a UIModal on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| text | in | Text | text (Text), passed into GetComponent<UIModal>().SetConfirmText(text). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose UIModal this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIModal>().SetConfirmText(text).

**Related:** Other UIModal nodes

### UIModal.SetCancelText(text)

`UIModal.SetCancelText/1`

Cancel button label. Empty leaves the button's own label.

**Requires:** Needs a UIModal on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| text | in | Text | text (Text), passed into GetComponent<UIModal>().SetCancelText(text). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose UIModal this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIModal>().SetCancelText(text).

**Related:** Other UIModal nodes

### UIModal.IsOpen()

`UIModal.IsOpen/0`

True while the dialog is shown.

**Requires:** Needs a UIModal on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose UIModal this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Bool | Result (Bool), the value GetComponent<UIModal>().IsOpen() gives back. | — |

**Example:** In a script this is GetComponent<UIModal>().IsOpen().

**Related:** Other UIModal nodes

## Engine API / UITheme

### UITheme.SetPreset(preset)

`UITheme.SetPreset/1`

Switch to a built-in look and bake it: 0 custom, 1 dark, 2 light, 3 sci-fi, 4 fantasy.

**Requires:** Needs a UITheme on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| preset | in | Number | preset (Number), passed into GetComponent<UITheme>().SetPreset(preset). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UITheme this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UITheme>().SetPreset(preset).

**Related:** Other UITheme nodes

### UITheme.Apply()

`UITheme.Apply/0`

Bake this skin onto this object and its children. Widgets keep their positions.

**Requires:** Needs a UITheme on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| Target | in | Object | The object whose UITheme this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UITheme>().Apply().

**Related:** Other UITheme nodes

### UITheme.ApplyTo(objectName)

`UITheme.ApplyTo/1`

Bake this skin onto the named object and its children.

**Requires:** Needs a UITheme on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| objectName | in | Text | objectName (Text), passed into GetComponent<UITheme>().ApplyTo(objectName). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose UITheme this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UITheme>().ApplyTo(objectName).

**Related:** Other UITheme nodes

## Engine API / UISlider

### UISlider.SetValue(value)

`UISlider.SetValue/1`

Set the value (snaps to the step). Fires OnValueChanged when it changes.

**Requires:** Needs a UISlider on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| value | in | Number | value (Number), passed into GetComponent<UISlider>().SetValue(value). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UISlider this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UISlider>().SetValue(value).

**Related:** Other UISlider nodes

### UISlider.SetStep(step)

`UISlider.SetStep/1`

Snap spacing. 0 keeps the slider continuous.

**Requires:** Needs a UISlider on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| step | in | Number | step (Number), passed into GetComponent<UISlider>().SetStep(step). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UISlider this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UISlider>().SetStep(step).

**Related:** Other UISlider nodes

### UISlider.SetFormat(format)

`UISlider.SetFormat/1`

Value label. "{v}" is the number. Empty keeps two decimals.

**Requires:** Needs a UISlider on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| format | in | Text | format (Text), passed into GetComponent<UISlider>().SetFormat(format). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose UISlider this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UISlider>().SetFormat(format).

**Related:** Other UISlider nodes

### UISlider.GetValue()

`UISlider.GetValue/0`

UISlider.GetValue() — The current value.

**Requires:** Needs a UISlider on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose UISlider this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Number | Result (Number), the value GetComponent<UISlider>().GetValue() gives back. | — |

**Example:** In a script this is GetComponent<UISlider>().GetValue().

**Related:** Other UISlider nodes

## Engine API / UIProgressBar

### UIProgressBar.SetValue(value)

`UIProgressBar.SetValue/1`

Fill amount, 0 to 1. A damage trail eases down when Trail is set.

**Requires:** Needs a UIProgressBar on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| value | in | Number | value (Number), passed into GetComponent<UIProgressBar>().SetValue(value). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIProgressBar this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIProgressBar>().SetValue(value).

**Related:** Other UIProgressBar nodes

### UIProgressBar.SetSegments(count)

`UIProgressBar.SetSegments/1`

Chunk count. 0 or 1 is one solid bar.

**Requires:** Needs a UIProgressBar on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| count | in | Number | count (Number), passed into GetComponent<UIProgressBar>().SetSegments(count). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIProgressBar this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIProgressBar>().SetSegments(count).

**Related:** Other UIProgressBar nodes

### UIProgressBar.SetTrail(seconds)

`UIProgressBar.SetTrail/1`

Seconds for the damage trail to catch a drop. 0 hides it.

**Requires:** Needs a UIProgressBar on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| seconds | in | Number | seconds (Number), passed into GetComponent<UIProgressBar>().SetTrail(seconds). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIProgressBar this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIProgressBar>().SetTrail(seconds).

**Related:** Other UIProgressBar nodes

### UIProgressBar.SetTextFormat(format)

`UIProgressBar.SetTextFormat/1`

Label on the bar. "{n}" is the percent, "{v}" is 0..1.

**Requires:** Needs a UIProgressBar on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| format | in | Text | format (Text), passed into GetComponent<UIProgressBar>().SetTextFormat(format). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose UIProgressBar this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIProgressBar>().SetTextFormat(format).

**Related:** Other UIProgressBar nodes

## Engine API / UIButton

### UIButton.SetEase(ease, seconds)

`UIButton.SetEase/2`

Hover/press ease: 0 linear, 1 smooth, 2 ease-out, 3 ease-in, and how many seconds it takes.

**Requires:** Needs a UIButton on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| ease | in | Number | ease (Number), passed into GetComponent<UIButton>().SetEase(ease, seconds). Unwired, this is 0. | 0 |
| seconds | in | Number | seconds (Number), passed into GetComponent<UIButton>().SetEase(ease, seconds). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose UIButton this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIButton>().SetEase(ease, seconds).

**Related:** Other UIButton nodes

### UIButton.SetInteractable(on)

`UIButton.SetInteractable/1`

False greys the button out and ignores clicks.

**Requires:** Needs a UIButton on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| on | in | Bool | on (Bool), passed into GetComponent<UIButton>().SetInteractable(on). Unwired, this is false. | false |
| Target | in | Object | The object whose UIButton this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIButton>().SetInteractable(on).

**Related:** Other UIButton nodes

### UIButton.Press()

`UIButton.Press/0`

Click the button. Fires OnClick.

**Requires:** Needs a UIButton on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| Target | in | Object | The object whose UIButton this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<UIButton>().Press().

**Related:** Other UIButton nodes

## Engine API / Input

### Input.mousePosition

`Input.mousePosition`

The cursor in screen pixels (x, y; z is 0).

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| mousePosition | out | Vector3 | mousePosition (Vector3), the value Input.mousePosition gives back. | — |

**Example:** In a script this is Input.mousePosition.

**Related:** Other Input nodes

### Input.gamepadConnected

`Input.gamepadConnected`

True while a controller is plugged in.

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| gamepadConnected | out | Bool | gamepadConnected (Bool), the value Input.gamepadConnected gives back. | — |

**Example:** In a script this is Input.gamepadConnected.

**Related:** Other Input nodes

### Input.GetKey(key)

`Input.GetKey/1`

True while the key is held.

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| key | in | Text | key (Text), passed into Input.GetKey(key). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Input.GetKey(key) gives back. | — |

**Example:** In a script this is Input.GetKey(key).

**Related:** Other Input nodes

### Input.GetKeyDown(key)

`Input.GetKeyDown/1`

True on the frame the key is pressed.

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| key | in | Text | key (Text), passed into Input.GetKeyDown(key). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Input.GetKeyDown(key) gives back. | — |

**Example:** In a script this is Input.GetKeyDown(key).

**Related:** Other Input nodes

### Input.GetKeyUp(key)

`Input.GetKeyUp/1`

True on the frame the key is released.

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| key | in | Text | key (Text), passed into Input.GetKeyUp(key). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Input.GetKeyUp(key) gives back. | — |

**Example:** In a script this is Input.GetKeyUp(key).

**Related:** Other Input nodes

### Input.GetAxis(axis)

`Input.GetAxis/1`

"Horizontal" (A/D, left/right, stick) or "Vertical" (W/S, up/down, stick): -1 to 1.

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| axis | in | Text | axis (Text), passed into Input.GetAxis(axis). Unwired, this is empty text. | empty text |
| Result | out | Number | Result (Number), the value Input.GetAxis(axis) gives back. | — |

**Example:** In a script this is Input.GetAxis(axis).

**Related:** Other Input nodes

### Input.GetMouseButton(button)

`Input.GetMouseButton/1`

True while the mouse button is held (0 left, 1 right, 2 middle).

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| button | in | Number | button (Number), passed into Input.GetMouseButton(button). Unwired, this is 0. | 0 |
| Result | out | Bool | Result (Bool), the value Input.GetMouseButton(button) gives back. | — |

**Example:** In a script this is Input.GetMouseButton(button).

**Related:** Other Input nodes

### Input.GetMouseButtonDown(button)

`Input.GetMouseButtonDown/1`

True on the frame the mouse button is pressed.

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| button | in | Number | button (Number), passed into Input.GetMouseButtonDown(button). Unwired, this is 0. | 0 |
| Result | out | Bool | Result (Bool), the value Input.GetMouseButtonDown(button) gives back. | — |

**Example:** In a script this is Input.GetMouseButtonDown(button).

**Related:** Other Input nodes

### Input.GetMouseButtonUp(button)

`Input.GetMouseButtonUp/1`

True on the frame the mouse button is released.

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| button | in | Number | button (Number), passed into Input.GetMouseButtonUp(button). Unwired, this is 0. | 0 |
| Result | out | Bool | Result (Bool), the value Input.GetMouseButtonUp(button) gives back. | — |

**Example:** In a script this is Input.GetMouseButtonUp(button).

**Related:** Other Input nodes

### Input.GetButton(action)

`Input.GetButton/1`

True while the action is held ("Jump", "Fire"...), on keyboard, mouse or controller.

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| action | in | Text | action (Text), passed into Input.GetButton(action). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Input.GetButton(action) gives back. | — |

**Example:** In a script this is Input.GetButton(action).

**Related:** Other Input nodes

### Input.GetButtonDown(action)

`Input.GetButtonDown/1`

True on the frame the action is pressed.

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| action | in | Text | action (Text), passed into Input.GetButtonDown(action). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Input.GetButtonDown(action) gives back. | — |

**Example:** In a script this is Input.GetButtonDown(action).

**Related:** Other Input nodes

### Input.GetButtonUp(action)

`Input.GetButtonUp/1`

True on the frame the action is released.

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| action | in | Text | action (Text), passed into Input.GetButtonUp(action). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Input.GetButtonUp(action) gives back. | — |

**Example:** In a script this is Input.GetButtonUp(action).

**Related:** Other Input nodes

### Input.GetActionValue(action)

`Input.GetActionValue/1`

How far the action is pressed, 0 to 1 (a trigger can be part way).

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| action | in | Text | action (Text), passed into Input.GetActionValue(action). Unwired, this is empty text. | empty text |
| Result | out | Number | Result (Number), the value Input.GetActionValue(action) gives back. | — |

**Example:** In a script this is Input.GetActionValue(action).

**Related:** Other Input nodes

### Input.GetActionVector(action)

`Input.GetActionVector/1`

A direction action ("Move", "Look") as x, y in -1..1 (y up; z is 0).

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| action | in | Text | action (Text), passed into Input.GetActionVector(action). Unwired, this is empty text. | empty text |
| Result | out | Vector3 | Result (Vector3), the value Input.GetActionVector(action) gives back. | — |

**Example:** In a script this is Input.GetActionVector(action).

**Related:** Other Input nodes

### Input.Rumble(low, high, seconds)

`Input.Rumble/3`

Shake the controller: low (heavy) and high (light) motor strength 0 to 1, for some seconds.

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| low | in | Number | low (Number), passed into Input.Rumble(low, high, seconds). Unwired, this is 0. | 0 |
| high | in | Number | high (Number), passed into Input.Rumble(low, high, seconds). Unwired, this is 0. | 0 |
| seconds | in | Number | seconds (Number), passed into Input.Rumble(low, high, seconds). Unwired, this is 0. | 0 |

**Example:** In a script this is Input.Rumble(low, high, seconds).

**Related:** Other Input nodes

### Input.RebindAction(action, binding)

`Input.RebindAction/2`

Bind an action to "key:e", "mouse:right", "pad:rt"... for this player (saved). False if the binding isn't understood.

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| action | in | Text | action (Text), passed into Input.RebindAction(action, binding). Unwired, this is empty text. | empty text |
| binding | in | Text | binding (Text), passed into Input.RebindAction(action, binding). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Input.RebindAction(action, binding) gives back. | — |

**Example:** In a script this is Input.RebindAction(action, binding).

**Related:** Other Input nodes

### Input.DefineAction(action, binding, binding2)

`Input.DefineAction/3`

Add a game action (or change its defaults), e.g. ("Dash", "key:q", "pad:rs"). Use "" for an unused binding.

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| action | in | Text | action (Text), passed into Input.DefineAction(action, binding, binding2). Unwired, this is empty text. | empty text |
| binding | in | Text | binding (Text), passed into Input.DefineAction(action, binding, binding2). Unwired, this is empty text. | empty text |
| binding2 | in | Text | binding2 (Text), passed into Input.DefineAction(action, binding, binding2). Unwired, this is empty text. | empty text |

**Example:** In a script this is Input.DefineAction(action, binding, binding2).

**Related:** Other Input nodes

### Input.DefineDirection(action, binding, binding2)

`Input.DefineDirection/3`

Add a direction action, e.g. ("Steer", "keys:i,k,j,l", "stick:right").

**Requires:** No component on this object is required. Input is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| action | in | Text | action (Text), passed into Input.DefineDirection(action, binding, binding2). Unwired, this is empty text. | empty text |
| binding | in | Text | binding (Text), passed into Input.DefineDirection(action, binding, binding2). Unwired, this is empty text. | empty text |
| binding2 | in | Text | binding2 (Text), passed into Input.DefineDirection(action, binding, binding2). Unwired, this is empty text. | empty text |

**Example:** In a script this is Input.DefineDirection(action, binding, binding2).

**Related:** Other Input nodes

## Engine API / Time

### Time.deltaTime

`Time.deltaTime`

Seconds since the last frame (inside FixedUpdate: the fixed step).

**Requires:** No component on this object is required. Time is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| deltaTime | out | Number | deltaTime (Number), the value Time.deltaTime gives back. | — |

**Example:** In a script this is Time.deltaTime.

**Related:** Other Time nodes

### Time.fixedDeltaTime

`Time.fixedDeltaTime`

The FixedUpdate step in seconds.

**Requires:** No component on this object is required. Time is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| fixedDeltaTime | out | Number | fixedDeltaTime (Number), the value Time.fixedDeltaTime gives back. | — |

**Example:** In a script this is Time.fixedDeltaTime.

**Related:** Other Time nodes

### Time.time

`Time.time`

Seconds since the game started.

**Requires:** No component on this object is required. Time is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| time | out | Number | time (Number), the value Time.time gives back. | — |

**Example:** In a script this is Time.time.

**Related:** Other Time nodes

### Time.frameCount

`Time.frameCount`

Frames since the game started.

**Requires:** No component on this object is required. Time is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| frameCount | out | Number | frameCount (Number), the value Time.frameCount gives back. | — |

**Example:** In a script this is Time.frameCount.

**Related:** Other Time nodes

### Time.timeScale

`Time.timeScale`

How fast time passes: 1 normal, 0.5 slow motion, 0 paused.

**Requires:** No component on this object is required. Time is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| timeScale | out | Number | timeScale (Number), the value Time.timeScale gives back. | — |

**Example:** In a script this is Time.timeScale.

**Related:** Other Time nodes

### Set Time.timeScale

`Time.timeScale=`

How fast time passes: 1 normal, 0.5 slow motion, 0 paused.

**Requires:** No component on this object is required. Time is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| timeScale | in | Number | timeScale (Number), passed into Time.timeScale = timeScale. Unwired, this is 0. | 0 |

**Example:** In a script this is Time.timeScale = timeScale.

**Related:** Other Time nodes

## Engine API / Debug

### Debug.Log(message)

`Debug.Log/1`

Print a message to the Console.

**Requires:** No component on this object is required. Debug is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| message | in | Any | message (Any), passed into Debug.Log(message). Unwired, this is nothing. | nothing |

**Example:** In a script this is Debug.Log(message).

**Related:** Other Debug nodes

### Debug.LogWarning(message)

`Debug.LogWarning/1`

Print a warning to the Console.

**Requires:** No component on this object is required. Debug is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| message | in | Any | message (Any), passed into Debug.LogWarning(message). Unwired, this is nothing. | nothing |

**Example:** In a script this is Debug.LogWarning(message).

**Related:** Other Debug nodes

### Debug.LogError(message)

`Debug.LogError/1`

Print an error to the Console.

**Requires:** No component on this object is required. Debug is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| message | in | Any | message (Any), passed into Debug.LogError(message). Unwired, this is nothing. | nothing |

**Example:** In a script this is Debug.LogError(message).

**Related:** Other Debug nodes

## Engine API / Audio

### Audio.masterVolume

`Audio.masterVolume`

Audio.masterVolume — Overall volume, 0 to 1.

**Requires:** No component on this object is required. Audio is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| masterVolume | out | Number | masterVolume (Number), the value Audio.masterVolume gives back. | — |

**Example:** In a script this is Audio.masterVolume.

**Related:** Other Audio nodes

### Set Audio.masterVolume

`Audio.masterVolume=`

Set Audio.masterVolume — Overall volume, 0 to 1.

**Requires:** No component on this object is required. Audio is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| masterVolume | in | Number | masterVolume (Number), passed into Audio.masterVolume = masterVolume. Unwired, this is 0. | 0 |

**Example:** In a script this is Audio.masterVolume = masterVolume.

**Related:** Other Audio nodes

### Audio.muted

`Audio.muted`

Audio.muted — Silence everything.

**Requires:** No component on this object is required. Audio is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| muted | out | Bool | muted (Bool), the value Audio.muted gives back. | — |

**Example:** In a script this is Audio.muted.

**Related:** Other Audio nodes

### Set Audio.muted

`Audio.muted=`

Set Audio.muted — Silence everything.

**Requires:** No component on this object is required. Audio is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| muted | in | Bool | muted (Bool), passed into Audio.muted = muted. Unwired, this is false. | false |

**Example:** In a script this is Audio.muted = muted.

**Related:** Other Audio nodes

### Audio.SetGroupVolume(group, volume)

`Audio.SetGroupVolume/2`

Set one group's volume, 0 to 1 ("Music", "SFX", "UI" or "Voice").

**Requires:** No component on this object is required. Audio is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| group | in | Text | group (Text), passed into Audio.SetGroupVolume(group, volume). Unwired, this is empty text. | empty text |
| volume | in | Number | volume (Number), passed into Audio.SetGroupVolume(group, volume). Unwired, this is 0. | 0 |

**Example:** In a script this is Audio.SetGroupVolume(group, volume).

**Related:** Other Audio nodes

### Audio.GetGroupVolume(group)

`Audio.GetGroupVolume/1`

Audio.GetGroupVolume(group) — One group's volume.

**Requires:** No component on this object is required. Audio is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| group | in | Text | group (Text), passed into Audio.GetGroupVolume(group). Unwired, this is empty text. | empty text |
| Result | out | Number | Result (Number), the value Audio.GetGroupVolume(group) gives back. | — |

**Example:** In a script this is Audio.GetGroupVolume(group).

**Related:** Other Audio nodes

### Audio.MuteGroup(group, mute)

`Audio.MuteGroup/2`

Mute or unmute one group.

**Requires:** No component on this object is required. Audio is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| group | in | Text | group (Text), passed into Audio.MuteGroup(group, mute). Unwired, this is empty text. | empty text |
| mute | in | Bool | mute (Bool), passed into Audio.MuteGroup(group, mute). Unwired, this is false. | false |

**Example:** In a script this is Audio.MuteGroup(group, mute).

**Related:** Other Audio nodes

### Audio.SaveSettings()

`Audio.SaveSettings/0`

Store the volumes in the player's prefs now (they are also saved when the game closes).

**Requires:** No component on this object is required. Audio is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |

**Example:** In a script this is Audio.SaveSettings().

**Related:** Other Audio nodes

## Engine API / SaveGame

### SaveGame.playTime

`SaveGame.playTime`

Seconds played, counting the time in the save that was loaded.

**Requires:** No component on this object is required. SaveGame is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| playTime | out | Number | playTime (Number), the value SaveGame.playTime gives back. | — |

**Example:** In a script this is SaveGame.playTime.

**Related:** Other SaveGame nodes

### SaveGame.Save(slot, label)

`SaveGame.Save/2`

Save the game to a slot ("1", "quick"...) with a label for the load menu. False if it couldn't.

**Requires:** No component on this object is required. SaveGame is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| slot | in | Text | slot (Text), passed into SaveGame.Save(slot, label). Unwired, this is empty text. | empty text |
| label | in | Text | label (Text), passed into SaveGame.Save(slot, label). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value SaveGame.Save(slot, label) gives back. | — |

**Example:** In a script this is SaveGame.Save(slot, label).

**Related:** Other SaveGame nodes

### SaveGame.Load(slot)

`SaveGame.Load/1`

Load a slot at the end of this frame. False if there's no save there.

**Requires:** No component on this object is required. SaveGame is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| slot | in | Text | slot (Text), passed into SaveGame.Load(slot). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value SaveGame.Load(slot) gives back. | — |

**Example:** In a script this is SaveGame.Load(slot).

**Related:** Other SaveGame nodes

### SaveGame.Exists(slot)

`SaveGame.Exists/1`

True if the slot has a save.

**Requires:** No component on this object is required. SaveGame is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| slot | in | Text | slot (Text), passed into SaveGame.Exists(slot). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value SaveGame.Exists(slot) gives back. | — |

**Example:** In a script this is SaveGame.Exists(slot).

**Related:** Other SaveGame nodes

### SaveGame.Delete(slot)

`SaveGame.Delete/1`

Delete the save in a slot.

**Requires:** No component on this object is required. SaveGame is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| slot | in | Text | slot (Text), passed into SaveGame.Delete(slot). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value SaveGame.Delete(slot) gives back. | — |

**Example:** In a script this is SaveGame.Delete(slot).

**Related:** Other SaveGame nodes

### SaveGame.Label(slot)

`SaveGame.Label/1`

The label a slot was saved with ("" if empty).

**Requires:** No component on this object is required. SaveGame is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| slot | in | Text | slot (Text), passed into SaveGame.Label(slot). Unwired, this is empty text. | empty text |
| Result | out | Text | Result (Text), the value SaveGame.Label(slot) gives back. | — |

**Example:** In a script this is SaveGame.Label(slot).

**Related:** Other SaveGame nodes

## Engine API / Localization

### Localization.Language

`Localization.Language`

The current language's code ("en", "fr"...).

**Requires:** No component on this object is required. Localization is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Language | out | Text | Language (Text), the value Localization.Language gives back. | — |

**Example:** In a script this is Localization.Language.

**Related:** Other Localization nodes

### Localization.Languages

`Localization.Languages`

The codes of the string table's languages, in the table's order.

**Requires:** No component on this object is required. Localization is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Languages | out | List | Languages (List), the value Localization.Languages gives back. | — |

**Example:** In a script this is Localization.Languages.

**Related:** Other Localization nodes

### Localization.Get(key)

`Localization.Get/1`

The key's text in the player's language (the default language's when it has none, else the key).

**Requires:** No component on this object is required. Localization is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| key | in | Text | key (Text), passed into Localization.Get(key). Unwired, this is empty text. | empty text |
| Result | out | Text | Result (Text), the value Localization.Get(key) gives back. | — |

**Example:** In a script this is Localization.Get(key).

**Related:** Other Localization nodes

### Localization.Get(key, arg0)

`Localization.Get/2`

The key's text with {0} filled in; a number also picks the plural form.

**Requires:** No component on this object is required. Localization is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| key | in | Text | key (Text), passed into Localization.Get(key, arg0). Unwired, this is empty text. | empty text |
| arg0 | in | Any | arg0 (Any), passed into Localization.Get(key, arg0). Unwired, this is nothing. | nothing |
| Result | out | Text | Result (Text), the value Localization.Get(key, arg0) gives back. | — |

**Example:** In a script this is Localization.Get(key, arg0).

**Related:** Other Localization nodes

### Localization.Get(key, arg0, arg1)

`Localization.Get/3`

The key's text with {0} and {1} filled in.

**Requires:** No component on this object is required. Localization is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| key | in | Text | key (Text), passed into Localization.Get(key, arg0, arg1). Unwired, this is empty text. | empty text |
| arg0 | in | Any | arg0 (Any), passed into Localization.Get(key, arg0, arg1). Unwired, this is nothing. | nothing |
| arg1 | in | Any | arg1 (Any), passed into Localization.Get(key, arg0, arg1). Unwired, this is nothing. | nothing |
| Result | out | Text | Result (Text), the value Localization.Get(key, arg0, arg1) gives back. | — |

**Example:** In a script this is Localization.Get(key, arg0, arg1).

**Related:** Other Localization nodes

### Localization.Get(key, arg0, arg1, arg2)

`Localization.Get/4`

The key's text with {0}, {1} and {2} filled in.

**Requires:** No component on this object is required. Localization is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| key | in | Text | key (Text), passed into Localization.Get(key, arg0, arg1, arg2). Unwired, this is empty text. | empty text |
| arg0 | in | Any | arg0 (Any), passed into Localization.Get(key, arg0, arg1, arg2). Unwired, this is nothing. | nothing |
| arg1 | in | Any | arg1 (Any), passed into Localization.Get(key, arg0, arg1, arg2). Unwired, this is nothing. | nothing |
| arg2 | in | Any | arg2 (Any), passed into Localization.Get(key, arg0, arg1, arg2). Unwired, this is nothing. | nothing |
| Result | out | Text | Result (Text), the value Localization.Get(key, arg0, arg1, arg2) gives back. | — |

**Example:** In a script this is Localization.Get(key, arg0, arg1, arg2).

**Related:** Other Localization nodes

### Localization.SetLanguage(code)

`Localization.SetLanguage/1`

Show every localized text in a language of the string table ("fr") and remember it for the next run. False when the table doesn't have it.

**Requires:** No component on this object is required. Localization is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| code | in | Text | code (Text), passed into Localization.SetLanguage(code). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Localization.SetLanguage(code) gives back. | — |

**Example:** In a script this is Localization.SetLanguage(code).

**Related:** Other Localization nodes

### Localization.LanguageName(code)

`Localization.LanguageName/1`

A language's name for a menu ("Français" for "fr").

**Requires:** No component on this object is required. Localization is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| code | in | Text | code (Text), passed into Localization.LanguageName(code). Unwired, this is empty text. | empty text |
| Result | out | Text | Result (Text), the value Localization.LanguageName(code) gives back. | — |

**Example:** In a script this is Localization.LanguageName(code).

**Related:** Other Localization nodes

## Engine API / Sequence

### Sequence.isPlaying

`Sequence.isPlaying`

True while a sequence plays.

**Requires:** No component on this object is required. Sequence is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| isPlaying | out | Bool | isPlaying (Bool), the value Sequence.isPlaying gives back. | — |

**Example:** In a script this is Sequence.isPlaying.

**Related:** Other Sequence nodes

### Sequence.time

`Sequence.time`

Seconds into the playing sequence (0 when none plays).

**Requires:** No component on this object is required. Sequence is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| time | out | Number | time (Number), the value Sequence.time gives back. | — |

**Example:** In a script this is Sequence.time.

**Related:** Other Sequence nodes

### Sequence.lastEvent

`Sequence.lastEvent`

The event being delivered: read it in OnSequenceEvent().

**Requires:** No component on this object is required. Sequence is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| lastEvent | out | Text | lastEvent (Text), the value Sequence.lastEvent gives back. | — |

**Example:** In a script this is Sequence.lastEvent.

**Related:** Other Sequence nodes

### Sequence.Play(name)

`Sequence.Play/1`

Play the sequence named like this (a Sequence Player's file, or Assets/<name>.okayseq). False if there is none.

**Requires:** No component on this object is required. Sequence is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| name | in | Text | name (Text), passed into Sequence.Play(name). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Sequence.Play(name) gives back. | — |

**Example:** In a script this is Sequence.Play(name).

**Related:** Other Sequence nodes

### Sequence.Stop()

`Sequence.Stop/0`

Stop every playing sequence; the camera goes back to the game.

**Requires:** No component on this object is required. Sequence is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |

**Example:** In a script this is Sequence.Stop().

**Related:** Other Sequence nodes

## Engine API / PostProcess

### PostProcess.exposure

`PostProcess.exposure`

Scene brightness multiplier (1 = as lit).

**Requires:** No component on this object is required. PostProcess is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| exposure | out | Number | exposure (Number), the value PostProcess.exposure gives back. | — |

**Example:** In a script this is PostProcess.exposure.

**Related:** Other PostProcess nodes

### Set PostProcess.exposure

`PostProcess.exposure=`

Scene brightness multiplier (1 = as lit).

**Requires:** No component on this object is required. PostProcess is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| exposure | in | Number | exposure (Number), passed into PostProcess.exposure = exposure. Unwired, this is 0. | 0 |

**Example:** In a script this is PostProcess.exposure = exposure.

**Related:** Other PostProcess nodes

### PostProcess.Set(name, value)

`PostProcess.Set/2`

Set one value by name: "exposure", "tonemap" (0 none, 1 ACES, 2 filmic), "bloom" (0/1), "bloom_threshold", "bloom_intensity", "bloom_radius", "grading", "temperature", "tint", "saturation", "contrast", "vignette", "vignette_smoothness", "fxaa", "ssao", "ssao_radius", "ssao_intensity", "dof", "dof_focus", "dof_range". False for a name it doesn't know.

**Requires:** No component on this object is required. PostProcess is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| name | in | Text | name (Text), passed into PostProcess.Set(name, value). Unwired, this is empty text. | empty text |
| value | in | Number | value (Number), passed into PostProcess.Set(name, value). Unwired, this is 0. | 0 |
| Result | out | Bool | Result (Bool), the value PostProcess.Set(name, value) gives back. | — |

**Example:** In a script this is PostProcess.Set(name, value).

**Related:** Other PostProcess nodes

## Engine API / Fog

### Fog.enabled

`Fog.enabled`

Fog.enabled — Whether the fog is on.

**Requires:** No component on this object is required. Fog is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| enabled | out | Bool | enabled (Bool), the value Fog.enabled gives back. | — |

**Example:** In a script this is Fog.enabled.

**Related:** Other Fog nodes

### Set Fog.enabled

`Fog.enabled=`

Set Fog.enabled — Whether the fog is on.

**Requires:** No component on this object is required. Fog is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| enabled | in | Bool | enabled (Bool), passed into Fog.enabled = enabled. Unwired, this is false. | false |

**Example:** In a script this is Fog.enabled = enabled.

**Related:** Other Fog nodes

### Fog.Set(enabled, start, end)

`Fog.Set/3`

Turn the fog on or off, fading from start to end (world units from the camera) in Linear mode.

**Requires:** No component on this object is required. Fog is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| enabled | in | Bool | enabled (Bool), passed into Fog.Set(enabled, start, end). Unwired, this is false. | false |
| start | in | Number | start (Number), passed into Fog.Set(enabled, start, end). Unwired, this is 0. | 0 |
| end | in | Number | end (Number), passed into Fog.Set(enabled, start, end). Unwired, this is 0. | 0 |

**Example:** In a script this is Fog.Set(enabled, start, end).

**Related:** Other Fog nodes

### Fog.SetColor(r, g, b)

`Fog.SetColor/3`

The fog colour (0 to 1 each). Stops matching the sky's horizon.

**Requires:** No component on this object is required. Fog is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| r | in | Number | r (Number), passed into Fog.SetColor(r, g, b). Unwired, this is 0. | 0 |
| g | in | Number | g (Number), passed into Fog.SetColor(r, g, b). Unwired, this is 0. | 0 |
| b | in | Number | b (Number), passed into Fog.SetColor(r, g, b). Unwired, this is 0. | 0 |

**Example:** In a script this is Fog.SetColor(r, g, b).

**Related:** Other Fog nodes

### Fog.SetMode(mode, density)

`Fog.SetMode/2`

"linear", "exponential" or "height", with the density the last two thicken by per unit of distance.

**Requires:** No component on this object is required. Fog is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| mode | in | Text | mode (Text), passed into Fog.SetMode(mode, density). Unwired, this is empty text. | empty text |
| density | in | Number | density (Number), passed into Fog.SetMode(mode, density). Unwired, this is 0. | 0 |

**Example:** In a script this is Fog.SetMode(mode, density).

**Related:** Other Fog nodes

## Engine API / Weather

### Weather.preset

`Weather.preset`

The current preset's name.

**Requires:** No component on this object is required. Weather is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| preset | out | Text | preset (Text), the value Weather.preset gives back. | — |

**Example:** In a script this is Weather.preset.

**Related:** Other Weather nodes

### Weather.Set(preset, intensity)

`Weather.Set/2`

"none", "rain", "snow" or "storm" at an intensity from 0 to 1; it cross-fades over the scene's weather fade time.

**Requires:** No component on this object is required. Weather is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| preset | in | Text | preset (Text), passed into Weather.Set(preset, intensity). Unwired, this is empty text. | empty text |
| intensity | in | Number | intensity (Number), passed into Weather.Set(preset, intensity). Unwired, this is 0. | 0 |

**Example:** In a script this is Weather.Set(preset, intensity).

**Related:** Other Weather nodes

### Weather.SetWind(x, y, z)

`Weather.SetWind/3`

The wind (world units per second) that particles drift with and water waves follow.

**Requires:** No component on this object is required. Weather is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| x | in | Number | x (Number), passed into Weather.SetWind(x, y, z). Unwired, this is 0. | 0 |
| y | in | Number | y (Number), passed into Weather.SetWind(x, y, z). Unwired, this is 0. | 0 |
| z | in | Number | z (Number), passed into Weather.SetWind(x, y, z). Unwired, this is 0. | 0 |

**Example:** In a script this is Weather.SetWind(x, y, z).

**Related:** Other Weather nodes

## Engine API / NavMeshAgent

### NavMeshAgent.speed

`NavMeshAgent.speed`

Top speed in units per second.

**Requires:** Needs a NavMeshAgent on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose NavMeshAgent this call uses. Leave it empty to use this object. It is an Object. | this object |
| speed | out | Number | speed (Number), the value GetComponent<NavMeshAgent>().speed gives back. | — |

**Example:** In a script this is GetComponent<NavMeshAgent>().speed.

**Related:** Other NavMeshAgent nodes

### Set NavMeshAgent.speed

`NavMeshAgent.speed=`

Top speed in units per second.

**Requires:** Needs a NavMeshAgent on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| speed | in | Number | speed (Number), passed into GetComponent<NavMeshAgent>().speed = speed. Unwired, this is 0. | 0 |
| Target | in | Object | The object whose NavMeshAgent this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<NavMeshAgent>().speed = speed.

**Related:** Other NavMeshAgent nodes

### NavMeshAgent.stoppingDistance

`NavMeshAgent.stoppingDistance`

How close to the destination counts as there.

**Requires:** Needs a NavMeshAgent on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose NavMeshAgent this call uses. Leave it empty to use this object. It is an Object. | this object |
| stoppingDistance | out | Number | stoppingDistance (Number), the value GetComponent<NavMeshAgent>().stoppingDistance gives back. | — |

**Example:** In a script this is GetComponent<NavMeshAgent>().stoppingDistance.

**Related:** Other NavMeshAgent nodes

### Set NavMeshAgent.stoppingDistance

`NavMeshAgent.stoppingDistance=`

How close to the destination counts as there.

**Requires:** Needs a NavMeshAgent on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| stoppingDistance | in | Number | stoppingDistance (Number), passed into GetComponent<NavMeshAgent>().stoppingDistance = stoppingDistance. Unwired, this is 0. | 0 |
| Target | in | Object | The object whose NavMeshAgent this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<NavMeshAgent>().stoppingDistance = stoppingDistance.

**Related:** Other NavMeshAgent nodes

### NavMeshAgent.isStopped

`NavMeshAgent.isStopped`

True after Stop, until Resume or a new destination.

**Requires:** Needs a NavMeshAgent on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose NavMeshAgent this call uses. Leave it empty to use this object. It is an Object. | this object |
| isStopped | out | Bool | isStopped (Bool), the value GetComponent<NavMeshAgent>().isStopped gives back. | — |

**Example:** In a script this is GetComponent<NavMeshAgent>().isStopped.

**Related:** Other NavMeshAgent nodes

### NavMeshAgent.velocity

`NavMeshAgent.velocity`

How it is moving right now, in units per second.

**Requires:** Needs a NavMeshAgent on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose NavMeshAgent this call uses. Leave it empty to use this object. It is an Object. | this object |
| velocity | out | Vector3 | velocity (Vector3), the value GetComponent<NavMeshAgent>().velocity gives back. | — |

**Example:** In a script this is GetComponent<NavMeshAgent>().velocity.

**Related:** Other NavMeshAgent nodes

### NavMeshAgent.destination

`NavMeshAgent.destination`

The point it was last sent to.

**Requires:** Needs a NavMeshAgent on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose NavMeshAgent this call uses. Leave it empty to use this object. It is an Object. | this object |
| destination | out | Vector3 | destination (Vector3), the value GetComponent<NavMeshAgent>().destination gives back. | — |

**Example:** In a script this is GetComponent<NavMeshAgent>().destination.

**Related:** Other NavMeshAgent nodes

### NavMeshAgent.SetDestination(point)

`NavMeshAgent.SetDestination/1`

Plan a path to a world point and start walking there.

**Requires:** Needs a NavMeshAgent on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| point | in | Vector3 | point (Vector3), passed into GetComponent<NavMeshAgent>().SetDestination(point). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose NavMeshAgent this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<NavMeshAgent>().SetDestination(point).

**Related:** Other NavMeshAgent nodes

### NavMeshAgent.Stop()

`NavMeshAgent.Stop/0`

Stop where it is (Resume continues to the same point).

**Requires:** Needs a NavMeshAgent on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| Target | in | Object | The object whose NavMeshAgent this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<NavMeshAgent>().Stop().

**Related:** Other NavMeshAgent nodes

### NavMeshAgent.Resume()

`NavMeshAgent.Resume/0`

Carry on to the destination after Stop.

**Requires:** Needs a NavMeshAgent on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| Target | in | Object | The object whose NavMeshAgent this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<NavMeshAgent>().Resume().

**Related:** Other NavMeshAgent nodes

### NavMeshAgent.HasPath()

`NavMeshAgent.HasPath/0`

True while it is walking to a destination.

**Requires:** Needs a NavMeshAgent on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose NavMeshAgent this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Bool | Result (Bool), the value GetComponent<NavMeshAgent>().HasPath() gives back. | — |

**Example:** In a script this is GetComponent<NavMeshAgent>().HasPath().

**Related:** Other NavMeshAgent nodes

### NavMeshAgent.RemainingDistance()

`NavMeshAgent.RemainingDistance/0`

Path length left to the destination (0 when there is none).

**Requires:** Needs a NavMeshAgent on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose NavMeshAgent this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Number | Result (Number), the value GetComponent<NavMeshAgent>().RemainingDistance() gives back. | — |

**Example:** In a script this is GetComponent<NavMeshAgent>().RemainingDistance().

**Related:** Other NavMeshAgent nodes

### NavMeshAgent.Arrived()

`NavMeshAgent.Arrived/0`

True once the last destination was reached (until the next SetDestination).

**Requires:** Needs a NavMeshAgent on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose NavMeshAgent this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Bool | Result (Bool), the value GetComponent<NavMeshAgent>().Arrived() gives back. | — |

**Example:** In a script this is GetComponent<NavMeshAgent>().Arrived().

**Related:** Other NavMeshAgent nodes

## Engine API / Dialogue

### Dialogue.Start(name)

`Dialogue.Start/1`

Start the conversation on the object (or graph) with this name; "" means this object's runner. False when there is none.

**Requires:** No component on this object is required. Dialogue is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| name | in | Text | name (Text), passed into Dialogue.Start(name). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Dialogue.Start(name) gives back. | — |

**Example:** In a script this is Dialogue.Start(name).

**Related:** Other Dialogue nodes

### Dialogue.Stop()

`Dialogue.Stop/0`

End the running conversation.

**Requires:** No component on this object is required. Dialogue is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |

**Example:** In a script this is Dialogue.Stop().

**Related:** Other Dialogue nodes

### Dialogue.IsActive()

`Dialogue.IsActive/0`

True while a conversation runs.

**Requires:** No component on this object is required. Dialogue is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Result | out | Bool | Result (Bool), the value Dialogue.IsActive() gives back. | — |

**Example:** In a script this is Dialogue.IsActive().

**Related:** Other Dialogue nodes

## Engine API / Story

### Story.Set(name, value)

`Story.Set/2`

Story.Set(name, value) — Set a number variable.

**Requires:** No component on this object is required. Story is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| name | in | Text | name (Text), passed into Story.Set(name, value). Unwired, this is empty text. | empty text |
| value | in | Number | value (Number), passed into Story.Set(name, value). Unwired, this is 0. | 0 |

**Example:** In a script this is Story.Set(name, value).

**Related:** Other Story nodes

### Story.SetText(name, value)

`Story.SetText/2`

Story.SetText(name, value) — Set a text variable.

**Requires:** No component on this object is required. Story is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| name | in | Text | name (Text), passed into Story.SetText(name, value). Unwired, this is empty text. | empty text |
| value | in | Text | value (Text), passed into Story.SetText(name, value). Unwired, this is empty text. | empty text |

**Example:** In a script this is Story.SetText(name, value).

**Related:** Other Story nodes

### Story.Get(name)

`Story.Get/1`

A variable as a number (0 when unset).

**Requires:** No component on this object is required. Story is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| name | in | Text | name (Text), passed into Story.Get(name). Unwired, this is empty text. | empty text |
| Result | out | Number | Result (Number), the value Story.Get(name) gives back. | — |

**Example:** In a script this is Story.Get(name).

**Related:** Other Story nodes

### Story.GetText(name)

`Story.GetText/1`

A variable as text ("" when unset).

**Requires:** No component on this object is required. Story is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| name | in | Text | name (Text), passed into Story.GetText(name). Unwired, this is empty text. | empty text |
| Result | out | Text | Result (Text), the value Story.GetText(name) gives back. | — |

**Example:** In a script this is Story.GetText(name).

**Related:** Other Story nodes

### Story.Has(name)

`Story.Has/1`

True once the variable was set.

**Requires:** No component on this object is required. Story is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| name | in | Text | name (Text), passed into Story.Has(name). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Story.Has(name) gives back. | — |

**Example:** In a script this is Story.Has(name).

**Related:** Other Story nodes

### Story.SetFlag(name, on)

`Story.SetFlag/2`

Story.SetFlag(name, on) — Set or clear a flag.

**Requires:** No component on this object is required. Story is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| name | in | Text | name (Text), passed into Story.SetFlag(name, on). Unwired, this is empty text. | empty text |
| on | in | Bool | on (Bool), passed into Story.SetFlag(name, on). Unwired, this is false. | false |

**Example:** In a script this is Story.SetFlag(name, on).

**Related:** Other Story nodes

### Story.HasFlag(name)

`Story.HasFlag/1`

True while the flag is set.

**Requires:** No component on this object is required. Story is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| name | in | Text | name (Text), passed into Story.HasFlag(name). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Story.HasFlag(name) gives back. | — |

**Example:** In a script this is Story.HasFlag(name).

**Related:** Other Story nodes

## Engine API / Quest

### Quest.Start(id)

`Quest.Start/1`

Start a quest. False when it is unknown, already active or its prerequisites aren't done.

**Requires:** No component on this object is required. Quest is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| id | in | Text | id (Text), passed into Quest.Start(id). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Quest.Start(id) gives back. | — |

**Example:** In a script this is Quest.Start(id).

**Related:** Other Quest nodes

### Quest.Complete(id)

`Quest.Complete/1`

Complete an active quest outright (its rewards are granted).

**Requires:** No component on this object is required. Quest is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| id | in | Text | id (Text), passed into Quest.Complete(id). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Quest.Complete(id) gives back. | — |

**Example:** In a script this is Quest.Complete(id).

**Related:** Other Quest nodes

### Quest.CompleteObjective(id, index)

`Quest.CompleteObjective/2`

Mark one objective (0-based) of an active quest done.

**Requires:** No component on this object is required. Quest is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| id | in | Text | id (Text), passed into Quest.CompleteObjective(id, index). Unwired, this is empty text. | empty text |
| index | in | Number | index (Number), passed into Quest.CompleteObjective(id, index). Unwired, this is 0. | 0 |
| Result | out | Bool | Result (Bool), the value Quest.CompleteObjective(id, index) gives back. | — |

**Example:** In a script this is Quest.CompleteObjective(id, index).

**Related:** Other Quest nodes

### Quest.AddProgress(id, index, amount)

`Quest.AddProgress/3`

Count toward a collect / kill style objective.

**Requires:** No component on this object is required. Quest is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| id | in | Text | id (Text), passed into Quest.AddProgress(id, index, amount). Unwired, this is empty text. | empty text |
| index | in | Number | index (Number), passed into Quest.AddProgress(id, index, amount). Unwired, this is 0. | 0 |
| amount | in | Number | amount (Number), passed into Quest.AddProgress(id, index, amount). Unwired, this is 0. | 0 |
| Result | out | Bool | Result (Bool), the value Quest.AddProgress(id, index, amount) gives back. | — |

**Example:** In a script this is Quest.AddProgress(id, index, amount).

**Related:** Other Quest nodes

### Quest.Fail(id)

`Quest.Fail/1`

Quest.Fail(id) — Fail an active quest.

**Requires:** No component on this object is required. Quest is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| id | in | Text | id (Text), passed into Quest.Fail(id). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Quest.Fail(id) gives back. | — |

**Example:** In a script this is Quest.Fail(id).

**Related:** Other Quest nodes

### Quest.State(id)

`Quest.State/1`

"inactive", "active", "completed" or "failed".

**Requires:** No component on this object is required. Quest is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| id | in | Text | id (Text), passed into Quest.State(id). Unwired, this is empty text. | empty text |
| Result | out | Text | Result (Text), the value Quest.State(id) gives back. | — |

**Example:** In a script this is Quest.State(id).

**Related:** Other Quest nodes

### Quest.IsActive(id)

`Quest.IsActive/1`

True while the quest is active.

**Requires:** No component on this object is required. Quest is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| id | in | Text | id (Text), passed into Quest.IsActive(id). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value Quest.IsActive(id) gives back. | — |

**Example:** In a script this is Quest.IsActive(id).

**Related:** Other Quest nodes

## Engine API / BehaviorTree

### BehaviorTree.isRunning

`BehaviorTree.isRunning`

True while the tree is being ticked.

**Requires:** No component on this object is required. BehaviorTree is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| isRunning | out | Bool | isRunning (Bool), the value BehaviorTree.isRunning gives back. | — |

**Example:** In a script this is BehaviorTree.isRunning.

**Related:** Other BehaviorTree nodes

### BehaviorTree.Set(key, value)

`BehaviorTree.Set/2`

Write a blackboard value: a number, bool, string, Vector3 or GameObject.

**Requires:** No component on this object is required. BehaviorTree is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| key | in | Text | key (Text), passed into BehaviorTree.Set(key, value). Unwired, this is empty text. | empty text |
| value | in | Any | value (Any), passed into BehaviorTree.Set(key, value). Unwired, this is nothing. | nothing |

**Example:** In a script this is BehaviorTree.Set(key, value).

**Related:** Other BehaviorTree nodes

### BehaviorTree.Get(key)

`BehaviorTree.Get/1`

Read a blackboard value as whatever it is (use GetFloat / GetBool / GetString / GetVector / GetObject for a typed one).

**Requires:** No component on this object is required. BehaviorTree is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| key | in | Text | key (Text), passed into BehaviorTree.Get(key). Unwired, this is empty text. | empty text |
| Result | out | Any | Result (Any), the value BehaviorTree.Get(key) gives back. | — |

**Example:** In a script this is BehaviorTree.Get(key).

**Related:** Other BehaviorTree nodes

### BehaviorTree.GetFloat(key)

`BehaviorTree.GetFloat/1`

A blackboard number (0 when the key is missing).

**Requires:** No component on this object is required. BehaviorTree is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| key | in | Text | key (Text), passed into BehaviorTree.GetFloat(key). Unwired, this is empty text. | empty text |
| Result | out | Number | Result (Number), the value BehaviorTree.GetFloat(key) gives back. | — |

**Example:** In a script this is BehaviorTree.GetFloat(key).

**Related:** Other BehaviorTree nodes

### BehaviorTree.GetBool(key)

`BehaviorTree.GetBool/1`

A blackboard bool (false when the key is missing).

**Requires:** No component on this object is required. BehaviorTree is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| key | in | Text | key (Text), passed into BehaviorTree.GetBool(key). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value BehaviorTree.GetBool(key) gives back. | — |

**Example:** In a script this is BehaviorTree.GetBool(key).

**Related:** Other BehaviorTree nodes

### BehaviorTree.GetString(key)

`BehaviorTree.GetString/1`

A blackboard string ("" when the key is missing).

**Requires:** No component on this object is required. BehaviorTree is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| key | in | Text | key (Text), passed into BehaviorTree.GetString(key). Unwired, this is empty text. | empty text |
| Result | out | Text | Result (Text), the value BehaviorTree.GetString(key) gives back. | — |

**Example:** In a script this is BehaviorTree.GetString(key).

**Related:** Other BehaviorTree nodes

### BehaviorTree.GetVector(key)

`BehaviorTree.GetVector/1`

A blackboard vector, or an object key's position.

**Requires:** No component on this object is required. BehaviorTree is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| key | in | Text | key (Text), passed into BehaviorTree.GetVector(key). Unwired, this is empty text. | empty text |
| Result | out | Vector3 | Result (Vector3), the value BehaviorTree.GetVector(key) gives back. | — |

**Example:** In a script this is BehaviorTree.GetVector(key).

**Related:** Other BehaviorTree nodes

### BehaviorTree.GetObject(key)

`BehaviorTree.GetObject/1`

The object a blackboard key refers to, or null.

**Requires:** No component on this object is required. BehaviorTree is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| key | in | Text | key (Text), passed into BehaviorTree.GetObject(key). Unwired, this is empty text. | empty text |
| Result | out | Object | Result (Object), the value BehaviorTree.GetObject(key) gives back. | — |

**Example:** In a script this is BehaviorTree.GetObject(key).

**Related:** Other BehaviorTree nodes

### BehaviorTree.Run(tree)

`BehaviorTree.Run/1`

Load a .okaybt file and start it ("" starts the current tree over). False when the file can't be read.

**Requires:** No component on this object is required. BehaviorTree is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| tree | in | Text | tree (Text), passed into BehaviorTree.Run(tree). Unwired, this is empty text. | empty text |
| Result | out | Bool | Result (Bool), the value BehaviorTree.Run(tree) gives back. | — |

**Example:** In a script this is BehaviorTree.Run(tree).

**Related:** Other BehaviorTree nodes

### BehaviorTree.Stop()

`BehaviorTree.Stop/0`

Stop the tree; running tasks are aborted and the NPC stops.

**Requires:** No component on this object is required. BehaviorTree is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |

**Example:** In a script this is BehaviorTree.Stop().

**Related:** Other BehaviorTree nodes

## Engine API / Spline

### Spline.pointCount

`Spline.pointCount`

How many control points it has.

**Requires:** Needs a Spline on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Spline this call uses. Leave it empty to use this object. It is an Object. | this object |
| pointCount | out | Number | pointCount (Number), the value GetComponent<Spline>().pointCount gives back. | — |

**Example:** In a script this is GetComponent<Spline>().pointCount.

**Related:** Other Spline nodes

### Spline.closed

`Spline.closed`

Whether the last point joins back to the first.

**Requires:** Needs a Spline on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Spline this call uses. Leave it empty to use this object. It is an Object. | this object |
| closed | out | Bool | closed (Bool), the value GetComponent<Spline>().closed gives back. | — |

**Example:** In a script this is GetComponent<Spline>().closed.

**Related:** Other Spline nodes

### Set Spline.closed

`Spline.closed=`

Whether the last point joins back to the first.

**Requires:** Needs a Spline on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| closed | in | Bool | closed (Bool), passed into GetComponent<Spline>().closed = closed. Unwired, this is false. | false |
| Target | in | Object | The object whose Spline this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Spline>().closed = closed.

**Related:** Other Spline nodes

### Spline.EvaluatePosition(t)

`Spline.EvaluatePosition/1`

The world point a fraction t (0 to 1) of the way along. Even steps of t are even steps on the curve.

**Requires:** Needs a Spline on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| t | in | Number | t (Number), passed into GetComponent<Spline>().EvaluatePosition(t). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose Spline this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Vector3 | Result (Vector3), the value GetComponent<Spline>().EvaluatePosition(t) gives back. | — |

**Example:** In a script this is GetComponent<Spline>().EvaluatePosition(t).

**Related:** Other Spline nodes

### Spline.EvaluateTangent(t)

`Spline.EvaluateTangent/1`

The direction of travel a fraction t of the way along (a unit vector).

**Requires:** Needs a Spline on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| t | in | Number | t (Number), passed into GetComponent<Spline>().EvaluateTangent(t). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose Spline this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Vector3 | Result (Vector3), the value GetComponent<Spline>().EvaluateTangent(t) gives back. | — |

**Example:** In a script this is GetComponent<Spline>().EvaluateTangent(t).

**Related:** Other Spline nodes

### Spline.Length()

`Spline.Length/0`

The length of the curve in world units.

**Requires:** Needs a Spline on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Spline this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Number | Result (Number), the value GetComponent<Spline>().Length() gives back. | — |

**Example:** In a script this is GetComponent<Spline>().Length().

**Related:** Other Spline nodes

### Spline.NearestPoint(position)

`Spline.NearestPoint/1`

The point on the curve closest to a world position.

**Requires:** Needs a Spline on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| position | in | Vector3 | position (Vector3), passed into GetComponent<Spline>().NearestPoint(position). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose Spline this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Vector3 | Result (Vector3), the value GetComponent<Spline>().NearestPoint(position) gives back. | — |

**Example:** In a script this is GetComponent<Spline>().NearestPoint(position).

**Related:** Other Spline nodes

## Engine API / SplineFollower

### SplineFollower.speed

`SplineFollower.speed`

Units per second along the curve (negative runs backwards).

**Requires:** Needs a SplineFollower on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose SplineFollower this call uses. Leave it empty to use this object. It is an Object. | this object |
| speed | out | Number | speed (Number), the value GetComponent<SplineFollower>().speed gives back. | — |

**Example:** In a script this is GetComponent<SplineFollower>().speed.

**Related:** Other SplineFollower nodes

### Set SplineFollower.speed

`SplineFollower.speed=`

Units per second along the curve (negative runs backwards).

**Requires:** Needs a SplineFollower on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| speed | in | Number | speed (Number), passed into GetComponent<SplineFollower>().speed = speed. Unwired, this is 0. | 0 |
| Target | in | Object | The object whose SplineFollower this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<SplineFollower>().speed = speed.

**Related:** Other SplineFollower nodes

### SplineFollower.distance

`SplineFollower.distance`

How far along the curve it is (setting it jumps there).

**Requires:** Needs a SplineFollower on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose SplineFollower this call uses. Leave it empty to use this object. It is an Object. | this object |
| distance | out | Number | distance (Number), the value GetComponent<SplineFollower>().distance gives back. | — |

**Example:** In a script this is GetComponent<SplineFollower>().distance.

**Related:** Other SplineFollower nodes

### Set SplineFollower.distance

`SplineFollower.distance=`

How far along the curve it is (setting it jumps there).

**Requires:** Needs a SplineFollower on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| distance | in | Number | distance (Number), passed into GetComponent<SplineFollower>().distance = distance. Unwired, this is 0. | 0 |
| Target | in | Object | The object whose SplineFollower this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<SplineFollower>().distance = distance.

**Related:** Other SplineFollower nodes

### SplineFollower.isPlaying

`SplineFollower.isPlaying`

SplineFollower.isPlaying — True while it moves.

**Requires:** Needs a SplineFollower on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose SplineFollower this call uses. Leave it empty to use this object. It is an Object. | this object |
| isPlaying | out | Bool | isPlaying (Bool), the value GetComponent<SplineFollower>().isPlaying gives back. | — |

**Example:** In a script this is GetComponent<SplineFollower>().isPlaying.

**Related:** Other SplineFollower nodes

### SplineFollower.Play()

`SplineFollower.Play/0`

Start (or carry on) moving.

**Requires:** Needs a SplineFollower on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| Target | in | Object | The object whose SplineFollower this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<SplineFollower>().Play().

**Related:** Other SplineFollower nodes

### SplineFollower.Stop()

`SplineFollower.Stop/0`

SplineFollower.Stop() — Stop where it is.

**Requires:** Needs a SplineFollower on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| Target | in | Object | The object whose SplineFollower this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<SplineFollower>().Stop().

**Related:** Other SplineFollower nodes

## Engine API / LineRenderer

### LineRenderer.positionCount

`LineRenderer.positionCount`

How many points the line has (setting it adds or drops points at the end).

**Requires:** Needs a LineRenderer on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose LineRenderer this call uses. Leave it empty to use this object. It is an Object. | this object |
| positionCount | out | Number | positionCount (Number), the value GetComponent<LineRenderer>().positionCount gives back. | — |

**Example:** In a script this is GetComponent<LineRenderer>().positionCount.

**Related:** Other LineRenderer nodes

### Set LineRenderer.positionCount

`LineRenderer.positionCount=`

How many points the line has (setting it adds or drops points at the end).

**Requires:** Needs a LineRenderer on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| positionCount | in | Number | positionCount (Number), passed into GetComponent<LineRenderer>().positionCount = positionCount. Unwired, this is 0. | 0 |
| Target | in | Object | The object whose LineRenderer this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<LineRenderer>().positionCount = positionCount.

**Related:** Other LineRenderer nodes

### LineRenderer.width

`LineRenderer.width`

Width in world units (times the width curve).

**Requires:** Needs a LineRenderer on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose LineRenderer this call uses. Leave it empty to use this object. It is an Object. | this object |
| width | out | Number | width (Number), the value GetComponent<LineRenderer>().width gives back. | — |

**Example:** In a script this is GetComponent<LineRenderer>().width.

**Related:** Other LineRenderer nodes

### Set LineRenderer.width

`LineRenderer.width=`

Width in world units (times the width curve).

**Requires:** Needs a LineRenderer on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| width | in | Number | width (Number), passed into GetComponent<LineRenderer>().width = width. Unwired, this is 0. | 0 |
| Target | in | Object | The object whose LineRenderer this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<LineRenderer>().width = width.

**Related:** Other LineRenderer nodes

### LineRenderer.SetPosition(index, position)

`LineRenderer.SetPosition/2`

Move one point (the line grows to reach a new index).

**Requires:** Needs a LineRenderer on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| index | in | Number | index (Number), passed into GetComponent<LineRenderer>().SetPosition(index, position). Unwired, this is 0. | 0 |
| position | in | Vector3 | position (Vector3), passed into GetComponent<LineRenderer>().SetPosition(index, position). Unwired, this is (0, 0, 0). | (0, 0, 0) |
| Target | in | Object | The object whose LineRenderer this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<LineRenderer>().SetPosition(index, position).

**Related:** Other LineRenderer nodes

### LineRenderer.SetPositions(positions)

`LineRenderer.SetPositions/1`

Replace every point with an array or List of Vector3.

**Requires:** Needs a LineRenderer on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| positions | in | Any | positions (Any), passed into GetComponent<LineRenderer>().SetPositions(positions). Unwired, this is nothing. | nothing |
| Target | in | Object | The object whose LineRenderer this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<LineRenderer>().SetPositions(positions).

**Related:** Other LineRenderer nodes

### LineRenderer.GetPosition(index)

`LineRenderer.GetPosition/1`

LineRenderer.GetPosition(index) — One point of the line.

**Requires:** Needs a LineRenderer on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| index | in | Number | index (Number), passed into GetComponent<LineRenderer>().GetPosition(index). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose LineRenderer this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Vector3 | Result (Vector3), the value GetComponent<LineRenderer>().GetPosition(index) gives back. | — |

**Example:** In a script this is GetComponent<LineRenderer>().GetPosition(index).

**Related:** Other LineRenderer nodes

## Engine API / Animator

### Animator.state

`Animator.state`

Name of the state the graph is in.

**Requires:** Needs a Animator on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose Animator this call uses. Leave it empty to use this object. It is an Object. | this object |
| state | out | Text | state (Text), the value GetComponent<Animator>().state gives back. | — |

**Example:** In a script this is GetComponent<Animator>().state.

**Related:** Other Animator nodes

### Animator.SetTrigger(name)

`Animator.SetTrigger/1`

Fire a trigger parameter (jump, for example).

**Requires:** Needs a Animator on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| name | in | Text | name (Text), passed into GetComponent<Animator>().SetTrigger(name). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose Animator this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Animator>().SetTrigger(name).

**Related:** Other Animator nodes

### Animator.SetBool(name, on)

`Animator.SetBool/2`

Set a bool parameter (crouch, prone, grounded).

**Requires:** Needs a Animator on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| name | in | Text | name (Text), passed into GetComponent<Animator>().SetBool(name, on). Unwired, this is empty text. | empty text |
| on | in | Bool | on (Bool), passed into GetComponent<Animator>().SetBool(name, on). Unwired, this is false. | false |
| Target | in | Object | The object whose Animator this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Animator>().SetBool(name, on).

**Related:** Other Animator nodes

### Animator.SetFloat(name, value)

`Animator.SetFloat/2`

Set a float parameter (speed).

**Requires:** Needs a Animator on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| name | in | Text | name (Text), passed into GetComponent<Animator>().SetFloat(name, value). Unwired, this is empty text. | empty text |
| value | in | Number | value (Number), passed into GetComponent<Animator>().SetFloat(name, value). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose Animator this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Animator>().SetFloat(name, value).

**Related:** Other Animator nodes

### Animator.Play(clip)

`Animator.Play/1`

Play one of the model's clips by name.

**Requires:** Needs a Animator on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| clip | in | Text | clip (Text), passed into GetComponent<Animator>().Play(clip). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose Animator this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<Animator>().Play(clip).

**Related:** Other Animator nodes

### Animator.GetFloat(name)

`Animator.GetFloat/1`

Read a float parameter (speed). 0 if the graph has no such name.

**Requires:** Needs a Animator on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| name | in | Text | name (Text), passed into GetComponent<Animator>().GetFloat(name). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose Animator this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Number | Result (Number), the value GetComponent<Animator>().GetFloat(name) gives back. | — |

**Example:** In a script this is GetComponent<Animator>().GetFloat(name).

**Related:** Other Animator nodes

### Animator.GetBool(name)

`Animator.GetBool/1`

Read a bool parameter (crouch, grounded).

**Requires:** Needs a Animator on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| name | in | Text | name (Text), passed into GetComponent<Animator>().GetBool(name). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose Animator this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Bool | Result (Bool), the value GetComponent<Animator>().GetBool(name) gives back. | — |

**Example:** In a script this is GetComponent<Animator>().GetBool(name).

**Related:** Other Animator nodes

### Animator.GetInt(name)

`Animator.GetInt/1`

Animator.GetInt(name) — Read an int parameter.

**Requires:** Needs a Animator on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| name | in | Text | name (Text), passed into GetComponent<Animator>().GetInt(name). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose Animator this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Number | Result (Number), the value GetComponent<Animator>().GetInt(name) gives back. | — |

**Example:** In a script this is GetComponent<Animator>().GetInt(name).

**Related:** Other Animator nodes

### Animator.SetLayerWeight(layer, weight)

`Animator.SetLayerWeight/2`

Set the mix weight of the Animator layer with this name (on this object or its model).

**Requires:** Needs a Animator on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| layer | in | Text | layer (Text), passed into GetComponent<Animator>().SetLayerWeight(layer, weight). Unwired, this is empty text. | empty text |
| weight | in | Number | weight (Number), passed into GetComponent<Animator>().SetLayerWeight(layer, weight). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose Animator this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Bool | Result (Bool), the value GetComponent<Animator>().SetLayerWeight(layer, weight) gives back. | — |

**Example:** In a script this is GetComponent<Animator>().SetLayerWeight(layer, weight).

**Related:** Other Animator nodes

### Animator.SetLayerWeight(layer, weight)

`Animator.SetLayerWeight/2:if`

Set the mix weight of the Animator layer with this index (0 is the base).

**Requires:** Needs a Animator on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| layer | in | Number | layer (Number), passed into GetComponent<Animator>().SetLayerWeight(layer, weight). Unwired, this is 0. | 0 |
| weight | in | Number | weight (Number), passed into GetComponent<Animator>().SetLayerWeight(layer, weight). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose Animator this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Bool | Result (Bool), the value GetComponent<Animator>().SetLayerWeight(layer, weight) gives back. | — |

**Example:** In a script this is GetComponent<Animator>().SetLayerWeight(layer, weight).

**Related:** Other Animator nodes

### Animator.GetLayerWeight(layer)

`Animator.GetLayerWeight/1`

Mix weight of the Animator layer with this name. 0 if there is no such layer.

**Requires:** Needs a Animator on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| layer | in | Text | layer (Text), passed into GetComponent<Animator>().GetLayerWeight(layer). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose Animator this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Number | Result (Number), the value GetComponent<Animator>().GetLayerWeight(layer) gives back. | — |

**Example:** In a script this is GetComponent<Animator>().GetLayerWeight(layer).

**Related:** Other Animator nodes

### Animator.GetLayerWeight(layer)

`Animator.GetLayerWeight/1:i`

Mix weight of the Animator layer with this index. 0 if there is no such layer.

**Requires:** Needs a Animator on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| layer | in | Number | layer (Number), passed into GetComponent<Animator>().GetLayerWeight(layer). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose Animator this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Number | Result (Number), the value GetComponent<Animator>().GetLayerWeight(layer) gives back. | — |

**Example:** In a script this is GetComponent<Animator>().GetLayerWeight(layer).

**Related:** Other Animator nodes

### Animator.SetRootMotion(on)

`Animator.SetRootMotion/1`

Turn root motion on or off for the model on this object (or its child).

**Requires:** Needs a Animator on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| on | in | Bool | on (Bool), passed into GetComponent<Animator>().SetRootMotion(on). Unwired, this is false. | false |
| Target | in | Object | The object whose Animator this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Bool | Result (Bool), the value GetComponent<Animator>().SetRootMotion(on) gives back. | — |

**Example:** In a script this is GetComponent<Animator>().SetRootMotion(on).

**Related:** Other Animator nodes

### Animator.SetCharacterModel(path)

`Animator.SetCharacterModel/1`

Swap the character model for the file at this path. False if the file does not import.

**Requires:** Needs a Animator on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| path | in | Text | path (Text), passed into GetComponent<Animator>().SetCharacterModel(path). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose Animator this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Bool | Result (Bool), the value GetComponent<Animator>().SetCharacterModel(path) gives back. | — |

**Example:** In a script this is GetComponent<Animator>().SetCharacterModel(path).

**Related:** Other Animator nodes

## Engine API / IK

### IK.LookAt(objectName)

`IK.LookAt/1`

Aim Look-At IK at the object with this name.

**Requires:** Needs a IK on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| objectName | in | Text | objectName (Text), passed into GetComponent<IK>().LookAt(objectName). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose IK this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<IK>().LookAt(objectName).

**Related:** Other IK nodes

### IK.SetWeight(weight)

`IK.SetWeight/1`

Set the weight (0..1) of Look-At, Foot and Full Body IK on this object.

**Requires:** Needs a IK on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| weight | in | Number | weight (Number), passed into GetComponent<IK>().SetWeight(weight). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose IK this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<IK>().SetWeight(weight).

**Related:** Other IK nodes

### IK.SetKindWeight(kind, weight)

`IK.SetKindWeight/2`

Set one IK weight: foot, look, aim, hand_l, hand_r or full. Also looks on children.

**Requires:** Needs a IK on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| kind | in | Text | kind (Text), passed into GetComponent<IK>().SetKindWeight(kind, weight). Unwired, this is empty text. | empty text |
| weight | in | Number | weight (Number), passed into GetComponent<IK>().SetKindWeight(kind, weight). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose IK this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Bool | Result (Bool), the value GetComponent<IK>().SetKindWeight(kind, weight) gives back. | — |

**Example:** In a script this is GetComponent<IK>().SetKindWeight(kind, weight).

**Related:** Other IK nodes

### IK.SetLimbTarget(limb, objectName)

`IK.SetLimbTarget/2`

Reach or look at the object with this name. Limb is hand_l, hand_r, foot_l, foot_r, look, aim or head.

**Requires:** Needs a IK on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| limb | in | Text | limb (Text), passed into GetComponent<IK>().SetLimbTarget(limb, objectName). Unwired, this is empty text. | empty text |
| objectName | in | Text | objectName (Text), passed into GetComponent<IK>().SetLimbTarget(limb, objectName). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose IK this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Bool | Result (Bool), the value GetComponent<IK>().SetLimbTarget(limb, objectName) gives back. | — |

**Example:** In a script this is GetComponent<IK>().SetLimbTarget(limb, objectName).

**Related:** Other IK nodes

### IK.SetLimbTarget(limb, x, y, z)

`IK.SetLimbTarget/4`

Reach or look at a world point. Limb is hand_l, hand_r, foot_l, foot_r, look, aim or head.

**Requires:** Needs a IK on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| limb | in | Text | limb (Text), passed into GetComponent<IK>().SetLimbTarget(limb, x, y, z). Unwired, this is empty text. | empty text |
| x | in | Number | x (Number), passed into GetComponent<IK>().SetLimbTarget(limb, x, y, z). Unwired, this is 0. | 0 |
| y | in | Number | y (Number), passed into GetComponent<IK>().SetLimbTarget(limb, x, y, z). Unwired, this is 0. | 0 |
| z | in | Number | z (Number), passed into GetComponent<IK>().SetLimbTarget(limb, x, y, z). Unwired, this is 0. | 0 |
| Target | in | Object | The object whose IK this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Bool | Result (Bool), the value GetComponent<IK>().SetLimbTarget(limb, x, y, z) gives back. | — |

**Example:** In a script this is GetComponent<IK>().SetLimbTarget(limb, x, y, z).

**Related:** Other IK nodes

### IK.ClearLimbTarget(limb)

`IK.ClearLimbTarget/1`

Drop the goal for that limb so the animation owns it again.

**Requires:** Needs a IK on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| limb | in | Text | limb (Text), passed into GetComponent<IK>().ClearLimbTarget(limb). Unwired, this is empty text. | empty text |
| Target | in | Object | The object whose IK this call uses. Leave it empty to use this object. It is an Object. | this object |
| Result | out | Bool | Result (Bool), the value GetComponent<IK>().ClearLimbTarget(limb) gives back. | — |

**Example:** In a script this is GetComponent<IK>().ClearLimbTarget(limb).

**Related:** Other IK nodes

## Engine API / TrailRenderer

### TrailRenderer.emitting

`TrailRenderer.emitting`

Whether new points are added as it moves (off: the trail fades away).

**Requires:** Needs a TrailRenderer on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| Target | in | Object | The object whose TrailRenderer this call uses. Leave it empty to use this object. It is an Object. | this object |
| emitting | out | Bool | emitting (Bool), the value GetComponent<TrailRenderer>().emitting gives back. | — |

**Example:** In a script this is GetComponent<TrailRenderer>().emitting.

**Related:** Other TrailRenderer nodes

### Set TrailRenderer.emitting

`TrailRenderer.emitting=`

Whether new points are added as it moves (off: the trail fades away).

**Requires:** Needs a TrailRenderer on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| emitting | in | Bool | emitting (Bool), passed into GetComponent<TrailRenderer>().emitting = emitting. Unwired, this is false. | false |
| Target | in | Object | The object whose TrailRenderer this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<TrailRenderer>().emitting = emitting.

**Related:** Other TrailRenderer nodes

### TrailRenderer.Clear()

`TrailRenderer.Clear/0`

Remove the trail at once (it starts again from where the object is).

**Requires:** Needs a TrailRenderer on the target object (this object when Target is empty). Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| flow | in | Flow | The flow arrives here. The call runs when a wire reaches this pin. | — |
| flow | out | Flow | After the call finishes, the flow continues from here. | — |
| Target | in | Object | The object whose TrailRenderer this call uses. Leave it empty to use this object. It is an Object. | this object |

**Example:** In a script this is GetComponent<TrailRenderer>().Clear().

**Related:** Other TrailRenderer nodes

## Engine API / Mathf

### Mathf.PI

`Mathf.PI`

Mathf.PI — 3.14159...

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| PI | out | Number | PI (Number), the value Mathf.PI gives back. | — |

**Example:** In a script this is Mathf.PI.

**Related:** Other Mathf nodes

### Mathf.Abs(value)

`Mathf.Abs/1`

The value without its sign.

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| value | in | Number | value (Number), passed into Mathf.Abs(value). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Abs(value) gives back. | — |

**Example:** In a script this is Mathf.Abs(value).

**Related:** Other Mathf nodes

### Mathf.Sqrt(value)

`Mathf.Sqrt/1`

Mathf.Sqrt(value) — Square root.

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| value | in | Number | value (Number), passed into Mathf.Sqrt(value). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Sqrt(value) gives back. | — |

**Example:** In a script this is Mathf.Sqrt(value).

**Related:** Other Mathf nodes

### Mathf.Sin(value)

`Mathf.Sin/1`

Mathf.Sin(value) — Sine (radians).

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| value | in | Number | value (Number), passed into Mathf.Sin(value). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Sin(value) gives back. | — |

**Example:** In a script this is Mathf.Sin(value).

**Related:** Other Mathf nodes

### Mathf.Cos(value)

`Mathf.Cos/1`

Mathf.Cos(value) — Cosine (radians).

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| value | in | Number | value (Number), passed into Mathf.Cos(value). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Cos(value) gives back. | — |

**Example:** In a script this is Mathf.Cos(value).

**Related:** Other Mathf nodes

### Mathf.Tan(value)

`Mathf.Tan/1`

Mathf.Tan(value) — Tangent (radians).

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| value | in | Number | value (Number), passed into Mathf.Tan(value). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Tan(value) gives back. | — |

**Example:** In a script this is Mathf.Tan(value).

**Related:** Other Mathf nodes

### Mathf.Floor(value)

`Mathf.Floor/1`

Mathf.Floor(value) — Round down.

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| value | in | Number | value (Number), passed into Mathf.Floor(value). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Floor(value) gives back. | — |

**Example:** In a script this is Mathf.Floor(value).

**Related:** Other Mathf nodes

### Mathf.Ceil(value)

`Mathf.Ceil/1`

Mathf.Ceil(value) — Round up.

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| value | in | Number | value (Number), passed into Mathf.Ceil(value). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Ceil(value) gives back. | — |

**Example:** In a script this is Mathf.Ceil(value).

**Related:** Other Mathf nodes

### Mathf.Round(value)

`Mathf.Round/1`

Round to the nearest whole number.

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| value | in | Number | value (Number), passed into Mathf.Round(value). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Round(value) gives back. | — |

**Example:** In a script this is Mathf.Round(value).

**Related:** Other Mathf nodes

### Mathf.Sign(value)

`Mathf.Sign/1`

-1 for negative values, else 1.

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| value | in | Number | value (Number), passed into Mathf.Sign(value). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Sign(value) gives back. | — |

**Example:** In a script this is Mathf.Sign(value).

**Related:** Other Mathf nodes

### Mathf.Clamp01(value)

`Mathf.Clamp01/1`

Mathf.Clamp01(value) — Keep between 0 and 1.

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| value | in | Number | value (Number), passed into Mathf.Clamp01(value). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Clamp01(value) gives back. | — |

**Example:** In a script this is Mathf.Clamp01(value).

**Related:** Other Mathf nodes

### Mathf.Min(a, b)

`Mathf.Min/2`

Mathf.Min(a, b) — The smaller value.

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| a | in | Number | a (Number), passed into Mathf.Min(a, b). Unwired, this is 0. | 0 |
| b | in | Number | b (Number), passed into Mathf.Min(a, b). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Min(a, b) gives back. | — |

**Example:** In a script this is Mathf.Min(a, b).

**Related:** Other Mathf nodes

### Mathf.Max(a, b)

`Mathf.Max/2`

Mathf.Max(a, b) — The larger value.

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| a | in | Number | a (Number), passed into Mathf.Max(a, b). Unwired, this is 0. | 0 |
| b | in | Number | b (Number), passed into Mathf.Max(a, b). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Max(a, b) gives back. | — |

**Example:** In a script this is Mathf.Max(a, b).

**Related:** Other Mathf nodes

### Mathf.Pow(value, power)

`Mathf.Pow/2`

Mathf.Pow(value, power) — value to the power.

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| value | in | Number | value (Number), passed into Mathf.Pow(value, power). Unwired, this is 0. | 0 |
| power | in | Number | power (Number), passed into Mathf.Pow(value, power). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Pow(value, power) gives back. | — |

**Example:** In a script this is Mathf.Pow(value, power).

**Related:** Other Mathf nodes

### Mathf.Atan2(y, x)

`Mathf.Atan2/2`

Angle of (x, y) in radians.

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| y | in | Number | y (Number), passed into Mathf.Atan2(y, x). Unwired, this is 0. | 0 |
| x | in | Number | x (Number), passed into Mathf.Atan2(y, x). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Atan2(y, x) gives back. | — |

**Example:** In a script this is Mathf.Atan2(y, x).

**Related:** Other Mathf nodes

### Mathf.Clamp(value, min, max)

`Mathf.Clamp/3`

Keep between min and max.

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| value | in | Number | value (Number), passed into Mathf.Clamp(value, min, max). Unwired, this is 0. | 0 |
| min | in | Number | min (Number), passed into Mathf.Clamp(value, min, max). Unwired, this is 0. | 0 |
| max | in | Number | max (Number), passed into Mathf.Clamp(value, min, max). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Clamp(value, min, max) gives back. | — |

**Example:** In a script this is Mathf.Clamp(value, min, max).

**Related:** Other Mathf nodes

### Mathf.Lerp(a, b, t)

`Mathf.Lerp/3`

Blend from a (t = 0) to b (t = 1).

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| a | in | Number | a (Number), passed into Mathf.Lerp(a, b, t). Unwired, this is 0. | 0 |
| b | in | Number | b (Number), passed into Mathf.Lerp(a, b, t). Unwired, this is 0. | 0 |
| t | in | Number | t (Number), passed into Mathf.Lerp(a, b, t). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.Lerp(a, b, t) gives back. | — |

**Example:** In a script this is Mathf.Lerp(a, b, t).

**Related:** Other Mathf nodes

### Mathf.MoveTowards(current, target, maxDelta)

`Mathf.MoveTowards/3`

Step from current toward target by at most maxDelta.

**Requires:** No component on this object is required. Mathf is used directly. Runs in Play mode.

| Pin | Direction | Type | Meaning | Default |
| --- | --- | --- | --- | --- |
| current | in | Number | current (Number), passed into Mathf.MoveTowards(current, target, maxDelta). Unwired, this is 0. | 0 |
| target | in | Number | target (Number), passed into Mathf.MoveTowards(current, target, maxDelta). Unwired, this is 0. | 0 |
| maxDelta | in | Number | maxDelta (Number), passed into Mathf.MoveTowards(current, target, maxDelta). Unwired, this is 0. | 0 |
| Result | out | Number | Result (Number), the value Mathf.MoveTowards(current, target, maxDelta) gives back. | — |

**Example:** In a script this is Mathf.MoveTowards(current, target, maxDelta).

**Related:** Other Mathf nodes

