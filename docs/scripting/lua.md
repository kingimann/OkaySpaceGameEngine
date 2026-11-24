# Lua scripts (okay::lua)

Internal record of our own Lua interpreter and how `.lua` scripts bind to the engine. Feature map row 13.3.

## Where it came from

Everything here was written from the **Lua 5.4 Reference Manual** (the language and library specification) and
general compiler and interpreter techniques. No code from the reference implementation, LuaJIT, MoonSharp, NLua or any
other Lua implementation was used, ported or followed (CLAUDE.md rule 19a). Per file:

| File | Written from |
| --- | --- |
| `engine/include/okay/Lua/Lua.hpp` | manual 2.1 (values and types), 2.4 (metatables), 2.6 (coroutines); our own object layout |
| `engine/src/Lua/LuaCode.hpp` | our own bytecode design: a stack machine whose bottom slots are the locals |
| `engine/src/Lua/LuaCompiler.cpp` | manual 3.1 (lexical conventions), 3.2-3.5 (variables, statements, expressions, scoping), 3.4.8 (precedence), 9 (complete syntax); the "expression descriptor" technique of one-pass compilers |
| `engine/src/Lua/LuaState.cpp` | manual 2.1, 2.4, 2.5 (we do a plain stop-the-world mark and sweep; 2.5.3 finalizers; 2.5.4 weak tables), 2.6, 3.3.5 (for loops), 3.3.8 (to-be-closed variables), 3.4.1-3.4.4 and 3.4.7 (arithmetic, conversions, comparisons, length); open-addressing hash tables; the engine's string hash (`StringId::HashOf`) |
| `engine/src/Lua/LuaLibBase.cpp` | manual 6.1 (basic functions), 6.2 (coroutine), 6.7 (math), 6.9 (os, safe part only); random numbers from `okay::Random` (Core/Random.hpp) |
| `engine/src/Lua/LuaLibString.cpp` | manual 6.4, 6.4.1 (string functions and patterns) and 6.4.2 (pack format strings); our own backtracking matcher and format reader |
| `engine/src/Lua/LuaLibTable.cpp` | manual 6.6 (table functions); our own quicksort |
| `engine/src/Lua/LuaLibUtf8.cpp` | manual 6.5 (UTF-8 support) and the UTF-8 encoding rules |
| `engine/src/Lua/LuaScriptVM.cpp` | the engine's `IScriptVM` interface, the typed-language binding table (`engine/src/Script/EngineApi.cpp`) and the generated binding layer (`okay/Scripting/ScriptBindings.hpp`) |

Why bytecode and not a tree-walker: coroutines must not use operating system threads (rule 21a). With an explicit frame
stack a yield saves the frames and returns; resuming continues the same loop. The instruction counter for the run budget
also has one natural place.

## Language and library

