# OkayVS — the visual scripting text format

A visual script is a graph of **nodes** wired by **execution** links (control
flow) and **data** links (values). You can build a graph in C++ with
`vs::NodeGraph::Add` / `ConnectExec` / `ConnectData`, or load it from this text
format with `VisualScriptComponent::LoadFromText` / `LoadFromFile`.

## Directives

```
node <id> <Type> [args...]   # declare a node with a file-local id
exec <fromId> <pin> <toId>   # control flow: pin of <from> -> <to>
data <consumerId> <inPin> <producerId> <outPin>   # value wire
entry <name> <id>            # mark an entry node ("OnStart" / "OnUpdate")
# comments start with '#'
```

## Node types

**Events** (1 exec out): `OnStart`, `OnUpdate`

**Data**: `Const <value>`, `GetVar <name>`, `Time`, `DeltaTime`, `AxisX`,
`AxisY`, `Add`, `Sub`, `Mul`, `Div`, `Compare <op>` (`>` `<` `>=` `<=` `==` `!=`)

**Actions** (1 exec out): `SetVar <name>`, `Print`, `Translate` (in0=x, in1=y),
`SetPosition` (in0=x, in1=y), `Rotate` (in0=degrees)

**Flow**: `Branch` — exec out 0 = true, exec out 1 = false; data in0 = condition

## Example: move right at 2 units/sec

```
node 0 OnUpdate
node 1 Const 2
node 2 DeltaTime
node 3 Mul
node 4 Const 0
node 5 Translate
data 3 0 1 0      # mul.in0 = 2
data 3 1 2 0      # mul.in1 = dt
data 5 0 3 0      # translate.x = 2 * dt
data 5 1 4 0      # translate.y = 0
exec 0 0 5        # OnUpdate -> Translate
entry OnUpdate 0
```
