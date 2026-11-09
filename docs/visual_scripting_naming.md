# Visual script node names

The name a person sees on a node is plain English. The short token stored in a
`.okayvs` file (`OnStart`, `Delay`) stays a single word so old files keep
loading. People search with either one.

This is the rule for every node, including nodes added after this page. The
check lives in `editor/VisualScriptNames.hpp` (`LintVsTitle`, `LintVsPin`) and
`tests/test_vs_names.cpp`.

## What people see

Title case. Words are separated by a single space. The first word starts with
a capital letter. Later small words stay lowercase: a, and, at, by, for, in,
is, of, on, or, the, to, up. Every other word is a full word with a capital
and then lowercase letters (`Seconds`, `Target`).

| Kind | Starts with | Examples |
|---|---|---|
| Event | `On` | On Start, On Every Frame, On Bump, On Key Pressed |
| Action (a step in the flow) | a verb | Play Sound, Wait Seconds, Move Towards, Set Position |
| Yes or no value | `Is` or `Has` | Is Touching, Is Key Held, Has Item |
| Reading a value that already exists | `Get` | Get Position, Get Name, Get Time |
| Working out a new value | a verb | Add, Subtract, Round Down, Make Vector |

Do not use:

- Abbreviations: Var, Vec, Vec3, Pos, Rot, Deg, Rad, Anim, Nav, Rpc, Fov, Obj, Num, Idx, Min, Max, UI, API, IK, Id, Dt
- Engine type names: Transform, Rigidbody, GameObject, Prefab, Quat, Euler
- Jargon where an everyday word exists: Collision, Trigger, Update, Timer, Enable, Disable, Fixed, Late, Custom, Sequence, Dialogue, Validate, Equip, Global, Logic
- Symbols, digits, or units in brackets: `+`, `/`, `%`, `Delay (s)`, `Every (s)`, `Switch (int)`, `Add (+)`
- Camel case: `MoveTowardsVec3`, `OverlapTest`

Put the unit in the words when it matters: `Wait Seconds`, pin `Seconds`.
Not `Wait (s)` and not a pin named `s`.

Pins use the same words: `Seconds`, `Target`, `Speed`, `Amount`, `Name`,
`Value`, `Text`, `Point`, `Object`, `Previous`, `First`, `Last`, `Count`.
Not `s`, `t`, `dt`, `pos`, `obj`, `In`, `Out`, `Bool`, `Old`, `Var`.

An empty flow pin (the triangle with no word) is fine. Do not label it `Exec`.

## Categories

Sidebar groups use the same voice. Old group on the left, name to use on the right.

| Was | Use |
|---|---|
| Events | Events |
| Flow | Order |
| Variables | Values |
| Math | Numbers |
| Logic | Choices |
| Input | Controls |
| Transform | Movement |
| Actions | Actions |
| Data | Words |
| Multiplayer & Steam | Online |
| Engine API | Objects |

Switch a group’s sidebar label when that group’s nodes are renamed, not before.
Until then the old label stays, so a half-renamed list is not a mix of both.

## Old names still work

Two different things, both required:

1. **Search keywords.** The old title (`On Collision`, `Delay (s)`, `On Var Changed`) and any old spelling people typed are keywords. They are not a second entry in the list and they are not the title on the node. Searching for the old words still finds the node. Add them on the row in `editor/VisualScriptNames.hpp` (`oldTitle` and `extra`).
2. **File aliases.** The token in the file (`OnCollision`) does not change just because the title changed, so saved graphs load with no alias. Change a token only when two nodes become one, or when the token itself was a name people should not have to read. Then add a line to `kNodeAliases` in `engine/include/okay/VisualScript/NodeAliases.hpp`. `MakeNode` resolves it before it builds the node. Do not delete the old token.

```cpp
// engine/include/okay/VisualScript/NodeAliases.hpp
{ "OverlapTest", "IsTouching" },
```

New graphs still write the token the editor already saves (`g.add` uses `d.type`).
Renaming a title does not rewrite files.

## Where to add a name

`editor/VisualScriptNames.hpp`, one `VsName` row per node:

- `type` — the file token, unchanged
- `kind` — Event, Action, Question, Reading, or Calculation (the table above)
- `title` — the words on the node
- `oldTitle` — the previous title, even when it matches `title` (the node was reviewed)
- `extra` — more search words, or `""`
- pin arrays only for pins whose words change (`Old` becomes `Previous`)

`Registry()` in `editor/VisualScriptEditor.hpp` applies this table when the
editor starts. Do not paste the friendly title into the big registry list;
that list is the file token and the fallback. The table is the name people see.

`tests/test_vs_names.cpp` fails if:

- a node in a finished group has no row
- a title or pin breaks the rules above
- the old title is missing from search
- the file token no longer builds a node

Finished groups so far: **Events**. Add the next group to `kGatedNameCategories`
in the test when its rows are in the table. A new node in a finished group
needs a row in the same change. A new node in any group should already follow
this page, even before its group is finished.

## Docs

The palette index in `docs/visual_scripting.md` uses the title people see, with
the file token in backticks. Update that group when you rename it.

`docs/visual_scripting_nodes.md` (generated on the node-docs branch) must use
these same titles the next time it is generated. Do not invent a second wording.