Supported: the whole Lua 5.4 syntax (locals, `<const>`, `<close>`, multiple assignment and results, varargs,
closures, numeric and generic `for`, `goto` and labels, integer and float subtypes with `//` and the 5.4 conversion
rules, all string escapes, long strings and comments), metatables with `__index`, `__newindex`, `__call`, the
arithmetic and bitwise events, `__eq`, `__lt`, `__le`, `__len`, `__concat`, `__unm`, `__close`, `__tostring`, `__name`,
`__metatable`, `__pairs`, `__mode`, `__gc`; coroutines (`create`, `resume`, `yield`, `status`, `wrap`, `isyieldable`, `running`,
`close`); `error`, `pcall`, `xpcall` with "chunk:line:" positions and variable names ("attempt to index a nil value
(global 'foo')"); `load` for text chunks (with an environment).

Libraries (sandboxed): base (no `dofile`, `loadfile`, `require`), `string` (all functions but `dump`; `pack`,
`unpack` and `packsize` follow manual 6.4.2 with this build's sizes: int 4, long / integer / size_t 8, float 4,
double 8, native order little-endian, native maximum alignment 8), `table`, `math`, `coroutine`, `utf8`, `os.clock`, `os.time`, `os.date`, `os.difftime`. Not
present: `io`, `os.execute`, `os.exit`, `os.getenv`, `os.remove`, `package`, `debug`.

Not supported (documented differences):
- `__gc` finalizers (manual 2.5.3) work for tables: `setmetatable` with a `__gc` field marks the table; when nothing
  reaches it any more, a collection clears its weak-value entries, keeps it alive for that cycle and calls `__gc`
  once, in reverse order of marking, at the end of the collection (a safe point). A finalizer may resurrect its
  object. An error in a finalizer prints "warning: error in __gc metamethod (...)" and does not stop the script.
  `State::Close()` runs the finalizers still pending, newest first (the manual's `lua_close`); the embedder calls it
  while its natives and print sink still work. Not done: game scripts (`LuaScriptVM`) are freed without `Close`
  (the scene may be half torn down), and engine userdata have no finalizers.
- Weak tables (`__mode` "k", "v", "kv") work; a weak-key table with strong values is an ephemeron table (its value
  is kept only while the key is reachable from elsewhere, converged during marking). Strings are never removed, as
  the manual says.
- Yielding works inside `pcall` and `xpcall` of a Lua function, inside Lua metamethods for `__index` (also a method
  lookup `obj:m()`), `__newindex`, the arithmetic and bitwise events, `__unm`, `__bnot`, `__len`, `__eq`, `__lt`,
  `__le` and `__concat` (also in a chain `a .. b .. c`), and inside `__close` on a block exit, `break`, `goto` or
  `return` (these run as frames of the interpreter loop, no C++ recursion). Still "attempt to yield across a C-call
  boundary", as in the reference interpreter: a metamethod or callback a library function calls (`tostring` ->
  `__tostring`, `pairs` -> `__pairs`, `table.sort`'s comparator, `string.gsub`'s function), `__gc`, a `__close` run
  by an error, and a native (engine) metamethod.
- Binary chunks: `load` refuses them.
- String comparison is by bytes (the C locale), character classes are ASCII.

Limits: an instruction budget (`State::SetBudget`; scripts get 20 million instructions per callback) that `pcall` can
not catch, a memory limit (`State::memoryLimit`, 256 MB), 200 nested native calls, 200 000 frames, 1 000 000 stack
slots, 200 syntax levels, strings up to 1 GB. Pattern matching and big string building count against the budget.

## Scripts on objects

A script returns a table. Fields holding a number, string, boolean or Vector3 are Inspector settings; functions named
like the lifecycle run by name with the table as `self`: `Awake`, `OnEnable`, `Start`, `Update(dt)`, `LateUpdate`,
`FixedUpdate(dt)`, `OnDisable`, `OnDestroy`, and engine events in either spelling (`on_trigger_enter` or
`OnTriggerEnter(other)`; `other` is the other GameObject). A script that returns nothing uses its global functions
(old scenes: `function update(dt)`), with lowercase names allowed.

Engine access comes from two existing binding tables, never a Lua copy: the typed table `Script/EngineApi` first,
then the generated binding layer `Scripting/ScriptBindings` (the one OkayScript uses, `docs/scripting/script_bindings.md`)
for every component and member the typed table does not list:
- `self.gameObject`, `self.transform` (also globals `gameObject`, `transform`), `GetComponent("Rigidbody")`,
  `obj:GetComponent(...)`, `Destroy(obj)`.
- Instance members: `obj.prop`, `obj.prop = v`, `obj:Method(args)` (overloads chosen by argument count and types).
- Static types are global tables: `Input.GetKey("space")`, `Time.deltaTime`, `Debug.Log(x)`, `GameObject.Find("A")`,
  `Vector3.up`, `Vector3.Distance(a, b)`.
- `Vector3.new(x, y, z)` or `Vector3(x, y, z)`; `v.x`, `v.magnitude`, `v.normalized`, `+ - * /`, unary `-`, `==`,
  `tostring`, `..`.
- Any other component: `local l = GetComponent("Light")` gives a component handle (`l.intensity = 2`, `l.type = "Spot"`,
  `l:FlickerFactor()`, `l.gameObject`); a typed-table component reaches the generated methods too
  (`GetComponent("Rigidbody"):InvMass()`). Values cross as the layer's `vs::VsValue`: numbers, booleans, strings,
  Vector3, objects, handles, and colours as `{r, g, b, a}` tables. A handle is looked up again on every use, so a
  removed component raises "... is null ...".
- Engine objects are userdata holding an object id: a destroyed object raises "This object was destroyed", never
  touches freed memory. A property read returns a copy of a Vector3, so `transform.position.x = 1` changes the copy;
  assign the whole vector back.

Each script object has its own interpreter (`LuaScriptVM` owns one `okay::lua::State`); share data through engine
objects or `ScriptHost::globals`.
